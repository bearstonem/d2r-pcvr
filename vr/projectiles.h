#pragma once
#include <cstdint>

namespace projectiles {
// Optional, signature-checked native motion-bow adjustments. Ordinary attacks
// from other owners and all non-basic-arrow missile types retain native rules.
void Configure(bool motionControls, float arrowSpeed, bool directRelease);
// Run speed slider ([gameplay] run_speed_pct, steamvr/run_speed.h): the local hero's
// run velocity, client and server alike, from the no-gear base (0) to the gear's (100).
void ConfigureRunSpeed(int percent);
// The right stick's switch (runspeed::StickToggle) has the hero running although Movement
// speed walks: the full run, as the game's own toggle ran. Any thread.
void StickFullRun(bool on);
// The gait the local hero last moved in, from the velocities the game gives it:
// 1 walking (modes 2, 6), 2 running (3), 0 never seen; at = its GetTickCount64;
// tired = its stamina was below a quarter (the game then walks whatever its toggle says).
int ObservedGait(uint64_t& at,bool* tired=nullptr);
void Install();
void Report();
bool Ready();
uintptr_t CreateVisual(uintptr_t params);
// Telemetry ("Frame missile flight:"): where the local hero's missiles end. The game thread.
void FollowFlights();
}
