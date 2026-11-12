#include "../vr/client_combat.h"
#include <D2RLPlugin/context.h>
#include <D2RLPlugin/threads.h>
#include <cassert>
#include <cstdio>
#include <vector>
namespace {
D2RL::Threads::Result result=D2RL::Threads::Result::Success;
unsigned calls=0;
D2RL::ThreadService service{};
std::vector<D2RL::Threads::Callback> pending;
D2RL::ServiceQueryResult __cdecl Query(const D2RL::PluginContext*,D2RL::ServiceId id,uint32_t version,const void** out) noexcept {
 assert(id==D2RL::ServiceId::Thread&&version==1);*out=&service;return D2RL::ServiceQueryResult::Success;
}
D2RL::Threads::Result __cdecl Run(const D2RL::PluginContext*,D2RL::Threads::Callback cb,void*) noexcept {
 ++calls;if(result==D2RL::Threads::Result::Success)pending.push_back(cb);return result;
}
}
int main(){
 D2RL::PluginApi api{};api.apiSize=sizeof api;api.queryService=Query;
 D2RL::PluginContext ctx{};ctx.contextSize=sizeof ctx;ctx.api=&api;
 service.serviceSize=sizeof service;service.serviceVersion=1;service.runOnGameThread=Run;
 clientcombat::Init(&ctx);assert(!clientcombat::NativeOnly(7));
 result=D2RL::Threads::Result::Unavailable;clientcombat::Tick(7,100);
 assert(clientcombat::NativeOnly(7)&&!clientcombat::NativeOnly(8)&&!clientcombat::NativeOnly(0));
 clientcombat::Tick(7,1099);assert(calls==1);clientcombat::Tick(7,1100);assert(calls==2);
 result=D2RL::Threads::Result::Busy;clientcombat::Tick(8,1200);assert(!clientcombat::NativeOnly(8));
 result=D2RL::Threads::Result::Success;clientcombat::Tick(8,2200);assert(!clientcombat::NativeOnly(8));
 result=D2RL::Threads::Result::Unavailable;clientcombat::Tick(9,2300);assert(clientcombat::NativeOnly(9));
 for(auto cb:pending)cb(&ctx,nullptr);assert(clientcombat::NativeOnly(9)); // stale callbacks do nothing
 result=D2RL::Threads::Result::Success;clientcombat::Tick(9,3300);assert(!clientcombat::NativeOnly(9));
 for(auto r:{D2RL::Threads::Result::Busy,D2RL::Threads::Result::InvalidArgument,D2RL::Threads::Result::OwnerInactive,D2RL::Threads::Result::CallbackFault}){
   result=r;clientcombat::Tick(10+unsigned(r),4400+unsigned(r)*1000);assert(!clientcombat::NativeOnly(10+unsigned(r)));}
 clientcombat::Tick(0,10000);assert(!clientcombat::NativeOnly(9));
 clientcombat::Init(nullptr);clientcombat::Tick(7,11000);assert(!clientcombat::NativeOnly(7));
 service.serviceSize=8;clientcombat::Init(&ctx);clientcombat::Tick(7,12000);assert(!clientcombat::NativeOnly(7));
 std::puts("client combat: authority, throttling, transitions, stale callbacks and SDK guards passed");
}
