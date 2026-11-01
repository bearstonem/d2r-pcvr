#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// An item taken out of the tabbed menu and let go outside it drops to the ground,
// as in flat D2R (tester, 2026-10-29: "take an item out of inventory and throw it
// away when i drop it outside of my inventory area"). The laser picks an item up
// onto the game's cursor with a click, as ever (owner: "a press and release to pick up,
// then press again to drop"). With an item on the cursor, a trigger pull that begins
// off the menu's art - past the hide, on the hide, or in the menu band's see-through
// margins - sends the game the pad's X: the controller menu's own Drop for the item
// held (owner 2026-10-29: "Yes its X to drop"). A mouse click in the world never
// reached it: the game takes every click under its tabbed menu as the menu's (the
// owner's rev4 log: the first such click switched the menu to its options page).
namespace framevr::menudrop {

// The menu's art inside its 16:9 band (Cal's captures, 2026-10-28/29: x 384-2688 of
// 3072, y 80-1489 of 1728), with room round it for the hint row and the Advanced
// Stats button below (stats_button.h): a pull there never drops.
constexpr float ArtLeft=384.f/3072,ArtRight=2688.f/3072,ArtTop=.03f,ArtBottom=.95f;
// The band's share of a surface's height: all of it on the leather (the surface is
// the band); the top width*9/16 of a taller room-panel canvas.
inline float BandShare(float width,float height) {
    if(!(width>0)||!(height>0))return 1.f;
    return std::min(1.f,width*9.f/16.f/height);
}
// u, v: the laser's hit on the surface from its top left, 0-1.
inline bool InArt(float u,float v,float width,float height) {
    if(!std::isfinite(u)||!std::isfinite(v))return false;
    const float bandV=v/BandShare(width,height);
    return u>=ArtLeft&&u<=ArtRight&&bandV>=ArtTop&&bandV<=ArtBottom;
}
// One X per trigger pull begun off the menu with an item on the cursor: held while the
// trigger is, at least MinMs so the game's poll sees it and at most HoldMs. Check,
// CheckMs after the release: whether the item left the cursor (to log).
constexpr uint64_t MinMs=80,HoldMs=1000,CheckMs=600;
class Drop {
    bool down=false,was=true;uint64_t at=0,check=0;
public:
    struct Out {bool press=false,release=false,check=false;};
    void Reset(){down=false;was=true;check=0;}
    bool Down() const{return down;}
    Out Step(bool off,bool item,bool trigger,uint64_t now) {
        Out o;
        if(!down&&trigger&&!was&&off&&item){down=true;at=now;o.press=true;}
        else if(down&&((!trigger&&now-at>=MinMs)||now-at>=HoldMs)){down=false;check=now+CheckMs;o.release=true;}
        if(check&&now>=check){check=0;o.check=true;}
        was=trigger;
        return o;
    }
};
}
