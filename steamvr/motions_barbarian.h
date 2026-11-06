#pragma once
#include "spell_motion.h"

// The Barbarian's motions (B1-B8, owner 2026-11-04), detected on spellmotion::Engine's history (head-local:
// +X right, +Y up from the eyes, forward -Z; hand 0 the right). Both grips are held while they are watched
// (the transport's gate); a motion that completes casts its card's skill. Include spell_motion.h, not this.
// Value-only: steamvr/motions_barbarian_test.cpp.
//
// The warcries are the owner's 10-23 call: a hand to the mouth, then a thrust, the thrust's direction
// telling them apart (Battle Orders up, War Cry forward and down, Shout both hands forward). The hand
// rests at the mouth a moment first (dwellSeconds, still): a sword's wind-up passes the face without
// stopping, so a swing never starts a warcry. Battle Command counts two beats on the chest (scratch).
// Leap goes to a spot ahead, the way the arms swung (Engine::Aim); Find Item's corpse is where the right
// hand points, so it leaves the aim to the hand's own ray. The controllers' palms and fingers are not
// known, and both grips are held throughout: every motion is the hands' positions only.
namespace framevr::spellmotion::barbarian {
struct Tuning {
    // The mouth (a fist at it: the controller just in front): |x| <= mouthSide, y in [mouthLow, mouthHigh],
    // forward in [mouthNear, mouthFar] (m from the eyes). Two hands at it (Shout) at most shoutApart apart.
    float mouthSide=.15f,mouthLow=-.28f,mouthHigh=.02f,mouthNear=.04f,mouthFar=.32f,shoutSide=.18f,shoutApart=.28f;
    double dwellSeconds=.15;  // a hand resting at the mouth (or the floor) this long first,
    float dwellStill=.08f;    // m: moving less than this over it
    double thrustWindow=.5;   // s from leaving the mouth to the thrust's end,
    float thrustSpeed=1.0f;   // m/s: the thrust's hand at least this fast at its fastest (a push, not a drift)
    float ordersAbove=.18f;   // m above the eyes: Battle Orders' fist at the end,
    float ordersRise=.30f;    // risen at least this from the mouth, 1.2 times as far as it went across
    float otherBelow=-.05f;   // m: the other hand stays below this (a Leap has both up)
    float cryReach=.40f;      // m ahead of the eyes: War Cry's fist (and Shout's hands) at the end,
    float cryTravel=.22f;     // m forward from the mouth,
    float cryDrop=.10f;       // m lower than at the mouth (War Cry), Shout within shoutLevel of it
    float shoutLevel=.20f,shoutEndApart=.45f;
    float otherAway=.20f;     // m: War Cry's other hand this far from the mouth (not a Shout)
    // Battle Command: the right fist in front of the chest (x in [chestLeft, chestRight], y in [chestLow,
    // chestHigh], forward at most chestIn) after it was out (forward chestOut or more), arriving at
    // beatSpeed or faster toward the body; two beats beatGap to beatWindow s apart.
    float chestLeft=-.22f,chestRight=.18f,chestLow=-.62f,chestHigh=-.22f,chestIn=.16f,chestOut=.26f,beatSpeed=.8f;
    double beatGap=.15,beatWindow=1.2;
    // Howl: both hands at the chest, at most howlTogether apart, then each out on its own side howlSide and
    // howlApart apart, no lower than howlLow, within howlWindow s.
    float howlTogether=.30f,howlSide=.40f,howlApart=.80f,howlLow=-.70f;double howlWindow=.7;
    float flingSpeed=1.2f;    // m/s: Howl's and Battle Cry's hands each at least this fast at their fastest
    // Battle Cry: both fists up by the shoulders (|x| in [cryFistIn, cryFistOut] on their own side, y in
    // [shoulderLow, shoulderHigh], at least fistsApart apart), then both pulled down past hipBelow, each
    // falling cryFall or more, within cryWindow s.
    float cryFistIn=.08f,cryFistOut=.40f,shoulderLow=-.40f,shoulderHigh=-.02f,fistsApart=.25f,hipBelow=-.68f,cryFall=.38f;
    double cryWindow=.6;
    // Leap: both arms back and down (y at most backLow, forward at most backBehind), then both above the
    // eyes by leapAbove, at least leapApart apart (not a two-handed grip), risen leapRise within leapWindow s.
    float backLow=-.50f,backBehind=-.08f,leapAbove=.10f,leapApart=.22f,leapRise=.60f;double leapWindow=.6;
    float swingSpeed=1.5f;    // m/s: the leap's swing, each arm at its fastest
    // Find Item: the right hand rests low (y at most reachLow, forward at least reachFront, the left hand
    // findApart or more from it) after coming down no faster than reachSpeed, then lifts findLift to above
    // findUp within findWindow s.
    float reachLow=-1.0f,reachFront=-.15f,findApart=.30f,reachSpeed=2.5f,findLift=.35f,findUp=-.80f;double findWindow=.8;
    float liftSpeed=.9f;      // m/s: the lift at its fastest
};
inline Tuning tuning;
// The order they are watched in: the first to complete in a frame casts. Shout before War Cry (both hands
// at the mouth, not one); Leap before Battle Orders (both arms up, not one).
constexpr Motion Order[]={Motion::Shout,Motion::WarCry,Motion::Leap,Motion::BattleOrders,Motion::BattleCommand,Motion::Howl,Motion::BattleCry,Motion::FindItem};
// How to do each, on the MOTIONS page and in the log (what the controllers' positions can tell: no palms or
// fingers, both grips held).
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::BattleOrders:return "right fist to the mouth a moment, then thrust it straight up over the head";
    case Motion::Shout:return "both fists to the mouth a moment, then push them out forward together";
    case Motion::BattleCommand:return "beat the right fist on the chest twice";
    case Motion::WarCry:return "right fist to the mouth a moment, then thrust it forward and down at the ground ahead";
    case Motion::Howl:return "fling both arms wide from the chest";
    case Motion::BattleCry:return "both fists up by the shoulders, pulled down hard to the hips";
    case Motion::Leap:return "swing both arms down behind, then up past the head, fast, as for a jump";
    case Motion::FindItem:return "reach the right hand down toward the floor, rest it there a moment, then lift it";
    default:return "no motion";
    }
}
inline float Forward(math::Vec l){return -l.z;}
inline float Len(math::Vec v){return std::sqrt(math::Dot(v,v));}
inline bool AtMouth(math::Vec l,float side) {
    const auto& t=tuning;
    return std::fabs(l.x)<=side&&l.y>=t.mouthLow&&l.y<=t.mouthHigh&&Forward(l)>=t.mouthNear&&Forward(l)<=t.mouthFar;
}
// The fastest the hands in `mask` moved (m/s) from sample i to the newest.
inline float Peak(const std::vector<Sample>& h,int i,int mask) {
    float most=0;
    for(size_t j=size_t(i)+1;j<h.size();++j) {
        const double dt=h[j].t-h[j-1].t;
        if(dt<=1e-4)continue;
        for(int k=0;k<2;++k)if((mask>>k)&1)most=std::max(most,Len(h[j].l[k]-h[j-1].l[k])/float(dt));
    }
    return most;
}
// The samples, newest first, that end a dwell: `seconds` of samples (back from it) all in the zone and the
// hands in `mask` (bit 0 the right, bit 1 the left) within dwellStill of where they are at its end; at most
// `within` s old. For each, until one returns true, `then(i)`.
template<class Zone,class Then> bool AfterDwell(const std::vector<Sample>& h,double within,double seconds,int mask,Zone zone,Then then) {
    if(h.empty())return false;
    const double now=h.back().t;
    for(int i=int(h.size())-1;i>=0;--i) {
        if(now-h[size_t(i)].t>within)break;
        const auto& end=h[size_t(i)];
        if(!zone(end))continue;
        bool dwelt=false;
        for(int j=i;j>=0;--j) {
            const auto& s=h[size_t(j)];
            if(!zone(s))break;
            bool still=true;
            for(int k=0;k<2;++k)if((mask>>k)&1)still=still&&Len(s.l[k]-end.l[k])<=tuning.dwellStill;
            if(!still)break;
            if(end.t-s.t>=seconds){dwelt=true;break;}
        }
        if(dwelt&&then(i))return true;
    }
    return false;
}
inline bool Done(Engine& e,Motion m) {
    const auto& h=e.History();
    if(h.empty())return false;
    const auto& n=h.back();
    const auto& t=tuning;
    const auto& r=n.l[0];const auto& l=n.l[1];
    switch(m) {
    case Motion::BattleOrders: {
        if(r.y<t.ordersAbove||std::fabs(r.x)>.35f||Forward(r)<-.25f||Forward(r)>.45f||l.y>t.otherBelow)return false;
        return AfterDwell(h,t.thrustWindow+t.dwellSeconds,t.dwellSeconds,1,[&](const Sample& s){return AtMouth(s.l[0],t.mouthSide);},[&](int i) {
            const auto a=h[size_t(i)].l[0];
            const float rise=r.y-a.y,across=std::hypot(r.x-a.x,r.z-a.z);
            return n.t-h[size_t(i)].t<=t.thrustWindow&&rise>=t.ordersRise&&rise>=1.2f*across&&Peak(h,i,1)>=t.thrustSpeed;
        });
    }
    case Motion::WarCry: {
        if(Forward(r)<t.cryReach||r.y<-.85f)return false;
        return AfterDwell(h,t.thrustWindow+t.dwellSeconds,t.dwellSeconds,1,
            [&](const Sample& s){return AtMouth(s.l[0],t.mouthSide)&&Len(s.l[1]-math::Vec{0,-.12f,-.16f})>=t.otherAway;},[&](int i) {
            const auto a=h[size_t(i)].l[0];
            const float travel=Forward(r)-Forward(a),drop=a.y-r.y;
            return n.t-h[size_t(i)].t<=t.thrustWindow&&travel>=t.cryTravel&&drop>=t.cryDrop&&drop<=1.5f*travel&&std::fabs(r.x-a.x)<=travel&&
                Peak(h,i,1)>=t.thrustSpeed;
        });
    }
    case Motion::Shout: {
        if(Forward(r)<t.cryReach||Forward(l)<t.cryReach||Len(r-l)>t.shoutEndApart)return false;
        return AfterDwell(h,t.thrustWindow+t.dwellSeconds,t.dwellSeconds,3,
            [&](const Sample& s){return AtMouth(s.l[0],t.shoutSide)&&AtMouth(s.l[1],t.shoutSide)&&Len(s.l[0]-s.l[1])<=t.shoutApart;},[&](int i) {
            const auto& a=h[size_t(i)];
            if(n.t-a.t>t.thrustWindow)return false;
            for(int k=0;k<2;++k){const auto& c=n.l[k];const auto& b=a.l[k];
                if(Forward(c)-Forward(b)<t.cryTravel||std::fabs(c.y-b.y)>t.shoutLevel)return false;}
            return Peak(h,i,3)>=t.thrustSpeed;
        });
    }
    case Motion::BattleCommand: {
        // scratch: 0 out since the last beat, 1 the last beat's time, 2 beats, 3 the first beat's time
        auto& s=e.scratch;
        const bool box=r.x>=t.chestLeft&&r.x<=t.chestRight&&r.y>=t.chestLow&&r.y<=t.chestHigh&&Forward(r)>=-.20f;
        const bool in=box&&Forward(r)<=t.chestIn,out=!box||Forward(r)>=t.chestOut;
        if(out)s[0]=1;
        if(!in||s[0]==0||e.Velocity(0).z<t.beatSpeed)return false;
        s[0]=0;
        if(s[2]==0||n.t-s[3]>t.beatWindow){s[2]=1;s[3]=s[1]=n.t;return false;}
        if(n.t-s[1]<t.beatGap)return false;
        s[1]=n.t;
        return ++s[2]>=2;
    }
    case Motion::Howl: {
        if(r.x<t.howlSide||l.x>-t.howlSide||Len(r-l)<t.howlApart||r.y<t.howlLow||l.y<t.howlLow)return false;
        for(size_t i=0;i<h.size();++i) {
            const auto& s=h[i];
            if(n.t-s.t>t.howlWindow)continue;
            bool chest=Len(s.l[0]-s.l[1])<=t.howlTogether;
            for(int k=0;k<2;++k){const auto& c=s.l[k];chest=chest&&std::fabs(c.x)<=.22f&&c.y>=-.62f&&c.y<=-.18f&&Forward(c)>=-.10f&&Forward(c)<=.35f;}
            if(chest&&Peak(h,int(i),1)>=t.flingSpeed&&Peak(h,int(i),2)>=t.flingSpeed)return true;
        }
        return false;
    }
    case Motion::BattleCry: {
        if(r.y>t.hipBelow||l.y>t.hipBelow||r.x<0||l.x>0)return false;
        for(int k=0;k<2;++k)if(Forward(n.l[k])<-.12f||Forward(n.l[k])>.35f)return false;
        for(size_t i=0;i<h.size();++i) {
            const auto& s=h[i];
            if(n.t-s.t>t.cryWindow)continue;
            const auto& a=s.l[0];const auto& b=s.l[1];
            const bool up=a.x>=t.cryFistIn&&a.x<=t.cryFistOut&&-b.x>=t.cryFistIn&&-b.x<=t.cryFistOut&&
                a.y>=t.shoulderLow&&a.y<=t.shoulderHigh&&b.y>=t.shoulderLow&&b.y<=t.shoulderHigh&&
                Forward(a)>=-.15f&&Forward(a)<=.30f&&Forward(b)>=-.15f&&Forward(b)<=.30f&&Len(a-b)>=t.fistsApart;
            if(up&&a.y-r.y>=t.cryFall&&b.y-l.y>=t.cryFall&&Peak(h,int(i),1)>=t.flingSpeed&&Peak(h,int(i),2)>=t.flingSpeed)return true;
        }
        return false;
    }
    case Motion::Leap: {
        if(r.y<t.leapAbove||l.y<t.leapAbove||Len(r-l)<t.leapApart||Forward(r)<-.20f||Forward(l)<-.20f)return false;
        for(size_t i=0;i<h.size();++i) {
            const auto& s=h[i];
            if(n.t-s.t>t.leapWindow)continue;
            const auto& a=s.l[0];const auto& b=s.l[1];
            if(a.y>t.backLow||b.y>t.backLow||Forward(a)>t.backBehind||Forward(b)>t.backBehind)continue;
            if(r.y-a.y<t.leapRise||l.y-b.y<t.leapRise||Peak(h,int(i),1)<t.swingSpeed||Peak(h,int(i),2)<t.swingSpeed)continue;
            // the way the arms swung, level; straight ahead if they swung straight up
            math::Vec way=((r-a)+(l-b))*.5f;way.y=0;
            if(Forward(way)<.05f)way={0,0,-1};
            e.Aim(way);
            return true;
        }
        return false;
    }
    case Motion::FindItem: {
        return AfterDwell(h,t.findWindow+t.dwellSeconds,t.dwellSeconds,1,
            [&](const Sample& s){return s.l[0].y<=t.reachLow&&Forward(s.l[0])>=t.reachFront&&Len(s.l[0]-s.l[1])>=t.findApart;},[&](int i) {
            const auto& low=h[size_t(i)];
            if(n.t-low.t>t.findWindow||r.y-low.l[0].y<t.findLift||r.y<t.findUp||Peak(h,i,1)<t.liftSpeed)return false;
            // came down reaching, not a swing that ends low: no faster than reachSpeed in the .35 s before
            double start=low.t;
            for(int j=i;j>=0&&low.t-h[size_t(j)].t<t.dwellSeconds;--j)start=h[size_t(j)].t;
            for(size_t j=1;j<h.size();++j) {
                const double at=h[j].t;
                if(at>start||start-at>.35)continue;
                const double dt=at-h[j-1].t;
                if(dt>1e-4&&Len(h[j].l[0]-h[j-1].l[0])/float(dt)>t.reachSpeed)return false;
            }
            return true;
        });
    }
    default:return false;
    }
}
}
