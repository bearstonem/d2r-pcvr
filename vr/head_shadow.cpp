// Head shadow, the game side: the hero keeps his head in the first-person pose (so his shadow has one) while the eye
// views cut everything just in front of the eyes. The reasoning, the pure math and the threads' handshake:
// steamvr/head_shadow.h.
//   HideHead: the skeleton hook's thread, each hero pose that would shrink the head (vr/skeletons.cpp PoseBody).
//   EyeNear: the game's render thread, each first-person eye projection (vr/vrcam.cpp VrProjInner).
// Reads no game memory and calls nothing in the game.
// Log "Frame head shadow:":
//   - the choice (head kept, or hidden and why), with the eye's cut at its pitch: once it has lasted a second and
//     differs from the last one told;
//   - the eye's near jumping past NearEase::StepRatio between passes: at most once a second, only when it did;
//   - with [debug] head_shadow=1, every second: poses kept or hidden, eye passes at the cut, held and eased, the near
//     before and after, the head pitch and the cut's range, how far the head bones reach.
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

constexpr uint64_t SummaryMs = 1000;   // the debug summary's period
constexpr uint64_t JumpLineMs = 1000;  // the jump line at most this often

hs::Handshake g_hand;
std::atomic<bool> g_trace{false};       // [debug] head_shadow
std::atomic<float> g_level{hs::LevelCm}, g_up{hs::UpCm}, g_down{hs::DownCm};   // [arms] head_cut_*
// The last eye pass, for the choice's line: its pitch (NaN: not known) and cut (cm; 0: none; -1: no pass yet).
std::atomic<float> g_eyePitch{NAN}, g_eyeCut{-1};

void PitchText(float pitchDeg, char* out, size_t size) {
    if (std::isfinite(pitchDeg)) std::snprintf(out, size, "%+.0f deg", pitchDeg);
    else std::snprintf(out, size, "not known (level cut)");
}

// The state line (pose thread), the jump watch (render thread) and the debug summary's counts (both), under g_lock;
// short holds.
SRWLOCK g_lock = SRWLOCK_INIT;
hs::StateLine g_state;
hs::NearWatch g_watch;
uint64_t g_nextJumpLine = 0;
struct Window {
    int poses = 0, kept = 0, hidden = 0;
    hs::Why lastHidden = hs::Why::NoCut;
    hs::Reach reach;   // the farthest seen this window
    int passes = 0, cutPasses = 0, heldPasses = 0, easedPasses = 0, jumps = 0;
    float nearFrom = 0, nearTo = 0, unitsPerMetre = 0, pitchDeg = 0, cutMin = 0, cutMax = 0;
};
Window g_window;
uint64_t g_nextSummary = 0;
}  // namespace

