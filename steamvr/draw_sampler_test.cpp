// The draw-thread sampler ([debug] draw_sampler): the tables from made-up samples and modules, then the
// sampler itself on a thread of this test - a busy one, a waiting one, and the report's file and log lines.
#include "draw_sampler_capture.h"
#include "render_task_signatures.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace framevr::drawsampler;

static bool Near(double a, double b) { return std::fabs(a - b) < 1e-6; }
static const Row* Find(const std::vector<Row>& rows, const char* label) {
    for (const Row& r : rows) if (r.label == label) return &r;
    return nullptr;
}
static Sample Make(int eye, std::initializer_list<uint64_t> frames) {
    Sample s{};
    for (uint64_t f : frames) s.frames[s.count++] = f;
    s.eye = uint8_t(eye);
    return s;
}

static void Kinds() {
    assert(Classify("D2R.exe", "C:\\Games\\D2R\\D2R.exe", true, false) == Kind::Game);
    assert(Classify("d2rl-pcvr.dll", "", false, true) == Kind::Mod);
    assert(Classify("ReShade64.dll", "", false, false) == Kind::ReShade);
    assert(Classify("nvwgf2umx.dll", "C:\\Windows\\System32\\DriverStore\\FileRepository\\nv_dispi.inf_amd64_1\\nvwgf2umx.dll", false, false) == Kind::Driver);
    assert(Classify("NVOGLV64.DLL", "", false, false) == Kind::Driver);
    assert(Classify("amdxc64.dll", "", false, false) == Kind::Driver && Classify("atidxx64.dll", "", false, false) == Kind::Driver);
    assert(Classify("somevendor.dll", "c:\\windows\\system32\\driverstore\\filerepository\\x\\somevendor.dll", false, false) == Kind::Driver);
    assert(Classify("nvngx_dlss.dll", "C:\\Windows\\System32\\DriverStore\\x\\nvngx_dlss.dll", false, false) == Kind::Ngx);
    assert(Classify("D3D12.dll", "", false, false) == Kind::D3D12 && Classify("D3D12Core.dll", "", false, false) == Kind::D3D12);
    assert(Classify("dxgi.dll", "", false, false) == Kind::Dxgi);
    assert(Classify("ntdll.dll", "", false, false) == Kind::Ntdll);
    assert(Classify("KERNELBASE.dll", "", false, false) == Kind::Kernel && Classify("KERNEL32.DLL", "", false, false) == Kind::Kernel);
    assert(Classify("win32u.dll", "", false, false) == Kind::Win32);
    assert(Classify("openxr_loader.dll", "", false, false) == Kind::OpenXR && Classify("vrclient_x64.dll", "", false, false) == Kind::OpenXR);
    assert(Classify("XR_APILAYER_ofxr_bridge.dll", "", false, false) == Kind::OpenXR);
    assert(Classify("ucrtbase.dll", "C:\\Windows\\System32\\ucrtbase.dll", false, false) == Kind::Other);
    assert(IsGraphics(Kind::Driver) && IsGraphics(Kind::D3D12) && IsGraphics(Kind::ReShade) && !IsGraphics(Kind::Game) && !IsGraphics(Kind::Ntdll));
    assert(IsSystem(Kind::Ntdll) && IsSystem(Kind::Kernel) && IsSystem(Kind::Win32) && !IsSystem(Kind::Driver));

    assert(IsWaitName("NtWaitForSingleObject") && IsWaitName("ZwWaitForSingleObject") && IsWaitName("NtWaitForMultipleObjects"));
    assert(IsWaitName("NtWaitForAlertByThreadId") && IsWaitName("NtWaitForKeyedEvent") && IsWaitName("NtDelayExecution"));
    assert(IsWaitName("NtUserMsgWaitForMultipleObjectsEx") && IsWaitName("NtGdiDdDDIWaitForSynchronizationObjectFromCpu") && IsWaitName("NtYieldExecution"));
    assert(!IsWaitName("NtClose") && !IsWaitName("NtGdiDdDDISubmitCommand") && !IsWaitName("RtlEnterCriticalSection") && !IsWaitName("Nt") && !IsWaitName(""));
}

// A module's functions from its unwind table: a piece chained to its function, a chain two deep, bad data.
static void Functions() {
    uint8_t image[0x200] = {};
    // unwind data at 0x100: no chain. At 0x110: chained, two unwind codes, its parent's row after them.
    // At 0x130: chained with one code (padded to two), parent = the piece at 0x110's row.
    image[0x100] = 1;
    image[0x110] = uint8_t(1 | (4 << 3)); image[0x112] = 2;
    const RuntimeFn parent{0x1000, 0x1100, 0x100}, piece{0x5000, 0x5040, 0x110};
    memcpy(image + 0x110 + 4 + 4, &parent, sizeof parent);
    image[0x130] = uint8_t(1 | (4 << 3)); image[0x132] = 1;
    memcpy(image + 0x130 + 4 + 4, &piece, sizeof piece);
    const RuntimeFn table[] = {
        {0x5000, 0x5040, 0x110},    // a piece of the function at 0x1000
        {0x1000, 0x1100, 0x100},
        {0x6000, 0x6010, 0x130},    // a piece of the piece: the same function
        {0x2000, 0x2400, 0x100},
        {0x7000, 0x7010, 0x1FE},    // unwind data past the image: starts at its own begin
        {0x8000, 0x8000, 0x100},    // empty: left out
        {0x9000, 0x9010, 0x101},    // the indirect form: its own begin
    };
    const auto f = BuildFunctions(table, std::size(table), image, sizeof image);
    assert(f.size() == 6);
    assert(f[0].begin == 0x1000 && f[0].start == 0x1000 && f[1].begin == 0x2000 && f[1].start == 0x2000);
    assert(f[2].begin == 0x5000 && f[2].start == 0x1000);
    assert(f[3].begin == 0x6000 && f[3].start == 0x1000);
    assert(f[4].begin == 0x7000 && f[4].start == 0x7000 && f[5].begin == 0x9000 && f[5].start == 0x9000);
    assert(BuildFunctions(table, std::size(table), nullptr, 0).size() == 6);   // no image: every row its own function

    Module m;
    m.functions = f;
    m.exports = {{0x2000, "Exported"}, {0xA000, "NtWaitForSingleObject"}, {0xA020, "NtClose"}};
    Place p = Locate(m, 0x1010); assert(p.exact && p.start == 0x1000 && !p.symbol);
    p = Locate(m, 0x10FF); assert(p.exact && p.start == 0x1000);
    p = Locate(m, 0x5020); assert(p.exact && p.start == 0x1000);            // the piece counts with its function
    p = Locate(m, 0x23FF); assert(p.exact && p.start == 0x2000 && p.symbol && !strcmp(p.symbol, "Exported"));
    p = Locate(m, 0x1100); assert(!p.exact && p.start == 0x1100);           // after the function's end: a leaf from there on
    p = Locate(m, 0x1800); assert(!p.exact && p.start == 0x1100);
    p = Locate(m, 0x0800); assert(!p.exact && p.start == 0x0800);           // before the first row
    p = Locate(m, 0xA014); assert(p.exact && p.start == 0xA000 && !strcmp(p.symbol, "NtWaitForSingleObject"));   // a stub, by its export
    p = Locate(m, 0xA020); assert(p.exact && p.start == 0xA020 && !strcmp(p.symbol, "NtClose"));
    p = Locate(m, 0xA100); assert(!p.exact);                                // too far past any export
    assert(NearestExport(m, 0x1FFF, 0x40) == nullptr && NearestExport(m, 0xA01F, 0x40)->rva == 0xA000 && NearestExport(m, 0xA000, 0)->rva == 0xA000);
    assert(NearestExport(m, 0xA001, 0) == nullptr);
}

