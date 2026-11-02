#pragma once
// Glyph casting's trail (glyph_recognizer.h): the line the hand draws, hanging in the air where it was
// drawn. While drawing it glows pale; recognised, it flares in the colour of the skill's element (fire
// orange, cold blue, lightning white, poison green, holy gold, magic violet, warcries red); refused, it
// greys. Then it fades (FadeSeconds).
//
// The transport paints it into the shared atlas (panelatlas::Glyph, Size x Size, straight alpha) and
// shows it as a quad in the drawing plane, Side metres across, centred on where the stroke began.
// Value-only apart from the worker: steamvr/glyph_trail_test.cpp.
#include "glyph_recognizer.h"
#include "latest_worker.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace framevr::glyphtrail {

constexpr int Size = 512;              // pixels a side
constexpr float Side = 1.0f;           // metres a side: the stroke begins in the middle
constexpr float CorePx = 3.5f;         // the bright line's half width
constexpr float GlowPx = 16.0f;        // the glow round it
constexpr float FadeSeconds = 2.0f;    // a cast glyph fades over this
constexpr float RefusedFadeSeconds = .8f;

struct Colour { float r = 1, g = 1, b = 1; bool operator==(const Colour&) const = default; };
constexpr Colour Drawing{.78f, .86f, 1.0f};   // pale arcane while drawing and for skills of no element
constexpr Colour Refused{.55f, .55f, .55f};
constexpr Colour Fire{1.0f, .55f, .15f}, Cold{.45f, .78f, 1.0f}, Lightning{.96f, .96f, 1.0f}, Poison{.45f, 1.0f, .35f},
                 Holy{1.0f, .84f, .38f}, Magic{.78f, .55f, 1.0f}, War{1.0f, .38f, .26f}, Nature{.55f, .9f, .35f}, Bone{.95f, .93f, .85f};

// The colour of a skill's element, by its game id (Skills.txt rows); a skill of none, pale arcane.
inline Colour ElementColour(int skill) {
    switch (skill) {
    // Fire: Enchant, Fire Golem, Holy Fire, Resist Fire, Armageddon
    case 52: case 94: case 102: case 100: case 249: return Fire;
    // Cold: Frozen / Shiver / Chilling Armor, Frost Nova, Holy Freeze, Resist Cold, Cyclone Armor, Hurricane
    case 40: case 50: case 60: case 44: case 114: case 105: case 235: case 250: return Cold;
    // Lightning: Static Field, Nova, Thunder Storm, Holy Shock, Resist Lightning
    case 42: case 48: case 57: case 118: case 110: return Lightning;
    // Poison: Poison Nova, Poison Creeper, Venom
    case 92: case 222: case 278: return Poison;
    // Magic: Energy Shield, Inner Sight, Valkyrie, Decoy, Burst of Speed, Fade, Cloak of Shadows, Shadow Warrior / Master
    case 58: case 8: case 32: case 28: case 258: case 267: case 264: case 268: case 279: return Magic;
    // Warcries: Howl, Shout, Battle Cry, Battle Orders, Grim Ward, War Cry, Battle Command
    case 130: case 138: case 146: case 149: case 150: case 154: case 155: return War;
    // Nature: Oak Sage, Heart of Wolverine, Spirit of Barbs, Carrion / Solar Creeper, the druid's beasts
    case 226: case 236: case 246: case 231: case 241: case 221: case 227: case 237: case 247: return Nature;
    // Bone: Bone Armor, the golems and skeletons
    case 68: case 70: case 75: case 80: case 85: case 90: case 95: return Bone;
    default: break;
    }
    // The Paladin's other auras and Holy Shield: holy gold.
    if (skill == 117 || (skill >= 98 && skill <= 125)) return Holy;
    return Drawing;
}

// The stroke in pixels: x right, y down, the stroke's first point in the middle.
inline glyphs::Point Pixel(glyphs::Point metres) {
    return {(metres.x / Side + .5f) * float(Size), (.5f - metres.y / Side) * float(Size)};
}

struct Job {
    std::vector<glyphs::Point> stroke;   // metres on the drawing plane, relative to its first point
    uint64_t strokeId = 0;               // which stroke: a new one starts the coverage over
    Colour colour = Drawing;
    float alpha = 1;                     // 0..1: the fade
    float flare = 0;                     // 0..1: a recognised glyph's flash, whiter and brighter
    uint64_t key = 0;
};

