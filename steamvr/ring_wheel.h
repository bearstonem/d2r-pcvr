#pragma once
// Lab Ring wheel (T025; Lab > Experiment: Ring wheel, exp.ring_wheel): the wheel's geometry, hit test and input state,
// pure and Windows-free so every number is unit-tested (ring_wheel_test.cpp). Build step R2 (vault Tickets\025-ring-
// skill-wheel-lab\build-plan.md E2, E5; look L1, L2).
//
// Coordinates: "wheel px" are the design kit's (Design\ring-wheel\kit\wheel.js): the 768 x 800 wheel region, its center
// C = (384, 384), the shelf row at y 760. "Canvas px" are the 1024 x 1024 canvas the region sits in, at (128, 112); the
// footer is in canvas px (upstream's FooterBounds, v = y / 1024). 960 canvas px a meter, so 15 px a degree at 0.9 m.
//
// Layout e, both Tree layouts (Jeff has not picked one, 2026-11-07; the default is Trees shown):
//  - Trees shown (kit idle=all): every tree's skills at fixed seats in its own 110 deg sector (10 deg gutters), three
//    loose bands at r 190 / 232 / 270, relaxed to 70 px apart, clamped to r 184-274; pointing a tree fades the others.
//  - Tree orbs (kit idle=orbs): one orb per tree at r 128; the pointed tree fans its skills over 110 deg, bands 204 / 248
//    / 286, relaxed to 70 px, clamped to r 198-286; the other trees are closed.
// Both: tree directions -90, 30, 150 deg (top, lower right, lower left); the same deterministic relaxation as the kit,
// seeded by skill id (the kit seeds by English name, so seats differ from the kit's pictures by a few px, never in kind).
//
// Seats are the hero's learned skills for now (the kit's seats=packed): upstream's catalog has no passive flag, so the
// unlearned seats of seats=all wait for trace T1 (plan E4). Unknown class (skillcategories::ValidClass false): no ring.
//
// Input (plan E5): nearest shown seat within 27 px of its rest position, kept until 27 + hysteresis px; a tree under 50%
// alpha takes no hits. Tree switching waits the dwell and never comes sooner than the hold after the last switch, with
// 10 deg of angle hysteresis. Every trigger action fires on release, and only on the target it was pressed on; after a
// page change presses are locked out for 250 ms and a trigger held through it must be let go first.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include "controller_labels.h"
#include "glyph_cast.h"
#include "interactions.h"
#include "skill_categories.h"

namespace framevr::ringwheel {
constexpr float Pi = 3.14159265358979323846f, Tau = 2 * Pi;
// The design kit's numbers (wheel px).
constexpr float WheelW = 768, WheelH = 800, C = 384, ContentR = 320;
constexpr float IconPx = 44, HitPx = 54, HitR = HitPx / 2;
constexpr float CenterR = 80, CenterGrownR = 136, CenterDeadBand = 3;   // the split's hairline: 6 px dead band
constexpr float ShelfY = 760, ShelfPitch = 60, ShelfGap = 56;
constexpr float MinPitch = 70;
// The canvas.
constexpr float CanvasPx = 1024, CanvasX = 128, CanvasY = 112, CanvasPerMeter = 960;
constexpr float CanvasCx = CanvasX + C, CanvasCy = CanvasY + C;
constexpr int Trees = 3, MaxSeats = 64, Slots = 5;
constexpr float TreeDirDeg[Trees] = {-90, 30, 150};
constexpr float SectorDeg = 120;    // a tree's third for pointing (its 110 deg of seats and half of each gutter)
constexpr float TreeHysteresisDeg = 10;

enum class TreeLayout { Shown, Orbs };

// One seat to place: tree 0-2 (placement order), or -1 for the shelf (general and item skills).
struct SeatIn { int id = -1, owner = -1, tree = -1; };
struct Seat {
    int id = -1, owner = -1, tree = -1;   // tree -1: the shelf
    float x = 0, y = 0, angle = 0, ring = 0;   // rest position, wheel px; angle and ring round C (trees only)
    bool shelfItem = false;   // the shelf's item group (an owner), after the gap
};
struct Layout {
    TreeLayout mode = TreeLayout::Shown;
    std::array<Seat, MaxSeats> seats{};
    int count = 0;
    std::array<float, Trees> dir{};          // each tree's direction, radians (canvas y down: -90 deg is up)
    std::array<int, Trees> perTree{};
    int shelf = 0;
};

// The kit's hash (wheel.js hash, Math.imul as uint32 wrap): [0, 1).
inline float Hash(uint32_t n) {
    n = (n ^ (n >> 16)) * 0x45d9f3bu; n = (n ^ (n >> 16)) * 0x45d9f3bu;
    return float(double(n ^ (n >> 16)) / 4294967296.0);
}
inline uint32_t SeedOf(int id) { uint32_t h = 2166136261u; for (int i = 0; i < 4; ++i) { h ^= uint32_t(id >> (8 * i)) & 0xff; h *= 16777619u; } return h; }
inline float Wrap(float a, float around) { while (a - around > Pi) a -= Tau; while (around - a > Pi) a += Tau; return a; }

// Places one tree's seats (kit layoutE): counts per band by band length, the middle band staggered, a fixed per-seat
// nudge, then pushed apart to MinPitch and clamped to the band range and the sector (deterministic, fixed order).
inline void PlaceTree(Seat* mine, int n, float dir, TreeLayout mode) {
    const bool shown = mode == TreeLayout::Shown;
    const float bands[3] = {shown ? 190.f : 204.f, shown ? 232.f : 248.f, shown ? 270.f : 286.f};
    // Both layouts fan a tree over 110 deg (10 deg gutters; designer 2026-11-07: Tree orbs' 140 deg overlapped the neighbor
    // trees by 20 deg whenever two trees show, the 300 ms switch crossfade and row 13 above 0).
    const float sec = 110.f * Pi / 180, r0 = shown ? 184.f : 198.f, r1 = shown ? 274.f : 286.f;
    const int iterations = shown ? 120 : 80;
    const float tot = bands[0] + bands[1] + bands[2];
    int counts[3];
    for (int b = 0; b < 3; ++b) counts[b] = int(std::floor(n * bands[b] / tot));
    int left = n - counts[0] - counts[1] - counts[2];
    for (int i = 2; left > 0; i = (i + 2) % 3) { ++counts[i]; --left; }
    int k = 0;
    for (int b = 0; b < 3; ++b) {
        const int c = counts[b]; if (!c) continue;
        const float step = sec / c;
        for (int i = 0; i < c; ++i, ++k) {
            const uint32_t seed = SeedOf(mine[k].id);
            const float h1 = Hash(seed + 11), h2 = Hash(seed + 17);
            const float a = dir - sec / 2 + step / 2 + i * step + (b == 1 && c > 1 ? (i % 2 ? .25f : -.25f) * step : 0) + (h1 - .5f) * .06f;
            const float rr = bands[b] - h2 * 9;
            mine[k].x = C + rr * std::cos(a); mine[k].y = C + rr * std::sin(a); mine[k].angle = a; mine[k].ring = rr;
        }
    }
    const float a0 = dir - sec / 2 + .08f, a1 = dir + sec / 2 - .08f;
    for (int it = 0; it < iterations; ++it) {
        for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) {
            float dx = mine[j].x - mine[i].x, dy = mine[j].y - mine[i].y, d = std::hypot(dx, dy);
            if (d == 0) d = .01f;
            if (d < MinPitch) { const float push = (MinPitch - d) / 2 * .6f; dx /= d; dy /= d; mine[i].x -= dx * push; mine[i].y -= dy * push; mine[j].x += dx * push; mine[j].y += dy * push; }
        }
        for (int i = 0; i < n; ++i) {
            float r = std::hypot(mine[i].x - C, mine[i].y - C), an = Wrap(std::atan2(mine[i].y - C, mine[i].x - C), dir);
            r = std::clamp(r, r0, r1); an = std::clamp(an, a0, a1);
            mine[i].x = C + r * std::cos(an); mine[i].y = C + r * std::sin(an); mine[i].angle = an; mine[i].ring = r;
        }
    }
}
// The whole wheel from its seats (in tree order, each tree in the order given; at most 10 a tree in the kit's
// numbers): trees first, then the shelf row (general skills, a gap, then item skills), centered on C.
inline Layout Build(const SeatIn* in, int n, TreeLayout mode) {
    Layout l; l.mode = mode;
    for (int t = 0; t < Trees; ++t) l.dir[t] = TreeDirDeg[t] * Pi / 180;
    n = std::clamp(n, 0, MaxSeats);
    for (int t = 0; t < Trees; ++t) {
        const int start = l.count;
        for (int i = 0; i < n; ++i) if (in[i].tree == t) { auto& s = l.seats[l.count++]; s.id = in[i].id; s.owner = in[i].owner; s.tree = t; }
        l.perTree[t] = l.count - start;
        PlaceTree(&l.seats[start], l.perTree[t], l.dir[t], mode);
    }
    int general = 0, items = 0;
    for (int i = 0; i < n; ++i) if (in[i].tree < 0 || in[i].tree >= Trees) (in[i].owner == -1 ? general : items)++;
    const float width = (general + items - 1) * ShelfPitch + (items && general ? ShelfGap : 0);
    float x = C - width / 2;
    for (int pass = 0; pass < 2; ++pass) for (int i = 0; i < n && l.count < MaxSeats; ++i) {
        if (in[i].tree >= 0 && in[i].tree < Trees) continue;
        const bool item = in[i].owner != -1;
        if (item != (pass == 1)) continue;
        if (item && general && l.shelf == general) x += ShelfGap;
        auto& s = l.seats[l.count++]; s.id = in[i].id; s.owner = in[i].owner; s.tree = -1; s.shelfItem = item; s.x = x; s.y = ShelfY;
        x += ShelfPitch; ++l.shelf;
    }
    return l;
}
// Each class's tree placement (the kit's per-class data.order, review G1): position 0 (top), 1 (lower right), 2 (lower left)
// holds that skillcategories page (0-2). The kit's data.trees[i] is skillcategories page i + 1 for every class (names and
// skills checked 2026-11-07). Amazon [0, 2, 1], Barbarian [1, 0, 2], Druid [0, 2, 1], Paladin [1, 0, 2]; the rest as listed.
constexpr int TreeOrder[8][Trees] = {{0, 2, 1}, {0, 1, 2}, {0, 1, 2}, {1, 0, 2}, {1, 0, 2}, {0, 2, 1}, {0, 1, 2}, {0, 1, 2}};
inline int PageAt(int cls, int position) { return cls >= 0 && cls < 8 && position >= 0 && position < Trees ? TreeOrder[cls][position] : position; }
inline int PositionOf(int cls, int page) { for (int p = 0; p < Trees; ++p) if (PageAt(cls, p) == page) return p; return -1; }
// The hero's seats from upstream's snapshot: the catalog's skills (owned, active) of a known class, each class skill
// in its tree (skillcategories pages 1-3, placed by TreeOrder, in id order), the rest (general skills, item skills, a skill
// with no tree) on the shelf. Unknown class: false, upstream's wheel stays.
inline bool SeatsFrom(const interaction::Snapshot& s, std::array<SeatIn, MaxSeats>& out, int& n) {
    n = 0;
    if (!skillcategories::ValidClass(s.skillClass)) return false;
    for (unsigned i = 0; i < s.skillCount && i < s.catalog.size() && n < MaxSeats; ++i) {
        const auto& k = s.catalog[i];
        if (k.id < 0) continue;
        const int category = skillcategories::Category(s.skillClass, k.id, k.owner);
        out[size_t(n++)] = {k.id, k.owner, category >= 1 && category <= 3 ? PositionOf(s.skillClass, category - 1) : -1};
    }
    std::stable_sort(out.begin(), out.begin() + n, [](const SeatIn& a, const SeatIn& b) {
        const int ta = a.tree < 0 ? Trees : a.tree, tb = b.tree < 0 ? Trees : b.tree;
        return ta != tb ? ta < tb : (a.tree >= 0 && a.id < b.id);
    });
    return true;
}
// Another Lab's bindable entry (headset fixes 2026-11-15: Lab Army X menu, T033, through the FilterRingEntry seam): one more
// General seat on the shelf, after the hero's own general skills (owner -1, no tree), so Build places it on the shelf row.
// id < 0, no room, or already there: unchanged. Returns the new count.
inline int AddShelfEntry(std::array<SeatIn, MaxSeats>& in, int n, int id) {
    if (id < 0 || n < 0 || n >= MaxSeats) return n;
    for (int i = 0; i < n; ++i) if (in[size_t(i)].id == id && in[size_t(i)].owner == -1) return n;
    in[size_t(n++)] = {id, -1, -1};
    return n;
}