static std::vector<Module> MadeUpModules() {
    std::vector<Module> modules(4);
    modules[0].base = 0x7FFA00000000ull; modules[0].size = 0x100000; modules[0].name = "ntdll.dll"; modules[0].kind = Kind::Ntdll;
    modules[0].timestamp = 0x11111111;
    modules[0].functions = {{0x3000, 0x3100, 0x3000}};
    modules[0].exports = {{0x3000, "RtlEnterCriticalSection"}, {0x9000, "NtWaitForSingleObject"}, {0x9020, "NtClose"}};
    modules[1].base = 0x140000000ull; modules[1].size = 0x200000; modules[1].name = "D2R.exe"; modules[1].kind = Kind::Game;
    modules[1].path = "C:\\Games\\D2R\\D2R.exe"; modules[1].timestamp = 0x65432100;
    modules[1].functions = {{0x1000, 0x1100, 0x1000}, {0x1100, 0x1200, 0x1100}, {0x2000, 0x2400, 0x2000}, {0x5000, 0x5040, 0x1000}};
    modules[2].base = 0x7FF800000000ull; modules[2].size = 0x100000; modules[2].name = "nvwgf2umx.dll"; modules[2].kind = Kind::Driver;
    modules[2].functions = {{0x100, 0x200, 0x100}};
    modules[3].base = 0x7FF900000000ull; modules[3].size = 0x100000; modules[3].name = "KERNELBASE.dll"; modules[3].kind = Kind::Kernel;
    modules[3].functions = {{0x400, 0x500, 0x400}};
    SortModules(modules);
    return modules;
}

static void Tables() {
    const auto modules = MadeUpModules();
    assert(modules[0].name == "D2R.exe" && modules[3].name == "ntdll.dll");
    assert(FindModule(modules, 0x140000000ull) == 0 && FindModule(modules, 0x1401FFFFFull) == 0 && FindModule(modules, 0x140200000ull) == -1);
    assert(FindModule(modules, 0x13FFFFFFFull) == -1 && FindModule(modules, 0x7FFA00009014ull) == 3 && FindModule(modules, 0) == -1);

    const uint64_t G = 0x140000000ull, D = 0x7FF800000000ull, K = 0x7FF900000000ull, N = 0x7FFA00000000ull;
    std::vector<Sample> samples = {
        Make(0, {G + 0x1010, G + 0x2100}),                       // in A, called by C
        Make(0, {G + 0x5010, G + 0x1050}),                       // in A's other piece, called by A: A once
        Make(1, {D + 0x150, G + 0x1150, G + 0x2100}),            // the driver, called by B, called by C
        Make(1, {D + 0x150, G + 0x1150}),
        Make(0, {N + 0x9014, K + 0x450, D + 0x180, G + 0x2100}), // waiting: the driver waits, for C
        Make(1, {N + 0x9014, K + 0x450, G + 0x1150}),            // waiting: B waits
        Make(0, {N + 0x3050, G + 0x1010}),                       // spinning on a lock: running
        Make(1, {0x12345678}),                                   // generated code
        Make(0, {G + 0x1800}),                                   // game code without an unwind row
        Make(1, {N + 0x9024}),                                   // a system call that is no wait
    };
    samples[2].cut = 1;
    Stats stats;
    stats.when = "2026-11-10 12:00:00"; stats.thread = 4242; stats.seconds = 30; stats.periodUs = 1000; stats.ticks = 12; stats.outside = 2;
    stats.passes[0] = stats.passes[1] = 10; stats.passMs[0] = 80; stats.passMs[1] = 60;
    stats.stoppedUsSum = 300; stats.stoppedUsMax = 55;
    const Report r = Aggregate(modules, samples.data(), samples.size(), stats);

    assert(r.samples[0] == 5 && r.samples[1] == 5 && r.total() == 10 && r.cut == 1);
    assert(Near(r.PassMs(0), 8) && Near(r.PassMs(1), 6) && Near(r.PassMs(2), 7));
    // 1. by whose code
    assert(r.kinds.size() == size_t(Kind::Count));
    const Row& game = r.kinds[size_t(Kind::Game)];
    assert(game.label == "game" && game.n[0] == 3 && game.n[1] == 0);
    assert(Near(r.Share(game), 0.3) && Near(r.Ms(game), 2.1) && Near(r.Ms(game, 0), 4.8) && Near(r.Ms(game, 1), 0));
    assert(r.kinds[size_t(Kind::Driver)].n[1] == 2 && r.kinds[size_t(Kind::Driver)].n[0] == 0);
    assert(r.kinds[size_t(Kind::Ntdll)].n[0] == 2 && r.kinds[size_t(Kind::Ntdll)].n[1] == 2);
    assert(r.kinds[size_t(Kind::None)].total() == 1 && r.kinds[size_t(Kind::Kernel)].total() == 0);
    assert(r.modules.size() == 4 && r.modules[0].label == "ntdll.dll" && r.modules[0].total() == 4);
    assert(Find(r.modules, "D2R.exe")->total() == 3 && Find(r.modules, "(no module)")->total() == 1);
    // 2. waiting
    assert(r.waiting[0] == 1 && r.waiting[1] == 1 && r.waits.size() == 1 && r.waits[0].label == "NtWaitForSingleObject" && r.waits[0].total() == 2);
    assert(r.waiters.size() == 2);
    assert(Find(r.waiters, "nvwgf2umx.dll+0x100 < D2R.exe+0x2000")->n[0] == 1 && Find(r.waiters, "D2R.exe+0x1100")->n[1] == 1);
    assert(r.systemCalls.size() == 3 && Find(r.systemCalls, "ntdll.dll+0x3000 RtlEnterCriticalSection")->total() == 1);
    assert(Find(r.systemCalls, "ntdll.dll+0x9020 NtClose")->total() == 1);
    // 3. the sampled function, by its start
    assert(r.leaves[0].total() == 2 && r.leaves[1].total() == 2 && r.leaves[2].total() == 2);
    assert(Find(r.leaves, "D2R.exe+0x1000")->n[0] == 2 && Find(r.leaves, "nvwgf2umx.dll+0x100")->n[1] == 2);
    assert(Find(r.leaves, "ntdll.dll+0x9000 NtWaitForSingleObject")->total() == 2);
    assert(Find(r.leaves, "D2R.exe+0x1200~")->total() == 1 && Find(r.leaves, "0x12345000 (no module, the page)")->total() == 1);
    // 4. game functions anywhere on the stack, once a sample
    assert(r.inclusive.size() == 4);
    assert(Find(r.inclusive, "D2R.exe+0x1000")->total() == 3 && Find(r.inclusive, "D2R.exe+0x1000")->n[0] == 3);
    assert(Find(r.inclusive, "D2R.exe+0x2000")->total() == 3 && Find(r.inclusive, "D2R.exe+0x1100")->total() == 3 && Find(r.inclusive, "D2R.exe+0x1100")->n[1] == 3);
    assert(Find(r.inclusive, "D2R.exe+0x1200~")->total() == 1);
    // 5. who makes the graphics calls
    assert(r.graphics[0] == 0 && r.graphics[1] == 2 && r.callers.size() == 1 && r.callers[0].label == "D2R.exe+0x1100" && r.callers[0].total() == 2);

    // the log's lines: the prefix, the length, what they say
    const auto lines = LogLines(r, "C:\\logs\\d2r-vr-draw-sampler.txt");
    assert(lines.size() >= 8);
    std::string all;
    for (const std::string& line : lines) {
        assert(line.rfind("Frame draw sampler: ", 0) == 0 && line.size() <= 255);
        all += line; all += '\n';
    }
    assert(all.find("10 samples of thread 4242 in 30.0 s, 20 eye passes (left 10, right 10); a pass 7.00 ms (left 8.00, right 6.00)") != std::string::npos);
    assert(all.find("the thread stopped 30 us a sample (longest 55)") != std::string::npos);
    assert(all.find("ntdll 40.0% (2.80 ms) | game 30.0% (2.10 ms) | GPU driver 20.0% (1.40 ms) | no module 10.0% (0.70 ms)") != std::string::npos);
    assert(all.find("waiting 20.0% (1.40 ms a pass; left 1.60, right 1.20), running 80.0%; graphics calls 20.0% (1.40 ms) - waits in: NtWaitForSingleObject 20.0% (1.40 ms)") != std::string::npos);
    assert(all.find("who waits: nvwgf2umx.dll+0x100 < D2R.exe+0x2000 10.0% (0.70 ms) | D2R.exe+0x1100 10.0% (0.70 ms)") != std::string::npos);
    assert(all.find("game functions making the graphics calls: D2R.exe+0x1100 20.0% (1.40 ms)") != std::string::npos);
    assert(all.find("left eye 5 samples, 8.00 ms a pass; right eye 5 samples, 6.00 ms a pass | game 4.80 / 0.00 ms") != std::string::npos);
    assert(lines.back().find("C:\\logs\\d2r-vr-draw-sampler.txt") != std::string::npos);

    // many long rows still make lines that fit
    Report wide = r;
    wide.leaves.clear();
    for (int i = 0; i < 40; ++i) wide.leaves.push_back({"some_module_with_a_long_name.dll+0x12345678 AnExportedNameOfSomeLength" + std::to_string(i), {5, 5}});
    for (const std::string& line : LogLines(wide, std::string(400, 'p').c_str())) assert(line.rfind("Frame draw sampler: ", 0) == 0 && line.size() <= 255);

    // the file: every table, and the modules to match a binary by
    const std::string file = FileText(r, modules);
    for (const char* part : {"1. Where the thread was", "2. Waiting or running", "2c. Who waits", "3. Top 25 functions", "4. Top 15 game functions",
                             "5. Graphics calls", "6. The two eyes", "0x000140000000  size 0x00200000  time 0x65432100  game", "C:\\Games\\D2R\\D2R.exe",
                             "   30.0    2.100    4.800     0.000        3  game", "D2R.exe+0x1200~", "waiting (stopped in a wait system call)"})
        assert(file.find(part) != std::string::npos);

    // nothing sampled: one line that says why, and a file that says so
    Stats idle; idle.seconds = 120; idle.ticks = 0;
    const Report none = Aggregate(modules, nullptr, 0, idle);
    const auto quiet = LogLines(none, "x");
    assert(quiet.size() == 1 && quiet[0].find("no samples in 120.0 s") != std::string::npos);
    assert(FileText(none, modules).find("No samples") != std::string::npos);
}

