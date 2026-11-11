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
//
// THE WALK. RtlVirtualUnwind needs the unwind row of the function an address is in. The system finds it
// (RtlLookupFunctionEntry) in the table it captured when the module was loaded - and the game's
// executable is loaded packed, with a three-row table for its unpacking stub; its real table appears in
// memory later, with the header pointing at it, and the system never looks again. So for every address
// in the game the system answers "no row", which the first sampler took for a leaf function and read a
// raw stack slot as its return address: the caller chains through game code were garbage. Now an address
// the system has no row for gets the row of its module's own table, as it is in memory; only without
// one is it a leaf. And each return address must be in executable memory, right after a call
// instruction, or the walk stops there and says so, rather than going on with whatever the stack held.

#include "draw_sampler_capture.h"
#include "memory.h"

#include <tlhelp32.h>
#include <share.h>

#include <cstdio>
#include <cstring>

namespace framevr::drawsampler {

Live g_live;

namespace tasks {
std::atomic<bool> g_armed{false};
namespace {
// One timed call, written once by the thread that made it; `done` last.
struct Record {
    int64_t start, end;
    uint64_t name;
    uint32_t pass, thread;
    uint8_t what, eye, index, onDraw;
    std::atomic<uint8_t> done;
};
std::atomic<Record*> g_records{nullptr};
std::atomic<uint32_t> g_capacity{0}, g_count{0}, g_generation{0};
std::atomic<int32_t> g_inFlight{0};   // calls between Begin and End: the buffer is not freed under them

void Write(drawtasks::What what, int64_t start, int64_t end, uint32_t pass, uint8_t eye, uint8_t index, uint64_t name) noexcept {
    const uint32_t i = g_count.fetch_add(1);
    Record* const records = g_records.load();
    if (!records || i >= g_capacity.load()) return;   // full: counted, not kept
    Record& r = records[i];
    r.start = start; r.end = end; r.name = name; r.pass = pass;
    r.thread = GetCurrentThreadId();
    r.what = uint8_t(what); r.eye = eye; r.index = index;
    r.onDraw = r.thread == g_live.thread.load() ? 1 : 0;
    r.done.store(1);
}
}  // namespace

int64_t Now() noexcept { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }

Open Begin() noexcept {
    Open o;
    g_inFlight.fetch_add(1);
    const uint32_t state = g_live.state.load();
    if (!g_armed.load() || !state) { g_inFlight.fetch_sub(1); return o; }   // no capture (it ended meanwhile), or no eye pass
    o.on = true;
    o.eye = uint8_t(state - 1);
    o.pass = g_live.sequence.load();
    o.generation = g_generation.load();
    o.start = Now();
    return o;
}
void End(const Open& open, drawtasks::What what, uint8_t index, uint64_t name) noexcept {
    const int64_t end = Now();
    if (g_armed.load() && open.generation == g_generation.load()) Write(what, open.start, end, open.pass, open.eye, index, name);
    g_inFlight.fetch_sub(1);
}

namespace {
// A whole eye pass, from the thread that drew it.
void Pass(int64_t start, int64_t end, uint32_t pass, uint8_t eye) noexcept {
    if (!Armed()) return;
    g_inFlight.fetch_add(1);
    if (g_armed.load()) Write(drawtasks::What::Pass, start, end, pass, eye, 0, 0);
    g_inFlight.fetch_sub(1);
}

// The buffer for one capture, every page of it touched: a hooked call never meets a new page.
void Arm(double seconds) {
    const size_t want = size_t(seconds * 20000.0) + 8192;   // the game: some 75 passes a second of about 26 records, ten times over
    const uint32_t capacity = uint32_t(want < (size_t(1) << 20) ? want : size_t(1) << 20);
    Record* const records = static_cast<Record*>(VirtualAlloc(nullptr, size_t(capacity) * sizeof(Record), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!records) return;
    memset(static_cast<void*>(records), 0, size_t(capacity) * sizeof(Record));
    g_generation.fetch_add(1);   // a call still in flight from an earlier capture writes nothing into this one
    g_count.store(0);
    g_capacity.store(capacity);
    g_records.store(records);
    g_armed.store(true);
}

// A name in the game's memory: printable text of 2-63 characters, or empty.
std::string ReadName(uint64_t address) {
    if (address < 0x10000) return {};
    char text[64];
    for (size_t size = sizeof text; size >= 4; size /= 2) {   // shorter when the text ends near an unreadable page
        if (!framevr::ReadMemory(text, reinterpret_cast<const void*>(uintptr_t(address)), size)) continue;
        size_t n = 0;
        while (n < size && text[n]) ++n;
        if (n == size) { if (size != sizeof text) return {}; n = size - 1; }
        if (n < 2) return {};
        for (size_t i = 0; i < n; ++i) if (text[i] < 0x20 || text[i] > 0x7E) return {};
        return std::string(text, n);
    }
    return {};
}

// Ends the recording and hands over what was recorded, times in ms from `epoch`.
void Collect(double usPerTick, int64_t epoch, std::vector<drawtasks::Run>& runs, std::vector<std::string>& names, uint32_t& dropped) {
    g_armed.store(false);
    for (int i = 0; i < 1000 && g_inFlight.load() > 0; ++i) Sleep(1);   // a task in flight ends within its pass
    const bool quiet = g_inFlight.load() <= 0;
    Record* const records = g_records.load();
    const uint32_t capacity = g_capacity.load(), count = g_count.load();
    dropped = count > capacity ? count - capacity : 0;
    if (!records) return;
    std::vector<uint64_t> addresses;
    runs.reserve(count < capacity ? count : capacity);
    for (uint32_t i = 0; i < count && i < capacity; ++i) {
        const Record& r = records[i];
        if (!r.done.load()) continue;
        drawtasks::Run run;
        run.start = double(r.start - epoch) * usPerTick * 0.001;
        run.end = double(r.end - epoch) * usPerTick * 0.001;
        run.pass = r.pass; run.thread = r.thread;
        run.what = drawtasks::What(r.what); run.eye = r.eye; run.index = r.index; run.onDraw = r.onDraw != 0;
        if (r.name) {
            const auto it = std::find(addresses.begin(), addresses.end(), r.name);
            run.name = int(it - addresses.begin());
            if (it == addresses.end()) addresses.push_back(r.name);
        }
        runs.push_back(run);
    }
    for (uint64_t address : addresses) names.push_back(ReadName(address));
    if (quiet) {   // otherwise a call is still out there with the buffer in hand: it is left to it
        g_records.store(nullptr);
        g_capacity.store(0);
        VirtualFree(records, 0, MEM_RELEASE);
    }
}
}  // namespace
}  // namespace tasks

void EnterPassArmed(int eye) noexcept {
    volatile char here = 0;   // in this call's frame: every frame of the pass that follows lies below it
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    g_live.entered = q.QuadPart;
    g_live.rootSp.store((uintptr_t)&here);
    g_live.sequence.fetch_add(1);
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
    tasks::Pass(g_live.entered, q.QuadPart, g_live.sequence.load(), uint8_t(s - 1));
}

namespace {

constexpr size_t StackCopy = 256 * 1024;   // at most this much of the stopped thread's stack, from its stack pointer up
constexpr size_t RootSlack = 1024;         // copied past the pass's mark: the frame that made the pass's call
constexpr int MaxWalk = 160;               // frames walked for the root's sake, beyond those kept
constexpr size_t Page = 4096;

// One stopped thread's state, in memory committed and touched before the capture.
struct Raw {
    CONTEXT ctx;
    uintptr_t stackAt;   // the stack pointer the copy starts at
    uintptr_t root;      // the pass's mark (Live::rootSp), 0 = none inside the stack
    size_t copied;       // bytes copied, 0 = none
    bool whole;          // the copy reaches the stack's base
    bool outside;        // stopped, but no longer drawing an eye pass
    uint8_t eye;
    uint8_t* stack;      // StackCopy + 16 bytes, a no-access page after them
};

// Stops the thread, reads it, lets it run. See THE RULE above: every line between the two calls counts.
__attribute__((noinline)) bool Take(HANDLE thread, uint32_t tid, Raw& raw) noexcept {
    raw.copied = 0; raw.whole = false; raw.outside = false; raw.root = 0;
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
                size_t n = high - sp < StackCopy ? size_t(high - sp) : StackCopy;
                // No farther than the frame the pass was called from: what is above it is the same in every sample.
                const uintptr_t root = g_live.rootSp.load();
                if (root > sp && root < high) {
                    raw.root = root;
                    if (root - sp + RootSlack < n) n = size_t(root - sp) + RootSlack;
                }
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
    // The executable sections as RVA ranges: how many, or -1 when the header does not read as one.
    int Code(Range* out, int most) const {
        const auto* nt = Nt();
        if (!nt) return -1;
        const size_t first = size_t(reinterpret_cast<const uint8_t*>(nt) - at) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
        const auto* sections = Ptr<IMAGE_SECTION_HEADER>(first, nt->FileHeader.NumberOfSections);
        if (!sections) return -1;
        int n = 0;
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections && n < most; ++i) {
            if (!(sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            const uint32_t length = sections[i].Misc.VirtualSize ? sections[i].Misc.VirtualSize : sections[i].SizeOfRawData;
            out[n++] = {sections[i].VirtualAddress, sections[i].VirtualAddress + length};
        }
        return n;
    }
};

constexpr int MaxCode = 24;
struct Header { uint32_t size, rows, table; int codeCount; Range code[MaxCode]; };
// A loaded module's size, own unwind table and executable sections, from its header in memory. The module
// can be unloaded while it is read: a fault leaves it unknown. size 0 = as its header says.
bool ReadHeader(uint64_t base, uint32_t size, Header* out) noexcept {
    __try {
        const Image probe{reinterpret_cast<const uint8_t*>(base), size ? size : 0x1000};
        const auto* nt = probe.Nt();
        if (!nt) return false;
        out->size = size ? size : nt->OptionalHeader.SizeOfImage;
        const Image image{probe.at, out->size};
        out->rows = 0; out->table = 0;
        if (const auto* dir = image.Directory(IMAGE_DIRECTORY_ENTRY_EXCEPTION)) {
            const uint32_t rows = dir->Size / sizeof(RuntimeFn);
            if (image.Ptr<RuntimeFn>(dir->VirtualAddress, rows)) { out->rows = rows; out->table = dir->VirtualAddress; }
        }
        out->codeCount = image.Code(out->code, MaxCode);
        return out->codeCount >= 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// What the walk knows of the memory it meets: the loaded modules (their own unwind tables and executable
// sections) and whether a stretch of memory is executable. Filled before the capture and between samples,
// never while a thread is stopped.
struct Walk {
    std::vector<WalkModule> modules;   // by base
    struct Region { uintptr_t begin, end, allocation; bool code, image; };
    Region regions[32] = {};           // stretches of memory as VirtualQuery last saw them
    int regionCount = 0, regionNext = 0;
    uint64_t blindBase = 0, blindSize = 0;
    bool ownRows = true;
    uint32_t frames = 0, own = 0;

    __attribute__((noinline)) bool Add(uint64_t base, uint32_t size) {
        Header h;
        if (!ReadHeader(base, size, &h)) return false;
        WalkModule m;
        m.base = base; m.size = h.size;
        m.image = reinterpret_cast<const uint8_t*>(base);
        if (h.rows) { m.table = reinterpret_cast<const RuntimeFn*>(m.image + h.table); m.rows = h.rows; }
        m.code.assign(h.code, h.code + h.codeCount);
        modules.insert(std::upper_bound(modules.begin(), modules.end(), base, [](uint64_t b, const WalkModule& w) { return b < w.base; }), std::move(m));
        return true;
    }
    // The stretch of memory an address is in, or nullptr when the system does not say.
    __attribute__((noinline)) const Region* Page(uint64_t address) {
        for (int i = 0; i < regionCount; ++i) if (address >= regions[i].begin && address < regions[i].end) return &regions[i];
        MEMORY_BASIC_INFORMATION mi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(uintptr_t(address)), &mi, sizeof mi)) return nullptr;
        const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        Region& r = regions[regionNext];
        regionNext = (regionNext + 1) % int(std::size(regions));
        if (regionCount < int(std::size(regions))) ++regionCount;
        r.begin = (uintptr_t)mi.BaseAddress; r.end = r.begin + mi.RegionSize;
        r.allocation = (uintptr_t)mi.AllocationBase;
        r.code = mi.State == MEM_COMMIT && (mi.Protect & executable) && !(mi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        r.image = mi.State == MEM_COMMIT && mi.Type == MEM_IMAGE;
        return &r;
    }
    // The module an address is in, or nullptr; one loaded since the capture began is added when first met.
    __attribute__((noinline)) const WalkModule* Module(uint64_t address) {
        if (const WalkModule* m = FindWalkModule(modules, address)) return m;
        const Region* r = Page(address);
        if (!r || !r->image || !r->allocation || FindWalkModule(modules, r->allocation)) return nullptr;
        return Add(r->allocation, 0) ? FindWalkModule(modules, address) : nullptr;
    }
    // Whether an address can be executed: in one of its module's executable sections, or in memory that is
    // executable now (generated code; code a loader placed in a module's other sections).
    __attribute__((noinline)) bool Executable(uint64_t address) {
        const WalkModule* m = Module(address);
        if (m && InCode(m->code, uint32_t(address - m->base))) return true;
        const Region* r = Page(address);
        return r && r->code;
    }
    // The unwind row for an address: the system's, else the module's own (ChooseRow).
    __attribute__((noinline)) RowChoice Row(uint64_t address) {
        const auto system = [this](uint64_t a, uint64_t& base) -> const RuntimeFn* {
            if (a - blindBase < blindSize) return nullptr;
            DWORD64 b = 0;
            const RuntimeFn* row = reinterpret_cast<const RuntimeFn*>(RtlLookupFunctionEntry(a, &b, nullptr));
            base = b;
            return row;
        };
        RowChoice c = ChooseRow(nullptr, address, system);
        if (c.from == RowFrom::None && ownRows) c = ChooseRow(Module(address), address, [](uint64_t, uint64_t&) -> const RuntimeFn* { return nullptr; });
        return c;
    }
};

// The callers, from the copy of the stack, with the thread running again. Returns the frames kept.
// `cut` = why the walk ended early (Cut::None: it ended at the thread's first function, or at the root);
// `root` = it reached the frame the eye pass was called from, and stopped there: what is above is no part of the pass.
int Unwind(const Raw& raw, Walk& walk, uint64_t* out, Cut& cut, bool& root) noexcept {
    int n = 0;
    cut = Cut::None; root = false;
    out[n++] = raw.ctx.Rip;
    if (!raw.copied) return n;
    __try {
        CONTEXT c = raw.ctx;
        const uintptr_t low = raw.stackAt, high = low + raw.copied;
        const uintptr_t copy = (uintptr_t)raw.stack + (low & 15), copyEnd = copy + raw.copied;
        const intptr_t delta = (intptr_t)(copy - low);   // a multiple of 16: alignment is kept
        bool more = false;                               // callers beyond the frames kept
        Relocate(c, low, high, delta);
        for (int step = 0;; ++step) {
            if (step == MaxWalk) { cut = Cut::Limit; break; }
            const DWORD64 before = c.Rsp;
            if (before + 8 > copyEnd) { if (!raw.whole) cut = Cut::Stack; break; }
            const RowChoice row = walk.Row(c.Rip);
            ++walk.frames;
            if (row.row) {
                if (row.from == RowFrom::Module) ++walk.own;
                PVOID handlerData = nullptr; DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, row.base, c.Rip, (PRUNTIME_FUNCTION)row.row, &c, &handlerData, &establisher, nullptr);
            } else {   // no row in the system's tables nor in the module's own: a leaf (a system call stub, a hook's trampoline) - the return address is on top
                c.Rip = *(const DWORD64*)c.Rsp;
                c.Rsp += 8;
            }
            Relocate(c, low, high, delta);   // a frame pointer popped off the copy still points into the real stack
            if (c.Rsp <= before || c.Rsp > copyEnd) { if (c.Rsp <= before || !raw.whole) cut = Cut::Stack; break; }
            if (c.Rip < 0x10000 || c.Rip >= 0x0000800000000000ull) {   // the thread's first function returns to nothing
                if (raw.root) cut = Cut::NotCode;                          // but the pass's call lies before it: this was no return address
                break;
            }
            if (!walk.Executable(c.Rip)) { cut = Cut::NotCode; break; }
            if (!AfterCall(reinterpret_cast<const uint8_t*>(c.Rip), 7)) { cut = Cut::NoCall; break; }
            if (n < MaxFrames) out[n++] = c.Rip; else more = true;
            if (raw.root && c.Rsp - (DWORD64)delta > raw.root) { root = true; break; }
        }
        if (more && cut == Cut::None) cut = Cut::Limit;
    } __except (EXCEPTION_EXECUTE_HANDLER) { cut = Cut::Fault; }   // the walk read past the copy (the page after it has no access), or a module went away
    return n;
}

std::string Utf8(const wchar_t* text) {
    char b[MAX_PATH * 3] = {};
    WideCharToMultiByte(CP_UTF8, 0, text, -1, b, (int)sizeof b - 1, nullptr, nullptr);
    return b;
}

__attribute__((noinline)) void FillUnguarded(Module& m, std::vector<Function>& functions, std::vector<Export>& exports, std::vector<Range>& code, bool& sections) {
    const Image image{reinterpret_cast<const uint8_t*>(m.base), m.size};
    {
        Range ranges[MaxCode];
        const int n = image.Code(ranges, MaxCode);
        sections = n >= 0;
        if (n > 0) code.assign(ranges, ranges + n);
    }
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
bool FillGuarded(Module* m, std::vector<Function>* functions, std::vector<Export>* exports, std::vector<Range>* code, bool* sections) noexcept {
    __try { FillUnguarded(*m, *functions, *exports, *code, *sections); return true; }
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
    std::vector<Range> code;
    bool sections = false;
    if (!FillGuarded(&module, &functions, &exports, &code, &sections)) return false;
    module.functions = std::move(functions);
    module.exports = std::move(exports);
    module.code = std::move(code);
    module.sections = sections;
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
    Walk walk;
    walk.blindBase = options.blindBase; walk.blindSize = options.blindSize; walk.ownRows = options.ownRows;
    {   // the modules loaded now; one loaded later is added when a walk first meets it
        std::vector<Module> loaded;
        ReadModules(loaded, options.mod);
        for (const Module& m : loaded) walk.Add(m.base, m.size);
    }
    std::vector<drawtasks::Run> runs;
    std::vector<std::string> names;
    uint32_t tasksDropped = 0;
    if (samples && raw && stack) {
        memset(raw, 0, sizeof *raw);
        memset(stack, 0, stackBytes);   // touched: no page of it is first met while a thread is stopped
        raw->stack = stack + (stackBytes - StackCopy - 16);   // the copy ends 0-16 bytes before the no-access page
        for (int e = 0; e < 2; ++e) { g_live.passes[e].store(0); g_live.passTicks[e].store(0); }
        g_live.state.store(0);
        g_stop.store(false);
        const int priority = GetThreadPriority(GetCurrentThread());
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);   // on time, and never stopped itself holding the thread
        tasks::Arm(seconds);
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
            Cut cut = Cut::None; bool root = false;
            s.count = uint8_t(Unwind(*raw, walk, s.frames, cut, root));
            s.eye = raw->eye; s.cut = uint8_t(cut); s.root = root ? 1 : 0;
            if (!raw->copied) ++stats.noStack;
        }
        g_live.armed.store(false);
        for (int i = 0; i < 200 && g_live.state.load(); ++i) Sleep(1);   // a pass being drawn is counted whole
        tasks::Collect(usPerTick, first ? first : armedAt, runs, names, tasksDropped);
        SetThreadPriority(GetCurrentThread(), priority);
        if (thread) CloseHandle(thread);
        stats.thread = g_live.thread.load();
        stats.seconds = first ? double(last - first) * usPerTick * 1e-6 : double(now() - armedAt) * usPerTick * 1e-6;
        for (int e = 0; e < 2; ++e) {
            stats.passes[e] = g_live.passes[e].load();
            stats.passMs[e] = double(g_live.passTicks[e].load()) * usPerTick * 0.001;
        }
        stats.framesWalked = walk.frames; stats.framesOwnRow = walk.own;
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
    report.tasks = drawtasks::Summarise(runs, names, options.tasksNote, tasksDropped);
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
