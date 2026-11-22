#include "active_keys.h"
#include "native_lock.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {
using framevr::ActiveKeys;
void Correctness() {
    ActiveKeys active;
    constexpr uint64_t first=0x1000;
    assert(!active.MaybeContains(first));
    active.Add(first);assert(active.MaybeContains(first));
    // Discover a collision through the public query. Removing one entry must
    // preserve another entry in the same bucket, including pointer reuse.
    uint64_t collision=0;
    for(uint64_t key=first+16;key<first+65536;key+=16)
        if(active.MaybeContains(key)){collision=key;break;}
    assert(collision);
    active.Add(collision);active.Remove(first);
    assert(active.MaybeContains(collision));
    active.Remove(collision);assert(!active.MaybeContains(first));
    active.Add(first);assert(active.MaybeContains(first));active.Remove(first);

    // HUD scissors can move from map to chat on the same list while another
    // list in the bucket also draws a map. Resetting either drops just its ref.
    active.Update(first,false,false);
    active.Update(first,false,true);
    active.Update(first,true,true);
    active.Update(collision,false,true);
    active.Update(first,true,false);
    assert(active.MaybeContains(collision));
    active.Update(collision,true,false);
    assert(!active.MaybeContains(first));
    active.Update(first,false,true);
    assert(active.MaybeContains(first));
    active.Update(first,true,false);

    // Registry mutations are serialized. Readers can skip negative entries
    // without acquiring that mutex; a published live key must never be missed.
    framevr::NativeMutex mutex;std::vector<std::thread> workers;
    for(unsigned t=0;t<8;++t)workers.emplace_back([&,t]{
        const uint64_t key=0x100000+uint64_t(t)*16;
        for(unsigned i=0;i<10000;++i) {
            {std::lock_guard lock(mutex);active.Add(key);}
            assert(active.MaybeContains(key));
            {std::lock_guard lock(mutex);active.Remove(key);}
        }
    });
    for(auto& worker:workers)worker.join();
    for(unsigned t=0;t<8;++t)assert(!active.MaybeContains(0x100000+uint64_t(t)*16));
    std::puts("Active command-list filter: collisions, removal/reuse and concurrent membership passed");
}
void Benchmark() {
    // Model the HUD's negative path: most lists do not draw the UI at all.
    // This measures callback registry contention, not rendering or game FPS.
    constexpr unsigned iterations=100000;
    for(unsigned threads:{1u,8u})for(bool filtered:{false,true}) {
        framevr::NativeMutex mutex;ActiveKeys active;std::unordered_set<uint64_t> registry;
        std::atomic<unsigned> ready=0;std::atomic<bool> go=false;
        std::vector<std::thread> workers;
        for(unsigned t=0;t<threads;++t)workers.emplace_back([&,t]{
            const uint64_t key=0x1000+uint64_t(t)*16;++ready;
            while(!go.load(std::memory_order_acquire))std::this_thread::yield();
            for(unsigned i=0;i<iterations;++i) {
                if(filtered&&!active.MaybeContains(key))continue;
                std::lock_guard lock(mutex);assert(!registry.contains(key));
            }
        });
        while(ready!=threads)std::this_thread::yield();
        const auto start=std::chrono::steady_clock::now();go.store(true,std::memory_order_release);
        for(auto& worker:workers)worker.join();
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        std::printf("Isolated HUD registry benchmark: threads=%u filtered=%d aggregate wall ns/query=%.2f (not game FPS)\n",
            threads,filtered,double(ns)/(threads*iterations));
    }
}
}
int main(int argc,char** argv) {
    Correctness();
    if(argc==2&&std::strcmp(argv[1],"--benchmark")==0)Benchmark();
}
