#include "hammer_gesture.h"
#include "spell_motion.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
// Body frame (yaw-local: +X right, +Y up, -Z forward) relative to the head.
// Hands are {right, left}. The controllers move between keyframes at 50 Hz,
// so velocities come from the motion.
struct Sim {
    // The motions' gate (spell_motion.h, owner 2026-11-04: "Both grips, like Sorceress"): gated, in.active is it.
    bool gated=false;spellmotion::Gate gate;float grips[2]{};
    hammerpump::Engine g;hammerpump::Input in;hammerpump::Output out;
    math::Quat yaw{0,0,0,1};math::Vec head{0,1.7f,0};
    int casts=0,lastHand=-1,drops=0;std::string lastDrop;
    // natural: minimum-jerk easing at 90 Hz, as a real arm moves (accelerates
    // from rest, peaks mid-stroke, slows to a stop). The 2026-10-27 headset
    // test rejected every real punch that constant-speed keyframes passed.
    bool natural=false;float frame=.02f;
    void Natural(){natural=true;frame=1/90.f;}
    float Ease(float t)const{return natural?t*t*t*(10+t*(-15+6*t)):t;}
    Sim(float heading=0){yaw=math::Yaw(heading);in.time=1;in.session=7;in.epoch=1;in.active=true;in.tracked[0]=in.tracked[1]=true;
        in.head=head;in.headRotation=yaw;Place({.25f,-.55f,-.2f},{-.25f,-.55f,-.2f});Step();}
    void Place(math::Vec right,math::Vec left){in.hand[0]=head+math::Rotate(yaw,right);in.hand[1]=head+math::Rotate(yaw,left);}
    void Move(math::Vec right,math::Vec left,float seconds) {
        const math::Vec a0=in.hand[0],b0=in.hand[1];
        const math::Vec a1=head+math::Rotate(yaw,right),b1=head+math::Rotate(yaw,left);
        const int steps=std::max(1,int(seconds/frame));
        for(int i=1;i<=steps;++i){const float t=Ease(float(i)/steps);in.hand[0]=a0+(a1-a0)*t;in.hand[1]=b0+(b1-b0)*t;Step();}
    }
    void Hold(float seconds){for(int i=0;i<int(seconds/frame);++i)Step();}
    void Step(){in.time+=frame;if(gated)in.active=gate.Step(grips,true).watch;out=g.Step(in);if(out.cast>=0){++casts;lastHand=out.cast;}if(out.dropped){++drops;lastDrop=out.dropped;}}
    // Right fist from the shoulder straight up past the head.
    void Pump(int hand=0,float seconds=.2f,float from=-.3f,float to=.2f) {
        const math::Vec rest[2]{{.25f,-.55f,-.2f},{-.25f,-.55f,-.2f}};
        const float x=hand?-.2f:.2f;
        in.grip[hand]=1;
        math::Vec r=hand?rest[0]:math::Vec{x,from,-.2f},l=hand?math::Vec{x,from,-.2f}:rest[1];
        Move(r,l,.25f);Hold(.1f);
        r=hand?rest[0]:math::Vec{x,to,-.25f};l=hand?math::Vec{x,to,-.25f}:rest[1];
        Move(r,l,seconds);Hold(.1f);
    }
    void Lower(int hand=0){const math::Vec r=hand?math::Vec{.25f,-.55f,-.2f}:math::Vec{.2f,-.3f,-.2f},l=hand?math::Vec{-.2f,-.3f,-.2f}:math::Vec{-.25f,-.55f,-.2f};Move(r,l,.25f);}
};
int main(){
    {   // Under the motions' gate (both grips, owner 2026-11-04): the old fist alone (one grip) no longer pumps.
        Sim one;one.gated=true;one.grips[0]=1;one.Pump();Check(one.casts==0,"gated, one grip (the old fist) no longer pumps");
        Sim both;both.gated=true;both.grips[0]=both.grips[1]=1;both.Pump();Check(both.casts==1,"gated, both grips held: the pump casts");
        Sim left;left.gated=true;left.grips[0]=left.grips[1]=1;left.Pump(1);Check(left.casts==1&&left.lastHand==1,"gated, either fist pumps");
    }
    for(float heading:{-2.f,0.f,2.f}) {
        Sim s(heading);s.Pump();
        Check(s.casts==1&&s.lastHand==0,"a right fist punched up past the head casts once at any heading");
        Sim l(heading);l.Pump(1);
        Check(l.casts==1&&l.lastHand==1,"the left fist casts too");
    }
    {Sim s;s.Pump(0,.2f,-.3f,.2f);s.Hold(1);Check(s.casts==1,"a fist held high after the pump never repeats");
     s.Lower();s.Pump();Check(s.casts==2,"bringing the fist back down loads the next pump");}
    {Sim s;s.Pump();s.Pump();Check(s.casts==2,"pump, down, pump: one hammer each");}
    {Sim s;s.in.grip[0]=0;const math::Vec r{.2f,-.3f,-.2f},l{-.25f,-.55f,-.2f};s.Move(r,l,.25f);s.Hold(.1f);
     s.Move({.2f,.2f,-.25f},l,.2f);Check(s.casts==0,"an open hand raised fast is not a pump");}
    {Sim s;s.in.grip[0]=1;s.in.busy[0]=true;s.Pump();Check(s.casts==0,"a busy hand (trigger squeezed, bottle) never pumps");}
    {Sim s;s.Pump(0,.8f);Check(s.casts==0,"a slow raise is not a punch");}
    {Sim s;s.Pump(0,.12f,-.3f,-.02f);Check(s.casts==0,"a punch that stops below the head does not cast");
     Check(s.drops==1&&s.lastDrop.find("stalled")!=std::string::npos,"the stalled punch logs once");}
    {Sim s;s.in.grip[0]=1;s.Move({.2f,.3f,-.25f},{-.25f,-.55f,-.2f},1.f);s.Hold(.2f);
     s.Move({.2f,-.05f,-.25f},{-.25f,-.55f,-.2f},.2f);s.Move({.2f,.3f,-.25f},{-.25f,-.55f,-.2f},.12f);
     Check(s.casts==0,"a fist bobbing just under the head without coming down never loads");}
    {Sim s;s.in.grip[0]=1;const math::Vec l{-.25f,-.55f,-.2f};s.Move({.2f,-.3f,-.2f},l,.25f);s.Hold(.1f);
     s.Move({.2f,.2f,-.9f},l,.2f);Check(s.casts==0,"a forward punch (strike) is not a pump");}
    {Sim s;s.in.grip[0]=1;const math::Vec l{-.25f,-.55f,-.2f};s.Move({.2f,-.3f,-.2f},l,.25f);s.Hold(.1f);
     s.Move({.2f,-.05f,-.25f},l,.1f);s.in.grip[0]=0;s.Move({.2f,.2f,-.25f},l,.1f);
     Check(s.casts==0&&s.lastDrop.find("opened")!=std::string::npos,"opening the fist mid-punch abandons it");}
    {Sim s;s.Pump();s.in.session=8;s.Hold(.1f);s.Lower();s.Pump();Check(s.casts==2,"a new session starts clean");}
    {Sim s;s.in.grip[0]=s.in.grip[1]=1;
     s.Move({.2f,-.3f,-.2f},{-.2f,-.3f,-.2f},.25f);s.Hold(.1f);
     s.Move({.2f,.2f,-.25f},{-.2f,.2f,-.25f},.2f);
     Check(s.casts==1,"both fists together cast one hammer");}
    // Real arm motion (minimum-jerk, 90 Hz): travel counts from where the punch began.
    for(float heading:{-2.f,0.f,2.f}) {
        Sim s(heading);s.Natural();s.Pump(0,.3f,-.3f,.25f);Check(s.casts==1,"a natural pump from the shoulder casts");
        Sim l(heading);l.Natural();l.Pump(1,.35f,-.55f,.2f);Check(l.casts==1,"a natural pump from the hip casts (left)");
    }
    {Sim s;s.Natural();for(int i=0;i<5;++i){s.Pump(0,.3f,-.3f,.25f);}Check(s.casts==5,"five natural pumps, five hammers");}
    {Sim s;s.Natural();s.Pump(0,.25f,-.3f,-.05f);Check(s.casts==0,"a natural punch that stops under the head does not cast");}
    {Sim s;s.Natural();s.Pump(0,.9f,-.3f,.25f);Check(s.casts==0,"a natural slow raise is not a punch");}
    {Sim s;s.Natural();s.in.grip[0]=1;const math::Vec l{-.25f,-.55f,-.2f};s.Move({.2f,-.3f,-.2f},l,.3f);s.Hold(.1f);
     s.natural=false;s.Move({.2f,-.12f,-.2f},l,1.f);s.natural=true;s.Move({.2f,.1f,-.25f},l,.12f);s.Hold(.1f);
     Check(s.casts==0,"a slow drift up then a short flick is not a pump");}
    std::puts("hammer gesture tests passed");
}
