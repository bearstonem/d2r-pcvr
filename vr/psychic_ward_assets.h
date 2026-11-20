#pragma once
#include <string_view>
namespace psychicward {
// psychicward.json and psychicward_hit_0..7.json. Do not match Psychic
// Hammer, hexes or the shared Warlock cast particles.
inline bool Asset(std::string_view path) {
    constexpr std::string_view paths[]={
        "data/hd/vfx/particles/overlays/warlock/psychic_ward/psychic_ward_overlay.particles",
        "data/hd/vfx/particles/overlays/warlock/psychic_ward/psychic_ward_onhit.particles"};
    for(auto candidate:paths) {
        if(path.size()!=candidate.size())continue;
        bool same=true;
        for(size_t i=0;i<path.size();++i) {
            char c=path[i];if(c>='A'&&c<='Z')c=char(c+'a'-'A');if(c=='\\')c='/';
            if(c!=candidate[i]){same=false;break;}
        }
        if(same)return true;
    }
    return false;
}
}
