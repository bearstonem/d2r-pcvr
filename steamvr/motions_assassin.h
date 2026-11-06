#pragma once
#include "spell_motion.h"
#include <cmath>
#include <vector>

// The Assassin's motions (S1-S7, owner 2026-11-04), detected on spellmotion::Engine's history (head-local:
// +X right, +Y up from the eyes, forward -Z; hand 0 the right). Both grips are held while they are watched
// (the transport's gate); a motion that completes casts its card's skill. Include spell_motion.h, not this.
// The controllers say where the hands are, not how the palms or fingers are, so each motion is its hands'
// path. Value-only: steamvr/motions_assassin_test.cpp.
//
// A tester's first pass (2026-11-05): "All fine except I couldnt do Burst of Speed and Fade. Just not
// registering or not clear enough." Burst of Speed wanted three pumps a hand, each back past the hip and
// never below it; Fade, the hands from above the eyes, close together. Both take more now (Tuning).
namespace framevr::spellmotion::assassin {
// The order they are watched in: the first to complete in a frame casts. Blade Shield, whose circles
// any curving path could start, last.
constexpr Motion Order[]={Motion::BurstOfSpeed,Motion::Fade,Motion::CloakOfShadows,Motion::Venom,Motion::ShadowWarrior,Motion::MindBlast,Motion::BladeShield};
// How to do each, on the MOTIONS page and in the log (what the controllers' positions can tell: no palms or
// fingers, both grips held).
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::BurstOfSpeed:return "pump both arms fast as in a sprint, two swings each";
    case Motion::Fade:return "draw both hands down in front of the face, brow to chin";
    case Motion::CloakOfShadows:return "open both arms wide at the shoulders, then sweep them across the front until they cross";
    case Motion::Venom:return "cross the wrists in front, then flick them apart fast";
    case Motion::BladeShield:return "circle the right hand in front twice";
    case Motion::ShadowWarrior:return "both hands to the chest, then push them out together to one side (she appears there)";
    case Motion::MindBlast:return "both hands beside the temples, then push them straight forward";
    default:return "no motion";
    }
}
struct Tuning {
    // Burst of Speed: sprint pumps, one hand forward while the other is back. A swing counts when the hand
    // covers pumpAmp of forward travel between turns at pumpSpeed on average, its front end between pumpFront
    // and pumpReach ahead of the eyes (a claw jab reaches farther), its back end behind pumpBack (back past
    // the hip: a jab comes back to the chest), both ends above pumpLow (bent arms, not a walk's hanging
    // ones), and the other hand pumpAgainst the other way at each end. A walking arm swing is about
    // .6 m/s. Since 2026-11-05 two swings a hand, the back end anywhere by the hip (pumpBack ahead of the
    // eyes at most: a claw jab comes back to the chest, farther out) and as low as pumpLow.
    float pumpAmp=.22f,pumpSpeed=.9f,pumpFront=.08f,pumpReach=.58f,pumpBack=.12f,pumpLow=-.95f,pumpAgainst=.10f;
    int pumpSwings=2;                 // swings each hand,
    float pumpWindow=1.2f;            // within these seconds, the last one under pumpRecent ago
    float pumpRecent=.35f;
    float turnBack=.05f;              // m a hand comes back before its turn counts
    // Fade: both hands in front of the face (within faceSide of the middle, faceNear to faceFar ahead, at
    // most fadeApart apart), at the eyes (fadeFrom or higher), then down to the chin (fadeTo or lower), each
    // fadeDrop or more, within fadeWindow s, straight down (not swept across or pushed out).
    float faceSide=.26f,faceNear=.02f,faceFar=.50f,fadeApart=.45f;
    float fadeFrom=-.08f,fadeTo=-.20f,fadeDrop=.14f,fadeWindow=1.f;
    // Cloak of Shadows: each hand cloakWide out on its own side at shoulder height (cloakHigh or higher),
    // then swept cloakSweep across, past the middle by cloakCross onto the other side, within cloakWindow s, ending
    // in front of the body or against it (cloakNear ahead or more). Two of the owner's nine (2026-11-05) missed:
    // the left hand crossed tight to the chest (5 cm ahead was the least) or the sweep took longer than 0.9 s.
    float cloakWide=.22f,cloakHigh=-.45f,cloakCross=.08f,cloakSweep=.30f,cloakWindow=1.2f,cloakNear=-.05f;
    // Venom: the wrists crossed in front (the right hand left of the left, venomClose apart or less), then
    // flicked venomFlick apart, each venomSide out on its own side, within venomWindow s.
    float venomClose=.25f,venomFlick=.45f,venomSide=.15f,venomWindow=.22f;
    // Blade Shield: the right hand circling circleTurns times (two, a little short allowed) round a centre,
    // in any plane, at least circleRadius out on average, the path round (its narrowest circleRound of its
    // widest), ahead of the eyes by circleFront all along, within circleWindow s.
    float circleTurns=1.8f,circleRadius=.07f,circleRound=.35f,circleFront=.10f,circleWindow=1.5f;
    // Shadow Warrior: both hands at the chest (within chestSide of the middle, chestApart apart, chestLow
    // to chestHigh, chestNear to chestFar ahead), then pushed pushSide or more to the same side, the leading
    // hand pushOut out and the other past the middle, mostly sideways, within pushWindow s.
    float chestSide=.22f,chestApart=.30f,chestLow=-.50f,chestHigh=-.10f,chestNear=0,chestFar=.40f;
    float pushSide=.30f,pushOut=.25f,pushWindow=.6f;
    // Mind Blast: each hand beside its temple (templeSide to templeWide out, templeLow to templeHigh,
    // templeNear to templeFar ahead), then both pushed blastOut ahead, each blastTravel forward, mostly
    // forward, within blastWindow s.
    float templeSide=.06f,templeWide=.28f,templeLow=-.18f,templeHigh=.14f,templeNear=-.12f,templeFar=.20f;
    float blastOut=.45f,blastTravel=.28f,blastWindow=.22f;
};
inline constexpr Tuning tuning{};

