#pragma once
#include "native_lock.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace framevr {
// Native command recording is spread across several game threads. A shared
// invocation counter or SRW reader lock makes every binding write the same
// cache line. Give each thread a separated slot instead; only replay's writer
// scans the reader slots. Slot collisions remain correct (just less efficient).
template<size_t SlotCount=256> class NativeHookGate {
    static_assert(SlotCount>0);
    struct alignas(64) Slot {
        std::atomic<uint32_t> inFlight{0},active{0},readers{0};
    };
    std::array<Slot,SlotCount> slots;
    alignas(64) std::atomic<DWORD> owner{0};
    NativeMutex writerMutex;
    inline static std::atomic<size_t> nextSlot{0};

    static size_t ThreadSlot()noexcept {
        thread_local const size_t index=nextSlot.fetch_add(1)%SlotCount;
        return index;
    }
    void LeaveReader(Slot& slot)noexcept {
        slot.readers.fetch_sub(1);
        if(owner.load())WakeByAddressAll(&slot.readers);
    }
public:
    class Call {
        NativeHookGate& gate;
        Slot& slot;
        bool reader=false;
    public:
        // A nested hook is already covered by its outer call's reader lease.
        // Calls made by replay's owning thread are allowed through as well.
        explicit Call(NativeHookGate& value,bool outer=true)noexcept:
            gate(value),slot(value.slots[ThreadSlot()]) {
            slot.inFlight.fetch_add(1);
            if(outer)for(;;){
                slot.readers.fetch_add(1);
                DWORD writer=gate.owner.load();
                if(!writer){reader=true;break;}
                gate.LeaveReader(slot);
                if(writer==GetCurrentThreadId())break;
                // Increment-before-check plus seq_cst ordering ensures a
                // writer either drains us or prevents entry. Blocked calls
                // remain in-flight for teardown, but are not active captures.
                WaitOnAddress(&gate.owner,&writer,sizeof writer,INFINITE);
            }
            slot.active.fetch_add(1);
        }
        ~Call(){
            slot.active.fetch_sub(1);
            if(reader)gate.LeaveReader(slot);
            slot.inFlight.fetch_sub(1);
        }
        Call(const Call&)=delete;
        Call& operator=(const Call&)=delete;
    };
    bool BeginExclusive()noexcept {
        const DWORD thread=GetCurrentThreadId();
        if(owner.load()==thread)return false;
        writerMutex.lock();owner.store(thread);
        for(auto& slot:slots)for(;;){
            auto readers=slot.readers.load();
            if(!readers)break;
            WaitOnAddress(&slot.readers,&readers,sizeof readers,INFINITE);
        }
        return true;
    }
    void EndExclusive()noexcept {
        if(owner.load()!=GetCurrentThreadId())return;
        owner.store(0);WakeByAddressAll(&owner);writerMutex.unlock();
    }
    bool HasActiveCalls()const noexcept {
        // Exact while this thread owns the exclusive lease. Outside that lease
        // this is diagnostic only: a cross-slot handoff can evade the scan.
        for(const auto& slot:slots)if(slot.active.load())return true;
        return false;
    }
    bool HasInFlightCalls()const noexcept {
        for(const auto& slot:slots)if(slot.inFlight.load())return true;
        return false;
    }
    NativeHookGate()=default;
    NativeHookGate(const NativeHookGate&)=delete;
    NativeHookGate& operator=(const NativeHookGate&)=delete;
};
}
