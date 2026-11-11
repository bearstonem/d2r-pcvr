// draw_sampler.h - the draw-thread sampler's tables ([debug] draw_sampler, steamvr/PERFORMANCE.md).
//
// The game draws each eye on one thread, 6.5-11 ms a pass, and records only a part of the pass's draws
// there itself. The sampler (draw_sampler_capture.h) stops that thread about a thousand times a second
// while an eye pass is drawn and keeps where it was and who called it. This header is what is made of
// those samples afterwards, with no Windows or game dependency so it is tested on made-up samples
// (draw_sampler_test.cpp): which module an address is in, the start of the function it is in (from the
// module's unwind table, so every sample inside one function counts together), whether a sample was
// waiting in a system call, the tables, and the report's text. Also the choices the walk up the stack makes
// (which unwind row an address gets, whether a return address can be one), so they are tested without
// the system's unwinder.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "draw_tasks.h"

namespace framevr::drawsampler {

constexpr int MaxFrames = 32;   // the sampled instruction and up to 31 callers

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

// The row of a module's own unwind table an RVA is in, or nullptr: the table is sorted by begin, as the
// linker writes it. This is what RtlLookupFunctionEntry does for a module the system knows the table of.
inline const RuntimeFn* FindRuntimeFn(const RuntimeFn* table, size_t count, uint32_t rva) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (table[mid].begin <= rva) lo = mid + 1; else hi = mid;
    }
    return lo && rva < table[lo - 1].end ? table + (lo - 1) : nullptr;
}
// A row in the indirect form (unwind bit 0) stands for the row at that RVA; nullptr when it is outside the image.
inline const RuntimeFn* DirectRuntimeFn(const RuntimeFn* row, const uint8_t* image, size_t imageSize) {
    if (!row || !(row->unwind & 1)) return row;
    const size_t at = row->unwind & ~uint32_t(1);
    if (!image || at + sizeof(RuntimeFn) > imageSize) return nullptr;
    return reinterpret_cast<const RuntimeFn*>(image + at);
}

struct Range { uint32_t begin, end; };

// What the walk up a stack needs of a loaded module: its own unwind table, where it is in memory, and
// its executable sections.
struct WalkModule {
    uint64_t base = 0;
    uint32_t size = 0;
    const RuntimeFn* table = nullptr;   // in the loaded image
    uint32_t rows = 0;
    const uint8_t* image = nullptr;
    std::vector<Range> code;            // RVAs
};
inline bool InCode(const std::vector<Range>& code, uint32_t rva) {
    for (const Range& r : code) if (rva >= r.begin && rva < r.end) return true;
    return false;
}
inline const WalkModule* FindWalkModule(const std::vector<WalkModule>& modules, uint64_t address) {   // sorted by base
    size_t lo = 0, hi = modules.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (modules[mid].base <= address) lo = mid + 1; else hi = mid;
    }
    return lo && address - modules[lo - 1].base < modules[lo - 1].size ? &modules[lo - 1] : nullptr;
}

// The unwind row for an address. The system is asked first (`system(address, base)` is
// RtlLookupFunctionEntry: it also knows generated code that registered a table). It has none for a module
// whose table it did not see at load - the game's executable is unpacked in memory by its loader, and the
// system keeps the table of the packed file - so the module's own table, as it is in memory now, is looked
// in next. Only an address with no row in either is a leaf function, whose return address is on top of
// the stack.
enum class RowFrom : uint8_t { System, Module, None };
struct RowChoice { RowFrom from = RowFrom::None; const RuntimeFn* row = nullptr; uint64_t base = 0; };
template<class System> RowChoice ChooseRow(const WalkModule* module, uint64_t address, System&& system) {
    RowChoice c;
    if (const RuntimeFn* row = system(address, c.base)) { c.from = RowFrom::System; c.row = row; return c; }
    c.base = 0;
    if (!module || !module->table || address - module->base >= module->size) return c;
    if (const RuntimeFn* row = DirectRuntimeFn(FindRuntimeFn(module->table, module->rows, uint32_t(address - module->base)), module->image, module->size)) {
        c.from = RowFrom::Module; c.row = row; c.base = module->base;
    }
    return c;
}

