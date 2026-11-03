#pragma once
#include "frame_math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Spells readied by a glyph and cast by their motion (owner, 2026-11-03: "the sorceress first draws
// the glyph to "activate" the right spell, then uses the trigger ... to then do the motion to cast
// it"). A glyph whose skill has a motion readies it instead of casting it, until another glyph
// readies another or a tap of Draw glyph clears it. While a spell is readied the right trigger is
// the spell's: held, only that spell's motion is watched, and completing it casts (the transport
// presses the skill as a glyph does, glyph_cast.h), so mana, cast rate and the spell stay native.
// Still held, the motion again casts again.
//
// Free (owner, 2026-11-03: "any spell we already have added to either hand's spell wheels should be
// accessible to us by simply doing the motion of it"): a spell on a hand slot casts by its motion
// alone, the triggers free, as the Paladin's and Druid's gestures; the transport runs one watcher per
// motion and takes the first to complete.
//
// Prototype: the Sorceress's self-casts and bursts, which need no target. Head-local frame as
// holycross (+X right, +Y up from the eyes, forward -Z, the head's yaw only); hand 0 is the right.
// Value-only: steamvr/spell_motion_test.cpp.
namespace framevr::spellmotion {
enum class Motion {None,FlingOut,PushDown,RubSnap,CrossGuard,ArmUp,MatchStrike,ForearmSweep,Shake,Hug};
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
    default:return Motion::None;
    }
}
// What to do, for the log when a spell is readied.
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::FlingOut:return "bring the hands together, then fling both arms out wide";
    case Motion::PushDown:return "push both hands down toward the floor";
    case Motion::RubSnap:return "rub the palms together, then snap them apart";
    case Motion::CrossGuard:return "cross the forearms in front of the face and hold";
    case Motion::ArmUp:return "raise one arm straight up and hold";
    case Motion::MatchStrike:return "strike one hand down the thigh, hip to knee";
    case Motion::ForearmSweep:return "sweep the left hand from the right hand up to the right shoulder";
    case Motion::Shake:return "shake both fists";
    case Motion::Hug:return "hug yourself, each hand on the other shoulder";
    default:return "no motion";
    }
}
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
};
class Engine {
    struct Sample {double t=0;math::Vec l[2]{};};
    std::vector<Sample> samples;
    uint64_t session=0,epoch=0;
    bool watching=false,wasHeld=false;
    int casts=0;                  // in this hold
    double since=0,last=0,castAt=-1,poseSince=-1;
    float rub=0;double parted=-1; // Static Field
    struct Swing {int sign=0;float peak=0;};
    Swing swing[2][3]{};          // Shiver Armor: each hand's last swing on each axis
    std::vector<double> turns[2][3];
    Output stats;
    static bool Finite(math::Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
    static float Len(math::Vec v){return std::sqrt(math::Dot(v,v));}
    static float Apart(const Sample& s){return Len(s.l[0]-s.l[1]);}
    static float Forward(math::Vec l){return -l.z;}
    void Clear() {
        samples.clear();poseSince=-1;rub=0;parted=-1;
        for(auto& h:swing)for(auto& a:h)a={};
        for(auto& h:turns)for(auto& a:h)a.clear();
    }
    void Stop(){watching=false;casts=0;Clear();}
    // The hand's velocity over the last few samples (m/s), zero until there are some.
    math::Vec Velocity(int h)const {
        if(samples.size()<3)return {};
        const auto& a=samples[samples.size()-3];const auto& b=samples.back();
        const double dt=b.t-a.t;
        return dt>1e-4?(b.l[h]-a.l[h])*float(1/dt):math::Vec{};
    }
    bool Still(int h)const{return Len(Velocity(h))<tuning.stillSpeed;}
    // A pose held: true once it has been kept (and kept still) for holdSeconds.
    bool Held(bool pose,double now) {
        if(!pose){poseSince=-1;return false;}
        if(poseSince<0)poseSince=now;
        return now-poseSince>=tuning.holdSeconds;
    }
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
        case Motion::Hug: {
            const auto& r=n.l[0];const auto& l=n.l[1];
            const bool pose=r.x>=-.40f&&r.x<=-.02f&&l.x<=.40f&&l.x>=.02f&&r.y>=-.60f&&r.y<=-.18f&&l.y>=-.60f&&l.y<=-.18f&&
                Forward(r)>=-.25f&&Forward(r)<=.15f&&Forward(l)>=-.25f&&Forward(l)<=.15f;   // at the shoulders, not up in front (Energy Shield)
            return Held(pose,n.t);
        }
        default:return false;
        }
    }
public:
    Tuning tuning;
    void Reset(){const auto keep=tuning;*this=Engine{};tuning=keep;}
    bool Watching()const{return watching;}
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
            const auto seconds=out.seconds;out=stats;out.seconds=seconds;out.cast=true;
            castAt=in.time;++casts;Clear();   // the motion again, from its start, casts again
        }
        return out;
    }
};
}
