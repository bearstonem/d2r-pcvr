#pragma once
#include "spell_motion.h"

// The Necromancer's motions (N1-N8, owner 2026-11-04), detected on spellmotion::Engine's history (head-local:
// +X right, +Y up from the eyes, forward -Z; hand 0 the right). Both grips are held while they are watched
// (the transport's gate); a motion that completes casts its card's skill. Include spell_motion.h, not this.
//
// Only the controllers' positions count: no palm, finger or fist. So the table's "clench" (Amplify Damage),
// "fling open" (Corpse Explosion) and "palm down" (Lower Resist) are punches: the right fist, both fists
// together, the left fist (the owner's, 2026-11-05, in the headset; first a yank back, a jab forward and a
// chop down, each from the right hand held out still at the monster, a start that was itself a punch:
// "Corpse explosion does not work ... It also right now interferes with the amplified damage ... lower
// resist ... does not work. It actually accidentally triggers amplified damage again"). The golems go to
// a spot along the arm raised (Aim, LandAt); Lower Resist along the left arm; the rest have no way of
// their own (the right hand's ray).
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
    // N5 Amplify Damage, a punch with the right fist (owner, 2026-11-05, in the headset: "Amplified damage could
    // be more of a punch. Punch with one hand"), and N6 Lower Resist the same with the left: from drawn in (at
    // most punchFrom ahead, punchLow to punchHigh) out to punchReach ahead, punchTravel forward within
    // punchWindow, mostly forward, at punchSpeed or faster, the other fist left where it was (forward
    // punchOther at most over pairWindow). N3 Corpse Explosion: both fists from drawn in together, each out to
    // pairReach and pairTravel forward within pairWindow, mostly forward, at punchSpeed or faster, ending at
    // most pairApart apart (Poison Nova flings them wide). Its lesser reach casts it before a lone fist would.
    float punchFrom=.25f,punchLow=-.65f,punchHigh=.10f,punchReach=.42f,punchTravel=.22f,punchWindow=.25f,punchSpeed=1.5f;
    float punchOther=.08f,pairReach=.38f,pairTravel=.18f,pairWindow=.35f,pairApart=.60f;
    // The summons by raised arms (owner, 2026-11-05, in the headset: Raise Skeleton "was really difficult to
    // execute honestly ... from ... around the hip level or wherever, if you have a fast motion, kind of like
    // with both arms ... motioning upwards, should basically raise the skeleton", then "right arm, just right
    // arm rising up to raise golem, and then left arm raising up to raise different kind of golem, and then
    // both arms raising up to raise skeleton"). Raise Skeleton had to stop at the chest; Golem was a slam from
    // overhead, whose wind-up is itself a raise of both arms.
    // N2 Raise Skeleton: both hands low (riseFrom or below), then swept up fast, each riseMin or more and
    // riseUpright times as far as it moves across, past riseTop (the eyes) within riseWindow, riseSpeed or
    // faster at their fastest, the two within riseTogether s of each other. It casts as they pass riseTop: no stop.
    float riseFrom=-.45f,riseTop=.02f,riseMin=.35f,riseWindow=.6f,riseSpeed=1.5f,riseUpright=1.2f,riseTogether=.25f;
    // N4 Golem (the right arm) and N4b (the left): that arm swept up the same way in front of the body, past
    // armTop and armUpright times as far up as across, the other arm left down (risen otherStill at most,
    // under otherBelow). Raise Skeleton, both arms, casts lower, before a lone arm would. The golem stands
    // golemAhead cells away where the arm heads (from its shoulder; straight ahead for an arm raised overhead).
    // The owner, 2026-11-05: "it shouldn't be raising our arm fast beside our head, it's more like in our
    // front of us ... Kind of like a rise motion, but instead of two hands, we're doing it with one hand, in
    // front of us, kind of pointing at whatever area we're raising from" (it had to end beside the head).
    float armTop=.08f,armUpright=1.0f,otherStill=.12f,otherBelow=-.25f,golemAhead=4;
    // N7 Poison Nova: hands together at the chest, then flung novaApart apart, each novaSide out on its
    // own side, at any height up to novaHigh (the owner's flings, 2026-11-05: 130-160 cm wide at shoulder
    // height; it once had to end below the chest), within novaWindow and at least flingSpeed fast.
    float novaTogether=.30f,novaApart=.75f,novaSide=.25f,novaHigh=.10f,novaWindow=.8f,flingSpeed=1.5f;
    // The owner's additions (2026-11-05, from the list of the Necromancer's skills without one):
    // N9 Bone Wall, "sweep your right hand sideways across in front of your chest, as if drawing the wall":
    // the right hand wallSweep or more across within wallWindow, either way, mostly sideways, wallFront
    // ahead or more and wallLow to wallHigh the whole way, at wallSpeed or faster, the left hand moved
    // wallOther at most. The wall stands wallAhead cells away where the sweep's middle heads from the eyes.
    float wallSweep=.40f,wallWindow=.40f,wallFront=.20f,wallLow=-.60f,wallHigh=.05f,wallSpeed=1.5f,wallOther=.15f,wallAhead=4;
    // N10 Poison Explosion, "push both fists down in front of you, from chest to waist, at a corpse": both
    // fists in front (pushFront ahead or more) and at the chest (pushFrom to pushTop) pushed down pushFall
    // or more each within pushWindow, mostly down (coming in or out at most 0.8 as far), at pushSpeed or
    // faster, ending pushEnd ahead or more and at most pairApart apart, neither over pushQuietTop for
    // pushQuiet s before (arms let down after Raise Skeleton are no push). Along the right hand's ray, as
    // Corpse Explosion. The owner's first pushes (2026-11-05) missed: 30-44 cm from the chin and the eyes (it
    // had to start under -.05 with nothing over it for pushQuiet), and 17-20 cm ending 9 cm ahead (pushFall
    // .22, pushEnd .15), and 58-80 cm pushes from the eyes to the hips ended against the body (16 cm back
    // to 20 ahead).
    float pushFront=.15f,pushFrom=-.42f,pushTop=.02f,pushQuietTop=.08f,pushFall=.15f,pushEnd=-.20f,pushSpeed=1.8f,pushWindow=.35f,pushQuiet=.5f;
    // N11 Raise Skeletal Mage, "the Raise Skeleton sweep with your hands spread wide in a V": Raise
    // Skeleton's sweep with the hands mageApart or more apart as they pass riseTop; narrower is Raise Skeleton.
    float mageApart=.80f;
    // N8 Bone Prison: both hands out in front prisonApart apart, then brought within prisonTogether,
    // each in by prisonIn, within prisonWindow and at least closeSpeed fast.
    float prisonApart=.55f,prisonTogether=.20f,prisonFront=.30f,prisonIn=.15f,prisonWindow=.9f,closeSpeed=1.0f;
};
inline constexpr Tuning tuning{};
// The order they are watched in: the first to complete in a frame casts. Raise Skeleton (both arms) before
// the golems (one arm each), Corpse Explosion (both fists) before the punches of one.
constexpr Motion Order[]={Motion::BoneArmor,Motion::BonePrison,Motion::RaiseSkeleton,Motion::RaiseMage,Motion::Golem,
    Motion::GolemLeft,Motion::PoisonNova,Motion::CorpseExplosion,Motion::AmplifyDamage,Motion::LowerResist,Motion::BoneWall,
    Motion::PoisonExplosion};
