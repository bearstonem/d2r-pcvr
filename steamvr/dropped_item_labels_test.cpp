#include "dropped_item_labels.h"
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
    assert(BackingAlpha(.6f)==.85f&&BackingAlpha(.95f)==.95f);
    assert(BackingAlpha(0)==0&&BackingAlpha(-1)==-1);
    std::array<int32_t,4> box{100,150,120,28};
    const int32_t beforeText[]={100,20},afterText[]={150,30};
    assert(Resize(box.data(),beforeText,afterText,1.5f)); // custom scaling still supported
    assert((box==std::array<int32_t,4>{70,136,180,42})); // stable center/bottom, matching text and padding
    const auto keep=box;
    for(float bad:{NAN,INFINITY,0.f,3.f})assert(!Resize(box.data(),beforeText,afterText,bad)&&box==keep);
    const int32_t empty[]={0,0};assert(!Resize(box.data(),empty,afterText,1.f)&&box==keep);
    box={INT32_MIN,150,120,28};const auto edge=box;
    assert(!Resize(box.data(),beforeText,afterText,1.f)&&box==edge);
    // The shipped first-person profile uses 45%, not 100% of native text size.
    // Both text and padding shrink, keeping the hit box centred and grounded.
    box={100,150,120,28};const int32_t smallText[]={45,9};
    assert(Resize(box.data(),beforeText,smallText,.45f));
    assert((box==std::array<int32_t,4>{133,165,54,13}));
    float multiplier=0;
    {TextScaleScope scope(multiplier,.45f);assert(multiplier==.45f);
        {TextScaleScope nested(multiplier,.75f);assert(multiplier==.75f);}
        assert(multiplier==.45f);}
    assert(multiplier==0); // other native text retains its original scale
    // Labels are kept over the whole eye, not only the interface's widescreen band.
    auto size=[](uint64_t w,uint64_t h){return h<<32|w;};
    assert(LabelArea(size(2048,1152),size(2048,2048))==size(2048,2048)); // 2048 square
    assert(LabelArea(size(2016,1134),size(2016,2240))==size(2016,2240)); // Index at 100%
    assert(LabelArea(size(1900,1152),size(2048,2048))==size(1900,2048)); // the width stays the interface's
    assert(LabelArea(size(3840,2160),size(3840,2160))==size(3840,2160)); // widescreen: nothing to add
    assert(LabelArea(size(1920,1080),0)==size(1920,1080)); // desktop play: no VR size
    std::puts("Loot labels: contrast/geometry, Show Items lease, lifecycle guards and the label area passed");
}
