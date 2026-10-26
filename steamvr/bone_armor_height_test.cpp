#include "../vr/bone_armor_height.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace billboards;
void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main() {
    using namespace bone_armor;
    // Scope: the captured asset only, any case or separator.
    Check(Asset("data/hd/vfx/particles/overlays/necro/bonearmor_front/vfx_BoneArmor_Front.particles"),"captured Bone Armor asset");
    Check(Asset("DATA\\HD\\VFX\\PARTICLES\\OVERLAYS\\NECRO\\BONEARMOR_FRONT\\VFX_BONEARMOR_FRONT.PARTICLES"),"case and separators");
    for(auto p:{"data/hd/vfx/particles/overlays/paladin/holyfreeze/FX_holyfreeze_overlay.particles",
        "data/hd/vfx/particles/overlays/necro/bonearmor_front/vfx_BoneArmor_Front.particles.extra",
        "data/hd/vfx/particles/overlays/necro/bonearmor_front/other.particles",
        "data/hd/vfx/particles/missiles/bonespear/vfx_bonespear.particles",
        "data/hd/vfx/particles/overlays/common/shrine_mana_regen/vfx_shrine_mana_regen.particles",""})
        Check(!Asset(p),"auras, missiles, shrines and other paths stay native");
    // Height from the INI: 0..3, else the default.
    Check(Height(.3f)==.3f&&Height(0)==0&&Height(3)==3,"configured heights kept");
    for(float f:{-1.f,3.5f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        Check(Height(f)==Default,"out-of-range or invalid height uses the default");
    Check(Default>0&&Default<1,"default is just above the floor");
    // Capture 2026-10-25: local (0,3,0) -> origin + 3 units up.
    portal::Vec v{9,9,9};
    Check(Lowered({0,3,0},Default,v)&&v.x==0&&v.y==Default&&v.z==0,"captured centre lowered");
    Check(Lowered({0,3,0},0,v)&&v.y==0,"floor height");
    portal::Vec keep{9,9,9};
    Check(!Lowered({0,3,0},Native,keep)&&keep.y==9,"native height is a no-op");
    for(auto p:{portal::Vec{0,2,0},portal::Vec{.5f,3,0},portal::Vec{0,3,.5f},portal::Vec{0,0,0},
        portal::Vec{std::numeric_limits<float>::quiet_NaN(),3,0}})
        Check(!Lowered(p,Default,keep)&&keep.y==9,"other local points stay native");
    std::puts("Bone Armor height: asset scope, INI range, captured centre lowered, other points native passed");
}
