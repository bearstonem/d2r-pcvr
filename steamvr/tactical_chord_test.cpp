#include "tactical_chord.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr::tacticalchord;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}

int main() {
    {   // grip on the mercenary, trigger pulled, both let go: armed, latched, carried out; the trigger is never the game's
        Hand h;
        Check(h.Step(0,0,true,true)==Event::None&&!h.triggerOwned,"idle");
        Check(h.Closing(.7f),"a closing grip is seen before the step");
        Check(h.Step(.7f,0,true,true)==Event::Armed&&h.triggerOwned,"the grip on the mercenary arms; the trigger is owned");
        Check(h.Step(.7f,.6f,false,true)==Event::Latched&&h.triggerOwned,"the trigger pull latches, wherever the hand points now");
        Check(h.Step(.7f,.6f,false,true)==Event::None&&h.latched,"held: the command is aimed");
        Check(h.Step(.7f,.1f,false,true)==Event::Release&&!h.Engaged(),"the trigger let go carries it out");
        Check(!h.triggerOwned,"and the trigger is the game's again");
    }
    {   // the grip let go first while latched: carried out, the still-held trigger stays the command's until let go
        Hand h;h.Step(.7f,0,true,true);h.Step(.7f,.6f,true,true);
        Check(h.Step(.1f,.6f,false,true)==Event::Release,"the grip let go carries it out");
        Check(h.triggerOwned,"the trigger still held is not handed to the game mid-pull");
        Check(h.Step(.1f,.6f,false,true)==Event::None&&h.triggerOwned,"still held, still owned");
        Check(h.Step(.1f,.1f,false,true)==Event::None&&!h.triggerOwned,"let go: the game's again");
        Check(h.Step(.1f,.6f,false,true)==Event::None&&!h.triggerOwned,"a new pull with no grip is the game's");
    }
    {   // a Paladin fist (grip held, trigger free) near the mercenary: armed, then let go with no pull - nothing ordered
        Hand h;
        Check(h.Step(.8f,0,true,true)==Event::Armed,"the fist arms the hand");
        for(int i=0;i<30;++i)Check(h.Step(.8f,.05f,false,true)==Event::None,"the call's motion: nothing");
        Check(h.Step(.2f,0,false,true)==Event::Disarmed&&!h.triggerOwned,"the fist opened: disarmed, no order");
    }
    {   // the grip closed pointing elsewhere: never arms, the trigger stays the game's
        Hand h;
        Check(h.Step(.8f,0,false,true)==Event::None&&!h.Engaged(),"not on the mercenary");
        Check(h.Step(.8f,0,true,true)==Event::None&&!h.Engaged(),"swinging onto the mercenary with the grip held does not arm");
        Check(h.Step(.8f,.7f,true,true)==Event::None&&!h.triggerOwned,"the trigger with the grip held is the game's");
    }
    {   // an attack in progress (trigger held) when the grip closes on the mercenary: never armed
        Hand h;h.Step(0,.8f,false,true);
        Check(h.Step(.8f,.8f,true,true)==Event::None&&!h.Engaged()&&!h.triggerOwned,"the held trigger stays the attack's");
    }
    {   // hysteresis: a grip easing between the thresholds stays closed, a trigger between them stays pulled
        Hand h;h.Step(.7f,0,true,true);
        Check(h.Step(.45f,0,false,true)==Event::None&&h.armed,"the grip above GripOff stays closed");
        Check(h.Step(.45f,.6f,false,true)==Event::Latched,"latched");
        Check(h.Step(.45f,.3f,false,true)==Event::None&&h.latched,"the trigger above TriggerOff stays pulled");
        Check(h.Step(.45f,.5f,false,true)==Event::None&&h.latched,"a trigger easing back up is no new pull");
    }
    {   // the hand stops (the belt, the wheel, untracked) while latched: disarmed, nothing ordered; the held trigger stays owned
        Hand h;h.Step(.7f,0,true,true);h.Step(.7f,.6f,true,true);
        Check(h.Step(.7f,.6f,true,false)==Event::Disarmed&&!h.Engaged(),"busy: no order");
        Check(h.triggerOwned,"the trigger pulled for the command is not handed to the game");
        Check(h.Step(.7f,.6f,true,true)==Event::None&&!h.Engaged(),"back: the held grip does not re-arm without a new press");
        Check(h.Step(.7f,.1f,true,true)==Event::None&&!h.triggerOwned,"the trigger let go: the game's again");
    }
    {   // Reset clears everything
        Hand h;h.Step(.7f,0,true,true);h.Reset();
        Check(!h.Engaged()&&!h.triggerOwned&&!h.grip&&!h.trigger,"reset");
    }
    std::puts("tactical-chord-test: ok");
    return 0;
}
