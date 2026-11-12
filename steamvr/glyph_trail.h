#pragma once
// Glyph casting's trail (glyph_recognizer.h): the line the hand draws, hanging in the air where it was
// drawn. While drawing it glows pale with a few glints; recognised, it takes the look of the skill's
// element (owner 2026-11-01, "more elemental", for every class): fire licks up off it and throws
// embers, cold grows ice shards that break off, lightning crackles along it in jumping arcs, poison
// drips and bubbles, holy light rises in rays and glints, magic sends motes along it, warcries ring
// out in echoes, nature lets spores drift, bone smokes and sheds chips, a curse sinks in crimson and
// closes in on itself, shadow smokes away, steel spins blades along it in sparks. Then each leaves its
// own way (fire burns away along a glowing edge, cold cracks and breaks up, lightning flickers out, the
// rest dissolve); refused, it greys and crumbles to ash.
//
// The transport paints it into the shared atlas (panelatlas::Glyph, Size x Size, straight alpha) and
// shows it as a quad in the drawing plane, Side metres across, centred on where the stroke began.
// Everything is drawn as light (summed, then turned to straight alpha), from the stroke, its id and
// the time alone: the same job paints the same picture.
// Value-only apart from the worker: steamvr/glyph_trail_test.cpp.
#include "atlas_dirty.h"
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
constexpr float FadeSeconds = 2.2f;    // a cast glyph's effect, its exit included
constexpr float RefusedFadeSeconds = .8f;
constexpr int Margin = 110;            // pixels round the stroke its effects reach (flames, embers, drops)

struct Colour { float r = 1, g = 1, b = 1; bool operator==(const Colour&) const = default; };
constexpr Colour Drawing{.78f, .86f, 1.0f};   // pale arcane while drawing and for skills of no element
constexpr Colour Refused{.55f, .55f, .55f};
constexpr Colour Fire{1.0f, .55f, .15f}, Cold{.45f, .78f, 1.0f}, Lightning{.96f, .96f, 1.0f}, Poison{.45f, 1.0f, .35f},
                 Holy{1.0f, .84f, .38f}, Magic{.78f, .55f, 1.0f}, War{1.0f, .38f, .26f}, Nature{.55f, .9f, .35f}, Bone{.95f, .93f, .85f},
                 Curse{.85f, .08f, .2f}, Shadow{.5f, .36f, .88f}, Steel{.78f, .84f, .92f};

// The colour of a skill's element, by its game id (Skills.txt rows, every class); a skill of none,
// pale arcane. Only skills that need no target are put on glyphs; the rest are here for completeness.
inline Colour ElementColour(int skill) {
    switch (skill) {
    // Fire: the Amazon's fire arrows; Fire Bolt, Inferno, Blaze, Fire Ball, Fire Wall, Enchant, Meteor,
    // Hydra; Fire Golem; Resist Fire, Holy Fire; Firestorm, Molten Boulder, Fissure, Volcano,
    // Armageddon; Fire Blast, Fists of Fire, Wake of Fire / Inferno
    case 7: case 16: case 27: case 36: case 41: case 46: case 47: case 51: case 52: case 56: case 62: case 94: case 100: case 102:
    case 225: case 229: case 234: case 244: case 249: case 251: case 259: case 262: case 272: return Fire;
    // Cold: the Amazon's cold arrows; Ice Bolt, Frozen / Shiver / Chilling Armor, Frost Nova, Ice Blast,
    // Glacial Spike, Blizzard, Frozen Orb; Resist Cold, Holy Freeze; Arctic Blast, Cyclone Armor,
    // Hurricane; Blades of Ice
    case 11: case 21: case 31: case 39: case 40: case 44: case 45: case 50: case 55: case 59: case 60: case 64: case 105: case 114:
    case 230: case 235: case 250: case 274: return Cold;
    // Lightning: the Amazon's lightning javelins; Charged Bolt, Static Field, Nova, Lightning, Chain
    // Lightning, Thunder Storm; Resist Lightning, Holy Shock; Shock Web, Charged Bolt / Lightning
    // Sentry, Claws of Thunder
    case 20: case 24: case 34: case 35: case 38: case 42: case 48: case 49: case 53: case 57: case 110: case 118:
    case 256: case 261: case 269: case 271: return Lightning;
    // Poison: Poison / Plague Javelin; Poison Dagger, Poison Explosion, Poison Nova; Poison Creeper; Venom
    case 15: case 25: case 73: case 83: case 92: case 222: case 278: return Poison;
    // Magic: Magic Arrow, Inner Sight, Slow Missiles, Decoy, Valkyrie; Telekinesis, Teleport, Energy
    // Shield; Meditation, Conviction; Psychic Hammer; Town Portal (the scroll, the tome, the shortcut)
    case 6: case 8: case 17: case 28: case 32: case 43: case 54: case 58: case 120: case 123: case 253:
    case 219: case 220: case 359: return Magic;
    // Warcries and battle: Might, Thorns; Howl, Find Potion, Taunt, Shout, Find Item, Battle Cry, Battle
    // Orders, Grim Ward, War Cry, Battle Command
    case 98: case 103: case 130: case 131: case 137: case 138: case 142: case 146: case 149: case 150: case 154: case 155: return War;
    // Nature: Cleansing; Raven, Werewolf, Oak Sage, Spirit Wolf, Werebear, Carrion Vine, Heart of
    // Wolverine, Dire Wolf, Solar Creeper, Spirit of Barbs, Grizzly
    case 109: case 221: case 223: case 226: case 227: case 228: case 231: case 236: case 237: case 241: case 246: case 247: return Nature;
    // Bone: Teeth, Bone Armor, the skeletons, Clay Golem, Bone Wall / Spear / Prison / Spirit, Revive
    case 67: case 68: case 70: case 75: case 78: case 80: case 84: case 88: case 93: case 95: return Bone;
    // Curses and blood: Amplify Damage, Dim Vision, Weaken, Corpse Explosion, Iron Maiden, Terror,
    // Confuse, Life Tap, Blood Golem, Attract, Decrepify, Lower Resist; Death Sentry
    case 66: case 71: case 72: case 74: case 76: case 77: case 81: case 82: case 85: case 86: case 87: case 91: case 276: return Curse;
    // Shadow: Burst of Speed, Cloak of Shadows, Fade, Shadow Warrior, Mind Blast, Shadow Master
    case 258: case 264: case 267: case 268: case 273: case 279: return Shadow;
    // Steel: Iron Golem; Blade Sentinel, Blade Fury, Blade Shield
    case 90: case 257: case 266: case 277: return Steel;
    default: break;
    }
    // The Paladin's other skills (Prayer, Defiance, Blessed Aim, Concentration, Vigor, Holy Shield,
    // Sanctuary, Fanaticism, Redemption, Salvation...): holy gold.
    if (skill >= 96 && skill <= 125) return Holy;
    // The Warlock (D2R's own class), by tree (skill_categories.h): Demon summons and binds hellspawn,
    // demonic crimson; Eldritch hexes and enchants weapons, eldritch violet; Chaos casts fire and magic
    // through sigils, fire. Per skill once headset play names them ("Frame glyph:" logs each cast's).
    if (skill >= 373 && skill <= 382) return Curse;
    if (skill >= 383 && skill <= 392) return Shadow;
    if (skill >= 393 && skill <= 402) return Fire;
    // Commander of the Dead virtual skills (summon_commander.h):
    if (skill == 3901 || skill == 3902) return Cold;
    if (skill == 3903 || skill == 3907) return Bone;
    if (skill == 3904) return War;
    if (skill == 3905) return Holy;
    if (skill == 3906) return Magic;
    return Drawing;
}

