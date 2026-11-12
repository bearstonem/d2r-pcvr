#include "server_target_scan.h"
#include <cassert>
#include <cstring>
#include <map>
#include <array>
#include <cstdio>
#include <limits>
using namespace framevr;
struct Memory {
 std::map<uintptr_t,unsigned char> bytes;
 void put(uintptr_t a,const void* v,size_t n){for(size_t i=0;i<n;++i)bytes[a+i]=static_cast<const unsigned char*>(v)[i];}
 template<class T>void put(uintptr_t a,const T&v){put(a,&v,sizeof v);}
 bool read(uintptr_t a,void*out,size_t n){for(size_t i=0;i<n;++i){auto it=bytes.find(a+i);if(it==bytes.end())return false;static_cast<unsigned char*>(out)[i]=it->second;}return true;}
};
int main(){
 constexpr uintptr_t game=0x100000,player=0x200000;
 Memory m;std::array<uintptr_t,128>buckets{};
 auto unit=[&](uintptr_t u,uint32_t type,uint32_t id,uint32_t mode,uint16_t x,uint16_t y){
  const uint32_t h[4]{type,100,id,mode};m.put(u,h);m.put(u+0xd8,game);
  const uintptr_t path=u+0x200,data=u+0x300,stats=u+0x400;const uint16_t xy[4]{0,x,0,y};
  m.put(u+0x38,path);m.put(path,xy);m.put(u+0x10,data);m.put(data,stats);m.put(stats+0x3c,uint32_t(0));m.put(u+0x158,uintptr_t(0));
 };
 unit(player,0,77,1,40000,20);m.put(player+0x200,uint16_t(32768));
 constexpr uintptr_t first=0x300000;
 for(int i=0;i<7;++i){auto u=first+i*0x1000;unit(u,1,501+i,1,40002+i,20);if(i<6)m.put(u+0x158,u+0x1000);}
 buckets[0]=first;m.put(game+0x4c6*8,buckets);
 // friendly, NPC, dead, foreign game, other area, blocked enemy
 m.put(first+2*0x1000+0x400+0x3c,uint32_t(0x100));
 m.put(first+3*0x1000+12,uint32_t(12));
 m.put(first+4*0x1000+0xd8,uintptr_t(game+1));
 auto read=[&](uintptr_t a,void*b,size_t n){return m.read(a,b,n);};
 auto lookup=[&](uintptr_t g,int t,uint32_t id){assert(g==game&&t==0);return id==77?player:uintptr_t(0);};
 unsigned relations=0,lines=0;
 auto relation=[&](uintptr_t g,uintptr_t viewer,uintptr_t monster){assert(g==game&&viewer==player);++relations;return monster==first+0x1000?0:1;};
 auto line=[&](uintptr_t viewer,uintptr_t monster,int mask){assert(viewer==player&&mask==0x804);++lines;return monster==first+6*0x1000?1:0;};
 auto level=[&](uintptr_t u){return u==first+5*0x1000?4:3;};
 targetcone::Target out[64]{};float hx=0,hy=0;
 auto scan=[&](uint32_t id=77,int cap=64){return servertarget::Scan(game,id,out,cap,30,hx,hy,read,lookup,relation,line,level);};
 assert(scan()==2);assert(hx==40000.5f&&hy==20&&out[0].id==501&&out[0].clear&&out[1].id==507&&!out[1].clear);
 assert(relations==3&&lines==2);assert(targetcone::Pick(out,2,hx,hy,1,0,0)==0);
 assert(scan(77,1)==1&&out[0].id==501);assert(scan(0)==-1&&scan(88)==-1&&scan(77,0)==-1);
 m.put(player+12,uint32_t(17));assert(scan()==-1);m.put(player+12,uint32_t(1));
 m.put(player+0xd8,uintptr_t(game+1));assert(scan()==-1);m.put(player+0xd8,game);
 // Missing flags cannot turn an unreadable unit into an enemy.
 m.bytes.erase(first+0x400+0x3c);assert(scan()==1&&out[0].id==507);
 // Valid empty roster clears, rather than retaining the previous targets.
 buckets.fill(0);m.put(game+0x4c6*8,buckets);assert(scan()==0);
 auto unknown=[](uintptr_t){return 0;};assert(servertarget::Scan(game,77,out,64,30,hx,hy,read,lookup,relation,line,unknown)==-1);
 std::puts("server target scan: remote viewer, unsigned coordinates, relation, LOS, level, NPC/pet/dead filtering and fail-closed reads passed");
}
