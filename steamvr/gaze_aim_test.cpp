// Eye-gaze aiming (gaze_aim.h): which samples count, the gaze into the world, the angle to a
// monster's body, the pick's hysteresis and the blink / tracking-loss fallback to the head.
#include "gaze_aim.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace framevr::gazeaim;
static int checks=0;
static void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
static bool Near(float a,float b,float e=1e-3f){return std::fabs(a-b)<=e;}
static Vec Unit(Vec v){const float l=std::sqrt(Dot(v,v));return {v.x/l,v.y/l,v.z/l};}
int main(){
    // ---- samples ----
    constexpr int64_t display=2'000'000'000;
    Check(Usable(true,display,display-50'000'000)&&Usable(true,display,display+20'000'000),"a tracked, fresh sample aims");
    Check(Usable(true,display,0),"an unstamped tracked sample aims (foveation alone wants the stamp)");
    Check(!Usable(false,display,display)&&!Usable(false,display,0),"untracked never aims (a blink, tracking lost)");
    Check(!Usable(true,display,display-100'000'001)&&!Usable(true,display,display+20'000'001),"a stale stamped sample does not aim");

    // ---- the gaze into the world through the left eye's camera ----
    {
        const float identity[9]={1,0,0, 0,1,0, 0,0,1};   // right, up, back
        Vec w;
        Check(ToWorld(identity,{0,0,-1},w)&&Near(w.z,-1)&&Near(w.x,0),"straight ahead is the camera's forward (-back)");
        // the camera turned 90 degrees left: right = -z, back = +x, so ahead is -x
        const float left[9]={0,0,-1, 0,1,0, 1,0,0};
        Check(ToWorld(left,{0,0,-1},w)&&Near(w.x,-1)&&Near(w.z,0),"the head's turn carries the gaze");
        Check(ToWorld(left,Unit({.3f,0,-1}),w)&&w.z<0&&w.x<0,"eyes to the right of the turned head look right of its forward");
        Check(ToWorld(identity,Unit({0,-.5f,-1}),w)&&w.y<0,"eyes looking down stay down");
        const float scaled[9]={2,0,0, 0,2,0, 0,0,2};
        Check(ToWorld(scaled,{0,0,-1},w)&&Near(std::sqrt(Dot(w,w)),1),"a scaled camera still gives a unit direction");
        const float broken[9]={};
        Check(!ToWorld(broken,{0,0,-1},w),"no camera: no direction");
    }

    // ---- degrees off a monster's body ----
    {
        const Vec eye{0,1.6f,0};
        const float low=BodyLowM,high=BodyHighM,mid=.5f*(low+high);
        // a monster 4 m ahead (-z): looked at anywhere on its body, well inside the catch; at its middle, 0
        Check(DegreesOff(eye,Unit({0,mid-eye.y,-4}),{0,0,-4},low,high)<.05f,"looking at a monster's middle: 0");
        for(float h:{.3f,.8f,1.4f}){
            const Vec dir=Unit({0,h-eye.y,-4});
            Check(DegreesOff(eye,dir,{0,0,-4},low,high)<CatchDeg/2,"looking at a monster's feet, middle or head is looking at it");
        }
        Check(DegreesOff(eye,Unit({0,1.4f-eye.y,-2}),{0,0,-2},low,high)<CatchDeg,"the head of one 2 m away is caught");
        Check(DegreesOff(eye,Unit({0,-1.6f,-4}),{0,0,-4},low,high)>2.f,"its floor spot below the body is not on it");
        // 5 degrees to its side: 5 degrees
        const float side=5.f/57.2957795f;
        const Vec turned=Unit({std::sin(side),(mid-eye.y)/4.f,-std::cos(side)});
        Check(Near(DegreesOff(eye,turned,{0,0,-4},low,high),5.f,.2f),"beside it: the side-to-side angle alone");
        Check(DegreesOff(eye,{0,0,1},{0,0,-4},low,high)>170.f,"behind you: far off");
        // two in a line: the gaze tells the near one from the far one by how low it looks
        const Vec nearM{0,0,-2},farM{0,0,-12};
        const Vec atFar=Unit({0,(mid-eye.y),-12});
        Check(DegreesOff(eye,atFar,farM,low,high)<.05f&&DegreesOff(eye,atFar,nearM,low,high)>1.f+SwitchDeg,
              "looking past the near monster's head at the far one's middle: the far one, clearly");
        const Vec atNear=Unit({0,(.6f-eye.y),-2});
        Check(DegreesOff(eye,atNear,nearM,low,high)<CatchDeg/2&&DegreesOff(eye,atNear,farM,low,high)>10.f,"looking at the near one: the near one");
        Check(DegreesOff(eye,{0,-1,0},{0,0,0},low,high)<1.f,"one right below the eyes, looked straight down at");
        Check(DegreesOff(eye,{0,0,0},{0,0,-4},low,high)==180.f,"no gaze direction: no angle");
        // world units: the same scene at 2.5 units a metre reads the same
        const float upm=2.5f;
        Check(Near(DegreesOff({0,1.6f*upm,0},atFar,{0,0,-12*upm},low*upm,high*upm),DegreesOff(eye,atFar,farM,low,high),1e-2f),"any world scale");
    }

    // ---- the pick: catch, keep, switch, dwell ----
    {
        Picker p;uint64_t t=1000;
        Candidate one[]={{7,3.f}};
        Check(p.Step(one,1,0,t)==0,"a first look waits for the dwell");
        Check(p.Step(one,1,0,t+DwellMs-1)==0,"still waiting just before it");
        Check(p.Step(one,1,0,t+DwellMs)==7,"then takes the monster looked at");
        Candidate far[]={{7,CatchDeg+1}};
        Check(p.Step(far,1,0,t)==0&&p.Step(far,1,0,t+500)==0,"outside the catch cone: nothing");
        // kept within KeepDeg against one only a little nearer: two close monsters do not trade the pick
        Picker k;
        Candidate close[]={{7,4.f},{8,3.f}};
        for(uint64_t ms=0;ms<1000;ms+=10)Check(k.Step(close,2,7,t+ms)==7,"a neighbour 1 degree nearer never takes it");
        Candidate jitter[]={{7,3.f},{8,4.f}};
        for(uint64_t ms=0;ms<1000;ms+=20){
            Check(k.Step(ms%40?close:jitter,2,7,t+ms)==7,"jitter between two close monsters keeps the first");
        }
        // a clearly nearer one takes over after the dwell
        Candidate clear[]={{7,8.f},{8,1.f}};
        Check(k.Step(clear,2,7,t)==7,"a clearly nearer one waits for the dwell");
        Check(k.Step(clear,2,7,t+DwellMs)==8,"then takes over");
        // a glance across: the monster crossed for less than the dwell is never taken
        Picker g;uint32_t pick=7;
        Candidate start[]={{7,1.f},{8,20.f},{9,40.f}},middle[]={{7,20.f},{8,1.f},{9,20.f}},end[]={{7,40.f},{8,20.f},{9,1.f}};
        pick=g.Step(start,3,pick,t);Check(pick==7,"looking at the first");
        pick=g.Step(middle,3,pick,t+10);pick=g.Step(middle,3,pick,t+40);Check(pick==7,"the glance crosses the second for 30 ms: still the first");
        pick=g.Step(end,3,pick,t+50);Check(pick==7,"lands on the third: the dwell starts over");
        pick=g.Step(end,3,pick,t+50+DwellMs);Check(pick==9,"and the third is taken, never the second");
        // left: the pick goes once the eyes stay away
        Candidate away[]={{9,KeepDeg+2}};
        Check(g.Step(away,1,9,t+1000)==9&&g.Step(away,1,9,t+1000+DwellMs)==0,"looking away lets go after the dwell");
        Candidate kept[]={{9,KeepDeg-1}};
        Check(g.Step(kept,1,9,t+2000)==9&&g.Step(kept,1,9,t+3000)==9,"within KeepDeg (past the catch) it stays");
        // gone: dead or out of sight gives way at once
        Candidate other[]={{4,2.f}};
        Check(g.Step(other,1,9,t+4000)==4,"the pick died: the one looked at, at once");
        Candidate none[1]={};
        Check(g.Step(none,0,9,t+5000)==0,"the pick died with none looked at: nothing");
        Candidate bad[]={{3,NAN}};
        Check(g.Step(bad,1,0,t+6000)==0&&g.Step(bad,1,0,t+7000)==0,"no angle: never picked");
    }

    // ---- the fallback: gaze, a blink held, tracking lost to the head, regained ----
    {
        Fallback f;uint64_t t=10'000;
        Check(f.Step(false,true,t)==Ray::Head&&!f.On(),"not wanted (option Off, no tracker, nothing aimed): the head");
        Check(f.Step(true,true,t)==Ray::Head,"samples arriving: the head until they have come a while");
        Check(f.Step(true,true,t+RegainMs-1)==Ray::Head,"still the head just before");
        Check(f.Step(true,true,t+RegainMs)==Ray::Gaze&&f.On(),"then the gaze");
        t+=RegainMs;
        // a blink: held, no flicker to the head
        for(uint64_t ms=1;ms<BlinkMs;ms+=11)Check(f.Step(true,false,t+ms)==Ray::Held,"a blink holds the gaze's pick");
        Check(f.Step(true,true,t+BlinkMs-1)==Ray::Gaze,"the eyes open: the gaze at once");
        // a single dropped sample: held too
        Check(f.Step(true,false,t+400)==Ray::Held&&f.Step(true,true,t+411)==Ray::Gaze,"one dropped sample is held");
        // lost: the head after BlinkMs, then RegainMs of samples before the gaze again
        Check(f.Step(true,false,t+500)==Ray::Held,"tracking lost: held at first");
        Check(f.Step(true,false,t+500+BlinkMs)==Ray::Head&&!f.On(),"lost for BlinkMs: the head");
        Check(f.Step(true,false,t+2000)==Ray::Head,"and stays with it while lost");
        Check(f.Step(true,true,t+3000)==Ray::Head,"samples back: the head a moment longer");
        Check(f.Step(true,false,t+3050)==Ray::Head&&f.Step(true,true,t+3060)==Ray::Head&&f.Step(true,true,t+3060+RegainMs-1)==Ray::Head,
              "a flicker of samples starts the wait over");
        Check(f.Step(true,true,t+3060+RegainMs)==Ray::Gaze,"steady samples: the gaze again");
        Check(f.Step(false,true,t+4000)==Ray::Head&&!f.On()&&f.Step(true,true,t+4001)==Ray::Head,"turned off: the head, and the wait again on return");
    }
    std::printf("gaze aim: %d checks passed\n",checks);
    return 0;
}
