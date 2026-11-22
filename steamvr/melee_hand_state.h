#pragma once
#include "melee_contact.h"
#include "attack_speed.h"

namespace framevr::melee {
// One instance per physical weapon; Classic's shared cadence lives outside it.
struct HandCombat {
    HitSet hits,landed,felt;
    attackspeed::Cadence recovery;   // Unleashed: the latest landed stroke of any kind
    attackspeed::Cadence full;       // Unleashed: the latest full-cycle stroke (combat_rules.h FixedCycle)
    uint64_t strokeSession=0,strokeId=0,cleaveSession=0,cleaveStroke=0;
    float strokeRecovery=1;unsigned cleaveLanded=0;bool strokeFull=true;
};
// Game-thread-owned history for one player. These are the existing PCVR clocks
// and hit sets, grouped so remote dispatch can own a separate instance without
// changing recovery rules or sharing a host's stroke history.
struct PlayerCombat {
    std::array<HandCombat,2> hands;
    attackspeed::Cadence cadence;
    struct TriggerLanded {uint64_t session=0,at=0;};
    TriggerLanded triggerLanded,triggerFull;
};
}
