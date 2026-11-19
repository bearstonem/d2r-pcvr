#pragma once
// Lab Ring wheel (T025, build steps R4-R5): what the compose pass draws, as plain data (plan E3, E4; look L1-L3;
// the design notes sections 2-3). Pure and Windows-free: the instance list a frame draws (shapes, icons, glyphs), when a
// frame composes at all, the 3-slot ring that never waits on the GPU, the atlas cell grid and the content rectangle inside
// the swapchain. vr/ring_wheel_compose.cpp draws the list; ring-wheel-preview renders it on WARP (the design notes 14-16).
//
// Every instance is one quad in canvas px (steamvr/ring_wheel.h); its recipe draws only inside its own quad, with no loop
// that grows with the input. Colors are premultiplied by the shader. A closed wheel builds no instance at all.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "ring_wheel.h"
#include "ring_wheel_badges.h"
#include "ring_wheel_orders.h"
#include "ring_wheel_skill_fx.h"
#include "ring_wheel_text.h"
#include "ring_wheel_tree_art.h"   // T025: the tree marks' painted pictures (Lab > Ring wheel > Layers > Tree icon)
#include "ring_wheel_glyph_art.h"   // the Glyphs page's shapes and the center's sigils, painted (Lab > Ring wheel > Center > Glyph icon)

namespace framevr::ringwheel {
constexpr int MaxInstances = 320, MaxText = 512, MaxDraw = MaxInstances + MaxText;   // R8 match stage 1: up to 2 rest glows and 2 over-face passes an icon   // fixed arrays (no growth per frame)
enum Recipe : uint32_t {
    RecipeIcon = 0,    // the icon atlas cell, cut to a circle (1.5 px feather), a rim of `param` px in the tint; param < 0: the art in gray;
                       // |param| >= IconDisc (T036): our dark disc under the art (a clean icon), the rim |param| - IconDisc
    RecipeRing = 1,    // a circle stroke `param` px wide at the quad's edge; u0 > 0: dashed, u0 px a period, 60% drawn
    RecipeDisc = 2,    // a soft disc, `param` px of feather
    RecipeRect = 3,    // the whole quad (the split's hairline)
    RecipeGlyph = 4,   // the glyph atlas: fill (red) in the tint over its dark outline (green)
    RecipeArc = 5,     // a ring stroke `param` px wide, clockwise from the top to the fill in u0 (0-1): the hold ring
    RecipeFx = 6,      // R8: an effect, param = kind + 256 x mode; u0 = time s, v0 = seed, u1 = the rest event's age ms (-1 none). Mode 0: a rest
                       // glow under the face (RestKind, quad radius RestCapPx x the icon's scale: the rest cap by geometry); 1: the hover
                       // effect (Element, r + 70; u1 = u0); 2: a rest pass over the face (lightning arcs, steel glints; same quad as 0)
    RecipeCrawl = 7,   // R8: the lightning face crawl over an icon's face; u0 = u1 = its 70 ms step
    RecipeFace = 8,    // R8: an icon face copy moved by (r, g) px from its seat, cut to the seat's circle (icon art motion)
    RecipeRunes = 9,   // R7c: the center's rune band (kit runeRing), runes at the quad's radius - 6.5 (mid gap); param = the rune count; u0 = u1 = the
                       // hash seed (the kit's round(r)); v0 = v1 = the dimmed half (0 none, 1 top, 2 bottom, at 55%)
    RecipeTreeGlyph = 10,   // R7c: a tree mark's glyph (kit glyph()), param = its kind (MarkGlyph: TreeGlyphOf); u0 = u1 = its size r
    RecipeSeat = 11,   // R8 match stage 1: a seat's icon as the kit's drawIcon: the art cut at r = half-size - SeatRingPx, its rim shade
                       // (0.6 r to r, 0 to 55% black), a 3 px #070504 line on r + 1, the rim `param` px wide on r + 1.5 + param / 2 in the
                       // tint (2: the tree rim on r + 2.5; 3: the hover ring on r + 3); param < 0: the shade and the rings only (face copies)
    RecipeBadge = 12,   // feedback 1 item 3: a bound-button badge (kit drawQuestBadge), param = BadgeKind; u0 = u1 = its body radius, v0 = v1 = the
                        // square's corner; its 7 px dark halo fills the rest of the quad. RecipeGlyph with param 1: the fill only, no outline (the coin)
    RecipeSector = 13,  // T036: a third of the center (a 120 deg ring sector), param = the third (0 Glyphs, 1 Gestures, 2 Quick cast), u0 = u1 = its
                        // inner radius; the quad's radius its outer; a 2 px edge in the tint over a fill at 30% (tint.a: the whole)
    RecipeFrame = 14,   // T036 Element frame: our own static band round a seat, param = its Element, u0 = u1 = the band's inner radius, v0 =
                        // v1 = its width (8 px at 1x); fire licks, ice shards, a lightning zigzag, poison bubbles, else rune ticks
    RecipeSquare = 15,  // T025 a tree mark's painted square: the atlas cell as a rounded square (the face is the quad less SeatRingPx each side), the
                        // seat's rim shade-less look: a 3 px #070504 line on 1 px out, the rim `param` px wide in the tint (the accent) on 1.5 px out
};
// T036 the skill circle's layers (Lab > Experiment: Ring wheel > Layers): each its own switch, drawn in this order. Float is the
// Animation rows' (Anim::floats). fx: the rest glow under the face, its passes over the face and the lightning crawl (today's
// look; the Animation layer). The Scene default is today's look so a caller that sets none draws as before.
struct SeatLayers {
    bool icon = true, ring = true, frame = false, fx = true;
    bool operator==(const SeatLayers&) const = default;
};
// RecipeSeat's param flags (T036), on top of the ring's width: param = +-(width + 16 x flags).
constexpr int SeatNoArt = 1, SeatNoRing = 2;
constexpr float IconDisc = 16;   // RecipeIcon's param offset: our dark disc under the art
constexpr float HoldNameY = -52;   // designer gate D1: the held order's short name, one line over the button's (the circle allows 207 px there)
constexpr float HoldTopY = 42, HoldLineY = 66, HoldLineMaxW = 192.8f;   // #3 (designer): the hold's lines under the center; a skill's name + 5 fits that width
constexpr float SeatRingPx = 4.5f;            // RecipeSeat: past the face, room for the hover ring's outer edge (r + 4.5)
constexpr float RestCapPx = 1.5f * IconPx / 2;   // r8-match-spec B2 (designer, 2026-11-07: 1.5 r): no rest effect pixel past 33 px from its icon's center
struct Instance {
    float cx = 0, cy = 0, w = 0, h = 0, rot = 0;   // canvas px; w, h the quad's full size; rot radians about its center
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;          // atlas rectangle (RecipeIcon, RecipeGlyph)
    float r = 1, g = 1, b = 1, a = 1;              // tint (straight; the shader premultiplies), a = opacity
    uint32_t recipe = RecipeIcon;
    float param = 0;
};
static_assert(sizeof(Instance) == 60, "the shader's StructuredBuffer stride (vr/ring_wheel_compose.cpp Inst)");

// ---- The atlases (plan E4): textures made once at the largest scale; the content packed at the render scale ----
constexpr int AtlasTexPx = 1536, IconMips = 4, AtlasCells = 9;   // 81 icon cells
constexpr int ExtraIcons = 17;   // round 2026-11-08 item 6: cells past the seats for the Gestures page's cards (ring_wheel_pages.h)
static_assert(MaxSeats + ExtraIcons <= AtlasCells * AtlasCells, "every seat and every page card fits the icon atlas");
// R8 match stage 3a: the game's effect textures (upstream's spell sprite cache) packed into one R8 atlas, FxTexPx square,
// FxTexMips mips; the cells are the compose shader's FxFire03 / FxFire04 / FxSmoke / FxArcs / FxGlow.
constexpr int FxTexPx = 1024, FxTexMips = 4;
inline int ScalePct(int pct) { return std::clamp(pct, 100, 150); }
inline int CellPx(int pct) { return 112 * ScalePct(pct) / 100; }   // 96 px of art at 100% and an 8 px gap each side
inline int ArtPx(int pct) { return 96 * ScalePct(pct) / 100; }
inline void CellRect(int cell, int pct, float& u0, float& v0, float& u1, float& v1) {
    const int c = CellPx(pct), a = ArtPx(pct), x = (cell % AtlasCells) * c + (c - a) / 2, y = (cell / AtlasCells) * c + (c - a) / 2;
    u0 = float(x) / AtlasTexPx; v0 = float(y) / AtlasTexPx; u1 = float(x + a) / AtlasTexPx; v1 = float(y + a) / AtlasTexPx;
}

// L1 colors (style guide tokens), straight RGB 0-1.
struct Rgb { float r, g, b; };
constexpr Rgb Gold{.627f, .490f, .278f}, GoldLight{.910f, .780f, .494f}, HoverRing{.949f, .875f, .651f};
constexpr Rgb Parchment{.949f, .922f, .847f}, Muted{.788f, .733f, .627f};
constexpr Rgb TreeAccent[Trees] = {{.910f, .780f, .494f}, {.878f, .416f, .290f}, {.486f, .784f, .659f}};   // by position: no class known
constexpr Rgb Hex(uint32_t h) { return {float((h >> 16) & 255) / 255.f, float((h >> 8) & 255) / 255.f, float(h & 255) / 255.f}; }
// A tree's colors from the kit's tree data (skills.js / skills-more.js trees[i], page i, with every class page's final values
// folded in by the designer, 2026-11-07): accent = the tree's identity (its mark's ring); rim = its seats' icon rims, gold
// when the tree has none (the kit: tree.rim || gold). Read through TreeOrder like the glyph (TreeGlyphOf) and the name, so
// all three name the same tree (designer, R7c: the Necromancer's bone sat in the by-position red ring). Unknown class: the
// plain accents by position for both.
struct TreeColors { uint32_t accent, rim; };   // rim 0: none (gold)
inline TreeColors KitTreeColors(int skillClass, int page) {
    static constexpr TreeColors table[8][Trees] = {
        {{0xC89060, 0xC89060}, {0xB4A0F0, 0xB4A0F0}, {0xF2D04A, 0xE0B020}},   // Amazon: Bow and Crossbow, Passive and Magic, Javelin and Spear
        {{0xE8591A, 0}, {0xF2D04A, 0xE0B020}, {0x7FC8F2, 0x7FD6F2}},          // Sorceress: Fire, Lightning, Cold
        {{0xB060C0, 0xB060C0}, {0xA0D890, 0xA89F84}, {0xD8584A, 0xD0503E}},   // Necromancer: Curses, Poison and Bone, Summoning
        {{0xE8C77E, 0xE8C77E}, {0xE06A4A, 0xE06A4A}, {0x7CC8A8, 0x7CC8A8}},   // Paladin: Combat, Offensive Auras, Defensive Auras
        {{0xD06040, 0xC8704A}, {0xB8B0A0, 0}, {0xE0A040, 0xE8A848}},          // Barbarian: Combat, Masteries, Warcries
        {{0x8CC860, 0x7CB85A}, {0xB4503C, 0xA84A36}, {0xF09040, 0xE8833A}},   // Druid: Summoning, Shape Shifting, Elemental
        {{0xC8B060, 0}, {0x8E7CC3, 0}, {0xE07A3A, 0}},                        // Assassin: Traps, Shadow Disciplines, Martial Arts
        {{0xF04A47, 0xF04A47}, {0xC578DE, 0xA055B7}, {0xF48C02, 0xD96A02}},   // Warlock: Demon, Eldritch, Chaos
    };
    return skillClass >= 0 && skillClass < 8 && page >= 0 && page < Trees ? table[skillClass][page] : TreeColors{0, 0};
}
inline Rgb TreeAccentOf(int skillClass, int position) {
    if (position < 0 || position >= Trees) return Gold;
    if (skillClass < 0 || skillClass >= 8) return TreeAccent[position];
    return Hex(KitTreeColors(skillClass, PageAt(skillClass, position)).accent);
}
inline Rgb TreeRimOf(int skillClass, int position) {
    if (position < 0 || position >= Trees) return Gold;
    if (skillClass < 0 || skillClass >= 8) return TreeAccent[position];
    const uint32_t rim = KitTreeColors(skillClass, PageAt(skillClass, position)).rim;
    return rim ? Hex(rim) : Gold;
}
inline float Seed(int id) { return float(SeedOf(id) % 1000u); }
// T036 Element frame (the mockup's layerFrame): the band's width at 1x and each element's color; the shape is the shader's.
constexpr float FrameBandPx = 8;
inline Rgb FrameColor(Element e) {
    switch (e) {
    case Element::Fire: return Hex(0xF08A30);
    case Element::Cold: return Hex(0x9FDCF5);
    case Element::Light: return Hex(0xF2D04A);
    case Element::Poison: return Hex(0x8CD860);
    case Element::Arcane: return Hex(0xB4A0F0);
    case Element::Holy: return Hex(0xE8C77E);
    case Element::Aura: return Hex(0xF0E0A0);
    case Element::Shadow: return Hex(0x8E7CC3);
    default: return Hex(0xB8B0A0);
    }
}

// ---- R8 match stage 1: the rest glow per skill (r8-match-spec sections 3 and 6.1) ----
// A seat's rest glow: up to 2 kinds (the kit's `el` list, steamvr/ring_wheel_skill_fx.h), each with a color (0: the kind's own).
struct RestGlow { RestKind kind[2] = {RestKind::None, RestKind::None}; uint32_t rgb[2] = {0, 0}; };
// The skill's glow from the kit table; a skill the table lacks: its tree's element (ElementOf), one kind.
inline RestGlow RestGlowOf(int id, int skillClass, int position) {
    RestGlow g;
    if (const SkillFx* f = SkillFxOf(id)) { g.kind[0] = f->kind[0]; g.kind[1] = f->kind[1]; g.rgb[0] = f->rgb[0]; g.rgb[1] = f->rgb[1]; }
    else g.kind[0] = RestKind(ElementOf(skillClass, position));
    return g;
}
inline bool ValidKind(RestKind k) { return uint32_t(k) < uint32_t(RestKindCount); }
// The element a seat's hover effect and face motion use (stage 2 replaces this): the kit's recipe = the first kind that is not
// a charge (RestKindElement: blood reads as steel, multi as fire, a class kind its nearest element); none: steel.
inline Element ElementOfGlow(const RestGlow& g) {
    for (const RestKind k : g.kind)
        if (ValidKind(k) && RestKindElement[uint32_t(k)] < uint8_t(Element::Count)) return Element(RestKindElement[uint32_t(k)]);
    return Element::Phys;
}
inline bool HasKind(const RestGlow& g, RestKind k) { return g.kind[0] == k || g.kind[1] == k; }
// R8 match stage 2 (spec 3 "Pointed effect", chain level 4): the pointed effect's element default (kit hover-defaults.js), a
// rest kind 0-11, or None: no drawn default (the kit's level 4 draws only the hover ring). The kit's recipe: the first entry
// that is not a charge (else the first); a class kind plays its nearest element (RestKindElement), one with none ('tint',
// 'demon': designer S2-2, a class charge) hands over to the other entry; nothing left: a charge's rings, else None.
// rgb: the color the aura and charge rings take (the entry's own, else the kit's #F0E0A0).
constexpr uint32_t HoverAuraRgb = 0xF0E0A0;
inline RestKind HoverKindOf(const RestGlow& g, uint32_t* rgb = nullptr) {
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < 2; ++k) {
            const RestKind kind = g.kind[k];
            if (!ValidKind(kind) || (pass == 0 && kind == RestKind::Charge)) continue;
            const uint32_t v = uint32_t(kind);
            const RestKind e = v < 12 ? kind : RestKindElement[v] < uint8_t(Element::Count) ? RestKind(RestKindElement[v]) : RestKind::None;
            if (e == RestKind::None) continue;
            if (rgb) *rgb = (e == RestKind::Aura || e == RestKind::Charge) && g.rgb[k] > 0xFF ? g.rgb[k] : HoverAuraRgb;   // <= 0xFF: a curse shape, no color
            return e;
        }
    if (rgb) *rgb = HoverAuraRgb;
    return HasKind(g, RestKind::Charge) ? RestKind::Charge : RestKind::None;
}
// The face motion's element default (kit WHEEL_FACE_DEFAULT, keyed by the first entry): steel lunges, shadow rises in;
// others none (Element::Count).
// The pointed effect's chain stage for the log (spec 7.3 S7): 3 the game-texture default, 4 the drawn one, 0 none.
inline int FxStageOf(RestKind hover, bool fxTex) {
    if (hover == RestKind::None) return 0;
    return fxTex && (hover == RestKind::Fire || hover == RestKind::Poison || hover == RestKind::Light) ? 3 : 4;
}
inline Element FaceElementOf(const RestGlow& g) {
    return g.kind[0] == RestKind::Phys ? Element::Phys : g.kind[0] == RestKind::Shadow ? Element::Shadow : Element::Count;
}
// The multi default's phases (kit: fire, cold, lightning, 1.2 s each; stage 2: the next fades in over the last 200 ms): the
// weight of element e (0 fire, 1 cold, 2 lightning) at ageMs.
inline float MultiWeight(int e, double ageMs) {
    const double q = std::max(0.0, ageMs) / 1200, f = q - std::floor(q);
    const int cur = int(std::floor(q)) % 3, next = (cur + 1) % 3;
    const float in = float(std::clamp((f - (1 - 200.0 / 1200)) / (200.0 / 1200), 0.0, 1.0));
    return e == cur ? 1 - in : e == next ? in : 0.f;
}
// Pointed reach (spec 7.3): every pointed effect inside 90 px of its icon's center, a 15 px soft edge; lightning reaches the
// two nearest shown icons whose rim side (22 px short of their center) lies inside it.
constexpr float PointedReachPx = 90, PointedEdgePx = 15, BoltStopPx = 22;
inline bool CrawlsAtRest(const RestGlow& g) { return HasKind(g, RestKind::Light) || HasKind(g, RestKind::Multi); }
// A kind drawn over the face as well (mode 2): the lightning arcs (kit: after the rim), the steel glints (on the face).
inline bool OverFace(RestKind k) { return ValidKind(k) && RestKindOver[uint32_t(k)]; }
// The panel-wide rest events (spec 7.3, critic S6, vr-ux cap 2): steel glints (720 ms), charge orbs (1 s) and the lightning
// burst at rest (300 ms). Every icon with one owns a slot on one shared clock: slots 334 ms apart (at most 3 starts a second,
// never two within 250 ms), every event at most 1 s long (so at most 3 alive), one cycle of max(slots x 334 ms, the period)
// (6 s, the kit's glint; the crawl row's seconds when longer). The age in ms of this icon's event, or -1.
constexpr double RestEventSlotMs = 334, RestEventMaxMs = 1000, RestEventPeriodMs = 6000;
constexpr double RestGlintMs = 720, RestChargeMs = 1000, RestBurstMs = 300;
inline double RestEventCycleMs(int events, double periodMs) {
    return std::max(double(events), std::ceil(std::max(periodMs, RestEventPeriodMs) / RestEventSlotMs)) * RestEventSlotMs;
}
inline double RestEventAge(int index, int events, double tMs, double periodMs) {
    if (index < 0 || index >= events) return -1;
    const double c = RestEventCycleMs(events, periodMs), p = std::fmod(std::fmod(tMs - index * RestEventSlotMs, c) + c, c);
    return p < RestEventMaxMs ? p : -1;
}
inline bool HasRestEvent(const RestGlow& g, int crawlS) {
    for (const RestKind k : g.kind) if (ValidKind(k) && RestKindEvent[uint32_t(k)]) return true;
    return crawlS > 0 && CrawlsAtRest(g);
}

