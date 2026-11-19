#pragma once
// Ring skill menu (): the On / Off edge's log lines, as plain data (ring-wheel-test). The On line says the ring's state
// at that moment (the rows that decide what the player sees), not a build step, so a log read later tells what was tested.
#include <cstdio>
#include <cstring>
#include <string>

namespace framevr::ringwheel {
struct LabState {
    bool orbs = false;           // Tree layout: Tree orbs (else Trees shown)
    bool worldLock = true;       // Placement: stays in the room (else upstream's hand anchor, the ring never drawn)
    float distanceCm = 90, widthCm = 80, belowDeg = 8;
    int holdMs = 1000, tapMs = 350;   // Hold to assign (0 Off), Tap limit
    int renderPct = 150;
    const char* animation = "Effect on hover";   // the Animation row's name (Custom when a row was changed by hand)
};
inline std::string OnLine(const LabState& s) {
    char hold[24];
    if (s.holdMs <= 0) std::snprintf(hold, sizeof hold, "Off");
    else std::snprintf(hold, sizeof hold, "%.1f s", s.holdMs / 1000.0);
    char line[400];
    if (!s.worldLock)
        std::snprintf(line, sizeof line, "Frame ring wheel: On; Stays in the room Off: upstream's wheel on its hand anchor (the ring needs the room-fixed place)");
    else
        std::snprintf(line, sizeof line,
                      "Frame ring wheel: On; %s, %.0f cm ahead, %.0f cm wide, %.0f deg below the eyes; hold to assign %s, tap limit %d ms; "
                      "animation %s; render %d%%; the ring on the Spells, Glyphs and Gestures pages once ready, upstream's picture on the others",
                      s.orbs ? "Tree orbs" : "Trees shown", s.distanceCm, s.widthCm, s.belowDeg, hold, s.tapMs,
                      s.animation && s.animation[0] ? s.animation : "?", s.renderPct);
    return line;
}
inline const char* OffLine() { return "Frame ring wheel: Off (upstream's wheel; the swapchain freed at once, the rest of the GPU side on its worker)"; }
// One line a close, written when it happens (audit 2026-11-10 bug 2: also when the input is cleared outside a focused frame).
inline std::string ClosedLine(const char* reason, double openS, const char* page) {
    char line[220];
    std::snprintf(line, sizeof line, "Frame ring wheel: closed reason=%s after %.1f s, page %s", reason, openS, page);
    return line;
}
// The trace for audit 2026-11-10 bug 6 (both sticks clicked nearly together swap the wheel's hand): the stick-click edges the
// ring saw that frame, the time since this open and since the last switch in it (under 0: none yet). Log only.
inline std::string SwitchedLine(int hand, const bool click[2], double sinceOpenMs, double sinceSwitchMs) {
    char last[48];
    if (sinceSwitchMs < 0) std::snprintf(last, sizeof last, "the first switch of this open");
    else std::snprintf(last, sizeof last, "%.0f ms after the last switch", sinceSwitchMs);
    char line[220];
    std::snprintf(line, sizeof line, "Frame ring wheel: switched to the %s hand's wheel (stick clicks this frame: %s; %.0f ms after the open; %s)",
                  hand == 1 ? "left" : "right", click[0] && click[1] ? "both" : click[1] ? "left" : click[0] ? "right" : "none", sinceOpenMs, last);
    return line;
}
// Audit 2026-11-10 bug 8: every release of a ring swapchain says so (the session's end said nothing). where: "Off edge",
// "session end", or a kept one's later release; ms under 0: not timed.
// Where a swapchain freed in the ring's Reap went (review N3). Reap waiting is the session's end, whichever of its two steps
// frees it: one kept at an Off edge, or one whose GPU finished between the session's Retire and its Reap, is then the
// session's. Not waiting: a kept one's later release, by the step that found it.
enum class ReapStep { GpuDone, WorkerGone };
inline const char* ReapWhere(bool wait, ReapStep step) {
    if (wait) return "session end";
    return step == ReapStep::GpuDone ? "kept at its Off edge, the GPU now done" : "kept at its Off edge, its worker now gone";
}
inline std::string ReleasedLine(const char* where, double ms) {
    char line[200];
    if (ms < 0) std::snprintf(line, sizeof line, "Frame ring wheel: swapchain destroyed (%s, the frame thread)", where);
    else std::snprintf(line, sizeof line, "Frame ring wheel: swapchain destroyed ms=%.1f (%s, the frame thread)", ms, where);
    return line;
}
// The quiet log (the player's rule for a PR: a low level that cannot touch the game). Every "Frame ring wheel:" line the ring says goes
// through this: the ring's On / Off edge, a button put on or taken off a menu, and every refusal or failure pass; everything else
// (pages, pointing, placement, per-open and per-pick lines, the 10 s line, icon and layer counts) is said only with
// [debug] ring_wheel_log=1 in d2r_pcvr.ini (off by default, in every build). A line that is not the ring's own passes unchanged.
inline bool KeepQuietLine(const char* line) {
    constexpr const char prefix[] = "Frame ring wheel: ";
    if (!line) return false;
    if (std::strncmp(line, prefix, sizeof prefix - 1) != 0) return true;
    const char* body = line + sizeof prefix - 1;
    if (std::strncmp(body, "On", 2) == 0 || std::strncmp(body, "Off", 3) == 0 || std::strncmp(body, "bind ", 5) == 0) return true;
    for (const char* mark : {"refused", "failed", "not put on", "not opened", "swapchain kept", "swapchain waits", "taken off", "let go of",
                             "(its button", "fx textures 0/8", "dropped"})
        if (std::strstr(body, mark)) return true;
    return false;
}
}  // namespace framevr::ringwheel
