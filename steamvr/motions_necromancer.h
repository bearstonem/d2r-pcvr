#pragma once
#include "spell_motion.h"

// The Necromancer's motions (N1-N8, owner 2026-11-04), detected on spellmotion::Engine's history (head-local:
// +X right, +Y up from the eyes, forward -Z; hand 0 the right). Both grips are held while they are watched
// (the transport's gate); a motion that completes casts its card's skill. Include spell_motion.h, not this.
//
// Only the controllers' positions count: no palm, finger or fist. So the table's "clench" (Amplify Damage)
// is a short sharp yank back, its "fling open" (Corpse Explosion) a jab forward, and Lower Resist's "palm
// down" a slow drag down, each from the right hand held out still at the corpse or monster (the pointing
// start), so it ends pointing where the right hand's own ray aims the cast. Golem goes to the spot ahead
// (Aim); the rest have no way of their own.
// Value-only: steamvr/motions_necromancer_test.cpp.
namespace framevr::spellmotion::necromancer {
struct Tuning {
    // N1 Bone Armor: each fist on the other side of the chest (the right this far left of the left),
    // between these heights, at most armorNear ahead of the eyes, held armorHold s.
    float armorCross=.04f,armorHigh=-.12f,armorLow=-.60f,armorNear=.30f,armorApart=.35f,armorHold=.40f;
    // The pointing start (N3, N5, N6): the right hand at least pointFrom ahead, held still (every step
    // under pointStill m/s) for pointHold s.
    float pointFrom=.30f,pointHold=.25f,pointStill=.35f;
    float jab=.12f,jabWindow=.20f;     // N3 Corpse Explosion: then this far forward within the window
    float yank=.15f,yankWindow=.20f;   // N5 Amplify Damage: then this far back toward the eyes
    float drag=.20f,dragMin=.30f,dragWindow=1.2f,dragSpeed=1.3f;   // N6 Lower Resist: this far down, slowly
    // N4 Golem: both hands pressed pressDrop down to below pressLow within pressWindow, then lifted
    // liftBack within liftWindow.
    float pressLow=-.75f,pressDrop=.20f,pressWindow=.6f,liftBack=.20f,liftWindow=.6f;
    // N2 Raise Skeleton: both hands low (below riseFrom) and still for restStill s, then lifted riseMin
    // each to above riseTo, taking at least riseSlow s and never faster than riseSpeed.
    float riseFrom=-.62f,riseTo=-.42f,riseMin=.25f,riseSlow=.45f,riseWindow=1.5f,riseSpeed=1.2f,restStill=.15f;
    // N7 Poison Nova: hands together at the chest, then flung novaApart apart, each novaSide out on its
    // own side and below novaLow, within novaWindow and at least flingSpeed fast.
    float novaTogether=.30f,novaApart=.75f,novaSide=.25f,novaLow=-.40f,novaWindow=.8f,flingSpeed=1.5f;
    // N8 Bone Prison: both hands out in front prisonApart apart, then brought within prisonTogether,
    // each in by prisonIn, within prisonWindow and at least closeSpeed fast.
    float prisonApart=.55f,prisonTogether=.20f,prisonFront=.30f,prisonIn=.15f,prisonWindow=.9f,closeSpeed=1.0f;
};
inline constexpr Tuning tuning{};
// The order they are watched in: the first to complete in a frame casts. Golem before Raise Skeleton
// (a press lifted slowly to the chest is the golem's); the three pointing motions end apart.
constexpr Motion Order[]={Motion::BoneArmor,Motion::BonePrison,Motion::Golem,Motion::RaiseSkeleton,Motion::PoisonNova,
    Motion::CorpseExplosion,Motion::AmplifyDamage,Motion::LowerResist};
// How to do each, on the MOTIONS page and in the log (what the controllers' positions can tell: no palms or
// fingers, both grips held).
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::BoneArmor:return "cross both fists on the chest and hold";
    case Motion::RaiseSkeleton:return "both hands low at the hips, then lift them slowly to the chest";
    case Motion::CorpseExplosion:return "hold the right hand out at a corpse, then jab it forward";
    case Motion::Golem:return "press both hands down toward the floor, then lift them";
    case Motion::AmplifyDamage:return "hold the right hand out at a monster, then yank it back";
    case Motion::LowerResist:return "hold the right hand out at a monster, then drag it down slowly";
    case Motion::PoisonNova:return "hands together at the chest, then fling them out low and wide";
    case Motion::BonePrison:return "hold both hands apart in front, then bring them together round a monster";
    default:return "no motion";
    }
}
namespace detail {
inline float Speed(const Sample& a,const Sample& b,int h){const double dt=b.t-a.t;return dt>1e-4?Engine::Len(b.l[h]-a.l[h])/float(dt):0.f;}
// Hand h (both, h<0) still up to sample i: every step in the seconds before it slower than speed, with the
// history reaching back that far.
inline bool StillBefore(const std::vector<Sample>& s,size_t i,int h,double seconds,float speed) {
    if(s[i].t-s.front().t<seconds)return false;
    for(size_t k=i;k>0&&s[i].t-s[k-1].t<=seconds;--k)
        for(int g=0;g<2;++g)if((h<0||g==h)&&Speed(s[k-1],s[k],g)>=speed)return false;
    return true;
}
// The fastest step of hand h (either, h<0) from sample i to the newest.
inline float PeakSpeed(const std::vector<Sample>& s,size_t i,int h) {
    float most=0;
    for(size_t k=i+1;k<s.size();++k)for(int g=0;g<2;++g)if(h<0||g==h)most=std::max(most,Speed(s[k-1],s[k],g));
    return most;
}
// The right hand held out still at a corpse or a monster: where the pointing motions begin.
inline bool Pointing(const std::vector<Sample>& s,size_t i) {
    const auto& r=s[i].l[0];
    return Engine::Forward(r)>=tuning.pointFrom&&r.y>=-.60f&&r.y<=.05f&&std::fabs(r.x)<=.45f&&
        StillBefore(s,i,0,tuning.pointHold,tuning.pointStill);
}
// A pointing motion: from a pointing start no more than window s ago, does end hold for the start and the newest?
template<class End> bool FromPoint(const std::vector<Sample>& s,double window,End end) {
    const auto& n=s.back();
    for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=window;)
        if(Pointing(s,i)&&end(s[i],i))return true;
    return false;
}
}
inline bool Done(Engine& e,Motion m) {
    using detail::FromPoint;
    const auto& s=e.History();
    if(s.empty())return false;
    const auto& n=s.back();
    const auto& t=tuning;
    const auto F=[](math::Vec l){return Engine::Forward(l);};
    switch(m) {
    case Motion::BoneArmor: {
        const auto& r=n.l[0];const auto& l=n.l[1];
        bool pose=r.x<=l.x-t.armorCross&&Engine::Apart(n)<=t.armorApart;
        for(const auto& h:n.l)pose=pose&&h.y>=t.armorLow&&h.y<=t.armorHigh&&F(h)>=-.20f&&F(h)<=t.armorNear;
        return e.Held(pose&&e.Still(0)&&e.Still(1),n.t,t.armorHold);
    }
    case Motion::RaiseSkeleton: {
        // up at the chest, side by side in front, the right hand on the right
        for(const auto& h:n.l)if(h.y<t.riseTo||F(h)<0||std::fabs(h.x)>.45f)return false;
        if(Engine::Apart(n)>.70f||n.l[0].x<=n.l[1].x)return false;
        // from the latest moment both hands were low and still, slowly
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.riseWindow;) {
            const auto& a=s[i];
            if(a.l[0].y>t.riseFrom||a.l[1].y>t.riseFrom||a.l[0].y<-1.f||a.l[1].y<-1.f)continue;
            if(!detail::StillBefore(s,i,-1,t.restStill,t.pointStill))continue;
            return n.t-a.t>=t.riseSlow&&n.l[0].y-a.l[0].y>=t.riseMin&&n.l[1].y-a.l[1].y>=t.riseMin&&
                detail::PeakSpeed(s,i,-1)<=t.riseSpeed;
        }
        return false;
    }
    case Motion::CorpseExplosion:
        return FromPoint(s,t.jabWindow,[&](const Sample& a,size_t){
            const auto& c=n.l[0];const auto& o=a.l[0];const float forward=F(c)-F(o);
            return forward>=t.jab&&std::fabs(c.y-o.y)<=.6f*forward&&std::fabs(c.x-o.x)<=.6f*forward;});
    case Motion::AmplifyDamage:
        return FromPoint(s,t.yankWindow,[&](const Sample& a,size_t){
            const auto& c=n.l[0];const auto& o=a.l[0];const float back=F(o)-F(c);
            return back>=t.yank&&std::fabs(c.y-o.y)<=.6f*back&&std::fabs(c.x-o.x)<=.6f*back;});
    case Motion::LowerResist:
        return FromPoint(s,t.dragWindow,[&](const Sample& a,size_t i){
            const auto& c=n.l[0];const auto& o=a.l[0];
            return o.y-c.y>=t.drag&&n.t-a.t>=t.dragMin&&F(c)>=t.pointFrom-.07f&&F(o)-F(c)<=.12f&&std::fabs(c.x-o.x)<=.15f&&
                detail::PeakSpeed(s,i,0)<=t.dragSpeed;});
    case Motion::Golem: {
        if(n.t-s.front().t<.1)return false;
        for(size_t j=s.size()-1;j-->0&&n.t-s[j].t<=t.liftWindow;) {
            const auto& lo=s[j];
            bool low=Engine::Apart(lo)<=.75f;
            for(const auto& h:lo.l)low=low&&h.y<=t.pressLow&&F(h)>=-.15f&&std::fabs(h.x)<=.50f;
            if(!low||n.l[0].y-lo.l[0].y<t.liftBack||n.l[1].y-lo.l[1].y<t.liftBack)continue;
            for(size_t k=j;k-->0&&lo.t-s[k].t<=t.pressWindow;)
                if(s[k].l[0].y-lo.l[0].y>=t.pressDrop&&s[k].l[1].y-lo.l[1].y>=t.pressDrop){e.Aim({0,0,-1});return true;}
        }
        return false;
    }
    case Motion::PoisonNova: {
        if(Engine::Apart(n)<t.novaApart||n.l[0].x<t.novaSide||n.l[1].x>-t.novaSide||n.l[0].y>t.novaLow||n.l[1].y>t.novaLow)return false;
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.novaWindow;) {
            const auto& a=s[i];
            bool chest=Engine::Apart(a)<=t.novaTogether;
            for(const auto& h:a.l)chest=chest&&h.y>=-.60f&&h.y<=-.10f&&F(h)>=-.10f;
            if(chest)return detail::PeakSpeed(s,i,0)>=t.flingSpeed&&detail::PeakSpeed(s,i,1)>=t.flingSpeed;
        }
        return false;
    }
    case Motion::BonePrison: {
        if(Engine::Apart(n)>t.prisonTogether||n.l[0].x<n.l[1].x-.05f)return false;
        for(const auto& h:n.l)if(F(h)<t.prisonFront||h.y<-.60f||h.y>0)return false;
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.prisonWindow;) {
            const auto& a=s[i];
            bool open=Engine::Apart(a)>=t.prisonApart&&a.l[0].x>a.l[1].x&&a.l[0].x-n.l[0].x>=t.prisonIn&&n.l[1].x-a.l[1].x>=t.prisonIn;
            for(const auto& h:a.l)open=open&&F(h)>=t.prisonFront-.05f&&h.y>=-.65f&&h.y<=.05f;
            if(open&&detail::PeakSpeed(s,i,0)>=t.closeSpeed&&detail::PeakSpeed(s,i,1)>=t.closeSpeed)return true;
        }
        return false;
    }
    default:return false;
    }
}
}
