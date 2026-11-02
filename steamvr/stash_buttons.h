#pragma once
#include "respec_button.h"
#include "menu_controls.h"

namespace framevr::stashbuttons {
// Bank is the game's stash layout; inventory alone also appears at merchants.
template<class Names> inline bool Shown(const Names& open) {
    bool stash=false;
    for(const auto& name:open) {
        const std::string_view n(name);
        if(n.starts_with("HoradricCube")||n.starts_with("Vendor")||
           n.starts_with("Trade")||n.starts_with("Gamble"))return false;
        stash=stash||n.starts_with("Bank")||n.starts_with("Stash");
    }
    return stash;
}
constexpr int Width=240,Height=64;
inline constexpr const wchar_t* Labels[]={L"< Previous tab",L"Next tab >"};
// Inside the stash frame, flanking the gold field below the grid. Measured
// from the owner's placement mockup against the previous buttons' known band
// coordinates (2026-11-02). Keep the gold and page-number fields uncovered.
// The 16:9 band is shared by the rendered plaques and their pointer hit boxes.
constexpr float ButtonHeight=.042f,CenterY=.708f;
constexpr float PreviousX=.215f,NextX=.407f;
inline respecbutton::Spot Place(float width,float height,int direction) {
    if(!(width>0)||!(height>0))return {};
    const float band=std::min(height,width*9.f/16.f),bandWidth=band*16.f/9.f;
    const float h=ButtonHeight*band;
    return {((direction?NextX:PreviousX)-.5f)*bandWidth,
        height*.5f-CenterY*band,h*Width/Height,h};
}
// One fresh pull owns one direction until release, even if the ray crosses
// to the other button. Cancel immediately when the stash/focus is lost.
class Input {
    statsbutton::Press press;
    int selected=-1;
    uint64_t pulseUntil=0;
public:
    void Reset(){press.Reset();selected=-1;pulseUntil=0;}
    uint16_t Step(bool active,int hover,bool trigger,uint64_t now) {
        if(!active){Reset();return 0;}
        if(press.Step(hover>=0,trigger,now)) {
            selected=hover;
            pulseUntil=now+statsbutton::MinMs;
        }
        // A short pulse lets the game poll the bumper without auto-repeat.
        return press.Held()&&now<pulseUntil?(selected==0?menus::LeftBumper:menus::RightBumper):0;
    }
    bool Held() const{return press.Held();}
};
}
