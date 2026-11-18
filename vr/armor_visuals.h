#pragma once
#include <string_view>
namespace billboards::armorvisuals {
inline constexpr std::string_view CyclonePath="data/hd/vfx/particles/overlays/druid/cyclonearmor_1back/vfx_druid_cyclonearmor.particles";
inline bool CycloneAsset(std::string_view path){
    if(path.size()!=CyclonePath.size())return false;
    for(size_t i=0;i<path.size();++i){char c=path[i];if(c>='A'&&c<='Z')c=char(c+'a'-'A');if(c=='\\')c='/';if(c!=CyclonePath[i])return false;}
    return true;
}
}