// ---- Pointing ----
enum class Kind { None, Gap, Seat, Tree, Glyphs, Gestures, Footer, Back, Hand, Quick };   // Back, Hand: the Glyphs and Gestures pages' center (R7); Quick: the center's third third (T036)
struct Target {
    Kind kind = Kind::None;
    int index = -1;   // Seat: the seat; Tree: the tree; Footer: upstream's cell id
    bool operator==(const Target&) const = default;
};
// The footer's drawn width in canvas px (plan E5 / L1): under the wheel (768, or 864 with developer mode's six
// tabs) or upstream's full width; then u over that span feeds upstream's FooterBounds unchanged.
inline float FooterWidth(bool fullWidth, int tabs) { return fullWidth ? CanvasPx : tabs > 5 ? 864.f : 768.f; }
inline int FooterAt(float cx, float cy, bool developer, bool fullWidth, interaction::MenuPage page = interaction::MenuPage::Spells) {   // R7: its page (review R2 G7)
    const int tabs = interaction::FooterCount(page, developer);
    const float w = FooterWidth(fullWidth, tabs), u = (cx - (CanvasPx - w) / 2) / w, v = cy / CanvasPx;
    for (int i = 0; i < tabs; ++i) if (interaction::FooterBounds(i, page, developer).Contains(u, v)) return interaction::FooterCell(i, page);
    return -1;
}
// ---- T036: the center in thirds (Jeff, 2026-11-15; vault Design\ring-wheel\t036) ----
// Glyphs upper left, Gestures upper right, Quick cast at the bottom: three 120 deg slices split by spokes at 12, 4 and 8
// o'clock, each spoke a 6 px dead band (CenterDeadBand on each side) that keeps the slice pointed last. The pointed slice
// reaches its grow past the center's edge (11%: 151 px grown), so the laser keeps it there; the other two dim (draw only).
enum Third { ThirdGlyphs, ThirdGestures, ThirdQuick, ThirdCount };
constexpr float ThirdMidDeg[ThirdCount] = {-150, -30, 90};   // canvas y down: upper left, upper right, bottom
constexpr float SpokeDeg[ThirdCount] = {-90, 30, 150};       // 12, 4 and 8 o'clock
constexpr float ThirdGrowDefault = 11, ThirdDimDefault = 70;  // the mockup's numbers (Jeff's picks): the slice +11%, the others at 70%
constexpr const char* ThirdName[ThirdCount] = {"Glyphs", "Gestures", "Quick cast"};
inline Kind ThirdKind(int t) { return t == ThirdGlyphs ? Kind::Glyphs : t == ThirdGestures ? Kind::Gestures : t == ThirdQuick ? Kind::Quick : Kind::None; }
inline int ThirdOf(Kind k) { return k == Kind::Glyphs ? ThirdGlyphs : k == Kind::Gestures ? ThirdGestures : k == Kind::Quick ? ThirdQuick : -1; }
// The third holding (dx, dy) from the center (wheel px, y down); -1 inside a spoke's dead band.
inline int ThirdAt(float dx, float dy) {
    for (const float s : SpokeDeg) {
        const float a = s * Pi / 180, ux = std::cos(a), uy = std::sin(a), along = dx * ux + dy * uy;
        const float off = along > 0 ? std::fabs(dx * uy - dy * ux) : std::hypot(dx, dy);
        if (off <= CenterDeadBand) return -1;
    }
    const float deg = std::atan2(dy, dx) * 180 / Pi;
    for (int t = 0; t < ThirdCount; ++t) { float d = std::fmod(deg - ThirdMidDeg[t] + 540.f, 360.f) - 180; if (std::fabs(d) <= 60) return t; }
    return -1;
}
// The pointed slice's reach: the center's radius (rest or grown) and the grow row's percent on top.
inline float ThirdReach(bool grown, float growPct) { return (grown ? CenterGrownR : CenterR) * (1 + std::clamp(growPct, 0.f, 100.f) / 100); }

// The tree whose third holds this angle (radians round C), keeping `current` until 10 deg past its edge.
inline int TreeAt(const Layout& l, float angle, int current) {
    const float half = SectorDeg / 2 * Pi / 180, keep = (SectorDeg / 2 + TreeHysteresisDeg) * Pi / 180;
    if (current >= 0 && current < Trees && std::fabs(Wrap(angle, l.dir[current]) - l.dir[current]) <= keep) return current;
    for (int t = 0; t < Trees; ++t) if (std::fabs(Wrap(angle, l.dir[t]) - l.dir[t]) <= half) return t;
    return -1;
}

struct Settings {
    TreeLayout layout = TreeLayout::Shown;
    float othersPct = 0;        // row 13: how visible the other trees stay while one is pointed, 0-100 (Trees shown; a look only, never what is hit)
    int dwellMs = 120;          // P4
    int holdMs = 300;           // P9: the least time between tree switches
    float hysteresisPx = 6;     // P5
    int lockoutMs = 250;        // after a page change
    float fadeMs = 160;         // L2: a tree's fade
    bool developer = false, footerFull = false;   // P7 Full width
    bool hasMotions = true;     // spellmotion::HasMotions: false (the Warlock) = one Glyphs center
    int growMs = 150;           // P10: the center grows after the laser is this long inside it (a sweep through never grows it)
    int blurbMs = 100;          // P11: a skill held this long shows its blurb (its highlight is at once)
    bool quickCast = false;     // R6Q (Jeff's design): seats take slot presses (QuickCast below); the trigger's release on a seat picks nothing
    bool thirds = false;        // T036: the center in thirds (the main page); false: the two halves (the glyph chooser's Back / Clear)
    float thirdGrowPct = ThirdGrowDefault;   // T036: the pointed third's reach past the center's edge (Center > Pointed third grows)
};
// No pick within this long of a hit taken (L2 Hit; the fight dim is R8, the rule is here).
constexpr double HitPickMs = 300;

// ---- Animation (L2; kit float and PRESET) ----
// A seat's float at time t (ms): bob 3 px, drift 4 and 2 px (layout e's periods x 1.6), tilt 2 deg; the summed offset
// clamped to 5 px so the picture never leaves its own hit area (critique B2). Seeded per seat, so neighbors differ.
struct Float { float dx = 0, dy = 0, rot = 0; };
inline Float FloatAt(int id, double tMs) {
    const uint32_t s = SeedOf(id);
    auto h = [&](int i) { return double(Hash(s + uint32_t(i))); };
    const double tm = 1.6, t = tMs, tau = Tau;
    const double bob = 3 * std::sin(tau * t / ((4000 + h(1) * 2000) * tm) + h(2) * tau);
    const double dx = 4 * std::sin(tau * t / ((6000 + h(3) * 3000) * tm) + h(4) * tau);
    const double dy2 = 2 * std::sin(tau * t / ((7000 + h(5) * 3000) * tm) + h(6) * tau);
    const double rot = 2 * Pi / 180 * std::sin(tau * t / (9000 + h(7) * 4000) + h(8) * tau);
    const double vy = bob + dy2, len = std::hypot(dx, vy), k = len > 5 ? 5 / len : 1;
    return {float(dx * k), float(vy * k), float(rot)};
}
// Row 1, Animation (L4; Jeff 2026-11-07, four levels: Off / Floating icons / Effect on hover / All effects), and rows 2-7
// it sets. Glow: 0 Off (a steady colored rim), 1 Pointed only, 2 Everywhere. Off also stops redraws
// (still: a compose only on a change). Custom shows when a row 2-7 differs from all three.
enum class Preset { Off, Floating, Hover, All, Custom };
enum Glow { GlowOff, GlowPointed, GlowEverywhere };
struct Anim {
    bool floats = true; int glow = GlowPointed; bool hoverFx = true, faceFx = true, burst = true;
    int crawlS = 0;
    bool still = false;
    bool operator==(const Anim&) const = default;
};
inline Anim PresetAnim(Preset p) {
    switch (p) {
    case Preset::Off: return {false, GlowOff, false, false, false, 0, true};
    case Preset::Floating: return {true, GlowOff, false, false, true, 0, false};
    case Preset::Hover: return {true, GlowPointed, true, true, true, 0, false};   // the default: at rest only floating, effects on hover
    case Preset::All: return {true, GlowEverywhere, true, true, true, 8, false};
    default: return {};
    }
}
inline Preset PresetOf(const Anim& a) {
    for (Preset p : {Preset::Off, Preset::Floating, Preset::Hover, Preset::All}) if (PresetAnim(p) == a) return p;
    return Preset::Custom;
}

