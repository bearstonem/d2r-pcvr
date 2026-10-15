#pragma once
#include <string_view>
#include "frame_math.h"

namespace framevr::panels {
inline constexpr char TabbedMenu[]="UISwitcher";
// Shops and conversations use the upper widescreen content band. Keep this
// independent of opacity: conversations retain the stereo world behind them.
inline bool UpperBand(std::string_view name) {
    return name==TabbedMenu||name.starts_with("Vendor")||
        name.starts_with("Trade")||name.starts_with("Gamble")||
        name=="NpcDialogPanel"||name=="ScrollSpeechPanel";
}
// These management and waypoint canvases share one opaque image against black in XR.
// VendorPanelLayout also hosts NPC gambling; keep speech/quest panels spatial.
inline bool OpaqueBackground(std::string_view name) {
    return name==TabbedMenu||name.starts_with("Waypoint")||name.starts_with("Vendor")||
        name.starts_with("Trade")||name.starts_with("Gamble")||
        name.starts_with("Bank")||name.starts_with("Stash")||
        name.starts_with("PlayerInventory")||name.starts_with("HoradricCube")||
        name.starts_with("HirelingInventory")||name.starts_with("MercenaryInventory");
}
// Spatial conversations/journals retain a live stereo world behind the UI.
// Keep its sky, fog and cave ceiling; opaque management surfaces and the
// flat-menu fallback keep their existing suppression of world effects.
inline bool BlockWorldEffects(bool menuOpen,bool spatialMenuReady,bool opaqueMenuOpen) {
    return menuOpen&&(!spatialMenuReady||opaqueMenuOpen);
}
// Persistent HUD roots are not modal. Speech can outlive NpcDialogPanel.
inline bool Modal(std::string_view name) {
    if(name==TabbedMenu||name=="ControllerOverlay")return true;
    if(name.starts_with("HUD")||name.starts_with("MiniMenu"))return false;
    for(auto key:{"Vendor","PlayerInventory","Bank","Stash","HoradricCube","CharacterStats",
        "SkillsTree","SkillSelect","QuestLog","Pause","Waypoint","Npc","NPC","Trade","Gamble",
        "HirelingInventory","MercenaryInventory","Options","ScrollSpeech","ScrollOfInifuss",
        "GoldTransfer","Quantity","Confirmation"})
        if(name.find(key)!=name.npos)return true;
    return false;
}

// One stable canvas preserves native relationships between inventory, shop,
// comparison tooltips, dragged icons and confirmation dialogs. Empty pixels
// use the canvas's background policy. Do not recrop when hovering: that would
// move the target.
constexpr float Width=1.9f,Distance=.55f,MainDistance=.70f;
// Medium restores 30 cm of breathing room; Close retains the prior build.
inline float MenuDistance(int preset,bool main=false) {
    return (main?MainDistance:Distance)+.30f*std::clamp(preset,0,2);
}
struct Size {float width,height;};
struct Placement {math::Vec position;math::Quat orientation;};
// A valid but untracked startup pose may still be at the reference-space origin.
// Never latch that pose into a persistent menu. Once placed, keep it world-fixed.
inline bool CanAnchor(bool placed,bool rendering,bool focused,bool tracked) {
    return !placed&&rendering&&focused&&tracked;
}
inline Placement AtEyeLevel(math::Vec head,math::Quat look,float distance) {
    const auto yaw=math::Yaw(math::Heading(look));
    return {head+math::Rotate(yaw,{0,0,-distance}),yaw};
}
// Fit the entire native canvas inside the original widescreen-sized workspace.
// Square/portrait eye targets must not turn menus into head-height billboards.
// Uniform scaling preserves artwork and every native pixel (including tooltips).
// The compositor and laser must use the same dimensions.
inline Size Fit(unsigned width,unsigned height,float maxWidth=Width) {
    const float maxHeight=maxWidth*9.f/16.f;
    if(!width||!height)return {maxWidth,maxHeight};
    const float aspect=float(width)/height;
    if(aspect<16.f/9.f)return {maxHeight*aspect,maxHeight};
    return {maxWidth,maxWidth/aspect};
}
// UISwitcher and merchant panes occupy the upper widescreen band of a taller canvas.
// Center that band on the placement anchor, retaining the lower canvas for
// HUD hints, tooltips and drags. Other native layouts use the canvas center.
inline float UpperBandOffset(Size canvas) {
    return -.5f*std::max(0.f,canvas.height-canvas.width*9.f/16.f);
}

// Require a fresh press on the surface; never drop a drag just because a ray
// briefly leaves it. Focus loss releases injected buttons and requires rearm.
struct MouseGate {
    bool held[2]{},down[2]{};
    struct Edges {bool press[2]{},release[2]{};};
    Edges Step(bool active,bool hit,bool left,bool right) {
        const bool pressed[]={left,right};Edges e;
        for(int i=0;i<2;++i) {
            e.release[i]=down[i]&&(!active||!pressed[i]);
            e.press[i]=active&&hit&&pressed[i]&&!held[i];
            if(e.release[i])down[i]=false;
            if(e.press[i])down[i]=true;
            held[i]=pressed[i];
        }
        return e;
    }
    void Suspend(){held[0]=held[1]=true;down[0]=down[1]=false;}
};
struct ScrollGate {
    double next=0;int direction=0;
    int Step(bool active,float y,double now) {
        const int d=active&&std::isfinite(y)?(y>.6f?1:y<-.6f?-1:0):0;
        if(!d){direction=0;next=0;return 0;}
        if(d!=direction){direction=d;next=now+.4;return d;}
        if(now<next)return 0;
        next=now+.12;return d;
    }
};
}
