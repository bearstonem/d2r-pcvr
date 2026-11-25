#pragma once
#include "target_cone.h"
#include <algorithm>
#include <cstddef>
namespace framevr::servertarget {
// Read once per bucket node on the game thread. The header and next link
// already had to be readable to advance, and their span is smaller than a page.
struct MonsterNode {
    uint32_t type, classId, id, mode;
    uintptr_t data;
    uint8_t reserved18[0x38-0x18];
    uintptr_t path;
    uint8_t reserved40[0xd8-0x40];
    uintptr_t game;
    uint8_t reservedE0[0x158-0xe0];
    uintptr_t next;
};
static_assert(offsetof(MonsterNode,data)==0x10 && offsetof(MonsterNode,path)==0x38 &&
              offsetof(MonsterNode,game)==0xd8 && offsetof(MonsterNode,next)==0x158 &&
              sizeof(MonsterNode)==0x160);

// Read-only game-thread body dimensions; caller verifies the native stats signature.
template<class Read,class Stats>
bool ReadBody(uintptr_t unit,targetcone::Target& target,Read read,Stats stats) {
    target.bodyWidth=target.bodyHeight=0;
    uint32_t header[4]{};uint8_t bank=0,fields[16]{};
    if(!unit||!read(unit,header,sizeof header)||header[0]!=1||header[2]!=target.id||
       header[3]==0||header[3]==12||header[1]>4095||!read(unit+0x1bd,&bank,sizeof bank)||bank>3)return false;
    const uintptr_t row=stats(bank,int(header[1]));
    if(!row||!read(row,fields,sizeof fields)||fields[8]<1||fields[8]>8||fields[9]<1||fields[9]>8)return false;
    target.bodyWidth=std::max(fields[8],fields[9]);target.bodyHeight=fields[13]?fields[13]:72;
    return true;
}
// Game-thread only. Reader/call adapters are shared by production and tests.
// No pointer leaves this bounded scan. Native relation: nonzero means hostile.
template<class Read,class Lookup,class Relation,class Line,class Level>
int Scan(uintptr_t game,uint32_t playerId,targetcone::Target*out,int cap,float range,
         float&heroX,float&heroY,Read read,Lookup lookup,Relation relation,Line line,Level level) {
    if(!game||!playerId||!out||cap<=0||!std::isfinite(range)||range<0)return -1;
    cap=std::min(cap,targetcone::MaxTargets);
    auto get=[&](uintptr_t a,auto&v){return read(a,&v,sizeof v);};
    auto position=[&](uintptr_t path,float&x,float&y){
        uint16_t xy[4]{};
        if(path<0x10000||!get(path,xy))return false;
        x=xy[1]+xy[0]/65536.f;y=xy[3]+xy[2]/65536.f;return true;
    };
    const uintptr_t viewer=lookup(game,0,playerId);
    uint32_t header[4]{};uintptr_t back=0,viewerPath=0;
    if(!viewer||!get(viewer,header)||header[0]!=0||header[2]!=playerId||header[3]==0||header[3]==17||
       !get(viewer+0xd8,back)||back!=game||!get(viewer+0x38,viewerPath)||!position(viewerPath,heroX,heroY))return -1;
    const int area=level(viewer);if(area<=0)return -1;
    uintptr_t buckets[128]{};if(!get(game+0x4c6*8,buckets))return -1;
    uintptr_t units[targetcone::MaxTargets]{};float distance[targetcone::MaxTargets]{};
    int count=0;unsigned visited=0;
    for(uintptr_t unit:buckets){
        for(unsigned chain=0;unit&&chain<256&&visited<4096;++chain,++visited){
            MonsterNode node;
            if(!get(unit,node))break;
            uintptr_t stats=0;uint32_t flags=0;float x=0,y=0;
            if(node.type==1&&node.id&&node.mode!=0&&node.mode!=12&&node.game==game&&
               position(node.path,x,y)&&level(unit)==area&&node.data>=0x10000&&
               get(node.data,stats)&&stats>=0x10000&&get(stats+0x3c,flags)&&!(flags&0x500)){
                const float d=std::hypot(x-heroX,y-heroY);
                if(d<=range&&relation(game,viewer,unit)!=0){
                    bool duplicate=false;for(int i=0;i<count;++i)if(out[i].id==node.id)duplicate=true;
                    const int slot=duplicate?-1:targetcone::NearestSlot(distance,count,cap,d);
                    if(slot>=0){distance[slot]=d;units[slot]=unit;out[slot]={node.id,x,y,false};if(slot==count)++count;}
                }
            }
            unit=node.next;
        }
    }
    for(int i=0;i<count;++i)out[i].clear=line(viewer,units[i],0x804)==0;
    return count;
}
}