inline float Ahead(math::Vec l){return -l.z;}
// Burst of Speed's swings by one hand, in the last pumpWindow s.
inline int Pumps(const std::vector<Sample>& h,int hand) {
    const auto& t=tuning;
    struct Turn {size_t i;float f;};
    std::vector<Turn> turns;
    int dir=0;Turn ext{0,Ahead(h[0].l[hand])};
    for(size_t i=1;i<h.size();++i) {
        const float f=Ahead(h[i].l[hand]);
        if(dir==0){if(std::fabs(f-ext.f)>=t.turnBack){dir=f>ext.f?1:-1;ext={i,f};}continue;}
        if((dir>0&&f>ext.f)||(dir<0&&f<ext.f)){ext={i,f};continue;}
        if(std::fabs(f-ext.f)>=t.turnBack){turns.push_back(ext);dir=-dir;ext={i,f};}
    }
    if(dir!=0)turns.push_back(ext);   // the newest end, while it is being reached
    const double now=h.back().t;
    int swings=0;
    for(size_t k=1;k<turns.size();++k) {
        const auto& a=turns[k-1];const auto& b=turns[k];
        const double dt=h[b.i].t-h[a.i].t;
        if(dt<=0||now-h[a.i].t>t.pumpWindow)continue;
        const float amp=std::fabs(b.f-a.f);
        if(amp<t.pumpAmp||amp/dt<t.pumpSpeed)continue;
        bool ok=true;
        for(const auto* e:{&a,&b}) {
            const auto& s=h[e->i];
            const bool front=e->f>=(e==&a?b.f:a.f);
            const float other=Ahead(s.l[1-hand]);
            ok=ok&&s.l[hand].y>=t.pumpLow&&(front?(e->f>=t.pumpFront&&e->f<=t.pumpReach&&other<=e->f-t.pumpAgainst):
                                                  (e->f<=t.pumpBack&&other>=e->f+t.pumpAgainst));
        }
        if(ok)++swings;
    }
    if(turns.empty()||now-h[turns.back().i].t>t.pumpRecent)return 0;
    return swings;
}
// Blade Shield: the right hand's path round a centre, in the plane of axes a and b (0 x, 1 y, 2 ahead),
// from sample i on: whole turns (signed), or 0 if it is not a round path.
inline float Turns(const std::vector<Sample>& h,size_t i,int a,int b) {
    const auto& t=tuning;
    const auto at=[&](size_t k,int axis){const auto& l=h[k].l[0];return axis==0?l.x:axis==1?l.y:Ahead(l);};
    double ca=0,cb=0;const size_t n=h.size()-i;
    for(size_t k=i;k<h.size();++k){ca+=at(k,a);cb+=at(k,b);}
    ca/=double(n);cb/=double(n);
    double sum=0,rSum=0,rMin=1e9,rMax=0;
    double pa=at(i,a)-ca,pb=at(i,b)-cb;
    for(size_t k=i;k<h.size();++k) {
        const double va=at(k,a)-ca,vb=at(k,b)-cb,r=std::hypot(va,vb);
        rSum+=r;rMin=std::min(rMin,r);rMax=std::max(rMax,r);
        if(k>i) {
            const double step=std::atan2(pa*vb-pb*va,pa*va+pb*vb);
            if(std::fabs(step)>1.05)return 0;   // through the centre: a line, not a circle
            sum+=step;
        }
        pa=va;pb=vb;
    }
    if(rSum/double(n)<t.circleRadius||rMin<t.circleRound*rMax)return 0;
    return float(sum/(2*3.14159265358979));
}
// most: the most whole turns seen (for the buzz).
inline bool Circles(const std::vector<Sample>& h,float& most) {
    const auto& t=tuning;
    most=0;
    const size_t last=h.size()-1;
    if(h.size()<20||Ahead(h[last].l[0])<t.circleFront)return false;
    // The path length over the window first: most frames have the hand nowhere near two circles of it.
    size_t from=last;
    float path=0;
    while(from>0&&h[last].t-h[from-1].t<=t.circleWindow&&Ahead(h[from-1].l[0])>=t.circleFront){
        path+=Engine::Len(h[from].l[0]-h[from-1].l[0]);--from;
    }
    if(path<.5f*2*3.14159265f*t.circleRadius)return false;   // not half a circle's path yet
    for(size_t i=from;i+20<=last;i+=3)
        for(const auto [a,b]:{std::pair{0,1},std::pair{0,2},std::pair{1,2}}) {
            const float turns=std::fabs(Turns(h,i,a,b));
            most=std::max(most,turns);
            if(turns>=t.circleTurns&&path>=t.circleTurns*2*3.14159265f*t.circleRadius)return true;
        }
    return false;
}

