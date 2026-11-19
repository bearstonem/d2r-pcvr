#pragma once
// Ring skill menu (build-plan L1b "Tree orbs: open-tree state", Boss decision on Q1): the Tree orbs layout's
// orbs, their closed names and the center while a tree is open, as plain data (kit wheel.js drawOrb, runeRing, the layout e
// readout). Pure and Windows-free, like ring_wheel_draw.h, and drawn only with its recipes (disc, ring, rune band, tree glyph,
// the glyph atlas): no new shader code. Every function draws nothing unless the wheel is open in the Tree orbs layout, so
// Trees shown (the default) is untouched.
//
// Wiring: Build calls PushOrbs in place of its own orb branch and OrbTexts with the other text; while CenterOpen
// is true it draws PushOpenCenter in place of its center rings and skips the split. OrbGlow is the orb's rest glow for the
// under-face effect pass. CenterBacking and PushGesturesMark are the design's two Trees shown notes (r8-match-spec, stage 1
// redo): the kit's backing (75% black to r 84, gone by r 88) and the kit's Gestures mark.
//
// Type: the names use Name22 (Cinzel 700 22 px) and the
// help line Body22 (Alegreya Sans 500 22 px; the kit's 600 is not shipped), so no new glyph style is baked.
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include "ring_wheel_draw.h"

