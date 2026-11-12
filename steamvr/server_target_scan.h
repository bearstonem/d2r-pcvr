#pragma once
#include "target_cone.h"
#include <algorithm>
#include <cstddef>
namespace framevr::servertarget {
// Game-thread only. Reader/call adapters are shared by production and tests.
// No pointer leaves this bounded scan. Native relation: nonzero means hostile.
template<class Read,class Lookup,class Relation,class Line,class Level>
int Scan(uintptr_t game,uint32_t playerId,targetcone::Target*out,int cap,float range,
         float&heroX,float&heroY,Read read,Lookup lookup,Relation relation,Line line,Level level) {
    if(!game||!playerId||!out||cap<=0||!std::isfinite(range)||range<0)return -1;
    cap=std::min(cap,targetcone::MaxTargets);
    auto get=[&](uintptr_t a,auto&v){return read(a,&v,sizeof v);};
    auto position=[&](uintptr_t u,float&x,float&y){
        uintptr_t path=0;uint16_t xy[4]{};
        if(!get(u+0x38,path)||path<0x10000||!get(path,xy))return false;
        x=xy[1]+xy[0]/65536.f;y=xy[3]+xy[2]/65536.f;return true;
    };
    const uintptr_t viewer=lookup(game,0,playerId);
    uint32_t header[4]{};uintptr_t back=0;
    if(!viewer||!get(viewer,header)||header[0]!=0||header[2]!=playerId||header[3]==0||header[3]==17||
       !get(viewer+0xd8,back)||back!=game||!position(viewer,heroX,heroY))return -1;
    const int area=level(viewer);if(area<=0)return -1;
    uintptr_t buckets[128]{};if(!get(game+0x4c6*8,buckets))return -1;
    uintptr_t units[targetcone::MaxTargets]{};float distance[targetcone::MaxTargets]{};
    int count=0;unsigned visited=0;
    for(uintptr_t unit:buckets){
        for(unsigned chain=0;unit&&chain<256&&visited<4096;++chain,++visited){
            uintptr_t next=0;uint32_t h[4]{};
            if(!get(unit+0x158,next)||!get(unit,h))break;
            uintptr_t owner=0,data=0,stats=0;uint32_t flags=0;float x=0,y=0;
            if(h[0]==1&&h[2]&&h[3]!=0&&h[3]!=12&&get(unit+0xd8,owner)&&owner==game&&
               position(unit,x,y)&&level(unit)==area&&get(unit+0x10,data)&&data>=0x10000&&
               get(data,stats)&&stats>=0x10000&&get(stats+0x3c,flags)&&!(flags&0x500)){
                const float d=std::hypot(x-heroX,y-heroY);
                if(d<=range&&relation(game,viewer,unit)!=0){
                    bool duplicate=false;for(int i=0;i<count;++i)if(out[i].id==h[2])duplicate=true;
                    const int slot=duplicate?-1:targetcone::NearestSlot(distance,count,cap,d);
                    if(slot>=0){distance[slot]=d;units[slot]=unit;out[slot]={h[2],x,y,false};if(slot==count)++count;}
                }
            }
            unit=next;
        }
    }
    for(int i=0;i<count;++i)out[i].clear=line(viewer,units[i],0x804)==0;
    return count;
}
}