inline bool Done(Engine& e,Motion m) {
    const auto& h=e.History();
    if(h.empty())return false;
    const auto& t=tuning;
    const auto& n=h.back();
    const auto& r=n.l[0];const auto& l=n.l[1];
    const auto within=[&](double seconds){return [&n,seconds](const Sample& s){return n.t-s.t<=seconds;};};
    switch(m) {
    case Motion::BurstOfSpeed: {
        const int least=std::min(Pumps(h,0),Pumps(h,1));
        if(least>=1)e.Toward(float(least)/float(t.pumpSwings));
        return least>=t.pumpSwings;
    }
    case Motion::Fade: {
        const auto face=[&](math::Vec p){return std::fabs(p.x)<=t.faceSide&&Ahead(p)>=t.faceNear&&Ahead(p)<=t.faceFar;};
        const bool end=face(r)&&face(l)&&r.y<=t.fadeTo&&l.y<=t.fadeTo&&Engine::Apart(n)<=t.fadeApart;
        const auto recent=within(t.fadeWindow);
        for(const auto& s:h) {
            if(!recent(s)||Engine::Apart(s)>t.fadeApart)continue;
            bool from=true;float least=1e9f;
            for(int k=0;k<2;++k){const auto& a=s.l[k];const auto& c=n.l[k];least=std::min(least,a.y-c.y);
                from=from&&face(a)&&a.y>=t.fadeFrom&&std::fabs(a.x-c.x)<=.20f&&std::fabs(Ahead(a)-Ahead(c))<=.25f;}
            if(!from)continue;
            e.Toward(least/t.fadeDrop);   // at the eyes, then warmer as they come down
            if(end&&least>=t.fadeDrop)return true;
        }
        return false;
    }
    case Motion::CloakOfShadows: {
        const auto front=[&](math::Vec p){return Ahead(p)>=t.cloakNear&&Ahead(p)<=.55f&&p.y>=-.65f&&p.y<=-.05f;};
        const bool end=r.x<=-t.cloakCross&&l.x>=t.cloakCross&&front(r)&&front(l);
        const auto recent=within(t.cloakWindow);
        for(const auto& s:h) {
            if(!recent(s)||s.l[0].x<t.cloakWide||s.l[1].x>-t.cloakWide||s.l[0].y<t.cloakHigh||s.l[1].y<t.cloakHigh)continue;
            const float least=std::min(s.l[0].x-r.x,l.x-s.l[1].x);
            e.Toward(least/(s.l[0].x+t.cloakCross));   // wide, then warmer as they sweep across
            if(end&&least>=t.cloakSweep)return true;
        }
        return false;
    }
    case Motion::Venom: {
        const bool end=r.x>=t.venomSide&&l.x<=-t.venomSide&&Engine::Apart(n)>=t.venomFlick&&Ahead(r)>=.10f&&Ahead(l)>=.10f&&r.y>=-.65f&&l.y>=-.65f;
        const auto recent=within(t.venomWindow);
        for(const auto& s:h)
            if(recent(s)&&s.l[0].x<s.l[1].x&&Engine::Apart(s)<=t.venomClose&&Ahead(s.l[0])>=.10f&&Ahead(s.l[1])>=.10f) {
                e.Toward((Engine::Apart(n)-t.venomClose)/(t.venomFlick-t.venomClose));
                if(end)return true;
            }
        if(r.x<l.x&&Engine::Apart(n)<=t.venomClose&&Ahead(r)>=.10f&&Ahead(l)>=.10f)e.Toward(0);   // crossed: the flick to come
        return false;
    }
    case Motion::BladeShield: {
        float most=0;
        const bool done=Circles(h,most);
        if(most>=.5f)e.Toward(most/t.circleTurns);
        return done;
    }
    case Motion::ShadowWarrior: {
        const auto chest=[&](math::Vec p){return std::fabs(p.x)<=t.chestSide&&p.y>=t.chestLow&&p.y<=t.chestHigh&&Ahead(p)>=t.chestNear&&Ahead(p)<=t.chestFar;};
        if(chest(r)&&chest(l)&&Engine::Apart(n)<=t.chestApart)e.Toward(0);   // at the chest: the push to come
        const auto recent=within(t.pushWindow);
        for(const int side:{1,-1}) {
            // the leading hand: the right going right, the left going left
            const auto& lead=side>0?r:l;const auto& trail=side>0?l:r;
            for(const auto& s:h) {
                if(!recent(s)||!chest(s.l[0])||!chest(s.l[1])||Engine::Apart(s)>t.chestApart)continue;
                bool pushed=true;float least=1e9f;math::Vec way{};
                for(int k=0;k<2;++k){const auto d=n.l[k]-s.l[k];least=std::min(least,d.x*float(side));
                    pushed=pushed&&d.x*float(side)>=1.2f*std::fabs(d.z)&&std::fabs(d.y)<=.25f;way=way+d*.5f;}
                if(!pushed||least<.06f)continue;
                e.Toward(least/t.pushSide);
                if(least>=t.pushSide&&lead.x*float(side)>=t.pushOut&&trail.x*float(side)>=.02f){e.Aim({way.x,0,way.z});return true;}
            }
        }
        return false;
    }
    case Motion::MindBlast: {
        const bool end=Ahead(r)>=t.blastOut&&Ahead(l)>=t.blastOut&&r.y>=-.40f&&r.y<=.20f&&l.y>=-.40f&&l.y<=.20f;
        const auto recent=within(t.blastWindow+.4);   // the temples a little longer ago: the buzz before the push
        for(const auto& s:h) {
            if(!recent(s))continue;
            bool from=true;float least=1e9f;
            for(int k=0;k<2;++k) {
                const auto& a=s.l[k];const auto& c=n.l[k];const float side=k==0?a.x:-a.x;
                const float travel=Ahead(c)-Ahead(a);least=std::min(least,travel);
                from=from&&side>=t.templeSide&&side<=t.templeWide&&a.y>=t.templeLow&&a.y<=t.templeHigh&&
                     Ahead(a)>=t.templeNear&&Ahead(a)<=t.templeFar&&std::fabs(c.y-a.y)<=.7f*std::max(travel,.05f)&&
                     std::fabs(c.x-a.x)<=.7f*std::max(travel,.05f);
            }
            if(!from)continue;
            e.Toward(least/t.blastTravel);
            if(end&&least>=t.blastTravel&&n.t-s.t<=t.blastWindow)return true;
        }
        return false;
    }
    default:return false;
    }
}
}
