#include "client_combat.h"
#include <D2RLPlugin/context.h>
#include <D2RLPlugin/threads.h>
#include <mutex>

namespace clientcombat {
namespace {
std::mutex mutex;
const D2RL::PluginContext* context=nullptr;
const D2RL::ThreadService* threads=nullptr;
uint64_t current=0,last=0,generation=0;
bool asked=false,native=false;
// The return value, not callback execution time, answers whether the loader
// owns an authoritative thread. A discarded or late callback changes nothing.
void __cdecl Probe(const D2RL::PluginContext*,void*) noexcept {}
}
void Init(const D2RL::PluginContext* ctx) {
    const D2RL::ThreadService* service=nullptr;
    if(ctx&&(ctx->QueryService(&service)!=D2RL::ServiceQueryResult::Success||
       !D2RL::HasThreadServiceField(service,D2RL::ThreadServiceRequiredSize)||!service->runOnGameThread))service=nullptr;
    std::lock_guard lock(mutex);
    context=ctx;threads=service;current=last=0;asked=native=false;++generation;
}
void Tick(uint64_t session,uint64_t now) {
    const D2RL::PluginContext* ctx=nullptr;
    const D2RL::ThreadService* service=nullptr;
    uint64_t ticket=0;
    {
        std::lock_guard lock(mutex);
        if(current!=session){current=session;last=0;asked=native=false;++generation;}
        if(!session||!context||!threads||(asked&&now>=last&&now-last<1000))return;
        asked=true;last=now;ticket=++generation;ctx=context;service=threads;
    }
    // Never call the loader or log while holding our state lock.
    const auto result=service->runOnGameThread(ctx,&Probe,nullptr);
    bool changed=false,value=false;
    {
        std::lock_guard lock(mutex);
        if(ticket!=generation||session!=current)return;
        const bool before=native;
        if(result==D2RL::Threads::Result::Unavailable)native=true;
        else if(result==D2RL::Threads::Result::Success)native=false;
        // Busy, OwnerInactive and other failures are not remote-client proof.
        changed=before!=native;value=native;
    }
    if(changed)ctx->LogInfo(value?
        "PCVR multiplayer: native client combat enabled (no authoritative game thread); release held controls once":
        "PCVR multiplayer: authoritative game thread available; original direct combat restored");
}
bool NativeOnly(uint64_t session) {
    std::lock_guard lock(mutex);
    return session&&session==current&&native;
}
}