namespace framevr::ringwheel::orbs {
constexpr float RingR = 128, ClosedR = 34, OpenR = 40;   // L1b: orbs at r 128 from C, -90 / 30 / 150 deg
constexpr float NameWrap = 120, CenterNameWrap = 128, CenterHelpWrap = 168;
constexpr float TopNameOut = 18, SideNameOut = 62;       // the name past the orb's edge: above the top orb, sideways for the others
constexpr float CenterNameDy = -24, CenterHelpDy = 18;   // the open tree's name and "Point at a skill", from C
constexpr float BackingSolid = 4, BackingFeather = 4;    // the kit's center backing: 75% black to r + 4, gone by r + 8
constexpr float SpinTurnMs = 20000;                      // kit runeRing: one turn per 20 s at spin 1
constexpr float ClosedSpin = .3f, OpenSpin = 1;
constexpr text::Style NameStyle = text::Name22, HelpStyle = text::Body22;
constexpr const char* PointAtASkill = "Point at a skill";
constexpr int MaxLines = 4;

inline float LineStep(text::Style st) { return text::Specs[st].px * 1.15f; }   // kit wrapText: size x 1.15
inline bool Active(const Scene& s) { return s.open && s.wheelAlpha > 0 && s.layout && s.layout->mode == TreeLayout::Orbs; }
// The center's grow 0-1, as Build reads it.
inline float GrowOf(const Scene& s) {
    if (s.holdFrac >= 0) return 1;
    return s.grow >= 0 ? std::clamp(s.grow, 0.f, 1.f) : s.step.centerGrown ? 1.f : 0.f;
}
// The open tree (the pointed one), -1 none.
inline int OpenTree(const Scene& s) { return Active(s) && s.step.tree >= 0 && s.step.tree < Trees ? s.step.tree : -1; }
// L1b center: a tree open, no skill pointed (no blurb), no button held, not the glyph chooser.
inline bool CenterOpen(const Scene& s) { return OpenTree(s) >= 0 && s.step.blurb < 0 && s.holdFrac < 0 && !s.chooseTitle; }

struct Orb { float x = 0, y = 0, R = 0, alpha = 0; bool open = false; };   // canvas px; alpha includes the wheel's
// Orb t as drawn: open R 40, closed R 34; with the center grown it fades out by half the grow and shrinks to 65%.
inline Orb OrbOf(const Scene& s, int t) {
    Orb o;
    if (!Active(s) || t < 0 || t >= Trees) return o;
    const float g = GrowOf(s), dir = s.layout->dir[size_t(t)];
    o.open = s.step.tree == t;
    o.R = (o.open ? OpenR : ClosedR) * (1 - .35f * g);
    o.x = CanvasCx + RingR * std::cos(dir); o.y = CanvasCy + RingR * std::sin(dir);
    o.alpha = std::clamp(s.wheelAlpha, 0.f, 1.f) * (1 - std::min(1.f, g / .5f));
    return o;
}
// Kit drawOrb's ring color: closed, the accent + 60 per channel; open, the hover cream.
inline Rgb RingColor(const Scene& s, int t, bool open) {
    if (open) return HoverRing;
    const Rgb a = TreeAccentOf(s.skillClass, t);
    return {std::min(1.f, a.r + 60 / 255.f), std::min(1.f, a.g + 60 / 255.f), std::min(1.f, a.b + 60 / 255.f)};
}
// The rune band's turn (radians): kit rot = t / 20000 x TAU x spin; none when the Animation row is still (it redraws on a
// change only).
inline float SpinOf(const Scene& s, bool open) {
    if (s.anim.still) return 0;
    const double turns = std::fmod(s.tMs / SpinTurnMs * (open ? OpenSpin : ClosedSpin), 1.0);
    return float(turns * Tau);
}

// ---- Shapes ----
// The kit's center backing at center radius cr: 75% black to cr + 4, fading out by cr + 8.
inline Instance CenterBacking(float cx, float cy, float cr, float a) {
    const float d = 2 * (cr + BackingSolid + BackingFeather);
    return {cx, cy, d, d, 0, 0, 0, 0, 0, 0, 0, 0, .75f * a, RecipeDisc, BackingFeather};
}
// Each orb (kit drawOrb): the glass (accent / 4 at 75%, the accent's light from the upper left at 35%, 55% open), the rune ring
// (2 px at R and R - 11, the band between; closed 80% in the accent + 60 turning once a 67 s, open full in the hover cream once
// a 20 s) and the tree's glyph (R x 0.45, parchment). The open orb keeps its ring. Returns the instances pushed.
inline int PushOrbs(const Scene& s, Instance* out, int& n, int max) {
    const int n0 = n;
    if (!Active(s)) return 0;
    for (int t = 0; t < Trees; ++t) {
        const Orb o = OrbOf(s, t);
        if (o.alpha <= 0) continue;
        const Rgb ac = TreeAccentOf(s.skillClass, t), rc = RingColor(s, t, o.open);
        const float glass = 2 * (o.R - 8), lit = .7f * (o.R - 8), off = .15f * o.R;
        Push(out, n, max, {o.x, o.y, glass, glass, 0, 0, 0, 0, 0, ac.r / 4, ac.g / 4, ac.b / 4, .75f * o.alpha, RecipeDisc, 1.5f});
        Push(out, n, max, {o.x - off, o.y - off, 2 * lit, 2 * lit, 0, 0, 0, 0, 0, ac.r, ac.g, ac.b, (o.open ? .55f : .35f) * o.alpha, RecipeDisc, .6f * lit});
        const float ra = (o.open ? 1.f : .8f) * o.alpha, d = 2 * o.R;
        Push(out, n, max, {o.x, o.y, d, d, 0, 0, 0, 0, 0, rc.r, rc.g, rc.b, ra, RecipeRing, 2});
        Push(out, n, max, {o.x, o.y, d - 22, d - 22, 0, 0, 0, 0, 0, rc.r, rc.g, rc.b, ra, RecipeRing, 2});
        const float seed = o.open ? OpenR : ClosedR;   // the unshrunk radius: no rune changes count or shape while the center grows
        Push(out, n, max, {o.x, o.y, d, d, SpinOf(s, o.open), seed, 0, seed, 0, rc.r, rc.g, rc.b, ra, RecipeRunes, float(RuneCount(seed))});
        const MarkGlyph glyph = TreeGlyphOf(s.skillClass, t);
        const float gr = .45f * o.R;
        if (glyph != MarkGlyph::None)
            Push(out, n, max, {o.x, o.y, 2 * gr + 4, 2 * gr + 4, 0, gr, 0, gr, 0, Parchment.r, Parchment.g, Parchment.b, o.alpha, RecipeTreeGlyph, float(glyph)});
    }
    return n - n0;
}
// The orb's rest glow under its glass (kit drawOrb: the tree's first skill's first rest effect at R - 6, the under pass): for
// Build's under-face effect list. False when the tree has no seat or no under-face kind.
inline bool OrbGlow(const Scene& s, int t, Instance& out) {
    const Orb o = OrbOf(s, t);
    if (o.alpha <= 0) return false;
    const auto& l = *s.layout;
    int first = -1;
    for (int i = 0; i < l.count; ++i) if (l.seats[size_t(i)].tree == t) { first = i; break; }
    if (first < 0) return false;
    RestGlow glow;
    if (s.rest) glow = s.rest[first];
    else glow.kind[0] = RestKind(s.elements ? std::min<uint32_t>(s.elements[first], uint32_t(Element::Count) - 1) : uint32_t(Element::Phys));
    for (int k = 0; k < 2; ++k) {
        const RestKind kind = glow.kind[k];
        if (!ValidKind(kind) || OverFace(kind)) continue;
        const Rgb c = glow.rgb[k] ? Hex(glow.rgb[k]) : Rgb{1, 1, 1};
        const float capR = RestCapPx * 2 * (o.R - 6) / IconPx, seed = Seed(l.seats[size_t(first)].id);
        const float tS = s.anim.still || !s.anim.glow ? 0.f : float(std::fmod(s.tMs / 1000.0, 3600.0));
        out = {o.x, o.y, 2 * capR, 2 * capR, 0, tS, seed, -1, seed, c.r, c.g, c.b, o.alpha, RecipeFx, float(uint32_t(kind))};
        return true;
    }
    return false;
}
// The center while a tree is open: the kit's backing (75% to r 84, gone by r 88) and the center rune ring at r 80 in gold
// light at 55%, not turning. In place of Build's center rings while CenterOpen.
inline int PushOpenCenter(const Scene& s, Instance* out, int& n, int max) {
    if (!CenterOpen(s)) return 0;
    const int n0 = n;
    const float A = std::clamp(s.wheelAlpha, 0.f, 1.f), cx = CanvasCx, cy = CanvasCy, cr = CenterR, a = .55f * A;
    Push(out, n, max, CenterBacking(cx, cy, cr, A));
    Push(out, n, max, {cx, cy, 2 * cr, 2 * cr, 0, 0, 0, 0, 0, GoldLight.r, GoldLight.g, GoldLight.b, a, RecipeRing, 2});
    Push(out, n, max, {cx, cy, 2 * (cr - 11), 2 * (cr - 11), 0, 0, 0, 0, 0, GoldLight.r, GoldLight.g, GoldLight.b, a, RecipeRing, 2});
    Push(out, n, max, {cx, cy, 2 * cr, 2 * cr, 0, cr, 0, cr, 0, GoldLight.r, GoldLight.g, GoldLight.b, a, RecipeRunes, float(RuneCount(cr))});
    return n - n0;
}
// The kit's Gestures mark (drawSmallSigil 'hand': an arc of r 9 over (0, 6), 1.05 to 1.95 pi, and four rays from r 9 to 15)
// in 2 px strokes on a 4 px dark underlay, as PushSigil draws.
inline void PushGesturesMark(Instance* out, int& n, int max, float x, float y, Rgb c, float a) {
    float p[10][4]; int k = 0;
    for (int i = 0; i < 6; ++i) {
        const float a0 = Pi * (1.05f + .15f * float(i)), a1 = Pi * (1.05f + .15f * float(i + 1));
        p[k][0] = 9 * std::cos(a0); p[k][1] = 6 + 9 * std::sin(a0); p[k][2] = 9 * std::cos(a1); p[k][3] = 6 + 9 * std::sin(a1); ++k;
    }
    for (int i = 0; i < 4; ++i) {
        const float an = Pi * (1.15f + .23f * float(i));
        p[k][0] = 9 * std::cos(an); p[k][1] = 6 + 9 * std::sin(an); p[k][2] = 15 * std::cos(an); p[k][3] = 6 + 15 * std::sin(an); ++k;
    }
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < k; ++i)
            PushLine(out, n, max, x + p[i][0], y + p[i][1], x + p[i][2], y + p[i][3], pass ? 2.f : 4.f, pass ? c : Rgb{0, 0, 0}, pass ? a : .6f * a);
}