// The trail's looks: drawing, refused, and one per element.
enum class Look : uint8_t { Drawing, Refused, Fire, Cold, Lightning, Poison, Holy, Magic, War, Nature, Bone, Curse, Shadow, Steel };
inline Look LookOf(Colour c) {
    if (c == Fire) return Look::Fire;
    if (c == Cold) return Look::Cold;
    if (c == Lightning) return Look::Lightning;
    if (c == Poison) return Look::Poison;
    if (c == Holy) return Look::Holy;
    if (c == Magic) return Look::Magic;
    if (c == War) return Look::War;
    if (c == Nature) return Look::Nature;
    if (c == Bone) return Look::Bone;
    if (c == Curse) return Look::Curse;
    if (c == Shadow) return Look::Shadow;
    if (c == Steel) return Look::Steel;
    if (c == Refused) return Look::Refused;
    return Look::Drawing;
}
inline Look ElementLook(int skill) { return LookOf(ElementColour(skill)); }
// How long a look shows after the stroke ends (Drawing: while drawing).
inline float Duration(Look look) { return look == Look::Refused ? RefusedFadeSeconds : FadeSeconds; }

// Three colours a look is painted in: its deep tint, its body and its hottest light.
struct Palette { Colour deep, mid, hot; };
inline Palette PaletteOf(Look look) {
    switch (look) {
    case Look::Fire: return {{.75f, .12f, .02f}, Fire, {1.0f, .93f, .62f}};
    case Look::Cold: return {{.12f, .35f, .85f}, Cold, {.88f, .97f, 1.0f}};
    case Look::Lightning: return {{.38f, .32f, 1.0f}, {.62f, .72f, 1.0f}, {.97f, .97f, 1.0f}};
    case Look::Poison: return {{.12f, .5f, .08f}, Poison, {.85f, 1.0f, .55f}};
    case Look::Holy: return {{.85f, .55f, .12f}, Holy, {1.0f, .97f, .82f}};
    case Look::Magic: return {{.42f, .16f, .85f}, Magic, {.95f, .85f, 1.0f}};
    case Look::War: return {{.6f, .05f, .03f}, War, {1.0f, .8f, .62f}};
    case Look::Nature: return {{.15f, .45f, .1f}, Nature, {.9f, 1.0f, .65f}};
    case Look::Bone: return {{.35f, .45f, .38f}, {.78f, .82f, .74f}, Bone};
    case Look::Curse: return {{.28f, .0f, .1f}, Curse, {1.0f, .5f, .45f}};
    case Look::Shadow: return {{.1f, .05f, .22f}, Shadow, {.88f, .84f, 1.0f}};
    case Look::Steel: return {{.25f, .3f, .38f}, Steel, {1.0f, 1.0f, 1.0f}};
    case Look::Refused: return {{.22f, .22f, .22f}, Refused, {.75f, .75f, .75f}};
    default: return {{.3f, .4f, .8f}, Drawing, {.93f, .96f, 1.0f}};
    }
}

// The stroke in pixels: x right, y down, the stroke's first point in the middle.
inline glyphs::Point Pixel(glyphs::Point metres) {
    return {(metres.x / Side + .5f) * float(Size), (.5f - metres.y / Side) * float(Size)};
}

struct Job {
    std::vector<glyphs::Point> stroke;   // metres on the drawing plane, relative to its first point
    uint64_t strokeId = 0;               // which stroke: a new one starts the coverage over
    Look look = Look::Drawing;
    float t = 0;                         // seconds in this look: since the stroke began (Drawing), else since it ended
    float alpha = 1;                     // 0..1: an overall fade
    uint64_t key = 0;
};

