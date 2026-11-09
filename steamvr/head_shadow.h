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
// small cut keeps the top of the chest. Looking up, 13 cm was short for a tester (2026-11-05, the Den of Evil: "when I
// look up, a warped image in a circle begins to appear in my vision ... Gets bigger the more you lift your head"): the
// hero's head stays upright while yours tips back, so looking up sees through its crown and the helm on it. Up is 22 cm
// now. [arms] head_cut_level, head_cut_up and head_cut_down (ini only) set the three (CleanCut), read live.
//
// Helms that reach past the cut (2026-11-08: #429, a Druid's dev caster gear, "its snout is visible to the player"; on
// Discord "the teeth hover in front of your face ... Also causes black flickering at times"): a Druid pelt is an
// animal's head worn over the hero's, its snout or beak well ahead of the face, so looking level the teeth stood in the
// view past 11 cm. A deeper cut for them would also cut what you bring to your face (the bow's nock at the cheek, a
// potion at the mouth, a warcry's fist, a shield held up), and the eye views cannot tell the helm from the head (one
// skeleton, the same bones in the shadow pass). So with such a helm the head shrinks as before (Why::Helm) and the eye
// does not cut at all: only the shadow loses its head, while that helm is worn. Which helms: [arms] head_hide_helms
// (ParseHelms), item codes and the words pelts, barbarian and none; absent, the 15 Druid pelts dr1-drf (Pelts: in the HD
// art all are animal heads; Totemic Mask, the dev gear's Jalal's Mane, wears Spirit Mask's). The Barbarian helms (horns,
// jaws, fangs) keep the cut until a headset shows them past it: no report yet with the dev Barbarian's Slayer Guard.
//
// The cut moves the near from [camera] near (0.2 world units as installed, about 4 cm) to the cut, 2.6 times level and 5
// looking up, when it switches on or off (a shapeshift, a helm, F4). Readers a pass behind see the other pass's near: the
// depth fog's one NearPlane uniform, which ReShade rewrites at each present before the GPU has run the last pass's
// effects (vrcam g_tech). That far off, depth turns into distances 2.6-5 times too far or near, and a cave's fog (dark)
// or ceiling covers that pass for a frame - a black flash. So the eye's near moves at most StepRatio from one pass
// to the next (NearEase): down always (a cut kept a few passes longer only hides more), up too unless a kept head needs
// the cut at once (a pose kept it within CutLiveMs). The pitch moves the cut less than that a pass (8 % at 200 deg/s
// and 90 passes a second, 13 % at 300). The pose only keeps the head once the near has reached the cut (atCut), so
// easing up delays keeping the head (17 passes at most from 0.05), never the cut it needs.
//
// Order between the pose thread and the render thread (Handshake): a pose keeps the head only while the eye's cut is live
// (raised within CutLiveMs), and the eye holds the last cut for CutLiveMs after the last pose that kept the head. So a pose
// with the head never meets an eye view without the cut, whichever thread runs first, as long as a pose is drawn within
// CutLiveMs of being posed (frames faster than four a second), including when head_shadow is switched off live or a helm
// that hides the head is put on. A shapeshift form keeps the old shrink, and the eye does not cut there.
//
// This header is pure (no game, no Windows): steamvr/head_shadow_test.cpp. The game side is vr/head_shadow.cpp.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace framevr::headshadow {

constexpr float LevelCm = 11.f;        // the cut looking level, cm at the player's scale
constexpr float UpCm = 22.f;           // looking up (the end value, reached at PitchSpanDeg); 13 until 2026-11-05
constexpr float DownCm = 3.f;          // looking down
constexpr float PitchSpanDeg = 60.f;   // about as far as a head turns up or down from level
constexpr uint64_t CutLiveMs = 250;    // the cut counts as live this long after it was last raised, and is held this long

// at: the ms something last happened (0: never); now: this call's ms. True within CutLiveMs of it. A stamp a little
// ahead of now (another thread read the clock later) counts as live.
inline bool Live(uint64_t at, uint64_t now) { return at != 0 && (now <= at || now - at <= CutLiveMs); }

// Why this pose kept or hid the head (the log names it).
enum class Why : int { Kept, Form, NoCut, Helm };
inline const char* WhyName(Why w) {
    switch (w) {
    case Why::Kept: return "head kept for its shadow";
    case Why::Form: return "head hidden: a shapeshift form's rig";
    case Why::NoCut: return "head hidden: the eye's cut is not live yet";
    case Why::Helm: return "head hidden and no cut: the helm reaches past the cut ([arms] head_hide_helms)";
    }
    return "?";
}

