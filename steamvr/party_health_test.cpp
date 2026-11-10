#include "party_health.h"
#include <cassert>
#include <limits>
int main(){using namespace framevr::partyhealth;Sample a{55,100},b;uint8_t wire[kPacketBytes]{};size_t n=0;
 assert(Encode(a,wire,sizeof wire,&n)&&n==12&&Decode(wire,n,&b));assert(b.health==55&&b.maxHealth==100);
 wire[2]=2;assert(!Decode(wire,n,&b));wire[2]=1;wire[3]=1;assert(!Decode(wire,n,&b));
 a.health=std::numeric_limits<float>::quiet_NaN();assert(!Encode(a,wire,sizeof wire,&n));
 Playback p;assert(p.Push(7,1,{50,100},100));assert(!p.Push(7,1,{99,100},900));Remote list[16];assert(p.List(list,16,1101)==0);
 assert(p.Push(7,1,{25,100},1200));assert(p.List(list,16,1200)==1&&list[0].pose.health==25);
 Report r{1,100,7,50,100,0,0,0,false,true};assert(r.Current(1,100));assert(!r.Current(2,100)&&!r.Current(1,99)&&!r.Current(1,1101));
}
