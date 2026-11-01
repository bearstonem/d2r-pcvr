// Head shadow, the game side: the hero keeps his head in the first-person pose (so his shadow has one) while the eye
// views cut everything just in front of the eyes. The reasoning, the pure math and the threads' handshake:
// steamvr/head_shadow.h.
//   HideHead: the skeleton hook's thread, each hero pose that would shrink the head (vr/skeletons.cpp PoseBody).
//   EyeNear: the game's render thread, each first-person eye projection (vr/vrcam.cpp VrProjInner).
// Reads no game memory and calls nothing in the game.
// Log "Frame head shadow:": a state (head kept, or hidden and why) once it has lasted a second and differs from the
// last one told. With [debug] head_shadow=1 also every 10 s while the head is kept: poses kept or hidden, eye passes at
// the cut, the near before and after, the head pitch and the cut's range, how far the head bones reach.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include "../steamvr/head_shadow.h"
#include "logging.h"

namespace {
namespace hs = framevr::headshadow;

constexpr uint64_t SummaryMs = 10000;   // the debug summary's period

hs::Handshake g_hand;
std::atomic<bool> g_trace{false};       // [debug] head_shadow

// The state line (pose thread) and the debug summary's counts (both threads), under g_lock; short holds.
SRWLOCK g_lock = SRWLOCK_INIT;
hs::StateLine g_state;
struct Window {
    int poses = 0, kept = 0, hidden = 0;
    hs::Why lastHidden = hs::Why::NoCut;
    hs::Reach reach;   // the farthest seen this window
    int passes = 0, cutPasses = 0, heldPasses = 0;
    float nearFrom = 0, nearTo = 0, unitsPerMetre = 0, pitchDeg = 0, cutMin = 0, cutMax = 0;
};
Window g_window;
uint64_t g_nextSummary = 0;
}  // namespace

namespace headshadow {
void SetTrace(bool on) { g_trace.store(on, std::memory_order_relaxed); }
bool Holding() { return g_hand.Holding(GetTickCount64()); }

bool HideHead(bool form, const float* bones, const char* inNeck, int count, const float* eye, float unitsPerCm) {
    const uint64_t now = GetTickCount64();
    const hs::Why w = g_hand.Pose(form, now);
    const bool trace = g_trace.load(std::memory_order_relaxed);
    char line[160];
    AcquireSRWLockExclusive(&g_lock);
    // A shapeshift form is the player's own choice and comes and goes often: the state line does not tell it.
    bool tell = false;
    if (w != hs::Why::Form && g_state.Step(w, now)) {
        std::snprintf(line, sizeof line, "Frame head shadow: %s (cut %.0f cm level, %.0f up, %.0f down; %d changes since the last line)",
                      hs::WhyName(w), hs::LevelCm, hs::UpCm, hs::DownCm, g_state.changes);
        g_state.changes = 0; tell = true;
    }
    bool measure = false;
    if (trace) {
        ++g_window.poses;
        if (w == hs::Why::Kept) ++g_window.kept; else { ++g_window.hidden; g_window.lastHidden = w; }
        measure = now + 1000 >= g_nextSummary;   // the reach only in the window's last second: a loop over the bones
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (measure) {
        const hs::Reach r = hs::HeadReach(bones, inNeck, count, eye, unitsPerCm);
        AcquireSRWLockExclusive(&g_lock);
        if (r.bones && (!g_window.reach.bones || r.farCm > g_window.reach.farCm)) g_window.reach = r;
        ReleaseSRWLockExclusive(&g_lock);
    }
    if (tell) framevr::logging::Write(line);   // after the lock: Write takes its own
    return w != hs::Why::Kept;
}

float EyeNear(float nearZ, bool on, float unitsPerMetre, float pitchDeg) {
    const uint64_t now = GetTickCount64();
    const float cut = hs::PitchCut(pitchDeg);
    const hs::Handshake::Near r = g_hand.Eye(nearZ, on, cut, unitsPerMetre, now);
    if (!g_trace.load(std::memory_order_relaxed)) return r.nearZ;
    Window s{};
    bool summary = false;
    AcquireSRWLockExclusive(&g_lock);
    if (!g_nextSummary) g_nextSummary = now + SummaryMs;
    else if (now >= g_nextSummary) {   // a window left open by a menu or a load (no passes) is dropped, not told as 10 s
        s = g_window; g_window = Window{};
        summary = s.kept > 0 && now - g_nextSummary < SummaryMs;
        g_nextSummary = now + SummaryMs;
    }
    Window& w = g_window;
    ++w.passes;
    if (r.atCut) ++w.cutPasses;
    if (r.held) ++w.heldPasses;
    w.nearFrom = nearZ; w.nearTo = r.nearZ; w.unitsPerMetre = unitsPerMetre; w.pitchDeg = pitchDeg;
    w.cutMin = w.passes == 1 ? cut : std::min(w.cutMin, cut);
    w.cutMax = w.passes == 1 ? cut : std::max(w.cutMax, cut);
    ReleaseSRWLockExclusive(&g_lock);
    if (!summary) return r.nearZ;
    char reach[96];
    if (s.reach.bones) std::snprintf(reach, sizeof reach, "head bones %d reach %.0f..%.0f cm from the eye", s.reach.bones, s.reach.nearCm, s.reach.farCm);
    else std::snprintf(reach, sizeof reach, "head bones' reach not known (no eye point)");
    char pitch[40];
    if (std::isfinite(s.pitchDeg)) std::snprintf(pitch, sizeof pitch, "%+.0f deg", s.pitchDeg);
    else std::snprintf(pitch, sizeof pitch, "not known (level cut)");
    char text[512];
    std::snprintf(text, sizeof text,
                  "Frame head shadow: 10 s: poses %d (head kept %d, hidden %d%s%s); eye passes %d, at the cut %d, held %d; near %.3f -> %.3f world units "
                  "at %.2f units/m; pitch %s, cut %.1f..%.1f cm; %s",
                  s.poses, s.kept, s.hidden, s.hidden ? ": " : "", s.hidden ? hs::WhyName(s.lastHidden) : "", s.passes, s.cutPasses, s.heldPasses,
                  s.nearFrom, s.nearTo, s.unitsPerMetre, pitch, s.cutMin, s.cutMax, reach);
    framevr::logging::Write(text);
    return r.nearZ;
}
}  // namespace headshadow
