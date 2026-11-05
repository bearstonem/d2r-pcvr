#pragma once
#include "spell_motion.h"

// The Necromancer's motions (N1-N8, owner 2026-11-04), detected on spellmotion::Engine's history (head-local:
// +X right, +Y up from the eyes, forward -Z; hand 0 the right). Both grips are held while they are watched
// (the transport's gate); a motion that completes casts its card's skill. Include spell_motion.h, not this.
// Value-only: steamvr/motions_necromancer_test.cpp.
namespace framevr::spellmotion::necromancer {
// The order they are watched in: the first to complete in a frame casts.
constexpr Motion Order[]={Motion::BoneArmor,Motion::RaiseSkeleton,Motion::CorpseExplosion,Motion::Golem,Motion::AmplifyDamage,Motion::LowerResist,Motion::PoisonNova,Motion::BonePrison};
// How to do each, on the MOTIONS page and in the log (what the controllers' positions can tell: no palms or
// fingers, both grips held).
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::BoneArmor:return "cross both fists on the chest and hold";
    case Motion::RaiseSkeleton:return "both palms up at the hips, lifted slowly to the chest";
    case Motion::CorpseExplosion:return "hold the right fist toward a corpse, then fling it open";
    case Motion::Golem:return "press both hands down toward the floor, then lift them";
    case Motion::AmplifyDamage:return "point the right hand at a monster, then clench it into a fist";
    case Motion::LowerResist:return "point the right hand at a monster, then drag it down slowly, palm down";
    case Motion::PoisonNova:return "hands together at the chest, then fling them out low and wide";
    case Motion::BonePrison:return "hold both hands apart in front, then bring them together round a monster";
    default:return "no motion";
    }
}
inline bool Done(Engine& e,Motion m) {
    (void)e;(void)m;
    return false;   // TODO: the detectors
}
}
