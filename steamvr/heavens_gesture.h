#pragma once
#include "frame_math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Paladin Fist of the Heavens (Cal, 2026-11-01): the weapon arm held straight
// out, raised about 45 degrees (the fist a little above the forehead), then
// brought down, still straight, to stop level with the shoulder. Grip held
// (a fist), trigger free. The stop is the cast. Value-only: the transport
// supplies the head and the weapon hand; the monster the arm points at is
// handed to the game (vrcam), else the game's own target pick.
//
// Unlike a weapon chop it pivots at the shoulder with the arm straight, and it
// stops at shoulder height instead of carrying on down.
namespace framevr::heavensfist {
struct Tuning {
    // Head-local (head position, head yaw only: +X right, +Y up, -Z forward).
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
    float levelHigh=14,levelLow=-35;   // degrees: the stop counts between these
    float belowMax=-50;          // passing this far below level is a chop: dropped
    float dropMin=22;            // the arm came down at least this many degrees
    float window=.90f;           // seconds from leaving the raised pose to the stop
    float sweepSpeed=.9f;        // m/s: the fist reaches this on the way down (a lowering arm doesn't)
    float stopSpeed=.8f;         // m/s: the fist this slow at level is the stop
    float stopHold=.04f;         // seconds slow at level
    float cooldown=.4f;
    float Reach(float armSpan)const{return armSpan>0?std::clamp(armSpan*reachShare,reachMin,reachMax):reachDefault;}
};
struct Input {
    double time=0;
    uint64_t session=0,epoch=0;
    bool active=false;
    math::Vec head{};math::Quat headRotation{0,0,0,1};
    math::Vec hand{},handVelocity{};   // the weapon hand, world frame
    float grip=0,armSpan=0;
    bool tracked=false,busy=false,velocityKnown=false;
};
struct Output {
    bool cast=false;
    const char* dropped=nullptr;   // a sweep in progress abandoned this step (for the log)
    float from=0,at=0;             // the sweep's start and current elevation (degrees), for the log
};
class Engine {
    enum class Phase {Open,Raised,Sweep,Spent};
    Phase phase=Phase::Open;
    uint64_t session=0,epoch=0;
    double last=0,sweepAt=0,slowAt=-1,castAt=-1;
    float top=0,peak=0;
    math::Vec previous{};bool havePrevious=false;
    static bool Finite(math::Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
    void Drop(Output& out,const char* why,float elevation){if(phase==Phase::Sweep&&!out.dropped){out.dropped=why;out.from=top;out.at=elevation;}phase=Phase::Open;slowAt=-1;}
public:
    Tuning tuning;
    void Reset(){const auto keep=tuning;*this=Engine{};tuning=keep;}
    bool Sweeping()const{return phase==Phase::Sweep;}
    Output Step(const Input& in) {
        Output out;
        if(!std::isfinite(in.time)||!Finite(in.head)){Reset();return out;}
        if(in.session!=session||in.epoch!=epoch){Reset();session=in.session;epoch=in.epoch;}
        const double dt=in.time-last;
        const bool continuous=last>0&&dt>0&&dt<=.15;
        last=in.time;
        const bool ready=in.active&&in.tracked&&!in.busy&&Finite(in.hand)&&std::isfinite(in.grip);
        if(!continuous||!ready){Drop(out,!continuous?"frame gap":"hand busy, untracked or input inactive",0);havePrevious=false;return out;}
        const auto v=in.velocityKnown?in.handVelocity:havePrevious?(in.hand-previous)*float(1/dt):math::Vec{};
        const bool speedKnown=in.velocityKnown||havePrevious;
        previous=in.hand;havePrevious=true;
        const bool fist=phase==Phase::Open||phase==Phase::Spent?in.grip>=tuning.gripOn:in.grip>tuning.gripOff;
        const auto inverse=math::Conjugate(math::Yaw(math::Heading(in.headRotation)));
        const auto local=math::Rotate(inverse,in.hand-in.head);
        const math::Vec arm{local.x-tuning.shoulderX,local.y-tuning.shoulderY,local.z-tuning.shoulderZ};
        const float length=std::sqrt(math::Dot(arm,arm)),flat=std::hypot(arm.x,arm.z),ahead=-arm.z;
        const float elevation=std::atan2(arm.y,std::max(flat,1e-4f))*57.29578f;
        const float side=std::atan2(std::fabs(arm.x),std::max(ahead,1e-4f))*57.29578f;
        const bool straightArm=length>=tuning.straight*tuning.Reach(in.armSpan);
        const bool forward=ahead>0&&side<=tuning.sideMax;
        const bool raised=straightArm&&forward&&ahead>=tuning.aheadMin&&elevation>=tuning.raisedMin&&elevation<=tuning.raisedMax;
        if(!fist){Drop(out,"fist opened",elevation);if(phase==Phase::Spent)phase=Phase::Open;return out;}
        if(castAt>=0&&in.time-castAt<tuning.cooldown)return out;
        switch(phase) {
        case Phase::Open:
        case Phase::Spent:
            // Spent: the cast's arm is still out; it loads again only once raised anew.
            // rev3 (Cal 2026-11-01: "requiring me to hold at the top ... It cant be so
            // strict"; 18 casts and no started-then-dropped calls: the misses never
            // loaded): reaching the raised pose at any speed loads it, no rest.
            if(raised){phase=Phase::Raised;top=elevation;}
            break;
        case Phase::Raised:
            if(raised){top=std::max(top,elevation);break;}
            if(elevation<top-8&&straightArm&&forward){phase=Phase::Sweep;sweepAt=in.time;slowAt=-1;peak=0;}
            else if(!straightArm||!forward)phase=Phase::Open;
            break;
        case Phase::Sweep: {
            if(elevation<tuning.belowMax){Drop(out,"carried on below the shoulder (a chop)",elevation);break;}
            if(!straightArm){Drop(out,"elbow bent: not a straight arm",elevation);break;}
            if(!forward){Drop(out,"arm swung away from the front",elevation);break;}
            if(in.time-sweepAt>tuning.window){Drop(out,"too slow to stop at the shoulder",elevation);break;}
            const float speed=speedKnown?std::sqrt(math::Dot(v,v)):0.f;
            peak=std::max(peak,speed);
            const bool level=elevation<=tuning.levelHigh&&elevation>=tuning.levelLow;
            const bool slow=speedKnown&&std::sqrt(math::Dot(v,v))<=tuning.stopSpeed;
            if(level&&slow) {
                if(slowAt<0)slowAt=in.time;
                if(in.time-slowAt>=tuning.stopHold) {
                    if(top-elevation<tuning.dropMin){Drop(out,"came down too little",elevation);break;}
                    if(peak<tuning.sweepSpeed){Drop(out,"lowered, not brought down (too slow)",elevation);break;}
                    out.cast=true;out.from=top;out.at=elevation;castAt=in.time;phase=Phase::Spent;
                }
            }
            else slowAt=-1;
            break;
        }
        }
        return out;
    }
};
}