namespace headshadow {
void SetTrace(bool on) { g_trace.store(on, std::memory_order_relaxed); }
void SetCuts(float levelCm, float upCm, float downCm) {
    g_level.store(hs::CleanCut(levelCm, hs::LevelCm), std::memory_order_relaxed);
    g_up.store(hs::CleanCut(upCm, hs::UpCm), std::memory_order_relaxed);
    g_down.store(hs::CleanCut(downCm, hs::DownCm), std::memory_order_relaxed);
}
bool Holding() { return g_hand.Holding(GetTickCount64()); }

bool HideHead(bool form, const float* bones, const char* inNeck, int count, const float* eye, float unitsPerCm) {
    const uint64_t now = GetTickCount64();
    const hs::Why w = g_hand.Pose(form, now);
    const bool trace = g_trace.load(std::memory_order_relaxed);
    char line[256];
    AcquireSRWLockExclusive(&g_lock);
    // A shapeshift form is the player's own choice and comes and goes often: the state line does not tell it.
    bool tell = false;
    if (w != hs::Why::Form && g_state.Step(w, now)) {
        char cut[128], pitch[40];
        const float level = g_level.load(std::memory_order_relaxed), up = g_up.load(std::memory_order_relaxed),
                    down = g_down.load(std::memory_order_relaxed), at = g_eyeCut.load(std::memory_order_relaxed);
        if (at > 0) {
            PitchText(g_eyePitch.load(std::memory_order_relaxed), pitch, sizeof pitch);
            std::snprintf(cut, sizeof cut, "the eye's cut %.0f cm at pitch %s (%.0f level, %.0f up, %.0f down)", at, pitch, level, up, down);
        } else std::snprintf(cut, sizeof cut, "cut %.0f cm level, %.0f up, %.0f down", level, up, down);
        std::snprintf(line, sizeof line, "Frame head shadow: %s; %s; %d changes since the last line", hs::WhyName(w), cut, g_state.changes);
        g_state.changes = 0; tell = true;
    }
    if (trace) {
        ++g_window.poses;
        if (w == hs::Why::Kept) ++g_window.kept; else { ++g_window.hidden; g_window.lastHidden = w; }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (trace) {   // the reach: a loop over the bones, outside the lock
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
    const float cut = hs::PitchCut(pitchDeg, g_level.load(std::memory_order_relaxed), g_up.load(std::memory_order_relaxed),
                                   g_down.load(std::memory_order_relaxed));
    const hs::Handshake::Near r = g_hand.Eye(nearZ, on, cut, unitsPerMetre, now);
    g_eyePitch.store(pitchDeg, std::memory_order_relaxed);
    g_eyeCut.store(on ? cut : 0.f, std::memory_order_relaxed);
    const bool trace = g_trace.load(std::memory_order_relaxed);
    char jump[320];
    bool tellJump = false, summary = false;
    Window s{};
    AcquireSRWLockExclusive(&g_lock);
    const bool jumped = g_watch.Pass(r.nearZ, now);
    if (g_watch.jumps && now >= g_nextJumpLine) {
        char pitch[40];
        PitchText(pitchDeg, pitch, sizeof pitch);
        std::snprintf(jump, sizeof jump,
                      "Frame head shadow: the eye's near jumped over %.0f%% between passes %d times since the last line (largest %.3f -> %.3f world "
                      "units; now %s): a reader a pass behind, the depth fog's one NearPlane, is off by as much for that frame; pitch %s",
                      (hs::NearEase::StepRatio - 1) * 100, g_watch.jumps, g_watch.from, g_watch.to,
                      r.atCut ? "at the cut" : r.held ? "on the held cut" : "no cut", pitch);
        g_watch.Take();
        g_nextJumpLine = now + JumpLineMs;
        tellJump = true;
    }
    if (trace) {
        if (!g_nextSummary) g_nextSummary = now + SummaryMs;
        else if (now >= g_nextSummary) {   // a window left open by a menu or a load (no passes) is dropped, not told
            s = g_window; g_window = Window{};
            // told while the head shadow is in play: poses asked about the head, a cut or a hold, or a jump
            summary = (s.poses > 0 || s.cutPasses > 0 || s.heldPasses > 0 || s.jumps > 0) && now - g_nextSummary < SummaryMs;
            g_nextSummary = now + SummaryMs;
        }
        Window& w = g_window;
        ++w.passes;
        if (r.atCut) ++w.cutPasses;
        if (r.held) ++w.heldPasses;
        if (r.eased) ++w.easedPasses;
        if (jumped) ++w.jumps;
        w.nearFrom = nearZ; w.nearTo = r.nearZ; w.unitsPerMetre = unitsPerMetre; w.pitchDeg = pitchDeg;
        w.cutMin = w.passes == 1 ? cut : std::min(w.cutMin, cut);
        w.cutMax = w.passes == 1 ? cut : std::max(w.cutMax, cut);
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (tellJump) framevr::logging::Write(jump);
    if (!summary) return r.nearZ;
    char reach[96];
    if (s.reach.bones) std::snprintf(reach, sizeof reach, "head bones %d reach %.0f..%.0f cm from the eye", s.reach.bones, s.reach.nearCm, s.reach.farCm);
    else std::snprintf(reach, sizeof reach, "head bones' reach not known (no eye point, or the head not posed)");
    char pitch[40];
    PitchText(s.pitchDeg, pitch, sizeof pitch);
    char text[512];
    std::snprintf(text, sizeof text,
                  "Frame head shadow: 1 s: poses %d (head kept %d, hidden %d%s%s); eye passes %d, at the cut %d, held %d, eased %d, jumps %d; "
                  "near %.3f -> %.3f world units at %.2f units/m; pitch %s, cut %.1f..%.1f cm; %s",
                  s.poses, s.kept, s.hidden, s.hidden ? ": " : "", s.hidden ? hs::WhyName(s.lastHidden) : "", s.passes, s.cutPasses,
                  s.heldPasses, s.easedPasses, s.jumps, s.nearFrom, s.nearTo, s.unitsPerMetre, pitch, s.cutMin, s.cutMax, reach);
    framevr::logging::Write(text);
    return r.nearZ;
}
}  // namespace headshadow
