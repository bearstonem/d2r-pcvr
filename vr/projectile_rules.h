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
// The Amazon's bow skills' own arrows (Missiles.txt rows, D2R 3.3's own table): Fire Arrow 12, Magic
// Arrow 27, Ice Arrow 28, Cold Arrow 40, Exploding Arrow 41, Immolation Arrow 85, Guided Arrow 86,
// Freezing Arrow 87, Multiple Shot's multipleshotarrow 213 and Strafe's strafearrow 682 (Skills.txt
// srvmissilea). The owner, 2026-11-08: Guided Arrow and Multiple Shot "are moving very slowly like the
// base game arrows ... it should apply to all her skills as well, they should all move like that" - so
// the hero's own arrows of these skills take the arrow speed setting however they are cast, as basic
// arrows do. Bolts (31, multipleshotbolt 214, strafebolt 683) keep the game's speed, as the basic bolt.
inline bool BowSkillArrow(uint32_t missile) {
    switch(missile) {
        case 12:case 27:case 28:case 40:case 41:case 85:case 86:case 87:case 213:case 682:return true;
        default:return false;
    }
}
inline int ScaledVelocity(int native,float multiplier) {
    if(native<=0||!std::isfinite(multiplier)||multiplier<1.f||multiplier>3.f)return native;
    const double value=double(native)*multiplier;
    return value<=std::numeric_limits<int>::max()?int(std::lround(value)):native;
}
}
