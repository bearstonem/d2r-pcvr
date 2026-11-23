#pragma once
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>

namespace framevr {
// Negative membership queries avoid taking a shared registry's mutex. Hash
// collisions deliberately return "maybe": the registry remains authoritative.
// Add/Remove must be serialized by that registry's existing mutex, once per
// insertion/removal. Queries may run concurrently on recording threads.
class ActiveKeys {
    std::array<uint32_t,64> references{};
    std::atomic<uint64_t> bits{0};
    static unsigned Bucket(uint64_t key) {
        const uint64_t hash=(key>>4)*0x9e3779b97f4a7c15ull;
        return unsigned((hash^(hash>>32))&63);
    }
public:
    void Add(uint64_t key) {
        const auto bucket=Bucket(key);
        if(++references[bucket]==1)bits.fetch_or(uint64_t(1)<<bucket,std::memory_order_release);
    }
    void Remove(uint64_t key) {
        const auto bucket=Bucket(key);assert(references[bucket]);
        if(--references[bucket]==0)bits.fetch_and(~(uint64_t(1)<<bucket),std::memory_order_release);
    }
    bool MaybeContains(uint64_t key) const {
        return (bits.load(std::memory_order_acquire)&(uint64_t(1)<<Bucket(key)))!=0;
    }
    template<class KeyAt> bool MaybeContainsAny(uint32_t count, KeyAt keyAt) const {
        const auto active = bits.load(std::memory_order_acquire);
        if (!active) return false;
        for (uint32_t i = 0; i < count; ++i)
            if (active & (uint64_t(1) << Bucket(keyAt(i)))) return true;
        return false;
    }
};
}
