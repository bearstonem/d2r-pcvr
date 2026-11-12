#pragma once
// What the skeleton hook remembers of each skeleton instance it meets (vr/skeletons.cpp), keyed
// by the instance's address. The game frees instances and reuses their addresses, so an entry
// is only ever a hint its user checks again; this keeps the hints from piling up:
//   - an entry not looked up for keepMs is forgotten (a freed instance is not posed again);
//   - at `limit` entries with nothing that old, the half looked up longest ago goes.
// Never everything at once, as the maps this replaces did at 4,096 entries: every skeleton in
// view was then identified again in the same frame.
// Not locked. Find only reads the map and stamps the entry through an atomic, so callers may
// run it under a shared lock; Put, Sweep and Clear need the exclusive one.
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace framevr {
// What one sweep forgot: entries not looked up for keepMs, and entries dropped to make room.
struct CacheSweep {
    size_t held = 0, aged = 0, crowded = 0;   // held: entries before the sweep
    size_t Dropped() const { return aged + crowded; }
};
struct PairHash {
    template <class A, class B> size_t operator()(const std::pair<A, B>& p) const noexcept {
        uint64_t h = uint64_t(p.first) * 0x9E3779B97F4A7C15ull;
        h ^= uint64_t(p.second) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        return size_t(h ^ (h >> 29));
    }
};
template <class Key, class Value, class Hash = std::hash<Key>>
class AgedCache {
public:
    // A look-up stamps its entry at most this often: the stamp is shared between threads.
    static constexpr uint64_t StampEvery = 250;
    AgedCache(size_t limit, uint64_t keepMs, uint64_t sweepEveryMs) : limit_(limit < 2 ? 2 : limit), keep_(keepMs), every_(sweepEveryMs) {}

    const Value* Find(const Key& key, uint64_t now) const {
        const auto it = map_.find(key);
        if (it == map_.end()) return nullptr;
        Stamp(it->second, now);
        return &it->second.value;
    }
    Value* Find(const Key& key, uint64_t now) { return const_cast<Value*>(std::as_const(*this).Find(key, now)); }

    // Adds or replaces. Sweeps first when one is due or the cache is full; the new entry stays.
    Value& Put(const Key& key, Value value, uint64_t now, CacheSweep* swept = nullptr) {
        if (const auto it = map_.find(key); it != map_.end()) {
            it->second.value = std::move(value);
            it->second.seen.store(now, std::memory_order_relaxed);
            return it->second.value;
        }
        if (map_.size() >= limit_ || now - swept_ >= every_ || now < swept_) {
            const CacheSweep s = Sweep(now);
            if (swept) *swept = s;
        }
        return map_.try_emplace(key, std::move(value), now).first->second.value;
    }

    CacheSweep Sweep(uint64_t now) {
        CacheSweep s;
        s.held = map_.size();
        swept_ = now;
        for (auto it = map_.begin(); it != map_.end();) {
            const uint64_t seen = it->second.seen.load(std::memory_order_relaxed);
            if (now > seen && now - seen > keep_) { it = map_.erase(it); ++s.aged; } else ++it;
        }
        if (map_.size() >= limit_) {
            std::vector<uint64_t> stamps;
            stamps.reserve(map_.size());
            for (const auto& entry : map_) stamps.push_back(entry.second.seen.load(std::memory_order_relaxed));
            const auto middle = stamps.begin() + std::ptrdiff_t(stamps.size() / 2);
            std::nth_element(stamps.begin(), middle, stamps.end());
            const uint64_t cut = *middle;   // the older half, and whatever shares the middle's stamp
            for (auto it = map_.begin(); it != map_.end();) {
                if (it->second.seen.load(std::memory_order_relaxed) <= cut) { it = map_.erase(it); ++s.crowded; } else ++it;
            }
        }
        return s;
    }

    template <class Visit> void Each(Visit&& visit) const { for (const auto& entry : map_) visit(entry.first, entry.second.value); }
    size_t Size() const { return map_.size(); }
    void Clear() { map_.clear(); }

private:
    struct Slot {
        Value value;
        mutable std::atomic<uint64_t> seen;
        Slot(Value v, uint64_t at) : value(std::move(v)), seen(at) {}
    };
    static void Stamp(const Slot& slot, uint64_t now) {
        const uint64_t seen = slot.seen.load(std::memory_order_relaxed);
        if (now > seen && now - seen >= StampEvery) slot.seen.store(now, std::memory_order_relaxed);
    }
    std::unordered_map<Key, Slot, Hash> map_;
    size_t limit_;
    uint64_t keep_, every_, swept_ = 0;
};
}  // namespace framevr
