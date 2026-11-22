#pragma once
#include "loot_beams.h"
#include <algorithm>
namespace framevr::serverloot {
// Host game-thread only. Same documented server item fields as loot_beam_scan.cpp.
// The requester, not the host hero, supplies the position and level. Only copied
// values escape. This read-only report must never authorize game mutations.
template<class Read,class Level,class Rune>
int Scan(uintptr_t game,uintptr_t viewer,uint32_t player,const lootbeams::Wanted& wanted,
         std::array<lootbeams::Lying,lootbeams::MaxBeams>& out,Read read,Level level,Rune rune) {
 out={};
 auto get=[&](uintptr_t a,auto&v){return a>=0x10000&&a<=UINTPTR_MAX-sizeof v&&read(a,&v,sizeof v);};
 uint32_t h[4]{};uintptr_t back=0,path=0;uint16_t hero[4]{};
 if(!game||!viewer||!player||!get(viewer,h)||h[0]!=0||h[2]!=player||h[3]==0||h[3]==17||
    !get(viewer+0xd8,back)||back!=game||!get(viewer+0x38,path)||!get(path,hero))return -1;
 const int area=level(viewer);if(area<=0)return -1;
 uintptr_t buckets[128]{};if(!get(game+lootbeams::ServerItemTable,buckets))return -1;
 size_t count=0;unsigned visited=0;
 for(uintptr_t unit:buckets)for(unsigned chain=0;unit&&chain<256&&visited<4096;++chain,++visited){
  uintptr_t next=0,data=0,p=0,owner=0;uint32_t header[4]{};
  if(!get(unit+0x158,next)||!get(unit,header))break;
  uint32_t quality=0;int x=0,y=0;
  if(header[0]==4&&header[2]&&lootbeams::OnFloor(header[3])&&get(unit+0xd8,owner)&&owner==game&&
     get(unit+0x10,data)&&get(data,quality)&&get(unit+0x38,p)&&get(p+0x10,x)&&get(p+0x14,y)&&
     x>=0&&x<=65535&&y>=0&&y<=65535&&lootbeams::InRange(x,y,hero[1],hero[3])&&level(unit)==area){
   const auto beam=lootbeams::Classify(quality,rune(header[1]),wanted);
   bool duplicate=false;for(size_t i=0;i<count;++i)if(out[i].id==header[2])duplicate=true;
   if(beam!=lootbeams::Beam::None&&!duplicate){
    std::array<lootbeams::Lying,lootbeams::MaxBeams+1> candidates{};
    std::copy_n(out.begin(),count,candidates.begin());candidates[count]={header[2],x,y,beam};
    out=lootbeams::Select(candidates.data(),count+1,hero[1],hero[3],count);
   }
  }
  unit=next;
 }
 return int(count);
}
}
