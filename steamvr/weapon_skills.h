#pragma once
#include <cmath>
#include "skill_slots.h"
#include <cstdint>
#include <string_view>

namespace framevr::weaponskills {
inline constexpr int Attack=0,Throw=2,Smite=97,Strafe=26,DoubleThrow=140,Werewolf=223,Werebear=228;
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
// The right trigger's skills that throw the potion in hand: Throw, or Attack
// (Skills.txt leaves tpot out of Attack, so the game is expected to throw).
inline bool PotionThrow(int skill){return skill==Attack||skill==Throw;}
// What a physical release can carry: a bow's arrow skills; a thrown weapon's
// javelin skills, and the Barbarian's Double Throw (Skills.txt srvdofunc 74,
// no srvmissile: each throw flies the throwing hand's own weapon).
inline bool Loadable(int skill,int missile,bool javelin) {
    return javelin?(JavelinSkill(skill)&&JavelinMissile(missile))||skill==DoubleThrow:QuiverSkill(skill)&&QuiverMissile(missile);
}
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
    int held=-1;      // slot of the current press; release rearms it
    bool owned=false; // that press toggled the quiver and never casts
public:
    enum class Change {None,Loaded,Unloaded};
    struct Result {bool consumed=false;Change change=Change::None;};
    const Loaded& Current() const {return loaded;}
    void Clear(){loaded={};held=-1;owned=false;}
    // slot: the face button currently held (-1 for none); its native assignment
    // follows. eligible: a bow (or, javelin, a thrown weapon) is ready and the
    // feature is enabled. A press keeps its first owner until released: a stale
    // frame mid-press neither leaks a native cast nor toggles again.
    Result Step(uint64_t world,int slot,int skill,int owner,int missile,bool eligible,bool javelin=false) {
        if(world!=session){session=world;Clear();}
        Result out;
        if(slot<0){held=-1;owned=false;return out;}
        if(slot==held){out.consumed=owned;return out;}
        held=slot;owned=eligible&&Loadable(skill,missile,javelin);
        if(!owned)return out;
        out.consumed=true;
        if(loaded.skill==skill&&loaded.owner==owner){loaded={};out.change=Change::Unloaded;}
        else {loaded={skill,owner,missile};out.change=Change::Loaded;}
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
        const double duration=hold>0?hold:nextId==Smite?.180:.065;
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
