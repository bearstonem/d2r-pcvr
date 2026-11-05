#pragma once
#include "spell_motion.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

// A simulated pair of hands for the motions' tests: head-local points (+X right, +Y up, -Z forward),
// hands {right, left}, eased between points (minimum jerk) at 90 Hz. Free by default, as the transport
// watches every class's motions (in.active false: the gate closed). Test-only.
namespace framevr::motionsim {
inline void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
inline const math::Vec RestR{.25f,-.6f,-.15f},RestL{-.25f,-.6f,-.15f};
struct Sim {
    spellmotion::Engine g;spellmotion::Input in;spellmotion::Output out;
    math::Quat yaw{0,0,0,1};math::Vec head{0,1.7f,0};
    int casts=0;
    math::Vec aim{};   // the last cast's way (head-local)
    float frame=1/90.f;
    explicit Sim(spellmotion::Motion m,float heading=0){yaw=math::Yaw(heading);in.time=1;in.session=3;in.epoch=1;in.active=true;in.free=true;in.motion=m;
        in.tracked[0]=in.tracked[1]=true;in.head=head;in.headRotation=yaw;Place(0,RestR);Place(1,RestL);Step();}
    void Place(int h,math::Vec p){in.hand[h]=head+math::Rotate(yaw,p);}
    static float Ease(float t){return t*t*t*(10+t*(-15+6*t));}
    // Both hands to their points together.
    void Move(math::Vec r,math::Vec l,float seconds) {
        const math::Vec a[2]={in.hand[0],in.hand[1]},b[2]={head+math::Rotate(yaw,r),head+math::Rotate(yaw,l)};
        const int steps=std::max(1,int(seconds/frame));
        for(int i=1;i<=steps;++i){for(int h=0;h<2;++h)in.hand[h]=a[h]+(b[h]-a[h])*Ease(float(i)/steps);Step();}
    }
    void MoveR(math::Vec r,float seconds){Move(r,Local(1),seconds);}
    void MoveL(math::Vec l,float seconds){Move(Local(0),l,seconds);}
    math::Vec Local(int h)const{return math::Rotate(math::Conjugate(yaw),in.hand[h]-head);}
    // Each hand where f puts it at t seconds in.
    void Drive(float seconds,const std::function<void(float,math::Vec&,math::Vec&)>& f) {
        for(int i=1;i<=int(seconds/frame);++i){math::Vec r=Local(0),l=Local(1);f(i*frame,r,l);Place(0,r);Place(1,l);Step();}
    }
    void Hold(float s){for(int i=0;i<int(s/frame);++i)Step();}
    void Step(){in.time+=frame;out=g.Step(in);if(out.cast){++casts;aim=out.aim;}}
};
}
