#pragma once
#include "spell_motion.h"

// The Necromancer's motions (N1-N8, owner 2026-11-04), detected on spellmotion::Engine's history (head-local:
// +X right, +Y up from the eyes, forward -Z; hand 0 the right). Both grips are held while they are watched
// (the transport's gate); a motion that completes casts its card's skill. Include spell_motion.h, not this.
//
// Only the controllers' positions count: no palm, finger or fist. So the table's "clench" (Amplify Damage)
// is a short sharp yank back, its "fling open" (Corpse Explosion) a jab forward, and Lower Resist's "palm
// down" a chop down, each from the right hand held out still at the corpse or monster (the pointing
// start), so it ends pointing where the right hand's own ray aims the cast. Golem goes to a spot ahead
// (Aim, LandAt); the rest have no way of their own.
//
// A tester's first pass (2026-11-05): "big issues with summoning skeletons and golem. I only got the
// skeleton one to work once and the golem one I could never get right. Also, any motions that require a
// slow movement as part of the sequence are not going to work well in combat. All motions should be quick
// and repeatable". Raise Skeleton was a slow lift from the hips, Golem a press toward the floor and Lower
// Resist a slow drag: now Raise Skeleton is a fast lift that stops at the chest, Golem both fists slammed
// down from overhead, Lower Resist a chop down, and the point before the three is held a moment less.
// Value-only: steamvr/motions_necromancer_test.cpp.
namespace framevr::spellmotion::necromancer {
struct Tuning {
    // N1 Bone Armor: each fist on the other side of the chest (the right this far left of the left),
    // between these heights, at most armorNear ahead of the eyes, held armorHold s.
    float armorCross=.04f,armorHigh=-.12f,armorLow=-.60f,armorNear=.30f,armorApart=.35f,armorHold=.40f;
    // The pointing start (N3, N5, N6): the right hand at least pointFrom ahead, held still (every step
    // under pointStill m/s) for pointHold s.
    // up to pointHigh above the eyes (the owner chopped from a hand raised over eye level, 2026-11-05).
    float pointFrom=.30f,pointHold=.15f,pointStill=.35f,pointHigh=.25f;
    float jab=.12f,jabWindow=.20f;     // N3 Corpse Explosion: then this far forward within the window
    float yank=.15f,yankWindow=.20f;   // N5 Amplify Damage: then this far back toward the eyes
    // N6 Lower Resist: then this far down, its fall at least as large as its move back toward the body (the
    // owner's chops arc down and in, 2026-11-05: 16-19 cm down, coming in as far; Amplify Damage's yank is
    // mostly back, at most 0.6 down for its pull, so the two keep apart).
    // The chop at chopSpeed or faster at its fastest: the arm let down after pointing is no chop (the owner's
    // were 7-8 m/s).
    float chop=.15f,chopWindow=.30f,chopSpeed=1.8f;
    // N4 Golem: both fists overhead (golemHigh above the eyes or more), then slammed down within golemWindow,
    // each golemFall or more, to golemLow or below, in front of the body or by its sides (the owner's slam
    // ended at the sides, 2026-11-05), at slamSpeed or faster at their fastest. It stands golemAhead cells ahead.
    float golemHigh=.15f,golemFall=.40f,golemLow=-.28f,golemWindow=.6f,slamSpeed=1.2f,golemAhead=4;
    // N2 Raise Skeleton: both hands low (riseFrom or below: at rest by the hips), then lifted fast, riseMin
    // or more each, to the chest (riseTo to riseTop) within riseWindow, riseSpeed or faster at their
    // fastest, and stopped there (both still): a lift on overhead passes the chest without stopping. Each
    // rises riseUpright times as far as it moves across, and they end at most riseApart apart, side by side (the
    // right hand riseCross or less left of the left: crossed is Bone Armor). The owner's headset session
    // (2026-11-05) widened them: lifts ended 58 cm apart, up to 7 cm over the eyes, the hands overlapping a
    // centimetre; Golem's fists, raised 25-40 cm over the eyes then, start at golemHigh, above riseTop.
    float riseFrom=-.48f,riseTo=-.36f,riseTop=.12f,riseMin=.18f,riseWindow=.7f,riseSpeed=1.0f,riseUpright=1.5f,riseApart=.70f,riseCross=.03f;
    // N7 Poison Nova: hands together at the chest, then flung novaApart apart, each novaSide out on its
    // own side, at any height up to novaHigh (the owner's flings, 2026-11-05: 130-160 cm wide at shoulder
    // height; it once had to end below the chest), within novaWindow and at least flingSpeed fast.
    float novaTogether=.30f,novaApart=.75f,novaSide=.25f,novaHigh=.10f,novaWindow=.8f,flingSpeed=1.5f;
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
    case Motion::RaiseSkeleton:return "both hands down by the hips, then lift them fast to the chest and stop";
    case Motion::CorpseExplosion:return "hold the right hand out at a corpse, then jab it forward";
    case Motion::Golem:return "raise both fists over the head, then slam them down in front of you";
    case Motion::AmplifyDamage:return "hold the right hand out at a monster, then yank it back";
    case Motion::LowerResist:return "hold the right hand out at a monster, then chop it down";
    case Motion::PoisonNova:return "hands together at the chest, then fling them out wide";
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
    return Engine::Forward(r)>=tuning.pointFrom&&r.y>=-.60f&&r.y<=tuning.pointHigh&&std::fabs(r.x)<=.45f&&
        StillBefore(s,i,0,tuning.pointHold,tuning.pointStill);
}
// A pointing motion: from a pointing start no more than window s ago, does end hold for the start and the newest?
// The hand pointing now is the first part done (Engine::Toward).
template<class End> bool FromPoint(Engine& e,const std::vector<Sample>& s,double window,End end) {
    const auto& n=s.back();
    if(Pointing(s,s.size()-1))e.Toward(0);
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
        // from the latest moment both hands were low, lifted fast
        bool end=Engine::Apart(n)<=t.riseApart&&n.l[0].x>=n.l[1].x-t.riseCross&&e.Still(0)&&e.Still(1);
        for(const auto& h:n.l)end=end&&h.y>=t.riseTo&&h.y<=t.riseTop&&F(h)>=0&&std::fabs(h.x)<=.45f;
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.riseWindow;) {
            const auto& a=s[i];
            if(a.l[0].y>t.riseFrom||a.l[1].y>t.riseFrom||a.l[0].y<-1.f||a.l[1].y<-1.f)continue;
            const float lift=std::min(n.l[0].y-a.l[0].y,n.l[1].y-a.l[1].y);
            if(lift>=.06f)e.Toward(lift/(t.riseTo-a.l[0].y));
            bool up=true;   // mostly up, each hand: Bone Prison's hands also rise as they open out wide
            for(int k=0;k<2;++k)up=up&&n.l[k].y-a.l[k].y>=t.riseUpright*std::hypot(n.l[k].x-a.l[k].x,n.l[k].z-a.l[k].z);
            return end&&up&&lift>=t.riseMin&&detail::PeakSpeed(s,i,0)>=t.riseSpeed&&detail::PeakSpeed(s,i,1)>=t.riseSpeed;
        }
        return false;
    }
    case Motion::CorpseExplosion:
        return FromPoint(e,s,t.jabWindow,[&](const Sample& a,size_t){
            const auto& c=n.l[0];const auto& o=a.l[0];const float forward=F(c)-F(o);
            if(forward>=.03f)e.Toward(forward/t.jab);
            return forward>=t.jab&&std::fabs(c.y-o.y)<=.6f*forward&&std::fabs(c.x-o.x)<=.6f*forward;});
    case Motion::AmplifyDamage:
        return FromPoint(e,s,t.yankWindow,[&](const Sample& a,size_t){
            const auto& c=n.l[0];const auto& o=a.l[0];const float back=F(o)-F(c);
            if(back>=.03f)e.Toward(back/t.yank);
            return back>=t.yank&&std::fabs(c.y-o.y)<=.6f*back&&std::fabs(c.x-o.x)<=.6f*back;});
    case Motion::LowerResist:
        return FromPoint(e,s,t.chopWindow,[&](const Sample& a,size_t i){
            const auto& c=n.l[0];const auto& o=a.l[0];const float down=o.y-c.y;
            if(down>=.04f)e.Toward(down/t.chop);
            return down>=t.chop&&std::fabs(F(c)-F(o))<=down&&std::fabs(c.x-o.x)<=.6f*down&&detail::PeakSpeed(s,i,0)>=t.chopSpeed;});
    case Motion::Golem: {
        // from the latest moment both fists were overhead, slammed down in front
        bool end=true;
        for(const auto& h:n.l)end=end&&h.y<=t.golemLow&&h.y>=-.95f&&F(h)>=-.40f&&std::fabs(h.x)<=.55f;
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.golemWindow;) {
            const auto& a=s[i];
            if(a.l[0].y<t.golemHigh||a.l[1].y<t.golemHigh)continue;
            const float fall=std::min(a.l[0].y-n.l[0].y,a.l[1].y-n.l[1].y);
            e.Toward(fall/t.golemFall);   // overhead, then warmer as they come down
            if(!end||fall<t.golemFall||detail::PeakSpeed(s,i,0)<t.slamSpeed||detail::PeakSpeed(s,i,1)<t.slamSpeed)return false;
            e.Aim({0,0,-1});e.LandAt(t.golemAhead,0);
            return true;
        }
        return false;
    }
    case Motion::PoisonNova: {
        const bool end=Engine::Apart(n)>=t.novaApart&&n.l[0].x>=t.novaSide&&n.l[1].x<=-t.novaSide&&n.l[0].y<=t.novaHigh&&n.l[1].y<=t.novaHigh;
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.novaWindow;) {
            const auto& a=s[i];
            bool chest=Engine::Apart(a)<=t.novaTogether;
            for(const auto& h:a.l)chest=chest&&h.y>=-.60f&&h.y<=-.10f&&F(h)>=-.10f;
            if(!chest)continue;
            e.Toward((Engine::Apart(n)-t.novaTogether)/(t.novaApart-t.novaTogether));
            return end&&detail::PeakSpeed(s,i,0)>=t.flingSpeed&&detail::PeakSpeed(s,i,1)>=t.flingSpeed;
        }
        return false;
    }
    case Motion::BonePrison: {
        bool end=Engine::Apart(n)<=t.prisonTogether&&n.l[0].x>=n.l[1].x-.05f;
        for(const auto& h:n.l)end=end&&F(h)>=t.prisonFront&&h.y>=-.60f&&h.y<=0;
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.prisonWindow;) {
            const auto& a=s[i];
            bool open=Engine::Apart(a)>=t.prisonApart&&a.l[0].x>a.l[1].x;
            for(const auto& h:a.l)open=open&&F(h)>=t.prisonFront-.05f&&h.y>=-.65f&&h.y<=.05f;
            if(!open)continue;
            e.Toward((Engine::Apart(a)-Engine::Apart(n))/(Engine::Apart(a)-t.prisonTogether));
            if(end&&a.l[0].x-n.l[0].x>=t.prisonIn&&n.l[1].x-a.l[1].x>=t.prisonIn&&
               detail::PeakSpeed(s,i,0)>=t.closeSpeed&&detail::PeakSpeed(s,i,1)>=t.closeSpeed)return true;
        }
        return false;
    }
    default:return false;
    }
}
}
