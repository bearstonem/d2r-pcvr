#pragma once
#include "spell_motion.h"

// The Amazon's motions (A1-A4, owner 2026-11-04), detected on spellmotion::Engine's history (head-local:
// +X right, +Y up from the eyes, forward -Z; hand 0 the right). Both grips are held while they are watched
// (the transport's gate); a motion that completes casts its card's skill. Include spell_motion.h, not this.
// Value-only: steamvr/motions_amazon_test.cpp.
namespace framevr::spellmotion::amazon {
// The order they are watched in: the first to complete in a frame casts.
constexpr Motion Order[]={Motion::InnerSight,Motion::SlowMissiles,Motion::Decoy,Motion::Valkyrie};
// How to do each, on the MOTIONS page and in the log (what the controllers' positions can tell: no palms or
// fingers, both grips held).
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::InnerSight:return "one hand flat at the brow, shading the eyes, then sweep it out forward";
    case Motion::SlowMissiles:return "push the right palm out flat toward the archers and hold it";
    case Motion::Decoy:return "both hands to the chest, then sweep them out together to one side";
    case Motion::Valkyrie:return "raise the right fist high, then bring it down to point where she lands";
    default:return "no motion";
    }
}
inline bool Done(Engine& e,Motion m) {
    (void)e;(void)m;
    return false;   // TODO: the detectors
}
}