// ---- random numbers and noise, from integers only (the same job paints the same picture) ----
inline uint32_t Hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
inline float Random(uint32_t seed, uint32_t a, uint32_t b = 0) {
    return float(Hash(seed ^ Hash(a * 0x9E3779B1u ^ Hash(b + 0x632BE5ABu)))) * (1.0f / 4294967296.0f);
}
inline float Smooth(float t) { return t * t * (3 - 2 * t); }
inline float SmoothStep(float a, float b, float x) { return Smooth(std::clamp((x - a) / (b - a), 0.0f, 1.0f)); }
// Value noise, 0..1, one cell per unit.
inline float Noise(float x, float y, uint32_t seed) {
    const float fx = std::floor(x), fy = std::floor(y);
    const int xi = int(fx), yi = int(fy);
    const float u = Smooth(x - fx), v = Smooth(y - fy);
    auto at = [&](int i, int j) { return Random(seed, uint32_t(xi + i), uint32_t(yi + j)); };
    const float top = at(0, 0) + (at(1, 0) - at(0, 0)) * u, bottom = at(0, 1) + (at(1, 1) - at(0, 1)) * u;
    return top + (bottom - top) * v;
}
inline float Fbm(float x, float y, uint32_t seed) {
    return .55f * Noise(x, y, seed) + .3f * Noise(2.03f * x, 2.03f * y, seed + 17) + .15f * Noise(4.1f * x, 4.1f * y, seed + 31);
}
// Cells round jittered points `size` pixels apart (ice breaking up): the nearest point's distance,
// the next one's, and the nearest cell's id.
struct Cell { float f1 = 1e9f, f2 = 1e9f; uint32_t id = 0; };
inline Cell Cellular(float x, float y, float size, uint32_t seed) {
    const float gx = x / size, gy = y / size;
    const int cx = int(std::floor(gx)), cy = int(std::floor(gy));
    Cell c;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            const uint32_t ux = uint32_t(cx + i), uy = uint32_t(cy + j);
            const float fx = float(cx + i) + Random(seed, ux, uy), fy = float(cy + j) + Random(seed + 1, ux, uy);
            const float d = std::hypot(gx - fx, gy - fy) * size;
            if (d < c.f1) { c.f2 = c.f1; c.f1 = d; c.id = Hash(ux * 73856093u ^ uy * 19349663u); }
            else if (d < c.f2) c.f2 = d;
        }
    return c;
}

// The line's coverage, kept between paints: each pixel's core (the bright line), glow and distance to
// the line, the largest / nearest over the segments; and the stroke's box. While drawing only the new
// segments are added; a fade or an effect frame only re-shades.
struct Coverage {
    static constexpr float Reach = 48;   // pixels from the line the distance is kept to (the echoes)
    uint64_t strokeId = 0;
    size_t points = 0;   // how many of the stroke's points are in
    std::vector<float> core, glow, dist;
    int x0 = Size, y0 = Size, x1 = -1, y1 = -1;   // the stroke's box, pixels
    void Add(const std::vector<glyphs::Point>& stroke, uint64_t id) {
        if (core.empty() || id != strokeId || stroke.size() < points) {
            core.assign(size_t(Size) * Size, 0); glow.assign(size_t(Size) * Size, 0); dist.assign(size_t(Size) * Size, Reach);
            strokeId = id; points = 0; x0 = y0 = Size; x1 = y1 = -1;
        }
        for (size_t s = points; s < stroke.size(); ++s) {
            const auto p = Pixel(stroke[s]);
            x0 = std::min(x0, int(std::floor(p.x))); y0 = std::min(y0, int(std::floor(p.y)));
            x1 = std::max(x1, int(std::ceil(p.x))); y1 = std::max(y1, int(std::ceil(p.y)));
        }
        for (size_t s = std::max<size_t>(points, 1); s < stroke.size(); ++s) {
            const auto a = Pixel(stroke[s - 1]), b = Pixel(stroke[s]);
            const int bx0 = std::max(0, int(std::floor(std::min(a.x, b.x) - Reach))), bx1 = std::min(Size - 1, int(std::ceil(std::max(a.x, b.x) + Reach)));
            const int by0 = std::max(0, int(std::floor(std::min(a.y, b.y) - Reach))), by1 = std::min(Size - 1, int(std::ceil(std::max(a.y, b.y) + Reach)));
            const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
            for (int y = by0; y <= by1; ++y)
                for (int x = bx0; x <= bx1; ++x) {
                    const float px = float(x) + .5f - a.x, py = float(y) + .5f - a.y;
                    const float t = len2 > 1e-6f ? std::clamp((px * dx + py * dy) / len2, 0.0f, 1.0f) : 0.0f;
                    const float ex = px - t * dx, ey = py - t * dy, d2 = ex * ex + ey * ey;
                    if (d2 >= Reach * Reach) continue;
                    const size_t i = size_t(y) * Size + size_t(x);
                    const float d = std::sqrt(d2);
                    dist[i] = std::min(dist[i], d);
                    core[i] = std::max(core[i], std::clamp((CorePx + 1 - d) / 2, 0.0f, 1.0f));
                    glow[i] = std::max(glow[i], std::exp(-d2 / (GlowPx * GlowPx)));
                }
        }
        points = std::max(points, stroke.size());
    }
};

