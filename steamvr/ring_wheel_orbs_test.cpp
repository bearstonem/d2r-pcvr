// Ring skill menu (, build-plan L1b): Tree orbs' open-tree state (steamvr/ring_wheel_orbs.h). Measured with the real glyph
// metrics: upstream's menu fonts through the atlas worker's own BuildGlyphs (DirectWrite; no GPU), so every width is the one
// the wheel draws with.
#include "ring_wheel_orbs.h"
#include "skill_categories.h"
#include "../vr/ring_wheel_atlas.h"
#include <cstdio>
#include <string>
#include <vector>

namespace rw = framevr::ringwheel;
namespace ob = framevr::ringwheel::orbs;
namespace sc = framevr::skillcategories;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    if (!ok) { ++failures; std::printf("FAIL: %s\n", what.c_str()); }
}
static const char* const ClassNames[8] = {"Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin", "Warlock"};

// A class's skills in tree order (each skill's skillcategories page placed by the class's tree order).
static rw::Layout ClassLayout(int cls) {
    const auto& c = sc::Classes[size_t(cls)];
    std::vector<rw::SeatIn> in;
    for (int t = 0; t < rw::Trees; ++t)
        for (int k = 0; c.pages[k]; ++k)
            if (rw::PositionOf(cls, c.pages[k] - '1') == t) in.push_back({c.first + k, -1, t});
    return rw::Build(in.data(), int(in.size()), rw::TreeLayout::Orbs);
}
// The distance from (px, py) to a glyph's quad, inset by its unused padding (the atlas pads ceil(2.5) + 1 px for a 2.5 px
// outline, so the drawn outline ends 1.5 px inside the quad).
constexpr float Inset = 1.5f;
static float RectDist(const rw::Instance& g, float px, float py) {
    const float hx = g.w / 2 - Inset, hy = g.h / 2 - Inset;
    const float dx = std::max(0.f, std::fabs(px - g.cx) - hx), dy = std::max(0.f, std::fabs(py - g.cy) - hy);
    return std::hypot(dx, dy);
}
static float FarCorner(const rw::Instance& g, float px, float py) {
    const float hx = g.w / 2 - Inset, hy = g.h / 2 - Inset;
    return std::hypot(std::fabs(px - g.cx) + hx, std::fabs(py - g.cy) + hy);
}

