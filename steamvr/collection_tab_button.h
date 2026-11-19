#pragma once
// The Collection tab on the stash bar, taken by the mod's own laser.
//
// The game's tab bar sends one message for all of its tabs and carries no index in
// it, so a sixth tab of the bar cannot be told from the fifth, and this D2RLoader
// (1.3.1-beta) has no widget API to route a click. So the mod takes the laser on
// the sixth tab itself, the way it already does for Advanced Stats and Reset Skills
// & Stats: the laser on it moves no game cursor, and its trigger is never the game's.
// Choosing it shows the Collection page without closing the stash.
#include <cmath>
#include <cstdint>
#include <string_view>

namespace framevr::collectiontab {

// While the stash (bank) is open: the same panels the stash plaques appear on.
template<class Names> inline bool Shown(const Names& open) {
    bool stash=false;
    for(const auto& name:open) {
        const std::string_view n(name);
        if(n.starts_with("HoradricCube")||n.starts_with("Vendor")||n.starts_with("Trade")||n.starts_with("Gamble"))return false;
        stash=stash||n.starts_with("Bank")||n.starts_with("Stash");
    }
    return stash;
}

// The sixth tab's place on the surface, measured in the headset (2026-11-19): the
// owner clicked the four corners of the Collection tab, giving u 0.409..0.451 and
// v 0.067..0.099 (the grid's middle reads u 0.312 v 0.225, below the tab). A small
// margin is added around those.
constexpr float Left=0.404f,Right=0.456f,Top=0.060f,Bottom=0.106f;
struct Spot {float x=0,y=0,w=0,h=0;};
inline Spot Place(float width,float height) {
    if(!(width>0)||!(height>0))return {};
    const float x0=(Left-.5f)*width,x1=(Right-.5f)*width;
    const float y0=(.5f-Top)*height,y1=(.5f-Bottom)*height;
    return {(x0+x1)*.5f,(y0+y1)*.5f,x1-x0,y0-y1};
}
// The laser's hit on the surface (u, v from its top left, 0-1), inside the tab.
inline bool Hit(Spot s,float width,float height,float u,float v) {
    if(!(s.w>0)||!(s.h>0))return false;
    const float x=(u-.5f)*width,y=(.5f-v)*height;
    return std::fabs(x-s.x)<=s.w*.5f&&std::fabs(y-s.y)<=s.h*.5f;
}

// One action per pull of the trigger begun on the tab: held at least MinMs, and
// while the trigger stays down at most MaxMs. A trigger already down when the laser
// reaches the tab does nothing; letting go anywhere rearms it.
constexpr uint64_t MinMs=80,MaxMs=400;
class Press {
    bool was=true,held=false;uint64_t since=0;
public:
    void Reset(){was=true;held=false;}
    bool Held() const{return held;}
    // True on the frame a press starts (to log it).
    bool Step(bool on,bool trigger,uint64_t now) {
        bool started=false;
        if(trigger&&!was&&on){held=true;since=now;started=true;}
        else if(held&&(now-since>=MaxMs||(!trigger&&now-since>=MinMs)))held=false;
        was=trigger;
        return started;
    }
};

}   // namespace framevr::collectiontab
