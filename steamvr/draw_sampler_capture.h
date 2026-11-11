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

namespace framevr::drawsampler {

// What the drawing thread publishes around each eye pass. Nothing but these is read while it is stopped.
struct Live {
    std::atomic<bool> armed{false};        // a capture wants passes marked
    std::atomic<uint32_t> state{0};        // 0 = no eye pass being drawn, 1 = the left eye's, 2 = the right's
    std::atomic<uint32_t> thread{0};       // the thread drawing it
    std::atomic<void*> teb{nullptr};       // that thread's TEB: its stack's bounds
    std::atomic<uint32_t> passes[2]{};     // passes finished, per eye
    std::atomic<int64_t> passTicks[2]{};   // their time, QueryPerformanceCounter ticks
    int64_t entered = 0;                   // the drawing thread's own
};
extern Live g_live;
void EnterPassArmed(int eye) noexcept;
void LeavePassArmed() noexcept;
// Around the game's draw of one eye, on the thread that draws it. One relaxed load each while no capture runs.
inline void EnterPass(int eye) noexcept { if (g_live.armed.load(std::memory_order_relaxed)) EnterPassArmed(eye); }
inline void LeavePass() noexcept { if (g_live.state.load(std::memory_order_relaxed)) LeavePassArmed(); }

struct Options {
    double seconds = 30;            // of sampling, from the first eye pass seen
    unsigned periodUs = 1000;       // between samples, +-25% at random so the samples do not keep step with the frame
    unsigned waitSeconds = 120;     // given up when no eye pass is drawn for this long after the start
    HMODULE mod = nullptr;          // this mod's own module, for the report's "this mod" row
    std::wstring reportPath;        // the text file, written anew by each capture; empty = none
    void (*log)(const char*) = nullptr;
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