// ---- R8: the animation layers (L2 "Animation layers", L4 rows 1-7) ----
// A skill's element for its glow and effects. Until trace T1 proves the Skills row's element column (plan E4), by its tree:
// each class's three trees (skillcategories order) mapped by the designer's style guide; shelf skills are steel.
// position: the seat's tree position on the wheel (Seat::tree, TreeOrder), turned into its skillcategories page here (R7c:
// R8 read the position as the page, so Amazon, Paladin, Barbarian and Druid glowed with another tree's element).
enum class Element : uint8_t { Fire, Cold, Light, Poison, Arcane, Holy, Aura, Shadow, Phys, Count };
inline Element ElementOf(int skillClass, int position) {
    using E = Element;
    static constexpr E table[8][Trees] = {
        {E::Phys, E::Arcane, E::Light},     // Amazon: Bow & Crossbow, Passive & Magic, Javelin & Spear
        {E::Fire, E::Light, E::Cold},       // Sorceress
        {E::Shadow, E::Poison, E::Arcane},  // Necromancer: Curses, Poison & Bone, Summoning
        {E::Phys, E::Aura, E::Holy},        // Paladin: Combat, Offensive Auras, Defensive Auras
        {E::Phys, E::Phys, E::Aura},        // Barbarian: Combat, Masteries, Warcries
        {E::Shadow, E::Phys, E::Fire},      // Druid: Summoning, Shape Shifting, Elemental
        {E::Light, E::Shadow, E::Phys},     // Assassin: Traps, Shadow Disciplines, Martial Arts
        {E::Fire, E::Arcane, E::Shadow},    // Warlock: Demon, Eldritch, Chaos
    };
    if (skillClass < 0 || skillClass >= 8 || position < 0 || position >= Trees) return E::Phys;
    return table[skillClass][PageAt(skillClass, position)];
}
// R7c (feedback 1 item 7, L1 Tree orb): each tree's mark glyph, the kit's sigil() map (skills.js / skills-more.js trees[i].sigil,
// page i): flame = Fire (the designer's two-tongue flame, TS1), bolt = Light, flake = Cold, hammer = Holy, sun and gear = Aura,
// shield = Arcane, eye = Shadow, fist = Phys, venom = Bone (TS3). The first nine share Element's numbers (the kit's glyph()
// kinds); drawn by the compose pass's RecipeTreeGlyph. position as ElementOf; None = no glyph.
enum class MarkGlyph : uint8_t { Fire, Cold, Light, Poison, Arcane, Holy, Aura, Shadow, Phys, Bone, None };
inline MarkGlyph TreeGlyphOf(int skillClass, int position) {
    using G = MarkGlyph;
    static constexpr G table[8][Trees] = {
        {G::Shadow, G::Arcane, G::Light},   // Amazon: eye, shield, bolt
        {G::Fire, G::Light, G::Cold},       // Sorceress: flame, bolt, flake
        {G::Shadow, G::Bone, G::Aura},      // Necromancer: eye, venom (a bone, TS3), gear
        {G::Holy, G::Aura, G::Arcane},      // Paladin: hammer, sun, shield
        {G::Phys, G::Arcane, G::Aura},      // Barbarian: fist, shield, sun
        {G::Aura, G::Phys, G::Fire},        // Druid: sun, fist, flame
        {G::Aura, G::Shadow, G::Phys},      // Assassin: gear, eye, fist
        {G::Shadow, G::Arcane, G::Fire},    // Warlock: eye, shield (Eldritch, TS3), flame
    };
    if (skillClass < 0 || skillClass >= 8 || position < 0 || position >= Trees) return G::None;
    return table[skillClass][PageAt(skillClass, position)];
}
// What one icon shows this frame, from the Animation rows (L4 table) and its state. glowMoves false = the glow's frozen
// frame (a steady colored rim, L4 row 3 Off); crawl: the lightning face crawl's strength 0-1.
struct Layers { bool floats = false, glowMoves = false, hoverFx = false, faceFx = false; float crawl = 0; };
// crawlIndex / crawlCount: this icon among the lightning icons (the stagger); -1 for a non-lightning icon.
inline Layers LayersFor(const Anim& a, bool pointed, int crawlIndex, int crawlCount, double tMs) {
    Layers l;
    l.floats = a.floats && !pointed;   // the pointed icon stops floating (L2)
    l.glowMoves = !a.still && (a.glow == GlowEverywhere || (a.glow == GlowPointed && pointed));
    l.hoverFx = pointed && a.hoverFx && !a.still;
    l.faceFx = pointed && a.faceFx && !a.still;
    if (crawlIndex >= 0 && !a.still) {
        if (pointed && (a.hoverFx || a.glow != GlowOff)) l.crawl = 1;   // continuous while pointed (L4 row 7, Effect on hover)
        else if (a.crawlS > 0 && crawlCount > 0) {   // at rest: one 300 ms burst per icon every N s, staggered (kit)
            const double per = a.crawlS * 1000.0, dur = std::min(320.0, per / crawlCount * .8), off = crawlIndex * per / crawlCount;
            const double p = std::fmod(std::fmod(tMs - off, per) + per, per);
            l.crawl = p < dur ? float(std::sin(p / dur * Pi)) : 0.f;
        }
    }
    return l;
}
// Icon art motion (kit face-examples.js and hover-defaults.js FACE): copies of the face to draw, each an offset, a turn, a
// scale and an opacity; ageMs since the icon was pointed. Zeal swings with two trail copies, Charge dashes out and back
// in, steel lunges, shadow rises in. Up to 3 copies.
struct FaceCopy { float dx = 0, dy = 0, rot = 0, scale = 1, alpha = 1; };
inline int FaceCopies(int skill, Element e, double ageMs, float r, FaceCopy out[3]) {
    constexpr int Zeal = 106, Charge = 107;
    auto ease = [](double f) { return f < .5 ? 2 * f * f : 1 - 2 * (1 - f) * (1 - f); };
    if (skill == Zeal) {
        const double p = std::fmod(ageMs, 1600.0) / 1600;
        if (p > .78) { out[0] = {}; return 1; }
        const double k = p / .78 * 5, f = k - std::floor(k);
        const int dir = int(std::floor(k)) % 2 ? -1 : 1;
        auto ang = [&](double f2) { return float(dir * (-.55 + 1.1 * ease(f2))); };
        out[0] = {0, 0, ang(std::max(0.0, f - .25)), 1, .25f}; out[1] = {0, 0, ang(std::max(0.0, f - .12)), 1, .45f}; out[2] = {0, 0, ang(f), 1, 1};
        return 3;
    }
    if (skill == Charge) {
        const double p = std::fmod(ageMs, 1800.0) / 1800;
        if (p < .2) { out[0] = {float(-r * .15 * std::sin(p / .2 * Pi))}; return 1; }
        if (p < .5) { const double f = (p - .2) / .3, x = r * 2.4 * f * f; out[0] = {float(x - r * .6), 0, 0, 1, .2f}; out[1] = {float(x - r * .3), 0, 0, 1, .4f}; out[2] = {float(x)}; return 3; }
        if (p < .8) { const double f = (p - .5) / .3, x = -r * 2.4 * (1 - f) * (1 - f) * (1 - f); out[0] = {float(x)}; return 1; }
        out[0] = {}; return 1;
    }
    if (e == Element::Phys) {
        const double p = std::fmod(ageMs, 1300.0) / 1300;
        const float k = p < .3 ? float(std::sin(p / .3 * Pi)) : 0.f;
        out[0] = {8 * k, 0, 0, 1 + .06f * k, 1}; return 1;
    }
    if (e == Element::Shadow) {   // rises in; the art never under 20% (R8 match stage 2, vr-ux cap 6)
        const double p = std::min(1.0, ageMs / 700); out[0] = {0, float(r * 1.6 * std::pow(1 - p, 3)), 0, 1, float(std::max(.2, p))}; return 1;
    }
    out[0] = {}; return 1;
}
// R8 match stage 2 (r8-match-spec 4a): the pointed effect's lifecycle. The hover ring and the 1.3x grow are immediate; the
// skill's effect starts 100 ms after the pointer settles on a target and fades in over 100 ms (a sweep across neighbors starts
// none). On leaving it fades out over 120 ms (the face eases back with it); a new target waits until that fade-out is done
// (one effect at a time). Re-pointed within 300 ms, the effect goes on from its age. Pure: the Lab steps it once a frame.
struct PointedFx {
    static constexpr double OnsetMs = 100, FadeInMs = 100, FadeOutMs = 120, ResumeMs = 300;
    struct Out { int seat = -1; double ageMs = 0; float alpha = 0; };
    int target = -1, shown = -1;
    double targetSince = 0, startMs = 0, inStart = 0;
    bool fading = false; double fadeStart = 0; float fadeFrom = 0, alpha = 0;
    int lastSeat = -1; double lastLeft = -1e9, lastStart = 0;
    Out Step(double nowMs, int pointed) {
        if (pointed != target) {
            if (shown >= 0 && shown == target && !fading) { fading = true; fadeStart = nowMs; fadeFrom = alpha; lastSeat = shown; lastLeft = nowMs; lastStart = startMs; }
            target = pointed; targetSince = nowMs;
        }
        if (fading) {
            alpha = float(fadeFrom * std::max(0.0, 1 - (nowMs - fadeStart) / FadeOutMs));
            if (alpha > 0 && target == shown) { fading = false; inStart = nowMs - alpha * FadeInMs; }   // back on it mid-fade: fades in from here
            else if (alpha > 0) return {shown, nowMs - startMs, alpha};
            else { fading = false; shown = -1; }
        }
        if (target < 0) { shown = -1; alpha = 0; return {}; }
        if (shown != target) {
            const bool resume = target == lastSeat && targetSince - lastLeft <= ResumeMs;
            if (!resume && nowMs - targetSince < OnsetMs) return {};
            shown = target; startMs = resume ? lastStart : nowMs; inStart = nowMs; alpha = 0;
        }
        alpha = float(std::clamp((nowMs - inStart) / FadeInMs, 0.0, 1.0));
        return {shown, nowMs - startMs, alpha};
    }
    void Reset() { *this = PointedFx{}; }
};
// Tree burst (L2, Tree orbs): a seat's way out of its orb, 0-1 (may overshoot), from the tree's open; k its order in the
// tree (28 ms stagger), 340 ms with overshoot. Burst off: a plain 160 ms ease.
inline float BurstAt(double ageMs, int k, bool burst) {
    if (!burst) { const float x = float(std::clamp(ageMs / 160, 0.0, 1.0)); return x * x * (3 - 2 * x); }
    const double f = std::clamp((ageMs - 28.0 * k) / 340, 0.0, 1.0), c1 = 1.70158, c3 = c1 + 1;
    return float(1 + c3 * std::pow(f - 1, 3) + c1 * std::pow(f - 1, 2));   // ease-out-back
}

