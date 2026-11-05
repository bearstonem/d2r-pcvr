#pragma once
#include <string>
#include "skill_slots.h"
#include "strike_skills.h"
#include "summon_commander.h"
#include "toss.h"
#include "weapon_skills.h"

// How a skill works from a hand slot in VR (2026-10-23, the owner: "describe
// how each skill works, including when gesture assigned"). Where the slot and
// what the hand holds make it a gesture it says how: strike skills and
// Fury / Zeal's timed hits, Smite with a shield, arrow skills with a bow, javelin
// and Double Throw throws, thrown traps. Anything else casts natively, aimed
// by that hand. Melee is Unleashed only (2026-10-23).
//  - Line: one or two short sentences for the hand wheel and slot chooser, at
//    most LineLength characters (their strip holds two lines);
//  - Details: the skill picker's tooltip card, at most DetailsLength.
namespace framevr::skillusage {
inline constexpr size_t LineLength=180,DetailsLength=900;
inline constexpr int Interact=357;   // the catalog's Interact / pick up entry (native_ui)
enum class Kind {None,Interact,Attack,Throw,Strike,Sequence,Smite,Bow,Quiver,Javelin,DoubleThrow,Toss,Pump,Punch,Cross,Call,Shape,Native};
inline Kind Classify(int skill) {
    namespace ws=weaponskills;namespace st=strikeskills;
    if(skill<0)return Kind::None;
    if(skill==Interact)return Kind::Interact;
    if(skill==ws::Attack)return Kind::Attack;
    if(skill==ws::Throw)return Kind::Throw;
    if(st::Known(skill))return st::MultiHit(skill)?Kind::Sequence:Kind::Strike;
    if(skill==ws::Smite)return Kind::Smite;
    if(ws::Bow(skill))return ws::QuiverSkill(skill)?Kind::Quiver:Kind::Bow;
    if(ws::JavelinSkill(skill))return Kind::Javelin;
    if(skill==ws::DoubleThrow)return Kind::DoubleThrow;
    if(toss::Skill(skill))return Kind::Toss;
    // Paladin body gestures (hammer_gesture.h, charge_gesture.h): from any slot.
    if(skill==ws::BlessedHammer)return Kind::Pump;
    if(skill==ws::Charge)return Kind::Punch;
    if(skill==ws::HolyShield)return Kind::Cross;            // cross_gesture.h
    if(skill==ws::FistOfHeavens)return Kind::Call;          // heavens_gesture.h
    if(skill==ws::Werewolf||skill==ws::Werebear)return Kind::Shape;   // shapeshift_gesture.h
    return Kind::Native;
}
struct Place {bool trigger=false,right=false,left=false;};
inline Place Where(int slot) {
    const int hand=skills::Hand(slot);const bool trigger=slot==skills::TriggerSlot(hand);
    return {trigger,trigger&&hand==0,trigger&&hand==1};
}
inline const char* Line(int skill,int slot) {
    if(slot<0||slot>=skills::Count)return "";
    if(commander::IsCommanderSkill(skill)) {
        switch(skill) {
            case commander::SkillPhalanxDef:return "Squad order: form a defensive frontal phalanx arc with clear central sightline and a tight 7.5-subtile leash.";
            case commander::SkillRingDef:return "Squad order: surround yourself in a 360-degree bone wall, shielding Skeleton Mages inside the outer ring.";
            case commander::SkillMarchBal:return "Squad order: form a balanced marching escort with a clear 24-degree aiming corridor dead ahead.";
            case commander::SkillWedgeAgg:return "Squad order: form an aggressive V-wedge vanguard behind your Golem with a wide 32-subtile combat leash.";
            case commander::SkillHoldGround:return "Squad order: march the summon army to the pointed ground spot and hold defensive formation there.";
            case commander::SkillRegroup:return "Squad order: cancel any ground hold or focus order and immediately regroup summons around you.";
            case commander::SkillFallOut:return "Squad order: break ranks and restore standard D2R vanilla AI with no formation restrictions.";
            default:break;
        }
    }
    const auto at=Where(slot);
    switch(Classify(skill)) {
        case Kind::None:return "";
        case Kind::Interact:return "Press to interact with or pick up what this hand points at.";
        case Kind::Attack:return "Press to attack what this hand points at. Swinging a melee weapon attacks without it.";
        case Kind::Throw:
            if(!at.right)break;
            return "Gesture: throw by hand. A javelin or throwing weapon flies along your throw; a gas or oil potion flies as a bottle and bursts where it shatters.";
        case Kind::Strike:
            if(!at.trigger)return "Press to cast it, aimed by this hand. On a trigger it is a gesture: a swing that lands casts it.";
            return at.left?"Gesture with a second weapon or claw: a full-cycle swing of the left hand that lands casts it; quicker swings stay weaker basic attacks.":
                           "Gesture: a full-cycle swing that lands casts it on what you hit; quicker swings stay weaker basic attacks. Holding the trigger still casts it.";
        case Kind::Sequence:
            if(!at.trigger)return "Press to cast it, aimed by this hand. On a trigger it is a gesture: a swing that lands casts it; its other hits land by themselves.";
            if(at.left)return "Gesture with a second weapon or claw: a full-cycle swing of the left hand that lands casts it, as on the right trigger.";
            return "Gesture: a full-cycle swing that lands casts it; its other hits then land by themselves. Swings while it runs stay basic attacks.";
        case Kind::Smite:
            return at.left?"Gesture with a shield: bash or thrust the shield into a monster to cast it. Without a shield, press to cast.":
                           "Press to cast it at what this hand points at. On the left trigger with a shield, a shield bash casts it.";
        case Kind::Bow:case Kind::Quiver:
            if(at.trigger)return "Gesture with a bow: draw the string and release to fire it. Without a bow, press to cast.";
            return Classify(skill)==Kind::Quiver?"With a bow: loads it into every following arrow; press again for basic arrows. Without a bow, press to cast.":
                                                 "Press to cast it natively. On a trigger with a bow, draw the string and release to fire it.";
        case Kind::Javelin:
            return at.right?"Gesture with a javelin: arms your throw; throw by hand and let go to cast it. Without a javelin, press to cast.":
                   at.left?"Press to cast it, aimed by this hand. With a javelin, the right trigger or a face button carries it on your throws.":
                           "With a javelin: loads it into every following throw; press again for basic throws. Without one, press to cast.";
        case Kind::DoubleThrow:
            return at.right?"Gesture with two throwing weapons: each hand throws its own, the right with this trigger and the left with the left trigger.":
                   at.left?"Press to cast it natively. On the right trigger with two throwing weapons, each hand throws its own.":
                           "With two throwing weapons: loads it, so each hand throws its own weapon; press again for basic throws.";
        case Kind::Toss:
            return at.right?"Gesture: press to hold the trap or bomb, throw it and let go; it lands where the arc meets the floor. A quick press casts where you point.":
                            "Press to cast it, aimed by this hand. On the right trigger you can throw it to place it.";
        case Kind::Pump:
            return "Gesture: hold the grip and punch either fist up past your head to cast it, one hammer a punch. Pressing still casts it.";
        case Kind::Punch:
            return "Gesture: hold the grip on your weapon hand and punch it straight out at a monster to charge it. Pressing still casts it.";
        case Kind::Cross:
            return "Gesture: make the sign of the cross with either hand: forehead, chest, then one shoulder and the other. Pressing still casts it.";
        case Kind::Call:
            return "Gesture: weapon arm straight and fist held, raise it 45 degrees, then bring it down to stop level with your shoulder at a monster. Pressing still casts it.";
        case Kind::Shape:
            return skill==weaponskills::Werewolf?
                "Gesture: cross your arms over your chest, bow, then wrench upright flinging both arms up and out. Again to change back. Pressing still casts it.":
                "Gesture: hold both hands above your head a moment, then slam them down together. Again to change back. Pressing still casts it.";
        case Kind::Native:break;
    }
    return at.trigger?"Press the trigger to cast it, aimed by this hand.":"Press the button to cast it, aimed by this hand.";
}
// The picker's card: the same, in full, with what the gesture adds.
inline std::string Details(int skill,int slot) {
    if(slot<0||slot>=skills::Count)return {};
    if(commander::IsCommanderSkill(skill)) {
        switch(skill) {
            case commander::SkillPhalanxDef:
                return "Squad formation & stance (zero individual skeleton micromanagement): puts your summoned army into Phalanx Arc "
                       "with Defensive stance (7.5-subtile leash). Melee skeletons form a frontal shield with a clear 24-degree aiming "
                       "corridor dead ahead, while Skeleton Mages stay protected behind them and never walk into melee.";
            case commander::SkillRingDef:
                return "Squad formation & stance: surrounds you with a 360-degree outer bone wall of melee skeletons, Golem and Revives "
                       "in Defensive stance, while Skeleton Mages fire safely from an inner ring around you.";
            case commander::SkillMarchBal:
                return "Squad formation & stance: puts your summoned army into Balanced Phalanx escort (14-subtile leash) with a clear "
                       "central aiming corridor and automatic narrow-corridor compression.";
            case commander::SkillWedgeAgg:
                return "Squad formation & stance: puts your Golem or Bear at the tip of an offensive Spearhead Wedge with melee wings "
                       "and Aggressive stance (32-subtile combat leash) to push forward into rooms.";
            case commander::SkillHoldGround:
                return "Squad order: anchors your summoned army's formation at the ground spot this hand points at in Defensive stance, "
                       "holding that choke point or doorway until you cast Army: Regroup Now.";
            case commander::SkillRegroup:
                return "Squad order: clears any ground hold anchor or priority target, switches to Balanced stance, and immediately "
                       "reforms your summoned army around your position.";
            case commander::SkillFallOut:
                return "Squad order: breaks formation ranks and restores standard Diablo II vanilla summon AI (free roam and unrestricted "
                       "movement/combat) until you activate another Army formation skill.";
            default:break;
        }
    }
    const auto at=Where(slot);
    const char* refused=" Without mana, outside its form or with the wrong weapon, the swing is a basic attack.";
    switch(Classify(skill)) {
        case Kind::None:return {};
        case Kind::Interact:return "Press to interact with or pick up what this hand points at: doors, chests, shrines, waypoints, items and people.";
        case Kind::Attack:
            // Unleashed swing rules (steamvr/GAMEPLAY.md, vrcam's [gameplay] defaults).
            return "Swing a melee weapon through a monster to attack it; no button is needed. Every stroke hits each monster it reaches once: "
                   "nothing misses, and attack speed sets no cooldown. Instead:\n"
                   "• Hit chance scales damage: a 70% chance to hit deals 70% damage.\n"
                   "• Recovery: damage x (the share of your attack cycle since your last landed stroke) squared, so swinging "
                   "after half a cycle deals 25%.\n"
                   "• Cleave: each further monster one stroke reaches takes 25% less: 100, 75, 50, then 25%.\n"
                   "• On-hit effects (leech, crushing blow, deadly strike, procs) come only from a full-cycle stroke's first "
                   "monster, and only as often as its hit roll would hit.\n"
                   "• A riposte after a perfect parry is a full hit with them.\n"
                   "A button attacks what the hand points at; on the right trigger a held javelin or potion is thrown by hand.";
        case Kind::Throw:
            if(!at.right)break;
            return "Gesture with a thrown weapon or a gas or oil potion in the right hand. Javelins, throwing knives and axes: hold the trigger, "
                   "make a deliberate throwing motion and let go; the weapon flies along your throw. Gas and oil potions: press, throw and let go; "
                   "with Bottle physics on the potion leaves your hand as a real bottle, and the game's own cloud or blast starts where it "
                   "shatters on the floor, a wall, a table or a prop, within the game's reach. A quick press or a still hold throws as the game does.";
        case Kind::Strike: {
            if(!at.trigger)return "Press to cast it natively, aimed by this hand. On a trigger it becomes a gesture: a swing that lands casts it on the monster you hit, with the skill's own mana, damage and effects.";
            std::string text=at.left?"Gesture, with a second weapon of a dual-wield pair or the other claw in a form. ":"Gesture with a melee weapon or claws. ";
            text+="A full-cycle swing that lands casts it on the monster you hit, with the skill's own mana, damage and effects. Quicker swings stay basic attacks, with less damage and no on-hit effects.";
            text+=refused;
            if(at.right)text+=" Holding the trigger still casts it natively.";
            return text;
        }
        case Kind::Sequence: {
            if(!at.trigger)return "Press to cast it natively, aimed by this hand. On a trigger it becomes a gesture: a swing that lands casts it on the monster you hit, and its other hits land by themselves.";
            std::string text=at.left?"Gesture, with a second weapon or the other claw, as on the right trigger. ":"Gesture with a melee weapon or claws. ";
            text+="A full-cycle swing that lands casts it on the monster you hit: its mana once and hit 1. Its other hits then land by themselves "
                  "at the skill's own pace, moving on to monsters nearby within reach, while you keep moving. Swings while it runs, and quicker "
                  "swings, stay basic attacks, with less damage and no on-hit effects.";
            text+=refused;
            if(at.right)text+=" Holding the trigger still casts it natively.";
            return text;
        }
        case Kind::Smite:
            return at.left?"Gesture with a shield: swing or thrust the shield through a monster to cast Smite on it, with its stun and damage; keep the right trigger released. "
                           "Without a shield, the left trigger casts it at what you point at.":
                           "Press to cast it at what this hand points at. On the left trigger with a shield equipped, swinging or thrusting the shield into a monster casts it.";
        case Kind::Bow:case Kind::Quiver:
            if(at.trigger)return "Gesture with a bow: nock with the right hand, draw the string back and release to fire it, with the skill's own mana, damage and effects. Without a bow, press to cast it.";
            return Classify(skill)==Kind::Quiver?
                "With a bow: loads it into every following arrow, which flies its physical path with the skill's mana, damage and effects. Press again for basic arrows, "
                "or another arrow skill's button to swap. Without a bow, press to cast it.":
                "Press to cast it natively. On a trigger with a bow, drawing the string and releasing it fires the skill.";
        case Kind::Javelin:
            return at.right?"Gesture with a javelin: the trigger arms your throw; throw by hand and let go to cast it along the throw, with its own mana and effects, using up a javelin. "
                            "Without a javelin, press to cast it.":
                   at.left?"Press to cast it natively, aimed by this hand. With a javelin, put it on the right trigger or a face button to throw it by hand.":
                           "With a javelin: loads it into every following throw, with its own mana and effects; press again for basic throws. Without a javelin, press to cast it.";
        case Kind::DoubleThrow:
            return at.right?"Gesture with a throwing weapon in each hand: each hand throws its own, the right with this trigger and the left with the left trigger, "
                            "together or one after the other. Each throw is one Double Throw with its own mana.":
                   at.left?"Press to cast it natively. On the right trigger with a throwing weapon in each hand, each hand throws its own weapon.":
                           "With a throwing weapon in each hand: loads it, so each hand throws its own weapon; press again for basic throws.";
        case Kind::Toss:
            return at.right?"Gesture: press the trigger and the trap or bomb appears in your hand; throw it and let go, and it is cast where the arc meets the floor, "
                            "within the skill's reach. A quick press casts where you point; holding the trigger still casts natively.":
                            "Press to cast it natively, aimed by this hand. On the right trigger you can throw it by hand to place it where it lands.";
        case Kind::Pump:
            return "Gesture with either hand, from any slot: make a fist (hold the grip, trigger free) below your head and punch it straight up past "
                   "your head. Each punch casts one hammer, with the skill's own mana and spiral; punching faster than your cast rate does nothing more. "
                   "Bring the fist back down to punch again: a fist held up high never repeats. Pressing this slot still casts it natively.";
        case Kind::Punch:
            return "Gesture with the weapon hand, from any slot: make a fist (hold the grip, trigger free) drawn back near your body and punch it "
                   "straight out, extending the arm. You charge the monster the hand points at, or the spot, with the skill's own mana and hit. "
                   "Draw the fist back in to punch again. A monster already within weapon reach also takes the thrust. Pressing this slot still casts it natively.";
        case Kind::Cross:
            return "Gesture with either hand, from any slot, trigger free: make the sign of the cross. Touch your forehead, then your chest, then one "
                   "shoulder and the other (left then right, or right then left), pausing briefly at each; a swing through the same places never counts. "
                   "It casts Holy Shield with its own mana, duration and glow. Pressing this slot still casts it natively.";
        case Kind::Call:
            return "Gesture with the weapon hand, from any slot: make a fist (hold the grip, trigger free), hold the arm straight out raised about "
                   "45 degrees (the fist a little above your forehead), then bring the straight arm down to stop level with your shoulder. The bolt "
                   "falls on the monster your arm points at; with none there, on the game's own pick. A chop that carries on down or bends the elbow "
                   "never calls it. Mana, cast rate and the bolt stay native. Pressing this slot still casts it natively.";
        case Kind::Shape:
            return std::string(skill==weaponskills::Werewolf?
                "Gesture, from any slot: cross both arms over your chest (each hand on the opposite side), bow forward over them, then wrench upright "
                "while flinging both arms up and out.":
                "Gesture, from any slot: raise both hands above your head, hold them there a moment, then slam them down together to waist height or lower.")+
                " Performing it again in form changes you back. The skill's own mana and transformation stay native; hands must be free (trigger "
                "released, nothing held). Pressing this slot still casts it natively.";
        case Kind::Native:break;
    }
    return std::string(at.trigger?"Press the trigger":"Press the button")+" to cast it natively, aimed by this hand: at the monster you point at, or where you point.";
}
// A gesture from this slot with the right equipment.
inline bool Gesture(int skill,int slot) {
    const char* text=Line(skill,slot);
    return text[0]=='G'&&text[1]=='e'&&text[2]=='s';
}
}
