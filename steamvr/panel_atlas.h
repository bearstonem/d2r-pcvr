#pragma once
#include "blood_screen.h"
#include "bow_laser.h"
#include "guard_strain.h"
#include "notice_card.h"
#include "vr_settings_help.h"
#include "wrist_compass.h"

// Panels that share swapchains. SteamVR gives an OpenXR session 16, and OFXR
// Bridge (frame generation) takes three more for each eye's: the mod's own
// panels had twelve, and VR Settings, the native menus and the hand map were
// refused in turn (owner, 2026-10-25: 16 made, then XR_ERROR_LIMIT_REACHED).
// The pictures painted once at the start share one static image, and the
// panels repainted as they change - VR Settings, the quest card, the blood
// visor, the guard strain light and the bow laser - one dynamic image, each in
// its own region with Gap texels round it, so filtering never reaches into a
// neighbour. Five swapchains fewer.
namespace framevr::panelatlas {
struct Region {int x,y,w,h;};
constexpr int Gap=8;
// Static: the gameplay art (palette labels, arrow guide, bow hints, sewer
// prompt), the pointer beam and dot beside it, the compass strip under it.
constexpr Region Gameplay{0,0,1024,1024};
constexpr Region Pointer{Gameplay.w+Gap,0,64,96};
constexpr Region Compass{0,Gameplay.h+Gap,compass::Width,compass::Height};
constexpr int StaticWidth=1536,StaticHeight=Compass.y+Compass.h;
// Dynamic: VR Settings with its help column across the top, the blood visor
// under it, then the strain light, the quest card and the bow laser beside it.
constexpr Region Settings{0,0,settings::AtlasWidth,settings::Height};
constexpr Region Blood{0,Settings.h+Gap,blood::Size,blood::Size};
constexpr Region Strain{Blood.x+Blood.w+Gap,Blood.y,strain::Size,strain::Size};
constexpr Region Notice{Strain.x+Strain.w+Gap,Blood.y,notice::Width,notice::Height};
constexpr Region BowLaser{Notice.x+Notice.w+Gap,Blood.y,bowlaser::AtlasWidth,bowlaser::AtlasHeight};
constexpr int SharedWidth=2048,SharedHeight=Blood.y+Blood.h;

constexpr bool Inside(Region r,int width,int height){return r.x>=0&&r.y>=0&&r.w>0&&r.h>0&&r.x+r.w<=width&&r.y+r.h<=height;}
// Apart by at least Gap texels on one axis.
constexpr bool Apart(Region a,Region b) {
    return a.x+a.w+Gap<=b.x||b.x+b.w+Gap<=a.x||a.y+a.h+Gap<=b.y||b.y+b.h+Gap<=a.y;
}
}