// ---- Glyph quads ----
inline void PushGlyph(Instance* out, int& n, int max, const text::Glyph& gl, float cx, float cy, float rot, Rgb c, float a) {
    if (n >= max || a <= 0 || gl.w <= 0) return;
    out[n++] = {cx, cy, gl.w, gl.h, rot, gl.u0, gl.v0, gl.u1, gl.v1, c.r, c.g, c.b, a, RecipeGlyph, 0};
}
// A line centered on (cx, y), y its middle.
inline void EmitLine(const text::Metrics& m, text::Style st, std::string_view s, float cx, float y, Rgb c, float a, Instance* out, int& n, int max) {
    float pen = cx - text::Width(m, st, s) / 2;
    const float base = text::Baseline(m, st, y);
    for (size_t i = 0; i < s.size();) {
        const auto& gl = m.g[size_t(st)][size_t(text::SlotOf(text::Next(s, i)))];
        PushGlyph(out, n, max, gl, pen + gl.ox + gl.w / 2, base + gl.oy + gl.h / 2, 0, c, a);
        pen += gl.advance;
    }
}
// A line on a circle of radius r round (cx, cy), centered on angle mid; below the middle it reads upright (kit textOnArc).
inline void EmitArc(const text::Metrics& m, text::Style st, std::string_view s, float cx, float cy, float r, float mid, Rgb c, float a,
                    Instance* out, int& n, int max) {
    const bool bottom = std::sin(mid) > .2f;
    float at = mid + (bottom ? 1 : -1) * text::Width(m, st, s) / r / 2;
    const float bOff = text::Baseline(m, st, 0);
    for (size_t i = 0; i < s.size();) {
        const auto& gl = m.g[size_t(st)][size_t(text::SlotOf(text::Next(s, i)))];
        const float cw = gl.advance / r, ac = bottom ? at - cw / 2 : at + cw / 2;
        const float rot = bottom ? ac - Pi / 2 : ac + Pi / 2, co = std::cos(rot), si = std::sin(rot);
        const float lx = -gl.advance / 2 + gl.ox + gl.w / 2, ly = bOff + gl.oy + gl.h / 2;   // the glyph's center, the character's frame
        PushGlyph(out, n, max, gl, cx + r * std::cos(ac) + lx * co - ly * si, cy + r * std::sin(ac) + lx * si + ly * co, rot, c, a);
        at += bottom ? -cw : cw;
    }
}

// ---- The footer (feedback 1 item 4): upstream's tabs, drawn from the numbers the hit test (FooterAt) uses ----
struct FooterView { bool on = false; interaction::MenuPage page = interaction::MenuPage::Spells; bool developer = false, fullWidth = false, handWheel = true; };
// Upstream's labels (interaction_ui.cpp Paint, the footer[6] line and the editors' two lines; ring-wheel-test checks those
// lines are still upstream's, so a rename there fails the test).
inline const char* FooterLabel(int i, interaction::MenuPage page, bool developer, bool handWheel) {
    using P = interaction::MenuPage;
    if (page == P::EditSlots) { constexpr const char* e[3] = {"BACK", "", "DONE"}; return i >= 0 && i < 3 ? e[i] : ""; }
    if (page == P::EditSkills || page == P::EditItems) { constexpr const char* e[3] = {"PREVIOUS", "NEXT", "BACK"}; return i >= 0 && i < 3 ? e[i] : ""; }
    const bool elsewhere = page == P::Equipment || page == P::Map || page == P::Developer || page == P::Glyphs || page == P::Motions;
    switch (i) {
    case 0: return elsewhere ? "SPELLS" : "EQUIPMENT";
    case 1: return "MAP";
    case 2: return "VR SETTINGS";
    case 3: return handWheel ? "INVENTORY" : "EDIT SKILLS";
    case 4: return "DEV";
    case 5: return "LAB";
    default: return "";
    }
}
struct FooterBox { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; int cell = -1; };
// Tab i's box in canvas px: upstream's FooterBounds over the footer's drawn width, exactly as FooterAt maps the laser.
inline FooterBox FooterBoxAt(int i, interaction::MenuPage page, bool developer, bool fullWidth) {
    const int tabs = interaction::FooterCount(page, developer);
    const float w = FooterWidth(fullWidth, tabs), x = (CanvasPx - w) / 2;
    const auto b = interaction::FooterBounds(i, page, developer);
    return {x + b.left * w, b.top * CanvasPx, x + b.right * w, b.bottom * CanvasPx, interaction::FooterCell(i, page)};
}
// How a footer label sits in its tab at one size: one line when it fits upstream's DrawText box (FooterMargin inside each
// side: the 2 px rim and the 3 px gap, so no letter touches the rim, review-r8 I4-2); else, with a space, two lines split at
// the space nearest its middle (upstream's DT_WORDBREAK: "VR / SETTINGS"); fits: every line inside that box. split: the
// space's index, npos one line.
constexpr float FooterMargin = 5;
struct FooterFit { text::Style style = text::Footer17; size_t split = std::string_view::npos; bool fits = false; };
inline FooterFit FooterFitAt(const text::Metrics& m, text::Style s, std::string_view label, float boxW) {
    const float one = text::Width(m, s, label), room = boxW - 2 * FooterMargin;
    if (one <= room) return {s, std::string_view::npos, true};
    size_t best = std::string_view::npos;
    for (size_t i = label.find(' '); i != std::string_view::npos; i = label.find(' ', i + 1))
        if (best == std::string_view::npos || std::max(i, label.size() - i) < std::max(best, label.size() - best)) best = i;
    if (best != std::string_view::npos &&
        std::max(text::Width(m, s, label.substr(0, best)), text::Width(m, s, label.substr(best + 1))) <= room) return {s, best, true};
    return {s, std::string_view::npos, false};
}
// The row's one size (L1 Footer, designer 2026-11-07: never 22 next to 17): the largest whole px from 22 down to 15 at which
// every label of this row (its page, tab count and width) fits its tab inside the margin; 15 when none does. A pure
// function of the fonts and the row, so it is the same every frame (at most 8 sizes x 6 labels of width sums: no cache).
inline text::Style FooterRowStyle(const text::Metrics& m, interaction::MenuPage page, bool developer, bool fullWidth, bool handWheel) {
    for (int px = text::FooterMaxPx; px > text::FooterMinPx; --px) {
        const text::Style s = text::FooterStyleOf(px);
        bool all = true;
        for (int i = 0; all && i < interaction::FooterCount(page, developer); ++i) {
            const char* label = FooterLabel(i, page, developer, handWheel);
            const auto b = FooterBoxAt(i, page, developer, fullWidth);
            all = !*label || FooterFitAt(m, s, label, b.x1 - b.x0).fits;
        }
        if (all) return s;
    }
    return text::Footer15;
}

