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
// A grabbed panel, kept in its default placement's own frame so it reopens in
// the same spot relative to where you face. Yaw then pitch: it never rolls.
struct Offset {math::Vec position{};float yaw=0,pitch=0;};
// A grabbed menu may sit a little inside and beyond the presets (.55-1.30 m).
constexpr float NearDistance=.40f,FarDistance=1.50f,MaxTilt=1.0471976f;
inline math::Quat Pitch(float angle) {return {std::sin(angle*.5f),0,0,std::cos(angle*.5f)};}
inline Placement Apply(Placement base,Offset o) {
    return {base.position+math::Rotate(base.orientation,o.position),
        math::Multiply(base.orientation,math::Multiply(math::Yaw(o.yaw),Pitch(o.pitch)))};
}
// Moves only the offset's position, along the line from the head, so a panel
// can never be pushed out of reach or into the face.
inline Offset Limit(Placement base,Offset o,math::Vec head) {
    const auto v=Apply(base,o).position-head;
    const float n=std::sqrt(math::Dot(v,v)),d=std::clamp(n,NearDistance,FarDistance);
    if(n==d)return o;
    const auto direction=n>1e-4f?v*(1/n):math::Rotate(base.orientation,{0,0,-1});
    o.position=math::Rotate(math::Conjugate(base.orientation),head+direction*d-base.position);
    return o;
}
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
// Right grip on the laser's panel grabs it like a window: it follows the
// hand's room-space travel 1:1, and the hand's heading/pitch changes turn it
// about the hand; hand roll is ignored. Grip-then-trigger stays native Shift-click: moving needs 3 cm of
// travel before any click, and a click first leaves the grip as Shift. Once
// moving, the panel catches up with the travel since the press. A trigger
// squeezed while moving is consumed until released.
// 2026-10-19: the game's own menus have black backgrounds, so they only
// push/pull: pass their default distance and only travel along the facing
// axis counts, keeping that distance within the limits. 0 grabs freely.
struct Grab {
    enum class State {Idle,Armed,Moving,Held,Spent} state=State::Idle;
    Placement from{};Offset start{};
    static constexpr float Slack=.03f;
    static float HandPitch(math::Quat q){return std::asin(std::clamp(math::Rotate(q,{0,0,-1}).y,-1.f,1.f));}
    bool Step(bool grip,bool hit,bool click,Placement hand,Placement base,math::Vec head,Offset& offset,float pushOnly=0) {
        auto travel=math::Rotate(math::Conjugate(base.orientation),hand.position-from.position);
        if(pushOnly>0)travel={0,0,travel.z};
        switch(state) {
        case State::Idle:if(grip){state=hit&&!click?State::Armed:State::Spent;from=hand;start=offset;}break;
        case State::Armed:
            if(!grip)state=State::Idle;
            else if(click)state=State::Spent;
            else if(math::Dot(travel,travel)>Slack*Slack)state=State::Moving;
            if(state!=State::Moving)break;
            [[fallthrough]];
        case State::Moving:
            if(!grip){state=click?State::Held:State::Idle;break;}
            if(pushOnly>0){
                const float distance=std::clamp(pushOnly-start.position.z-travel.z,NearDistance,FarDistance);
                offset={{0,0,pushOnly-distance},0,0};break;
            }
            {
                // 2026-10-19: turning the panel about its own centre made an
                // arm sweep (which turns the controller too) swing it around a
                // point in front of it. The grabbed point now stays fixed in the
                // hand's roll-free frame, like holding a real window.
                const float heading=math::Heading(from.orientation),pitch=HandPitch(from.orientation);
                const float yaw=math::Heading(hand.orientation)-heading;
                offset.pitch=std::clamp(start.pitch+HandPitch(hand.orientation)-pitch,-MaxTilt,MaxTilt);
                const auto before=math::Multiply(math::Yaw(heading),Pitch(pitch));
                const auto after=math::Multiply(math::Yaw(heading+yaw),Pitch(pitch+offset.pitch-start.pitch));
                const auto held=math::Rotate(math::Conjugate(before),Apply(base,start).position-from.position);
                offset.position=math::Rotate(math::Conjugate(base.orientation),hand.position+math::Rotate(after,held)-base.position);
                offset.yaw=std::remainder(start.yaw+yaw,6.2831853f);
                offset=Limit(base,offset,head);
            }
            break;
        case State::Held:if(!click)state=grip?State::Spent:State::Idle;break;
        case State::Spent:if(!grip)state=State::Idle;break;
        }
        return state==State::Moving||state==State::Held;
    }
    void Suspend(){state=State::Spent;}
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