// ---- R8: the fight dim and the death / load fade (L2 "Hit"; critique-vr-ux S7; plan "Fight safety", draw only) ----
// A health drop: the wheel (all but the pointed and picked icons) fades to 40% over 150 ms, holds 1.5 s after the last hit,
// comes back over 300 ms; a new hit extends it and never re-flashes. Death (health <= 0) or a load (the snapshot stale, or
// its session changed): 15% and no hits until the wheel is closed and opened again.
struct FightOut { float dim = 1, wheel = 1; bool hitsOff = false; double lastHitMs = -1e18; };
class FightFade {
    float health = -1; uint64_t session = 0; double lastHit = -1e18, dimStart = -1e18, staleSince = -1; bool dead = false;
public:
    static constexpr double FadeMs = 150, HoldMs = 1500, BackMs = 300;
    static constexpr double StaleMs = 1000;   // a load: the snapshot stale this long without a break (one late frame is not a load)
    static constexpr float HitLevel = .4f, DeadLevel = .15f;
    void Reset() { *this = FightFade{}; }   // the wheel closed: a reopen starts clean
    FightOut Step(double nowMs, bool vitalsKnown, float hp, bool fresh, uint64_t snapSession) {
        FightOut o;
        if (vitalsKnown && hp <= 0) dead = true;
        if (session && snapSession != session) dead = true;
        if (!fresh) { if (staleSince < 0) staleSince = nowMs; if (nowMs - staleSince >= StaleMs) dead = true; } else staleSince = -1;
        if (snapSession) session = snapSession;
        if (vitalsKnown && health >= 0 && hp < health - .5f) {   // a hit (half a point: no rounding noise)
            if (nowMs - lastHit > HoldMs + FadeMs + BackMs) dimStart = nowMs;   // a new dim; one already running is extended, not re-flashed
            lastHit = nowMs;
        }
        if (vitalsKnown) health = hp;
        o.lastHitMs = lastHit;
        if (dead) { o.wheel = DeadLevel; o.hitsOff = true; return o; }
        const double since = nowMs - lastHit;
        if (since <= HoldMs) o.dim = 1 - (1 - HitLevel) * float(std::clamp((nowMs - dimStart) / FadeMs, 0.0, 1.0));
        else if (since <= HoldMs + BackMs) o.dim = HitLevel + (1 - HitLevel) * float((since - HoldMs) / BackMs);
        return o;
    }
};

// ---- Bind slots (E5): the open hand's five, by upstream's rule (hand_controls.h), named by the controller ----
// slot: 0 trigger, 1 primary, 2 secondary, 3 grip + primary, 4 grip + secondary.
inline int SlotOf(int hand, int slot) {
    if (slot < 0 || slot >= Slots) return -1;
    const int b = slot == 0 ? 0 : (slot - 1) % 2 + 1;
    return skills::HandSlot(hand, b + (b && slot >= 3 ? 2 : 0));
}
inline std::string SlotName(controllers::Layout layout, int hand, int slot) {
    if (slot == 0) return "Trigger";
    if (slot < 0 || slot >= Slots) return "";
    const std::string face = controllers::Face(layout, hand, slot % 2 == 0);
    return slot >= 3 ? "Grip + " + face : face;
}
// Hover-change haptics at most one per gap per hand (P12, 0 = no limit); pick and bind pulses never pass through it.
class HapticGate {
    double at[2] = {-1e18, -1e18};
public:
    bool Hover(int hand, double nowMs, int gapMs) {
        if (hand < 0 || hand > 1) return false;
        if (gapMs > 0 && nowMs - at[hand] < gapMs) return false;
        at[hand] = nowMs;
        return true;
    }
};

// What one step reads: the laser's hit in canvas px, the pointer hand's trigger, a slot pressed this step on the
// wheel hand (0-4: trigger, primary, secondary, grip + primary, grip + secondary; -1 none), the skill bound to each.
struct StepIn {
    double nowMs = 0;
    bool hit = false; float x = 0, y = 0;
    bool trigger = false;
    int slotPressed = -1;
    std::array<int, Slots> bound{-1, -1, -1, -1, -1};
    double lastHitMs = -1e18;       // the last hit the hero took (no pick within HitPickMs)
    bool glyphClickSpent = false;   // Jeff 2026-11-08 #3: the trigger was held on the Glyphs half past the tap limit: its release opens nothing
};
enum class Event { None, Pick, Cancel, Bind, Clear, Upstream, OpenQuick };   // OpenQuick (T036): the Quick cast third's click (2026-11-16: its page in the ring)
struct StepOut {
    Target under, pressed;          // under the laser; held down on (Kind::None when not pressed)
    int tree = -1;                  // the pointed tree (-1 none)
    double treeAgeMs = 1e9;         // R8: since the pointed tree last changed (Tree orbs' burst)
    std::array<float, Trees> alpha{1, 1, 1};
    int picked = -1;                // the picked seat
    Event event = Event::None;
    int slot = -1, skill = -1, owner = -1, cell = -1;   // Bind / Clear: the slot and skill; Upstream: the cell to fire
    int movedFrom = -1;             // Bind: another slot of the hand that held this skill (D2R clears that duplicate), -1 none
    int replaced = -1;              // Bind: the skill the pressed slot held before ("X moved from Fire Wall", review G2), -1 none
    bool locked = false;            // presses locked out (a page change)
    bool centerGrown = false;       // the center at r 136 (a blurb, or a half held P10)
    int blurb = -1;                 // the seat whose blurb shows (held P11), -1 none
    bool refused = false;           // the last bind came back refused (said once)
};

