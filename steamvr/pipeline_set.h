#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace framevr {
// The few pipeline handles a bind_pipeline callback replaces, readable without
// a lock: ReShade calls every subscriber for every bind on every recording
// thread, and nearly all of them are for other pipelines. MayContain false is
// certain; true (also once more than Size were added) only sends the caller on
// to its own locked lookup. Add and Remove are called under the owner's lock,
// from init_pipeline / destroy_pipeline, so a handle is in the set before the
// game can bind it and out of it before its value can be used again.
template<size_t Size=16> class PipelineSet {
    std::array<std::atomic<uint64_t>,Size> slots{};
    std::atomic<uint32_t> used{0};       // slots ever filled; never shrinks
    std::atomic<bool> overflow{false};
public:
    bool MayContain(uint64_t handle) const {
        const uint32_t n=used.load(std::memory_order_acquire);
        for(uint32_t i=0;i<n;++i)if(slots[i].load(std::memory_order_relaxed)==handle)return handle!=0;
        return overflow.load(std::memory_order_relaxed);
    }
    void Add(uint64_t handle) {
        if(!handle)return;
        for(uint32_t i=0;i<Size;++i)if(slots[i].load(std::memory_order_relaxed)==handle)return;
        for(uint32_t i=0;i<Size;++i) {
            if(slots[i].load(std::memory_order_relaxed))continue;
            slots[i].store(handle,std::memory_order_relaxed);
            if(i>=used.load(std::memory_order_relaxed))used.store(i+1,std::memory_order_release);
            return;
        }
        overflow.store(true,std::memory_order_release);
    }
    void Remove(uint64_t handle) {
        if(!handle)return;
        for(uint32_t i=0;i<Size;++i)if(slots[i].load(std::memory_order_relaxed)==handle){slots[i].store(0,std::memory_order_relaxed);return;}
    }
};
}
