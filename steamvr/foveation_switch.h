#pragma once
#include "native_lock.h"
#include <atomic>
#include <cstdint>
#include <mutex>
namespace framevr::foveation {
// Whether the per-draw and per-bind callbacks follow command-list state at all.
// With foveation off they return after one relaxed load (On).
//
// Off: following goes on for GraceEyes more eye passes, so a list recording at
// the switch still has its rate image taken off by its next draw, as before.
// On: a list that was reset while nothing was followed has missed its targets,
// viewport and pipeline; it carries stamp 0 or an older one and stays at full
// quality until its next reset (every recording starts with one, ReShade's
// reset_command_list), which stamps it Current again.
class TrackingSwitch {
    std::atomic<bool> tracking{true};
    std::atomic<uint64_t> epoch{1};
    NativeMutex lock;       // Set and EyePass only; never the draw path
    bool wanted=true;
    int grace=0;
public:
    static constexpr int GraceEyes=8;
    bool On() const {return tracking.load(std::memory_order_relaxed);}
    // The stamp a command list takes when it is reset.
    uint64_t Stamp() const {return On()?epoch.load(std::memory_order_relaxed):0;}
    bool Current(uint64_t stamp) const {return stamp==epoch.load(std::memory_order_relaxed);}
    void Set(bool on) {
        std::lock_guard guard(lock);
        if(on==wanted)return;
        wanted=on;
        if(!on){grace=GraceEyes;return;}
        // Still following (switched back within the grace): every list is current.
        if(!tracking.load()){epoch.fetch_add(1);tracking.store(true);}
    }
    // Once per eye pass.
    void EyePass() {
        if(!On())return;
        std::lock_guard guard(lock);
        if(!wanted&&--grace<=0)tracking.store(false);
    }
};
}
