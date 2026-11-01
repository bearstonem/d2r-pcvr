#include "talk_stop.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}

int main() {
    Check(talkstop::TalkPanel("NpcDialogPanel")&&talkstop::TalkPanel("ScrollSpeechPanel"),"the conversation's panels");
    for(auto other:{"UISwitcher","VendorPanelLayout","WaypointsPanelExp","BankExpansionLayout","QuestLogPanelExpansion",""})
        Check(!talkstop::TalkPanel(other),"merchants, stash, waypoints and the menu are not a conversation");

    using talkstop::HoldMs;using talkstop::MinDownMs;using talkstop::WalkMaxMs;
    // A on an NPC while walking (Cal's 2026-10-29 tests): the walk goes first, then A.
    {
        talkstop::Hold h;bool began=false;
        Check(!h.Poll(true,true,true,4,1000,&began)&&began,"the press: A held back, a hold begun");
        Check(h.WalkHeld(1000),"the walk let go at the press");
        for(uint64_t t=1001;t<1000+HoldMs;++t)Check(!h.Poll(true,true,true,4,t)&&h.WalkHeld(t),"no A while the walk stops");
        Check(h.Poll(true,true,true,4,1000+HoldMs),"then A reaches the game");
        Check(h.WalkHeld(1000+HoldMs+5),"the walk stays let go until the conversation opens");
        Check(h.Poll(true,true,true,5,1000+HoldMs+20)&&!h.WalkHeld(1000+HoldMs+21),"the conversation up: the walk is the game's again");
        Check(h.Poll(true,true,true,5,1300)&&!h.Poll(false,true,true,5,1301),"A then follows the button as ever");
    }
    // A quick tap, released inside the hold: A still reaches the game, for MinDownMs.
    {
        talkstop::Hold h;
        h.Poll(true,true,true,0,0);
        Check(!h.Poll(false,true,true,0,20),"released during the hold: not yet");
        Check(h.Poll(false,true,true,0,HoldMs)&&h.Poll(false,true,true,0,HoldMs+MinDownMs-1),"then down for MinDownMs");
        Check(!h.Poll(false,true,true,0,HoldMs+MinDownMs),"then up");
        Check(h.WalkHeld(HoldMs+MinDownMs+1),"the walk still let go: the conversation may yet open");
        Check(!h.Poll(false,true,true,0,WalkMaxMs)&&!h.WalkHeld(WalkMaxMs),"none came: the walk is back after WalkMaxMs");
    }
    // Standing, or not an NPC (an item, a chest, a door): A as ever, no hold.
    {
        talkstop::Hold h;bool began=true;
        Check(h.Poll(true,true,false,0,0,&began)&&!began&&!h.WalkHeld(0),"standing: A at once");
        h.Poll(false,true,false,0,10);
        Check(h.Poll(true,false,true,0,20,&began)&&!began&&!h.WalkHeld(20),"walking onto an item: A at once");
    }
    // A held down (no new press) as the stick comes up: no hold mid-press.
    {
        talkstop::Hold h;
        Check(h.Poll(true,true,false,0,0),"pressed while standing");
        Check(h.Poll(true,true,true,0,10)&&!h.WalkHeld(10),"walking off with A still held: nothing held");
    }
    // A second press inside a hold does not restart it.
    {
        talkstop::Hold h;
        h.Poll(true,true,true,0,0);h.Poll(false,true,true,0,10);
        Check(!h.Poll(true,true,true,0,30),"pressed again inside the hold: still held");
        Check(h.Poll(true,true,true,0,HoldMs),"and A at the first press's time");
    }

    // The drift after an open (developer log): the first call only counts.
    {
        talkstop::Watch w;
        Check(!w.Wants(7,0),"nothing wanted before the first call");
        Check(w.Step(true,7,1000,true,10,10)==talkstop::Watch::Action::None&&!w.Wants(7,1001),"counted, idle");
        Check(w.Wants(8,1001),"a new conversation wants the position");
        w.Step(true,8,1001,true,5369.9f,4261.2f);
        Check(!w.Wants(8,1002)&&!w.Wants(8,1001+talkstop::WatchMs-1)&&w.Wants(8,1001+talkstop::WatchMs),"read again only at the end");
        Check(w.Step(true,8,1500,false,0,0)==talkstop::Watch::Action::None,"nothing mid-watch");
        Check(w.Step(true,8,1001+talkstop::WatchMs,true,5369.6f,4255.6f)==talkstop::Watch::Action::Report&&
              std::fabs(w.drift-5.608f)<.01f,"rev1's 21:48:41: 5.6 cells after the open");
        Check(!w.Wants(8,4000),"idle after the report");
    }
    {
        talkstop::Watch w;w.Step(true,0,0,true,0,0);
        Check(w.Step(true,1,10,false,0,0)==talkstop::Watch::Action::None,"no position yet");
        Check(w.Step(true,1,10+talkstop::LostMs,false,0,0)==talkstop::Watch::Action::Lost,"no position: lost");
        for(uint64_t t=1;t<50;++t)Check(w.Step(false,2,1000+t,true,0,0)==talkstop::Watch::Action::None,"inactive: nothing");
        Check(w.Step(true,2,2000,true,0,0)==talkstop::Watch::Action::None&&!w.Wants(2,2001),"an open while inactive is not watched");
    }
    std::puts("talk_stop_test: ok");
}