// Whether the bytes that end at `end` are a call instruction, so that `end` can be a return address:
// call rel32, or an indirect call (FF /2) in any of its lengths. `have` = bytes readable before `end`.
// Bytes that are not a call can look like one, so this rejects garbage rather than proves a call.
inline bool AfterCall(const uint8_t* end, size_t have) {
    if (have >= 5 && end[-5] == 0xE8) return true;
    const auto indirect = [&](size_t length) {
        if (have < length || end[-ptrdiff_t(length)] != 0xFF) return false;
        const uint8_t modrm = end[1 - ptrdiff_t(length)], mod = modrm >> 6, rm = modrm & 7;
        if ((modrm & 0x38) != 0x10) return false;
        const bool sib = mod != 3 && rm == 4;
        size_t disp = mod == 1 ? 1 : mod == 2 ? 4 : mod == 0 && rm == 5 ? 4 : 0;
        if (sib && mod == 0 && length >= 3 && (end[2 - ptrdiff_t(length)] & 7) == 5) disp = 4;
        return length == 2 + (sib ? 1 : 0) + disp;
    };
    return indirect(2) || indirect(3) || indirect(4) || indirect(6) || indirect(7);
}

// Why a walk up the stack ended before the thread's first function or the pass's own call.
enum class Cut : uint8_t {
    None,
    Limit,     // more callers than frames are kept
    NotCode,   // the next "return address" is not in executable memory: the walk had gone wrong, and stops
    NoCall,    // it is in code, but no call instruction ends there
    Stack,     // the walk left the copy of the stack
    Fault,     // memory the walk needed could not be read
    Count
};

struct Module {
    uint64_t base = 0;
    uint32_t size = 0, timestamp = 0;   // the PE header's link time: with the size it names one build of a binary
    std::string name, path;
    Kind kind = Kind::Other;
    std::vector<Function> functions;    // by begin; filled for the modules that were sampled
    std::vector<Export> exports;        // by rva; the system modules only (system call stubs have no unwind rows)
    std::vector<Range> code;            // its executable sections, RVAs
    bool sections = false;              // `code` was read from its header (else every address counts as code)
};
// False for an address outside a module's executable sections: as a caller on a stack it is a pointer into
// the module's data, never a function.
inline bool IsCode(const Module& m, uint32_t rva) { return !m.sections || InCode(m.code, rva); }

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
    if (const Export* e = IsSystem(m.kind) ? NearestExport(m, rva, 0x40) : nullptr) {   // a system call stub: 32 bytes, no unwind row
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
    uint8_t cut;     // a Cut: why the callers end early, 0 = they do not
    uint8_t root;    // 1 = the walk reached the frame the eye pass was called from (a task's sample: the frame its run was called from)
    uint16_t task;   // a sample of a thread inside a watched render task: the task, 1-based; 0 in the draw thread's own samples
    uint8_t onDraw;  // that thread was the draw thread
};

// The render tasks whose threads are sampled ([debug] draw_sampler_tasks): parts of task names, any case,
// separated by commas or semicolons; "all" (or "*") is every task; nothing, or "none", is none.
inline constexpr char DefaultWatch[] =
    "Late Post Processing, Forward Pass, Depth Pass, Collect Draw Calls, Directional Shadows, Shadow Directional Draw Call Collect";
struct Watch {
    std::vector<std::string> parts;   // lower case, trimmed
    bool all = false;
    bool Any() const { return all || !parts.empty(); }
};
inline Watch ParseWatch(std::string_view text) {
    Watch w;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find_first_of(",;|", at);
        if (end == std::string_view::npos) end = text.size();
        std::string_view part = text.substr(at, end - at);
        while (!part.empty() && (part.front() == ' ' || part.front() == '\t' || part.front() == '"')) part.remove_prefix(1);
        while (!part.empty() && (part.back() == ' ' || part.back() == '\t' || part.back() == '"' || part.back() == '\r' || part.back() == '\n')) part.remove_suffix(1);
        const std::string low = Lower(part);
        if (low == "all" || low == "*") w.all = true;
        else if (!low.empty() && low != "none" && low != "0") w.parts.push_back(low);
        at = end + 1;
    }
    return w;
}
// Whether a task of this name is watched: the name holds one of the parts.
inline bool Watched(const Watch& w, std::string_view name) {
    if (w.all) return true;
    const std::string low = Lower(name);
    for (const std::string& part : w.parts) if (low.find(part) != std::string::npos) return true;
    return false;
}

