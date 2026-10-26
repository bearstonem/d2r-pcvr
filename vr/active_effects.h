#pragma once
#include "../steamvr/active_effects.h"
namespace D2RL {struct PluginContext;}
namespace effectstate {
void Init(const D2RL::PluginContext*);
framevr::effects::Status Sample(uint64_t session); // owning game thread only
bool HolyShield(uint64_t session);   // the last sample had Holy Shield (any thread; false when older than 1 s)
int AuraState(uint64_t session);     // the last sample had an aura: 1, none: 0, older than 1 s or unknown: -1
}
