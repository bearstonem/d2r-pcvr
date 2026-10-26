#pragma once
#include <cstdint>
#include "aim_reach.h"

// How each skill uses the aimed monster, and which hand's target it takes
// (Jeff, 2026-10-20). One table, so
// an exception is a row here, not another path in the casts.
namespace framevr::aimrules {
enum class Use {
    Native,   // the game picks (self skills, Strafe, unit skills until they get the monster)
    Heading,  // projectiles and fans: toward the target, else the free aim
    Ground,   // a ground skill: on the target, else the free-aim floor; a landing circle
    Curse,    // centred on the target, else the free-aim floor; instant, no circle
    Attack,   // Charge, Leap Attack, Dragon Flight, Whirlwind: the target, else the free-aim spot
    Spot,     // Teleport, Leap, golems, Raise Skeleton: the free-aim spot, never the target
    CorpseAtTarget,   // Corpse Explosion, Poison Explosion: at the target, else the free-aim spot
};
// The Warlock's skills (373-402, class 7, new in D2R) keep upstream's aim, with no
// target, rule or placement, until he is tested (Jeff, 2026-10-20: too buggy).
inline bool Stable(int skill){return skill>=373&&skill<=402;}
// directed: the game's own "aimed" flag for the skill (spellaim::Directed).
inline Use Of(int skill,bool directed) {
    if(Stable(skill))return Use::Native;
    switch(skill) {
        // ground skills
        case 251: case 256: case 261: case 262: case 271: case 272: case 276: case 257: case 273:   // Assassin
        case 51: case 56: case 59: case 62:                                                         // Sorceress
        case 234: case 244:                                                                         // Druid
        case 78: case 88:                                                                           // Bone Wall, Bone Prison
            return Use::Ground;
        // curses and other areas on a monster
        case 66: case 71: case 72: case 76: case 77: case 81: case 82: case 86: case 87: case 91:  // Necromancer
        case 17:    // Slow Missiles
        case 137:   // Taunt
            return Use::Curse;
        // attacks that travel
        case 107: case 143: case 275: case 151:   // Charge, Leap Attack, Dragon Flight, Whirlwind
            return Use::Attack;
        // the pointed spot only
        case 54: case 132:                        // Teleport, Leap
        case 28:                                  // Decoy
        case 75: case 85: case 90: case 94:       // golems
        case 70: case 80: case 95:                // Raise Skeleton, Raise Skeletal Mage, Revive
        case 131: case 142: case 150:             // Find Potion, Find Item, Grim Ward
            return Use::Spot;
        case 74: case 83:                         // Corpse Explosion, Poison Explosion
            return Use::CorpseAtTarget;
        case 26:                                  // Strafe picks its own targets
            return Use::Native;
        default:
            return directed?Use::Heading:Use::Native;
    }
}
// The cast is placed at a point (on the floor), not sent along a heading.
inline bool Placed(Use use){return use==Use::Ground||use==Use::Curse||use==Use::Attack||use==Use::Spot||use==Use::CorpseAtTarget;}
// The aimed monster counts for it.
inline bool TakesTarget(Use use){return use==Use::Heading||use==Use::Ground||use==Use::Curse||use==Use::Attack||use==Use::CorpseAtTarget;}
// A landing circle shows where it will hit.
inline bool Circle(Use use){return use==Use::Ground;}
// VR Settings > Aim: Spells and Bow skills go to the aimed monster, and Ground
// skills (0 the game's own aim, 1 where you point, 2 the aimed monster).
struct Switches {bool spells=false,bowSkills=false;int ground=0;};
// Any option On: the rules can aim some cast. All Off, nothing of them runs:
// no arm tracking, transport reads or hand-offs (cost while Off). Aim follows
// your view is not one of them: it only changes which monster the picks take
// (Tries, targetcone::PickTunnel), so alone it has nothing to do.
inline bool Any(const Switches& s){return s.spells||s.bowSkills||s.ground>0;}
// The rules aim a cast only with its kind's option On. Off (the defaults),
// every cast aims as it did before them: the casting hand's ray, Bone Wall and
// Prison on their own floor point, ground skills by the game (#159 review).
inline bool RulesAim(Use use,bool bow,const Switches& s) {
    if(use==Use::Native)return false;
    if(use==Use::Ground)return s.ground>0;
    return bow?s.bowSkills:s.spells;
}
// The aimed monster counts for the cast (with TakesTarget).
inline bool MonsterSwitch(Use use,bool bow,const Switches& s) {
    if(use==Use::Ground)return s.ground==2;
    return bow?s.bowSkills:s.spells;
}
// Cells short of the target, toward the caster: Bone Wall rises in front of the
// monster, Bone Prison (and every other skill) on it (Jeff, 2026-10-20).
inline float FrontOfTarget(int skill){return skill==78?3.f:0.f;}
// Skills the game should be handed the monster itself, when there is one: seekers
// home in on it (Guided Arrow, Bone Spirit), Attract and Taunt need a unit, and
// the traveling attacks (Charge, Leap Attack, Dragon Flight) fight it. With no
// monster they keep their rule. Only a skill the rules aim reaches the hand-off
// (RulesAim). Fist of the Heavens and Zeal stay with the game's own pick: the
// game marks both as needing a target, not as aimed, so they have no rule.
inline bool WantsUnit(int skill){
    switch(skill){case 22: case 93: case 86: case 137: case 107: case 143: case 275: return true;default: return false;}
}
// The monster handed to the game for a unit skill answers the game's target
// queries for this long after the cast (RulesUnit).
inline constexpr unsigned long long HandOffMs=500;
inline bool HandOffLive(int skill,unsigned long long at,unsigned long long now){return skill>=0&&now-at<=HandOffMs;}
// Letting go of a button drops the hand-off its own cast made while it was held.
// A cast made with the button already up (-1) keeps the HandOffMs expiry.
inline bool DropOnRelease(int handedButton,int released){return handedButton>=0&&handedButton==released;}
// A held target stays while the scan still finds it and it is inside the game's
// own cast reach (aimreach): the scan reaches past it.
inline bool KeepHeld(bool found,float dx,float dy){return found&&aimreach::Within(dx,dy);}

// Where the target comes from: the right hand (0), the left hand (1), the eyes (2).
enum Source {Right=0,Left=1,Eyes=2};
// The order to try: the button's own hand, the other hand, the eyes. A bow skill
// counts as the bow hand (left) while the bow points at something.
struct Order {Source at[3];};
inline Order For(int buttonHand,bool bowSkill) {
    if(bowSkill||buttonHand==1)return {{Left,Right,Eyes}};
    return {{Right,Left,Eyes}};
}
// VR Settings > Aim > Aim follows your view (Off by default): a raised hand aims
// alone. While the first hand of a cast's order is held out and pointing, the
// cast tries that hand's monster only: with none it goes where the hand points,
// never to the other hand's or the eyes' monster. A lowered hand, or the row
// Off, tries the whole order as before. The melee attacks (Use::Attack: Charge,
// Leap Attack, Dragon Flight, Whirlwind) from a raised hand also try the other
// hand's monster while it is outlined (otherOutlined), never the eyes': a
// raised hand sent 35 of 45 Leap Attacks where it pointed,
// past the other hand's outlined monster (Jeff, 2026-10-22). How many of For's
// sources to try.
inline int Tries(bool followsView,bool firstPointing,Use use,bool otherOutlined) {
    if(!(followsView&&firstPointing))return 3;
    return use==Use::Attack&&otherOutlined?2:1;
}
// Is the other hand's monster outlined (VR Settings > Aim > Monster highlights):
// with the outline On, Both hands outlines each hand's; Dominant hand only that
// hand's (dominant: 0 right, 1 left).
inline bool OtherOutlined(bool outline,bool bothHands,int dominant,Source other){return outline&&(bothHands||int(other)==dominant);}
// The outline shows the eyes' monster only while the hand that would cast at it
// (the one whose color it takes) is not aiming alone.
inline bool EyesOutlined(bool followsView,bool handPointing){return !(followsView&&handPointing);}
}
