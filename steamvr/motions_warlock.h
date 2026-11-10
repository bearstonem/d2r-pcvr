#pragma once
#include "spell_motion.h"

// The Warlock's motions (owner, 2026-11-10: "build the motion controls for the missing classes, I think Warlock
// is one, similar to how we did the motion controls for other already implemented classes"). D2R's own class
// (class 7, skills 373-402) had none: its skills cast from their buttons only.
//
// How its skills are cast, from the game's own tables (skills.txt, read 2026-11-10), which decides the motion:
//   - the demons (Summon Goatman, Tainted, Defiler) are summoned on a spot, as the Necromancer's golems are
//     (the same server function): an arm swept up toward the spot;
//   - the sigils (Lethargy, Rancor, Death) are laid on the ground where the pointer is, as Blizzard and Meteor
//     are (SearchOpenXY): the owner, 2026-11-10: "These sigils and stuff, are they supposed to be targeted? Like
//     Sorcerer's Blizzard?" - they are. So they are punched or drawn AT the spot, and two of them land where the
//     main hand points (Engine::LandPointed, as the Valkyrie), not on a fixed spot ahead as they first did;
//   - the hexes (Bane, Purge, Siphon) go on the Warlock's own weapon for minutes, Psychic Ward on the Warlock
//     (Bone Armor's function), Ring of Fire and Eldritch Blast burst out from the Warlock: none has a target.
// The skills that need a monster, a corpse or the demon under the pointer (Death Mark, Engorge, Bind Demon,
// Consume, Miasma Chain), the bolt, the weapon's own attacks (Cleave, Echoing Strike, Blade Warp, Mirrored
// Blades) and Blood Boil, Flame Wave, Apocalypse and Abyss stay on their buttons.
//
// Each takes one of the Necromancer's shapes, as tuned on the owner in the headset (motions_necromancer.h,
// 2026-11-05: quick, repeatable, nothing by the floor) and kept apart from one another there: a class watches
// only its own motions, so the two never meet. Shape() says which; the detector, its tuning and its order are
// the Necromancer's.
// Value-only: steamvr/motions_warlock_test.cpp.
namespace framevr::spellmotion::warlock {
// Cells ahead of the eyes' heading for the demon summoned by both arms.
constexpr float Ahead=4;
// The Necromancer's motion whose shape a Warlock's takes.
inline Motion Shape(Motion m) {
    switch(m) {
    case Motion::SummonGoatman:return Motion::Golem;            // the right arm swept up, toward its spot
    case Motion::SummonTainted:return Motion::GolemLeft;        // the left arm
    case Motion::SummonDefiler:return Motion::RaiseSkeleton;    // both arms
    case Motion::SigilLethargy:return Motion::AmplifyDamage;    // the right fist punched out at the spot
    case Motion::SigilRancor:return Motion::CorpseExplosion;    // both fists punched out at the spot
    case Motion::SigilDeath:return Motion::BoneWall;            // the right hand swept across the chest, drawing it
    case Motion::PsychicWard:return Motion::BoneArmor;          // fists crossed on the chest, held
    case Motion::RingOfFire:return Motion::PoisonNova;          // hands together, flung wide
    case Motion::EldritchBlast:return Motion::RaiseMage;        // both arms swept up in a wide V
    case Motion::HexBane:return Motion::LowerResist;            // the left fist punched out
    case Motion::HexPurge:return Motion::PoisonExplosion;       // both fists pushed down, chest to waist
    case Motion::HexSiphon:return Motion::BonePrison;           // hands apart in front, brought together
    default:return Motion::None;
    }
}
// The order they are watched in: the Necromancer's, shape for shape (both arms before one, both fists before one).
constexpr Motion Order[]={Motion::PsychicWard,Motion::HexSiphon,Motion::SummonDefiler,Motion::EldritchBlast,Motion::SummonGoatman,
    Motion::SummonTainted,Motion::RingOfFire,Motion::SigilRancor,Motion::SigilLethargy,Motion::HexBane,Motion::SigilDeath,
    Motion::HexPurge};
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::SummonGoatman:return "sweep the right arm up fast in front of you, toward its spot";
    case Motion::SummonTainted:return "sweep the left arm up fast in front of you, toward its spot";
    case Motion::SummonDefiler:return "sweep both arms up fast from the hips, past your face";
    case Motion::SigilLethargy:return "punch the right fist out at the ground where you want it";
    case Motion::SigilRancor:return "punch both fists out together, the right one at the ground where you want it";
    case Motion::SigilDeath:return "sweep the right hand sideways across in front of your chest, drawing the sigil";
    case Motion::PsychicWard:return "cross both fists on the chest and hold";
    case Motion::RingOfFire:return "hands together at the chest, then fling them out wide";
    case Motion::EldritchBlast:return "sweep both arms up fast from the hips, spread wide in a V";
    case Motion::HexBane:return "punch the left fist out";
    case Motion::HexPurge:return "push both fists down in front of you, from the chest to the waist";
    case Motion::HexSiphon:return "hold both hands apart in front, then bring them together";
    default:return "no motion";
    }
}
inline bool Done(Engine& e,Motion m) {
    const Motion shape=Shape(m);
    if(shape==Motion::None||!necromancer::Done(e,shape))return false;
    switch(m) {
    case Motion::SummonDefiler:e.Aim({0,0,-1});e.LandAt(Ahead,0);break;                 // both arms: straight ahead
    case Motion::SigilLethargy:case Motion::SigilRancor:e.LandPointed();break;          // where the main hand points
    case Motion::HexBane:e.Aim({});break;   // on the weapon: the left arm's heading (Lower Resist's) means nothing
    default:break;   // the one-armed demons and Sigil: Death have their spot from the shape; the rest go nowhere
    }
    return true;
}
}