// The walk's choices, without the system's unwinder: a module whose unwind table the system does not have
// (the game's executable: unpacked in memory after it was loaded) gets its rows from its own table, an
// address with no row there is a leaf, and a data address is never a function.
static void WalkChoices() {
    // the module's table as it lies in its image: three functions, and a row in the indirect form
    alignas(4) static uint8_t image[0x1000] = {};
    const RuntimeFn rows[] = {{0x1000, 0x1100, 0x900}, {0x1100, 0x1200, 0x900}, {0x2000, 0x2400, 0x900}, {0x3000, 0x3010, 0x800 | 1}};
    const RuntimeFn real{0x3000, 0x3010, 0x900};
    memcpy(image + 0x400, rows, sizeof rows);
    memcpy(image + 0x800, &real, sizeof real);
    WalkModule game;
    game.base = 0x140000000ull; game.size = 0x10000; game.image = image;
    game.table = reinterpret_cast<const RuntimeFn*>(image + 0x400); game.rows = 4;
    game.code = {{0x1000, 0x4000}};
    const std::vector<WalkModule> loaded = {game};
    assert(FindWalkModule(loaded, game.base + 0x1050) == &loaded[0] && FindWalkModule(loaded, game.base + 0x10000) == nullptr && FindWalkModule(loaded, 0x1000) == nullptr);

    assert(FindRuntimeFn(game.table, 4, 0x1000) == game.table && FindRuntimeFn(game.table, 4, 0x10FF) == game.table);
    assert(FindRuntimeFn(game.table, 4, 0x1100) == game.table + 1 && FindRuntimeFn(game.table, 4, 0x23FF) == game.table + 2);
    assert(!FindRuntimeFn(game.table, 4, 0x0FFF) && !FindRuntimeFn(game.table, 4, 0x1200) && !FindRuntimeFn(game.table, 4, 0x2400) && !FindRuntimeFn(game.table, 4, 0x9000));
    assert(!FindRuntimeFn(game.table, 0, 0x1000));

    int asked = 0;
    const auto none = [&](uint64_t, uint64_t&) -> const RuntimeFn* { ++asked; return nullptr; };   // the system has no table for this module
    RowChoice c = ChooseRow(&game, game.base + 0x1050, none);
    assert(asked == 1 && c.from == RowFrom::Module && c.row == game.table && c.base == game.base);   // the module's own row, with its base
    c = ChooseRow(&game, game.base + 0x2200, none);
    assert(c.from == RowFrom::Module && c.row == game.table + 2);
    c = ChooseRow(&game, game.base + 0x3008, none);
    assert(c.from == RowFrom::Module && c.row == reinterpret_cast<const RuntimeFn*>(image + 0x800));   // the indirect form: the row it stands for
    c = ChooseRow(&game, game.base + 0x1800, none);
    assert(c.from == RowFrom::None && !c.row);                                                    // code with no row: a true leaf
    c = ChooseRow(&game, game.base + 0x5000, none);
    assert(c.from == RowFrom::None && !c.row);                                                    // data: no row either
    c = ChooseRow(nullptr, 0x12345678, none);
    assert(c.from == RowFrom::None);                                                              // in no module
    c = ChooseRow(&game, game.base + 0x20000, none);
    assert(c.from == RowFrom::None);                                                              // past the module
    const RuntimeFn theirs{0x1000, 0x1100, 0x900};
    c = ChooseRow(&game, game.base + 0x1050, [&](uint64_t, uint64_t& base) -> const RuntimeFn* { base = 0x7FF000000000ull; return &theirs; });
    assert(c.from == RowFrom::System && c.row == &theirs && c.base == 0x7FF000000000ull);         // the system's row wins when it has one
    WalkModule bare = game; bare.table = nullptr; bare.rows = 0;
    assert(ChooseRow(&bare, game.base + 0x1050, none).from == RowFrom::None);
    const RuntimeFn outside{0x3000, 0x3010, 0xFFF0 | 1};
    assert(DirectRuntimeFn(&outside, image, sizeof image) == nullptr && DirectRuntimeFn(&real, image, sizeof image) == &real && !DirectRuntimeFn(nullptr, image, 1));

    assert(InCode(game.code, 0x1000) && InCode(game.code, 0x3FFF) && !InCode(game.code, 0x4000) && !InCode(game.code, 0x0FFF) && !InCode({}, 0x1000));

    // a return address follows a call instruction
    const auto after = [](std::initializer_list<uint8_t> bytes) {
        uint8_t b[16] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        size_t n = 8;
        for (uint8_t v : bytes) b[n++] = v;
        return AfterCall(b + n, n);
    };
    assert(after({0xE8, 0x10, 0x20, 0x30, 0x40}));               // call rel32
    assert(after({0xFF, 0xD0}) && after({0x41, 0xFF, 0xD4}));    // call rax, call r12
    assert(after({0xFF, 0x10}) && after({0xFF, 0x50, 0x08}));    // call [rax], call [rax+8]
    assert(after({0xFF, 0x15, 1, 2, 3, 4}));                     // call [rip+disp]
    assert(after({0xFF, 0x90, 1, 2, 3, 4}));                     // call [rax+disp32]
    assert(after({0xFF, 0x14, 0xC8}) && after({0xFF, 0x54, 0x24, 0x20}) && after({0xFF, 0x94, 0xC8, 1, 2, 3, 4}));   // with an index
    assert(after({0xFF, 0x14, 0x25, 1, 2, 3, 4}));               // call [disp32]
    assert(!after({0xFF, 0xE0}) && !after({0xFF, 0x25, 1, 2, 3, 4}));   // jumps
    assert(!after({0x48, 0x89, 0x5C, 0x24, 0x08}) && !after({0, 0, 0, 0, 0, 0, 0, 0}) && !after({0xC3}));
    const uint8_t two[] = {0xFF, 0xD0};
    assert(AfterCall(two + 2, 2) && !AfterCall(two + 2, 1) && !AfterCall(two + 2, 0));

    // In the tables: a data address on a stack is no function - not in the game functions, not "past the last row".
    std::vector<Module> modules(1);
    modules[0].base = game.base; modules[0].size = 0x10000; modules[0].name = "D2R.exe"; modules[0].kind = Kind::Game;
    modules[0].functions = {{0x1000, 0x1100, 0x1000}, {0x1100, 0x1200, 0x1100}, {0x2000, 0x2400, 0x2000}};
    modules[0].code = {{0x1000, 0x4000}}; modules[0].sections = true;
    assert(IsCode(modules[0], 0x2400) && IsCode(modules[0], 0x3FFF) && !IsCode(modules[0], 0x5000) && !IsCode(modules[0], 0x0800));
    const uint64_t G = game.base;
    std::vector<Sample> samples = {
        Make(0, {G + 0x1010, G + 0x5000, G + 0x2100}),   // a pointer into .data between two real callers
        Make(1, {G + 0x1010, G + 0x9000}),               // a pointer far past the last unwind row
        Make(0, {G + 0x1010, G + 0x2500}),               // code past the last row: a leaf function there
        Make(1, {G + 0x1150, G + 0x2100}),
    };
    samples[0].root = samples[2].root = samples[3].root = 1;
    samples[1].cut = uint8_t(Cut::NotCode);
    Stats stats;
    stats.passes[0] = stats.passes[1] = 1; stats.passMs[0] = stats.passMs[1] = 8;
    stats.framesWalked = 9; stats.framesOwnRow = 7;
    const Report r = Aggregate(modules, samples.data(), samples.size(), stats);
    assert(r.notCode == 2 && r.cut == 1 && r.cuts[size_t(Cut::NotCode)] == 1 && r.cuts[size_t(Cut::Limit)] == 0);
    assert(r.root[0] == 2 && r.root[1] == 1 && Near(r.RootShare(), 0.75) && Near(r.RootShare(0), 1.0) && Near(r.RootShare(1), 0.5));
    assert(r.inclusive.size() == 4);
    assert(Find(r.inclusive, "D2R.exe+0x1000")->total() == 3 && Find(r.inclusive, "D2R.exe+0x2000")->total() == 2 && Find(r.inclusive, "D2R.exe+0x1100")->total() == 1);
    assert(Find(r.inclusive, "D2R.exe+0x2400~")->total() == 1);   // the leaf in code
    for (const std::vector<Row>* table : {&r.inclusive, &r.leaves, &r.callers, &r.waiters})
        for (const Row& row : *table) assert(row.label.find("not code") == std::string::npos && row.label.find("0x5000") == std::string::npos && row.label.find("0x9000") == std::string::npos);
    assert(r.leaves.size() == 2);   // the two functions sampled: no row for a data address
    // the sampled instruction itself outside the code sections did run (code a loader put there): a row by its page that says so
    const Sample odd = Make(0, {G + 0x5000});
    const Report o = Aggregate(modules, &odd, 1, stats);
    assert(o.leaves.size() == 1 && o.leaves[0].label == "D2R.exe+0x5000 (not code by the module's sections: the page)" && o.inclusive.empty() && o.notCode == 0);

    std::string all;
    for (const std::string& line : LogLines(r, "")) { assert(line.size() <= 255); all += line; all += '\n'; }
    assert(all.find("the walks up the stack: 75.0% of the samples reached the eye pass's own call (left 100.0%, right 50.0%)") != std::string::npos);
    assert(all.find("not to be trusted; 7 of 9 frames by a module's own table\n") != std::string::npos);
    assert(all.find("Frame draw sampler: walks cut short: 0 frame limit, 1 not code, 0 not after a call, 0 stack copy, 0 unreadable\n") != std::string::npos);
    const std::string file = FileText(r, modules);
    assert(file.find("the walks up the stack: 75.0% of the samples reached the eye pass's own call in this mod (left 100.0%, right 50.0%)") != std::string::npos);
    assert(file.find("7 of 9 frames were unwound with a module's own unwind table") != std::string::npos && file.find("2 caller addresses were no code") != std::string::npos);
}

