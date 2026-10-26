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
                     Conversion=116,Bash=126,Stun=139,Concentrate=144,Berserk=152;
// The Skills.txt srvstfunc / srvdofunc each skill must still have in its
// compiled row (D2R skills data; D2MOO for the logic). A row that differs (a
// game patch, a mod) keeps the basic attack. The Druid's single-hit form skills:
//  - Maul / Feral Rage: start 56 pre-rolls the hit, do 120 resolves it and adds the stack;
//  - Rabies: start 57 (refuses on a Classic miss), do 121 (poison that spreads);
//  - Fire Claws: start 58, do 2 (the shared pre-rolled melee resolve);
//  - Hunger: no start function (0), do 122 rolls and resolves; the do
//    dispatcher pays its mana.
//  - Fury: start 37 sets the cast's hit count and first target, do 13 lands
//    one hit per call (multi-hit: Flurry below).
// The weapon skills that strike once a cast (#223), each with its own start:
//  - Bash, Stun, Concentrate and Conversion 32, Power Strike and Charged
//    Strike 6, Impale 7, Lightning Strike 10, Poison Dagger 16, Sacrifice 29,
//    Vengeance 35, Berserk 39;
//  - do 2, the shared melee resolve, or the skill's own: Charged Strike 11
//    (its bolts), Lightning Strike 14 (its chain), Poison Dagger 32,
//    Sacrifice 64, Conversion 79.
// Zeal is Fury's start 37 and do 13 with a weapon: multi-hit (Flurry below).
// Jab, Fend, Frenzy, Double Swing and Whirlwind hit more than once a cast
// with functions of their own, and Charge runs first: not (yet) strike skills.
struct Row {int skill,start,action;};
inline constexpr Row Supported[]={{Maul,56,120},{FeralRage,56,120},{Rabies,57,121},{FireClaws,58,2},{Hunger,0,122},{Fury,37,13},
    {Bash,32,2},{Stun,32,2},{Concentrate,32,2},{Berserk,39,2},{Sacrifice,29,64},{Vengeance,35,2},
    {PowerStrike,6,2},{Impale,7,2},{ChargedStrike,6,11},{LightningStrike,10,14},{PoisonDagger,16,32},{Zeal,37,13},{Conversion,32,79}};
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
// Multi-hit strike skills (Cal, 2026-10-23, option A: "a werewolf swipe flurry
// feeds Fury's hits"). One native cast, its hits delivered by the strokes that
// follow. Zeal (2026-10-23, the owner: "add Zeal") is the same with a weapon.
// The casting stroke (the rule above) pays the mana and lands hit 1; each
// later landed stroke of a claw or weapon carrying the same skill, in the same form,
// lands the next hit on the creature it struck: the skill's own damage and
// on-hit effects, no mana, one hit per stroke (the stroke's first landed contact).
// Pace, measured from the flurry's previous hit (never from an early stroke):
//  - Unleashed (Cal, 2026-10-23 rev5: "make the next X hits be Fury hits once
//    the first is cast"): none. Every landed stroke takes the next hit, so no
//    swipe is lost; the burst limits itself, since a new cast still needs a
//    full-cycle stroke. Only one swipe's follow-through, which the sweep can
//    split into a second stroke 15-80 ms later, is kept from taking a second
//    hit: FlurryStrokeGapMs.
//  - Classic (native pacing): the skill's Param2 % of the attack cycle (Fury 70:
//    natively each middle hit comes after 70 % of the wind-up; Zeal 100).
// The unused hits lapse when none lands for two attack cycles; the next
// qualifying stroke casts anew.
// Spread (Cal, 2026-10-23 rev6, "most like native"): natively each hit after
// the first jumps to another creature nearby. A later stroke that reaches more
// than one creature lands Fury on up to FlurryHitsPerStroke of them, the first
// and the next one it touches, each a whole Fury hit; the casting stroke stays
// one hit. Further creatures in that stroke stay cleave basics.
inline bool MultiHit(int skill) {return skill==Fury||skill==Zeal;}
inline constexpr uint32_t FlurryWindowCycles=2,FlurryUnknownCycleMs=1500,FlurryStrokeGapMs=100;
inline constexpr int FlurryHitsPerStroke=2;
// A Param2 outside 10..100 (unread, a mod) paces one hit per full cycle.
inline int FlurryPace(int param2) {return param2>=10&&param2<=100?param2:100;}
class Flurry {
    uint64_t session=0,lastHit=0,stroke=0;int skill=-1,form=-1,total=0,left=0,pace=100,strokeHits=0;bool castStroke=false;
public:
    enum class Feed:uint8_t {None,TooSoon,Hit};
    // The cast landed its first hit at `now`; hits: the cast's whole count.
    // strokeKey: the casting stroke (StrokeKey).
    void Begin(uint64_t s,int sk,int shape,int hits,int param2,uint64_t now,uint64_t strokeKey=0) {
        session=s;skill=sk;form=shape;total=hits>0?hits:0;left=total>1?total-1:0;pace=FlurryPace(param2);lastHit=now;
        stroke=strokeKey;strokeHits=1;castStroke=true;
    }
    // One hand's stroke, as both claws number their strokes separately.
    static uint64_t StrokeKey(int hand,uint64_t strokeId) {return strokeId*2+uint64_t(hand&1)+2;}
    void End() {left=0;}
    bool Open(uint64_t s,int sk,int shape) const {return left>0&&s==session&&sk==skill&&shape==form;}
    // The least time since the previous hit before a stroke takes the next one.
    uint64_t Floor(uint32_t cycle,bool classic) const {return classic?uint64_t(cycle)*uint64_t(pace)/100:FlurryStrokeGapMs;}
    static uint64_t Window(uint32_t cycle) {return cycle?uint64_t(cycle)*FlurryWindowCycles:FlurryUnknownCycleMs;}
    // Closes a flurry whose window passed, or on a stroke in another form or game;
    // returns the hits it forfeits. shape: the form this stroke was made in.
    int Lapse(uint64_t s,uint64_t now,uint32_t cycle,int shape) {
        if(left<=0||(s==session&&shape==form&&(now<=lastHit||now-lastHit<=Window(cycle))))return 0;
        const int forfeited=left;left=0;return forfeited;
    }
    // What a landed stroke of this skill and form is to the open flurry. A
    // contact timed before the last hit (one batch applies out of order) is too soon.
    // strokeKey 0: no stroke spread (the pace rule only).
    Feed Feeds(uint64_t s,int sk,int shape,uint64_t now,uint32_t cycle,bool classic,uint64_t strokeKey=0) const {
        if(!Open(s,sk,shape))return Feed::None;
        if(strokeKey&&strokeKey==stroke)   // another creature in the stroke that took the last hit
            return !castStroke&&strokeHits<FlurryHitsPerStroke?Feed::Hit:Feed::TooSoon;
        return now>=lastHit&&now-lastHit>=Floor(cycle,classic)?Feed::Hit:Feed::TooSoon;
    }
    // This stroke already landed a Fury hit: its next creature is a spread hit, not a new swipe.
    bool SameStroke(uint64_t strokeKey) const {return strokeKey&&strokeKey==stroke;}
    // A follow-up hit ran (hit or miss: natively a miss spends the hit too); returns its ordinal.
    int Hit(uint64_t now,uint64_t strokeKey=0) {
        if(left>0)--left;
        lastHit=std::max(lastHit,now);
        if(strokeKey&&strokeKey==stroke)++strokeHits;else{stroke=strokeKey;strokeHits=1;castStroke=false;}
        return total-left;
    }
    int Total() const {return total;}
    int Left() const {return left;}
    uint64_t SinceHit(uint64_t now) const {return now>=lastHit?now-lastHit:0;}   // diagnostics
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