int main() {
    rw::Fonts fonts;
    if (!rw::FontsFromFolder(RING_WHEEL_FONTS, fonts)) { std::printf("FAIL: fonts not found\n"); return 1; }
    rw::AtlasImage img; rw::text::Metrics m; std::string error;
    if (!rw::BuildGlyphs(fonts, 100, img, m, error)) { std::printf("FAIL: glyphs: %s\n", error.c_str()); return 1; }
    std::array<rw::Instance, rw::MaxDraw> buf{};
    auto base = [&](const rw::Layout& l, int cls, int open) {
        rw::Scene s; s.open = true; s.layout = &l; s.skillClass = cls; s.metrics = &m; s.grow = 0;
        s.step.tree = open;
        for (int t = 0; t < rw::Trees; ++t) s.step.alpha[size_t(t)] = open < 0 || open == t ? 1.f : 0.f;
        for (int t = 0; t < rw::Trees; ++t) s.treeNames[size_t(t)] = sc::Name(cls, rw::PageAt(cls, t) + 1);
        return s;
    };
    {   // Wrap: kit wrapText, a long word keeps its own line
        const auto ls = ob::Wrap(m, ob::NameStyle, "Bow & Crossbow", ob::NameWrap);
        Check(ls.count == 2 && ls.line[0] == "Bow &" && ls.line[1] == "Crossbow", "wrap: Bow & / Crossbow (" + std::to_string(ls.count) + " lines)");
        const auto one = ob::Wrap(m, ob::NameStyle, "Fire", ob::NameWrap);
        Check(one.count == 1 && one.line[0] == "Fire", "wrap: one word, one line");
        Check(ob::Wrap(m, ob::NameStyle, "", ob::NameWrap).count == 0, "wrap: nothing, no line");
    }
    // Trees shown (the default): nothing at all, whatever the step says.
    for (int cls = 0; cls < 8; ++cls) {
        auto in = ClassLayout(cls);
        in.mode = rw::TreeLayout::Shown;
        for (int open = -1; open < rw::Trees; ++open) {
            rw::Scene s = base(in, cls, open);
            int n = 0;
            const int shapes = ob::PushOrbs(s, buf.data(), n, rw::MaxInstances);
            const int center = ob::PushOpenCenter(s, buf.data(), n, rw::MaxInstances);
            const int glyphs = ob::OrbTexts(s, m, buf.data(), n, rw::MaxDraw);
            rw::Instance glow;
            Check(n == 0 && shapes == 0 && center == 0 && glyphs == 0 && !ob::CenterOpen(s) && !ob::OrbGlow(s, 0, glow),
                  std::string("Trees shown draws nothing (") + ClassNames[cls] + ", open " + std::to_string(open) + ": " + std::to_string(n) + ")");
        }
    }
    {   // a closed or faded wheel draws nothing
        const auto l = ClassLayout(1);
        rw::Scene s = base(l, 1, 0);
        s.open = false; int n = 0;
        ob::PushOrbs(s, buf.data(), n, rw::MaxInstances); ob::OrbTexts(s, m, buf.data(), n, rw::MaxDraw); ob::PushOpenCenter(s, buf.data(), n, rw::MaxInstances);
        Check(n == 0, "closed wheel: nothing");
        s.open = true; s.wheelAlpha = 0; n = 0;
        ob::PushOrbs(s, buf.data(), n, rw::MaxInstances); ob::OrbTexts(s, m, buf.data(), n, rw::MaxDraw); ob::PushOpenCenter(s, buf.data(), n, rw::MaxInstances);
        Check(n == 0, "faded wheel: nothing");
    }
    float worstName = 1e9f, worstCenter = 0, worstEdge = 0;
    std::string worstNameAt, worstCenterAt;
    for (int cls = 0; cls < 8; ++cls) {
        const auto l = ClassLayout(cls);
        for (int open = -1; open < rw::Trees; ++open) {
            const std::string at = std::string(ClassNames[cls]) + (open < 0 ? ", no tree open" : std::string(", ") + sc::Name(cls, rw::PageAt(cls, open) + 1) + " open");
            rw::Scene s = base(l, cls, open);
            // The orbs: the open one keeps its ring, full and in the hover cream; the closed ones at 80% in the accent + 60.
            int n = 0;
            ob::PushOrbs(s, buf.data(), n, rw::MaxInstances);
            int rings = 0, openRings = 0, runes = 0;
            for (int i = 0; i < n; ++i) {
                const auto& q = buf[size_t(i)];
                if (q.recipe == rw::RecipeRunes) ++runes;
                if (q.recipe != rw::RecipeRing) continue;
                ++rings;
                if (q.w > 2 * ob::ClosedR + 1) openRings += q.a == 1 && q.r == rw::HoverRing.r && q.g == rw::HoverRing.g;
                else if (q.w > 2 * ob::ClosedR - 1) Check(std::fabs(q.a - .8f) < 1e-4f, "closed orb ring at 80% (" + at + ")");
            }
            Check(rings == 6 && runes == 3, "three orbs, each two rings and a rune band (" + at + ": " + std::to_string(rings) + " rings, " + std::to_string(runes) + " bands)");
            Check(openRings == (open >= 0 ? 1 : 0), "the open orb keeps its ring, full, hover cream (" + at + ")");
            // The names alone (a skill pointed: no center text), then with the center.
            rw::Scene names = s; names.step.blurb = 0;
            n = 0;
            const int nNames = ob::OrbTexts(names, m, buf.data(), n, rw::MaxDraw);
            std::array<float, rw::Trees> drawnR{};
            for (int t = 0; t < rw::Trees; ++t) drawnR[size_t(t)] = ob::OrbOf(s, t).R;
            int perOrb[rw::Trees] = {};
            for (int i = 0; i < nNames; ++i) {
                const auto& g = buf[size_t(i)];
                // Its orb: the nearest one.
                int mine = 0; float best = 1e9f;
                for (int t = 0; t < rw::Trees; ++t) { const auto o = ob::OrbOf(s, t); const float d = std::hypot(g.cx - o.x, g.cy - o.y); if (d < best) { best = d; mine = t; } }
                ++perOrb[mine];
                for (int t = 0; t < rw::Trees; ++t) {   // clear of every orb (its ring's outer edge) by 2 px
                    const auto o = ob::OrbOf(s, t);
                    const float d = RectDist(g, o.x, o.y) - o.R;
                    if (d < worstName) { worstName = d; worstNameAt = at + ", orb " + std::to_string(t); }
                    Check(d >= 2, "name clear of orb " + std::to_string(t) + " by 2 px (" + at + ": " + std::to_string(d) + ")");
                }
                const float dc = RectDist(g, rw::CanvasCx, rw::CanvasCy) - rw::CenterR;   // clear of the center ring
                Check(dc >= 2, "name clear of the center by 2 px (" + at + ": " + std::to_string(dc) + ")");
                // Clear of the open tree's icons at rest: the rim's outer edge (r 22 + 3.5), the float (5 px), 2 px.
                for (int k = 0; k < l.count; ++k) {
                    const auto& seat = l.seats[size_t(k)];
                    if (seat.tree < 0 || seat.tree != open) continue;
                    const float d = RectDist(g, rw::CanvasX + seat.x, rw::CanvasY + seat.y) - (rw::IconPx / 2 + 3.5f + 5);
                    if (d < worstName) { worstName = d; worstNameAt = at + ", seat " + std::to_string(seat.id); }
                    Check(d >= 2, "name clear of the open tree's icon " + std::to_string(seat.id) + " by 2 px (" + at + ": " + std::to_string(d) + ")");
                }
                const float edge = FarCorner(g, rw::CanvasCx, rw::CanvasCy);
                worstEdge = std::max(worstEdge, edge);
                Check(edge <= rw::ContentR, "name inside the 320 px circle (" + at + ": " + std::to_string(edge) + ")");
            }
            for (int t = 0; t < rw::Trees; ++t)
                Check((perOrb[t] > 0) == (t != open), "a closed orb shows its name, the open one none (" + at + ", orb " + std::to_string(t) + ": " + std::to_string(perOrb[t]) + " glyphs)");
            // The center: with a tree open and nothing pointed, its name and "Point at a skill"; else nothing.
            n = 0;
            const int all = ob::OrbTexts(s, m, buf.data(), n, rw::MaxDraw);
            const int nCenter = all - nNames;
            Check(ob::CenterOpen(s) == (open >= 0), "the open center only with a tree open (" + at + ")");
            if (open < 0) { Check(nCenter == 0, "no tree open: no center text (" + at + ")"); continue; }
            const std::string_view tree = sc::Name(cls, rw::PageAt(cls, open) + 1);
            const auto nameLines = ob::Wrap(m, ob::NameStyle, tree, ob::CenterNameWrap);
            int letters = 0;
            for (int k = 0; k < nameLines.count; ++k) for (char ch : nameLines.line[size_t(k)]) letters += ch != ' ';
            int help = 0; for (char ch : std::string_view(ob::PointAtASkill)) help += ch != ' ';
            Check(nCenter == letters + help, "center: the open tree's name and Point at a skill (" + at + ": " + std::to_string(nCenter) + " glyphs, want " + std::to_string(letters + help) + ")");
            Check(ob::CenterLayout(m, nameLines, ob::Wrap(m, ob::HelpStyle, ob::PointAtASkill, ob::CenterHelpWrap)).fits, "center: name and help fit the ring (" + at + ")");
            float nameHelp = 1e9f;   // the name's glyphs (first) clear of the help line's (after) by 2 px
            for (int i = nNames; i < nNames + letters && i < all; ++i)
                for (int j = nNames + letters; j < all; ++j) {
                    const auto& a = buf[size_t(i)]; const auto& b = buf[size_t(j)];
                    const float gx = std::fabs(a.cx - b.cx) - (a.w + b.w) / 2 + 2 * Inset, gy = std::fabs(a.cy - b.cy) - (a.h + b.h) / 2 + 2 * Inset;
                    nameHelp = std::min(nameHelp, std::max(gx, gy));
                }
            Check(nameHelp >= 2, "center: the tree name clear of Point at a skill by 2 px (" + at + ": " + std::to_string(nameHelp) + ")");
            for (int i = nNames; i < all; ++i) {   // inside the center's outer ring stroke (78-80)
                const float r = FarCorner(buf[size_t(i)], rw::CanvasCx, rw::CanvasCy);
                if (r > worstCenter) { worstCenter = r; worstCenterAt = at; }
                Check(r <= rw::CenterR - 2, "center text inside the ring (" + at + ": " + std::to_string(r) + ")");
            }
            int nc = 0;
            Check(ob::PushOpenCenter(s, buf.data(), nc, rw::MaxInstances) == 4, "the open center: backing, two rings, rune band (" + at + ")");
            // A pointed skill (its blurb) or a held button replaces the center text.
            rw::Scene held = s; held.holdFrac = .5f; n = 0;
            Check(ob::OrbTexts(held, m, buf.data(), n, rw::MaxDraw) == 0 && !ob::CenterOpen(held), "a held button: no orb text, no open center (" + at + ")");
        }
    }
    std::printf("tree orbs: names %.1f px clear at the closest (%s); center text out to r %.1f (%s); names out to r %.1f\n",
                worstName, worstNameAt.c_str(), worstCenter, worstCenterAt.c_str(), worstEdge);
    {   // for the designer's sheet: the center names that leave the kit's C - 24 / C + 18 (22 px floor)
        std::string moved;
        const auto help = ob::Wrap(m, ob::HelpStyle, ob::PointAtASkill, ob::CenterHelpWrap);
        for (const auto& c : sc::Classes) for (const char* nm : c.names) {
            const auto p = ob::CenterLayout(m, ob::Wrap(m, ob::NameStyle, nm, ob::CenterNameWrap), help);
            const std::string item = std::string(nm) + " " + std::to_string(int(p.nameY)) + "/+" + std::to_string(int(p.helpY));
            if ((p.nameY != ob::CenterNameDy || p.helpY != ob::CenterHelpDy) && moved.find(item) == std::string::npos) moved += (moved.empty() ? "" : ", ") + item;
        }
        std::printf("tree orbs: center names off the kit's -24/+18: %s\n", moved.empty() ? "none" : moved.c_str());
    }
    {   // the rune ring's turn: open once a 20 s, closed once a 67 s; none with the Animation row still
        const auto l = ClassLayout(1);
        rw::Scene s = base(l, 1, 0);
        s.tMs = 5000;
        Check(std::fabs(ob::SpinOf(s, true) - rw::Tau / 4) < 1e-3f, "open ring: a quarter turn in 5 s");
        s.tMs = 20000 / .3 / 2;
        Check(std::fabs(ob::SpinOf(s, false) - rw::Pi) < 1e-3f, "closed ring: half a turn in 33 s");
        s.anim.still = true;
        Check(ob::SpinOf(s, true) == 0 && ob::SpinOf(s, false) == 0, "still: the rings do not turn");
    }
    {   // the orb rune bands keep their count and seed while the center grows (the orbs shrink to 65%): no rune re-rolls
        const auto l = ClassLayout(1);
        rw::Scene s = base(l, 1, 0);
        std::array<float, 6> rest{}, grown{};
        auto bands = [&](float g, std::array<float, 6>& v) {
            s.grow = g; int n = 0, k = 0;
            ob::PushOrbs(s, buf.data(), n, rw::MaxInstances);
            for (int i = 0; i < n; ++i) if (buf[size_t(i)].recipe == rw::RecipeRunes && k < 3) { v[size_t(k)] = buf[size_t(i)].param; v[size_t(3 + k)] = buf[size_t(i)].u0; ++k; }
        };
        bands(0, rest); bands(.3f, grown);
        Check(rest == grown && rest[0] > 0, "orb rune bands: the same count and seed at grow 0 and 0.3");
    }
    {   // the kit's backing: 75% to r 84, gone by r 88 at rest
        const auto b = ob::CenterBacking(rw::CanvasCx, rw::CanvasCy, rw::CenterR, 1);
        Check(b.w == 176 && b.param == 4 && b.recipe == rw::RecipeDisc && std::fabs(b.a - .75f) < 1e-6f, "center backing: 75% black to r 84, gone by r 88");
    }
    {   // the kit's Gestures mark: 10 strokes on their underlay, inside a 32 px box
        int n = 0;
        ob::PushGesturesMark(buf.data(), n, rw::MaxInstances, 500, 500, rw::Gold, 1);
        bool inside = true;
        for (int i = 0; i < n; ++i) inside = inside && std::fabs(buf[size_t(i)].cx - 500) < 16 && std::fabs(buf[size_t(i)].cy - 500) < 16;
        Check(n == 20 && inside, "Gestures mark: 10 strokes and their underlay, within 16 px (" + std::to_string(n) + ")");
    }
    {   // the orb's rest glow: the tree's first skill's under-face kind, inside the orb's cap; none for a tree with no seat
        const auto l = ClassLayout(1);
        rw::Scene s = base(l, 1, -1);
        std::array<rw::RestGlow, rw::MaxSeats> rest{};
        for (int i = 0; i < l.count; ++i) rest[size_t(i)] = rw::RestGlowOf(l.seats[size_t(i)].id, 1, l.seats[size_t(i)].tree);
        s.rest = rest.data();
        int with = 0;
        for (int t = 0; t < rw::Trees; ++t) {
            rw::Instance g;
            if (!ob::OrbGlow(s, t, g)) continue;
            ++with;
            Check(g.recipe == rw::RecipeFx && g.param < 256 && g.w <= 2 * rw::RestCapPx * 2 * (ob::ClosedR - 6) / rw::IconPx + 1e-3f, "orb glow: a rest kind under the face, inside the orb's cap");
        }
        Check(with > 0, "orb glow: the Sorceress orbs glow");
        rw::Layout empty = l; empty.count = 0;
        s.layout = &empty; rw::Instance g;
        Check(!ob::OrbGlow(s, 0, g), "orb glow: none for a tree with no seat");
    }
    if (failures) { std::printf("ring-wheel-orbs: %d failed\n", failures); return 1; }
    std::printf("ring-wheel-orbs: all passed\n");
    return 0;
}
