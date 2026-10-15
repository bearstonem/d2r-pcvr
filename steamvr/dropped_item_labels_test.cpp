#include "dropped_item_labels.h"
#include "hand_controls.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace framevr;
int main() {
    using namespace lootlabels;
    Display look;
    assert(!look.Active(1000)&&!look.ShowAll(1000));
    look.Refresh(1,1000,true);assert(look.Active(1000)&&!look.ShowAll(1000)); // normal drop/hover behavior by default
    look.Refresh(1,1020,true,true);assert(look.ShowAll(1020));
    look.Refresh(1,1100,true,true);assert(look.ShowAll(1200));
    assert(!look.Active(1400)&&!look.ShowAll(1400)); // no stale Alt hold in desktop play
    look.Refresh(1,1500,true);assert(!look.ShowAll(1500)); // no retained toggle after expiration
    look.Refresh(1,1600,false);assert(!look.Active(1600)&&!look.ShowAll(1600));
    look.Refresh(1,1650,true,true);assert(look.ShowAll(1650));
    look.Refresh(1,1651,true);assert(!look.ShowAll(1651)&&look.Active(1651)); // release keeps darker native labels
    look.Refresh(2,1700,true);assert(!look.ShowAll(1700)); // new session, no saved choice
    look.Refresh(2,1701,true,true);look.Clear();assert(!look.ShowAll(1701));
    look.Refresh(0,1800,true,true);assert(!look.Active(1800)&&!look.ShowAll(1800));
    look.Refresh(2,1900,false,true);assert(!look.Active(1900)&&!look.ShowAll(1900));
    assert(TextScale==1.f&&BackingAlpha(.6f)==.85f&&BackingAlpha(.95f)==.95f);
    assert(BackingAlpha(0)==0&&BackingAlpha(-1)==-1);
    Shortcut combo;uint64_t now=1000,session=1,epoch=1;
    auto step=[&](float trigger,float grip=1,bool allowed=true){now+=20;return combo.Step(session,epoch,now,allowed,trigger,grip);};
    assert(!step(true)&&combo.Consumed()); // first sample must see a release before showing
    assert(!step(false)&&!combo.Consumed());
    assert(combo.GripConsumed()&&!combo.GripConsumed(true)); // releasing trigger first cannot replay grip
    assert(step(true)&&combo.Consumed());
    for(int i=0;i<5;++i)assert(step(true)); // stays shown for the entire hold
    assert(!step(true,0)&&combo.Consumed()); // release grip first: no attack replay
    assert(!combo.GripConsumed());
    assert(step(true,1)&&combo.GripConsumed()); // regrip resumes labels and consumes the new grip
    assert(!step(true,0)&&combo.Consumed());
    assert(!combo.Consumed(true)); // native panel trigger and Shift remain available
    assert(!step(false,0)&&!combo.Consumed());
    assert(!step(true,0)&&!combo.Consumed());
    assert(!step(true,1)&&!combo.Consumed()); // adding grip during an attack cannot steal its trigger
    step(false);assert(step(true));
    for(int event=0;event<5;++event) {
        step(false);
        if(event==0)++session;if(event==1)++epoch;if(event==2)now+=1000;
        if(event==3)combo.Suspend();
        assert(!step(true,1,event!=4));
        assert(!step(true,1)); // recovery cannot replay a held trigger
        step(false);assert(step(true));
    }
    step(false);assert(!step(true,NAN));step(false);assert(step(true));
    step(false);assert(!step(NAN,1));step(false);assert(step(true));
    // Analog squeeze noise cannot interrupt a consumed chord.
    step(0,0);step(0,.6f);assert(step(.8f,.6f));
    for(float value:{.5f,.4f,.3f})assert(step(value,.45f)&&combo.Consumed()&&combo.GripConsumed());
    assert(!step(.19f,.45f)&&!combo.Consumed()&&combo.GripConsumed());
    assert(step(.8f,.45f)); // repeat while the same grip stays held
    assert(!step(1,.3f)&&combo.Consumed()&&!combo.GripConsumed());
    assert(step(1,1)&&combo.Consumed()&&combo.GripConsumed()); // regrip resumes without a grab
    step(0,0);assert(!step(.4f,.4f)&&!combo.Consumed()); // gentle touch is not a chord
    step(0,1);assert(step(1,1));
    // The production filter consumes right trigger/grip before skill routing,
    // while preserving the other hand, face buttons, movement and wheel click.
    menus::Pad raw;raw.rt=raw.lt=255;raw.buttons=menus::RightBumper|menus::LeftBumper|menus::RightStick|menus::A;raw.lx=18000;
    auto filtered=raw;float grip=1;combo.Filter(filtered,grip);
    assert(!filtered.rt&&grip==0&&filtered.lt==255&&filtered.lx==18000);
    assert(filtered.buttons==(raw.buttons&~menus::RightBumper));
    auto native=raw;float nativeGrip=1;combo.Filter(native,nativeGrip,true);
    assert(native.rt==255&&native.buttons==raw.buttons&&nativeGrip==1);
    skills::HandControls hands;skills::HandInput input;input.context=input.layout=1;input.active=true;
    for(int i=0;i<3;++i){input.time+=.02;hands.Step(input);}
    input.occupied=combo.Consumed()||combo.GripConsumed();
    input.trigger[0]=filtered.rt/255.f;input.time+=.02;
    assert(hands.Step(input).slot==-1); // showing names does not cast the assigned skill
    step(1,0);filtered=raw;grip=0;combo.Filter(filtered,grip);assert(!filtered.rt);
    step(0,0);filtered=raw;grip=0;combo.Filter(filtered,grip);assert(filtered.rt==255);
    input.occupied=false;input.time+=.02;hands.Step(input);
    input.click[0]=(filtered.buttons&menus::RightStick)!=0;input.time+=.02;
    assert(hands.Step(input).wheel==0); // ordinary stick clicks keep their wheel binding after release
    std::array<int32_t,4> box{100,150,120,28};
    const int32_t beforeText[]={100,20},afterText[]={150,30};
    assert(Resize(box.data(),beforeText,afterText,1.5f)); // legacy custom scaling still supported
    assert((box==std::array<int32_t,4>{70,136,180,42})); // stable center/bottom, matching text and padding
    const auto keep=box;
    for(float bad:{NAN,INFINITY,0.f,3.f})assert(!Resize(box.data(),beforeText,afterText,bad)&&box==keep);
    const int32_t empty[]={0,0};assert(!Resize(box.data(),empty,afterText,TextScale)&&box==keep);
    box={INT32_MIN,150,120,28};const auto edge=box;
    assert(!Resize(box.data(),beforeText,afterText,TextScale)&&box==edge);
    float multiplier=0;
    {TextScaleScope scope(multiplier,TextScale);assert(multiplier==1.f);
        {TextScaleScope nested(multiplier,.75f);assert(multiplier==.75f);}
        assert(multiplier==1.f);}
    assert(multiplier==0); // other native text retains its original scale
    std::puts("Loot labels: contrast/geometry, Show Items lease, grip/trigger hysteresis, combat consumption and lifecycle guards passed");
}
