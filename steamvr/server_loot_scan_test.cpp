#include "server_loot_scan.h"
#include <cassert>
#include <map>
#include <cstring>
#include <cstdio>
using namespace framevr;
struct Memory {
 std::map<uintptr_t,unsigned char> b;
 template<class T> void put(uintptr_t p,const T& v){auto* a=reinterpret_cast<const unsigned char*>(&v);for(size_t i=0;i<sizeof v;++i)b[p+i]=a[i];}
 bool read(uintptr_t p,void* v,size_t n){auto*a=static_cast<unsigned char*>(v);for(size_t i=0;i<n;++i){auto it=b.find(p+i);if(it==b.end())return false;a[i]=it->second;}return true;}
};
int main(){
 constexpr uintptr_t game=0x100000,viewer=0x200000,first=0x300000;
 Memory m;uint32_t hero[4]{0,1,77,1};m.put(viewer,hero);m.put(viewer+0xd8,game);m.put(viewer+0x38,uintptr_t(viewer+0x200));
 uint16_t xy[4]{0,40000,0,20};m.put(viewer+0x200,xy);
 std::array<uintptr_t,128> buckets{};buckets[0]=first;
 auto item=[&](int i,uint32_t quality,int x,uint32_t mode=3){uintptr_t u=first+i*0x1000;uint32_t h[4]{4,uint32_t(100+i),uint32_t(500+i),mode};m.put(u,h);m.put(u+0xd8,game);m.put(u+0x10,u+0x300);m.put(u+0x300,quality);m.put(u+0x38,u+0x200);m.put(u+0x200+0x10,x);m.put(u+0x200+0x14,20);m.put(u+0x158,uintptr_t(0));return u;};
 item(0,6,40002);item(1,5,40004);item(2,7,40006);item(3,2,40008);item(4,6,40001,0);item(5,6,40001);item(6,6,40001);item(7,6,40100);
 for(int i=0;i<7;++i)m.put(first+i*0x1000+0x158,first+(i+1)*0x1000);
 m.put(first+6*0x1000+0xd8,uintptr_t(game+1));m.put(game+lootbeams::ServerItemTable,buckets);
 auto read=[&](uintptr_t p,void*v,size_t n){return m.read(p,v,n);};
 auto level=[&](uintptr_t u){return u==first+5*0x1000?4:3;};
 auto rune=[](uint32_t c){return c==103?26:0;};
 lootbeams::Wanted wanted;std::array<lootbeams::Lying,8> out{};
 auto scan=[&]{return serverloot::Scan(game,viewer,77,wanted,out,read,level,rune);};
 assert(scan()==4&&out[0].id==500&&out[3].beam==lootbeams::Beam::Rune);
 wanted.rare=false;assert(scan()==3&&out[0].beam==lootbeams::Beam::Set);wanted.rare=true;
 wanted.highRune=27;assert(scan()==3);wanted.highRune=26;
 // Requester identity and level are mandatory, even with valid nearby loot.
 assert(serverloot::Scan(game,viewer,78,wanted,out,read,level,rune)==-1);
 m.put(viewer+0xc,uint32_t(17));assert(scan()==-1);m.put(viewer+0xc,uint32_t(1));
 m.put(viewer+0xd8,uintptr_t(game+1));assert(scan()==-1);m.put(viewer+0xd8,game);
 auto unknown=[](uintptr_t){return 0;};assert(serverloot::Scan(game,viewer,77,wanted,out,read,unknown,rune)==-1);
 // Unreadable quality is never guessed rare. A cycle stays bounded and deduped.
 m.b.erase(first+0x300);assert(scan()==3);m.put(first+0x300,uint32_t(6));
 m.put(first+7*0x1000+0x158,first);assert(scan()==4);
 buckets.fill(0);m.put(game+lootbeams::ServerItemTable,buckets);assert(scan()==0&&out[0].id==0);
 // Nearest items must not be crowded out by the first 64 far-away items.
 buckets[0]=first;for(int i=0;i<80;++i){auto u=item(i,6,i==79?40000:40050);if(i<79)m.put(u+0x158,u+0x1000);}m.put(game+lootbeams::ServerItemTable,buckets);
 assert(scan()==8&&out[0].id==579);
 puts("server loot scan: requester, same-level, rarity/rune filters, pickup, empty, unreadable, cycles and nearest-eight passed");
}