// An item code as the game keeps it (Items::ItemInfo::code, gamestate CodeText): its letters from the low byte up, a
// three-letter code padded with a space ("dr1 ").
constexpr uint32_t ItemCode(char a, char b, char c) {
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(' ')) << 24;
}
constexpr uint32_t ItemCode(const char (&code)[4]) { return ItemCode(code[0], code[1], code[2]); }
// The letters of a code for the log ("dr1"), into out; "" for 0.
inline const char* CodeLetters(uint32_t code, char (&out)[5]) {
    for (int i = 0; i < 4; ++i) {
        const char ch = char((code >> (8 * i)) & 0xFF);
        out[i] = ch >= 33 && ch <= 126 ? ch : (ch == ' ' || ch == 0 ? 0 : '?');
    }
    out[4] = 0;
    for (int i = 0; i < 4; ++i) if (!out[i]) { for (int j = i; j < 4; ++j) out[j] = 0; break; }
    return out;
}

struct NamedHelm { uint32_t code; const char* name; };
// The Druid pelts (armor.txt type pelt; normal, exceptional, elite): an animal's head worn over the hero's.
inline constexpr NamedHelm Pelts[] = {
    {ItemCode("dr1"), "Wolf Head"},    {ItemCode("dr2"), "Hawk Helm"},        {ItemCode("dr3"), "Antlers"},
    {ItemCode("dr4"), "Falcon Mask"},  {ItemCode("dr5"), "Spirit Mask"},      {ItemCode("dr6"), "Alpha Helm"},
    {ItemCode("dr7"), "Griffon Headress"}, {ItemCode("dr8"), "Hunter's Guise"}, {ItemCode("dr9"), "Sacred Feathers"},
    {ItemCode("dra"), "Totemic Mask"}, {ItemCode("drb"), "Blood Spirit"},     {ItemCode("drc"), "Sun Spirit"},
    {ItemCode("drd"), "Earth Spirit"}, {ItemCode("dre"), "Sky Spirit"},       {ItemCode("drf"), "Dream Spirit"}};
// The Barbarian helms (type phlm): horns, jaws and fangs. Not hidden by default ([arms] head_hide_helms=barbarian).
inline constexpr NamedHelm BarbarianHelms[] = {
    {ItemCode("ba1"), "Jawbone Cap"},   {ItemCode("ba2"), "Fanged Helm"},    {ItemCode("ba3"), "Horned Helm"},
    {ItemCode("ba4"), "Assault Helmet"}, {ItemCode("ba5"), "Avenger Guard"}, {ItemCode("ba6"), "Jawbone Visor"},
    {ItemCode("ba7"), "Lion Helm"},     {ItemCode("ba8"), "Rage Mask"},      {ItemCode("ba9"), "Savage Helmet"},
    {ItemCode("baa"), "Slayer Guard"},  {ItemCode("bab"), "Carnage Helm"},   {ItemCode("bac"), "Fury Visor"},
    {ItemCode("bad"), "Destroyer Helm"}, {ItemCode("bae"), "Conqueror Crown"}, {ItemCode("baf"), "Guardian Crown"}};
// The base name of a class helm, for the log; nullptr for any other.
inline const char* HelmName(uint32_t code) {
    for (const NamedHelm& h : Pelts) if (h.code == code) return h.name;
    for (const NamedHelm& h : BarbarianHelms) if (h.code == code) return h.name;
    return nullptr;
}

