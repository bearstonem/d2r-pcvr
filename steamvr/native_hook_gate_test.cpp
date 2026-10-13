#include "native_hook_gate.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
struct Event {
    HANDLE h=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Event(){assert(h);}
    ~Event(){CloseHandle(h);}
    void Set(){assert(SetEvent(h));}
    void Wait(){assert(WaitForSingleObject(h,5000)==WAIT_OBJECT_0);}
};
void WaitUntil(const auto& predicate){
    const auto deadline=GetTickCount64()+5000;
    while(!predicate()){assert(GetTickCount64()<deadline);Sleep(0);}
}
void DrainExistingReader(){
    framevr::NativeHookGate<> gate;
    Event entered,release,writerRequested,writerEntered;
    std::thread reader([&]{framevr::NativeHookGate<>::Call outer(gate);entered.Set();release.Wait();
        // Existing calls may reenter while the writer is waiting for them.
        framevr::NativeHookGate<>::Call nested(gate,false);
        assert(gate.HasActiveCalls());
    });
    entered.Wait();
    std::thread writer([&]{writerRequested.Set();assert(gate.BeginExclusive());writerEntered.Set();
        assert(!gate.HasActiveCalls());gate.EndExclusive();
    });
    writerRequested.Wait();
    assert(WaitForSingleObject(writerEntered.h,20)==WAIT_TIMEOUT);
    release.Set();reader.join();writer.join();
    assert(!gate.HasInFlightCalls());
}
void BlockedCallsAndOwnerReentry(){
    framevr::NativeHookGate<> gate;
    assert(gate.BeginExclusive());assert(!gate.BeginExclusive());
    std::atomic<unsigned> entered{0};
    std::vector<std::thread> readers;
    for(unsigned i=0;i<8;++i)readers.emplace_back([&]{framevr::NativeHookGate<>::Call call(gate);++entered;});
    WaitUntil([&]{return gate.HasInFlightCalls();});
    // A teardown must retain its hooks for these blocked callers, while a
    // replay snapshot must not mistake them for active command mutations.
    Sleep(20);assert(!gate.HasActiveCalls());assert(!entered.load());
    {framevr::NativeHookGate<>::Call owned(gate);assert(gate.HasActiveCalls());
        framevr::NativeHookGate<>::Call nested(gate,false);assert(gate.HasActiveCalls());}
    assert(!gate.HasActiveCalls());assert(gate.HasInFlightCalls());
    gate.EndExclusive();
    for(auto& reader:readers)reader.join();
    assert(entered==8&&!gate.HasInFlightCalls());
    // Reuse after the drained teardown boundary follows the Stop/Start path.
    assert(gate.BeginExclusive());gate.EndExclusive();
    {framevr::NativeHookGate<>::Call restarted(gate);assert(gate.HasActiveCalls());}
    assert(!gate.HasInFlightCalls());
}
template<size_t Slots> void ConcurrentExclusion(){
    framevr::NativeHookGate<Slots> gate;
    std::atomic<bool> stop{false};std::atomic<unsigned> ready{0};
    std::array<uint64_t,8> progress{};
    std::vector<std::thread> readers;
    unsigned first=0,second=0;
    for(size_t i=0;i<progress.size();++i)readers.emplace_back([&,i]{
        ++ready;
        do{typename framevr::NativeHookGate<Slots>::Call call(gate);
            assert(first==second);++progress[i];
        }while(!stop.load());
    });
    WaitUntil([&]{return ready==progress.size();});
    for(unsigned i=1;i<=250;++i){
        assert(gate.BeginExclusive());assert(!gate.HasActiveCalls());
        first=i;SwitchToThread();second=i;gate.EndExclusive();
    }
    stop=true;
    for(auto& reader:readers)reader.join();
    for(auto count:progress)assert(count>0);
    assert(first==250&&second==250&&!gate.HasInFlightCalls());
}
}
int main(){
    DrainExistingReader();BlockedCallsAndOwnerReentry();
    ConcurrentExclusion<256>();ConcurrentExclusion<2>();ConcurrentExclusion<1>();
    std::puts("native hook gate: concurrent exclusion, nested/owned calls, blocked teardown, collision and reader progress checks passed");
}
