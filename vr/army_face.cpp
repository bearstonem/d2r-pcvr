// The summon army forms where you face: glue around the pure engine steamvr/army_face.h (its header says why and the rules).
//   - Facing (vrcam's update loop, once a loop): the headset view's forward on the ground plane, kept for the game thread.
//   - Heading (vr/companion_ai.cpp StepSummonArmyServer, the game thread, about every 250 ms with an army out, twice a tick,
//     each under the coordinator's lock): the first call decides the tick's heading and replaces the model's forward for the
//     corridor check; the second hands the same heading on and seeds the coordinator's HeadingFilter with it (Reset, Update),
//     so Step's own Update forms on it unchanged.
// The mercenary reads companion_ai's cached forward, never these, so it is untouched.
// Log "Frame army face:": armed (once a session, the first time the army forms on the facing) and a lost facing (once a session).
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../steamvr/army_face.h"

namespace {

// The facing, written by the UpdateThread (Facing) and read by the game thread (Heading): x and y in one word, so a read
// never pairs two loops' halves; its time (GetTickCount64, the clock the army tick's nowMs comes from), 0 before the first.
std::atomic<uint64_t> g_facing{0};
std::atomic<uint64_t> g_facingAtMs{0};
constexpr float MinFlat = .25f;            // the view's forward this flat or less (looking within about 15 deg of straight down or up): no facing
constexpr uint64_t FacingStaleMs = 1000;   // a facing older than this is none (the view not updating: a load, a menu)
constexpr uint64_t GapMs = 2000;           // no army tick for this long: armed again from the facing (an area change, the army gone)

uint64_t Pack(float x, float y) {
    uint32_t a = 0, b = 0;
    std::memcpy(&a, &x, 4); std::memcpy(&b, &y, 4);
    return uint64_t(a) | uint64_t(b) << 32;
}
void Unpack(uint64_t v, float& x, float& y) {
    const uint32_t a = uint32_t(v), b = uint32_t(v >> 32);
    std::memcpy(&x, &a, 4); std::memcpy(&y, &b, 4);
}
float Deg(armyface::Vec2 v) { return std::atan2(v.y, v.x) * 57.2957795f; }   // the ground plane's angle, for the log

// Game thread only (Heading runs in StepSummonArmyServer).
struct State {
    bool on = false;
    uint64_t lastCallMs = 0;
    armyface::Latch latch;
    bool armedSaid = false, noFacingSaid = false;   // each line once a session
    bool decided = false; uint64_t decidedMs = 0; armyface::Vec2 heading{};   // the tick's heading, for its Step call
};
State g;

void Say(const armyface::HeadingFrame& f, const char* line) { if (f.say) f.say(line); }
}  // namespace

namespace armyface {

void Facing(bool ok, float x, float y) {
    if (!ok || !std::isfinite(x) || !std::isfinite(y)) return;
    const float len = std::hypot(x, y);
    if (len <= MinFlat) return;
    g_facing.store(Pack(x / len, y / len), std::memory_order_relaxed);
    g_facingAtMs.store(GetTickCount64(), std::memory_order_release);
}

void Heading(HeadingFrame f) {
    char line[300];
    if (f.step) {   // Step's call: this tick's decided heading, and the coordinator's filter seeded with it at the hero's place
        if (!g.decided || g.decidedMs != f.nowMs) return;   // nothing decided this tick (not armed)
        f.fx = g.heading.x;
        f.fy = g.heading.y;
        f.filter.Reset();   // Step's own Update then returns it unchanged (no walk since, the same heading)
        (void)f.filter.Update({f.heroX, f.heroY}, f.fx, f.fy);
        return;
    }
    g.decided = false;
    const armyface::Tuning t = armyface::tuning;
    if (!g.on || f.nowMs - g.lastCallMs > GapMs) g.latch.Reset();
    g.on = true;
    g.lastCallMs = f.nowMs;

    float faceX = 0.0f, faceY = 0.0f;
    const uint64_t at = g_facingAtMs.load(std::memory_order_acquire);
    const bool faceOk = at != 0 && (at >= f.nowMs || f.nowMs - at <= FacingStaleMs);
    if (faceOk) Unpack(g_facing.load(std::memory_order_relaxed), faceX, faceY);
    const bool wasArmed = g.latch.Armed();
    const armyface::Step s = g.latch.Update({f.heroX, f.heroY}, {f.fx, f.fy}, faceOk, {faceX, faceY}, f.nowMs, t);
    if (!faceOk && s.apply && !g.noFacingSaid) {   // the facing lost while the army is out (the view not updating)
        g.noFacingSaid = true;
        std::snprintf(line, sizeof line, "Frame army face: no headset facing: the army's heading held at %.0f deg, a walk follows the model as before (said once a session)", Deg(s.heading));
        Say(f, line);
    }
    if (!s.apply) return;   // not armed: the model's forward and the filter as they are this tick

    const armyface::Vec2 model{f.fx, f.fy};
    f.fx = s.heading.x;   // the corridor check's fx, fy
    f.fy = s.heading.y;
    g.decided = true; g.decidedMs = f.nowMs; g.heading = s.heading;   // Step's call hands it on and seeds the filter

    if (s.why == armyface::Why::First && !wasArmed && !g.armedSaid) {
        g.armedSaid = true;
        std::snprintf(line, sizeof line, "Frame army face: armed: the army forms on the headset's facing (%.0f deg now; the model's %.0f deg); said once a session", Deg(s.heading), Deg(model));
        Say(f, line);
    }
}
}  // namespace armyface
