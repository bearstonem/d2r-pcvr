#pragma once
#include "../steamvr/skill_cooldowns.h"
// Wrist skill cooldowns, the game side (steamvr/skill_cooldowns.h): the hero's skills the server's use check blocks
// for a delay, and the frame each is ready on, read from the server's own timers.
namespace cooldownstate {
// Owning game thread only (interactionstate's 200 ms sample). Unknown when there is no local server (joined
// games), the code is not the studied code, or a read fails.
framevr::cooldowns::Board Sample(uint64_t session);
}
