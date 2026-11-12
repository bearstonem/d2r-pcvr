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
//             and its skill slots, A / B and X / Y, the turn stick and the walk stick (unless they stay
//             right-handed: Sticks), the melee lanes.
// Right-handed the two are the same number, which is why the engines (gameplay.h, hand_controls.h,
// interactions.h, melee_contact.h) say "right = 0": they take ROLE order and need no change. The
// transport feeds them through a Map and sends what comes back (a pulse, a pose) to the physical hand.
//
// Built in phases. What a phase has not built keeps playing right-handed, whole: never half swapped.
//   phase 1 (this file's Mirrors): one-handed weapons, a shield, dual wield, throws, potions, bare hands;
//   phase 2 (Mirrored(System)): the laser and the menus, the wrist HUD and map, the leather menu,
//           the class motions, and every text that names a hand (hand_text.h): built, all on;
//   phase 3 (Mirrors): bows, crossbows, staves, spears, polearms and other two-handed grips - built:
//           the engines take them by role as they are (gameplay.h: the bow is hand[1], the string
//           hand[0]), and the body poses them in a mirror world (vr/hand_swap_pose.h).
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
// The kinds left-handed play mirrors now, one switch per kind. One-handed covers swords, axes, maces,
// scepters, wands, daggers, claws, orbs, javelins and throwing weapons, bare hands, a shield beside any
// of them, a weapon in each hand, and a shapeshift form's paws. Since phase 3 every kind is mirrored,
// so the roles no longer follow the weapon: taking up or putting away a bow or a staff never moves a
// stick or a button. A kind turned off here falls back whole again (Resolve) and the log says so.
constexpr bool Mirrors(Kind kind) {
    switch(kind) {
        case Kind::OneHand:return true;
        case Kind::Bow:return true;        // the bow in the right hand, the left draws the string
        case Kind::Crossbow:return true;   // the stock in the left hand, the right supports it
        case Kind::Staff:return true;      // the left hand drives it, the right takes the shaft
        case Kind::Polearm:return true;
        case Kind::Spear:return true;
        case Kind::TwoHands:return true;   // a great sword's hilt, an axe's or a maul's haft
    }
    return false;
}
// How a kind is held left-handed, for the log.
constexpr const char* HeldLeftHanded(Kind kind) {
    switch(kind) {
        case Kind::Bow:return "the bow in the RIGHT hand (off hand: bow bash, the draw's pulse), the LEFT hand draws the string with the main trigger";
        case Kind::Crossbow:return "the stock in the LEFT hand (main trigger fires), the RIGHT hand supports it with its grip";
        case Kind::Staff:return "the LEFT hand drives the staff and aims it, the RIGHT hand takes the shaft with its grip";
        case Kind::Polearm:case Kind::Spear:return "the LEFT hand drives it, the RIGHT hand slides along the shaft with its grip";
        case Kind::TwoHands:return "the LEFT hand drives it, the RIGHT hand takes the hilt or haft with its grip";
        default:return "the weapon in the LEFT hand, a shield or second weapon on the RIGHT";
    }
}
inline constexpr const char* MirroredKinds="one-handed weapons, shield, dual wield, throws, potions, bare hands, bow, crossbow, staff, spear, polearm, two-handed grips";
inline constexpr const char* RightHandedKinds="no weapon kind";
// The two thumbsticks. Every reader of a stick takes the pad's two (menus::Pad):
//   walk stick   the pad's left stick: walking, the leather menu's walk, Room follows you's idle stick, the
//                native menus' left stick and its click; Walk direction: Controller follows its controller;
//   turn stick   the pad's right stick: smooth and snap turns, up for the Stick toggle's run / walk, held down
//                for every loot name, the wrist map's and the floating map's zoom, the floating map's push and
//                pull, the native menus' right stick and its click (held: sort); its gestures buzz its controller.
// Right-handed the left controller walks and the right one turns. Left-handed the off hand walks and the main
// hand turns: the right and the left controller. Sticks stay right-handed (VR Settings > Controls > Hands, shown
// with Left-handed on; [interactions] left_handed_sticks_right; asked for by left-handed players used to walking
// with the left thumb) keeps them where a right-handed player has them while everything else stays mirrored.
// A hand's own stick click is the hand's, not the stick's, in every combination (OwnStick): it opens that hand's
// wheel, and the off hand's grip with its click opens the inventory (without motion controls the click alone
// is the map).
struct Sticks {
    int walk=1,turn=0;   // the physical hand (0 right, 1 left) whose controller has each
    // The pad stick (0 the right one, turning; 1 the left one, walking) on a physical hand's controller.
    constexpr int Pad(int physical) const {return physical==walk?1:0;}
    // The physical hand whose controller has a pad stick.
    constexpr int Hand(int pad) const {return pad?walk:turn;}
    constexpr bool operator==(const Sticks&) const = default;
};
constexpr Sticks SticksOf(const Map& map,bool keepRight) {return keepRight?Sticks{}:Sticks{map.Physical(1),map.Physical(0)};}
constexpr const char* Side(int physical) {return physical?"LEFT":"RIGHT";}
struct State {
    bool wanted=false;      // the setting
    bool sticksRight=false; // Sticks stay right-handed, the setting (no effect while the roles are right-handed)
    Kind kind=Kind::OneHand;
    Map map{};              // the roles in force: left only when wanted and the kind is mirrored
    Sticks sticks{};        // the sticks in force (SticksOf)
    constexpr bool Fallback() const {return wanted&&!map.left;}   // wanted, but this weapon plays right-handed for now
    // The sticks are not where the roles would put them: left-handed with Sticks stay right-handed.
    constexpr bool SticksKept() const {return map.left&&sticksRight;}
    // The role (0 main, 1 off) whose controller has a pad stick (0 turn, 1 walk).
    constexpr int StickRole(int pad) const {return map.Role(sticks.Hand(pad));}
    // The pad stick (0 the right, 1 the left) on a role's own controller: that hand's stick click.
    constexpr int OwnStick(int role) const {return sticks.Pad(map.Physical(role));}
    constexpr bool operator==(const State&) const = default;
};
// A kind that is not mirrored (none since phase 3; Mirrors) plays right-handed in every respect
// (weapon and shield, triggers, grips, face buttons, sticks, haptics), exactly as with the setting
// off, so nothing is half swapped: a bow drawn by the hand that does not hold the string, or a
// trigger on the hand without the weapon.
constexpr State Resolve(bool wanted,uint32_t weaponType,uint32_t twoHanded,bool sticksRight=false) {
    State s;s.wanted=wanted;s.sticksRight=sticksRight;s.kind=KindOf(weaponType,twoHanded);s.map.left=wanted&&Mirrors(s.kind);
    s.sticks=SticksOf(s.map,sticksRight);return s;
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
// are by role everywhere, menus included, and so are the sticks unless they stay right-handed (Sticks). The
// text follows the same roles (hand_text.h).
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
// The dominant hand's behaviours (the shoulder weapon swap's hand, the monster outline's first hand, the
// loot pick's pointing hand): the right hand, and with Left-handed on the left, whatever the weapon
// (the swap hand must not change as the swap changes the weapon). Physical: 0 right, 1 left.
// There was a Dominant hand setting beside Left-handed until 2026-11-10; the one switch does it now.
constexpr int Dominant(bool leftHanded) {return leftHanded?1:0;}
}
