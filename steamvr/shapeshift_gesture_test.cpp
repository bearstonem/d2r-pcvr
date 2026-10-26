#include "shapeshift_gesture.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
// Body frame keyframes (yaw-local: +X right, +Y up, -Z forward) relative to
// the standing head. Each hand is {right, left}. The simulation walks the
// controllers between keyframes at 50 Hz, so velocities come from motion.
struct Sim {
    shapeshift::Engine g;shapeshift::Input in;shapeshift::Output out;
    math::Quat yaw{0,0,0,1};math::Vec standing{0,1.7f,0};
    int casts=0;shapeshift::Form last=shapeshift::Form::None;
    Sim(float heading=0){yaw=math::Yaw(heading);in.time=1;in.session=7;in.epoch=1;in.active=true;in.tracked[0]=in.tracked[1]=true;Pose({0,0,0},0,{{.25f,-.4f,-.3f},{-.25f,-.4f,-.3f}});}
    void Pose(math::Vec headOffset,float pitchDeg,math::Vec hands[2]) {
        in.head=standing+math::Rotate(yaw,headOffset);
        const float p=pitchDeg*3.14159265f/180;
        in.headRotation=math::Multiply(yaw,{-std::sin(p*.5f),0,0,std::cos(p*.5f)}); // positive pitch looks down
        for(int h=0;h<2;++h)in.hand[h]=in.head+math::Rotate(yaw,hands[h]);
    }
    void Pose(math::Vec headOffset,float pitchDeg,std::initializer_list<math::Vec> hands){math::Vec h[2]{*hands.begin(),*(hands.begin()+1)};Pose(headOffset,pitchDeg,h);}
    // Move from the current pose to the target over `seconds`.
    void Move(math::Vec headOffset,float pitchDeg,std::initializer_list<math::Vec> hands,float seconds) {
        const math::Vec head0=in.head,hand0[2]{in.hand[0],in.hand[1]};const auto rot0=in.headRotation;
        Pose(headOffset,pitchDeg,hands);const math::Vec head1=in.head,hand1[2]{in.hand[0],in.hand[1]};const auto rot1=in.headRotation;
        const int steps=std::max(1,int(seconds/.02f));
        for(int i=1;i<=steps;++i) {
            const float t=float(i)/steps;
            in.head=head0+(head1-head0)*t;in.headRotation=t<.5f?rot0:rot1;
            for(int h=0;h<2;++h)in.hand[h]=hand0[h]+(hand1[h]-hand0[h])*t;
            Step();
        }
    }
    void Hold(float seconds){for(int i=0;i<int(seconds/.02f);++i)Step();}
    void Step(){in.time+=.02;out=g.Step(in);if(out.cast!=shapeshift::Form::None){++casts;last=out.cast;}}
    void Werewolf(float flingSeconds=.25f) {
        Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);   // cross arms: right hand on the left side
        Hold(.2f);
        Move({0,-.2f,.18f},45,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f); // bow forward over them
        Move({0,0,0},0,{{.55f,.35f,-.25f},{-.55f,.35f,-.25f}},flingSeconds); // wrench up, fling open
        Hold(.1f);
    }
    void Werebear(float slamSeconds=.2f) {
        Move({0,0,0},0,{{.15f,.25f,-.2f},{-.15f,.25f,-.2f}},.4f);       // hands above the head
        Hold(.35f);
        Move({0,-.1f,.1f},20,{{.15f,-.5f,-.3f},{-.15f,-.5f,-.3f}},slamSeconds); // slam down
        Hold(.1f);
    }
};
int main(){
    for(float heading:{-2.f,0.f,2.f}) {
        Sim s(heading);s.Werewolf();
        Check(s.casts==1&&s.last==shapeshift::Form::Werewolf,"crossed arms, bow and wrench open casts Werewolf at any heading");
        Sim b(heading);b.Werebear();
        Check(b.casts==1&&b.last==shapeshift::Form::Werebear,"hands high then a slam casts Werebear at any heading");
    }
    {Sim s;s.Werewolf();s.Werewolf();Check(s.casts==1,"a second performance inside the cooldown is ignored");
     s.Hold(1.5f);s.Werewolf();Check(s.casts==2&&s.last==shapeshift::Form::Werewolf,"after the cooldown the gesture casts again, which reverts the form");}
    {Sim s;s.Werewolf(1.2f);Check(s.casts==0,"a slow, lazy opening is not a wrench");}
    for(float hold:{0.f,.5f,1.2f}) {
        // Deep bow: the head drops 35 cm and comes forward past the folded
        // arms, which end level with or above it, then a pause before the wrench.
        Sim s;s.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Hold(.2f);
        s.Move({0,-.35f,.30f},70,{{-.12f,.03f,.10f},{.12f,.01f,.12f}},.5f);
        s.Hold(hold);
        Check(s.casts==0&&s.out.stance==shapeshift::Form::Werewolf,"a deep bow keeps the wolf stance while the arms stay folded");
        s.Move({0,0,0},0,{{.55f,.35f,-.25f},{-.55f,.35f,-.25f}},.25f);s.Hold(.1f);
        Check(s.casts==1&&s.last==shapeshift::Form::Werewolf,"a deep bow, held a moment, then the wrench casts Werewolf");
    }
    {Sim s;s.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Hold(.2f);
     s.Move({0,-.2f,.18f},45,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);
     s.Move({0,-.2f,.18f},45,{{.30f,-.25f,-.22f},{-.30f,-.27f,-.20f}},.3f);s.Hold(.8f);
     Check(s.casts==0&&s.out.stance==shapeshift::Form::None,"uncrossing while still bowed starts the wrench window, which then expires");}
    {// Bow, then open the arms too slowly: the performance is dropped with a reason.
     Sim r;bool reason=false;
     r.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);r.Hold(.2f);
     r.Move({0,-.2f,.18f},45,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);
     const math::Vec h0[2]{r.in.hand[0],r.in.hand[1]};
     for(int i=1;i<=60;++i){
         r.in.time+=.02;
         for(int k=0;k<2;++k)r.in.hand[k]=h0[k]+math::Rotate(r.yaw,{k?-.004f*i:.004f*i,.004f*i,0});
         r.out=r.g.Step(r.in);
         if(r.out.dropped){reason=true;Check(std::string(r.out.stage)=="fling","the drop names the fling stage");}
     }
     Check(reason&&r.casts==0,"an abandoned performance reports why");}
    {Sim s;s.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Hold(.2f);
     s.Move({0,0,0},0,{{.55f,.35f,-.25f},{-.55f,.35f,-.25f}},.25f);s.Hold(.1f);
     Check(s.casts==0,"flinging open without bowing first does not cast");}
    {Sim s;s.Move({0,0,0},0,{{.12f,-.25f,-.22f},{-.12f,-.27f,-.20f}},.4f);s.Hold(.2f);
     s.Move({0,-.2f,.18f},45,{{.12f,-.25f,-.22f},{-.12f,-.27f,-.20f}},.4f);
     s.Move({0,0,0},0,{{.55f,.35f,-.25f},{-.55f,.35f,-.25f}},.25f);s.Hold(.1f);
     Check(s.casts==0,"uncrossed hands at the chest never arm the wolf");}
    {Sim s;s.Move({0,-.2f,.18f},45,{{.2f,-.5f,-.3f},{-.2f,-.5f,-.3f}},.4f);s.Move({0,0,0},0,{{.25f,-.4f,-.3f},{-.25f,-.4f,-.3f}},.2f);
     Check(s.casts==0,"stooping for loot and standing back up does not cast");}
    {Sim s;s.Move({0,0,0},0,{{.15f,.25f,-.2f},{-.15f,.25f,-.2f}},.4f);s.Hold(.35f);
     s.Move({0,0,0},0,{{.15f,-.2f,-.3f},{-.15f,-.2f,-.3f}},.6f);s.Hold(.1f);
     Check(s.casts==0,"lowering raised hands gently is not a slam");}
    {Sim s;s.Move({0,0,0},0,{{.15f,.25f,-.2f},{-.15f,.25f,-.2f}},.4f);
     s.Move({0,-.1f,.1f},20,{{.15f,-.5f,-.3f},{-.15f,-.5f,-.3f}},.2f);s.Hold(.1f);
     Check(s.casts==0,"a slam without first holding the hands high (an overhead swing) does not cast");}
    {Sim s;s.in.busy[1]=true;s.Werewolf();Check(s.casts==0,"a busy hand (trigger, bottle, bow) blocks the gesture");}
    {Sim s;s.in.active=false;s.Werewolf();Check(s.casts==0,"menus, wheel and blocks suppress the gesture");}
    {Sim s;s.in.tracked[0]=false;s.Werebear();Check(s.casts==0,"an untracked controller cannot perform");}
    {Sim s;s.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Hold(.2f);Check(s.out.stance==shapeshift::Form::Werewolf,"crossed arms report the wolf stance");
     s.in.epoch=2;s.Step();Check(s.out.stance==shapeshift::Form::None,"an epoch change drops the stance");
     s.Move({0,-.2f,.18f},45,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Move({0,0,0},0,{{.55f,.35f,-.25f},{-.55f,.35f,-.25f}},.25f);s.Hold(.1f);
     Check(s.casts==1,"the stance re-arms from the still-crossed arms and the performance completes");}
    {Sim s;s.Move({0,0,0},0,{{-.12f,-.25f,-.22f},{.12f,-.27f,-.20f}},.4f);s.Hold(.2f);s.in.time+=1;s.Step();
     Check(s.out.stance==shapeshift::Form::None,"a frame gap resets the stance");}
    {Sim s;s.in.head.x=NAN;s.Step();Check(s.out.stance==shapeshift::Form::None&&s.out.cast==shapeshift::Form::None,"non-finite poses are ignored");}
    std::puts("shapeshift gesture ok");
}
