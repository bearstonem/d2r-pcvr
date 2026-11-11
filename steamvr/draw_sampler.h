// draw_sampler.h - the draw-thread sampler's tables ([debug] draw_sampler, steamvr/PERFORMANCE.md).
//
// The game draws each eye on one thread, 6.5-11 ms a pass, and records only a part of the pass's draws
// there itself. The sampler (draw_sampler_capture.h) stops that thread about a thousand times a second
// while an eye pass is drawn and keeps where it was and who called it. This header is what is made of
// those samples afterwards, with no Windows or game dependency so it is tested on made-up samples
// (draw_sampler_test.cpp): which module an address is in, the start of the function it is in (from the
// module's unwind table, so every sample inside one function counts together), whether a sample was
// waiting in a system call, the tables, and the report's text.
#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace framevr::drawsampler {

constexpr int MaxFrames = 24;   // the sampled instruction and up to 23 callers

// Whose code a module is. The order is the report's.
enum class Kind : uint8_t { Game, Mod, ReShade, Driver, Ngx, D3D12, Dxgi, Ntdll, Kernel, Win32, OpenXR, Other, None, Count };
inline const char* KindName(Kind k) {
    switch (k) {
    case Kind::Game: return "game";
    case Kind::Mod: return "this mod";
    case Kind::ReShade: return "ReShade";
    case Kind::Driver: return "GPU driver";
    case Kind::Ngx: return "DLSS (NGX)";
    case Kind::D3D12: return "D3D12";
    case Kind::Dxgi: return "DXGI";
    case Kind::Ntdll: return "ntdll";
    case Kind::Kernel: return "kernel32/KERNELBASE";
    case Kind::Win32: return "win32u/user32/gdi32";
    case Kind::OpenXR: return "OpenXR runtime and layers";
    case Kind::Other: return "other modules";
    default: return "no module";
    }
}
// Graphics work done for the game: the driver, D3D12, DXGI, DLSS and ReShade.
inline bool IsGraphics(Kind k) { return k == Kind::ReShade || k == Kind::Driver || k == Kind::Ngx || k == Kind::D3D12 || k == Kind::Dxgi; }
// The system's own wrappers around a wait: the one waiting is the first caller outside them.
inline bool IsSystem(Kind k) { return k == Kind::Ntdll || k == Kind::Kernel || k == Kind::Win32; }

inline std::string Lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return out;
}
// A module's kind from its file name and path (any case). The game is the process's own executable and
// the mod the DLL this code is in: the caller knows both.
inline Kind Classify(std::string_view name, std::string_view path, bool game, bool mod) {
    if (game) return Kind::Game;
    if (mod) return Kind::Mod;
    const std::string n = Lower(name), p = Lower(path);
    const auto starts = [&](const char* s) { return n.rfind(s, 0) == 0; };
    const auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
    if (starts("reshade")) return Kind::ReShade;
    if (starts("nvngx") || starts("_nvngx")) return Kind::Ngx;
    if (n == "d3d12.dll" || n == "d3d12core.dll" || n == "d3d12sdklayers.dll") return Kind::D3D12;
    if (n == "dxgi.dll" || n == "dxcore.dll") return Kind::Dxgi;
    if (n == "ntdll.dll") return Kind::Ntdll;
    if (n == "kernel32.dll" || n == "kernelbase.dll") return Kind::Kernel;
    if (n == "win32u.dll" || n == "user32.dll" || n == "gdi32.dll" || n == "gdi32full.dll") return Kind::Win32;
    if (has("openxr") || has("ofxr") || has("vrclient") || has("virtualdesktop") || has("vdxr") || has("libovr") || has("steamxr") ||
        starts("xr_") || p.find("\\steamvr\\") != std::string::npos)
        return Kind::OpenXR;
    // The user-mode halves of the GPU drivers: NVIDIA (nvwgf2umx, nvoglv64, nvldumdx, nvgpucomp64...), AMD (amdxc64,
    // atidxx64, amdxx64, atiuxp64...) and Intel (igd10iumd64, igc64, igdgmm64...). Windows keeps them in the driver store.
    if (p.find("\\driverstore\\") != std::string::npos || starts("nvwgf2um") || starts("nvoglv") || starts("nvldumd") || starts("nvgpucomp") ||
        starts("nvd3dum") || starts("amdxc") || starts("amdxx") || starts("atidxx") || starts("atiuxp") || starts("aticfx") || starts("amdvlk") ||
        starts("igd10") || starts("igd11") || starts("igd12") || starts("igc64") || starts("igdgmm") || starts("igvk"))
        return Kind::Driver;
    return Kind::Other;
}

