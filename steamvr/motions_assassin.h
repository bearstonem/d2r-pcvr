#pragma once
#include "spell_motion.h"

// The Assassin's motions (S1-S7, owner 2026-11-04), detected on spellmotion::Engine's history (head-local:
// +X right, +Y up from the eyes, forward -Z; hand 0 the right). Both grips are held while they are watched
// (the transport's gate); a motion that completes casts its card's skill. Include spell_motion.h, not this.
// Value-only: steamvr/motions_assassin_test.cpp.
namespace framevr::spellmotion::assassin {
// The order they are watched in: the first to complete in a frame casts.
constexpr Motion Order[]={Motion::BurstOfSpeed,Motion::Fade,Motion::CloakOfShadows,Motion::Venom,Motion::BladeShield,Motion::ShadowWarrior,Motion::MindBlast};
// How to do each, on the MOTIONS page and in the log (what the controllers' positions can tell: no palms or
// fingers, both grips held).
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::BurstOfSpeed:return "pump both arms as in a sprint, three swings each";
    case Motion::Fade:return "draw both hands down over the face, brow to chin";
    case Motion::CloakOfShadows:return "sweep both arms from the shoulders across the front until they cross";
    case Motion::Venom:return "cross the wrists in front, then flick them apart";
    case Motion::BladeShield:return "circle the right hand in front twice";
    case Motion::ShadowWarrior:return "both hands to the chest, then push them out to the right";
    case Motion::MindBlast:return "both hands at the temples, then push them forward";
    default:return "no motion";
    }
}
inline bool Done(Engine& e,Motion m) {
    (void)e;(void)m;
    return false;   // TODO: the detectors
}
}
