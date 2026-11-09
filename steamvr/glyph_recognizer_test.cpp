#include "glyph_recognizer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
using namespace framevr::glyphs;
static int failures = 0;
static void Check(bool ok, const std::string& what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; } }

// A shape as a hand might draw it: size (m), turned (radians), stretched sideways, jittered,
// drawn backwards, mirrored (the other way round: a bolt zigzagging the other way, a wave starting
// down, a spiral clockwise), started part way round (closed shapes), and sampled unevenly (fast in
// the middle).
struct Style { float size = .35f, turn = 0, stretch = 1, jitter = 0, startAt = 0; bool backwards = false, mirrored = false, uneven = false; };
static Cloud Drawn(Shape s, const Style& st, std::mt19937& rng) {
    Cloud t = Template(s);
    if (st.mirrored) for (auto& p : t) p.x = 1 - p.x;
    if (st.startAt > 0 && (s == Shape::Circle || s == Shape::Square || s == Shape::Triangle || s == Shape::Star)) {
        const size_t k = size_t(st.startAt * float(t.size() - 1));
        Cloud r(t.begin() + ptrdiff_t(k), t.end() - 1);
        r.insert(r.end(), t.begin(), t.begin() + ptrdiff_t(k) + 1);
        t = r;
    }
    if (st.backwards) std::reverse(t.begin(), t.end());
    // A hand wobbles smoothly, not sample by sample: two slow swings on each axis (jitter: their
    // amplitude, a share of the size) and a little tremor (a tenth of it).
    std::uniform_real_distribution<float> phase(0, 6.2831853f);
    std::normal_distribution<float> tremor(0, st.jitter * .1f);
    const float px1 = phase(rng), px2 = phase(rng), py1 = phase(rng), py2 = phase(rng);
    Cloud out;
    const float c = std::cos(st.turn), s2 = std::sin(st.turn);
    for (size_t i = 0; i < t.size(); ++i) {
        if (st.uneven && i % 3 == 1 && i > t.size() / 4 && i < 3 * t.size() / 4) continue;   // fewer samples while fast
        const float u = float(i) / float(t.size());
        const float wx = st.jitter * (std::sin(u * 7 + px1) + .5f * std::sin(u * 17 + px2)) + tremor(rng);
        const float wy = st.jitter * (std::sin(u * 9 + py1) + .5f * std::sin(u * 19 + py2)) + tremor(rng);
        const float x = (t[i].x - .5f) * st.size * st.stretch, y = (t[i].y - .5f) * st.size;
        out.push_back({c * x - s2 * y + wx * st.size, s2 * x + c * y + wy * st.size});
    }
    return out;
}