// The render tasks' tables from made-up records.
static void Tasks() {
    using namespace framevr::drawtasks;
    const std::vector<std::string> names = {"Collect Draw Calls", "Forward Pass", "UI Pass", "Depth Pass", ""};
    const auto run = [](What what, uint32_t pass, int eye, double start, double end, uint32_t thread, bool onDraw, int name, uint8_t index = 0) {
        Run r;
        r.what = what; r.pass = pass; r.eye = uint8_t(eye); r.start = start; r.end = end; r.thread = thread; r.onDraw = onDraw; r.name = name; r.index = index;
        return r;
    };
    const std::vector<Run> runs = {
        // pass 1, left: the drain 4-9; a worker's task begun before it; one of the draw thread's own with another inside; the last ends at 8.8
        run(What::Task, 1, 0, 3.5, 6.0, 7, false, 0),
        run(What::Drain, 1, 0, 4.0, 9.0, 1, true, -1),
        run(What::Task, 1, 0, 4.2, 5.0, 1, true, 2),
        run(What::Task, 1, 0, 4.4, 4.6, 1, true, 3),
        run(What::Task, 1, 0, 6.0, 8.8, 8, false, 1),
        run(What::Pass, 1, 0, 0.0, 10.0, 1, true, -1),
        // pass 2, right: the draw thread's own task ends last
        run(What::Pass, 2, 1, 20.0, 29.0, 1, true, -1),
        run(What::Drain, 2, 1, 23.0, 28.0, 1, true, -1),
        run(What::Task, 2, 1, 23.1, 25.1, 7, false, 0),
        run(What::Task, 2, 1, 25.1, 26.0, 8, false, 1),
        run(What::Task, 2, 1, 26.5, 27.9, 1, true, 2),
        // pass 3: no window (the capture's edge): left out
        run(What::Task, 3, 0, 30.0, 31.0, 7, false, 0),
        // pass 4, left: the drain not timed - from the pass's start; a task with no name, one whose name did not read
        run(What::Pass, 4, 0, 40.0, 45.0, 1, true, -1),
        run(What::Task, 4, 0, 40.5, 42.0, 7, false, -1, 5),
        run(What::Task, 4, 0, 40.6, 41.0, 8, false, 4, 6),
        // pass 5: a window with no task: not a pass of tasks
        run(What::Pass, 5, 1, 50.0, 51.0, 1, true, -1),
    };
    const TaskReport r = Summarise(runs, names, "", 3);
    assert(r.Any() && r.Drained() && r.dropped == 3 && r.orphans == 1);
    const EyeTasks& left = r.eye[0]; const EyeTasks& right = r.eye[1];
    assert(left.passes == 2 && left.drained == 1 && left.tasks == 6 && right.passes == 1 && right.drained == 1 && right.tasks == 3);
    assert(Near(left.passMs, 15) && Near(left.drainMs, 5) && Near(left.drainLongest, 5) && Near(right.drainMs, 5));
    assert(Near(left.lastEndMs, 4.8 + 2.0) && Near(left.lastEndLongest, 4.8) && Near(right.lastEndMs, 4.9));
    assert(Near(left.ownMs, 0.8) && Near(right.ownMs, 1.4));   // the task inside another on the draw thread is not counted twice
    const auto find = [&](const char* name) -> const TaskRow* { for (const TaskRow& t : r.rows) if (t.name == name) return &t; return nullptr; };
    const TaskRow* collect = find("Collect Draw Calls"); const TaskRow* forward = find("Forward Pass"); const TaskRow* ui = find("UI Pass"); const TaskRow* depth = find("Depth Pass");
    assert(collect && forward && ui && depth && r.rows.size() == 6);
    assert(collect->runs[0] == 1 && collect->runs[1] == 1 && Near(collect->ms[0], 2.5) && Near(collect->longest[0], 2.5) && Near(collect->ms[1], 2.0));
    assert(Near(collect->startAt[0], -0.5) && Near(collect->endAt[0], 2.0) && Near(collect->startAt[1], 0.1) && collect->onDraw[0] == 0 && collect->Last() == 0);
    assert(forward->last[0] == 1 && forward->last[1] == 0 && Near(forward->idleByThen[0], 3.8) && Near(forward->endAt[0], 4.8) && forward->onDraw[0] == 0);
    assert(ui->runs[0] == 1 && ui->onDraw[0] == 1 && ui->inside[0] == 0 && ui->last[1] == 1 && Near(ui->idleByThen[1], 0.0) && Near(ui->endAt[1], 4.9));
    assert(depth->runs[0] == 1 && depth->inside[0] == 1 && depth->onDraw[0] == 1 && depth->runs[1] == 0);
    const TaskRow* five = find("task 5 (no name read)"); const TaskRow* six = find("task 6 (no name read)");
    assert(five && six && five->last[0] == 1 && Near(five->startAt[0], 0.5) && Near(five->endAt[0], 2.0) && Near(five->idleByThen[0], 2.0));
    assert(r.rows[0].Last() == 1 && r.rows[3].Last() == 0);   // the tasks ending last come first
    assert(r.rows[0].name == "UI Pass" || r.rows[0].name == "Forward Pass");

    const LogParts parts = LogText(r);
    assert(parts.lines.size() == 2);
    assert(parts.lines[0] == "render tasks: 3.0 a pass in 3 passes; the drain (the draw thread waiting for them) 5.00 ms a pass (left 5.00, right 5.00; longest 5.00), 52.6% of a pass");
    assert(parts.lines[1] == "render tasks: the last one ends 3.90 ms into the drain; the draw thread ran tasks itself for 1.10 ms of the drain and had none for 3.90 ms");
    for (const std::string& line : parts.lines) assert(line.size() <= 220);
    assert(parts.lastItems.size() == 3 && parts.longItems.size() == 6);
    bool said = false;
    for (const std::string& item : parts.lastItems) said |= item == "Forward Pass 33% (ends +3.90 ms, runs 1.85 ms, idle 3.80 ms)";
    assert(said);
    assert(parts.longItems[0] == "Collect Draw Calls 1.50 ms (-0.20 to +2.05, a worker)");
    bool own = false;
    for (const std::string& item : parts.longItems) own |= item.find("UI Pass") == 0 && item.find("the draw thread") != std::string::npos;
    assert(own);
    const std::string file = FileText(r);
    for (const char* part : {"7. Render tasks", "left eye: 2 passes of 7.500 ms, 3.0 task runs a pass", "the drain: 5.000 ms a pass (longest 5.000), 50.0% of the pass",
                             "the draw thread ran tasks itself for 0.800 ms", "(the drain was timed in 1 of the 2 passes", "right eye: 1 passes of 9.000 ms",
                             "3 records lost to a full buffer, 1 task runs of passes cut", "Forward Pass", "task 5 (no name read)"})
        assert(file.find(part) != std::string::npos);

    // no drain timed at all, and a note from the hooks
    std::vector<Run> bare;
    for (const Run& one : runs) if (one.what != What::Drain) bare.push_back(one);
    const TaskReport n = Summarise(bare, names, "the render graph's drain is not hooked");
    assert(n.Any() && !n.Drained() && n.eye[0].drained == 0 && Near(n.eye[0].lastEndMs, 8.8 + 2.0));
    const LogParts quiet = LogText(n);
    assert(quiet.lines.size() == 2 && quiet.lines[0].find("the drain itself was not timed") != std::string::npos && quiet.lines[1] == "render tasks: the render graph's drain is not hooked");
    assert(FileText(n).find("the drain was not timed: offsets are from the pass's start") != std::string::npos);
    // nothing recorded
    const TaskReport empty = Summarise({}, {}, "the game's render task runner is not where and what this mod knows");
    assert(!empty.Any() && LogText(empty).lines.size() == 1 && LogText(empty).lines[0].find("none timed - the game's render task runner") != std::string::npos);
    assert(FileText(Summarise({}, {})).find("none timed: no eye pass with its tasks") != std::string::npos);

    // the fingerprints agree with the layout the hook reads by
    using namespace rendertasks::abi;
    const uint32_t slot = uint32_t(SlotSize), name = uint32_t(NameOffset);
    assert(sizeof RunSig == 39 && RunHookBytes == 16 && !memcmp(RunSig + 25, &slot, 4) && RunSig[38] == 8);   // imul by the slot size; the graph at +8
    assert(!memcmp(SlotSig + 3, &slot, 4) && !memcmp(NameSig + 11, &name, 4));
    assert(sizeof DrainSig == 23 && DrainHookBytes == 18 && DrainHookBytes <= sizeof DrainSig && RunHookBytes <= sizeof RunSig && MaxTasks == 32);
}