// How to do each, on the MOTIONS page and in the log (what the controllers' positions can tell: no palms or
// fingers, both grips held).
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::BoneArmor:return "cross both fists on the chest and hold";
    case Motion::RaiseSkeleton:return "sweep both arms up fast from the hips, past your face";
    case Motion::CorpseExplosion:return "punch both fists out together at a corpse";
    case Motion::Golem:return "sweep the right arm up fast in front of you, toward its spot (your strongest golem)";
    case Motion::GolemLeft:return "sweep the left arm up fast in front of you, toward its spot (your next golem)";
    case Motion::AmplifyDamage:return "punch the right fist out at a monster";
    case Motion::LowerResist:return "punch the left fist out at a monster";
    case Motion::PoisonNova:return "hands together at the chest, then fling them out wide";
    case Motion::BonePrison:return "hold both hands apart in front, then bring them together round a monster";
    case Motion::BoneWall:return "sweep the right hand sideways across in front of your chest, drawing the wall";
    case Motion::PoisonExplosion:return "push both fists down in front of you, from the chest to the waist, at a corpse";
    case Motion::RaiseMage:return "sweep both arms up fast from the hips, spread wide in a V";
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
// Arm k's heading from its shoulder, level and head-local (unit), or zero for an arm raised straight up.
inline math::Vec ArmHeading(math::Vec fist,int k) {
    const math::Vec shoulder{k?-.17f:.17f,-.22f,.05f};
    const float x=fist.x-shoulder.x,z=fist.z-shoulder.z,level=std::hypot(x,z);
    return level>=.20f?math::Vec{x/level,0,z/level}:math::Vec{};
}
// Fist k drawn in (where a punch starts) at sample a.
inline bool DrawnIn(const Sample& a,int k) {
    const auto& o=a.l[k];
    return Engine::Forward(o)<=tuning.punchFrom&&o.y>=tuning.punchLow&&o.y<=tuning.punchHigh;
}
// Fist k from sample a to the newest, n: out to reach, travel forward or more, mostly forward.
inline bool PunchedOut(const Sample& a,const Sample& n,int k,float reach,float travel) {
    const auto& o=a.l[k];const auto& c=n.l[k];const float forward=Engine::Forward(c)-Engine::Forward(o);
    return Engine::Forward(c)>=reach&&forward>=travel&&std::fabs(c.y-o.y)<=.6f*forward&&std::fabs(c.x-o.x)<=.6f*forward;
}
// N5, N6: fist k punched out, the other left where it was.
inline bool Punch(Engine& e,const std::vector<Sample>& s,int k) {
    const auto& t=tuning;const auto& n=s.back();
    float least=Engine::Forward(n.l[1-k]);
    for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.pairWindow;)least=std::min(least,Engine::Forward(s[i].l[1-k]));
    if(Engine::Forward(n.l[1-k])-least>t.punchOther)return false;   // both out: Corpse Explosion's
    for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.punchWindow;) {
        if(!DrawnIn(s[i],k))continue;
        const float forward=Engine::Forward(n.l[k])-Engine::Forward(s[i].l[k]);
        if(forward>=.05f)e.Toward(forward/t.punchTravel);
        if(PunchedOut(s[i],n,k,t.punchReach,t.punchTravel)&&PeakSpeed(s,i,k)>=t.punchSpeed)return true;
    }
    return false;
}
// N2, N11: both arms swept up fast from the hips past riseTop, together; apart: how far apart as they did.
inline bool BothUp(Engine& e,const std::vector<Sample>& s,float& apart) {
    const auto& t=tuning;const auto& n=s.back();
    // from the latest moment both hands were low
    for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.riseWindow;) {
        const auto& a=s[i];
        if(a.l[0].y>t.riseFrom||a.l[1].y>t.riseFrom||a.l[0].y<-1.1f||a.l[1].y<-1.1f)continue;
        const float lift=std::min(n.l[0].y-a.l[0].y,n.l[1].y-a.l[1].y);
        if(lift>=.06f)e.Toward(lift/(t.riseTop-std::min(a.l[0].y,a.l[1].y)));
        bool up=lift>=t.riseMin;
        for(int k=0;k<2;++k)up=up&&n.l[k].y>=t.riseTop&&n.l[k].y-a.l[k].y>=t.riseUpright*std::hypot(n.l[k].x-a.l[k].x,n.l[k].z-a.l[k].z);
        if(!up)return false;
        double past[2]={-1,-1};   // when each first passed riseTop: together, not one arm and then the other
        for(size_t j=i;j<s.size();++j)for(int k=0;k<2;++k)if(past[k]<0&&s[j].l[k].y>=t.riseTop)past[k]=s[j].t;
        apart=Engine::Apart(n);
        return std::fabs(past[0]-past[1])<=t.riseTogether&&PeakSpeed(s,i,0)>=t.riseSpeed&&PeakSpeed(s,i,1)>=t.riseSpeed;
    }
    return false;
}
// N4, N4b: arm k swept up fast from low past armTop, the other arm left down.
inline bool ArmUp(Engine& e,const std::vector<Sample>& s,int k) {
    const auto& t=tuning;const auto& n=s.back();const auto& c=n.l[k];
    for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.riseWindow;) {
        const auto& a=s[i].l[k];
        if(a.y>t.riseFrom||a.y<-1.1f)continue;
        if(n.l[1-k].y-s[i].l[1-k].y>t.otherStill||n.l[1-k].y>t.otherBelow)return false;   // both up: Raise Skeleton's
        const float lift=c.y-a.y;
        if(lift>=.06f)e.Toward(lift/(t.armTop-a.y));
        return c.y>=t.armTop&&lift>=t.riseMin&&lift>=t.armUpright*std::hypot(c.x-a.x,c.z-a.z)&&
            PeakSpeed(s,i,k)>=t.riseSpeed;
    }
    return false;
}
}
inline bool Done(Engine& e,Motion m) {
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
    case Motion::RaiseSkeleton:case Motion::RaiseMage: {   // both arms up: narrow the skeleton, a wide V the mage
        float apart=0;
        return detail::BothUp(e,s,apart)&&(apart>=t.mageApart)==(m==Motion::RaiseMage);
    }
    case Motion::BoneWall: {   // the right hand swept across in front of the chest, either way
        const auto& c=n.l[0];
        const auto front=[&](math::Vec h){return F(h)>=t.wallFront&&h.y>=t.wallLow&&h.y<=t.wallHigh;};
        if(!front(c))return false;
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.wallWindow;) {
            const auto& a=s[i].l[0];
            if(!front(a))break;   // in front of the chest all along: none from before it left
            const float across=std::fabs(c.x-a.x);
            if(across>=.05f)e.Toward(across/t.wallSweep);
            if(across<t.wallSweep||std::fabs(c.y-a.y)>.5f*across||std::fabs(F(c)-F(a))>.5f*across||
               Engine::Len(n.l[1]-s[i].l[1])>t.wallOther||detail::PeakSpeed(s,i,0)<t.wallSpeed)continue;
            const float mx=(a.x+c.x)/2,mz=(a.z+c.z)/2,level=std::hypot(mx,mz);
            math::Vec way=level>=.15f?math::Vec{mx/level,0,mz/level}:math::Vec{0,0,-1};
            if(-way.z<.3f)way={0,0,-1};
            e.Aim(way);e.LandAt(t.wallAhead*-way.z,t.wallAhead*way.x);
            return true;
        }
        return false;
    }
    case Motion::PoisonExplosion: {   // both fists pushed down in front, from the chest
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.pushWindow;) {
            const auto& a=s[i];
            bool chest=true;
            for(const auto& h:a.l)chest=chest&&F(h)>=t.pushFront&&h.y>=t.pushFrom&&h.y<=t.pushTop;
            if(!chest)continue;
            bool quiet=true;   // neither over pushQuietTop for pushQuiet s before
            for(size_t j=i+1;j-->0&&a.t-s[j].t<=t.pushQuiet&&quiet;)quiet=s[j].l[0].y<=t.pushQuietTop&&s[j].l[1].y<=t.pushQuietTop;
            if(!quiet)return false;
            const float fall=std::min(a.l[0].y-n.l[0].y,a.l[1].y-n.l[1].y);
            if(fall>=.05f)e.Toward(fall/t.pushFall);
            bool down=fall>=t.pushFall&&Engine::Apart(n)<=t.pairApart;
            for(int k=0;k<2&&down;++k){const float f=a.l[k].y-n.l[k].y;
                down=F(n.l[k])>=t.pushEnd&&std::fabs(F(n.l[k])-F(a.l[k]))<=.8f*f&&std::fabs(n.l[k].x-a.l[k].x)<=.6f*f&&detail::PeakSpeed(s,i,k)>=t.pushSpeed;}
            if(down)return true;
        }
        return false;
    }
    case Motion::CorpseExplosion:   // both fists punched out together
        for(size_t i=s.size()-1;i-->0&&n.t-s[i].t<=t.pairWindow;) {
            if(!detail::DrawnIn(s[i],0)||!detail::DrawnIn(s[i],1))continue;
            const float forward=std::min(F(n.l[0])-F(s[i].l[0]),F(n.l[1])-F(s[i].l[1]));
            if(forward>=.05f)e.Toward(forward/t.pairTravel);
            if(Engine::Apart(n)<=t.pairApart&&detail::PunchedOut(s[i],n,0,t.pairReach,t.pairTravel)&&detail::PunchedOut(s[i],n,1,t.pairReach,t.pairTravel)&&
               detail::PeakSpeed(s,i,0)>=t.punchSpeed&&detail::PeakSpeed(s,i,1)>=t.punchSpeed)return true;
        }
        return false;
    case Motion::AmplifyDamage:return detail::Punch(e,s,0);   // the right fist: the right hand's ray aims it
    case Motion::LowerResist:   // the left fist, along the left arm
        if(!detail::Punch(e,s,1))return false;
        e.Aim(detail::ArmHeading(n.l[1],1));
        return true;
    case Motion::Golem:case Motion::GolemLeft: {   // one arm swept up in front, toward the golem's spot
        const int k=m==Motion::Golem?0:1;
        if(!detail::ArmUp(e,s,k))return false;
        auto way=detail::ArmHeading(n.l[k],k);
        if(-way.z<.3f)way={0,0,-1};   // overhead, or heading off behind: straight ahead
        e.Aim(way);e.LandAt(t.golemAhead*-way.z,t.golemAhead*way.x);
        return true;
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
