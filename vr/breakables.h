#pragma once
#include "../steamvr/melee_contact.h"
#include <span>

namespace breakables {
struct Strike {
    framevr::melee::Sweep sweep;
    framevr::melee::Sample sample;
    uint64_t session=0,at=0;
};
struct ArrowContact {
    float fraction=1.f;
    uintptr_t unit=0;
    uint32_t id=0;
};
void Install();
// Owning game thread only. Uses the same cancellation/age/equipment gates as
// creature contacts. Native object pointers never leave that thread.
void Apply(uintptr_t game,uintptr_t player,std::span<const Strike> strikes,bool (*allowed)(const Strike&));
// Owning game thread only. Tests whether the arrow trajectory segment from -> to
// intersects an attackable breakable container (pot, urn, basket, barrel) before stopFraction.
bool ArrowHit(uintptr_t game,framevr::math::Vec from,framevr::math::Vec to,float unitsPerMetre,float playerY,float stopFraction,ArrowContact& out);
// Operates the breakable object (breaks it, drops loot/explodes, skips barrel kick).
bool OperateArrow(uintptr_t game,uintptr_t player,uintptr_t unit,uint32_t id);
void Report();
}
