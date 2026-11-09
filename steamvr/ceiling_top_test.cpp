#include "ceiling_top.h"
#include <cassert>

using namespace framevr::ceilingtop;

int main() {
    // Built rooms' border floors, per level.
    LevelFloors f;
    const float flat[4]={0,0,0,0},stair[4]={0,0,-6.5f,0},nan4[4]={NAN,NAN,NAN,NAN};
    float hi=0;
    assert(!f.Highest(0xA,hi));
    f.Add(0xA,0.03f,flat);f.Add(0xA,6.53f,stair);f.Add(0xB,-2.96f,flat);
    assert(f.Highest(0xA,hi)&&hi==6.53f);
    assert(f.Highest(0xB,hi)&&hi==-2.96f);
    f.Add(0xA,3.0f,nan4);assert(f.Highest(0xA,hi)&&hi==6.53f);   // no edges: the height itself
    f.Add(0,99,flat);assert(!f.Highest(0,hi));
    // Old levels drop out once more than Slots are in use; the most recent stay.
    for(uintptr_t l=0x100;l<0x100+LevelFloors::Slots;++l)f.Add(l,1,flat);
    assert(!f.Highest(0xB,hi)&&f.Highest(0x100+LevelFloors::Slots-1,hi));

    // The tomb: he enters at 0.03 with the level's rooms built up to 6.53: the ceiling's top is 6.53 at once.
    {
        Top t;
        assert(t.Step(0,1,0xA,0.03f,true,6.53f)==6.53f);
        // up the stairs and down again: it never moves
        for(double s=0.1;s<5;s+=0.1)assert(t.Step(s,1,0xA,0.03f+float(s),true,6.53f)==std::max(6.53f,0.03f+float(s)));
        // (the hero's own floor counts: he stood at ~4.9 above 6.53 is impossible here, but if he did it follows)
    }
    {
        Top t;
        t.Step(0,1,0xA,0.03f,true,6.53f);
        // a higher room built later (after the settle time): eased up, never down
        float v=t.Step(3,1,0xA,0.03f,true,13.0f);
        assert(v>6.53f&&v<13.0f);
        for(double s=3.1;s<6;s+=0.05)v=t.Step(s,1,0xA,0.03f,true,13.0f);
        assert(v==13.0f);
        assert(t.Step(6.1,1,0xA,0.03f,true,13.0f)==13.0f);
        // within the settle time a new level is taken whole, higher or lower
        assert(t.Step(7,2,0xB,-2.96f,true,-2.96f)==-2.96f);
        assert(t.Step(7.5,2,0xB,-2.96f,true,3.03f)==3.03f);
        // the hero's level unknown: his own floor
        Top u;assert(u.Step(0,1,0,1.5f,false,0)==1.5f);
        assert(u.Step(0.1,1,0xC,1.5f,true,4.0f)==4.0f);   // the level now known: a fresh start, taken whole
        assert(std::isfinite(u.Step(0.2,1,0xC,NAN,true,4.0f)));
    }
    return 0;
}