class Engine {
    Layout layout{};
    double last = -1, lockUntil = 0, switchedAt = -1e9, pendingSince = 0;
    int tree = -1, pending = -1, picked = -1, seatHeld = -1;
    bool triggerWas = false, mustRelease = false, grown = false, refusedPending = false;
    double centerSince = -1, seatSince = 0;
    Target held{}, lastUnder{};
    std::array<float, Trees> alpha{1, 1, 1};
    std::array<float, Trees> hitGate{1, 1, 1};   // what can be hit (HitTarget): the row's look (alpha) never moves it
public:
    void Reset(const Layout& l) { *this = Engine{}; layout = l; }
    const Layout& Seats() const { return layout; }
    // A page change (Glyphs, Gestures, Back, a glyph's skill chosen): presses locked out, hysteresis, dwell and the
    // held press reset (not kept), a trigger already down must be let go first.
    void PageChanged(double nowMs, bool triggerDown, const Settings& s) {
        lockUntil = nowMs + s.lockoutMs; held = {}; lastUnder = {}; pending = tree; pendingSince = nowMs; mustRelease = triggerDown; triggerWas = triggerDown;
        grown = false; centerSince = -1; seatHeld = -1;
    }
    // The transport answered the last bind Refused: the next step says so; the pick stays ended.
    void Refused() { refusedPending = true; }
    float TreeTarget(int t, const Settings& s) const {
        if (s.layout == TreeLayout::Orbs || tree < 0 || tree == t) return HitTarget(t, s);
        return std::clamp(s.othersPct, 0.f, 100.f) / 100;
    }
    // What the laser can hit never depends on the row (Boss, 2026-11-08): a tree takes hits by this gate, which fades as the
    // tree does at row 0 (160 ms; half way it opens or closes), so even a fully visible other tree answers as its third until
    // the dwell and the hold switch to it.
    float HitTarget(int t, const Settings& s) const {
        if (s.layout == TreeLayout::Orbs) return tree == t ? 1.f : 0.f;
        return tree < 0 || tree == t ? 1.f : 0.f;
    }
    Target HitTest(float cx, float cy, const Settings& s) const {
        if (const int cell = FooterAt(cx, cy, s.developer, s.footerFull); cell >= 0) return {Kind::Footer, cell};
        const float x = cx - CanvasX, y = cy - CanvasY, r = std::hypot(x - C, y - C);
        if (s.thirds) {   // T036: by angle; the slice pointed last keeps its grown reach; a spoke's dead band keeps the last
            const int last = ThirdOf(lastUnder.kind), t = ThirdAt(x - C, y - C);
            if (r <= (grown ? CenterGrownR : CenterR) || (last >= 0 && t == last && r <= ThirdReach(grown, s.thirdGrowPct))) {
                if (t == ThirdGestures && !s.hasMotions) return {Kind::Gap};   // a class with no gestures: that third is shut
                if (t >= 0) return {ThirdKind(t)};
                return last >= 0 ? lastUnder : Target{Kind::Gap};
            }
        } else if (r <= (grown ? CenterGrownR : CenterR)) {
            if (!s.hasMotions) return {Kind::Glyphs};
            if (y < C - CenterDeadBand) return {Kind::Glyphs};
            if (y > C + CenterDeadBand) return {Kind::Gestures};
            return lastUnder.kind == Kind::Glyphs || lastUnder.kind == Kind::Gestures ? lastUnder : Target{Kind::Gap};
        }
        auto shown = [&](int i) { const int t = layout.seats[size_t(i)].tree; return t < 0 || hitGate[size_t(t)] >= .5f; };   // a faded or closed tree takes no hits, at any Other trees value
        auto dist = [&](int i) { return std::hypot(x - layout.seats[size_t(i)].x, y - layout.seats[size_t(i)].y); };
        // The seat pointed last frame is kept until the laser is the hysteresis past its edge; else the nearest wins.
        if (lastUnder.kind == Kind::Seat && lastUnder.index >= 0 && lastUnder.index < layout.count && shown(lastUnder.index) &&
            dist(lastUnder.index) <= HitR + s.hysteresisPx) return lastUnder;
        int best = -1; float bestD = HitR;
        for (int i = 0; i < layout.count; ++i) if (shown(i)) { const float d = dist(i); if (d <= bestD) { best = i; bestD = d; } }
        if (best >= 0) return {Kind::Seat, best};
        if (r <= ContentR) {
            const int t = TreeAt(layout, std::atan2(y - C, x - C), tree);
            return t >= 0 ? Target{Kind::Tree, t} : Target{Kind::Gap};
        }
        return {Kind::None};
    }
    StepOut Step(const StepIn& in, const Settings& s) {
        StepOut out;
        if (last < 0) for (int t = 0; t < Trees; ++t) { alpha[size_t(t)] = TreeTarget(t, s); hitGate[size_t(t)] = HitTarget(t, s); }   // an open starts settled: Tree orbs closed
        const double dt = last < 0 ? 0 : std::clamp(in.nowMs - last, 0.0, 1000.0);
        last = in.nowMs;
        const Target under = in.hit ? HitTest(in.x, in.y, s) : Target{};
        lastUnder = under; out.under = under;
        // The center grows after P10 inside it, or for a blurb (a seat held P11); a sweep through does neither.
        const bool inCenter = in.hit && (ThirdOf(under.kind) >= 0 ||   // T036: a third's grown reach counts as the center
            (std::hypot(in.x - CanvasCx, in.y - CanvasCy) <= (grown ? CenterGrownR : CenterR) && under.kind == Kind::Gap));
        if (inCenter) { if (centerSince < 0) centerSince = in.nowMs; } else centerSince = -1;
        if (under.kind == Kind::Seat) { if (seatHeld != under.index) { seatHeld = under.index; seatSince = in.nowMs; } } else seatHeld = -1;
        out.blurb = seatHeld >= 0 && in.nowMs - seatSince >= s.blurbMs ? seatHeld : -1;
        grown = out.blurb >= 0 || (centerSince >= 0 && in.nowMs - centerSince >= s.growMs);
        out.centerGrown = grown;
        out.refused = refusedPending; refusedPending = false;
        // The pointed tree: a seat names its tree, a third names itself, the center and the footer keep it, off the
        // wheel is none; a change waits the dwell and the hold.
        int want = tree;
        if (under.kind == Kind::Seat && layout.seats[size_t(under.index)].tree >= 0) want = layout.seats[size_t(under.index)].tree;
        else if (under.kind == Kind::Tree) want = under.index;
        else if (under.kind == Kind::None) want = -1;
        if (want != tree) {
            if (want != pending) { pending = want; pendingSince = in.nowMs; }
            if (in.nowMs - pendingSince >= s.dwellMs && in.nowMs - switchedAt >= s.holdMs) { tree = want; switchedAt = in.nowMs; }
        } else pending = tree;
        out.tree = tree; out.treeAgeMs = in.nowMs - switchedAt;
        for (int t = 0; t < Trees; ++t) {
            const float goal = TreeTarget(t, s), step = s.fadeMs > 0 ? float(dt) / s.fadeMs : 1.f;
            alpha[size_t(t)] = alpha[size_t(t)] < goal ? std::min(goal, alpha[size_t(t)] + step) : std::max(goal, alpha[size_t(t)] - step);
            const float gateGoal = HitTarget(t, s);
            hitGate[size_t(t)] = hitGate[size_t(t)] < gateGoal ? std::min(gateGoal, hitGate[size_t(t)] + step) : std::max(gateGoal, hitGate[size_t(t)] - step);
        }
        out.alpha = alpha;
        // Release fires, on the target pressed; lockout and a held-through trigger ignore the press.
        out.locked = in.nowMs < lockUntil;
        const bool down = in.trigger, rise = down && !triggerWas, fall = !down && triggerWas;
        triggerWas = down;
        if (fall) mustRelease = false;
        if (rise && !out.locked && !mustRelease) held = under.kind == Kind::None ? Target{Kind::Gap} : under;
        Target fired{};
        if (fall && held.kind != Kind::None) { if (held == under) fired = held; held = {}; }
        out.pressed = held;
        if (s.quickCast && (fired.kind == Kind::Seat || fired.kind == Kind::Gap || fired.kind == Kind::Tree)) fired = {};   // QuickCast owns seat presses
        if (in.glyphClickSpent && ThirdOf(fired.kind) >= 0) fired = {};   // a hold on the Glyphs half (#3) or a third (T036), not a click
        if (fired.kind == Kind::Seat) {
            if (picked == fired.index) { picked = -1; out.event = Event::Cancel; }
            else if (in.nowMs - in.lastHitMs >= HitPickMs) { picked = fired.index; out.event = Event::Pick; }
        } else if (fired.kind == Kind::Gap || fired.kind == Kind::Tree) {
            if (picked >= 0) { picked = -1; out.event = Event::Cancel; }
        } else if (fired.kind == Kind::Quick) {
            out.event = Event::OpenQuick;
        } else if (fired.kind == Kind::Glyphs || fired.kind == Kind::Gestures || fired.kind == Kind::Footer) {
            out.event = Event::Upstream;
            out.cell = fired.kind == Kind::Glyphs ? interaction::GlyphsCell : fired.kind == Kind::Gestures ? interaction::MotionsCell : fired.index;
        }
        // A slot pressed while a seat is picked binds it there; the slot already holding it clears it.
        if (out.event == Event::None && !s.quickCast && picked >= 0 && in.slotPressed >= 0 && in.slotPressed < Slots && !out.locked) {
            const auto& seat = layout.seats[size_t(picked)];
            out.slot = in.slotPressed;
            if (in.bound[size_t(in.slotPressed)] == seat.id) { out.event = Event::Clear; out.skill = -1; out.owner = -1; }
            else {
                out.event = Event::Bind; out.skill = seat.id; out.owner = seat.owner;
                for (int k = 0; k < Slots; ++k) if (k != in.slotPressed && in.bound[size_t(k)] == seat.id) out.movedFrom = k;
                if (const int was = in.bound[size_t(in.slotPressed)]; was >= 0 && was != seat.id) out.replaced = was;
            }
            picked = -1;
        }
        out.picked = picked;
        return out;
    }
};
// ---- Placement (R3; plan E3, L1; vr-numbers.md must-fix 1-3) ----
// On open the wheel spawns ahead of the eyes along the heading (yaw only), its center below eye level, its face turned to
// the eyes; then it stays in the room while open (world-locked). A hand switch or a Placement change respawns it.
// widthM is the 768 px wheel region's width: 960 canvas px a meter at 0.80 m, 15 px a degree at 0.90 m.
constexpr float BeamMissM = 1.5f;   // the laser's length when it hits nothing (upstream 0.65 m: shorter than the panel)
struct Placement {
    float distanceM = .9f, widthM = .8f, belowDeg = 8;
    bool worldLock = true;
    bool operator==(const Placement&) const = default;
};
inline float PxPerMeter(float widthM) { return widthM > 0 ? WheelW / widthM : 0; }
// Canvas px per degree at the panel's center (a degree of arc at the distance).
inline float PxPerDeg(float widthM, float distanceM) { return PxPerMeter(widthM) * distanceM * std::tan(Pi / 180); }
// The angle a span of px covers, seen from the eyes at the panel's center.
inline float DegOf(float px, float widthM, float distanceM) {
    const float m = widthM > 0 ? px / PxPerMeter(widthM) : 0;
    return distanceM > 0 ? 2 * std::atan(m / 2 / distanceM) * 180 / Pi : 0;
}
// A quad of contentPx canvas px across, in meters.
inline float QuadMeters(float contentPx, float widthM) { return contentPx * widthM / WheelW; }
inline interaction::Pose Spawn(const interaction::Pose& head, const Placement& p) {
    namespace m = math;
    const float h = m::Heading(head.q), b = std::clamp(p.belowDeg, -89.f, 89.f) * Pi / 180;
    const m::Vec forward{-std::sin(h), 0, -std::cos(h)};
    const m::Vec dir = forward * std::cos(b) + m::Vec{0, -std::sin(b), 0};
    const m::Vec center = head.p + dir * std::max(p.distanceM, .05f);
    const m::Vec normal = m::Normalized(head.p - center);
    const m::Vec right = m::Normalized(m::Cross({0, 1, 0}, normal));
    return {center, m::FromAxes(right, m::Cross(normal, right), normal)};
}
// The spawn pose kept while the wheel is open: respawned on an open, a hand switch or a Placement change.
class Placer {
    bool open = false;
    int hand = -1;
    Placement was{};
    interaction::Pose pose{};
public:
    bool Spawned = false;   // this step respawned it (for the log)
    const interaction::Pose& Step(int wheelHand, const interaction::Pose& head, const Placement& p) {
        Spawned = false;
        if (wheelHand < 0) { open = false; hand = -1; return pose; }
        if (!open || wheelHand != hand || !(p == was)) { pose = Spawn(head, p); open = true; hand = wheelHand; was = p; Spawned = true; }
        return pose;
    }
    void Reset() { open = false; hand = -1; }
};

