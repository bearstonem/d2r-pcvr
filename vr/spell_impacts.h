#pragma once
#include "../steamvr/spell_impacts.h"
#include <array>
#include <cstdint>
namespace framevr::bowflight {struct Shot;}
// The hero's spell missiles reach what weapons reach (steamvr/spell_impacts.h):
// they break the breakable containers they pass or burst at, as arrows do
// (breakables.h), and their passage is posted to the physics world that
// knocks the level's small props and dropped bottles about (dropped_bottles.h).
// Native calls stay on the owning game thread; the XR thread sees copies.
namespace impactvr {
using framevr::spellimpacts::Passage;
using framevr::spellimpacts::Mailbox;
void Configure(bool on);  // [interactions] spell_impacts
// A hand-cast spell's flight also breaks containers (the arrows' own test in
// vr/bow.cpp) only while this is on; arrows, bolts and thrown weapons always do.
bool Containers(const framevr::bowflight::Shot&);
// Game thread, as the server makes a missile: the hero's own spell missiles are followed from here.
void Created(uintptr_t game,uintptr_t params,uintptr_t unit);
// Game thread, inside a hand-cast spell's flight step: where it really flew
// (rendered metres), up to the wall or floor that stops it.
void Flew(const framevr::bowflight::Shot&,framevr::math::Vec from,framevr::math::Vec to,float stop);
// Game thread, around one server step of any missile (the missile update
// hook in vr/bow.cpp). Done takes the step's result (2: the missile ended)
// and returns it unchanged.
class Step {
    uintptr_t game=0,unit=0;
    uint32_t id=0,missile=0;
    bool active=false;
public:
    Step(uintptr_t game,uintptr_t unit);
    ~Step();
    Step(const Step&)=delete;
    Step& operator=(const Step&)=delete;
    int Done(int result);
};
// XR thread: the passages posted since the last call, for this session.
unsigned Take(uint64_t session,std::array<Passage,Mailbox::Most>& out);
}
