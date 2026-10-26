#pragma once
#include "frame_math.h"
#include <cmath>
#include <cstdint>

// Druid shapeshift gestures. Value-only: the transport supplies head and hand
// poses and pulses the slot the shape skill is bound to, so native skill
// state, mana and the transformation itself stay authoritative.
//
// Werewolf: cross both arms over the chest, bow forward, then wrench upright
// while flinging the arms up and outward.
// Werebear: raise both hands above the head, hold a moment, then slam them
// down together.
// Each performance fires once. Performing it again while transformed casts
// the skill again, which the game treats as returning to human form; the
// cooldown keeps one sloppy performance from casting twice and cancelling.
namespace framevr::shapeshift {
enum class Form { None, Werewolf, Werebear };
struct Tuning {
    // Body frame: head position, head yaw only (+X right, +Y up, -Z forward),
    // so bowing moves the hands relative to the head without tilting the box.
    float chestTop=-.08f,chestBottom=-.45f,chestNear=.03f,chestFar=.45f,chestHalfWidth=.30f;
    float crossOverlap=.05f;   // right hand at least this far left of the left hand
    float holdBehind=.20f;     // while bowing the head leans past the hands by up to this
    float bowDrop=.12f;        // head falls this far below its crossed-arms height...
    float bowPitchDeg=35;      // ...or pitches down this far
    float wrenchWindow=.6f;    // bowed -> fling must finish within this
    float flingUp=1.0f;        // both hands rising at least this fast (m/s, world)
    float flingSpread=1.0f;    // lateral separation growing at least this fast (m/s)
    float flingHeight=-.15f;   // hands end above this head-local height
    float raiseHeight=.08f;    // werebear: both hands above the head by this
    float raiseDwell=.3f;      // seconds held high; an overhead chop passes through faster
    float slamSpeed=1.8f;      // both hands falling at least this fast (m/s, world)
    float slamHeight=-.30f;    // and reaching below this head-local height
    float slamWindow=.7f;      // raised -> slam must finish within this
    float holdTimeout=2.5f;    // abandon a stance held this long
    float cooldown=1.5f;       // seconds after a cast before the next can start
};
struct Input {
    double time=0;
    uint64_t session=0,epoch=0;
    bool active=false;                        // gestures allowed this frame
    math::Vec head{};math::Quat headRotation{0,0,0,1};
    math::Vec hand[2]{},handVelocity[2]{};    // world frame; 0 right, 1 left
    bool tracked[2]{},busy[2]{},velocityKnown[2]{}; // busy: bottle held, trigger squeezed, bow drawn
};
struct Output {
    Form cast=Form::None;      // fires for one step
    Form stance=Form::None;    // a performance is in progress
    const char* dropped=nullptr; // why a performance in progress was abandoned this step (for the log)
    const char* stage=nullptr;   // phase it was abandoned from
};
class Engine {
    enum class Phase { Idle, Crossed, Bowed, Fling, Raised, Slam };
    Phase phase=Phase::Idle;
    uint64_t session=0,epoch=0;
    double last=0,phaseAt=0,castAt=-1;
    float crossedHeight=0,separation=0;
    bool upSeen=false,spreadSeen=false,slamSeen=false;
    math::Vec previous[2]{};
    bool havePrevious=false;
    static bool Finite(math::Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
    void Enter(Phase next,double now){phase=next;phaseAt=now;upSeen=spreadSeen=slamSeen=false;}
    static const char* Name(Phase p){switch(p){case Phase::Crossed:return "crossed";case Phase::Bowed:return "bowed";case Phase::Fling:return "fling";case Phase::Raised:return "raised";case Phase::Slam:return "slam";default:return "idle";}}
    void Drop(Output& out,const char* why,double now){if(phase!=Phase::Idle){out.dropped=why;out.stage=Name(phase);}Enter(Phase::Idle,now);}
public:
    Tuning tuning;
    void Reset(){const auto keep=tuning;*this=Engine{};tuning=keep;}
    Form Stance()const{return phase==Phase::Idle?Form::None:phase==Phase::Raised||phase==Phase::Slam?Form::Werebear:Form::Werewolf;}
    Output Step(const Input& in) {
        Output out;
        if(!std::isfinite(in.time)||!Finite(in.head)){Reset();return out;}
        if(in.session!=session||in.epoch!=epoch){Reset();session=in.session;epoch=in.epoch;}
        const double dt=in.time-last;
        const bool continuous=last>0&&dt>0&&dt<=.15;
        last=in.time;
        const bool ready=in.active&&in.tracked[0]&&in.tracked[1]&&!in.busy[0]&&!in.busy[1]&&Finite(in.hand[0])&&Finite(in.hand[1]);
        if(!continuous||!ready){Drop(out,!continuous?"frame gap":"hand busy, untracked or input inactive",in.time);havePrevious=false;return out;}
        if(castAt>=0&&in.time-castAt<tuning.cooldown){Enter(Phase::Idle,in.time);havePrevious=false;return out;}  // quiet: no stance can start
        const auto inverse=math::Conjugate(math::Yaw(math::Heading(in.headRotation)));
        math::Vec local[2],velocity[2];
        for(int h=0;h<2;++h) {
            local[h]=math::Rotate(inverse,in.hand[h]-in.head);
            velocity[h]=in.velocityKnown[h]?in.handVelocity[h]:havePrevious?(in.hand[h]-previous[h])*float(1/dt):math::Vec{};
            previous[h]=in.hand[h];
        }
        havePrevious=true;
        const auto forward=math::Rotate(in.headRotation,{0,0,-1});
        const float pitchDown=-std::asin(std::clamp(forward.y,-1.f,1.f))*57.29578f;
        auto inChest=[&](const math::Vec& p){return p.y<=tuning.chestTop&&p.y>=tuning.chestBottom&&-p.z>=tuning.chestNear&&-p.z<=tuning.chestFar&&std::fabs(p.x)<=tuning.chestHalfWidth;};
        const bool overlapping=local[1].x-local[0].x>=tuning.crossOverlap;
        const bool crossed=overlapping&&inChest(local[0])&&inChest(local[1]);
        // Bowing carries the head forward and down over the folded arms.
        auto nearChest=[&](const math::Vec& p){return p.y<=tuning.chestTop+.05f&&-p.z>=-tuning.holdBehind&&-p.z<=tuning.chestFar&&std::fabs(p.x)<=tuning.chestHalfWidth;};
        const bool holding=overlapping&&nearChest(local[0])&&nearChest(local[1]);
        auto bowHolding=[&](const math::Vec& p){return -p.z>=-tuning.holdBehind&&-p.z<=tuning.chestFar&&std::fabs(p.x)<=tuning.chestHalfWidth;};
        const bool raised=local[0].y>=tuning.raiseHeight&&local[1].y>=tuning.raiseHeight;
        const float spread=local[0].x-local[1].x;             // positive once the arms open
        const float spreadRate=(spread-separation)*float(1/dt);
        separation=spread;
        const double held=in.time-phaseAt;
        switch(phase) {
        case Phase::Idle:
            if(crossed){Enter(Phase::Crossed,in.time);crossedHeight=in.head.y;}
            else if(raised)Enter(Phase::Raised,in.time);
            break;
        case Phase::Crossed:
            if(!holding)Drop(out,"arms left the chest before the bow",in.time);
            else if(held>tuning.holdTimeout)Drop(out,"no bow within the hold timeout",in.time);
            else if(crossedHeight-in.head.y>=tuning.bowDrop||pitchDown>=tuning.bowPitchDeg)Enter(Phase::Bowed,in.time);
            break;
        case Phase::Bowed:
            // A deep bow brings the head down to or below the folded arms, so
            // height relative to the head says nothing here. The wrench begins
            // when the arms uncross or a hand leaves the chest sideways or
            // forward.
            if(held>tuning.holdTimeout)Drop(out,"stayed bowed past the hold timeout",in.time);
            else if(!overlapping||!bowHolding(local[0])||!bowHolding(local[1]))Enter(Phase::Fling,in.time);
            break;
        case Phase::Fling: {
            if(held>tuning.wrenchWindow){
                Drop(out,!upSeen?"fling too slow upward (both hands)":!spreadSeen?"arms did not open fast enough":"not upright with hands high in time",in.time);break;}
            if(velocity[0].y>=tuning.flingUp&&velocity[1].y>=tuning.flingUp)upSeen=true;
            if(spreadRate>=tuning.flingSpread)spreadSeen=true;
            const bool upright=crossedHeight-in.head.y<tuning.bowDrop*.5f||pitchDown<tuning.bowPitchDeg*.5f;
            const bool high=local[0].y>=tuning.flingHeight&&local[1].y>=tuning.flingHeight&&spread>0;
            if(upSeen&&spreadSeen&&upright&&high){out.cast=Form::Werewolf;castAt=in.time;Enter(Phase::Idle,in.time);}
            break;
        }
        case Phase::Raised:
            if(held>tuning.holdTimeout)Drop(out,"held overhead past the hold timeout",in.time);
            else if(!raised){if(held>=tuning.raiseDwell)Enter(Phase::Slam,in.time);else Drop(out,"hands came down before the overhead hold",in.time);}
            break;
        case Phase::Slam: {
            if(raised){Drop(out,"hands went back overhead",in.time);break;}
            if(held>tuning.slamWindow){Drop(out,!slamSeen?"slam too slow (both hands)":"slam did not reach low enough",in.time);break;}
            if(velocity[0].y<=-tuning.slamSpeed&&velocity[1].y<=-tuning.slamSpeed)slamSeen=true;
            const bool low=local[0].y<=tuning.slamHeight&&local[1].y<=tuning.slamHeight;
            if(slamSeen&&low){out.cast=Form::Werebear;castAt=in.time;Enter(Phase::Idle,in.time);}
            break;
        }
        }
        out.stance=Stance();
        return out;
    }
};
}
