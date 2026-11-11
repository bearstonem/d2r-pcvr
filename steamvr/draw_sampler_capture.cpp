// draw_sampler_capture.cpp - see draw_sampler_capture.h.
//
// THE RULE: between SuspendThread and ResumeThread (Take) the stopped thread may hold any lock in the
// process - the heap's, the loader's, the log's, the lock on the system's unwind tables that
// RtlLookupFunctionEntry takes. So Take makes three system calls on the thread's handle and reads plain
// memory into buffers allocated before the capture began: the register context, the pass flags the thread
// published itself, the stack bounds in its TEB and a copy of the top of its stack. No allocation, no
// log, no C runtime stream, no loader call, no unwinding, and no guarded (__try) read whose fault would
// itself look up unwind tables. The callers are worked out AFTER the thread runs again (Unwind), on the
// copy of its stack: the registers that point into the stack are moved to point into the copy, and
// RtlVirtualUnwind then walks the copy while the real stack changes under the running thread.

#include "draw_sampler_capture.h"

#include <tlhelp32.h>
#include <share.h>

#include <cstdio>
#include <cstring>

namespace framevr::drawsampler {

Live g_live;

void EnterPassArmed(int eye) noexcept {
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    g_live.entered = q.QuadPart;
    g_live.teb.store(NtCurrentTeb());
    g_live.thread.store(GetCurrentThreadId());
    g_live.state.store(1u + uint32_t(eye & 1));   // last: the sampler trusts thread and teb once it reads this
}
void LeavePassArmed() noexcept {
    const uint32_t s = g_live.state.exchange(0);
    if (!s) return;
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    g_live.passes[s - 1].fetch_add(1);
    g_live.passTicks[s - 1].fetch_add(q.QuadPart - g_live.entered);
}

namespace {

constexpr size_t StackCopy = 64 * 1024;   // of the stopped thread's stack, from its stack pointer up
constexpr size_t Page = 4096;

// One stopped thread's state, in memory committed and touched before the capture.
struct Raw {
    CONTEXT ctx;
    uintptr_t stackAt;   // the stack pointer the copy starts at
    size_t copied;       // bytes copied, 0 = none
    bool whole;          // the copy reaches the stack's base
    bool outside;        // stopped, but no longer drawing an eye pass
    uint8_t eye;
    uint8_t* stack;      // StackCopy + 16 bytes, a no-access page after them
};

// Stops the thread, reads it, lets it run. See THE RULE above: every line between the two calls counts.
__attribute__((noinline)) bool Take(HANDLE thread, uint32_t tid, Raw& raw) noexcept {
    raw.copied = 0; raw.whole = false; raw.outside = false;
    if (SuspendThread(thread) == (DWORD)-1) return false;
    bool ok = false;
    raw.ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (GetThreadContext(thread, &raw.ctx)) {   // returns once the thread has really stopped
        const uint32_t state = g_live.state.load();
        const NT_TIB* tib = static_cast<const NT_TIB*>(g_live.teb.load());
        if (state && tib && g_live.thread.load() == tid) {   // it is the thread in a pass, so the TEB is its own
            ok = true;
            raw.eye = uint8_t(state - 1);
            // Its stack from the stack pointer up: committed memory by the TEB's own bounds, so a plain read.
            const uintptr_t sp = raw.ctx.Rsp, low = (uintptr_t)tib->StackLimit, high = (uintptr_t)tib->StackBase;
            if (sp >= low && sp < high && !(sp & 7)) {
                const size_t n = high - sp < StackCopy ? size_t(high - sp) : StackCopy;
                memcpy(raw.stack + (sp & 15), (const void*)sp, n);
                raw.stackAt = sp; raw.copied = n; raw.whole = sp + n == high;
            }
        } else {
            raw.outside = true;
        }
    }
    ResumeThread(thread);
    return ok;
}

void Relocate(CONTEXT& c, uintptr_t low, uintptr_t high, intptr_t delta) noexcept {
    DWORD64* const regs[] = {&c.Rsp, &c.Rbp, &c.Rbx, &c.Rsi, &c.Rdi, &c.R12, &c.R13, &c.R14, &c.R15};
    for (DWORD64* r : regs) if (*r >= low && *r < high) *r += (DWORD64)delta;
}

// The callers, from the copy of the stack, with the thread running again. Returns the frames filled;
// `cut` = the walk ended at the copy's end or the frame limit rather than at the thread's first function.
int Unwind(const Raw& raw, uint64_t* out, bool& cut) noexcept {
    int n = 0;
    cut = false;
    out[n++] = raw.ctx.Rip;
    if (!raw.copied) return n;
    __try {
        CONTEXT c = raw.ctx;
        const uintptr_t low = raw.stackAt, high = low + raw.copied;
        const uintptr_t copy = (uintptr_t)raw.stack + (low & 15), copyEnd = copy + raw.copied;
        const intptr_t delta = (intptr_t)(copy - low);   // a multiple of 16: alignment is kept
        Relocate(c, low, high, delta);
        for (;;) {
            if (n == MaxFrames) { cut = true; break; }
            const DWORD64 before = c.Rsp;
            if (before + 8 > copyEnd) { cut = !raw.whole; break; }
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
            if (fe) {
                PVOID handlerData = nullptr; DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fe, &c, &handlerData, &establisher, nullptr);
            } else {   // no unwind row: a leaf (a system call stub, a hook's trampoline) - the return address is on top
                c.Rip = *(const DWORD64*)c.Rsp;
                c.Rsp += 8;
            }
            Relocate(c, low, high, delta);   // a frame pointer popped off the copy still points into the real stack
            if (c.Rsp <= before || c.Rsp > copyEnd) { cut = !raw.whole; break; }
            if (c.Rip < 0x10000 || c.Rip >= 0x0000800000000000ull) break;   // the thread's first function returns to nothing
            out[n++] = c.Rip;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { cut = true; }   // the walk read past the copy (the page after it has no access)
    return n;
}

std::string Utf8(const wchar_t* text) {
    char b[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, text, -1, b, (int)sizeof b - 1, nullptr, nullptr);
    return b;
}

// The image's PE header fields and tables, bounded by `size` (the module as loaded).
struct Image {
    const uint8_t* at; size_t size;
    template<class T> const T* Ptr(size_t rva, size_t count = 1) const {
        return rva <= size && count <= (size - rva) / sizeof(T) ? reinterpret_cast<const T*>(at + rva) : nullptr;
    }
    const IMAGE_NT_HEADERS64* Nt() const {
        const auto* dos = Ptr<IMAGE_DOS_HEADER>(0);
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return nullptr;
        const auto* nt = Ptr<IMAGE_NT_HEADERS64>(size_t(dos->e_lfanew));
        return nt && nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? nt : nullptr;
    }
    const IMAGE_DATA_DIRECTORY* Directory(unsigned index) const {
        const auto* nt = Nt();
        return nt && index < nt->OptionalHeader.NumberOfRvaAndSizes && nt->OptionalHeader.DataDirectory[index].VirtualAddress ?
            &nt->OptionalHeader.DataDirectory[index] : nullptr;
    }
};

__attribute__((noinline)) void FillUnguarded(Module& m, std::vector<Function>& functions, std::vector<Export>& exports) {
    const Image image{reinterpret_cast<const uint8_t*>(m.base), m.size};
    if (const auto* dir = image.Directory(IMAGE_DIRECTORY_ENTRY_EXCEPTION)) {
        const size_t count = dir->Size / sizeof(RuntimeFn);
        if (const RuntimeFn* table = image.Ptr<RuntimeFn>(dir->VirtualAddress, count)) functions = BuildFunctions(table, count, image.at, image.size);
    }
    if (!IsSystem(m.kind)) return;
    const auto* dir = image.Directory(IMAGE_DIRECTORY_ENTRY_EXPORT);
    const auto* ex = dir ? image.Ptr<IMAGE_EXPORT_DIRECTORY>(dir->VirtualAddress) : nullptr;
    if (!ex) return;
    const auto* names = image.Ptr<uint32_t>(ex->AddressOfNames, ex->NumberOfNames);
    const auto* ordinals = image.Ptr<uint16_t>(ex->AddressOfNameOrdinals, ex->NumberOfNames);
    const auto* addresses = image.Ptr<uint32_t>(ex->AddressOfFunctions, ex->NumberOfFunctions);
    if (!names || !ordinals || !addresses) return;
    exports.reserve(ex->NumberOfNames);
    for (uint32_t i = 0; i < ex->NumberOfNames; ++i) {
        if (ordinals[i] >= ex->NumberOfFunctions) continue;
        const uint32_t rva = addresses[ordinals[i]];
        if (!rva || (rva >= dir->VirtualAddress && rva - dir->VirtualAddress < dir->Size)) continue;   // forwarded to another module
        const char* name = image.Ptr<char>(names[i], 2);
        if (!name) continue;
        const size_t room = image.size - names[i], len = strnlen(name, room < 128 ? room : 128);
        exports.push_back({rva, std::string(name, len)});
    }
    // One name an address: Nt before its Zw twin, otherwise the first by name.
    const auto rank = [](const Export& e) { return e.name.rfind("Nt", 0) == 0 ? 0 : 1; };
    std::sort(exports.begin(), exports.end(), [&](const Export& a, const Export& b) {
        return a.rva != b.rva ? a.rva < b.rva : rank(a) != rank(b) ? rank(a) < rank(b) : a.name < b.name;
    });
    exports.erase(std::unique(exports.begin(), exports.end(), [](const Export& a, const Export& b) { return a.rva == b.rva; }), exports.end());
}

// The module can be unloaded while it is read: a fault leaves it without tables.
bool FillGuarded(Module* m, std::vector<Function>* functions, std::vector<Export>* exports) noexcept {
    __try { FillUnguarded(*m, *functions, *exports); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool TimestampGuarded(uint64_t base, uint32_t size, uint32_t* out) noexcept {
    __try {
        const Image image{reinterpret_cast<const uint8_t*>(base), size};
        const auto* nt = image.Nt();
        if (!nt) return false;
        *out = nt->FileHeader.TimeDateStamp;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::atomic<bool> g_running{false}, g_stop{false};
Options g_options;

DWORD WINAPI Run(void*) {
    std::vector<Module> modules;
    const Report report = Capture(g_options, &modules);
    bool wrote = false;
    if (!g_options.reportPath.empty()) {
        if (FILE* f = _wfsopen(g_options.reportPath.c_str(), L"wb", _SH_DENYWR)) {
            const std::string text = FileText(report, modules);
            wrote = fwrite(text.data(), 1, text.size(), f) == text.size();
            fclose(f);
        }
    }
    if (g_options.log) {
        const std::string path = wrote ? Utf8(g_options.reportPath.c_str()) : std::string();
        for (const std::string& line : LogLines(report, path.c_str())) g_options.log(line.c_str());
        if (!wrote && !g_options.reportPath.empty()) g_options.log("Frame draw sampler: the report file could not be written; the lines above are all there is");
    }
    g_running.store(false);
    return 0;
}

}  // namespace

bool FillModule(Module& module) {
    std::vector<Function> functions;
    std::vector<Export> exports;
    if (!FillGuarded(&module, &functions, &exports)) return false;
    module.functions = std::move(functions);
    module.exports = std::move(exports);
    return true;
}

bool ReadModules(std::vector<Module>& out, HMODULE mod) {
    out.clear();
    HANDLE snap = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 8 && snap == INVALID_HANDLE_VALUE; ++attempt) {   // ERROR_BAD_LENGTH while the list changes: again
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
        if (snap == INVALID_HANDLE_VALUE && GetLastError() != ERROR_BAD_LENGTH) break;
    }
    if (snap == INVALID_HANDLE_VALUE) return false;
    const HMODULE game = GetModuleHandleW(nullptr);
    MODULEENTRY32W me{}; me.dwSize = sizeof me;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        Module m;
        m.base = (uint64_t)(uintptr_t)me.modBaseAddr;
        m.size = me.modBaseSize;
        m.name = Utf8(me.szModule);
        m.path = Utf8(me.szExePath);
        m.kind = Classify(m.name, m.path, me.hModule == game, mod && me.hModule == mod);
        TimestampGuarded(m.base, m.size, &m.timestamp);
        out.push_back(std::move(m));
    }
    CloseHandle(snap);
    SortModules(out);
    return !out.empty();
}

Report Capture(const Options& options, std::vector<Module>* modulesOut) {
    Stats stats;
    const double seconds = options.seconds < 0.05 ? 0.05 : options.seconds > 300 ? 300 : options.seconds;
    const unsigned periodUs = options.periodUs < 200 ? 200 : options.periodUs > 50000 ? 50000 : options.periodUs;
    stats.periodUs = periodUs;

    // Everything the sampling needs, before any thread is stopped.
    const size_t capacity = size_t(seconds * 1e6 / periodUs * 1.4) + 4096;
    Sample* samples = static_cast<Sample*>(VirtualAlloc(nullptr, capacity * sizeof(Sample), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Raw* raw = static_cast<Raw*>(VirtualAlloc(nullptr, sizeof(Raw), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    const size_t stackBytes = (StackCopy + 16 + Page - 1) / Page * Page;
    uint8_t* stack = static_cast<uint8_t*>(VirtualAlloc(nullptr, stackBytes + Page, MEM_RESERVE, PAGE_NOACCESS));   // the last page stays without access
    if (stack && !VirtualAlloc(stack, stackBytes, MEM_COMMIT, PAGE_READWRITE)) { VirtualFree(stack, 0, MEM_RELEASE); stack = nullptr; }
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    const double usPerTick = 1e6 / double(frequency.QuadPart);
    const auto now = [] { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; };
    size_t count = 0;
    if (samples && raw && stack) {
        memset(raw, 0, sizeof *raw);
        memset(stack, 0, stackBytes);   // touched: no page of it is first met while a thread is stopped
        raw->stack = stack + (stackBytes - StackCopy - 16);   // the copy ends 0-16 bytes before the no-access page
        for (int e = 0; e < 2; ++e) { g_live.passes[e].store(0); g_live.passTicks[e].store(0); }
        g_live.state.store(0);
        g_stop.store(false);
        const int priority = GetThreadPriority(GetCurrentThread());
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);   // on time, and never stopped itself holding the thread
        g_live.armed.store(true);

        HANDLE thread = nullptr; uint32_t opened = 0;
        uint32_t random = 0x9E3779B9u ^ GetCurrentThreadId() ^ uint32_t(now());
        const int64_t armedAt = now();
        int64_t first = 0, last = 0, deadline = 0, due = armedAt;   // due: when the next tick should be, so the timer's lateness does not add up
        for (;;) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            const unsigned wait = unsigned(double(periodUs) * (0.75 + 0.5 * double(random) / 4294967296.0));
            due += int64_t(double(wait) / usPerTick);
            const double aheadUs = double(due - now()) * usPerTick;
            if (aheadUs < -5.0 * periodUs) due = now();   // far behind (this thread was kept waiting): no burst to catch up
            if (aheadUs >= 50.0) {
                if (timer) {
                    LARGE_INTEGER in; in.QuadPart = -int64_t(aheadUs * 10.0);
                    if (!SetWaitableTimer(timer, &in, 0, nullptr, nullptr, FALSE) || WaitForSingleObject(timer, 100) == WAIT_FAILED) Sleep(1);
                } else {
                    Sleep(aheadUs >= 1000.0 ? DWORD(aheadUs / 1000.0) : 1);
                }
            }
            const int64_t t = now();
            if (g_stop.load()) break;
            if (!first) {   // the capture's time starts with the first eye pass seen
                if (g_live.state.load()) { first = t; deadline = t + int64_t(seconds * 1e6 / usPerTick); }
                else if (double(t - armedAt) * usPerTick > double(options.waitSeconds) * 1e6) break;
                else continue;
            }
            if (t >= deadline) {   // no new pass is marked; one being drawn is sampled to its end
                g_live.armed.store(false);
                if (!g_live.state.load() || double(t - deadline) * usPerTick > 200000.0) break;
            }
            last = t;
            ++stats.ticks;
            const uint32_t tid = g_live.thread.load();
            if (!g_live.state.load() || !tid) { ++stats.outside; continue; }
            if (tid == GetCurrentThreadId()) { ++stats.failed; continue; }   // never itself: nobody would let it run again
            if (tid != opened) {
                if (thread) CloseHandle(thread);
                thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
                opened = thread ? tid : 0;
            }
            if (!thread) { ++stats.failed; continue; }
            const int64_t before = now();
            const bool ok = Take(thread, tid, *raw);
            const double stoppedUs = double(now() - before) * usPerTick;
            if (!ok) { ++(raw->outside ? stats.outside : stats.failed); continue; }
            stats.stoppedUsSum += stoppedUs;
            if (stoppedUs > stats.stoppedUsMax) stats.stoppedUsMax = stoppedUs;
            if (count == capacity) { ++stats.dropped; continue; }
            Sample& s = samples[count++];
            bool cut = false;
            s.count = uint8_t(Unwind(*raw, s.frames, cut));
            s.eye = raw->eye; s.cut = cut ? 1 : 0; s.unused = 0;
            if (!raw->copied) ++stats.noStack;
        }
        g_live.armed.store(false);
        for (int i = 0; i < 200 && g_live.state.load(); ++i) Sleep(1);   // a pass being drawn is counted whole
        SetThreadPriority(GetCurrentThread(), priority);
        if (thread) CloseHandle(thread);
        stats.thread = g_live.thread.load();
        stats.seconds = first ? double(last - first) * usPerTick * 1e-6 : double(now() - armedAt) * usPerTick * 1e-6;
        for (int e = 0; e < 2; ++e) {
            stats.passes[e] = g_live.passes[e].load();
            stats.passMs[e] = double(g_live.passTicks[e].load()) * usPerTick * 0.001;
        }
    }
    if (timer) CloseHandle(timer);
    {
        SYSTEMTIME t{}; GetLocalTime(&t);
        char when[40];
        snprintf(when, sizeof when, "%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        stats.when = when;
    }

    // The tables, with nothing stopped: the modules as they are now, the sampled ones' functions.
    std::vector<Module> modules;
    ReadModules(modules, options.mod);
    std::vector<uint8_t> sampled(modules.size(), 0);
    for (size_t i = 0; i < count; ++i)
        for (int f = 0; f < samples[i].count; ++f)
            if (const int m = FindModule(modules, samples[i].frames[f]); m >= 0) sampled[size_t(m)] = 1;
    for (size_t m = 0; m < modules.size(); ++m) if (sampled[m]) FillModule(modules[m]);
    Report report = Aggregate(modules, samples, count, stats);
    if (samples) VirtualFree(samples, 0, MEM_RELEASE);
    if (raw) VirtualFree(raw, 0, MEM_RELEASE);
    if (stack) VirtualFree(stack, 0, MEM_RELEASE);
    if (modulesOut) *modulesOut = std::move(modules);
    return report;
}

bool Start(const Options& options) {
    if (g_running.exchange(true)) return false;
    g_options = options;
    HANDLE t = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
    if (!t) { g_running.store(false); return false; }
    CloseHandle(t);
    return true;
}
void Stop() { if (g_running.load()) g_stop.store(true); }
bool Running() { return g_running.load(); }

}  // namespace framevr::drawsampler