// The thread the sampler stops: three functions deep in this executable, in "eye passes" - busy, waiting, or
// in and out of the locks a stopped thread must never be waited on for (the heap's, the loader's, the unwind
// tables', the C runtime's stream lock).
static std::atomic<bool> g_quit{false};
static std::atomic<bool> g_waitMode{false}, g_lockMode{false};
static FILE* g_nowhere = nullptr;
static std::atomic<uint64_t> g_loops{0};
static HANDLE g_never = nullptr;
static volatile uint64_t g_sink = 0;

__attribute__((noinline)) static uint64_t Innermost(uint64_t seed) {
    if (g_waitMode.load(std::memory_order_relaxed)) { WaitForSingleObject(g_never, 2); return seed + 1; }
    if (g_lockMode.load(std::memory_order_relaxed)) {
        for (int i = 0; i < 100; ++i) {
            void* block = malloc(size_t(48 + (seed + uint64_t(i)) % 4000));
            DWORD64 base = 0;
            seed += (uintptr_t)GetModuleHandleW(L"kernel32.dll") + (uintptr_t)RtlLookupFunctionEntry((DWORD64)(uintptr_t)&GetTickCount64 + i, &base, nullptr);
            if (g_nowhere) fprintf(g_nowhere, "%p %llu\n", block, (unsigned long long)seed);
            free(block);
        }
        return seed;
    }
    uint64_t x = seed | 1;
    for (int i = 0; i < 200000; ++i) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; }   // about a millisecond, no calls
    return x;
}
__attribute__((noinline)) static uint64_t Middle(uint64_t seed) { const uint64_t x = Innermost(seed + 3); g_sink = g_sink + x; return x ^ seed; }
__attribute__((noinline)) static uint64_t Outer(uint64_t seed) { const uint64_t x = Middle(seed * 7); g_sink = g_sink + x; return x + seed; }
// As the hooks on the game's task runner and drain do (vr/render_task_hooks.cpp): the pass's work is one
// "task" on the drawing thread inside a "drain", and a worker thread runs another task meanwhile.
static const char kOwnTask[] = "Own Task", kWorkerTask[] = "Worker Task";
static void Drawer() {
    uint64_t x = 1;
    for (int eye = 0; !g_quit.load(); eye ^= 1) {
        EnterPass(eye);
        const tasks::Open drain = tasks::Armed() ? tasks::Begin() : tasks::Open{};
        const tasks::Open own = tasks::Armed() ? tasks::Begin() : tasks::Open{};
        x = Outer(x);
        if (own.on) tasks::End(own, framevr::drawtasks::What::Task, 1, (uint64_t)(uintptr_t)kOwnTask);
        if (drain.on) tasks::End(drain, framevr::drawtasks::What::Drain, 0, 0);
        LeavePass();
        g_loops.fetch_add(1);
    }
}
static void Worker() {
    while (!g_quit.load()) {
        const tasks::Open open = tasks::Armed() ? tasks::Begin() : tasks::Open{};
        if (!open.on) { Sleep(0); continue; }
        uint64_t x = 88172645463325252ull;
        for (int i = 0; i < 200000; ++i) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; }
        g_sink = g_sink + x;
        tasks::End(open, framevr::drawtasks::What::Task, 2, (uint64_t)(uintptr_t)kWorkerTask);
    }
}

