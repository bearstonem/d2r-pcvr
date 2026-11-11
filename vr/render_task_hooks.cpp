// The draw sampler's render task times: see render_task_hooks.h and steamvr/draw_tasks.h.
//
// Both hooks are on code the game runs on many threads at once, so they do as little as a hook can. With
// no capture running: one flag test, then the game's function. During a capture, inside an eye pass: two
// guarded reads for the task's index and name (before the clock starts), two clock reads and one record
// in a buffer made before the capture (steamvr/draw_sampler_capture.cpp), and around a task's run the
// thread's own slot written to say which task it is in (steamvr/task_slots.h). No lock, no allocation, no log.
// The arguments go to the game's function as they came, and its result comes back as it was.
#include <windows.h>
#include <atomic>
#include <cstdio>
#include "render_task_hooks.h"
#include "render_task_signatures.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include "../steamvr/draw_sampler_capture.h"

namespace rendertasks {
namespace {
using namespace abi;
namespace tasks = framevr::drawsampler::tasks;
using framevr::drawtasks::What;

// Four integer registers in, one out: everything either function takes or returns (the runner: a pointer
// and a byte, returning a bool; the drain: the graph, returning nothing).
using Fn = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
Fn originalRun = nullptr, originalDrain = nullptr;
std::atomic<bool> names{false};   // the name's place in a task slot was proven

// The loader switches its detour on just before it publishes the way back to the game's function. A task
// cannot be skipped (the graph would never finish), so a call that arrives in between waits for it.
inline Fn Original(Fn& published) noexcept {
    auto& slot = reinterpret_cast<Fn const volatile&>(published);
    Fn call = slot;
    for (unsigned i = 0; !call; ++i) {
        if (i < 4096) YieldProcessor(); else SwitchToThread();
        call = slot;
    }
    return call;
}

uintptr_t Run(uintptr_t job, uintptr_t b, uintptr_t c, uintptr_t d) {
    const Fn call = Original(originalRun);
    if (!tasks::Armed()) return call(job, b, c, d);
    tasks::Open open = tasks::Begin();
    if (!open.on) return call(job, b, c, d);
    uint64_t pair[2] = {}, name = 0;   // {task index, graph}
    uint8_t index = 255;
    if (framevr::ReadMemory(pair, reinterpret_cast<const void*>(job), sizeof pair) && pair[0] < MaxTasks) {
        index = uint8_t(pair[0]);
        if (names.load(std::memory_order_relaxed) && pair[1] > 0x10000 &&
            !framevr::ReadMemory(&name, reinterpret_cast<const void*>(uintptr_t(pair[1] + pair[0] * SlotSize + NameOffset)), sizeof name))
            name = 0;
    }
    // "This thread is inside this task", for the sampler (steamvr/task_slots.h): the thread's own slot in a fixed
    // table, a few stores, and only when the capture watches tasks. `here` marks this frame: the task's are below it.
    volatile char here = 0;
    tasks::Enter(open, index, name, reinterpret_cast<uintptr_t>(&here));
    open.start = tasks::Now();   // the reads above are not the task's time
    const uintptr_t result = call(job, b, c, d);
    tasks::End(open, What::Task, index, name);
    return result;
}

uintptr_t Drain(uintptr_t graph, uintptr_t b, uintptr_t c, uintptr_t d) {
    const Fn call = Original(originalDrain);
    if (!tasks::Armed()) return call(graph, b, c, d);
    const tasks::Open open = tasks::Begin();
    if (!open.on) return call(graph, b, c, d);
    const uintptr_t result = call(graph, b, c, d);
    tasks::End(open, What::Drain, 0, 0);
    return result;
}

bool runIn = false, drainIn = false;
std::string said;
}  // namespace

std::string Install() {
    std::string why;
    if (!runIn) {
        // All of the fingerprint first (it proves what the hook reads), then the hook over its first instructions.
        if (!d2rsig::Check(RunRva, RunSig, sizeof RunSig))
            why = "the game's render task runner is not where and what this mod knows (another game build, or its code not run yet): no task was timed";
        else if (!(runIn = d2rsig::Hook(RunRva, RunSig, RunHookBytes, reinterpret_cast<void*>(&Run), reinterpret_cast<void**>(&originalRun))))
            why = "the hook on the game's render task runner was refused: no task was timed";
    }
    if (runIn && !names.load()) {
        if (d2rsig::Check(SlotRva, SlotSig, sizeof SlotSig) && d2rsig::Check(NameRva, NameSig, sizeof NameSig)) names.store(true);
        else why = "the place of a task's name was not proven in this game build: tasks are numbered, not named";
    }
    if (runIn && !drainIn) {
        if (!d2rsig::Check(DrainRva, DrainSig, sizeof DrainSig) ||
            !(drainIn = d2rsig::Hook(DrainRva, DrainSig, DrainHookBytes, reinterpret_cast<void*>(&Drain), reinterpret_cast<void**>(&originalDrain)))) {
            if (!why.empty()) why += "; ";
            why += "the render graph's drain is not hooked (its code differs in this game build, or the hook was refused): task offsets are from the pass's start";
        }
    }
    const std::string now = why.empty() ? "render tasks, their names and the graph's drain are timed during a capture" : why;
    if (now != said) {
        said = now;
        framevr::logging::Write(("Frame draw sampler: " + now).c_str());
    }
    return why;
}
}  // namespace rendertasks
