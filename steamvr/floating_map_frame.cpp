// Floating map: transport's two calls (steamvr/floating_map.h has the engine and what it does).
// StepFrame steps the engine once a frame, takes the grips and the right stick it holds, asks for the map capture and
// tells the map zoom when the right laser points at a map; DrawFrame draws the copy: one quad of transport's map picture
// (mapEye, the wrist's own capture), so no new capture, panel or swapchain. Nothing in the game changes: no game call,
// no memory write. With no copy pulled nothing is drawn, captured or buzzed, and the stick and grips pass untouched.
// Telemetry "Frame floatmap:": pulled, grabbed, let go (where, its turn, the stick's push / pull), put away, two-hand
// resize, Save and Exit / a new game putting it back on the wrist, the stick zooming a pointed map (one line a pointing, at most one a 2 s), the
// first aim buzz of a run, a Right Grip press near a map not taken (at most one a 2 s), a grip held back for two hands
// and given to the game (at most one a 2 s), what the copy shows against Map view (on a change), no picture for 3 s.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "floating_map.h"
#include "map_zoom.h"
#include "vr_settings.h"
#include "wrist_full_map.h"
#include "../vr/settings_menu.h"

namespace framevr::floatmap {
namespace {
namespace full = framevr::fullmap;
constexpr uint64_t QuietMs = 2000, NoPictureMs = 3000;
constexpr uint64_t WristFreshMs = 150;   // the wrist's map drawn this recently counts as shown (pull from it, put away at it)
constexpr float GrabBuzz = .35f, AwayBuzz = .2f, AimBuzzStrength = .15f;

struct State {
    Engine engine;
    Output out;                // this frame's Input step, drawn by DrawFrame
    bool fresh = false;        // out is this frame's (DrawFrame takes it)
    uint64_t nowMs = 0;
    unsigned grabs = 0, resizes = 0, pulls = 0, putAways = 0, aims = 0;
    bool aimTold = false;
    unsigned missed = 0; uint64_t missAt = 0;   // Right Grip presses not taken since the last line; that line's time
    uint64_t gaveBackAt = 0;
    bool laserTold = false;    // the right laser against the grip: once a run
    int zoomMap = -1; uint64_t zoomAt = 0;   // the map the stick zooms by pointing (-1 none); its last line
    bool shownWas = false, lookSeen = false, lookTold = false; unsigned looks = 0;   // look to show: the first change a run is told, the rest counted
    uint64_t wantSince = 0; bool pictureTold = false;
    KindWatch kind;            // what the copy shows, against Map view
} st;

const char* MapViewName() {
    const float view = vrsettings::Read().values[framevr::settings::MapTopDown];
    return framevr::settings::MapViewFull(view) ? (framevr::settings::MapViewTopDown(view) ? "Full top-down" : "Full map")
                                                : (framevr::settings::MapViewTopDown(view) ? "Top-down" : "Isometric");
}
// Where it is, for the pulled / let go / kept lines: distance, angle from the view, size, yaw / pitch / roll, height.
void Where(char* out, size_t size, const Output& o, math::Vec head) {
    const auto d = o.p - head;
    const auto a = AnglesOf(o.q);
    std::snprintf(out, size, "%.2f m away, %.0f deg from your view, size %.0f cm; yaw %.0f %s, pitch %.0f, roll %.0f deg; %+.0f cm vs eyes",
                  std::sqrt(math::Dot(d, d)), o.lookDeg, o.size * 100, std::fabs(a.yaw), a.yaw < 0 ? "right" : "left", a.pitch, a.roll, d.y * 100);
}
// Where a press's ray passed a map: in map sizes from its center (within +-0.5: on it) and in degrees.
void AimText(char* out, size_t size, const char* name, const MapAim& a) {
    char at[64];
    if (a.plane) std::snprintf(at, sizeof at, "x %+.2f y %+.2f map sizes from its center", a.x, a.y);
    else std::snprintf(at, sizeof at, "the ray misses its plane");
    std::snprintf(out, size, "%s: %s, %.0f deg off its edge, its center %+.0f deg right %+.0f deg up of the ray, %.2f m", name, at, a.miss, a.right, a.up, a.distance);
}
// An event line, with the look-to-show changes counted since the last one.
void Tell(const InputFrame& f, char* line, size_t size) {
    if (st.looks) {
        const size_t used = std::strlen(line);
        if (used < size) std::snprintf(line + used, size - used, "; shown or hidden %u times since the last line", st.looks);
        st.looks = 0;
    }
    f.say(line);
}
}  // namespace

void StepFrame(InputFrame& f) {
    const Tuning t;
    st.nowMs = f.nowMs;
    floatmap::Input in;
    in.time = f.time; in.session = f.session; in.gameplay = f.gameplay; in.handFree = f.handFree; in.leftFree = f.leftFree; in.nearGrab = f.nearGrab;
    in.head = f.head; in.headQ = f.headQ; in.headValid = f.headValid;
    for (int h = 0; h < 2; ++h) {in.hand[h] = f.hand[h]; in.handTracked[h] = f.handTracked[h];}
    in.aim = f.aim; in.aimQ = f.aimQ; in.aimTracked = f.aimTracked;
    in.aimLeft = f.aimLeft; in.aimLeftQ = f.aimLeftQ; in.aimLeftTracked = f.aimLeftTracked;
    in.grip[0] = f.gripRight; in.grip[1] = f.gripLeft;
    in.stickX = f.stickRx / 32767.f; in.stickY = f.stickRy / 32767.f;
    in.wristShown = f.wristMs && f.nowMs >= f.wristMs && f.nowMs - f.wristMs <= WristFreshMs;
    in.wristP = f.wristP; in.wristQ = f.wristQ; in.wristSize = f.wristSize;
    st.out = st.engine.Step(in, t);
    st.fresh = true;
    const auto& o = st.out;
    if (o.own[0]) f.gripRight = 0;
    if (o.own[1]) f.gripLeft = 0;
    if (o.ownStick) f.stickRx = f.stickRy = 0;
    f.want = o.want;
    f.pointed = o.pointed;
    if (o.pulse >= 0) f.pulse(o.pulse, GrabBuzz);
    else if (o.awayEdge) f.pulse(0, AwayBuzz);   // held at the wrist: a let go puts it away
    else if (o.aimEdge) {   // Aim buzz: lighter than the grab's
        f.pulse(0, AimBuzzStrength); ++st.aims;
        if (!st.aimTold) {st.aimTold = true; f.say("Frame floatmap: aim buzz (the right laser came onto a map: Right Grip takes it); first this run");}
    }
    char line[560];
    // The stick zooming a pointed map: one line on the first push past the zoom's dead zone while pointing (pointing
    // first and pushing later, or pushing as the laser arrives), once a pointing, at most one a 2 s. Not while the map
    // still holds the stick from a push / pull (the zoom sees it zeroed then).
    if (o.pointedMap < 0) st.zoomMap = -1;
    else if (o.pointedMap != st.zoomMap && !o.ownStick && std::fabs(in.stickY) > mapzoom::Dead && st.nowMs - st.zoomAt >= QuietMs) {
        st.zoomAt = st.nowMs; st.zoomMap = o.pointedMap;
        std::snprintf(line, sizeof line, "Frame floatmap: right stick %s zooms the floating map it points at (%.0f cm; map view %s)", in.stickY > 0 ? "up" : "down",
                      o.size * 100, MapViewName());
        f.say(line);
    }
    // Once a run, the first time a copy is out: where the right laser leaves the controller, against its grip pose (no
    // laser is drawn in the world, so a tilt here is an aim you cannot see).
    if (!st.laserTold && o.event == Event::Pulled && f.aimTracked && f.handTracked[0]) {
        st.laserTold = true;
        const auto d = math::Rotate(math::Multiply(math::Conjugate(f.handQ), in.aimQ), {0, 0, -1});
        std::snprintf(line, sizeof line, "Frame floatmap: the right laser leaves the controller %+.0f deg up, %+.0f deg right of the grip's forward; %.0f cm from it",
                      std::asin(std::fmax(-1.f, std::fmin(1.f, d.y))) / Deg, std::atan2(d.x, -d.z) / Deg, std::sqrt(math::Dot(in.aim - in.hand[0], in.aim - in.hand[0])) * 100);
        f.say(line);
    }
    // A grip held back for the other one, and given to the game when none came (at most one line a 2 s).
    if (o.gaveBack && st.nowMs - st.gaveBackAt >= QuietMs) {
        st.gaveBackAt = st.nowMs;
        std::snprintf(line, sizeof line, "Frame floatmap: %s Grip held back %.0f ms, then the game's: no second grip came", o.gaveBack == 2 ? "Left" : "Right", t.bothGripsS * 1000);
        f.say(line);
    }
    // A press near a map that took nothing (at most one line a 2 s, with the count): why, and where its ray passed each
    // map. Only presses within twice the point-to-grab band of a map, so the game's own presses elsewhere stay quiet.
    // the copy: within twice its band, in its sides; the wrist map: within twice its band, in degrees
    const bool nearCopy = o.pressMap.known && o.pressMap.plane && std::fabs(o.pressMap.x) <= .5f + 2 * t.aimMargin && std::fabs(o.pressMap.y) <= .5f + 2 * t.aimMargin;
    const bool nearWrist = o.pressWrist.known && o.pressWrist.miss <= 2 * t.aimDeg;
    if (o.pressed && !o.held && (nearCopy || nearWrist)) {
        ++st.missed;
        if (st.nowMs - st.missAt >= QuietMs) {
            char a[150], b[150];
            if (o.pressMap.known) AimText(a, sizeof a, "map", o.pressMap); else std::snprintf(a, sizeof a, "no map out");
            if (o.pressWrist.known) AimText(b, sizeof b, "wrist", o.pressWrist); else std::snprintf(b, sizeof b, "the wrist's not shown");
            std::snprintf(line, sizeof line, "Frame floatmap: Right Grip not taken (%s; the copy within %.2f of its side past an edge and %.0f deg, the wrist map within %.0f deg); %s; %s; %u such presses",
                          RefusedName(o.refused), t.aimMargin, t.aimDeg, t.aimDeg, a, b, st.missed);
            f.say(line);
            st.missAt = st.nowMs; st.missed = 0;
        }
    }
    // The things that happened (each a hand's deliberate act; a few a minute at most).
    if (o.event == Event::None) return;
    char where[200], aimed[150];
    Where(where, sizeof where, o, in.head);
    aimed[0] = 0;
    if (o.pressed) {
        if (o.event == Event::Pulled && o.pressWrist.known) AimText(aimed, sizeof aimed, "wrist", o.pressWrist);
        else if (o.pressMap.known) AimText(aimed, sizeof aimed, "map", o.pressMap);
    }
    switch (o.event) {
    case Event::Grabbed:
        ++st.grabs;
        std::snprintf(line, sizeof line, "Frame floatmap: grabbed with Right Grip, %.2f m along the laser (%u this run; %u aim buzzes); %s", o.reach, st.grabs, st.aims, aimed);
        Tell(f, line, sizeof line); break;
    case Event::Released: {
        char push[96];
        if (o.pushes > 0) std::snprintf(push, sizeof push, "stick %d push%s, %.2f -> %.2f m along the laser", o.pushes, o.pushes == 1 ? "" : "es", o.reachStart, o.reach);
        else std::snprintf(push, sizeof push, "no stick push");
        std::snprintf(line, sizeof line, "Frame floatmap: let go %s; %s%s", where, push, t.faceOnRelease ? "; turned to face you" : "");
        Tell(f, line, sizeof line); break;
    }
    case Event::PutAwayNewGame:
        ++st.putAways;
        std::snprintf(line, sizeof line, "Frame floatmap: new game (session %llu), the map went back to the wrist", (unsigned long long)f.session);
        Tell(f, line, sizeof line); break;
    case Event::Pulled:
        ++st.pulls;
        std::snprintf(line, sizeof line, "Frame floatmap: pulled from the wrist with Right Grip, %s (%u this run); map view %s; %s", where, st.pulls, MapViewName(), aimed);
        Tell(f, line, sizeof line); break;
    case Event::PutAway:
        ++st.putAways;
        std::snprintf(line, sizeof line, "Frame floatmap: put away at the wrist (let go within %.0f cm; %u this run)", t.awayM * 100, st.putAways);
        Tell(f, line, sizeof line); break;
    case Event::ResizeStart:
        ++st.resizes;
        std::snprintf(line, sizeof line, "Frame floatmap: two-hand resize from %.0f cm, hands %.0f cm apart, %s (%u this run)", o.size * 100, o.spread * 100,
                      o.bothFirst == 2 ? "Left Grip first" : o.bothFirst == 1 ? "Right Grip first" : o.bothFirst == 3 ? "both grips together" : "Left Grip while held", st.resizes);
        Tell(f, line, sizeof line); break;
    case Event::ResizeEnd:
        std::snprintf(line, sizeof line, "Frame floatmap: resize done at %.0f cm", o.size * 100);
        Tell(f, line, sizeof line); break;
    default: break;
    }
}

void DrawFrame(const LayerFrame& f) {
    const bool fresh = st.fresh;
    st.fresh = false;
    const auto& o = st.out;
    const bool drawn = fresh && f.visible && f.picture && o.shown && o.size > 0;
    if (drawn) f.quad(f.quadCtx, o.p, o.q, o.size);
    char line[200];
    // What the copy shows against Map view: a line when it changes and holds (never per frame), loud if they differ.
    {
        const bool viewFull = drawn && framevr::settings::MapViewFull(vrsettings::Read().values[framevr::settings::MapTopDown]);
        const bool on = full::g_on.load(std::memory_order_relaxed), capture = full::g_capture.load(std::memory_order_relaxed), panel = full::g_panel.load(std::memory_order_relaxed);
        const int layout = full::g_layout.load(std::memory_order_relaxed);
        if (st.kind.Step(drawn, drawn && full::Active(on, capture, layout, panel), viewFull, f.nowMs * .001)) {
            std::snprintf(line, sizeof line, "Frame floatmap: the copy shows the %s map, Map view %s%s (redirect %d, capture %d, layout %d, panel %d)",
                          st.kind.Full() ? "full" : "corner", MapViewName(), st.kind.Matches() ? "" : ": THEY DIFFER", on, capture, layout, panel);
            f.say(line);
        }
    }
    // No picture while it is wanted, 3 s: once a run (the map surface's own "Frame menu map:" lines say why).
    if (fresh && o.want && f.visible && !f.picture) {
        if (!st.wantSince) st.wantSince = f.nowMs;
        else if (!st.pictureTold && f.nowMs - st.wantSince >= NoPictureMs) {
            st.pictureTold = true;
            f.say("Frame floatmap: no map picture for 3 s while wanted (see the \"Frame menu map:\" lines)");
        }
    } else st.wantSince = 0;
    // Shown / hidden as you look toward it or away: the first change a run is told; the rest (constant in play) are
    // counted onto the next event line.
    if (!st.engine.Placed()) st.lookSeen = false;
    else if (!st.lookSeen) {st.lookSeen = true; st.shownWas = o.shown;}   // a copy out: where it starts, untold
    else if (fresh && f.visible && o.shown != st.shownWas) {
        st.shownWas = o.shown;
        if (!st.lookTold) {
            st.lookTold = true;
            std::snprintf(line, sizeof line, "Frame floatmap: %s (%.0f deg from your view, shown within %.0f); later changes are counted on the next event line",
                          o.shown ? "shown" : "hidden", o.lookDeg, double(LookDeg));
            f.say(line);
        } else ++st.looks;
    }
}
}  // namespace framevr::floatmap
