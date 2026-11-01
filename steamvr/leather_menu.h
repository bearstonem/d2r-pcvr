#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>
#include "frame_math.h"
#include "native_panels.h"

// The tabbed game menu (inventory, character, skill tree, quests, options...)
// on a sheet of leather strapped to the left hand, with the world kept around
// it instead of the shared black background (owner, 2026-10-27). The menu
// laser is the right hand's, so the sheet is always in the left.
namespace framevr::leather {
constexpr int Hand=1;   // the left hand (index 1 in every hand array)

// The panels that put the menu on the leather: the tabbed menu, any page of
// it, and the cube opened from its inventory. The panels the world opens
// (stash, merchants, trade, gambling, imbue, hiring, waypoints, NPC talk)
// keep the room panel, even with a cube or inventory beside them.
inline bool SheetPanel(std::string_view name) {
    // Advanced Stats (opened by Start, stats_button.h): kept on the leather should the
    // game close UISwitcher for it, as it does for the cube.
    return name=="UISwitcher"||name.starts_with("HoradricCube")||name.starts_with("AdvancedStats");
}
inline bool RoomPanel(std::string_view name) {
    for(auto key:{"Vendor","Trade","Gamble","Bank","Stash","Waypoint","Imbue","HireMenu","ScrollSpeech","ScrollOfInifuss"})
        if(name.starts_with(key))return true;
    return name=="NpcDialogPanel";
}
template<class Names> inline bool OnSheet(const Names& open) {
    bool sheet=false;
    for(const auto& name:open) {
        const std::string_view n(name);
        if(RoomPanel(n))return false;
        sheet=sheet||SheetPanel(n);
    }
    return sheet;
}

// Right B on the leather (owner, 2026-10-28: "B did not exit the menu"). The
// game's own Back runs first: it closes the menu, or a cube, page or dialog in
// it. A press the game leaves unanswered, with no panel opened or closed for
// BackWaitMs, closes the menu the way the left menu button does
// (gamestate::CloseLeatherMenu), but only while nothing but the tabbed menu and
// its pages is open: a cube, a quantity, confirmation or options dialog keeps
// its own Back. Not while the laser drags an item (gamestate also refuses with
// an item on the cursor).
inline bool SheetPage(std::string_view name) {
    if(name==panels::TabbedMenu||name=="ControllerOverlay")return true;
    for(auto key:{"PlayerInventory","CharacterStats","AdvancedStats","SkillsTree","QuestLog"})
        if(name.starts_with(key))return true;
    return false;
}
inline bool OwnBack(std::string_view name) {   // a dialog over the menu answers B itself
    if(SheetPage(name))return false;
    for(auto key:{"Modal","Dialog","Popup","Prompt","Settings"})if(name.find(key)!=name.npos)return true;
    return panels::Modal(name);
}
template<class Names> inline bool BackCloses(const Names& open) {
    bool menu=false;
    for(const auto& name:open) {
        const std::string_view n(name);
        if(n==panels::TabbedMenu)menu=true;
        else if(OwnBack(n))return false;
    }
    return menu;
}
constexpr uint64_t BackWaitMs=250;
class Back {
    uint64_t at=0;uint32_t changes=0;bool held=false;
public:
    void Reset(){*this=Back{};}
    bool Waiting() const{return at!=0;}
    // pressed: Right B (held presses released first, menus::ReleaseGate);
    // panelChanges: gamestate::PanelChanges(). True once: close the menu now.
    bool Step(bool open,bool pressed,bool dragging,uint32_t panelChanges,uint64_t now) {
        if(!open){Reset();return false;}
        if(pressed&&!held&&!dragging){at=now?now:1;changes=panelChanges;}
        held=pressed;
        if(!at)return false;
        if(panelChanges!=changes||dragging){at=0;return false;}   // the game answered, or a drag began
        if(now-at<BackWaitMs)return false;
        at=0;return true;
    }
};

// The leather's picture and where on it the menu is pinned: pixels, y down,
// the menu 16:9 (its height menuW x 9/16). The game's own hide comes from the
// install-time cache (prepare_leather.py, ParseCache); the painted stand-in
// (PaintPlaceholder) keeps the menu inside a Border of the sheet's width.
constexpr int MaxArtWidth=1024,MaxArtHeight=800;   // panelatlas::Leather
constexpr float Border=.04f;
struct Art {
    int width=0,height=0;
    float menuX=0,menuY=0,menuW=0;
    bool Valid() const {
        return width>0&&height>0&&width<=MaxArtWidth&&height<=MaxArtHeight&&
            std::isfinite(menuX)&&std::isfinite(menuY)&&std::isfinite(menuW)&&menuW>=8&&menuX>=0&&menuY>=0&&
            menuX+menuW<=width+.5f&&menuY+menuW*9/16<=height+.5f;
    }
};
constexpr int PlaceholderWidth=1024,PlaceholderHeight=612;
inline Art PlaceholderArt() {
    const float w=PlaceholderWidth*(1-2*Border);
    return {PlaceholderWidth,PlaceholderHeight,PlaceholderWidth*Border,(PlaceholderHeight-w*9/16)*.5f,w};
}
// The cache file: "PVLH", format 1, width, height, 0, then the menu's x, y,
// width (float pixels), then width x height RGBA rows, top first.
inline bool ParseCache(const uint8_t* data,size_t size,Art& art,std::vector<uint8_t>& rgba) {
    if(!data||size<32||data[0]!='P'||data[1]!='V'||data[2]!='L'||data[3]!='H')return false;
    uint32_t head[4];float menu[3];
    std::memcpy(head,data+4,16);std::memcpy(menu,data+20,12);
    Art read{int(std::min<uint32_t>(head[1],1u<<20)),int(std::min<uint32_t>(head[2],1u<<20)),menu[0],menu[1],menu[2]};
    if(head[0]!=1||head[3]!=0||!read.Valid()||size!=32+size_t(read.width)*read.height*4)return false;
    art=read;rgba.assign(data+32,data+size);return true;
}
// The leather goes to the headset premultiplied (its quad blends that way):
// colour scaled by its alpha, so the cache's transparent surround, which keeps
// an edge colour under alpha 0, adds nothing. Sent straight, some runtimes
// still added that colour: a translucent brown rectangle round the hide
// (owner, 2026-10-28).
inline void Premultiply(std::vector<uint8_t>& rgba) {
    for(size_t i=0;i+3<rgba.size();i+=4) {
        const unsigned a=rgba[i+3];
        for(int c=0;c<3;++c)rgba[i+c]=uint8_t((rgba[i+c]*a+127)/255);
    }
}

// [interactions] leather_menu (VR Settings > Interface, beside Menu distance):
// 0 Off, the tabbed menu in front of you against black; 1 On, on the hide in the
// left hand; 2 Menu only, the same menu held in the left hand with no hide
// (owner 2026-10-29: "in case players dont like the hide"). A value that is
// not one of these reads On.
enum class Style:int {Off=0,Hide=1,MenuOnly=2};
inline Style NormalizeStyle(float value) {
    if(!std::isfinite(value)||value!=std::round(value)||value<0||value>2)return Style::Hide;
    return Style(int(value));
}
// The menu's art inside its 16:9 band, across: x 384-2688 of 3072 (Cal's captures,
// 2026-10-28/29, the tabbed menu and the cube alike), so 12.5% of the band's
// width is see-through on each side. With Menu only the fist holds the art's left
// edge, not the band's (owner: "Make sure the menu left edge is held in the hand").
constexpr float MenuArtInset=384.f/3072;

// [interactions] leather_menu_width: the menu across, in metres; the leather
// is sized round it (the hide comes out about 0.83 m for the default).
constexpr float DefaultMenuWidth=.57f,MinMenuWidth=.30f,MaxMenuWidth=.90f;
// [interactions] leather_tilt: degrees the sheet leans back from the quest
// log's angle (0, the sheet's face along the fist's top); + toward you.
constexpr float DefaultTilt=0,MinTilt=-45,MaxTilt=60;
inline float NormalizeMenuWidth(float metres){return std::isfinite(metres)?std::clamp(metres,MinMenuWidth,MaxMenuWidth):DefaultMenuWidth;}
inline float NormalizeTilt(float degrees){return std::isfinite(degrees)?std::clamp(degrees,MinTilt,MaxTilt):DefaultTilt;}
// In metres: the sheet, the menu, and the menu's centre from the sheet's
// (x right, y up, in the sheet's plane).
struct Size {float width,height,menuWidth,menuHeight;math::Vec menuCentre;};
inline Size Measure(const Art& art,float menuWidth) {
    menuWidth=NormalizeMenuWidth(menuWidth);
    const Art use=art.Valid()?art:PlaceholderArt();
    const float scale=menuWidth/use.menuW;
    return {use.width*scale,use.height*scale,menuWidth,menuWidth*9/16,
            {(use.menuX+use.menuW*.5f-use.width*.5f)*scale,(use.height*.5f-use.menuY-use.menuW*9/32)*scale,0}};
}

// The menu's 16:9 band of the UI canvas: the canvas's full width at the top of
// a taller canvas (the native "upper widescreen band", native_panels.h
// UpperBand); the full height in the middle of a wider canvas. The sheet shows
// only this band, so nothing the game draws outside it (a dimmed backdrop, the
// lower canvas) leaves the leather. The Horadric Cube opened from the inventory
// is laid out in the same band as the tabbed menu (owner's capture 2026-10-29:
// cube and inventory at x 384-2687, y 80-1489 of 3072x3072, as the tabbed menu),
// though the game closes UISwitcher for it, so the band never follows
// native_panels' UpperBand list (v0.3.29 showed the middle band: the cube cut).
struct Rect {int x=0,y=0,w=0,h=0;};
inline Rect Band(int width,int height) {
    if(width<=0||height<=0)return {};
    if(int64_t(width)*9<=int64_t(height)*16) {
        const int h=int(int64_t(width)*9/16);
        return {0,0,width,h};
    }
    const int w=int(int64_t(height)*16/9);
    return {(width-w)/2,0,w,height};
}

// The gameplay HUD held with the menu (owner 2026-10-29, a tester: "With inventory open
// you no longer get to see hp/mana/map"; Cal chose "on the hide, beside the menu", then
// "health/mana UI to be above the menu and slightly larger"): the life / mana / XP panel
// (the wrist's picture, 2:1, HudVitalsWidth across) centred above the menu band, HudGap
// over it; the minimap (HudMap square) centred on the band's left edge - over the band's
// see-through margin and the hide beyond it, clear of the art (MenuArtInset in) - its top
// HudAbove above the band, so it sits above the fist, which holds the band's middle.
// Menu-local metres from the menu's centre, x right, y up, HudLift in front of the menu.
constexpr float HudMap=.10f,HudVitalsWidth=.14f,HudVitalsHeight=HudVitalsWidth*.5f,HudAbove=.04f,HudGap=.01f,HudLift=.004f;
struct Hud {math::Vec map{},vitals{};float mapSize=HudMap,vitalsWidth=HudVitalsWidth,vitalsHeight=HudVitalsHeight;};
inline Hud HudPlaces(float menuWidth) {
    const float w=NormalizeMenuWidth(menuWidth),top=w*9.f/32.f;
    Hud h;
    h.map={-w*.5f,top+HudAbove-HudMap*.5f,HudLift};
    h.vitals={0,top+HudGap+HudVitalsHeight*.5f,HudLift};
    return h;
}

// The sheet strapped to the left hand: the quest log's hold (its face along the
// fist's top, its up along the fist), turned back by `tilt` degrees. The fist
// holds the leather at the middle of the menu's left edge, Gap short of it, so
// whatever leather is beyond (the hide's neck and legs) reaches past the hand.
// The menu sits Lift in front of the leather. `inset` (metres): how far inside
// the band's left edge the edge held is (Menu only: the art's, MenuArtInset of
// the width; 0 with the hide).
struct Placement {math::Vec p{};math::Quat q{0,0,0,1};};
struct Strapped {Placement sheet{},menu{};};
constexpr float Gap=.01f,Raise=.025f,Lift=.003f;
inline math::Quat AboutX(float radians){return {std::sin(radians*.5f),0,0,std::cos(radians*.5f)};}
inline Strapped Strap(math::Vec hand,math::Quat grip,const Art& art,float menuWidth,float tilt,float inset=0) {
    const auto size=Measure(art,menuWidth);
    const float back=NormalizeTilt(tilt)*3.14159265f/180;
    Strapped out;
    out.sheet.q=out.menu.q=math::Multiply(grip,AboutX(-1.57079633f+back));
    inset=std::isfinite(inset)?std::clamp(inset,0.f,size.menuWidth*.5f):0;
    const auto centre=hand+math::Rotate(out.sheet.q,{size.menuWidth*.5f-inset+Gap,0,Raise});
    out.sheet.p=centre-math::Rotate(out.sheet.q,size.menuCentre);
    out.menu.p=centre+math::Rotate(out.menu.q,{0,0,Lift});
    return out;
}

// The sheet follows the hand; a lost hand leaves it where it was last held.
class Holder {
    bool placed=false;
    Strapped pose{};
public:
    void Open(){placed=false;pose={};}
    bool Placed() const{return placed;}
    Strapped Get() const{return pose;}
    void Step(math::Vec hand,math::Quat grip,bool tracked,const Art& art,float menuWidth,float tilt,float inset=0) {
        const bool finite=std::isfinite(hand.x)&&std::isfinite(hand.y)&&std::isfinite(hand.z)&&
            std::isfinite(grip.x)&&std::isfinite(grip.y)&&std::isfinite(grip.z)&&std::isfinite(grip.w);
        const float length=finite?std::sqrt(grip.x*grip.x+grip.y*grip.y+grip.z*grip.z+grip.w*grip.w):0;
        if(!tracked||length<.5f)return;
        pose=Strap(hand,{grip.x/length,grip.y/length,grip.z/length,grip.w/length},art,menuWidth,tilt,inset);placed=true;
    }
};

// Reaching the left hand over the left shoulder and gripping opens the tabbed
// menu. The same volume as the weapon swap's, left side only: the swap takes
// the dominant hand over either shoulder, so with Dominant hand = Left this
// stays off (Left Grip + left stick click and the wheel still open the leather).
inline bool OverLeftShoulder(math::Vec hand,math::Vec head,math::Quat heading) {
    if(!std::isfinite(hand.x)||!std::isfinite(hand.y)||!std::isfinite(hand.z)||
       !std::isfinite(head.x)||!std::isfinite(head.y)||!std::isfinite(head.z))return false;
    const auto p=math::Rotate(math::Conjugate(heading),hand-head);
    return p.x<=-.10f&&p.x>=-.48f&&p.y>=-.38f&&p.y<=.16f&&p.z>=.025f&&p.z<=.38f;
}
struct ReachInput {
    double time=0;
    uint64_t session=0,epoch=0;
    bool active=false,tracked=false,allowed=false;
    math::Vec head{},hand{};math::Quat heading{0,0,0,1};
    float grip=0;
};
struct ReachOutput {bool open=false,block=false,inReach=false;};
// A fresh grip press with the hand behind the shoulder for at least 0.12 s.
// A grip already held when the gesture becomes possible (menu closing, a
// session or tracking change) must be released first.
class Reach {
    bool primed=false,gripWas=false,consumed=true;
    double previous=0,nearSince=0;
    uint64_t session=0,epoch=0;
public:
    void Reset(){*this=Reach{};}
    ReachOutput Step(const ReachInput& in) {
        ReachOutput out;
        const bool down=std::isfinite(in.grip)&&in.grip>(gripWas?.20f:.55f);
        const bool valid=in.active&&in.tracked&&in.session&&std::isfinite(in.time)&&in.time>0;
        if(!valid||!primed||session!=in.session||epoch!=in.epoch||in.time<=previous||in.time-previous>.15) {
            Reset();primed=valid;session=in.session;epoch=in.epoch;previous=in.time;
            gripWas=down;consumed=down;out.block=down&&valid&&OverLeftShoulder(in.hand,in.head,in.heading);return out;
        }
        previous=in.time;const bool edge=down&&!gripWas;gripWas=down;
        if(!down)consumed=false;
        out.inReach=OverLeftShoulder(in.hand,in.head,in.heading);
        if(!out.inReach)nearSince=0;else if(!nearSince)nearSince=in.time;
        out.block=(consumed&&out.inReach)||(down&&out.inReach&&in.allowed);
        if(out.inReach&&in.allowed&&edge&&in.time-nearSince>=.12){consumed=true;out.open=out.block=true;}
        return out;
    }
};

// The stand-in hide, used when the install-time cache of the game's own hide
// is missing: mottled brown, darker toward a ragged edge, a stitched border.
// RGBA, rows top first; transparent outside the hide.
namespace detail {
inline float Hash(int x,int y,int seed) {
    uint32_t h=uint32_t(x)*374761393u+uint32_t(y)*668265263u+uint32_t(seed)*2246822519u;
    h=(h^(h>>13))*1274126177u;h^=h>>16;
    return float(h&0xffffffu)/float(0xffffffu);
}
inline float Noise(float x,float y,int seed) {
    const int xi=int(std::floor(x)),yi=int(std::floor(y));
    const float fx=x-float(xi),fy=y-float(yi),u=fx*fx*(3-2*fx),v=fy*fy*(3-2*fy);
    const float a=Hash(xi,yi,seed),b=Hash(xi+1,yi,seed),c=Hash(xi,yi+1,seed),d=Hash(xi+1,yi+1,seed);
    return a+(b-a)*u+(c-a)*v+(a-b-c+d)*u*v;
}
inline float Fbm(float x,float y,int seed,int octaves) {
    float sum=0,amplitude=.5f,scale=1,total=0;
    for(int i=0;i<octaves;++i){sum+=amplitude*Noise(x*scale,y*scale,seed+i*17);total+=amplitude;amplitude*=.5f;scale*=2;}
    return sum/total;
}
}
inline std::vector<uint8_t> PaintPlaceholder(int width,int height) {
    std::vector<uint8_t> rgba(size_t(std::max(width,0))*std::max(height,0)*4);
    if(width<=0||height<=0)return rgba;
    const float unit=float(std::min(width,height))/600.f;
    const float inset=10*unit,radius=48*unit,ragged=10*unit,feather=1.5f*unit;
    const float halfW=width*.5f-inset,halfH=height*.5f-inset;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const float px=x+.5f-width*.5f,py=y+.5f-height*.5f;
        // Rounded rectangle, its edge pushed in and out by slow noise.
        const float qx=std::fabs(px)-(halfW-radius),qy=std::fabs(py)-(halfH-radius);
        const float ox=std::max(qx,0.f),oy=std::max(qy,0.f);
        const float smooth=std::sqrt(ox*ox+oy*oy)+std::min(std::max(qx,qy),0.f)-radius;
        const float edge=smooth+(detail::Fbm(x/(60*unit),y/(60*unit),7,3)-.5f)*2*ragged;
        const float alpha=std::clamp(.5f-edge/(2*feather),0.f,1.f);
        const float depth=-edge;   // pixels inside the edge
        // Mottled hide with a fine grain and a few faint creases.
        const float mottle=detail::Fbm(x/(140*unit),y/(140*unit),11,4);
        const float grain=detail::Fbm(x/(3*unit),y/(3*unit),23,2);
        const float crease=std::fabs(detail::Fbm(x/(220*unit),y/(90*unit),31,3)-.5f);
        float shade=.78f+.40f*mottle+.10f*(grain-.5f)-.05f*std::clamp(1-crease/.008f,0.f,1.f);
        shade*=.55f+.45f*std::clamp(depth/(46*unit),0.f,1.f);   // burnt toward the edge
        float r=112*shade,g=72*shade,b=42*shade;
        // Stitching: dashes 26 units inside the (unragged) outline, along the nearer side.
        const float stitch=std::fabs(-smooth-26*unit);
        const float along=qy>qx?float(x):float(y);   // top or bottom edge: dashes run along x
        if(stitch<1.6f*unit&&std::fmod(along,18*unit)<11*unit){r=168;g=128;b=86;}
        uint8_t* p=&rgba[(size_t(y)*width+x)*4];
        p[0]=uint8_t(std::clamp(r,0.f,255.f));p[1]=uint8_t(std::clamp(g,0.f,255.f));
        p[2]=uint8_t(std::clamp(b,0.f,255.f));p[3]=uint8_t(alpha*255+.5f);
    }
    return rgba;
}
}