// One row of a module's unwind table (the layout of Windows' RUNTIME_FUNCTION), RVAs.
struct RuntimeFn { uint32_t begin, end, unwind; };
// A stretch of code and the start of the function it belongs to: a function the compiler cut in pieces
// has one row a piece, the later ones chained to the first.
struct Function { uint32_t begin, end, start; };
struct Export { uint32_t rva; std::string name; };

// The functions of a module from its unwind table. `image` is the module as loaded (RVAs index it); a
// chained row (unwind flag 4) names its parent row after its unwind codes, followed up to 8 deep. Every
// read is bounded by `imageSize`; a row whose unwind data cannot be read starts at its own begin.
inline std::vector<Function> BuildFunctions(const RuntimeFn* table, size_t count, const uint8_t* image, size_t imageSize) {
    std::vector<Function> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RuntimeFn row = table[i];
        if (row.end <= row.begin) continue;
        Function f{row.begin, row.end, row.begin};
        for (int depth = 0; depth < 8 && image; ++depth) {
            if (row.unwind & 1) break;                            // the indirect form: no unwind data of its own here
            if (size_t(row.unwind) + 4 > imageSize) break;
            const uint8_t flags = uint8_t(image[row.unwind] >> 3), codes = image[row.unwind + 2];
            if (!(flags & 4)) break;
            const size_t at = size_t(row.unwind) + 4 + size_t((codes + 1) & ~1) * 2;
            if (at + sizeof(RuntimeFn) > imageSize) break;
            RuntimeFn parent;
            std::copy_n(image + at, sizeof parent, reinterpret_cast<uint8_t*>(&parent));
            if (parent.end <= parent.begin) break;
            f.start = parent.begin;
            row = parent;
        }
        out.push_back(f);
    }
    std::sort(out.begin(), out.end(), [](const Function& a, const Function& b) { return a.begin < b.begin; });
    return out;
}

struct Module {
    uint64_t base = 0;
    uint32_t size = 0, timestamp = 0;   // the PE header's link time: with the size it names one build of a binary
    std::string name, path;
    Kind kind = Kind::Other;
    std::vector<Function> functions;    // by begin; filled for the modules that were sampled
    std::vector<Export> exports;        // by rva; the system modules only (system call stubs have no unwind rows)
};

// The module an address is in, or -1. `modules` is sorted by base (SortModules).
inline void SortModules(std::vector<Module>& modules) {
    std::sort(modules.begin(), modules.end(), [](const Module& a, const Module& b) { return a.base < b.base; });
}
inline int FindModule(const std::vector<Module>& modules, uint64_t address) {
    size_t lo = 0, hi = modules.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (modules[mid].base <= address) lo = mid + 1; else hi = mid;
    }
    if (!lo) return -1;
    const Module& m = modules[lo - 1];
    return address - m.base < m.size ? int(lo - 1) : -1;
}

// The named export at or closest below rva, no farther than `reach` bytes; nullptr if none.
inline const Export* NearestExport(const Module& m, uint32_t rva, uint32_t reach) {
    auto it = std::upper_bound(m.exports.begin(), m.exports.end(), rva, [](uint32_t v, const Export& e) { return v < e.rva; });
    if (it == m.exports.begin()) return nullptr;
    --it;
    return rva - it->rva <= reach ? &*it : nullptr;
}

// Where in a module an address is. `start` is the RVA of the function's first instruction when the
// unwind table or an export names it (exact); otherwise the end of the function before it - code with no
// unwind row is a leaf function somewhere from there on.
struct Place { uint32_t start = 0; bool exact = false; const char* symbol = nullptr; };
inline Place Locate(const Module& m, uint32_t rva) {
    Place p;
    auto it = std::upper_bound(m.functions.begin(), m.functions.end(), rva, [](uint32_t v, const Function& f) { return v < f.begin; });
    if (it != m.functions.begin() && rva < (it - 1)->end) {
        p.start = (it - 1)->start; p.exact = true;
        if (const Export* e = NearestExport(m, p.start, 0)) p.symbol = e->name.c_str();
        return p;
    }
    if (const Export* e = NearestExport(m, rva, 0x40)) {   // a system call stub: 32 bytes, no unwind row
        p.start = e->rva; p.exact = true; p.symbol = e->name.c_str();
        return p;
    }
    p.start = it != m.functions.begin() ? (it - 1)->end : rva;
    return p;
}

