#pragma once
// Ring skill menu (T025, review R6 X3): the icons the ring's atlas worker reads. The frame thread fetches them from upstream's
// icon cache (interaction::icons::Get, as upstream's own Prefetch does) until every one is in or 2 s pass; the worker only
// reads this box. So the worker never calls upstream: at session end upstream's icons::Shutdown runs before OnUiReset, and
// a Get from our worker after it would start upstream's icon worker again (a thread left running at exit).
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "interaction_icons.h"

namespace framevr::ringwheel {
class IconBox {
    using Image = interaction::icons::Image;
    std::mutex m;
    std::condition_variable cv;
    std::vector<int> want;
    std::unordered_map<int, std::shared_ptr<const Image>> got;
    bool done = true;
    double since = 0;
public:
    // Frame thread: a new set of skill ids (a class or skill change). Images already in stay.
    void Want(const int* ids, int n, double nowMs) {
        std::lock_guard lock(m);
        want.assign(ids, ids + std::max(n, 0));
        done = false; since = nowMs;
    }
    // Frame thread, each frame while not Done: asks `get` for the ones still missing; done once all are in or after
    // timeoutMs (the rest draw as initials discs). Returns true when it just finished.
    template <class Get>
    bool Poll(Get get, double nowMs, double timeoutMs = 2000) {
        std::vector<int> missing;
        {
            std::lock_guard lock(m);
            if (done) return false;
            for (int id : want) if (id >= 0 && !got.count(id)) missing.push_back(id);
        }
        std::vector<std::pair<int, std::shared_ptr<const Image>>> found;
        for (int id : missing) if (auto im = get(id)) found.emplace_back(id, std::move(im));   // upstream's lock, not ours
        std::lock_guard lock(m);
        for (auto& f : found) got[f.first] = std::move(f.second);
        if (found.size() == missing.size() || nowMs - since >= timeoutMs) { done = true; cv.notify_all(); return true; }
        return false;
    }
    bool Done() { std::lock_guard lock(m); return done; }
    // Frame thread: an icon ring's remap changed (IconVariant below): every image is let go, so the next Want and Poll ask
    // upstream's cache again (Kick's s1 is then our art, or the game's plate again).
    void Forget() { std::lock_guard lock(m); got.clear(); }
    // Frame thread: the Off edge or a reset; a worker waiting in Settle goes on at once.
    void Close() { std::lock_guard lock(m); done = true; cv.notify_all(); }
    // The worker: its icon source, and its settle (waits for the set, at most `ms`).
    std::shared_ptr<const Image> Take(int id) {
        std::lock_guard lock(m);
        const auto it = got.find(id);
        return it == got.end() ? nullptr : it->second;
    }
    void Settle(unsigned ms) {
        std::unique_lock lock(m);
        cv.wait_for(lock, std::chrono::milliseconds(ms), [&] { return done; });
    }
};
// Headset fixes 2026-11-15 (ring Kick icon On at 8:50, the ring kept the blank s1): the icon remaps that were On when the atlas
// was asked for (ring::IconVariantFrame, one bit per icon ring). Step: true once on each change, either edge, so the ring forgets
// its icons and asks its worker for a new atlas (AtlasWorker::Request::iconVariant makes it a new request).
struct IconVariant {
    uint32_t seen = 0;
    bool Step(uint32_t now) { if (now == seen) return false; seen = now; return true; }
};
}  // namespace framevr::ringwheel
