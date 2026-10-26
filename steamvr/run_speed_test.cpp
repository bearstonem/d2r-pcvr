#include "run_speed.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace framevr::runspeed;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
int main() {
    Check(NormalizePercent(100)==100&&NormalizePercent(54)==50&&NormalizePercent(55)==60&&NormalizePercent(-20)==WalkPercent&&NormalizePercent(250)==100,
          "the slider saves WALK (-10) and 0..100 in steps of 10");
    Check(Walks(WalkPercent)&&!Walks(0)&&!Walks(100),"only the WALK stop walks; BASE RUN and above run");
    Check(Scaled(2856,193,WalkPercent)==Scaled(2856,193,0),"still running at WALK (before the toggle lands): BASE RUN, never slower");
    Check(NormalizePercent(std::numeric_limits<float>::quiet_NaN())==DefaultPercent&&DefaultPercent==100,"a broken value reads the gear's full speed");
    // Logged 2026-10-25: a running hero with no speed bonus is stat 67 = 150 at velocity 2227
    // (Necromancer, Paladin); Paladin with Vigor 163 at 2411; level-90 Barbarian 193 at 2856.
    Check(BonusPoints(150)==0&&BonusPoints(193)==43&&BonusPoints(163)==13,"stat 67 is absolute: 150 is a run with no bonus");
    for(int p=0;p<=100;p+=10)Check(Scaled(2227,150,p)==2227,"no speed bonus: every stop is the game's own run (the 2026-10-25 test slowed it - never again)");
    Check(std::fabs(BaseOf(2856,193)-2219.7)<1&&std::fabs(BaseOf(2411,163)-2218.7)<1,"the Barbarian's and the Paladin's bases land on the no-bonus run");
    Check(Scaled(2856,193,100)==2856,"100%: the game's own speed");
    Check(Scaled(2856,193,0)==2220&&Scaled(2411,163,0)==2219,"0%: the class's no-bonus run, never slower");
    Check(Scaled(2856,193,50)==2538,"50%: halfway between base and full");
    // The FRW test: +30% FRW boots on a Druid, stat 67 150 -> 169, velocity 2227 -> 2503.
    Check(Scaled(2503,169,100)==2503&&Scaled(2503,169,0)==2222,"item FRW (inside stat 67): FULL keeps the boots, BASE runs without them");
    for(int p=0;p<=100;p+=10)Check(Scaled(2856,193,p)<=2856&&Scaled(2856,193,p)>=2220,"never faster than the game, never slower than the base");
    for(int p=10;p<=100;p+=10)Check(Scaled(2856,193,p)>Scaled(2856,193,p-10),"every step is faster than the one below");
    Check(Scaled(1228,193,0)==955,"a start-up (ramping) velocity scales by the same share");
    Check(Scaled(2119,143,0)==2119,"a walk's stat 67 (100 + bonus) never reads as a run bonus above 150");
    Check(Scaled(1800,120,0)==1800,"slowed below a plain run (cold, a curse): the game's speed stays");
    Check(Scaled(0,193,0)==0&&Scaled(-5,193,0)==-5,"a stop (0) or anything not a run velocity passes through");
    Check(Scaled(2856,193,-40)==2220&&Scaled(2856,193,400)==2856,"out-of-range sliders clamp");
    Check(Scaled(2856,0,0)==2856&&Scaled(2856,-10,0)==2856,"an unreadable stat 67 keeps the game's speed");
    {   // Gait sync: the game's run / walk toggle follows the slider without reading a run's start as "set to walk"
        using G=GaitSync;G g;const uint64_t S=7;uint64_t t=10000;
        Check(!g.Step(S,0,0,0,true,true,t)&&g.state==G::Unknown,"a new character: toggle unknown, nothing sent");
        // Cal's second test: every start of a run began as a walk with the stick partly pushed.
        for(int i=0;i<20;++i){t+=50;Check(!g.Step(S,G::Walk,t,.6f,false,true,t),"a run's start (walk, stick partly pushed) never sends the toggle");}
        t+=50;g.Step(S,G::Run,t,1,false,true,t);Check(g.state==G::Run,"a run seen: the toggle is on run");
        t+=50;Check(!g.Step(S,G::Walk,t,.5f,false,true,t),"stopping and starting again (walk at the start): still nothing sent");
        Check(g.Step(S,0,0,0,true,true,t+100),"slider to WALK while standing: the toggle goes at once");
        g.Sent(true,t+100);Check(g.state==G::Walk,"sent: the toggle is now on walk");
        Check(!g.Step(S,G::Run,t+300,1,true,true,t+300)&&g.state==G::Walk,"a run still reported just after the toggle is ignored");
        t+=2000;Check(!g.Step(S,G::Walk,t,1,true,true,t),"walking on WALK: nothing to send");
        Check(g.Step(S,G::Walk,t,1,false,true,t),"slider back to a run: the toggle goes");g.Sent(false,t);
        // Toggle believed on run but the game walks with the stick fully pushed (e.g. a refused toggle): learnt in 600 ms.
        t+=2000;uint64_t first=t;bool sent=false;
        for(int i=0;i<10&&!sent;++i){t+=100;sent=g.Step(S,G::Walk,t,1,false,true,t);}
        Check(sent&&t-first>=G::FullWalkMs&&g.state==G::Walk,"a full-stick walk for 600 ms shows the toggle is on walk, then it is put back");
        g.Sent(false,t);
        Check(!g.Step(S,G::Walk,t+200,1,false,true,t+200),"one toggle a second at most");
        Check(!g.Step(S+1,G::Run,t+5000,1,true,true,t+5000-1000)&&g.state==G::Unknown,"another character: unknown again");
        {   // Out of stamina the game walks whatever its toggle says: no inference, no toggles.
            G tiredSync;uint64_t u=50000;tiredSync.Step(S,G::Run,u,1,false,true,u);
            bool any=false;for(int i=0;i<60;++i){u+=100;any|=tiredSync.Step(S,G::Walk,u,1,false,true,u,true);}
            Check(!any&&tiredSync.state==G::Run,"a tired full-stick walk (6 s) never reads as the toggle on walk");
            bool after=false;for(int i=0;i<10;++i){u+=100;after|=tiredSync.Step(S,G::Walk,u,1,false,true,u,false);}
            Check(after,"with stamina back, a full-stick walk is evidence again");
        }
        {   // Stamina: the logged run (138 max, x256 in the game; the ratios are what count)
            Stamina st;
            Check(!st.Tired(1,3,138,138)&&!st.Tired(1,3,40,138),"running with stamina: not tired");
            Check(st.Tired(1,3,30,138),"below a quarter: tired");
            Check(st.Tired(1,2,3,138),"exhausted (3 of 138): tired");
            Check(st.Tired(1,2,39,138)&&st.Tired(1,2,84,138),"recovering (still walking at 39 and 84 of 138): tired, no toggles");
            Check(!st.Tired(1,3,82,138),"running again: rested");
            Check(!st.Tired(1,2,90,138),"a walk with stamina back after a run: evidence again");
            st.Tired(1,2,2,138);Check(!st.Tired(1,2,132,138),"recovered to 95% while still walking (toggle on walk): evidence again");
            st.Tired(1,2,2,138);Check(!st.Tired(2,2,100,138),"another hero starts rested");
            Check(!st.Tired(3,2,0,0),"no stamina known: not tired");
        }
        G hidden;hidden.Step(S,G::Run,100,1,true,false,100);Check(!hidden.Step(S,G::Run,200,1,true,false,200),"no slider (settings not ready): nothing sent");
    }
    {   // Right stick up with the Stick toggle on (off by default)
        StickToggle s;
        Check(!s.Step(1,false,100,true)&&!s.Step(1,false,100,true),"off: a push changes nothing");
        Check(s.Step(1,true,100,true)&&s.Step(1,true,100,false),"on: a push from a run walks, and stays walking");
        Check(!s.Step(1,true,100,true)&&!s.FullRun(),"the next push runs again, at Movement speed's run");
        Check(s.Step(1,true,50,true)&&!s.FullRun()&&s.Step(1,true,50,false),"from Run 50%: a push walks");
        Check(!s.Step(1,true,60,false),"Movement speed changed: its run again");
        Check(s.Step(1,true,WalkPercent,false)&&!s.FullRun(),"Movement speed on Walk walks");
        Check(!s.Step(1,true,WalkPercent,true)&&s.FullRun(),"from Walk: a push runs, at the full run");
        Check(s.Step(2,true,WalkPercent,false)&&!s.FullRun(),"a new character starts at Movement speed");
        s.Step(2,true,100,true);Check(!s.Step(2,false,100,false)&&!s.Step(2,true,100,false),"turning it off forgets the switch");
    }
    std::puts("Run speed slider tests passed");
}
