#include "../vr/shrine_placement.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace billboards;
void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main() {
    using namespace shrine_placement;
    Check(CandidateAsset("data/hd/vfx/particles/overlays/common/shrine_mana_regen/vfx_shrine_mana_regen.particles"),"captured mana symbol");
    Check(CandidateAsset("DATA\\HD\\VFX\\PARTICLES\\OVERLAYS\\COMMON\\SHRINE_STAMINA\\VFX_SHRINE_STAMINA_LOOPING.PARTICLES"),"captured stamina symbol with native case/separators");
    for(auto p:{"data/hd/vfx/particles/objects/env_stone/forest_altar/vfx_forest_altar.particles",
        "data/hd/vfx/particles/objects/shrines_other/deer_shrine/vfx_deer_shrine.particles",
        "data/hd/vfx/particles/overlays/common/shrine_armor/vfx_shrine_armor_looping.particles",
        "data/hd/vfx/particles/overlays/common/shrine_mana_regen/other.particles",
        "data/hd/vfx/particles/overlays/common/shrine_mana_regen/vfx_shrine_mana_regen.particles.extra",
        "data/hd/vfx/particles/objects/vfx_only/town_portal/vfx_town_portal_newstuff.particles",
        "data/hd/vfx/particles/overlays/common/shrine_mana_regen/../vfx_shrine_mana_regen.particles"})
        Check(!CandidateAsset(p),"environment, portals and uncaptured symbol families unchanged");
    // Actual view-direction outputs from the second owner capture: stationary
    // shrine mana, stamina, and the moving player-attached mana instance.
    const std::array<portal::Vec,6> samples{{
        {-.00499851f,-.0637793f,-.997952f},
        {-.681016f,-.0589089f,-.729895f},
        {.440339f,-.0219721f,.897563f},
        {.0977004f,.106245f,.989529f},
        {-.991519f,-.0410738f,.123299f},
        {-.107025f,-.431253f,-.895861f}}};
    for(auto native:samples) {
        portal::Vec corrected;
        Check(HorizontalDirection(native,corrected),"captured unit direction accepted");
        Check(corrected.x==0&&corrected.z==0&&corrected.y==native.y,"vertical input preserved exactly per lane");
        // This proves the candidate's contract for an additive displacement,
        // not that every script consumer uses this formula (headset check).
        for(float multiplier:{-20.f,-3.f,0.f,1.f,15.f}) {
            const portal::Vec anchor{10614.1f,12.6039f,12434.1f};
            Check(anchor.y+multiplier*corrected.y==anchor.y+multiplier*native.y,
                "additive placement keeps height for positive and negative factors");
            Check(anchor.x+multiplier*corrected.x==anchor.x&&anchor.z+multiplier*corrected.z==anchor.z,
                "candidate removes only horizontal camera contribution");
        }
    }
    portal::Vec result{8,9,10};
    for(auto native:{portal::Vec{0,0,0},portal::Vec{1,1,0},portal::Vec{0,2,0},
        portal::Vec{std::numeric_limits<float>::quiet_NaN(),0,1},
        portal::Vec{0,0,std::numeric_limits<float>::infinity()}}) {
        const auto before=result;
        Check(!HorizontalDirection(native,result)&&result.x==before.x&&result.y==before.y&&result.z==before.z,
            "invalid native input rejects without producing a replacement");
    }
    Check(HorizontalDirection({1,0,0},result)&&result.x==0&&result.y==0&&result.z==0,
        "horizontal view does not invent a vertical displacement or normalize zero");
    Check(HorizontalDirection({0,-1,0},result)&&result.y==-1,"vertical view height unchanged");
    std::puts("Shrine placement candidate: captured directions, exact scope, preserved vertical inputs and rejection guards passed");
}