// ---- R6: the ring's content quad, the laser's hit on it, the open hand's slot press, the bind request ----
// The content (canvas x0..x0 + canvasW, y0..y0 + rows) as a quad: its size and its center's offset from the wheel's center
// (the placed pose), meters in the panel's frame (x right, y up).
struct ContentQuad { float w = 0, h = 0, dx = 0, dy = 0; };
inline ContentQuad QuadFor(float x0, float y0, float canvasW, float rows, float widthM) {
    ContentQuad q;
    const float k = PxPerMeter(widthM);
    if (k <= 0) return q;
    q.w = canvasW / k; q.h = rows / k;
    q.dx = (x0 + canvasW / 2 - CanvasCx) / k; q.dy = -(y0 + rows / 2 - CanvasCy) / k;
    return q;
}
// math::IntersectPanel's (u, v) on that quad (u left to right, v top to bottom) in canvas px.
inline void CanvasOf(float u, float v, float x0, float y0, float canvasW, float rows, float& x, float& y) { x = x0 + u * canvasW; y = y0 + v * rows; }
// The open hand's press this frame, by upstream's slot rule (hand_controls.h): trigger 0, primary 1, secondary 2, grip +
// primary 3, grip + secondary 4; -1 none. `held` is last frame's trigger, primary, secondary (updated).
inline int SlotPress(const bool down[3], bool grip, bool held[3]) {
    int slot = -1;
    for (int b = 0; b < 3; ++b) {
        if (down[b] && !held[b] && slot < 0) slot = b == 0 ? 0 : b + (grip ? 2 : 0);
        held[b] = down[b];
    }
    return slot;
}
// A BindSkill for upstream's dispatch: skill index `index` (skills::HandSlot) to (skill, owner), -1 clears; the bank and
// the slot's current skill from the snapshot, as upstream's CanBind checks them. The sequence has its high bit set, so it
// is never one of upstream's own.
inline interaction::Request BindRequest(const interaction::Snapshot& s, int index, int skill, int owner, uint64_t number, uint64_t session, uint64_t now) {
    interaction::Request r;
    if (index < 0 || index >= skills::Count) return r;
    r.action = interaction::Action::BindSkill;
    r.sequence = (1ull << 63) | number;
    r.session = session; r.at = now;
    r.index = index; r.skill = skill; r.skillOwner = skill < 0 ? -1 : owner;
    r.bank = s.bindingBank; r.previousSkill = s.skills[size_t(index)].id; r.previousOwner = s.skills[size_t(index)].owner;
    return r;
}

// Jeff 2026-11-08 #3: this button draws glyphs, as upstream's button picker sends it (interactions.h, skill.id == GlyphDrawId): the
// transport saves [glyphs] draw_slot first, then clears the button's game skill. So it is never sent when that clear could not
// pass (upstream's own gate: fresh and editable): the setting must not change for a bind that is then refused.
inline interaction::Request GlyphDrawRequest(const interaction::Snapshot& s, int index, uint64_t number, uint64_t session, uint64_t now) {
    if (!interaction::Fresh(s, session, now) || !s.bindingsEditable) return {};
    interaction::Request r = BindRequest(s, index, -1, -1, number, session, now);
    if (r.action != interaction::Action::None) r.action = interaction::Action::BindGlyphDraw;
    return r;
}

