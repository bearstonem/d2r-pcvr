#pragma once
#include <cstdint>
#include "../steamvr/target_cone.h"
namespace D2RL {struct PluginContext;}

// Target lock: hostile monsters near the hero, read on the owning game
// thread (melee::Hostiles) ten times a second and copied here for any thread.
namespace targetlock {
struct Snapshot {
    framevr::targetcone::Target targets[framevr::targetcone::MaxTargets];
    int count=0;
    float heroX=0,heroY=0;
    uint64_t at=0; // GetTickCount64 of the read, 0 = never
};
void Init(const D2RL::PluginContext* ctx);
void Pump(bool wanted); // vrcam's update thread: schedules a refresh every 100 ms while wanted
Snapshot Get();
}