// ---- Text ----
// Kit wrapText: words onto lines no wider than `wrap` (a single longer word keeps its own line). No allocation: views into s.
struct Lines { std::array<std::string_view, MaxLines> line{}; int count = 0; float widest = 0; };
inline Lines Wrap(const text::Metrics& m, text::Style st, std::string_view s, float wrap) {
    Lines out;
    size_t start = 0, end = 0;   // the current line: s[start, end)
    for (size_t i = 0; i <= s.size();) {
        size_t j = s.find(' ', i);
        if (j == std::string_view::npos) j = s.size();
        if (j > i) {
            const std::string_view test = s.substr(start, j - start);
            if (end > start && text::Width(m, st, test) > wrap) {
                if (out.count < MaxLines) out.line[size_t(out.count++)] = s.substr(start, end - start);
                start = i;
            }
            end = j;
        } else if (end == start) start = j + 1;
        i = j + 1;
    }
    if (end > start && out.count < MaxLines) out.line[size_t(out.count++)] = s.substr(start, end - start);
    for (int k = 0; k < out.count; ++k) out.widest = std::max(out.widest, text::Width(m, st, out.line[size_t(k)]));
    return out;
}
// Lines centered on (cx, cy), one step apart (kit wrapText).
inline void EmitLines(const text::Metrics& m, text::Style st, const Lines& ls, float cx, float cy, Rgb c, float a, Instance* out, int& n, int max) {
    for (int k = 0; k < ls.count; ++k)
        EmitLine(m, st, ls.line[size_t(k)], cx, cy + (float(k) - (ls.count - 1) / 2.f) * LineStep(st), c, a, out, n, max);
}
// Where a closed orb's name block is centered (kit drawOrb): the top orb's above it (R + 18), a side orb's sideways away from
// the center (R + 62), same height. At our 22 px floor the kit's spots touch the orb (orbs test), so two rules keep them
// clear: a block of two lines grows away from its orb (the line nearest the orb stays at the kit's spot), and a side name
// wider than the kit's 120 px keeps its inner edge SideGap past the ring.
constexpr float SideGap = 8;
inline void NameAt(const Scene& s, int t, const Orb& o, const Lines& ls, float& x, float& y) {
    const float a = s.layout->dir[size_t(t)], sn = std::sin(a);
    const int vert = std::fabs(sn) > .5f ? (sn > 0 ? 1 : -1) : 0;
    const float side = std::cos(a) >= 0 ? 1.f : -1.f, grow = (ls.count - 1) * LineStep(NameStyle) / 2;
    x = o.x + (vert ? 0 : side * (o.R + std::max(SideNameOut, SideGap + ls.widest / 2)));
    y = o.y + (vert ? float(vert) * (o.R + TopNameOut + grow) : 0);
}
// The open tree's name and "Point at a skill" in the center circle: the kit's C - 24 and C + 18, kept inside the ring and
// apart. At 22 px a wide name ("Summoning", 141 px) moves toward C, and a two-line one ("Offensive / Auras") pushes the help
// line down, 1 px at a time, until every glyph's far corner is CenterTextR from C or less and the two blocks keep CenterGap
// between their glyphs. Measured on the glyph quads as EmitLine places them, less the atlas's unused padding (it pads
// ceil(2.5) + 1 px round a 2.5 px outline). Nothing fits (no upstream name needs it): the kit's places.
constexpr float CenterTextR = CenterR - 2, CenterGap = 2, GlyphInset = 1.5f;   // 2 px inside the ring's outer edge, 2 px between
struct Extent { float top = 1e9f, bottom = -1e9f, far = 0; };   // from C: the drawn glyphs' top and bottom, the farthest corner
inline void Measure(const text::Metrics& m, text::Style st, const Lines& ls, float cy, Extent& e) {
    for (int k = 0; k < ls.count; ++k) {
        const std::string_view s = ls.line[size_t(k)];
        const float y = cy + (float(k) - (ls.count - 1) / 2.f) * LineStep(st), base = text::Baseline(m, st, y);
        float pen = -text::Width(m, st, s) / 2;
        for (size_t i = 0; i < s.size();) {
            const auto& gl = m.g[size_t(st)][size_t(text::SlotOf(text::Next(s, i)))];
            if (gl.w > 0) {
                const float x0 = pen + gl.ox + GlyphInset, x1 = pen + gl.ox + gl.w - GlyphInset, y0 = base + gl.oy + GlyphInset, y1 = base + gl.oy + gl.h - GlyphInset;
                e.top = std::min(e.top, y0); e.bottom = std::max(e.bottom, y1);
                e.far = std::max(e.far, std::hypot(std::max(std::fabs(x0), std::fabs(x1)), std::max(std::fabs(y0), std::fabs(y1))));
            }
            pen += gl.advance;
        }
    }
}
struct CenterPlace { float nameY = CenterNameDy, helpY = CenterHelpDy; bool fits = false; };   // from C
inline CenterPlace CenterLayout(const text::Metrics& m, const Lines& name, const Lines& help) {
    for (float dy = CenterNameDy; dy <= 0; dy += 1) {
        CenterPlace p; p.nameY = dy;
        Extent en, eh;
        Measure(m, NameStyle, name, dy, en);
        Measure(m, HelpStyle, help, 0, eh);
        p.helpY = std::max(CenterHelpDy, name.count ? en.bottom + CenterGap - eh.top : CenterHelpDy);
        Extent all = en;
        Measure(m, HelpStyle, help, p.helpY, all);
        if (all.far <= CenterTextR) { p.fits = true; return p; }
    }
    return {};
}
// The text: each closed orb's tree name (hidden on the open orb: the fan names it), and while CenterOpen the open tree's name
// with "Point at a skill" under it. Names come from Scene::treeNames (upstream's short names until the player picks). Returns the
// glyphs pushed.
inline int OrbTexts(const Scene& s, const text::Metrics& m, Instance* out, int& n, int max) {
    if (!Active(s) || !m.ok) return 0;
    const int n0 = n;
    for (int t = 0; t < Trees; ++t) {
        const char* name = s.treeNames[size_t(t)];
        const Orb o = OrbOf(s, t);
        if (!name || o.open || o.alpha <= 0) continue;
        const Lines ls = Wrap(m, NameStyle, name, NameWrap);
        float x, y; NameAt(s, t, o, ls, x, y);
        EmitLines(m, NameStyle, ls, x, y, GoldLight, o.alpha, out, n, max);
    }
    if (CenterOpen(s)) {
        const float A = std::clamp(s.wheelAlpha, 0.f, 1.f);
        const char* name = s.treeNames[size_t(s.step.tree)];
        const Lines ls = name ? Wrap(m, NameStyle, name, CenterNameWrap) : Lines{}, help = Wrap(m, HelpStyle, PointAtASkill, CenterHelpWrap);
        const CenterPlace p = CenterLayout(m, ls, help);
        EmitLines(m, NameStyle, ls, CanvasCx, CanvasCy + p.nameY, GoldLight, A, out, n, max);
        EmitLines(m, HelpStyle, help, CanvasCx, CanvasCy + p.helpY, Muted, A, out, n, max);
    }
    return n - n0;
}
}  // namespace framevr::ringwheel::orbs
