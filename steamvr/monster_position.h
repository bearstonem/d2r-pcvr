#pragma once
#include "world_coordinates.h"
#include <cstdint>

namespace framevr::immunity {
// Read-only equivalent of the native label's unit -> model -> root transform
// lookup (76A7D0/76B300/970530 in 3.3.93787). All pointers stay on the game thread.
template<class Read> bool RenderedPosition(uintptr_t manager,uintptr_t registry,
                                         uint32_t transformType,uintptr_t unit,
                                         Read read,float out[3]) {
    uint64_t key=0,buckets=0,count=0;
    if(!manager||!registry||!unit||!read(unit+0xd8,key)||!key||
       !read(manager+0x1738,count)||!count||count>65536||(count&(count-1))||
       !read(manager+0x1740,buckets)||!buckets)return false;
    uint64_t hash=key;
    hash=(hash^(hash>>33))*0xff51afd7ed558ccdULL;
    hash=(hash^(hash>>33))*0xc4ceb9fe1a85ec53ULL;hash^=hash>>33;
    uintptr_t node=0,model=0;
    if(!read(buckets+(hash&(count-1))*8,node))return false;
    for(unsigned i=0;node&&i<128;++i) {
        uint64_t candidate=0;
        if(!read(node+8,candidate))return false;
        if(candidate==key) {if(!read(node+0x18,model))return false;break;}
        if(!read(node,node))return false;
    }
    uintptr_t control=0;uint32_t references=0;
    if(!model||!read(node+0x20,control)||
       (control&&(!read(control+8,references)||!references)))return false;
    uint32_t entity=0,back=0,index=0;
    uintptr_t alive=0,aliveEnd=0,pools=0,poolsEnd=0,storage=0;
    if(!model||!read(model+0xa8,entity)||(entity&0xfffff)==0xfffff||
       !read(registry+0x30,alive)||!read(registry+0x38,aliveEnd)||aliveEnd<alive||
       (aliveEnd-alive)/4>0x100000||(entity&0xfffff)>=(aliveEnd-alive)/4||
       !read(alive+(entity&0xfffff)*4,back)||back!=entity||
       !read(registry,pools)||!read(registry+8,poolsEnd)||poolsEnd<pools||
       (poolsEnd-pools)%0x28||(poolsEnd-pools)/0x28>4096||
       transformType>=(poolsEnd-pools)/0x28||
       !read(pools+transformType*0x28+0x20,storage)||!storage)return false;
    uintptr_t pages=0,pagesEnd=0,page=0,packed=0,packedEnd=0,components=0;
    const uint32_t pageIndex=(entity>>8)&0xfff;
    if(!read(storage+8,pages)||!read(storage+16,pagesEnd)||pagesEnd<pages||
       (pagesEnd-pages)/8>4096||pageIndex>=(pagesEnd-pages)/8||
       !read(pages+pageIndex*8,page)||!page||!read(page+(entity&255)*4,index))return false;
    index&=0xfffff;
    if(index==0xfffff||!read(storage+0x20,packed)||!read(storage+0x28,packedEnd)||
       packedEnd<packed||(packedEnd-packed)/4>0x100000||index>=(packedEnd-packed)/4||
       !read(packed+index*4,back)||back!=entity||!read(storage+0x38,components)||!components)return false;
    // +0x40 is the composed world matrix used by native labels, including parents.
    coordinates::Vec point;
    if(!read(components+uintptr_t(index)*0xd0+0x70,point)||!coordinates::Finite(point))return false;
    out[0]=point.x;out[1]=point.y;out[2]=point.z;return true;
}
}
