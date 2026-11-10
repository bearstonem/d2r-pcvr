#include "hook_metrics.h"
#include "generation_cache.h"
#include "native_lock.h"
#include "pipeline_set.h"
#include "foveation_switch.h"
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>
using namespace framevr;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static uint64_t Sum(const hookmetrics::Snapshot& s,hookmetrics::Kind kind) {
    uint64_t n=0;for(const auto& t:s)n+=t.counters[kind].calls;return n;
}
int main() {
    {hookmetrics::Scope disabled(hookmetrics::MemoryWrite,0,64);}
    Check(hookmetrics::allocated==0,"disabled diagnostics do not even allocate a thread slot");
    auto baseline=hookmetrics::Start(1);
    std::atomic<bool> done=false;
    std::thread reader([&]{while(!done){auto snapshot=hookmetrics::Read();(void)snapshot;}});
    std::vector<std::thread> workers;
    for(int t=0;t<8;++t)workers.emplace_back([]{
        for(int i=0;i<10000;++i){hookmetrics::Scope scope(hookmetrics::MemoryWrite,63,64);}
    });
    for(auto& t:workers)t.join();done=true;reader.join();hookmetrics::Stop();
    auto first=hookmetrics::Read();
    Check(Sum(first,hookmetrics::MemoryWrite)-Sum(baseline,hookmetrics::MemoryWrite)==80000,"all worker calls included without shared counter contention");
    unsigned observed=0;
    for(const auto& t:first)if(t.id){++observed;const auto& c=t.counters[hookmetrics::MemoryWrite];
        Check(c.calls==10000&&c.bytes==640000&&c.samples==156,"independent per-thread counts, bytes and sample cadence");}
    Check(observed==8,"workers remain identifiable after thread exit");
    {hookmetrics::Scope disabled(hookmetrics::MemoryWrite,0);}
    Check(Sum(first,hookmetrics::MemoryWrite)==Sum(hookmetrics::Read(),hookmetrics::MemoryWrite),"stopped counters idle");
    baseline=hookmetrics::Start(2);
    {
        hookmetrics::Scope crossing(hookmetrics::MemoryRead,0,16);
        hookmetrics::Stop();hookmetrics::Start(3);
    }
    for(const auto& t:hookmetrics::Read())Check(t.counters[hookmetrics::MemoryRead].samples==0,"old timed scope cannot contaminate a new capture");
    hookmetrics::Stop();
    GenerationCache<bool,2> cache;bool value=false;
    Check(!cache.Get(0,0,value),"empty cache does not match zero-valued key/generation");
    cache.Put(0,1,false);Check(cache.Get(0,1,value)&&!value,"negative lookup cached");
    cache.Put(0,2,true);Check(!cache.Get(0,1,value)&&cache.Get(0,2,value)&&value,"creation or handle reuse invalidates old result");
    cache.Put(2,2,false);Check(!cache.Get(0,2,value)&&cache.Get(2,2,value)&&!value,"colliding keys cannot inherit another pipeline classification");
    NativeMutex mutex;uint64_t sum=0;workers.clear();
    for(int t=0;t<8;++t)workers.emplace_back([&]{for(int i=0;i<10000;++i){std::lock_guard lock(mutex);++sum;}});
    for(auto& t:workers)t.join();
    {std::shared_lock lock(mutex);Check(sum==80000,"native callback lock serializes writes and supports shared reads");}
    {   // The pipelines a bind callback replaces: every other bind is rejected without a lock.
        PipelineSet<4> set;
        Check(!set.MayContain(0)&&!set.MayContain(0x1000),"an empty set rejects every bind, the null pipeline too");
        set.Add(0);Check(!set.MayContain(0),"the null pipeline is never interesting");
        set.Add(0x1000);set.Add(0x2000);set.Add(0x1000);
        Check(set.MayContain(0x1000)&&set.MayContain(0x2000)&&!set.MayContain(0x3000),"created copies are found; other pipelines are not");
        set.Remove(0x1000);
        Check(!set.MayContain(0x1000)&&set.MayContain(0x2000),"a destroyed pipeline's handle, used again by the game, is not taken for the copy's original");
        set.Add(0x3000);set.Remove(0x3000);set.Add(0x1000);
        Check(set.MayContain(0x1000)&&!set.MayContain(0x3000)&&!set.MayContain(0),"freed slots are used again and hold nothing meanwhile");
        set.Remove(0x1000);set.Remove(0x2000);
        Check(!set.MayContain(0x1000)&&!set.MayContain(0x2000),"listed once: one removal takes a handle out");
        for(uint64_t h=1;h<=4;++h)set.Add(h*0x10);
        Check(set.MayContain(0x40)&&!set.MayContain(0x50),"a full set still rejects");
        set.Add(0x50);
        Check(set.MayContain(0x50)&&set.MayContain(0x9990),"past its size nothing is rejected: the caller's own lookup decides, never a missed copy");
        PipelineSet<> shared;std::atomic<bool> stop=false;std::atomic<uint64_t> wrong{0};
        shared.Add(0x7000);
        std::vector<std::thread> readers;
        for(int t=0;t<4;++t)readers.emplace_back([&]{while(!stop){if(!shared.MayContain(0x7000)||shared.MayContain(0x7008))++wrong;}});
        for(uint64_t i=0;i<20000;++i){shared.Add(0x8000+i*16);shared.Remove(0x8000+i*16);}
        stop=true;for(auto& t:readers)t.join();
        Check(wrong==0,"recording threads read the set while pipelines are created and destroyed");
    }
    {   // Foveation off: draws and binds return on one load; on again without stale command-list state.
        foveation::TrackingSwitch gate;
        const auto first=gate.Stamp();
        Check(gate.On()&&first!=0&&gate.Current(first)&&!gate.Current(0),"on from the start: lists reset now are followed; a list never reset is not");
        gate.Set(true);Check(gate.Current(first),"the same setting again changes nothing");
        gate.Set(false);
        Check(gate.On()&&gate.Current(first),"off: lists in flight are still followed, so their rate image is taken off");
        for(int i=0;i<foveation::TrackingSwitch::GraceEyes-1;++i)gate.EyePass();
        Check(gate.On(),"until the grace has passed");
        gate.Set(true);for(int i=0;i<3*foveation::TrackingSwitch::GraceEyes;++i)gate.EyePass();
        Check(gate.On()&&gate.Current(first),"on again within the grace: nothing was missed, every list stays current");
        gate.Set(false);for(int i=0;i<foveation::TrackingSwitch::GraceEyes;++i)gate.EyePass();
        Check(!gate.On()&&gate.Stamp()==0,"off after the grace: callbacks return early, lists reset meanwhile are unstamped");
        for(int i=0;i<100;++i)gate.EyePass();gate.Set(false);
        Check(!gate.On(),"and stays off");
        gate.Set(true);
        Check(gate.On()&&!gate.Current(first)&&!gate.Current(0),"on mid-session: a list reset before or during the off time has stale state and waits for its reset");
        const auto second=gate.Stamp();
        Check(second!=0&&second!=first&&gate.Current(second),"a list reset after the switch is followed");
        std::atomic<bool> stop=false;std::atomic<uint64_t> wrong{0};
        std::thread recorder([&]{uint64_t last=second;while(!stop){const auto stamp=gate.Stamp();if(stamp){if(stamp<last)++wrong;last=stamp;}}});
        for(int i=0;i<2000;++i){gate.Set(i&1);for(int e=0;e<foveation::TrackingSwitch::GraceEyes;++e)gate.EyePass();}
        stop=true;recorder.join();gate.Set(true);
        Check(wrong==0&&gate.On()&&gate.Current(gate.Stamp())&&!gate.Current(second),"switched while lists record: stamps only move on, and it ends on and current");
    }
    std::puts("All-thread counters, capture boundaries, generation invalidation, native lock stress, pipeline set and foveation switch passed");
}
