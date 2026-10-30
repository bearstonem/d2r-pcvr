#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>
#include "leather_menu.h"
#include "stats_button.h"

// Reset Skills & Stats on the tabbed menu's Character page (owner, 2026-10-30:
// "Free and unlimited", "definitely confirmation needed", labeled, "Use the
// bottom right empty area of the Character menu for the button"). The first
// click turns it to "Confirm?" ("no timer. Stays until clicked"); the second
// asks vr/respec.cpp to mint a Token of Absolution and use it natively (the
// game's own refund, proven by the DEV token probe 2026-10-30). A stamped quad
// like the Advanced Stats button (stats_button.h): VR only, no game data files.
namespace framevr::respecbutton {

// While the tabbed menu shows the Character page and nothing covers it: not
// Advanced Stats (opened from this page), a cube, the stash, a merchant or any
// other world panel.
template<class Names> inline bool Shown(const Names& open) {
    bool menu=false,character=false;
    for(const auto& name:open) {
        const std::string_view n(name);
        if(leather::RoomPanel(n)||n.starts_with("HoradricCube")||n.starts_with("AdvancedStats"))return false;
        menu=menu||n==panels::TabbedMenu;
        character=character||n.starts_with("CharacterStats");
    }
    return menu&&character;
}

// What the button says. Reset and Confirm are clickable; the rest are the
// engine's answer, shown in place for NoticeMs, then it reads Reset again.
enum class Face {Reset,Confirm,Busy,Done,NoRoom,Failed,Count};
constexpr int Faces=int(Face::Count);
inline constexpr const wchar_t* Labels[Faces]={
    L"Reset Skills & Stats",L"Confirm?",L"Resetting...",L"Skills & Stats reset",
    L"Needs 1 free inventory square",L"Reset not available"};
inline bool Clickable(Face f){return f==Face::Reset||f==Face::Confirm;}

// The engine's progress as the button reads it (vr/respec.cpp).
enum class Engine {Idle,Busy,Done,NoRoom,Failed};

// Click state. Confirm waits with no timer (owner) but goes back to Reset when
// the button stops showing (the Character page left, the menu closed, something
// over it), so a later visit never opens one click from wiping the build.
constexpr uint64_t NoticeMs=3000;
class Button {
    bool confirm=false,waiting=false;Face notice=Face::Reset;uint64_t noticeUntil=0;
public:
    // Returns true on the click that confirms: the caller starts the reset.
    bool Click(uint64_t now,Engine engine) {
        if(waiting||engine==Engine::Busy||now<noticeUntil)return false;
        if(!confirm){confirm=true;return false;}
        confirm=false;waiting=true;return true;
    }
    // Every frame: shown or not, and the engine's state.
    void Step(bool shown,Engine engine,uint64_t now) {
        if(!shown)confirm=false;
        if(waiting&&engine!=Engine::Busy&&engine!=Engine::Idle) {
            waiting=false;
            notice=engine==Engine::Done?Face::Done:engine==Engine::NoRoom?Face::NoRoom:Face::Failed;
            noticeUntil=now+NoticeMs;
        }
    }
    // The engine would not take the request (it reads Busy from the moment it does).
    void Refused(){waiting=false;}
    Face Showing(uint64_t now) const {
        if(waiting)return Face::Busy;
        if(now<noticeUntil)return notice;
        return confirm?Face::Confirm:Face::Reset;
    }
    bool Confirming() const{return confirm;}
    void Hide(){confirm=false;}   // something took the menu over (VR Settings, calibration)
};

// Where it sits: the Character page's empty lower right, inside the menu's 16:9
// band (the same band stats_button.h measures). Height as the Advanced Stats
// button (6.2% of the band, about 2 cm on the leather), five times as wide for
// its label. Centre as fractions of the band from its top left: the middle of
// the page's open stone (owner: "center the button to that open space"), right
// of the Stamina/Life/Mana boxes, under Poison Resistance, inside the frame -
// band x 1932-2642, y 876-1290 of 3072 x 1728. Measured on Cal's headset
// capture (2026-10-30) after undoing its perspective: a homography from the
// menu art's corners to their band texels (x 384-2688, y 80-1490), which puts
// the stats button within .002 of its designed spot. rev1's guess (.80, .78)
// sat on the frame's corner; rev4's (.739, .644), measured on the tilted
// capture, sat low. The quad takes the menu surface's own orientation
// (transport.cpp), so it lies flat on the menu, never crooked.
constexpr float Height=statsbutton::Size,Aspect=5.f,CenterX=.744f,CenterY=.627f;
constexpr int TexelWidth=320,TexelHeight=64;   // the atlas picture: Aspect to 1
static_assert(TexelWidth==int(Aspect)*TexelHeight,"picture and quad share one aspect");
struct Spot {float x=0,y=0,width=0,height=0;};
inline Spot Place(float width,float height) {
    if(!(width>0)||!(height>0))return {};
    const float band=std::min(height,width*9.f/16.f),bandWidth=band*16.f/9.f;
    const float h=Height*band;
    return {(CenterX-.5f)*bandWidth,height*.5f-CenterY*band,h*Aspect,h};
}
// The laser's hit on the surface (u, v from its top left, 0-1), inside the button.
inline bool Hit(Spot s,float width,float height,float u,float v) {
    if(!(s.width>0)||!(s.height>0))return false;
    const float x=(u-.5f)*width,y=(.5f-v)*height;
    return std::fabs(x-s.x)<=s.width*.5f&&std::fabs(y-s.y)<=s.height*.5f;
}

// The face behind the label: the Advanced Stats button's stone (grey, grained),
// stretched to a plaque with a gold rim, like the native selected tab's frame
// (owner 2026-10-30: "yellow frame and lettering to be white"); `lit` brightens
// it. The label is drawn over it on Windows (transport.cpp, GDI). Premultiplied RGBA.
inline std::vector<uint8_t> PaintFace(int w,int h,bool lit) {
    std::vector<uint8_t> px(size_t(std::max(w,0))*size_t(std::max(h,0))*4,0);
    if(w<=0||h<=0)return px;
    const float ax=float(w)/float(h);   // x in units of the height
    const auto box=[](float x,float y,float cx,float cy,float hx,float hy,float r) {
        const float qx=std::fabs(x-cx)-hx+r,qy=std::fabs(y-cy)-hy+r;
        return std::hypot(std::max(qx,0.f),std::max(qy,0.f))+std::min(std::max(qx,qy),0.f)-r;
    };
    const float gold=lit?1.f:.8f;
    const float rimGold[3]={236*gold/255.f,196*gold/255.f,112*gold/255.f};
    constexpr int S=4;
    for(int py=0;py<h;++py)for(int qx=0;qx<w;++qx) {
        float acc[4]{};
        for(int sy=0;sy<S;++sy)for(int sx=0;sx<S;++sx) {
            const float x=(qx+(sx+.5f)/S)/h,y=(py+(sy+.5f)/S)/h;   // both in heights
            const float plaque=box(x,y,ax*.5f,.5f,ax*.5f-.04f,.46f,.10f);
            if(plaque>0)continue;
            const float grain=.5f*statsbutton::StoneNoise(x*7,y*7)+.3f*statsbutton::StoneNoise(x*19+3,y*19+5)+.2f*statsbutton::StoneNoise(x*53+7,y*53+1);
            float g=.30f+.16f*(grain-.5f)*2.f;
            const bool rim=plaque>-.07f,lip=!rim&&plaque>-.085f;
            if(rim)g+=.10f+.10f*(.5f-y);
            if(lip)g*=.45f;
            if(!rim)g*=.78f+.22f*(1-y);
            float c[3]={g,g*.99f,g*.96f};
            if(rim)for(int i=0;i<3;++i)c[i]=rimGold[i]*(1.f-.2f*y);
            for(int i=0;i<3;++i)acc[i]+=std::clamp(c[i],0.f,1.f);
            acc[3]+=1;
        }
        auto* p=&px[(size_t(py)*w+qx)*4];
        const float n=float(S*S);
        for(int i=0;i<3;++i)p[i]=uint8_t(std::clamp(acc[i]/n,0.f,1.f)*255.f+.5f);
        p[3]=uint8_t(acc[3]/n*255.f+.5f);
    }
    return px;
}
// Lays label coverage (0-255 per texel, w x h, e.g. GDI's antialiased text)
// over a premultiplied face in white (owner 2026-10-30), a little brighter lit:
// covered texels take the white, the stone shows through the edges.
inline void Stamp(std::vector<uint8_t>& face,const std::vector<uint8_t>& coverage,int w,int h,bool lit) {
    if(w<=0||h<=0||face.size()!=size_t(w)*h*4||coverage.size()!=size_t(w)*h)return;
    const float white=lit?1.f:.9f;
    const float c[3]={255*white,252*white,246*white};
    for(size_t i=0;i<coverage.size();++i) {
        const float a=coverage[i]/255.f;if(a<=0)continue;
        auto* p=&face[i*4];const float under=p[3]/255.f;
        if(under<=0)continue;   // text never spills off the plaque
        for(int k=0;k<3;++k)p[k]=uint8_t(std::clamp(p[k]*(1-a)+c[k]*a*under,0.f,255.f)+.5f);
    }
}
}
