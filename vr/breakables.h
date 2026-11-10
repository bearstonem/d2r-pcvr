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
// A container on the game's grid plane, in world units (two to a cell), for the hero's spell missiles
// (vr/spell_impacts.cpp), which the game moves on that plane.
struct Container {uint32_t id=0;float x=0,z=0;int sizeX=0,sizeY=0;};
// Owning game thread only. The attackable containers within reach of (x, z); returns how many were written.
size_t Nearby(uintptr_t game,float x,float z,float reach,std::span<Container> out);
// Owning game thread only. Breaks that container as a weapon does (loot or explosion, no barrel kick), with no
// pulse in a hand: the missile may be far from it.
bool OperateMissile(uintptr_t game,uintptr_t player,uint32_t id);
void Report();
}
