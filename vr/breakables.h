#pragma once
#include "../steamvr/melee_contact.h"
#include <span>

namespace breakables {
struct Strike {
    framevr::melee::Sweep sweep;
    framevr::melee::Sample sample;
    uint64_t session=0,at=0;
};
void Install();
// Owning game thread only. Uses the same cancellation/age/equipment gates as
// creature contacts. Native object pointers never leave that thread.
void Apply(uintptr_t game,uintptr_t player,std::span<const Strike> strikes,bool (*allowed)(const Strike&));
void Report();
}
