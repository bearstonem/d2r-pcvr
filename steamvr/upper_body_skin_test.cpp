#include "../vr/upper_body_skin.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>
using Fn=uintptr_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
Fn hook=nullptr;float captured=0;bool supplyTrampoline=false;
uintptr_t Original(uintptr_t,uintptr_t pose,uintptr_t,uintptr_t,uintptr_t){uintptr_t p=0;memcpy(&p,(void*)(pose+4),8);captured=*(float*)p;return 42;}
namespace d2rsig {
uintptr_t Addr(uint64_t){return 1;}
bool Check(uint64_t,const void*,uint32_t){return true;}
bool Hook(uint64_t,const void*,uint32_t,void*target,void**original){hook=reinterpret_cast<Fn>(target);*original=supplyTrampoline?reinterpret_cast<void*>(&Original):nullptr;return true;}
}
int main(){
 uint8_t current[32]{},previous[32]{};int n=1;float game[16]{},a[16]{},b[16]{};a[0]=3;b[0]=7;uintptr_t p=(uintptr_t)game;
 for(auto*header:{current,previous}){memcpy(header,&n,4);memcpy(header+4,&p,8);}
 upperbodyskin::Init(nullptr);assert(hook);upperbodyskin::Publish((uintptr_t)current,(uintptr_t)previous,a,1);
 assert(hook(0,(uintptr_t)current,0,0,0)==0);supplyTrampoline=true;
 upperbodyskin::Init(nullptr);upperbodyskin::Publish((uintptr_t)current,(uintptr_t)previous,a,1);
 assert(hook(0,(uintptr_t)current,0,0,0)==42&&captured==3);
 // Production owner/skeleton binding accepts a live rig and rejects rebinding.
 uint8_t owner[64]{},model[16]{};uintptr_t cp=(uintptr_t)current,pp=(uintptr_t)previous,mp=(uintptr_t)model,sk=123;
 memcpy(owner+0x20,&cp,8);memcpy(owner+0x28,&pp,8);memcpy(owner+8,&mp,8);memcpy(model+8,&sk,8);
 upperbodyskin::Clear();upperbodyskin::Publish(cp,pp,a,1,sk,(uintptr_t)owner);
 hook(sk,cp,0,0,0);assert(captured==3);
 uintptr_t other=456;memcpy(model+8,&other,8);hook(sk,cp,0,0,0);assert(captured==0);
 memcpy(model+8,&sk,8);uintptr_t zero=0;memcpy(owner+0x20,&zero,8);hook(sk,cp,0,0,0);assert(captured==0);
 memcpy(owner+0x20,&cp,8);hook(other,cp,0,0,0);assert(captured==0);
 upperbodyskin::Clear();upperbodyskin::Publish(cp,pp,a,1);
 // Same pose header rebound to a different matrix allocation must not inherit cached data.
 float replacement[16]{};replacement[0]=11;uintptr_t replacementPtr=(uintptr_t)replacement;
 memcpy(current+4,&replacementPtr,8);hook(0,(uintptr_t)current,0,0,0);assert(captured==11);
 memcpy(current+4,&p,8);
 upperbodyskin::Publish((uintptr_t)previous,(uintptr_t)current,b,1);
 hook(0,(uintptr_t)previous,0,0,0);assert(captured==7);
 hook(0,(uintptr_t)current,0,0,0);assert(captured==3);
 upperbodyskin::Clear();hook(0,(uintptr_t)current,0,0,0);assert(captured==0);
 upperbodyskin::Shutdown();upperbodyskin::Publish((uintptr_t)current,0,a,1);hook(0,(uintptr_t)current,0,0,0);assert(captured==0);
 puts("upper body skin layout and lifecycle tests passed");
}
