#pragma once
#include "spell_motion.h"

// The Barbarian's motions (B1-B8, owner 2026-11-04), detected on spellmotion::Engine's history (head-local:
// +X right, +Y up from the eyes, forward -Z; hand 0 the right). Both grips are held while they are watched
// (the transport's gate); a motion that completes casts its card's skill. Include spell_motion.h, not this.
// Value-only: steamvr/motions_barbarian_test.cpp.
namespace framevr::spellmotion::barbarian {
// The order they are watched in: the first to complete in a frame casts.
constexpr Motion Order[]={Motion::BattleOrders,Motion::Shout,Motion::BattleCommand,Motion::WarCry,Motion::Howl,Motion::BattleCry,Motion::Leap,Motion::FindItem};
// How to do each, on the MOTIONS page and in the log (what the controllers' positions can tell: no palms or
// fingers, both grips held).
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::BattleOrders:return "right fist to the mouth, then thrust it straight up over the head";
    case Motion::Shout:return "cup both hands round the mouth, then push them out forward together";
    case Motion::BattleCommand:return "beat the right fist on the chest twice";
    case Motion::WarCry:return "right fist to the mouth, then thrust it forward and down at the ground ahead";
    case Motion::Howl:return "fling both arms wide from the chest";
    case Motion::BattleCry:return "both fists at the shoulders, pulled down hard to the hips";
    case Motion::Leap:return "swing both arms down behind, then up past the head, fast, as for a jump";
    case Motion::FindItem:return "reach the right hand down toward the floor, then lift it";
    default:return "no motion";
    }
}
inline bool Done(Engine& e,Motion m) {
    (void)e;(void)m;
    return false;   // TODO: the detectors
}
}
