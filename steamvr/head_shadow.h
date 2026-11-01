#pragma once
// Head shadow: in first person the hero keeps his whole head (and helm) in the pose, so his shadow has one, and the eye
// views draw nothing just in front of the eyes instead, so the head is not seen from inside.
//
// Before, the head and neck shrank into a point at the neck (vr/skeletons.cpp PoseBody, skel::Input::hideHead) so a
// camera at the eyes saw out instead of the inside of the skull. The shadow pass draws the same bones, so the shadow was
// headless too. Now [arms] hide_head, in VR's full-body view (F4), keeps the head in the pose and raises the eye
// projection's near clip (vrcam VrProjInner, [camera] near) to the cut below. The near clip is a plane: nothing nearer
// than the cut along the view is drawn (toward the edges of the view, somewhat farther from the eye). The shadow map is
// drawn from the light, not through the eye's frustum, so the shadow keeps the head. Every reader of the eye's near takes
// the same value (the projection, the headset's projection and depth, DLSS, the depth fog's NearPlane).
// [arms] head_shadow=0 shrinks the head as before.
//
// The cut follows head pitch: level, it is LevelCm; it eases (smoothstep) toward UpCm looking up and DownCm looking down
// and reaches them at PitchSpanDeg. The parts of the head and helm in front of the eyes fall inside it; the three values
// were tuned in the headset (a Paladin, helm on and off) so nothing of the head shows at any pitch, and looking down the
// small cut keeps the top of the chest.
//
// Order between the pose thread and the render thread (Handshake): a pose keeps the head only while the eye's cut is live
// (raised within CutLiveMs), and the eye holds the last cut for CutLiveMs after the last pose that kept the head. So a pose
// with the head never meets an eye view without the cut, whichever thread runs first, as long as a pose is drawn within
// CutLiveMs of being posed (frames faster than four a second), including when head_shadow is switched off live. A
// shapeshift form keeps the old shrink, and the eye does not cut there.
//
// This header is pure (no game, no Windows): steamvr/head_shadow_test.cpp. The game side is vr/head_shadow.cpp.
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace framevr::headshadow {

constexpr float LevelCm = 11.f;        // the cut looking level, cm at the player's scale
constexpr float UpCm = 13.f;           // looking up (the end value, reached at PitchSpanDeg)
constexpr float DownCm = 3.f;          // looking down
constexpr float PitchSpanDeg = 60.f;   // about as far as a head turns up or down from level
constexpr uint64_t CutLiveMs = 250;    // the cut counts as live this long after it was last raised, and is held this long

// at: the ms something last happened (0: never); now: this call's ms. True within CutLiveMs of it. A stamp a little
// ahead of now (another thread read the clock later) counts as live.
inline bool Live(uint64_t at, uint64_t now) { return at != 0 && (now <= at || now - at <= CutLiveMs); }

// Why this pose kept or hid the head (the log names it).
enum class Why : int { Kept, Form, NoCut };
inline const char* WhyName(Why w) {
    switch (w) {
    case Why::Kept: return "head kept for its shadow";
    case Why::Form: return "head hidden: a shapeshift form's rig";
    case Why::NoCut: return "head hidden: the eye's cut is not live yet";
    }
    return "?";
}

// How far along toward the end value, 0 (level) to 1 (at or past spanDeg): smoothstep of |pitch| / span.
// A pitch that is not known (NaN, infinite) or a span that is not positive: 0, the level cut.
inline float PitchWeight(float pitchDeg, float spanDeg = PitchSpanDeg) {
    if (!std::isfinite(pitchDeg) || !(spanDeg > 0) || !std::isfinite(spanDeg)) return 0;
    const float t = std::fabs(pitchDeg) / spanDeg;
    if (t >= 1) return 1;
    return t * t * (3 - 2 * t);
}
// The cut in cm at this head pitch (pitchDeg: up + / down -, degrees from the horizon; NaN: not known, the level cut).
// Level is levelCm exactly, easing toward upCm or downCm. A NaN, infinite or negative cut counts as 0 (none).
inline float PitchCut(float pitchDeg, float levelCm = LevelCm, float upCm = UpCm, float downCm = DownCm, float spanDeg = PitchSpanDeg) {
    auto clean = [](float cm) { return std::isfinite(cm) && cm > 0 ? cm : 0.f; };
    const float level = clean(levelCm);
    if (!(level > 0)) return 0;
    const float w = PitchWeight(pitchDeg, spanDeg);
    if (!(w > 0)) return level;
    const float end = clean(pitchDeg > 0 ? upCm : downCm);
    return w >= 1 ? end : level + (end - level) * w;
}

// The eye's near clip in world units: never nearer than the configured one, at least cutCm at the player's scale.
// unitsPerMetre: world units per metre of the player (vrcam ScaleHeight / UserEyeHeightM). 0 or a bad scale: unchanged.
inline float RaisedNear(float nearZ, float cutCm, float unitsPerMetre) {
    if (!(cutCm > 0) || !(unitsPerMetre > 0) || !std::isfinite(unitsPerMetre)) return nearZ;
    const float cut = cutCm * .01f * unitsPerMetre;
    return std::isfinite(cut) && cut > nearZ ? cut : nearZ;
}

// The two threads' meeting point. Pose: the skeleton hook's thread, each hero pose that would shrink the head with
// head_shadow on. Eye: the render thread, each first-person eye projection while the head may be kept or Holding.
struct Handshake {
    std::atomic<uint64_t> cutAt{0};   // ms the eye's near last stood at the cut (0: never)
    std::atomic<uint64_t> keptAt{0};  // ms a pose last kept the head (0: never)
    std::atomic<float> heldNear{0};   // the last cut's near (world units), held while Holding
    // form: a shapeshift form's rig. Kept: skip the shrink this pose (stamped before the pose is written).
    Why Pose(bool form, uint64_t now) {
        if (form) return Why::Form;
        if (!Live(cutAt.load(std::memory_order_acquire), now)) return Why::NoCut;
        keptAt.store(now, std::memory_order_release);
        return Why::Kept;
    }
    // A pose kept the head within CutLiveMs: the eye must keep the cut even with the head no longer to be kept.
    bool Holding(uint64_t now) const { return Live(keptAt.load(std::memory_order_acquire), now); }
    struct Near { float nearZ; bool atCut, held; };
    // on: the head may be kept (head_shadow, hide_head, full body, no form). cutCm: the cut at this pitch.
    Near Eye(float nearZ, bool on, float cutCm, float unitsPerMetre, uint64_t now) {
        Near r{nearZ, false, false};
        if (on && cutCm > 0 && unitsPerMetre > 0 && std::isfinite(unitsPerMetre)) {
            r.nearZ = RaisedNear(nearZ, cutCm, unitsPerMetre);   // at least the cut ([camera] near may already be past it)
            r.atCut = true;
            heldNear.store(r.nearZ, std::memory_order_relaxed);
            cutAt.store(now, std::memory_order_release);
        }
        // The hold: a pose kept the head within CutLiveMs (switched off, a form begun, or no scale this pass): keep the
        // last cut until the shrunk head is back.
        if (Holding(now)) {
            const float last = heldNear.load(std::memory_order_relaxed);
            if (last > r.nearZ) { r.nearZ = last; r.held = true; }
        }
        return r;
    }
};

// The state line: a state is told once it has lasted SettleMs, so a short gap (an area load's first poses before the
// first projection) says nothing. changes: the transitions seen since the last line.
struct StateLine {
    static constexpr uint64_t SettleMs = 1000;
    Why told = Why::Kept, seen = Why::Kept;
    bool any = false, seenAny = false;
    uint64_t seenAt = 0;
    int changes = 0;
    // True: say the line for `seen` now (changes: how many transitions were folded into it).
    bool Step(Why w, uint64_t now) {
        if (!seenAny || w != seen) { if (seenAny) ++changes; seen = w; seenAt = now; seenAny = true; }
        if ((any && seen == told) || now < seenAt || now - seenAt < SettleMs) return false;
        told = seen; any = true;
        return true;
    }
};

// How far the head's bones reach from the eye, for the debug summary: the nearest and farthest bone flagged in inNeck
// (the neck and everything under it), in cm. Joints, not the mesh.
struct Reach { int bones = 0; float nearCm = 0, farCm = 0; };
// bones: count matrices of 16 floats (row vectors: the translation at [12..14]); eye: model space; unitsPerCm > 0.
inline Reach HeadReach(const float* bones, const char* inNeck, int count, const float* eye, float unitsPerCm) {
    Reach r;
    if (!bones || !inNeck || !eye || count <= 0 || !(unitsPerCm > 0)) return r;
    for (int i = 0; i < count; ++i) {
        if (!inNeck[i]) continue;
        const float* m = bones + size_t(i) * 16;
        const float dx = m[12] - eye[0], dy = m[13] - eye[1], dz = m[14] - eye[2];
        const float cm = std::sqrt(dx * dx + dy * dy + dz * dz) / unitsPerCm;
        if (!std::isfinite(cm)) continue;
        if (!r.bones || cm < r.nearCm) r.nearCm = cm;
        if (!r.bones || cm > r.farCm) r.farCm = cm;
        ++r.bones;
    }
    return r;
}

}  // namespace framevr::headshadow

// The game side (vr/head_shadow.cpp). Not used by the pure test.
namespace headshadow {
// [debug] head_shadow: the 10 s summary line (off by default).
void SetTrace(bool on);
// PoseBody, just before the head shrink, only when it would shrink the head with head_shadow on in full body. form: a
// shapeshift form's rig. bones .. unitsPerCm: the pose, for the debug summary's reach only. Returns the new hide: false
// when the head stays whole this pose.
bool HideHead(bool form, const float* bones, const char* inNeck, int count, const float* eye, float unitsPerCm);
// VrProjInner, each first-person eye projection, when on or Holding(). on: the head may be kept. Returns the near clip
// to draw with.
float EyeNear(float nearZ, bool on, float unitsPerMetre, float pitchDeg);
bool Holding();
}  // namespace headshadow
