#pragma once
#include "spell_motion.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// The Druid's motions: Werewolf and Werebear, watched on spellmotion::Engine as every other class's (owner,
// 2026-11-08: "We should migrate those older detectors, I think there are some other ones like Druid for example
// like that"): the warmer/colder hum as one nears its cast (Output::progress), the cast through the transport's
// motionCast (both controllers pulse), the class's order and its MOTIONS page's cards. Both grips are held while
// they are watched (the transport's gate), and the frame they open (WatchesOpening). Include spell_motion.h, not
// this. Value-only: steamvr/motions_druid_test.cpp.
//
// Werewolf: cross both arms over the chest, bow forward, then wrench upright while flinging the arms up and outward.
// Werebear: raise both hands above the head, hold a moment, then slam them down together. Each performance casts
// once. Performing it again while transformed casts the skill again, which the game treats as returning to human
// form; the rest after a cast (cooldown) keeps one sloppy performance from casting twice and cancelling.
//
// Until then both ran on one engine of their own (shapeshift_gesture.h). Its geometry, thresholds and timing are kept
// as they were, as is how it ran: in the hands' own metres (l * Engine::Body()), the head's height in the room and
// its pitch for the bow, a hand's velocity the runtime's when it gives one (Input::handVelocity) else from the last
// frame. Each form's watcher runs that engine's whole performance (Machine: either form, in its scratch), so one
// stance in progress still keeps the other from starting, and casts only its own; each logs its own stages' drops.
namespace framevr::spellmotion::druid {
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
    float cooldown=1.5f;       // seconds after a cast before the next can start (Rest)
};
inline constexpr Tuning tuning{};
// The order they are watched in (the engine's: the crossed arms checked before the raised hands).
constexpr Motion Order[]={Motion::Werewolf,Motion::Werebear};
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::Werewolf:return "cross the arms on the chest, bow, then straighten up flinging them out";
    case Motion::Werebear:return "raise both hands overhead, hold, then slam them down together";
    default:return "no motion";
    }
}
// Either form's rest after it casts, as its engine's was (the transport adds every motion's second).
inline float Rest(Motion){return tuning.cooldown;}
enum Phase {Idle,Crossed,Bowed,Fling,Raised,Slam};
inline const char* Name(int p) {
    switch(p) {
    case Crossed:return "crossed";case Bowed:return "bowed";case Fling:return "fling";case Raised:return "raised";case Slam:return "slam";
    default:return "idle";
    }
}
// The form a stage belongs to.
inline Motion FormOf(int p){return p==Crossed||p==Bowed||p==Fling?Motion::Werewolf:p==Raised||p==Slam?Motion::Werebear:Motion::None;}
// The performance (shapeshift_gesture.h's engine) in scratch: [0] its phase (Phase), [1] since when, [2] the head's
// height as the arms crossed (m, the room's), [3] the hands' spread last frame (m), [4] what the stage has seen (1
// both rising, 2 the arms opening, 4 both falling), [5] when the other form cast here (0: none; times are positive).
inline bool Machine(Engine& e,Motion form) {
    const auto& t=tuning;const auto& in=e.Current();auto& s=e.scratch;
    const auto& h=e.History();const double now=h.back().t;
    double& phase=s[0];double& phaseAt=s[1];double& crossedHeight=s[2];double& separation=s[3];double& seen=s[4];double& otherAt=s[5];
    const auto enter=[&](Phase next){phase=next;phaseAt=now;seen=0;};
    const auto drop=[&](const char* why){if(FormOf(int(phase))==form)e.Abandon(why,-1,float(phase));enter(Idle);};
    const auto saw=[&](int bit){return (int(seen)&bit)!=0;};
    const auto see=[&](int bit){seen=double(int(seen)|bit);};
    if(otherAt>0&&now-otherAt<t.cooldown){enter(Idle);return false;}   // quiet: no stance can start
    math::Vec local[2],velocity[2];
    for(int k=0;k<2;++k) {
        local[k]=h.back().l[k]*e.Body();
        bool ok=false;
        if(in.velocityKnown[k]){velocity[k]=in.handVelocity[k];ok=true;}
        else if(h.size()>=2){const auto& a=h[h.size()-2];velocity[k]=(h.back().w[k]-a.w[k])*float(1/(now-a.t));ok=true;}
        if(!ok)velocity[k]={};
    }
    const double dt=h.size()>=2?now-h[h.size()-2].t:0;
    const auto forward=math::Rotate(in.headRotation,{0,0,-1});
    const float pitchDown=-std::asin(std::clamp(forward.y,-1.f,1.f))*57.29578f;
    const auto inChest=[&](const math::Vec& p){return p.y<=t.chestTop&&p.y>=t.chestBottom&&-p.z>=t.chestNear&&-p.z<=t.chestFar&&std::fabs(p.x)<=t.chestHalfWidth;};
    const bool overlapping=local[1].x-local[0].x>=t.crossOverlap;
    const bool crossed=overlapping&&inChest(local[0])&&inChest(local[1]);
    // Bowing carries the head forward and down over the folded arms.
    const auto nearChest=[&](const math::Vec& p){return p.y<=t.chestTop+.05f&&-p.z>=-t.holdBehind&&-p.z<=t.chestFar&&std::fabs(p.x)<=t.chestHalfWidth;};
    const bool holding=overlapping&&nearChest(local[0])&&nearChest(local[1]);
    const auto bowHolding=[&](const math::Vec& p){return -p.z>=-t.holdBehind&&-p.z<=t.chestFar&&std::fabs(p.x)<=t.chestHalfWidth;};
    const bool raised=local[0].y>=t.raiseHeight&&local[1].y>=t.raiseHeight;
    const float spread=local[0].x-local[1].x;             // positive once the arms open
    const float spreadRate=dt>0?(spread-float(separation))*float(1/dt):0.f;
    separation=spread;
    const double held=now-phaseAt;
    Motion cast=Motion::None;
    switch(int(phase)) {
    case Idle:
        if(crossed){enter(Crossed);crossedHeight=in.head.y;}
        else if(raised)enter(Raised);
        break;
    case Crossed:
        if(!holding)drop("arms left the chest before the bow");
        else if(held>t.holdTimeout)drop("no bow within the hold timeout");
        else if(crossedHeight-in.head.y>=t.bowDrop||pitchDown>=t.bowPitchDeg)enter(Bowed);
        break;
    case Bowed:
        // A deep bow brings the head down to or below the folded arms, so height relative to the head says nothing
        // here. The wrench begins when the arms uncross or a hand leaves the chest sideways or forward.
        if(held>t.holdTimeout)drop("stayed bowed past the hold timeout");
        else if(!overlapping||!bowHolding(local[0])||!bowHolding(local[1]))enter(Fling);
        break;
    case Fling: {
        if(held>t.wrenchWindow) {
            drop(!saw(1)?"fling too slow upward (both hands)":!saw(2)?"arms did not open fast enough":"not upright with hands high in time");break;
        }
        if(velocity[0].y>=t.flingUp&&velocity[1].y>=t.flingUp)see(1);
        if(spreadRate>=t.flingSpread)see(2);
        const bool upright=crossedHeight-in.head.y<t.bowDrop*.5f||pitchDown<t.bowPitchDeg*.5f;
        const bool high=local[0].y>=t.flingHeight&&local[1].y>=t.flingHeight&&spread>0;
        if(saw(1)&&saw(2)&&upright&&high)cast=Motion::Werewolf;
        else if(form==Motion::Werewolf)e.Toward(.5f+.125f*float(int(saw(1))+int(saw(2))+int(upright)+int(high)));
        break;
    }
    case Raised:
        if(held>t.holdTimeout)drop("held overhead past the hold timeout");
        else if(!raised){if(held>=t.raiseDwell)enter(Slam);else drop("hands came down before the overhead hold");}
        break;
    case Slam: {
        if(raised){drop("hands went back overhead");break;}
        if(held>t.slamWindow){drop(!saw(4)?"slam too slow (both hands)":"slam did not reach low enough");break;}
        if(velocity[0].y<=-t.slamSpeed&&velocity[1].y<=-t.slamSpeed)see(4);
        const bool low=local[0].y<=t.slamHeight&&local[1].y<=t.slamHeight;
        if(saw(4)&&low)cast=Motion::Werebear;
        else if(form==Motion::Werebear)
            e.Toward(.5f+.5f*std::clamp((t.raiseHeight-std::max(local[0].y,local[1].y))/(t.raiseHeight-t.slamHeight),0.f,1.f));   // brought down
        break;
    }
    }
    if(cast==form)return true;   // the Engine starts over, then rests (Rest)
    if(cast!=Motion::None){enter(Idle);otherAt=now;return false;}   // the other form's: quiet here too, as on its engine
    // Its first part done: the arms crossed (Werewolf), the hands overhead (Werebear, warmer as they are held there).
    if(form==Motion::Werewolf&&(int(phase)==Crossed||int(phase)==Bowed))e.Toward(int(phase)==Crossed?0.f:.35f);
    if(form==Motion::Werebear&&int(phase)==Raised)e.Toward(.5f*std::min(float(now-phaseAt)/t.raiseDwell,1.f));
    return false;
}
inline bool Done(Engine& e,Motion m) {
    // The watch's first frame (a new hold, a frame gap, a reset) is only its start, as its engine's first
    // continuous frame was.
    if(e.History().size()<2||(m!=Motion::Werewolf&&m!=Motion::Werebear))return false;
    return Machine(e,m);
}
// The watch stopped (spellmotion::ClassStopped): a stage of this form in progress is given up, logged as its engine did.
inline void Stopped(Engine& e,Motion m,const char* why) {
    const int phase=int(e.scratch[0]);
    if(phase!=Idle&&FormOf(phase)==m)e.Abandon(why,-1,float(phase));
}
// Its engine's line for a performance given up (Output::abandoned): the stage it was given up from.
inline bool DropLine(Motion m,const Output& o,char* line,size_t size) {
    if(m!=Motion::Werewolf&&m!=Motion::Werebear)return false;
    std::snprintf(line,size,"Frame shapeshift: dropped at %s: %s",Name(int(o.abandonedFigures[0])),o.abandoned);
    return true;
}
}