// A thread stopped in one of these is waiting, not working: for an object, an alert (SRW locks,
// critical sections and condition variables park in NtWaitForAlertByThreadId), a keyed event, a sleep,
// a message, the GPU. NtYieldExecution is a spin lock giving its turn away.
inline bool IsWaitName(std::string_view name) {
    if (name.size() > 2 && name[0] == 'Z' && name[1] == 'w') { std::string nt = "Nt"; nt += name.substr(2); return IsWaitName(nt); }
    static const char* const prefixes[] = {"NtWaitFor", "NtDelayExecution", "NtSignalAndWaitForSingleObject", "NtRemoveIoCompletion",
        "NtYieldExecution", "NtUserMsgWaitForMultipleObjects", "NtUserWaitMessage", "NtUserGetMessage", "NtGdiDdDDIWaitFor",
        "NtDCompositionWaitFor", "NtUserWaitForInputIdle"};
    for (const char* p : prefixes) if (name.rfind(p, 0) == 0) return true;
    return false;
}

// One sample: the thread's instruction (frames[0]) and its callers outward, and the eye being drawn.
struct Sample {
    uint64_t frames[MaxFrames];
    uint8_t count;   // frames filled, at least 1
    uint8_t eye;     // 0 left, 1 right
    uint8_t cut;     // 1 = the callers end where the stack copy did, not at the thread's first function
    uint8_t unused;
};

// What the capture measured beside the samples.
struct Stats {
    std::string when;                 // local time the capture ended
    uint32_t thread = 0;
    double seconds = 0;               // first tick to last
    double periodUs = 0;              // the timer's mean period as asked
    uint32_t ticks = 0;               // timer ticks
    uint32_t outside = 0;             // ticks with no eye pass being drawn
    uint32_t failed = 0;              // the thread could not be stopped or read
    uint32_t dropped = 0;             // samples after the buffer was full
    uint32_t noStack = 0;             // samples whose stack could not be copied (the instruction only)
    uint32_t passes[2] = {};          // eye passes finished while armed
    double passMs[2] = {};            // their time on the clock, summed
    double stoppedUsSum = 0, stoppedUsMax = 0;   // each sample: SuspendThread to ResumeThread
};

struct Row { std::string label; uint32_t n[2] = {}; uint32_t total() const { return n[0] + n[1]; } };
struct Report {
    Stats stats;
    uint32_t samples[2] = {};         // per eye
    uint32_t cut = 0;
    uint32_t waiting[2] = {}, graphics[2] = {};
    std::vector<Row> kinds;           // every Kind, in Kind's order
    std::vector<Row> modules;         // every module sampled
    std::vector<Row> waits;           // waiting samples by system call
    std::vector<Row> waiters;         // waiting samples by who waits
    std::vector<Row> leaves;          // by function of the sampled instruction
    std::vector<Row> inclusive;       // game functions anywhere on the stack
    std::vector<Row> callers;         // graphics samples by the nearest game function
    std::vector<Row> systemCalls;     // samples in ntdll / win32u by function
    uint32_t total() const { return samples[0] + samples[1]; }
    // A pass's mean time, ms: eye 0, 1, or 2 for both.
    double PassMs(int eye) const {
        const double ms = eye < 2 ? stats.passMs[eye] : stats.passMs[0] + stats.passMs[1];
        const uint32_t n = eye < 2 ? stats.passes[eye] : stats.passes[0] + stats.passes[1];
        return n ? ms / n : 0.0;
    }
    // A row's share of the pass and what that is in ms: its samples are spread evenly over the passes' time.
    double Share(const Row& r, int eye = 2) const {
        const uint32_t n = eye < 2 ? r.n[eye] : r.total(), of = eye < 2 ? samples[eye] : total();
        return of ? double(n) / of : 0.0;
    }
    double Ms(const Row& r, int eye = 2) const { return Share(r, eye) * PassMs(eye); }
};

