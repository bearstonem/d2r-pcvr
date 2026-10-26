#include "stick_gestures.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace framevr::stickgestures;
static void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main() {
    Gestures g;
    Check(!g.Step(true,0,1).toggleRun,"a stick already pushed when play starts waits for the centre");
    g.Step(true,0,0);
    Check(!g.Step(true,0,.5f).owned,"a small push is nothing yet");
    auto up=g.Step(true,.1f,.9f);
    Check(up.toggleRun&&up.owned&&!up.showLoot,"up toggles run / walk and owns the stick");
    for(int i=0;i<20;++i){const auto held=g.Step(true,.3f,.95f);Check(!held.toggleRun&&held.owned,"holding up toggles once, turning nothing");}
    Check(!g.Step(true,0,.2f).owned,"back near the centre releases the stick");
    Check(g.Step(true,0,1).toggleRun,"the next push toggles again");
    g.Step(true,0,0);
    auto down=g.Step(true,-.1f,-.8f);
    Check(down.showLoot&&down.owned&&!down.toggleRun,"down shows the loot labels");
    for(int i=0;i<20;++i)Check(g.Step(true,.4f,-.7f).showLoot,"for as long as it is held, wandering a little");
    Check(!g.Step(true,0,-.1f).showLoot,"letting go hides them");
    g.Step(true,0,0);
    Check(!g.Step(true,.9f,.1f).owned,"a sideways push is a turn");
    Check(!g.Step(true,.6f,.75f).owned&&!g.Step(true,.2f,.9f).toggleRun,"a turn sliding up is still a turn until the centre");
    g.Step(true,0,0);
    Check(!g.Step(true,.6f,.8f).owned,"a diagonal push is a turn");
    g.Step(true,0,0);
    Check(g.Step(true,.5f,.8f).toggleRun,"a mostly vertical push counts");
    g.Step(true,0,0);g.Step(true,0,-1);
    Check(!g.Step(false,0,-1).showLoot,"a menu or wheel drops the labels at once");
    Check(!g.Step(true,0,-1).showLoot,"and the stick, still held, must come back first");
    g.Step(true,0,0);
    Check(!g.Step(true,NAN,1).owned&&!g.Step(true,0,1).toggleRun,"a broken sample is an interruption");
    g.Step(true,0,0);g.Step(true,0,1);g.Reset();
    Check(!g.Step(true,0,1).owned,"Reset drops a push");
    std::puts("Right stick gestures: up toggles run / walk once per push, down holds the loot labels, turns and interruptions keep out");
    return 0;
}
