#pragma once
#include <array>
#include <cstdint>

// Read-only render entity -> gameplay recipient resolver. Storage addresses
// must come from verified discovery; runtime pool indices are never constants.
namespace aurarecipient {
constexpr uint32_t NoEntity=0xfffff;
struct Unit {uint64_t address=0;uint32_t type=0,cls=0,id=0;};
enum class Kind {Unknown,Player,Summon};
struct Friendly {uint32_t type=0,cls=0,id=0;};
inline Kind Classify(const Unit& unit,const Friendly* friendly,unsigned count){
    if(!unit.address||!friendly||count>65)return Kind::Unknown;
    for(unsigned i=0;i<count;++i)
        if(unit.type==friendly[i].type&&unit.cls==friendly[i].cls&&unit.id==friendly[i].id)
            return unit.type==0?Kind::Player:unit.type==1?Kind::Summon:Kind::Unknown;
    return Kind::Unknown; // includes enemies: never hide by exclusion
}
template<class Read>bool Alive(Read read,uint64_t registry,uint32_t entity){
    uint64_t range[2]{};uint32_t current=0;const auto index=entity&NoEntity;
    return index!=NoEntity&&read(range,registry+0x30,sizeof range)&&range[0]&&range[1]>=range[0]&&
        range[1]-range[0]<=0x400000&&index<(range[1]-range[0])/4&&
        read(&current,range[0]+index*4ull,4)&&current==entity;
}
template<class Read>uint64_t Component(Read read,uint64_t storage,uint32_t entity,unsigned stride){
    uint64_t header[9]{};uint64_t page=0;uint32_t dense=0,back=0;
    if(!storage||!stride||stride>0x1000||!read(header,storage,sizeof header))return 0;
    const auto sparseIndex=(entity>>8)&0xfff;
    if(!header[1]||header[2]<header[1]||header[2]-header[1]>4096*8||sparseIndex>=(header[2]-header[1])/8||
       !read(&page,header[1]+sparseIndex*8ull,8)||!page||!read(&dense,page+(entity&255)*4ull,4))return 0;
    dense&=NoEntity;
    if(dense==NoEntity||!header[4]||header[5]<header[4]||header[5]-header[4]>0x400000||
       dense>=(header[5]-header[4])/4||!header[7]||header[8]<header[7]||
       header[8]-header[7]!=((header[5]-header[4])/4)*stride||
       !read(&back,header[4]+dense*4ull,4)||back!=entity)return 0;
    return header[7]+dense*uint64_t(stride);
}
template<class Read>bool Resolve(Read read,uint64_t registry,uint64_t transforms,uint64_t units,uint32_t entity,Unit& out){
    out={};std::array<uint32_t,16> seen{};unsigned used=0;
    for(;used<seen.size();++used){
        if(!Alive(read,registry,entity))return false;
        for(unsigned i=0;i<used;++i)if(seen[i]==entity)return false;
        seen[used]=entity;
        const auto transform=Component(read,transforms,entity,0xd0);
        uint32_t parent=0;
        if(!transform||!read(&parent,transform+0xc0,4))return false;
        const auto component=Component(read,units,entity,16);
        if(component){
            uint64_t pointer=0,again=0;uint32_t header[4]{};
            if(!read(&pointer,component+8,8)||!pointer||!read(header,pointer,sizeof header)||
               header[0]>1||header[1]>4096||header[3]>32)return false;
            // Revalidate generations and component identity after following pointers.
            for(unsigned i=0;i<=used;++i)if(!Alive(read,registry,seen[i]))return false;
            if(Component(read,units,entity,16)!=component||!read(&again,component+8,8)||again!=pointer)return false;
            out={pointer,header[0],header[1],header[2]};return true;
        }
        if((parent&NoEntity)==NoEntity)return false;
        entity=parent;
    }
    return false; // cycles and overly deep hierarchies remain visible
}
}