namespace detail {
inline std::string Text(const char* fmt, ...) {
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    return b;
}
struct Where { int module = -1; Kind kind = Kind::None; uint64_t key = 0; };
// Rows keyed by number, kept in first-seen order until sorted.
struct Table {
    std::unordered_map<uint64_t, size_t> at;
    std::vector<Row> rows;
    template<class Label> void Add(uint64_t key, int eye, Label&& label) {
        auto [it, fresh] = at.try_emplace(key, rows.size());
        if (fresh) { rows.emplace_back(); rows.back().label = label(); }
        ++rows[it->second].n[eye & 1];
    }
    std::vector<Row> Sorted() {
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.total() > b.total(); });
        return std::move(rows);
    }
};
}  // namespace detail

// The tables from the samples. `modules` is sorted by base with the sampled modules' functions (and the
// system modules' exports) filled.
inline Report Aggregate(const std::vector<Module>& modules, const Sample* samples, size_t count, const Stats& stats) {
    using detail::Text;
    Report r;
    r.stats = stats;
    struct Known { detail::Where where; std::string label; bool wait = false; const char* symbol = nullptr; };
    std::unordered_map<uint64_t, Known> known;   // per address: most samples repeat a few thousand
    const auto look = [&](uint64_t address) -> const Known& {
        auto [it, fresh] = known.try_emplace(address);
        Known& k = it->second;
        if (!fresh) return k;
        const int mi = FindModule(modules, address);
        if (mi < 0) {   // generated code (a hook's trampoline): by its page
            k.where.key = (1ull << 63) | (address >> 12);
            k.label = Text("0x%llX (no module, the page)", (unsigned long long)(address & ~0xFFFull));
            return k;
        }
        const Module& m = modules[size_t(mi)];
        const Place p = Locate(m, uint32_t(address - m.base));
        k.where = {mi, m.kind, (uint64_t(mi + 1) << 32) | p.start};
        k.label = Text("%s+0x%X%s%s%s", m.name.c_str(), p.start, p.exact ? "" : "~", p.symbol ? " " : "", p.symbol ? p.symbol : "");
        k.symbol = p.symbol;
        k.wait = IsSystem(m.kind) && p.symbol && IsWaitName(p.symbol);
        return k;
    };
    detail::Table kinds, mods, waits, waiters, leaves, inclusive, callers, systemCalls;
    for (int k = 0; k < int(Kind::Count); ++k) { kinds.at[uint64_t(k)] = kinds.rows.size(); kinds.rows.push_back({KindName(Kind(k)), {}}); }
    for (size_t i = 0; i < count; ++i) {
        const Sample& s = samples[i];
        if (!s.count) continue;
        const int eye = s.eye & 1, n = std::min<int>(s.count, MaxFrames);
        ++r.samples[eye];
        if (s.cut) ++r.cut;
        const Known& leaf = look(s.frames[0]);   // references stay good while `known` grows
        ++kinds.rows[size_t(leaf.where.kind)].n[eye];
        mods.Add(uint64_t(leaf.where.module + 1), eye, [&] {
            return leaf.where.module < 0 ? std::string("(no module)") : modules[size_t(leaf.where.module)].name;
        });
        leaves.Add(leaf.where.key, eye, [&] { return leaf.label; });
        if (leaf.where.kind == Kind::Ntdll || leaf.where.kind == Kind::Win32) systemCalls.Add(leaf.where.key, eye, [&] { return leaf.label; });

        // every game function on the stack once; the game function nearest the sampled instruction; the
        // first function outside the system's wrappers
        uint64_t seen[MaxFrames]; int seenCount = 0;
        const Known* nearestGame = nullptr;
        const Known* waiter = nullptr;
        for (int f = 0; f < n; ++f) {
            const Known& k = look(s.frames[f]);
            if (!waiter && !IsSystem(k.where.kind)) waiter = &k;
            if (k.where.kind != Kind::Game) continue;
            if (!nearestGame) nearestGame = &k;
            if (std::find(seen, seen + seenCount, k.where.key) != seen + seenCount) continue;
            seen[seenCount++] = k.where.key;
            inclusive.Add(k.where.key, eye, [&] { return k.label; });
        }
        if (leaf.wait) {
            ++r.waiting[eye];
            waits.Add(leaf.where.key, eye, [&] { return std::string(leaf.symbol); });
            const bool behind = waiter && nearestGame && nearestGame->where.key != waiter->where.key;
            const uint64_t a = waiter ? waiter->where.key : 0, b = behind ? nearestGame->where.key : 0;
            waiters.Add(a * 0x9E3779B97F4A7C15ull ^ b, eye, [&] {
                if (!waiter) return std::string("(no caller outside the system within the frames kept)");
                return behind ? waiter->label + " < " + nearestGame->label : waiter->label;
            });
        }
        if (IsGraphics(leaf.where.kind)) {
            ++r.graphics[eye];
            callers.Add(nearestGame ? nearestGame->where.key : 0, eye, [&] {
                return nearestGame ? nearestGame->label : std::string("(no game function within the frames kept)");
            });
        }
    }
    r.kinds = std::move(kinds.rows);   // in Kind's order
    r.modules = mods.Sorted(); r.waits = waits.Sorted(); r.waiters = waiters.Sorted(); r.leaves = leaves.Sorted();
    r.inclusive = inclusive.Sorted(); r.callers = callers.Sorted(); r.systemCalls = systemCalls.Sorted();
    return r;
}