// The helms that hide the head (the shrink, no cut) instead of keeping it behind the cut.
struct HelmList {
    static constexpr int Max = 48;
    uint32_t codes[Max]{};
    int count = 0;
    bool Has(uint32_t code) const {
        for (int i = 0; code && i < count; ++i) if (codes[i] == code) return true;
        return false;
    }
    // False: the list is full.
    bool Add(uint32_t code) {
        if (!code || Has(code)) return true;
        if (count >= Max) return false;
        codes[count++] = code;
        return true;
    }
    template <size_t N> bool AddAll(const NamedHelm (&helms)[N]) {
        bool all = true;
        for (const NamedHelm& h : helms) all = Add(h.code) && all;
        return all;
    }
};
inline HelmList DefaultHelms() { HelmList l; l.AddAll(Pelts); return l; }
// [arms] head_hide_helms: item codes (dr1, ba3: three letters or digits) and the words pelts (the 15 Druid pelts),
// barbarian (the 15 Barbarian helms) and none, split by spaces, commas or semicolons, in any case. Nothing (the key
// absent or empty): the default, the pelts. none alone: no helm hides the head. bad: the words not understood, or past
// HelmList::Max.
inline HelmList ParseHelms(const char* text, int* bad = nullptr) {
    auto sep = [](char ch) { return ch == ' ' || ch == ',' || ch == ';' || ch == '\t' || ch == '\r' || ch == '\n'; };
    HelmList l;
    int rejected = 0;
    bool any = false;
    for (const char* p = text ? text : ""; *p;) {
        if (sep(*p)) { ++p; continue; }
        char w[12] = {};
        int n = 0;
        bool tooLong = false;
        for (; *p && !sep(*p); ++p) {
            if (n < int(sizeof w) - 1) w[n++] = char(std::tolower(static_cast<unsigned char>(*p)));
            else tooLong = true;
        }
        any = true;
        const std::string_view word(w, size_t(n));
        bool alnum = n == 3;
        for (int i = 0; alnum && i < 3; ++i) alnum = std::isalnum(static_cast<unsigned char>(w[i])) != 0;
        if (tooLong) ++rejected;
        else if (word == "none") {}
        else if (word == "pelts" || word == "pelt") { if (!l.AddAll(Pelts)) ++rejected; }
        else if (word == "barbarian" || word == "phlm") { if (!l.AddAll(BarbarianHelms)) ++rejected; }
        else if (alnum) { if (!l.Add(ItemCode(w[0], w[1], w[2]))) ++rejected; }
        else ++rejected;
    }
    if (!any) l = DefaultHelms();
    if (bad) *bad = rejected;
    return l;
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

// A cut from the ini, cm: within 0..MaxCutCm, or the default when it is not a number.
constexpr float MaxCutCm = 40.f;
inline float CleanCut(float cm, float fallback) { return std::isfinite(cm) ? std::clamp(cm, 0.f, MaxCutCm) : fallback; }

// The eye's near clip in world units: never nearer than the configured one, at least cutCm at the player's scale.
// unitsPerMetre: world units per metre of the player (vrcam ScaleHeight / UserEyeHeightM). 0 or a bad scale: unchanged.
inline float RaisedNear(float nearZ, float cutCm, float unitsPerMetre) {
    if (!(cutCm > 0) || !(unitsPerMetre > 0) || !std::isfinite(unitsPerMetre)) return nearZ;
    const float cut = cutCm * .01f * unitsPerMetre;
    return std::isfinite(cut) && cut > nearZ ? cut : nearZ;
}

// The eye's near from one pass to the next (the render thread's own; see the top). Up to StepRatio either way, up at
// once when upFree; the first pass, or one after a gap of over GapMs (a load, a menu), takes what it wants.
struct NearEase {
    static constexpr float StepRatio = 1.15f;   // a pass's near within 15 % of the last's
    static constexpr uint64_t GapMs = 250;
    float last = 0;        // the near the last pass drew with (0: none)
    uint64_t lastAt = 0;   // its ms
    float Step(float want, uint64_t now, bool upFree) {
        if (!(want > 0) || !std::isfinite(want)) { last = 0; lastAt = 0; return want; }
        float out = want;
        const bool fresh = last > 0 && lastAt != 0 && (now <= lastAt || now - lastAt <= GapMs);
        if (fresh) {
            if (want < last / StepRatio) out = last / StepRatio;
            else if (!upFree && want > last * StepRatio) out = last * StepRatio;
        }
        last = out;
        lastAt = now;
        return out;
    }
};

// The two threads' meeting point. Pose: the skeleton hook's thread, each hero pose that would shrink the head with
// head_shadow on. Eye: the render thread, each first-person eye projection (vrcam VrProjInner).
struct Handshake {
    std::atomic<uint64_t> cutAt{0};   // ms the eye's near last stood at the cut (0: never)
    std::atomic<uint64_t> keptAt{0};  // ms a pose last kept the head (0: never)
    std::atomic<float> heldNear{0};   // the last cut's near (world units), held while Holding
    NearEase ease;                    // the render thread's only
    // form: a shapeshift form's rig. helm: a helm that hides the head ([arms] head_hide_helms). Kept: skip the shrink
    // this pose (stamped before the pose is written).
    Why Pose(bool form, uint64_t now, bool helm = false) {
        if (form) return Why::Form;
        if (helm) return Why::Helm;
        if (!Live(cutAt.load(std::memory_order_acquire), now)) return Why::NoCut;
        keptAt.store(now, std::memory_order_release);
        return Why::Kept;
    }
    // A pose kept the head within CutLiveMs: the eye must keep the cut even with the head no longer to be kept.
    bool Holding(uint64_t now) const { return Live(keptAt.load(std::memory_order_acquire), now); }
    // nearZ: the configured near this pass. atCut: the near drawn is at least this pitch's cut (the pose may keep the
    // head); held: the hold supplied it; eased: NearEase moved it off what the pass wanted.
    struct Near { float nearZ; bool atCut, held, eased; };
    // on: the head may be kept (head_shadow, hide_head, full body, no form, no helm that hides it). cutCm: the cut at
    // this pitch.
    Near Eye(float nearZ, bool on, float cutCm, float unitsPerMetre, uint64_t now) {
        Near r{nearZ, false, false, false};
        const bool cutting = on && cutCm > 0 && unitsPerMetre > 0 && std::isfinite(unitsPerMetre);
        float want = cutting ? RaisedNear(nearZ, cutCm, unitsPerMetre) : nearZ;   // ([camera] near may already be past it)
        // The hold: a pose kept the head within CutLiveMs and this pass does not cut (switched off, a form or a helm
        // begun, or no scale this pass): keep the last cut until the shrunk head is back.
        const bool holding = Holding(now);
        if (!cutting && holding) {
            const float last = heldNear.load(std::memory_order_relaxed);
            if (last > want) { want = last; r.held = true; }
        }
        r.nearZ = ease.Step(want, now, holding);
        r.eased = r.nearZ != want;
        if (cutting && r.nearZ >= want) {   // at the cut, not still easing up to it
            r.atCut = true;
            heldNear.store(r.nearZ, std::memory_order_relaxed);
            cutAt.store(now, std::memory_order_release);
        }
        return r;
    }
};

// The state line: a state is told once it has lasted SettleMs, so a short gap (an area load's first poses before the
// first projection) says nothing. A new helm is a new state (the line names the helm the choice was made with).
// changes: the transitions seen since the last line.
struct StateLine {
    static constexpr uint64_t SettleMs = 1000;
    Why told = Why::Kept, seen = Why::Kept;
    uint32_t toldHelm = 0, seenHelm = 0;
    bool any = false, seenAny = false;
    uint64_t seenAt = 0;
    int changes = 0;
    // True: say the line for `seen` now (changes: how many transitions were folded into it). helm: the helm's item code
    // (0: none).
    bool Step(Why w, uint64_t now, uint32_t helm = 0) {
        if (!seenAny || w != seen || helm != seenHelm) {
            if (seenAny) ++changes;
            seen = w; seenHelm = helm; seenAt = now; seenAny = true;
        }
        if ((any && seen == told && seenHelm == toldHelm) || now < seenAt || now - seenAt < SettleMs) return false;
        told = seen; toldHelm = seenHelm; any = true;
        return true;
    }
};

// The near from pass to pass, for the log: a jump past NearEase::StepRatio (a pass after a gap, or up at once for a
// kept head) puts a reader a pass behind off by as much. gap: the passes are over NearEase::GapMs apart (not a jump).
struct NearWatch {
    float last = 0;
    uint64_t lastAt = 0;
    int jumps = 0;            // since the last Take
    float from = 0, to = 0;   // the largest of them
    bool Pass(float nearZ, uint64_t now) {
        const float was = last;
        const bool fresh = was > 0 && lastAt != 0 && (now <= lastAt || now - lastAt <= NearEase::GapMs);
        last = nearZ; lastAt = now;
        if (!fresh || !(nearZ > 0) || !std::isfinite(nearZ) || !std::isfinite(was)) return false;
        const float ratio = nearZ > was ? nearZ / was : was / nearZ;
        if (!(ratio > NearEase::StepRatio * 1.0001f)) return false;
        const float largest = from > 0 && to > 0 ? std::max(from / to, to / from) : 0;
        if (ratio > largest) { from = was; to = nearZ; }
        ++jumps;
        return true;
    }
    void Take() { jumps = 0; from = to = 0; }
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
// [debug] head_shadow: the once-a-second summary line (off by default).
void SetTrace(bool on);
// [arms] head_cut_level, head_cut_up, head_cut_down: the cuts in cm (CleanCut'ed), for every eye pass from now.
void SetCuts(float levelCm, float upCm, float downCm);
// [arms] head_hide_helms (ParseHelms; nullptr or "": the pelts). Returns how many helms hide the head; bad: the words
// not understood.
int SetHelms(const char* text, int* bad);
// PoseBody, just before the head shrink, only when it would shrink the head with head_shadow on in full body. form: a
// shapeshift form's rig. bones .. unitsPerCm: the pose, for the debug summary's reach only. Returns the new hide: false
// when the head stays whole this pose.
bool HideHead(bool form, const float* bones, const char* inNeck, int count, const float* eye, float unitsPerCm);
// VrProjInner, every first-person eye projection, the head kept or not (the ease and the hold run on each). on: the
// head may be kept (full body, head_shadow, hide_head, no form; the helm is checked here). Returns the near clip to
// draw with.
float EyeNear(float nearZ, bool on, float unitsPerMetre, float pitchDeg);
bool Holding();
}  // namespace headshadow
