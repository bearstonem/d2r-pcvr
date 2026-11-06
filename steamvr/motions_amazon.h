#pragma once
#include "spell_motion.h"

// The Amazon's motions (A1-A4, owner 2026-11-04), detected on spellmotion::Engine's history (head-local:
// +X right, +Y up from the eyes, forward -Z; hand 0 the right). Both grips are held while they are watched
// (the transport's gate); a motion that completes casts its card's skill. Include spell_motion.h, not this.
// Value-only: steamvr/motions_amazon_test.cpp.
//
// Only the hands' positions are known (no palms, no fingers, the fists stay closed), so each motion is a
// path: Inner Sight a sweep out from the brow, Slow Missiles a push stopped at arm's length, Decoy both hands
// pushed from the chest to one side, Valkyrie a fist brought down from overhead. Each is kept apart from the
// others: Inner Sight never goes above the head and ends at eye level; Slow Missiles starts below the brow,
// travels forward and stops there; Decoy needs both hands; Valkyrie starts overhead.
//
// A tester's first pass (2026-11-05): "Not sure what Slow Missiles is supposed to do, but couldnt get it
// going" (its push had to end below the shoulder and be held a third of a second), "Decoy never worked for
// me either" (the sweep had 0.35 s), "Valkyrie worked but it didnt spawn her where I pointed. Was off to the
// side" (she went along the arm's line from the shoulder, levelled), and "All motions should be quick and
// repeatable sequences that don't need you to move slow". Slow Missiles' push now ends anywhere up to eye
// level and stops a moment; Decoy is the Assassin's Shadow Warrior push (0.6 s), to a spot on that side
// (Engine::LandAt); Valkyrie lands where the right hand points (Engine::LandPointed).
namespace framevr::spellmotion::amazon {
struct Tuning {
    // Inner Sight: either hand at the brow (in front of the face, the eyes' height up to just over them)
    // for browSeconds, then swept forward to sweepReach ahead within sweepWindow, mostly forward, ending
    // at eye level and never above the head.
    float browLow=-.06f,browHigh=.16f,browFront=.26f,browSide=.16f;
    double browSeconds=.08;
    float sweepReach=.45f,sweepEndLow=-.10f,overHead=.19f;
    double sweepWindow=.40;
    // Slow Missiles: the right hand pushed from below the brow (at most pushFrom ahead, below the eyes by
    // pushStartLow..pushStartHigh) to pushReach ahead within pushWindow, mostly forward, then stopped there
    // (chest to eye level) for holdSeconds, the stop begun within holdAfter of the push.
    float pushFrom=.28f,pushStartLow=-.65f,pushStartHigh=-.08f,pushReach=.42f,holdReach=.38f;
    float holdLow=-.62f,holdHigh=.08f,holdSide=.50f;
    double pushWindow=.45,holdSeconds=.15,holdAfter=.30;
    // Decoy: both hands at the chest (within chestSide of the middle, chestApart apart, chestLow to
    // chestHigh, chestNear to chestFar ahead), then pushed sideTravel or more to the same side, the leading
    // hand sideReach out and the other past the middle, mostly sideways, within decoyWindow s. She stands
    // decoyAhead cells ahead and decoyAcross to that side.
    float chestSide=.22f,chestApart=.30f,chestLow=-.50f,chestHigh=-.10f,chestNear=0,chestFar=.40f;
    float sideTravel=.30f,sideReach=.25f,decoyAhead=3,decoyAcross=6;
    double decoyWindow=.6;
    // Valkyrie: the right fist overhead (above the eyes by raiseHigh), then brought down within
    // valkyrieWindow, at least valkyrieDrop, to point ahead at shoulder height (pointReach ahead).
    float raiseHigh=.22f,valkyrieDrop=.30f,pointReach=.35f,pointLow=-.45f,pointHigh=-.02f,pointSide=.60f;
    double valkyrieWindow=.50;
    math::Vec shoulder{.17f,-.22f,.05f};   // the right shoulder, head-local: the arm's line points from it
};
inline Tuning tuning;
// The order they are watched in: the first to complete in a frame casts.
constexpr Motion Order[]={Motion::InnerSight,Motion::SlowMissiles,Motion::Decoy,Motion::Valkyrie};
// How to do each, on the MOTIONS page and in the log (what the controllers' positions can tell: no palms or
// fingers, both grips held).
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::InnerSight:return "one hand at the brow, shading the eyes, then sweep it out forward";
    case Motion::SlowMissiles:return "push the right hand straight out toward the archers and stop it there, like \"halt!\"";
    case Motion::Decoy:return "both hands to the chest, then push them out together to one side (she appears there)";
    case Motion::Valkyrie:return "raise the right fist high, then bring it down to point at the floor where she lands";
    default:return "no motion";
    }
}
namespace detail {
inline bool AtBrow(const math::Vec& l,const Tuning& t) {
    return l.y>=t.browLow&&l.y<=t.browHigh&&Engine::Forward(l)>=-.02f&&Engine::Forward(l)<=t.browFront&&std::fabs(l.x)<=t.browSide;
}
inline bool InnerSight(Engine& e,const Tuning& t) {
    const auto& h=e.History();const auto& n=h.back();
    for(int k=0;k<2;++k) {
        const auto& c=n.l[k];
        if(c.y>t.overHead)continue;
        // The last stretch at the brow, sweepWindow or less before now, kept browSeconds; never overhead since.
        double browEnd=-1,browStart=-1;bool high=false;
        for(auto it=h.rbegin();it!=h.rend();++it) {
            const auto& s=*it;
            if(n.t-s.t>t.sweepWindow+t.browSeconds+.5)break;
            if(s.l[k].y>t.overHead){high=true;break;}
            if(AtBrow(s.l[k],t)) {
                if(browEnd<0){if(n.t-s.t>t.sweepWindow)break;browEnd=s.t;}
                browStart=s.t;
            } else if(browEnd>=0)break;
        }
        if(high||browEnd<0||browEnd-browStart<t.browSeconds)continue;
        // mostly forward from the brow
        const Sample* from=nullptr;for(const auto& s:h)if(s.t==browEnd){from=&s;break;}
        if(!from)continue;
        const auto& a=from->l[k];
        const float forward=Engine::Forward(c)-Engine::Forward(a);
        e.Toward(forward/std::max(.15f,t.sweepReach-Engine::Forward(a)));   // at the brow, then warmer as it sweeps out
        if(Engine::Forward(c)<t.sweepReach||c.y<t.sweepEndLow)continue;
        if(forward>=.15f&&std::fabs(c.y-a.y)*1.5f<=forward&&std::fabs(c.x-a.x)*1.5f<=forward)return true;
    }
    return false;
}
inline bool SlowMissiles(Engine& e,const Tuning& t) {
    const auto& h=e.History();const auto& n=h.back();
    const auto& c=n.l[0];
    // the push: arm's length reached from below the brow, mostly forward, within pushWindow (scratch[0]: when)
    float pushed=0;   // the farthest forward from such a start, for the buzz
    for(const auto& s:h) {
        const auto& a=s.l[0];
        const float forward=Engine::Forward(c)-Engine::Forward(a);
        if(n.t-s.t>t.pushWindow||Engine::Forward(a)>t.pushFrom||a.y<t.pushStartLow||a.y>t.pushStartHigh||
           std::fabs(c.y-a.y)*1.5f>forward||std::fabs(c.x-a.x)*1.5f>forward)continue;
        pushed=std::max(pushed,forward);
        if(Engine::Forward(c)>=t.pushReach&&forward>=.18f){e.scratch[0]=n.t;break;}
    }
    if(pushed>=.06f)e.Toward(.5f*std::min(pushed/.18f,Engine::Forward(c)/t.pushReach));
    const double at=e.scratch[0];
    const bool pose=at>0&&Engine::Forward(c)>=t.holdReach&&c.y>=t.holdLow&&c.y<=t.holdHigh&&std::fabs(c.x)<=t.holdSide&&e.Still(0);
    if(!pose){e.Held(false,n.t);if(at>0&&n.t-at>t.holdAfter+t.holdSeconds)e.scratch[0]=0;return false;}
    e.Toward(.5f);   // pushed out: the stop to come (Held, warmer as it lasts)
    return e.Held(true,n.t,t.holdSeconds);
}
inline bool Decoy(Engine& e,const Tuning& t) {
    const auto& h=e.History();const auto& n=h.back();
    const auto& r=n.l[0];const auto& l=n.l[1];
    const auto chest=[&](math::Vec p){return std::fabs(p.x)<=t.chestSide&&p.y>=t.chestLow&&p.y<=t.chestHigh&&
        Engine::Forward(p)>=t.chestNear&&Engine::Forward(p)<=t.chestFar;};
    if(chest(r)&&chest(l)&&Engine::Apart(n)<=t.chestApart)e.Toward(0);   // at the chest: the push to come
    for(const int side:{1,-1}) {
        // the leading hand: the right going right, the left going left
        const auto& lead=side>0?r:l;const auto& trail=side>0?l:r;
        for(const auto& s:h) {
            if(n.t-s.t>t.decoyWindow||!chest(s.l[0])||!chest(s.l[1])||Engine::Apart(s)>t.chestApart)continue;
            float least=1e9f;bool sideways=true;math::Vec way{};
            for(int k=0;k<2;++k) {
                const auto d=n.l[k]-s.l[k];least=std::min(least,d.x*float(side));
                sideways=sideways&&d.x*float(side)>=1.2f*std::fabs(d.z)&&std::fabs(d.y)<=.25f;way=way+d*.5f;
            }
            if(!sideways||least<.06f)continue;
            e.Toward(least/t.sideTravel);
            if(least>=t.sideTravel&&lead.x*float(side)>=t.sideReach&&trail.x*float(side)>=.02f) {
                e.Aim({way.x,0,way.z});e.LandAt(t.decoyAhead,float(side)*t.decoyAcross);
                return true;
            }
        }
    }
    return false;
}
inline bool Valkyrie(Engine& e,const Tuning& t) {
    const auto& h=e.History();const auto& n=h.back();
    const auto& c=n.l[0];
    float drop=-1;   // the most the fist came down from overhead within the window
    for(const auto& s:h)if(n.t-s.t<=t.valkyrieWindow&&s.l[0].y>=t.raiseHigh)drop=std::max(drop,s.l[0].y-c.y);
    if(drop<0)return false;
    e.Toward(drop/t.valkyrieDrop);   // raised: warmer as it comes down
    if(drop<t.valkyrieDrop||Engine::Forward(c)<t.pointReach||c.y<t.pointLow||c.y>t.pointHigh||std::fabs(c.x)>t.pointSide)return false;
    e.Aim(c-t.shoulder);   // the arm's line: her way when the hand points at no floor
    e.LandPointed();
    return true;
}
}
inline bool Done(Engine& e,Motion m) {
    switch(m) {
    case Motion::InnerSight:return detail::InnerSight(e,tuning);
    case Motion::SlowMissiles:return detail::SlowMissiles(e,tuning);
    case Motion::Decoy:return detail::Decoy(e,tuning);
    case Motion::Valkyrie:return detail::Valkyrie(e,tuning);
    default:return false;
    }
}
}
