#pragma once
#include <algorithm>
#include <cstdint>
// Melee-skill contact adapter (Cal, 2026-10-22/23). A melee skill on a hand's
// trigger is that hand's strike skill: a landed physical strike casts it
// natively on the creature it struck, instead of the basic attack. The cast
// runs the skill's own server start and do functions (the hand-spell
// pipeline, without an animation), so its mana, damage, states and charges
// stay native; the strike decides only whether and on whom it lands.
// The Druid's form skills came first (Cal); the single-hit weapon skills
// followed (#223, 2026-10-23). Each further one is added here once verified.
namespace framevr::strikeskills {
inline constexpr int FeralRage=232,Maul=233,Rabies=238,FireClaws=239,Hunger=242,Fury=248;
inline constexpr int PowerStrike=14,Impale=19,ChargedStrike=24,LightningStrike=34,PoisonDagger=73,Sacrifice=96,Zeal=106,Vengeance=111,
                     Conversion=116,Bash=126,DoubleSwing=133,Stun=139,Concentrate=144,Frenzy=147,Berserk=152;
// The Skills.txt srvstfunc / srvdofunc each skill must still have in its
// compiled row (D2R skills data; D2MOO for the logic). A row that differs (a
// game patch, a mod) keeps the basic attack. The Druid's single-hit form skills:
//  - Maul / Feral Rage: start 56 pre-rolls the hit, do 120 resolves it and adds the stack;
//  - Rabies: start 57 (refuses on a Classic miss), do 121 (poison that spreads);
//  - Fire Claws: start 58, do 2 (the shared pre-rolled melee resolve);
//  - Hunger: no start function (0), do 122 rolls and resolves; the do
//    dispatcher pays its mana.
//  - Fury: start 37 sets the cast's hit count and first target, do 13 lands
//    one hit per call (multi-hit: Sequence below).
// The weapon skills that strike once a cast (#223), each with its own start:
//  - Bash, Stun, Concentrate and Conversion 32, Power Strike and Charged
//    Strike 6, Impale 7, Lightning Strike 10, Poison Dagger 16, Sacrifice 29,
//    Vengeance 35, Berserk 39;
//  - do 2, the shared melee resolve, or the skill's own: Charged Strike 11
//    (its bolts), Lightning Strike 14 (its chain), Poison Dagger 32,
//    Sacrifice 64, Conversion 79.
// Zeal is Fury's start 37 and do 13 with a weapon: multi-hit (Sequence below).
// Double Swing (start 68, do 70) and Frenzy (start 67, do 9) swing each of two
// weapons once a cast: dual strike skills (DualCast below, Cal 2026-11-03).
// Jab, Fend and Whirlwind hit more than once a cast with functions of their
// own, and Charge runs first: not (yet) strike skills.
struct Row {int skill,start,action;};
inline constexpr Row Supported[]={{Maul,56,120},{FeralRage,56,120},{Rabies,57,121},{FireClaws,58,2},{Hunger,0,122},{Fury,37,13},
    {Bash,32,2},{Stun,32,2},{Concentrate,32,2},{Berserk,39,2},{Sacrifice,29,64},{Vengeance,35,2},
    {PowerStrike,6,2},{Impale,7,2},{ChargedStrike,6,11},{LightningStrike,10,14},{PoisonDagger,16,32},{Zeal,37,13},{Conversion,32,79},
    {DoubleSwing,68,70},{Frenzy,67,9}};
inline bool Known(int skill) {
    for(const auto& r:Supported)if(r.skill==skill)return true;
    return false;
}
inline bool Matches(int skill,int start,int action) {
    for(const auto& r:Supported)if(r.skill==skill)return r.start==start&&r.action==action;
    return false;
}
// The skill a hand's strikes carry: its trigger skill, when skill gestures are
// on, the assignment is current and the row was confirmed. -1: basic attacks.
inline int Armed(bool gestures,bool fresh,bool known,bool available,bool strike,int skill) {
    return gestures&&fresh&&known&&available&&strike&&Known(skill)?skill:-1;
}
// Which trigger the left hand's strikes follow, [gameplay] left_hand_strikes
// (the skill wheel's lower-left box): 0 its own (left) trigger, 1 the right
// trigger's skill (a skill sits on one trigger only, so this is how both claws
// carry it). Basic strikes need no choice: leave the left trigger without a
// strike skill.
enum class LeftStrikes:uint8_t {Own=0,SameAsRight=1};
inline LeftStrikes LeftChoice(float value) {
    return int(value+.5f)==1?LeftStrikes::SameAsRight:LeftStrikes::Own;
}
// The trigger hand whose skill this hand's strikes carry.
inline int StrikeTrigger(int hand,LeftStrikes left) {
    if(hand!=1)return 0;
    return left==LeftStrikes::SameAsRight?0:1;
}
// Native use state (D2Common SKILLS_GetUseState, D2R 33F360): the packet
// handler's own check of weapon type, mana, shapeshift form and skill delay,
// which the server start function does not repeat.
enum class Refusal:uint8_t {None,Mana,Weapon,Form,Delay,Other,Count};
inline Refusal FromUseState(int state) {
    switch(state) {
        case 0:return Refusal::None;     // usable
        case 1:return Refusal::Mana;     // no mana
        case 2:return Refusal::Weapon;   // no quantity: wrong weapon type
        case 4:return Refusal::Form;     // shapeshift restriction
        case 8:return Refusal::Delay;    // blocked by the skill delay
        default:return Refusal::Other;   // disabled, passive, aura, no level
    }
}
// Which landed contact casts. Unleashed: only a full-cycle stroke's first
// landed contact (the on-hit gate's rule); faster strokes and cleave
// follow-ups stay recovery-scaled basic attacks. Classic: every contact the
// shared native cadence lets through, one per attack cycle.
inline bool Casts(int skill,bool classic,bool fullCycleFirst) {
    return skill>=0&&(classic||fullCycleFirst);
}
// Multi-hit strike skills: Fury and Zeal (Cal, 2026-10-25: "no longer have the
// player act out the entire attack"). The casting stroke (the rule above) pays
// the mana and lands hit 1 on the creature it struck; the mod then lands the
// cast's other hits itself, at the skill's native pace, each on the nearest
// other creature in reach as the native sequence does (none: the cast ends).
// rev2 (Cal: "the player stays locked in position for duration of attack"):
// the hero is never put in the skill's mode, so movement stays free; rev1 asked
// the server for the native sequence, which roots the hero for its length.
// Strokes while it runs stay basic attacks (Cal: "land basic"); the next
// full-cycle stroke after it ends casts again. Its later hits are the cast's own
// (rev3): whole, with on-hit effects, attack rating still scaling them, as the
// old flurry's follow-ups were; not penalised as trigger hits, but each is the
// latest attack swings are measured from (rev5), so swings during it stay scaled.
inline bool MultiHit(int skill) {return skill==Fury||skill==Zeal;}
// Pace (rev4, Cal: "native damage and animation speed"): the native sequence's
// own rhythm, measured from rev1, which ran it natively (2026-10-25 21:08-21:14,
// trigger hits of 19 Fury and 10 Zeal casts): Fury at a 360 ms attack cycle
// landed a hit every 156-163 ms (4 of its 9 animation frames), Zeal at 560 ms
// every 269-284 ms (7 of 14). The animation's speed scales the whole cycle, so
// the gap is that share of the hero's current cycle: Fury 4/9, Zeal 1/2. The
// swing itself is the wind-up (natively hit 1 came 240-280 ms after the cast
// started), so hit 2 follows hit 1 by one gap. An unknown cycle is
// SequenceUnknownCycleMs; never sooner than SequenceMinGapMs.
inline constexpr uint64_t SequenceUnknownCycleMs=500,SequenceMinGapMs=60;
struct SequenceShare {uint32_t frames,cycleFrames;};
inline SequenceShare SequenceShareOf(int skill) {return skill==Zeal?SequenceShare{1,2}:SequenceShare{4,9};}
inline uint64_t SequenceGap(uint32_t cycle,int skill) {
    const auto share=SequenceShareOf(skill);
    const uint64_t c=cycle?uint64_t(cycle):SequenceUnknownCycleMs;
    return std::max<uint64_t>(SequenceMinGapMs,(c*share.frames+share.cycleFrames/2)/share.cycleFrames);
}
// A due hit runs on the game thread's next update (40 ms apart): it may run
// this early, and the next one is timed from when this one was due, so the
// rhythm holds instead of drifting late by a tick each hit.
inline constexpr uint64_t SequenceEarlyMs=20;
// One cast's remaining hits (game thread).
class Sequence {
    uint64_t session=0,nextAt=0,gap=0;int skill=-1,owner=0,form=-1,total=0,left=0;uint32_t last=0;
public:
    // The cast landed hit 1 of `hits` on `struck` at `now`.
    void Begin(uint64_t s,int sk,int skillOwner,int shape,int hits,uint32_t cycle,uint64_t now,uint32_t struck) {
        session=s;skill=sk;owner=skillOwner;form=shape;total=hits>0?hits:0;left=total>1?total-1:0;
        gap=SequenceGap(cycle,sk);nextAt=now+gap;last=struck;
    }
    bool Open(uint64_t s) const {return left>0&&s==session;}
    // When the next hit is due; 0: none.
    uint64_t DueAt() const {return left>0?nextAt:0;}
    bool Due(uint64_t s,uint64_t now) const {return Open(s)&&now+SequenceEarlyMs>=nextAt;}
    // A game or form change ends it (the hero is no longer that attack's).
    bool Lapse(uint64_t s,int shape) {
        if(left<=0||(s==session&&shape==form))return false;
        left=0;return true;
    }
    // The next hit ran on `struck`; returns its ordinal (2..total). The one after
    // is due a gap after this one was due (never less than half a gap from now).
    int Hit(uint64_t now,uint32_t struck) {
        if(left>0)--left;
        last=struck;nextAt=std::max(nextAt+gap,now+gap/2);return total-left;
    }
    void End() {left=0;}
    int Skill() const {return skill;}
    int Owner() const {return owner;}
    int Total() const {return total;}
    int Left() const {return left;}
    uint64_t Gap() const {return gap;}
    uint32_t Last() const {return last;}   // the creature the previous hit struck
};
// Dual strike skills: Double Swing and Frenzy (Cal, 2026-11-03). Natively one cast
// swings each of two weapons once (Skills.txt weapsel 3, sequence 11): hit 1, then
// hit 2 about 120-190 ms later on the next creature in reach, or the same one when
// it is alone. In VR one cast is one swing of each blade (decision A): a full-cycle
// stroke with either blade starts the cast (mana, use check, start function) and
// lands hit 1 with that blade's weapon; the OTHER blade's first landed contact before
// the cast's beat ends lands hit 2 with its weapon, on whatever it struck (no second
// mana, whole with its on-hit effects as a cast's own hits are). No second contact
// by then: hit 2 is forfeited, so a cast never lands more than native. Each hit is
// the blade that struck it (decision 2); the beat while one is armed is the skill's
// own cast length (attack_speed.h DualSequenceFrames).
inline bool DualStrike(int skill) {return skill==DoubleSwing||skill==Frenzy;}
class DualCast {
    uint64_t session=0,closesAt=0;int skill=-1,blade=-1,owner=0;bool open=false;
public:
    // blade (rendered hand 0/1) cast `sk` (its skill owner) at `now`; its beat ends castMs later.
    void Begin(uint64_t s,int sk,int bladeHand,uint64_t now,uint32_t castMs,int skillOwner=0) {
        session=s;skill=sk;blade=bladeHand;owner=skillOwner;closesAt=now+(castMs?castMs:500);open=true;
    }
    int Owner() const {return owner;}
    bool Open(uint64_t s,uint64_t now) const {return open&&s==session&&now<closesAt;}
    // The other blade's first landed contact while it is open: true = it lands hit 2 (closes it).
    bool Second(uint64_t s,int bladeHand,uint64_t now) {
        if(!Open(s,now)||bladeHand==blade)return false;
        open=false;return true;
    }
    // Once the beat has ended unclaimed: true once (a forfeited hit 2).
    bool Forfeit(uint64_t s,uint64_t now) {
        if(!open||(s==session&&now<closesAt))return false;
        open=false;return s==session;
    }
    void End() {open=false;}
    int Skill() const {return skill;}
    int Blade() const {return blade;}
};
// A refused cast lands the strike as a basic attack (Cal, 2026-10-22); a mana
// refusal also asks for the native "I need mana" voice. A start function that
// refuses after the use check (a Classic miss for some skills) is the native
// outcome and is not retried as a basic attack: no second roll.
inline bool Voice(Refusal r) {return r==Refusal::Mana;}
// The form's own attack voice (MonSounds werewolf / werebear). Natively every
// attack animation in form plays Attack1, a growl, with Att1Prb % chance (wolf
// 85, bear 80), and the bite animation (Rabies, Hunger: anim S3) plays Skill3,
// the bite. Strikes skip the animation, so the voice is played here once per
// stroke that natively is one attack: a full-cycle stroke's first landed
// contact in Unleashed, every paced contact in Classic; basic or cast alike.
// Rows are sounds.txt groups that redirect to their HD group, so the game
// picks the variant (as object_glass_break_1 does for the potion toss).
inline constexpr int WolfGrowl=857,WolfBite=866,BearGrowl=838,BearBite=847;   // druidwerewolf/werebear _attack_1 / _bite_1
inline constexpr int WolfGrowlChance=85,BearGrowlChance=80;
inline bool Bites(int skill) {return skill==Rabies||skill==Hunger;}
// form: 1 Werewolf, 2 Werebear (framevr::shapeshift). cast: the skill this
// stroke cast, -1 for a basic strike. roll: 0..99. Returns a sounds.txt row, 0 none.
inline int FormVoice(int form,bool classic,bool fullCycleFirst,int cast,int roll) {
    if((form!=1&&form!=2)||!(classic||fullCycleFirst))return 0;
    if(Bites(cast))return form==1?WolfBite:BearBite;
    return roll<(form==1?WolfGrowlChance:BearGrowlChance)?(form==1?WolfGrowl:BearGrowl):0;
}
}
