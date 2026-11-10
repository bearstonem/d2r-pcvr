#include "shoulder_swap.h"
#include "hand_role.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
struct Sim {
    shoulder::Gesture g;shoulder::Input in;shoulder::Output out;
    Sim(){in.time=1;in.session=7;in.epoch=1;in.active=in.tracked=in.allowed=true;in.head={0,1.7f,0};in.hand={.23f,1.5f,.14f};}
    shoulder::Output Step(int n=1){while(n--){in.time+=.02;out=g.Step(in);}return out;}
};
int main(){
    for(int hand=0;hand<2;++hand)for(float side:{-.23f,.23f})for(float yaw:{-2.f,0.f,2.f}){
        Sim s;s.in.dominant=hand;s.in.heading=math::Yaw(yaw);s.in.head={2,1.2f,-4};
        s.in.hand=s.in.head+math::Rotate(s.in.heading,{side,-.2f,.14f});
        s.Step(10);Check(!s.out.block,"reaching behind head without grip leaves javelin wind-up available");s.in.grip=1;
        Check(s.Step().swap&&s.out.block,"either hand can swap from either shoulder at any yaw, standing or seated");
        for(int i=0;i<15;++i)Check(!s.Step().swap&&s.out.block,"held grip never repeats or attacks");
        s.in.hand=s.in.head+math::Rotate(s.in.heading,{0,-.3f,-.4f});Check(s.Step().block,"moving away while held still consumes grip");
        s.in.grip=0;Check(!s.Step().block,"release restores combat away from shoulder");
    }
    for(math::Vec p:{math::Vec{.23f,1.5f,-.14f},math::Vec{.23f,.9f,0},math::Vec{0,1.5f,.14f},math::Vec{.9f,1.5f,.14f}}){
        Sim s;s.in.hand=p;s.Step(10);s.in.grip=1;Check(!s.Step().swap&&!s.out.block,"face, hip, head centre and distant hand do not claim grip");}
    {Sim s;s.in.grip=1;s.Step(10);Check(!s.out.swap&&s.out.block,"held grip on tracking start cannot swap");
     s.in.grip=0;s.Step();s.in.grip=1;Check(s.Step().swap,"fresh press after release swaps");}
    {Sim s;s.in.hand.z=-.2f;s.Step(10);s.in.grip=1;s.Step();s.in.hand.z=.14f;
     for(int i=0;i<15;++i)Check(!s.Step().swap,"entering shoulder with grip held cannot swap");}
    {Sim s;s.Step(10);s.in.allowed=false;s.in.grip=1;Check(!s.Step().swap,"wheel, grabbed bottle and pending actions prevent swap");
     s.in.allowed=true;Check(!s.Step().swap,"context reopening cannot replay a held press");}
    {Sim s;s.Step(10);s.in.tracked=false;s.in.grip=1;s.Step();s.in.tracked=true;
     for(int i=0;i<12;++i)Check(!s.Step().swap,"tracking return cannot swap with held grip");}
    for(int change=0;change<3;++change){Sim s;s.Step(10);s.in.grip=1;
        if(change==0)++s.in.epoch;if(change==1)++s.in.session;if(change==2)s.in.dominant=1;
        Check(!s.Step().swap,"recenter, session or dominant hand change consumes holds");}
    {Sim s;s.Step(10);s.in.time+=1;s.in.grip=1;Check(!s.Step().swap,"frame hitch cannot replay gesture");}
    {   // Left-handed play (hand_role.h): the swap hand is the LEFT hand, and
        // stays the left hand in the same role when the gesture takes out a bow or a staff (phase 3: those
        // are mirrored too, so the roles no longer move with the weapon). The gesture is fed that hand's
        // pose and grip; `dominant` is its role index.
        using namespace handrole;
        for(const uint32_t weapon:{uint32_t(D2RVR_TYPE_SWORD),uint32_t(D2RVR_TYPE_BOW),uint32_t(D2RVR_TYPE_STAFF),uint32_t(D2RVR_TYPE_CROSSBOW)}){
            const auto state=Resolve(true,weapon,0);
            const int physical=Dominant(true),role=state.map.Role(physical);
            Check(physical==1&&role==0,"the left hand, the main hand with a sword, a bow, a staff or a crossbow alike: swapping never moves the swap hand's role");
            math::Vec hands[2]={{.25f,1.2f,-.3f},{-.23f,1.5f,.14f}};   // physical: the left one is over the left shoulder
            float grips[2]={0,0};
            Sim g;g.in.dominant=role;
            const auto step=[&](int n=1){g.in.hand=hands[state.map.Physical(role)];g.in.grip=grips[state.map.Physical(role)];return g.Step(n);};
            step(10);grips[0]=1;Check(!step().swap,"the right hand's grip is not the swap's");
            grips[0]=0;step();grips[1]=1;Check(step().swap,"the left hand's grip over the shoulder swaps");
        }
        Check(Dominant(false)==0,"Left-handed off: the right hand");
        Sim a,b;a.in.dominant=0;b.in.dominant=Map{}.Role(Dominant(false));
        for(int i=0;i<12;++i){a.in.grip=b.in.grip=i>9?1.f:0.f;const auto x=a.Step(),y=b.Step();Check(x.swap==y.swap&&x.block==y.block&&x.inReach==y.inReach,"and the gesture unchanged");}
    }
    std::puts("Shoulder swap: either shoulder, dominant hand, yaw, seated pose, one press, release, tracking/context guards and the left-handed swap hand passed");
}