// The line's coverage, kept between paints: each pixel's core (the bright line) and glow, the
// largest over the segments. While drawing only the new segments are added; a fade only re-shades.
struct Coverage {
    uint64_t strokeId = 0;
    size_t points = 0;   // how many of the stroke's points are in
    std::vector<float> core, glow;
    void Add(const std::vector<glyphs::Point>& stroke, uint64_t id) {
        if (core.empty() || id != strokeId || stroke.size() < points) {
            core.assign(size_t(Size) * Size, 0); glow.assign(size_t(Size) * Size, 0);
            strokeId = id; points = 0;
        }
        const float reach = GlowPx * 2.2f;
        for (size_t s = std::max<size_t>(points, 1); s < stroke.size(); ++s) {
            const auto a = Pixel(stroke[s - 1]), b = Pixel(stroke[s]);
            const int x0 = std::max(0, int(std::floor(std::min(a.x, b.x) - reach))), x1 = std::min(Size - 1, int(std::ceil(std::max(a.x, b.x) + reach)));
            const int y0 = std::max(0, int(std::floor(std::min(a.y, b.y) - reach))), y1 = std::min(Size - 1, int(std::ceil(std::max(a.y, b.y) + reach)));
            const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float px = float(x) + .5f - a.x, py = float(y) + .5f - a.y;
                    const float t = len2 > 1e-6f ? std::clamp((px * dx + py * dy) / len2, 0.0f, 1.0f) : 0.0f;
                    const float ex = px - t * dx, ey = py - t * dy, d2 = ex * ex + ey * ey;
                    if (d2 > reach * reach) continue;
                    const size_t i = size_t(y) * Size + size_t(x);
                    const float d = std::sqrt(d2);
                    core[i] = std::max(core[i], std::clamp((CorePx + 1 - d) / 2, 0.0f, 1.0f));
                    glow[i] = std::max(glow[i], std::exp(-d2 / (GlowPx * GlowPx)));
                }
        }
        points = std::max(points, stroke.size());
    }
};

// Straight-alpha RGBA, Size x Size, from the coverage.
inline std::vector<uint8_t> Shade(const Coverage& coverage, Colour colour, float alpha, float flare) {
    std::vector<uint8_t> rgba(size_t(Size) * Size * 4, 0);
    flare = std::clamp(flare, 0.0f, 1.0f); alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (!(alpha > 0) || coverage.core.size() != size_t(Size) * Size) return rgba;
    for (size_t i = 0; i < coverage.core.size(); ++i) {
        const float c = coverage.core[i], g = coverage.glow[i] * (.6f + .3f * flare);
        const float a = std::max(c, g) * alpha;
        if (a <= 1.0f / 255) continue;
        // The core runs whiter than its glow, more so in the flash.
        const float white = c * (.45f + .45f * flare);
        auto channel = [&](float base) { return std::clamp(base * (1 - white) + white, 0.0f, 1.0f); };
        uint8_t* p = &rgba[i * 4];
        p[0] = uint8_t(std::lround(channel(colour.r) * 255));
        p[1] = uint8_t(std::lround(channel(colour.g) * 255));
        p[2] = uint8_t(std::lround(channel(colour.b) * 255));
        p[3] = uint8_t(std::lround(std::min(1.0f, a) * 255));
    }
    return rgba;
}
inline std::vector<uint8_t> Paint(const Job& job) {
    Coverage coverage;
    if (job.stroke.size() >= 2) coverage.Add(job.stroke, job.strokeId);
    return Shade(coverage, job.colour, job.alpha, job.flare);
}

struct Image { uint64_t key = 0, strokeId = 0; std::vector<uint8_t> pixels; };

// On a worker thread: the latest job only (latest_worker.h), as the other atlas painters. The worker
// keeps the coverage of the stroke it last painted.
class Painter {
    LatestWorker<Job, Image> worker;
    uint64_t request = 0;
public:
    using Work = std::function<Image(const Job&)>;
    static Work Incremental() {
        auto coverage = std::make_shared<Coverage>();
        return [coverage](const Job& job) {
            if (job.stroke.size() >= 2) coverage->Add(job.stroke, job.strokeId);
            else *coverage = {};
            return Image{job.key, job.strokeId, Shade(*coverage, job.colour, job.alpha, job.flare)};
        };
    }
    explicit Painter(Work work = Incremental()) : worker(std::move(work)) {}
    bool Start() { return worker.Start(); }
    bool Busy() const { return request != 0; }
    bool Submit(Job job) {
        if (request) return false;
        request = worker.Submit(std::move(job));
        return request != 0;
    }
    bool Take(Image& image) {
        if (!request || !worker.Take(request, image)) return false;
        request = 0;
        return true;
    }
    void Discard() { request = 0; }
    void Stop() { worker.Stop(); Discard(); }
};

}  // namespace framevr::glyphtrail
