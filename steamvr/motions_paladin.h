#pragma once
#include "spell_motion.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// The Paladin's motions: Charge's punch, Blessed Hammer's fist pump, Holy Shield's sign of the cross and Fist of
// the Heavens' call, watched on spellmotion::Engine as every other class's (owner, 2026-11-08: "Sorceress' motion
// gestures produce haptic feedback through vibrations but paladin motions do not ... We should migrate those older
// detectors"): the warmer/colder hum as one nears its cast (Output::progress), the cast through the transport's
// motionCast (both controllers pulse), the class's order and its MOTIONS page's cards. Both grips are held while
// they are watched (the transport's gate), and the frame they open (WatchesOpening). Include spell_motion.h, not
// this. Value-only: steamvr/motions_paladin_test.cpp.
//
// Until then each ran an engine of its own (hammer_gesture.h, charge_gesture.h, cross_gesture.h, heavens_gesture.h;
// Cal, 2026-10-27 and 11-01, and the owner, 2026-11-01, tuned them in the headset). Their geometry, thresholds and
// timing are kept as they were, as is how each ran: in the hands' own metres (l * Engine::Body(): Charge's
// extension and Fist of the Heavens' reach follow the calibrated arm span instead, Input::armSpan), a hand's
// velocity the runtime's when it gives one (Input::handVelocity) else from the last frame, the grips as pulled for
// a fist (Input::grip), each detector's state in the Engine's scratch, and each one's own rest after a cast (Rest).
// The first part of each is done (Engine::Toward: the hum) once the fist is punching (Charge, Blessed Hammer), the
// forehead is touched (Holy Shield) or the straight arm is raised (Fist of the Heavens).
namespace framevr::spellmotion::paladin {
// Blessed Hammer (Cal, 2026-10-27): a fist below the head punched up to the sky, ending above the head. One pump
// casts one hammer; to pump again the fist comes back down below the head first, so a fist held high never
// repeats. Head-local heights (head yaw only, +Y up).
struct HammerTuning {
    float gripOn=.6f,gripOff=.4f; // a fist closes above gripOn and opens below gripOff
    float loadBelow=-.10f;        // a pump starts from a fist below the head by this (chin, shoulder or lower)
    float punchSpeed=1.6f;        // upward speed (m/s, world) that starts the punch
    float steep=1.0f;             // ...and at least this times the horizontal speed (within 45 deg of straight up)
    float topHeight=.08f;         // the fist ends above the head by this
    float minTravel=.25f;         // and rose at least this far from where the punch began
    float followSpeed=.5f;        // while loaded and rising slower than this, the punch's start follows the fist
    float window=.35f;            // seconds from reaching punchSpeed to clearing the head
    float stallSpeed=.2f;         // upward speed under this below the top abandons the punch
};
// Charge (Cal, 2026-10-27): the weapon (right) hand's fist drawn back near the body, then punched straight out
// ahead, extending the arm. One punch casts once; to punch again the fist comes back in first. Head-local (+X
// right, +Y up, -Z forward).
struct ChargeTuning {
    float gripOn=.6f,gripOff=.4f;   // the fist closes above gripOn, opens below gripOff
    float drawnBack=.30f;           // a punch starts from a fist at most this far ahead of the head
    float lowest=-.70f,highest=.10f;// ...between these heights (hip to just over the eyes)
    float punchSpeed=2.0f;          // forward speed (m/s) that starts the punch
    float straight=1.5f;            // forward speed at least this times the vertical and the sideways speed
    float extendShare=.28f;         // extended: fist ahead of the head by this share of the arm span
    float extendDefault=.45f;       // ...or by this when no body calibration is stored
    float extendMin=.38f,extendMax=.60f;
    float minTravel=.20f;           // the fist moved at least this far ahead from where the punch began
    float followSpeed=.5f;          // while loaded and moving forward slower than this, the punch's start follows the fist
    float window=.30f;              // seconds from reaching punchSpeed to full extension
    float stallSpeed=.2f;           // forward speed under this before extension abandons the punch
    constexpr float Extended(float armSpan)const{return armSpan>0?std::clamp(armSpan*extendShare,extendMin,extendMax):extendDefault;}
};
// Holy Shield (Cal, 2026-11-01: "a holy trinity (the father, son and holy spirit) hand motion"): with either hand,
// touch the forehead, then the chest, then one shoulder and the other (left then right, or right then left). Each
// touch is a pause: the hand slows inside the spot, so a swing passing through the same places never counts. One
// sign casts once. Head-local boxes, f forward of the eyes (-z).
struct Box {float x0,x1,y0,y1,f0,f1;};
struct CrossTuning {
    Box forehead{-.12f,.12f,-.06f,.14f,-.10f,.25f};
    Box chest{-.16f,.16f,-.58f,-.26f,-.15f,.30f};
    Box left{-.34f,-.10f,-.42f,-.12f,-.20f,.30f};
    Box right{.10f,.34f,-.42f,-.12f,-.20f,.30f};
    float touchSpeed=1.0f;   // m/s: inside a spot and slower than this is a touch
    float stepWindow=1.0f;   // s between one touch and the next
    float cooldown=1.5f;     // s after a cast before the next sign can start (Rest)
};
// Fist of the Heavens (Cal, 2026-11-01): the weapon arm held straight out, raised about 45 degrees (the fist a little
// above the forehead), then brought down, still straight, to stop level with the shoulder. The stop is the cast, or
// the fist opening once the arm has come down far and fast enough. Unlike a weapon chop it pivots at the shoulder
// with the arm straight, and it stops at shoulder height instead of carrying on down. Head-local.
struct HeavensTuning {
    float shoulderX=.18f,shoulderY=-.22f,shoulderZ=.06f;   // right shoulder: beside, below and a little behind the eyes
    float gripOn=.6f,gripOff=.4f;
    float straight=.70f;         // hand at least this share of the arm's reach from the shoulder
    float reachShare=.38f;       // the arm's reach: this share of the calibrated arm span...
    float reachDefault=.62f;     // ...or this without calibration
    float reachMin=.45f,reachMax=.85f;
    float raisedMin=25,raisedMax=70;   // degrees above level to load (higher is the hammer's punch)
    float aheadMin=.30f;         // the raised fist is at least this far ahead of the shoulder (m)
    float sideMax=50;            // and within this many degrees of straight ahead (head yaw)
    // Cal's 2026-11-01 test: 21 of 36 calls overshot to -22..-27 degrees and were
    // dropped as chops (the stop settles a little below the estimated shoulder).
    // The owner's (2026-11-01, 37 casts / 41 dropped): his calls end at -36..-47
    // and 5 reached -51..-53, so the stop counts down to -55 and a chop is past -65:
    // where a straight arm's call ends varies from player to player.
    float levelHigh=14,levelLow=-55;   // degrees: the stop counts between these
    float belowMax=-65;          // passing this far below level is a chop: dropped
    float dropMin=22;            // the arm came down at least this many degrees
    float window=.90f;           // seconds from leaving the raised pose to the stop
    float sweepSpeed=.9f;        // m/s: the fist reaches this on the way down (a lowering arm doesn't)
    float stopSpeed=.8f;         // m/s: the fist this slow at level is the stop
    float stopHold=.04f;         // seconds slow at level
    float cooldown=.4f;          // s after a cast (Rest)
    constexpr float Reach(float armSpan)const{return armSpan>0?std::clamp(armSpan*reachShare,reachMin,reachMax):reachDefault;}
};
struct Tuning {HammerTuning hammer;ChargeTuning charge;CrossTuning cross;HeavensTuning heavens;};
inline constexpr Tuning tuning{};
// The order they are watched in: the first to complete in a frame casts. Blessed Hammer last: its pumps start no
// wait and leave the other watchers be (spellmotion::Rapid), so a call's raise that passes for a pump still calls.
constexpr Motion Order[]={Motion::HolyShield,Motion::FistOfHeavens,Motion::Charge,Motion::BlessedHammer};
// How to do each, on the MOTIONS page and in the log.
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::Charge:return "draw the right fist in, then punch it straight out at a monster";
    case Motion::BlessedHammer:return "punch a fist from below your head up past it, one hammer a punch";
    case Motion::HolyShield:return "make the sign of the cross: forehead, chest, one shoulder, the other";
    case Motion::FistOfHeavens:return "raise the right arm straight, then bring it down level with the shoulder";
    default:return "no motion";
    }
}
// A motion's own rest after it casts, as its engine's was (the transport adds every motion's second, but Blessed
// Hammer's): Holy Shield 1.5 s, Fist of the Heavens 0.4 s, Charge and Blessed Hammer none.
inline float Rest(Motion m) {
    switch(m) {
    case Motion::HolyShield:return tuning.cross.cooldown;
    case Motion::FistOfHeavens:return tuning.heavens.cooldown;
    default:return 0;
    }
}
namespace detail {
// A hand's velocity (m/s, world), as their engines took it: the runtime's when known, else over the last frame (ok:
// either was there).
inline math::Vec Velocity(const Engine& e,int k,bool& ok) {
    const auto& in=e.Current();
    if(in.velocityKnown[k]){ok=true;return in.handVelocity[k];}
    const auto& h=e.History();
    ok=h.size()>=2;
    if(!ok)return {};
    const auto& a=h[h.size()-2];const auto& b=h.back();
    return (b.w[k]-a.w[k])*float(1/(b.t-a.t));
}
// The hand where it is, head-local in metres (not the reference body's).
inline math::Vec Local(const Engine& e,int k){return e.History().back().l[k]*e.Body();}
}
// Blessed Hammer: each fist's phase in scratch [3k] (HammerPhase), when its punch reached punchSpeed [3k+1], and where
// the punch began [3k+2] (m above the eyes).
enum HammerPhase {HammerOpen,HammerHigh,HammerLoaded,HammerRising};
inline bool Hammer(Engine& e) {
    const auto& t=tuning.hammer;const auto& in=e.Current();auto& s=e.scratch;
    const double now=e.History().back().t;
    bool cast=false;float most=-1;
    for(int k=0;k<2;++k) {
        double& phase=s[size_t(3*k)];double& startAt=s[size_t(3*k+1)];double& startY=s[size_t(3*k+2)];
        const auto drop=[&](const char* why,float y){if(int(phase)==HammerRising)e.Abandon(why,k,y-float(startY));phase=HammerOpen;};
        if(!std::isfinite(in.grip[k])){drop("hand busy, untracked or input inactive",0);continue;}
        // Loaded: startY is where the punch begins, the fist's height whenever it is not yet rising fast, so it
        // settles at the bottom of a dip or wind-up. A real punch accelerates: by the time it reaches punchSpeed it
        // has already covered part of its rise, which counts toward minTravel.
        const auto load=[&](float y){phase=HammerLoaded;startY=y;};
        bool ok=false;const auto velocity=detail::Velocity(e,k,ok);
        const float y=detail::Local(e,k).y;
        const bool fist=int(phase)==HammerOpen?in.grip[k]>=t.gripOn:in.grip[k]>t.gripOff;
        if(!fist){drop("fist opened before clearing the head",0);continue;}   // logged as its engine's (rose: 0 less the start)
        switch(int(phase)) {
        case HammerOpen:
            // A fist made up high waits for the hand to come down first.
            if(y<=t.loadBelow)load(y);else phase=HammerHigh;
            break;
        case HammerHigh:
            if(y<=t.loadBelow)load(y);
            break;
        case HammerLoaded: {
            const bool slow=!ok||velocity.y<t.followSpeed;
            if(slow)startY=y;
            const float horizontal=std::hypot(velocity.x,velocity.z);
            if(ok&&velocity.y>=t.punchSpeed&&velocity.y>=t.steep*horizontal){phase=HammerRising;startAt=now;}
            else if(slow&&y>t.loadBelow)phase=HammerHigh;   // drifted up slowly: not a punch
            break;
        }
        case HammerRising: {
            // Casts once the fist is over the head AND has risen far enough; a short punch keeps going until both hold.
            const bool over=y>=t.topHeight,risen=y-float(startY)>=t.minTravel;
            if(over&&risen){phase=HammerHigh;cast=true;break;}   // spent: comes back down to load the next pump
            const char* why=over?"punch too short (started too close to the head)":nullptr;
            if(now-startAt>t.window){drop(why?why:"too slow to clear the head",y);if(y<=t.loadBelow)load(y);else phase=HammerHigh;}
            else if(velocity.y<t.stallSpeed){drop(why?why:"punch stalled below the head",y);if(y<=t.loadBelow)load(y);else phase=HammerHigh;}
            else most=std::max(most,(y-float(startY))/std::max(t.minTravel,t.topHeight-float(startY)));   // risen of what it needs
            break;
        }
        }
    }
    if(cast){e.KeepAfterCast();return true;}   // the other fist's pump goes on; this one comes back down for the next
    if(most>=0)e.Toward(most);
    return false;
}
// Charge: the weapon hand's phase in scratch [0] (ChargePhase), when its punch reached punchSpeed [1], and where the
// punch began [2] (m ahead of the eyes).
enum ChargePhase {ChargeOpen,ChargeOut,ChargeLoaded,ChargePunching};
inline bool ChargePunch(Engine& e) {
    const auto& t=tuning.charge;const auto& in=e.Current();auto& s=e.scratch;
    const double now=e.History().back().t;
    double& phase=s[0];double& startAt=s[1];double& startReach=s[2];
    const float extended=t.Extended(in.armSpan);
    const auto drop=[&](const char* why,float reach){if(int(phase)==ChargePunching)e.Abandon(why,0,reach-float(startReach),reach,extended);phase=ChargeOpen;};
    if(!std::isfinite(in.grip[0])){drop("hand busy, untracked or input inactive",0);return false;}
    // Loaded: startReach is where the punch begins, the fist's reach whenever it is not yet moving out fast, so it
    // settles at the most drawn-back point. A real punch accelerates: by the time it reaches punchSpeed it has already
    // covered part of its travel (and may have left the drawn-back zone), which counts toward minTravel.
    const auto load=[&](float reach){phase=ChargeLoaded;startReach=reach;};
    bool ok=false;const auto world=detail::Velocity(e,0,ok);
    const auto local=detail::Local(e,0);
    const auto velocity=math::Rotate(math::Conjugate(math::Yaw(math::Heading(in.headRotation))),world);
    const float reach=-local.z,forward=-velocity.z;
    const bool fist=int(phase)==ChargeOpen?in.grip[0]>=t.gripOn:in.grip[0]>t.gripOff;
    if(!fist){drop("fist opened before the arm was out",0);return false;}   // logged as its engine's (out to 0)
    const bool drawn=reach<=t.drawnBack&&local.y>=t.lowest&&local.y<=t.highest;
    switch(int(phase)) {
    case ChargeOpen:
        // A fist made with the arm already out waits for it to come back in.
        if(drawn)load(reach);else phase=ChargeOut;
        break;
    case ChargeOut:
        if(drawn)load(reach);
        break;
    case ChargeLoaded: {
        const bool slow=!ok||forward<t.followSpeed;
        if(slow)startReach=reach;
        if(ok&&forward>=t.punchSpeed&&forward>=t.straight*std::fabs(velocity.y)&&forward>=t.straight*std::fabs(velocity.x)){phase=ChargePunching;startAt=now;}
        else if(!drawn&&slow)phase=ChargeOut;   // drifted out slowly: not a punch
        break;
    }
    case ChargePunching: {
        // Casts once the arm is out AND the fist has travelled far enough; a punch from near the line keeps going
        // until both hold. Spent, the Engine starts over: the arm still out, the fist must come back in first.
        const bool out=reach>=extended,travelled=reach-float(startReach)>=t.minTravel;
        if(out&&travelled)return true;
        const char* why=out?"punch too short (started too far out)":nullptr;
        if(now-startAt>t.window){drop(why?why:"arm not extended in time",reach);if(drawn)load(reach);else phase=ChargeOut;}
        else if(forward<t.stallSpeed){drop(why?why:"punch stalled before the arm was out",reach);if(drawn)load(reach);else phase=ChargeOut;}
        else e.Toward((reach-float(startReach))/std::max(t.minTravel,extended-float(startReach)));   // out of what it needs
        break;
    }
    }
    return false;
}
// Holy Shield: each hand's touches in scratch [4k] (0 none, 1 the forehead, 2 the chest, 3 a shoulder), its first
// shoulder [4k+1] and the spot it touched last [4k+2] (Spot), and when [4k+3].
enum class Spot {None,Forehead,Chest,Left,Right};
inline bool In(const Box& b,math::Vec p){const float f=-p.z;return p.x>=b.x0&&p.x<=b.x1&&p.y>=b.y0&&p.y<=b.y1&&f>=b.f0&&f<=b.f1;}
inline Spot Touch(math::Vec local) {
    const auto& t=tuning.cross;
    if(In(t.forehead,local))return Spot::Forehead;
    if(In(t.chest,local))return Spot::Chest;
    if(In(t.left,local))return Spot::Left;
    if(In(t.right,local))return Spot::Right;
    return Spot::None;
}
inline bool HolyShield(Engine& e) {
    const auto& t=tuning.cross;auto& s=e.scratch;
    const double now=e.History().back().t;
    int cast=-1;float most=-1;
    for(int k=0;k<2;++k) {
        double& stage=s[size_t(4*k)];double& first=s[size_t(4*k+1)];double& last=s[size_t(4*k+2)];double& at=s[size_t(4*k+3)];
        const auto clear=[&]{stage=first=last=at=0;};
        const auto touched=[&](int next,Spot spot){stage=next;last=double(int(spot));at=now;};
        bool ok=false;const auto v=detail::Velocity(e,k,ok);
        if(stage>0&&now-at>t.stepWindow) {
            if(stage>=2)e.Abandon("too long between touches",k,float(stage));
            clear();continue;
        }
        if(ok&&std::sqrt(math::Dot(v,v))<=t.touchSpeed) {   // moving: not a touch
            const Spot spot=Touch(detail::Local(e,k));
            if(spot!=Spot::None&&double(int(spot))!=last) {
                switch(int(stage)) {
                case 0:if(spot==Spot::Forehead)touched(1,spot);break;
                case 1:
                    if(spot==Spot::Chest)touched(2,spot);
                    else if(spot!=Spot::Forehead)clear();   // forehead then a shoulder: not the sign
                    break;
                case 2:
                    if(spot==Spot::Left||spot==Spot::Right){touched(3,spot);first=double(int(spot));}
                    else if(spot==Spot::Forehead)touched(1,spot);   // started over
                    break;
                case 3:
                    if((spot==Spot::Left||spot==Spot::Right)&&double(int(spot))!=first){if(cast<0)cast=k;clear();}
                    else if(spot==Spot::Forehead){clear();touched(1,spot);}
                    break;
                }
            }
        }
        if(stage>=1)most=std::max(most,float(stage-1)/3);   // the forehead touched: warmer at each touch
    }
    if(cast>=0)return true;   // both hands start over (the scratch goes with the cast)
    if(most>=0)e.Toward(most);
    return false;
}
// Fist of the Heavens: the weapon arm's phase in scratch [0] (HeavensPhase), when the sweep began [1], since when it
// has been slow at level [2] (0 not: times are positive), its top [3] (degrees above level) and its fastest [4]
// (m/s). Its engine's Spent phase (the cast's arm still out) was Open's twin: it loads again only once raised anew.
enum HeavensPhase {HeavensOpen,HeavensRaised,HeavensSweep};
inline bool Heavens(Engine& e) {
    const auto& t=tuning.heavens;const auto& in=e.Current();auto& s=e.scratch;
    const double now=e.History().back().t;
    double& phase=s[0];double& sweepAt=s[1];double& slowAt=s[2];double& top=s[3];double& peak=s[4];
    const auto drop=[&](const char* why,float elevation){if(int(phase)==HeavensSweep)e.Abandon(why,0,float(top),elevation);phase=HeavensOpen;slowAt=0;};
    if(!std::isfinite(in.grip[0])){drop("hand busy, untracked or input inactive",0);return false;}
    bool ok=false;const auto v=detail::Velocity(e,0,ok);
    const bool fist=int(phase)==HeavensOpen?in.grip[0]>=t.gripOn:in.grip[0]>t.gripOff;
    const auto local=detail::Local(e,0);
    const math::Vec arm{local.x-t.shoulderX,local.y-t.shoulderY,local.z-t.shoulderZ};
    const float length=std::sqrt(math::Dot(arm,arm)),flat=std::hypot(arm.x,arm.z),ahead=-arm.z;
    const float elevation=std::atan2(arm.y,std::max(flat,1e-4f))*57.29578f;
    const float side=std::atan2(std::fabs(arm.x),std::max(ahead,1e-4f))*57.29578f;
    const bool straightArm=length>=t.straight*t.Reach(in.armSpan);
    const bool forward=ahead>0&&side<=t.sideMax;
    const bool raised=straightArm&&forward&&ahead>=t.aheadMin&&elevation>=t.raisedMin&&elevation<=t.raisedMax;
    if(!fist) {
        // The hand opening ends the call (the owner's 2026-11-01 session: 29 of the 41 dropped calls opened the fist
        // on the way down, at -36..-47): a sweep already come down far and fast enough, within the window, casts.
        if(int(phase)==HeavensSweep&&top-elevation>=t.dropMin&&peak>=t.sweepSpeed&&elevation<=t.levelHigh&&elevation>=t.belowMax) {
            e.CastFigures(float(top),elevation);return true;
        }
        drop("fist opened",elevation);return false;
    }
    switch(int(phase)) {
    case HeavensOpen:
        // rev3 (Cal 2026-11-01: "requiring me to hold at the top ... It cant be so strict"; 18 casts and no
        // started-then-dropped calls: the misses never loaded): reaching the raised pose at any speed loads it, no rest.
        if(raised){phase=HeavensRaised;top=elevation;}
        break;
    case HeavensRaised:
        if(raised){top=std::max(top,double(elevation));break;}
        if(elevation<top-8&&straightArm&&forward){phase=HeavensSweep;sweepAt=now;slowAt=0;peak=0;}
        else if(!straightArm||!forward)phase=HeavensOpen;
        break;
    case HeavensSweep: {
        if(elevation<t.belowMax){drop("carried on below the shoulder (a chop)",elevation);break;}
        if(!straightArm){drop("elbow bent: not a straight arm",elevation);break;}
        if(!forward){drop("arm swung away from the front",elevation);break;}
        if(now-sweepAt>t.window){drop("too slow to stop at the shoulder",elevation);break;}
        const float speed=ok?std::sqrt(math::Dot(v,v)):0.f;
        peak=std::max(peak,double(speed));
        const bool level=elevation<=t.levelHigh&&elevation>=t.levelLow;
        const bool slow=ok&&speed<=t.stopSpeed;
        if(level&&slow) {
            if(slowAt<=0)slowAt=now;
            if(now-slowAt>=t.stopHold) {
                if(top-elevation<t.dropMin){drop("came down too little",elevation);break;}
                if(peak<t.sweepSpeed){drop("lowered, not brought down (too slow)",elevation);break;}
                e.CastFigures(float(top),elevation);return true;
            }
        }
        else slowAt=0;
        break;
    }
    }
    // Raised: the first part; then warmer as the straight arm comes down toward the shoulder's level.
    if(int(phase)==HeavensRaised)e.Toward(0);
    else if(int(phase)==HeavensSweep)e.Toward(float(top-elevation)/std::max(t.dropMin,float(top)-t.levelHigh));
    return false;
}
inline bool Done(Engine& e,Motion m) {
    // The watch's first frame (a new hold, a frame gap, a reset) is only its start, as their engines' first
    // continuous frame was.
    if(e.History().size()<2)return false;
    switch(m) {
    case Motion::Charge:return ChargePunch(e);
    case Motion::BlessedHammer:return Hammer(e);
    case Motion::HolyShield:return HolyShield(e);
    case Motion::FistOfHeavens:return Heavens(e);
    default:return false;
    }
}
// The watch stopped (spellmotion::ClassStopped): what had begun is given up, logged as their engines did.
inline void Stopped(Engine& e,Motion m,const char* why) {
    const auto& s=e.scratch;
    switch(m) {
    case Motion::BlessedHammer:for(int k=0;k<2;++k)if(int(s[size_t(3*k)])==HammerRising)e.Abandon(why,k,0-float(s[size_t(3*k+2)]));break;
    case Motion::Charge:if(int(s[0])==ChargePunching)e.Abandon(why,0,0-float(s[2]),0,tuning.charge.Extended(e.Current().armSpan));break;
    case Motion::HolyShield:for(int k=0;k<2;++k)if(s[size_t(4*k)]>=2)e.Abandon(why,k,float(s[size_t(4*k)]));break;
    case Motion::FistOfHeavens:if(int(s[0])==HeavensSweep)e.Abandon(why,0,float(s[3]),0);break;
    default:break;
    }
}
// Their engines' lines for a gesture given up (Output::abandoned).
inline bool DropLine(Motion m,const Output& o,char* line,size_t size) {
    const auto& f=o.abandonedFigures;const char* hand=o.abandonedHand==1?"left":"right";
    switch(m) {
    case Motion::BlessedHammer:std::snprintf(line,size,"Frame hammer: %s fist pump dropped: %s (rose %.0f cm)",hand,o.abandoned,f[0]*100);return true;
    case Motion::Charge:
        std::snprintf(line,size,"Frame charge: punch dropped: %s (moved %.0f cm, out to %.0f cm, extended at %.0f cm)",o.abandoned,f[0]*100,f[1]*100,f[2]*100);
        return true;
    case Motion::HolyShield:std::snprintf(line,size,"Frame holy shield: %s hand sign dropped after %d of 4 touches: %s",hand,int(f[0]),o.abandoned);return true;
    case Motion::FistOfHeavens:std::snprintf(line,size,"Frame heavens: call dropped: %s (from %.0f to %.0f deg)",o.abandoned,f[0],f[1]);return true;
    default:return false;
    }
}
}