// The game functions whose inclusive time is tabulated whatever their rank ([debug] draw_sampler_watch): the
// RVAs of their starts, in hex with or without 0x, separated by spaces, commas or semicolons. What is no
// number is left out; at most 64, each once, in the order given.
inline std::vector<uint32_t> ParseFunctions(std::string_view text) {
    std::vector<uint32_t> out;
    const auto gap = [](char c) { return c == 32 || c == 44 || c == 59 || c == 9 || c == 13 || c == 10; };   // space , ; tab and line ends
    size_t at = 0;
    while (at < text.size() && out.size() < 64) {
        while (at < text.size() && gap(text[at])) ++at;
        size_t end = at;
        while (end < text.size() && !gap(text[end])) ++end;
        std::string_view word = text.substr(at, end - at);
        at = end;
        if (word.size() > 2 && word[0] == '0' && (word[1] == 'x' || word[1] == 'X')) word.remove_prefix(2);
        if (word.empty() || word.size() > 8) continue;
        uint32_t v = 0;
        bool number = true;
        for (const char c : word) {
            const char low = char(c | 0x20);
            if (c >= '0' && c <= '9') v = (v << 4) | uint32_t(c - '0');
            else if (low >= 'a' && low <= 'f') v = (v << 4) | uint32_t(low - 'a' + 10);
            else { number = false; break; }
        }
        if (number && v && std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
    }
    return out;
}

// What the sampling of the threads inside watched tasks measured.
struct TaskSampling {
    bool on = false;                  // some task was watched
    std::string watch;                // the list as asked
    uint32_t perTick = 0;             // threads stopped at one tick at most, the draw thread not counted
    uint32_t ticks = 0;               // ticks with a thread other than the draw thread inside a watched task
    uint32_t capped = 0;              // of them, with more such threads than are stopped at a tick: they took turns
    uint32_t taken = 0;               // samples kept of threads other than the draw thread
    uint32_t fromDraw = 0;            // samples of the draw thread that were inside a watched task (the same stops as the draw thread's own)
    uint32_t moved = 0;               // stopped, but that run had ended: not kept
    uint32_t failed = 0;              // the thread could not be stopped or read
    uint32_t dropped = 0;             // samples after the buffer was full
    uint32_t noStack = 0;             // samples whose stack could not be copied (the instruction only)
    uint32_t threads = 0;             // threads that ran a render task since the first capture
    uint32_t overflow = 0;            // task runs not published: more threads than the table has slots
    uint32_t unchecked = 0;           // threads whose TEB the system did not name: their own word for it was taken
    double stoppedUsSum = 0, stoppedUsMax = 0;   // the stops of threads other than the draw thread
};
struct TaskProfile;

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
    uint32_t framesWalked = 0;        // frames unwound in all
    uint32_t framesOwnRow = 0;        // of them, with a row of the module's own table that the system did not have
};