// ---- R6Q: quick cast and hold to assign (Jeff's design; plan E5 "Quick cast and hold to assign", eval #9) ----
// Any of the ten slots (either hand: trigger, primary, secondary, grip + primary, grip + secondary) pressed with the laser
// on a seat: released under the tap limit (P14) = QuickCast; held to full (P13, Hold to assign) = Assign at once; released
// between = NoChange; after full a release does nothing. The seat is the one under the laser 80 ms before the press (a
// thumb's jolt at the press does not move it) and stays fixed while the button is held. One press at a time; a page change
// or a close mid-hold drops it. The plain cancel button (right secondary, left primary, no grip) with the laser off the
// wheel = Close; on a seat it is a slot press; on a gap inside the wheel nothing. Main page only (`main` false: nothing).
constexpr double SeatLatchMs = 80, PressJoltMs = 30;
// P13 as the hold runs: 0 = Off (every press a quick cast); 100-400 snap to 500 (L4's floor); at least tap + 150 ms.
inline int EffectiveHoldMs(int holdMs, int tapMs) {
    if (holdMs <= 0) return 0;
    return std::max({holdMs < 500 ? 500 : holdMs, tapMs + 150});
}
inline bool IsCancel(int hand, int slot) { return (hand == 0 && slot == 2) || (hand == 1 && slot == 1); }   // right B, left X: plain, no grip
struct QuickIn {
    double nowMs = 0;
    bool main = false;               // the ring's main page shows and takes presses
    Target under{};                  // the laser's target (Kind::None: off the wheel, or the quad missed)
    int press[2] = {-1, -1};         // a slot (0-4) pressed this frame, per hand (0 right, 1 left)
    bool down[2][Slots] = {};        // each hand's slots held now
    std::array<std::array<int, Slots>, 2> bound{};   // the skill on each hand's slot (-1 none)
    int holdMs = 1000, tapMs = 350;  // P13, P14
    double lastHitMs = -1e18;        // R8: no assign within 300 ms of a hit (L2 Hit); a cast is never blocked
    bool thirds = false;             // T036: the Gestures and Quick cast thirds take presses too (their pseudo seats)
};
// Jeff 2026-11-08 #3: the Glyphs half of the center takes a press like a seat does, under this pseudo seat (no real seat has it).
// T036: the Gestures and Quick cast thirds likewise: a tap opens the sub-menu, a hold puts that sub-menu on the button.
constexpr int GlyphsSeat = MaxSeats, GesturesSeat = MaxSeats + 1, QuickSeat = MaxSeats + 2;
inline int ThirdSeat(int third) { return third >= 0 && third < ThirdCount ? GlyphsSeat + third : -1; }
inline int SeatThird(int seat) { return seat >= GlyphsSeat && seat < GlyphsSeat + ThirdCount ? seat - GlyphsSeat : -1; }
inline bool IsThirdSeat(int seat) { return SeatThird(seat) >= 0; }
// MenuTap / MenuAssign (T036): the Gestures or Quick cast third (QuickOut::seat says which); Glyphs keeps GlyphTap / GlyphAssign.
enum class Quick { None, QuickCast, Assign, NoChange, Close, GlyphTap, GlyphAssign, MenuTap, MenuAssign };
struct QuickOut {
    Quick event = Quick::None;
    int seat = -1, hand = -1, slot = -1, replaced = -1;
    double heldMs = 0;
    float holdFrac = 0;              // the hold ring's fill (L2): 0 until the tap limit, then to 1 at full
    int holding = -1;                // the seat a hold runs on (stays at 100%, L5 Q5), -1 none
    bool assigned = false;           // that hold reached full and assigned
};
// #3 (designer): the center's words under a hold, skills and the Glyphs half alike. A bound button says what it loses, always on
// two lines ("Replaces", then the skill alone: never chosen by width), since binding clears that button's skill (interactions.h);
// else one line; "Assigned" at full, in the tick's green (literals: the draw key sees the change).
struct HoldLines { const char* top = nullptr; const char* line = nullptr; bool green = false; };
inline HoldLines HoldLinesFor(const QuickOut& o, bool bound, const char* text) {
    if (o.assigned) return {nullptr, "Assigned", true};
    return {bound ? "Replaces" : nullptr, text, false};
}
inline const char* EmptyHoldStatus(int holdingSeat) {
    return holdingSeat == GlyphsSeat ? "Draw glyph" : holdingSeat == GesturesSeat ? "Gestures menu" : holdingSeat == QuickSeat ? "Quick cast" : "Hold to assign";
}
// Audit 2026-11-10 bug 1: what a button gives up to an assign. With no game skill it may still be a hand's Draw glyph button
// (drawSlots: upstream's interaction::Output::glyphDraw, each hand's slot from the two [glyphs] values); a skill put there
// takes Draw glyph off it (transport, glyphcast::DrawKeyHolding), so the hold and the log say so. hand, slot: QuickOut's.
enum class Held { None, Skill, DrawGlyph };
inline Held HeldBy(int gameSkill, const std::array<int, 2>& drawSlots, int hand, int slot) {
    if (gameSkill >= 0) return Held::Skill;
    return glyphcast::DrawKeyHolding(float(drawSlots[0]), float(drawSlots[1]), SlotOf(hand, slot)) >= 0 ? Held::DrawGlyph : Held::None;
}
// The hold's words for it (HoldLinesFor's text and bound). skillName: nullptr when the skill list does not have it (the empty
// words, as before). The Glyphs half held on a button that already draws loses nothing: one line.
struct HoldWords { std::string text; bool bound = false; };
inline HoldWords HoldWordsFor(Held held, const char* skillName, int holdingSeat) {
    if (held == Held::Skill && skillName) return {skillName, true};
    if (held == Held::DrawGlyph && holdingSeat != GlyphsSeat) return {"Draw glyph", true};
    return {EmptyHoldStatus(holdingSeat), false};
}
// The assign log's "(was ...)".
inline std::string WasText(Held held, const char* skillName) {
    return held == Held::DrawGlyph ? "Draw glyph" : held == Held::None ? "none" : skillName ? skillName : "?";
}
// #3: the pointing hand's trigger is also the engine's click, so a release after the tap limit must not open the Glyphs page too.
// heldMs is the hold's own time on QuickCast's clock (the shown one, ring_wheel_close.h), never compared with the real clock.
inline bool GlyphClickSpent(const QuickOut& last, int pointer, int holdMs, int tapMs) {
    return IsThirdSeat(last.holding) && last.slot == 0 && last.hand == pointer && EffectiveHoldMs(holdMs, tapMs) > 0 && last.heldMs >= tapMs;   // T036: any third
}
// #3: a tap on the Glyphs half opens the page. The pointing hand's trigger already did, through the engine's click.
inline bool GlyphTapClicks(int hand, int slot, int pointer) { return !(slot == 0 && hand == pointer); }
class QuickCast {
    static constexpr int History = 128;   // samples kept: over 80 ms at any frame rate (1 s at 120 Hz)
    struct Sample { double t; int seat; };
    std::array<Sample, History> history{};
    int head = 0, count = 0;
    struct Hold { bool on = false, fired = false; int hand = -1, slot = -1, seat = -1; double since = 0; } hold;
    int SeatAt(double t) const {   // the seat under the laser at time t (the newest sample at or before it), -1 none
        for (int k = 0; k < count; ++k) { const auto& s = history[size_t((head - 1 - k + History) % History)]; if (s.t <= t) return s.seat; }
        return -1;
    }
public:
    void Reset() { *this = QuickCast{}; }
    // A page change or the wheel closing: a running hold is dropped (no assign, no cast).
    void Drop() { hold = {}; }
    QuickOut Step(const QuickIn& in, const Layout& l) {
        QuickOut out;
        history[size_t(head)] = {in.nowMs, in.under.kind == Kind::Seat ? in.under.index : in.under.kind == Kind::Glyphs ? GlyphsSeat :
                                           in.thirds && ThirdOf(in.under.kind) >= 0 ? ThirdSeat(ThirdOf(in.under.kind)) : -1};
        head = (head + 1) % History; count = std::min(count + 1, History);
        if (!in.main) { hold = {}; return out; }
        const int full = EffectiveHoldMs(in.holdMs, in.tapMs);
        if (hold.on) {
            const double held = in.nowMs - hold.since;
            out.heldMs = held; out.holding = hold.seat; out.hand = hold.hand; out.slot = hold.slot; out.assigned = hold.fired;
            if (!hold.fired) out.holdFrac = full > 0 && held >= in.tapMs ? float(std::clamp((held - in.tapMs) / std::max(1.0, double(full - in.tapMs)), 0.0, 1.0)) : 0.f;
            else out.holdFrac = 1;
            if (!hold.fired && full > 0 && held >= full && in.nowMs - in.lastHitMs >= HitPickMs) {   // full: assign at once (not within 300 ms of a hit)
                hold.fired = true; out.assigned = true;
                const int was = in.bound[size_t(hold.hand)][size_t(hold.slot)];
                out.seat = hold.seat; out.hand = hold.hand; out.slot = hold.slot;
                if (hold.seat == GlyphsSeat) { out.event = Quick::GlyphAssign; out.replaced = was; return out; }   // #3: this button draws glyphs
                if (IsThirdSeat(hold.seat)) { out.event = Quick::MenuAssign; out.replaced = was; return out; }   // T036: this button opens that sub-menu
                out.event = Quick::Assign;
                out.replaced = was >= 0 && was != l.seats[size_t(hold.seat)].id ? was : -1;
                return out;
            }
            if (!in.down[hold.hand][hold.slot]) {   // released
                if (!hold.fired) {
                    out.event = full == 0 || held < in.tapMs ? (hold.seat == GlyphsSeat ? Quick::GlyphTap : IsThirdSeat(hold.seat) ? Quick::MenuTap : Quick::QuickCast) : Quick::NoChange;
                    out.seat = hold.seat; out.hand = hold.hand; out.slot = hold.slot;
                }
                hold = {};
                out.holding = -1; out.holdFrac = 0;
            }
            return out;   // one press at a time: other presses are ignored while one is held
        }
        for (int hand = 0; hand < 2; ++hand) {
            const int slot = in.press[hand];
            if (slot < 0 || slot >= Slots) continue;
            if (in.under.kind == Kind::None && IsCancel(hand, slot)) {
                out.event = Quick::Close; out.hand = hand; out.slot = slot; return out;
            }
            const int seat = SeatAt(in.nowMs - SeatLatchMs);
            if (seat < 0 || (seat >= l.count && !IsThirdSeat(seat))) continue;   // no seat held 80 ms before the press: nothing
            // Held through the 80 ms (not a sweep that crossed it): on it at every sample until the last 30 ms, where a
            // thumb's jolt at the press may move the ray.
            bool stayed = true;
            for (int k = 0; k < count; ++k) {
                const auto& s = history[size_t((head - 1 - k + History) % History)];
                if (s.t < in.nowMs - SeatLatchMs) break;
                if (s.t <= in.nowMs - PressJoltMs && s.seat != seat) { stayed = false; break; }
            }
            if (!stayed) continue;
            hold = {true, false, hand, slot, seat, in.nowMs};
            out.holding = seat; out.hand = hand; out.slot = slot;
            return out;
        }
        return out;
    }
};
// ---- T036: each sub-menu on a button (Jeff: "bindable to a button from the General row, as Army X menu is") ----
// The three sub-menus' entries on the General shelf (AddShelfEntry). Never a game skill id (those stay under 400; upstream's
// Draw glyph is 9000, Lab Army X menu 9001). A hold there, or on the sub-menu's third (Gestures, Quick cast), sends upstream's
// own clear of that button's game skill (BindSkill, skill -1, as Draw glyph's pick does); on its Applied the Lab keeps the slot.
constexpr int MenuEntryFirst = 9101;
// Quick cast's potions and scrolls on its chooser's row (2026-11-16): Lab Quick cast radial's item ids (quick_radial.h: exact
// 1 << 28, family 2 << 28), never a skill or a Lab entry.
inline bool ItemSeat(int id) { return id >= (1 << 28); }
inline bool KindSeat(int id) { return (id >> 28) == 2; }   // a kind (quick_radial.h IsFamilyId): the best one carried when cast
inline int MenuEntryId(int third) { return third >= 0 && third < ThirdCount ? MenuEntryFirst + third : -1; }
inline int MenuOfEntry(int id) { return id >= MenuEntryFirst && id < MenuEntryFirst + ThirdCount ? id - MenuEntryFirst : -1; }
constexpr const char* MenuEntryName[ThirdCount] = {"Glyphs menu", "Gestures menu", "Quick cast"};   // Quick cast's entry is Lab Quick cast radial's own (2026-11-16)
// Per sub-menu the hand slot (skills::HandSlot numbering, 0-9) it is on, -1 none. The Lab's keys hold slot + 1 (0: none).
struct MenuButtons {
    std::array<int, ThirdCount> slot{-1, -1, -1};
    int MenuOf(int s) const { if (s < 0) return -1; for (int t = 0; t < ThirdCount; ++t) if (slot[size_t(t)] == s) return t; return -1; }
    bool Any() const { return slot[0] >= 0 || slot[1] >= 0 || slot[2] >= 0; }
    bool operator==(const MenuButtons&) const = default;
};
inline int ButtonSlotOf(float stored) { const long v = std::lround(stored) - 1; return v >= 0 && v < skills::Count ? int(v) : -1; }
// A sub-menu put on a slot: the others let it go (one button, one thing).
inline MenuButtons Assigned(MenuButtons b, int third, int slot) {
    for (auto& s : b.slot) if (s == slot) s = -1;
    if (third >= 0 && third < ThirdCount) b.slot[size_t(third)] = slot >= 0 && slot < skills::Count ? slot : -1;
    return b;
}
// A sub-menu keeps its button only while the button holds no game skill and draws no glyph: a skill put there later through
// upstream's Edit Slots or Center > Reset page, the ring's own hold to assign (headset 2026-11-16: a seat or third under the laser
// takes the sub-menu button's press, MenuPressOpens), or Draw glyph, takes the button back. Never a trigger (review F1). The
// stored key stays (review S5) for Edit Slots, Reset page and Draw glyph: only this per-frame list drops the slot while a skill or
// glyph is there; the ring's own skill bind lets the key go (review N4 of 2026-11-16: SkillTakesButton, labbuttons::LetGo).
// gameSkill: each slot's skill id (-1 none).
inline MenuButtons Kept(MenuButtons b, const std::array<int, skills::Count>& gameSkill, const std::array<int, 2>& drawSlots) {
    for (auto& s : b.slot) if (s >= 0 && (gameSkill[size_t(s)] >= 0 || s == drawSlots[0] || s == drawSlots[1])) s = -1;
    return b;
}
// A bound button's press (the FilterRingHands seam, before HandControls steps): a down edge by upstream's slot rule
// (SlotPress: trigger, primary, secondary, the grip layer). held: each hand's last trigger, primary, secondary (updated).
struct MenuPress { int third = -1, hand = -1; };
inline MenuPress MenuPressed(const MenuButtons& b, const bool down[2][3], const bool grip[2], bool held[2][3]) {
    MenuPress p;
    for (int h = 0; h < 2; ++h) {
        const int k = SlotPress(down[h], grip[h], held[h]);
        if (k < 0 || p.third >= 0) continue;
        if (const int t = b.MenuOf(SlotOf(h, k)); t >= 0) p = {t, h};
    }
    return p;
}

