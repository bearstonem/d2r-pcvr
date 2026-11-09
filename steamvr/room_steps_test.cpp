#include "room_steps.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
using namespace framevr;using namespace framevr::roomsteps;using math::Vec;using math::Quat;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b,float e=.02f){return std::fabs(a-b)<=e;}
static Quat Pitch(float a){return {std::sin(a*.5f),0,0,std::cos(a*.5f)};}
// A standing player at the origin, facing yaw (radians, 0 = -z ahead), stepped at 90 Hz.
struct Sim {
    Chase chase;Input in;Output out;
    Sim(float yaw=0){in.on=in.tracked=true;in.eye={0,1.6f,0};in.head=math::Yaw(yaw);in.origin=in.eye;
        in.handTracked[0]=in.handTracked[1]=true;in.hand[0]={.25f,1.f,-.2f};in.hand[1]={-.25f,1.f,-.2f};Step();}
    const Output& Step(double seconds=1/90.){in.time+=seconds;out=chase.Step(in);return out;}
    void Run(double seconds){for(double t=0;t<seconds;t+=1/90.)Step();}
    // The whole body moves (eyes and hands together) at a walker's 0.8 m/s, frame by frame: a real step.
    void Move(Vec d){
        const int n=std::max(1,int(std::ceil(Len(d)/(.8f/90))));const Vec s=d*(1.f/n);
        for(int i=0;i<n;++i){in.eye=in.eye+s;in.hand[0]=in.hand[0]+s;in.hand[1]=in.hand[1]+s;Step();}
    }
};
int main(){
    {   // Turning the head or looking down in place is not a step.
        Sim s;const Vec neck=Neck(s.in.eye,s.in.head);
        for(float yaw:{.8f,1.6f,3.1f,-2.f}) {
            s.in.head=math::Yaw(yaw);s.in.eye=neck-math::Rotate(s.in.head,EyeToNeck);s.Step();
            Check(s.out.offset<.01f&&!s.out.push,"a head turned about the neck is not a step");
        }
        s.in.head=math::Multiply(math::Yaw(0),Pitch(-1.05f));s.in.eye=neck-math::Rotate(s.in.head,EyeToNeck);s.Run(.5);
        Check(s.out.offset<.01f&&!s.out.push,"looking down 60 degrees is not a step");
    }
    {   // A step just past StartMetres walks ahead after the hold; the stick is partly pushed, never a run.
        Sim s;s.Move({0,0,-(StartMetres+.05f)});s.Step();
        Check(Near(s.out.offset,StartMetres+.05f)&&!s.out.push,"the step is measured, nothing pushed before the hold");
        s.Run(HoldSeconds+.02);
        Check(s.out.push&&Near(s.out.x,0)&&Near(s.out.y,Push)&&s.out.follow,"after the hold: ahead, partly pushed, the origin follows");
        Check(Push>.316f&&Push<.95f,"past the game's dead zone, short of a run");
    }
    {   // The stick is in the view's frame: facing +x (yaw -90 degrees), a step to +x is ahead, one to -z is left.
        Sim s(-1.5707963f);s.Move({.5f,0,0});s.Run(.3);
        Check(s.out.push&&Near(s.out.x,0)&&Near(s.out.y,Push),"facing +x, a step to +x pushes ahead");
        Sim t(-1.5707963f);t.Move({0,0,-.5f});t.Run(.3);
        Check(t.out.push&&Near(t.out.x,-Push)&&Near(t.out.y,0),"facing +x, a step to -z pushes left");
        Sim u;u.Move({-.3f,0,.3f});u.Run(.3);
        Check(u.out.push&&Near(u.out.x,-Push*.7071f)&&Near(u.out.y,-Push*.7071f),"a step back and left pushes back and left");
    }
    {   // Inside StartMetres nothing walks; past it only after a steady hold; a lunge starts nothing.
        Sim s;s.Move({0,0,-(StartMetres-.02f)});s.Run(1);
        Check(!s.out.push&&!s.out.follow,"short of the start, nothing walks");
        Sim l;for(int i=0;i<30;++i){l.in.eye=l.in.eye+Vec{0,0,-.03f};l.Step();}   // the head lunges at 2.7 m/s
        Check(!l.out.push&&l.out.offset>StartMetres,"a lunge (the head fast) starts nothing");
        l.Run(HoldSeconds+.05);Check(l.out.push,"held there, it walks");
    }
    {   // Hysteresis and the tail: walking holds down to StopMetres, then the origin follows TailSeconds more.
        Sim s;s.Move({0,0,-.5f});s.Run(.3);Check(s.out.push,"walking");
        const float between=(StartMetres+StopMetres)*.5f;
        s.Move({0,0,.5f-between});s.Step();Check(s.out.push&&Near(s.out.offset,between),"still walking between stop and start");
        s.Move({0,0,between-StopMetres+.02f});s.Step();Check(!s.out.push&&s.out.follow,"stops inside StopMetres, the origin still follows");
        s.Run(TailSeconds+.02);Check(!s.out.push&&!s.out.follow,"the tail ends");
    }
    {   // A wall: walking without coming closer for BlockedSeconds stops the push; a step elsewhere tries again.
        Sim s;s.Move({0,0,-.5f});s.Run(HoldSeconds+.05);Check(s.out.push,"walking");
        s.Run(BlockedSeconds+.05);Check(!s.out.push&&s.out.blocked,"no closer for BlockedSeconds: blocked, not pushed");
        s.Run(1);Check(!s.out.push&&s.out.blocked,"still blocked while you stay");
        s.Move({.3f,0,0});s.Run(HoldSeconds+.05);Check(s.out.push&&!s.out.blocked,"a step elsewhere walks again");
        Sim c;c.Move({0,0,-.5f});c.Run(HoldSeconds+.05);
        for(int i=0;i<90;++i){c.in.origin=c.in.origin+Vec{0,0,-.004f};c.chase.Shifted({0,0,-.004f});c.Step();}   // the hero catching up
        Check(!c.out.blocked,"a hero coming closer is not blocked");
    }
    {   // Pauses: a grip, a trigger, the bow, a cast, a menu or the real stick stop the push and the follow at once.
        bool Input::*gates[]={&Input::grip,&Input::trigger,&Input::bow,&Input::cast,&Input::menu,&Input::stick};
        const Pause names[]={Pause::Grip,Pause::Trigger,Pause::Bow,Pause::Cast,Pause::Menu,Pause::Stick};
        for(int i=0;i<6;++i) {
            Sim s;s.Move({0,0,-.5f});s.Run(.3);Check(s.out.push,"walking before the pause");
            s.in.*gates[i]=true;s.Step();
            Check(!s.out.push&&!s.out.follow&&s.out.pause==names[i],"a pause ends the push and the follow");
            s.in.*gates[i]=false;s.Step();Check(!s.out.push,"let go, the hold starts over");
            s.Run(HoldSeconds+.02);Check(s.out.push,"then it walks again");
        }
        Sim off;off.in.on=false;off.Move({0,0,-.5f});off.Run(.5);Check(!off.out.push&&off.out.pause==Pause::Off,"off: nothing");
    }
    {   // A swing (a hand faster than SwingSpeed) pauses, and stays paused SwingSeconds after it.
        Sim s;s.Move({0,0,-.5f});s.Run(.3);Check(s.out.push,"walking");
        s.in.hand[0]=s.in.hand[0]+Vec{0,0,-.05f};s.Step();   // 4.5 m/s
        Check(s.out.pause==Pause::Swing&&!s.out.push,"a swing pauses");
        s.Run(SwingSeconds-.1);Check(s.out.pause==Pause::Swing,"still paused just after it");
        s.Run(.15+HoldSeconds);Check(s.out.pause==Pause::None&&s.out.push,"then it walks again");
    }
    {   // The origin moved without us (F11, Calibrate body, a world load): re-anchored where the head is.
        Sim s;s.Move({0,0,-.5f});s.Run(.3);Check(s.out.push,"walking");
        s.in.origin=s.in.eye;s.Step();Check(s.out.offset<.01f&&!s.out.push&&!s.out.follow,"a recenter re-anchors: no step owed");
    }
    {   // Lost tracking keeps the step owed; it walks once the head is back.
        Sim s;s.in.tracked=false;s.Move({0,0,-.5f});s.Run(.3);Check(s.out.pause==Pause::Untracked&&!s.out.push,"untracked: paused");
        s.in.tracked=true;s.Run(.3);Check(s.out.push&&Near(s.out.offset,.5f),"tracked again: the step is still owed");
    }
    {   // The origin following the hero keeps the camera still and pays the step off.
        const Quat base=math::Yaw(.7f);const float upm=4.f;
        Vec hero{100,0,200};Sim s;s.Move({.3f,0,-.4f});s.Run(.3);Check(s.out.push,"walking");
        const auto camera=[&](Vec origin){return hero+math::Rotate(base,s.in.eye-origin)*upm;};
        const Vec before=camera(s.in.origin);
        const Vec world=math::Rotate(base,Vec{.3f,0,-.4f})*upm;   // the hero walks the whole step
        for(int i=1;i<=10;++i) {
            const Vec d=world*.1f;hero=hero+d;const Vec shift=Shift(d,base,upm);
            s.in.origin=s.in.origin+shift;s.chase.Shifted(shift);s.Step();
        }
        const Vec after=camera(s.in.origin);
        Check(Len(after-before)<1e-3f,"the camera stays where the head is");
        Check(s.out.offset<.01f&&!s.out.push&&s.out.follow,"the step is paid off and the push ends; the origin still follows");
        Check(Len(Shift({1,5,1},base,0))==0&&Shift({0,9,0},base,upm).y==0,"no shift without a scale, and never up or down");
    }
    std::puts("room-steps-test: ok");
}