// The report's lines for the frame log: each starts "Frame draw sampler: " and fits the log's 255 characters.
constexpr size_t LogLineMax = 250;
inline std::vector<std::string> LogLines(const Report& r, const char* filePath) {
    using detail::Text;
    const std::string head = "Frame draw sampler: ";
    std::vector<std::string> out;
    const auto fit = [&](std::string line) { if (line.size() > LogLineMax) line.resize(LogLineMax); out.push_back(std::move(line)); };
    // `items` after `title`, as many to a line as fit, continued on further lines
    const auto pack = [&](const std::string& title, const std::vector<std::string>& items) {
        std::string line = head + title;
        bool any = false;
        for (const std::string& item : items) {
            if (any && line.size() + 3 + item.size() > LogLineMax) { out.push_back(line); line = head + title; any = false; }
            line += any ? " | " : " ";
            line += item;
            any = true;
        }
        if (any) fit(line);
    };
    const auto rows = [&](const std::vector<Row>& table, size_t most) {
        std::vector<std::string> items;
        for (size_t i = 0; i < table.size() && i < most; ++i)
            if (table[i].total()) items.push_back(Text("%s %.1f%% (%.2f ms)", table[i].label.c_str(), 100.0 * r.Share(table[i]), r.Ms(table[i])));
        return items;
    };
    const Stats& s = r.stats;
    if (!r.total()) {
        fit(head + Text("no samples in %.1f s (%u timer ticks, %u with no eye pass being drawn, %u passes seen) - "
                        "the sampler only looks while an eye is drawn: start it in the world, in the headset", s.seconds, s.ticks, s.outside, s.passes[0] + s.passes[1]));
        return out;
    }
    fit(head + Text("%u samples of thread %u in %.1f s, %u eye passes (left %u, right %u); a pass %.2f ms (left %.2f, right %.2f); "
                    "one sample stands for %.2f us of a pass", r.total(), s.thread, s.seconds, s.passes[0] + s.passes[1], s.passes[0], s.passes[1],
                    r.PassMs(2), r.PassMs(0), r.PassMs(1), 1000.0 * r.PassMs(2) / r.total()));
    const double stoppedMs = s.stoppedUsSum * 0.001, passMs = s.passMs[0] + s.passMs[1];
    fit(head + Text("its own cost: the thread stopped %.0f us a sample (longest %.0f), %.1f%% of the passes' time; %u ticks, %u outside a pass, "
                    "%u not read, %u dropped, %u without callers, %u with callers cut short",
                    r.total() ? s.stoppedUsSum / r.total() : 0.0, s.stoppedUsMax, passMs > 0 ? 100.0 * stoppedMs / passMs : 0.0, s.ticks, s.outside,
                    s.failed, s.dropped, s.noStack, r.cut));
    {
        std::vector<Row> byShare = r.kinds;
        std::stable_sort(byShare.begin(), byShare.end(), [](const Row& a, const Row& b) { return a.total() > b.total(); });
        pack("where the thread was, % of a pass (ms):", rows(byShare, byShare.size()));
    }
    {
        Row wait{"", {r.waiting[0], r.waiting[1]}}, gfx{"", {r.graphics[0], r.graphics[1]}};
        std::vector<std::string> items = rows(r.waits, 4);
        pack(Text("waiting %.1f%% (%.2f ms a pass; left %.2f, right %.2f), running %.1f%%; graphics calls %.1f%% (%.2f ms) - waits in:",
                  100.0 * r.Share(wait), r.Ms(wait), r.Ms(wait, 0), r.Ms(wait, 1), 100.0 - 100.0 * r.Share(wait), 100.0 * r.Share(gfx), r.Ms(gfx)),
             items.empty() ? std::vector<std::string>{"none"} : items);
    }
    if (!r.waiters.empty()) pack("who waits:", rows(r.waiters, 4));
    pack("top functions (module+RVA of the function's start):", rows(r.leaves, 10));
    if (!r.inclusive.empty()) pack("game functions on the stack (inclusive):", rows(r.inclusive, 8));
    if (!r.callers.empty()) pack("game functions making the graphics calls:", rows(r.callers, 6));
    {
        std::string line = head + Text("left eye %u samples, %.2f ms a pass; right eye %u samples, %.2f ms a pass", r.samples[0], r.PassMs(0), r.samples[1], r.PassMs(1));
        for (const Row& k : r.kinds) {
            if (!k.total() || 100.0 * r.Share(k) < 3.0) continue;
            line += Text(" | %s %.2f / %.2f ms", k.label.c_str(), r.Ms(k, 0), r.Ms(k, 1));
        }
        fit(line);
    }
    if (filePath && *filePath) fit(head + "every table, and the modules to match the RVAs to a binary: " + filePath);
    return out;
}

