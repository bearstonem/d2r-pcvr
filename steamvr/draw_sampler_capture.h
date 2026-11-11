// draw_sampler_capture.h - the draw-thread sampler itself ([debug] draw_sampler, steamvr/PERFORMANCE.md):
// a thread of its own stops the thread drawing an eye pass about a thousand times a second, keeps where
// it was and who called it, and reports the tables of draw_sampler.h in the log and in a text file.
// A diagnostic: off unless asked for, and it costs the pass a few percent while it runs.
#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include <vector>
#include "draw_sampler.h"
#include "task_slots.h"

namespace framevr::drawsampler {

// What the drawing thread publishes around each eye pass. Nothing but these is read while it is stopped.
struct Live {
    std::atomic<bool> armed{false};        // a capture wants passes marked
    std::atomic<uint32_t> state{0};        // 0 = no eye pass being drawn, 1 = the left eye's, 2 = the right's
    std::atomic<uint32_t> thread{0};       // the thread drawing it
    std::atomic<void*> teb{nullptr};       // that thread's TEB: its stack's bounds
    std::atomic<uint32_t> passes[2]{};     // passes finished, per eye
    std::atomic<int64_t> passTicks[2]{};   // their time, QueryPerformanceCounter ticks
    std::atomic<uintptr_t> rootSp{0};      // an address in the stack frame of the marking call: the pass's frames are all below it
    std::atomic<uint32_t> sequence{0};     // counts the passes marked: the pass a render task's run belongs to
    int64_t entered = 0;                   // the drawing thread's own
};
extern Live g_live;
void EnterPassArmed(int eye) noexcept;
void LeavePassArmed() noexcept;
// Around the game's draw of one eye, on the thread that draws it. One relaxed load each while no capture runs.
inline void EnterPass(int eye) noexcept { if (g_live.armed.load(std::memory_order_relaxed)) EnterPassArmed(eye); }
inline void LeavePass() noexcept { if (g_live.state.load(std::memory_order_relaxed)) LeavePassArmed(); }

// The render tasks of a pass (draw_tasks.h): the hooks on the game's task runner and on the graph's drain
// (vr/render_task_hooks.cpp) time each call with these while a capture runs. Begin and End run on some
// seventeen threads at once: no lock, no allocation, no log - a count of the calls in flight, two clock
// reads and one record in a buffer made before the capture. A call outside an eye pass is not recorded.
namespace tasks {
struct Open { int64_t start = 0; uint32_t pass = 0, generation = 0; uint8_t eye = 0; bool on = false; taskslots::Slot* slot = nullptr; };
extern std::atomic<bool> g_armed;
// One relaxed load: all a hooked call costs while no capture runs.
inline bool Armed() noexcept { return g_armed.load(std::memory_order_relaxed); }
// Before the timed call; `on` false = nothing to record (End must still not be called then).
Open Begin() noexcept;
// The clock now, for a start taken after the hook's own reads.
int64_t Now() noexcept;
// A render task's run, after Begin said on and just before the timed call, on the thread that runs it: says
// "this thread is inside this task" in the thread's slot (task_slots.h) so that the sampler can stop it there.
// `root` is an address in the caller's own stack frame: the task's frames are all below it. Does nothing
// unless the capture watches tasks. End takes it back.
void Enter(Open& open, uint8_t index, uint64_t name, uintptr_t root) noexcept;
// After the timed call, when Begin said on. `name` = the address of the task's name in the game (0 = none),
// read when the capture ends.
void End(const Open& open, drawtasks::What what, uint8_t index, uint64_t name) noexcept;
}  // namespace tasks

struct Options {
    double seconds = 30;            // of sampling, from the first eye pass seen
    unsigned periodUs = 1000;       // between samples, +-25% at random so the samples do not keep step with the frame
    unsigned waitSeconds = 120;     // given up when no eye pass is drawn for this long after the start
    HMODULE mod = nullptr;          // this mod's own module, for the report's "this mod" row
    std::wstring reportPath;        // the text file, written anew by each capture; empty = none
    void (*log)(const char*) = nullptr;
    std::string tasksNote;          // why the render tasks or the drain are not timed (the hooks' own words); empty = both are
    std::string watch;              // the render tasks whose threads are sampled too (ParseWatch); empty = none
    unsigned perTick = 3;           // such threads stopped at one tick at most, beside the draw thread (1-8)
    std::vector<uint32_t> functions;   // game functions whose inclusive time is tabulated whatever their rank (ParseFunctions); empty = none
    // For the tests only. The system's unwind-table lookup is taken to fail for addresses in
    // [blindBase, blindBase + blindSize), as it does for the game's executable; ownRows false walks as the
    // sampler first did, without the module's own table.
    uint64_t blindBase = 0, blindSize = 0;
    bool ownRows = true;
};

// One capture on a thread of its own: it ends by itself and reports to `log` and `reportPath`.
// False when one is already running or its thread could not start.
bool Start(const Options& options);
// Ends a running capture early; it still reports what it has.
void Stop();
bool Running();

// The capture on the calling thread, for the tests: blocks until done, returns the tables.
Report Capture(const Options& options, std::vector<Module>* modules = nullptr);
// Every module loaded now, sorted by base, without functions or exports.
bool ReadModules(std::vector<Module>& out, HMODULE mod);
// A loaded module's functions (its unwind table) and, for the system's modules, its named exports.
// False when the image could not be read (unloaded since).
bool FillModule(Module& module);

}  // namespace framevr::drawsampler
