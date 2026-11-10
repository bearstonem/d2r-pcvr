#pragma once
#include "spell_motion.h"
#include <cmath>

// Test-only. Left-handed play (hand_role.h, #307) for the motions' fixtures: every fixture is written for a
// right-handed player (hand 0 the right). With `on`, the same fixture is PLAYED BY A LEFT-HANDED PLAYER
// making its mirror image, as the transport would hand it to the engine: the left hand where the fixture's
// right hand is in a mirror and given as hand 0, the right hand as hand 1, the head leaning the other way,
// Input::mirror set. Seen turns what the engine answers with a side back to the fixture's way round, so
// every right-handed expectation is checked again, unchanged, of the left-handed player.
//
// The reflection here is written on its own (a vector reflected in the plane through the head across its
// level right; the head's axes reflected and made a rotation again), not through spellmotion::Mirrored,
// so the two check each other.
namespace framevr::motionleft {
inline bool on=false;
inline spellmotion::Input Played(spellmotion::Input in) {
    if(!on)return in;
    const auto forward=math::Rotate(in.headRotation,{0,0,-1});
    const float level=std::hypot(forward.x,forward.z);
    const math::Vec n=level>1e-6f?math::Vec{-forward.z/level,0,forward.x/level}:math::Vec{1,0,0};   // the head's level right
    const auto reflect=[&](math::Vec v){return v-n*(2*math::Dot(v,n));};
    for(int h=0;h<2;++h){in.hand[h]=in.head+reflect(in.hand[h]-in.head);in.handVelocity[h]=reflect(in.handVelocity[h]);}
    const auto up=reflect(math::Rotate(in.headRotation,{0,1,0})),back=reflect(math::Rotate(in.headRotation,{0,0,1}));
    in.headRotation=math::FromAxes(math::Cross(up,back),up,back);
    in.mirror=true;
    return in;
}
inline spellmotion::Output Seen(spellmotion::Output out) {
    if(!on)return out;
    out.aim.x=-out.aim.x;out.point.x=-out.point.x;out.spot.right=-out.spot.right;
    for(auto& e:out.end)e.x=-e.x;
    return out;
}
// Runs a test's whole body right-handed, then again played left-handed.
template<class Body> void Both(Body body) {
    on=false;body();
    on=true;body();
    on=false;
}
}