struct Row { std::string label; uint32_t n[2] = {}; uint32_t total() const { return n[0] + n[1]; } };
struct Report {
    Stats stats;
    uint32_t samples[2] = {};         // per eye
    uint32_t cut = 0;                 // samples whose callers end early, whatever the reason
    uint32_t cuts[size_t(Cut::Count)] = {};   // by reason
    uint32_t root[2] = {};            // samples whose walk reached the frame the eye pass was called from
    uint32_t notCode = 0;             // caller frames that were no code address (left out of every table)
    drawtasks::TaskReport tasks;      // the render tasks of the passes (draw_tasks.h)
    uint32_t waiting[2] = {}, graphics[2] = {};
    std::vector<Row> kinds;           // every Kind, in Kind's order
    std::vector<Row> modules;         // every module sampled
    std::vector<Row> waits;           // waiting samples by system call
    std::vector<Row> waiters;         // waiting samples by who waits
    std::vector<Row> leaves;          // by function of the sampled instruction
    std::vector<Row> inclusive;       // game functions anywhere on the stack
    std::vector<Row> functions;       // of them, the watched ones (draw_sampler_watch), in the list's order; one never seen has no samples
    std::vector<Row> callers;         // graphics samples by the nearest game function
    std::vector<Row> systemCalls;     // samples in ntdll / win32u by function
    std::vector<Row> hooks;           // samples in this mod or ReShade, by function
    std::vector<Row> foreign;         // samples in graphics code or this mod: whose code < the nearest game function
    TaskSampling sampling;            // the threads inside watched render tasks...
    std::vector<TaskProfile> watched; // ... and a report of each such task, most samples first
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
    // The share of the samples whose walk reached the pass's own call: near 1 in a sound capture.
    double RootShare(int eye = 2) const {
        const uint32_t n = eye < 2 ? root[eye] : root[0] + root[1], of = eye < 2 ? samples[eye] : total();
        return of ? double(n) / of : 0.0;
    }
};
// One watched render task: the samples of the threads that ran it, as a report of its own in which a
// "pass" is one run of the task (stats.passes = its runs, stats.passMs = their time), so a row's ms is
// its share of a run.
struct TaskProfile {
    std::string name;
    uint32_t draw = 0, worker = 0;    // samples from the draw thread, from other threads
    Report report;
    uint32_t Runs() const { return report.stats.passes[0] + report.stats.passes[1]; }
    double RunMs() const { return report.PassMs(2); }
    // Samples per second of the task's run time: what the sampling achieved for it.
    double Rate() const { const double ms = report.stats.passMs[0] + report.stats.passMs[1]; return ms > 0 ? 1000.0 * report.total() / ms : 0.0; }
};
// Whether the two eyes are to be shown apart: a run's time, or what a tenth or more of it goes to (whose
// code; waiting), differs between them by more than 10%. One eye alone sampled counts as differing.
inline bool EyesDiffer(const Report& r) {
    if (!r.total()) return false;
    if (!r.samples[0] || !r.samples[1]) return true;
    const auto apart = [](double a, double b) { const double most = std::max(a, b); return most > 0 && std::fabs(a - b) > 0.1 * most; };
    if (apart(r.PassMs(0), r.PassMs(1))) return true;
    const auto row = [&](const Row& k) { return r.Share(k) >= 0.1 && k.total() >= 100 && apart(r.Ms(k, 0), r.Ms(k, 1)); };
    for (const Row& k : r.kinds) if (row(k)) return true;
    return row(Row{"", {r.waiting[0], r.waiting[1]}});
}

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
inline Report Aggregate(const std::vector<Module>& modules, const Sample* samples, size_t count, const Stats& stats,
                        const std::vector<uint32_t>& functions = {}) {
    using detail::Text;
    Report r;
    r.stats = stats;
    struct Known { detail::Where where; std::string label; bool wait = false, notCode = false; const char* symbol = nullptr; };
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
        if (!IsCode(m, uint32_t(address - m.base))) {
            // Outside the module's executable sections. As a caller it is a data address picked off the stack: never a
            // function, in no table. As the sampled instruction it did run (code a loader placed there): by its page.
            k.notCode = true;
            k.where = {mi, m.kind, (1ull << 62) | (uint64_t(mi + 1) << 32) | (uint32_t(address - m.base) >> 12)};
            k.label = Text("%s+0x%X (not code by the module's sections: the page)", m.name.c_str(), uint32_t(address - m.base) & ~0xFFFu);
            return k;
        }
        const Place p = Locate(m, uint32_t(address - m.base));
        k.where = {mi, m.kind, (uint64_t(mi + 1) << 32) | p.start};
        k.label = Text("%s+0x%X%s%s%s", m.name.c_str(), p.start, p.exact ? "" : "~", p.symbol ? " " : "", p.symbol ? p.symbol : "");
        k.symbol = p.symbol;
        k.wait = IsSystem(m.kind) && p.symbol && IsWaitName(p.symbol);
        return k;
    };
    detail::Table kinds, mods, waits, waiters, leaves, inclusive, callers, systemCalls, hooks, foreign;
    for (int k = 0; k < int(Kind::Count); ++k) { kinds.at[uint64_t(k)] = kinds.rows.size(); kinds.rows.push_back({KindName(Kind(k)), {}}); }
    for (size_t i = 0; i < count; ++i) {
        const Sample& s = samples[i];
        if (!s.count) continue;
        const int eye = s.eye & 1, n = std::min<int>(s.count, MaxFrames);
        ++r.samples[eye];
        if (s.cut) { ++r.cut; ++r.cuts[s.cut < uint8_t(Cut::Count) ? s.cut : uint8_t(Cut::Fault)]; }
        if (s.root) ++r.root[eye];
        const Known& leaf = look(s.frames[0]);   // references stay good while `known` grows
        ++kinds.rows[size_t(leaf.where.kind)].n[eye];
        mods.Add(uint64_t(leaf.where.module + 1), eye, [&] {
            return leaf.where.module < 0 ? std::string("(no module)") : modules[size_t(leaf.where.module)].name;
        });
        leaves.Add(leaf.where.key, eye, [&] { return leaf.label; });
        if (leaf.where.kind == Kind::Ntdll || leaf.where.kind == Kind::Win32) systemCalls.Add(leaf.where.key, eye, [&] { return leaf.label; });
        if (leaf.where.kind == Kind::Mod || leaf.where.kind == Kind::ReShade) hooks.Add(leaf.where.key, eye, [&] { return leaf.label; });

        // every game function on the stack once; the game function nearest the sampled instruction; the
        // first function outside the system's wrappers
        uint64_t seen[MaxFrames]; int seenCount = 0;
        const Known* nearestGame = nullptr;
        const Known* waiter = nullptr;
        for (int f = 0; f < n; ++f) {
            const Known& k = look(s.frames[f]);
            if (k.notCode) { if (f) ++r.notCode; continue; }   // no function: it names no caller
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
        if (IsGraphics(leaf.where.kind) || leaf.where.kind == Kind::Mod)
            foreign.Add((uint64_t(leaf.where.kind) + 1) * 0x9E3779B97F4A7C15ull ^ (nearestGame ? nearestGame->where.key : 0), eye, [&] {
                return std::string(KindName(leaf.where.kind)) + " < " + (nearestGame ? nearestGame->label : std::string("(no game function within the frames kept)"));
            });
        if (IsGraphics(leaf.where.kind)) {
            ++r.graphics[eye];
            callers.Add(nearestGame ? nearestGame->where.key : 0, eye, [&] {
                return nearestGame ? nearestGame->label : std::string("(no game function within the frames kept)");
            });
        }
    }
    // The watched game functions: their rows of the inclusive table, by the RVA of the function's start.
    for (const uint32_t rva : functions) {
        Row row;
        for (size_t m = 0; m < modules.size() && row.label.empty(); ++m) {
            if (modules[m].kind != Kind::Game) continue;
            if (const auto it = inclusive.at.find((uint64_t(m + 1) << 32) | rva); it != inclusive.at.end()) row = inclusive.rows[it->second];
        }
        if (row.label.empty()) row.label = Text("+0x%X (on no sampled stack: not run in an eye pass, inlined, or not a function's start)", rva);
        r.functions.push_back(std::move(row));
    }
    r.kinds = std::move(kinds.rows);   // in Kind's order
    r.modules = mods.Sorted(); r.waits = waits.Sorted(); r.waiters = waiters.Sorted(); r.leaves = leaves.Sorted();
    r.inclusive = inclusive.Sorted(); r.callers = callers.Sorted(); r.systemCalls = systemCalls.Sorted();
    r.hooks = hooks.Sorted(); r.foreign = foreign.Sorted();
    return r;
}