// What a frame shows (the engine's step, the animation rows, the time, the atlases' data).
struct Scene {
    bool open = false;
    float wheelAlpha = 1;          // the open / close / hit fades (R8); 0 = draws nothing
    const Layout* layout = nullptr;
    StepOut step{};
    Anim anim{};
    double tMs = 0;
    bool hasMotions = true;
    float grow = -1;               // the center's grow 0-1 (r 80 + 56 g, L3's 220 ms ease, R8); -1: 0 or 1 from step.centerGrown
    int atlasPct = 100;            // the scale the icon atlas was packed at
    const int16_t* cells = nullptr;           // per seat index: its icon cell, -1 missing (the initials disc); null: cell = index
    const text::Metrics* metrics = nullptr;   // null: no text (the glyph atlas not built yet)
    const char* const* initials = nullptr;    // per seat index: a missing icon's initials (1-2 letters)
    const text::Blurb* blurb = nullptr;       // the pointed skill's blurb (BlurbCache), drawn once the center is 55% grown
    std::array<const char*, Trees> treeNames{};   // on the outer arc at r 313 (Trees shown; feedback 1 nit, was 308: COLD SPELLS touched a floating icon)
    uint32_t revision = 0;         // the caller's data revision: bumped when the layout, the atlas or a name changes in place (review R5 K6)
    // R6Q2, a button held on a skill past the tap limit (L2 Hold, L3 "Center while a button is held"): the fill 0-1 (-1: no
    // hold), the held seat, the slot's name ("Right trigger") and the status line ("Replaces Fire Wall", "Hold to assign").
    DrawBadge drawBadge{};   // #3: the wheel hand's Draw glyph button (main page only; off: none)
    float holdFrac = -1; int holdSeat = -1;
    const char* holdSlot = nullptr; const char* holdStatus = nullptr; const char* holdTop = nullptr; bool holdGreen = false;   // holdTop: the first of two lines (#3)
    // Designer gate D1 (2026-11-14): the glyph chooser, a button held on an order: its short name ("Spearhead Assault") over
    // the button's, and that order stays the marked one (seven orders share one picture). Null everywhere else.
    const char* holdName = nullptr;
    // R7 (L3): the pointed half's count line ("8 shapes, right hand", "11 of 14 on") under its label with "Trigger to open";
    // the glyph chooser ("Choose a skill" state): the halves' labels (Back / Clear glyph), its title in the grown center, and
    // per seat 1 = not on upstream's glyph picker list (drawn at 25%).
    const char* topCount = nullptr; const char* bottomCount = nullptr;
    const char* topLabel = nullptr; const char* bottomLabel = nullptr;
    // R8: per seat its element (ringwheel::Element) and its index among the lightning icons (-1 none) for the crawl's
    // stagger; the fight dim (all but the pointed icon) and the time the pointed icon was first pointed (its effects' age).
    const uint8_t* elements = nullptr;
    const int8_t* crawlIndex = nullptr; int crawlCount = 0;
    const RestGlow* rest = nullptr;   // R8 match stage 1: per seat its rest glow (RestGlowOf); null: one kind, its element
    float fight = 1;
    double pointedSinceMs = 0;
    // R8 match stage 2: the pointed effect's seat, age and alpha (the Lab's PointedFx); fxSeat -2: none given, from
    // pointedSinceMs (the effect 100 ms after the pointing, fading in over 100 ms).
    int fxSeat = -2; double fxAgeMs = 0; float fxAlpha = 0;
    bool fxTex = false;   // stage 3a: the game texture atlas is in (level 3 for fire, poison and lightning; else level 4)
    FooterView footer{};   // feedback 1 item 4: the footer's tabs (off: none drawn)
    const char* chooseTitle = nullptr;   // the shape's name (the arc's gold part)
    const char* clearLine = nullptr;     // the Clear half's first line: "<shape> will cast nothing"
    // Quick cast's chooser (2026-11-16): its title's muted lead and its Back half's line (nullptr: the glyph chooser's words)
    const char* chooseLead = nullptr; const char* chooseBack = nullptr;
    const uint8_t* seatDim = nullptr;
    int skillClass = -1;   // R7c: the hero's class (the tree marks' glyphs, TreeGlyphOf); -1: plain marks
    const BadgeSet* badges = nullptr;   // feedback 1 item 3: the bound-button badges (BadgeBoard); main page only, never with chooseTitle
    // T036: the layers (Layers page), the center in thirds (main page) with the grow and the dim, each sub-menu's button badge on
    // its third's mark (on: bound), per seat a sub-menu entry's mark (0 none, 1 + the third). quickWord (T035, 2026-11-16, review N1):
    // why the Quick cast third opens nothing (QuickThirdWord: Off in the Lab, Not for this class), said instead of "Trigger to open".
    SeatLayers layers{};
    bool thirds = false;
    float thirdGrowPct = ThirdGrowDefault, thirdDimPct = ThirdDimDefault;
    std::array<DrawBadge, ThirdCount> menuBadge{};
    const uint8_t* seatMark = nullptr;
    const char* quickWord = nullptr;
    // T036 (2026-11-16): each third's picture's atlas cell (ring_wheel_menu_art.h; -1: none, the drawn mark in silver). A shelf
    // entry's picture comes through `cells` as any seat's.
    std::array<int16_t, ThirdCount> thirdCell{-1, -1, -1};
    // T025 (Lab > Ring wheel > Layers > Tree icon): the mode (treeart::Mode: 0 Drawn is today's glyph), the mark's size in percent
    // (treeart::SizePct: 100 today's, 0 hides the mark) and each tree position's painted picture's atlas cell (-1: none, the drawn
    // glyph). Trees shown only: the orbs keep their glyphs.
    int treeMode = 0; int treeSizePct = treeart::SizeDefault;
    std::array<int16_t, Trees> treeCell{-1, -1, -1};
    // Lab > Ring wheel > Center > Glyph icon: the Glyphs page's eight shapes and the center split's four sigils as painted pictures
    // (mode 0 Drawn: today's strokes; a slot with no cell keeps its drawn shape or sigil).
    glyphart::View glyph{};
};
// R7c: the rune band (kit runeRing): runes 13 px apart on r - 5.5, at least 12, each one of three strokes by the kit's hash of
// (index x 31 + round r). Two static sets: the rest center's (r 80) and the grown one's (r 136), crossfaded by the grow, so no
// rune changes shape while the center grows (the kit re-rolls them every px of r).
constexpr float MarkR = 14, MarkGlyphR = 16.f / 1.5f;   // R7c TS2: the Trees shown mark's outer radius; its glyph 16 px tall
inline int RuneCount(float r) { return std::max(12, int(std::lround(Tau * (r - 5.5f) / 13))); }
enum class RuneShape { Line, Caret, Zed };
inline RuneShape RuneOf(int i, int seedR) { const float h = Hash(uint32_t(i * 31 + seedR)); return h < .33f ? RuneShape::Line : h < .66f ? RuneShape::Caret : RuneShape::Zed; }
// The hold ring's numbers (L2): on the center's outer edge, 12 px, hover ring color over a gold track at 45% on a black
// underlay, a 12 px head dot.
constexpr float NameArcR = 313;   // the tree names' and the chooser title's arc (L1; the badges keep under r 296)
constexpr float HoldRingR = 145, HoldRingW = 12, HoldDot = 12;
inline void Push(Instance* out, int& n, int max, const Instance& i) { if (n < max && i.a > 0) out[n++] = i; }
inline float Ease(float x) { x = std::clamp(x, 0.f, 1.f); return x * x * (3 - 2 * x); }
// The footer's frames (upstream's frame(): a 2 px rim, gold when pointed, a 3 px dark gap, a dark fill) on its dark band.
inline void PushFooter(const Scene& s, float A, Instance* out, int& n, int max) {
    if (!s.footer.on) return;
    const auto& f = s.footer;
    const int tabs = interaction::FooterCount(f.page, f.developer);
    const float w = FooterWidth(f.fullWidth, tabs), x = (CanvasPx - w) / 2;
    auto rect = [&](float x0, float y0, float x1, float y1, float r, float g, float b) {
        Push(out, n, max, {(x0 + x1) / 2, (y0 + y1) / 2, x1 - x0, y1 - y0, 0, 0, 0, 0, 0, r, g, b, A, 3 /* RecipeRect */, 0});
    };
    rect(x + 20.f / 768 * w, 686.f / 768 * CanvasPx, x + 749.f / 768 * w, 745.f / 768 * CanvasPx, 18 / 255.f, 16 / 255.f, 14 / 255.f);   // upstream's band
    for (int i = 0; i < tabs; ++i) {
        const auto b = FooterBoxAt(i, f.page, f.developer, f.fullWidth);
        const bool hot = s.step.under.kind == Kind::Footer && s.step.under.index == b.cell;
        if (hot) rect(b.x0, b.y0, b.x1, b.y1, 183 / 255.f, 143 / 255.f, 73 / 255.f); else rect(b.x0, b.y0, b.x1, b.y1, 104 / 255.f, 83 / 255.f, 53 / 255.f);
        rect(b.x0 + 2, b.y0 + 2, b.x1 - 2, b.y1 - 2, 18 / 255.f, 16 / 255.f, 14 / 255.f);
        if (hot) rect(b.x0 + 5, b.y0 + 5, b.x1 - 5, b.y1 - 5, 65 / 255.f, 49 / 255.f, 29 / 255.f); else rect(b.x0 + 5, b.y0 + 5, b.x1 - 5, b.y1 - 5, 35 / 255.f, 29 / 255.f, 23 / 255.f);
    }
}
// The footer's labels (upstream's 238, 235, 220), centered in each tab.
inline void FooterText(const Scene& s, const text::Metrics& m, float A, Instance* out, int& n, int max) {
    if (!s.footer.on) return;
    const auto& f = s.footer;
    const Rgb ink{238 / 255.f, 235 / 255.f, 220 / 255.f};
    const text::Style style = FooterRowStyle(m, f.page, f.developer, f.fullWidth, f.handWheel);
    for (int i = 0; i < interaction::FooterCount(f.page, f.developer); ++i) {
        const auto b = FooterBoxAt(i, f.page, f.developer, f.fullWidth);
        const char* label = FooterLabel(i, f.page, f.developer, f.handWheel);
        if (!*label) continue;
        const std::string_view l = label;
        const auto fit = FooterFitAt(m, style, l, b.x1 - b.x0);
        const float cx = (b.x0 + b.x1) / 2, cy = (b.y0 + b.y1) / 2, half = text::Specs[fit.style].px * .58f;
        if (fit.split == std::string_view::npos) EmitLine(m, fit.style, l, cx, cy, ink, A, out, n, max);
        else { EmitLine(m, fit.style, l.substr(0, fit.split), cx, cy - half, ink, A, out, n, max); EmitLine(m, fit.style, l.substr(fit.split + 1), cx, cy + half, ink, A, out, n, max); }
    }
}
// ---- The center split (L3; kit centerSplit): two halves, each a one-word label and a 20 px sigil beyond it; the pointed
// half lit (26 px, hover ring color), the other at 55%; the pointed half's two lines between the hairline and its label ----
enum class Sigil { None, Star, Hand, Back, Cross, Glyph, Dots };   // Glyph, Dots (T036): the Glyphs and Quick cast thirds' marks
inline void PushLine(Instance* out, int& n, int max, float x0, float y0, float x1, float y1, float w, Rgb c, float a) {
    const float dx = x1 - x0, dy = y1 - y0, len = std::hypot(dx, dy);
    if (len < .01f) return;
    Push(out, n, max, {(x0 + x1) / 2, (y0 + y1) / 2, len + w * .5f, w, std::atan2(dy, dx), 0, 0, 0, 0, c.r, c.g, c.b, a, 3 /* RecipeRect */, 0});
}
// A 20 px sigil from strokes (2 px, on a 4 px dark underlay): a five-point star (Glyphs), an open hand (Gestures), a back
// arrow, a cross (Clear). T036: a drawn glyph stroke with its start dot (the Glyphs third) and a ring of 8 dots round one (the
// Quick cast third); scale: 1 = 20 px (the thirds' marks: 36 px at rest, 1.8), the strokes thicken with it.
inline void PushSigil(Instance* out, int& n, int max, Sigil s, float x, float y, Rgb c, float a, float scale = 1) {
    float p[12][4]; int k = 0;   // segments x0 y0 x1 y1, relative, px
    auto seg = [&](float x0, float y0, float x1, float y1) { if (k < 12) { p[k][0] = x0; p[k][1] = y0; p[k][2] = x1; p[k][3] = y1; ++k; } };
    const float w = std::max(1.f, scale * .75f);
    auto dot = [&](float dx, float dy, float r) {
        Push(out, n, max, {x + dx * scale, y + dy * scale, 2 * r * scale + 2 * w, 2 * r * scale + 2 * w, 0, 0, 0, 0, 0, 0, 0, 0, .6f * a, RecipeDisc, 1});
        Push(out, n, max, {x + dx * scale, y + dy * scale, 2 * r * scale, 2 * r * scale, 0, 0, 0, 0, 0, c.r, c.g, c.b, a, RecipeDisc, 1});
    };
    switch (s) {
    case Sigil::Glyph: {
        const float q[5][2] = {{-7.5f, 5.5f}, {-2.5f, -7}, {1, 3.5f}, {4.5f, -4.5f}, {8, 6}};
        for (int i = 0; i < 4; ++i) seg(q[i][0], q[i][1], q[i + 1][0], q[i + 1][1]);
        dot(q[0][0], q[0][1], 1.6f);
        break;
    }
    case Sigil::Dots:
        for (int i = 0; i < 8; ++i) dot(7 * std::cos(Tau * float(i) / 8 - Pi / 2), 7 * std::sin(Tau * float(i) / 8 - Pi / 2), 1.3f);
        dot(0, 0, 1.8f);
        return;
    case Sigil::Star:
        for (int i = 0; i < 5; ++i) {
            const float a0 = -Pi / 2 + Tau * float(i * 2) / 5, a1 = -Pi / 2 + Tau * float(i * 2 + 2) / 5;
            seg(10 * std::cos(a0), 10 * std::sin(a0), 10 * std::cos(a1), 10 * std::sin(a1));
        }
        break;
    case Sigil::Hand:   // a palm arc and four fingers
        for (int i = 0; i < 4; ++i) { const float a0 = Pi * float(i) / 4, a1 = Pi * float(i + 1) / 4; seg(-8 * std::cos(a0), 2 + 6 * std::sin(a0), -8 * std::cos(a1), 2 + 6 * std::sin(a1)); }
        for (int i = 0; i < 4; ++i) { const float fx = -6 + 4 * float(i); seg(fx, 2, fx, i == 1 || i == 2 ? -10 : -7); }
        break;
    case Sigil::Back: seg(-9, 0, 9, 0); seg(-9, 0, -3, -6); seg(-9, 0, -3, 6); break;
    case Sigil::Cross: seg(-7, -7, 7, 7); seg(-7, 7, 7, -7); break;
    default: return;
    }
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < k; ++i)
            PushLine(out, n, max, x + p[i][0] * scale, y + p[i][1] * scale, x + p[i][2] * scale, y + p[i][3] * scale, (pass ? 2.f : 4.f) * w, pass ? c : Rgb{0, 0, 0}, pass ? a : .6f * a);
}
// ---- T036: the center in thirds (the design kit: rest, each third pointed) ----
// Each third's mark (36 px at rest, on r 44 at its middle angle; grown, 40 px in the middle of its ring 80..136, r 110); the
// pointed one 11% larger, lit and in the middle of its grown slice, the other two at the dim row's opacity; a class with no
// gestures: that third at 35%. MarkScale: 36 px of a 20 px sigil. out: the pointed slice's half grow (its ring's middle moves out).
// Grown, the rest center stays as the hub (r 80, CenterR) and holds the name; the thirds unfold into the ring 80..136.
constexpr float MarkRestR = 44, MarkGrownR = 110, MarkScale = 1.8f, MarkGrownScale = 2.f, ShutDim = .35f;
constexpr Rgb ThirdFill = Hex(0x2A2118), ThirdFillOn = Hex(0x4A3620), HubFill = Hex(0x100C08);
constexpr Sigil ThirdSigil[ThirdCount] = {Sigil::Glyph, Sigil::Hand, Sigil::Dots};
inline void ThirdMarkAt(int t, float g, float& x, float& y, float out = 0) {
    const float a = ThirdMidDeg[t] * Pi / 180, k = std::clamp(g, 0.f, 1.f), r = MarkRestR + (MarkGrownR - MarkRestR) * k + out * k;
    x = CanvasCx + r * std::cos(a); y = CanvasCy + r * std::sin(a);
}
// T036 (2026-11-16, Jeff: the sub-menu marks showed gold): a third's mark is its picture (ring_wheel_menu_art.h), drawn as a seat
// draws its icon (RecipeSeat: our dark disc, the art cut round, the rim shade, the dark line, the rim in `ring`), `size` px across
// the face. No picture: the drawn sigil in MarkSilver (never gold).
constexpr Rgb MarkSilver = Hex(0xC9CDD2);
// crop (T025): the share of the cell the face shows, from its center (1: all of it; a tree's plate square 104 / 132, the skill frame's
// crop that drops the bevel); recipe: RecipeSeat (the circle) or RecipeSquare (T025 a tree's painted square).
constexpr float TreeCircleCrop = 1.f;   // the painted squares are borderless full-bleed rust (art change 2026-11-17: no frame, bevel or rivets), motif in the middle 80%: the circle shows all of it
inline void PushArtSeat(Instance* out, int& n, int max, int cell, int pct, float x, float y, float size, Rgb ring, float ringW, float a, float crop = 1, uint32_t recipe = RecipeSeat) {
    const float quad = size + 2 * SeatRingPx, grow = quad / size;
    Instance ic{x, y, quad, quad, 0};
    CellRect(cell, pct, ic.u0, ic.v0, ic.u1, ic.v1);
    const float cu = (ic.u0 + ic.u1) / 2, cv = (ic.v0 + ic.v1) / 2, hu = (ic.u1 - ic.u0) / 2 * grow * crop, hv = (ic.v1 - ic.v0) / 2 * grow * crop;
    ic.u0 = cu - hu; ic.u1 = cu + hu; ic.v0 = cv - hv; ic.v1 = cv + hv;
    ic.r = ring.r; ic.g = ring.g; ic.b = ring.b; ic.a = a; ic.recipe = recipe; ic.param = ringW;
    Push(out, n, max, ic);
}
// A sub-menu's button badge on its third's mark (lower right of the mark; the Glyphs third's Draw glyph button keeps that place,
// so the Glyphs menu's goes lower left).
inline void MenuBadgeAt(int t, bool left, float g, float k, float& x, float& y, float out = 0) {
    ThirdMarkAt(t, g, x, y, out);
    x += (left ? -.55f : .55f) * 20 * MarkScale * k; y += .45f * 20 * MarkScale * k;
}
struct Half { const char* label = nullptr; Sigil sigil = Sigil::None; const char* line0 = nullptr; const char* line1 = nullptr; };
// The painted picture's slot of a split sigil (Lab > Ring wheel > Center > Glyph icon); -1: no picture for it (the thirds' marks).
inline int SigilSlot(Sigil s) {
    switch (s) { case Sigil::Back: return glyphart::SlotBack; case Sigil::Cross: return glyphart::SlotClear; case Sigil::Star: return glyphart::SlotStar; case Sigil::Hand: return glyphart::SlotHand; default: return -1; }
}
// A split sigil: its painted picture (a seat: our dark disc, the art cut round, the rim in the sigil's tint; or the plate square) when
// the Glyph icon row asks for one and the atlas has it, else the drawn strokes. `scene` null (the early tests): drawn.
inline void PushSigilOrArt(const Scene* scene, Sigil sg, Instance* out, int& n, int max, float x, float y, Rgb c, float a) {
    const int slot = scene ? SigilSlot(sg) : -1;
    if (slot < 0 || !scene->glyph.Shows(slot)) { PushSigil(out, n, max, sg, x, y, c, a); return; }
    const float k = scene->glyph.Scale(), face = std::max(4.f, glyphart::SigilPx * k - 7.f);
    PushArtSeat(out, n, max, scene->glyph.cell[size_t(slot)], scene->atlasPct, x, y, face, c, 2.f, a, 1.f, scene->glyph.mode == int(glyphart::Mode::Square) ? RecipeSquare : RecipeSeat);
}
// pointed: 0 none, 1 the top half, 2 the bottom half; g the center's grow 0-1.
inline void DrawSplit(const text::Metrics& m, const Half& top, const Half& bottom, int pointed, float g, float A, Instance* out, int& n, int max, const Scene* scene = nullptr) {
    const float cx = CanvasCx, cy = CanvasCy, d = 26 + 40 * g, lines = Ease((g - .55f) / .45f);
    for (int h = 0; h < 2; ++h) {
        const Half& half = h ? bottom : top;
        const float sgn = h ? 1.f : -1.f;
        const bool on = pointed == h + 1, dim = pointed && !on;
        const float a = A * (dim ? .55f : 1.f);
        PushSigilOrArt(scene, half.sigil, out, n, max, cx, cy + sgn * (d + 24), on ? HoverRing : Gold, a);
        if (half.label) EmitLine(m, on ? text::Label26 : text::Label22, half.label, cx, cy + sgn * d, on ? HoverRing : GoldLight, a, out, n, max);
        if (on && lines > 0) {   // between the hairline and the label, 16 px clear of the line (kit centerSplit; our y is the middle)
            const float y0 = h ? cy + d - 52 : cy - d + 28;   // grown: C + 14 / C + 38 below, C - 38 / C - 14 above
            if (half.line0) EmitLine(m, text::Body22, half.line0, cx, y0, Muted, A * lines, out, n, max);
            if (half.line1) EmitLine(m, text::Bold22, half.line1, cx, y0 + 24, Parchment, A * lines, out, n, max);
        }
    }
}
// ---- Bound-button badges (feedback 1 item 3, L4 rows 16-18; the kit's drawQuestBadge) ----
// Where each seat was drawn this frame (canvas px, with its float and burst), its opacity and whether it is the pointed one.
struct SeatDrawn { float x = 0, y = 0, a = 0; bool on = false, pointed = false; };
constexpr Rgb Ink = Hex(0x14110D), TickGreen = Hex(0x9BE07A);
struct BadgeAt { float x = 0, y = 0, R = 0, a = 0, pop = 1; };
// A badge rides with its icon: at rest its placed offset; pointed, out along its angle with the 1.3x icon; it pops 150 ms
// when its slot takes the skill.
inline bool BadgeShows(const Scene& s, const Badge& b, const std::array<SeatDrawn, MaxSeats>& d, BadgeAt& at) {
    if (b.seat < 0 || b.seat >= s.layout->count || !d[size_t(b.seat)].on) return false;
    const SeatDrawn& sd = d[size_t(b.seat)];
    const Seat& seat = s.layout->seats[size_t(b.seat)];
    at.pop = BadgePop(s.tMs - b.bornMs); at.R = s.badges->look.sizePx / 2.f * at.pop; at.a = sd.a;
    if (sd.pointed) { const float dd = BadgeDistPointed(s.badges->look, b.dist); at.x = sd.x + std::cos(b.angle) * dd; at.y = sd.y + std::sin(b.angle) * dd; }
    else { at.x = sd.x + (b.x - seat.x); at.y = sd.y + (b.y - seat.y); }
    return at.a > .002f && at.R > 0;
}
// One badge's body: the 7 px dark halo, the disc / coin / square with its 2 px gold light rim (RecipeBadge).
inline void PushBadgeBody(float x, float y, float R, float corner, float a, BadgeKind kind, Instance* out, int& n, int max) {
    const float q = 2 * (R + 7);
    Push(out, n, max, {x, y, q, q, 0, R, corner, R, corner, 1, 1, 1, a, RecipeBadge, float(int(kind))});
}
// The bodies, after every icon so no neighbor covers one, and the green tick ring of a fresh bind.
inline void PushBadges(const Scene& s, const std::array<SeatDrawn, MaxSeats>& d, Instance* out, int& n, int max) {
    if (!s.badges || s.chooseTitle) return;
    const float corner = 6.f * s.badges->look.sizePx / 28;
    for (int i = 0; i < s.badges->count; ++i) {
        const Badge& b = s.badges->b[size_t(i)];
        BadgeAt at;
        if (!BadgeShows(s, b, d, at)) continue;
        PushBadgeBody(at.x, at.y, at.R, corner * at.pop, at.a, b.kind, out, n, max);
        if (const float tick = BadgeTick(s.tMs - b.bornMs); tick > 0) {
            const float o = 2 * (at.R + 5.5f);   // the kit's body(-4) stroked 3 px: centerline R + 4
            Push(out, n, max, {at.x, at.y, o, o, 0, 0, 0, 0, 0, TickGreen.r, TickGreen.g, TickGreen.b, at.a * tick, RecipeRing, 3});
        }
    }
}
// The letters, 22 px Alegreya Sans bold at 28 px (scaled with the size row and the pop), centered 1 px low as the kit's:
// parchment over its dark outline on a disc or square; ink with no outline on the coin (the outline hid the A's crossbar).
inline void PushBadgeLetter(const text::Metrics& m, char letter, BadgeKind kind, float x, float y, float R, float a, Instance* out, int& n, int max) {
    const auto& gl = m.g[size_t(text::Bold22)][size_t(text::SlotOf(uint8_t(letter)))];
    if (gl.w <= 0 || n >= max) return;
    const float k = R / 14.f, pen = x - gl.advance * k / 2, base = y + k * (1 + text::Baseline(m, text::Bold22, 0));
    const bool coin = kind == BadgeKind::Coin;
    const Rgb c = coin ? Ink : Parchment;
    out[n++] = {pen + k * (gl.ox + gl.w / 2), base + k * (gl.oy + gl.h / 2), gl.w * k, gl.h * k, 0, gl.u0, gl.v0, gl.u1, gl.v1, c.r, c.g, c.b, a, RecipeGlyph, coin ? 1.f : 0.f};
}
inline void BadgeLetters(const Scene& s, const text::Metrics& m, const std::array<SeatDrawn, MaxSeats>& d, Instance* out, int& n, int max) {
    if (!s.badges || s.chooseTitle) return;
    for (int i = 0; i < s.badges->count && n < max; ++i) {
        const Badge& b = s.badges->b[size_t(i)];
        BadgeAt at;
        if (BadgeShows(s, b, d, at)) PushBadgeLetter(m, b.letter, b.kind, at.x, at.y, at.R, at.a, out, n, max);
    }
}
// #3 (designer): the Draw glyph badge stays while the center is pointed (it rides the ring), on the split and on the Warlock's
// one Glyphs center; never over a skill's blurb, the hold's picture or the chooser.
// The seat drawn as the pointed one (1.3 x, the 3 px hover ring): the one under the ray. On the glyph chooser a button held
// on an order keeps that order marked while the ray drifts (designer gate D1); the main ring is as it was (holdName null).
inline int MarkedSeat(const Scene& s) {
    if (s.holdFrac >= 0 && s.holdName && s.layout && s.holdSeat >= 0 && s.holdSeat < s.layout->count) return s.holdSeat;
    return s.step.under.kind == Kind::Seat ? s.step.under.index : -1;
}
inline bool DrawBadgeShows(const Scene& s) { return s.drawBadge.on && !s.chooseTitle && s.step.blurb < 0 && s.holdFrac < 0; }
// A pointed default's part over its own face (param flag; PSFx cuts it to the face and keeps the 90 px reach's fade).
constexpr uint32_t PointedOverFace = 4096;
// The compose's draws: one per run of instances on the same pixel shader (effects, recipe 6, have their own: PSFx).
inline int DrawRuns(const Instance* list, int n) {
    int runs = n > 0;
    for (int i = 1; i < n; ++i) runs += (list[i].recipe == RecipeFx) != (list[i - 1].recipe == RecipeFx);
    return runs;
}
// The instance list: shapes and icons first (at most MaxInstances), then glyphs (at most MaxText); 0 when closed or faded.
// Perf: the effects go in two passes, the rest glows and the pointed effect first
// (under every icon), the over-face glows after the shapes (the text stays on top), so a compose is 4 draws. Safe
// because no effect under a face reaches another icon: the rest cap 33 px (43 pointed) against 70 - 22 = 48 px from a
// neighbor's center to its face (ring-wheel-test checks it); the pointed effect is meant to lie under its neighbors.
inline int Build(const Scene& s, Instance* out, int maxShapes = MaxInstances, int maxText = MaxText) {
    int n = 0;
    maxShapes = std::min(maxShapes, MaxInstances);   // review N2: the effect pass buffers hold MaxInstances
    if (!s.open || s.wheelAlpha <= 0 || !s.layout) return 0;
    const float A = std::clamp(s.wheelAlpha, 0.f, 1.f);
    const auto& l = *s.layout;
    const float cx = CanvasCx, cy = CanvasCy;
    const bool holding = s.holdFrac >= 0;
    const float g = holding ? 1.f : s.grow >= 0 ? std::clamp(s.grow, 0.f, 1.f) : s.step.centerGrown ? 1.f : 0.f;
    // The center: its dark backing disc (75%, L1), the rune ring's outer and inner strokes, the split's hairline.
    const float cr = CenterR + (CenterGrownR - CenterR) * g;
    Push(out, n, maxShapes, {cx, cy, 2 * cr + 20, 2 * cr + 20, 0, 0, 0, 0, 0, 0, 0, 0, .75f * A, RecipeDisc, 10});
    // T036 (the mockup's pointed panel): the pointed third's slice reaches its grow past the edge: its dark backing first; the
    // thirds' fills (hub to the inner rune ring, the pointed one to its reach), the spokes, its outline and the hub after the runes.
    const int third = s.thirds && !holding && s.step.blurb < 0 ? ThirdOf(s.step.under.kind) : -1;
    const float reach = cr * (1 + std::clamp(s.thirdGrowPct, 0.f, 100.f) / 100), dimK = std::clamp(s.thirdDimPct, 0.f, 100.f) / 100;
    const bool unfolded = third >= 0 && cr - 11 > CenterR + 2;   // grown far enough for the ring 80..136 (the hub inside it)
    if (third >= 0) Push(out, n, maxShapes, {cx, cy, 2 * reach, 2 * reach, 0, cr - 2, 0, cr - 2, 0, 0, 0, 0, .75f * A, RecipeSector, float(third + 4)});
    Push(out, n, maxShapes, {cx, cy, 2 * cr, 2 * cr, 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, A, RecipeRing, 2});
    Push(out, n, maxShapes, {cx, cy, 2 * (cr - 11), 2 * (cr - 11), 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, .8f * A, RecipeRing, 2});   // 2 px (R7c TS4)
    {   // R7c: the rune band between the two rings; a pointed half's band full, the other half at 55% (L3 pointed half)
        // T036 thirds: the pointed third's band full, the other two at the dim row (code 4 + third + 3 x the dim's percent).
        const int half = s.thirds ? (third >= 0 ? 4 + third + 3 * int(std::lround(dimK * 100)) : 0)
                                  : s.step.under.kind == Kind::Glyphs ? 2 : s.step.under.kind == Kind::Gestures ? 1 : 0;   // the half that dims
        for (int set = 0; set < 2; ++set) {
            const float a = (set ? g : 1 - g) * .8f * A, seed = set ? CenterGrownR : CenterR;
            Push(out, n, maxShapes, {cx, cy, 2 * cr, 2 * cr, 0, seed, float(half), seed, float(half), Gold.r, Gold.g, Gold.b, a, RecipeRunes, float(RuneCount(seed))});
        }
    }
    if (unfolded)   // each third's fill, a light ramp outward; the pointed one over the runes to its reach, the others at the dim row
        for (int t = 0; t < ThirdCount; ++t) {
            const bool on = t == third;
            const float r1 = on ? reach : cr - 11;
            const Rgb c = on ? ThirdFillOn : ThirdFill;
            Push(out, n, maxShapes, {cx, cy, 2 * r1, 2 * r1, 0, CenterR, 0, CenterR, 0, c.r, c.g, c.b, A * (on ? 1.f : dimK), RecipeSector, float(t + 8)});
        }
    if (holding) {   // the fill ring on the center's outer edge, clockwise from the top
        const float f = std::clamp(s.holdFrac, 0.f, 1.f), d = 2 * HoldRingR + HoldRingW;
        Push(out, n, maxShapes, {cx, cy, d + 4, d + 4, 0, 0, 0, 0, 0, 0, 0, 0, .6f * A, RecipeRing, HoldRingW + 4});
        Push(out, n, maxShapes, {cx, cy, d, d, 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, .45f * A, RecipeRing, HoldRingW});
        if (f > 0) {
            Push(out, n, maxShapes, {cx, cy, d, d, 0, f, 0, 0, 0, HoverRing.r, HoverRing.g, HoverRing.b, A, RecipeArc, HoldRingW});
            const float a = f * Tau - Pi / 2, r = HoldRingR;
            Push(out, n, maxShapes, {cx + r * std::cos(a), cy + r * std::sin(a), HoldDot, HoldDot, 0, 0, 0, 0, 0, 1, .97f, .88f, A, RecipeDisc, 1.5f});
        }
    }
    const bool split = s.step.blurb < 0 && s.hasMotions && !holding && !s.thirds;
    if (s.thirds && s.step.blurb < 0 && !holding) {   // T036: the three spokes (the dead bands' middles): to the middle at rest, hub to runes grown
        for (const float sd : SpokeDeg) {
            const float a = sd * Pi / 180, r0 = unfolded ? CenterR : third >= 0 ? cr - 11 : 6, r1 = unfolded ? cr - 11 : cr - 2;
            PushLine(out, n, maxShapes, cx + r0 * std::cos(a), cy + r0 * std::sin(a), cx + r1 * std::cos(a), cy + r1 * std::sin(a), 1.5f, Gold, (unfolded ? .8f : 1.f) * A);
        }
        if (third < 0) Push(out, n, maxShapes, {cx, cy, 6, 6, Pi / 4, 0, 0, 0, 0, GoldLight.r, GoldLight.g, GoldLight.b, A, RecipeRect, 0});
        if (third >= 0)   // the pointed slice's 2.5 px outline (hub to its reach), then the hub over it with its gold ring
            Push(out, n, maxShapes, {cx, cy, 2 * reach, 2 * reach, 0, unfolded ? CenterR : cr - 11, 0, unfolded ? CenterR : cr - 11, 0, GoldLight.r, GoldLight.g, GoldLight.b, A, RecipeSector, float(third)});
        if (unfolded) {
            Push(out, n, maxShapes, {cx, cy, 2 * (CenterR - 2), 2 * (CenterR - 2), 0, 0, 0, 0, 0, HubFill.r, HubFill.g, HubFill.b, .95f * A, RecipeDisc, 1.5f});
            Push(out, n, maxShapes, {cx, cy, 2 * CenterR, 2 * CenterR, 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, A, RecipeRing, 2});
        }
    }
    if (split) {   // the hairline and its 6 px diamond (L3)
        Push(out, n, maxShapes, {cx, cy, 2 * (cr - 15), 2, 0, 0, 0, 0, 0, Gold.r, Gold.g, Gold.b, A, RecipeRect, 0});
        Push(out, n, maxShapes, {cx, cy, 6, 6, Pi / 4, 0, 0, 0, 0, GoldLight.r, GoldLight.g, GoldLight.b, A, RecipeRect, 0});
    }
    // Each tree's sigil at r 128 (Trees shown) or its orb (Tree orbs), in its accent; it fades out by half the center's
    // grow (designer's R4 fix: gone at g 0.5, r 108, before the disc edge meets it), orbs also shrink to 65%.
    const float aside = 1 - std::min(1.f, g / .5f);
    for (int t = 0; t < Trees; ++t) {
        const float ta = A * aside * (l.mode == TreeLayout::Orbs ? 1.f : std::max(.4f, s.step.alpha[size_t(t)]));
        const bool orb = l.mode == TreeLayout::Orbs;
        const float r = orb ? (s.step.tree == t ? 40.f : 34.f) * (1 - .35f * g) : 12.f;
        const float x = cx + 128 * std::cos(l.dir[size_t(t)]), y = cy + 128 * std::sin(l.dir[size_t(t)]);
        const Rgb c = s.step.tree == t ? HoverRing : TreeAccentOf(s.skillClass, t);
        // T025 (Trees shown only; the orbs keep today's look): the mark's size (Tree icon size: 100% today's, 0 hides the mark), and
        // its painted picture when the mode asks for one and the tree has it: a skill-style seat (the circle, the plate's inside) or the
        // plate's square, its rim the accent; today's outer reach (MarkR) kept at 100%. No picture: the drawn mark below.
        const float k = orb ? 1.f : treeart::SizeScale(float(s.treeSizePct));
        if (k <= 0) continue;
        if (const int cell = !orb && s.treeMode > 0 && s.treeMode <= 2 ? s.treeCell[size_t(t)] : -1; cell >= 0) {
            const bool square = s.treeMode == 2;
            PushArtSeat(out, n, maxShapes, cell, s.atlasPct, x, y, 2 * (MarkR - 3.5f) * k, c, 2.f, ta, square ? 1.f : TreeCircleCrop, square ? RecipeSquare : RecipeSeat);
            continue;
        }
        // Trees shown (R7c TS2): a 28 px mark, dark disc r 14, the 2 px accent ring on centerline r 13, a 16 px glyph
        // (MarkGlyphR: the kit's paths span +-0.75 r). Orbs as before: disc r + 4, ring r + 3, glyph 0.8 r.
        const float disc = orb ? 2 * r + 8 : 2 * MarkR * k, ring = orb ? 2 * r + 6 : 2 * MarkR * k, gr = orb ? .8f * r : MarkGlyphR * k;
        Push(out, n, maxShapes, {x, y, disc, disc, 0, 0, 0, 0, 0, .055f, .043f, .031f, .9f * ta, RecipeDisc, 2});
        Push(out, n, maxShapes, {x, y, ring, ring, 0, 0, 0, 0, 0, c.r, c.g, c.b, ta, RecipeRing, 2});
        const MarkGlyph glyph = TreeGlyphOf(s.skillClass, t);   // R7c: the kit's glyph in gold light, inside the mark or the orb
        if (glyph != MarkGlyph::None)
            Push(out, n, maxShapes, {x, y, 2 * gr + 4, 2 * gr + 4, 0, gr, 0, gr, 0, GoldLight.r, GoldLight.g, GoldLight.b, ta, RecipeTreeGlyph, float(glyph)});
    }
    // The seats: each icon at its rest place plus its float, cut round, a 2 px rim; the pointed one 1.3x with the hover
    // ring. A seat with no icon gets L1's initials disc (its rim, a dark disc and the initials).
    struct Initial { float x, y, a; int seat; };
    std::array<Initial, MaxSeats> initials{};
    int nInitials = 0;
    const int pointedSeat = MarkedSeat(s);
    const int pointedTree = pointedSeat >= 0 && pointedSeat < l.count ? l.seats[size_t(pointedSeat)].tree : -2;
    const double tS = std::fmod(s.tMs / 1000.0, 3600.0);
    std::array<int, Trees> inTree{};   // each seat's order in its tree (the burst's stagger)
    std::array<SeatDrawn, MaxSeats> drawn{};   // feedback 1 item 3: where the badges ride
    auto restOf = [&](int i) {
        if (s.rest) return s.rest[i];
        RestGlow g; g.kind[0] = RestKind(s.elements ? std::min<uint32_t>(s.elements[i], uint32_t(Element::Count) - 1) : uint32_t(Element::Phys));
        return g;
    };
    // A seat's drawn center: its rest place, out of its orb while the tree bursts open (L2), plus its float.
    auto seatAt = [&](int i, bool pointed, int k, float& x, float& y, Float* fo) {
        const auto& seat = l.seats[size_t(i)];
        const Float f = s.anim.floats && !pointed ? FloatAt(seat.id, s.tMs) : Float{};   // as LayersFor's floats
        float sx = seat.x, sy = seat.y;
        if (l.mode == TreeLayout::Orbs && seat.tree >= 0 && seat.tree == s.step.tree) {   // Tree burst (L2): out of the orb
            const float ox = C + 128 * std::cos(l.dir[size_t(seat.tree)]), oy = C + 128 * std::sin(l.dir[size_t(seat.tree)]);
            const float b = BurstAt(s.step.treeAgeMs, k, s.anim.burst && !s.anim.still);
            sx = ox + (seat.x - ox) * b; sy = oy + (seat.y - oy) * b;
        }
        x = CanvasX + sx + f.dx; y = CanvasY + sy + f.dy;
        if (fo) *fo = f;
    };
    static thread_local std::array<Instance, MaxInstances> fxUnder, fxOver;   // the two effect passes (perf nit: not on the stack)
    int nU = 0, nO = 0;
    auto pushFx = [&](bool over, const Instance& i) { if (n + nU + nO < maxShapes && i.a > 0) (over ? fxOver[size_t(nO++)] : fxUnder[size_t(nU++)]) = i; };
    // R8 match stage 2 (spec 4a): the pointed effect, one at a time, under every icon's art (the first pass).
    int fxSeat = s.fxSeat; double fxAge = s.fxAgeMs; float fxA = s.fxAlpha;
    if (fxSeat == -2) { fxSeat = pointedSeat; fxAge = s.tMs - s.pointedSinceMs - PointedFx::OnsetMs; fxA = float(std::clamp(fxAge / PointedFx::FadeInMs, 0.0, 1.0)); }
    if (fxSeat < 0 || fxSeat >= l.count || fxAge < 0 || fxA <= 0 || !s.anim.hoverFx || s.anim.still) fxSeat = -1;
    if (fxSeat >= 0) {
        const auto& seat = l.seats[size_t(fxSeat)];
        int k = 0;
        for (int j = 0; j < fxSeat; ++j) k += l.seats[size_t(j)].tree == seat.tree;
        float x, y; seatAt(fxSeat, fxSeat == pointedSeat, k, x, y, nullptr);
        const float ta = seat.tree >= 0 ? s.step.alpha[size_t(seat.tree)] : 1.f, a = A * fxA * ta, r = IconPx * 1.3f / 2;
        uint32_t rgb = HoverAuraRgb;
        const RestKind hk = HoverKindOf(restOf(fxSeat), &rgb);
        if (hk == RestKind::None) fxSeat = -1;   // no drawn default: the hover ring only (the face default may still move)
        const Rgb c = Hex(rgb);
        const float ageS = float(std::fmod(fxAge / 1000.0, 3600.0)), seed = Seed(seat.id);
        // Stage 3a: the kit's level 3 (hover-game.js) where it has one and the game textures are in: fire, poison (mode 4)
        const bool level3 = FxStageOf(hk, s.fxTex) == 3, game = level3 && hk != RestKind::Light;   // review N5: the log's rule
        if (hk != RestKind::None) {
            const float param = float(uint32_t(hk) + 256u * (game ? 4u : 1u));
            pushFx(false, {x, y, 2 * PointedReachPx, 2 * PointedReachPx, 0, ageS, seed, r, seed, c.r, c.g, c.b, a, RecipeFx, param});
            // Designer S3a FIX: the kit draws every pointed effect after the face (wheel.js, one hover call after the face clip),
            // all elements: the same effect on a face-sized quad in the over pass, cut to the face. Its light only adds (no
            // pixel of the face gets darker; the shadow puffs, the one normal blend, start outside r). Fire's plate and
            // poison's puffs are the ones that reach in. Neighbors' art stays on top of the rest (spec 4a): no neighbor face
            // reaches the pointed one (70 px pitch).
            pushFx(true, {x, y, 2 * r + 2, 2 * r + 2, 0, ageS, seed, r, seed, c.r, c.g, c.b, a, RecipeFx, param + float(PointedOverFace)});
        }
        const float boltW = hk == RestKind::Light ? 1.f : hk == RestKind::Multi ? MultiWeight(2, fxAge) : 0.f;
        if (boltW > 0) {   // the kit's arcs to the two nearest shown icons, each pulsing (0.7 s), cut where under 20%
            int nbr[2] = {-1, -1}; float nd[2] = {1e9f, 1e9f};
            for (int j = 0; j < l.count; ++j) {
                const auto& o = l.seats[size_t(j)];
                if (j == fxSeat || (o.tree >= 0 && s.step.alpha[size_t(o.tree)] < .5f) || (s.seatDim && s.seatDim[j])) continue;
                const float dd = std::hypot(o.x - seat.x, o.y - seat.y);
                if (dd < nd[0]) { nd[1] = nd[0]; nbr[1] = nbr[0]; nd[0] = dd; nbr[0] = j; }
                else if (dd < nd[1]) { nd[1] = dd; nbr[1] = j; }
            }
            for (int b = 0; b < 2; ++b) {
                if (nbr[b] < 0) continue;
                float nx, ny; seatAt(nbr[b], false, 0, nx, ny, nullptr);
                const float dx = nx - x, dy = ny - y, d = std::hypot(dx, dy);
                if (d - BoltStopPx > PointedReachPx - 5 || d - BoltStopPx - r < 4) continue;
                const double ph = fxAge / 700 + b * .5;
                const float on = float(std::sin((ph - std::floor(ph)) * Pi));
                if (on < .2f) continue;
                const float ux = dx / d, uy = dy / d, len = d - BoltStopPx - r, mid = r + len / 2;
                const bool arcs = level3 && hk == RestKind::Light;   // stage 3a: the game's arcs (mode 5), 46 px wide as the kit's
                pushFx(false, {x + ux * mid, y + uy * mid, len + 24, arcs ? 48.f : 40.f, std::atan2(uy, ux), ageS, seed + 7.f * (b + 1), len, seed, 1, 1, 1, a * on * boltW, RecipeFx, float(uint32_t(RestKind::Light) + 256u * (arcs ? 5u : 3u))});
            }
        }
    }
    const int crawlS = s.anim.still ? 0 : s.anim.crawlS;
    int events = 0;   // the panel's rest events (RestEventAge): every seat with one, in seat order
    for (int i = 0; i < l.count; ++i) events += HasRestEvent(restOf(i), crawlS);
    const double eventPeriod = crawlS * 1000.0;
    int eventIndex = 0;
    for (int i = 0; i < l.count && n + nU + nO < maxShapes - 8; ++i) {
        const auto& seat = l.seats[size_t(i)];
        const bool pointed = pointedSeat == i;
        const int k = seat.tree >= 0 ? inTree[size_t(seat.tree)]++ : 0;
        // R8: the fight dim spares the pointed icon (L2 Hit); same-tree neighbors of the pointed one dim 15%, their glows to 50%
        const bool neighbor = !pointed && pointedTree >= 0 && seat.tree == pointedTree;
        const float ta = (seat.tree >= 0 ? s.step.alpha[size_t(seat.tree)] : 1.f) * (s.seatDim && s.seatDim[i] ? .25f : 1.f) *
                         (pointed ? 1.f : s.fight) * (neighbor ? .85f : 1.f);
        const RestGlow glow = restOf(i);
        const int myEvent = HasRestEvent(glow, crawlS) ? eventIndex++ : -1;
        if (ta <= .002f) continue;
        Layers lay = LayersFor(s.anim, pointed, s.crawlIndex ? s.crawlIndex[i] : -1, s.crawlCount, s.tMs);
        // At rest the event gate owns the lightning burst (its slot on the panel's clock, not the crawl's own stagger).
        const double eventAge = lay.glowMoves || (!pointed && crawlS > 0) ? RestEventAge(myEvent, events, s.tMs, eventPeriod) : -1;
        if (!pointed) lay.crawl = crawlS > 0 && CrawlsAtRest(glow) && eventAge >= 0 && eventAge < RestBurstMs ? float(std::sin(eventAge / RestBurstMs * Pi)) : 0.f;
        Float f; float x, y; seatAt(i, pointed, k, x, y, &f);
        const float size = IconPx * (pointed ? 1.3f : 1.f), r = size / 2;
        drawn[size_t(i)] = {x, y, A * ta, true, pointed};   // feedback 1 item 3: where the badges ride (seatAt's place)
        const int cell = s.cells ? s.cells[i] : i % (AtlasCells * AtlasCells);
        const Rgb rim = seat.tree >= 0 ? TreeRimOf(s.skillClass, seat.tree) : Gold;
        const float seed = Seed(seat.id), glowT = lay.glowMoves ? float(tS) : 0.f;
        const float ev = lay.glowMoves ? float(eventAge) : -1.f;   // a frozen glow (Off) shows no event
        const float capR = RestCapPx * size / IconPx, glowA = A * ta * (neighbor ? .5f : 1.f);
        if (pointed) Push(out, n, maxShapes, {x, y, size + 32, size + 32, 0, 0, 0, 0, 0, 0, 0, 0, .75f * A, RecipeDisc, 16});   // dark halo
        auto pushRest = [&](int mode) {   // the kit's FX per kind, inside the rest cap (Off: frozen)
            for (int k = 0; k < 2; ++k) {
                const RestKind kind = glow.kind[k];
                if (!ValidKind(kind) || (mode == 2 && !OverFace(kind))) continue;
                const Rgb c = glow.rgb[k] ? Hex(glow.rgb[k]) : Rgb{1, 1, 1};
                pushFx(mode == 2, {x, y, 2 * capR, 2 * capR, 0, glowT, seed, ev, seed, c.r, c.g, c.b, glowA, RecipeFx, float(uint32_t(kind) + 256u * uint32_t(mode))});
            }
        };
        const SeatLayers& L = s.layers;   // T036: each layer its own switch (Lab > Experiment: Ring wheel > Layers)
        if (L.fx) pushRest(0);
        if (L.frame) {   // our own static band outside the color ring (8 px at 1x, so a framed seat stays inside the 70 px pitch)
            const float k = size / IconPx, r0 = r + 4 * k, band = FrameBandPx * k, el = s.elements ? float(std::min<uint32_t>(s.elements[i], uint32_t(Element::Count) - 1)) : float(Element::Phys);
            const Rgb fc = FrameColor(Element(uint32_t(el)));
            Push(out, n, maxShapes, {x, y, 2 * (r0 + band) + 4, 2 * (r0 + band) + 4, 0, r0, band, r0, band, fc.r, fc.g, fc.b, A * ta, RecipeFrame, el});
        }
        const Rgb ring = pointed ? HoverRing : rim;
        const float ringW = pointed ? 3.f : 2.f, quad = size + 2 * SeatRingPx, grow = quad / size;
        const float flags = float(16 * ((L.icon ? 0 : SeatNoArt) | (L.ring ? 0 : SeatNoRing)));
        auto seatIcon = [&](float a, float param) {   // RecipeSeat: the quad grows past the face; the atlas rectangle with it
            if (!L.icon && !L.ring) return;   // nothing of the seat's own left to draw
            Instance ic{x, y, quad, quad, f.rot};
            CellRect(cell, s.atlasPct, ic.u0, ic.v0, ic.u1, ic.v1);
            const float cu = (ic.u0 + ic.u1) / 2, cv = (ic.v0 + ic.v1) / 2, hu = (ic.u1 - ic.u0) / 2 * grow, hv = (ic.v1 - ic.v0) / 2 * grow;
            ic.u0 = cu - hu; ic.u1 = cu + hu; ic.v0 = cv - hv; ic.v1 = cv + hv;
            ic.r = ring.r; ic.g = ring.g; ic.b = ring.b; ic.a = a; ic.recipe = RecipeSeat; ic.param = param < 0 ? param - flags : param + flags;
            Push(out, n, maxShapes, ic);
        };
        if (cell >= 0 || (s.seatMark && s.seatMark[i] && L.icon)) {   // T036: a sub-menu entry draws its picture as any icon (2026-11-16)
            const int mark = s.seatMark ? int(s.seatMark[i]) - 1 : -1;
            if (mark >= 0 && mark < ThirdCount && cell < 0) {   // its picture missing: its third's drawn mark, silver
                if (L.icon) {
                    Push(out, n, maxShapes, {x, y, size, size, 0, 0, 0, 0, 0, .09f, .075f, .06f, A * ta, RecipeDisc, 1.5f});
                    PushSigil(out, n, maxShapes, ThirdSigil[mark], x, y, pointed ? HoverRing : MarkSilver, A * ta, size / IconPx * 1.25f);
                }
                seatIcon(A * ta, -ringW);
                drawn[size_t(i)] = {x, y, A * ta, true, pointed};
                continue;
            }
            // R8 match stage 2: the face motion goes with the pointed effect (its age), easing back with its alpha on leaving
            const bool faceOn = i == fxSeat && s.anim.faceFx && !s.anim.still && L.icon;
            FaceCopy copies[3]; int nc = 1; copies[0] = {};
            if (faceOn) {
                nc = FaceCopies(seat.id, FaceElementOf(glow), fxAge, r, copies);
                for (int c = 0; c < nc; ++c) {
                    auto& fc = copies[c];
                    fc.dx *= fxA; fc.dy *= fxA; fc.rot *= fxA; fc.scale = 1 + (fc.scale - 1) * fxA;
                    fc.alpha = nc == 1 ? 1 + (fc.alpha - 1) * fxA : fc.alpha * (c == nc - 1 ? 1.f : fxA);
                }
            }
            if (!faceOn) seatIcon(A * ta, ringW);
            else {
                for (int c = 0; c < nc; ++c) {
                    const auto& fc = copies[c];
                    const bool moved = fc.dx != 0 || fc.dy != 0;
                    Instance ic{x + fc.dx, y + fc.dy, size * fc.scale, size * fc.scale, f.rot + fc.rot};
                    CellRect(cell, s.atlasPct, ic.u0, ic.v0, ic.u1, ic.v1);
                    ic.r = rim.r; ic.g = rim.g; ic.b = rim.b; ic.a = A * ta * fc.alpha; ic.recipe = RecipeIcon; ic.param = IconDisc;
                    if (moved) { ic.r = fc.dx; ic.g = fc.dy; ic.b = 0; ic.recipe = RecipeFace; }   // clipped to the seat (kit)
                    Push(out, n, maxShapes, ic);
                }
                seatIcon(A * ta, -ringW);   // the shade and the rings stay put
            }
            if (lay.crawl > 0 && L.fx) {   // over the face: two bolts, new every 70 ms (T036: the Animation layer)
                const float st = float(std::floor(s.tMs / 70)) + seed;
                Push(out, n, maxShapes, {x, y, size, size, 0, st, 0, st, 0, 1, 1, 1, A * ta * lay.crawl, RecipeCrawl, 0});
            }
        } else {
            if (L.icon) Push(out, n, maxShapes, {x, y, size, size, 0, 0, 0, 0, 0, .16f, .12f, .09f, A * ta, RecipeDisc, 1.5f});
            seatIcon(A * ta, -ringW);
            if (L.icon) initials[size_t(nInitials++)] = {x, y, A * ta, i};
        }
        if (L.fx) pushRest(2);   // over the face: the lightning arcs (kit: after the rim), the steel glint (T036: the Animation layer)
    }
    PushFooter(s, A, out, n, maxShapes);   // feedback 1 item 4: fades with the wheel, never with the fight dim
    // The effect passes (perf R8 stage 2): the under pass in front of everything, the over pass after the shapes and before
    // the text (perf d26be09f look check: badges, letters and the tree names stay on top): 4 draws.
    nU = std::min(nU, std::max(0, maxShapes - n)); nO = std::min(nO, std::max(0, maxShapes - n - nU));
    if (nU) { std::copy_backward(out, out + n, out + n + nU); std::copy(fxUnder.begin(), fxUnder.begin() + nU, out); n += nU; }
    std::copy(fxOver.begin(), fxOver.begin() + nO, out + n); n += nO;
    // Batch 3 merge (Boss: badges and text on top): the badge bodies after the over pass, before their letters; still 4 draws
    // (under fx, shapes, over fx, then badges and text on the same PS).
    PushBadges(s, drawn, out, n, maxShapes);   // feedback 1 item 3: over every icon and every effect
    const bool drawBadge = DrawBadgeShows(s);
    const float dbOff = cr * DrawBadgeRing, dbR = s.drawBadge.sizePx / 2.f;
    float dbx = cx + dbOff, dby = cy - dbOff;
    float dbA = A * (s.step.under.kind == Kind::Gestures ? DrawBadgeDimmed : 1.f);
    // T036 thirds: the marks' scale and opacity (the pointed one 11% larger and lit, the others at the dim row); the Draw glyph
    // badge rides the Glyphs mark, each bound sub-menu's badge its own mark.
    const bool marks = s.thirds && !holding && s.step.blurb < 0;
    auto markK = [&](int t) { return (1 + (MarkGrownScale / MarkScale - 1) * g) * (t == third ? 1.f + std::clamp(s.thirdGrowPct, 0.f, 100.f) / 100 : 1.f); };
    auto markOut = [&](int t) { return t == third ? (reach - cr) / 2 : 0.f; };
    auto markA = [&](int t) { return A * (t == ThirdGestures && !s.hasMotions ? ShutDim : third >= 0 && t != third ? dimK : 1.f); };
    if (s.thirds) { MenuBadgeAt(ThirdGlyphs, false, g, markK(ThirdGlyphs), dbx, dby, markOut(ThirdGlyphs)); dbA = markA(ThirdGlyphs); }
    // T036 (2026-11-16): the thirds' pictures, as seats (rim gold, the pointed one's the hover ring), before the badges so each
    // badge stays on top; drawn whenever the text pass below would draw the marks (a picture missing: its sigil there).
    if (s.thirds && !holding && !(s.blurb && s.step.blurb >= 0))
        for (int t = 0; t < ThirdCount; ++t)
            if (const int cell = s.thirdCell[size_t(t)]; cell >= 0) {
                float mx, my; ThirdMarkAt(t, g, mx, my, markOut(t));
                PushArtSeat(out, n, maxShapes, cell, s.atlasPct, mx, my, 20 * MarkScale * markK(t), t == third ? HoverRing : Gold, t == third ? 3.f : 2.f, markA(t));
            }
    if (drawBadge && (!s.thirds || marks)) PushBadgeBody(dbx, dby, dbR, 6.f * s.drawBadge.sizePx / 28, dbA, s.drawBadge.kind, out, n, maxShapes);   // #3
    std::array<float, ThirdCount> mbx{}, mby{};
    if (marks)
        for (int t = 0; t < ThirdCount; ++t) {
            const auto& b = s.menuBadge[size_t(t)];
            if (!b.on) continue;
            MenuBadgeAt(t, t == ThirdGlyphs && drawBadge, g, markK(t), mbx[size_t(t)], mby[size_t(t)], markOut(t));
            PushBadgeBody(mbx[size_t(t)], mby[size_t(t)], b.sizePx / 2.f, 6.f * b.sizePx / 28, markA(t), b.kind, out, n, maxShapes);
        }
    // ---- Text (the glyph atlas), on top ----
    if (!s.metrics || !s.metrics->ok) return n;
    const auto& m = *s.metrics;
    const int max = n + maxText;
    FooterText(s, m, A, out, n, max);
    for (int k = 0; k < nInitials; ++k)
        if (s.initials && s.initials[initials[size_t(k)].seat])
            EmitLine(m, text::Bold22, s.initials[initials[size_t(k)].seat], initials[size_t(k)].x, initials[size_t(k)].y, Parchment, initials[size_t(k)].a, out, n, max);
    BadgeLetters(s, m, drawn, out, n, max);
    if (drawBadge && (!s.thirds || marks)) PushBadgeLetter(m, s.drawBadge.letter, s.drawBadge.kind, dbx, dby, dbR, dbA, out, n, max);   // #3
    if (marks)
        for (int t = 0; t < ThirdCount; ++t)
            if (const auto& b = s.menuBadge[size_t(t)]; b.on) PushBadgeLetter(m, b.letter, b.kind, mbx[size_t(t)], mby[size_t(t)], b.sizePx / 2.f, markA(t), out, n, max);
    if (l.mode == TreeLayout::Shown)
        for (int t = 0; t < Trees; ++t) if (s.treeNames[size_t(t)] && !(s.chooseTitle && std::sin(l.dir[size_t(t)]) < -.5f)) {   // the chooser's title takes the top arc
            const float ta = A * s.fight * std::max(.4f, s.step.alpha[size_t(t)]);
            EmitArc(m, text::Name24, s.treeNames[size_t(t)], CanvasX + C, CanvasY + C, NameArcR, l.dir[size_t(t)], s.step.tree == t ? HoverRing : GoldLight, ta, out, n, max);
        }
    if (s.chooseTitle) {   // the chooser's order row (2026-11-14): its small name centered over the orders' seats; Quick cast's potions and scrolls (2026-11-16)
        float x0 = 1e9f, x1 = -1e9f, i0 = 1e9f, i1 = -1e9f;
        for (int i = 0; i < l.count; ++i) {
            const auto& seat = l.seats[size_t(i)];
            if (seat.tree < 0 && seat.owner == -1 && orders::Is(seat.id)) { x0 = std::min(x0, seat.x); x1 = std::max(x1, seat.x); }
            if (seat.tree < 0 && seat.owner == -1 && ItemSeat(seat.id)) { i0 = std::min(i0, seat.x); i1 = std::max(i1, seat.x); }
        }
        if (x1 >= x0) EmitLine(m, text::Footer15, orders::RowLabel(), CanvasX + (x0 + x1) / 2, CanvasY + ShelfY - orders::RowLabelDy, Muted, A * s.fight, out, n, max);
        if (i1 >= i0) EmitLine(m, text::Footer15, "POTIONS AND SCROLLS", CanvasX + (i0 + i1) / 2, CanvasY + ShelfY - orders::RowLabelDy, Muted, A * s.fight, out, n, max);
    }
    // The glyph chooser's title (designer, R7 answers 2): "Choose a skill for" muted and the shape in gold, one run centered on
    // the top arc at r 313 (review r7b Z1: the tree names' band, clear of the top icons), 22 px (kit titleArc).
    if (s.chooseTitle) {
        constexpr float r = NameArcR;
        const char* lead = s.chooseLead ? s.chooseLead : "Choose a skill for ";
        const float wm = text::Width(m, text::Name22, lead), wg = text::Width(m, text::Name22, s.chooseTitle), tot = (wm + wg) / r;
        EmitArc(m, text::Name22, lead, cx, cy, r, -Pi / 2 - tot / 2 + wm / r / 2, Muted, A, out, n, max);
        EmitArc(m, text::Name22, s.chooseTitle, cx, cy, r, -Pi / 2 + tot / 2 - wg / r / 2, GoldLight, A, out, n, max);
    }
    if (holding) {   // the held button in words where its picture goes (the picture set is L1's, later), the status below
        if (s.holdName) EmitLine(m, text::Body22, s.holdName, cx, cy + HoldNameY, Parchment, A, out, n, max);
        if (s.holdSlot) EmitLine(m, text::Label26, s.holdSlot, cx, cy - 14, GoldLight, A, out, n, max);
        if (s.holdTop) EmitLine(m, text::Body22, s.holdTop, cx, cy + HoldTopY, Muted, A, out, n, max);
        if (s.holdStatus) EmitLine(m, text::Body22, s.holdStatus, cx, cy + HoldLineY, s.holdGreen ? TickGreen : Muted, A, out, n, max);
    } else if (s.blurb && s.step.blurb >= 0) {
        const float ta = A * Ease((g - .55f) / .45f);   // the text fades in once the circle is 55% grown
        const int lines = int(s.blurb->lines.size());
        for (int k = 0; k < lines; ++k) {
            const auto& ln = s.blurb->lines[size_t(k)];
            const Rgb c = ln.kind == text::KName ? GoldLight : ln.kind == text::KSub ? Muted : Parchment;
            EmitLine(m, ln.style, ln.text, cx, cy + (k - (lines - 1) / 2.f) * text::LineH, c, ta, out, n, max);
        }
    } else if (s.thirds) {   // T036: each third's mark; the pointed third's name in the middle at the skill-name size, "Trigger to open"
        for (int t = 0; t < ThirdCount; ++t) {
            if (s.thirdCell[size_t(t)] >= 0) continue;   // its picture, drawn with the shapes (2026-11-16)
            float mx, my; ThirdMarkAt(t, g, mx, my, markOut(t));
            PushSigil(out, n, max, ThirdSigil[t], mx, my, t == third ? HoverRing : MarkSilver, markA(t), MarkScale * markK(t));
        }
        if (const float lines = Ease((g - .55f) / .45f); third >= 0 && lines > 0) {   // on the hub (the marks sit out in the ring by then)
            EmitLine(m, text::Name24, ThirdName[third], cx, cy - 13, GoldLight, A * lines, out, n, max);
            EmitLine(m, text::Bold22, third == ThirdQuick && s.quickWord ? s.quickWord : "Trigger to open", cx, cy + 17, third == ThirdQuick && s.quickWord ? Muted : Parchment, A * lines, out, n, max);
        }
    } else if (s.chooseTitle || s.hasMotions) {   // the split (L3): the main page's Glyphs / Gestures, or the chooser's Back / Clear
        const int pointed = s.step.under.kind == Kind::Glyphs ? 1 : s.step.under.kind == Kind::Gestures ? 2 : 0;
        if (s.chooseTitle) DrawSplit(m, {s.topLabel ? s.topLabel : "Back", Sigil::Back, s.chooseBack ? s.chooseBack : "To the glyphs", "Trigger to go back"},
                                     {s.bottomLabel ? s.bottomLabel : "Clear", Sigil::Cross, s.clearLine, "Trigger to clear"}, pointed, g, A, out, n, max, &s);
        else DrawSplit(m, {s.topLabel ? s.topLabel : "Glyphs", Sigil::Star, s.topCount, "Trigger to open"},
                       {s.bottomLabel ? s.bottomLabel : "Gestures", Sigil::Hand, s.bottomCount, "Trigger to open"}, pointed, g, A, out, n, max, &s);
    } else {
        EmitLine(m, text::Label22, "Glyphs", cx, cy - 4 - 30 * g, s.step.under.kind == Kind::Glyphs ? HoverRing : GoldLight, A, out, n, max);
    }
    return n;
}

