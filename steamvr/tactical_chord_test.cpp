#include "tactical_chord.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr::tacticalchord;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}

// One frame: what the game would get (Withholds, before the step), then the step.
struct Sim {
    Hand h;double t=1;bool withheld=false;
    Event Frame(float grip,float trigger,bool pointing,bool allowed=true){
        t+=1.0/90;withheld=h.Withholds(t,grip,trigger);
        return h.Step(t,grip,trigger,pointing,allowed);
    }
};

int main() {
    {   // grip on the mercenary, trigger pulled, both let go: armed, latched, carried out; the trigger is never the game's
        Sim s;
        Check(s.Frame(0,0,true)==Event::None&&!s.h.triggerOwned,"idle");
        Check(s.Frame(.7f,0,true)==Event::Armed&&s.h.triggerOwned,"the grip on the mercenary arms; the trigger is owned");
        Check(s.Frame(.7f,.6f,false)==Event::Latched&&s.withheld,"the trigger pull latches, wherever the hand points now, never the game's");
        Check(s.Frame(.7f,.6f,false)==Event::None&&s.h.latched&&s.withheld,"held: the command is aimed");
        Check(s.Frame(.7f,.1f,false)==Event::Release&&!s.h.Engaged(),"the trigger let go carries it out");
        Check(!s.h.triggerOwned&&!s.h.Withholds(s.t,.7f,.1f),"and the trigger is the game's again");
    }
    {   // gripped first, then pointed at the mercenary: armed all the same (it was missed before)
        Sim s;
        Check(s.Frame(.8f,0,false)==Event::None&&!s.h.Engaged(),"gripped, pointing elsewhere: nothing");
        Check(s.Frame(.8f,0,true)==Event::Armed,"swung onto the mercenary with the grip held: armed");
        Check(s.Frame(.8f,.7f,true)==Event::Latched&&s.withheld,"the pull latches, withheld from the game");
    }
    {   // grip and trigger pressed in one go on the mercenary: latched, and that very pull never reaches the game
        Sim s;s.Frame(0,0,true);
        Check(s.Frame(.8f,.8f,true)==Event::Latched,"both in one frame: latched");
        Check(s.withheld,"the pull is withheld before the game sees it");
    }
    {   // the trigger a frame ahead of the grip (squeezing both) while on the mercenary: an attack, kept whole
        Sim s;s.Frame(0,0,true);
        Check(s.Frame(.3f,.8f,true)==Event::None&&!s.withheld,"the trigger alone: the game's");
        Check(s.Frame(.8f,.8f,true)==Event::None&&!s.h.Engaged()&&!s.withheld,"the grip then: the attack keeps its trigger");
        Check(s.Frame(.8f,.1f,true)==Event::Armed,"let go: the held grip on the mercenary arms");
        Check(s.Frame(.8f,.8f,true)==Event::Latched&&s.withheld,"the next pull is the command's");
    }
    {   // the hand slips off the mercenary for a moment: still armed within the grace, then disarmed
        Sim s;s.Frame(.8f,0,true);
        for(int i=0;i<15;++i)Check(s.Frame(.8f,0,false)==Event::None&&s.h.armed,"within the grace: armed");
        Check(s.Frame(.8f,.7f,false)==Event::Latched&&s.withheld,"a pull in the grace latches");
        Sim off;off.Frame(.8f,0,true);
        Event e=Event::None;for(int i=0;i<30&&e==Event::None;++i)e=off.Frame(.8f,0,false);
        Check(e==Event::Disarmed&&!off.h.triggerOwned,"off the mercenary past the grace: disarmed, no order");
        Check(off.Frame(.8f,.7f,false)==Event::None&&!off.withheld&&!off.h.Engaged(),"then the trigger is the game's");
    }
    {   // the grip let go first while latched: carried out, the still-held trigger stays the command's until let go
        Sim s;s.Frame(.7f,0,true);s.Frame(.7f,.6f,true);
        Check(s.Frame(.1f,.6f,false)==Event::Release,"the grip let go carries it out");
        Check(s.h.triggerOwned&&s.withheld,"the trigger still held is not handed to the game mid-pull");
        Check(s.Frame(.1f,.1f,false)==Event::None&&!s.h.triggerOwned,"let go: the game's again");
        Check(s.Frame(.1f,.6f,false)==Event::None&&!s.withheld,"a new pull with no grip is the game's");
    }
    {   // a Paladin fist (grip held, trigger free) across the mercenary: armed, then let go with no pull - nothing ordered
        Sim s;
        Check(s.Frame(.8f,0,true)==Event::Armed,"the fist arms the hand");
        for(int i=0;i<30;++i)Check(s.Frame(.8f,.05f,true)!=Event::Latched,"the call's motion: no command");
        Check(s.Frame(.2f,0,false)==Event::Disarmed&&!s.h.triggerOwned,"the fist opened: disarmed, no order");
    }
    {   // an attack in progress (trigger held) when the grip closes on the mercenary: never armed
        Sim s;s.Frame(0,.8f,false);
        Check(s.Frame(.8f,.8f,true)==Event::None&&!s.h.Engaged()&&!s.h.triggerOwned&&!s.withheld,"the held trigger stays the attack's");
    }
    {   // hysteresis: a grip easing between the thresholds stays closed, a trigger between them stays pulled
        Sim s;s.Frame(.7f,0,true);
        Check(s.Frame(.45f,0,true)==Event::None&&s.h.armed,"the grip above GripOff stays closed");
        Check(s.Frame(.45f,.6f,true)==Event::Latched,"latched");
        Check(s.Frame(.45f,.3f,false)==Event::None&&s.h.latched,"the trigger above TriggerOff stays pulled");
        Check(s.Frame(.45f,.5f,false)==Event::None&&s.h.latched,"a trigger easing back up is no new pull");
    }
    {   // the hand stops (the belt, the wheel, untracked) while latched: disarmed, nothing ordered; the held trigger stays owned
        Sim s;s.Frame(.7f,0,true);s.Frame(.7f,.6f,true);
        Check(s.Frame(.7f,.6f,true,false)==Event::Disarmed&&!s.h.Engaged(),"busy: no order");
        Check(s.h.triggerOwned,"the trigger pulled for the command is not handed to the game");
        Check(s.Frame(.7f,.6f,true)!=Event::Latched,"back: the held trigger is no new pull");
        Check(s.Frame(.7f,.1f,true)==Event::Armed&&s.h.triggerOwned,"let go with the grip still on the mercenary: armed again");
    }
    {   // Reset clears everything
        Sim s;s.Frame(.7f,0,true);s.h.Reset();
        Check(!s.h.Engaged()&&!s.h.triggerOwned&&!s.h.grip&&!s.h.trigger&&s.h.pointedAt<0,"reset");
    }
    std::puts("tactical-chord-test: ok");
    return 0;
}
