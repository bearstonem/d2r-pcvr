#pragma once
#include "bone_armor_height.h"

namespace billboards::frozen_overlay {
// Confirmed in this installation's frozenarmor.json, shiverarmor.json and
// chillarmor.json: all three use this permanent particle. Hit/cast effects and
// Chilling Armor retaliation projectiles are separate and remain unchanged.
inline constexpr std::string_view Path="data/hd/vfx/particles/overlays/sorceress/shiverarmor/fx_ice_shivern_armor.particles";
inline bool Asset(std::string_view path) {
    if(path.size()!=Path.size())return false;
    for(size_t i=0;i<Path.size();++i) {
        char c=path[i];if(c>='A'&&c<='Z')c=char(c+'a'-'A');if(c=='\\')c='/';
        if(c!=Path[i])return false;
    }
    return true;
}
inline bool PointMask(uint32_t mask){return mask==17||mask==19;}
template<class Read>bool Hide(portal::Vec* points,uint32_t count,const portal::Context& c,
        uint64_t base,Read read,portal::Vec hero,bool active) {
    if(!active||!points||!count||count>64||!portal::Finite(hero))return false;
    struct Entry {int32_t id=-1;bool own=false;};Entry cache[8];bool changed=false;
    for(uint32_t i=0;i<count;++i) {
        if(!portal::Finite(points[i]))continue;
        int32_t id=c.instance;
        if(id<0&&(i>=c.count||!read(&id,c.indices+4ull*i,4)))continue;
        if(id<0)continue;
        auto& e=cache[uint32_t(id)%8];
        if(e.id!=id) {
            e={id,false};portal::Instance instance{};portal::Vec origin{};char path[160]{};
            e.own=portal::Resolve(read,c,i,base,instance)&&
                portal::DefinitionPath(read,instance.definition,base,path)&&Asset(path)&&
                portal::Origin(read,instance,origin)&&bone_armor::Own(origin,hero);
        }
        if(e.own){points[i].y-=bone_armor::HiddenDepth;changed=true;}
    }
    return changed;
}
}
