#pragma once
#include "portal_billboard.h"

// Developer experiment, not a general view-direction replacement. The second
// live capture identifies these two symbol scripts; it does not yet establish
// whether every consumer of their view.direction is positional. Keep this
// candidate out of regular builds until visual placement/orientation is checked.
namespace billboards::shrine_placement {
inline bool CandidateAsset(std::string_view path) {
    constexpr std::string_view paths[]={
        "data/hd/vfx/particles/overlays/common/shrine_mana_regen/vfx_shrine_mana_regen.particles",
        "data/hd/vfx/particles/overlays/common/shrine_stamina/vfx_shrine_stamina_looping.particles"};
    for(auto candidate:paths) {
        if(path.size()!=candidate.size())continue;
        bool match=true;
        for(size_t i=0;i<path.size();++i) {
            char c=path[i];if(c>='A'&&c<='Z')c=char(c+'a'-'A');if(c=='\\')c='/';
            if(c!=candidate[i]){match=false;break;}
        }
        if(match)return true;
    }
    return false;
}
// Removing the X/Z contribution to a scripted displacement keeps its native Y.
// Do not normalize afterward: that would change the height the owner says is
// already correct. This leaves effect origins, authored transforms and the
// normal quad-facing path alone. A headset test must confirm script consumers.
inline bool HorizontalDirection(portal::Vec native,portal::Vec& result) {
    if(!portal::Finite(native))return false;
    const float length2=native.x*native.x+native.y*native.y+native.z*native.z;
    if(!std::isfinite(length2)||std::fabs(length2-1.f)>.01f)return false;
    result={0,native.y,0};return true;
}
}
