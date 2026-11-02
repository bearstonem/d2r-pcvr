#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <array>

namespace projectiles {
// 3.3.93787 asset-instance entity list and Transform component layout. The
// renderer's initializer iterates this list; Transform recursively evaluates
// the full-generation parent at +0xc0. A root's parent is outside the instance.
// Release-point absence is NOT root identity: Ice Bolt's VFX child lacks it too.
template<class Read> bool VisualRoot(uintptr_t definition,uintptr_t storage,uint32_t entity,Read read) {
    uintptr_t instance=0,entities=0,count=0;
    if((entity&0xfffff)==0xfffff||!read(definition,instance)||!instance||
       !read(instance+0x10,entities)||!entities||!read(instance+0x18,count)||!count||count>64)return false;
    std::array<uint32_t,64> members{};bool found=false;
    for(size_t i=0;i<count;++i){if(!read(entities+i*4,members[i]))return false;found|=members[i]==entity;}
    if(!found)return false;
    uintptr_t begin=0,end=0,page=0,packed=0,packedEnd=0,components=0;
    uint32_t index=0,full=0,parent=0;
    const auto pageIndex=(entity>>8)&0xfff;
    if(!read(storage+8,begin)||!read(storage+16,end)||!begin||end<begin||(end-begin)/8>4096||
       pageIndex>=(end-begin)/8||!read(begin+pageIndex*8,page)||!page||
       !read(page+(entity&255)*4,index))return false;
    index&=0xfffff;
    if(index==0xfffff||!read(storage+0x20,packed)||!read(storage+0x28,packedEnd)||!packed||packedEnd<packed||
       (packedEnd-packed)/4>0x100000||index>=(packedEnd-packed)/4||!read(packed+index*4,full)||full!=entity||
       !read(storage+0x38,components)||!components||!read(components+uintptr_t(index)*0xd0+0xc0,parent))return false;
    return std::find(members.begin(),members.begin()+count,parent)==members.begin()+count;
}
inline bool SamePlayer(uint32_t localType,uint32_t localId,uint32_t ownerType,uint32_t ownerId) {
    return localType==0&&ownerType==0&&localId!=UINT32_MAX&&localId==ownerId;
}
// necromage1..4, emitted by the dedicated summoned necromage monster row.
// Check both fields so enemy elemental bolts and mercenaries cannot match.
template<class Read> uint32_t MageMissile(uintptr_t params,Read read) {
    uint32_t missile=~0u,header[2]{};uintptr_t owner=0;
    if(!read(params+0x20,missile)||missile<208||missile>211||
       !read(params+8,owner)||!owner||!read(owner,header)||header[0]!=1||header[1]!=364)return ~0u;
    return missile;
}
inline int ScaledVelocity(int native,float multiplier,float maximum=3.f) {
    if(native<=0||!std::isfinite(multiplier)||multiplier<1.f||multiplier>maximum)return native;
    const double value=double(native)*multiplier;
    return value<=std::numeric_limits<int>::max()?int(std::lround(value)):native;
}
}
