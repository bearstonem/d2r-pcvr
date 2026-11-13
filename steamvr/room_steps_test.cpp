#include "room_steps.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
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
// transport.cpp View's follow, as written there: each head view the hero's move since the last (world units)
// becomes an origin move (Shift), taken as far as Follow allows - or whole, as before 2026-11-12 (whole=true),
// to show the detach it made; a jump over MaxHeroStep is not followed. Then an input frame, and a watchdog
// recentre moves the origin as transport does. The camera is hero + Rotate(base, eye - origin) * upm.
struct Room {
    Sim s;Quat base=math::Yaw(.7f);float upm=4.27f;Vec hero{9941.6f,0,10111.5f};bool whole=false;
    float most=0;int recentres=0;Output recentred{};   // the farthest the camera got off the hero; the watchdog's recentres
    Vec Camera()const{return hero+math::Rotate(base,s.in.eye-s.in.origin)*upm;}
    float Apart()const{const Vec d=Camera()-hero;return std::sqrt(d.x*d.x+d.z*d.z)/upm;}   // the camera off the hero, m
    Vec World(Vec tracking)const{return math::Rotate(base,tracking)*upm;}                  // a move in the room, in the world
    void Moves(Vec d) {   // the hero moved d (world units) since the last head view
        hero=hero+d;
        const float metres=std::sqrt(d.x*d.x+d.z*d.z)/upm;
        if(metres>MaxHeroStep)s.chase.Jumped();
        else if(s.out.follow&&metres>0) {
            const Vec wanted=Shift(d,base,upm),shift=whole?wanted:s.chase.Follow(wanted);
            s.in.origin=s.in.origin+shift;s.chase.Shifted(shift,metres);
        }
        s.Step();
        if(s.out.recentre){s.in.origin=s.in.origin+s.out.shift;++recentres;recentred=s.out;}
        most=std::max(most,Apart());
    }
    // Leap's flight as logged 2026-11-12: ~41 world units (25 cells, 9.7 m) over 0.75 s, one head view a 90 Hz frame.
    void Flies(Vec metresInRoom,double seconds=.75){const int n=int(seconds*90);const Vec d=World(metresInRoom)*(1.f/n);for(int i=0;i<n;++i)Moves(d);}
    void Still(double seconds){for(double t=0;t<seconds;t+=1/90.)Moves({});}
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
    {   // A wall: pushed BlockedSeconds while the hero does not walk stops the push; a step elsewhere tries again.
        Sim s;s.Move({0,0,-.5f});s.Run(HoldSeconds+.05);Check(s.out.push,"walking");
        s.Run(BlockedSeconds+.05);Check(!s.out.push&&s.out.blocked,"the hero did not walk for BlockedSeconds: blocked, not pushed");
        s.Run(1);Check(!s.out.push&&s.out.blocked,"still blocked while you stay");
        s.Move({.3f,0,0});s.Run(HoldSeconds+.05);Check(s.out.push&&!s.out.blocked,"a step elsewhere walks again");
        Sim c;c.Move({0,0,-.5f});c.Run(HoldSeconds+.05);
        for(int i=0;i<90;++i){c.in.origin=c.in.origin+Vec{0,0,-.004f};c.chase.Shifted({0,0,-.004f});c.Step();}   // the hero catching up
        Check(!c.out.blocked,"a hero coming closer is not blocked");
    }
    {   // Pauses: a grip, a trigger, the bow, a cast, a menu or the real stick stop the push and the follow at once.
        bool Input::*gates[]={&Input::grip,&Input::trigger,&Input::bow,&Input::cast,&Input::menu,&Input::stick,&Input::carried};
        const Pause names[]={Pause::Grip,Pause::Trigger,Pause::Bow,Pause::Cast,Pause::Menu,Pause::Stick,Pause::Carried};
        for(int i=0;i<7;++i) {
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
            const Vec d=world*.1f;hero=hero+d;const Vec wanted=Shift(d,base,upm),shift=s.chase.Follow(wanted);
            Check(Len(shift-wanted)<1e-5f,"a walk toward you is followed whole");
            s.in.origin=s.in.origin+shift;s.chase.Shifted(shift);s.Step();
        }
        const Vec after=camera(s.in.origin);
        Check(Len(after-before)<1e-3f,"the camera stays where the head is");
        Check(s.out.offset<.01f&&!s.out.push&&s.out.follow,"the step is paid off and the push ends; the origin still follows");
        Check(Len(Shift({1,5,1},base,0))==0&&Shift({0,9,0},base,upm).y==0,"no shift without a scale, and never up or down");
    }
    {   // A fast hero coasts: the push ends early by what he will coast, at most MaxCoastMetres.
        Sim s;s.Move({0,0,-.6f});s.Run(HoldSeconds+.05);Check(s.out.push,"walking");
        bool stoppedEarly=false;float at=0;
        for(int i=0;i<90&&s.out.push;++i){s.in.origin=s.in.origin+Vec{0,0,-.045f};s.chase.Shifted({0,0,-.045f});s.Step();   // 4 m/s
            if(!s.out.push){stoppedEarly=s.out.offset>StopMetres+.02f;at=s.out.offset;}}
        Check(stoppedEarly&&at<=StopMetres+MaxCoastMetres+.05f,"at 4 m/s the push ends before StopMetres, by no more than the coast");
    }
    {   // The room turned about the head (room_scale.h): the owed step turns with the room; standing at the centre, a turn owes none.
        const auto turnAboutHead=[](Vec origin,Vec head,Quat was,Quat now){
            const Vec d=head-origin;const Vec t=math::Rotate(math::Multiply(math::Conjugate(now),was),Vec{d.x,0,d.z});
            return Vec{head.x-t.x,origin.y,head.z-t.z};};
        const Quat was=math::Yaw(.4f),now=math::Yaw(.4f-1.5707963f);const Quat rel=math::Multiply(math::Conjugate(now),was);
        Sim s;s.Step();const Vec o=turnAboutHead(s.in.origin,s.in.eye,was,now);
        s.chase.Turned(o,rel);s.in.origin=o;s.Run(.5);
        // The turn pivots at the eyes and the neck sits behind them: it swings a little round the hero
        // (under 2 x 8 cm), never as far as a step.
        Check(s.out.offset<2*EyeToNeck.z+.01f&&s.out.offset<StartMetres&&!s.out.push,"at the centre a turn is not a step");
        Sim w;w.Move({.4f,0,0});w.Step();const float owed=w.out.offset;
        const Vec o2=turnAboutHead(w.in.origin,w.in.eye,was,now);w.chase.Turned(o2,rel);w.in.origin=o2;w.Step();
        Check(std::fabs(w.out.offset-owed)<2*EyeToNeck.z+.01f&&w.out.offset>StartMetres,"a step owed stays owed after a turn (give or take the neck's swing)");
    }
    // 2026-11-12, a player: Leap "disconnects the player from the in game character causing a sort of broken 3rd
    // person view ... I ended up outside the walls while my actual character was inside", until a save and reload.
    {   // The detach, as it was: a step owed behind the hero, the follow on, then a Leap carries him 25 cells ahead.
        // Taken whole, every head view's 0.1-0.2 m of flight is under MaxHeroStep, so the origin flew with him and
        // the camera stayed where he took off - and nothing but a reload moved the origin back.
        Room r;r.whole=true;r.s.Move({0,0,.3f});r.s.Run(HoldSeconds+.05);Check(r.s.out.push&&r.s.out.follow,"walking after the step");
        Check(9.7f/67<MaxHeroStep,"Leap's flight is no jump by distance: 0.14 m a head view");
        r.Flies({0,0,-9.7f});
        Check(r.Apart()>9.5f&&r.s.out.offset>9.5f,"the old follow: Leap's flight followed whole leaves the camera ~10 m off the hero");
        // The watchdog puts even that back: he cannot walk the 10 m to you (here not at all: a wall), so after FarSeconds the room is put under you.
        r.Still(FarSeconds+.1);
        Check(r.recentres==1&&r.Apart()<.02f&&r.s.out.offset<.02f,"the watchdog: the view back on the hero");
        r.Still(1);Check(r.recentres==1&&!r.s.out.push&&!r.s.out.blocked,"once, and nothing owed after it");
    }
    {   // Now: Follow takes only what pays the step off, so the same flight leaves the camera on the hero (the step
        // owed, 0.3 m, and no more) - as for Whirlwind or Charge, which nothing else pauses.
        Room r;r.s.Move({0,0,.3f});r.s.Run(HoldSeconds+.05);Check(r.s.out.push&&r.s.out.follow,"walking after the step");
        r.Flies({0,0,-9.7f});
        Check(r.most<.31f&&Near(r.Apart(),.3f),"the flight moves the camera with the hero: never farther off than the step owed");
        Check(r.s.out.push&&!r.s.out.blocked&&Near(r.s.out.offset,.3f),"the step is still owed and walked: a hero the game carries is no wall");
    }
    {   // And a Leap pauses the follow outright (Pause::Carried: its cast, take-off and flight), resuming on landing.
        Room r;r.s.Move({0,0,.3f});r.s.Run(HoldSeconds+.05);Check(r.s.out.push,"walking after the step");
        r.s.in.carried=true;r.Flies({0,0,-9.7f});
        Check(r.s.out.pause==Pause::Carried&&!r.s.out.push&&!r.s.out.follow&&r.most<.31f,"in the air: no push, no follow, the camera with him");
        Check(std::string(PauseName(Pause::Carried))!="?","the pause has a name for the log");
        r.s.in.carried=false;r.Still(HoldSeconds+.05);Check(r.s.out.push&&Near(r.s.out.offset,.3f),"landed: the step owed is walked again");
    }
    {   // A Leap toward your step pays it off, and no more: he flies in under you, and the camera goes on with him.
        Room r;r.s.Move({0,0,-.5f});r.s.Run(HoldSeconds+.05);Check(r.s.out.follow,"walking after the step ahead");
        r.Flies({0,0,-9.7f});
        Check(r.most<=.5f+.01f&&r.Apart()<.02f,"never farther off than the step owed; he came in under you");
    }
    {   // The owner's log, 17:42:41.905-.989: walking 0.23 m behind the hero (stick 0.07, -0.54), a Whirlwind carried him
        // 0.47 m ahead in 84 ms, and the origin followed it: "stopped 0.70 m from the hero". Now it stays 0.23 m.
        for(bool whole:{true,false}) {
            Room r;r.whole=whole;r.s.Move({0,0,.23f});r.s.Run(HoldSeconds+.05);Check(r.s.out.push&&Near(r.s.out.y,-Push),"walking back to the hero");
            r.Flies({0,0,-.47f},.084);
            if(whole)Check(Near(r.s.out.offset,.70f,.03f),"as logged: the origin followed him away, 0.23 -> 0.70 m");
            else Check(Near(r.s.out.offset,.23f,.01f)&&Near(r.Apart(),.23f,.01f),"now: the 0.23 m owed, the camera went with him");
        }
    }
    {   // Follow never leaves you farther from the hero, whatever the move; a move that leaves you no farther is taken whole.
        uint32_t seed=12345;const auto rnd=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/float(1u<<24)*2-1;};
        for(int i=0;i<2000;++i) {
            Sim s;s.in.grip=true;const Vec o{rnd()*2,0,rnd()*2};s.in.eye=s.in.eye+o;s.Step();
            const Vec d{rnd()*.5f,rnd(),rnd()*.5f},f=s.chase.Follow(d);
            const float before=Horizontal(o),after=Horizontal(o-f),whole=Horizontal(o-Vec{d.x,0,d.z});
            Check(after<=before+1e-5f&&f.y==0,"the follow never adds to the step owed, and never moves up or down");
            if(whole<=before)Check(Near(f.x,d.x,1e-6f)&&Near(f.z,d.z,1e-6f),"a move leaving you no farther is followed whole");
            if(math::Dot(Vec{d.x,0,d.z},o)<=0)Check(Horizontal(f)<1e-6f||whole<=before,"a move away from you is not followed");
        }
        Chase none;Check(Horizontal(none.Follow({1,0,1}))==0,"before an anchor, nothing");
    }
    {   // The watchdog: FarMetres from the hero's spot for FarSeconds - a grip held while you walked 2 m, here - puts
        // the room back under you, once; not a moment before.
        Room r;r.s.in.grip=true;r.s.in.eye=r.s.in.eye+Vec{0,0,-2.f};r.Moves({});
        Check(r.s.out.pause==Pause::Grip&&Near(r.Apart(),2.f),"2 m off, paused");
        r.Still(FarSeconds-.05);Check(r.recentres==0&&Near(r.Apart(),2.f),"not before FarSeconds");
        r.Still(.1);Check(r.recentres==1&&r.Apart()<.02f&&r.s.out.offset<.02f&&Near(r.recentred.apart,2.f),"then the camera is on the hero again");
        Check(r.recentred.pause==Pause::Grip&&!r.recentred.blocked&&r.recentred.heldFor>=FarSeconds,"what it says: why and how long");
        r.Still(2);Check(r.recentres==1,"once");
        Room n;n.s.in.grip=true;n.s.in.eye=n.s.in.eye+Vec{0,0,-(FarMetres-.1f)};n.Still(5);
        Check(n.recentres==0&&Near(n.Apart(),FarMetres-.1f),"short of FarMetres it never acts");
        for(int k=0;k<2;++k) {
            Room m;(k?m.s.in.on:m.s.in.menu)=!k;m.s.in.eye=m.s.in.eye+Vec{0,0,-3.f};m.Still(FarSeconds*2);
            Check(m.recentres==0,k?"off: no watchdog (the room never followed)":"a menu open: left alone");
        }
    }
    {   // ... and a walk closing on it is let finish: 2 m owed (tracking came back there), the hero walks over at 1.2 m/s.
        Room r;r.s.in.eye=r.s.in.eye+Vec{0,0,-2.f};r.Moves({});
        const Vec walk=r.World({0,0,-1.2f/90});
        for(int i=0;i<300&&r.s.out.offset>.3f;++i)r.Moves(r.s.out.push?walk:Vec{});
        Check(r.recentres==0&&r.s.out.offset<=.3f&&r.most<=2.f+.01f,"caught up by his walk, no recentre");
    }
    {   // ... but a wall in his way (he cannot come closer: blocked) is no walk: put back under you after FarSeconds.
        Room r;r.s.in.eye=r.s.in.eye+Vec{0,0,-2.f};r.Moves({});
        r.Still(HoldSeconds+BlockedSeconds+.1);Check(r.s.out.blocked&&r.recentres==0,"blocked at 2 m");
        r.Still(FarSeconds);Check(r.recentres==1&&r.recentred.blocked&&r.Apart()<.02f,"then the view back on the hero");
    }
    {   // A jump (a teleport, waypoint, another area) leaves the wall the hero met behind: a step owed walks again.
        Sim s;s.Move({0,0,-.5f});s.Run(HoldSeconds+BlockedSeconds+.1);Check(s.out.blocked,"blocked");
        s.chase.Jumped();s.Step();Check(!s.out.blocked&&!s.out.push,"a jump: not blocked, the hold starts over");
        s.Run(HoldSeconds+.02);Check(s.out.push,"then it walks again");
    }
    std::puts("room-steps-test: ok");
}