// The whole report, for the text file beside the log.
inline std::string FileText(const Report& r, const std::vector<Module>& modules) {
    using detail::Text;
    const Stats& s = r.stats;
    std::string out;
    out += "D2R PCVR draw-thread sampler ([debug] draw_sampler; steamvr/PERFORMANCE.md)\n";
    out += Text("captured %s: %.1f s, thread %u\n", s.when.c_str(), s.seconds, s.thread);
    out += Text("samples %u (left eye %u, right eye %u) in %u eye passes (left %u, right %u)\n", r.total(), r.samples[0], r.samples[1],
                s.passes[0] + s.passes[1], s.passes[0], s.passes[1]);
    out += Text("a pass on the clock: %.3f ms (left %.3f, right %.3f); one sample stands for %.2f us of a pass\n", r.PassMs(2), r.PassMs(0), r.PassMs(1),
                r.total() ? 1000.0 * r.PassMs(2) / r.total() : 0.0);
    const double passMs = s.passMs[0] + s.passMs[1];
    out += Text("timer: %u ticks, asked every %.0f us (jittered), every %.0f us in fact; %u ticks outside an eye pass\n", s.ticks, s.periodUs,
                s.ticks ? s.seconds * 1e6 / s.ticks : 0.0, s.outside);
    out += Text("the sampler's own cost: the thread stopped %.1f us a sample (longest %.0f us), %.3f ms a pass, %.2f%% of the passes' time\n",
                r.total() ? s.stoppedUsSum / r.total() : 0.0, s.stoppedUsMax, s.passes[0] + s.passes[1] ? s.stoppedUsSum * 0.001 / (s.passes[0] + s.passes[1]) : 0.0,
                passMs > 0 ? 100.0 * s.stoppedUsSum * 0.001 / passMs : 0.0);
    out += Text("lost: %u could not be stopped or read, %u dropped (buffer full); %u samples without callers (stack not readable), "
                "%u with callers cut short by the %d-frame / stack-copy limit\n", s.failed, s.dropped, s.noStack, r.cut, MaxFrames);
    out += "\nHow to read it: each sample is where the draw thread was at one instant of an eye pass. A row's share is its samples over all\n"
           "samples; its ms is that share of the measured pass time (the stops themselves included). Addresses are module+0xRVA of the START\n"
           "of the function sampled, from the module's unwind table, so they can be looked up in a disassembler; \"~\" marks code with no\n"
           "unwind row (a leaf function at or after that RVA). A name after an address is the module's export at exactly that address.\n";
    if (!r.total()) { out += "\nNo samples: no eye pass was drawn during the capture.\n"; return out; }

    const auto table = [&](const char* title, const std::vector<Row>& rows, size_t most, const char* none) {
        out += "\n"; out += title; out += "\n";
        if (rows.empty()) { out += "  "; out += none; out += "\n"; return; }
        out += "      %  ms/pass  left ms  right ms  samples  \n";
        size_t shown = 0; uint32_t rest[2] = {};
        for (const Row& row : rows) {
            if (!row.total()) continue;
            if (shown >= most) { rest[0] += row.n[0]; rest[1] += row.n[1]; continue; }
            ++shown;
            out += Text("  %5.1f  %7.3f  %7.3f  %8.3f  %7u  %s\n", 100.0 * r.Share(row), r.Ms(row), r.Ms(row, 0), r.Ms(row, 1), row.total(), row.label.c_str());
        }
        if (rest[0] + rest[1]) {
            const Row other{"(the rest)", {rest[0], rest[1]}};
            out += Text("  %5.1f  %7.3f  %7.3f  %8.3f  %7u  %s\n", 100.0 * r.Share(other), r.Ms(other), r.Ms(other, 0), r.Ms(other, 1), other.total(), other.label.c_str());
        }
    };
    const size_t all = size_t(-1);
    table("1. Where the thread was, by whose code the sampled instruction is", r.kinds, all, "none");
    table("1b. The same by module", r.modules, all, "none");
    {
        const Row wait{"waiting (stopped in a wait system call)", {r.waiting[0], r.waiting[1]}};
        const Row run{"running (everything else, system calls that are not waits included)", {r.samples[0] - r.waiting[0], r.samples[1] - r.waiting[1]}};
        table("2. Waiting or running", {wait, run}, all, "none");
    }
    table("2b. Waits, by system call", r.waits, all, "no sample was waiting");
    table("2c. Who waits: the first caller outside ntdll / kernel32 / KERNELBASE / win32u ( < the nearest game function behind it)", r.waiters, 25,
          "no sample was waiting");
    table("2d. Samples inside ntdll and win32u, by function (locks spinning, system calls that are not waits)", r.systemCalls, 20, "none");
    table("3. Top 25 functions by the sampled instruction (self time)", r.leaves, 25, "none");
    table("4. Top 15 game functions on the stack (inclusive: a sample counts once for every game function it is inside)", r.inclusive, 15,
          "no game function on any sampled stack");
    {
        const Row gfx{"", {r.graphics[0], r.graphics[1]}};
        const std::string title = Text("5. Graphics calls (the sampled instruction in the GPU driver, D3D12, DXGI, DLSS or ReShade: %.1f%%, %.3f ms a pass) "
                                       "by the nearest game function, top 10", 100.0 * r.Share(gfx), r.Ms(gfx));
        table(title.c_str(), r.callers, 10, "no sample was in graphics code");
    }
    out += "\n6. The two eyes\n";
    out += Text("  left:  %u passes, %.3f ms a pass, %u samples, waiting %.3f ms, graphics calls %.3f ms\n", s.passes[0], r.PassMs(0), r.samples[0],
                r.samples[0] ? double(r.waiting[0]) / r.samples[0] * r.PassMs(0) : 0.0, r.samples[0] ? double(r.graphics[0]) / r.samples[0] * r.PassMs(0) : 0.0);
    out += Text("  right: %u passes, %.3f ms a pass, %u samples, waiting %.3f ms, graphics calls %.3f ms\n", s.passes[1], r.PassMs(1), r.samples[1],
                r.samples[1] ? double(r.waiting[1]) / r.samples[1] * r.PassMs(1) : 0.0, r.samples[1] ? double(r.graphics[1]) / r.samples[1] * r.PassMs(1) : 0.0);

    out += "\nModules (base, size, PE link timestamp: the three name one build of a binary; * = sampled)\n";
    for (size_t i = 0; i < modules.size(); ++i) {
        const Module& m = modules[i];
        bool sampled = false;
        for (const Row& row : r.modules) if (row.label == m.name) { sampled = true; break; }
        out += Text("  %c 0x%012llX  size 0x%08X  time 0x%08X  %-22s %s\n", sampled ? '*' : ' ', (unsigned long long)m.base, m.size, m.timestamp,
                    KindName(m.kind), m.path.empty() ? m.name.c_str() : m.path.c_str());
    }
    return out;
}

}  // namespace framevr::drawsampler
