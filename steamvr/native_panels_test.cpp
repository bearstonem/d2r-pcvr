#include "native_panels.h"
#include "npc_dialogue.h"
#include "panel_input_mode.h"
#include "menu_controls.h"
#include "quick_cast.h"
#include "journal.h"
#include "input.h"
#include "vr_resolution.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static void Check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    for(int preset=0;preset<3;++preset)for(bool main:{false,true}) {
        const float distance=panels::MenuDistance(preset,main);
        Check(std::fabs(distance-(main?.70f:.55f)-.30f*preset)<.00001f,"distance presets add 30 cm without changing canvas scale");
        const auto p=panels::AtEyeLevel({0,1.7f,0},math::Yaw(.8f),distance);
        Check(p.position.y==1.7f,"all distance presets retain eye-level anchor");
    }
    Check(panels::NearDistance<panels::MenuDistance(0)&&panels::FarDistance>panels::MenuDistance(2,true),
        "grab limits reach past every preset");
    {
        const math::Vec head{0,1.6f,0};
        const auto base=panels::AtEyeLevel(head,math::Yaw(.5f),panels::MenuDistance(1));
        auto same=[](math::Vec a,math::Vec b){const auto d=a-b;return math::Dot(d,d)<1e-9f;};
        auto len=[](math::Vec a){return std::sqrt(math::Dot(a,a));};
        auto at=[&](math::Vec p,math::Quat q=math::Yaw(1)){return panels::Placement{math::Vec{.2f,1.2f,-.3f}+p,q};};
        auto where=[&](panels::Offset o){return panels::Apply(base,o);};
        const math::Quat roll{0,0,std::sin(.35f),std::cos(.35f)};
        Check(same(where({}).position,base.position),"no grab keeps the default placement");
        panels::Grab grab;panels::Offset o;
        Check(!grab.Step(true,false,false,at({}),base,head,o)&&!grab.Step(true,true,false,at({.2f,0,0}),base,head,o)&&
              same(where(o).position,base.position),"grip started off the panel never grabs");
        grab.Step(false,true,false,at({}),base,head,o);
        Check(!grab.Step(true,true,false,at({}),base,head,o)&&!grab.Step(true,true,false,at({.014f,.014f,0}),base,head,o)&&
              same(where(o).position,base.position),"slack keeps a still grip as Shift");
        Check(grab.Step(true,true,false,at({.02f,.02f,.02f}),base,head,o)&&same(where(o).position,base.position+math::Vec{.02f,.02f,.02f}),
              "once moving, the panel catches up with all travel since the press");
        const math::Vec travel{.1f,.05f,-.1f};
        Check(grab.Step(true,false,false,at(travel),base,head,o)&&same(where(o).position,base.position+travel),
              "diagonal travel moves the panel 1:1 in room space, even off the panel");
        Check(std::fabs(o.yaw)<1e-6f&&std::fabs(o.pitch)<1e-6f,"translation alone does not tilt");
        for(const math::Vec slide:{math::Vec{.6f,0,0},math::Vec{-.6f,0,0},math::Vec{0,.45f,0},math::Vec{0,-.45f,0},math::Vec{.5f,.3f,0}}){
            const auto moved=travel+slide;
            Check(grab.Step(true,true,false,at(moved),base,head,o)&&same(where(o).position,base.position+moved)&&
                  std::fabs(o.yaw)<1e-6f&&std::fabs(o.pitch)<1e-6f,"sideways and vertical hand travel slides the panel exactly, with no turn");
        }
        grab.Step(true,true,false,at(travel),base,head,o);
        const auto tilted=math::Multiply(math::Yaw(1.3f),panels::Pitch(.2f));
        grab.Step(true,true,false,at(travel,tilted),base,head,o);
        Check(std::fabs(o.yaw-.3f)<1e-4f&&std::fabs(o.pitch-.2f)<1e-4f,"heading and pitch follow the hand's change");
        const auto before=o;
        grab.Step(true,true,false,at(travel,math::Multiply(tilted,roll)),base,head,o);
        Check(std::fabs(o.yaw-before.yaw)<1e-4f&&std::fabs(o.pitch-before.pitch)<1e-4f,"hand roll never rolls the panel");
        Check(std::fabs(math::Rotate(where(o).orientation,{1,0,0}).y)<1e-5f,"panel's horizontal edge stays level");
        grab.Step(true,true,false,at(travel,math::Multiply(math::Yaw(1),panels::Pitch(1.4f))),base,head,o);
        Check(o.pitch==panels::MaxTilt,"tilt up is clamped");
        grab.Step(true,true,false,at(travel,math::Multiply(math::Yaw(1),panels::Pitch(-1.4f))),base,head,o);
        Check(o.pitch==-panels::MaxTilt,"tilt down is clamped");
        const auto forward=math::Rotate(base.orientation,{0,0,-1});
        grab.Step(true,true,false,at(forward*3),base,head,o);
        Check(std::fabs(len(where(o).position-head)-panels::FarDistance)<1e-4f,"far limit from the head");
        grab.Step(true,true,false,at(forward*-.6f),base,head,o);
        Check(std::fabs(len(where(o).position-head)-panels::NearDistance)<1e-4f,"near limit from the head");
        grab.Step(true,true,false,at(travel),base,head,o);
        Check(grab.Step(true,true,true,at(travel),base,head,o),"a trigger squeezed while moving is consumed");
        const auto kept=o;
        Check(grab.Step(false,true,true,at({}),base,head,o)&&same(o.position,kept.position),"release keeps the place; held trigger stays consumed");
        Check(!grab.Step(false,true,false,at({}),base,head,o),"trigger release ends the grab");
        const auto reopened=panels::AtEyeLevel({1,1.5f,2},math::Yaw(-1),panels::MenuDistance(1));
        Check(same(math::Rotate(math::Conjugate(reopened.orientation),panels::Apply(reopened,o).position-reopened.position),o.position),
              "a reopened menu keeps its place relative to where you face");
        o={};grab.Step(true,true,false,at({}),base,head,o);
        Check(!grab.Step(true,true,true,at({}),base,head,o)&&!grab.Step(true,true,false,at({.3f,0,0}),base,head,o)&&
              same(where(o).position,base.position),"grip then click stays Shift-click for the whole hold");
        grab.Step(false,true,false,at({}),base,head,o);
        Check(!grab.Step(true,true,true,at({}),base,head,o)&&!grab.Step(true,true,true,at({.3f,0,0}),base,head,o)&&
              same(where(o).position,base.position),"grip during a drag or held click never grabs");
        grab.Step(false,true,false,at({}),base,head,o);grab.Step(true,true,false,at({}),base,head,o);grab.Suspend();
        Check(!grab.Step(true,true,false,at({.3f,0,0}),base,head,o)&&same(where(o).position,base.position),"a surface change needs a fresh grip");
        grab.Step(false,true,false,at({}),base,head,o);grab.Step(true,true,false,at({}),base,head,o);
        Check(grab.Step(true,true,false,at({.1f,0,0}),base,head,o),"fresh grip after suspension grabs again");
        {
            panels::Grab hold;panels::Offset g;const auto press=at({});
            hold.Step(true,true,false,press,base,head,g);
            const auto turned=at({.04f,0,0},math::Yaw(1.3f));
            Check(hold.Step(true,true,false,turned,base,head,g)&&
                  same(where(g).position,turned.position+math::Rotate(math::Yaw(.3f),base.position-press.position))&&std::fabs(g.yaw-.3f)<1e-5f,
                  "turning the hand swings the panel about the hand, keeping the grabbed point on the laser");
        }
        // Game menus: push/pull only.
        const float d0=panels::MenuDistance(1);
        auto distance=[&](panels::Offset p){return len(where(p).position-head);};
        const auto right=math::Rotate(base.orientation,{1,0,0});
        panels::Grab push;o={};
        Check(!push.Step(true,true,false,at({}),base,head,o,d0)&&!push.Step(true,true,false,at(right*.2f+math::Vec{0,.2f,0}),base,head,o,d0)&&
              same(where(o).position,base.position),"sideways or vertical travel never moves a game menu or ends Shift");
        Check(push.Step(true,true,false,at(right*.2f+math::Vec{0,.2f,0}+forward*.04f),base,head,o,d0)&&std::fabs(distance(o)-(d0+.04f))<1e-4f,
              "push catches up with all travel along the axis since the press");
        push.Step(true,true,false,at(right*-.3f+forward*.25f,tilted),base,head,o,d0);
        Check(std::fabs(distance(o)-(d0+.25f))<1e-4f&&o.position.x==0&&o.position.y==0&&o.yaw==0&&o.pitch==0,
              "diagonal travel pushes by its depth only and never tilts");
        push.Step(true,true,false,at(forward*-.2f),base,head,o,d0);
        Check(std::fabs(distance(o)-(d0-.2f))<1e-4f,"pull brings it nearer 1:1");
        push.Step(true,true,false,at(forward*3),base,head,o,d0);
        Check(std::fabs(distance(o)-panels::FarDistance)<1e-4f,"push stops at the far limit");
        push.Step(true,true,false,at(forward*-3),base,head,o,d0);
        Check(std::fabs(distance(o)-panels::NearDistance)<1e-4f,"pull stops at the near limit, still in front");
        push.Step(false,true,false,at({}),base,head,o,d0);
        const auto title=panels::AtEyeLevel(head,math::Yaw(.5f),panels::MenuDistance(1,true));
        Check(std::fabs(len(panels::Apply(title,o).position-head)-(panels::MenuDistance(1,true)-d0+panels::NearDistance))<1e-4f,
              "the same push/pull carries to the title menu");
        Check(!push.Step(true,true,false,at({}),base,head,o,d0)&&!push.Step(true,true,true,at({}),base,head,o,d0)&&
              !push.Step(true,true,false,at(forward*.3f),base,head,o,d0),"grip then click stays Shift-click");
    }
    panels::TooltipDwell dwell;
    Check(dwell.Ready(1,2,100,0,1),"instant tooltip on first hover");
    Check(!dwell.Ready(1,2,110,1,1),"short mode starts fresh on setting change");
    Check(!dwell.Ready(1,2,210,1,1),"short dwell not yet elapsed");
    Check(!dwell.Ready(3,0,211,1,1),"empty neighboring grid cannot reset hovered item dwell");
    Check(!dwell.Ready(1,2,310,1,1)&&dwell.Ready(1,2,410,1,1),"same item across pixels reaches 300 ms");
    Check(!dwell.Ready(1,4,420,1,1),"different item resets dwell");
    Check(!dwell.Ready(1,4,430,1,2),"ray miss or drag generation resets dwell");
    Check(!dwell.Ready(1,4,1000,1,2),"stalled frame cannot mature a stale hover");
    Check(!dwell.Ready(1,4,1,1,2),"clock reset restarts dwell");
    Check(dwell.Ready(1,4,2,2,2),"Original mode delegates delay to native producer");
    Check(!panels::ItemCursorSnap(true,true,true),"item draw uses laser coordinates outside pointer callbacks");
    Check(panels::ItemCursorSnap(false,true,true)&&panels::ItemCursorSnap(true,false,true),"desktop and unrelated cursor queries remain native");
    Check(panels::CursorBoundsCaller(0x7f4b2)&&panels::CursorBoundsCaller(0x14fb2a),
        "native height and tail-called width clamp use full-source bounds");
    for(auto address:panels::CursorConversionReturns)
        Check(panels::CursorBoundsCaller(address)&&!panels::UiCanvasCaller(address),
            "the cursor's window/game conversions use full-source bounds, never the UI band (X stopped at 2100 of 3072)");
    for(auto address:panels::CursorClampReturns)
        Check(panels::CursorBoundsCaller(address)&&!panels::UiCanvasCaller(address),"the controller cursor's clamp uses full-source bounds");
    Check(std::size(panels::CursorConversionReturns)==7&&std::size(panels::CursorClampReturns)==2,"every audited cursor size query is listed");
    for(auto address:panels::UiCanvasReturns)
        Check(!panels::CursorBoundsCaller(address),"cursor bounds cannot enlarge native font/layout canvas");
    for(auto address:{uintptr_t(0),uintptr_t(0x14fb46),uintptr_t(0x667ff1),uintptr_t(0x904b3d)})
        Check(!panels::CursorBoundsCaller(address),"unrelated and approximate callers retain original size");
    Check(panels::UiCanvasCaller(0x843ed5)&&panels::UiCanvasCaller(0x8453a9)&&
        panels::UiCanvasCaller(0x846239)&&panels::UiCanvasCaller(0x14f31d),"native UI layout and cursor queries use VR canvas");
    for(auto caller:{uintptr_t(0),uintptr_t(0x904b3d),uintptr_t(0x667ff1),uintptr_t(0x668003),uintptr_t(0x846238)})
        Check(!panels::UiCanvasCaller(caller),"movies, engine queries and unknown return sites stay native");
    {
        bool anchored=false;panels::Placement menu{};
        auto frame=[&](bool rendering,bool focused,bool tracked,math::Vec head) {
            if(panels::CanAnchor(anchored,rendering,focused,tracked)) {
                menu=panels::AtEyeLevel(head,math::Yaw(.4f),panels::MainDistance);
                anchored=true;
            }
        };
        frame(true,false,false,{0,0,0});
        frame(true,true,false,{0,0,0});
        frame(true,false,true,{0,0,0});
        frame(false,true,true,{0,0,0});
        Check(!anchored,"startup waits for rendering, focus and tracked position/orientation");
        frame(true,true,true,{0,1.7f,0});
        Check(anchored&&menu.position.y==1.7f,"first reliable pose anchors menu at standing eye height");
        const auto fixed=menu.position;
        frame(true,true,true,{.2f,1.5f,.1f});
        Check(menu.position.x==fixed.x&&menu.position.y==fixed.y&&menu.position.z==fixed.z,"head movement does not drag an open menu");
        anchored=false; // Focus return or explicit recenter invalidates placement.
        frame(true,true,false,{0,0,0});
        Check(!anchored,"reconnect cannot anchor from an untracked origin");
        frame(true,true,true,{0,1.2f,0});
        Check(anchored&&menu.position.y==1.2f,"reacquired tracking supports seated eye height");
    }
    for(auto name:{"WaypointPanel","WaypointPanelExpansion","WaypointPanelOriginal"})
        Check(panels::Modal(name)&&panels::OpaqueBackground(name),"waypoint destinations use the shared opaque black background instead of different per-eye worlds");
    for(auto name:{"UISwitcher","VendorPanelLayout","VendorPanelOriginalLayout","TradeLayout","GamblePanel",
        "BankExpansionLayout","BankOriginalLayout","StashPanel","PlayerInventoryExpansionLayout",
        "PlayerInventoryOriginalLayout","HoradricCubeLayout","HirelingInventoryPanel","MercenaryInventoryLayout"})
        Check(panels::Modal(name)&&panels::OpaqueBackground(name),"management surfaces use the same opaque background in both eyes");
    for(auto name:{"NpcDialogPanel","ScrollSpeechPanel","QuestLogPanelExpansion","GoldTransferPanel","QuantityPanel","HUDPanel","HUDTradeHint","MiniMenuPanel","TooltipsPanel"})
        Check(!panels::OpaqueBackground(name),"dialogue, journal and auxiliary panels do not independently black out the world");
    for(auto name:{"NpcDialogPanel","ScrollSpeechPanel","QuestLogSpeechPanel","QuestLogPanelExpansion","ConfirmationPanel"}) {
        Check(!panels::BlockWorldEffects(panels::Modal(name),true,panels::OpaqueBackground(name)),
            "spatial conversations, speech and journals keep sky, fog and cave ceiling behind their transparent UI");
        Check(panels::BlockWorldEffects(panels::Modal(name),false,panels::OpaqueBackground(name)),
            "unavailable spatial UI retains flat-menu suppression of world effects");
    }
    for(auto name:{"VendorPanelLayout","GamblePanel","WaypointPanel","UISwitcher","BankExpansionLayout"})
        Check(panels::BlockWorldEffects(panels::Modal(name),true,panels::OpaqueBackground(name)),
            "opaque management menus keep their black background and suppress world effects");
    Check(!panels::BlockWorldEffects(false,false,false),"ordinary gameplay retains world effects without spatial UI");
    Check(!panels::BlockWorldEffects(false,true,true),"a stale opaque flag alone cannot disable gameplay effects");
    // An NPC conversation may open a merchant without closing all of its
    // other roots. Any remaining opaque root owns the backdrop until closed.
    for(bool merchant:{false,true,false})
        Check(panels::BlockWorldEffects(true,true,panels::OpaqueBackground("NpcDialogPanel")||
            (merchant&&panels::OpaqueBackground("VendorPanelLayout")))==merchant,
            "dialogue to merchant to dialogue restores world effects when the opaque root closes");
    for(auto name:{"UISwitcher","VendorPanelLayout","VendorPanelOriginalLayout","TradeLayout","GamblePanel","NpcDialogPanel","ScrollSpeechPanel"})
        Check(panels::UpperBand(name),"inventory, merchants and both conversation roots center the same upper content band");
    for(auto name:{"WaypointPanel","QuestLogPanelExpansion","QuantityPanel","ConfirmationPanel","HUDTradeHint","TooltipsPanel"})
        Check(!panels::UpperBand(name),"unrelated layouts and auxiliary popups cannot independently shift the workspace");
    panels::InputModeLease mode;
    Check(!mode.PreserveController(100,true),"desktop input untouched without VR ownership");
    mode.Refresh(100);
    Check(mode.PreserveController(100,true),"guard armed before queued cursor movement");
    Check(!mode.PreserveController(101,false),"do not silently force a mouse layout into controller mode");
    Check(mode.PreserveController(399,true),"queued click and release retain controller mode");
    Check(!mode.PreserveController(400,true),"tracking/focus loss expires guard without another frame");
    mode.Refresh(500);mode.Refresh(750);
    Check(mode.PreserveController(1049,true)&&!mode.PreserveController(1050,true),"active pointer renews bounded ownership");
    Check(panels::FocusUsesPointer(mode.Active(800),true),"laser hover keeps exact widget focus");
    Check(!panels::FocusUsesPointer(mode.Active(800),false),"native navigation restores cursor snapping despite queued mouse-release lease");
    Check(mode.Active(800),"switching focus does not cancel protection for queued pointer events");
    panels::ItemTooltipHover hover;
    Check(!hover.Active(100),"desktop starts without immediate tooltip or automatic comparison ownership");
    hover.Update(100,true,true,false);
    Check(hover.Active(100),"first laser hit enables tooltips without a dwell timer");
    int keyReads=0;
    auto key=[&](int k){++keyReads;return k==0x11?1:0;};
    Check(panels::ItemCompareKey(hover.Active(100),true,0x10,key)==1&&keyReads==0,
        "native equipped-item comparison needs no physical Shift press");
    Check(!panels::ItemCompareKey(hover.Active(100),false,0x10,key)&&keyReads==1,
        "purchase, transfer and gameplay Shift queries retain native state");
    Check(panels::ItemCompareKey(hover.Active(100),true,0x11,key)==1&&keyReads==2,
        "other held modifiers retain native state even inside tooltip code");
    Check(hover.Active(399)&&!hover.Active(400),"stopped XR frames expire tooltip ownership");
    for(int lost=0;lost<3;++lost) {
        hover.Update(500,true,true,false);
        hover.Update(501,lost!=0,lost!=1,lost==2);
        Check(!hover.Active(501),"focus/tracking/navigation loss, ray miss or drag clears item hover immediately");
        Check(!panels::ItemCompareKey(hover.Active(501),true,0x10,key),"inactive laser cannot force native comparison");
        Check(panels::ItemCompareKey(hover.Active(501),true,0x10,[](int){return 1;})==1,
            "real Shift still compares items with automatic comparison inactive");
    }
    hover.Update(600,true,true,false);
    Check(hover.Active(600),"reacquiring item hover is immediate after a miss or drag");
    // Reproduce a laser-started Amazon session: physical bow/javelin releases
    // and walking send no native A, so KB/M remains active and target lookup
    // never gives the semantic interact guard a usable item/entrance target.
    panels::WorldInputMode world;
    panels::InputModeLease pointerLease;
    Check(!world.NeedsController(100,true,false,false),"desktop session never requests a world layout change");
    for(uint64_t now=100;now<1000;now+=20) {
        world.Update(now,true);
        Check(world.NeedsController(now,true,false,false),"tracked physical combat can recover controller targeting without pressing attack");
        Check(!world.NeedsController(now,true,false,true),"committed controller layout needs no further transition");
        Check(!pointerLease.Active(now),"world ownership never blocks walking or claims UI pointer dispatch");
    }
    Check(!world.NeedsController(1000,false,false,false),"character loading and teardown cannot request a native world transition");
    Check(!world.NeedsController(1000,true,true,false),"open native menus retain their input ownership");
    Check(world.NeedsController(1279,true,false,false)&&!world.NeedsController(1280,true,false,false),"missing XR samples expire world ownership without another input frame");
    world.Update(1300,true);world.Update(1301,false);
    Check(!world.NeedsController(1301,true,false,false),"tracking loss, settings and inactive gameplay release ownership immediately");
    world.Update(1400,true);
    Check(world.NeedsController(1400,true,false,false),"returning to tracked world gameplay recovers targeting after menus or transitions");
    Check(panels::MessageHash("InputMessage")==0x4ae067c6248b6042ull&&
          panels::MessageHash("LeftMouseButtonDown")==0xbdf8daff2e44666full,"native pointer message hashes");
    for(auto command:{"MouseMove","MouseScroll","LeftMouseButtonDown","LeftMouseButtonUp","RightMouseButtonDown","RightMouseButtonUp"})
        Check(panels::PointerMessage(panels::MessageHash("InputMessage"),panels::MessageHash(command)),"all laser event kinds use pointer widget behavior");
    Check(!panels::PointerMessage(panels::MessageHash("PanelManager"),panels::MessageHash("OpenPanel")),"panel changes use actual controller layout");
    bool pointer=false;int calls=0;
    Check(panels::RoutePointer(true,pointer,[&]{
        ++calls;Check(pointer,"native UI receives pointer behavior");
        {panels::PointerScope nested(pointer,false);Check(!pointer,"nested panel open retains controller mode");}
        Check(pointer,"nested dispatch restores outer pointer behavior");
        return false; // A blank panel pixel: no widget handled this click.
    }),"UI miss is consumed before world input");
    Check(calls==1&&!pointer,"native pointer callback runs once and restores mode");
    Check(panels::RoutePointer(true,pointer,[&]{return true;}),"handled UI event remains consumed");
    Check(!panels::RoutePointer(false,pointer,[&]{Check(!pointer,"desktop event retains native mode");return false;}),"desktop miss is not swallowed");
    Check(panels::RoutePointer(false,pointer,[&]{return true;}),"desktop handled result preserved");
    Check(!panels::RoutePointer(mode.Active(1050),pointer,[&]{Check(!pointer,"expired ownership restores normal dispatch");return false;}),"ownership expiry stops consuming events");
    // Recorded vendor failure: MouseMove finds a Yes/No row, then the separate
    // controller-cursor pass replaces it with the grid beneath the popup.
    enum Hover {None,Yes,No,VendorGrid};
    auto nativeFocus=[&](Hover hit,bool nativeSnap,bool binding=false){
        Hover hover=hit;
        if(nativeSnap&&panels::CursorSnapAllowed(pointer,binding))hover=VendorGrid;
        return hover;
    };
    Check(nativeFocus(Yes,true)==VendorGrid,"fixture reproduces native cursor snap stealing popup focus");
    for(Hover row:{Yes,No}) {
        panels::RoutePointer(true,pointer,[&]{
            Check(nativeFocus(row,true)==row,"laser keeps exact Yes/No row through native focus update");
            Check(nativeFocus(None,true)==None,"laser miss cannot snap to a nearby purchase or grid");
            Check(nativeFocus(row,true,true)==VendorGrid,"explicit controller assignment keeps native snapping");
            {panels::PointerScope nested(pointer,false);
                Check(nativeFocus(row,true)==VendorGrid,"panel layout message keeps native cursor behavior");}
            Check(nativeFocus(row,true)==row,"nested message restores laser focus policy");
            return true;
        });
        Check(nativeFocus(row,true)==VendorGrid,"controller snapping restored after pointer callback");
        Check(nativeFocus(row,false)==row,"native disabled snapping is never enabled");
    }
    for(auto name:{"UISwitcher","ControllerOverlay","PlayerInventoryExpansionLayout","VendorPanelLayout","BankExpansionLayout","NpcDialogPanel","ScrollSpeechPanel","QuestLogSpeechPanel","HoradricCubeLayout","GoldTransferPanel"})
        Check(panels::Modal(name),"native management/dialog root routed as modal");
    for(auto name:{"HUDPanel","HUDMonsterHealth","MiniMenuPanel","TooltipsPanel","ShowItemsPanel","PanelBordersPanel"})
        Check(!panels::Modal(name),"persistent HUD must not open VR workspace");
    panels::MouseGate mouse;
    mouse.Step(false,false,true,false);
    Check(!mouse.Step(true,true,true,false).press[0],"held attack cannot pick a dialog option");
    mouse.Step(true,true,false,false);
    Check(mouse.Step(true,true,true,false).press[0],"fresh trigger begins item drag");
    Check(!mouse.Step(true,false,true,false).release[0]&&mouse.down[0],"ray miss keeps item held");
    Check(mouse.Step(true,false,false,false).release[0],"physical release ends drag at last valid pixel");
    Check(mouse.Step(true,true,false,true).press[1],"fresh secondary pointer press");
    Check(mouse.Step(false,true,false,true).release[1],"lost focus releases context click");
    Check(!mouse.Step(true,true,false,true).press[1],"focus return requires new context press");
    mouse.Suspend();Check(!mouse.Step(true,true,true,false).press[0],"mode transition consumes held trigger");
    menus::LaserClick laser;
    laser.Step(true,false);Check(laser.Left()&&!laser.Right(),"laser trigger selects and drags");
    laser.Step(true,true);Check(laser.Left()&&!laser.Right(),"grip pressed during drag cannot turn it into a context click");
    laser.Step(false,true);Check(!laser.Left()&&!laser.Right(),"physical release ends either laser button");
    laser.Step(true,true);Check(laser.Right()&&!laser.Left(),"left grip plus fresh trigger supplies pointer context action without native A");
    laser.Step(true,false);Check(laser.Right()&&!laser.Left(),"releasing modifier first cannot start an unintended second click");
    panels::ScrollGate scroll;
    Check(scroll.Step(true,1,0)==1&&scroll.Step(true,1,.2)==0&&scroll.Step(true,1,.4)==1,"scroll delay and repeat");
    Check(scroll.Step(true,-1,.41)==-1&&scroll.Step(false,-1,.6)==0,"scroll direction and focus guard");
    // World -> stash -> world, with the same A/trigger/stick held across both
    // boundaries. New controls remain usable without releasing every grip.
    menus::ReleaseGate input;
    // Quest's system menu button can be reserved by the runtime. The left
    // grip/stick shortcut must not also open a wheel, sort items, or re-fire
    // when the native menu changes epoch while both controls remain held.
    {
        menus::ReleaseGate gate;GripStickButton shortcut;menus::Pad p;
        gate.Step(menus::Mode::World,0,{});
        p.buttons=menus::LeftBumper|menus::LeftStick;
        auto filtered=gate.Step(menus::Mode::World,0,p);
        Check(shortcut.Update(filtered.buttons&menus::LeftStick,1)&&shortcut.Consumed(),"fresh grip/stick opens menu once");
        filtered=gate.Step(menus::Mode::Native,1,p);
        Check(!shortcut.Update(filtered.buttons&menus::LeftStick,0)&&!shortcut.PlainPressed(),"held chord cannot act in newly opened menu");
        p.buttons=menus::LeftStick;filtered=gate.Step(menus::Mode::Native,1,p);
        Check(!shortcut.Update(filtered.buttons&menus::LeftStick,0)&&!shortcut.PlainPressed(),"releasing grip first cannot sort or toggle a wheel");
        gate.Step(menus::Mode::Native,1,{});shortcut.Update(false,0);
        p.buttons=menus::LeftBumper|menus::LeftStick;filtered=gate.Step(menus::Mode::Native,1,p);
        Check(shortcut.Update(filtered.buttons&menus::LeftStick,1),"fresh chord can close the inventory menu");
    }
    menus::Pad held;held.buttons=menus::A|menus::RightBumper;held.rt=255;held.lx=20000;
    input.Step(menus::Mode::World,0,{});
    Check(input.Step(menus::Mode::World,0,held).rt==255,"fresh world attack available");
    auto guarded=input.Step(menus::Mode::Native,1,held);
    Check(!menus::Busy(guarded),"held attack and movement cannot activate newly opened stash");
    held.buttons|=menus::B;
    guarded=input.Step(menus::Mode::Native,1,held);
    Check(guarded.buttons==menus::B,"fresh menu B works while old A/grip remain held");
    Check(!menus::Busy(input.Step(menus::Mode::World,2,held)),"menu close cannot attack, swap, or walk in world");
    input.Step(menus::Mode::World,2,{});
    Check(input.Step(menus::Mode::World,2,held).buttons==held.buttons,"release rearms world controls");
    Check(!menus::Busy(input.Step(menus::Mode::Blocked,2,held)),"opening VR settings consumes held native controls");
    input.Suspend();Check(!menus::Busy(input.Step(menus::Mode::Native,2,held)),"focus/tracking recovery requires release");
    skills::QuickCast quick;
    menus::Pad shortcut;shortcut.lt=255;shortcut.buttons=menus::Y|menus::LeftBumper;
    quick.Capture(true,shortcut.lt/255.f,8);
    Check(!menus::SettingsChord(shortcut,quick.Suppress(3)),"simultaneous LT + grip + Y remains quick-cast slot 8, not VR settings");
    quick.Capture(false,0,0);quick.Capture(false,0,8);
    Check(menus::SettingsChord(shortcut,quick.Suppress(3)),"grip + Y opens VR settings when quick cast does not own it");
    for(auto mode:{menus::Mode::World,menus::Mode::Native,menus::Mode::Blocked}) {
        const auto tag=uint32_t(mode);
        Check(menus::Tagged(tag),"tagged report opts into context validation");
        Check(!menus::Allowed(tag,7,8,true,false)&&!menus::Allowed(tag,7,8,false,false),"open-close-open rejects an older report even when menu state matches");
        Check(!menus::Allowed(tag,7,7,true,true),"mod-owned settings never receive native menu pad");
    }
    Check(menus::Allowed(uint32_t(menus::Mode::Native),7,7,true,false),"current native report reaches menu");
    Check(!menus::Allowed(uint32_t(menus::Mode::Native),7,7,false,false),"native A cannot leak into world before next XR frame");
    Check(!menus::Allowed(uint32_t(menus::Mode::World),7,7,true,false),"stale world attack cannot leak into menu");
    Check(!menus::Tagged(0),"legacy BodyWalk reports retain their format");

    menus::Navigation navigation;
    menus::Pad ui;
    Check(!menus::Busy(navigation.Step(true,{},true,.5f,.5f,false))&&!navigation.Native(),"laser starts without native pad events");
    ui.rt=255;
    Check(!menus::Busy(navigation.Step(true,ui,true,.5f,.5f,false)),"laser trigger does not also send native RT");
    ui.buttons=menus::A|menus::RightBumper;ui.lt=255;
    Check(!menus::Busy(navigation.Step(true,ui,false,0,0,true))&&!navigation.Native(),"drag retains ownership while ray leaves canvas or a face button is touched");
    auto native=navigation.Step(true,ui,true,.5f,.5f,false);
    Check(navigation.Native()&&native.buttons==menus::A&&!native.lt&&!native.rt,"native A has one action and existing laser modifiers are consumed");
    navigation.Step(true,{},true,.5f,.5f,false);
    Check(navigation.Native(),"idle laser does not steal native focus");
    ui={};ui.buttons=menus::LeftBumper|menus::RightBumper;ui.lt=128;ui.rt=255;ui.ly=18000;ui.rx=-18000;
    native=navigation.Step(true,ui,true,.8f,.5f,false);
    Check(navigation.Native()&&native.buttons==ui.buttons&&native.lt==128&&native.rt==255&&native.ly==18000&&native.rx==-18000,"native bumpers, triggers and both sticks pass while navigating");
    navigation.Step(true,{},true,.81f,.51f,false);
    Check(navigation.Native(),"small tracked aim jitter cannot overwrite native focus");
    Check(!menus::Busy(navigation.Step(true,{},true,.9f,.5f,false))&&!navigation.Native(),"deliberate reaim returns to laser without synthetic native action");
    navigation.Step(true,menus::Pad{menus::X},true,.9f,.5f,false);
    Check(navigation.Native(),"native X is available rather than toggling menu closed");
    navigation.Step(false,{},false,0,0,false);
    Check(!navigation.Native(),"focus loss resets navigation ownership");
    menus::Navigation laserOnly;laserOnly.pointer=menus::Pointer::Laser;
    Check(laserOnly.Step(true,menus::Pad{menus::X},true,.5f,.5f,false).buttons==menus::X&&laserOnly.Native(),"Laser: a held face button still reaches the game");
    Check(!menus::Busy(laserOnly.Step(true,{},true,.5f,.5f,false))&&!laserOnly.Native(),"Laser: letting go returns the laser without moving the aim");
    ui={};ui.ly=18000;laserOnly.Step(true,ui,true,.5f,.5f,false);laserOnly.Step(true,{},false,0,0,false);
    Check(!laserOnly.Native(),"Laser: a stick returns to the laser on release too, even off the menu");
    menus::Navigation pad;pad.pointer=menus::Pointer::Controller;
    pad.Step(true,{},true,.5f,.5f,false);
    Check(pad.Native(),"Controller: game menus start on native navigation");
    pad.Step(true,{},true,.9f,.5f,false);
    Check(pad.Native(),"Controller: moving the aim never brings the laser back");
    menus::Navigation automatic;
    automatic.Step(true,menus::Pad{menus::X},true,.5f,.5f,false);automatic.Step(true,{},true,.5f,.5f,false);
    Check(automatic.Native(),"Auto keeps the original behavior: the laser waits for a deliberate aim change");
    menus::ReleaseGate sortGate;menus::Navigation inventory;
    sortGate.Step(menus::Mode::Native,9,{});
    menus::Pad sort;sort.buttons=menus::RightStick;
    for(int frame=0;frame<180;++frame) {
        const auto report=inventory.Step(true,sortGate.Step(menus::Mode::Native,9,sort),true,.5f,.5f,false);
        Check(report.buttons==menus::RightStick,"right-stick hold reaches native sort on every frame, not just a click edge");
        Check(!menus::WorldButtons(sort),"physical right-stick click cannot request a gameplay weapon swap");
    }
    Check(!inventory.Step(true,{},true,.5f,.5f,false).buttons,"releasing right-stick ends native hold-to-sort");
    menus::Pad drop;drop.buttons=menus::X;
    for(int frame=0;frame<90;++frame)
        Check(inventory.Step(true,drop,true,.5f,.5f,false).buttons==menus::X,"X/drop is forwarded for the entire native hold duration");
    Check(!menus::Busy(sortGate.Step(menus::Mode::World,10,sort)),"closing inventory during a sort hold cannot swap in gameplay");
    const auto square=panels::Fit(4096,4096),wide=panels::Fit(3840,2160);
    Check(std::fabs(square.height-1.06875f)<.00001f&&square.width==square.height,"4096 square inventory fits original workspace height without distortion");
    Check(wide.width==panels::Width&&wide.height==square.height,"widescreen workspace remains unchanged");
    const auto title=panels::Fit(4096,4096,2.f);
    Check(title.width==1.125f&&title.height==1.125f,"square main menu fits original 2m widescreen envelope");
    const auto missing=panels::Fit(0,0);
    Check(missing.width==wide.width&&missing.height==wide.height,"missing source uses finite widescreen fallback");
    // A reported widget box at 2535,498 on a 3072-square source looked valid
    // but the owner saw only the right edge of the choices. Native widget
    // coordinates cannot define the sampled region after resize. Exercise the
    // whole visible surface through the actual ray and source/desktop mapping.
    for(auto source:{journal::Rect{0,0,1536,1536},journal::Rect{0,0,3072,3072},
        journal::Rect{0,0,4096,4096},journal::Rect{0,0,3840,2160},journal::Rect{0,0,2160,2400}}) {
        const auto crop=dialogue::Canvas(source.w,source.h);
        const auto size=panels::Fit(crop.w,crop.h);
        Check(std::fabs(size.width/size.height-float(source.w)/source.h)<.00001f,"complete NPC canvas preserves text proportions at each render aspect");
        const auto placed=panels::AtEyeLevel({0,1.65f,0},math::Yaw(.4f),panels::Distance);
        const auto origin=placed.position+math::Rotate(placed.orientation,{0,panels::UpperBandOffset(size),0});
        for(float u:{.01f,.2f,.5f,.8f,.99f})for(float v:{.01f,.3f,.7f,.99f}) {
            const auto p=origin+math::Rotate(placed.orientation,{(u-.5f)*size.width,(.5f-v)*size.height,0});
            const auto front=math::Rotate(placed.orientation,{0,0,1});math::PanelHit hit;math::Vec pixel{};
            Check(math::IntersectPanel(p+front,front*-1,origin,placed.orientation,size.width,size.height,hit),"laser reaches both edges and every part of the complete NPC surface");
            Check(journal::Pixel(crop,hit.u,hit.v,source.w,source.h,pixel)&&
                std::fabs(pixel.x-u)<.00001f&&std::fabs(pixel.y-v)<.00001f,
                "dialogue rendering and laser use the complete source rather than a shifted widget crop");
            const int clientX=int(std::round(pixel.x*3839)),clientY=int(std::round(pixel.y*2159));
            Check(std::abs(resolution::Coordinate(clientX,3840,source.w)-u*(source.w-1))<2&&
                std::abs(resolution::Coordinate(clientY,2160,source.h)-v*(source.h-1))<2,
                "NPC laser coordinates survive a desktop-to-VR resolution mismatch without edge truncation");
        }
    }
    Check(!dialogue::Canvas(0,3072).w&&!dialogue::Canvas(4096,0).h,"missing NPC source cannot create an invalid XR surface");
    Check(panels::UpperBandOffset(wide)==0,"widescreen tabbed canvas needs no vertical correction");
    Check(std::fabs(panels::UpperBandOffset(square)+.23378906f)<.00001f,"square inventory centers its visible upper band instead of blank lower canvas");
    for(float eyeHeight:{1.15f,1.75f})for(float pitch:{-.6f,0.f,.6f}) {
        const math::Vec head{.2f,eyeHeight,-.3f};
        const auto look=math::Multiply(math::Yaw(.65f),{std::sin(pitch/2),0,0,std::cos(pitch/2)});
        for(float distance:{panels::Distance,panels::MainDistance}) {
            const auto placed=panels::AtEyeLevel(head,look,distance);
            Check(placed.position.y==eyeHeight,"seated/standing menu anchor is at eye level even when opening while looking up or down");
            Check(std::fabs(math::Dot(placed.position-head,placed.position-head)-distance*distance)<.00001f,"menu stays at the requested close distance");
            Check(placed.orientation.x==0&&placed.orientation.z==0,"menu remains upright without inheriting head pitch");
        }
        const auto placed=panels::AtEyeLevel(head,look,panels::Distance);
        const auto origin=placed.position+math::Vec{0,panels::UpperBandOffset(square),0};
        math::PanelHit hit;
        const auto direction=math::Rotate(placed.orientation,{0,0,-1});
        Check(math::IntersectPanel(head,direction,origin,placed.orientation,square.width,square.height,hit),"eye-level ray hits the lowered square menu");
        Check(std::fabs(hit.u-.5f)<.00001f&&std::fabs(hit.v-.28125f)<.00001f,"eye-level ray targets center of upper widescreen inventory band");
        Check(square.width/panels::Distance>1.45f*square.width/1.25f,"square inventory has substantially larger angular text size than first height candidate");
    }
    // Source pixels, physical panel and mouse mapping agree at both menu sizes,
    // including square presets, portrait headset targets and ultrawide desktop.
    for(float maximum:{panels::Width,2.f})
    for(auto size:{journal::Rect{0,0,3840,2160},journal::Rect{0,0,2560,1440},journal::Rect{0,0,1920,1200},
                  journal::Rect{0,0,1536,1536},journal::Rect{0,0,4096,4096},journal::Rect{0,0,2160,2400},journal::Rect{0,0,3440,1440}}) {
        const auto surface=panels::Fit(size.w,size.h,maximum);
        Check(surface.width<=maximum&&surface.height<=maximum*9.f/16.f,"native canvas stays within comfortable width and height");
        Check(std::fabs(surface.width/surface.height-float(size.w)/size.h)<.00001f,"menu artwork retains source aspect");
        const auto larger=panels::Fit(size.w*2,size.h*2,maximum);
        Check(larger.width==surface.width&&larger.height==surface.height,"pixel density does not change physical menu size");
        for(float u:{.01f,.25f,.5f,.75f,.99f})for(float v:{.01f,.5f,.99f}) {
            const auto placed=panels::AtEyeLevel({.3f,1.4f,0},math::Yaw(.65f),panels::Distance);
            const auto rot=placed.orientation;const auto origin=placed.position+math::Vec{0,panels::UpperBandOffset(surface),0};
            auto point=origin+math::Rotate(rot,{(u-.5f)*surface.width,(.5f-v)*surface.height,0});
            const auto front=math::Rotate(rot,{0,0,1});math::PanelHit hit;math::Vec pixel{};
            Check(math::IntersectPanel(point+front,front*-1,origin,rot,surface.width,surface.height,hit),"rotated panel ray");
            Check(journal::Pixel(size,hit.u,hit.v,size.w,size.h,pixel)&&std::fabs(pixel.x-u)<.00001f&&std::fabs(pixel.y-v)<.00001f,"native pixel mapping across both panes and tooltips");
        }
    }
    std::puts("Native panel routing, drag ownership, focus recovery, scrolling and pixel mapping passed");
}
