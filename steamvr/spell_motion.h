#pragma once
#include "frame_math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <iterator>
#include <vector>

// Spells cast by their motion (owner, 2026-11-03). Every Sorceress spell with a motion, unless the
// wheel's MOTIONS page turned it off, casts by it while both grips are held (owner: "we are tracking
// motion all the time so it gets triggered in unwanted situations", "what about holding both grips to
// cast?"): no button or glyph needed. The transport runs one watcher per motion (free: watched while
// active, the grips' hold) and casts the first to complete as a glyph casts (glyph_cast.h), so mana,
// cast rate and the spell stay native. Held, the motion again casts again.
//
// The first prototype readied a glyph's spell on the right trigger instead (Input::held, a gate
// pressed fresh, with a drop logged on its release: the tests still use it); the MOTIONS page retired
// that ("I don't want this to get over complicated").
//
// The Paladin's and the Druid's gestures (hammer_gesture.h, charge_gesture.h, cross_gesture.h,
// heavens_gesture.h, shapeshift_gesture.h) take the same gate and their own MOTIONS page (owner,
// 2026-11-04: "Both grips, like Sorceress"); their detectors stay their own (PageOf).
//
// The Amazon's, the Barbarian's, the Necromancer's and the Assassin's (owner, 2026-11-04: "also have
// multiple agents implement the motions", the table at claude.ai/artifact/MLr2xB4qEMTf8pzaTRKQ9T, codes
// A1-A4, B1-B8, N1-N8 with N6 Lower Resist, S1-S7) take the Sorceress's engine and gate: one watcher per
// motion, each class's detectors in its own header (motions_amazon.h, motions_barbarian.h,
// motions_necromancer.h, motions_assassin.h; ClassDone at the end of this file).
//
// Head-local frame as holycross (+X right, +Y up from the eyes, forward -Z, the head's yaw only); hand
// 0 is the right. Value-only: steamvr/spell_motion_test.cpp.
namespace framevr::spellmotion {
enum class Motion {None,FlingOut,PushDown,RubSnap,CrossGuard,ArmUp,MatchStrike,ForearmSweep,Shake,Hug,Punch,
    InnerSight,SlowMissiles,Decoy,Valkyrie,                                                       // Amazon A1-A4
    BattleOrders,Shout,BattleCommand,WarCry,Howl,BattleCry,Leap,FindItem,                         // Barbarian B1-B8
    BoneArmor,RaiseSkeleton,CorpseExplosion,Golem,AmplifyDamage,LowerResist,PoisonNova,BonePrison,// Necromancer N1-N8
    BurstOfSpeed,Fade,CloakOfShadows,Venom,BladeShield,ShadowWarrior,MindBlast,                   // Assassin S1-S7
    End};
constexpr int SorceressCount=int(Motion::Punch);   // the Sorceress's, 1..SorceressCount ([motions] off: her bits)
constexpr int MotionCount=int(Motion::End)-1;      // every class's, 1..MotionCount (a watcher each)
constexpr int MaxCards=10;                         // a MOTIONS page's cards (interaction::MotionCards), one bit each
inline Motion For(int skill) {
    switch(skill) {
    case 48:return Motion::FlingOut;      // Nova
    case 44:return Motion::PushDown;      // Frost Nova
    case 42:return Motion::RubSnap;       // Static Field
    case 58:return Motion::CrossGuard;    // Energy Shield
    case 57:return Motion::ArmUp;         // Thunder Storm
    case 46:return Motion::MatchStrike;   // Blaze
    case 40:return Motion::ForearmSweep;  // Frozen Armor
    case 50:return Motion::Shake;         // Shiver Armor
    case 60:return Motion::Hug;           // Chilling Armor
    case 54:return Motion::Punch;         // Teleport (owner: "as a motion similar to Paladin's Charge")
    case 8:return Motion::InnerSight;case 17:return Motion::SlowMissiles;case 28:return Motion::Decoy;case 32:return Motion::Valkyrie;
    case 149:return Motion::BattleOrders;case 138:return Motion::Shout;case 155:return Motion::BattleCommand;case 154:return Motion::WarCry;
    case 130:return Motion::Howl;case 146:return Motion::BattleCry;case 132:return Motion::Leap;case 142:return Motion::FindItem;
    case 68:return Motion::BoneArmor;case 70:return Motion::RaiseSkeleton;case 74:return Motion::CorpseExplosion;
    case 75:case 85:case 94:return Motion::Golem;   // the card is Clay Golem's; it casts the strongest learned (CastSkillOf)
    case 66:return Motion::AmplifyDamage;case 91:return Motion::LowerResist;case 92:return Motion::PoisonNova;case 88:return Motion::BonePrison;
    case 258:return Motion::BurstOfSpeed;case 267:return Motion::Fade;case 264:return Motion::CloakOfShadows;case 278:return Motion::Venom;
    case 277:return Motion::BladeShield;case 268:case 279:return Motion::ShadowWarrior;case 273:return Motion::MindBlast;
    default:return Motion::None;
    }
}
// The MOTIONS page (the wheel; owner 2026-11-03: "a place where you can see the motion spells that are
// active"): the motion spells in the order it shows them (skill ids, the trees' order), each with its
// bit in [motions] off (the motion's number - 1), set to turn it off.
constexpr int PageSkills[SorceressCount]={40,42,44,46,48,50,54,57,58,60};
inline int Bit(int skill){const int m=int(For(skill));return m>0&&m<=SorceressCount?m-1:-1;}
inline int OffMask(float value){return std::isfinite(value)?std::clamp(int(std::lround(value)),0,(1<<MaxCards)-1):0;}
inline bool On(int skill,int off){const int b=Bit(skill);return b>=0&&!((off>>b)&1);}
// Every class's MOTIONS page (owner, 2026-11-04: "Both grips, like Sorceress" for the Paladin's and the
// Druid's gestures as well): its skills in card order, each turned off by a bit of the class's own
// setting. The Sorceress's bit is her motion's number - 1 ([motions] off, as saved since 2026-11-03);
// another class's is its card's. skillClass as the game's: 1 Sorceress, 3 Paladin, 5 Druid.
constexpr int PaladinSkills[]={107,112,117,121};   // Charge, Blessed Hammer, Holy Shield, Fist of the Heavens
constexpr int DruidSkills[]={223,228};             // Werewolf, Werebear
// The table's order (owner, 2026-11-04): A1-A4, B1-B8, N1-N8 (N4 the golems' card, N6 Lower Resist), S1-S7.
constexpr int AmazonSkills[]={8,17,28,32};
constexpr int BarbarianSkills[]={149,138,155,154,130,146,132,142};
constexpr int NecromancerSkills[]={68,70,74,75,66,91,92,88};
constexpr int AssassinSkills[]={258,267,264,278,277,268,273};
struct Page {const int* skills=nullptr;int count=0;};
inline Page PageOf(int skillClass) {
    switch(skillClass) {
    case 0:return {AmazonSkills,int(std::size(AmazonSkills))};
    case 1:return {PageSkills,SorceressCount};
    case 2:return {NecromancerSkills,int(std::size(NecromancerSkills))};
    case 3:return {PaladinSkills,int(std::size(PaladinSkills))};
    case 4:return {BarbarianSkills,int(std::size(BarbarianSkills))};
    case 5:return {DruidSkills,int(std::size(DruidSkills))};
    case 6:return {AssassinSkills,int(std::size(AssassinSkills))};
    default:return {};
    }
}
inline bool HasMotions(int skillClass){return PageOf(skillClass).count>0;}
inline int Bit(int skillClass,int skill) {
    if(skillClass==1)return Bit(skill);
    const auto page=PageOf(skillClass);
    for(int i=0;i<page.count;++i)if(page.skills[i]==skill)return i;
    return -1;
}
inline bool On(int skillClass,int skill,int off){const int b=Bit(skillClass,skill);return b>=0&&!((off>>b)&1);}
// Its name, for a hero without the skill (the catalog has none then).
inline const char* SpellName(int skill) {
    switch(skill) {
    case 40:return "Frozen Armor";case 42:return "Static Field";case 44:return "Frost Nova";case 46:return "Blaze";
    case 48:return "Nova";case 50:return "Shiver Armor";case 54:return "Teleport";case 57:return "Thunder Storm";
    case 58:return "Energy Shield";case 60:return "Chilling Armor";
    case 107:return "Charge";case 112:return "Blessed Hammer";case 117:return "Holy Shield";case 121:return "Fist of the Heavens";
    case 223:return "Werewolf";case 228:return "Werebear";
    case 8:return "Inner Sight";case 17:return "Slow Missiles";case 28:return "Decoy";case 32:return "Valkyrie";
    case 149:return "Battle Orders";case 138:return "Shout";case 155:return "Battle Command";case 154:return "War Cry";
    case 130:return "Howl";case 146:return "Battle Cry";case 132:return "Leap";case 142:return "Find Item";
    case 68:return "Bone Armor";case 70:return "Raise Skeleton";case 74:return "Corpse Explosion";case 75:return "Clay Golem";
    case 85:return "Blood Golem";case 94:return "Fire Golem";case 66:return "Amplify Damage";case 91:return "Lower Resist";
    case 92:return "Poison Nova";case 88:return "Bone Prison";
    case 258:return "Burst of Speed";case 267:return "Fade";case 264:return "Cloak of Shadows";case 278:return "Venom";
    case 277:return "Blade Shield";case 268:return "Shadow Warrior";case 279:return "Shadow Master";case 273:return "Mind Blast";
    default:return "";
    }
}
// A card that stands for a family (the table's N4 and S6): its name on the MOTIONS page; nullptr, the skill's.
inline const char* CardName(int skill) {
    switch(skill){case 75:return "Golem";case 268:return "Shadow Warrior / Master";default:return nullptr;}
}
// The skill a card casts: the strongest of its family learned (known(id): the hero has the skill). The golems
// Fire, then Blood, then Clay (Iron Golem takes an item, so it stays on its button); Shadow Master once learned.
template<class Known> int CastSkillOf(int skill,Known known) {
    switch(skill) {
    case 75:return known(94)?94:known(85)?85:75;
    case 268:return known(279)?279:268;
    default:return skill;
    }
}
// The Amazon's, Barbarian's, Necromancer's and Assassin's cues (their headers; defined at the end of this file).
inline const char* ClassCue(Motion m);
// How to do it: on the MOTIONS page and in the log.
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::FlingOut:return "bring the hands together, then fling both arms out wide";
    case Motion::PushDown:return "push both hands down toward the floor";
    case Motion::RubSnap:return "rub the palms together, then snap them apart";
    case Motion::CrossGuard:return "both hands up at the face, close together (crossed or not), and hold";
    case Motion::ArmUp:return "raise one arm straight up and hold";
    case Motion::MatchStrike:return "strike one hand down the thigh, hip to knee";
    case Motion::ForearmSweep:return "sweep the left hand from the right hand up to the right shoulder";
    case Motion::Shake:return "shake both fists";
    case Motion::Hug:return "hug yourself, each hand on the other shoulder";
    case Motion::Punch:return "draw the right fist in, then punch it straight out where you want to land";
    default:return m>Motion::Punch&&m<Motion::End?ClassCue(m):"no motion";
    }
}
// A page skill's cue: the Sorceress's by her motion, the Paladin's and the Druid's by their gesture.
inline const char* CueOf(int skill) {
    switch(skill) {
    case 107:return "draw the right fist in, then punch it straight out at a monster";
    case 112:return "punch a fist from below your head up past it, one hammer a punch";
    case 117:return "make the sign of the cross: forehead, chest, one shoulder, the other";
    case 121:return "raise the right arm straight, then bring it down level with the shoulder";
    case 223:return "cross the arms on the chest, bow, then straighten up flinging them out";
    case 228:return "raise both hands overhead, hold, then slam them down together";
    default:return Cue(For(skill));
    }
}
// The grips' hold every class's motions take (owner, 2026-11-03: "what about holding both grips to
// cast?"): both held above .6, let go below .35, so a squeeze that dips mid-motion keeps it.
struct GripGate {
    bool down[2]{};
    void Reset(){*this=GripGate{};}
    bool Step(const float grip[2]) {
        for(int h=0;h<2;++h)down[h]=std::isfinite(grip[h])&&grip[h]>(down[h]?.35f:.6f);
        return Held();
    }
    bool Held()const{return down[0]&&down[1];}
};
// The gate every class's motions take: the grips' hold with the hands free for them (hands: the
// transport's - a motion of the class learned and on, Weapon skill gestures, both triggers free, no
// bottle, holster, bow, wheel, menu or glyph stroke). open: the motions are watched; watch: open, or
// the grips open this frame after a hold (a gesture may end as they open: Fist of the Heavens' fist).
struct Gate {
    GripGate grips;bool was=false;
    struct State{bool held=false,open=false,watch=false;};
    void Reset(){*this=Gate{};}
    State Step(const float grip[2],bool hands) {
        State s;s.held=grips.Step(grip);s.open=s.held&&hands;s.watch=hands&&(s.held||was);
        was=s.held;return s;
    }
};
// While both grips cast spells (owner, 2026-11-03: "when both grips are held you should prevent
// actions like grabbing potions or grabbing above the shoulder"): a grip counts for a grab only
// alone - held SoloSeconds with the other free - and then until it is let go; pressed with the
// other, or while the other is held, never until it is let go. Held above .6, let go below .35, so a
// squeeze that dips mid-motion is not a new press (the grabs take a grip's press: a dip and back at
// the belt or a shoulder took a potion or swapped weapons).
struct SoloGrips {
    static constexpr double SoloSeconds=.1;
    bool held[2]{},solo[2]{};
    double since[2]{-1,-1};   // when pressed alone; -2 pressed with the other (never, until let go)
    void Reset(){*this=SoloGrips{};}
    void Step(double now,const float grip[2]) {
        for(int h=0;h<2;++h)held[h]=std::isfinite(grip[h])&&grip[h]>(held[h]?.35f:.6f);
        for(int h=0;h<2;++h) {
            if(!held[h]){solo[h]=false;since[h]=-1;continue;}
            if(solo[h])continue;
            if(held[1-h]){since[h]=-2;continue;}
            if(since[h]==-1)since[h]=now;
            if(since[h]>=0&&now-since[h]>=SoloSeconds)solo[h]=true;
        }
    }
};
struct Tuning {
    // Tuned on the owner's first session (2026-11-03): Nova flings that reached 78 and 89 cm, Frost
    // Nova pushes begun at the chest and ending at the sides, and an Energy Shield held 12-22 cm apart
    // at the face that never cast.
    float together=.30f;      // m: Nova starts with the hands this close...
    float flingApart=.75f;    // ...and ends with them this far apart,
    float flingSide=.25f;     // each this far out on its own side,
    float flingWindow=.8f;    // s after they were together
    float pushDrop=.20f;      // m: Frost Nova: both hands this much lower than
    float pushWindow=.5f;     // s ago (a push, not lowering the arms),
    float pushBelow=-.40f;    // m: ending this far below the eyes, not behind the back
    float rubClose=.18f;      // m: Static Field: controllers this close are palms together;
    float rubPath=.15f;       // m of their rubbing charges it,
    float snapApart=.45f;     // m: then the hands this far apart
    float snapWindow=.5f;     // s after they parted
    float guardApart=.35f;    // m: Energy Shield: both hands up at the face, this close (crossed or not)
    float holdSeconds=.35f;   // a held pose (Energy Shield, Thunder Storm, Chilling Armor)
    float stillSpeed=.8f;     // m/s: a held pose moves slower than this
    float raiseAbove=.22f;    // m above the eyes: Thunder Storm's raised hand
    float strikeDrop=.22f;    // m: Blaze: one hand this much lower than
    float strikeWindow=.25f;  // s ago (a strike),
    float strikeFrom=-.40f;   // m: starting below this (the hip, not the chest)
    float sweepAt=.20f;       // m: Frozen Armor: the left hand this near the right hand,
    float sweepTo=.22f;       // m: then this near the right shoulder,
    float sweepWindow=1.2f;   // s later
    float shakeSpeed=.35f;    // m/s: Shiver Armor: a swing faster than this, then back,
    int shakeTurns=3;         // this many times with each hand
    float shakeWindow=.9f;    // s
    // Teleport, the Paladin's Charge punch (charge_gesture.h): the right hand drawn in (at most punchFrom
    // ahead of the eyes, hip to brow), then punched straight out to punchReach ahead, punchTravel
    // forward within punchWindow, at least 1.5 times as far forward as up, down or sideways.
    // The cast goes the way of the punch (Output::aim).
    float punchFrom=.30f,punchReach=.45f,punchTravel=.25f,punchWindow=.20f;
    float cooldown=.6f;       // s after a cast
    float history=1.5f;       // s of the hands kept
};
struct Input {
    double time=0;
    uint64_t session=0,epoch=0;
    bool active=false;            // a spell with a motion readied, the hands free for it (no wheel, menu, bottle)
    bool held=false;              // the right trigger
    bool free=false;              // no trigger: watched whenever active, and never dropped (a spell on a slot)
    Motion motion=Motion::None;
    math::Vec head{};math::Quat headRotation{0,0,0,1};
    math::Vec hand[2]{};          // world frame; 0 right, 1 left
    bool tracked[2]{};
};
struct Output {
    bool began=false;             // the trigger pressed: watching
    bool cast=false;              // the motion completed
    const char* dropped=nullptr;  // let go, or stopped, without it (for the log)
    float seconds=0;              // held so far
    // Over the hold, for tuning: the hands least and most apart, the highest and lowest hand from the
    // eyes, the fastest hand, each hand's biggest fall within pushWindow, and where the hands were last
    // (head-local: x right, y up, z back) (m, m/s).
    float apartMin=0,apartMax=0,top=0,low=0,fastest=0,fall[2]{};
    math::Vec end[2]{};
    // With a cast that goes somewhere (Teleport): the way the motion went, head-local as end (the punch,
    // from where it began to where it ended); zero for the rest.
    math::Vec aim{};
};
// The hands at one frame, head-local: l[0] the right, l[1] the left.
struct Sample {double t=0;math::Vec l[2]{};};
class Engine;
// The Amazon's, Barbarian's, Necromancer's and Assassin's detectors (defined at the end of this file).
inline bool ClassDone(Engine& e,Motion m);
class Engine {
    std::vector<Sample> samples;
    uint64_t session=0,epoch=0;
    bool watching=false,wasHeld=false;
    int casts=0;                  // in this hold
    double since=0,last=0,castAt=-1,poseSince=-1;
    float rub=0;double parted=-1; // Static Field
    math::Vec aimed{};            // Teleport: the punch Done found
    struct Swing {int sign=0;float peak=0;};
    Swing swing[2][3]{};          // Shiver Armor: each hand's last swing on each axis
    std::vector<double> turns[2][3];
    Output stats;
    static bool Finite(math::Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
    void Clear() {
        samples.clear();poseSince=-1;rub=0;parted=-1;aimed={};scratch={};
        for(auto& h:swing)for(auto& a:h)a={};
        for(auto& h:turns)for(auto& a:h)a.clear();
    }
    void Stop(){watching=false;casts=0;Clear();}
public:
    // For a class's detector (motions_<class>.h, through ClassDone): the hands' history (head-local, the
    // newest last, history seconds of it), these helpers, a scratch store cleared with the hold and after
    // each cast, and Aim for a cast that goes somewhere (a spot: Output::aim, head-local).
    const std::vector<Sample>& History()const{return samples;}
    std::array<double,8> scratch{};
    void Aim(math::Vec way){aimed=way;}
    static float Len(math::Vec v){return std::sqrt(math::Dot(v,v));}
    static float Apart(const Sample& s){return Len(s.l[0]-s.l[1]);}
    static float Forward(math::Vec l){return -l.z;}
    // The hand's velocity over the last few samples (m/s), zero until there are some.
    math::Vec Velocity(int h)const {
        if(samples.size()<3)return {};
        const auto& a=samples[samples.size()-3];const auto& b=samples.back();
        const double dt=b.t-a.t;
        return dt>1e-4?(b.l[h]-a.l[h])*float(1/dt):math::Vec{};
    }
    bool Still(int h)const{return Len(Velocity(h))<tuning.stillSpeed;}
    // A pose held: true once it has been kept (and kept still) for seconds (holdSeconds when not given).
    bool Held(bool pose,double now,double seconds=-1) {
        if(!pose){poseSince=-1;return false;}
        if(poseSince<0)poseSince=now;
        return now-poseSince>=(seconds>=0?seconds:tuning.holdSeconds);
    }
private:
    bool Done(Motion m) {
        const auto& n=samples.back();
        const auto& t=tuning;
        switch(m) {
        case Motion::FlingOut: {
            if(Apart(n)<t.flingApart||n.l[0].x<t.flingSide||n.l[1].x>-t.flingSide)return false;
            for(const auto& s:samples)
                if(n.t-s.t<=t.flingWindow&&Apart(s)<=t.together&&Forward(s.l[0])>-.1f&&Forward(s.l[1])>-.1f)return true;
            return false;
        }
        case Motion::PushDown: {
            if(n.l[0].y>t.pushBelow||n.l[1].y>t.pushBelow||Forward(n.l[0])<-.25f||Forward(n.l[1])<-.25f)return false;
            for(const auto& s:samples)
                if(n.t-s.t<=t.pushWindow&&s.l[0].y-n.l[0].y>=t.pushDrop&&s.l[1].y-n.l[1].y>=t.pushDrop)return true;
            return false;
        }
        case Motion::RubSnap: {
            const float apart=Apart(n);
            if(samples.size()>=2) {
                const auto& p=samples[samples.size()-2];
                if(apart<=t.rubClose&&Apart(p)<=t.rubClose)rub+=Len((n.l[0]-n.l[1])-(p.l[0]-p.l[1]));
            }
            if(apart<=t.rubClose)parted=-1;
            else if(parted<0&&rub>0)parted=n.t;
            if(parted>=0&&n.t-parted>t.snapWindow){rub=0;parted=-1;}   // apart too long: the charge is lost
            return rub>=t.rubPath&&parted>=0&&apart>=t.snapApart;
        }
        case Motion::CrossGuard: {
            // Crossed or not: the owner's guard held the controllers 12-22 cm apart at the face.
            bool pose=Apart(n)<=t.guardApart;
            for(int h=0;h<2;++h){const auto& l=n.l[h];pose=pose&&l.y>=-.30f&&l.y<=.20f&&Forward(l)>=0&&Forward(l)<=.60f&&std::fabs(l.x)<=.30f&&Still(h);}
            return Held(pose,n.t);
        }
        case Motion::ArmUp: {
            bool pose=false;
            for(int h=0;h<2;++h){const auto& l=n.l[h];pose=pose||(l.y>=t.raiseAbove&&std::fabs(l.x)<=.55f&&Forward(l)>=-.35f&&Forward(l)<=.60f&&Still(h));}
            return Held(pose,n.t);
        }
        case Motion::MatchStrike: {
            for(int h=0;h<2;++h) {
                const auto& c=n.l[h];
                if(Forward(c)>.20f||Forward(c)<-.35f||std::fabs(c.x)<.15f)continue;   // at the side, not in front
                for(const auto& s:samples)
                    if(n.t-s.t<=t.strikeWindow&&s.l[h].y<=t.strikeFrom&&s.l[h].y-c.y>=t.strikeDrop&&std::fabs(s.l[h].x-c.x)<=.20f)return true;
            }
            return false;
        }
        case Motion::ForearmSweep: {
            const math::Vec shoulder{.17f,-.22f,.05f};
            // The right arm held out on its own side (a hug has it across the chest).
            if(Len(n.l[1]-shoulder)>t.sweepTo||Len(n.l[0]-shoulder)<.30f||n.l[0].x<.10f)return false;
            for(const auto& s:samples)if(n.t-s.t<=t.sweepWindow&&Apart(s)<=t.sweepAt)return true;
            return false;
        }
        case Motion::Shake: {
            if(samples.size()<3)return false;
            int most[2]{};
            for(int h=0;h<2;++h) {
                const auto v=Velocity(h);const float axis[3]={v.x,v.y,v.z};
                for(int a=0;a<3;++a) {
                    auto& w=swing[h][a];auto& times=turns[h][a];
                    const int sign=axis[a]>0?1:axis[a]<0?-1:0;
                    if(sign&&w.sign&&sign!=w.sign){if(w.peak>=t.shakeSpeed)times.push_back(n.t);w.peak=0;}
                    if(sign)w.sign=sign;
                    w.peak=std::max(w.peak,std::fabs(axis[a]));
                    times.erase(std::remove_if(times.begin(),times.end(),[&](double at){return n.t-at>t.shakeWindow;}),times.end());
                    most[h]=std::max(most[h],int(times.size()));
                }
            }
            const bool front=Forward(n.l[0])>-.10f&&Forward(n.l[1])>-.10f&&n.l[0].y>-.90f&&n.l[1].y>-.90f;
            return front&&most[0]>=t.shakeTurns&&most[1]>=t.shakeTurns;
        }
        case Motion::Punch: {
            const auto& c=n.l[0];
            if(Forward(c)<t.punchReach)return false;
            for(const auto& s:samples) {
                const auto& a=s.l[0];
                const float forward=Forward(c)-Forward(a);
                if(n.t-s.t<=t.punchWindow&&Forward(a)<=t.punchFrom&&a.y>=-.70f&&a.y<=.10f&&forward>=t.punchTravel&&
                   std::fabs(c.y-a.y)*1.5f<=forward&&std::fabs(c.x-a.x)*1.5f<=forward){aimed=c-a;return true;}
            }
            return false;
        }
        case Motion::Hug: {
            const auto& r=n.l[0];const auto& l=n.l[1];
            const bool pose=r.x>=-.40f&&r.x<=-.02f&&l.x<=.40f&&l.x>=.02f&&r.y>=-.60f&&r.y<=-.18f&&l.y>=-.60f&&l.y<=-.18f&&
                Forward(r)>=-.25f&&Forward(r)<=.15f&&Forward(l)>=-.25f&&Forward(l)<=.15f;   // at the shoulders, not up in front (Energy Shield)
            return Held(pose,n.t);
        }
        default:return m>Motion::Punch&&m<Motion::End&&ClassDone(*this,m);
        }
    }
public:
    Tuning tuning;
    void Reset(){const auto keep=tuning;*this=Engine{};tuning=keep;}
    bool Watching()const{return watching;}
    // The figures of the hold being watched, or of the last one (until the next begins).
    const Output& Stats()const{return stats;}
    Output Step(const Input& in) {
        Output out;
        // A reset forgets the hold: a trigger still held must be pressed again.
        if(!std::isfinite(in.time)||!Finite(in.head)){Reset();wasHeld=in.held;return out;}
        if(in.session!=session||in.epoch!=epoch){Reset();session=in.session;epoch=in.epoch;wasHeld=in.held;}
        const double dt=in.time-last;
        const bool continuous=last>0&&dt>0&&dt<=.15;
        last=in.time;
        const bool pressed=in.held&&!wasHeld;
        wasHeld=in.held;
        const bool ready=in.active&&in.motion!=Motion::None&&in.tracked[0]&&in.tracked[1]&&Finite(in.hand[0])&&Finite(in.hand[1]);
        if(watching&&(!(in.free||in.held)||!ready||!continuous)) {
            if(!casts&&!in.free) {   // a hold that cast at least once was not dropped
                out=stats;out.seconds=float(in.time-since);
                out.dropped=!in.held?"let go without the motion":!continuous?"frame gap":"hands busy, untracked or input inactive";
            }
            Stop();return out;
        }
        if(!watching) {
            if(!(in.free||pressed)||!ready)return out;   // only a fresh press watches: a trigger held as the spell was readied does not
            watching=true;since=in.time;casts=0;Clear();
            stats={};stats.apartMin=1e9f;stats.top=-1e9f;stats.low=1e9f;
            out.began=!in.free;
        }
        const auto inverse=math::Conjugate(math::Yaw(math::Heading(in.headRotation)));
        Sample s;s.t=in.time;
        for(int h=0;h<2;++h)s.l[h]=math::Rotate(inverse,in.hand[h]-in.head);
        samples.push_back(s);
        samples.erase(samples.begin(),std::find_if(samples.begin(),samples.end(),[&](const Sample& x){return in.time-x.t<=tuning.history;}));
        const float apart=Apart(s);
        stats.apartMin=std::min(stats.apartMin,apart);stats.apartMax=std::max(stats.apartMax,apart);
        for(int h=0;h<2;++h) {
            stats.top=std::max(stats.top,s.l[h].y);stats.low=std::min(stats.low,s.l[h].y);stats.fastest=std::max(stats.fastest,Len(Velocity(h)));
            for(const auto& x:samples)if(in.time-x.t<=tuning.pushWindow)stats.fall[h]=std::max(stats.fall[h],x.l[h].y-s.l[h].y);
            stats.end[h]=s.l[h];
        }
        out.seconds=float(in.time-since);
        if(castAt>=0&&in.time-castAt<tuning.cooldown)return out;
        if(Done(in.motion)) {
            const auto seconds=out.seconds;out=stats;out.seconds=seconds;out.cast=true;out.aim=aimed;
            castAt=in.time;++casts;Clear();   // the motion again, from its start, casts again
        }
        return out;
    }
};
}
// The classes' detectors. Each header includes this one; include spell_motion.h, never those directly.
#include "motions_amazon.h"
#include "motions_barbarian.h"
#include "motions_necromancer.h"
#include "motions_assassin.h"
namespace framevr::spellmotion {
inline bool ClassDone(Engine& e,Motion m) {
    if(m>=Motion::InnerSight&&m<=Motion::Valkyrie)return amazon::Done(e,m);
    if(m>=Motion::BattleOrders&&m<=Motion::FindItem)return barbarian::Done(e,m);
    if(m>=Motion::BoneArmor&&m<=Motion::BonePrison)return necromancer::Done(e,m);
    if(m>=Motion::BurstOfSpeed&&m<=Motion::MindBlast)return assassin::Done(e,m);
    return false;
}
inline const char* ClassCue(Motion m) {
    if(m>=Motion::InnerSight&&m<=Motion::Valkyrie)return amazon::Cue(m);
    if(m>=Motion::BattleOrders&&m<=Motion::FindItem)return barbarian::Cue(m);
    if(m>=Motion::BoneArmor&&m<=Motion::BonePrison)return necromancer::Cue(m);
    if(m>=Motion::BurstOfSpeed&&m<=Motion::MindBlast)return assassin::Cue(m);
    return "no motion";
}
// The order a class's motions are watched in: the first to complete in a frame casts. The Sorceress's
// Static Field before Nova (its snap apart is as wide as a fling); the other classes' their header's Order.
constexpr Motion SorceressOrder[]={Motion::RubSnap,Motion::FlingOut,Motion::PushDown,Motion::CrossGuard,Motion::ArmUp,
    Motion::MatchStrike,Motion::ForearmSweep,Motion::Shake,Motion::Hug,Motion::Punch};
struct Watched {const Motion* motions=nullptr;int count=0;};
inline Watched OrderOf(int skillClass) {
    switch(skillClass) {
    case 0:return {amazon::Order,int(std::size(amazon::Order))};
    case 1:return {SorceressOrder,int(std::size(SorceressOrder))};
    case 2:return {necromancer::Order,int(std::size(necromancer::Order))};
    case 4:return {barbarian::Order,int(std::size(barbarian::Order))};
    case 6:return {assassin::Order,int(std::size(assassin::Order))};
    default:return {};   // the Paladin's and the Druid's gestures have their own detectors
    }
}
}
