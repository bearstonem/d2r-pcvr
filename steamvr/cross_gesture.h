#pragma once
#include "frame_math.h"
#include <cmath>
#include <cstdint>

// Paladin Holy Shield: the sign of the cross (Cal, 2026-11-01: "a holy trinity
// (the father, son and holy spirit) hand motion"). Value-only: the transport
// supplies the head and hands and pulses the slot Holy Shield is bound to, so
// mana, the buff and its glow stay native.
//
// The sign: with either hand (trigger free), touch the forehead, then the
// chest, then one shoulder and the other (left then right, or right then
// left). Each touch is a pause: the hand slows inside the spot, so a swing
// passing through the same places never counts. One sign casts once.
namespace framevr::holycross {
// Head-local box: head position, head yaw only (+X right, +Y up, -Z forward).
struct Box {float x0,x1,y0,y1,f0,f1;};   // f: forward of the eyes (-z)
struct Tuning {
    Box forehead{-.12f,.12f,-.06f,.14f,-.10f,.25f};
    Box chest{-.16f,.16f,-.58f,-.26f,-.15f,.30f};
    Box left{-.34f,-.10f,-.42f,-.12f,-.20f,.30f};
    Box right{.10f,.34f,-.42f,-.12f,-.20f,.30f};
    float touchSpeed=1.0f;   // m/s: inside a spot and slower than this is a touch
    float stepWindow=1.0f;   // s between one touch and the next
    float cooldown=1.5f;     // s after a cast before the next sign can start
};
enum class Spot {None,Forehead,Chest,Left,Right};
struct Input {
    double time=0;
    uint64_t session=0,epoch=0;
    bool active=false;
    math::Vec head{};math::Quat headRotation{0,0,0,1};
    math::Vec hand[2]{},handVelocity[2]{};   // world frame; 0 right, 1 left
    bool tracked[2]{},busy[2]{},velocityKnown[2]{};
};
struct Output {
    int cast=-1;                  // the hand whose sign completed this step
    int droppedHand=-1;
    const char* dropped=nullptr;  // a sign in progress abandoned this step (for the log)
    int reached=0;                // touches it had made
};
class Engine {
    // stage: touches made (0 none, 1 forehead, 2 chest, 3 first shoulder).
    struct Hand {int stage=0;Spot first=Spot::None,last=Spot::None;double at=0;math::Vec previous{};bool havePrevious=false;};
    Hand hands[2]{};
    uint64_t session=0,epoch=0;
    double last=0,castAt=-1;
    static bool Finite(math::Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
    static bool In(const Box& b,math::Vec p){const float f=-p.z;return p.x>=b.x0&&p.x<=b.x1&&p.y>=b.y0&&p.y<=b.y1&&f>=b.f0&&f<=b.f1;}
    void Drop(Output& out,int h,const char* why){auto& s=hands[h];if(s.stage>=2&&!out.dropped){out.dropped=why;out.droppedHand=h;out.reached=s.stage;}s=Hand{};}
public:
    Tuning tuning;
    void Reset(){const auto keep=tuning;*this=Engine{};tuning=keep;}
    int Stage(int h)const{return h>=0&&h<2?hands[h].stage:0;}
    Spot Touch(math::Vec local)const {
        if(In(tuning.forehead,local))return Spot::Forehead;
        if(In(tuning.chest,local))return Spot::Chest;
        if(In(tuning.left,local))return Spot::Left;
        if(In(tuning.right,local))return Spot::Right;
        return Spot::None;
    }
    Output Step(const Input& in) {
        Output out;
        if(!std::isfinite(in.time)||!Finite(in.head)){Reset();return out;}
        if(in.session!=session||in.epoch!=epoch){Reset();session=in.session;epoch=in.epoch;}
        const double dt=in.time-last;
        const bool continuous=last>0&&dt>0&&dt<=.15;
        last=in.time;
        const bool cooling=castAt>=0&&in.time-castAt<tuning.cooldown;
        const auto inverse=math::Conjugate(math::Yaw(math::Heading(in.headRotation)));
        for(int h=0;h<2;++h) {
            auto& s=hands[h];
            const bool ready=in.active&&in.tracked[h]&&!in.busy[h]&&Finite(in.hand[h]);
            if(!continuous||!ready||cooling){Drop(out,h,!continuous?"frame gap":cooling?"cooldown":"hand busy, untracked or input inactive");s.havePrevious=false;continue;}
            const auto v=in.velocityKnown[h]?in.handVelocity[h]:s.havePrevious?(in.hand[h]-s.previous)*float(1/dt):math::Vec{};
            const bool speedKnown=in.velocityKnown[h]||s.havePrevious;
            s.previous=in.hand[h];s.havePrevious=true;
            if(s.stage&&in.time-s.at>tuning.stepWindow){Drop(out,h,"too long between touches");continue;}
            if(!speedKnown||std::sqrt(math::Dot(v,v))>tuning.touchSpeed)continue;   // moving: not a touch
            const Spot spot=Touch(math::Rotate(inverse,in.hand[h]-in.head));
            if(spot==Spot::None||spot==s.last)continue;
            switch(s.stage) {
            case 0:if(spot==Spot::Forehead){s.stage=1;s.last=spot;s.at=in.time;}break;
            case 1:
                if(spot==Spot::Chest){s.stage=2;s.last=spot;s.at=in.time;}
                else if(spot!=Spot::Forehead){s=Hand{};s.havePrevious=true;s.previous=in.hand[h];}   // forehead then a shoulder: not the sign
                break;
            case 2:
                if(spot==Spot::Left||spot==Spot::Right){s.stage=3;s.first=spot;s.last=spot;s.at=in.time;}
                else if(spot==Spot::Forehead){s.stage=1;s.last=spot;s.at=in.time;}   // started over
                break;
            case 3:
                if((spot==Spot::Left||spot==Spot::Right)&&spot!=s.first) {
                    if(out.cast<0)out.cast=h;
                    castAt=in.time;s=Hand{};s.havePrevious=true;s.previous=in.hand[h];
                }
                else if(spot==Spot::Forehead){s=Hand{};s.stage=1;s.last=spot;s.at=in.time;s.havePrevious=true;s.previous=in.hand[h];}
                break;
            }
        }
        if(out.cast>=0)for(int h=0;h<2;++h)if(h!=out.cast){hands[h]=Hand{};}
        return out;
    }
};
}