// ---- When a frame composes (frame-budget section 2) ----
// The drawn state's key: anything here changing redraws. Transitions run at the display rate; ambient motion at half of it;
// the Animation row's Off (still) redraws only on a change. A compose the ring skipped (busy) stays due (perf R4 item 3).
// Every Scene field Build reads is here (review R4 J5): a step that makes Build read a new one adds it.
struct DrawKey {
    int under = -1, kind = 0, tree = -1, blurb = -1, picked = -1, pressed = -1, pressedKind = 0; bool locked = false, motions = true;
    uint32_t growQ = 0, alphaQ = 0, wheelQ = 0, blurbHash = 0, revision = 0, holdQ = 0; int holdSeat = -1; const void* holdSlot = nullptr; const void* holdStatus = nullptr; int layoutCount = 0, atlasPct = 0;
    std::array<const void*, 7> r7{};
    uint32_t fightQ = 0, burstQ = 0; const void* elements = nullptr; const void* crawl = nullptr; int crawlCount = 0;   // R8
    const void* rest = nullptr;   // R8 match stage 1
    int skillClass = -1;   // R7c: the tree marks' glyphs
    const void* badges = nullptr; uint32_t badgeRev = 0, badgePop = 0;   // feedback 1 item 3: the set, its placement, a pop or tick running
    DrawBadge drawBadge{};   // #3
    const void* holdTop = nullptr; bool holdGreen = false;
    const void* holdName = nullptr;   // designer gate D1
    int footer = 0;   // feedback 1 item 4: on, page, developer, width, hand (its hover: under)   // R7: topCount, bottomCount, topLabel, bottomLabel, chooseTitle, seatDim, clearLine (their text: the revision)
    const void* layout = nullptr; const void* cells = nullptr; const void* metrics = nullptr; const void* initials = nullptr; const void* blurbText = nullptr;
    std::array<const void*, Trees> names{};
    SeatLayers layers{}; bool thirds = false; int thirdQ = 0; std::array<DrawBadge, ThirdCount> menuBadge{}; const void* seatMark = nullptr; const char* quickWord = nullptr; const char* chooseLead = nullptr; const char* chooseBack = nullptr;   // T036; T035
    std::array<int16_t, ThirdCount> thirdCell{-1, -1, -1};   // T036 (2026-11-16): the thirds' pictures
    int treeQ = 0; std::array<int16_t, Trees> treeCell{-1, -1, -1};   // T025: the tree icon's mode x 1000 + size, and its pictures' cells
    glyphart::View glyph{};   // Glyph icon: its mode, size and the twelve cells
    bool operator==(const DrawKey&) const = default;
};
inline DrawKey KeyOf(const Scene& s) {
    DrawKey k; k.under = s.step.under.index; k.kind = int(s.step.under.kind); k.tree = s.step.tree; k.blurb = s.step.blurb;
    k.picked = s.step.picked; k.pressed = s.step.pressed.index; k.pressedKind = int(s.step.pressed.kind); k.locked = s.step.locked;
    k.motions = s.hasMotions;
    k.growQ = uint32_t(std::lround((s.grow >= 0 ? s.grow : s.step.centerGrown ? 1.f : 0.f) * 255));
    for (int t = 0; t < Trees; ++t) k.alphaQ = k.alphaQ * 257 + uint32_t(std::lround(s.step.alpha[size_t(t)] * 255));
    k.wheelQ = uint32_t(std::lround(std::clamp(s.wheelAlpha, 0.f, 1.f) * 255));
    k.layoutCount = s.layout ? s.layout->count : 0; k.atlasPct = s.atlasPct; k.revision = s.revision;
    k.holdQ = s.holdFrac < 0 ? 0 : 1 + uint32_t(std::lround(std::clamp(s.holdFrac, 0.f, 1.f) * 1000));
    k.holdSeat = s.holdSeat; k.holdSlot = s.holdSlot; k.holdStatus = s.holdStatus; k.holdTop = s.holdTop; k.holdGreen = s.holdGreen; k.holdName = s.holdName; k.drawBadge = s.drawBadge;
    k.layout = s.layout; k.cells = s.cells; k.metrics = s.metrics; k.initials = s.initials; k.blurbText = s.blurb;
    if (s.blurb) {   // the cache keeps one Blurb object: its text (a new help line on the same seat) is part of the key
        uint32_t h = 2166136261u;
        for (const auto& l : s.blurb->lines) for (const char c : l.text) { h ^= uint8_t(c); h *= 16777619u; }
        k.blurbHash = h;
    }
    for (int t = 0; t < Trees; ++t) k.names[size_t(t)] = s.treeNames[size_t(t)];
    k.r7 = {s.topCount, s.bottomCount, s.topLabel, s.bottomLabel, s.chooseTitle, s.seatDim, s.clearLine};
    k.fightQ = uint32_t(std::lround(std::clamp(s.fight, 0.f, 1.f) * 255));
    k.burstQ = s.layout && s.layout->mode == TreeLayout::Orbs && s.step.treeAgeMs < 700 ? 1 + uint32_t(s.step.treeAgeMs) : 0;   // a burst redraws each frame
    k.elements = s.elements; k.crawl = s.crawlIndex; k.crawlCount = s.crawlCount; k.skillClass = s.skillClass; k.rest = s.rest;
    if (s.badges) {
        k.badges = s.badges; k.badgeRev = s.badges->revision;
        for (int i = 0; i < s.badges->count; ++i) if (BadgeTick(s.tMs - s.badges->b[size_t(i)].bornMs) > 0) k.badgePop = 1 + uint32_t(s.tMs);   // a pop redraws each frame
    }
    k.footer = s.footer.on ? 1 + int(s.footer.page) * 16 + s.footer.developer * 2 + s.footer.fullWidth * 4 + s.footer.handWheel * 8 : 0;
    k.layers = s.layers; k.thirds = s.thirds; k.thirdQ = int(std::lround(s.thirdGrowPct * 10)) * 10000 + int(std::lround(s.thirdDimPct * 10));
    k.menuBadge = s.menuBadge; k.seatMark = s.seatMark; k.quickWord = s.quickWord; k.chooseLead = s.chooseLead; k.chooseBack = s.chooseBack;
    k.thirdCell = s.thirdCell;
    k.treeQ = s.treeMode * 1000 + treeart::SizePct(float(s.treeSizePct)); k.treeCell = s.treeCell;   // T025
    k.glyph = s.glyph;
    return k;
}
class ComposePolicy {
    DrawKey last{}, want{}; bool drawn = false, ambientDue = false; uint64_t frame = 0;
public:
    unsigned composes = 0, reused = 0, busy = 0;
    // True: compose this frame (then call Done with the result); false: re-submit the last image (or nothing when closed).
    bool Want(const Scene& s) {
        ++frame;
        if (!s.open || s.wheelAlpha <= 0) { drawn = false; ambientDue = false; return false; }
        want = KeyOf(s);
        // R8; T036: the rest glow and the crawl move only with the Animation layer On (no redraw for what is not drawn)
        const bool fx = s.layers.fx;
        const bool pointedFx = s.step.under.kind == Kind::Seat && (s.anim.hoverFx || s.anim.faceFx || (fx && s.anim.glow != GlowOff));
        const bool ambient = !s.anim.still && (s.anim.floats || (fx && (s.anim.glow == GlowEverywhere || s.anim.crawlS > 0)) || pointedFx);
        if (ambient && frame % 2 == 0) ambientDue = true;
        const bool yes = !drawn || !(want == last) || ambientDue;
        if (!yes) ++reused;
        return yes;
    }
    // The compose ran (true) or the ring was busy (false: the change stays due for the next frame).
    void Done(bool composed) {
        if (composed) { last = want; drawn = true; ambientDue = false; ++composes; } else ++busy;
    }
};

