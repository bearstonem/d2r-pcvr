#pragma once
// Stick select: the thumbstick as a clock hand over n items, item 0 at 12 o'clock, clockwise (Quick cast's ring). The dead zone,
// the pick hold past the half-way line and the center hold that lets a flick released as the thumb comes back still count are its
// feel (Feel). Pure and Windows-free (quick-radial-test).
#include <algorithm>
#include <cmath>

namespace framevr::stickselect {
struct Vec2f { float x = 0, y = 0; };
inline float StepDeg(int n) noexcept { return n > 0 ? 360.f / float(n) : 360.f; }
inline float ItemClock(int i, int n) noexcept { return n > 0 ? StepDeg(n) * float(((i % n) + n) % n) : 0.f; }
inline Vec2f AtClock(float clockDeg, float r) noexcept {
    const float a = clockDeg * 0.01745329f;
    return {std::sin(a) * r, std::cos(a) * r};
}
// The stick's (or any vector's) clock angle: up 0, right 90, down 180, left 270.
inline float ClockOf(float x, float y) noexcept {
    float a = std::atan2(x, y) * 57.29578f;
    if (a < 0) a += 360.f;
    return a >= 360.f ? 0.f : a;
}
inline float ClockDiff(float a, float b) noexcept {   // |a-b| round the dial, 0..180
    float d = std::fmod(std::fabs(a - b), 360.f);
    return d > 180.f ? 360.f - d : d;
}
// The item whose angle is nearest (-1: no items).
inline int NearestItem(float clockDeg, int n) noexcept {
    if (n <= 0) return -1;
    const int k = int(std::floor(clockDeg / StepDeg(n) + .5f));
    return ((k % n) + n) % n;
}
// The nearest of the items whose bit is set in `filled` (bit i: item i; -1: none of them): Quick cast's Fixed places,
// where an empty place is never picked. Ties: the lower item.
inline int NearestFilled(float clockDeg, int n, unsigned filled) noexcept {
    int best = -1; float bestD = 1e9f;
    for (int i = 0; i < n && i < 32; ++i)
        if (filled >> i & 1u) { const float d = ClockDiff(clockDeg, ItemClock(i, n)); if (d < bestD) { bestD = d; best = i; } }
    return best;
}
// The feel (the radial's tuned defaults).
struct Feel {
    float deadZone = .35f;       // the stick's center: inside it nothing is picked (0..1 of a full push; 0: any push picks)
    float hysteresisDeg = 8.f;   // a pick holds until the stick is this far past the half-way line to a neighbor
    float centerMs = 120.f;      // back in the dead zone this long, the pick clears
};
// The hysteresis actually used: under 40% of the spacing, so a flick onto a neighbor's own center always moves the pick.
inline float HysteresisFor(int n, const Feel& f) noexcept { return std::clamp(f.hysteresisDeg, 0.f, StepDeg(n) * .4f); }
// Out of the dead zone: the nearest item, held past the half-way line by the hysteresis. Back in the dead zone (a little
// smaller once picked: 80%, so a resting thumb on its edge does not flicker): the pick stays centerMs, then clears.
struct Selector {
    int pick = -1;
    double centerSince = -1;
    void Reset() noexcept { pick = -1; centerSince = -1; }
    // true when the pick changed (a tick when it is an item).
    bool Step(float x, float y, int n, double nowMs, const Feel& f) noexcept {
        const int was = pick;
        if (n <= 0) { Reset(); return was != pick; }
        if (pick >= n) pick = -1;
        const float r = std::hypot(x, y);
        const float zone = pick >= 0 ? f.deadZone * .8f : f.deadZone;
        if (!(r > zone) || r < 1e-3f) {
            if (pick >= 0) {
                if (centerSince < 0) centerSince = nowMs;
                if (nowMs - centerSince >= double(f.centerMs)) { pick = -1; centerSince = -1; }
            }
            return was != pick;
        }
        centerSince = -1;
        const float c = ClockOf(x, y);
        const int nearest = NearestItem(c, n);
        if (pick >= 0 && nearest != pick && ClockDiff(c, ItemClock(pick, n)) <= StepDeg(n) * .5f + HysteresisFor(n, f)) return was != pick;
        pick = nearest;
        return was != pick;
    }
    // Step over the items whose bit is set in `filled` only (Quick cast's Fixed places): the same dead zone and center
    // hold; out of it the nearest filled item, a pick held until the stick is the hysteresis past the half-way line to the filled
    // item now nearest (with every item filled and the nearest a neighbor, Step's rule).
    bool StepFilled(float x, float y, int n, unsigned filled, double nowMs, const Feel& f) noexcept {
        const int was = pick;
        if (n <= 0 || NearestFilled(0, n, filled) < 0) { Reset(); return was != pick; }
        if (pick >= n || (pick >= 0 && !(filled >> pick & 1u))) pick = -1;
        const float r = std::hypot(x, y);
        const float zone = pick >= 0 ? f.deadZone * .8f : f.deadZone;
        if (!(r > zone) || r < 1e-3f) {
            if (pick >= 0) {
                if (centerSince < 0) centerSince = nowMs;
                if (nowMs - centerSince >= double(f.centerMs)) { pick = -1; centerSince = -1; }
            }
            return was != pick;
        }
        centerSince = -1;
        const float c = ClockOf(x, y);
        const int nearest = NearestFilled(c, n, filled);
        if (pick >= 0 && nearest != pick && ClockDiff(c, ItemClock(pick, n)) - ClockDiff(c, ItemClock(nearest, n)) <= 2 * HysteresisFor(n, f)) return was != pick;
        pick = nearest;
        return was != pick;
    }
};
// After a pick is used (a cast, Back, the menu shut) the selecting stick stays out of the game's hands until the thumb
// comes back to the middle, so a still-pushed stick neither walks nor turns.
constexpr float ReleaseR = .25f;
struct Hold {
    bool on = false;
    void Start(float x, float y) noexcept { on = std::hypot(x, y) > ReleaseR; }
    // true while the stick is still the menu's (zero it for the game).
    bool Step(float x, float y) noexcept { if (on && !(std::hypot(x, y) > ReleaseR)) on = false; return on; }
};
}  // namespace framevr::stickselect
