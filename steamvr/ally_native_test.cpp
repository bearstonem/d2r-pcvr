#include "ally_native.h"
#include "ally_health_position.h"
#include <cassert>
#include <vector>
#include <cstdio>
int main(){using namespace framevr;constexpr uintptr_t base=0x100000;std::vector<uint8_t> mem(0x10000);
 auto put=[&](uintptr_t p,const auto& v){std::memcpy(mem.data()+p-base,&v,sizeof v);};
 auto read=[&](uintptr_t p,void*d,size_t n){if(p<base||p-base>mem.size()||n>mem.size()-(p-base))return false;std::memcpy(d,mem.data()+p-base,n);return true;};
 const uintptr_t unit=base+0x2000,path=base+0x3000,data=base+0x4000;uint32_t type=1,id=22,cls=271,mode=1,owner=9;
 put(base+0x400,unit);put(unit,type);put(unit+4,cls);put(unit+8,id);put(unit+12,mode);put(unit+0x10,data);put(data+0x54,owner);put(unit+0x38,path);
 uint16_t xy[4]={32768,100,0,200};put(path,xy);allynative::Unit found{};unsigned count=0;
 assert(allynative::Scan(base,1,read,[&](auto u){found=u;++count;}));assert(count==1&&found.x==100.5f&&found.z==200);
 assert(allynative::OwnMercenary(found,9)&&!allynative::OwnMercenary(found,10));found.owner=UINT32_MAX;assert(!allynative::OwnMercenary(found,9));
 found.owner=9;found.cls=1;assert(!allynative::OwnMercenary(found,9));
 put(unit+0x158,unit);assert(!allynative::Scan(base,1,read,[](auto){}));
 allyhealth::Entry e;e.id=22;std::strcpy(e.name.data(),"Mercenary");e.health=50;e.maxHealth=100;e.positionKnown=e.gridPosition=true;e.world={100,0,200};
 math::Vec out;assert(allyhealth::Overhead(e,{0,1.6f,0},{200,0,400},{0,0,0,1},1,1.6f,{0,1.6f,0},out));assert(std::abs(out.y-2)<.001f);
 e.positionKnown=false;assert(!allyhealth::Overhead(e,{},{},{0,0,0,1},1,1.6f,{},out));
 // Wrist row is centered below the Merc footer, with compact width.
 const auto wrist=allyhealth::WristOffset(.24f,.04f,1);
 assert(std::abs(wrist.x)<.00001f);
 assert(wrist.y<-.12f-.04f&&allyhealth::WristWidth<.20f);
 const auto two=allyhealth::WristOffset(.24f,.04f,2);
 assert(two.y<wrist.y);
 // Distance must not shrink angular label width.
 assert(std::abs(allyhealth::OverheadWidth({0,0,10},{})/10-allyhealth::OverheadWidth({0,0,2},{})/2)<.00001f);
 std::puts("ally native: exact owner/class, bounded/cyclic hash, grid and overhead position passed");
}
