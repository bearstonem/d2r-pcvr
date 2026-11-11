#pragma once
#include "spell_motion.h"

// The Warlock's motions (owner, 2026-11-10: "build the motion controls for the missing classes, I think Warlock
// is one, similar to how we did the motion controls for other already implemented classes"). D2R's own class
// (class 7, skills 373-402) had none: its skills cast from their buttons only.
//
// How its skills are cast, from the game's own tables (skills.txt, read 2026-11-10), which decides the motion:
//   - the demons (Summon Goatman, Tainted, Defiler) are summoned on a spot, as the Necromancer's golems are
//     (the same server function): an arm swept up toward the spot;
//   - the sigils (Lethargy, Rancor, Death) and Abyss are laid on the ground where the pointer is, as Blizzard
//     and Meteor are (SearchOpenXY; Abyss by Meteor's own function). The owner, 2026-11-10: "These sigils and
//     stuff, are they supposed to be targeted? Like Sorcerer's Blizzard?" - they are - and "or they could land
//     on targeted monsters like sorceress": they land on the highlighted monster when there is one
//     (aimrules::MotionOnTarget, as Meteor and Blizzard), else where the main hand points (Engine::LandPointed,
//     as the Valkyrie) or where Sigil: Death's sweep draws it;
//   - Flame Wave rolls out the way it is sent: both fists punched out, along the main hand's ray;
//   - the hexes (Bane, Purge, Siphon) go on the Warlock's own weapon for minutes, Psychic Ward on the Warlock
//     (Bone Armor's function), and Ring of Fire, Eldritch Blast, Apocalypse and Blood Boil burst out from the
//     Warlock or his demon: none has a target.
// The skills that need a monster, a corpse or the demon under the pointer (Death Mark, Engorge, Bind Demon,
// Consume, Miasma Chain), the bolt and the weapon's own attacks (Cleave, Echoing Strike, Blade Warp, Mirrored
// Blades) stay on their buttons.
//
// Sixteen motions (the owner added Abyss, Apocalypse, Blood Boil and Flame Wave the same day). Each takes a
// shape tuned on the owner in the headset: the Necromancer's twelve (motions_necromancer.h, 2026-11-05: quick,
// repeatable, nothing by the floor) and four of the Sorceress's own (spell_motion.h: the rub and snap, the
// guard at the face, the forearm sweep, the shake). Shape() says which; the detectors and their tuning are
// those classes'. A class watches only its own motions, so the classes never meet - but these sixteen have not
// been done together in a headset: only on the simulated hands of motions_warlock_test.cpp.
// Value-only: steamvr/motions_warlock_test.cpp.
namespace framevr::spellmotion::warlock {
// Cells ahead of the eyes' heading for the demon summoned by both arms.
constexpr float Ahead=4;
// The Necromancer's or the Sorceress's motion whose shape a Warlock's takes.
inline Motion Shape(Motion m) {
    switch(m) {
    case Motion::SummonGoatman:return Motion::Golem;            // the right arm swept up, toward its spot
    case Motion::SummonTainted:return Motion::GolemLeft;        // the left arm
    case Motion::SummonDefiler:return Motion::RaiseSkeleton;    // both arms
    case Motion::SigilLethargy:return Motion::AmplifyDamage;    // the right fist punched out at the spot
    case Motion::SigilRancor:return Motion::PoisonExplosion;    // both fists pushed down, chest to waist
    case Motion::SigilDeath:return Motion::BoneWall;            // the right hand swept across the chest, drawing it
    case Motion::Abyss:return Motion::BonePrison;               // hands apart in front, brought together: it pulls them in
    case Motion::FlameWave:return Motion::CorpseExplosion;      // both fists punched out, the way it rolls
    case Motion::HexBane:return Motion::LowerResist;            // the left fist punched out
    case Motion::HexPurge:return Motion::CrossGuard;            // both hands up at the face, held (the Sorceress's guard)
    case Motion::HexSiphon:return Motion::ForearmSweep;         // the left hand along the right arm (her forearm sweep)
    case Motion::PsychicWard:return Motion::BoneArmor;          // fists crossed on the chest, held
    case Motion::EldritchBlast:return Motion::RaiseMage;        // both arms swept up in a wide V
    case Motion::RingOfFire:return Motion::PoisonNova;          // hands together, flung wide
    case Motion::Apocalypse:return Motion::RubSnap;             // palms rubbed, snapped apart (her Static Field)
    case Motion::BloodBoil:return Motion::Shake;                // both fists shaken (her Shiver Armor)
    default:return Motion::None;
    }
}
// The order they are watched in: the Necromancer's for his shapes (both arms before one, both fists before one),
// with the Sorceress's where hers stand: the rub before the fling (its snap apart is as wide), the guard, the
// forearm sweep and the shake after the rest.
constexpr Motion Order[]={Motion::PsychicWard,Motion::Apocalypse,Motion::Abyss,Motion::SummonDefiler,Motion::EldritchBlast,
    Motion::SummonGoatman,Motion::SummonTainted,Motion::RingOfFire,Motion::FlameWave,Motion::SigilLethargy,Motion::HexBane,
    Motion::SigilDeath,Motion::SigilRancor,Motion::HexPurge,Motion::HexSiphon,Motion::BloodBoil};
// Short: the Warlock's page has sixteen cards, each two lines of cue at most.
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::SummonGoatman:return "sweep the right arm up fast in front, toward its spot";
    case Motion::SummonTainted:return "sweep the left arm up fast in front, toward its spot";
    case Motion::SummonDefiler:return "sweep both arms up fast from the hips, past your face";
    case Motion::SigilLethargy:return "punch the right fist out, pointing where you want it";
    case Motion::SigilRancor:return "push both fists down, chest to waist, the right one pointing at its spot";
    case Motion::SigilDeath:return "sweep the right hand sideways across your chest, drawing it";
    case Motion::Abyss:return "hands apart in front, then together; it opens where the right hand points";
    case Motion::FlameWave:return "punch both fists out together, the way it should roll";
    case Motion::HexBane:return "punch the left fist out";
    case Motion::HexPurge:return "both hands up at the face, close together, and hold";
    case Motion::HexSiphon:return "sweep the left hand from the right hand up to the right shoulder";
    case Motion::PsychicWard:return "cross both fists on the chest and hold";
    case Motion::EldritchBlast:return "sweep both arms up fast from the hips, spread wide in a V";
    case Motion::RingOfFire:return "hands together at the chest, then fling them out wide";
    case Motion::Apocalypse:return "rub the palms together, then snap them apart";
    case Motion::BloodBoil:return "shake both fists hard, held well apart";
    default:return "no motion";
    }
}
inline bool Done(Engine& e,Motion m) {
    const Motion shape=Shape(m);
    if(shape==Motion::None)return false;
    if(!(shape<=Motion::Punch?e.Own(shape):necromancer::Done(e,shape)))return false;
    switch(m) {
    case Motion::SummonDefiler:e.Aim({0,0,-1});e.LandAt(Ahead,0);break;   // both arms: straight ahead
    case Motion::SigilLethargy:case Motion::SigilRancor:case Motion::Abyss:e.LandPointed();break;   // where the main hand points
    case Motion::HexBane:e.Aim({});break;   // on the weapon: the left arm's heading (Lower Resist's) means nothing
    default:break;   // the one-armed demons and Sigil: Death have their spot from the shape; the rest go nowhere of their own
    }
    return true;
}
}
