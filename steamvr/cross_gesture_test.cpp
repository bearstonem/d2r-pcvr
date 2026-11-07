#include "cross_gesture.h"
#include "spell_motion.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
// Head-local points (+X right, +Y up, -Z forward). Hands {right, left}; the
// moving hand travels between points with minimum-jerk easing at 90 Hz.
static const math::Vec Rest{.25f,-.6f,-.15f},Forehead{0,.06f,-.12f},Chest{0,-.42f,-.12f},LeftShoulder{-.2f,-.28f,-.05f},RightShoulder{.2f,-.28f,-.05f};
struct Sim {
    // The motions' gate (spell_motion.h, owner 2026-11-04: "Both grips, like Sorceress"): gated, in.active is it.
    bool gated=false;spellmotion::Gate gate;float grips[2]{};
    holycross::Engine g;holycross::Input in;holycross::Output out;
    math::Quat yaw{0,0,0,1};math::Vec head{0,1.7f,0};
    int casts=0,lastHand=-1,drops=0;std::string lastDrop;
    float frame=1/90.f;
    Sim(float heading=0){yaw=math::Yaw(heading);in.time=1;in.session=3;in.epoch=1;in.active=true;in.tracked[0]=in.tracked[1]=true;
        in.head=head;in.headRotation=yaw;Place(0,Rest);Place(1,{-.25f,-.6f,-.15f});Step();}
    void Place(int h,math::Vec p){in.hand[h]=head+math::Rotate(yaw,p);}
    static float Ease(float t){return t*t*t*(10+t*(-15+6*t));}
    void Move(int h,math::Vec to,float seconds) {
        const math::Vec a=in.hand[h],b=head+math::Rotate(yaw,to);
        const int steps=std::max(1,int(seconds/frame));
        for(int i=1;i<=steps;++i){in.hand[h]=a+(b-a)*Ease(float(i)/steps);Step();}
    }
    void Hold(float s){for(int i=0;i<int(s/frame);++i)Step();}
    void Step(){in.time+=frame;if(gated)in.active=gate.Step(grips,true).watch;out=g.Step(in);if(out.cast>=0){++casts;lastHand=out.cast;}if(out.dropped){++drops;lastDrop=out.dropped;}}
    // The sign at a deliberate pace: each move ~0.35 s, a short touch at each point.
    void Sign(int h=0,bool rightFirst=false,float move=.35f,float touch=.08f) {
        const math::Vec a=rightFirst?RightShoulder:LeftShoulder,b=rightFirst?LeftShoulder:RightShoulder;
        for(const auto& p:{Forehead,Chest,a,b}){Move(h,p,move);Hold(touch);}
        Move(h,h?math::Vec{-.25f,-.6f,-.15f}:Rest,.4f);
    }
};
int main(){
    {   // Under the motions' gate (both grips, owner 2026-11-04): a sign with no grip, as before, no longer casts.
        Sim none;none.gated=true;none.Sign();Check(none.casts==0,"gated, a sign with no grip no longer casts");
        Sim one;one.gated=true;one.grips[1]=1;one.Sign();Check(one.casts==0,"gated, one grip is not enough");
        Sim both;both.gated=true;both.grips[0]=both.grips[1]=1;both.Sign();Check(both.casts==1,"gated, both grips held: the sign casts");
    }
    for(float heading:{-2.f,0.f,2.f}){Sim s(heading);s.Sign();Check(s.casts==1&&s.lastHand==0,"forehead, chest, left, right casts at any heading");}
    {Sim s;s.Sign(0,true);Check(s.casts==1,"right shoulder before left (Orthodox) casts too");}
    {Sim s;s.Sign(1);Check(s.casts==1&&s.lastHand==1,"the left hand can sign");}
    {Sim s;s.Sign(0,false,.35f,0);Check(s.casts==1,"a flowing sign with no hold at each point still casts (the hand slows to turn)");}
    {Sim s;s.Sign();s.Sign();Check(s.casts==1,"a second sign inside the cooldown is ignored");s.Hold(1.6f);s.Sign();Check(s.casts==2,"after the cooldown it casts again");}
    {Sim s;for(const auto& p:{Forehead,Chest,LeftShoulder}){s.Move(0,p,.35f);s.Hold(.08f);}s.Move(0,Rest,.4f);s.Hold(1.2f);
     Check(s.casts==0&&s.lastDrop.find("too long")!=std::string::npos,"three touches then nothing: dropped, logged");}
    {Sim s;s.in.busy[0]=true;s.Sign();Check(s.casts==0,"a busy hand (trigger squeezed, bottle) never signs");}
    {Sim s;for(const auto& p:{Chest,Forehead,LeftShoulder,RightShoulder}){s.Move(0,p,.35f);s.Hold(.08f);}Check(s.casts==0,"chest first is not the sign");}
    {Sim s;for(const auto& p:{Forehead,LeftShoulder,Chest,RightShoulder}){s.Move(0,p,.35f);s.Hold(.08f);}Check(s.casts==0,"forehead then a shoulder is not the sign");}
    {Sim s;for(const auto& p:{Forehead,Chest,LeftShoulder,LeftShoulder}){s.Move(0,p,.35f);s.Hold(.08f);}Check(s.casts==0,"the same shoulder twice is not the sign");}
    {Sim s;for(int i=0;i<3;++i){s.Move(0,{-.2f,.05f,-.15f},.12f);s.Move(0,{.2f,.05f,-.15f},.12f);}Check(s.casts==0,"a face wipe never signs");}
    {Sim s;for(const auto& p:{Forehead,Chest,LeftShoulder,RightShoulder})s.Move(0,p,.07f);Check(s.casts==0,"a fast swing through the same points is not a touch");}
    {Sim s;s.Move(0,{.2f,.3f,-.3f},.3f);s.Move(0,{-.2f,-.7f,-.5f},.15f);s.Move(0,{.3f,-.2f,-.6f},.15f);s.Move(0,{-.3f,-.3f,-.6f},.15f);
     Check(s.casts==0,"overhead chop then slashes in front never sign");}
    {Sim s;s.Move(0,Forehead,.35f);s.Hold(.08f);s.Move(0,Chest,.35f);s.Hold(.08f);s.in.session=4;s.Move(0,LeftShoulder,.35f);s.Hold(.08f);s.Move(0,RightShoulder,.35f);
     Check(s.casts==0,"a new session forgets a sign in progress");}
    std::puts("cross gesture tests passed");
}
