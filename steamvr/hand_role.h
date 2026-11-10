#pragma once
#include "../shared/d2r_vr_state.h"
#include <cstdint>
#include <utility>

// Left-handed play (#307; VR Settings > Controls > Hands > Left-handed, [interactions] left_handed).
//
// Two ways to number the hands, kept apart everywhere:
//   physical  0 right, 1 left: the OpenXR hand paths, the shared tracking state, the hero's arm bones,
//             a controller's palm side, the controller a haptic pulse goes to;
//   role      0 main, 1 off: the weapon hand and the shield (or second weapon) hand, the main trigger
//             and its skill slots, A / B and X / Y, the turn stick and the walk stick, the melee lanes.
// Right-handed the two are the same number, which is why the engines (gameplay.h, hand_controls.h,
// interactions.h, melee_contact.h) say "right = 0": they take ROLE order and need no change. The
// transport feeds them through a Map and sends what comes back (a pulse, a pose) to the physical hand.
//
// Built in phases. What a phase has not built keeps playing right-handed, whole: never half swapped.
//   phase 1 (this file's Mirrors): one-handed weapons, a shield, dual wield, throws, potions, bare hands;
//   phase 2 (Mirrored(System)): the laser and the menus, the wrist HUD and map, the leather menu,
//           the class motions, and every text that names a hand (hand_text.h): built, all on;
//   phase 3 (Mirrors): bows, crossbows, staves, spears, polearms and other two-handed grips.
namespace framevr::handrole {
struct Map {
    bool left=false;   // the main hand is the physical left one
    // The physical hand playing a role, and the role a physical hand plays (the same swap both ways).
    constexpr int Physical(int role) const {return left&&(role==0||role==1)?1-role:role;}
    constexpr int Role(int physical) const {return Physical(physical);}
    constexpr int Main() const {return left?1:0;}   // physical
    constexpr int Off() const {return left?0:1;}    // physical
    // A bit per hand (bit 0 right / main): a physical mask as a role mask, and back.
    constexpr uint32_t Bits(uint32_t mask) const {return left?((mask&1u)<<1)|((mask>>1)&1u)|(mask&~3u):mask;}
    // The value for a role, given the physical right's and left's.
    template<class T> constexpr const T& Pick(int role,const T& right,const T& left_) const {return Physical(role)==1?left_:right;}
    // A pair held in physical order, put in role order (or back): swapped when left.
    template<class T> void Order(T (&pair)[2]) const {if(left)std::swap(pair[0],pair[1]);}
    constexpr bool operator==(const Map&) const = default;
};
// What the equipped weapon is held as, for the roles.
enum class Kind { OneHand, Bow, Crossbow, Staff, Polearm, Spear, TwoHands };
constexpr Kind KindOf(uint32_t weaponType,uint32_t twoHanded) {
    switch(weaponType) {
        case D2RVR_TYPE_BOW:return Kind::Bow;
        case D2RVR_TYPE_CROSSBOW:return Kind::Crossbow;
        case D2RVR_TYPE_STAFF:return Kind::Staff;
        case D2RVR_TYPE_POLEARM:return Kind::Polearm;
        case D2RVR_TYPE_SPEAR:return Kind::Spear;
        // Any other weapon with both hands on it (a great sword or axe, a maul). A Barbarian's
        // two-handed sword carried in one hand is one-handed: the game's own bit says which.
        default:return (twoHanded&D2RVR_TWO_HANDS_ON)?Kind::TwoHands:Kind::OneHand;
    }
}
constexpr const char* Name(Kind kind) {
    switch(kind) {
        case Kind::Bow:return "bow";
        case Kind::Crossbow:return "crossbow";
        case Kind::Staff:return "staff";
        case Kind::Polearm:return "polearm";
        case Kind::Spear:return "spear";
        case Kind::TwoHands:return "two-handed weapon";
        default:return "one-handed";
    }
}
// The kinds left-handed play mirrors now. One-handed covers swords, axes, maces, scepters, wands,
// daggers, claws, orbs, javelins and throwing weapons, bare hands, a shield beside any of them, a
// weapon in each hand, and a shapeshift form's paws. Phase 3 returns true for the rest.
constexpr bool Mirrors(Kind kind) {return kind==Kind::OneHand;}
inline constexpr const char* MirroredKinds="one-handed weapons, shield, dual wield, throws, potions, bare hands";
inline constexpr const char* RightHandedKinds="bow, crossbow, staff, spear, polearm, two-handed grips";
struct State {
    bool wanted=false;      // the setting
    Kind kind=Kind::OneHand;
    Map map{};              // the roles in force: left only when wanted and the kind is mirrored
    constexpr bool Fallback() const {return wanted&&!map.left;}   // wanted, but this weapon plays right-handed for now
    constexpr bool operator==(const State&) const = default;
};
// The whole mode follows the weapon: a kind that is not mirrored yet plays right-handed in every
// respect (weapon and shield, triggers, grips, face buttons, sticks, haptics), exactly as with the
// setting off, so nothing is half swapped: a bow drawn by the hand that does not hold the string, or a
// trigger on the hand without the weapon.
constexpr State Resolve(bool wanted,uint32_t weaponType,uint32_t twoHanded) {
    State s;s.wanted=wanted;s.kind=KindOf(weaponType,twoHanded);s.map.left=wanted&&Mirrors(s.kind);return s;
}
// The interface and the class motions (phase 2), each with its own switch so one can be put back alone.
// All are mirrored now: with the main hand on the left,
//   Laser         the menus' and VR Settings' laser, its click (trigger) and grab (grip), on the left controller;
//   LeatherMenu   the leather menu and the journal held in the right hand, the reach over the right shoulder;
//   WristHud      the wrist HUD, the vitals, the wrist map and compass on the right wrist; the pulled-off map
//                 taken by the left hand's laser and grip;
//   ClassMotions  every motion as its mirror image: the hands exchanged and left and right reversed
//                 (spell_motion.h, Input::mirror).
// A system that returned false here would keep its physical hands whatever the roles (For): the laser on
// the right controller, the menu and the wrist HUD on the left, the motions right-handed. The face buttons
// and sticks are by role everywhere, menus included. The text follows the same roles (hand_text.h).
enum class System { Combat, Buttons, Haptics, SkillSlots, Laser, WristHud, LeatherMenu, ClassMotions };
constexpr bool Mirrored(System system) {
    switch(system) {
        case System::Combat:case System::Buttons:case System::Haptics:case System::SkillSlots:
        case System::Laser:case System::WristHud:case System::LeatherMenu:case System::ClassMotions:return true;
        default:return false;
    }
}
// For the log (Frame left-handed:).
inline constexpr const char* MirroredSystems="class motions, menu laser, leather menu and journal, wrist HUD and map, floating map, labels and text";
// The map a system uses under a state: the roles, or right-handed while it is not built.
constexpr Map For(System system,const State& state) {return Mirrored(system)?state.map:Map{};}
// Dominant hand's behaviours (the shoulder weapon swap's hand, the monster outline's first hand, the
// loot pick's pointing hand; the left-shoulder reach for the leather menu off): Left-handed turns
// them all to the left hand with the one switch, whatever Dominant hand says and whatever the weapon
// (the swap hand must not change as the swap changes the weapon). Physical: 0 right, 1 left.
constexpr int Dominant(bool leftHanded,int dominantSetting) {return leftHanded||dominantSetting==1?1:0;}
}
