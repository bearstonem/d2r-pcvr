#pragma once
#include <cmath>
#include "skill_slots.h"
#include <cstdint>
#include <string_view>

namespace framevr::weaponskills {
inline constexpr int Attack=0,Throw=2,Smite=97,Charge=107,BlessedHammer=112,HolyShield=117,FistOfHeavens=121,Strafe=26,DoubleThrow=140,Werewolf=223,Werebear=228,
                     MultipleShot=12,GuidedArrow=22;
enum class Action { Native, Arrow, Volley, Shield };
inline Action Classify(int skill) {
    switch(skill) {
        case 6:case 7:case 11:case 12:case 16:case 21:case 22:case 27:case 31:return Action::Arrow;
        case Strafe:return Action::Volley;
        case Smite:return Action::Shield;
        default:return Action::Native;
    }
}
inline bool Bow(int skill) {return Classify(skill)==Action::Arrow||Classify(skill)==Action::Volley;}
// Single-missile arrow skills: Skills.txt srvdofunc 0 with one srvmissile,
// the same native shape as the hand bolts. Multiple Shot, Guided Arrow and
// Strafe have their own server functions and keep the native button path.
inline bool QuiverSkill(int skill) {
    switch(skill) {
        case 6:case 7:case 11:case 16:case 21:case 27:case 31:return true;
        default:return false;
    }
}
// Their missiles (Missiles.txt): magicarrow, firearrow, icearrow, coldarrow,
// explodingarrow, immolationarrow, freezingarrow.
inline bool QuiverMissile(int missile) {
    switch(missile) {
        case 12:case 27:case 28:case 40:case 41:case 85:case 87:return true;
        default:return false;
    }
}
// Javelin skills (#224): Skills.txt srvstfunc 4 with one srvmissile, the
// same native shape as Fire Arrow, and a javelin used up per throw: Poison
// Javelin (missile 38, poisonjav), Lightning Bolt (205, lightningjavelin),
// Plague Javelin (43, plaguejavelin) and Lightning Fury (206, lightningfury).
inline bool JavelinSkill(int skill) {
    switch(skill) {
        case 15:case 20:case 25:case 35:return true;
        default:return false;
    }
}
inline bool JavelinMissile(int missile) {
    switch(missile) {
        case 38:case 205:case 43:case 206:return true;
        default:return false;
    }
}
// Throwing potions (Weapons.txt type tpot): Rancid Gas, Oil, Choking Gas,
// Exploding, Strangling Gas and Fulminating Potion. Their missiles (Missiles.txt
// 44-49, CollideType 6) pass over monsters, stop at walls and burst where they
// land, so a hand throw picks the spot like a tossed trap (toss.h). By item
// code, for when the game's ItemTypes has no tpot row (gamestate.cpp).
inline bool ThrowingPotion(std::string_view code) {
    while(!code.empty()&&code.back()==' ')code.remove_suffix(1);
    return code=="gps"||code=="ops"||code=="gpm"||code=="opm"||code=="gpl"||code=="opl";
}
// A trigger's skills that throw the hero's potion: Throw, or Attack
// (Skills.txt leaves tpot out of Attack, so the game is expected to throw).
inline bool PotionThrow(int skill){return skill==Attack||skill==Throw;}
// A throwing potion in either hand (owner, 2026-11-10: "physical throwing of potions only seems to work if
// they're equipped in the primary hand ... make it so they both work the same way"). Hands in role order
// (hand_role.h): 0 main, 1 off; a hand holds what its own inventory arm holds (body location 4 main, 5 off).
// Each potion hand throws its own with one native press of a trigger slot whose skill throws that potion:
//   the main hand's: Attack or Throw (the game throws the primary weapon);
//   the off hand's, alone (the main hand empty or a shield: it is the hero's weapon): Attack, Throw or Left
//     Hand Throw; beside a weapon or another potion in the main hand: Left Hand Throw only (Skills.txt 4, the
//     native Throw's twin for the other hand), as Attack and Throw would throw the main hand's.
// The hand's own trigger arms its throw when it holds such a skill; else, for a potion that is the hero's
// only weapon, the other trigger does when it holds one (the press that threw it natively before): the
// potion's hand still makes the throw.
inline constexpr int LeftHandThrow=4;
enum class Held:uint8_t {None,Shield,Weapon,Potion};
inline bool Armed(Held held){return held==Held::Weapon||held==Held::Potion;}
inline bool PotionThrow(int hand,const Held held[2],int skill) {
    if(hand<0||hand>1||held[hand]!=Held::Potion)return false;
    if(hand==0)return PotionThrow(skill);
    return Armed(held[0])?skill==LeftHandThrow:PotionThrow(skill)||skill==LeftHandThrow;
}
// trigger[hand]: the trigger (0 main, 1 off) that arms that hand's potion throw, -1 none. A trigger arms
// one hand only. skill[t]: trigger t's skill, -1 when it is not known, not available or not allowed.
struct PotionRoute {
    int trigger[2]{-1,-1};
    int Hand(int t) const {return trigger[0]==t?0:trigger[1]==t?1:-1;}   // the hand trigger t throws, -1 none
    bool operator==(const PotionRoute&) const=default;
};
inline PotionRoute RoutePotions(const Held held[2],const int skill[2]) {
    PotionRoute route;
    for(int hand=0;hand<2;++hand)if(PotionThrow(hand,held,skill[hand]))route.trigger[hand]=hand;
    // The hero's only weapon, its own trigger holding another skill: the other trigger's Attack or Throw.
    for(int hand=0;hand<2;++hand) {
        const int other=1-hand;
        if(route.trigger[hand]<0&&held[hand]==Held::Potion&&!Armed(held[other])&&route.Hand(other)<0&&PotionThrow(skill[other]))route.trigger[hand]=other;
    }
    return route;
}
// What a physical release can carry: a bow's arrow skills; a thrown weapon's
// javelin skills, and the Barbarian's Double Throw (Skills.txt srvdofunc 74,
// no srvmissile: each throw flies the throwing hand's own weapon).
inline bool Loadable(int skill,int missile,bool javelin) {
    return javelin?(JavelinSkill(skill)&&JavelinMissile(missile))||skill==DoubleThrow:QuiverSkill(skill)&&QuiverMissile(missile);
}
// The bow skills with their own server functions - Multiple Shot's fan, Guided
// Arrow's homing, Strafe's volley - load from a face button too (#364: "when i
// swapped that to multishot, its only fire on button press"). They never ride
// the physical arrow: each following draw and release presses their button
// natively, as the left trigger's bow skill does, so the game keeps the fan,
// the homing and the volley.
inline bool NativeLoad(int skill) {return skill==MultipleShot||skill==GuidedArrow||skill==Strafe;}
// Bow quiver: pressing a face button assigned to a quiver skill loads that
// skill into every following nocked arrow instead of casting it. Pressing the
// loaded skill's button again returns to basic arrows; another skill swaps.
// The press is consumed while held. Native mana, ammunition and skill checks
// stay authoritative at each release.
struct Loaded {
    int skill=-1,owner=-1,missile=-1;
    bool Active() const {return skill>=0;}
    bool operator==(const Loaded&) const=default;
};
class Quiver {
    uint64_t session=0;
    Loaded loaded{};
    int from=-1;      // the face slot that loaded it (a native load presses it at each release)
    int held=-1;      // slot of the current press; release rearms it
    bool owned=false; // that press toggled the quiver and never casts
public:
    enum class Change {None,Loaded,Unloaded};
    struct Result {bool consumed=false;Change change=Change::None;};
    const Loaded& Current() const {return loaded;}
    int Slot() const {return loaded.Active()?from:-1;}
    void Clear(){loaded={};from=-1;held=-1;owned=false;}
    // slot: the face button currently held (-1 for none); its native assignment
    // follows. eligible: a bow (or, javelin, a thrown weapon) is ready and the
    // feature is enabled. A press keeps its first owner until released: a stale
    // frame mid-press neither leaks a native cast nor toggles again.
    Result Step(uint64_t world,int slot,int skill,int owner,int missile,bool eligible,bool javelin=false) {
        if(world!=session){session=world;Clear();}
        Result out;
        if(slot<0){held=-1;owned=false;return out;}
        if(slot==held){out.consumed=owned;return out;}
        held=slot;owned=eligible&&(Loadable(skill,missile,javelin)||(!javelin&&NativeLoad(skill)));
        if(!owned)return out;
        out.consumed=true;
        if(loaded.skill==skill&&loaded.owner==owner){loaded={};from=-1;out.change=Change::Unloaded;}
        else {loaded={skill,owner,missile};from=slot;out.change=Change::Loaded;}
        return out;
    }
};
inline const char* Hint(int skill,bool bow,bool shield) {
    if(bow&&Bow(skill))return skill==Strafe?"Release for volley":"Draw & release";
    if(shield&&skill==Smite)return "Shield swing / thrust";
    return "Point & trigger";
}
struct ContactContext {
    uint64_t session=0,epoch=0,assignment=0;
    uint32_t equipment=0;
    int slot=-1,owner=-1;
    bool operator==(const ContactContext&) const=default;
};
inline bool Current(const ContactContext& saved,const ContactContext& current,uint64_t at,uint64_t now,uint64_t maxAge=1500) {
    return saved.session&&saved.slot>=0&&saved.slot<skills::Count&&saved==current&&now>=at&&now-at<=maxAge;
}
// One brief native button press per physical release. Native attack state,
// skill availability, mana/ammunition and sequence timing stay authoritative.
class Pulse {
    int slot=-1,id=-1,owner=-1;
    uint64_t context=0;
    double started=0,until=0;
public:
    void Cancel(){*this=Pulse{};}
    bool Start(int nextSlot,int nextId,int nextOwner,uint64_t nextContext,double now,double hold=0) {
        if(nextSlot<0||nextSlot>=skills::Count||nextId<0||!std::isfinite(now)||now<until)return false;
        // Shield contact uses the left trigger's alternate bank. SkillPad
        // needs 50 ms at the native consumer before exposing its button; a
        // 65 ms producer pulse can disappear between the game's polls.
        // Allow bank preparation plus two native ticks and XR scheduling.
        // FinishShieldSkill still cancels immediately when Smite executes.
        // Every alternate-bank (LT) slot needs the same: a bow skill loaded
        // from slot 7 fired about half its releases at 35 pairs/s on
        // 2026-11-09 (the LT went down and up again before its button was
        // ever exposed). Glyph casts already hold LT slots this long.
        const double duration=hold>0?hold:nextId==Smite||skills::Alternate(nextSlot)?.180:.065;
        slot=nextSlot;id=nextId;owner=nextOwner;context=nextContext;started=now;until=now+duration;return true;
    }
    int Get(int currentSlot,int currentId,int currentOwner,uint64_t currentContext,double now) {
        if(!std::isfinite(now)||now<started||now>=until||currentSlot!=slot||currentId!=id||currentOwner!=owner||currentContext!=context) {
            Cancel();return -1;
        }
        return slot;
    }
};
}
