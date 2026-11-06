#pragma once
#include "spell_motion.h"

// The Amazon's motions (A1-A4, owner 2026-11-04), detected on spellmotion::Engine's history (head-local:
// +X right, +Y up from the eyes, forward -Z; hand 0 the right). Both grips are held while they are watched
// (the transport's gate); a motion that completes casts its card's skill. Include spell_motion.h, not this.
// Value-only: steamvr/motions_amazon_test.cpp.
//
// Only the hands' positions are known (no palms, no fingers, the fists stay closed), so each motion is a
// path: Inner Sight a sweep out from the brow, Slow Missiles a push held at arm's length, Decoy both hands
// swept from the chest to one side, Valkyrie a fist brought down from overhead. Each is kept apart from the
// others: Inner Sight never goes above the head and ends at eye level; Slow Missiles starts at the chest,
// travels forward and must be held; Decoy needs both hands; Valkyrie starts overhead.
namespace framevr::spellmotion::amazon {
struct Tuning {
    // Inner Sight: either hand at the brow (in front of the face, the eyes' height up to just over them)
    // for browSeconds, then swept forward to sweepReach ahead within sweepWindow, mostly forward, ending
    // at eye level and never above the head.
    float browLow=-.06f,browHigh=.16f,browFront=.26f,browSide=.16f;
    double browSeconds=.08;
    float sweepReach=.45f,sweepEndLow=-.10f,overHead=.19f;
    double sweepWindow=.40;
    // Slow Missiles: the right hand pushed from the chest (at most pushFrom ahead, below the eyes by
    // pushStartLow..pushStartHigh) to pushReach ahead within pushWindow, mostly forward, then held still
    // there (chest to shoulder height) for holdSeconds, the hold begun within holdAfter of the push.
    float pushFrom=.25f,pushStartLow=-.58f,pushStartHigh=-.10f,pushReach=.45f,holdReach=.40f;
    float holdLow=-.58f,holdHigh=-.15f,holdSide=.45f;
    double pushWindow=.45,holdSeconds=.35,holdAfter=.30;
    // Decoy: both hands together in front of the chest (each within chestNear of chestPoint, chestApart
    // apart), then swept together to one side within decoyWindow: each this far across, ending with their
    // middle sideReach out, still near each other.
    math::Vec chestPoint{0,-.38f,-.25f};
    float chestNear=.22f,chestApart=.32f,sideTravel=.22f,sideReach=.30f,sideApart=.50f;
    double decoyWindow=.35;
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
    case Motion::SlowMissiles:return "push the right hand straight out from the chest toward the archers and hold it";
    case Motion::Decoy:return "both hands to the chest, then sweep them out together to one side";
    case Motion::Valkyrie:return "raise the right fist high, then bring it down to point where she lands";
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
        if(Engine::Forward(c)<t.sweepReach||c.y<t.sweepEndLow||c.y>t.overHead)continue;
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
        const float forward=Engine::Forward(c)-Engine::Forward(from->l[k]);
        if(forward>=.15f&&std::fabs(c.y-from->l[k].y)*1.5f<=forward&&std::fabs(c.x-from->l[k].x)*1.5f<=forward)return true;
    }
    return false;
}
inline bool SlowMissiles(Engine& e,const Tuning& t) {
    const auto& h=e.History();const auto& n=h.back();
    const auto& c=n.l[0];
    // the push: arm's length reached from the chest, mostly forward, within pushWindow (scratch[0]: when)
    if(Engine::Forward(c)>=t.pushReach) {
        for(const auto& s:h) {
            const auto& a=s.l[0];
            const float forward=Engine::Forward(c)-Engine::Forward(a);
            if(n.t-s.t<=t.pushWindow&&Engine::Forward(a)<=t.pushFrom&&a.y>=t.pushStartLow&&a.y<=t.pushStartHigh&&forward>=.18f&&
               std::fabs(c.y-a.y)*1.5f<=forward&&std::fabs(c.x-a.x)*1.5f<=forward){e.scratch[0]=n.t;break;}
        }
    }
    const double pushed=e.scratch[0];
    const bool pose=pushed>0&&Engine::Forward(c)>=t.holdReach&&c.y>=t.holdLow&&c.y<=t.holdHigh&&std::fabs(c.x)<=t.holdSide&&e.Still(0);
    if(!pose){e.Held(false,n.t);if(pushed>0&&n.t-pushed>t.holdAfter+t.holdSeconds)e.scratch[0]=0;return false;}
    return e.Held(true,n.t,t.holdSeconds);
}
inline math::Vec Middle(const Sample& s){return (s.l[0]+s.l[1])*.5f;}
inline bool Decoy(Engine& e,const Tuning& t) {
    const auto& h=e.History();const auto& n=h.back();
    const auto end=Middle(n);
    if(std::fabs(end.x)<t.sideReach||Engine::Apart(n)>t.sideApart)return false;
    const float side=end.x>0?1.f:-1.f;
    for(int k=0;k<2;++k){const auto& l=n.l[k];if(Engine::Forward(l)<-.10f||l.y<-.80f||l.y>.05f)return false;}
    for(const auto& s:h) {
        if(n.t-s.t>t.decoyWindow)continue;
        if(Engine::Apart(s)>t.chestApart||Engine::Len(s.l[0]-t.chestPoint)>t.chestNear||Engine::Len(s.l[1]-t.chestPoint)>t.chestNear)continue;
        if((n.l[0].x-s.l[0].x)*side<t.sideTravel||(n.l[1].x-s.l[1].x)*side<t.sideTravel)continue;
        e.Aim(end-Middle(s));
        return true;
    }
    return false;
}
inline bool Valkyrie(Engine& e,const Tuning& t) {
    const auto& h=e.History();const auto& n=h.back();
    const auto& c=n.l[0];
    if(Engine::Forward(c)<t.pointReach||c.y<t.pointLow||c.y>t.pointHigh||std::fabs(c.x)>t.pointSide)return false;
    for(const auto& s:h)
        if(n.t-s.t<=t.valkyrieWindow&&s.l[0].y>=t.raiseHigh&&s.l[0].y-c.y>=t.valkyrieDrop){e.Aim(c-t.shoulder);return true;}
    return false;
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
