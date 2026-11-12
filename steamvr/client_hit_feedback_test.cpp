#include "client_hit_feedback.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace framevr::clienthit;
int main(){
 Gate g;g.Arm(7,0,100);assert(g.Confirm(7,99)==-1);assert(g.Confirm(7,150)==0);assert(g.Confirm(7,151)==-1);
 g.Arm(7,1,200);assert(g.Confirm(8,250)==-1);g.Arm(7,1,200);assert(g.Confirm(7,1001)==-1);
 g.Arm(7,1,200);g.Clear();assert(g.Confirm(7,250)==-1);
 struct Unit{uint32_t type,cls,id,mode;};Unit a{0,1,44,1},d{1,2,55,1};
 unsigned char damage[0x140]{};uint16_t flags=1;int dealt=256;
 auto read=[](uintptr_t a,void*b,size_t n){if(!a)return false;memcpy(b,(const void*)a,n);return true;};
 LifeLoss loss{uintptr_t(&d)};int before=512,after=256;
 unsigned calls=0,events=0;auto original=[&]{++calls;loss.Write(uintptr_t(&d),6,0,true,before,dealt?after:before);memcpy(damage+4,&flags,2);memcpy(damage+0x134,&dealt,4);return uint64_t(123);};
 auto publish=[&](uint32_t who,uint32_t target){assert(who==44&&target==55);++events;};
 auto hit=[&](int missile){loss={uintptr_t(&d)};return Observe(uintptr_t(&a),uintptr_t(&d),missile,uintptr_t(damage),read,original,[&]{return loss.lost;},publish);};
 assert(hit(0)==123&&calls==1&&events==1);dealt=0;hit(0);assert(events==1);
 dealt=256;flags=0x10;hit(0);assert(events==1);flags=1;hit(1);assert(events==1);
 a.type=1;hit(0);assert(events==1);a.type=0;d.type=0;hit(0);assert(events==1);
 d.type=1;flags=1|2;hit(0);assert(events==2&&calls==7);
 auto failRead=[](uintptr_t,void*,size_t){return false;};
 Observe(uintptr_t(&a),uintptr_t(&d),0,uintptr_t(damage),failRead,original,[&]{return loss.lost;},publish);assert(calls==8&&events==2);
 after=before;hit(0);assert(events==2); // positive damage record but fully absorbed
 after=0;hit(0);assert(events==3); // killing life write
 LifeLoss strict{55};strict.Write(56,6,0,true,100,0);strict.Write(55,8,0,true,100,0);
 strict.Write(55,6,1,true,100,0);strict.Write(55,6,0,false,100,0);strict.Write(55,6,0,true,100,120);assert(!strict.lost);
 strict.Write(55,6,0,true,100,0);assert(strict.lost);
 puts("client hit feedback: actual damage only, kills, misses, blocks, spells, identity, hand, expiry and reset passed");
}
