// Head shadow (steamvr/head_shadow.h): the pose and eye handshake (when the head is kept, the eye's near clip, the
// hold), the helms that hide the head, the near's ease from pass to pass, the state line, the cut by head pitch, the
// head bones' reach.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include "head_shadow.h"

namespace hs = framevr::headshadow;
static int g_fail = 0;
static void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; } }
static bool Near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }

int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (int w = 0; w <= int(hs::Why::Helm); ++w) Check(hs::WhyName(hs::Why(w))[0] != '?', "every reason has a name");
    // The handshake as vr/head_shadow.cpp drives it: Pose from the skeleton thread, Eye from the render thread.
    // The rule under test: a pose that keeps the head (Kept at time t) is never followed, within CutLiveMs, by an eye
    // pass whose near is below the cut it saw last.
    const float upm = 4.78f, base = .2f, cutNear = hs::RaisedNear(base, hs::LevelCm, upm);
    {   // the first poses come before any eye pass: shrunk until the eye has cut
        hs::Handshake h;
        Check(h.Pose(false, 1000) == hs::Why::NoCut && !h.Holding(1000), "no eye pass yet: shrunk, nothing to hold");
        const auto e = h.Eye(base, true, hs::LevelCm, upm, 1010);
        Check(e.atCut && !e.held && Near(e.nearZ, cutNear), "the eye raises the near to the cut");
        Check(h.Pose(false, 1020) == hs::Why::Kept && h.Holding(1020), "then the pose keeps the head and the eye must hold");
        Check(h.Pose(true, 1030) == hs::Why::Form, "a shapeshift form's rig: shrunk even with the cut live");
    }
    {   // steady play, then head_shadow switched off live: the eye holds the last cut until the shrunk head is back
        hs::Handshake h;
        uint64_t t = 1000;
        for (int i = 0; i < 30; ++i, t += 11) { h.Eye(base, true, hs::LevelCm, upm, t); Check(h.Pose(false, t + 5) == hs::Why::Kept, "steady: kept every pose"); }
        const uint64_t lastKept = t - 11 + 5;
        bool safe = true, released = false;
        for (uint64_t now = t; now < t + 1000; now += 11) {
            const auto e = h.Eye(base, false, hs::LevelCm, upm, now);   // off: no raise of its own
            if (now <= lastKept + hs::CutLiveMs) safe = safe && e.held && Near(e.nearZ, cutNear);
            else released = released || Near(e.nearZ, base);
            if (now > t + hs::CutLiveMs + 20) Check(h.Pose(false, now) == hs::Why::NoCut, "off: the cut goes stale, the pose shrinks again");
        }
        Check(safe, "switched off: the cut held for CutLiveMs after the last kept pose");
        Check(released, "then the near returns to [camera] near");
    }
    {   // a pass with no scale (the eye height not known yet): the hold keeps the last cut
        hs::Handshake h;
        h.Eye(base, true, hs::LevelCm, upm, 1000);
        Check(h.Pose(false, 1005) == hs::Why::Kept, "kept");
        const auto e = h.Eye(base, true, hs::LevelCm, 0, 1016);
        Check(!e.atCut && e.held && Near(e.nearZ, cutNear), "no scale this pass: the last cut, held");
        const auto n = h.Eye(base, true, hs::LevelCm, nan, 1027);
        Check(n.held && Near(n.nearZ, cutNear), "a NaN scale: held too");
    }
    {   // the render thread stalls: the cut goes stale and the pose shrinks the head again (never kept without a live cut)
        hs::Handshake h;
        h.Eye(base, true, hs::LevelCm, upm, 1000);
        Check(h.Pose(false, 1000 + hs::CutLiveMs) == hs::Why::Kept, "exactly CutLiveMs after the cut: still kept");
        Check(h.Pose(false, 1001 + hs::CutLiveMs) == hs::Why::NoCut, "past it: shrunk");
        Check(h.Pose(false, 990) == hs::Why::Kept, "a pose thread that read the clock just before the eye's stamp: the cut counts as live");
    }
    {   // the pitch moves the cut down: the eye's own lower cut wins over the hold (the hold only covers a missing raise),
        // reached at most 15 % a pass (a cut a few passes deeper only hides more); at the cut all the way
        hs::Handshake h;
        h.Eye(base, true, hs::UpCm, upm, 1000);
        h.Pose(false, 1005);
        const float down = std::max(base, hs::RaisedNear(base, hs::DownCm, upm));
        float last = hs::RaisedNear(base, hs::UpCm, upm);
        bool atCut = true, neverHeld = true, smooth = true, reached = false;
        for (uint64_t t = 1016; t < 1400; t += 11) {
            const auto e = h.Eye(base, true, hs::DownCm, upm, t);
            h.Pose(false, t + 5);
            atCut = atCut && e.atCut; neverHeld = neverHeld && !e.held;
            smooth = smooth && e.nearZ <= last && e.nearZ >= last / hs::NearEase::StepRatio * .9999f;
            reached = reached || Near(e.nearZ, down);
            last = e.nearZ;
        }
        Check(atCut && neverHeld && smooth && reached, "looking down: down to the down cut, 15 % a pass at most, never the held up cut");
    }
    {   // a helm that hides the head (vr/head_shadow.cpp passes on && !helm): the pose shrinks it, nothing to hold; the
        // eye does not cut, so what you bring to the face (a bow's nock, a potion at the mouth) is drawn
        hs::Handshake h;
        const auto e = h.Eye(base, false, hs::LevelCm, upm, 1000);
        Check(!e.atCut && !e.held && Near(e.nearZ, base), "a pelt: the near as configured");
        Check(h.Pose(false, 1005, true) == hs::Why::Helm && !h.Holding(1005), "a pelt: the pose shrinks the head for the helm and stamps nothing");
        Check(h.Pose(true, 1006, true) == hs::Why::Form, "in a form, the form's reason first");
    }
    for (int eyeFirst = 0; eyeFirst < 2; ++eyeFirst) {   // the cut live, then a pelt put on: whichever thread sees it
        // first (the other three passes later), a kept head never meets a pass short of the cut
        hs::Handshake h;
        uint64_t t = 1000;
        for (int i = 0; i < 20; ++i, t += 11) { h.Eye(base, true, hs::LevelCm, upm, t); h.Pose(false, t + 5); }
        uint64_t lastKept = t - 11 + 5;
        bool safe = true, shrunk = false, released = false;
        for (int i = 0; i < 80; ++i, t += 11) {
            const bool eyeSees = eyeFirst || i >= 3, poseSees = !eyeFirst || i >= 3;
            const auto e = h.Eye(base, !eyeSees, hs::LevelCm, upm, t);
            if (t <= lastKept + hs::CutLiveMs) safe = safe && e.nearZ >= cutNear * .9999f;
            const hs::Why w = h.Pose(false, t + 5, poseSees);
            if (w == hs::Why::Kept) lastKept = t + 5;
            shrunk = shrunk || w == hs::Why::Helm;
            released = released || Near(e.nearZ, base);
        }
        Check(safe, eyeFirst ? "a pelt on, the eye first: the cut held while poses still kept the head"
                             : "a pelt on, the pose first: the cut held for the last kept pose");
        Check(shrunk && released, "then the head shrunk for the helm and the near back to [camera] near");
    }
    for (const float configured : {.05f, .2f}) {   // the pelt taken off (or the cut switched on): the near eases up to the
        // cut 15 % a pass; the pose keeps the head only once it is there, so no kept head is drawn short of the cut.
        // .2: [camera] near as installed; .05: the least VrProjInner takes, ten times short of the cut
        hs::Handshake h;
        const float cut = hs::RaisedNear(configured, hs::LevelCm, upm);
        uint64_t t = 1000;
        for (int i = 0; i < 5; ++i, t += 11) { h.Eye(configured, false, hs::LevelCm, upm, t); h.Pose(false, t + 5, true); }
        float last = configured;
        int shortPasses = 0;
        bool kept = false, keptShort = false, smooth = true;
        for (int i = 0; i < 40; ++i, t += 11) {
            const auto e = h.Eye(configured, true, hs::LevelCm, upm, t);
            smooth = smooth && e.nearZ <= last * hs::NearEase::StepRatio * 1.0001f;
            keptShort = keptShort || (kept && e.nearZ < cut * .9999f);
            if (!e.atCut) ++shortPasses;
            last = e.nearZ;
            kept = h.Pose(false, t + 5) == hs::Why::Kept;
        }
        const int expected = int(std::ceil(std::log(cut / configured) / std::log(hs::NearEase::StepRatio)));
        Check(smooth && std::abs(shortPasses - expected) <= 1, "switched on: up to the cut 15 % a pass");
        Check(kept && !keptShort, "the head kept only once the near stands at the cut");
    }
    for (const float configured : {.05f, .2f}) {   // a head pitching at 300 deg/s, 90 passes a second (3.3 deg a pass), up
        // and back down: the cut moves less than the ease's step a pass, so nothing is eased and no pass jumps
        hs::Handshake h;
        hs::NearWatch watch;
        uint64_t t = 1000;
        bool eased = false, atCut = true;
        int jumps = 0;
        for (int dir = 1; dir >= -1; dir -= 2)
            for (float p = -90.f * dir; p * dir <= 90; p += dir * 300.f / 90.f, t += 11) {
                const auto e = h.Eye(configured, true, hs::PitchCut(p), upm, t);
                h.Pose(false, t + 5);
                eased = eased || e.eased; atCut = atCut && e.atCut;
                jumps += watch.Pass(e.nearZ, t) ? 1 : 0;
            }
        Check(!eased && atCut && jumps == 0, "pitch alone: never eased, always at the cut, no jump");
    }
    {   // NearEase: 15 % a pass either way, up at once when free; the first pass and one after a gap take what they want
        hs::NearEase e;
        Check(Near(e.Step(.05f, 1000, false), .05f), "the first pass: as wanted");
        Check(Near(e.Step(.526f, 1011, false), .05f * hs::NearEase::StepRatio), "up 15 % a pass");
        Check(Near(e.Step(.526f, 1022, true), .526f), "up at once when free (a kept head needs the cut)");
        Check(Near(e.Step(.05f, 1033, true), .526f / hs::NearEase::StepRatio), "down 15 % a pass, free or not");
        Check(Near(e.Step(.05f, 1033 + hs::NearEase::GapMs + 1, false), .05f), "after a gap (a load, a menu): as wanted");
        Check(std::isnan(e.Step(nan, 2000, false)) && Near(e.Step(.3f, 2011, false), .3f), "a bad near passes through and starts afresh");
    }
    {   // NearWatch: a jump past the ease's step between two passes; a gap is no jump
        hs::NearWatch w;
        Check(!w.Pass(.05f, 1000) && !w.Pass(.055f, 1011) && w.jumps == 0, "small steps: no jump");
        Check(w.Pass(.526f, 1022) && w.jumps == 1 && Near(w.from, .055f) && Near(w.to, .526f), "the near up ten times at once: a jump, with its two nears");
        Check(w.Pass(.2f, 1033) && w.jumps == 2 && Near(w.from, .055f), "a smaller jump after it: counted, the largest kept");
        Check(!w.Pass(.05f, 1033 + hs::NearEase::GapMs + 1), "after a gap: not a jump");
        w.Take();
        Check(w.jumps == 0 && w.from == 0 && w.to == 0, "taken: counted afresh");
    }
    {   // the helms: item codes as the game keeps them, the default list (the Druid pelts), the class helms' names
        Check(hs::ItemCode("dr1") == (uint32_t('d') | uint32_t('r') << 8 | uint32_t('1') << 16 | uint32_t(' ') << 24),
              "an item code: the letters from the low byte up, padded with a space (gamestate CodeText)");
        char letters[5];
        Check(std::string_view(hs::CodeLetters(hs::ItemCode("dra"), letters)) == "dra" && hs::CodeLetters(0, letters)[0] == 0,
              "a code's letters for the log");
        const hs::HelmList d = hs::DefaultHelms();
        Check(d.count == 15, "the default: the 15 Druid pelts");
        for (const char* p = "123456789abcdef"; *p; ++p) Check(d.Has(hs::ItemCode('d', 'r', *p)), "every pelt dr1-drf hides the head");
        Check(d.Has(hs::ItemCode("dra")) && d.Has(hs::ItemCode("dr1")) && d.Has(hs::ItemCode("dr6")) && d.Has(hs::ItemCode("drb")),
              "the reported ones: Totemic Mask (the dev Druid's Jalal's Mane, #429), Wolf Head, Alpha Helm, Blood Spirit");
        for (const char* c : {"uap", "cap", "msk", "usk", "ghm", "crn", "ci3", "bhm", "ba1", "ba3", "baa"})
            Check(!d.Has(hs::ItemCode(c[0], c[1], c[2])), "other helms keep the head behind the cut (caps, masks, crowns, circlets, Barbarian helms)");
        Check(!d.Has(0), "no helm: the head kept");
        Check(std::string_view(hs::HelmName(hs::ItemCode("dra"))) == "Totemic Mask" && std::string_view(hs::HelmName(hs::ItemCode("baa"))) == "Slayer Guard" &&
              hs::HelmName(hs::ItemCode("uap")) == nullptr, "class helms named for the log");
    }
    {   // ParseHelms ([arms] head_hide_helms): codes and words, any case; nothing means the pelts, none means no helm
        int bad = -1;
        Check(hs::ParseHelms(nullptr, &bad).count == 15 && bad == 0, "the key absent: the pelts");
        Check(hs::ParseHelms("", &bad).count == 15 && hs::ParseHelms("  , ;", &bad).count == 15, "empty: the pelts");
        const hs::HelmList none = hs::ParseHelms("none", &bad);
        Check(none.count == 0 && bad == 0 && !none.Has(hs::ItemCode("dr1")), "none: no helm hides the head");
        const hs::HelmList mix = hs::ParseHelms("Pelts, BA3;ba5 dr1", &bad);
        Check(mix.count == 17 && bad == 0 && mix.Has(hs::ItemCode("ba3")) && mix.Has(hs::ItemCode("ba5")) && mix.Has(hs::ItemCode("drf")),
              "pelts and two Barbarian helms, any case; a code given twice counts once");
        Check(hs::ParseHelms("barbarian", &bad).count == 15 && hs::ParseHelms("pelts barbarian", &bad).count == 30, "barbarian: the 15 Barbarian helms");
        const hs::HelmList odd = hs::ParseHelms("dr wolfhead d-1 dr1 averyveryverylongword", &bad);
        Check(odd.count == 1 && odd.Has(hs::ItemCode("dr1")) && bad == 4, "words not understood are counted and left out");
        std::string many;
        for (int i = 0; i < 60; ++i) { char c[8]; std::snprintf(c, sizeof c, "x%02d ", i); many += c; }
        const hs::HelmList full = hs::ParseHelms(many.c_str(), &bad);
        Check(full.count == hs::HelmList::Max && bad == 60 - hs::HelmList::Max, "past the list's size: counted as not understood");
    }
    {   // StateLine with the helm: a new helm is a new state, told after a second; the same state and helm not again
        hs::StateLine s;
        const uint32_t cap = hs::ItemCode("uap"), pelt = hs::ItemCode("dra");
        Check(!s.Step(hs::Why::Kept, 0, cap) && s.Step(hs::Why::Kept, 1000, cap), "kept with a Shako: told after a second");
        Check(!s.Step(hs::Why::Kept, 2000, cap), "the same: not again");
        s.changes = 0;
        Check(!s.Step(hs::Why::Helm, 3000, pelt) && s.Step(hs::Why::Helm, 4000, pelt) && s.changes == 1, "a pelt put on: hidden for the helm, told after a second");
        Check(!s.Step(hs::Why::Kept, 5000, 0) && !s.Step(hs::Why::Kept, 5500, 0) && s.Step(hs::Why::Kept, 6000, 0), "taken off: kept with no helm, told");
        s.changes = 0;
        Check(!s.Step(hs::Why::Kept, 7000, cap) && !s.Step(hs::Why::Kept, 7400, 0) && !s.Step(hs::Why::Kept, 9000, 0) && s.changes == 2,
              "a helm on and off within a second: nothing told, the changes counted for the next line");
    }
    {   // [camera] near past the cut: kept, and still counts as the cut
        hs::Handshake h;
        const auto e = h.Eye(1.5f, true, hs::LevelCm, upm, 1000);
        Check(e.atCut && Near(e.nearZ, 1.5f) && h.Pose(false, 1001) == hs::Why::Kept, "a farther configured near: kept as is, the head kept");
    }
    {   // StateLine: told once a state has lasted a second; a short gap (an area load) says nothing; changes counted
        hs::StateLine s;
        Check(!s.Step(hs::Why::Kept, 0) && !s.Step(hs::Why::Kept, 500), "the first second: nothing yet");
        Check(s.Step(hs::Why::Kept, 1000) && s.changes == 0, "a second kept: told once");
        Check(!s.Step(hs::Why::Kept, 5000), "the same state: not told again");
        s.changes = 0;
        Check(!s.Step(hs::Why::NoCut, 6000) && !s.Step(hs::Why::NoCut, 6400) && !s.Step(hs::Why::Kept, 6500) && !s.Step(hs::Why::Kept, 9000),
              "a short NoCut (an area load) and back: nothing told");
        Check(!s.Step(hs::Why::NoCut, 10000) && s.Step(hs::Why::NoCut, 11000) && s.changes == 3, "a NoCut that lasts a second: told, with the changes since the last line");
        Check(!s.Step(hs::Why::Kept, 10900) && !s.Step(hs::Why::Kept, 10800), "a new state, then a clock read a little behind (another pose thread): not told early");
    }
    {   // Live: within CutLiveMs of a stamp; never before the first; a stamp a little ahead (another thread) counts
        Check(!hs::Live(0, 1000), "never stamped: not live");
        Check(hs::Live(1000, 1000) && hs::Live(1000, 1000 + hs::CutLiveMs), "the same ms, and exactly CutLiveMs later: live");
        Check(!hs::Live(1000, 1001 + hs::CutLiveMs), "past CutLiveMs: not live");
        Check(hs::Live(1016, 1000), "stamped by the other thread after this one read the clock: live, not a wrapped gap");
        Check(hs::CutLiveMs >= 100 && hs::CutLiveMs <= 1000, "the hold spans several frames and stays short");
    }
    {   // The tuned values: level, up and down, and the span
        Check(hs::LevelCm == 11 && hs::UpCm == 22 && hs::DownCm == 3, "the headset-tuned cuts: 11 cm level, 22 up (13 showed the helm from inside), 3 down");
        Check(hs::CleanCut(26, hs::UpCm) == 26 && hs::CleanCut(99, hs::UpCm) == hs::MaxCutCm && hs::CleanCut(-4, hs::UpCm) == 0 &&
              hs::CleanCut(nan, hs::UpCm) == hs::UpCm && hs::CleanCut(inf, hs::UpCm) == hs::UpCm, "an ini cut: kept within 0..40 cm, a non-number the default");
        Check(hs::PitchSpanDeg > 0 && hs::PitchSpanDeg < 90, "the span is a real head pitch, short of straight up");
        Check(hs::PitchCut(0) == hs::LevelCm && hs::PitchCut(90) == hs::UpCm && hs::PitchCut(-90) == hs::DownCm, "the defaults: level, straight up, straight down");
    }
    {   // RaisedNear: never nearer than the configured near, the cut at the player's scale, bad input keeps the near
        Check(Near(hs::RaisedNear(.2f, 25, 4.f), 1.f), "25 cm at 4 units/m: 1 unit");
        Check(Near(hs::RaisedNear(.2f, 11, 4.78f), .5258f, 1e-3f), "11 cm at 4.78 units/m (a headset log): 0.526 units");
        Check(Near(hs::RaisedNear(1.5f, 25, 4.f), 1.5f), "[camera] near already past the cut: kept");
        Check(Near(hs::RaisedNear(.2f, 0, 4.f), .2f), "no cut: the near as configured");
        Check(Near(hs::RaisedNear(.2f, 25, 0), .2f), "no scale: the near as configured");
        Check(Near(hs::RaisedNear(.2f, 25, -3), .2f), "a negative scale: the near as configured");
        Check(Near(hs::RaisedNear(.2f, 25, inf), .2f), "an infinite scale: the near as configured");
        Check(Near(hs::RaisedNear(.2f, 25, nan), .2f), "a NaN scale: the near as configured");
        Check(Near(hs::RaisedNear(.2f, nan, 4.f), .2f), "a NaN cut: the near as configured");
    }
    {   // HeadReach: only inNeck bones, distance from the eye in cm
        float bones[4 * 16] = {};
        auto at = [&](int i, float x, float y, float z) { bones[i * 16 + 12] = x; bones[i * 16 + 13] = y; bones[i * 16 + 14] = z; bones[i * 16 + 15] = 1; };
        at(0, 0, 0, 0);          // the pelvis: not in the neck
        at(1, 0, 1.6f, 0);       // the neck: 10 cm below the eye
        at(2, 0, 1.75f, .03f);   // the head: 5 cm above, 3 back
        at(3, 0, 1.9f, 0);       // a helm's bone: 20 cm above
        const char inNeck[4] = {0, 1, 1, 1};
        const float eye[3] = {0, 1.7f, 0};
        const hs::Reach r = hs::HeadReach(bones, inNeck, 4, eye, .01f);   // 1 model unit = 1 m
        Check(r.bones == 3, "three bones in the neck");
        Check(Near(r.nearCm, std::sqrt(25.f + 9.f), 1e-2f) && Near(r.farCm, 20, 1e-2f), "nearest the head bone, farthest the helm's");
        Check(hs::HeadReach(bones, inNeck, 4, nullptr, .01f).bones == 0, "no eye point: nothing measured");
        Check(hs::HeadReach(bones, inNeck, 4, eye, 0).bones == 0, "no scale: nothing measured");
        Check(hs::HeadReach(nullptr, inNeck, 4, eye, .01f).bones == 0 && hs::HeadReach(bones, nullptr, 4, eye, .01f).bones == 0, "no bones: nothing");
        const char none[4] = {};
        Check(hs::HeadReach(bones, none, 4, eye, .01f).bones == 0, "no neck bones: nothing");
    }
    {   // PitchCut: level is the level cut exactly, easing to the up / down cut by PitchSpanDeg, flat past it
        const float span = hs::PitchSpanDeg;
        Check(hs::PitchCut(0, 12, 30, 6) == 12 && hs::PitchCut(0, 25, 50, 0) == 25 && hs::PitchCut(-0.f, 12, 30, 6) == 12, "level (and -0): the level cut");
        for (float p = -90; p <= 90; p += 0.5f) Check(hs::PitchCut(p, 12, 12, 12) == 12, "equal cuts: the same at every pitch, bit for bit");
        Check(hs::PitchCut(span, 12, 30, 6) == 30 && hs::PitchCut(-span, 12, 30, 6) == 6, "at the span: the up / down cut exactly");
        Check(hs::PitchCut(89, 12, 30, 6) == 30 && hs::PitchCut(-89, 12, 30, 6) == 6 && hs::PitchCut(1000, 12, 30, 6) == 30, "past it: clamped at the end value");
        Check(Near(hs::PitchCut(span / 2, 12, 30, 6), 21) && Near(hs::PitchCut(-span / 2, 12, 30, 6), 9), "half way (smoothstep 0.5): the midpoint");
        Check(Near(hs::PitchCut(span / 4, 10, 20, 10), 10 + 10 * .15625f) && Near(hs::PitchCut(span * .75f, 10, 20, 10), 10 + 10 * .84375f),
              "a quarter and three quarters: smoothstep 0.156 and 0.844");
        Check(Near(hs::PitchWeight(span / 2), .5f) && hs::PitchWeight(0) == 0 && hs::PitchWeight(span) == 1 && hs::PitchWeight(-span * 2) == 1, "the weight: 0, 0.5, 1, clamped");
        Check(hs::PitchCut(1, 12, 30, 6) - 12 < .05f && hs::PitchCut(1, 12, 30, 6) > 12, "flat at level: a degree up barely moves it");
        Check(hs::PitchCut(nan) == hs::LevelCm && hs::PitchCut(inf) == hs::LevelCm && hs::PitchCut(-inf) == hs::LevelCm, "a pitch not known: the level cut");
        Check(hs::PitchCut(30, 12, 30, 6, nan) == 12 && hs::PitchCut(30, 12, 30, 6, 0) == 12 && hs::PitchCut(30, 12, 30, 6, -5) == 12, "a bad span: the level cut");
        Check(hs::PitchCut(40, 0, 30, 6) == 0 && hs::PitchCut(40, -3, 30, 6) == 0 && hs::PitchCut(40, nan, 30, 6) == 0, "no level cut: none at any pitch");
        Check(hs::PitchCut(span, 12, nan, 6) == 0 && hs::PitchCut(span, 12, -4, 6) == 0 && hs::PitchCut(-span, 12, 30, inf) == 0, "a bad end counts as none");
        bool upRises = true, downFalls = true;
        float prevUp = hs::PitchCut(0), prevDown = prevUp;
        for (float p = .25f; p <= 90; p += .25f) {
            const float u = hs::PitchCut(p), d = hs::PitchCut(-p);
            upRises = upRises && u >= prevUp; downFalls = downFalls && d <= prevDown;
            prevUp = u; prevDown = d;
            Check(u >= hs::LevelCm && u <= hs::UpCm && d <= hs::LevelCm && d >= hs::DownCm, "always between the level and the end");
        }
        Check(upRises && downFalls, "monotonic from level to each end");
    }
    if (g_fail) { std::printf("head shadow: %d failed\n", g_fail); return 1; }
    std::puts("head shadow: ok");
    return 0;
}