// A report per watched task from the samples of the threads that ran them (Sample::task, 1-based, into
// `names`). A task's runs and their time come from the render task records by its name, so that a row's ms
// is its share of one run. Most samples first.
inline std::vector<TaskProfile> Profiles(const std::vector<Module>& modules, const Sample* samples, size_t count, const std::vector<std::string>& names,
                                         const drawtasks::TaskReport& tasks) {
    std::vector<TaskProfile> out;
    std::vector<std::vector<Sample>> by(names.size());
    for (size_t i = 0; i < count; ++i)
        if (samples[i].count && samples[i].task && samples[i].task <= names.size()) by[samples[i].task - 1].push_back(samples[i]);
    for (size_t t = 0; t < names.size(); ++t) {
        if (by[t].empty()) continue;
        TaskProfile p;
        p.name = names[t];
        Stats stats;
        for (const drawtasks::TaskRow& row : tasks.rows) {
            if (row.name != p.name) continue;
            for (int e = 0; e < 2; ++e) { stats.passes[e] = row.runs[e]; stats.passMs[e] = row.ms[e]; }
        }
        for (const Sample& s : by[t]) ++(s.onDraw ? p.draw : p.worker);
        p.report = Aggregate(modules, by[t].data(), by[t].size(), stats);
        out.push_back(std::move(p));
    }
    std::stable_sort(out.begin(), out.end(), [](const TaskProfile& a, const TaskProfile& b) { return a.report.total() > b.report.total(); });
    return out;
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
    fit(head + Text("the walks up the stack: %.1f%% of the samples reached the eye pass's own call (left %.1f%%, right %.1f%%) - near 100%% or the caller "
                    "tables are not to be trusted; %u of %u frames by a module's own table", 100.0 * r.RootShare(), 100.0 * r.RootShare(0), 100.0 * r.RootShare(1),
                    s.framesOwnRow, s.framesWalked));
    if (r.cut)
        fit(head + Text("walks cut short: %u frame limit, %u not code, %u not after a call, %u stack copy, %u unreadable", r.cuts[size_t(Cut::Limit)],
                        r.cuts[size_t(Cut::NotCode)], r.cuts[size_t(Cut::NoCall)], r.cuts[size_t(Cut::Stack)], r.cuts[size_t(Cut::Fault)]));
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
    {
        const drawtasks::LogParts parts = drawtasks::LogText(r.tasks);
        for (const std::string& line : parts.lines) fit(head + line);
        if (!parts.lastItems.empty()) pack(parts.lastTitle, parts.lastItems);
        if (!parts.longItems.empty()) pack(parts.longTitle, parts.longItems);
    }
    if (r.sampling.on) {
        const TaskSampling& t = r.sampling;
        fit(head + Text("task threads: %u samples of workers in the watched tasks, %u of the draw thread's own (%u threads ran tasks); "
                        "a stop %.0f us (longest %.0f); %u ticks had more such threads than the %u stopped a tick; %u moved on, %u not read, %u dropped",
                        t.taken, t.fromDraw, t.threads, t.taken ? t.stoppedUsSum / t.taken : 0.0, t.stoppedUsMax, t.capped, t.perTick, t.moved,
                        t.failed, t.dropped));
        if (r.watched.empty()) fit(head + "task threads: no thread was sampled inside a watched task (the names are parts of those in table 7): " + t.watch);
        size_t shown = 0;
        for (const TaskProfile& p : r.watched) {
            if (shown++ == 8) break;
            const Report& q = p.report;
            const auto items = [&](const std::vector<Row>& table, size_t most) {
                std::vector<std::string> list;
                for (size_t i = 0; i < table.size() && list.size() < most; ++i)
                    if (table[i].total()) list.push_back(Text("%s %.1f%% (%.2f ms)", table[i].label.c_str(), 100.0 * q.Share(table[i]), q.Ms(table[i])));
                return list;
            };
            const std::string task = "task " + p.name + ": ";
            std::vector<Row> byShare = q.kinds;
            std::stable_sort(byShare.begin(), byShare.end(), [](const Row& a, const Row& b) { return a.total() > b.total(); });
            fit(head + task + Text("%u samples, %.0f a second of its run time (%.0f%% of them from the draw thread); a run %.2f ms%s; %.0f%% of the walks reached its call",
                                   q.total(), p.Rate(), 100.0 * p.draw / q.total(), p.RunMs(),
                                   EyesDiffer(q) ? Text(" (left %.2f, right %.2f)", q.PassMs(0), q.PassMs(1)).c_str() : "", 100.0 * q.RootShare()));
            pack(task + "whose code, % of a run (ms):", items(byShare, 6));
            const Row wait{"", {q.waiting[0], q.waiting[1]}};
            std::vector<std::string> waits = items(q.waits, 2);
            for (std::string& who : items(q.waiters, 2)) waits.push_back("by " + who);
            pack(task + Text("waiting %.1f%% (%.2f ms a run), running %.1f%% - waits in:", 100.0 * q.Share(wait), q.Ms(wait), 100.0 - 100.0 * q.Share(wait)),
                 waits.empty() ? std::vector<std::string>{"none"} : waits);
            pack(task + "top functions:", items(q.leaves, 5));
            if (!q.hooks.empty()) pack(task + "in this mod and ReShade:", items(q.hooks, 4));
        }
    }
    if (filePath && *filePath) fit(head + "every table, and the modules to match the RVAs to a binary: " + filePath);
    return out;
}