// ---- T035/T036 headset fixes (2026-11-16, Jeff's test 1:06-1:29 PM; vault Tickets-quick-cast-radial log) ----
// Bug 1: a sub-menu's button pressed while the open ring's main page has a seat or a center third under the laser is the ring's
// press, as every other button's (a tap casts that seat; a hold of Hold to assign, about 1 s, puts the seat on this button, so the
// menu's own button can always be rebound). Anywhere else (the wheel shut, another page, the laser off the seats) it opens its
// sub-menu as before.
inline bool MenuPressOpens(bool wheelOpen, bool ringMain, Kind under) {
    return !(wheelOpen && ringMain && (under == Kind::Seat || ThirdOf(under) >= 0));
}
// Bug 4: a Lab menu (Glyphs or Gestures menu, Quick cast, Army X menu) put on a button clears that button's game skill (upstream's
// own clear, as Draw glyph's pick). That skill is never dropped without a word: it moves to the menu's old button when that one is
// free now (a swap), else into Quick cast's ring (quickCastTakes: Quick cast On and a skill of this hero's catalog), else it is
// said taken off. was: the skill the button held (-1 none); oldSlot: the menu's button before (-1 none) and what it holds now.
enum class MovedTo { Nothing, OldButton, QuickCast, Dropped };
inline MovedTo WhereMoved(int was, int newSlot, int oldSlot, int oldSlotSkill, bool oldSlotGlyph, bool quickCastTakes) {
    if (was < 0) return MovedTo::Nothing;
    if (oldSlot >= 0 && oldSlot < skills::Count && oldSlot != newSlot && oldSlotSkill < 0 && !oldSlotGlyph) return MovedTo::OldButton;
    return quickCastTakes ? MovedTo::QuickCast : MovedTo::Dropped;
}
// The retired Quick cast menu button (T036's exp.ring_wheel.quick_button, slot + 1) becomes Quick cast's own button
// (exp.ideas.quick_radial.button) once, unless Quick cast has a button already or it is a trigger (review N2: a trigger stored
// before review F1 stays the attack): the stored value it gets (0: none to move).
inline float MovedQuickButton(float oldMenuButton, float quickCastButton) {
    const long slot = std::lround(oldMenuButton) - 1;
    if (slot == skills::TriggerSlot(0) || slot == skills::TriggerSlot(1)) return 0.f;
    return oldMenuButton >= .5f && quickCastButton < .5f ? oldMenuButton : 0.f;
}
// Review F1 (2026-11-16 fresh review): Quick cast put on a button from the center's third, why not (nullptr: it may). The trigger
// first, as every sub-menu (review F1 of T036: the attack, and the ring's own click), then the switch, the class, Draw glyph.
// slot: the hand's button (0 trigger .. 4 grip + secondary).
inline const char* QuickBindRefusal(int slot, bool on, bool classKnown, bool drawsGlyph) {
    if (slot <= 0) return "never a trigger (the attack, and the ring's click)";   // as ring_wheel_pages.h MenuButtonAllowed
    if (!on) return "Quick cast is Off (Lab > Experiment: Ideas > Combat > Quick cast radial)";
    if (!classKnown) return "this class keeps upstream's buttons";
    if (drawsGlyph) return "that button draws glyphs (put a skill on it first)";
    return nullptr;
}

// The Quick cast third's word when pointed (review N1): why nothing opens, or nullptr ("Trigger to open").
inline const char* QuickThirdWord(bool on, bool classKnown) { return !on ? "Off in the Lab" : !classKnown ? "Not for this class" : nullptr; }
// Review N4 (2026-11-16): an Applied ring bind that puts a skill on a button (a game skill or a commander order: ids under 9000),
// so a Lab menu on that button lets go of it. Never a sub-menu's or Lab entry's bind (Draw glyph 9000, Lab entries 9001 on, a clear).
inline bool SkillTakesButton(int skill, int menu, bool draw) { return menu < 0 && !draw && skill >= 0 && skill < 9000; }

// The hold's haptic ticks at 25 / 50 / 75 % (L2, eval #10): how many quarter marks the fill crossed since last frame.
inline int TicksCrossed(float before, float now) {
    int n = 0;
    for (const float mark : {.25f, .5f, .75f}) if (before < mark && now >= mark) ++n;
    return n;
}

// ---- R6Q: a quick cast waiting for the wheel to shut, then started through upstream's glyph press ----
// The same steps as transport's recognised glyph (its stroke path): the skill known and available, the press free, then
// glyphcast::Dispatch (its hand slot, else the right trigger's record, parked there first). `CastT` is glyphcast::Cast in
// the game and a fake in the tests; nothing here touches a binding (a cast never changes a button's skill).
struct CastWait { bool on = false; int skill = -1, owner = -1, hand = 0, slot = -1; double since = 0; };
struct CastResult { bool started = false; const char* refused = nullptr; glyphcast::How how = glyphcast::How::None; };
template <class CastT, class ParkF>
CastResult StartCast(CastWait& w, CastT& cast, const interaction::Snapshot& s, bool labOn, bool fresh, bool active, bool wheelShut, double nowS, double waitedMs, ParkF&& park) {
    CastResult r;
    if (!labOn) { w = {}; return r; }   // the Lab went Off with a cast queued: it never fires (perf R6Q)
    if (!w.on) return r;
    if (!wheelShut) {   // the cast waits for upstream's close (cell 8 the frame before)
        if (waitedMs > 1000) { r.refused = "the wheel did not close"; w = {}; }
        return r;
    }
    const interaction::Skill* k = nullptr;
    for (unsigned i = 0; i < s.skillCount && i < s.catalog.size(); ++i) if (s.catalog[i].id == w.skill && s.catalog[i].owner == w.owner) { k = &s.catalog[i]; break; }
    if (!fresh || !active) r.refused = "the game's skill list is not fresh";
    else if (!k || !k->known) r.refused = "not this hero's skill";
    else if (!k->available) r.refused = "the skill is unavailable now";
    else if (cast.Busy()) r.refused = "the last glyph or quick cast is still casting";
    if (!r.refused) {
        std::array<glyphcast::Slot, skills::Count> slots{};
        for (int i = 0; i < skills::Count; ++i) { const auto& b = s.skills[size_t(i)]; slots[size_t(i)] = {b.id, b.owner, b.known, b.available}; }
        const bool record = s.glyphRecord >= 0;
        const auto plan = glyphcast::Dispatch(w.skill, w.owner, slots, record ? s.glyphSkill : -2, record ? s.glyphOwner : -2);
        r.how = plan.how;
        if (plan.how == glyphcast::How::None) r.refused = "no way to press it";
        else if (plan.how == glyphcast::How::Park && !record) r.refused = "the right trigger's skill record is unknown";
        else if (plan.how == glyphcast::How::Park && !park(w.skill, w.owner)) r.refused = "the last skill change is still running";
        else { cast.Begin(plan, w.skill, w.owner, nowS); r.started = true; }
    }
    w = {};
    return r;
}
// The wheel's edges and the close's reason (bug 9 logging; review-bug9 M1), once a frame at the top of the Input seam
// (after HandControls, before interactions.Step). Wrote: what the Lab released to upstream later in that same seam; upstream
// acts on it in that frame's interactions.Step (the center closes the wheel there, transport closes HandControls), so the
// close shows on the next frame's edge, and that next Frame reads it. A write is kept one frame only. First match wins:
// the Lab's write, a stick click (left first), the frame's safety reason (ring_wheel_close.h), a new session, else "upstream",
// which the Lab's log line turns into a named reason or `unknown` with its source.
class WheelEdges {
    int was = -1;
    uint64_t session = 0;
    const char* wrote = nullptr;
public:
    enum Kind { None, Opened, Switched, Closed };
    struct Edge { Kind kind = None; std::string reason; };
    Edge Frame(int wheelHand, const bool click[2], const char* safety, uint64_t sessionNow) {
        Edge e;
        if (was >= 0 && wheelHand < 0) {
            e.kind = Closed;
            e.reason = wrote ? wrote : click[1] ? "stick-left" : click[0] ? "stick-right" : safety ? safety
                     : sessionNow != session ? "session" : "upstream";
        } else if (was < 0 && wheelHand >= 0) e.kind = Opened;
        else if (was >= 0 && wheelHand >= 0 && wheelHand != was) e.kind = Switched;
        was = wheelHand; session = sessionNow; wrote = nullptr;
        return e;
    }
    void Wrote(const char* why) { wrote = why; }
    // Audit 2026-11-10 bug 2: the wheel's input was cleared outside a focused frame (transport ClearInput: the headset
    // unfocused past the blip, the session ending, a failed sync or copy), which closes the wheel there, so the close is that
    // moment's and not the next focused frame's (it came late, or never when the game ended first). The next Frame finds
    // the wheel closed already: one line a close. shutBefore: upstream had closed it on the last focused frame, after that
    // frame's Frame call, so the clear is not its reason (the Lab's write, else "upstream"). Only then is the write the
    // reason (review N1): no Frame runs between the last focused frame and the clear, so a write upstream ignored is still
    // here, and with the wheel still open at the clear it closed nothing.
    Edge Cleared(const char* why, bool shutBefore) {
        Edge e;
        if (was >= 0) { e.kind = Closed; e.reason = shutBefore ? (wrote ? wrote : "upstream") : why; }
        was = -1; wrote = nullptr;
        return e;
    }
    bool Open() const { return was >= 0; }
    void Off() { was = -1; wrote = nullptr; }   // the Lab's Off edge (the caller logs lab-off)
};
// Audit 2026-11-10 bug 3: a footer tab clicked while upstream's own page shows (Map, Equipment, or any page while the ring
// is not drawn). The Lab writes nothing there, so the close was named upstream-page. Upstream's engine closes the wheel on
// a trigger press over Inventory, VR Settings and the DEV window's tab (interactions.h Engine::Step; no editor page has
// them). Stepped once a frame with the hover and trigger that engine read; the press edge is the engine's own (over .55 to
// press, .2 to let go). The caller hands the name to WheelEdges::Wrote, which keeps it one frame: it names only a close on
// that frame, and every other close on those pages stays upstream-page.
inline const char* FooterCloseName(int cell, bool developer) {
    if (cell == 11) return "footer-inventory";
    if (cell == 10) return "footer-vr-settings";
    return nullptr;   // the developer tab (13) switches to upstream's developer page: the wheel stays open
}
class FooterClick {
    bool down = false;
public:
    const char* Step(bool upstreamPage, int page, int hover, float trigger, bool developer) {
        const bool now = trigger > (down ? .2f : .55f);
        const bool edge = now && !down;
        down = now;
        return upstreamPage && edge && !interaction::Editing(interaction::MenuPage(page)) ? FooterCloseName(hover, developer) : nullptr;
    }
};
}  // namespace framevr::ringwheel