// ---- the picture: light summed into an RGB buffer over the stroke's box and its margin ----
namespace detail {
struct Light {
    int ox = 0, oy = 0, w = 0, h = 0;   // the region of the image it covers
    std::vector<float> rgb;
    void Add(int x, int y, Colour c, float v) {
        if (x < ox || y < oy || x >= ox + w || y >= oy + h || !(v > 0)) return;
        float* p = &rgb[(size_t(y - oy) * size_t(w) + size_t(x - ox)) * 3];
        p[0] += c.r * v; p[1] += c.g * v; p[2] += c.b * v;
    }
    // A soft round dot.
    void Dot(float cx, float cy, float radius, Colour c, float v) {
        if (!(v > 0) || !(radius > 0)) return;
        const int r = int(std::ceil(radius * 2.6f));
        for (int y = int(cy) - r; y <= int(cy) + r; ++y)
            for (int x = int(cx) - r; x <= int(cx) + r; ++x) {
                const float dx = float(x) + .5f - cx, dy = float(y) + .5f - cy;
                Add(x, y, c, v * std::exp(-(dx * dx + dy * dy) / (radius * radius)));
            }
    }
    // A streak from a to b, its half width going from wa to wb, with a glow round it.
    void Streak(glyphs::Point a, glyphs::Point b, float wa, float wb, Colour c, float v, float glowPx = 0, Colour glowColour = {}) {
        if (!(v > 0)) return;
        const float reach = std::max(std::max(wa, wb) + 1.5f, glowPx * 2.2f);
        const int bx0 = int(std::floor(std::min(a.x, b.x) - reach)), bx1 = int(std::ceil(std::max(a.x, b.x) + reach));
        const int by0 = int(std::floor(std::min(a.y, b.y) - reach)), by1 = int(std::ceil(std::max(a.y, b.y) + reach));
        const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
        for (int y = std::max(by0, oy); y <= std::min(by1, oy + h - 1); ++y)
            for (int x = std::max(bx0, ox); x <= std::min(bx1, ox + w - 1); ++x) {
                const float px = float(x) + .5f - a.x, py = float(y) + .5f - a.y;
                const float t = len2 > 1e-6f ? std::clamp((px * dx + py * dy) / len2, 0.0f, 1.0f) : 0.0f;
                const float ex = px - t * dx, ey = py - t * dy, d = std::sqrt(ex * ex + ey * ey);
                const float half = wa + (wb - wa) * t;
                Add(x, y, c, v * std::clamp(half + .5f - d, 0.0f, 1.0f));
                if (glowPx > 0) Add(x, y, glowColour, v * .45f * std::exp(-d * d / (glowPx * glowPx)));
            }
    }
    // A four-pointed glint.
    void Glint(float cx, float cy, float size, Colour c, float v) {
        if (!(v > 0) || !(size > 0)) return;
        Streak({cx - size, cy}, {cx, cy}, 0, .9f, c, v);
        Streak({cx, cy}, {cx + size, cy}, .9f, 0, c, v);
        Streak({cx, cy - size}, {cx, cy}, 0, .9f, c, v);
        Streak({cx, cy}, {cx, cy + size}, .9f, 0, c, v);
        Dot(cx, cy, size * .35f, c, v * .8f);
    }
};
// The stroke in pixels, evenly spaced, with each point's unit normal.
struct Path {
    std::vector<glyphs::Point> p, n;
    float length = 0;
    glyphs::Point At(float u, glyphs::Point* normal = nullptr) const {   // u: 0..1 along the stroke
        if (p.empty()) return {};
        const float f = std::clamp(u, 0.0f, 1.0f) * float(p.size() - 1);
        const size_t i = std::min(p.size() - 1, size_t(f));
        if (normal) *normal = n[i];
        if (i + 1 >= p.size()) return p.back();
        const float k = f - float(i);
        return {p[i].x + (p[i + 1].x - p[i].x) * k, p[i].y + (p[i + 1].y - p[i].y) * k};
    }
};
inline Path PathOf(const std::vector<glyphs::Point>& stroke, int count) {
    Path path;
    if (stroke.size() < 2) return path;
    glyphs::Cloud px;
    for (const auto& s : stroke) px.push_back(Pixel(s));
    for (size_t i = 1; i < px.size(); ++i) path.length += glyphs::Distance(px[i - 1], px[i]);
    if (path.length < 1) return path;
    path.p = glyphs::Resample(px, count);
    path.n.resize(path.p.size());
    for (size_t i = 0; i < path.p.size(); ++i) {
        const auto& a = path.p[i ? i - 1 : i];
        const auto& b = path.p[i + 1 < path.p.size() ? i + 1 : i];
        const float tx = b.x - a.x, ty = b.y - a.y, l = std::hypot(tx, ty);
        path.n[i] = l > 1e-4f ? glyphs::Point{-ty / l, tx / l} : glyphs::Point{0, -1};
    }
    return path;
}
inline Colour Mix(Colour a, Colour b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
// deep -> mid -> hot as v goes 0 -> 1.
inline Colour Ramp(const Palette& pal, float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v < .5f ? Mix(pal.deep, pal.mid, v * 2) : Mix(pal.mid, pal.hot, v * 2 - 1);
}
}  // namespace detail

// Straight-alpha RGBA, Size x Size: the coverage in the job's look at its time.
inline std::vector<uint8_t> Shade(const Coverage& coverage, const Job& job) {
    using namespace detail;
    std::vector<uint8_t> rgba(size_t(Size) * Size * 4, 0);
    const float alpha = std::clamp(job.alpha, 0.0f, 1.0f);
    if (!(alpha > 0) || coverage.core.size() != size_t(Size) * Size || coverage.x1 < coverage.x0) return rgba;
    const Look look = job.look;
    const Palette pal = PaletteOf(look);
    const float t = std::max(0.0f, job.t);
    const bool drawing = look == Look::Drawing, refused = look == Look::Refused;
    const float duration = Duration(look);
    if (!drawing && t >= duration) return rgba;
    const uint32_t seed = Hash(uint32_t(job.strokeId) * 7919u + uint32_t(look) * 104729u + 1);

    // The envelope: the effect grows in, a white flash on recognition, then the exit from `hold` on.
    const float ignite = drawing ? 1 : SmoothStep(0, .2f, t);
    const float flash = drawing || refused ? 0 : std::max(0.0f, 1 - t / .35f);
    const float hold = refused ? .08f : duration * .45f;
    const float exit = drawing ? 0 : std::clamp((t - hold) / (duration - hold), 0.0f, 1.0f);
    const float particlesLeft = 1 - SmoothStep(.55f, 1, exit);

    Light light;
    light.ox = std::max(0, coverage.x0 - Margin); light.oy = std::max(0, coverage.y0 - Margin);
    light.w = std::min(Size, coverage.x1 + Margin + 1) - light.ox; light.h = std::min(Size, coverage.y1 + Margin + 1) - light.oy;
    if (light.w <= 0 || light.h <= 0) return rgba;
    light.rgb.assign(size_t(light.w) * size_t(light.h) * 3, 0);
    auto at = [&](int x, int y) { return size_t(y) * Size + size_t(x); };

    // 1. The line and its glow, and the exit: the line goes in its element's way while the glow fades
    // evenly. Fire burns away along a glowing edge; cold cracks, then breaks up in pieces; lightning
    // flickers out; refused crumbles; the rest dissolve. Lightning's own line is dimmer (its arcs carry
    // it); war's beats twice, as a shout.
    const uint32_t frame = uint32_t(t * 24);   // lightning's crackle
    const float lineGain = look == Look::Lightning ? .3f : 1.0f;
    const float glowGain = (look == Look::Holy ? .85f : .55f) + .6f * flash;
    const Colour hot = Mix(pal.hot, Colour{1, 1, 1}, .6f * flash);
    const float beat = look == Look::War ? 1 + .45f * std::exp(-std::pow((t - .15f) / .07f, 2.0f)) + .35f * std::exp(-std::pow((t - .45f) / .07f, 2.0f)) : 1;
    const float glowLeft = std::pow(1 - exit, 1.5f);
    const float flickerLeft = look == Look::Lightning && exit > 0 ? (1 - exit) * (Random(seed, frame, 999) > exit * .85f ? 1.0f : .15f) : 1.0f;
    const float cracksIn = look == Look::Cold && !drawing ? SmoothStep(hold - .3f, hold, t) : 0;
    const bool burns = look == Look::Fire || refused, sparkles = look == Look::Holy || look == Look::Magic || look == Look::Curse;
    for (int y = light.oy; y < light.oy + light.h; ++y)
        for (int x = light.ox; x < light.ox + light.w; ++x) {
            const size_t i = at(x, y);
            const float c = coverage.core[i], g = coverage.glow[i];
            if (c <= 0 && g <= .002f) continue;
            float keep = 1, edge = 0, crack = 0;
            const bool line = c > 0 || g > .25f;
            if (line && look == Look::Cold && (exit > 0 || cracksIn > 0)) {
                const Cell cell = Cellular(float(x), float(y), 11, seed + 3);
                keep = SmoothStep(exit - .04f, exit + .04f, Random(seed + 5, cell.id) * .9f + .05f);
                crack = std::exp(-std::pow((cell.f2 - cell.f1) / 1.1f, 2.0f)) * cracksIn * keep;
            } else if (line && look == Look::Lightning) {
                keep = flickerLeft;
            } else if (line && exit > 0) {
                const float level = Fbm(float(x) * .06f, float(y) * .06f, seed + 211) * .9f + .05f;
                keep = SmoothStep(exit - .07f, exit + .07f, level);
                if (burns || sparkles) edge = std::exp(-std::pow((level - exit) / .035f, 2.0f)) * SmoothStep(.25f, .6f, g);
            }
            const float halo = look == Look::Holy ? std::pow(g, .6f) : g;
            light.Add(x, y, hot, c * lineGain * beat * (1 + .8f * flash) * keep);
            light.Add(x, y, pal.mid, halo * glowGain * beat * (look == Look::Lightning ? flickerLeft : glowLeft));
            if (edge > 0) light.Add(x, y, look == Look::Fire ? pal.hot : refused ? pal.deep : pal.hot, edge * (sparkles ? .7f : 1.3f));
            if (crack > 0) light.Add(x, y, Colour{1, 1, 1}, crack * .9f);
        }

    // 2. What rises or falls off the line: flames, mist, holy rays, ghostly smoke. Each column carries
    // the line's light up (or down), fading over `height` pixels, broken by noise scrolling with it;
    // it dies down with the exit.
    struct Smear { int direction = 0; float height = 0, gain = 0, sx = 0, sy = 0, speed = 0; };
    Smear smear;
    if (look == Look::Fire) smear = {-1, 46, 1.7f, .06f, .03f, 2.6f};
    else if (look == Look::Poison) smear = {1, 34, .85f, .05f, .03f, -1.2f};
    else if (look == Look::Holy) smear = {-1, 50, .7f, .14f, .012f, .5f};
    else if (look == Look::Bone) smear = {-1, 40, .6f, .045f, .03f, .9f};
    else if (look == Look::Curse) smear = {1, 30, .75f, .05f, .03f, -1.0f};
    else if (look == Look::Shadow) smear = {-1, 40, .5f, .04f, .025f, .6f};
    if (smear.direction) {
        const float decay = std::exp(-1 / smear.height), left = (1 - exit) * ignite;
        std::vector<float> column(size_t(light.h));
        for (int x = light.ox; x < light.ox + light.w; ++x) {
            float carried = 0;
            for (int k = 0; k < light.h; ++k) {
                const int y = smear.direction < 0 ? light.oy + light.h - 1 - k : light.oy + k;
                const size_t i = at(x, y);
                carried = std::max(std::max(coverage.core[i], coverage.glow[i] * .55f), carried * decay);
                column[size_t(y - light.oy)] = carried;
            }
            for (int y = light.oy; y < light.oy + light.h; ++y) {
                const float f = column[size_t(y - light.oy)];
                if (f < .04f) continue;
                const float n = Fbm(float(x) * smear.sx, float(y) * smear.sy + t * smear.speed, seed + 101);
                const float v = std::clamp(f * 1.6f - (1 - n) * 1.0f, 0.0f, 1.0f) * left;
                if (v <= 0) continue;
                light.Add(x, y, look == Look::Fire ? Ramp(pal, .15f + .9f * v) : pal.mid, v * smear.gain);
            }
        }
    }

    // 3. Particles, sparks and arcs along the stroke.
    const Path path = PathOf(job.stroke, 64);
    if (!path.p.empty()) {
        auto rnd = [&](uint32_t i, uint32_t k) { return Random(seed, i, k); };
        switch (look) {
        case Look::Drawing:   // a few glints along the line as it is drawn
            for (uint32_t i = 0; i < 16; ++i) {
                const float tw = std::pow(std::max(0.0f, std::sin(t * (5 + 4 * rnd(i, 1)) + 6.28f * rnd(i, 2))), 6.0f);
                const auto p = path.At(rnd(i, 0));
                light.Glint(p.x, p.y, 6 + 5 * rnd(i, 3), pal.hot, tw);
            }
            break;
        case Look::Refused:   // ash falling from the crumbling line
            for (uint32_t i = 0; i < 26; ++i) {
                const float start = .06f + .45f * rnd(i, 1), age = t - start;
                if (age < 0) continue;
                const auto p = path.At(rnd(i, 0));
                const float x = p.x + 10 * std::sin(age * 5 + 6.28f * rnd(i, 2)), y = p.y + 30 * age + 140 * age * age;
                light.Dot(x, y, 1.4f + rnd(i, 3), pal.mid, .55f * std::max(0.0f, 1 - age / .7f));
            }
            break;
        case Look::Fire:      // embers rising, wobbling, cooling from yellow to red
            for (uint32_t i = 0; i < 64; ++i) {
                const float start = 1.3f * rnd(i, 1), life = .55f + .55f * rnd(i, 2), age = t - start;
                if (age < 0 || age > life) continue;
                const float k = age / life;
                const auto p = path.At(rnd(i, 0));
                const float x = p.x + 8 * std::sin(age * (4 + 3 * rnd(i, 3)) + 6.28f * rnd(i, 4));
                const float y = p.y - 6 - (70 + 110 * rnd(i, 5)) * age;
                light.Dot(x, y, 1.6f + 1.6f * (1 - k), Ramp(pal, 1 - .7f * k), 2.0f * (1 - k) * particlesLeft);
            }
            break;
        case Look::Cold: {    // ice shards grow out of the line, then break off and fall; frost glints
            for (uint32_t i = 0; i < 18; ++i) {
                glyphs::Point normal;
                const auto base = path.At(.03f + .94f * rnd(i, 0), &normal);
                const float side = rnd(i, 1) < .5f ? -1.0f : 1.0f, turn = (rnd(i, 2) - .5f) * 1.3f;
                const float ca = std::cos(turn), sa = std::sin(turn);
                const glyphs::Point dir{side * (normal.x * ca - normal.y * sa), side * (normal.x * sa + normal.y * ca)};
                const float grow = Smooth(std::clamp((t - .04f * rnd(i, 3)) / .16f, 0.0f, 1.0f));
                const float length = (16 + 26 * rnd(i, 4)) * grow;
                // Broken off from the exit on: drifting out and falling, turning, gone.
                const float broken = std::max(0.0f, t - hold - .25f * rnd(i, 5));
                const glyphs::Point a{base.x + dir.x * 45 * broken, base.y + dir.y * 45 * broken + 260 * broken * broken};
                const float spin = broken * (rnd(i, 6) - .5f) * 6, cs = std::cos(spin), sn = std::sin(spin);
                const glyphs::Point tip{a.x + length * (dir.x * cs - dir.y * sn), a.y + length * (dir.x * sn + dir.y * cs)};
                light.Streak(a, tip, 2.4f, 0, pal.hot, 1.1f * std::max(0.0f, 1 - broken / .6f), 6, pal.mid);
            }
            for (uint32_t i = 0; i < 22; ++i) {
                glyphs::Point normal;
                const auto p = path.At(rnd(i, 7), &normal);
                const float off = (rnd(i, 8) - .5f) * 30;
                const float tw = std::pow(std::max(0.0f, std::sin(t * (6 + 5 * rnd(i, 9)) + 6.28f * rnd(i, 10))), 8.0f);
                light.Glint(p.x + normal.x * off, p.y + normal.y * off, 4 + 5 * rnd(i, 11), pal.hot, tw * ignite * particlesLeft);
            }
            break;
        }
        case Look::Lightning: {   // arcs that jump along the stroke 24 times a second, with forks and sparks
            for (uint32_t arc = 0; arc < 3; ++arc) {
                const float amp = arc ? 14.0f : 6.0f, flicker = (.65f + .35f * Random(seed, frame, arc + 50)) * 1.4f * flickerLeft;
                glyphs::Point last{};
                for (size_t k = 0; k < path.p.size(); ++k) {
                    const float n = Noise(float(k) * .45f + float(arc) * 13.7f, float(frame) * 3.1f, seed + 7) - .5f;
                    const float ends = std::min(1.0f, std::min(float(k), float(path.p.size() - 1 - k)) / 4);   // pinned at the ends
                    const glyphs::Point p{path.p[k].x + path.n[k].x * n * 2 * amp * ends, path.p[k].y + path.n[k].y * n * 2 * amp * ends};
                    if (k) light.Streak(last, p, arc ? 1.0f : 1.6f, arc ? 1.0f : 1.6f, pal.hot, flicker * ignite * (arc ? .8f : 1.1f), 7, pal.mid);
                    last = p;
                }
            }
            for (uint32_t f = 0; f < 6; ++f) {   // forks: short jagged branches off the line
                glyphs::Point normal;
                auto p = path.At(Random(seed, frame, f + 70), &normal);
                const float side = Random(seed, frame, f + 80) < .5f ? -1.0f : 1.0f;
                for (int s = 0; s < 5; ++s) {
                    const float j = (Random(seed, frame * 8 + uint32_t(s), f + 90) - .5f) * 1.6f;
                    const glyphs::Point q{p.x + side * (normal.x + j * normal.y) * 10, p.y + side * (normal.y - j * normal.x) * 10};
                    light.Streak(p, q, 1.0f - .15f * float(s), .85f - .15f * float(s), pal.hot, 1.1f * ignite * flickerLeft, 6, pal.mid);
                    p = q;
                }
            }
            for (uint32_t i = 0; i < 12; ++i) {   // sparks thrown off, each lives a few frames
                const uint32_t born = frame / 3;
                const float age = t - float(born * 3) / 24;
                glyphs::Point normal;
                const auto p = path.At(Random(seed, born, i + 110), &normal);
                const float side = Random(seed, born, i + 120) < .5f ? -1.0f : 1.0f, speed = 120 + 160 * Random(seed, born, i + 130);
                light.Dot(p.x + side * normal.x * speed * age, p.y + side * normal.y * speed * age + 200 * age * age, 1.5f, pal.hot,
                          1.8f * std::max(0.0f, 1 - age / .15f) * ignite * flickerLeft);
            }
            break;
        }
        case Look::Poison:    // drops falling from the line, bubbles rising and popping
            for (uint32_t i = 0; i < 22; ++i) {
                const float start = .05f + 1.0f * rnd(i, 1), age = t - start;
                if (age < 0) continue;
                const auto p = path.At(rnd(i, 0));
                // A drop swells on the line, then falls, stretching.
                const float swell = std::min(1.0f, age / .25f), fall = std::max(0.0f, age - .25f);
                const float v = 300 * fall, y = p.y + 3 * swell + 150 * fall * fall;
                light.Streak({p.x, y - 2 - std::min(18.0f, v * .04f)}, {p.x, y}, .8f, 1.2f + 1.6f * swell, pal.hot,
                             1.2f * std::max(0.0f, 1 - fall / .7f) * particlesLeft, 5, pal.mid);
            }
            for (uint32_t i = 0; i < 16; ++i) {
                const float start = .9f * rnd(i, 4), life = .5f + .5f * rnd(i, 5), age = t - start;
                if (age < 0 || age > life) continue;
                const auto p = path.At(rnd(i, 3));
                const float r = 2.5f + 5.5f * age / life, cx = p.x + 6 * std::sin(age * 7 + 6.28f * rnd(i, 6)), cy = p.y - 32 * age;
                for (int s = 0; s < 12; ++s) {   // a ring
                    const float a0 = 6.2832f * float(s) / 12, a1 = 6.2832f * float(s + 1) / 12;
                    light.Streak({cx + r * std::cos(a0), cy + r * std::sin(a0)}, {cx + r * std::cos(a1), cy + r * std::sin(a1)}, .7f, .7f, pal.hot,
                                 .9f * (1 - age / life) * particlesLeft);
                }
            }
            break;
        case Look::Holy:      // glints along the line, motes rising like sparks of light
            for (uint32_t i = 0; i < 20; ++i) {
                const auto p = path.At(rnd(i, 0));
                const float tw = .35f + .65f * std::pow(std::max(0.0f, std::sin(t * (3 + 3 * rnd(i, 1)) + 6.28f * rnd(i, 2))), 4.0f);
                light.Glint(p.x, p.y, (7 + 6 * rnd(i, 3)) * ignite, pal.hot, tw * ignite * particlesLeft);
            }
            for (uint32_t i = 0; i < 20; ++i) {
                const float start = 1.2f * rnd(i, 5), age = t - start;
                if (age < 0) continue;
                const auto p = path.At(rnd(i, 4));
                light.Dot(p.x + 5 * std::sin(age * 3 + 6.28f * rnd(i, 6)), p.y - (35 + 40 * rnd(i, 7)) * age, 1.6f, pal.hot,
                          .9f * std::max(0.0f, 1 - age / 1.1f) * particlesLeft);
            }
            break;
        case Look::Magic:     // motes running along the line both ways, weaving across it
            for (uint32_t i = 0; i < 34; ++i) {
                const float speed = (.18f + .3f * rnd(i, 1)) * (rnd(i, 2) < .5f ? 1 : -1);
                glyphs::Point trail{};
                for (int s = 0; s < 4; ++s) {   // the mote and its fading tail
                    const float tt = t - .025f * float(s);
                    const float u = std::fmod(rnd(i, 0) + tt * speed + 4, 1.0f);
                    glyphs::Point normal;
                    const auto p = path.At(u, &normal);
                    const float off = 13 * std::sin(tt * (5 + 4 * rnd(i, 3)) + 6.28f * rnd(i, 4));
                    const glyphs::Point q{p.x + normal.x * off, p.y + normal.y * off};
                    if (s) light.Streak(trail, q, 1.6f - .4f * float(s), 1.2f - .4f * float(s), pal.mid, .7f * ignite * particlesLeft);
                    else light.Dot(q.x, q.y, 2.2f + 1.4f * rnd(i, 5), rnd(i, 6) < .35f ? pal.hot : pal.mid, 1.4f * ignite * particlesLeft);
                    trail = q;
                }
            }
            break;
        case Look::Nature:    // spores drifting up and aside
            for (uint32_t i = 0; i < 28; ++i) {
                const float start = 1.1f * rnd(i, 1), age = t - start;
                if (age < 0) continue;
                const auto p = path.At(rnd(i, 0));
                const float x = p.x + 14 * std::sin(age * 2.2f + 6.28f * rnd(i, 2)) + 20 * age * (rnd(i, 3) - .5f), y = p.y - (22 + 30 * rnd(i, 4)) * age;
                light.Dot(x, y, 2.0f + 1.4f * rnd(i, 5), rnd(i, 6) < .4f ? pal.hot : pal.mid, 1.3f * std::max(0.0f, 1 - age / 1.2f) * particlesLeft);
            }
            break;
        case Look::Bone:      // chips breaking off and falling, turning
            for (uint32_t i = 0; i < 14; ++i) {
                const float start = .15f + .9f * rnd(i, 1), age = t - start;
                if (age < 0) continue;
                const auto p = path.At(rnd(i, 0));
                const float x = p.x + 30 * age * (rnd(i, 2) - .5f), y = p.y + 15 * age + 220 * age * age;
                const float a = 6.28f * rnd(i, 3) + age * 8 * (rnd(i, 4) - .5f), l = 3 + 3 * rnd(i, 5);
                light.Streak({x - l * std::cos(a), y - l * std::sin(a)}, {x + l * std::cos(a), y + l * std::sin(a)}, 1.2f, .6f, pal.hot,
                             .9f * std::max(0.0f, 1 - age / .8f) * particlesLeft);
            }
            break;
        case Look::Curse:     // crimson motes sinking and swaying
            for (uint32_t i = 0; i < 26; ++i) {
                const float start = 1.2f * rnd(i, 1), age = t - start;
                if (age < 0) continue;
                const auto p = path.At(rnd(i, 0));
                light.Dot(p.x + 9 * std::sin(age * 3 + 6.28f * rnd(i, 2)), p.y + (18 + 26 * rnd(i, 3)) * age, 1.8f + 1.2f * rnd(i, 4),
                          rnd(i, 5) < .35f ? pal.hot : pal.mid, 1.2f * std::max(0.0f, 1 - age / 1.1f) * particlesLeft);
            }
            break;
        case Look::Shadow:    // wisps of smoke drifting off both sides
            for (uint32_t i = 0; i < 40; ++i) {
                const float start = 1.0f * rnd(i, 1), age = t - start;
                if (age < 0) continue;
                glyphs::Point normal;
                const auto p = path.At(rnd(i, 0), &normal);
                const float side = rnd(i, 2) < .5f ? -1.0f : 1.0f, out = (14 + 22 * rnd(i, 3)) * age;
                light.Dot(p.x + side * normal.x * out, p.y + side * normal.y * out - 10 * age, 5 + 5 * rnd(i, 4) + 8 * age, rnd(i, 5) < .5f ? pal.mid : pal.deep,
                          .8f * std::max(0.0f, 1 - age / 1.2f) * particlesLeft);
            }
            break;
        case Look::Steel:     // blades spinning along the line, striking sparks
            for (uint32_t i = 0; i < 12; ++i) {
                const float u = std::fmod(rnd(i, 0) + t * (.3f + .25f * rnd(i, 1)) * (rnd(i, 2) < .5f ? 1 : -1) + 4, 1.0f);
                const auto p = path.At(u);
                const float a = t * (14 + 8 * rnd(i, 3)) + 6.28f * rnd(i, 4), l = 11 + 4 * rnd(i, 5);
                // A blade: thick in the middle, pointed at both ends.
                const glyphs::Point c{p.x, p.y}, e{l * std::cos(a), l * std::sin(a)};
                light.Streak({c.x - e.x, c.y - e.y}, c, 0, 2.2f, pal.hot, 1.4f * ignite * particlesLeft, 6, pal.mid);
                light.Streak(c, {c.x + e.x, c.y + e.y}, 2.2f, 0, pal.hot, 1.4f * ignite * particlesLeft, 6, pal.mid);
            }
            for (uint32_t i = 0; i < 26; ++i) {
                const uint32_t born = uint32_t(t * 8);
                const float age = t - float(born) / 8;
                const auto p = path.At(Random(seed, born, i + 300));
                const float a = 6.28f * Random(seed, born, i + 310), speed = 90 + 140 * Random(seed, born, i + 320);
                light.Dot(p.x + std::cos(a) * speed * age, p.y + std::sin(a) * speed * age + 260 * age * age, 1.4f, Colour{1, .8f, .45f},
                          2.0f * std::max(0.0f, 1 - age / .12f) * ignite * particlesLeft);
            }
            break;
        default: break;
        }
    }

    // 4. War and nature ring out in two echoes of the line, moving out from it; a curse closes in.
    if (look == Look::War || look == Look::Nature || look == Look::Curse)
        for (int y = light.oy; y < light.oy + light.h; ++y)
            for (int x = light.ox; x < light.ox + light.w; ++x) {
                const float d = coverage.dist[at(x, y)];
                if (d >= Coverage::Reach) continue;
                for (int wave = 0; wave < 2; ++wave) {
                    const float age = t - .12f - .3f * float(wave);
                    if (age <= 0) continue;
                    const float r = look == Look::Curse ? 42 - age * 60.0f : 6 + age * (look == Look::War ? 80.0f : 50.0f);
                    if (r <= 4) continue;
                    if (r >= Coverage::Reach) continue;
                    const float band = std::exp(-std::pow((d - r) / 4.0f, 2.0f)) * (look == Look::Curse ? .7f : (1 - r / Coverage::Reach) * (look == Look::War ? 1.0f : .6f));
                    light.Add(x, y, pal.mid, band);
                }
            }

    // Light to straight alpha: the brightest channel is the coverage; light past white runs to white.
    for (int y = light.oy; y < light.oy + light.h; ++y)
        for (int x = light.ox; x < light.ox + light.w; ++x) {
            const float* p = &light.rgb[(size_t(y - light.oy) * size_t(light.w) + size_t(x - light.ox)) * 3];
            const float peak = std::max(p[0], std::max(p[1], p[2]));
            const float a = std::min(1.0f, peak) * alpha;
            if (a <= 1.0f / 255) continue;
            const float white = std::clamp((peak - 1) * .5f, 0.0f, .7f);
            uint8_t* out = &rgba[at(x, y) * 4];
            for (int c = 0; c < 3; ++c) {
                const float v = std::clamp(p[c] / std::max(peak, 1e-6f), 0.0f, 1.0f);
                out[c] = uint8_t(std::lround((v * (1 - white) + white) * 255));
            }
            out[3] = uint8_t(std::lround(a * 255));
        }
    return rgba;
}
inline std::vector<uint8_t> Paint(const Job& job) {
    Coverage coverage;
    if (job.stroke.size() >= 2) coverage.Add(job.stroke, job.strokeId);
    return Shade(coverage, job);
}

struct Image { uint64_t key = 0, strokeId = 0; std::vector<uint8_t> pixels; atlasdirty::Change change; };   // change: filled by the transport's painter (atlas_dirty.h)

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
            return Image{job.key, job.strokeId, Shade(*coverage, job)};
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
