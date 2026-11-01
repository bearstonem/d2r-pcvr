// Head shadow (steamvr/head_shadow.h): the pose and eye handshake (when the head is kept, the eye's near clip, the
// hold), the state line, the cut by head pitch, the head bones' reach.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include "head_shadow.h"

namespace hs = framevr::headshadow;
static int g_fail = 0;
static void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; } }
static bool Near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }

int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (int w = 0; w <= int(hs::Why::NoCut); ++w) Check(hs::WhyName(hs::Why(w))[0] != '?', "every reason has a name");
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
    {   // the pitch moves the cut down: the eye's own lower cut wins over the hold (the hold only covers a missing raise)
        hs::Handshake h;
        h.Eye(base, true, hs::UpCm, upm, 1000);
        h.Pose(false, 1005);
        const auto e = h.Eye(base, true, hs::DownCm, upm, 1016);
        Check(e.atCut && !e.held && Near(e.nearZ, std::max(base, hs::RaisedNear(base, hs::DownCm, upm))), "looking down: the down cut, not the held up cut");
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
        Check(hs::LevelCm == 11 && hs::UpCm == 13 && hs::DownCm == 3, "the headset-tuned cuts: 11 cm level, 13 up, 3 down");
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