// ---- The 3-slot ring (frame-budget section 3): never wait; a busy slot skips the compose ----
class SlotRing {
    std::array<uint64_t, 3> fenceOf{};   // the fence value each slot was submitted with (0: never)
    int next = 0;
public:
    unsigned skippedBusy = 0;
    // The slot to record into, or -1 when it is still in flight (the caller re-submits the last image and counts it).
    int Acquire(uint64_t completed) {
        if (fenceOf[size_t(next)] > completed) { ++skippedBusy; return -1; }
        return next;
    }
    void Submitted(int slot, uint64_t fence) { if (slot >= 0 && slot < 3) { fenceOf[size_t(slot)] = fence; next = (slot + 1) % 3; } }
    uint64_t LastFence() const { return std::max({fenceOf[0], fenceOf[1], fenceOf[2]}); }
};

// ---- A window of cost samples (the E8 10 s line): mean, max and p95 from a 0.01 ms histogram up to 5 ms ----
class CostWindow {
    std::array<uint16_t, 501> bins{};
public:
    unsigned n = 0; double sum = 0, max = 0;
    void Add(double ms) {
        if (ms < 0) return;
        ++n; sum += ms; max = std::max(max, ms);
        auto& b = bins[size_t(std::min(500.0, ms * 100))];
        if (b < 0xFFFF) ++b;
    }
    double Mean() const { return n ? sum / n : 0; }
    double P95() const {
        if (!n) return 0;
        const unsigned want = unsigned(std::ceil(n * .95));
        unsigned seen = 0;
        for (size_t i = 0; i < bins.size(); ++i) { seen += bins[i]; if (seen >= want) return (double(i) + 1) / 100; }
        return max;
    }
    void Reset() { *this = CostWindow{}; }
};

// ---- The swapchain's content rectangle (plan E3): made once at the largest size; the content at the render scale ----
constexpr int SwapchainW = 1536, SwapchainH = 1320, ContentRows = 880;
struct ContentRect { int w = 0, h = 0; float x0 = 0, y0 = 0, canvasW = 0; };   // texels; the canvas px it starts at, its width in canvas px
inline ContentRect Content(float footerPx, int renderPct) {
    renderPct = ScalePct(renderPct);
    footerPx = std::clamp(footerPx, 768.f, 1024.f);
    ContentRect c;
    c.canvasW = footerPx; c.x0 = (CanvasPx - footerPx) / 2; c.y0 = CanvasY;
    c.w = int(std::lround(footerPx * renderPct / 100.0)); c.h = int(std::lround(ContentRows * renderPct / 100.0));
    return c;
}
}  // namespace framevr::ringwheel
