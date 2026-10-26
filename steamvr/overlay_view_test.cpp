#include "../vr/overlay_view.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace billboards;
void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
bool Near(float a,float b,float e=1e-3f){return std::fabs(a-b)<=e;}
int main() {
    using namespace overlay_view;
    // Scope: Paladin aura overlays (the headset-checked family), any case or separator.
    for(auto p:{"data/hd/vfx/particles/overlays/paladin/holyfreeze/FX_holyfreeze_overlay.particles",
        "data/hd/vfx/particles/overlays/paladin/aura_resistcold/FX_resistcold_overlay.particles",
        "data/hd/vfx/particles/overlays/paladin/convictionfront/FX_Conviction_Overlay.particles",
        "data/hd/vfx/particles/overlays/paladin/blessedaimback/FX_BlessedAimOverlay.particles"})
        Check(Asset(p),"Paladin aura overlay");
    Check(Asset("DATA\\HD\\VFX\\PARTICLES\\OVERLAYS\\PALADIN\\AURA_HOLYFIRE_FRONT\\FX_HOLYFIRE_OVERLAY.PARTICLES"),"case and separators");
    for(auto p:{"data/hd/vfx/particles/objects/vfx_only/town_portal/vfx_town_portal_newstuff.particles",
        "data/hd/vfx/particles/missiles/fireball/vfx_fireball.particles",
        "data/hd/vfx/particles/overlays/common/shrine_mana_regen/vfx_shrine_mana_regen.particles",
        "data/hd/vfx/particles/overlays/necro/bonearmor_front/vfx_bonearmor_front.particles",
        "data/hd/vfx/particles/overlays/paladin/.particles",
        "data/hd/vfx/particles/overlays/paladin/aura.particles.extra",
        "data/hd/vfx/particles/overlays/paladin/../necro/x.particles"})
        Check(!Asset(p),"portals, missiles, shrine symbols, other overlays and malformed paths keep their native view direction");

    // Headset capture 2026-10-21: the flat camera direction, and Holy Freeze
    // spawning with the VR view (0.895,-0.442,0.056) -> local (-2.686,1.425,-0.167):
    // local = -direction * 3 + (0, 0.1, 0).
    const portal::Vec flat{-.612f,-.5f,-.612f};
    portal::Vec v;
    Check(Upright(flat,v)&&v.x==0&&v.z==0&&v.y==flat.y*Lift,"flat camera: Lift of the vertical part kept");
    const portal::Vec spawn{-3*v.x,.1f-3*v.y,-3*v.z};
    Check(spawn.x==0&&spawn.z==0&&Near(spawn.y,.8125f),"the spawn push becomes a straight lift");
    // The full lift was 1.5 units; about 15 cm (~0.79 units at 5.25 per metre) lower.
    Check(Near(1.5f-(spawn.y-.1f),.7875f),"lowered about 15 cm");
    const portal::Vec vrView{.895f,-.442f,.056f};
    const float len=std::sqrt(vrView.x*vrView.x+vrView.y*vrView.y+vrView.z*vrView.z);
    Check(Near(-3*vrView.x,-2.686f,.01f)&&Near(.1f-3*vrView.y,1.425f,.01f),"captured push is -3 x view direction");
    Check(Upright({vrView.x/len,vrView.y/len,vrView.z/len},v)&&v.x==0&&v.z==0,"any downward view loses its sideways part");

    // Guards: no replacement from a level, upward or invalid camera.
    portal::Vec keep{8,9,10};
    for(auto f:{portal::Vec{1,0,0},portal::Vec{0,1,0},portal::Vec{.6f,.8f,0},portal::Vec{0,0,0},portal::Vec{0,-2,0},
        portal::Vec{std::numeric_limits<float>::quiet_NaN(),-1,0},portal::Vec{0,-std::numeric_limits<float>::infinity(),0}}) {
        Check(!Upright(f,keep)&&keep.x==8&&keep.y==9&&keep.z==10,"invalid game camera rejected without a replacement");
    }
    std::puts("Overlay view: Paladin aura scope, captured aura pushes, kept lift and camera guards passed");
}
