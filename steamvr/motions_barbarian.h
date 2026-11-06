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
//
// Find Item reached down to the floor and rested there until a tester's first pass (2026-11-05: "Find Item
// never worked for me. Also, we're all getting old man... Please no floor stuff"): now the right hand is
// held out at the corpse a moment, then snatched back to the shoulder, quick, with nothing low.
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
    // Howl: both hands together in front, at most howlTogether apart, from the chest up to the face (howlFrom
    // below the eyes to howlTop over them), then each out on its own side howlSide and howlApart apart, no lower
    // than howlLow, within howlWindow s. The owner's flings (2026-11-05) began with the hands together at the
    // face, 11-20 cm under the eyes to 13-22 over them, and missed: the start had to be at the chest (18 cm
    // under the eyes or lower). Shout, from the mouth too, ends with the fists pushed forward together.
    float howlTogether=.30f,howlSide=.40f,howlApart=.80f,howlLow=-.70f,howlFrom=-.62f,howlTop=.22f;double howlWindow=.7;
    float flingSpeed=1.2f;    // m/s: Howl's and Battle Cry's hands each at least this fast at their fastest
    // Battle Cry: both fists up by the shoulders (|x| in [cryFistIn, cryFistOut] on their own side, y in
    // [shoulderLow, shoulderHigh], at least fistsApart apart), then both pulled down past hipBelow, each
    // falling cryFall or more, within cryWindow s.
    // The owner's pulls (2026-11-05) ended 53-67 cm under the eyes, the line was 68, up to 31 cm behind them
    // (pulled down and back; 12 was the most), and some began over the head (up to 12 cm over the eyes, the top
    // was 2 under): now from up to 20 over, down to 50 under, as far back as cryBack. Later pulls began at the
    // chest, 41 cm under the eyes (40 was the bottom), and fell 27-31 cm (38 was the least): now from 46 under,
    // falling 26, ending at least fistsApart apart as they began (a two-handed weapon chopped down from over the
    // head ends with the hands together on it). The fists at least
    // fistsApart apart, wider than Howl's start (howlTogether), which may be at the face too.
    float cryFistIn=.08f,cryFistOut=.40f,shoulderLow=-.46f,shoulderHigh=.20f,fistsApart=.32f,hipBelow=-.50f,cryFall=.26f,cryBack=.35f;
    double cryWindow=.6;
    // Leap: both arms back and down (y at most backLow, forward at most backBehind), then both up past leapAbove
    // (shoulder height), at least leapApart apart (not a two-handed grip), risen leapRise within leapWindow s.
    // It cast as the hands passed over the head (leapAbove .10, leapRise .60) until the owner felt the game's
    // own take-off crouch as a stop before the jump (2026-11-05: the hero ran at full speed up to the cast, then
    // stood 0.17 s): cast at the shoulders, the crouch overlaps the rest of the swing.
    float backLow=-.50f,backBehind=-.08f,leapAbove=-.15f,leapApart=.22f,leapRise=.38f;double leapWindow=.6;
    float swingSpeed=1.5f;    // m/s: the leap's swing, each arm at its fastest
    // Leap lands leapAhead cells away the way the arms swung (straight ahead, where you face, if straight up).
    // Its way alone went nowhere: a placed skill takes a spot (the owner, 2026-11-05: "it goes to where the
    // character was last moving ... Not where we're facing or not where our arms are ending up"). 25 cells, the
    // game's own reach (owner, 2026-11-06: "the leaps themselves are way too short"): sent 10 and 20 the hero
    // landed on the spot (9.5, 19.6 cells, 0 of 30 refused); sent 30 he went 21-26 (24 on average, his Leap's
    // limit), and 4 of 17 never left the ground - a spot that far is more often one the game will not take.
    float leapAhead=25;
    // Find Item: the right hand held out at the corpse (pointOut ahead or more, y in [pointLow, pointHigh],
    // |x| at most pointSide), resting dwellSeconds, then snatched back to the right shoulder (forward at most
    // snatchTo, y in [shoulderMin, shoulderMax], x in [shoulderIn, shoulderOut]), snatchBack or more toward
    // the eyes within snatchWindow s, at snatchSpeed or faster at its fastest.
    float pointOut=.30f,pointLow=-.75f,pointHigh=-.05f,pointSide=.45f;
    float snatchTo=.15f,snatchBack=.22f,shoulderMin=-.30f,shoulderMax=.10f,shoulderIn=.08f,shoulderOut=.42f,snatchSpeed=1.0f;
    double snatchWindow=.45;
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
    case Motion::Howl:return "hands together at the chest or face, then fling both arms out wide";
    case Motion::BattleCry:return "both fists up by the shoulders, pulled down hard to the hips";
    case Motion::Leap:return "swing both arms down behind, then up past the head, fast, as for a jump";
    case Motion::FindItem:return "hold the right hand out at a corpse a moment, then snatch it back to the shoulder";
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
        const bool end=r.y>=t.ordersAbove&&std::fabs(r.x)<=.35f&&Forward(r)>=-.25f&&Forward(r)<=.45f&&l.y<=t.otherBelow;
        return AfterDwell(h,t.thrustWindow+t.dwellSeconds,t.dwellSeconds,1,[&](const Sample& s){return AtMouth(s.l[0],t.mouthSide);},[&](int i) {
            const auto a=h[size_t(i)].l[0];
            if(n.t-h[size_t(i)].t>t.thrustWindow)return false;
            const float rise=r.y-a.y,across=std::hypot(r.x-a.x,r.z-a.z);
            e.Toward(rise/t.ordersRise);   // at the mouth, then warmer as it rises
            return end&&rise>=t.ordersRise&&rise>=1.2f*across&&Peak(h,i,1)>=t.thrustSpeed;
        });
    }
    case Motion::WarCry: {
        const bool end=Forward(r)>=t.cryReach&&r.y>=-.85f;
        return AfterDwell(h,t.thrustWindow+t.dwellSeconds,t.dwellSeconds,1,
            [&](const Sample& s){return AtMouth(s.l[0],t.mouthSide)&&Len(s.l[1]-math::Vec{0,-.12f,-.16f})>=t.otherAway;},[&](int i) {
            const auto a=h[size_t(i)].l[0];
            if(n.t-h[size_t(i)].t>t.thrustWindow)return false;
            const float travel=Forward(r)-Forward(a),drop=a.y-r.y;
            e.Toward(travel/t.cryTravel);
            return end&&travel>=t.cryTravel&&drop>=t.cryDrop&&drop<=1.5f*travel&&std::fabs(r.x-a.x)<=travel&&
                Peak(h,i,1)>=t.thrustSpeed;
        });
    }
    case Motion::Shout: {
        const bool end=Forward(r)>=t.cryReach&&Forward(l)>=t.cryReach&&Len(r-l)<=t.shoutEndApart;
        return AfterDwell(h,t.thrustWindow+t.dwellSeconds,t.dwellSeconds,3,
            [&](const Sample& s){return AtMouth(s.l[0],t.shoutSide)&&AtMouth(s.l[1],t.shoutSide)&&Len(s.l[0]-s.l[1])<=t.shoutApart;},[&](int i) {
            const auto& a=h[size_t(i)];
            if(n.t-a.t>t.thrustWindow)return false;
            float least=1e9f;bool level=true;
            for(int k=0;k<2;++k){const auto& c=n.l[k];const auto& b=a.l[k];
                least=std::min(least,Forward(c)-Forward(b));level=level&&std::fabs(c.y-b.y)<=t.shoutLevel;}
            e.Toward(least/t.cryTravel);
            return end&&level&&least>=t.cryTravel&&Peak(h,i,3)>=t.thrustSpeed;
        });
    }
    case Motion::BattleCommand: {
        // scratch: 0 out since the last beat, 1 the last beat's time, 2 beats, 3 the first beat's time
        auto& s=e.scratch;
        if(s[2]>=1&&n.t-s[3]<=t.beatWindow)e.Toward(.5f);   // a beat: one more to come
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
        const bool end=r.x>=t.howlSide&&l.x<=-t.howlSide&&Len(r-l)>=t.howlApart&&r.y>=t.howlLow&&l.y>=t.howlLow;
        for(size_t i=0;i<h.size();++i) {
            const auto& s=h[i];
            if(n.t-s.t>t.howlWindow)continue;
            bool chest=Len(s.l[0]-s.l[1])<=t.howlTogether;
            for(int k=0;k<2;++k){const auto& c=s.l[k];chest=chest&&std::fabs(c.x)<=.22f&&c.y>=t.howlFrom&&c.y<=t.howlTop&&Forward(c)>=-.10f&&Forward(c)<=.35f;}
            if(!chest)continue;
            e.Toward((Len(r-l)-t.howlTogether)/(t.howlApart-t.howlTogether));
            if(end&&Peak(h,int(i),1)>=t.flingSpeed&&Peak(h,int(i),2)>=t.flingSpeed)return true;
        }
        return false;
    }
    case Motion::BattleCry: {
        bool end=r.y<=t.hipBelow&&l.y<=t.hipBelow&&r.x>=0&&l.x<=0&&Len(r-l)>=t.fistsApart;
        for(int k=0;k<2;++k)end=end&&Forward(n.l[k])>=-t.cryBack&&Forward(n.l[k])<=.35f;
        for(size_t i=0;i<h.size();++i) {
            const auto& s=h[i];
            if(n.t-s.t>t.cryWindow)continue;
            const auto& a=s.l[0];const auto& b=s.l[1];
            const bool up=a.x>=t.cryFistIn&&a.x<=t.cryFistOut&&-b.x>=t.cryFistIn&&-b.x<=t.cryFistOut&&
                a.y>=t.shoulderLow&&a.y<=t.shoulderHigh&&b.y>=t.shoulderLow&&b.y<=t.shoulderHigh&&
                Forward(a)>=-.15f&&Forward(a)<=.30f&&Forward(b)>=-.15f&&Forward(b)<=.30f&&Len(a-b)>=t.fistsApart;
            if(!up)continue;
            e.Toward(std::min(a.y-r.y,b.y-l.y)/t.cryFall);
            if(end&&a.y-r.y>=t.cryFall&&b.y-l.y>=t.cryFall&&Peak(h,int(i),1)>=t.flingSpeed&&Peak(h,int(i),2)>=t.flingSpeed)return true;
        }
        return false;
    }
    case Motion::Leap: {
        const bool end=r.y>=t.leapAbove&&l.y>=t.leapAbove&&Len(r-l)>=t.leapApart&&Forward(r)>=-.20f&&Forward(l)>=-.20f;
        for(size_t i=0;i<h.size();++i) {
            const auto& s=h[i];
            if(n.t-s.t>t.leapWindow)continue;
            const auto& a=s.l[0];const auto& b=s.l[1];
            if(a.y>t.backLow||b.y>t.backLow||Forward(a)>t.backBehind||Forward(b)>t.backBehind)continue;
            e.Toward(std::min(r.y-a.y,l.y-b.y)/t.leapRise);
            if(!end||r.y-a.y<t.leapRise||l.y-b.y<t.leapRise||Peak(h,int(i),1)<t.swingSpeed||Peak(h,int(i),2)<t.swingSpeed)continue;
            // the way the arms swung, level; straight ahead if they swung straight up
            math::Vec way=((r-a)+(l-b))*.5f;way.y=0;
            const float level=Len(way);
            way=Forward(way)<.05f||level<1e-3f?math::Vec{0,0,-1}:way*(1/level);
            if(-way.z<.3f)way={0,0,-1};   // off to the side or behind: where you face
            e.Aim(way);e.LandAt(t.leapAhead*-way.z,t.leapAhead*way.x);
            return true;
        }
        return false;
    }
    case Motion::FindItem: {
        // the right hand held out at the corpse a moment (its ray aims the cast), then snatched back to the shoulder
        const bool end=Forward(r)<=t.snatchTo&&Forward(r)>=-.15f&&r.y>=t.shoulderMin&&r.y<=t.shoulderMax&&r.x>=t.shoulderIn&&r.x<=t.shoulderOut;
        return AfterDwell(h,t.snatchWindow+t.dwellSeconds,t.dwellSeconds,1,[&](const Sample& s) {
            const auto& p=s.l[0];return Forward(p)>=t.pointOut&&p.y>=t.pointLow&&p.y<=t.pointHigh&&std::fabs(p.x)<=t.pointSide;},[&](int i) {
            const auto a=h[size_t(i)].l[0];
            if(n.t-h[size_t(i)].t>t.snatchWindow)return false;
            const float back=Forward(a)-Forward(r);
            e.Toward(back/std::max(t.snatchBack,Forward(a)-t.snatchTo));
            return end&&back>=t.snatchBack&&Peak(h,i,1)>=t.snatchSpeed;
        });
    }
    default:return false;
    }
}
}
