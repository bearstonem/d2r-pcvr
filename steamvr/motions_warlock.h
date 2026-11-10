#pragma once
#include "spell_motion.h"

// The Warlock's motions (owner, 2026-11-10: "build the motion controls for the missing classes, I think Warlock
// is one, similar to how we did the motion controls for other already implemented classes"). D2R's own class
// (class 7, skills 373-402) had none: its skills cast from their buttons only.
//
// Twelve of its skills that are cast, not shot or swung: the three demons, the three hexes on the weapon, the
// three sigils on the ground, Psychic Ward, Ring of Fire and Eldritch Blast. The skills that need a monster,
// a corpse or the demon under the pointer (Death Mark, Engorge, Bind Demon, Consume, Miasma Chain), the bolt,
// the weapon's own attacks (Cleave, Echoing Strike, Blade Warp, Mirrored Blades) and Blood Boil, Flame Wave,
// Apocalypse and Abyss stay on their buttons.
//
// Each takes one of the Necromancer's shapes, as tuned on the owner in the headset (motions_necromancer.h,
// 2026-11-05: quick, repeatable, nothing by the floor) and kept apart from one another there: a class watches
// only its own motions, so the two never meet. Shape() says which; the detector, its tuning and its order are
// the Necromancer's. What differs is where a cast goes: every summon and sigil lands on a spot (the arm's
// heading for a one-armed sweep, the sweep's middle for Sigil: Lethargy, straight ahead for the rest), and a
// hex has no way at all (it goes on the weapon).
// Value-only: steamvr/motions_warlock_test.cpp.
namespace framevr::spellmotion::warlock {
// Cells ahead of the eyes' heading for a summon or a sigil laid by both hands (the one-armed ones: the
// Necromancer's golemAhead and wallAhead, the same).
constexpr float Ahead=4;
// The Necromancer's motion whose shape a Warlock's takes.
inline Motion Shape(Motion m) {
    switch(m) {
    case Motion::SummonGoatman:return Motion::Golem;            // the right arm swept up, toward its spot
    case Motion::SummonTainted:return Motion::GolemLeft;        // the left arm
    case Motion::SummonDefiler:return Motion::RaiseSkeleton;    // both arms
    case Motion::PsychicWard:return Motion::BoneArmor;          // fists crossed on the chest, held
    case Motion::RingOfFire:return Motion::PoisonNova;          // hands together, flung wide
    case Motion::EldritchBlast:return Motion::CorpseExplosion;  // both fists punched out
    case Motion::HexBane:return Motion::AmplifyDamage;          // the right fist punched out
    case Motion::HexPurge:return Motion::LowerResist;           // the left fist
    case Motion::HexSiphon:return Motion::RaiseMage;            // both arms swept up in a wide V
    case Motion::SigilLethargy:return Motion::BoneWall;         // the right hand swept across the chest
    case Motion::SigilRancor:return Motion::PoisonExplosion;    // both fists pushed down, chest to waist
    case Motion::SigilDeath:return Motion::BonePrison;          // hands apart in front, brought together
    default:return Motion::None;
    }
}
// The order they are watched in: the Necromancer's, shape for shape (both arms before one, both fists before one).
constexpr Motion Order[]={Motion::PsychicWard,Motion::SigilDeath,Motion::SummonDefiler,Motion::HexSiphon,Motion::SummonGoatman,
    Motion::SummonTainted,Motion::RingOfFire,Motion::EldritchBlast,Motion::HexBane,Motion::HexPurge,Motion::SigilLethargy,
    Motion::SigilRancor};
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::SummonGoatman:return "sweep the right arm up fast in front of you, toward its spot";
    case Motion::SummonTainted:return "sweep the left arm up fast in front of you, toward its spot";
    case Motion::SummonDefiler:return "sweep both arms up fast from the hips, past your face";
    case Motion::PsychicWard:return "cross both fists on the chest and hold";
    case Motion::RingOfFire:return "hands together at the chest, then fling them out wide";
    case Motion::EldritchBlast:return "punch both fists out together";
    case Motion::HexBane:return "punch the right fist out";
    case Motion::HexPurge:return "punch the left fist out";
    case Motion::HexSiphon:return "sweep both arms up fast from the hips, spread wide in a V";
    case Motion::SigilLethargy:return "sweep the right hand sideways across in front of your chest, drawing the sigil";
    case Motion::SigilRancor:return "push both fists down in front of you, from the chest to the waist";
    case Motion::SigilDeath:return "hold both hands apart in front, then bring them together";
    default:return "no motion";
    }
}
inline bool Done(Engine& e,Motion m) {
    const Motion shape=Shape(m);
    if(shape==Motion::None||!necromancer::Done(e,shape))return false;
    switch(m) {
    case Motion::SummonDefiler:case Motion::SigilRancor:case Motion::SigilDeath:   // laid by both hands: straight ahead
        e.Aim({0,0,-1});e.LandAt(Ahead,0);break;
    case Motion::HexPurge:e.Aim({});break;   // on the weapon: the left arm's heading (Lower Resist's) means nothing
    default:break;   // the one-armed summons and Sigil: Lethargy have their spot from the shape; the rest go nowhere
    }
    return true;
}
}
