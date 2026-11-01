#include "heavens_gesture.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
// Head-local (+X right, +Y up, -Z forward). The right shoulder sits where the
// tuning expects it; points on the arm are given by elevation (degrees above
// level) and reach (m from the shoulder), straight ahead unless yawed.
static math::Vec Arm(float elevationDeg,float reach,float yawDeg=0) {
    const float e=elevationDeg*3.14159265f/180,y=yawDeg*3.14159265f/180;
    return {.18f+reach*std::cos(e)*std::sin(y),-.22f+reach*std::sin(e),.06f-reach*std::cos(e)*std::cos(y)};
}
struct Sim {
    heavensfist::Engine g;heavensfist::Input in;heavensfist::Output out;
    math::Quat yaw{0,0,0,1};math::Vec head{0,1.7f,0};
    int casts=0,drops=0;std::string lastDrop;float frame=1/90.f;
    Sim(float heading=0,float span=0){yaw=math::Yaw(heading);in.time=1;in.session=5;in.epoch=1;in.active=in.tracked=true;in.armSpan=span;
        in.head=head;in.headRotation=yaw;Place({.25f,-.6f,-.15f});Step();}
    void Place(math::Vec p){in.hand=head+math::Rotate(yaw,p);}
    static float Ease(float t){return t*t*t*(10+t*(-15+6*t));}
    void Move(math::Vec to,float s){const math::Vec a=in.hand,b=head+math::Rotate(yaw,to);const int n=std::max(1,int(s/frame));
        for(int i=1;i<=n;++i){in.hand=a+(b-a)*Ease(float(i)/n);Step();}}
    // Swing the straight arm about the shoulder from one elevation to another.
    void Sweep(float from,float to,float s,float reach=.6f,float yawDeg=0){const int n=std::max(1,int(s/frame));
        for(int i=1;i<=n;++i){Place(Arm(from+(to-from)*Ease(float(i)/n),reach,yawDeg));Step();}}
    void Hold(float s){for(int i=0;i<int(s/frame);++i)Step();}
    void Step(){in.time+=frame;out=g.Step(in);if(out.cast)++casts;if(out.dropped){++drops;lastDrop=out.dropped;}}
    // Raise the straight arm to 45 degrees, pause, bring it down to level, pause.
    void Call(float down=.35f,float to=0,float reach=.6f,float from=45){in.grip=1;Move(Arm(from,reach),.4f);Hold(.15f);Sweep(from,to,down,reach);Hold(.2f);}
};
int main(){
    for(float h:{-2.f,0.f,2.f}){Sim s(h);s.Call();Check(s.casts==1,"straight arm from 45 degrees down to the shoulder casts, at any heading");}
    {Sim s;s.Call(.2f);Check(s.casts==1,"a quick call casts");}
    {Sim s;s.Call(.6f);Check(s.casts==1,"a deliberate call casts");}
    {Sim s;s.Call(1.2f);Check(s.casts==0&&s.lastDrop.find("slow")!=std::string::npos,"a slow lowering is not a call");}
    {Sim s;s.Call();s.Hold(1);Check(s.casts==1,"an arm held at the shoulder never repeats");s.Call();Check(s.casts==2,"raising it again loads the next");}
    {Sim s;s.Call(.35f,-75);Check(s.casts==0&&s.lastDrop.find("chop")!=std::string::npos,"a straight arm carried down past -65 degrees is a chop");}
    {Sim s;s.Call(.35f,-8);Check(s.casts==1,"stopping a little below level still counts");}
    {Sim s;s.Call(.35f,-28);Check(s.casts==1,"the headset test's overshoot (stopping near -25 degrees) counts");}
    {Sim s;s.Call(.25f,-45);Check(s.casts==1,"the owner's natural stop (-36..-47 degrees, 2026-11-01) counts");}
    {Sim s;s.Call(.3f,-53);Check(s.casts==1,"a stop a little past -50 still counts");}
    // The fist opening ends the call: after a fast sweep far enough down it casts, before that it drops.
    {Sim s;s.in.grip=1;s.Move(Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,-40,.3f);s.in.grip=0;s.Step();s.Hold(.2f);
     Check(s.casts==1,"opening the fist at -40 after a fast sweep casts (the owner's 29 dropped calls)");}
    {Sim s;s.in.grip=1;s.Move(Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,-5,.3f);s.in.grip=0;s.Step();s.Hold(.2f);
     Check(s.casts==1,"opening the fist at level casts once, not twice");}
    {Sim s;s.in.grip=1;s.Move(Arm(40,.6f),.4f);s.Hold(.15f);s.Sweep(40,22,.1f);s.in.grip=0;s.Step();
     Check(s.casts==0&&s.lastDrop=="fist opened","opening the fist early in the sweep (18 degrees down, under 22) drops the call");}
    {Sim s;s.in.grip=1;s.Move(Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,-30,2.5f);s.in.grip=0;s.Step();
     Check(s.casts==0,"opening the fist after a slow lowering does not cast");}
    {Sim s;s.in.grip=1;s.Move(Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,-80,.35f);s.in.grip=0;s.Step();
     Check(s.casts==0,"opening the fist after a chop past -65 does not cast");}
    {Sim s;s.Call(.35f,30);Check(s.casts==0,"stopping above the shoulder does not cast");}
    {Sim s;s.in.grip=0;s.Move(Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,0,.35f);s.Hold(.2f);Check(s.casts==0,"an open hand is not a fist: no cast");}
    {Sim s;s.in.busy=true;s.Call();Check(s.casts==0,"a busy hand never calls");}
    {Sim s;s.in.grip=1;s.Move(Arm(45,.6f),.4f);s.Hold(.15f);s.Move({.2f,-.45f,-.25f},.3f);s.Hold(.2f);
     Check(s.casts==0,"bending the elbow and pulling the fist in (a chop's follow-through) does not cast");}
    {Sim s;s.in.grip=1;s.Move({.2f,-.3f,-.2f},.3f);s.Hold(.1f);s.Move({.2f,.2f,-.25f},.2f);s.Hold(.2f);s.Move({.2f,-.3f,-.2f},.3f);s.Hold(.2f);
     Check(s.casts==0,"the hammer pump (fist straight up beside the head) and back down never calls");}
    {Sim s;s.in.grip=1;s.Move({.2f,-.25f,-.15f},.3f);s.Hold(.1f);s.Move({.15f,-.2f,-.62f},.15f);s.Hold(.3f);
     Check(s.casts==0,"the Charge punch never calls");}
    {Sim s;s.Call(.35f,0,.38f);Check(s.casts==0,"a half-bent arm at 45 degrees never loads");}
    {Sim s;s.in.grip=1;s.Move(Arm(45,.6f,80),.4f);s.Hold(.15f);s.Sweep(45,0,.35f,.6f,80);s.Hold(.2f);Check(s.casts==0,"an arm out to the side does not call");}
    {Sim s;s.in.grip=1;s.Move(Arm(80,.6f),.4f);s.Hold(.15f);s.Sweep(80,0,.4f);s.Hold(.2f);Check(s.casts==1,"rev3: from higher up, the straight arm passing the raised pose loads and calls");}
    {Sim s;s.in.grip=1;s.Move(Arm(-10,.6f),.3f);s.Sweep(-10,45,.25f);s.Sweep(45,-5,.3f);s.Hold(.2f);
     Check(s.casts==1,"rev3: a fluid up-and-down with no rest at the top calls");}
    {Sim s;s.in.grip=1;s.Move(Arm(-10,.6f),.3f);s.Sweep(-10,40,.2f);s.Sweep(40,-15,.22f);s.Hold(.15f);
     Check(s.casts==1,"rev3: a quick flick up and down calls");}
    {Sim tall(0,2.1f);tall.Call(.35f,0,.78f);Check(tall.casts==1,"a tall player's straight arm casts");
     Sim tallBent(0,2.1f);tallBent.Call(.35f,0,.55f);Check(tallBent.casts==0,"a tall player's half-bent arm does not");
     Sim small(0,1.3f);small.Call(.35f,0,.47f);Check(small.casts==1,"a small player's straight arm casts");}
    {Sim s;s.in.grip=1;s.Move(Arm(45,.6f),.4f);s.Hold(.15f);s.Sweep(45,20,.15f);s.in.session=6;s.Sweep(20,0,.2f);s.Hold(.2f);
     Check(s.casts==0,"a new session forgets a call in progress");}
    std::puts("heavens gesture tests passed");
}