// Table 8 of the file: the threads inside the watched render tasks.
inline std::string TaskThreadsText(const Report& r) {
    using detail::Text;
    const TaskSampling& t = r.sampling;
    std::string out = "\n8. Inside the render tasks: the threads that ran the watched ones, sampled\n";
    if (!t.on) {
        out += "  not asked for: [debug] draw_sampler_tasks names the tasks to watch (parts of the names in table 7, or all)\n";
        return out;
    }
    out += "  At each tick the sampler also stops the threads that are inside a watched task just then - whichever threads those are - the same\n"
           "  way as the draw thread: registers and a copy of the stack while stopped, the callers worked out afterwards. A task's samples are\n"
           "  where its threads were during its runs. A row's share is its samples over the task's samples; its ms is that share of the task's\n"
           "  mean run (table 7). The walk up a stack ends at this mod's hook on the game's task runner, which called the task: the share that\n"
           "  reached it should be near 100%, or only self time stands. A run inside another on the same thread (table 7: inside) counts for the\n"
           "  inner task alone, while the outer task's run time still holds it: that task's ms are then too high by the inner run's share.\n"
           "  Both eyes are shown together unless a run's time, or a tenth of it, differs between them by more than 10%.\n";
    out += "  watched: " + t.watch + "\n";
    out += Text("  %u samples of threads other than the draw thread (%u threads have run render tasks), %u of the draw thread inside a watched task\n"
                "  (those are the draw thread's own stops, counted again here). At most %u threads are stopped at a tick beside the draw thread:\n"
                "  %u of %u ticks with a thread in a watched task had more, and they took turns - each task's achieved rate is in its head line.\n",
                t.taken, t.threads, t.fromDraw, t.perTick, t.capped, t.ticks);
    out += Text("  cost: a thread stopped %.1f us a sample (longest %.0f us). lost: %u stopped after that run had ended (not kept), %u could not be\n"
                "  stopped or read, %u dropped (buffer full), %u task runs of threads beyond the table's slots; %u samples without callers\n",
                t.taken ? t.stoppedUsSum / t.taken : 0.0, t.stoppedUsMax, t.moved, t.failed, t.dropped, t.overflow, t.noStack);
    if (t.unchecked) out += Text("  %u threads' stack bounds were read where the thread itself said, the system not naming the place\n", t.unchecked);
    if (r.watched.empty()) { out += "\n  No thread was sampled inside a watched task: check the names against table 7.\n"; return out; }
    for (const TaskProfile& p : r.watched) {
        const Report& q = p.report;
        const bool eyes = EyesDiffer(q);
        out += "\n  == " + p.name + " ==\n";
        out += Text("  %u samples (%u on the draw thread, %u on other threads), %.0f a second of its run time; %u runs of %.3f ms (left %.3f, right %.3f)%s\n",
                    q.total(), p.draw, p.worker, p.Rate(), p.Runs(), p.RunMs(), q.PassMs(0), q.PassMs(1),
                    p.Runs() ? "" : " - its runs are not in table 7, so the ms columns are empty");
        out += Text("  the walks: %.1f%% reached the task's own call; cut short: %u frame limit, %u not code, %u not after a call, %u stack copy, %u unreadable%s\n",
                    100.0 * q.RootShare(), q.cuts[size_t(Cut::Limit)], q.cuts[size_t(Cut::NotCode)], q.cuts[size_t(Cut::NoCall)], q.cuts[size_t(Cut::Stack)],
                    q.cuts[size_t(Cut::Fault)], eyes ? "; the eyes differ: shown apart" : "");
        const auto table = [&](const char* title, const std::vector<Row>& rows, size_t most, const char* none) {
            out += "  "; out += title; out += "\n";
            if (rows.empty()) { out += "    "; out += none; out += "\n"; return; }
            out += eyes ? "        %   ms/run  left ms  right ms  samples  \n" : "        %   ms/run  samples  \n";
            const auto line = [&](const Row& row) {
                if (eyes) out += Text("    %5.1f  %7.3f  %7.3f  %8.3f  %7u  %s\n", 100.0 * q.Share(row), q.Ms(row), q.Ms(row, 0), q.Ms(row, 1), row.total(), row.label.c_str());
                else out += Text("    %5.1f  %7.3f  %7u  %s\n", 100.0 * q.Share(row), q.Ms(row), row.total(), row.label.c_str());
            };
            size_t shown = 0; uint32_t rest[2] = {};
            for (const Row& row : rows) {
                if (!row.total()) continue;
                if (shown >= most) { rest[0] += row.n[0]; rest[1] += row.n[1]; continue; }
                ++shown;
                line(row);
            }
            if (rest[0] + rest[1]) line(Row{"(the rest)", {rest[0], rest[1]}});
        };
        const size_t all = size_t(-1);
        table("a. by whose code the sampled instruction is", q.kinds, all, "none");
        {
            const Row wait{"waiting (stopped in a wait system call)", {q.waiting[0], q.waiting[1]}};
            const Row run{"running", {q.samples[0] - q.waiting[0], q.samples[1] - q.waiting[1]}};
            table("b. waiting or running", {wait, run}, all, "none");
        }
        table("   waits, by system call", q.waits, 10, "no sample was waiting");
        table("   who waits: the first caller outside the system's DLLs ( < the nearest game function behind it)", q.waiters, 10, "no sample was waiting");
        table("c. top 15 functions by the sampled instruction (self time)", q.leaves, 15, "none");
        table("d. top 15 game functions on the stack (inclusive)", q.inclusive, 15, "no game function on any sampled stack");
        table("e. samples in the GPU driver, D3D12, DXGI, DLSS, ReShade or this mod: whose code < the nearest game function, top 15", q.foreign, 15,
              "no sample was in that code");
        table("f. samples in this mod and ReShade, by function (the RVA is in the DLL's link map; a name is its export there)", q.hooks, 15,
              "no sample was in this mod or ReShade");
    }
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
    out += Text("lost: %u could not be stopped or read, %u dropped (buffer full); %u samples without callers (stack not readable)\n", s.failed, s.dropped,
                s.noStack);
    out += Text("the walks up the stack: %.1f%% of the samples reached the eye pass's own call in this mod (left %.1f%%, right %.1f%%). A sound capture\n"
                "  is near 100%%; much less means the callers below are wrong or missing, and only self time (tables 1-3) stands.\n",
                100.0 * r.RootShare(), 100.0 * r.RootShare(0), 100.0 * r.RootShare(1));
    out += Text("  cut short: %u at the %d-frame limit, %u at an address that is not code, %u at an address no call ends at, %u where the walk left the\n"
                "  stack copy, %u unreadable. %u of %u frames were unwound with a module's own unwind table because the system had none for it\n"
                "  (the game's executable). %u caller addresses were no code and are in no table.\n",
                r.cuts[size_t(Cut::Limit)], MaxFrames, r.cuts[size_t(Cut::NotCode)], r.cuts[size_t(Cut::NoCall)], r.cuts[size_t(Cut::Stack)],
                r.cuts[size_t(Cut::Fault)], s.framesOwnRow, s.framesWalked, r.notCode);
    out += "\nHow to read it: each sample is where the draw thread was at one instant of an eye pass. A row's share is its samples over all\n"
           "samples; its ms is that share of the measured pass time (the stops themselves included). Addresses are module+0xRVA of the START\n"
           "of the function sampled, from the module's unwind table, so they can be looked up in a disassembler; \"~\" marks code with no\n"
           "unwind row (a leaf function at or after that RVA). A name after an address is the module's export at exactly that address.\n"
           "An address outside a module's executable sections is never a function: it shows as \"(not code)\" and is in no function table.\n";
    if (!r.total()) { out += "\nNo samples: no eye pass was drawn during the capture.\n"; out += drawtasks::FileText(r.tasks); out += TaskThreadsText(r); return out; }

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
    if (!r.functions.empty()) {
        out += "\n4b. Watched game functions on the stack (inclusive, [debug] draw_sampler_watch; in the list's order)\n";
        out += "      %  ms/pass  left ms  right ms  samples  \n";
        for (const Row& row : r.functions)
            out += Text("  %5.1f  %7.3f  %7.3f  %8.3f  %7u  %s\n", 100.0 * r.Share(row), r.Ms(row), r.Ms(row, 0), r.Ms(row, 1), row.total(), row.label.c_str());
    }
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

    out += drawtasks::FileText(r.tasks);
    out += TaskThreadsText(r);
    out += "\nModules (base, size, PE link timestamp: the three name one build of a binary; * = sampled)\n";
    for (size_t i = 0; i < modules.size(); ++i) {
        const Module& m = modules[i];
        bool sampled = false;
        for (const Row& row : r.modules) if (row.label == m.name) { sampled = true; break; }
        for (const TaskProfile& p : r.watched)
            for (const Row& row : p.report.modules) if (row.label == m.name) { sampled = true; break; }
        out += Text("  %c 0x%012llX  size 0x%08X  time 0x%08X  %-22s %s\n", sampled ? '*' : ' ', (unsigned long long)m.base, m.size, m.timestamp,
                    KindName(m.kind), m.path.empty() ? m.name.c_str() : m.path.c_str());
    }
    return out;
}

}  // namespace framevr::drawsampler