static std::vector<std::string> g_logged;
static void Logged(const char* line) { g_logged.push_back(line); }

static void Native(const char* keepReport, double lockSeconds) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    char exeName[MAX_PATH] = {};
    const wchar_t* slash = wcsrchr(exe, L'\\');
    WideCharToMultiByte(CP_UTF8, 0, slash ? slash + 1 : exe, -1, exeName, sizeof exeName - 1, nullptr, nullptr);

    // the loaded modules: this executable is the "game", ntdll's stubs resolve by name
    std::vector<Module> modules;
    assert(ReadModules(modules, nullptr) && modules.size() >= 3);
    const int self = FindModule(modules, (uint64_t)(uintptr_t)&Innermost), nt = FindModule(modules, (uint64_t)(uintptr_t)GetModuleHandleW(L"ntdll.dll"));
    assert(self >= 0 && modules[size_t(self)].kind == Kind::Game && modules[size_t(self)].name == exeName && modules[size_t(self)].timestamp);
    assert(nt >= 0 && modules[size_t(nt)].kind == Kind::Ntdll);
    assert(FillModule(modules[size_t(self)]) && modules[size_t(self)].functions.size() > 50 && modules[size_t(self)].exports.empty());
    {
        const Module& m = modules[size_t(self)];
        const uint32_t inner = uint32_t((uintptr_t)&Innermost - m.base), middle = uint32_t((uintptr_t)&Middle - m.base);
        const Place a = Locate(m, inner + 8), b = Locate(m, middle + 4);
        assert(a.exact && a.start == inner && b.exact && b.start == middle);
    }
    assert(FillModule(modules[size_t(nt)]) && modules[size_t(nt)].exports.size() > 500);
    {
        const Module& m = modules[size_t(nt)];
        const uint32_t stub = uint32_t((uintptr_t)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtWaitForSingleObject") - m.base);
        const Place p = Locate(m, stub + 0x14);   // where a thread in the call is
        assert(p.exact && p.start == stub && p.symbol && !strcmp(p.symbol, "NtWaitForSingleObject") && IsWaitName(p.symbol));
    }

    g_never = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(g_never);
    EnterPass(0); LeavePass();   // no capture: nothing is marked
    assert(g_live.state.load() == 0 && g_live.thread.load() == 0);
    assert(!tasks::Armed() && !tasks::Begin().on);   // no capture: a hooked call records nothing
    std::thread drawer(Drawer);
    std::thread worker(Worker);

    // 1. a busy thread: the samples are in this executable, three functions deep
    Options options;
    options.seconds = 0.25;
    const Report busy = Capture(options, &modules);
    const Stats& s = busy.stats;
    std::printf("Draw sampler, busy thread: %u samples in %.3f s (%u ticks, %u outside a pass, %u not read), %u passes of %.3f ms; "
                "the thread stopped %.1f us a sample (longest %.0f us), %.2f%% of the passes' time\n",
                busy.total(), s.seconds, s.ticks, s.outside, s.failed, s.passes[0] + s.passes[1], busy.PassMs(2),
                busy.total() ? s.stoppedUsSum / busy.total() : 0.0, s.stoppedUsMax,
                s.passMs[0] + s.passMs[1] > 0 ? 100.0 * s.stoppedUsSum * 0.001 / (s.passMs[0] + s.passMs[1]) : 0.0);
    assert(busy.total() >= 50 && s.failed == 0 && s.dropped == 0 && s.noStack == 0);
    assert(s.thread && s.thread != GetCurrentThreadId() && s.passes[0] > 5 && s.passes[1] > 5 && busy.PassMs(2) > 0.05);
    assert(busy.samples[0] > 5 && busy.samples[1] > 5);
    const Row& inGame = busy.kinds[size_t(Kind::Game)];
    assert(busy.Share(inGame) >= 0.8);                         // most of the sampled instructions are in this executable
    assert(busy.cut * 5 <= busy.total());                      // and the stacks walk to the thread's first function
    assert(busy.inclusive.size() >= 3);
    {
        const Module& m = modules[size_t(FindModule(modules, (uint64_t)(uintptr_t)&Innermost))];
        char label[3][MAX_PATH + 32];
        snprintf(label[0], sizeof label[0], "%s+0x%X", exeName, unsigned((uintptr_t)&Innermost - m.base));
        snprintf(label[1], sizeof label[1], "%s+0x%X", exeName, unsigned((uintptr_t)&Middle - m.base));
        snprintf(label[2], sizeof label[2], "%s+0x%X", exeName, unsigned((uintptr_t)&Outer - m.base));
        const Row* leaf = Find(busy.leaves, label[0]);
        assert(leaf && busy.Share(*leaf) >= 0.7);                               // self time: the loop
        for (const char* l : label) { const Row* row = Find(busy.inclusive, l); assert(row && busy.Share(*row) >= 0.7); }   // its callers, unwound from the copy
    }
    assert(busy.Share(Row{"", {busy.waiting[0], busy.waiting[1]}}) < 0.2);
    assert(s.stoppedUsMax < 100000.0);
    assert(!g_live.armed.load());
    // the walks end at the frame the pass was called from, and say so
    std::printf("Draw sampler, busy thread: %.1f%% of the walks reached the pass's call; %u of %u frames by a module's own table; cut %u\n",
                100.0 * busy.RootShare(), s.framesOwnRow, s.framesWalked, busy.cut);
    assert(busy.RootShare() >= 0.9 && busy.RootShare(0) >= 0.8 && busy.RootShare(1) >= 0.8 && s.framesWalked >= busy.total() && s.framesOwnRow == 0);
    // the render tasks recorded meanwhile: the drawing thread's own inside its drain, the worker's beside it
    {
        const auto& t = busy.tasks;
        const auto find = [&](const char* name) -> const framevr::drawtasks::TaskRow* { for (const auto& row : t.rows) if (row.name == name) return &row; return nullptr; };
        const auto* own = find(kOwnTask); const auto* other = find(kWorkerTask);
        std::printf("Draw sampler, tasks: %u + %u passes, drained %u + %u, %u + %u task runs, %u dropped, %u outside a recorded pass\n", t.eye[0].passes, t.eye[1].passes,
                    t.eye[0].drained, t.eye[1].drained, t.eye[0].tasks, t.eye[1].tasks, t.dropped, t.orphans);
        assert(t.Any() && t.Drained() && t.dropped == 0 && t.note.empty());
        for (int e = 0; e < 2; ++e) {
            assert(t.eye[e].passes > 5 && t.eye[e].drained == t.eye[e].passes && t.eye[e].passes <= s.passes[e]);
            assert(t.eye[e].drainMs > 0 && t.eye[e].drainMs <= t.eye[e].passMs && t.eye[e].ownMs > 0 && t.eye[e].ownMs <= t.eye[e].drainMs + 1e-9);
            assert(own && own->runs[e] == t.eye[e].passes && own->onDraw[e] == own->runs[e] && own->inside[e] == 0);   // once a pass, on the drawing thread
            assert(other && other->runs[e] > 0 && other->onDraw[e] == 0);
        }
        assert(own->Last() + other->Last() == t.eye[0].passes + t.eye[1].passes);
    }

    // 1b. the game's case: the system has no unwind table for the executable (here: it is not asked). With the
    // module's own table the callers are the same; without it, as the sampler first walked, they are lost.
    {
        const Module& m = modules[size_t(FindModule(modules, (uint64_t)(uintptr_t)&Innermost))];
        options.blindBase = m.base; options.blindSize = m.size;
        const Report blind = Capture(options, &modules);
        char outer[MAX_PATH + 32];
        snprintf(outer, sizeof outer, "%s+0x%X", exeName, unsigned((uintptr_t)&Outer - modules[size_t(FindModule(modules, (uint64_t)(uintptr_t)&Outer))].base));
        const Row* row = Find(blind.inclusive, outer);
        std::printf("Draw sampler, no system table for the executable: %.1f%% of the walks reached the pass's call, %u of %u frames by its own table, "
                    "the outer function on %.1f%% of the stacks\n", 100.0 * blind.RootShare(), blind.stats.framesOwnRow, blind.stats.framesWalked,
                    row ? 100.0 * blind.Share(*row) : 0.0);
        assert(blind.total() >= 50 && blind.RootShare() >= 0.9 && blind.stats.framesOwnRow >= blind.total());
        assert(row && blind.Share(*row) >= 0.7);
        options.ownRows = false;
        const Report old = Capture(options, &modules);
        const Row* lost = Find(old.inclusive, outer);
        std::printf("Draw sampler, the same without the module's own table: %.1f%% reached the pass's call, the outer function on %.1f%% of the stacks, "
                    "%u of %u walks cut short (%u not code, %u not after a call, %u stack)\n", 100.0 * old.RootShare(), lost ? 100.0 * old.Share(*lost) : 0.0,
                    old.cut, old.total(), old.cuts[size_t(Cut::NotCode)], old.cuts[size_t(Cut::NoCall)], old.cuts[size_t(Cut::Stack)]);
        assert(old.total() >= 50 && old.stats.framesOwnRow == 0 && old.cut * 10 >= old.total() * 7);
        assert(old.RootShare() <= 0.3 && (!lost || old.Share(*lost) <= 0.3));   // the report's own check shows the broken walk
        options.blindBase = options.blindSize = 0; options.ownRows = true;
    }

    // 2. the same thread waiting: the samples are in a wait system call, asked for by this executable
    g_waitMode.store(true);
    Sleep(20);
    const Report waiting = Capture(options, &modules);
    std::printf("Draw sampler, waiting thread: %u samples, %.0f%% waiting, in %s, asked by %s\n", waiting.total(),
                waiting.total() ? 100.0 * (waiting.waiting[0] + waiting.waiting[1]) / waiting.total() : 0.0,
                waiting.waits.empty() ? "?" : waiting.waits[0].label.c_str(), waiting.waiters.empty() ? "?" : waiting.waiters[0].label.c_str());
    assert(waiting.total() >= 50);
    assert((waiting.waiting[0] + waiting.waiting[1]) * 10 >= waiting.total() * 8);
    assert(!waiting.waits.empty() && waiting.waits[0].label == "NtWaitForSingleObject");
    {
        const Module& m = modules[size_t(FindModule(modules, (uint64_t)(uintptr_t)&Innermost))];
        char label[MAX_PATH + 32];
        snprintf(label, sizeof label, "%s+0x%X", exeName, unsigned((uintptr_t)&Innermost - m.base));
        assert(!waiting.waiters.empty() && waiting.waiters[0].label == label);   // through the stub and KERNELBASE to the caller
        assert(waiting.RootShare() >= 0.9 && waiting.cut * 10 <= waiting.total());   // and on to the pass's call: the system's return addresses pass the checks
        // the same with no table from the system for any module: every frame by the modules' own tables (ntdll's, KERNELBASE's, this executable's)
        options.blindBase = 0; options.blindSize = ~0ull;
        const Report own = Capture(options, &modules);
        options.blindSize = 0;
        std::printf("Draw sampler, waiting thread, every module by its own table: %.1f%% of the walks reached the pass's call, %u of %u frames, cut %u, asked by %s\n",
                    100.0 * own.RootShare(), own.stats.framesOwnRow, own.stats.framesWalked, own.cut, own.waiters.empty() ? "?" : own.waiters[0].label.c_str());
        assert(own.total() >= 50 && own.RootShare() >= 0.9 && own.stats.framesOwnRow * 10 >= own.stats.framesWalked * 7);
        assert(!own.waiters.empty() && own.waiters[0].label == label);
    }
    g_waitMode.store(false);

    // 2b. the same thread in and out of the heap, the loader, the unwind tables and a C stream, its lock held at
    // some of the stops: the sampler takes none of them while the thread is stopped, so the capture ends
    g_nowhere = fopen("NUL", "w");
    g_lockMode.store(true);
    Sleep(20);
    options.seconds = lockSeconds;
    const Report locks = Capture(options, &modules);
    g_lockMode.store(false);
    options.seconds = 0.25;
    std::printf("Draw sampler, thread taking locks: %u samples in %.2f s, %u not read; the thread stopped %.1f us a sample (longest %.0f us)\n",
                locks.total(), locks.stats.seconds, locks.stats.failed, locks.total() ? locks.stats.stoppedUsSum / locks.total() : 0.0, locks.stats.stoppedUsMax);
    assert(locks.total() >= 50 && locks.stats.failed == 0);
    assert(locks.inclusive.size() >= 3 && locks.Share(locks.inclusive[0]) >= 0.7);
    std::printf("Draw sampler, thread taking locks: %.1f%% of the walks reached the pass's call; cut %u (%u frame limit, %u not code, %u not after a call, %u stack, %u unreadable)\n",
                100.0 * locks.RootShare(), locks.cut, locks.cuts[size_t(Cut::Limit)], locks.cuts[size_t(Cut::NotCode)], locks.cuts[size_t(Cut::NoCall)],
                locks.cuts[size_t(Cut::Stack)], locks.cuts[size_t(Cut::Fault)]);
    assert(locks.RootShare() >= 0.9);   // through the heap, the loader, the C runtime and the system's stubs

    // 3. as the mod runs it: a thread of its own, the file and the log lines
    wchar_t dir[MAX_PATH]; GetTempPathW(MAX_PATH, dir);
    options.reportPath = std::wstring(dir) + L"d2r-vr-draw-sampler-test-" + std::to_wstring(GetCurrentProcessId()) + L".txt";
    options.log = Logged;
    options.seconds = 0.1;
    assert(Start(options) && Running() && !Start(options));
    for (int i = 0; i < 5000 && Running(); ++i) Sleep(1);
    assert(!Running());
    assert(g_logged.size() >= 8);
    for (const std::string& line : g_logged) assert(line.rfind("Frame draw sampler: ", 0) == 0 && line.size() <= 255);
    assert(g_logged.back().find("d2r-vr-draw-sampler-test-") != std::string::npos);
    {
        FILE* f = _wfopen(options.reportPath.c_str(), L"rb");
        assert(f);
        std::string text(1 << 20, ' ');
        text.resize(fread(text.data(), 1, text.size(), f));
        fclose(f);
        if (keepReport) {   // draw-sampler-test PATH: the report of this run kept there, to look at
            if (FILE* out = fopen(keepReport, "wb")) { fwrite(text.data(), 1, text.size(), out); fclose(out); }
            for (const std::string& line : g_logged) std::puts(line.c_str());
        }
        DeleteFileW(options.reportPath.c_str());
        assert(text.find("D2R PCVR draw-thread sampler") == 0 && text.find("3. Top 25 functions") != std::string::npos);
        assert(text.find(exeName) != std::string::npos && text.find("Modules (base, size, PE link timestamp") != std::string::npos);
    }
    // ended early: it still reports
    g_logged.clear();
    options.reportPath.clear();
    options.seconds = 60;
    assert(Start(options));
    Sleep(60);
    Stop();
    for (int i = 0; i < 5000 && Running(); ++i) Sleep(1);
    assert(!Running() && !g_logged.empty() && g_logged[0].find(" samples of thread ") != std::string::npos);

    // the thread that was stopped a few hundred times goes on
    const uint64_t before = g_loops.load();
    for (int i = 0; i < 3000 && g_loops.load() < before + 20; ++i) Sleep(1);
    assert(g_loops.load() >= before + 20);
    g_quit.store(true);
    drawer.join();
    worker.join();
    assert(g_live.state.load() == 0);

    // no eye pass drawn: given up, said so
    options.seconds = 0.1; options.waitSeconds = 0; options.log = nullptr;
    const Report idle = Capture(options);
    assert(idle.total() == 0 && LogLines(idle, "").size() == 1);
    CloseHandle(g_never);
    if (g_nowhere) fclose(g_nowhere);
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);   // a failed check ends the run at once: what was printed before it is kept
    Kinds();
    Functions();
    Tables();
    WalkChoices();
    Tasks();
    // draw-sampler-test PATH keeps a report there; draw-sampler-test --locks SECONDS runs the lock check that long
    const bool locks = argc > 2 && !strcmp(argv[1], "--locks");
    Native(argc > 1 && !locks ? argv[1] : nullptr, locks ? atof(argv[2]) : 0.3);
    std::puts("Draw sampler: module kinds, wait calls, function starts from unwind tables, the tables and report text, the walk's "
              "unwind rows and return addresses, the render task tables, and live captures of a busy, a waiting and a lock-taking "
              "thread (with and without the system's unwind table) passed");
}