int main() {
#ifndef GLYPH_SEED
#define GLYPH_SEED 7
#endif
    std::mt19937 rng(GLYPH_SEED);
    const Style styles[] = {
        {}, {.size = .25f}, {.size = .6f}, {.turn = .2f}, {.turn = -.2f}, {.stretch = .75f}, {.stretch = 1.3f},
        {.jitter = .02f}, {.jitter = .035f}, {.startAt = .3f}, {.startAt = .7f}, {.backwards = true}, {.uneven = true},
        {.size = .3f, .turn = .15f, .stretch = 1.15f, .jitter = .03f, .startAt = .5f, .backwards = true, .uneven = true},
        {.mirrored = true}, {.backwards = true, .mirrored = true}, {.turn = .3f, .stretch = .75f, .startAt = .4f, .mirrored = true},
        {.size = .45f, .turn = -.3f, .stretch = 1.35f, .jitter = .03f, .startAt = .15f, .backwards = true, .mirrored = true, .uneven = true},
    };
    int total = 0, right = 0;
    int confusion[ShapeCount][ShapeCount + 1] = {};
    for (int si = 0; si < ShapeCount; ++si)
        for (const auto& st : styles)
            for (int rep = 0; rep < 3; ++rep) {
                const auto r = Recognize(Drawn(Shape(si), st, rng), 1.2f);
                ++total;
                confusion[si][r.shape == Shape::None ? ShapeCount : int(r.shape)]++;
                if (r.shape == Shape(si)) ++right;
                char detail[160];
                std::snprintf(detail, sizeof detail, " (got %s: %d corners, score %.2f, next %.2f; style size %.2f turn %.2f stretch %.2f wobble %.3f start %.1f%s%s)",
                              Name(r.shape), r.corners, r.score, r.second, st.size, st.turn, st.stretch, st.jitter, st.startAt, st.backwards ? " backwards" : "", st.uneven ? " uneven" : "");
                Check(r.shape == Shape(si) || r.shape == Shape::None, std::string("a drawn ") + Name(Shape(si)) + " is never another glyph" + detail);
            }
    // Every shape must be recognised from nearly all of its drawings; a few refusals are acceptable, wrong casts are not.
    for (int si = 0; si < ShapeCount; ++si) {
        int ok = confusion[si][si], n = 0;
        for (int j = 0; j <= ShapeCount; ++j) n += confusion[si][j];
        Check(ok * 10 >= n * 9, std::string(Name(Shape(si))) + " recognised in at least 90% of drawings (" + std::to_string(ok) + "/" + std::to_string(n) + ")");
    }
    std::printf("glyphs: %d of %d drawings recognised as drawn\n", right, total);

    // Every way round: each shape in all four directions (as drawn, backwards, mirrored, both), started
    // anywhere, turned up to .3 rad, as wide as .75 to 1.35 of its height and wobbling: nearly always
    // recognised, never another glyph.
    {
        std::uniform_real_distribution<float> u01(0, 1), turn(-.3f, .3f), stretch(.75f, 1.35f), size(.22f, .6f), wobble(0, .035f);
        for (int si = 0; si < ShapeCount; ++si)
            for (int way = 0; way < 4; ++way) {
                int ok = 0, wrong = 0;
                const int n = 40;
                for (int k = 0; k < n; ++k) {
                    Style st; st.backwards = way & 1; st.mirrored = way & 2;
                    st.size = size(rng); st.turn = turn(rng); st.stretch = stretch(rng); st.jitter = wobble(rng); st.startAt = u01(rng);
                    const auto r = Recognize(Drawn(Shape(si), st, rng), 1.2f);
                    ok += r.shape == Shape(si); wrong += r.shape != Shape(si) && r.shape != Shape::None;
                }
                const std::string what = std::string(Name(Shape(si))) + (way & 2 ? " mirrored" : "") + (way & 1 ? " backwards" : "");
                Check(ok * 20 >= n * 19, what + " recognised in at least 95% of drawings (" + std::to_string(ok) + "/" + std::to_string(n) + ")");
                Check(wrong == 0, what + " is never another glyph (" + std::to_string(wrong) + ")");
            }
    }
    // A wave of three swells is still a wave.
    {
        Cloud wave;
        for (int i = 0; i <= 96; ++i) { const float x = float(i) / 96; wave.push_back({x * .45f, .07f * std::sin(x * 3 * 6.2831853f)}); }
        const auto r = Recognize(wave, 1.5f);
        Check(r.shape == Shape::Wave, std::string("three swells are a wave (got ") + Name(r.shape) + ", " + std::to_string(r.corners) + " corners)");
    }

    // Not glyphs.
    {
        Style st; st.size = .08f;
        Check(Recognize(Drawn(Shape::Circle, st, rng), 1).why == std::string("too small"), "a small circle is too small");
    }
    {
        Cloud line{{0, 0}, {.05f, .05f}, {.1f, .13f}};
        Check(Recognize(line, .3f).shape == Shape::None, "a flick is no glyph");
    }
    Check(Recognize(Drawn(Shape::Circle, {}, rng), 5).why == std::string("held too long"), "held over 4 s is no glyph");
    {
        // A straight line across: no shape is close.
        Cloud line;
        for (int i = 0; i <= 30; ++i) line.push_back({float(i) * .012f, float(i) * .002f});
        const auto r = Recognize(line, 1);
        Check(r.shape == Shape::None, std::string("a straight line is no glyph (got ") + Name(r.shape) + ")");
    }
    {
        // Random scribbles: at most an occasional match.
        int cast = 0;
        std::uniform_real_distribution<float> u(-.2f, .2f);
        for (int k = 0; k < 200; ++k) {
            Cloud scribble;
            for (int i = 0; i < 12; ++i) scribble.push_back({u(rng), u(rng)});
            if (Recognize(scribble, 1.5f).shape != Shape::None) ++cast;
        }
        std::printf("glyphs: %d of 200 random scribbles cast\n", cast);
        Check(cast <= 20, "random scribbles rarely cast (" + std::to_string(cast) + "/200)");
    }
    // Up stays up: an upside-down V is not a V.
    {
        Style st; st.turn = 3.14159265f;
        const auto r = Recognize(Drawn(Shape::V, st, rng), 1);
        Check(r.shape != Shape::V, std::string("an upside-down V is not a V (got ") + Name(r.shape) + ")");
    }
    // Flatten: a shape drawn in the room on a plane turned to the side, tilted back or lying flat keeps
    // its proportions; up stays the room's up (away from the player on a flat one).
    {
        const auto inRoom = [](const Cloud& shape, float yaw, float pitch, float facing) {
            // The plane turned yaw about up, then tilted back pitch; the player facing 'facing' (0: -z).
            const float rx = std::cos(yaw), rz = -std::sin(yaw);
            const float ux = -std::sin(pitch) * std::sin(yaw), uy = std::cos(pitch), uz = -std::sin(pitch) * std::cos(yaw);
            std::vector<Spot> path;
            for (size_t i = 0; i < shape.size(); ++i) {
                const float a = (shape[i].x - .5f) * .35f, b = (shape[i].y - .5f) * .35f;
                const float x = a * rx + b * ux, y = 1.3f + b * uy, z = -.45f + a * rz + b * uz + .5f * a * a / .6f;   // the arm's arc
                const float c = std::cos(facing), s = std::sin(facing);
                path.push_back({c * x + s * z, y, -s * x + c * z});
            }
            return path;
        };
        const float facing = .8f, rightX = std::cos(facing), rightZ = -std::sin(facing);
        struct Case { float yaw, pitch; const char* what; };
        for (const Case& c : {Case{0, 0, "facing"}, Case{1.05f, 0, "turned 60 degrees"}, Case{-1.05f, 0, "turned 60 degrees the other way"},
                              Case{0, 1.05f, "tilted back 60 degrees"}, Case{.6f, .7f, "turned and tilted"}, Case{0, 1.5f, "lying nearly flat"}})
            for (Shape s : {Shape::Circle, Shape::Square, Shape::V, Shape::Lightning, Shape::Wave}) {
                const auto flat = Flatten(inRoom(Template(s), c.yaw, c.pitch, facing), rightX, rightZ);
                const auto r = Recognize(flat.stroke, 1.2f);
                Check(flat.own && r.shape == s, std::string("a ") + Name(s) + " drawn " + c.what + " (got " + Name(r.shape) + (flat.own ? "" : ", no plane") + ")");
            }
        {
            const auto flat = Flatten(inRoom(Template(Shape::Circle), 1.05f, 0, facing), rightX, rightZ);
            Check(std::fabs(flat.degrees - 60) < 3, "the plane's turn from facing: " + std::to_string(flat.degrees));
        }
        {   // an upside-down V on a tilted plane is still not a V
            Cloud caret = Template(Shape::V);
            for (auto& p : caret) p.y = 1 - p.y;
            const auto r = Recognize(Flatten(inRoom(caret, .3f, .6f, facing), rightX, rightZ).stroke, 1);
            Check(r.shape != Shape::V, std::string("an upside-down V on a tilted plane is not a V (got ") + Name(r.shape) + ")");
        }
        {   // a path as deep as it is tall has no plane of its own: it is seen as facing the player
            std::vector<Spot> deep;
            for (int i = 0; i <= 64; ++i) { const float a = 6.2831853f * float(i) / 64; deep.push_back({.17f * std::cos(a), 1.3f + .17f * std::sin(a), -.45f + .17f * std::sin(2 * a)}); }
            const auto flat = Flatten(deep, 1, 0);
            Check(!flat.own && flat.stroke.size() == deep.size(), "a deep path falls back to the plane facing the player");
        }
    }
    // The plane: a hand moving to the player's right and up, facing a turned direction.
    {
        const float yaw = 1.0f;   // facing turned
        const auto plane = Plane::Facing(0, 1.5f, 0, yaw);
        const auto p = plane.Project(std::cos(yaw) * .3f, 1.7f, -std::sin(yaw) * .3f);
        Check(std::fabs(p.x - .3f) < 1e-4f && std::fabs(p.y - .2f) < 1e-4f, "the plane: right and up of the start");
    }
    if (failures) { std::fprintf(stderr, "%d failures\n", failures); return 1; }
    std::puts("glyph-recognizer-test: ok");
    return 0;
}
