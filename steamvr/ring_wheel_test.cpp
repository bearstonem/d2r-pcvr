// Ring skill menu (, build step R2; steamvr/ring_wheel.h): the seats of both Tree layouts against the kit's numbers,
// the hit test (nearest wins, hysteresis, faded trees take no hits, the center halves, the footer remap onto upstream's
// cells), the tree dwell and hold, fire on release, the page lockout, and pick / bind / clear.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "hand_controls.h"
#include "ring_wheel.h"
#include "ring_wheel_pages.h"
#include "ring_wheel_iconbox.h"
#include "ring_wheel_badges.h"
#include "ring_wheel_draw.h"
#include "ring_wheel_shot.h"
#include "ring_wheel_pointing.h"
#include "ring_wheel_close.h"
#include "ring_wheel_grab.h"
#include <cstring>
#include "ring_wheel_life.h"
#include "ring_wheel_reply.h"
#include "ring_wheel_log.h"
#include "ring_wheel_unbake.h"
#include "stick_select.h"
#include <atomic>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <mutex>

namespace rw = framevr::ringwheel;
namespace ia = framevr::interaction;
namespace sc = framevr::skillcategories;
static int g_fail = 0;
static void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; } }
static void Check(bool ok, const std::string& what) { Check(ok, what.c_str()); }

// A class's seats: the first `per[t]` skills of each of its trees (id order), every one of the class's 30 when per is 10.
static rw::Layout ClassLayout(int cls, const int per[3], rw::TreeLayout mode, int shelfGeneral = 0, int shelfItems = 0) {
    std::array<rw::SeatIn, rw::MaxSeats> in{}; int n = 0, taken[3] = {};
    const auto& c = sc::Classes[size_t(cls)];
    for (int i = 0; i < 30; ++i) { const int t = c.pages[i] - '1'; if (taken[t] < per[t]) { in[size_t(n++)] = {c.first + i, -1, t}; ++taken[t]; } }
    for (int i = 0; i < shelfGeneral; ++i) in[size_t(n++)] = {i, -1, -1};
    for (int i = 0; i < shelfItems; ++i) in[size_t(n++)] = {100 + i, 1000 + i, -1};
    return rw::Build(in.data(), n, mode);
}
static float Dist(const rw::Seat& a, const rw::Seat& b) { return std::hypot(a.x - b.x, a.y - b.y); }
// A step with the laser at wheel px (x, y).
static rw::StepIn At(double ms, float x, float y, bool trigger = false, int slot = -1) {
    rw::StepIn s; s.nowMs = ms; s.hit = true; s.x = x + rw::CanvasX; s.y = y + rw::CanvasY; s.trigger = trigger; s.slotPressed = slot; return s;
}

int main() {
    {   // Seats, both layouts, every class, every count of 1-10 a tree: the kit's self-test numbers (D1: min pitch 70 in a tree,
        // 60 across a gutter, outermost target 301 px Trees shown / 313 Tree orbs, everything inside the 320 px circle)
        float minIn[2] = {1e9f, 1e9f}, minAcross = 1e9f, minAcrossOrbs = 1e9f, outer[2] = {};
        for (int mode = 0; mode < 2; ++mode) for (int cls = 0; cls < int(sc::Classes.size()); ++cls) for (int k = 1; k <= 10; ++k) {
            const int per[3] = {k, std::max(1, 11 - k), (k * 7) % 10 + 1};
            const auto l = ClassLayout(cls, per, mode ? rw::TreeLayout::Orbs : rw::TreeLayout::Shown);
            Check(l.perTree[0] + l.perTree[1] + l.perTree[2] == l.count, "every seat in a tree");
            for (int i = 0; i < l.count; ++i) {
                const auto& a = l.seats[size_t(i)];
                outer[mode] = std::max(outer[mode], std::hypot(a.x - rw::C, a.y - rw::C) + rw::HitR);
                for (int j = i + 1; j < l.count; ++j) {
                    const auto& b = l.seats[size_t(j)];
                    if (a.tree == b.tree) minIn[mode] = std::min(minIn[mode], Dist(a, b));
                    else if (mode == 0) minAcross = std::min(minAcross, Dist(a, b));
                    else minAcrossOrbs = std::min(minAcrossOrbs, Dist(a, b));
                }
            }
        }
        std::printf("seats: min pitch in a tree %.1f (shown) %.1f (orbs), across a gutter %.1f, outermost %.1f / %.1f px\n", minIn[0], minIn[1], minAcross, outer[0], outer[1]);
        Check(minIn[0] >= rw::MinPitch - .5f && minIn[1] >= rw::MinPitch - .5f, "70 px between seats in a tree, both layouts");
        Check(minAcross >= 60, "60 px across a gutter (Trees shown)");
        // Designer 2026-11-07: Tree orbs fan 110 deg, not 140 (at 140 seats of neighbor trees overlapped: 6.7 px in the kit)
        std::printf("seats: Tree orbs across trees %.1f px\n", minAcrossOrbs);
        Check(minAcrossOrbs >= rw::MinPitch, "Tree orbs: seats of different trees at least 70 px apart (every class, 1-10 a tree)");
        // Perf R8 stage 2: the effects under the faces are drawn before every icon; safe only while no rest glow (33 px,
        // 43 px on the 1.3x pointed icon) reaches a neighbor's face (its center minus 22 px).
        const float nearest = std::min({minIn[0], minIn[1], minAcross, minAcrossOrbs}) - rw::IconPx / 2;
        Check(nearest >= 1.3f * rw::RestCapPx, "the rest cap (pointed 1.3x) never reaches a neighbor's face (" + std::to_string(nearest) + " px)");
        Check(outer[0] <= 301.5f && outer[1] <= 313.5f && outer[1] <= rw::ContentR, "outermost target 301 px (Trees shown), 313 (Tree orbs), inside 320");
        // Fixed seats: the same input gives the same seats (the hand learns them), and a tree's seats stay in its 110 deg
        const int per[3] = {10, 10, 10};
        const auto a = ClassLayout(3, per, rw::TreeLayout::Shown), b = ClassLayout(3, per, rw::TreeLayout::Shown);
        bool same = a.count == b.count;
        for (int i = 0; i < a.count && same; ++i) same = a.seats[size_t(i)].x == b.seats[size_t(i)].x && a.seats[size_t(i)].y == b.seats[size_t(i)].y;
        Check(same && a.count == 30, "Paladin, 30 seats, deterministic");
        bool inSector = true;
        for (int i = 0; i < a.count; ++i) { const auto& s = a.seats[size_t(i)]; inSector = inSector && std::fabs(rw::Wrap(s.angle, a.dir[size_t(s.tree)]) - a.dir[size_t(s.tree)]) <= 55 * rw::Pi / 180 + 1e-4f; }
        Check(inSector, "each seat inside its tree's 110 deg");
    }
    {   // The shelf: general skills, a 56 px gap, then item skills, 60 px apart on y 760, centered
        const int per[3] = {3, 3, 3};
        const auto l = ClassLayout(1, per, rw::TreeLayout::Shown, 6, 2);
        Check(l.shelf == 8, "eight on the shelf");
        const auto* s = &l.seats[size_t(l.count - 8)];
        bool row = true;
        for (int i = 0; i < 8; ++i) row = row && s[i].y == rw::ShelfY && s[i].tree == -1 && s[i].shelfItem == (i >= 6);
        Check(row && std::fabs(s[1].x - s[0].x - rw::ShelfPitch) < 1e-3f && std::fabs(s[6].x - s[5].x - rw::ShelfPitch - rw::ShelfGap) < 1e-3f, "pitch 60, the gap before the items");
        Check(std::fabs((s[0].x + s[7].x) / 2 - rw::C) < 1e-3f, "centered under the wheel");
    }
    {   // Seats from upstream's snapshot: a known class's skills by tree, the rest on the shelf; an unknown class keeps upstream
        ia::Snapshot s; s.skillClass = 1;
        const int ids[] = {39, 36, 0, 54, 38};   // Ice Bolt (cold), Fire Bolt (fire), Attack (general), Teleport, Charged Bolt
        for (int id : ids) { auto& k = s.catalog[s.skillCount++]; k.id = id; k.known = k.available = true; }
        { auto& k = s.catalog[s.skillCount++]; k.id = 61; k.owner = 7; k.known = true; }   // an item's skill: the shelf
        std::array<rw::SeatIn, rw::MaxSeats> in{}; int n = 0;
        Check(rw::SeatsFrom(s, in, n) && n == 6, "six seats");
        Check(in[0].id == 36 && in[0].tree == 0 && in[1].id == 38 && in[1].tree == 1 && in[2].id == 54 && in[2].tree == 1 && in[3].id == 39 && in[3].tree == 2,
              "Sorceress: Fire, then Lightning in id order, then Cold");
        // review G1: each class's top tree is the kit's (data.order); expected names from the kit's class files
        const char* top[8] = {"Bow & Crossbow", "Fire", "Curses", "Offensive Auras", "Masteries", "Summoning", "Traps", "Demon"};
        const char* lowerRight[8] = {"Javelin & Spear", "Lightning", "Poison & Bone", "Combat", "Combat", "Elemental", "Shadow Disciplines", "Eldritch"};
        bool tops = true;
        for (int cls = 0; cls < 8; ++cls) {
            ia::Snapshot c; c.skillClass = cls;
            const auto& k = sc::Classes[size_t(cls)];
            int want[3] = {-1, -1, -1};   // the first skill id on each page
            for (int i = 0; i < 30; ++i) { const int pg = k.pages[i] - '1'; if (want[pg] < 0) want[pg] = k.first + i; }
            for (int pg = 0; pg < 3; ++pg) { auto& e = c.catalog[c.skillCount++]; e.id = want[pg]; e.known = e.available = true; }
            std::array<rw::SeatIn, rw::MaxSeats> got{}; int m = 0; rw::SeatsFrom(c, got, m);
            int at[3] = {-1, -1, -1};
            for (int i = 0; i < m; ++i) for (int pg = 0; pg < 3; ++pg) if (got[size_t(i)].id == want[pg]) at[got[size_t(i)].tree] = pg;
            tops = tops && m == 3 && std::string(sc::Name(cls, at[0] + 1)) == top[cls] && std::string(sc::Name(cls, at[1] + 1)) == lowerRight[cls];
        }
        Check(tops, "G1: every class's top and lower-right trees are the kit's (Amazon, Barbarian, Druid, Paladin reordered)");
        Check(in[4].id == 0 && in[4].tree == -1 && in[5].id == 61 && in[5].owner == 7 && in[5].tree == -1, "Attack and the item skill on the shelf");
        s.skillClass = -1; Check(!rw::SeatsFrom(s, in, n) && n == 0, "unknown class: no ring, upstream's wheel");
        s.skillClass = 7; Check(rw::SeatsFrom(s, in, n), "the Warlock has trees (skillcategories), so it gets the ring");
    }
    const int full[3] = {10, 10, 10};
    rw::Settings set;
    {   // Hit test: each seat at its center, the gaps, the center halves, the footer on upstream's cells
        const auto l = ClassLayout(3, full, rw::TreeLayout::Shown, 6, 0);
        rw::Engine e; e.Reset(l);
        bool every = true;
        for (int i = 0; i < l.count; ++i) { e.Reset(l); every = every && e.HitTest(l.seats[size_t(i)].x + rw::CanvasX, l.seats[size_t(i)].y + rw::CanvasY, set) == rw::Target{rw::Kind::Seat, i}; }
        Check(every, "every seat is hit at its rest position (nothing pointed: every tree shown)");
        e.Reset(l);
        Check(e.HitTest(rw::CanvasCx, rw::CanvasCy - 40, set).kind == rw::Kind::Glyphs && e.HitTest(rw::CanvasCx, rw::CanvasCy + 40, set).kind == rw::Kind::Gestures,
              "the center: Glyphs on top, Gestures below");
        Check(e.HitTest(rw::CanvasCx + 50, rw::CanvasCy, set).kind == rw::Kind::Gap, "the hairline's dead band with nothing kept: no half");
        rw::Settings war = set; war.hasMotions = false;
        Check(e.HitTest(rw::CanvasCx, rw::CanvasCy + 40, war).kind == rw::Kind::Glyphs, "no motions (the Warlock): the whole center is Glyphs");
        Check(e.HitTest(rw::CanvasCx + rw::C + 60, rw::CanvasCy, set).kind == rw::Kind::None, "past the wheel: nothing");
        // The footer: each of upstream's tabs at its center, at every drawn width, is upstream's own cell
        for (int dev = 0; dev < 2; ++dev) for (int fullWidth = 0; fullWidth < 2; ++fullWidth) {
            rw::Settings f = set; f.developer = dev; f.footerFull = fullWidth;
            const auto page = ia::MenuPage::Spells; const int tabs = ia::FooterCount(page, dev);
            const float w = rw::FooterWidth(fullWidth, tabs), x0 = (rw::CanvasPx - w) / 2;
            bool all = true;
            for (int i = 0; i < tabs; ++i) {
                const auto b = ia::FooterBounds(i, page, dev);
                const auto t = e.HitTest(x0 + (b.left + b.right) / 2 * w, (b.top + b.bottom) / 2 * rw::CanvasPx, f);
                all = all && t.kind == rw::Kind::Footer && t.index == ia::FooterCell(i, page);
            }
            Check(all, dev ? "developer mode's six tabs, each upstream's cell (LAB included)" : "the five tabs, each upstream's cell (LAB the fifth)");
            if (!fullWidth) Check(e.HitTest(x0 - 10, .93f * rw::CanvasPx, f).kind != rw::Kind::Footer, "left of the narrowed footer: no tab");
        }
        Check(rw::FooterWidth(false, 5) == 768 && rw::FooterWidth(false, 6) == 864 && rw::FooterWidth(true, 5) == 1024, "768, 864 with six tabs, 1024 full");
        const float shelfBottom = rw::CanvasY + rw::ShelfY + rw::HitR, footerTop = ia::FooterBounds(0, ia::MenuPage::Spells, false).top * rw::CanvasPx;
        Check(footerTop > shelfBottom + 10, "the footer clear of the shelf's hit areas");
    }
    {   // Hysteresis: a seat pointed stays until the laser is 6 px past its edge; jitter on a seat never flickers to a neighbor
        const auto l = ClassLayout(3, full, rw::TreeLayout::Shown);
        rw::Engine e; e.Reset(l);
        int flicker = 0;
        for (int i = 0; i < l.count; ++i) {
            const auto& a = l.seats[size_t(i)];
            int j = -1; float dj = 1e9f;
            for (int k = 0; k < l.count; ++k) if (k != i && Dist(a, l.seats[size_t(k)]) < dj) { dj = Dist(a, l.seats[size_t(k)]); j = k; }
            const float ux = (l.seats[size_t(j)].x - a.x) / dj, uy = (l.seats[size_t(j)].y - a.y) / dj;
            e.Reset(l); double ms = 0;
            e.Step(At(ms, a.x, a.y), set);
            for (int s = 0; s < 40; ++s) {   // tremor: +-6 px across, and a lean 28 px toward the nearest neighbor (past 27, inside 27 + 6)
                const float jitter = (s % 2 ? 6.f : -6.f), lean = s % 5 == 0 ? 28.f : 0.f;
                const auto o = e.Step(At(ms += 11, a.x + ux * lean - uy * jitter, a.y + uy * lean + ux * jitter), set);
                if (!(o.under == rw::Target{rw::Kind::Seat, i})) ++flicker;
            }
        }
        Check(flicker == 0, "no flicker to a neighbor under 6 px tremor and a 28 px lean, every seat");
        rw::Settings none = set; none.hysteresisPx = 0;
        e.Reset(l); const auto& a = l.seats[0];
        e.Step(At(0, a.x, a.y), none);
        Check(e.Step(At(11, a.x + 31, a.y), none).under.kind != rw::Kind::Seat || e.Step(At(22, a.x + 31, a.y), none).under.index != 0, "hysteresis 0: no margin");
    }
    {   // Trees: a third points its tree after the dwell; the others fade to the row's level and take no hits; the hold
        const auto l = ClassLayout(3, full, rw::TreeLayout::Shown);
        rw::Engine e; e.Reset(l);
        auto treePoint = [&](int t) { const float a = l.dir[size_t(t)]; return std::pair<float, float>{rw::C + 150 * std::cos(a), rw::C + 150 * std::sin(a)}; };
        double ms = 0; auto [x0, y0] = treePoint(0);
        auto o = e.Step(At(ms, x0, y0), set);
        Check(o.under == rw::Target{rw::Kind::Tree, 0} && o.tree == -1, "a tree's third, before the dwell: nothing pointed yet");
        for (; ms < 110; ms += 11) o = e.Step(At(ms, x0, y0), set);
        Check(o.tree == -1, "not before 120 ms");
        for (; ms < 140; ms += 11) o = e.Step(At(ms, x0, y0), set);
        Check(o.tree == 0, "pointed after the dwell");
        for (; ms < 400; ms += 11) o = e.Step(At(ms, x0, y0), set);
        Check(o.alpha[0] == 1 && o.alpha[1] == 0 && o.alpha[2] == 0, "the others fade to 0 (row 13 at 0%) within 160 ms");
        int tree1 = -1; for (int i = 0; i < l.count; ++i) if (l.seats[size_t(i)].tree == 1) { tree1 = i; break; }
        o = e.Step(At(ms += 11, l.seats[size_t(tree1)].x, l.seats[size_t(tree1)].y), set);
        Check(o.under.kind == rw::Kind::Tree && o.under.index == 1, "a faded tree's seat takes no hit: its third answers");
        // Sweeping back and forth every 50 ms for 3 s: never more than one switch per 300 ms
        int switches = 0, was = o.tree; double lastSwitch = -1e9, closest = 1e9;
        for (int s = 0; s < 60; ++s) {
            auto [x, y] = treePoint(s / 3 % 2 ? 0 : 1);
            for (int k = 0; k < 5; ++k) {
                o = e.Step(At(ms += 10, x, y), set);
                if (o.tree != was) { ++switches; closest = std::min(closest, ms - lastSwitch); lastSwitch = ms; was = o.tree; }
            }
        }
        std::printf("sweep: %d switches in 3 s, closest %.0f ms apart\n", switches, closest);
        Check(switches >= 1 && closest >= 300, "switches at least 300 ms apart (no faster than 3.3 a second)");
        // Row 13 at 40%: the others stay at 0.4, under half, still no hits
        rw::Settings forty = set; forty.othersPct = 40;
        e.Reset(l); ms = 0; for (; ms < 500; ms += 11) o = e.Step(At(ms, x0, y0), forty);
        Check(std::fabs(o.alpha[1] - .4f) < 1e-4f && e.HitTest(l.seats[size_t(tree1)].x + rw::CanvasX, l.seats[size_t(tree1)].y + rw::CanvasY, forty).kind == rw::Kind::Tree, "others at 40%: shown, not hit");
        // The laser off the wheel: no tree, all back to full
        for (int k = 0; k < 60; ++k) o = e.Step(At(ms += 11, rw::C + 700, rw::C), forty);
        Check(o.tree == -1 && o.alpha[1] == 1, "off the wheel: no tree, every tree back");
        // Tree orbs: closed trees take no hits until pointed
        const auto orbs = ClassLayout(3, full, rw::TreeLayout::Orbs);
        rw::Settings os = set; os.layout = rw::TreeLayout::Orbs;
        // (its fan is 140 deg, wider than its 120 deg third: take the seat nearest the tree's own direction)
        int mid = 0; float best = 1e9f;
        for (int i = 0; i < orbs.perTree[0]; ++i) { const float d = std::fabs(rw::Wrap(orbs.seats[size_t(i)].angle, orbs.dir[0]) - orbs.dir[0]); if (d < best) { best = d; mid = i; } }
        const auto& m = orbs.seats[size_t(mid)];
        rw::Engine eo; eo.Reset(orbs); ms = 0; o = eo.Step(At(ms, m.x, m.y), os);
        Check(o.under.kind == rw::Kind::Tree && o.under.index == 0, "Tree orbs: a closed tree's seat is its third");
        for (; ms < 400; ms += 11) o = eo.Step(At(ms, m.x, m.y), os);
        Check(o.tree == 0 && o.under == (rw::Target{rw::Kind::Seat, mid}), "opened by pointing: its seats take hits");
        bool fanHolds = true;   // an open fan's seats past its third still hit (they keep the tree open)
        for (int i = 0; i < orbs.perTree[0]; ++i) { const auto& q = orbs.seats[size_t(i)]; for (int k = 0; k < 3; ++k) o = eo.Step(At(ms += 11, q.x, q.y), os); fanHolds = fanHolds && o.tree == 0 && o.under.kind == rw::Kind::Seat; }
        Check(fanHolds, "the open fan's every seat keeps its tree open");
    }
    {   // Fire on release, the target pressed; pick, cancel, bind, clear; the lockout
        const auto l = ClassLayout(3, full, rw::TreeLayout::Shown, 2, 0);
        rw::Engine e; e.Reset(l);
        const auto& a = l.seats[0]; const auto& b = l.seats[1];
        double ms = 0;
        auto o = e.Step(At(ms, a.x, a.y, true), set);
        Check(o.event == rw::Event::None && o.pressed == (rw::Target{rw::Kind::Seat, 0}), "a press fires nothing; it holds its target");
        o = e.Step(At(ms += 11, b.x, b.y, true), set); o = e.Step(At(ms += 11, b.x, b.y, false), set);
        Check(o.event == rw::Event::None && o.picked == -1, "pressed on A, let go on B: nothing");
        o = e.Step(At(ms += 11, a.x, a.y, true), set); o = e.Step(At(ms += 11, a.x, a.y, false), set);
        Check(o.event == rw::Event::Pick && o.picked == 0, "press and release on A: picked");
        o = e.Step(At(ms += 11, a.x, a.y, false, 2), set);
        Check(o.event == rw::Event::Bind && o.slot == 2 && o.skill == a.id && o.owner == -1 && o.picked == -1, "a slot while picked: bound there, the pick ends");
        rw::StepIn in = At(ms += 11, a.x, a.y, true); e.Step(in, set); in = At(ms += 11, a.x, a.y, false); o = e.Step(in, set);
        in = At(ms += 11, a.x, a.y, false, 2); in.bound[2] = a.id; o = e.Step(in, set);
        Check(o.event == rw::Event::Clear && o.slot == 2 && o.skill == -1, "its own slot again: cleared");
        e.Step(At(ms += 11, a.x, a.y, true), set); o = e.Step(At(ms += 11, a.x, a.y, false), set);
        e.Step(At(ms += 11, a.x, a.y, true), set); o = e.Step(At(ms += 11, a.x, a.y, false), set);
        Check(o.event == rw::Event::Cancel && o.picked == -1, "release on the picked seat again: cancelled");
        e.Step(At(ms += 11, a.x, a.y, true), set); e.Step(At(ms += 11, a.x, a.y, false), set);
        e.Step(At(ms += 11, rw::C + 100, rw::C + 100, true), set); o = e.Step(At(ms += 11, rw::C + 100, rw::C + 100, false), set);
        Check(o.event == rw::Event::Cancel, "release on a gap: cancelled");
        Check(e.Step(At(ms += 11, a.x, a.y, false, 1), set).event == rw::Event::None, "a slot with nothing picked: nothing");
        // Center halves and the footer go to upstream on release
        e.Step(At(ms += 11, rw::C, rw::C - 40, true), set); o = e.Step(At(ms += 11, rw::C, rw::C - 40, false), set);
        Check(o.event == rw::Event::Upstream && o.cell == ia::GlyphsCell, "Glyphs: upstream's GLYPHS cell, once");
        e.Step(At(ms += 11, rw::C, rw::C + 40, true), set); o = e.Step(At(ms += 11, rw::C, rw::C + 40, false), set);
        Check(o.event == rw::Event::Upstream && o.cell == ia::MotionsCell, "Gestures: upstream's MOTIONS cell");
        // The lockout: a page change locks presses for 250 ms; a trigger held through it must be let go first
        e.PageChanged(ms, true, set);
        o = e.Step(At(ms += 11, a.x, a.y, false), set);
        Check(o.event == rw::Event::None && o.locked, "the held trigger's release after a page change fires nothing");
        e.Step(At(ms += 11, a.x, a.y, true), set); o = e.Step(At(ms += 11, a.x, a.y, false), set);
        Check(o.event == rw::Event::None, "a click inside the 250 ms: ignored");
        ms += 250;
        e.Step(At(ms, a.x, a.y, true), set); o = e.Step(At(ms += 11, a.x, a.y, false), set);
        Check(o.event == rw::Event::Pick && !o.locked, "after the lockout: clicks again");
        rw::Engine h; h.Reset(l); ms = 0;
        h.Step(At(ms, a.x, a.y, true), set); h.PageChanged(ms, true, set); ms += 400;
        o = h.Step(At(ms, a.x, a.y, false), set);
        Check(o.event == rw::Event::None, "a trigger held through a page change: its release is not a click, even after the lockout");
    }
    // ---- the design notes "Unit-test list, R2-R6", R2 items (numbered as there); the blocks above stay ----
    {   // #1 every learned class skill has a seat, all 8 classes, both layouts; count == input; angles inside the sector minus
        // 0.08 rad; radii 184-274 (Trees shown) / 198-286 (Tree orbs)
        const int full[3] = {10, 10, 10};
        for (int mode = 0; mode < 2; ++mode) for (int cls = 0; cls < int(sc::Classes.size()); ++cls) {
            const auto m = mode ? rw::TreeLayout::Orbs : rw::TreeLayout::Shown;
            const auto l = ClassLayout(cls, full, m);
            int per[3] = {};
            for (int i = 0; i < 30; ++i) ++per[sc::Classes[size_t(cls)].pages[i] - '1'];
            bool all = l.count == 30;
            for (int i = 0; i < 30 && all; ++i) { bool found = false; for (int k = 0; k < l.count; ++k) found = found || l.seats[size_t(k)].id == sc::Classes[size_t(cls)].first + i; all = found; }
            Check(all && l.perTree[0] == per[0] && l.perTree[1] == per[1] && l.perTree[2] == per[2], "#1 all 30 class skills seated, each in its own tree's count");
            const float half = (mode ? 70.f : 55.f) * rw::Pi / 180 - .08f, r0 = mode ? 198.f : 184.f, r1 = mode ? 286.f : 274.f;
            bool inside = true;
            for (int i = 0; i < l.count; ++i) {
                const auto& s = l.seats[size_t(i)]; const float r = std::hypot(s.x - rw::C, s.y - rw::C);
                inside = inside && std::fabs(rw::Wrap(std::atan2(s.y - rw::C, s.x - rw::C), l.dir[size_t(s.tree)]) - l.dir[size_t(s.tree)]) <= half + 1e-3f && r >= r0 - .01f && r <= r1 + .01f;
            }
            Check(inside, mode ? "#1 Tree orbs: inside 140 deg minus 0.08 rad, r 198-286" : "#1 Trees shown: inside 110 deg minus 0.08 rad, r 184-274");
        }
    }
    {   // #2 a skill with no seat in its class's trees (another class's id, an item's) goes to the shelf, never dropped
        ia::Snapshot s; s.skillClass = 3;   // Paladin
        const int ids[] = {98, 36, 6};      // Might (its own), Fire Bolt and Magic Arrow (not the Paladin's trees)
        for (int id : ids) { auto& k = s.catalog[s.skillCount++]; k.id = id; k.known = k.available = true; }
        std::array<rw::SeatIn, rw::MaxSeats> in{}; int n = 0;
        rw::SeatsFrom(s, in, n);
        const auto l = rw::Build(in.data(), n, rw::TreeLayout::Shown);
        int shelf = 0, tree = 0; for (int i = 0; i < l.count; ++i) (l.seats[size_t(i)].tree < 0 ? shelf : tree)++;
        Check(l.count == 3 && tree == 1 && shelf == 2, "#2 two foreign skills on the shelf, none dropped");
        s.skillClass = 8; Check(!rw::SeatsFrom(s, in, n), "#2 a class past the eight: upstream");
    }
    {   // #3 float: at most 5 px, 1000 seeds over 60 s; peak speed at most 0.5 deg/s (7.5 px/s at 15 px/deg); neighbors out of step
        float maxOff = 0, maxSpeed = 0; int inStep = 0;
        for (int id = 0; id < 1000; ++id) {
            rw::Float was = rw::FloatAt(id, 0);
            for (double t = 10; t <= 60000; t += 10) {
                const auto f = rw::FloatAt(id, t);
                maxOff = std::max(maxOff, std::hypot(f.dx, f.dy));
                maxSpeed = std::max(maxSpeed, std::hypot(f.dx - was.dx, f.dy - was.dy) / .01f);
                was = f;
            }
            const auto a = rw::FloatAt(id, 12345), b = rw::FloatAt(id + 1, 12345);
            if (std::fabs(a.dx - b.dx) < .01f && std::fabs(a.dy - b.dy) < .01f) ++inStep;
        }
        std::printf("#3 float: max %.2f px, peak %.2f px/s (%.2f deg/s)\n", maxOff, maxSpeed, maxSpeed / 15);
        Check(maxOff <= 5.0001f && maxOff >= 4, "#3 float reaches near its 5 px cap and never past it");
        Check(maxSpeed / 15 <= .5f, "#3 peak speed at most 0.5 deg/s at 15 px/deg");
        Check(inStep <= 2, "#3 neighbors (ids n and n + 1) never in step");
        // the Animation row (L4 table, the player 2026-11-07: Off / Floating icons / Effect on hover / All effects, default Effect on hover)
        const auto off = rw::PresetAnim(rw::Preset::Off), flo = rw::PresetAnim(rw::Preset::Floating), hov = rw::PresetAnim(rw::Preset::Hover), all = rw::PresetAnim(rw::Preset::All);
        Check(!off.floats && off.glow == rw::GlowOff && !off.hoverFx && !off.faceFx && !off.burst && off.crawlS == 0 && off.still, "#3 Off: rows 2-7 off, redraw only on a change");
        Check(flo.floats && flo.glow == rw::GlowOff && !flo.hoverFx && !flo.faceFx && flo.burst && flo.crawlS == 0 && !flo.still, "#3 Floating icons: float and burst only");
        Check(hov.floats && hov.glow == rw::GlowPointed && hov.hoverFx && hov.faceFx && hov.burst && hov.crawlS == 0 && !hov.still, "#3 Effect on hover: float, glow pointed only, effects on, crawl off");
        Check(all.floats && all.glow == rw::GlowEverywhere && all.hoverFx && all.faceFx && all.burst && all.crawlS == 8 && !all.still, "#3 All effects: all on, glow everywhere, crawl 8 s");
        Check(rw::PresetOf(off) == rw::Preset::Off && rw::PresetOf(flo) == rw::Preset::Floating && rw::PresetOf(hov) == rw::Preset::Hover && rw::PresetOf(all) == rw::Preset::All, "#3 each value reads back");
        Check(int(rw::Preset::Hover) == 2 && rw::PresetOf(rw::Anim{}) == rw::Preset::Hover, "#3 the rows' defaults are Effect on hover, the row's default (index 2)");
        auto custom = hov; custom.burst = false; Check(rw::PresetOf(custom) == rw::Preset::Custom, "#3 one row changed by hand: Custom");
        auto crawl4 = all; crawl4.crawlS = 4; Check(rw::PresetOf(crawl4) == rw::Preset::Custom, "#3 another crawl row: Custom");
    }
    const int ten[3] = {10, 10, 10};
    rw::Settings st;
    {   // #4 hit order: footer, center, skill, shelf, tree third, nothing; a 6 px jitter near a seat's edge never flips; a 12 px move does
        const auto l = ClassLayout(3, ten, rw::TreeLayout::Shown, 4, 0);
        rw::Engine e; e.Reset(l);
        const auto b0 = ia::FooterBounds(0, ia::MenuPage::Spells, false);
        Check(e.HitTest(rw::CanvasCx - 768.f / 2 + (b0.left + b0.right) / 2 * 768, (b0.top + b0.bottom) / 2 * rw::CanvasPx, st).kind == rw::Kind::Footer, "#4 footer first");
        Check(e.HitTest(rw::CanvasCx, rw::CanvasCy - 30, st).kind == rw::Kind::Glyphs, "#4 then the center");
        const auto& seat = l.seats[0];
        Check(e.HitTest(seat.x + rw::CanvasX, seat.y + rw::CanvasY, st) == (rw::Target{rw::Kind::Seat, 0}), "#4 then a skill");
        const auto& sh = l.seats[size_t(l.count - 1)];
        Check(e.HitTest(sh.x + rw::CanvasX, sh.y + rw::CanvasY, st) == (rw::Target{rw::Kind::Seat, l.count - 1}) && sh.tree == -1, "#4 then the shelf");
        Check(e.HitTest(rw::CanvasCx, rw::CanvasCy - 150, st).kind == rw::Kind::Tree, "#4 then a tree's third (between the center and the bands)");
        Check(e.HitTest(rw::CanvasCx + 360, rw::CanvasCy - 200, st).kind == rw::Kind::None, "#4 then nothing");
        // two closest seats of one tree
        int ia_ = 0, ib = 1; float best = 1e9f;
        for (int i = 0; i < l.count; ++i) for (int j = i + 1; j < l.count; ++j) if (l.seats[size_t(i)].tree >= 0 && l.seats[size_t(i)].tree == l.seats[size_t(j)].tree && Dist(l.seats[size_t(i)], l.seats[size_t(j)]) < best) { best = Dist(l.seats[size_t(i)], l.seats[size_t(j)]); ia_ = i; ib = j; }
        const auto& A = l.seats[size_t(ia_)]; const auto& B = l.seats[size_t(ib)];
        const float ux = (B.x - A.x) / best, uy = (B.y - A.y) / best;
        e.Reset(l); double ms = 0; e.Step(At(ms, A.x, A.y), st);
        int flips = 0; uint32_t r = 12345;
        for (int k = 0; k < 10000; ++k) {
            r = r * 1664525u + 1013904223u; const float j1 = float(int(r >> 8) % 601 - 300) / 100;   // -3..3 px
            r = r * 1664525u + 1013904223u; const float j2 = float(int(r >> 8) % 601 - 300) / 100;
            const auto o = e.Step(At(ms += 11, A.x + ux * (29 + j1) - uy * j2, A.y + uy * (29 + j1) + ux * j2), st);
            if (!(o.under == rw::Target{rw::Kind::Seat, ia_})) ++flips;
        }
        Check(flips == 0, "#4 10000 samples of 6 px jitter 29 px out toward the neighbor (past 27): never flips");
        const auto moved = e.Step(At(ms += 11, A.x + ux * (best - 26), A.y + uy * (best - 26)), st);
        Check(moved.under == (rw::Target{rw::Kind::Seat, ib}), "#4 a real move on, 26 px from the neighbor: the neighbor");
    }
    {   // #5 tree switching: dwell 120, hold 300, 3 Hz sweep at most 3 a second, 10 deg hysteresis, dwell 0 instant, hold 0 none
        const auto l = ClassLayout(3, ten, rw::TreeLayout::Shown);
        auto third = [&](int t, float offDeg = 0) { const float a = l.dir[size_t(t)] + offDeg * rw::Pi / 180; return std::pair<float, float>{rw::C + 150 * std::cos(a), rw::C + 150 * std::sin(a)}; };
        rw::Engine e; e.Reset(l); double ms = 0; rw::StepOut o;
        auto [x0, y0] = third(0);
        for (; ms <= 200; ms += 10) o = e.Step(At(ms, x0, y0), st);
        Check(o.tree == 0, "#5 tree 0 pointed");
        auto [xh, yh] = third(0, 65);   // past its 60 deg half, inside the 10 deg hysteresis
        for (int k = 0; k < 40; ++k) o = e.Step(At(ms += 10, xh, yh), st);
        Check(o.tree == 0, "#5 65 deg off its direction: kept (10 deg hysteresis)");
        auto [xo, yo] = third(0, 75);
        for (int k = 0; k < 40; ++k) o = e.Step(At(ms += 10, xo, yo), st);
        Check(o.tree == 1, "#5 75 deg off: the next tree");
        // a 3 Hz sweep (one side every 167 ms) for 4 s
        int switches = 0, was = o.tree;
        for (int k = 0; k < 400; ++k) { auto [x, y] = third((k / 17) % 2 ? 2 : 1); o = e.Step(At(ms += 10, x, y), st); if (o.tree != was) { ++switches; was = o.tree; } }
        Check(switches <= 12, "#5 a 3 Hz sweep: at most 3 switches a second");
        rw::Settings quick = st; quick.dwellMs = 0; quick.holdMs = 0;
        rw::Engine q; q.Reset(l); ms = 0;
        auto [a1, b1] = third(1); auto [a2, b2] = third(2);
        q.Step(At(ms, x0, y0), quick); o = q.Step(At(ms += 10, a1, b1), quick);
        Check(o.tree == 1, "#5 dwell 0: at once");
        o = q.Step(At(ms += 10, a2, b2), quick);
        Check(o.tree == 2, "#5 hold 0: no wait after a switch");
    }
    {   // #6 (round 2026-11-08, Boss's decision; before: 60% took hits) the row is a look only: at 0, 30, 49, 50, 60 and 100 % the other
        // trees show at the row's level and take no hit; the laser on one of their seats answers as its third, and after the
        // dwell that tree takes over and the seat is hit. The pointed tree stays full.
        const auto l = ClassLayout(3, ten, rw::TreeLayout::Shown);
        int other = -1; for (int i = 0; i < l.count; ++i) if (l.seats[size_t(i)].tree == 1) { other = i; break; }
        for (float pct : {0.f, 30.f, 49.f, 50.f, 60.f, 100.f}) {
            rw::Settings s = st; s.othersPct = pct;
            rw::Engine e; e.Reset(l); double ms = 0; rw::StepOut o;
            const float a = l.dir[0];
            for (; ms <= 600; ms += 10) o = e.Step(At(ms, rw::C + 150 * std::cos(a), rw::C + 150 * std::sin(a)), s);
            const auto t = e.HitTest(l.seats[size_t(other)].x + rw::CanvasX, l.seats[size_t(other)].y + rw::CanvasY, s);
            Check(o.alpha[0] == 1 && std::fabs(o.alpha[1] - pct / 100) < 1e-4f, "#6 the pointed tree full, the others at the row's level");
            Check(t.kind == rw::Kind::Tree && t.index == 1, "#6 at " + std::to_string(int(pct)) + "%: an other tree's seat takes no hit, its third answers");
            const float sx = l.seats[size_t(other)].x, sy = l.seats[size_t(other)].y;
            const double start = ms;
            for (; ms <= start + 100; ms += 10) o = e.Step(At(ms, sx, sy), s);
            Check(o.tree == 0 && o.under.kind == rw::Kind::Tree, "#6 at " + std::to_string(int(pct)) + "%: before the dwell the first tree stays");
            for (; ms <= start + 500; ms += 10) o = e.Step(At(ms, sx, sy), s);
            Check(o.tree == 1 && o.under == rw::Target{rw::Kind::Seat, other} && o.alpha[1] == 1, "#6 at " + std::to_string(int(pct)) + "%: after the dwell that tree takes over and the seat is hit");
        }
        // Review N2: what can be hit fades over the same 160 ms as the look at row 0 and takes the same frames at 100, in both directions:
        // 11 ms-free steps of 10 ms move the gate 1/16 a frame, and it opens or closes at half.
        for (float pct : {0.f, 100.f}) {
            rw::Settings s = st; s.othersPct = pct;
            rw::Engine e; e.Reset(l); double ms = 0; rw::StepOut o;
            const float a = l.dir[0], tx = rw::C + 150 * std::cos(a), ty = rw::C + 150 * std::sin(a);
            const float hx = l.seats[size_t(other)].x + rw::CanvasX, hy = l.seats[size_t(other)].y + rw::CanvasY;
            const std::string tag = "N2 at " + std::to_string(int(pct)) + "%: ";
            while (o.tree != 0 && ms < 1000) { o = e.Step(At(ms, tx, ty), s); ms += 10; }
            Check(o.tree == 0, tag + "the tree is pointed");
            for (int k = 1; k <= 20; ++k) {
                Check((e.HitTest(hx, hy, s).kind == rw::Kind::Seat) == (k <= 8), tag + "pointing, frame " + std::to_string(k) + " of the 160 ms fade: the seat answers through frame 8 only");
                o = e.Step(At(ms, tx, ty), s); ms += 10;
            }
            while (o.tree != -1 && ms < 3000) { o = e.Step(At(ms, rw::C + 700, rw::C), s); ms += 10; }
            Check(o.tree == -1, tag + "the laser left the wheel: no tree");
            for (int k = 1; k <= 12; ++k) {
                Check((e.HitTest(hx, hy, s).kind == rw::Kind::Seat) == (k >= 8), tag + "back, frame " + std::to_string(k) + ": the seat answers from frame 8");
                o = e.Step(At(ms, rw::C + 700, rw::C), s); ms += 10;
            }
        }
        // The same laser script at every value: frame by frame the same targets, trees and blurbs as at 0 (the default), so the
        // dwell, the hold, both hysteresis rules and the 160 ms fade of what can be hit do not depend on the row.
        std::vector<std::pair<float, float>> script;
        const auto seatOf = [&](int t, int k) { int n = 0; for (int i = 0; i < l.count; ++i) if (l.seats[size_t(i)].tree == t && n++ == k) return i; return 0; };
        for (int round = 0; round < 12; ++round) {
            const int t = round % 3, s0 = seatOf(t, round % 4), s1 = seatOf((t + 1) % 3, (round + 1) % 4);
            const float a = l.dir[size_t(t)];
            for (int k = 0; k < 8 + round; ++k) script.push_back({rw::C + 150 * std::cos(a), rw::C + 150 * std::sin(a)});
            for (int k = 0; k < 14; ++k) script.push_back({l.seats[size_t(s0)].x + 27.f + 5.f * float(k % 3), l.seats[size_t(s0)].y});   // the seat's hysteresis edge
            for (int k = 0; k < 10 + 3 * round; ++k) script.push_back({l.seats[size_t(s1)].x, l.seats[size_t(s1)].y});
            for (int k = 0; k < 6; ++k) script.push_back({rw::C, rw::C - 40.f});   // the center
            if (round % 4 == 3) for (int k = 0; k < 12; ++k) script.push_back({rw::C + 700, rw::C});   // off the wheel
        }
        auto run = [&](float pct) {
            rw::Settings s = st; s.othersPct = pct;
            rw::Engine e; e.Reset(l); double ms = 0; std::vector<rw::StepOut> out;
            for (const auto& p : script) { out.push_back(e.Step(At(ms, p.first, p.second), s)); ms += 11; }
            return out;
        };
        const auto base = run(0);
        int seatHits = 0, otherTreesSeen = 0;
        for (const auto& o : base) { seatHits += o.under.kind == rw::Kind::Seat; otherTreesSeen += o.tree >= 0; }
        Check(seatHits > 20 && otherTreesSeen > 100, "#6 the script hits seats and points trees");
        for (float pct : {49.f, 50.f, 100.f}) {
            const auto other2 = run(pct);
            int diffs = 0;
            for (size_t i = 0; i < base.size(); ++i) diffs += !(base[i].under == other2[i].under) || base[i].tree != other2[i].tree || base[i].blurb != other2[i].blurb || base[i].centerGrown != other2[i].centerGrown;
            Check(diffs == 0, "#6 the same laser at " + std::to_string(int(pct)) + "% as at 0%, frame by frame (" + std::to_string(diffs) + " frames differ)");
        }
        {   // Tree orbs ignore the row: the closed orbs stay closed at 100%
            const auto orbs = ClassLayout(3, ten, rw::TreeLayout::Orbs);
            rw::Settings s = st; s.layout = rw::TreeLayout::Orbs; s.othersPct = 100;
            rw::Engine e; e.Reset(orbs); double ms = 0; rw::StepOut o;
            const float a = orbs.dir[0];
            for (; ms <= 600; ms += 10) o = e.Step(At(ms, rw::C + 150 * std::cos(a), rw::C + 150 * std::sin(a)), s);
            Check(o.tree == 0 && o.alpha[0] == 1 && o.alpha[1] == 0 && o.alpha[2] == 0, "#6 Tree orbs: the other orbs stay closed at 100%");
        }
    }
    {   // Round 2026-11-08 item 4: a layer shot's pixels, from an 8-bit premultiplied target to sRGB straight alpha
        namespace sh = framevr::ringwheel::shot;
        const uint8_t px[] = {10, 20, 30, 255,   0, 0, 0, 0,   60, 30, 15, 128,   200, 0, 0, 100,   9, 9, 9, 9};   // 5 x 1; pitch 24
        uint8_t rows[24 * 2] = {};
        std::memcpy(rows, px, 20); std::memcpy(rows + 24, px, 20);
        const auto un = sh::ToStraightRgba8(rows, 24, 5, 2, false, false);
        Check(un.size() == 40 && un[0] == 10 && un[1] == 20 && un[2] == 30 && un[3] == 255, "shot: an opaque texel as it is");
        Check(un[4] == 0 && un[5] == 0 && un[6] == 0 && un[7] == 0, "shot: a clear texel stays clear");
        Check(un[8] == 120 && un[9] == 60 && un[10] == 30 && un[11] == 128, "shot UNORM: color divided by its alpha");
        Check(un[12] == 255 && un[15] == 100, "shot: light past its alpha is capped at white");
        Check(un[20] == 10 && un[28] == un[8], "shot: the second row reads at the row pitch");
        const auto bgra = sh::ToStraightRgba8(rows, 24, 5, 2, true, false);
        Check(bgra[0] == 30 && bgra[1] == 20 && bgra[2] == 10 && bgra[3] == 255, "shot: B8G8R8A8 comes out RGBA");
        const auto srgb = sh::ToStraightRgba8(rows, 24, 5, 2, false, true);
        Check(srgb[0] == 10 && srgb[3] == 255 && srgb[7] == 0, "shot sRGB: opaque as it is, clear stays clear");
        // sRGB byte v of premultiplied linear L x a: decode, divide by a, encode. 60 at a = 128: linear .0452 / .502 = .0900 -> sRGB 84.
        Check(std::abs(int(srgb[8]) - 84) <= 1 && srgb[11] == 128, "shot sRGB: decoded, divided in linear light, encoded (" + std::to_string(int(srgb[8])) + ")");
    }
    {   // #7 the center: grows after 150 ms inside, never on a 100 ms pass-through; a blurb after 100 ms on a skill, highlight at
        // once; the halves' dead band keeps the last half; the Warlock has one Glyphs half
        const auto l = ClassLayout(3, ten, rw::TreeLayout::Shown);
        rw::Engine e; e.Reset(l); double ms = 0; rw::StepOut o;
        for (int k = 0; k < 10; ++k) o = e.Step(At(ms += 10, rw::C, rw::C - 40), st);
        Check(!o.centerGrown, "#7 a 100 ms pass through the center: not grown");
        for (int k = 0; k < 6; ++k) o = e.Step(At(ms += 10, rw::C, rw::C - 40), st);
        Check(o.centerGrown, "#7 150 ms inside: grown");
        o = e.Step(At(ms += 10, rw::C + 60, rw::C + 1), st);
        Check(o.under.kind == rw::Kind::Glyphs, "#7 into the hairline's 6 px band: the last half (Glyphs) kept");
        e.Reset(l); ms = 0;
        const auto& s0 = l.seats[0];
        o = e.Step(At(ms, s0.x, s0.y), st);
        Check(o.under == (rw::Target{rw::Kind::Seat, 0}) && o.blurb == -1, "#7 a skill's highlight at once, its blurb not yet");
        for (int k = 0; k < 9; ++k) o = e.Step(At(ms += 10, s0.x, s0.y), st);
        Check(o.blurb == -1, "#7 no blurb before 100 ms");
        o = e.Step(At(ms += 10, s0.x, s0.y), st);
        Check(o.blurb == 0 && o.centerGrown, "#7 after 100 ms: its blurb, the center grown");
        rw::Settings war = st; war.hasMotions = false;
        Check(e.HitTest(rw::CanvasCx, rw::CanvasCy + 40, war).kind == rw::Kind::Glyphs && e.HitTest(rw::CanvasCx, rw::CanvasCy - 40, war).kind == rw::Kind::Glyphs, "#7 the Warlock: Glyphs both halves");
    }
    {   // #8 press/release: moving off before release cancels; the lockout at 249 and 251 ms; timers reset on a page change
        const auto l = ClassLayout(3, ten, rw::TreeLayout::Shown);
        rw::Engine e; e.Reset(l); double ms = 0; rw::StepOut o;
        const auto& a = l.seats[0];
        e.Step(At(ms, a.x, a.y, true), st); e.Step(At(ms += 10, rw::C + 360, rw::C - 200, true), st); o = e.Step(At(ms += 10, rw::C + 360, rw::C - 200, false), st);
        Check(o.event == rw::Event::None && o.picked == -1, "#8 moved off before release: nothing");
        e.PageChanged(ms, false, st);
        e.Step(At(ms + 249, a.x, a.y, true), st); o = e.Step(At(ms + 249.5, a.x, a.y, false), st);
        Check(o.event == rw::Event::None, "#8 a press at 249 ms: ignored");
        e.Step(At(ms + 251, a.x, a.y, true), st); o = e.Step(At(ms + 252, a.x, a.y, false), st);
        Check(o.event == rw::Event::Pick, "#8 a press at 251 ms: accepted");
        ms += 300;
        for (int k = 0; k < 20; ++k) o = e.Step(At(ms += 10, rw::C, rw::C - 40), st);
        Check(o.centerGrown, "#8 grown before the page change");
        e.PageChanged(ms, false, st);
        o = e.Step(At(ms += 10, rw::C, rw::C - 40), st);
        Check(!o.centerGrown, "#8 the grow timer starts over after a page change");
        e.Reset(l); ms = 0;
        for (int k = 0; k < 20; ++k) o = e.Step(At(ms += 10, a.x, a.y), st);
        Check(o.blurb == 0, "#8 a blurb before the page change");
        e.PageChanged(ms, false, st); o = e.Step(At(ms += 10, a.x, a.y), st);
        Check(o.blurb == -1, "#8 the blurb's hold starts over");
        const float t0 = l.dir[1];
        e.Reset(l); ms = 0;
        for (int k = 0; k < 12; ++k) o = e.Step(At(ms += 10, rw::C + 150 * std::cos(t0), rw::C + 150 * std::sin(t0)), st);   // 110 ms of dwell (10 to 120)
        e.PageChanged(ms, false, st);
        o = e.Step(At(ms += 10, rw::C + 150 * std::cos(t0), rw::C + 150 * std::sin(t0)), st);
        Check(o.tree == -1, "#8 the dwell starts over after a page change");
    }
    {   // #9 pick / bind: each of the 5 slots binds; moved from; the names per hand and controller; refused; no pick right after a hit
        const auto l = ClassLayout(3, ten, rw::TreeLayout::Shown);
        const auto& a = l.seats[0];
        for (int slot = 0; slot < rw::Slots; ++slot) {
            rw::Engine e; e.Reset(l); double ms = 0;
            e.Step(At(ms, a.x, a.y, true), st); e.Step(At(ms += 10, a.x, a.y, false), st);
            const auto o = e.Step(At(ms += 10, a.x, a.y, false, slot), st);
            Check(o.event == rw::Event::Bind && o.slot == slot && o.skill == a.id, "#9 each of the five slots binds");
        }
        Check(rw::SlotOf(0, 0) == framevr::skills::HandSlot(0, 0) && rw::SlotOf(0, 1) == framevr::skills::HandSlot(0, 1) && rw::SlotOf(0, 2) == framevr::skills::HandSlot(0, 2) &&
              rw::SlotOf(0, 3) == framevr::skills::HandSlot(0, 3) && rw::SlotOf(0, 4) == framevr::skills::HandSlot(0, 4), "#9 the right hand's slots by upstream's rule (b + 2 with the grip)");
        Check(rw::SlotOf(1, 0) == 4 && rw::SlotOf(1, 1) == 5 && rw::SlotOf(1, 2) == 6 && rw::SlotOf(1, 3) == 7 && rw::SlotOf(1, 4) == 9, "#9 the left hand's: LT, LT+B, LT+X, LT+Y, LT+A as upstream numbers them");
        rw::Engine e; e.Reset(l); double ms = 0;
        e.Step(At(ms, a.x, a.y, true), st); e.Step(At(ms += 10, a.x, a.y, false), st);
        rw::StepIn in = At(ms += 10, a.x, a.y, false, 3); in.bound[1] = a.id;
        auto o = e.Step(in, st);
        Check(o.event == rw::Event::Bind && o.slot == 3 && o.movedFrom == 1 && o.replaced == -1, "#9 a skill bound elsewhere: moved from that slot");
        e.Step(At(ms += 10, a.x, a.y, true), st); e.Step(At(ms += 10, a.x, a.y, false), st);
        in = At(ms += 10, a.x, a.y, false, 3); in.bound[3] = 4242;
        o = e.Step(in, st);
        Check(o.event == rw::Event::Bind && o.slot == 3 && o.replaced == 4242 && o.movedFrom == -1, "#9 (G2) a taken button: the skill it held is reported (X moved from Fire Wall)");
        using L = framevr::controllers::Layout;
        Check(rw::SlotName(L::Touch, 0, 1) == "A" && rw::SlotName(L::Touch, 0, 2) == "B" && rw::SlotName(L::Touch, 1, 1) == "X" && rw::SlotName(L::Touch, 1, 2) == "Y",
              "#9 Quest: right A / B, left X / Y");
        Check(rw::SlotName(L::Touch, 1, 4) == "Grip + Y" && rw::SlotName(L::Touch, 0, 0) == "Trigger", "#9 grip and trigger slots named");
        Check(rw::SlotName(L::Index, 1, 1) == "A" && rw::SlotName(L::Index, 1, 2) == "B", "#9 Index: A / B on both hands");
        Check(rw::SlotName(L::Frame, 1, 1) == "D-pad Down" && rw::SlotName(L::Frame, 0, 2) == "B", "#9 Steam Frame: its own names");
        bool noPad = true;
        for (L lay : {L::Touch, L::Index, L::Frame}) for (int h = 0; h < 2; ++h) for (int s = 0; s < rw::Slots; ++s) { const auto n = rw::SlotName(lay, h, s); noPad = noPad && n.find("RB") == std::string::npos && n.find("LT") == std::string::npos; }
        Check(noPad, "#9 never gamepad names");
        e.Refused(); o = e.Step(At(ms += 10, a.x, a.y), st);
        Check(o.refused && o.picked == -1, "#9 a refused bind: said once, the pick ended");
        o = e.Step(At(ms += 10, a.x, a.y), st); Check(!o.refused, "#9 said once only");
        e.Step(At(ms += 10, a.x, a.y, true), st); o = e.Step(At(ms += 10, a.x, a.y, false), st);
        Check(o.event == rw::Event::Pick, "#9 after a refusal the next click picks again");
        rw::Engine hit; hit.Reset(l); ms = 1000;
        in = At(ms, a.x, a.y, true); in.lastHitMs = 900; hit.Step(in, st); in = At(ms += 10, a.x, a.y, false); in.lastHitMs = 900; o = hit.Step(in, st);
        Check(o.event == rw::Event::None && o.picked == -1, "#9 110 ms after a hit: no pick");
        in = At(ms += 200, a.x, a.y, true); in.lastHitMs = 900; hit.Step(in, st); in = At(ms += 10, a.x, a.y, false); in.lastHitMs = 900; o = hit.Step(in, st);
        Check(o.event == rw::Event::Pick, "#9 320 ms after: picks");
    }
    {   // #10 hover haptics at most one per 100 ms per hand over a 15 changes a second sweep; P12 0 = no limit
        rw::HapticGate g; int pulses = 0, other = 0;
        for (int k = 0; k < 150; ++k) { const double t = k * 1000.0 / 15; pulses += g.Hover(1, t, 100); other += g.Hover(0, t + 1, 100); }
        Check(pulses == 75 && other == pulses, "#10 15 changes a second for 10 s: every other change (75), never two within 100 ms, each hand its own");
        int quick = 0; rw::HapticGate g2; for (int k = 0; k < 30; ++k) quick += g2.Hover(0, k * 10.0, 100);
        Check(quick == 3, "#10 30 changes in 300 ms: 3 pulses");
        rw::HapticGate g3; int all = 0; for (int k = 0; k < 30; ++k) all += g3.Hover(0, k * 10.0, 0);
        Check(all == 30, "#10 P12 0: no limit");
    }
    // ---- R3 (the design notes items 11-13): placement ----
    {   // #11 sizes: 768 px is 48 deg at 0.80 m and 0.90 m; 15 px a degree at the center; an icon 2.9 deg; the quads
        Check(std::fabs(rw::DegOf(768, .8f, .9f) - 48.0f) < .15f, "#11 the 768 px wheel region is 48 deg wide at 0.80 m, 0.90 m away");
        Check(std::fabs(rw::PxPerDeg(.8f, .9f) - 15) <= .5f, "#11 15 +- 0.5 px a degree at the center");
        Check(std::fabs(rw::DegOf(44, .8f, .9f) - 2.9f) < .05f && rw::DegOf(54, .8f, .9f) >= 3.5f, "#11 a 44 px icon is 2.9 deg, its 54 px hit 3.6");
        Check(std::fabs(rw::QuadMeters(768, .8f) - .80f) < 1e-4f && std::fabs(rw::QuadMeters(880, .8f) - .9167f) < 1e-3f &&
              std::fabs(rw::QuadMeters(864, .8f) - .90f) < 1e-4f && std::fabs(rw::QuadMeters(1024, .8f) - 1.0667f) < 1e-3f,
              "#11 quads: 0.80 x 0.917 m by default, 0.90 m wide at 864 px, 1.067 m at 1024");
    }
    {   // #12 the spawn pose: yaw only, below_deg below eye level, facing the eyes, world-locked, respawned on a hand switch or a change
        namespace m = framevr::math;
        auto headAt = [](float yawDeg, float pitchDeg) {
            const float y = yawDeg * rw::Pi / 180, p = pitchDeg * rw::Pi / 180;
            const m::Quat qy{0, std::sin(y / 2), 0, std::cos(y / 2)}, qp{std::sin(p / 2), 0, 0, std::cos(p / 2)};
            return ia::Pose{{.3f, 1.6f, -.2f}, m::Multiply(qy, qp)};
        };
        rw::Placement pl;
        const auto a = rw::Spawn(headAt(30, 0), pl), b = rw::Spawn(headAt(30, 30), pl), c = rw::Spawn(headAt(30, -30), pl);
        auto same = [](const ia::Pose& x, const ia::Pose& y) { return std::hypot(x.p.x - y.p.x, x.p.y - y.p.y, x.p.z - y.p.z) < 1e-3f && std::fabs(m::Dot({x.q.x, x.q.y, x.q.z}, {y.q.x, y.q.y, y.q.z}) + x.q.w * y.q.w) > .9999f; };
        Check(same(a, b) && same(a, c), "#12 head pitch 0, 30 and -30: the same panel (yaw only)");
        for (float below : {5.f, 8.f}) {
            rw::Placement q = pl; q.belowDeg = below;
            const auto h = headAt(-50, 10); const auto s = rw::Spawn(h, q);
            const m::Vec d = s.p - h.p; const float dist = std::sqrt(m::Dot(d, d));
            Check(std::fabs(std::asin(-d.y / dist) * 180 / rw::Pi - below) < .05f && std::fabs(dist - .9f) < 1e-4f, below == 5 ? "#12 below 5: its center 5 deg under eye level, 0.90 m away" : "#12 below 8: 8 deg under");
            const m::Vec n = m::Rotate(s.q, {0, 0, 1});
            Check(m::Dot(n, m::Normalized(h.p - s.p)) > .9999f, "#12 its face toward the eyes");
            const m::Vec f = m::Rotate(h.q, {0, 0, -1}), flat = m::Normalized({f.x, 0, f.z});
            Check(m::Dot(m::Normalized({d.x, 0, d.z}), flat) > .9999f, "#12 along the heading");
        }
        rw::Placer placer;
        const auto& first = placer.Step(0, headAt(0, 0), pl); const ia::Pose kept = first;
        Check(placer.Spawned, "#12 an open spawns it");
        const auto& moved = placer.Step(0, headAt(40, 20), pl);
        Check(!placer.Spawned && same(moved, kept), "#12 world-locked: the head turns, the panel stays");
        placer.Step(1, headAt(40, 0), pl);
        Check(placer.Spawned && !same(placer.Step(1, headAt(40, 0), pl), kept), "#12 a hand switch respawns it where you face");
        rw::Placement wider = pl; wider.widthM = .9f;
        placer.Step(1, headAt(40, 0), wider);
        Check(placer.Spawned, "#12 a Placement change respawns it");
        // review H1: reopen with the same hand and the same Placement as before the close, from another heading, so only the
        // close can make it respawn
        const ia::Pose beforeClose = placer.Step(1, headAt(40, 0), wider);
        Check(!placer.Spawned, "#12 (H1) the same hand and Placement held open: kept");
        placer.Step(-1, headAt(-60, 0), wider);
        const ia::Pose reopened = placer.Step(1, headAt(-60, 0), wider);
        Check(placer.Spawned && !same(reopened, beforeClose), "#12 (H1) a close and a reopen, same hand and Placement: respawned where you face now");
        bool sane = true;
        for (float d : {.5f, 1.5f}) for (float w : {.4f, 1.f}) for (float bdeg : {0.f, 20.f}) {
            rw::Placement x{d, w, bdeg, true};
            const auto h = headAt(123, -15); const auto s = rw::Spawn(h, x);
            const m::Vec f = m::Rotate(h.q, {0, 0, -1}), v = s.p - h.p;
            sane = sane && std::isfinite(s.p.x + s.p.y + s.p.z + s.q.x + s.q.y + s.q.z + s.q.w) && m::Dot(m::Normalized({v.x, 0, v.z}), m::Normalized({f.x, 0, f.z})) > .99f &&
                   std::isfinite(rw::PxPerDeg(w, d)) && rw::QuadMeters(768, w) > 0;
        }
        Check(sane, "#12 the rows' ends (50 / 150 cm, 40 / 100 cm, 0 / 20 deg): finite, ahead, never behind the head");
    }
    {   // #13 the laser: 1.5 m on a miss; a ray hits the same picture point whether the head is straight or turned 20 deg
        namespace m = framevr::math;
        Check(rw::BeamMissM == 1.5f, "#13 1.5 m beam on a miss (upstream's 0.65 m is shorter than the panel)");
        rw::Placement pl; rw::Placer placer;
        const ia::Pose head{{0, 1.6f, 0}, {0, 0, 0, 1}};
        const ia::Pose panel = placer.Step(0, head, pl);
        const m::Vec hand{.15f, 1.2f, -.3f}, target = panel.p + m::Rotate(panel.q, {.1f, .05f, 0});
        const m::Vec dir = m::Normalized(target - hand);
        m::PanelHit straight, turned;
        const bool h1 = m::IntersectPanel(hand, dir, panel.p, panel.q, pl.widthM, pl.widthM, straight);
        const float t = 20 * rw::Pi / 180;
        const ia::Pose& after = placer.Step(0, {{0, 1.6f, 0}, {0, std::sin(t / 2), 0, std::cos(t / 2)}}, pl);
        const bool h2 = m::IntersectPanel(hand, dir, after.p, after.q, pl.widthM, pl.widthM, turned);
        Check(h1 && h2 && std::fabs(straight.u - turned.u) < 1e-5f && std::fabs(straight.v - turned.v) < 1e-5f && std::fabs(straight.u - (.5f + .1f / .8f)) < 1e-3f,
              "#13 the head turned 20 deg: the same ray lands on the same point of the picture");
        m::PanelHit miss;
        Check(!m::IntersectPanel(hand, {0, 0, 1}, panel.p, panel.q, pl.widthM, pl.widthM, miss), "#13 a ray away from it misses (the beam takes 1.5 m)");
    }
    {   // R6 (plan E3, E5): the ring's content quad, the laser's hit on it, the open hand's slot press, the bind request
        namespace m = framevr::math;
        auto near = [](float a, float b, float e) { return std::fabs(a - b) <= e; };
        const auto q768 = rw::QuadFor(128, 112, 768, 880, .8f), q864 = rw::QuadFor(80, 112, 864, 880, .8f), q1024 = rw::QuadFor(0, 112, 1024, 880, .8f);
        Check(near(q768.w, .8f, 1e-4f) && near(q768.h, 880.f / 960, 1e-4f) && near(q864.w, .9f, 1e-4f) && near(q1024.w, 1024.f / 960, 1e-4f),
              "R6 E3: the quad 0.80 x 0.917 m; the 864 px footer 0.90 m wide; full width 1.067 m");
        Check(near(q768.dx, 0, 1e-5f) && near(q768.dy, -56.f / 960, 1e-5f), "R6 E3: the content's center 56 canvas px (5.8 cm) below the wheel's");
        float x = 0, y = 0;
        rw::CanvasOf(.5f, .5f, 128, 112, 768, 880, x, y);
        Check(near(x, 512, 1e-3f) && near(y, 552, 1e-3f), "R6: the quad's middle is canvas (512, 552)");
        rw::CanvasOf(0, 1, 128, 112, 768, 880, x, y);
        Check(near(x, 128, 1e-3f) && near(y, 992, 1e-3f), "R6: its lower left corner is canvas (128, 992)");
        // A ray at the wheel's center (the placed pose) lands on the wheel's center in canvas px through the shifted quad.
        for (const float yaw : {0.f, .7f, -1.9f}) {
            const ia::Pose wheel{{.3f, 1.4f, -.9f}, {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}};
            const m::Vec quadCenter = wheel.p + m::Rotate(wheel.q, {q768.dx, q768.dy, 0});
            const m::Vec target = wheel.p + m::Rotate(wheel.q, {.1f, .05f, 0});   // 96 px right, 48 px up of the wheel's center
            const m::Vec from = target + m::Rotate(wheel.q, {0, 0, .6f});
            m::PanelHit h;
            const bool ok = m::IntersectPanel(from, m::Rotate(wheel.q, {0, 0, -1}), quadCenter, wheel.q, q768.w, q768.h, h);
            rw::CanvasOf(h.u, h.v, 128, 112, 768, 880, x, y);
            Check(ok && near(x, rw::CanvasCx + 96, .05f) && near(y, rw::CanvasCy - 48, .05f), "R6: a hit 10 cm right, 5 cm up of the wheel's center is canvas (+96, -48) at any heading");
        }
        bool held[3] = {};
        const bool trig[3] = {true, false, false}, prim[3] = {false, true, false}, sec[3] = {false, false, true}, none[3] = {};
        Check(rw::SlotPress(trig, false, held) == 0 && rw::SlotPress(trig, false, held) == -1, "R6: the trigger's press is slot 0, once (held: no repeat)");
        rw::SlotPress(none, false, held);
        Check(rw::SlotPress(prim, false, held) == 1 && (rw::SlotPress(none, false, held), rw::SlotPress(sec, false, held)) == 2, "R6: primary slot 1, secondary slot 2");
        rw::SlotPress(none, false, held);
        Check(rw::SlotPress(prim, true, held) == 3 && (rw::SlotPress(none, true, held), rw::SlotPress(sec, true, held)) == 4, "R6: grip + primary 3, grip + secondary 4 (upstream's HandSlot rule)");
        bool down[3] = {true, true, false};
        Check(rw::SlotPress(down, false, held) == 0, "R6: two buttons on one frame: the trigger (one press a frame)");
        // The bind request passes upstream's own CanBind, and a stale one does not.
        ia::Snapshot s; s.skillClass = 1; s.session = 77; s.at = 1000; s.bindingsEditable = true; s.bindingBank = 0;
        { auto& k = s.catalog[s.skillCount++]; k.id = 47; k.owner = -1; k.known = k.available = true; }
        const int index = rw::SlotOf(0, 1);
        s.skills[size_t(index)].id = 36; s.skills[size_t(index)].owner = -1;
        const auto r = rw::BindRequest(s, index, 47, -1, 5, 77, 1200);
        Check(r.action == ia::Action::BindSkill && (r.sequence >> 63) == 1 && r.index == index && r.skill == 47 && r.previousSkill == 36,
              "R6: a BindSkill with the slot, the skill, the slot's old skill, the sequence's high bit (never upstream's)");
        Check(ia::CanBind(s, r, 1200), "R6: upstream's CanBind takes the ring's bind");
        Check(!ia::CanBind(s, r, 1000 + 651), "R6: a stale snapshot refuses it (upstream's rule stands)");
        auto other = s; other.skills[size_t(index)].id = 39;
        Check(!ia::CanBind(other, r, 1200), "R6: the slot changed under it: refused");
        const auto clear = rw::BindRequest(s, index, -1, 3, 6, 77, 1200);
        Check(clear.skill == -1 && clear.skillOwner == -1 && ia::CanBind(s, clear, 1200), "R6: a clear (skill -1) passes CanBind with owner -1");
        Check(rw::BindRequest(s, -1, 47, -1, 7, 77, 1200).action == ia::Action::None && rw::BindRequest(s, framevr::skills::Count, 47, -1, 7, 77, 1200).action == ia::Action::None,
              "R6: a slot out of range sends nothing");
        {   // the player 2026-11-08 #3: the Draw glyph request is upstream's picker's (skill_categories_test): skill -1, the old skill, CanBind passes
            const auto d = rw::GlyphDrawRequest(s, index, 8, 77, 1200);
            Check(d.action == ia::Action::BindGlyphDraw && d.index == index && d.skill == -1 && d.skillOwner == -1 && d.bank == s.bindingBank && d.previousSkill == 36 && (d.sequence >> 63) == 1, "#3 Draw glyph: upstream's BindGlyphDraw for this slot, its game skill cleared");
            Check(ia::CanBind(s, d, 1200), "#3 Draw glyph: the clear it asks for passes upstream's CanBind");
            auto locked = s; locked.bindingsEditable = false;
            Check(rw::GlyphDrawRequest(locked, index, 8, 77, 1200).action == ia::Action::None, "#3 not editable: nothing sent (the transport would save draw_slot before the clear is refused)");
            Check(rw::GlyphDrawRequest(s, index, 8, 77, 1000 + 651).action == ia::Action::None, "#3 a stale snapshot: nothing sent");
            Check(rw::GlyphDrawRequest(s, framevr::skills::Count, 8, 77, 1200).action == ia::Action::None, "#3 a slot out of range: nothing sent");
        }
    }
    {   // #9 (R6Q, the player's design): quick cast and hold to assign
        const int per[3] = {10, 10, 10};
        const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
        const rw::Target seatA{rw::Kind::Seat, 4}, seatB{rw::Kind::Seat, 12}, gap{rw::Kind::Gap}, off{};
        // Run one press: the laser on `at` from t 0, `slot` of `hand` pressed at t 200, released at 200 + heldMs (or never).
        struct Run { std::vector<rw::QuickOut> events; rw::QuickOut last; };
        auto run = [&](int hand, int slot, double heldMs, rw::Target at, int holdMs = 1000, int tapMs = 350, bool main = true, double until = -1) {
            rw::QuickCast q; Run r;
            const double end = until > 0 ? until : 200 + heldMs + 50;
            for (double t = 0; t <= end; t += 1) {
                rw::QuickIn in; in.nowMs = t; in.main = main; in.under = at; in.holdMs = holdMs; in.tapMs = tapMs;
                in.bound[0] = {36, 39, 40, -1, -1}; in.bound[1] = {-1, -1, -1, -1, -1};
                const bool isDown = t >= 200 && (heldMs < 0 || t < 200 + heldMs);
                if (t == 200) in.press[hand] = slot;
                in.down[hand][slot] = isDown;
                const auto o = q.Step(in, l);
                if (o.event != rw::Quick::None) r.events.push_back(o);
                r.last = o;
            }
            return r;
        };
        auto only = [](const Run& r, rw::Quick e) { return r.events.size() == 1 && r.events[0].event == e; };
        Check(only(run(0, 1, 349, seatA), rw::Quick::QuickCast) && run(0, 1, 349, seatA).events[0].seat == 4, "#9 a tap of 349 ms: QuickCast on the seat");
        Check(only(run(0, 1, 351, seatA), rw::Quick::NoChange), "#9 351 ms: past the tap limit (350, not the old 250): NoChange");
        Check(only(run(0, 1, 260, seatA), rw::Quick::QuickCast), "#9 260 ms is still a tap (the old 250 ms limit does not leak in)");
        Check(only(run(0, 1, 999, seatA), rw::Quick::NoChange), "#9 released at 999 ms: NoChange");
        const auto full = run(0, 1, -1, seatA, 1000, 350, true, 200 + 1001);
        Check(only(full, rw::Quick::Assign) && full.events[0].slot == 1 && full.events[0].replaced == 39, "#9 held to 1000 ms: Assign without a release, replacing the slot's old skill");
        Check(only(run(0, 1, 1500, seatA), rw::Quick::Assign), "#9 released after full: nothing more (no cast, no second assign)");
        Check(rw::EffectiveHoldMs(300, 350) == 500 && rw::EffectiveHoldMs(500, 400) == 550 && rw::EffectiveHoldMs(0, 350) == 0 && rw::EffectiveHoldMs(1200, 350) == 1200,
              "#9 Hold to assign: 100-400 snap to 500, at least tap + 150, 0 = Off");
        Check(only(run(0, 1, 520, seatA, 500, 400), rw::Quick::NoChange) && only(run(0, 1, -1, seatA, 500, 400, true, 200 + 551), rw::Quick::Assign),
              "#9 the clamp: hold 500 with tap 400 assigns at 550, not 500");
        Check(only(run(0, 1, 2000, seatA, 0), rw::Quick::QuickCast), "#9 Hold to assign Off: every press a QuickCast, however long");
        {   // the player 2026-11-08 #3: the Glyphs half of the center takes a press like a skill: a tap opens its page, a hold assigns Draw glyph
            const rw::Target glyphsHalf{rw::Kind::Glyphs}, gesturesHalf{rw::Kind::Gestures};
            Check(only(run(0, 1, 349, glyphsHalf), rw::Quick::GlyphTap), "#3 a tap on the Glyphs half: GlyphTap (no cast)");
            Check(only(run(0, 1, 351, glyphsHalf), rw::Quick::NoChange), "#3 released between the tap limit and full: no change");
            const auto held = run(0, 1, -1, glyphsHalf, 1000, 350, true, 200 + 1001);
            Check(held.last.assigned && held.last.holding == rw::GlyphsSeat, "#3 held on after full: the hold stays, marked assigned");
            Check(only(held, rw::Quick::GlyphAssign) && held.events[0].hand == 0 && held.events[0].slot == 1 && held.events[0].seat == rw::GlyphsSeat && held.events[0].replaced == 39, "#3 held to full: GlyphAssign on that button, the skill it replaces named");
            Check(only(run(1, 4, -1, glyphsHalf, 1000, 350, true, 200 + 1001), rw::Quick::GlyphAssign), "#3 any of the ten buttons (left grip + secondary)");
            Check(only(run(0, 0, -1, glyphsHalf, 1000, 350, true, 200 + 1001), rw::Quick::GlyphAssign), "#3 the trigger too (upstream's picker allows it)");
            Check(only(run(0, 1, 1500, glyphsHalf), rw::Quick::GlyphAssign), "#3 released after full: nothing more");
            Check(only(run(0, 1, 2000, glyphsHalf, 0), rw::Quick::GlyphTap), "#3 Hold to assign Off: never assigns, a press opens the page");
            Check(run(0, 1, -1, gesturesHalf, 1000, 350, true, 200 + 1001).events.empty(), "#3 the Gestures half takes no hold");
            Check(run(0, 1, -1, glyphsHalf, 1000, 350, false, 200 + 1001).events.empty(), "#3 off the main page: nothing");
            Check(std::fabs(run(0, 1, -1, glyphsHalf, 1000, 350, true, 200 + 675).last.holdFrac - .5f) < .01f && run(0, 1, -1, glyphsHalf, 1000, 350, true, 200 + 675).last.holding == rw::GlyphsSeat, "#3 the hold ring fills as on a skill");
            {   // a sweep onto the half at the press never starts a hold (the seats' 80 ms rule)
                rw::QuickCast q; bool any = false;
                for (double t = 0; t <= 1500; t += 1) {
                    rw::QuickIn in; in.nowMs = t; in.main = true; in.under = t >= 190 ? glyphsHalf : rw::Target{}; in.bound[0] = {-1, -1, -1, -1, -1}; in.bound[1] = {-1, -1, -1, -1, -1};
                    if (t == 200) in.press[0] = 1;
                    in.down[0][1] = t >= 200;
                    const auto o = q.Step(in, l); any |= o.event != rw::Quick::None || o.holding >= 0;
                }
                Check(!any, "#3 a sweep onto the Glyphs half 10 ms before the press: no hold");
            }
            // The pointing hand's trigger is also the engine's click: a tap opens through the engine once; a hold opens nothing.
            rw::QuickOut h; h.holding = rw::GlyphsSeat; h.hand = 0; h.slot = 0;
            h.heldMs = 349; Check(!rw::GlyphClickSpent(h, 0, 1000, 350), "#3 the click is not spent under the tap limit");
            h.heldMs = 350; Check(rw::GlyphClickSpent(h, 0, 1000, 350), "#3 the click is spent from the tap limit on");
            Check(!rw::GlyphClickSpent(h, 1, 1000, 350), "#3 another hand's trigger is not this click");
            Check(!rw::GlyphClickSpent(h, 0, 0, 350), "#3 Hold to assign Off: the click stands");
            { auto f = h; f.slot = 1; Check(!rw::GlyphClickSpent(f, 0, 1000, 350), "#3 a face button's hold leaves the trigger's click alone"); }
            { auto f = h; f.holding = 4; Check(!rw::GlyphClickSpent(f, 0, 1000, 350), "#3 a hold on a skill is not this rule"); }
            Check(!rw::GlyphTapClicks(0, 0, 0) && rw::GlyphTapClicks(0, 1, 0) && rw::GlyphTapClicks(1, 0, 0), "#3 only the pointing hand's trigger leaves the click to the engine");
            rw::Settings qs; qs.quickCast = true;
            auto release = [&](bool spent) {
                rw::Engine x; x.Reset(l); rw::StepOut last{}; bool opened = false;
                for (double t = 0; t < 2000; t += 10) {
                    rw::StepIn in; in.nowMs = t; in.hit = true; in.x = rw::CanvasCx; in.y = rw::CanvasCy - 40; in.trigger = t >= 200 && t < 1800; in.glyphClickSpent = spent;
                    last = x.Step(in, qs); opened |= last.event == rw::Event::Upstream && last.cell == ia::GlyphsCell;
                }
                return opened;
            };
            Check(release(false), "#3 the engine's trigger click on the Glyphs half still opens the page");
            Check(!release(true), "#3 a spent click opens nothing");
        }
        for (int hand = 0; hand < 2; ++hand) for (int slot = 0; slot < rw::Slots; ++slot)
            Check(only(run(hand, slot, 100, seatA), rw::Quick::QuickCast) && run(hand, slot, 100, seatA).events[0].hand == hand && run(hand, slot, 100, seatA).events[0].slot == slot,
                  "#9 all ten slots, either hand, quick cast");
        Check(run(0, 1, 100, seatA, 1000, 350, false).events.empty(), "#9 not the main page (Glyphs, Gestures, a glyph choice): no QuickCast or Assign");
        Check(run(0, 1, 100, gap).events.empty(), "#9 a press on a gap: nothing");
        // The seat latch: the laser on seat A until the press, then a jolt onto seat B at the press: still A.
        {
            rw::QuickCast q; std::vector<rw::QuickOut> ev;
            for (double t = 0; t <= 500; t += 1) {
                rw::QuickIn in; in.nowMs = t; in.main = true; in.under = t < 195 ? seatA : seatB;
                if (t == 200) in.press[0] = 1;
                in.down[0][1] = t >= 200 && t < 300;
                const auto o = q.Step(in, l); if (o.event != rw::Quick::None) ev.push_back(o);
            }
            Check(ev.size() == 1 && ev[0].event == rw::Quick::QuickCast && ev[0].seat == 4, "#9 a jolt onto another seat at the press: the seat held 80 ms before (and fixed while held)");
        }
        {   // A sweep that crossed a seat 80 ms ago and left it: nothing. A laser that just arrived: nothing.
            rw::QuickCast q; int n = 0;
            for (double t = 0; t <= 500; t += 1) {
                rw::QuickIn in; in.nowMs = t; in.main = true; in.under = t >= 110 && t < 130 ? seatA : gap;
                if (t == 200) in.press[0] = 1;
                in.down[0][1] = t >= 200 && t < 300;
                n += q.Step(in, l).event != rw::Quick::None;
            }
            rw::QuickCast q2; int n2 = 0;
            for (double t = 0; t <= 500; t += 1) {
                rw::QuickIn in; in.nowMs = t; in.main = true; in.under = t >= 170 ? seatA : gap;
                if (t == 200) in.press[0] = 1;
                in.down[0][1] = t >= 200 && t < 300;
                n2 += q2.Step(in, l).event != rw::Quick::None;
            }
            Check(n == 0 && n2 == 0, "#9 no seat held through the 80 ms before the press: the press does nothing");
        }
        {   // A second press while one is held is ignored; a drop (page change, close) mid-hold assigns nothing.
            rw::QuickCast q; std::vector<rw::QuickOut> ev;
            for (double t = 0; t <= 1400; t += 1) {
                rw::QuickIn in; in.nowMs = t; in.main = true; in.under = seatA;
                if (t == 200) in.press[0] = 1;
                if (t == 300) in.press[1] = 2;
                in.down[0][1] = t >= 200 && t < 1300; in.down[1][2] = t >= 300 && t < 400;
                if (t == 900) q.Drop();
                const auto o = q.Step(in, l); if (o.event != rw::Quick::None) ev.push_back(o);
            }
            Check(ev.empty(), "#9 a second button while one is held is ignored; a drop at 900 ms: no assign at 1000");
        }
        // Cancel: off the wheel + the plain cancel button = Close; on a seat it is a slot; on a gap inside nothing; grip + B a slot.
        Check(only(run(0, 2, 50, off), rw::Quick::Close) && only(run(1, 1, 50, off), rw::Quick::Close), "#9 off the wheel: right B (secondary) or left X (primary) = Close");
        Check(only(run(0, 2, 50, seatA), rw::Quick::QuickCast), "#9 B on a seat is the secondary slot's press");
        Check(run(0, 2, 50, gap).events.empty(), "#9 B on a gap inside the wheel does nothing");
        Check(run(0, 4, 50, off).events.empty() && only(run(0, 4, 50, seatA), rw::Quick::QuickCast), "#9 grip + B is always a slot: off the wheel nothing, on a seat a cast");
        Check(run(0, 1, 50, off).events.empty(), "#9 right A off the wheel: not a cancel");
        const auto mid = run(0, 1, -1, seatA, 1000, 350, true, 200 + 675);
        Check(std::fabs(mid.last.holdFrac - .5f) < .01f && mid.last.holding == 4, "#9 the hold ring: half full halfway from the tap limit to full; the held seat named");
        Check(run(0, 1, -1, seatA, 1000, 350, true, 200 + 300).last.holdFrac == 0, "#9 the ring starts only at the tap limit");
        // The engine with quick cast: a trigger release on a seat picks nothing; on a center half it still clicks upstream.
        rw::Engine e; e.Reset(l); rw::Settings set; set.quickCast = true;
        const auto& s4 = l.seats[4];
        rw::StepOut o{};
        for (double t = 0; t < 400; t += 10) { rw::StepIn in; in.nowMs = t; in.hit = true; in.x = rw::CanvasX + s4.x; in.y = rw::CanvasY + s4.y; in.trigger = t >= 200 && t < 300; o = e.Step(in, set); if (o.event != rw::Event::None) break; }
        Check(o.event == rw::Event::None && o.picked == -1, "#9 quick cast on: the trigger's release on a seat is no pick");
        rw::Engine e2; e2.Reset(l); rw::StepOut o2{}; bool clicked = false;
        for (double t = 0; t < 400; t += 10) { rw::StepIn in; in.nowMs = t; in.hit = true; in.x = rw::CanvasCx; in.y = rw::CanvasCy - 40; in.trigger = t >= 200 && t < 300; o2 = e2.Step(in, set); clicked |= o2.event == rw::Event::Upstream && o2.cell == ia::GlyphsCell; }
        Check(clicked, "#9 quick cast on: the trigger on the Glyphs half still clicks upstream on release");
    }
    {   // #9 (R6Q2): the cast through upstream's glyph press, with a fake glyphcast::Cast; #10 the hold's ticks
        namespace gc = framevr::glyphcast;
        struct FakeCast { int begins = 0; bool busy = false; gc::Plan plan{}; int skill = -1, owner = -2; bool Busy() const { return busy; }
                          void Begin(const gc::Plan& p, int k, int o, double) { ++begins; plan = p; skill = k; owner = o; } };
        ia::Snapshot s; s.skillClass = 1;
        auto add = [&](int id, bool available) { auto& k = s.catalog[s.skillCount++]; k.id = id; k.owner = -1; k.known = true; k.available = available; };
        add(47, true); add(56, true); add(59, false);
        // Fire Ball (47) on the right hand's primary; Meteor (56) on no slot; the right trigger's record known (Fire Bolt there).
        const int primary = rw::SlotOf(0, 1);
        s.skills[size_t(primary)].id = 47; s.skills[size_t(primary)].owner = -1; s.skills[size_t(primary)].known = s.skills[size_t(primary)].available = true;
        s.glyphRecord = 5; s.glyphSkill = 36; s.glyphOwner = -1;
        const auto before = s.skills;
        int parks = 0;
        auto park = [&](int, int) { ++parks; return true; };
        auto go = [&](FakeCast& c, int skill, bool labOn = true, bool shut = true, double waited = 0, bool fresh = true) {
            rw::CastWait w{true, skill, -1, 0, 1, 0};
            const auto r = rw::StartCast(w, c, s, labOn, fresh, true, shut, 1.0, waited, park);
            return std::make_pair(r, w);
        };
        {
            FakeCast c;
            const auto [r, w] = go(c, 47);
            Check(r.started && c.begins == 1 && c.skill == 47 && c.plan.how == gc::How::Slot && !w.on, "#9 a tap on a skill on a slot: one Begin, through its hand slot");
            Check(s.skills == before && parks == 0, "#9 a cast changes no binding and parks nothing");
        }
        {
            FakeCast c;
            const auto [r, w] = go(c, 56);
            Check(r.started && c.begins == 1 && c.plan.how == gc::How::Park && parks == 1 && s.skills == before, "#9 a skill on no slot: parked on the right trigger's record, one Begin, no binding change");
        }
        {
            FakeCast c; s.glyphRecord = -1;
            const auto [r, w] = go(c, 56);
            Check(!r.started && r.refused && c.begins == 0, "#9 the record unknown: refused, no cast");
            s.glyphRecord = 5;
        }
        {
            FakeCast c; c.busy = true;
            const auto busy = go(c, 47);
            FakeCast c2;
            const auto unavailable = go(c2, 59);
            FakeCast c3;
            const auto unknown = go(c3, 999);
            FakeCast c4;
            const auto stale = go(c4, 47, true, true, 0, false);
            Check(busy.first.refused && unavailable.first.refused && unknown.first.refused && stale.first.refused &&
                  c.begins + c2.begins + c3.begins + c4.begins == 0, "#9 refused when busy, unavailable, not the hero's, or stale: no cast");
        }
        {
            FakeCast c;
            const auto [r, w] = go(c, 47, true, false, 200);
            Check(!r.started && !r.refused && w.on && c.begins == 0, "#9 the wheel not shut yet: the cast waits");
            FakeCast c2;
            const auto late = go(c2, 47, true, false, 1200);
            Check(late.first.refused && !late.second.on && c2.begins == 0, "#9 the wheel never shut within 1 s: refused");
        }
        {
            FakeCast c;
            const auto [r, w] = go(c, 47, false);
            Check(!r.started && !r.refused && !w.on && c.begins == 0, "perf R6Q: the ring Off with a cast queued: it is dropped, never fires");
        }
        // One tap = one Begin: the quick cast state machine emits one QuickCast, and StartCast runs it once.
        {
            const int per[3] = {10, 10, 10};
            const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
            rw::QuickCast q; FakeCast c; rw::CastWait w; int casts = 0;
            for (double t = 0; t <= 600; t += 1) {
                rw::QuickIn in; in.nowMs = t; in.main = true; in.under = {rw::Kind::Seat, 4};
                if (t == 200) in.press[0] = 1;
                in.down[0][1] = t >= 200 && t < 300;
                const auto o = q.Step(in, l);
                if (o.event == rw::Quick::QuickCast) { w = {true, 47, -1, o.hand, o.slot, t}; ++casts; }
                rw::StartCast(w, c, s, true, true, true, t > 320, t / 1000, 0, park);
            }
            Check(casts == 1 && c.begins == 1, "#9 one tap: one QuickCast, one Begin");
        }
        Check(rw::TicksCrossed(.2f, .3f) == 1 && rw::TicksCrossed(.3f, .45f) == 0 && rw::TicksCrossed(.45f, .8f) == 2 && rw::TicksCrossed(0, 1) == 3 && rw::TicksCrossed(.75f, 1) == 0,
              "#10 ticks at 25 / 50 / 75 % of the hold, each once");
    }
    {   // R7 (eval #7, #18-#20; review R2 G7): the Glyphs and Gestures pages' routing, the grow delay and blurb hold, the glyph list
        rw::Settings set;
        auto at = [](int i, int n, float r) { float x, y; rw::RingPos(i, n, r, x, y); return std::pair<float, float>{x + rw::CanvasX, y + rw::CanvasY}; };
        auto release = [&](rw::PageEngine& e, rw::RingPage page, int count, float x, float y, double& t) {
            rw::PageIn in; in.page = page; in.count = count; in.hit = true; in.x = x; in.y = y;
            in.nowMs = t; in.trigger = true; e.Step(in, set); t += 20;
            in.nowMs = t; in.trigger = false; const auto o = e.Step(in, set); t += 20; return o;
        };
        auto settle = [&](rw::PageEngine& e, rw::RingPage page, int count, double& t) {   // past the page change's lockout
            rw::PageIn in; in.page = page; in.count = count; in.nowMs = t; e.Step(in, set); t += 300;
        };
        {
            rw::PageEngine e; double t = 0; settle(e, rw::RingPage::Glyphs, 8, t);
            for (int i = 0; i < 8; ++i) {
                const auto [x, y] = at(i, 8, rw::GlyphR);
                const auto o = release(e, rw::RingPage::Glyphs, 8, x + 30, y, t);   // 30 px off its center: inside the 40 px hit
                Check(o.event == rw::PageEvent::ChooseSkill && o.shape == i, "R7 Glyphs: a release on shape i chooses its skill (no upstream cell)");
            }
            auto o = release(e, rw::RingPage::Glyphs, 8, rw::CanvasCx, rw::CanvasCy - 40, t);
            Check(o.event == rw::PageEvent::Upstream && o.cell == ia::GlyphsCell, "R7 Glyphs: the top half is Back (upstream's GLYPHS box again)");
            o = release(e, rw::RingPage::Glyphs, 8, rw::CanvasCx, rw::CanvasCy + 40, t);
            Check(o.event == rw::PageEvent::Upstream && o.cell == ia::GlyphHandCell, "R7 Glyphs: the lower half is the other hand (upstream's hand box)");
            o = release(e, rw::RingPage::Glyphs, 8, rw::CanvasCx + 1, rw::CanvasCy + 1, t);
            Check(o.event == rw::PageEvent::Upstream && o.cell == ia::GlyphHandCell, "R7 Glyphs: the 6 px dead band keeps the last half");
            o = release(e, rw::RingPage::Glyphs, 8, rw::CanvasCx + 120, rw::CanvasCy - 50, t);   // between the center and the shapes
            Check(o.event == rw::PageEvent::None, "R7 Glyphs: a gap fires nothing");
            const float fx = (rw::CanvasPx - rw::FooterWidth(false, 5)) / 2 + rw::FooterWidth(false, 5) * .1f, fy = rw::CanvasPx * .93f;
            o = release(e, rw::RingPage::Glyphs, 8, fx, fy, t);
            Check(o.event == rw::PageEvent::Upstream && o.cell == ia::FooterCell(0, ia::MenuPage::Glyphs), "R7 Glyphs: the footer on upstream's Glyphs page (G7)");
            // Pressed on one shape, released on another: nothing.
            const auto [ax, ay] = at(0, 8, rw::GlyphR); const auto [bx, by] = at(4, 8, rw::GlyphR);
            rw::PageIn in; in.page = rw::RingPage::Glyphs; in.count = 8; in.hit = true; in.x = ax; in.y = ay; in.nowMs = t; in.trigger = true; e.Step(in, set);
            in.x = bx; in.y = by; in.nowMs = t + 20; in.trigger = false;
            Check(e.Step(in, set).event == rw::PageEvent::None, "R7 fires only on the target pressed");
        }
        {   // the page change: 250 ms lockout, a trigger held across it let go first
            rw::PageEngine e; rw::PageIn in; in.page = rw::RingPage::Gestures; in.count = 10; in.hit = true;
            const auto [x, y] = at(3, 10, rw::GestureR); in.x = x; in.y = y;
            in.nowMs = 0; in.trigger = true; Check(e.Step(in, set).locked, "R7 locked at the page change");
            in.nowMs = 300; in.trigger = false; Check(e.Step(in, set).event == rw::PageEvent::None, "R7 a trigger held across the change: its release fires nothing");
            in.nowMs = 320; in.trigger = true; e.Step(in, set);
            in.nowMs = 340; in.trigger = false; const auto o = e.Step(in, set);
            Check(o.event == rw::PageEvent::Upstream && o.cell == ia::MotionCardCell(3), "R7 Gestures: a release on card i = upstream's card cell i");
            rw::PageEngine l; rw::PageIn q = in; q.nowMs = 0; q.trigger = false; l.Step(q, set);
            q.nowMs = 100; q.trigger = true; l.Step(q, set); q.nowMs = 120; q.trigger = false;
            Check(l.Step(q, set).event == rw::PageEvent::None, "R7 a press inside the 250 ms lockout fires nothing");
        }
        {   // 2026-11-16 (the player: "ring menu > quick cast / ring menu > gestures / ring menu > glyph"): Quick cast's page, its 12 places
            // on r 205 open the chooser for that place, the whole center is Back (the ring's page: no upstream cell), the footer upstream's Spells
            rw::PageEngine e; double t = 0; settle(e, rw::RingPage::Quick, rw::QuickPlaces, t);
            for (int i = 0; i < rw::QuickPlaces; ++i) {
                const auto [x, y] = at(i, rw::QuickPlaces, rw::QuickR);
                const auto o = release(e, rw::RingPage::Quick, rw::QuickPlaces, x + 25, y, t);   // 25 px off its center: inside the 34 px hit
                Check(o.event == rw::PageEvent::ChooseSkill && o.shape == i && o.cell < 0, "Quick cast page: a release on place i opens its chooser (no upstream cell)");
            }
            // 2026-11-17 (review F8: Move costs no chooser seat): the center's upper half is Back, its lower half Move
            {
                const auto o = release(e, rw::RingPage::Quick, rw::QuickPlaces, rw::CanvasCx, rw::CanvasCy - 40, t);
                Check(o.event == rw::PageEvent::Back && o.cell < 0, "Quick cast page: the center's upper half is Back, to the ring menu");
                const auto m = release(e, rw::RingPage::Quick, rw::QuickPlaces, rw::CanvasCx, rw::CanvasCy + 40, t);
                Check(m.event == rw::PageEvent::Move && m.cell < 0, "Quick cast page: the center's lower half is Move (no upstream cell)");
            }
            const float fx = (rw::CanvasPx - rw::FooterWidth(false, 5)) / 2 + rw::FooterWidth(false, 5) * .1f, fy = rw::CanvasPx * .93f;
            const auto o = release(e, rw::RingPage::Quick, rw::QuickPlaces, fx, fy, t);
            Check(o.event == rw::PageEvent::Upstream && o.cell == ia::FooterCell(0, ia::MenuPage::Spells), "Quick cast page: the footer is upstream's, on its Spells page");
            Check(rw::UpstreamPage(rw::RingPage::Quick) == ia::MenuPage::Spells && rw::UpstreamPage(rw::RingPage::QuickChoose) == ia::MenuPage::Spells &&
                  rw::QuickPage(rw::RingPage::Quick) && rw::QuickPage(rw::RingPage::QuickChoose) && !rw::QuickPage(rw::RingPage::Glyphs), "Quick cast's pages sit over upstream's Spells page");
            float gx, gy, hx, hy; rw::RingPos(0, rw::QuickPlaces, rw::QuickR, gx, gy); rw::RingPos(1, rw::QuickPlaces, rw::QuickR, hx, hy);
            Check(std::hypot(gx - hx, gy - hy) > 2 * rw::QuickHitR + 20 && rw::QuickR + rw::QuickIconPx * .6f < rw::ContentR, "Quick cast page: the places' hit circles never touch, inside the wheel");
            // its view: place k holds the radial's item k, the rest empty
            rw::PageView v; const int ids[3] = {54, (2 << 28) | 1, 36};
            rw::FillQuick(v, ids, 3, [](int id) { return id == 54 ? std::string("Teleport") : std::string("Other"); }, [](int id) { return id == 54 ? 7 : -1; });
            Check(v.page == rw::RingPage::Quick && v.count == 12 && v.has[0] && v.has[2] && !v.has[3] && !v.has[11] && v.skill[1] == ((2 << 28) | 1) && v.skill[3] == -1 &&
                  v.cell[0] == 7 && v.cell[1] == -1 && v.name[0] == "Teleport" && v.name[5] == "Place 6" && v.below == "3 of 12 filled" && v.top == "Back" && v.title == "Quick cast",
                  "Quick cast page view: its places, names and count");
            Check(v.help[0] == "Trigger to change it" && v.help[4] == "Trigger to fill it" && v.line[3] == "Empty: the next place", "Quick cast page view: its help lines");
            rw::PageView none; rw::FillQuick(none, nullptr, 5, [](int) { return std::string(); }, [](int) { return -1; });
            Check(!none.has[0] && none.count == 12, "Quick cast page view: no ids: all empty, never read");
            Check(v.best[1] && !v.best[0] && !v.best[2] && !v.best[3], "Quick cast page view: a kind's place wears Best, a skill's or an exact one's never");
            {   // the player's headset 2026-11-17: Antidote, Thawing and Stamina have no better version, so no Best pill (only kinds with tiers)
                const int fam[6] = {(2 << 28) | 1, (2 << 28) | 2, (2 << 28) | 3, (2 << 28) | 4, (2 << 28) | 5, (2 << 28) | 6};   // Healing, Mana, Rejuvenation, Antidote, Thawing, Stamina
                rw::PageView fv2; rw::FillQuick(fv2, fam, 6, [](int) { return std::string("x"); }, [](int) { return -1; });
                Check(fv2.best[0] && fv2.best[1] && fv2.best[2] && !fv2.best[3] && !fv2.best[4] && !fv2.best[5] && rw::BestSeat(fam[2]) && !rw::BestSeat(fam[5]) &&
                      !rw::BestSeat(54) && !rw::BestSeat((1 << 28) | 0x737672),
                      "Quick cast page view: Healing, Mana and Rejuvenation wear Best; Antidote, Thawing and Stamina never");
                rw::PageView kv2; const int one[1] = {fam[3]}; rw::FillQuickKind(kv2, one, 1, 0, "x", [](int) { return std::string("x"); }, [](int) { return -1; });
                Check(!kv2.best[0] && kv2.line[0] != "The best one you carry, when cast", "Quick cast kind page view: a kind with no tiers has no best line");
            }
            // reviews S3 / S4: before the character's ring is read the page shows no places as fact; kept for this game, it says so
            rw::PageView r = v; rw::MarkQuick(r, rw::QuickRingState::Reading);
            Check(!r.has[0] && !r.has[2] && r.skill[0] == -1 && r.cell[0] == -1 && !r.best[1] && r.below == "Reading its ring" && r.line[0] == "Reading this character's ring" && r.count == 12,
                  "Quick cast page view: Reading: every place empty, the center says so");
            rw::PageView g = v; rw::MarkQuick(g, rw::QuickRingState::ThisGame); Check(g.has[0] && g.below == "This game only", "Quick cast page view: kept for this game: its places, the center says so");
            rw::PageView sv = v; rw::MarkQuick(sv, rw::QuickRingState::Saved); Check(sv == v, "Quick cast page view: saved: as filled");
            // Quick cast layout Fixed places (2026-11-17): the places named as a clock's hours, gaps kept, an empty place fills exactly there
            const int fixedIds[10] = {-1, -1, -1, 54, -1, -1, -1, -1, -1, (2 << 28) | 1};
            rw::PageView fv; rw::FillQuick(fv, fixedIds, 10, [](int id) { return id == 54 ? std::string("Teleport") : std::string("Other"); }, [](int) { return -1; }, true);
            Check(fv.clock && fv.has[3] && fv.has[9] && !fv.has[0] && !fv.has[4] && !fv.has[10] && fv.skill[0] == -1 && fv.skill[3] == 54 && fv.below == "2 of 12 filled" &&
                  fv.name[0] == "Place 12" && fv.name[4] == "Place 4" && fv.line[3] == "Place 3 (3 o'clock)" && fv.line[5] == "Empty: fills exactly here" && fv.help[5] == "Trigger to fill it",
                  "Quick cast page view, Fixed places: clock names, the gaps kept, the count");
            Check(!v.clock && v.moveFrom == -1, "Quick cast page view, Growing: numbered from 1 as before, no Move");
            // Move: place 3's item waiting for its target; the places say what a trigger there does
            rw::PageView mv = fv; rw::MarkQuickMove(mv, 3);
            Check(mv.moveFrom == 3 && mv.title == "Move place 3" && mv.below == "Trigger where it goes" && mv.help[3] == "Trigger to leave it here" && mv.help[9] == "Trigger to swap them" &&
                  mv.help[0] == "Trigger to move it here" && mv.line[0] == "Empty: it goes exactly here" && mv.line[9] == "Swaps with place 3", "Quick cast page view: Move from place 3");
            rw::PageView me = fv; rw::MarkQuickMove(me, 0); Check(me == fv, "Quick cast page view: Move from an empty place: nothing");
            rw::PageView mg = v; rw::MarkQuickMove(mg, 0); Check(mg.title == "Move place 1" && mg.line[5] == "Empty: it goes last", "Quick cast page view: Move, Growing: an empty place takes it last");
            Check(fv.bottom == "Move" && fv.bottomHelp == "Trigger, then two places" && mv.bottomHelp == "Trigger to stop", "Quick cast page view: Move on the center's lower half, Trigger to stop while moving");
            rw::PageView pk = fv; rw::MarkQuickMove(pk, -1, true);
            Check(pk.moveFrom == -1 && pk.title == "Move which place?" && pk.help[3] == "Trigger to move this one" && pk.help[0] == "Pick a filled place" && pk.bottomCount == "Picking a place",
                  "Quick cast page view: Move, picking the place to move");
            rw::PageView em; rw::FillQuick(em, nullptr, 0, [](int) { return std::string(); }, [](int) { return -1; }, true);
            Check(em.bottomHelp == "Fill a place first", "Quick cast page view: an empty ring: nothing to move");
            rw::PageView kd; rw::FillQuickKind(kd, ids, 3, 0, "x", [](int) { return std::string(); }, [](int) { return -1; }); Check(kd.bottom.empty(), "Quick cast kind page: no Move");
        }
        {   // Review 2026-11-16 (exact items, the player: "if they have normal rejuv and a full rejuv, and they select the normal rejuv, it should
            // not drink a full rejuv"): a kind's page: the kind, then each exact one carried, round Quick's ring; the whole center Back
            const int kind = (2 << 28) | 3, rvl = (1 << 28) | 0x6c7672, rvs = (1 << 28) | 0x737672, ids[3] = {kind, rvl, rvs};
            rw::PageEngine e; double t = 0; settle(e, rw::RingPage::QuickKind, 3, t);
            for (int i = 0; i < 3; ++i) {
                const auto [x, y] = at(i, 3, rw::QuickR);
                const auto o = release(e, rw::RingPage::QuickKind, 3, x, y + 20, t);
                Check(o.event == rw::PageEvent::ChooseSkill && o.shape == i && o.cell < 0, "Quick cast kind page: a release on item i picks it (no upstream cell)");
            }
            for (const float dy : {-40.f, 40.f}) Check(release(e, rw::RingPage::QuickKind, 3, rw::CanvasCx, rw::CanvasCy + dy, t).event == rw::PageEvent::Back, "Quick cast kind page: the whole center is Back (the chooser)");
            Check(rw::QuickPage(rw::RingPage::QuickKind) && rw::QuickRing(rw::RingPage::QuickKind) && rw::QuickRing(rw::RingPage::Quick) && !rw::QuickRing(rw::RingPage::QuickChoose) &&
                  rw::UpstreamPage(rw::RingPage::QuickKind) == ia::MenuPage::Spells, "Quick cast kind page: a Quick cast page over upstream's Spells, drawn as Quick's ring");
            rw::PageView v;
            rw::FillQuickKind(v, ids, 3, 4, "Rejuvenation", [&](int id) { return id == kind ? std::string("Rejuvenation") : id == rvl ? std::string("Full Rejuvenation Potion") : std::string("Rejuvenation Potion"); },
                              [&](int id) { return id == rvl ? 9 : 8; });
            Check(v.page == rw::RingPage::QuickKind && v.count == 3 && v.has[2] && v.best[0] && !v.best[1] && !v.best[2] && v.skill[1] == rvl && v.cell[1] == 9 && v.cell[0] == 8 &&
                  v.name[2] == "Rejuvenation Potion" && v.title == "Rejuvenation" && v.top == "Back" && v.below == "For place 5" && v.help[1] == "Trigger puts it in place 5" &&
                  v.line[0] == "The best one you carry, when cast" && v.line[2] == "Exactly this one, never a stronger one", "Quick cast kind page view: the kind first (Best), the exact ones, their words");
            rw::PageView none; rw::FillQuickKind(none, nullptr, 3, 0, "x", [](int) { return std::string(); }, [](int) { return -1; });
            Check(none.count == 0, "Quick cast kind page view: no ids: nothing, never read");
            {   // Potion choice (2026-11-17): Mana with nothing carried: Best and 5 tiers, all tiers dim but choosable
                const auto mp = [](char d) { return (1 << 28) | int('m') | int('p') << 8 | int(d) << 16; };   // an exact Mana tier: mp1 .. mp5
                const int mids[6] = {(2 << 28) | 2, mp('5'), mp('4'), mp('3'), mp('2'), mp('1')};
                rw::PageView mv; rw::FillQuickKind(mv, mids, 6, 2, "Mana", [](int) { return std::string("x"); }, [](int) { return 5; }, [](int i) { return i > 0; });
                Check(mv.count == 6 && mv.best[0] && !mv.dim[0] && mv.dim[1] && mv.dim[5] && mv.has[5] && mv.skill[1] == mp('5') && mv.skill[5] == mp('1'),
                      "Quick cast kind page view: Mana, none carried: Best then Super..Minor, every tier dim yet present");
                rw::Scene ms; ms.open = true; static rw::Instance mlist[rw::MaxDraw];
                const int mn = rw::BuildPage(ms, mv, mlist);
                Check(mn > 6 * 2 && mn <= rw::MaxDraw, "Quick cast kind page: six items with dim tiers build within the budget");
            }
            rw::Scene s; s.open = true;
            static rw::Instance list[rw::MaxDraw];
            const int n = rw::BuildPage(s, v, list);
            Check(n > 3 * 2 && n <= rw::MaxDraw, "Quick cast kind page builds within its budget");
            Check(rw::KindSeat(kind) && !rw::KindSeat(rvl) && !rw::KindSeat(54) && rw::ItemSeat(rvl), "a kind seat is a family id only");
        }
        {
            rw::PageEngine e; double t = 0; settle(e, rw::RingPage::Gestures, 14, t);
            auto o = release(e, rw::RingPage::Gestures, 14, rw::CanvasCx, rw::CanvasCy + 40, t);
            Check(o.event == rw::PageEvent::Upstream && o.cell == rw::SpellsTabCell, "R7 Gestures: the whole center is Back (upstream's SPELLS tab)");
            for (int i = 0; i < 14; ++i) { const auto [x, y] = at(i, 14, rw::GestureR); o = release(e, rw::RingPage::Gestures, 14, x, y, t); Check(o.cell == ia::MotionCardCell(i), "R7 Gestures: 14 cards, each its own cell"); }
        }
        {   // P10 / P11: the center grows only after 150 ms in it; an item's lines after 100 ms; 0 = at once
            rw::PageEngine e; rw::PageIn in; in.page = rw::RingPage::Glyphs; in.count = 8; in.hit = true; in.x = rw::CanvasCx; in.y = rw::CanvasCy - 40;
            in.nowMs = 0; e.Step(in, set);
            in.nowMs = 149; Check(!e.Step(in, set).grown, "R7 #7 149 ms in the center: not grown");
            in.nowMs = 150; Check(e.Step(in, set).grown, "R7 #7 150 ms: grown");
            const auto [x, y] = at(5, 8, rw::GlyphR); in.x = x; in.y = y;
            in.nowMs = 200; auto o = e.Step(in, set); Check(o.blurb < 0 && !o.grown && o.under.index == 5, "R7 #7 an item pointed: lit at once, no lines yet");
            in.nowMs = 299; Check(e.Step(in, set).blurb < 0, "R7 #7 99 ms: no lines");
            in.nowMs = 300; o = e.Step(in, set); Check(o.blurb == 5 && o.grown, "R7 #7 100 ms: its lines, the center grown");
            rw::Settings zero; zero.growMs = 0; zero.blurbMs = 0;
            rw::PageEngine z; in.nowMs = 0; in.x = rw::CanvasCx; in.y = rw::CanvasCy - 40; Check(z.Step(in, zero).grown, "R7 #7 P10 at 0: at once");
            // A sweep through the center (100 ms) never grows it.
            rw::PageEngine w; in.nowMs = 0; w.Step(in, set); in.nowMs = 100; w.Step(in, set);
            in.x = rw::CanvasCx + 200; in.nowMs = 110; Check(!w.Step(in, set).grown, "R7 #7 a 100 ms sweep through the center: never grown");
        }
        // The main page with the Engine: P10 for the halves (it already grows after growMs), a row value reaches it.
        {
            const int per[3] = {10, 10, 10};
            const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
            rw::Engine e; e.Reset(l); rw::Settings s2; s2.growMs = 300;
            e.Step(At(0, rw::C, rw::C - 40), s2);
            Check(!e.Step(At(299, rw::C, rw::C - 40), s2).centerGrown && e.Step(At(300, rw::C, rw::C - 40), s2).centerGrown, "R7 #7 Center grow delay 300 reaches the main page's halves");
        }
        // #19: BindGlyph's index is upstream's GlyphIndex for both hands and all shapes; Clear sends -1; the high bit.
        for (int hand = 0; hand < 2; ++hand) for (int shape = 0; shape < 8; ++shape) {
            const auto r = rw::GlyphRequest(hand, shape, 54, -1, 7, 10, 1000);
            Check(r.action == ia::Action::BindGlyph && r.index == ia::GlyphIndex(hand, shape) && r.skill == 54 && r.skillOwner == -1 && (r.sequence >> 63) == 1, "R7 #19 BindGlyph index == GlyphIndex(hand, shape)");
            const auto c = rw::GlyphRequest(hand, shape, -1, 5, 8, 10, 1000);
            Check(c.skill == -1 && c.skillOwner == -1 && c.index == ia::GlyphIndex(hand, shape), "R7 #19 Clear glyph sends skill -1");
        }
        Check(rw::GlyphRequest(0, 8, 54, -1, 1, 1, 1).action == ia::Action::None && rw::GlyphRequest(0, -1, 54, -1, 1, 1, 1).action == ia::Action::None, "R7 no shape out of range");
        // #20: the glyph list equals upstream's picker (interaction::Engine opened on a shape, every page read) for all 8 classes.
        for (int cls = 0; cls < 8; ++cls) {
            ia::Engine e; ia::Input in; ia::Output out;
            in.active = true; in.tracked[0] = in.tracked[1] = true; in.session = 10; in.epoch = 1; in.head.p = {0, 1.7f, 0};
            in.hand[0].p = {.25f, 1.3f, -.4f}; in.hand[1].p = {-.25f, 1.3f, -.4f}; in.stage.known = true;
            in.snapshot.session = 10; in.snapshot.skillClass = cls;
            const auto& k = sc::Classes[size_t(cls)];
            int n = 0;
            for (int i = 0; i < 30; ++i) { auto& x = in.snapshot.catalog[size_t(n++)]; x = {}; x.id = k.first + i; x.known = x.available = true; x.directed = i % 3 == 0; }
            auto add = [&](int id, int owner, bool directed) { auto& x = in.snapshot.catalog[size_t(n++)]; x = {}; x.id = id; x.owner = owner; x.known = x.available = true; x.directed = directed; };
            add(357, -1, false); add(359, -1, true); add(220, 1000, false); add(0, -1, true); add(217, -1, false);
            in.snapshot.skillCount = unsigned(n);
            auto stepIn = [&] { in.time += .02; in.at = 1000000 + uint64_t(in.time * 1000); in.snapshot.at = in.at; out = e.Step(in); return out; };
            auto click = [&](int hit) { in.trigger = 0; stepIn(); in.hover = hit; in.trigger = 1; return stepIn(); };
            for (int i = 0; i < 3; ++i) stepIn();
            in.wheel = true; in.wheelHand = 1; stepIn();
            click(ia::GlyphsCell); click(2);
            std::vector<int> upstream;
            for (int page = 0; page < 10 && out.editor == 2; ++page) {
                for (int i = 0; i < out.editCount; ++i) upstream.push_back(out.editOptions[size_t(i)].id);
                if (page + 1 >= out.editPages) break;
                click(10);
            }
            std::vector<int> ours;
            for (const auto& choice : rw::GlyphChoices(in.snapshot)) ours.push_back(choice.id);   // the ring's one glyph list: what the chooser draws from
            std::sort(upstream.begin(), upstream.end()); std::sort(ours.begin(), ours.end());
            Check(!upstream.empty() && upstream == ours, ("R7 #20 the glyph list == upstream's picker (Engine::OpenGlyphSkills), class " + std::to_string(cls)).c_str());
        }
        {   // The Commander orders' row on the glyph chooser (2026-11-14, upstream v0.3.35 #439; the player's picks; the designer's sheet B).
            namespace od = rw::orders;
            namespace cp = framevr::companions;
            // A hero: the class's 30 skills (its army summons left out when asked), the shelf's five, a live army or not.
            auto hero = [&](int cls, bool summonSkills, bool liveArmy) {
                ia::Snapshot h; h.session = 10; h.skillClass = cls;
                int n = 0;
                const auto& k = sc::Classes[size_t(cls)];
                for (int i = 0; i < 30; ++i) {
                    const int id = k.first + i, type = cp::SkillType(id);
                    if (!summonSkills && type > 0 && (cp::Bit(type) & cp::ArmyTypeMask)) continue;
                    auto& x = h.catalog[size_t(n++)]; x = {}; x.id = id; x.known = x.available = true; x.directed = i % 3 == 0;
                }
                auto add = [&](int id, int owner, bool directed) { auto& x = h.catalog[size_t(n++)]; x = {}; x.id = id; x.owner = owner; x.known = x.available = true; x.directed = directed; };
                add(0, -1, true); add(357, -1, false); add(359, -1, true); add(217, -1, false); add(220, 1000, false);
                h.skillCount = unsigned(n);
                if (liveArmy) h.companions.relevant = cp::ArmyTypeMask;
                return h;
            };
            auto mainOf = [&](const ia::Snapshot& h, rw::TreeLayout mode) { std::array<rw::SeatIn, rw::MaxSeats> in{}; int n = 0; rw::SeatsFrom(h, in, n); return rw::Build(in.data(), n, mode); };
            auto ordersIn = [&](const rw::Layout& l) { std::vector<int> ids; for (int i = 0; i < l.count; ++i) if (rw::OrderSeat(l, i)) ids.push_back(l.seats[size_t(i)].id); return ids; };
            const std::vector<int> all7{3901, 3902, 3903, 3904, 3905, 3906, 3907};
            {   // 2026-11-16: Quick cast's chooser: every tree seat where the main ring has it, the shelf skills Quick cast casts, then its
                // eight potions and scrolls (ring Quick cast radial's family ids) on the row; the list never a ring entry or an order
                const std::vector<int> fams{(2 << 28) | 1, (2 << 28) | 2, (2 << 28) | 3, (2 << 28) | 6, (2 << 28) | 4, (2 << 28) | 5, (2 << 28) | 16, (2 << 28) | 17};
                int ninthCost = 0;   // review F8 (2026-11-17): classes whose chooser would lose a shelf skill to a ninth row seat
                const auto shelfKept = [](const rw::Layout& l) { int k = 0; for (int i = 0; i < l.count; ++i) k += l.seats[size_t(i)].tree < 0 && !rw::ItemSeat(l.seats[size_t(i)].id) && !rw::OrderSeat(l, i); return k; };
                for (int cls = 0; cls < 7; ++cls) for (int live = 0; live < 2; ++live) {
                    const auto h = hero(cls, true, live != 0);
                    const auto main = mainOf(h, rw::TreeLayout::Shown);
                    const auto list = rw::QuickChoices(h);
                    const auto cl = rw::ChooserRow(main, list, fams);
                    { std::vector<int> nine = fams; nine.push_back(3 << 28); ninthCost += shelfKept(rw::ChooserRow(main, list, nine)) < shelfKept(cl); }
                    const std::string who = "Quick cast chooser, class " + std::to_string(cls) + (live ? ", a live army" : "");
                    bool trees = true; int t2 = 0;
                    for (int i = 0; i < main.count; ++i) if (main.seats[size_t(i)].tree >= 0) {
                        const auto& a = main.seats[size_t(i)]; const auto& b = cl.seats[size_t(t2++)];
                        trees = trees && a.id == b.id && a.x == b.x && a.y == b.y;
                    }
                    Check(trees, "every tree seat in its place: " + who);
                    std::vector<int> row;
                    for (int i = 0; i < cl.count; ++i) if (rw::ItemSeat(cl.seats[size_t(i)].id)) { row.push_back(cl.seats[size_t(i)].id); Check(cl.seats[size_t(i)].y == rw::ShelfY && cl.seats[size_t(i)].tree < 0, "a potion on the shelf's row: " + who); }
                    Check(row == fams && ordersIn(cl).empty(), "the eight potions and scrolls in order, no order seat: " + who);
                    bool clean = !list.empty();
                    for (const auto& k : list) clean = clean && k.id >= 0 && k.id < 4096 && !rw::orders::Is(k.id) && k.known;
                    Check(clean && rw::ChoiceOf(list, 220, 1000) != nullptr && rw::ChoiceOf(list, 0, -1) != nullptr, "the list: known skills under 4096 (an item skill, a directed one too), never an order: " + who);
                }
                Check(!rw::ItemSeat(54) && !rw::ItemSeat(9003) && !rw::ItemSeat(9101) && rw::ItemSeat((1 << 28) | 0x337068) && rw::ItemSeat((2 << 28) | 1), "an item seat is Quick cast's item id only");
                // review F8: a ninth seat (Move, first built there) pushes a shelf skill off the chooser for every class here, so Move sits
                // on the Quick cast page's center instead and the row keeps its eight
                std::printf("Quick cast chooser: a ninth row seat would drop a shelf skill on %d of 14 class pages\n", ninthCost);
                Check(ninthCost == 14, "review F8: a ninth row seat costs a shelf skill on every class page (so Move is no seat)");
                // ChooserLayout is ChooserRow with the list's orders (the glyph chooser unchanged)
                const auto h = hero(2, true, true); const auto main = mainOf(h, rw::TreeLayout::Shown); const auto list = rw::GlyphChoices(h);
                std::vector<int> ord; for (const auto& k : list) if (rw::orders::Is(k.id) && k.owner == -1) ord.push_back(k.id);
                const auto a = rw::ChooserLayout(main, list), b = rw::ChooserRow(main, list, ord);
                bool same = a.count == b.count && a.shelf == b.shelf;
                for (int i = 0; i < a.count && same; ++i) same = a.seats[size_t(i)].id == b.seats[size_t(i)].id && a.seats[size_t(i)].x == b.seats[size_t(i)].x && a.seats[size_t(i)].y == b.seats[size_t(i)].y;
                Check(same && !ord.empty(), "the glyph chooser: ChooserLayout == ChooserRow with its orders");
            }
            auto ly0 = [](float dy) { return dy - 9 >= rw::HitR && rw::ShelfY - dy - 9 > 0; };   // the name's box clears the row's hit circles
            int armies = 0;
            for (int mode = 0; mode < 2; ++mode) for (int cls = 0; cls < 8; ++cls) for (int live = 0; live < 2; ++live) for (int skills = 0; skills < 2; ++skills) {
                const auto h = hero(cls, skills != 0, live != 0);
                const bool army = ia::HasSummonArmy(h);   // upstream's own answer
                armies += army;
                const std::string who = "class " + std::to_string(cls) + (mode ? ", Tree orbs" : ", Trees shown") + (live ? ", a live army" : "") + (skills ? "" : ", no summon skill");
                const auto main = mainOf(h, mode ? rw::TreeLayout::Orbs : rw::TreeLayout::Shown);
                const auto list = rw::GlyphChoices(h);
                const auto cl = rw::ChooserLayout(main, list);
                Check(ordersIn(main).empty(), "orders row: never on the main ring: " + who);
                Check(ordersIn(cl) == (army ? all7 : std::vector<int>{}), "orders row: the chooser page shows 3901-3907 with a summon army and none without: " + who);
                if (!army) {   // today's chooser exactly
                    bool same = cl.count == main.count && cl.shelf == main.shelf;
                    for (int i = 0; i < main.count && same; ++i) same = cl.seats[size_t(i)].id == main.seats[size_t(i)].id && cl.seats[size_t(i)].owner == main.seats[size_t(i)].owner && cl.seats[size_t(i)].x == main.seats[size_t(i)].x && cl.seats[size_t(i)].y == main.seats[size_t(i)].y;
                    Check(same, "orders row: no army: the chooser's seats are the main ring's exactly: " + who);
                    continue;
                }
                // the trees stay where they are, seat for seat
                const int trees = main.perTree[0] + main.perTree[1] + main.perTree[2];
                bool treesSame = cl.perTree == main.perTree;
                for (int i = 0; i < trees && treesSame; ++i) treesSame = cl.seats[size_t(i)].id == main.seats[size_t(i)].id && cl.seats[size_t(i)].tree == main.seats[size_t(i)].tree && cl.seats[size_t(i)].x == main.seats[size_t(i)].x && cl.seats[size_t(i)].y == main.seats[size_t(i)].y;
                Check(treesSame, "orders row: every tree seat keeps its index and place: " + who);
                // the row: the shelf skills a glyph can take (Town Portal 359, Identify 217), a gap, the 7 orders; the others step out
                std::vector<int> row; for (int i = trees; i < cl.count; ++i) row.push_back(cl.seats[size_t(i)].id);
                Check(row == std::vector<int>({359, 217, 3901, 3902, 3903, 3904, 3905, 3906, 3907}) && cl.shelf == 9 && cl.count == trees + 9, "orders row: Town Portal, Identify, then the orders in upstream's order; Attack, 357 and the item skill step out: " + who);
                bool placed = true; float minTree = 1e9f;
                for (int i = trees; i < cl.count; ++i) {
                    const auto& seat = cl.seats[size_t(i)];
                    placed = placed && seat.y == rw::ShelfY && seat.tree == -1 && seat.x - rw::HitR >= 0 && seat.x + rw::HitR <= rw::WheelW && seat.y + rw::HitR < rw::WheelH;
                    if (i > trees) { const float step = seat.x - cl.seats[size_t(i - 1)].x; placed = placed && std::fabs(step - (i == trees + 2 ? rw::ShelfPitch + rw::ShelfGap : rw::ShelfPitch)) < .01f; }
                    for (int t = 0; t < trees; ++t) minTree = std::min(minTree, Dist(seat, cl.seats[size_t(t)]));
                    placed = placed && rw::FooterAt(seat.x + rw::CanvasX, seat.y + rw::HitR + rw::CanvasY, true, true, ia::MenuPage::Glyphs) < 0;   // its hit circle's lowest point is no footer tab
                }
                Check(placed && std::fabs((cl.seats[size_t(trees)].x + cl.seats[size_t(cl.count - 1)].x) / 2 - rw::C) < .01f,
                      "orders row: on the shelf line, 60 px apart with the shelf's gap before the orders, centered, inside the wheel, above the footer: " + who);
                Check(minTree >= rw::HitPx, "orders row: no order's hit circle meets a tree seat's (closest " + std::to_string(minTree) + " px): " + who);
                rw::Engine e; e.Reset(cl); rw::Settings set; set.hasMotions = true;
                bool hits = true;
                for (int i = trees; i < cl.count; ++i) { const auto t = e.HitTest(cl.seats[size_t(i)].x + rw::CanvasX, cl.seats[size_t(i)].y + rw::CanvasY, set); hits = hits && t.kind == rw::Kind::Seat && t.index == i; }
                Check(hits, "orders row: the laser on each row seat hits that seat: " + who);
                // every seat on the list is lit, every other dimmed: the list is the only source
                bool lit = true;
                for (int i = 0; i < cl.count; ++i) { const auto& seat = cl.seats[size_t(i)]; const auto* c = rw::ChoiceOf(list, seat.id, seat.owner); if (i >= trees) lit = lit && c != nullptr; if (rw::OrderSeat(cl, i)) lit = lit && c && !c->directed && std::string(c->name) == od::Label(seat.id); }
                Check(lit, "orders row: every row seat is on the ring's glyph list; an order there is upstream's own entry, no target: " + who);
                {   // the draw: an icon at each order's seat
                    static rw::Instance list2[rw::MaxDraw];
                    rw::Scene scn; scn.open = true; scn.layout = &cl; scn.chooseTitle = "Triangle"; scn.clearLine = "x"; scn.hasMotions = true;
                    const int n = rw::Build(scn, list2);
                    int drawn = 0;
                    for (int i = trees; i < cl.count; ++i) {
                        bool found = false;
                        for (int k2 = 0; k2 < n && !found; ++k2) found = list2[k2].recipe == rw::RecipeSeat && std::fabs(list2[k2].cx - (cl.seats[size_t(i)].x + rw::CanvasX)) < 8 && std::fabs(list2[k2].cy - (cl.seats[size_t(i)].y + rw::CanvasY)) < 8;
                        drawn += found;
                    }
                    Check(n > 0 && n <= rw::MaxDraw && drawn == 9, "orders row: the chooser page draws an icon at each of the row's 9 seats (" + std::to_string(drawn) + "): " + who);
                }
            }
            Check(armies > 16 && armies < 64, "orders row: the sweep had heroes with and without an army (" + std::to_string(armies) + " of 64)");
            {   // the widest row still fits: a shelf of 6 pickable skills and the 7 orders
                rw::Layout wide; std::vector<ia::Skill> list;
                for (int i = 0; i < 6; ++i) { auto& seat = wide.seats[size_t(wide.count++)]; seat.id = 400 + i; seat.owner = -1; seat.tree = -1; seat.y = rw::ShelfY; ia::Skill k; k.id = 400 + i; k.owner = -1; list.push_back(k); }
                for (int id = od::First; id <= od::Last; ++id) list.push_back(ia::MakeCommanderSkill(id, true));
                const auto cl = rw::ChooserLayout(wide, list);
                Check(cl.count == 12 && cl.seats[0].x - rw::HitR >= 0 && cl.seats[11].x + rw::HitR <= rw::WheelW && cl.seats[5].x - cl.seats[4].x >= rw::ShelfPitch && rw::OrderSeat(cl, 5) && rw::OrderSeat(cl, 11),
                      "orders row: 6 shelf skills a glyph can take: the gap narrows, the sixth steps out, all 7 orders stay, the row inside the wheel (" + std::to_string(cl.seats[0].x - rw::HitR) + " px to spare each side)");
                list.erase(list.begin() + 4, list.begin() + 6);
                const auto four = rw::ChooserLayout(wide, list);
                Check(four.count == 11 && std::fabs(four.seats[4].x - four.seats[3].x - rw::ShelfPitch - rw::ShelfGap) < .01f && four.seats[0].x - rw::HitR >= 0, "orders row: up to 4 shelf skills keep the full gap");
            }
            {   // the row's small name: drawn over the orders with an army, never without, clear of every tree seat
                namespace tx2 = rw::text;
                tx2::Metrics met; met.ok = true;
                for (auto& style : met.g) for (auto& gl : style) { gl.advance = 8; gl.w = 8; gl.h = 10; }
                int with = 0, without = 0; float closest = 1e9f;
                for (int mode = 0; mode < 2; ++mode) for (int cls = 0; cls < 8; ++cls) for (int army = 0; army < 2; ++army) {
                    const auto h = hero(cls, army != 0, false);
                    const auto cl = rw::ChooserLayout(mainOf(h, mode ? rw::TreeLayout::Orbs : rw::TreeLayout::Shown), rw::GlyphChoices(h));
                    static rw::Instance one[rw::MaxDraw], two[rw::MaxDraw];
                    rw::Scene scn; scn.open = true; scn.layout = &cl; scn.chooseTitle = "Triangle"; scn.clearLine = "x"; scn.hasMotions = true; scn.metrics = &met;
                    const int a = rw::Build(scn, one);
                    scn.chooseTitle = nullptr;
                    const int b = rw::Build(scn, two);   // the same seats off the chooser: no name
                    const bool has = !ordersIn(cl).empty();
                    float x0 = 1e9f, x1 = -1e9f;
                    for (int i = 0; i < cl.count; ++i) if (rw::OrderSeat(cl, i)) { x0 = std::min(x0, cl.seats[size_t(i)].x); x1 = std::max(x1, cl.seats[size_t(i)].x); }
                    const float lx = (x0 + x1) / 2, ly = rw::ShelfY - od::RowLabelDy;
                    int glyphs = 0, glyphsOff = 0;
                    auto near = [&](const rw::Instance& g) { return g.recipe == rw::RecipeGlyph && std::fabs(g.cx - (lx + rw::CanvasX)) < 24 && std::fabs(g.cy - (ly + rw::CanvasY)) < 14; };
                    if (has) { for (int i = 0; i < a; ++i) glyphs += near(one[i]); for (int i = 0; i < b; ++i) glyphsOff += near(two[i]); }
                    if (has) { ++with; Check(glyphs == 4 && glyphsOff == 0, "orders row: the 4 letters of ARMY over the orders on the chooser, none off it: class " + std::to_string(cls) + " (" + std::to_string(glyphs) + ", " + std::to_string(glyphsOff) + ")"); }
                    else { ++without; int any = 0; for (int i = 0; i < a; ++i) any += one[i].recipe == rw::RecipeGlyph && std::fabs(one[i].cy - (ly + rw::CanvasY)) < 14 && one[i].cx > rw::CanvasX + 300; Check(any == 0, "orders row: no army, no name on the shelf line: class " + std::to_string(cls)); }
                    if (has) for (int i = 0; i < cl.count; ++i) if (cl.seats[size_t(i)].tree >= 0) closest = std::min(closest, std::max(std::fabs(cl.seats[size_t(i)].x - lx) - 22, 0.f) + std::max(std::fabs(cl.seats[size_t(i)].y - ly) - 9, 0.f));
                }
                Check(with == 8 && without == 24, "orders row: the name sweep saw both (" + std::to_string(with) + ", " + std::to_string(without) + ")");
                Check(closest >= rw::HitR && ly0(od::RowLabelDy), "orders row: the name's box stays outside every tree seat's hit circle, both layouts (closest " + std::to_string(closest) + " px), over the row's hit circles and inside the wheel");
            }
            {   // Designer gate D1: a button held on an order marks that order (1.3 x, the 3 px hover ring) and names it in the center
                namespace tx2 = rw::text;
                tx2::Metrics met; met.ok = true;
                for (auto& style : met.g) for (auto& gl : style) { gl.advance = 8; gl.w = 8; gl.h = 10; }
                const auto h = hero(2, true, true);
                const auto cl = rw::ChooserLayout(mainOf(h, rw::TreeLayout::Shown), rw::GlyphChoices(h));
                int first = -1;
                for (int i = 0; i < cl.count && first < 0; ++i) if (rw::OrderSeat(cl, i)) first = i;
                const int held = first + 3, other = first + 1;
                const char* name = od::Short(cl.seats[size_t(held)].id);
                static rw::Instance list[rw::MaxDraw];
                rw::Scene rest; rest.open = true; rest.layout = &cl; rest.chooseTitle = "Triangle"; rest.clearLine = "x"; rest.hasMotions = true; rest.metrics = &met; rest.anim = rw::PresetAnim(rw::Preset::Off);
                // How a seat was drawn: 2 = 1.3 x with the 3 px hover ring, 1 = at rest with its own 2 px rim, 0 = neither.
                auto mark = [&](const rw::Scene& x, int seat) {
                    const int n = rw::Build(x, list); const auto& st = cl.seats[size_t(seat)];
                    for (int i = 0; i < n; ++i) {
                        const auto& g = list[i];
                        if (g.recipe != rw::RecipeSeat || std::fabs(g.cx - (st.x + rw::CanvasX)) > .5f || std::fabs(g.cy - (st.y + rw::CanvasY)) > .5f) continue;
                        const bool big = std::fabs(g.w - (rw::IconPx * 1.3f + 2 * rw::SeatRingPx)) < .01f, small = std::fabs(g.w - (rw::IconPx + 2 * rw::SeatRingPx)) < .01f;
                        const bool hover = g.r == rw::HoverRing.r && g.g == rw::HoverRing.g && g.b == rw::HoverRing.b;
                        return big && hover && std::fabs(g.param) == 3 ? 2 : small && !hover && std::fabs(g.param) == 2 ? 1 : 0;
                    }
                    return 0;
                };
                // The glyphs on the name's line, C - 52, inside the center.
                auto named = [&](const rw::Scene& x) {
                    const int n = rw::Build(x, list); int glyphs = 0;
                    for (int i = 0; i < n; ++i) glyphs += list[i].recipe == rw::RecipeGlyph && std::fabs(list[i].cy - (rw::CanvasCy + rw::HoldNameY)) < 12 && std::fabs(list[i].cx - rw::CanvasCx) < 104;
                    return glyphs;
                };
                const int letters = int(std::strlen(name));   // a space is a glyph too
                Check(first >= 0 && mark(rest, held) == 1 && mark(rest, other) == 1 && named(rest) == 0, "order hold: at rest every order has its own rim and the center names none");
                auto hold = rest; hold.holdFrac = .5f; hold.holdSeat = held; hold.holdSlot = "Right trigger"; hold.holdTop = "Replaces"; hold.holdStatus = "Bone Armor"; hold.holdName = name;
                hold.step.under = {rw::Kind::Seat, held};
                Check(mark(hold, held) == 2 && mark(hold, other) == 1, "order hold: the held order is drawn 1.3 x with the 3 px hover ring, its neighbor at rest");
                Check(rw::HoldNameY == -52 && named(hold) == letters && letters >= 6, "order hold: its short name on one line at C - 52 (" + std::to_string(named(hold)) + " of " + std::to_string(letters) + " characters)");
                { auto x = hold; x.step.under = {}; Check(mark(x, held) == 2 && named(x) == letters, "order hold: the ray drifts off the wheel: the held order stays marked and named"); }
                { auto x = hold; x.step.under = {rw::Kind::Seat, other}; Check(mark(x, held) == 2 && mark(x, other) == 1, "order hold: the ray drifts to another order: only the held one is marked"); }
                { auto x = hold; x.holdName = nullptr; x.step.under = {}; Check(mark(x, held) == 1 && named(x) == 0, "order hold: no name given (the main ring's hold): only the ray marks a seat, as before"); }
                { auto x = hold; x.holdName = nullptr; Check(mark(x, held) == 2, "order hold: no name given, the ray on the seat: marked by the ray, as before"); }
                { auto x = hold; x.holdFrac = -1; x.step.under = {}; Check(mark(x, held) == 1 && named(x) == 0, "order hold: a name with no hold running marks and names nothing"); }
                { auto x = hold, y = hold; y.holdName = od::Short(cl.seats[size_t(other)].id); Check(!(rw::KeyOf(x) == rw::KeyOf(y)), "order hold: the name is in the draw key"); }
                bool distinct = true;
                for (int a = od::First; a <= od::Last; ++a) for (int b = a + 1; b <= od::Last; ++b) distinct = distinct && std::string(od::Short(a)) != od::Short(b) && *od::Short(a);
                Check(distinct, "order hold: the 7 short names differ, so the name tells the seven same pictures apart");
            }
            {   // Designer gate D2: an order's help line is upstream's own line, cut at run time by the designer's rule
                Check(od::HelpCut("Commands your army into a wall ahead. Warriors hold the line, mages fire from behind.") == "Commands your army into a wall ahead.", "orders help: two sentences: up to the first full stop");
                Check(od::HelpCut("Orders your army to hold position at the targeted location, guarding doorways, choke points, or chests without following you.") == "Orders your army to hold position at the targeted location.",
                      "orders help: one sentence: up to its first comma, closed with a full stop (Hold Ground)");
                Check(od::HelpCut("Recalls your entire army back to your side immediately, breaking any hold order and reforming around you.") == "Recalls your entire army back to your side immediately.", "orders help: one sentence (Regroup)");
                Check(od::HelpCut("One plain sentence.") == "One plain sentence." && od::HelpCut("First, with a comma. Second one.") == "First, with a comma." && od::HelpCut("No stop, at all") == "No stop, at all" && od::HelpCut("").empty(),
                      "orders help: one sentence with no comma whole; a comma inside the first of two sentences stays; no full stop: the line as it is");
                bool cut = true, slots = true;
                for (int id = od::First; id <= od::Last; ++id) {
                    const std::string line = framevr::skillusage::Line(id, 0), help = od::Help(id);
                    const size_t n = help.size();
                    cut = cut && n > 10 && n < line.size() && help.back() == '.' && help.find('.') == n - 1 && line.compare(0, n - 1, help, 0, n - 1) == 0 && (line[n - 1] == '.' || line[n - 1] == ',');
                    for (int slot = 0; slot < framevr::skills::Count; ++slot) slots = slots && line == framevr::skillusage::Line(id, slot);
                }
                Check(cut && od::Help(0).empty() && od::Help(od::Last + 1).empty(), "orders help: each of the 7 is upstream's line (skill_usage.h) up to its first full stop or comma; no order, no line");
                Check(slots, "orders help: upstream's line for an order is the same from every slot");
            }
            {   // Designer gate D3: a blurb shown whole keeps every line as written; the short title when the long one does not fit
                namespace tx2 = rw::text;
                tx2::Metrics met; met.ok = true;
                for (auto& style : met.g) for (auto& gl : style) { gl.advance = 8; gl.w = 8; gl.h = 10; }
                auto of = [](const tx2::Blurb& b, tx2::Kind k) { std::string t; for (const auto& l : b.lines) if (l.kind == k) t += (t.empty() ? "" : " ") + l.text; return t; };
                const std::string essay = "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen";
                tx2::BlurbText a{"Name", "Sub line", essay, "Action"};
                const auto cutA = tx2::LayBlurb(met, a); a.whole = true; const auto wholeA = tx2::LayBlurb(met, a);
                Check(of(cutA, tx2::KDesc) != essay && of(wholeA, tx2::KDesc) == essay && wholeA.over.empty() && int(wholeA.lines.size()) <= wholeA.maxLines && wholeA.maxLines == 8,
                      "whole blurb: a description over two lines is shortened for a skill, kept word for word when whole");
                tx2::BlurbText b{"Abcdefghijkl Abcdefghijklm Abcd", "Sub line", "short", "Action"};
                const auto dropB = tx2::LayBlurb(met, b); b.whole = true; const auto keptB = tx2::LayBlurb(met, b);
                Check(of(dropB, tx2::KSub).empty() && of(keptB, tx2::KSub) == "Sub line" && keptB.over.empty() && of(keptB, tx2::KName) == b.name, "whole blurb: a two-line name drops the line under it for a skill, keeps it when whole");
                const tx2::BlurbText longTitle{"Abcdefghijklmnop Abcdefghijklmnop Abcdefghijklmnop", "Sub line", "short", "Action"};
                const auto picked = tx2::WholeBlurb(met, longTitle, "Short");
                Check(picked.name == "Short" && picked.whole && picked.desc == "short" && picked.sub == "Sub line" && picked.help == "Action", "whole blurb: a title that does not fit gives way to the short one; every other line as given");
                Check(tx2::WholeBlurb(met, {"Fits", "Sub line", "short", "Action"}, "Short").name == "Fits", "whole blurb: a title that fits stays");
                Check(std::string(od::ChooserAction()) == "Trigger picks, hold assigns" && std::string(od::Sub()) == "Squad order, no mana", "orders blurb: the designer's action line (only the trigger picks) and the line under the name");
                {   // the fewest lines: a whole blurb takes each line's own chord, so text the careful wrap spreads over more lines closes up
                    int closer = 0, same = 0, wider = 0;
                    for (int words = 4; words <= 30; ++words) {
                        std::string d; for (int i = 0; i < words; ++i) d += (d.empty() ? "" : " ") + std::string(size_t(3 + i % 5), 'a');
                        tx2::BlurbText x{"Name", "Sub line", d, "Action line here"};
                        const auto plain = tx2::LayBlurb(met, x); x.whole = true; const auto whole = tx2::LayBlurb(met, x);
                        if (!whole.over.empty()) {   // too long for the circle: reported, not shortened; never where the careful wrap holds the same words
                            Check(of(plain, tx2::KDesc) != d || !plain.over.empty(), "whole blurb: words the careful wrap fits are never over the circle when whole (" + std::to_string(words) + " words: " + whole.over[0] + ")");
                            continue;
                        }
                        closer += whole.lines.size() < plain.lines.size(); same += whole.lines.size() == plain.lines.size(); wider += whole.lines.size() > plain.lines.size() && of(plain, tx2::KDesc) == d;
                        bool inside = true;
                        for (size_t k = 0; k < whole.lines.size(); ++k) inside = inside && tx2::Width(met, whole.lines[k].style, whole.lines[k].text) + 5 <= tx2::Chord(int(k), int(whole.lines.size())) + tx2::OverSlackPx;
                        Check(inside && of(whole, tx2::KDesc) == d, "whole blurb: every line inside its chord, the description word for word (" + std::to_string(words) + " words)");
                    }
                    Check(closer > 0 && wider == 0, "whole blurb: never more lines than the careful wrap for the same words, fewer where its spare room cost a line (" + std::to_string(closer) + " closer, " + std::to_string(same) + " the same)");
                }
                {   // the last step: the short title does not fit either: the line under the name goes, never the description
                    // six 20-letter words (160 px here): one to a line, so the description is 6 lines of the circle's 8
                    const std::string d7 = "aaaaaaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbb cccccccccccccccccccc dddddddddddddddddddd eeeeeeeeeeeeeeeeeeee ffffffffffffffffffff";
                    const auto bare = tx2::WholeBlurb(met, {"Long title here", "Sub line", d7, "Action"}, "Short");
                    tx2::BlurbText with{"Short", "Sub line", d7, "Action"}; with.whole = true;
                    Check(!tx2::LayBlurb(met, with).over.empty() && bare.name == "Short" && bare.sub.empty() && bare.desc == d7 && bare.help == "Action" && tx2::LayBlurb(met, bare).over.empty(),
                          "whole blurb: when the short title does not fit either, the line under the name goes; the description and the last line stay whole");
                }
                tx2::BlurbText over{"Name", "Sub line", essay + " " + essay + " " + essay, "Action"}; over.whole = true;
                const auto overB = tx2::LayBlurb(met, over);
                Check(!overB.over.empty() && of(overB, tx2::KDesc) == over.desc, "whole blurb: what cannot fit is reported, never shortened");
            }
            {   // Review M1: the army watch. The answer is asked for on a catalog change or a flip of the live bit only, and
                // only a change of the answer is reported; the seats' key (the main ring's reset) never sees any of it.
                {   // a Necromancer with summon skills: the companions copy going stale and coming back changes nothing
                    rw::ArmyWatch w; auto fresh = hero(2, true, true), stale = fresh; stale.companions.relevant = 0;
                    const auto first = w.Step(fresh, 77);
                    Check(first.army && first.changed && first.icons && first.iconsNew && w.Asked() == 1, "M1 watch: the first frame with an army: the row and its icons, asked once");
                    int changes = 0, iconsNew = 0;
                    for (int i = 0; i < 300; ++i) { const auto o = w.Step(i / 50 % 2 ? stale : fresh, 77); changes += o.changed; iconsNew += o.iconsNew; if (!o.army || !o.icons) ++changes; }
                    Check(changes == 0 && iconsNew == 0, "M1 watch: a stale companions copy (5 drops in 300 frames) on a hero with summon skills changes nothing (" + std::to_string(changes) + ")");
                    Check(w.Asked() == 1 + 5, "M1 watch: upstream is asked once per flip of the live bit, never per frame (" + std::to_string(w.Asked()) + ")");
                    w.Step(fresh, 77);   // back from the last drop
                    const int before = w.Asked();
                    for (int i = 0; i < 1000; ++i) w.Step(fresh, 77);
                    Check(w.Asked() == before, "M1 watch: 1000 steady frames ask nothing");
                    w.Step(fresh, 78);
                    Check(w.Asked() == before + 1, "M1 watch: a catalog change asks once");
                }
                {   // a hero whose only army is live (no summon skill): the row turns on and off with it; the icons stay once asked for
                    rw::ArmyWatch w; auto on = hero(1, true, true), off = on; off.companions.relevant = 0;
                    const auto none = w.Step(off, 5);
                    Check(!none.army && !none.changed && !none.icons && !none.iconsNew, "M1 watch: no army: nothing");
                    const auto a = w.Step(on, 5);
                    Check(a.army && a.changed && a.icons && a.iconsNew, "M1 watch: a live army appears: the row on, its icons asked for");
                    const auto b = w.Step(off, 5);
                    Check(!b.army && b.changed && b.icons && !b.iconsNew, "M1 watch: it goes: the row off, the icons kept");
                    const auto c = w.Step(on, 5);
                    Check(c.army && c.changed && c.icons && !c.iconsNew, "M1 watch: it comes back: the row on, no new icon request");
                    const auto other = w.Step(hero(3, true, false), 6);
                    Check(!other.army && other.changed && !other.icons, "M1 watch: another class: the icons start over");
                    w.Reset();
                    Check(w.Asked() == 0 && !w.Step(off, 5).icons, "M1 watch: Reset forgets");
                }
                for (int cls = 0; cls < 8; ++cls) for (int live = 0; live < 2; ++live) for (int skills = 0; skills < 2; ++skills) {
                    rw::ArmyWatch w; const auto h = hero(cls, skills != 0, live != 0);
                    Check(w.Step(h, 1).army == ia::HasSummonArmy(h) && w.Step(h, 1).army == ia::HasSummonArmy(h), "M1 watch: its answer is upstream's HasSummonArmy");
                }
            }
            {   // the one header: upstream's names, the short form, the icon ids the atlas is asked for
                bool names = true;
                for (int id = od::First; id <= od::Last; ++id) names = names && std::string(od::Label(id)) == framevr::commander::CommanderSkillName(id) && std::string("Army: ") + od::Short(id) == od::Label(id) && od::Initials(id)[0] && od::Help(id)[0];
                Check(names && od::Count == 7 && od::Is(3901) && od::Is(3907) && !od::Is(3900) && !od::Is(3908) && !od::Is(69), "orders header: upstream's 7 names, each short form without the prefix, letters and a help line for each");
                int ids[40] = {5, 6, od::IconSkill(3903)};
                int n = rw::AddOrderIcons(ids, 3, 40);
                bool each = n == 9;
                for (int id = od::First; id <= od::Last; ++id) each = each && std::count(ids, ids + n, od::IconSkill(id)) == 1;
                Check(each && rw::AddOrderIcons(ids, n, 40) == n && rw::AddOrderIcons(ids, 3, 5) == 5, "orders header: each order's icon is asked for once, within the request's room");
            }
            {   // A button on an order (the player's pick B): upstream's real nothing here, the ring's real Engine and QuickCast, 5 ms frames.
                const auto h = hero(2, true, false);
                const auto cl = rw::ChooserLayout(mainOf(h, rw::TreeLayout::Shown), rw::GlyphChoices(h));
                int order = -1, skill = -1;
                for (int i = 0; i < cl.count; ++i) { if (cl.seats[size_t(i)].id == 3904) order = i; if (skill < 0 && cl.seats[size_t(i)].tree >= 0) skill = i; }
                struct Got { bool enginePick = false, pick = false, assign = false, noChange = false, other = false; float first = -1, last = 0, top = 0; int slot = -1, hand = -1, seat = -1; double assignAt = -1; };
                const int pointer = 0;
                auto run = [&](int seat, int hand, int slot, double heldMs, int holdMs = 1000) {
                    rw::Engine eng; eng.Reset(cl); rw::QuickCast q; rw::Settings set; set.hasMotions = true;
                    Got g; bool was = false;
                    for (double t = 0; t <= 2600; t += 5) {
                        const bool down = t >= 400 && t < 400 + heldMs;
                        const auto st = eng.Step(At(t, cl.seats[size_t(seat)].x, cl.seats[size_t(seat)].y, down && slot == 0 && hand == pointer), set);
                        rw::QuickIn qi; qi.nowMs = t; qi.main = true; qi.holdMs = holdMs; qi.under = rw::OrderUnder(cl, st.under);
                        for (auto& b : qi.bound) b.fill(-1);
                        if (down && !was) qi.press[hand] = slot;
                        qi.down[hand][slot] = down; was = down;
                        const auto e = q.Step(qi, cl);
                        const float frac = rw::OrderHoldFrac(e, qi.holdMs, qi.tapMs);
                        if (down && g.first < 0) g.first = frac;
                        g.top = std::max(g.top, frac); g.last = frac;
                        if (st.event == rw::Event::Pick && rw::EnginePicks(cl, st.picked)) g.enginePick = true;
                        const auto act = rw::OrderActOf(e, pointer);
                        if (act == rw::OrderAct::Pick) { g.pick = true; g.seat = e.seat; }
                        if (act == rw::OrderAct::Assign) { g.assign = true; g.slot = e.slot; g.hand = e.hand; g.seat = e.seat; g.assignAt = t - 400; }
                        if (act == rw::OrderAct::NoChange) g.noChange = true;
                        if (e.event != rw::Quick::None && act == rw::OrderAct::None) g.other = true;
                    }
                    return g;
                };
                Check(order >= 0 && skill >= 0, "order hold: the fixture has an order seat and a skill seat");
                { const auto g = run(order, pointer, 0, 200);   // a tap
                  Check(g.pick && g.seat == order && !g.assign && !g.noChange && !g.enginePick, "order hold: the pointing hand's trigger, a 200 ms tap: the order is picked for the glyph, once, by the tap rule");
                  Check(g.first > 0 && g.last == 0, "order hold: the hold ring shows from the first frame of the press (" + std::to_string(g.first) + ") and is gone after the release"); }
                { const auto g = run(order, pointer, 0, 600);   // in between
                  Check(!g.pick && !g.assign && g.noChange && !g.enginePick, "order hold: let go at 600 ms, between the Tap limit and Hold to assign: nothing is picked, nothing assigned");
                  Check(g.first > 0 && g.top > .55f && g.top < .65f, "order hold: the ring had filled to the press's share of the hold (" + std::to_string(g.top) + ")"); }
                { const auto g = run(order, pointer, 0, 1500);   // held to full
                  Check(g.assign && g.slot == 0 && g.hand == pointer && g.seat == order && std::fabs(g.assignAt - 1000) <= 5 && !g.pick && !g.enginePick && !g.noChange, "order hold: held to Hold to assign: it goes on that hand's Trigger at 1000 ms, and the release after picks nothing");
                  Check(g.top == 1, "order hold: the ring is full at the assign"); }
                { const auto g = run(order, 1, 2, 200); Check(!g.pick && !g.assign && !g.enginePick, "order hold: another button's tap on an order does nothing (no order is given from the chooser)"); }
                { const auto g = run(order, 1, 2, 1500); Check(g.assign && g.slot == 2 && g.hand == 1 && !g.pick, "order hold: another button held to full: the order goes on that button"); }
                { const auto g = run(order, 1, 0, 200); Check(!g.pick && !g.assign, "order hold: the wheel hand's trigger is not the pick"); }
                { const auto g = run(order, pointer, 0, 1500, 0); Check(g.pick && !g.assign && g.top == 0, "order hold: Hold to assign Off: a press of any length picks, and no ring shows"); }
                for (const double held : {200.0, 600.0, 1500.0}) {   // a skill's seat on the chooser: today's pick at any press length
                    const auto g = run(skill, pointer, 0, held);
                    Check(g.enginePick && !g.pick && !g.assign && !g.noChange && !g.other && g.top == 0, "order hold: a skill's seat keeps the chooser's pick at any press length, no hold ring: " + std::to_string(int(held)) + " ms");
                }
                { const auto g = run(skill, 1, 2, 1500); Check(!g.assign && !g.pick && !g.enginePick && g.top == 0, "order hold: a button held on a skill's seat on the chooser assigns nothing"); }
                // a cancel button off the wheel closes nothing on the chooser (the main ring's rule stays the main ring's)
                rw::QuickCast q; rw::QuickIn qi; qi.nowMs = 100; qi.main = true; qi.under = rw::OrderUnder(cl, rw::Target{}); qi.press[0] = 2; qi.down[0][2] = true;
                Check(q.Step(qi, cl).event == rw::Quick::None, "order hold: B off the wheel on the chooser is not a close");
            }
        }
        // #20: the Gestures page in upstream's card order; the Warlock has none (no half, no route).
        for (int cls = 0; cls < 8; ++cls) {
            rw::PageView v;
            rw::FillGestures(v, cls, 0, [](int) { return std::string("x"); }, [](int) { return -1; }, [](int) { return true; });
            const auto page = framevr::spellmotion::PageOf(cls);
            bool same = v.count == page.count;
            for (int i = 0; i < v.count && same; ++i) same = framevr::spellmotion::Family(page.skills[i]) || v.skill[size_t(i)] == page.skills[i];
            Check(same && (v.count == 0) == !framevr::spellmotion::HasMotions(cls), "R7 #20 Gestures order == spellmotion::PageOf; a class without motions: none (the Warlock has motions since v0.3.35, #523)");
            Check(v.below == std::to_string(v.count) + " of " + std::to_string(v.count) + " on" || v.count == 0, "R7 all on: 'n of n on'");
        }
        {   // the main page without motions: one Glyphs center (no Gestures half to route)
            const int per[3] = {10, 10, 10};
            rw::Engine e; e.Reset(ClassLayout(7, per, rw::TreeLayout::Shown)); rw::Settings s2; s2.hasMotions = false;
            Check(e.HitTest(rw::CanvasCx, rw::CanvasCy + 40, s2).kind == rw::Kind::Glyphs, "R7 #20 the Warlock: the lower half is Glyphs too");
        }
        {   // the draw: both pages build, a closed wheel builds none, the shapes' strokes fit the text budget
            rw::PageView g; auto sets = ia::NoGlyphs; sets[0][1] = 40;
            rw::FillGlyphs(g, sets, 0, [](int) { return std::string("Frozen Armor"); }, [](int) { return 3; });
            Check(g.line[1] == "Casts Frozen Armor" && g.line[0] == "No skill yet" && g.title == "Right hand's glyphs" && g.bottom == "Left" && g.bottomCount == "Left hand's 8 shapes", "R7 the Glyphs page's words (L3)");
            rw::Scene s; s.open = true;
            static rw::Instance list[rw::MaxDraw];
            const int n = rw::BuildPage(s, g, list);
            Check(n > 8 * 4 && n <= rw::MaxDraw, "R7 the Glyphs page builds within its budget");
            s.open = false; Check(rw::BuildPage(s, g, list) == 0, "R7 closed: nothing");
        }
    }
    {   // Round 2026-11-08 item 6: the Gestures page's cards all have an icon, learned or not, every class; every family member too
        for (int cls = 0; cls < 8; ++cls) {
            const auto page = framevr::spellmotion::PageOf(cls);
            std::array<int, rw::MaxSeats + rw::ExtraIcons> ids{}; std::array<int16_t, rw::MaxSeats + rw::ExtraIcons> cells{};
            int n = 0;
            for (; n < rw::MaxSeats; ++n) ids[size_t(n)] = 5000 + n;   // the fullest wheel, none of them a card
            for (int i = 0; i < page.count; i += 2) ids[size_t(i)] = page.skills[i];   // every second card learned: the seats have those
            const int seats = n;
            n = rw::AddGesturePageIcons(cls, ids.data(), n, int(ids.size()));
            Check(n - seats <= rw::ExtraIcons && n <= rw::AtlasCells * rw::AtlasCells, "item 6 icon count inside the atlas (" + std::to_string(n) + ")");
            Check(rw::AddGesturePageIcons(cls, ids.data(), n, int(ids.size())) == n, "item 6 a second call adds nothing");
            std::vector<int> sorted(ids.begin(), ids.begin() + n); std::sort(sorted.begin(), sorted.end());
            Check(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(), "item 6 no id twice");
            if (!framevr::spellmotion::HasMotions(cls)) Check(n == seats, "item 6 a class with no page: nothing added");
            for (int i = 0; i < n; ++i) cells[size_t(i)] = int16_t(i);
            auto cellOf = [&](int id) { return rw::CellOfSkill(ids.data(), cells.data(), n, id); };
            for (int mask = 0; mask < 32; ++mask) {   // every family member learned or not
                const int family[5] = {75, 85, 94, 268, 279};
                auto known = [&](int id) { for (int b = 0; b < 5; ++b) if (family[b] == id) return (mask >> b & 1) != 0; return false; };
                rw::PageView v;
                rw::FillGestures(v, cls, 0, [](int) { return std::string("x"); }, cellOf, known);
                bool all = v.count == page.count;
                for (int i = 0; i < v.count; ++i) all = all && v.cell[size_t(i)] >= 0;
                Check(all, "item 6 every card has a cell, class " + std::to_string(cls) + " mask " + std::to_string(mask));
            }
        }
        // The look: learned and on, no points, and turned Off (with and without points)
        rw::PageView v; v.page = rw::RingPage::Gestures; v.count = 4;
        for (int i = 0; i < 4; ++i) v.cell[size_t(i)] = int16_t(i);
        v.on = {true, true, false, false}; v.has = {true, false, true, false};
        rw::Scene s; s.open = true; s.anim = rw::PresetAnim(rw::Preset::Off);
        static rw::Instance list[rw::MaxDraw];
        const int n = rw::BuildPage(s, v, list);
        auto iconAt = [&](int k) { float x, y; rw::RingPos(k, 4, rw::GestureR, x, y); x += rw::CanvasX; y += rw::CanvasY;
            for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeIcon && std::fabs(list[i].cx - x) < .01f && std::fabs(list[i].cy - y) < .01f) return i; return -1; };
        auto pillAt = [&](int k) { float x, y; rw::RingPos(k, 4, rw::GestureR, x, y); x += rw::CanvasX + 20; y += rw::CanvasY + 20;
            for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeRect && std::fabs(list[i].cx - x) < .01f && std::fabs(list[i].cy - y) < .01f) return true; return false; };
        const int on = iconAt(0), none = iconAt(1), off = iconAt(2), offNone = iconAt(3);
        Check(on >= 0 && list[on].param == 2 + rw::IconDisc && list[on].a == 1 && !pillAt(0), "item 6 learned and on: in color, full, no pill (on its dark disc, the clean icon's plate)");
        Check(none > 0 && list[none].param < 0 && std::fabs(list[none].a - .45f) < 1e-4f && !pillAt(1) && list[none - 1].recipe == rw::RecipeDisc && list[none - 1].r == 0 && list[none - 1].a == 1, "item 6 no points: gray at 45% over a black disc, no Off pill");
        Check(off >= 0 && list[off].param < 0 && list[off].a == .5f && pillAt(2) && offNone >= 0 && list[offNone].param < 0 && list[offNone].a == .5f && pillAt(3), "item 6 turned Off keeps its look: gray at 50% with the pill, learned or not");
    }
    {   // R8 (eval #3, L4 table): each preset's per-icon weights, at rest and pointed (a lightning icon)
        struct W { bool floats, glow, hover, face; float crawl; };
        auto w = [](rw::Preset p, bool pointed) { const auto l = rw::LayersFor(rw::PresetAnim(p), pointed, 0, 1, 50); return W{l.floats, l.glowMoves, l.hoverFx, l.faceFx, l.crawl}; };
        auto same = [](W a, W b) { return a.floats == b.floats && a.glow == b.glow && a.hover == b.hover && a.face == b.face && std::fabs(a.crawl - b.crawl) < .01f; };
        Check(same(w(rw::Preset::Off, false), {false, false, false, false, 0}) && same(w(rw::Preset::Off, true), {false, false, false, false, 0}), "R8 Off: nothing moves, pointed or not");
        Check(same(w(rw::Preset::Floating, false), {true, false, false, false, 0}) && same(w(rw::Preset::Floating, true), {false, false, false, false, 0}), "R8 Floating icons: floats at rest, no effects; the pointed one still");
        Check(same(w(rw::Preset::Hover, false), {true, false, false, false, 0}) && same(w(rw::Preset::Hover, true), {false, true, true, true, 1}), "R8 Effect on hover: floats at rest; the pointed one glows, hover, face, crawl");
        Check(same(w(rw::Preset::All, true), {false, true, true, true, 1}) && w(rw::Preset::All, false).glow && w(rw::Preset::All, false).floats, "R8 All effects: every glow moves");
        Check(rw::PresetAnim(rw::Preset::Off).still && !rw::PresetAnim(rw::Preset::Floating).still, "R8 Off is still (a compose only on a change)");
        auto a = rw::PresetAnim(rw::Preset::Hover); a.crawlS = 4;
        Check(rw::PresetOf(a) == rw::Preset::Custom && rw::PresetOf(rw::PresetAnim(rw::Preset::All)) == rw::Preset::All, "R8 a row changed by hand reads Custom");
        // The crawl at rest (All effects, 8 s): one 300 ms burst per lightning icon, never two at once, each once a period.
        const auto all = rw::PresetAnim(rw::Preset::All);
        int overlap = 0; std::array<int, 5> starts{}; std::array<bool, 5> was{};
        for (double t = 0; t < 16000; t += 1) {
            int on = 0;
            for (int i = 0; i < 5; ++i) { const bool b = rw::LayersFor(all, false, i, 5, t).crawl > 0; on += b; if (b && !was[size_t(i)]) ++starts[size_t(i)]; was[size_t(i)] = b; }
            overlap += on > 1;
        }
        Check(overlap == 0 && starts == std::array<int, 5>{2, 2, 2, 2, 2}, "R8 crawl: staggered, never two at once, once each 8 s");
        Check(rw::LayersFor(all, false, -1, 5, 0).crawl == 0, "R8 crawl only on lightning icons");
        // Icon art motion: Zeal three swings' copies within 0.55 rad; Charge leaves and comes back; steel lunges at most 8 px.
        rw::FaceCopy c[3];
        float maxRot = 0, maxDx = 0, minDx = 0;
        for (double t = 0; t < 3200; t += 5) { const int n = rw::FaceCopies(106, rw::Element::Holy, t, 28, c); for (int i = 0; i < n; ++i) maxRot = std::max(maxRot, std::fabs(c[i].rot)); }
        for (double t = 0; t < 1800; t += 5) { const int n = rw::FaceCopies(107, rw::Element::Holy, t, 28, c); for (int i = 0; i < n; ++i) { maxDx = std::max(maxDx, c[i].dx); minDx = std::min(minDx, c[i].dx); } }
        Check(maxRot > .5f && maxRot <= .551f, "R8 Zeal swings to 0.55 rad");
        Check(maxDx > 28 * 2 && minDx < -28 * 2 && rw::FaceCopies(107, rw::Element::Holy, 1799, 28, c) == 1 && std::fabs(c[0].dx) < 1, "R8 Charge dashes out and comes back to rest");
        float lunge = 0;
        for (double t = 0; t < 1300; t += 5) { rw::FaceCopies(1, rw::Element::Phys, t, 28, c); lunge = std::max(lunge, c[0].dx); }
        Check(lunge > 7 && lunge <= 8.01f, "R8 steel lunges 8 px");
        // Tree burst: from the orb, overshoot, settled at 340 ms + its stagger; Off: a plain 160 ms ease.
        float peak = 0; for (double t = 0; t < 400; t += 2) peak = std::max(peak, rw::BurstAt(t, 0, true));
        Check(rw::BurstAt(0, 0, true) < .01f && peak > 1.02f && std::fabs(rw::BurstAt(340, 0, true) - 1) < .01f && rw::BurstAt(28 * 3, 3, true) < .01f, "R8 burst: overshoot, 28 ms stagger");
        Check(rw::BurstAt(80, 0, false) == .5f && rw::BurstAt(160, 0, false) == 1, "R8 burst Off: a plain 160 ms ease");
        // R8 match stage 2 (spec 4a): the pointed effect's lifecycle.
        rw::PointedFx p;
        const bool early = p.Step(0, 3).seat < 0 && p.Step(99, 3).seat < 0;
        const auto s0 = p.Step(100, 3), s1 = p.Step(150, 3), s2 = p.Step(200, 3);
        Check(early && s0.seat == 3 && s0.alpha == 0 && std::fabs(s1.alpha - .5f) < .01f && s2.alpha == 1 && std::fabs(s2.ageMs - 100) < .01,
              "stage 2: the effect starts 100 ms after the pointing and fades in over 100 ms; its age from its start");
        rw::PointedFx sweep; bool any = false;
        for (int k = 0; k < 6; ++k) for (double t = 0; t < 60; t += 10) any |= sweep.Step(k * 60 + t, k).seat >= 0;
        Check(!any, "stage 2: a sweep across neighbors (60 ms each) starts no effect");
        const auto l0 = p.Step(300, 5), l1 = p.Step(360, 5), l2 = p.Step(420, 5), l3 = p.Step(470, 5);
        Check(l0.seat == 3 && l0.alpha == 1 && l1.seat == 3 && std::fabs(l1.alpha - .5f) < .01f && l2.seat == 5 && l2.alpha == 0 && l2.ageMs == 0 &&
              std::fabs(l3.alpha - .5f) < .01f, "stage 2: leaving fades out over 120 ms; the next target waits for it (one effect at a time)");
        p.Step(600, -1);
        const bool gone = p.Step(720, -1).seat < 0;
        const auto r0 = p.Step(800, 5), r1 = p.Step(850, 5);
        Check(gone && r0.seat == 5 && std::fabs(r0.ageMs - 380) < .01 && std::fabs(r1.alpha - .5f) < .01f, "stage 2: re-pointed within 300 ms, the effect goes on from its age");
        p.Step(1000, -1);
        const bool waits = p.Step(1400, 5).seat < 0;
        const auto n0 = p.Step(1500, 5);
        Check(waits && n0.seat == 5 && n0.ageMs == 0, "stage 2: re-pointed after 300 ms, it starts over");
        p.Step(1600, 5); p.Step(1650, 7);
        const auto b0 = p.Step(1680, 5), b1 = p.Step(1700, 5);
        Check(b0.seat == 5 && b1.seat == 5 && b1.alpha > b0.alpha && b0.alpha > .4f, "stage 2: back on it mid-fade, it fades in again from there");
        // The element defaults (kit hover-defaults.js) and the face's (kit WHEEL_FACE_DEFAULT); multi's crossfade; the art floor.
        auto glowOf = [](rw::RestKind a, rw::RestKind b = rw::RestKind::None, uint32_t ca = 0) { rw::RestGlow g; g.kind[0] = a; g.kind[1] = b; g.rgb[0] = ca; return g; };
        auto kindNamed = [](const char* n) { for (int k = 0; k < rw::RestKindCount; ++k) if (std::string(n) == rw::RestKindName[k]) return rw::RestKind(k); return rw::RestKind::None; };
        uint32_t rgb = 0;
        const bool chargeSkips = rw::HoverKindOf(glowOf(rw::RestKind::Charge, rw::RestKind::Aura), &rgb) == rw::RestKind::Aura && rgb == rw::HoverAuraRgb;
        const bool auraColor = rw::HoverKindOf(glowOf(rw::RestKind::Aura, rw::RestKind::None, 0x3050F0), &rgb) == rw::RestKind::Aura && rgb == 0x3050F0;
        const bool curse = rw::HoverKindOf(glowOf(kindNamed("necromancer/curse"), rw::RestKind::None, 3), &rgb) == rw::RestKind::Shadow && rgb == rw::HoverAuraRgb;
        Check(chargeSkips && auraColor && curse && rw::HoverKindOf(glowOf(rw::RestKind::Charge)) == rw::RestKind::Charge &&
              rw::HoverKindOf(glowOf(rw::RestKind::Multi)) == rw::RestKind::Multi && rw::HoverKindOf(rw::RestGlow{}) == rw::RestKind::None &&
              rw::HoverKindOf(glowOf(kindNamed("warlock/demon"))) == rw::RestKind::None,
              "stage 2: the hover recipe is the first entry that is not a charge; a class kind plays its element; 'demon' none (S2-2); a curse shape is no color");
        bool sums = true;
        for (double t = 0; t < 7200; t += 25) { float w = 0; for (int e = 0; e < 3; ++e) w += rw::MultiWeight(e, t); sums &= std::fabs(w - 1) < 1e-4f; }
        Check(sums && rw::MultiWeight(0, 0) == 1 && std::fabs(rw::MultiWeight(1, 1100) - .5f) < .01f && rw::MultiWeight(1, 1300) == 1, "stage 2: multi plays fire, cold, lightning 1.2 s each, a 200 ms crossfade");
        float floorA = 1; for (double t = 0; t < 1000; t += 5) { rw::FaceCopies(1, rw::Element::Shadow, t, 28, c); floorA = std::min(floorA, c[0].alpha); }
        Check(floorA >= .2f && rw::FaceElementOf(glowOf(rw::RestKind::Phys, rw::RestKind::Fire)) == rw::Element::Phys &&
              rw::FaceElementOf(glowOf(rw::RestKind::Fire, rw::RestKind::Phys)) == rw::Element::Count, "stage 2: the face default keys on the first entry; shadow's art never under 20%");
    }
    {   // R8 fight safety (L2 Hit; critique-vr-ux S7): 40% over 150 ms, held 1.5 s after the last hit, back over 300 ms
        rw::FightFade f;
        auto at = [&](double t, float hp, bool fresh = true, uint64_t session = 7) { return f.Step(t, true, hp, fresh, session); };
        at(0, 100); at(990, 100);
        auto o = at(1000, 80);
        Check(o.dim == 1 && o.lastHitMs == 1000, "R8 a hit: the dim starts");
        Check(std::fabs(at(1075, 80).dim - .7f) < .01f && std::fabs(at(1150, 80).dim - .4f) < .01f, "R8 to 40% over 150 ms");
        Check(std::fabs(at(2400, 80).dim - .4f) < .01f, "R8 held at 40% for 1.5 s after the hit");
        o = at(2000, 70);   // a second hit: extends, never re-flashes
        Check(std::fabs(o.dim - .4f) < .01f && std::fabs(at(3400, 70).dim - .4f) < .01f, "R8 a new hit extends the hold and never re-flashes");
        Check(std::fabs(at(3650, 70).dim - .7f) < .01f && at(3800, 70).dim == 1, "R8 back over 300 ms");
        Check(!o.hitsOff && o.wheel == 1, "R8 a hit keeps hits on and the wheel full");
        o = at(4000, 0);
        Check(o.hitsOff && o.wheel == .15f, "R8 death: 15%, no hits");
        Check(at(5000, 100).hitsOff, "R8 dead until the wheel closes and opens again");
        f.Reset();
        Check(!at(6000, 100).hitsOff, "R8 a reopen starts clean");
        Check(!at(6500, 100, false).hitsOff && !at(7400, 100, false).hitsOff, "R8 a stale snapshot under 1 s: not a load");
        Check(at(7500, 100, false).hitsOff, "R8 stale for 1 s: a load (15%, no hits)");
        f.Reset(); at(0, 100, true, 7);
        Check(at(10, 100, true, 8).hitsOff, "R8 a new session: a load");
        // No assign within 300 ms of a hit; the hold fires once it is clear.
        const int per[3] = {10, 10, 10};
        const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
        rw::QuickCast q; int assignAt = -1;
        for (double t = 0; t <= 1600; t += 1) {
            rw::QuickIn in; in.nowMs = t; in.main = true; in.under = {rw::Kind::Seat, 4}; in.lastHitMs = 900;
            if (t == 200) in.press[0] = 1;
            in.down[0][1] = t >= 200;
            if (q.Step(in, l).event == rw::Quick::Assign) assignAt = int(t);
        }
        Check(assignAt == 1200, "R8 hold to assign waits until 300 ms after a hit (full at 1200 ms: " + std::to_string(assignAt) + ")");
    }
    {   // R8 draw: a glow behind each icon (frozen when Off), the hover effect and crawl on the pointed lightning icon; redraws
        const int per[3] = {10, 10, 10};
        const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
        std::vector<uint8_t> el(size_t(l.count)); std::vector<int8_t> ci(size_t(l.count), -1); int nc = 0;
        for (int i = 0; i < l.count; ++i) { el[size_t(i)] = uint8_t(rw::ElementOf(1, l.seats[size_t(i)].tree)); if (el[size_t(i)] == uint8_t(rw::Element::Light)) ci[size_t(i)] = int8_t(nc++); }
        int light = -1; for (int i = 0; i < l.count; ++i) if (ci[size_t(i)] >= 0) { light = i; break; }
        rw::Scene s; s.open = true; s.layout = &l; s.elements = el.data(); s.crawlIndex = ci.data(); s.crawlCount = nc; s.tMs = 5000; s.pointedSinceMs = 4500;
        s.step.under = {rw::Kind::Seat, light};
        static rw::Instance list[rw::MaxDraw];
        auto count = [&](uint32_t recipe, float* t = nullptr, int mode = -1) { int c = 0; const int n = rw::Build(s, list); for (int i = 0; i < n; ++i) if (list[i].recipe == recipe && (mode < 0 || int(list[i].param) / 256 == mode)) { ++c; if (t && list[i].param < 256) *t = std::max(*t, list[i].u0); } return c; };
        s.anim = rw::PresetAnim(rw::Preset::Hover);
        float t = 0;
        Check(count(rw::RecipeFx, &t, 0) == l.count && count(rw::RecipeFx, nullptr, 1) == 1 && count(rw::RecipeCrawl) == 1, "R8 Effect on hover: a glow per icon, a hover effect and a crawl on the pointed lightning icon");
        {   // designer S3a FIX: the pointed default again over its own face: one face-sized quad, after every icon (the over pass)
            const int n = rw::Build(s, list);
            int face = -1, faces = 0, lastIcon = -1;
            for (int i = 0; i < n; ++i) {
                if (list[i].recipe == rw::RecipeFx && uint32_t(list[i].param) >= rw::PointedOverFace) { face = i; ++faces; }
                if (list[i].recipe == rw::RecipeSeat || list[i].recipe == rw::RecipeIcon || list[i].recipe == rw::RecipeFace) lastIcon = i;
            }
            Check(faces == 1 && face > lastIcon && list[face].w <= rw::IconPx * 1.3f + 2.01f, "S3a: the pointed default over its own face: one face-sized quad after every icon");
        }
        {   // review N5: the stage the log names is the stage Build draws (the game textures only when they are in)
            Check(rw::FxStageOf(rw::RestKind::Fire, true) == 3 && rw::FxStageOf(rw::RestKind::Fire, false) == 4 && rw::FxStageOf(rw::RestKind::Light, true) == 3 &&
                  rw::FxStageOf(rw::RestKind::Cold, true) == 4 && rw::FxStageOf(rw::RestKind::None, true) == 0, "N5: FxStageOf follows the game textures (3 with them, 4 without)");
            int fire = -1; for (int i = 0; i < l.count; ++i) if (el[size_t(i)] == uint8_t(rw::Element::Fire)) { fire = i; break; }
            rw::Scene f = s; f.step.under = {rw::Kind::Seat, fire};
            auto game = [&](bool tex) { f.fxTex = tex; const int n = rw::Build(f, list); int g = 0; for (int i = 0; i < n; ++i) g += list[i].recipe == rw::RecipeFx && (uint32_t(list[i].param) / 256) % 16 == 4; return g; };
            Check(fire >= 0 && game(true) == 2 && game(false) == 0, "N5: a pointed fire skill draws the game-texture default (under + over its face) only with the textures in");
            std::vector<uint8_t> pe = el; pe[size_t(fire)] = uint8_t(rw::Element::Poison); f.elements = pe.data();
            Check(game(true) == 2, "S3a recheck: a pointed poison skill's cloud also over its face (every element, as the kit)");
        }
        {   // review N6: Build never passes MaxInstances shapes, whatever the caller allows (the effect buffers hold that many)
            rw::Layout big; big.count = rw::MaxSeats;
            for (int i = 0; i < big.count; ++i) { auto& b = big.seats[size_t(i)]; b.id = 200 + i; b.tree = i % rw::Trees; b.x = 60.f + 50.f * float(i % 8); b.y = 60.f + 50.f * float(i / 8); }
            std::vector<rw::RestGlow> two(size_t(big.count));
            for (auto& g : two) { g.kind[0] = rw::RestKind::Light; g.kind[1] = rw::RestKind::Light; }
            rw::Scene b = s; b.layout = &big; b.rest = two.data(); b.elements = nullptr; b.crawlIndex = nullptr; b.anim = rw::PresetAnim(rw::Preset::All);
            const int n = rw::Build(b, list, rw::MaxDraw, 0);
            Check(n <= rw::MaxInstances && n >= rw::MaxInstances - 16, "N6: 64 seats of two lightning glows fill the shapes to MaxInstances and no further (" + std::to_string(n) + ")");
        }
        s.anim = rw::PresetAnim(rw::Preset::Off); t = 0;
        Check(count(rw::RecipeFx, &t, 0) == l.count && t == 0 && count(rw::RecipeFx, nullptr, 1) == 0 && count(rw::RecipeCrawl) == 0, "R8 Off: the glows frozen (a steady rim), no effects");
        Check(rw::Build(s, list) <= rw::MaxDraw, "R8 within the instance budget");
        {   // perf R8 stage 2 check: the effects in two passes, so a compose is at most 4 draws (3; +1 for badges)
            s.anim = rw::PresetAnim(rw::Preset::All);
            const int n = rw::Build(s, list), runs = rw::DrawRuns(list, n);
            int fx = 0; for (int i = 0; i < n; ++i) fx += list[i].recipe == rw::RecipeFx;
            Check(fx > l.count && runs <= 4, "perf: a compose is at most 4 draws (" + std::to_string(runs) + " for " + std::to_string(fx) + " effects)");
            static rw::text::Metrics m3;   // fake fonts: every glyph 8 x 10 px, so the footer's labels are text
            m3.ok = true; for (int st = 0; st < rw::text::Styles; ++st) for (auto& g : m3.g[size_t(st)]) { g.advance = 9; g.w = 8; g.h = 10; }
            rw::Scene t2 = s; t2.metrics = &m3; t2.footer.on = true;
            const int n2 = rw::Build(t2, list);
            int lastFx = -1, firstText = n2, texts = 0;
            for (int i = 0; i < n2; ++i) { if (list[i].recipe == rw::RecipeFx) lastFx = i; if (list[i].recipe == rw::RecipeGlyph) { ++texts; if (firstText == n2) firstText = i; } }
            rw::Instance runs5[5]{};   // review N3: base, fx, fx, base, fx = 4 runs; one = 1; none = 0
            for (int i : {1, 2, 4}) runs5[i].recipe = rw::RecipeFx;
            Check(rw::DrawRuns(runs5, 5) == 4 && rw::DrawRuns(runs5, 1) == 1 && rw::DrawRuns(runs5 + 1, 2) == 1 && rw::DrawRuns(runs5, 0) == 0, "perf: DrawRuns counts shader changes");
            Check(texts > 0 && lastFx >= 0 && lastFx < firstText && rw::DrawRuns(list, n2) <= 4, "perf look check: the text (letters, names, badges) comes after every effect");
        }
        rw::ComposePolicy p;
        s.anim = rw::PresetAnim(rw::Preset::Off);
        p.Want(s); p.Done(true);
        int redraws = 0; for (int i = 0; i < 20; ++i) { s.tMs += 11; if (p.Want(s)) { ++redraws; p.Done(true); } }
        Check(redraws == 0, "R8 Off: no compose without a change");
        s.anim = rw::PresetAnim(rw::Preset::Hover); s.anim.floats = false;
        redraws = 0; for (int i = 0; i < 20; ++i) { s.tMs += 11; if (p.Want(s)) { ++redraws; p.Done(true); } }
        Check(redraws >= 9 && redraws <= 11, "R8 a pointed icon's effects: ambient, at half the display rate");
        s.fight = .4f; Check(p.Want(s), "R8 the fight dim redraws");
        // Icon art motion: Charge's dashing copies are cut to the seat's circle (RecipeFace), never a plain icon off its seat.
        const auto pal = ClassLayout(3, per, rw::TreeLayout::Shown);
        int charge = -1; for (int i = 0; i < pal.count; ++i) if (pal.seats[size_t(i)].id == 107) charge = i;
        rw::Scene q; q.open = true; q.layout = &pal; q.anim = rw::PresetAnim(rw::Preset::Hover); q.step.under = {rw::Kind::Seat, charge};
        q.pointedSinceMs = 0; q.tMs = 760;   // mid dash
        const int n = rw::Build(q, list);
        int faces = 0, offSeat = 0;
        const float sx = rw::CanvasX + pal.seats[size_t(charge)].x, sy = rw::CanvasY + pal.seats[size_t(charge)].y;
        for (int i = 0; i < n; ++i) {
            if (list[i].recipe == rw::RecipeFace) ++faces;
            if (list[i].recipe == rw::RecipeIcon && std::hypot(list[i].cx - sx, list[i].cy - sy) > 1 && std::hypot(list[i].cx - sx, list[i].cy - sy) < 60) ++offSeat;
        }
        Check(charge >= 0 && faces == 3 && offSeat == 0, "R8 Charge's three copies are cut to the seat's circle");
    }
    {   // Feedback 1 item 4: the footer drawn from the hit test's numbers; upstream's labels
        using P = ia::MenuPage;
        int boxes = 0, bad = 0;
        for (P page : {P::Spells, P::Glyphs, P::Motions, P::EditSlots, P::EditSkills, P::EditItems}) for (int dev = 0; dev < 2; ++dev) for (int full = 0; full < 2; ++full)
            for (int i = 0; i < ia::FooterCount(page, dev); ++i) {
                const auto b = rw::FooterBoxAt(i, page, dev, full);
                ++boxes;
                const float cx = (b.x0 + b.x1) / 2, cy = (b.y0 + b.y1) / 2;
                auto at = [&](float x, float y) { return rw::FooterAt(x, y, dev, full, page); };
                if (at(cx, cy) != b.cell || at(b.x0 + 1, cy) != b.cell || at(b.x1 - 1, cy) != b.cell || at(cx, b.y0 + 1) != b.cell || at(cx, b.y1 - 1) != b.cell) ++bad;
                if (at(b.x0 - 1, cy) == b.cell || at(b.x1 + 1, cy) == b.cell || at(cx, b.y0 - 1) == b.cell || at(cx, b.y1 + 1) == b.cell) ++bad;
            }
        Check(boxes == 2 * (3 * 4 + 9) + 2 * (3 * 5 + 9) && bad == 0, "item 4: every drawn tab box is FooterAt's box (4 and 5 tabs, both widths, every ring page and the editors)");
        Check(std::string(rw::FooterLabel(0, P::Spells, false, true)) == "EQUIPMENT" && std::string(rw::FooterLabel(0, P::Glyphs, false, true)) == "SPELLS" &&
              std::string(rw::FooterLabel(3, P::Spells, false, true)) == "INVENTORY" && std::string(rw::FooterLabel(4, P::Spells, true, true)) == "DEV", "item 4: the labels as upstream's");
        Check(std::string(rw::FooterLabel(0, P::EditSkills, false, true)) == "PREVIOUS" && std::string(rw::FooterLabel(1, P::EditSkills, false, true)) == "NEXT" &&
              std::string(rw::FooterLabel(2, P::EditSkills, false, true)) == "BACK", "item 4: the editor footer reads PREVIOUS / NEXT / BACK");
        // Upstream's own lines, so a rename there fails here (the ring's copy stays upstream's words).
        std::string ui;
        { std::string here = __FILE__; here = here.substr(0, here.find_last_of("/\\") + 1); std::ifstream in(here + "interaction_ui.cpp"); std::stringstream ss; ss << in.rdbuf(); ui = ss.str(); }
        Check(ui.find(R"(const char* footer[5]={(o.utility||o.map||o.developer||o.glyphs||o.motions)?"SPELLS":"EQUIPMENT","MAP","VR SETTINGS",handWheel?"INVENTORY":"EDIT SKILLS","DEV"};)") != std::string::npos &&
              ui.find(R"(if(o.editor==1){footer[0]="BACK";footer[1]="";footer[2]="DONE";})") != std::string::npos &&
              ui.find(R"(if(o.editor>=2){footer[0]="PREVIOUS";footer[1]="NEXT";footer[2]="BACK";})") != std::string::npos,
              "item 4: upstream's footer label lines unchanged (interaction_ui.cpp)");
        // Hover outlines that tab only (the gold rim 183, 143, 73), and no footer when it is off.
        const int per[3] = {10, 10, 10};
        const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
        rw::Scene s; s.open = true; s.layout = &l; s.anim = rw::PresetAnim(rw::Preset::Off);
        s.footer = {true, P::Spells, true, false, true};
        s.step.under = {rw::Kind::Footer, ia::FooterCell(2, P::Spells)};
        static rw::Instance list[rw::MaxDraw];
        auto rims = [&](float r) { int c = 0; const int n = rw::Build(s, list); for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeRect && std::fabs(list[i].r - r) < .002f) ++c; return c; };
        Check(rims(183 / 255.f) == 1 && rims(104 / 255.f) == 4, "item 4: the pointed tab's rim gold, the other four upstream's");
        const auto hot = rw::FooterBoxAt(2, P::Spells, true, false);
        int found = 0; { const int n = rw::Build(s, list); for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeRect && std::fabs(list[i].r - 183 / 255.f) < .002f &&
            std::fabs(list[i].cx - (hot.x0 + hot.x1) / 2) < .01f && std::fabs(list[i].w - (hot.x1 - hot.x0)) < .01f && std::fabs(list[i].h - (hot.y1 - hot.y0)) < .01f) ++found; }
        Check(found == 1, "item 4: the gold rim is exactly the hit box");
        s.footer.on = false; Check(rims(104 / 255.f) == 0, "item 4: footer off, none drawn");
        // One size per row (designer, after f1-footer) with a 5 px margin inside each side (review-r8 I4-2): fake fonts, every
        // capital 0.7 em wide. EQUIPMENT (9) fits the 174 px tabs (4 tabs, 768 px) at 22 px, the five tabs (developer mode) up to 19;
        // VR SETTINGS wraps at the five tabs' size; full width takes 22. At 0.8 em the five tabs shrink to 16.
        static rw::text::Metrics fake;
        for (int st = 0; st < rw::text::Styles; ++st) for (auto& g : fake.g[size_t(st)]) g.advance = .7f * rw::text::Specs[st].px;
        Check(rw::FooterRowStyle(fake, P::Spells, false, false, true) == rw::text::Footer22 && rw::FooterRowStyle(fake, P::Spells, true, false, true) == rw::text::Footer19,
              "item 4: the row's one size is the largest that fits every label (22 px with 4 tabs, 19 with 5)");
        Check(rw::FooterRowStyle(fake, P::Spells, false, true, true) == rw::text::Footer22, "item 4: full width, 22 px");
        const auto vr = rw::FooterBoxAt(2, P::Spells, true, false);
        const auto wrap = rw::FooterFitAt(fake, rw::text::Footer19, "VR SETTINGS", vr.x1 - vr.x0);
        Check(wrap.fits && wrap.split == 2, "item 4: VR SETTINGS wraps to two lines at the five tabs' size");
        const auto inv = rw::FooterBoxAt(3, P::Spells, true, false);
        Check(!rw::FooterFitAt(fake, rw::text::Footer20, "INVENTORY", inv.x1 - inv.x0).fits && rw::FooterFitAt(fake, rw::text::Footer19, "INVENTORY", inv.x1 - inv.x0).fits,
              "I4-2: INVENTORY over the 5 px margin at 20, inside at 19 (five tabs)");
        for (int st = 0; st < rw::text::Styles; ++st) for (auto& g : fake.g[size_t(st)]) g.advance = .8f * rw::text::Specs[st].px;
        Check(rw::FooterRowStyle(fake, P::Spells, true, false, true) == rw::text::Footer16, "I4-2: five tabs at .8 em shrink the whole row (16 px)");
        for (int st = 0; st < rw::text::Styles; ++st) for (auto& g : fake.g[size_t(st)]) g.advance = .7f * rw::text::Specs[st].px;
        bool caps = true;
        for (P page : {P::Spells, P::Glyphs, P::EditSlots, P::EditSkills}) for (int dev = 0; dev < 2; ++dev) for (int hw = 0; hw < 2; ++hw)
            for (int i = 0; i < 6; ++i) for (const char* c = rw::FooterLabel(i, page, dev, hw); *c; ++c) caps = caps && *c >= ' ' && *c <= 'Z';
        Check(caps, "item 4: every footer label is in the footer sizes' glyphs (space to Z)");
    }
    {   // R7c (feedback 1 item 7): the tree marks' glyphs and the center's rune band
        using E = rw::Element;
        using G = rw::MarkGlyph;
        // ElementOf / TreeGlyphOf take the wheel position (Seat::tree) and read the class's skillcategories page through TreeOrder
        Check(rw::ElementOf(3, rw::PositionOf(3, 1)) == E::Aura && rw::ElementOf(3, rw::PositionOf(3, 0)) == E::Phys &&
              rw::ElementOf(0, rw::PositionOf(0, 2)) == E::Light && rw::ElementOf(0, rw::PositionOf(0, 1)) == E::Arcane,
              "R7c: ElementOf by position (Paladin Offensive Auras = aura, Amazon Javelin = lightning)");
        Check(rw::TreeGlyphOf(1, 0) == G::Fire && rw::TreeGlyphOf(1, 1) == G::Light && rw::TreeGlyphOf(1, 2) == G::Cold,
              "R7c: Sorceress marks flame, bolt, flake");
        Check(rw::TreeGlyphOf(3, rw::PositionOf(3, 0)) == G::Holy && rw::TreeGlyphOf(3, rw::PositionOf(3, 1)) == G::Aura && rw::TreeGlyphOf(3, rw::PositionOf(3, 2)) == G::Arcane,
              "R7c: Paladin marks hammer, sun, shield by page");
        Check(rw::TreeGlyphOf(6, rw::PositionOf(6, 0)) == G::Aura && rw::TreeGlyphOf(6, rw::PositionOf(6, 1)) == G::Shadow && rw::TreeGlyphOf(6, rw::PositionOf(6, 2)) == G::Phys &&
              rw::TreeGlyphOf(7, rw::PositionOf(7, 2)) == G::Fire && rw::TreeGlyphOf(-1, 0) == G::None && rw::TreeGlyphOf(8, 0) == G::None,
              "R7c: Assassin gear, eye, fist; Warlock Chaos flame; an unknown class has none");
        Check(rw::TreeGlyphOf(2, rw::PositionOf(2, 1)) == G::Bone && rw::TreeGlyphOf(7, rw::PositionOf(7, 1)) == G::Arcane,
              "R7c TS3: Necromancer Poison and Bone = bone, Warlock Eldritch = shield (arcane)");
        Check(int(G::Phys) == int(E::Phys) && int(G::Fire) == int(E::Fire) && int(G::Shadow) == int(E::Shadow), "R7c: the mark glyphs share the elements' shader kinds");
        {   // Designer (R7c, after 610ee30e): every mark's name, glyph and accent come from the same kit tree entry (skills.js /
            // skills-more.js trees[page], with the class pages' final values folded in, 2026-11-07); this table is the kit's data,
            // copied by hand (name: a word of the game's tree name; rim 0: none, gold rims).
            struct KitTree { const char* word; uint32_t accent, rim; G glyph; };
            const KitTree kit[8][3] = {
                {{"Bow", 0xC89060, 0xC89060, G::Shadow}, {"Passive", 0xB4A0F0, 0xB4A0F0, G::Arcane}, {"Javelin", 0xF2D04A, 0xE0B020, G::Light}},
                {{"Fire", 0xE8591A, 0, G::Fire}, {"Lightning", 0xF2D04A, 0xE0B020, G::Light}, {"Cold", 0x7FC8F2, 0x7FD6F2, G::Cold}},
                {{"Curses", 0xB060C0, 0xB060C0, G::Shadow}, {"Poison", 0xA0D890, 0xA89F84, G::Bone}, {"Summoning", 0xD8584A, 0xD0503E, G::Aura}},
                {{"Combat", 0xE8C77E, 0xE8C77E, G::Holy}, {"Offensive", 0xE06A4A, 0xE06A4A, G::Aura}, {"Defensive", 0x7CC8A8, 0x7CC8A8, G::Arcane}},
                {{"Combat", 0xD06040, 0xC8704A, G::Phys}, {"Masteries", 0xB8B0A0, 0, G::Arcane}, {"Warcries", 0xE0A040, 0xE8A848, G::Aura}},
                {{"Summoning", 0x8CC860, 0x7CB85A, G::Aura}, {"Shape", 0xB4503C, 0xA84A36, G::Phys}, {"Elemental", 0xF09040, 0xE8833A, G::Fire}},
                {{"Traps", 0xC8B060, 0, G::Aura}, {"Shadow", 0x8E7CC3, 0, G::Shadow}, {"Martial", 0xE07A3A, 0, G::Phys}},
                {{"Demon", 0xF04A47, 0xF04A47, G::Shadow}, {"Eldritch", 0xC578DE, 0xA055B7, G::Arcane}, {"Chaos", 0xF48C02, 0xD96A02, G::Fire}},
            };
            auto same = [](rw::Rgb a, rw::Rgb b) { return std::fabs(a.r - b.r) < .002f && std::fabs(a.g - b.g) < .002f && std::fabs(a.b - b.b) < .002f; };
            int bad = 0, drawnBad = 0;
            const int per3[3] = {4, 4, 4};
            for (int cls = 0; cls < 8; ++cls) {
                const auto lc = ClassLayout(cls, per3, rw::TreeLayout::Shown);
                rw::Scene sc; sc.open = true; sc.layout = &lc; sc.anim = rw::PresetAnim(rw::Preset::Off); sc.skillClass = cls;
                static rw::Instance drawn[rw::MaxDraw];
                const int n = rw::Build(sc, drawn);
                for (int pos = 0; pos < 3; ++pos) {
                    const KitTree& k = kit[cls][rw::PageAt(cls, pos)];
                    const bool name = std::string(sc::Name(cls, rw::PageAt(cls, pos) + 1)).find(k.word) != std::string::npos;
                    const rw::Rgb rimWant = k.rim ? rw::Hex(k.rim) : rw::Gold;
                    if (!name || rw::TreeGlyphOf(cls, pos) != k.glyph || !same(rw::TreeAccentOf(cls, pos), rw::Hex(k.accent)) || !same(rw::TreeRimOf(cls, pos), rimWant)) {
                        ++bad; std::printf("  R7c mark mismatch: class %d position %d (%s)\n", cls, pos, k.word);
                    }
                    // drawn: the mark's ring at this position in that accent, its glyph that kind, and its seats' rims that accent
                    const float x = rw::CanvasCx + 128 * std::cos(lc.dir[size_t(pos)]), y = rw::CanvasCy + 128 * std::sin(lc.dir[size_t(pos)]);
                    int ring = 0, glyph = 0, rims = 0, seats = 0;
                    for (int i = 0; i < n; ++i) {
                        const auto& d = drawn[i];
                        const bool here = std::fabs(d.cx - x) < .01f && std::fabs(d.cy - y) < .01f;
                        if (here && d.recipe == rw::RecipeRing && same({d.r, d.g, d.b}, rw::Hex(k.accent))) ++ring;
                        if (here && d.recipe == rw::RecipeTreeGlyph && d.param == float(k.glyph)) ++glyph;
                    }
                    for (int si = 0; si < lc.count; ++si) {
                        if (lc.seats[size_t(si)].tree != pos) continue;
                        ++seats;
                        const float sx = rw::CanvasX + lc.seats[size_t(si)].x, sy = rw::CanvasY + lc.seats[size_t(si)].y;
                        for (int i = 0; i < n; ++i)
                            if (drawn[i].recipe == rw::RecipeSeat && std::fabs(drawn[i].cx - sx) < 1 && std::fabs(drawn[i].cy - sy) < 1 && same({drawn[i].r, drawn[i].g, drawn[i].b}, rimWant)) { ++rims; break; }
                    }
                    if (ring != 1 || glyph != 1 || seats == 0 || rims != seats) { ++drawnBad; std::printf("  R7c drawn mismatch: class %d position %d ring %d glyph %d rims %d/%d\n", cls, pos, ring, glyph, rims, seats); }
                }
            }
            Check(bad == 0, "R7c: every class's mark name, glyph, accent and rim come from the same kit tree (24 trees)");
            Check(drawnBad == 0, "R7c: every drawn mark's ring (accent), glyph and seat rims (rim, gold when none) match its tree (8 classes x 3)");
        }
        Check(rw::RuneCount(rw::CenterR) == 36 && rw::RuneCount(rw::CenterGrownR) == 63 && rw::RuneCount(20) == 12, "R7c: 36 runes at rest, 63 grown, at least 12");
        int shapes[3] = {};
        for (int i = 0; i < 36; ++i) ++shapes[int(rw::RuneOf(i, 80))];
        Check(shapes[0] > 3 && shapes[1] > 3 && shapes[2] > 3, "R7c: the rest band mixes all three rune shapes");
        const int per[3] = {10, 10, 10};
        const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
        rw::Scene s; s.open = true; s.layout = &l; s.anim = rw::PresetAnim(rw::Preset::Off); s.skillClass = 1;
        static rw::Instance list[rw::MaxDraw];
        auto of = [&](uint32_t recipe, std::vector<rw::Instance>& got) { got.clear(); const int n = rw::Build(s, list); for (int i = 0; i < n; ++i) if (list[i].recipe == recipe) got.push_back(list[i]); };
        std::vector<rw::Instance> g;
        of(rw::RecipeTreeGlyph, g);
        bool placed = g.size() == 3;
        for (int t = 0; placed && t < 3; ++t)
            placed = g[size_t(t)].param == float(rw::TreeGlyphOf(1, t)) && std::fabs(g[size_t(t)].u0 - 16.f / 1.5f) < .01f &&
                     std::fabs(g[size_t(t)].cx - (rw::CanvasCx + 128 * std::cos(l.dir[size_t(t)]))) < .01f && std::fabs(g[size_t(t)].cy - (rw::CanvasCy + 128 * std::sin(l.dir[size_t(t)]))) < .01f;
        Check(placed, "R7c: one glyph per tree mark at r 128, the class's kind, 16 px tall (TS2)");
        {   // TS2: the Trees shown mark is 28 px: dark disc r 14, the 2 px accent ring's outer edge r 14 (centerline 13)
            const float x = rw::CanvasCx + 128 * std::cos(l.dir[0]), y = rw::CanvasCy + 128 * std::sin(l.dir[0]);
            int disc = 0, ring = 0; const int n = rw::Build(s, list);
            for (int i = 0; i < n; ++i) if (std::fabs(list[i].cx - x) < .01f && std::fabs(list[i].cy - y) < .01f) {
                if (list[i].recipe == rw::RecipeDisc && list[i].w == 28) ++disc;
                if (list[i].recipe == rw::RecipeRing && list[i].w == 28 && list[i].param == 2) ++ring;
            }
            Check(disc == 1 && ring == 1, "R7c TS2: the mark's dark disc r 14 and its 2 px accent ring on r 13");
        }
        s.grow = .5f; of(rw::RecipeTreeGlyph, g); Check(g.empty(), "R7c: the glyphs fade with their marks (gone at half the grow)");
        s.grow = -1; s.skillClass = -1; of(rw::RecipeTreeGlyph, g); Check(g.empty(), "R7c: no class, plain marks");
        s.skillClass = 1;
        of(rw::RecipeRunes, g);
        Check(g.size() == 1 && g[0].param == 36 && g[0].u0 == rw::CenterR && g[0].v0 == 0 && std::fabs(g[0].w - 2 * rw::CenterR) < .01f, "R7c: at rest one band, 36 runes seeded by r 80, on the outer ring");
        of(rw::RecipeRing, g);
        int rings2 = 0; for (const auto& i : g) rings2 += i.param == 2 && (i.w == 2 * rw::CenterR || i.w == 2 * (rw::CenterR - 11));
        Check(rings2 == 2, "R7c TS4: both center rings 2 px");
        s.grow = 1; of(rw::RecipeRunes, g);
        Check(g.size() == 1 && g[0].param == 63 && g[0].u0 == rw::CenterGrownR && std::fabs(g[0].w - 2 * rw::CenterGrownR) < .01f, "R7c: grown, the r 136 band (63 runes)");
        s.grow = .5f; of(rw::RecipeRunes, g);
        Check(g.size() == 2 && std::fabs(g[0].a - g[1].a) < .001f, "R7c: halfway, the two bands crossfade");
        s.grow = 1; s.step.under = {rw::Kind::Glyphs, 0}; of(rw::RecipeRunes, g);
        Check(g.size() == 1 && g[0].v0 == 2, "R7c: Glyphs pointed, the bottom half's runes dim");
        s.step.under = {rw::Kind::Gestures, 0}; of(rw::RecipeRunes, g);
        Check(g.size() == 1 && g[0].v0 == 1, "R7c: Gestures pointed, the top half's runes dim");
        const auto o = ClassLayout(3, per, rw::TreeLayout::Orbs);
        s.layout = &o; s.skillClass = 3; s.grow = -1; s.step.under = {}; of(rw::RecipeTreeGlyph, g);
        Check(g.size() == 3 && std::fabs(g[0].u0 - .8f * 34) < .01f, "R7c: Tree orbs, the glyph inside each orb (0.8 of r 34)");
    }
    {   // Round 2026-11-08 #1, the player's rule for closing (ring_wheel_close.h). Upstream's real HandControls and interaction::Engine,
        // fed as transport feeds them (handInput.active, interactionInput.tracked, the close at transport:4760), through closing::Hold.
        namespace sk = framevr::skills;
        namespace cl = rw::closing;
        namespace lootaim = framevr::lootaim;
        struct Frame {
            sk::HandControls hc; sk::HandInput hi; sk::HandOutput ho; ia::Engine ie; ia::Input ii; ia::Output io; cl::Hold hold; cl::Hold::HandOut out;
            bool labOn = true, tracked[2]{true, true}, aim[2]{true, true}, local = true, restored = true;
            uint32_t gate = 0;
            uint64_t context = 1, session = 10, epoch = 1, snapshotAge = 0;
            double real = 100, shownHand = 0, shownEngine = 0;
            Frame() {
                hi.layout = 1; ii.head.p = {0, 1.7f, 0};
                ii.hand[0].p = {.25f, 1.3f, -.4f}; ii.hand[1].p = {-.25f, 1.3f, -.4f}; ii.stage.known = true;
                Step(3);
            }
            void One(double dt) {
                real += dt;
                hi.time = real; hi.context = context;
                hi.active = !gate && local && tracked[0] && tracked[1] && aim[0] && aim[1];   // transport handInput.active
                out = {};
                if (labOn) out = hold.Hand(hi.time, hi.context, hi.active, hc.Wheel() >= 0, session, epoch, gate, local);
                shownHand = hi.time;
                ho = hc.Step(hi);
                const bool active = !gate && local;
                ii.wheel = ho.wheel >= 0; ii.wheelHand = ho.wheel; ii.active = active; ii.session = session; ii.epoch = epoch;
                for (int h = 0; h < 2; ++h) ii.tracked[h] = tracked[h];
                ii.time = real; ii.at = 1000000 + uint64_t(real * 1000); ii.snapshot.at = ii.at - snapshotAge; ii.snapshot.session = session;
                if (labOn) hold.Engine(ii, ho.wheel >= 0);
                shownEngine = ii.time;
                io = ie.Step(ii);
                if (labOn) hold.Restore(ii);
                if (ii.time != real || ii.epoch != epoch || ii.active != active || ii.tracked[0] != tracked[0] || ii.tracked[1] != tracked[1]) restored = false;
                if (ho.wheel >= 0 && !io.wheel) { hc.Close(); ho = {}; }   // transport.cpp:4760
            }
            void Step(int n = 1) { while (n--) One(.02); }
            void Click(int h) { hi.click[h] = true; Step(); hi.click[h] = false; Step(); }
            bool Open(int h) const { return hc.Wheel() == h && io.wheel; }
        };
        const float nan = std::nanf("");
        for (int hand = 0; hand < 2; ++hand) for (int how = 0; how < 4; ++how) {   // a hand gone, its aim alone, both hands, the headset
            static const char* const names[4] = {"the hand", "its aim alone", "both hands", "headset tracking"};
            Frame f; f.Click(hand); f.Step(60);
            const bool was = f.Open(hand);
            if (how == 0) { f.tracked[hand] = f.aim[hand] = false; f.ii.hand[hand].p.x = nan; }
            if (how == 1) f.aim[hand] = false;
            if (how == 2) { f.tracked[0] = f.tracked[1] = f.aim[0] = f.aim[1] = false; f.ii.hand[0].p.x = f.ii.hand[1].p.x = nan; }
            if (how == 3) { f.gate = lootaim::GateHead; f.ii.head.p.x = nan; }
            int open = 0;
            for (int i = 0; i < 300; ++i) { f.Step(); open += f.Open(hand); }
            Check(was && open == 300, std::string("#1 lost, the wheel stays open: ") + names[how] + " (" + std::to_string(open) + " of 300 frames)");
            Check(f.restored && !std::isfinite(how == 3 ? f.ii.head.p.x : how == 1 ? nan : f.ii.hand[hand].p.x), std::string("#1 the engine's input is put back after the hold: ") + names[how]);
            f.Click(hand);
            Check(f.hc.Wheel() == -1, std::string("#1 G: the stick click still closes: ") + names[how]);
        }
        for (const double gap : {.101, .106, .15, .5, 5.0, 3600.0}) {   // a hitch of any length, both gates
            const std::string g = std::to_string(int(gap * 1000)) + " ms";
            Frame f; f.Click(1); f.Step(60);
            const double a = f.shownHand;
            f.One(gap);
            Check(f.Open(1) && std::fabs(f.out.gapS - gap) < 1e-6, "#1 a " + g + " gap: the wheel stays open, HandControls and the engine");
            Check(f.shownHand > a && f.shownHand - a <= cl::MaxStepS + 1e-9 && f.shownEngine == f.shownHand, "#1 a " + g + " gap: one step under both gates");
            const double b = f.shownHand;
            f.Step();
            Check(f.Open(1) && f.shownHand > b && f.shownHand - b < .03 && f.out.gapS == 0, "#1 the frame after a " + g + " gap: no second jump");
            const double off = f.hold.Offset(); bool same = true; double was = f.shownHand, real = f.real;
            auto stepSame = [&](double dt) { f.One(dt); same = same && std::fabs((f.shownHand - was) - (f.real - real)) < 1e-6 && f.shownEngine == f.shownHand && f.hold.Offset() == off; was = f.shownHand; real = f.real; };
            f.hi.click[1] = true; stepSame(.02); f.hi.click[1] = false; stepSame(.02);   // the close itself: no snap back to real time
            Check(f.hc.Wheel() == -1 && same, "#1 after a " + g + " gap: a stick click closes, the close frame steps as real time");
            for (int i = 0; i < 200; ++i) stepSame(i % 40 == 39 ? .5 : .02);   // closed: every step and every real gap as upstream's
            Check(same && std::fabs(f.real - f.shownHand - off) < 1e-6 && f.restored, "#1 after a " + g + " gap, closed: a constant bias, steps and gaps exactly upstream's");
            f.Click(1); f.Step(5);
            Check(f.Open(1), "#1 after a " + g + " gap: the reopen is normal");
        }
        {   // ring Off: upstream's rules, as before
            Frame f; f.labOn = false; f.Click(1); f.Step(60); f.One(.101);
            Check(f.hc.Wheel() == -1, "#1 ring Off: a 101 ms gap closes (upstream)");
            Frame g; g.labOn = false; g.Click(1); g.Step(60); g.One(.09);
            Check(g.Open(1), "#1 ring Off: a 90 ms gap stays open (upstream)");
            Frame h; h.labOn = false; h.Click(1); h.Step(60); h.tracked[0] = false; h.Step();
            Check(h.hc.Wheel() == -1, "#1 ring Off: a lost hand closes (upstream)");
        }
        {   // the wheel closed, no offset: every value is upstream's; no other user of HandControls sees a change
            Frame f; bool same = true;
            for (int i = 0; i < 200; ++i) {
                f.gate = i % 11 == 0 ? lootaim::GateHead : 0; f.context = 1 + i / 20;
                f.One(.014 + (i % 7) * .05);
                same = same && f.shownHand == f.real && f.shownEngine == f.real && f.hi.context == f.context && f.hi.active == !f.gate && f.restored;
            }
            Check(same, "#1 closed: time, context and active are upstream's, gaps included");
        }
        {   // the bias ends only at a new game session and the Off edge, and neither misfires
            auto hitched = [](Frame& f) { f.Click(1); f.Step(60); f.One(5.0); f.Step(5); f.Click(1); f.Step(5); };
            {   Frame f; hitched(f); const double off = f.hold.Offset();
                f.gate = lootaim::GatePanel; f.Step(5); f.gate = 0; f.Step(5);                   // a panel, closed
                f.context = 2; f.hi.layout = 2; f.Step(5); f.epoch = 2; f.Step(5); f.One(1.0); f.Step(5);   // gear, recenter, a closed gap
                f.Click(1); f.Step(5); f.gate = lootaim::GateMenu; f.Step(5); f.gate = 0; f.Step(5);        // a reopen and a safety close
                f.ii.snapshot.vitalsKnown = true; f.Step(5);
                Check(off > 4 && f.hold.Offset() == off, "#1 the bias stays through panels, gear, recenter, gaps, reopens and safety closes"); }
            {   Frame f; hitched(f); f.session = 11; f.context = 99; f.One(.02);
                Check(f.hold.Offset() == 0 && f.shownHand == f.real && f.shownEngine == f.real, "#1 a new game session ends the bias on the frame upstream resets anyway");
                f.Step(3); f.Click(1); f.Step(5);
                Check(f.Open(1) && f.shownHand == f.real, "#1 after a new session: the wheel opens normally, real time"); }
            {   Frame f; hitched(f); f.labOn = false; f.hold.Reset(); f.One(.02);
                Check(f.hc.Wheel() == -1 && f.hi.time == f.real && f.ii.time == f.real, "#1 the Off edge, wheel closed: real time at once");
                f.Step(3); f.Click(1); f.Step(5);
                Check(f.Open(1), "#1 the Off edge: one frame only, the wheel opens normally after it"); }
            {   Frame f; f.Click(1); f.Step(60); f.One(5.0); f.Step(5); f.labOn = false; f.hold.Reset(); f.One(.02); f.Step(3);
                Check(f.hc.Wheel() == -1, "#1 the Off edge with the wheel open: closes (upstream's gap), cannot stick open"); }
            {   Frame f; hitched(f); f.hold.Reset(); f.One(.02);
                Check(f.hold.Offset() == 0 && f.shownHand == f.real, "#1 On again after a reset: real time, no bias"); }
            {   Frame f; f.Click(1); f.Step(60); const double before = f.real; f.Step(30); (void)before;
                Check(f.hold.Offset() == 0 && f.shownHand == f.real, "#1 no hitch, wheel open: exactly real time, no drift"); }
        }
        for (const double gap : {500.0, 1200.0, 5000.0}) for (int letGo = 0; letGo < 2; ++letGo) {   // review M1: a hitch is not a long hold (the real QuickCast)
            const std::string g = std::to_string(int(gap)) + " ms" + (letGo ? ", let go in the hitch" : ", held through");
            const int per[3] = {4, 4, 4};
            const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
            cl::Hold h; rw::QuickCast qc; double t = 100;
            auto frame = [&](double dt, int press, bool down) {
                t += dt; double shown = t; uint64_t ctx = 1; bool active = true;
                h.Hand(shown, ctx, active, true, 1, 1, 0, true);
                rw::QuickIn q; q.nowMs = h.NowMs(t * 1000); q.main = true; q.under = {rw::Kind::Seat, 0}; q.press[0] = press; q.down[0][0] = down; q.holdMs = 1000; q.tapMs = 350;
                return qc.Step(q, l).event;
            };
            for (int i = 0; i < 30; ++i) frame(.011, -1, false);
            frame(.011, 0, true); for (int i = 0; i < 5; ++i) frame(.011, -1, true);
            const auto after = frame(gap / 1000, -1, !letGo);
            if (letGo) Check(after == rw::Quick::QuickCast, "#1 M1 " + g + ": a tap stays a quick cast");
            else {
                Check(after == rw::Quick::None, "#1 M1 " + g + ": no assign from the hitch");
                auto ev = rw::Quick::None; int n = 0;
                while (ev == rw::Quick::None && n < 400) { ev = frame(.011, -1, true); ++n; }
                Check(ev == rw::Quick::Assign && n * 11 + 90 + 66 >= 1000 && n * 11 + 90 + 66 <= 1030, "#1 M1 " + g + ": a real hold assigns at the full time of shown frames (" + std::to_string(n) + " frames)");
            }
        }
        {   // Merge check M1 (ff4007a8): the Glyphs half's "click spent" test and the hold it reads share one clock. As the ring's Input seam:
            // the test first, on last frame's QuickOut, then QuickCast on the shown clock. A hitch leaves an offset until the session ends.
            struct Tap { bool spent = false, tapped = false; };
            auto run = [&](double earlierHitch, double hitchDuring, int heldFrames) {
                const int per[3] = {4, 4, 4};
                const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
                cl::Hold h; rw::QuickCast qc; rw::QuickOut last; double t = 100; Tap out;
                auto frame = [&](double dt, int press, bool down) {
                    t += dt; double shown = t; uint64_t ctx = 1; bool active = true;
                    h.Hand(shown, ctx, active, true, 1, 1, 0, true);
                    out.spent |= rw::GlyphClickSpent(last, 0, 1000, 350);
                    rw::QuickIn q; q.nowMs = h.NowMs(t * 1000); q.main = true; q.under = {rw::Kind::Glyphs}; q.press[0] = press; q.down[0][0] = down; q.holdMs = 1000; q.tapMs = 350;
                    last = qc.Step(q, l);
                    out.tapped |= last.event == rw::Quick::GlyphTap;
                };
                for (int i = 0; i < 30; ++i) frame(.011, -1, false);
                if (earlierHitch > 0) frame(earlierHitch, -1, false);
                for (int i = 0; i < 30; ++i) frame(.011, -1, false);
                frame(.011, 0, true);
                for (int i = 0; i < heldFrames; ++i) frame(.011, -1, true);
                if (hitchDuring > 0) frame(hitchDuring, -1, true);
                frame(.011, -1, false);
                return out;
            };
            const auto calm = run(0, 0, 1);
            Check(!calm.spent && calm.tapped, "M1 no hitch, a 22 ms trigger tap on the Glyphs half: the click stands");
            const auto after = run(1, 0, 1);
            Check(!after.spent && after.tapped, "M1 an earlier 1 s hitch, then a 22 ms trigger tap on the Glyphs half: the click stands");
            const auto during = run(0, 1, 1);
            Check(!during.spent && during.tapped, "M1 a 1 s hitch during a tap on the Glyphs half: a hitch is not a long hold");
            const auto held = run(1, 0, 37);
            Check(held.spent && !held.tapped, "M1 an earlier 1 s hitch, then a real 407 ms hold on the Glyphs half: the click is spent");
        }
        {   // the clock itself: a real clock that stalls or steps back; the Off edge
            cl::Clock c; double t = 5;
            c.Step(t, true); const double a = c.Step(t, true).shown, b = c.Step(t - .5, true).shown;
            Check(b > a, "#1 a real clock that stalls or steps back: still strictly increasing");
            Check(cl::MaxStepS < .1 && cl::MaxStepS < .15, "#1 the step is under both gates (.1 HandControls, .15 the engine)");
            cl::Hold h; double tt = 5; uint64_t ctx = 1; bool act = true;
            h.Hand(tt, ctx, act, true, 1, 1, 0, true); tt = 50; h.Hand(tt, ctx, act, true, 1, 1, 0, true);
            Check(tt < 50 && h.Offset() > 0, "#1 the hold builds an offset");
            h.Reset(); tt = 77; h.Hand(tt, ctx, act, false, 1, 1, 0, true);
            Check(h.Offset() == 0 && tt == 77, "#1 the Off edge: back on real time at once");
        }
        {   // D: weapon set and gear (context, layout), recenter and eye height (epoch): open, then upstream's values again
            Frame f; f.Click(1); f.Step(60);
            f.context = 2; f.hi.layout = 2; f.Step(30);
            Check(f.Open(1), "#1 D: a weapon or gear change: the wheel stays open");
            f.epoch = 2; f.Step();
            Check(f.out.moved && f.Open(1), "#1 D: a recenter or eye height: the wheel stays open and is told to place itself again");
            f.Step(30);
            Check(!f.out.moved && f.Open(1) && f.restored, "#1 D: the place is asked for once");
            f.Click(1); f.Step();
            Check(f.hc.Wheel() == -1 && f.hi.context == f.context, "#1 D: closed, upstream's context again");
            const ia::Pose h0{{0, 1.6f, 0}, {0, 0, 0, 1}}, h1{{3, 1.2f, 2}, {0, 0, 0, 1}};
            rw::Placement pl; pl.distanceM = .9f; pl.widthM = .8f; pl.belowDeg = 8; pl.worldLock = true;
            rw::Placer pr; pr.Step(1, h0, pl);
            const auto kept = pr.Step(1, h1, pl);
            pr.Reset();
            const auto again = pr.Step(1, h1, pl);
            auto dist = [](const framevr::math::Vec& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
            Check(pr.Spawned && dist(again.p - h1.p) < 1 && dist(kept.p - h1.p) > 2, "#1 D: after a recenter the wheel spawns in reach of the new head");
        }
        {   // a real reset still closes: a new session, each safety reason, bad controller values
            Frame f; f.Click(1); f.Step(60); f.session = 11; f.context = 99; f.Step();
            Check(f.hc.Wheel() == -1 && f.hi.context == 99, "#1 a new session closes, upstream's context shown at once");
            using namespace lootaim;
            struct R { uint32_t g; const char* w; } rows[] = {{GateWorld, "load"}, {GatePanel, "panel"}, {GateSpatial, "panel"}, {GateSettings, "panel"}, {GateInventory, "inventory-button"},
                {GateMenu, "menu-button"}, {GateChord, "settings-chord"}, {GateControls, "gameplay-off"}, {GateChain, "gameplay-off"}};
            for (const auto& r : rows) {
                Frame s; s.Click(1); s.Step(60); s.gate = r.g | GateHead; s.Step();
                Check(s.hc.Wheel() == -1 && s.hold.Safety() && std::string(s.hold.Safety()) == r.w, std::string("#1 a safety reason closes, named: ") + r.w);
            }
            Frame p; p.Click(1); p.Step(60); p.local = false; p.Step();
            Check(p.hc.Wheel() == -1 && p.hold.Safety() && std::string(p.hold.Safety()) == "session", "#1 no local player closes, named: session");
            Frame b; b.Click(1); b.Step(60); b.hi.trigger[0] = nan; b.Step();
            Check(b.hc.Wheel() == -1, "#1 bad controller values close");
            Check(!cl::SafetyOf(0, true) && !cl::SafetyOf(GateHead, true), "#1 headset tracking alone is no safety reason");
        }
        for (int c = 0; c < 4; ++c) {   // death: only known, fresh vitals at or under zero
            static const char* const names[4] = {"known, fresh, dead", "not known", "stale", "alive"};
            Frame f; f.Click(1); f.Step(60);
            f.ii.snapshot.vitalsKnown = c != 1; f.ii.snapshot.health = c == 3 ? 40.f : 0.f; f.snapshotAge = c == 2 ? 2000 : 0;
            f.Step(30);
            const bool closed = f.hc.Wheel() == -1;
            Check(c == 0 ? closed : !closed, std::string("#1 death: ") + names[c]);
            if (c == 0) Check(f.Open(1) == false, "#1 death: closed for the engine too");
        }
        {   // death names itself the frame it closes
            Frame f; f.Click(1); f.Step(60); f.ii.snapshot.vitalsKnown = true; f.ii.snapshot.health = 0; f.Step(2);
            Check(f.hc.Wheel() == -1 && f.hold.Safety() && std::string(f.hold.Safety()) == "death", "#1 death: reason=death");
        }
        {   // the focus blip: under 250 ms holds; 250 closes; ending never holds; closed never holds; focus starts it over
            cl::FocusBlip b;
            Check(b.Step(true, 1000, false) && b.Step(true, 1249, false) && !b.Step(true, 1250, false), "#1 unfocused: held under 250 ms, then closes");
            Check(!b.Step(false, 2000, false) && !b.Step(true, 3000, true), "#1 a closed wheel or a session ending is never held");
            Check(b.Step(true, 4000, false), "#1 a new blip holds"); b.Focused();
            Check(b.Step(true, 9000, false), "#1 back in focus: the next blip starts over");
        }
        {   // B: only a fresh press cancels (SlotPress state as the ring keeps it; right B is index 2)
            const int per[3] = {4, 4, 4};
            const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
            rw::QuickCast qc; rw::QuickIn q; q.main = true; q.under = {};
            bool held[3] = {true, true, true};   // the ring's state at an open: everything counts as held
            auto frame = [&](double ms, bool down) { const bool d[3] = {false, false, down}; q.nowMs = ms; q.press[0] = rw::SlotPress(d, false, held); return qc.Step(q, l).event; };
            Check(frame(1000, true) == rw::Quick::None, "#1 B held at the open: no close");
            Check(frame(6000, true) == rw::Quick::None, "#1 B held through a 5 s gap: no close");
            Check(frame(6020, false) == rw::Quick::None, "#1 B released: no close");
            Check(frame(9000, true) == rw::Quick::Close, "#1 a real press after a 3 s gap closes");
        }
        {   // Audit 2026-11-10 bug 3: a footer tab clicked on upstream's own page names the close. Upstream's real engine reads the
            // hover and trigger; rw::FooterClick reads the same two after it (the Request seam). The name must come on exactly
            // the frames that engine closes the wheel for Inventory, VR Settings or the DEV window, and on no other close.
            struct Run {
                Frame f; rw::FooterClick fc; rw::WheelEdges edges; bool developer = false;
                std::string reason = "(no close)", named, did; int wrong = 0, closes = 0, tabCloses = 0, ignored = 0;
                explicit Run(bool dev) : developer(dev) { f.ii.snapshot.developer = dev; f.ii.snapshot.bindingsEditable = true; f.Click(1); f.Step(5); }
                void One(int hover, float trigger) {
                    const bool none[2]{};
                    const auto e = edges.Frame(f.hc.Wheel(), none, nullptr, 10);   // the Input seam, before the engine steps
                    if (e.kind == rw::WheelEdges::Closed) { reason = e.reason; ++closes; tabCloses += reason.rfind("footer-", 0) == 0; }
                    const bool open = f.hc.Wheel() >= 0;
                    const int page = int(f.ie.Page());
                    f.ii.hover = hover; f.ii.trigger = trigger;
                    f.Step();
                    const char* tab = fc.Step(open, page, f.ii.hover, f.ii.trigger, developer);   // the Request seam, after it
                    if (tab) { edges.Wrote(tab); named = tab; }
                    const bool shut = open && !f.io.wheel;
                    const char* now = !shut ? nullptr : f.io.inventory ? "footer-inventory" : f.io.settings ? "footer-vr-settings" : nullptr;
                    if (now) did = now;                                      // what upstream's engine itself says it closed for
                    if (now && (!tab || std::string(tab) != now)) ++wrong;   // upstream closed for a tab, and it was not named
                    if (tab && shut && !now) ++wrong;                        // a name on some other close
                    if (tab && !shut) ++ignored;                             // upstream ignored the press: the name lapses in a frame
                }
                void Click(int cell) { One(cell, 0); One(cell, 1); One(cell, 0); }
                std::string After() { One(-1, 0); return reason; }
            };
            auto closeOn = [&](int pageTab, int cell, bool dev) { Run r(dev); if (pageTab >= 0) r.Click(pageTab); r.Click(cell); const std::string why = r.After(); return r.wrong ? std::string("(wrong)") : why; };
            {
                Run r(false); r.Click(12);
                Check(r.f.ie.Page() == ia::MenuPage::Map && r.f.Open(1), "bug 3: the Map tab opens upstream's Map page (the fixture)");
                r.Click(11);
                Check(r.did == "footer-inventory" && r.f.hc.Wheel() < 0, "bug 3: upstream's engine closes the wheel for Inventory on its Map page");
                Check(r.After() == "footer-inventory" && r.closes == 1 && !r.wrong, "bug 3: Inventory clicked on the Map page: footer-inventory, not upstream (" + r.reason + ")");
            }
            Check(closeOn(12, 10, false) == "footer-vr-settings", "bug 3: VR Settings clicked on the Map page: footer-vr-settings (" + closeOn(12, 10, false) + ")");
            Check(closeOn(9, 11, false) == "footer-inventory" && closeOn(9, 10, true) == "footer-vr-settings", "bug 3: the same on the Equipment page");
            Check(closeOn(-1, 11, false) == "footer-inventory", "bug 3: and on upstream's Spells page (the ring not drawn)");
            Check(closeOn(9, 8, false) == "upstream", "bug 3: the center clicked on the Equipment page stays upstream's own close (" + closeOn(9, 8, false) + ")");
            {   // a tab that changes the page closes nothing and names nothing
                Run r(true); r.Click(12); r.Click(9); r.Click(13);
                Check(r.f.Open(1) && r.named.empty() && !r.closes, "bug 3: Map, Equipment and DEV page tabs: no close, no name");
            }
            {   // a trigger held from before the ray reached the tab is no click, for upstream and for the name
                Run r(false); r.Click(12); r.One(12, 1); r.One(11, 1); r.One(11, 1);
                Check(r.f.Open(1) && r.named.empty(), "bug 3: a held trigger moved onto Inventory: no click, no name");
                r.One(11, .3f); r.One(11, .6f); r.One(11, .3f); r.One(11, 1);   // still held by upstream's own rule (let go under .2)
                Check(r.f.Open(1) && r.named.empty() && !r.ignored, "bug 3: a trigger eased to 30% and pulled again is one hold (upstream's .2), no second press");
                r.One(11, 0); r.One(11, 1);
                Check(r.After() == "footer-inventory", "bug 3: let go and pressed again: the click");
            }
            {   // an editor page has no closing tab under those cells
                Run r(false); r.Click(0);
                const bool editing = ia::Editing(r.f.ie.Page());
                r.Click(10); r.Click(11);
                Check(editing && r.named.empty() && !r.wrong && r.did.empty(), "bug 3: an editor page: its cells 10 and 11 are never named as footer tabs (page " + std::to_string(int(r.f.ie.Page())) + ", editing " + std::to_string(editing) + ")");
            }
            {   // a name upstream ignored lapses: the close two frames later is not the tab's
                rw::WheelEdges e; rw::FooterClick fc; const bool none[2]{}, left[2]{false, true};
                e.Frame(1, none, nullptr, 10);
                if (const char* tab = fc.Step(true, int(ia::MenuPage::Map), 11, 1, false)) e.Wrote(tab);
                e.Frame(1, none, nullptr, 10);
                Check(e.Frame(-1, left, nullptr, 10).reason == "stick-left", "bug 3: a press upstream ignored names no later close");
                Check(!fc.Step(false, int(ia::MenuPage::Spells), 11, 0, false) && !fc.Step(false, int(ia::MenuPage::Spells), 11, 1, false),
                      "bug 3: the ring's own page: nothing (the ring's own write names that close)");
            }
            {   // the ring On cost of the one step this adds to every frame (the Request seam): printed for the perf gate
                rw::FooterClick fc; unsigned seed = 7; int named = 0;
                const int n = 4000000;
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < n; ++i) {
                    seed = seed * 1664525u + 1013904223u;
                    named += fc.Step((seed >> 8) & 1, int(ia::MenuPage::Map), 8 + int((seed >> 12) % 6), float((seed >> 20) & 3) * .34f, false) != nullptr;
                }
                const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / n;
                std::printf("bug 3 cost: FooterClick::Step %.1f ns a frame (%d named of %d)\n", ns, named, n);
                Check(named > 0, "bug 3 cost: the timed loop ran");
            }
            {   // many frames of random pointing and pressing on upstream's pages: every tab close named, no other close named
                unsigned seed = 12345; auto rnd = [&](unsigned n) { seed = seed * 1664525u + 1013904223u; return (seed >> 10) % n; };
                int tabCloses = 0, ignored = 0, wrong = 0, closes = 0;
                for (int dev = 0; dev < 2; ++dev) {
                    Run r(dev != 0);
                    static const int cells[] = {-1, 8, 9, 10, 11, 12, 13, 14};
                    static const float pulls[] = {0, 0, .1f, .3f, .6f, 1, 1};
                    int hover = -1; float pull = 0;
                    for (int i = 0; i < 40000; ++i) {
                        if (r.f.hc.Wheel() < 0) { r.One(-1, 0); r.f.Click(1); r.f.Step(3); r.One(-1, 0); continue; }
                        if (!rnd(4)) hover = cells[rnd(8)];
                        if (!rnd(3)) pull = pulls[rnd(7)];
                        r.One(hover, pull);
                    }
                    ignored += r.ignored; wrong += r.wrong; tabCloses += r.tabCloses; closes += r.closes;
                }
                std::printf("bug 3 sweep: %d closes, %d named for a footer tab, %d wrong, %d presses upstream ignored\n", closes, tabCloses, wrong, ignored);
                Check(wrong == 0 && ignored == 0 && tabCloses > 300 && closes > tabCloses, "bug 3: 80000 random frames on upstream's pages: " + std::to_string(tabCloses) + " of " + std::to_string(closes) +
                      " closes named for a tab, " + std::to_string(wrong) + " wrong, " + std::to_string(ignored) + " named presses upstream ignored");
            }
        }
    }
    {   // Review-bug9 M1: each close reason. the ring writes at frame k-1 (after that frame's Frame call), the close edge shows at
        // frame k; a write from two frames back is stale (the old code read that one, so every ring close said upstream).
        const bool none[2]{}, left[2]{false, true}, right[2]{true, false};
        auto close = [&](const char* wrote, int staleFrames, const bool* click, const char* safety, uint64_t sessionAtClose) {
            rw::WheelEdges e;
            const bool* c0 = none;
            Check(e.Frame(1, c0, nullptr, 10).kind == rw::WheelEdges::Opened, "M1: the open edge");
            if (wrote) e.Wrote(wrote);
            for (int i = 0; i < staleFrames; ++i) e.Frame(1, none, nullptr, 10);
            const auto r = e.Frame(-1, click, safety, sessionAtClose);
            return r.kind == rw::WheelEdges::Closed ? r.reason : std::string("(no close)");
        };
        for (const char* why : {"center", "back", "glyphs", "footer-inventory", "quick-cast", "cancel-off-wheel"})
            Check(close(why, 0, none, nullptr, 10) == why, std::string("M1: a ring write the frame before the close names it: ") + why + " (got " + close(why, 0, none, nullptr, 10) + ")");
        Check(close("center", 1, none, nullptr, 10) == "upstream", "M1: a write two frames back is not the reason");
        Check(close("center", 0, left, "panel", 11) == "center", "M1: the ring's write wins over a click, a safety reason and a session");
        Check(close(nullptr, 0, left, nullptr, 10) == "stick-left" && close(nullptr, 0, right, nullptr, 10) == "stick-right", "M1: a stick click edge");
        Check(close(nullptr, 0, none, "load", 11) == "load", "#1: a safety reason is named ahead of the session");
        Check(close(nullptr, 0, none, nullptr, 11) == "session", "M1: a new session");
        Check(close(nullptr, 0, none, nullptr, 10) == "upstream", "M1: none of the above: upstream");
        rw::WheelEdges e;
        Check(e.Frame(0, none, nullptr, 10).kind == rw::WheelEdges::Opened && e.Frame(1, none, nullptr, 10).kind == rw::WheelEdges::Switched &&
              e.Frame(1, none, nullptr, 10).kind == rw::WheelEdges::None && e.Open(), "M1: switched hands; a steady frame is no edge");
        e.Off();
        Check(!e.Open() && e.Frame(-1, none, nullptr, 10).kind == rw::WheelEdges::None, "M1: after the ring's Off edge no second close is logged");
    }
    {   // Audit 2026-11-10 bug 2: the input cleared outside a focused frame closes the wheel there, and its line is written there.
        const bool none[2]{}, left[2]{false, true};
        auto opened = [&] { rw::WheelEdges e; e.Frame(1, none, nullptr, 10); return e; };
        for (const char* why : {"headset-focus", "session-end", "xr-sync-failed", "copy-failed"}) {
            auto e = opened();
            const auto c = e.Cleared(why, false);
            Check(c.kind == rw::WheelEdges::Closed && c.reason == why && !e.Open(), std::string("bug 2: the clear closes the open wheel and names it: ") + why);
            Check(e.Frame(-1, none, nullptr, 10).kind == rw::WheelEdges::None && e.Frame(-1, left, "panel", 11).kind == rw::WheelEdges::None,
                  std::string("bug 2: the next focused frames log no second close: ") + why);
            Check(e.Cleared(why, false).kind == rw::WheelEdges::None, std::string("bug 2: every later unfocused frame clears again and says nothing: ") + why);
            Check(e.Frame(0, none, nullptr, 10).kind == rw::WheelEdges::Opened && e.Frame(-1, left, nullptr, 10).reason == "stick-left", std::string("bug 2: the reopen and its close are normal: ") + why);
        }
        { auto e = opened(); Check(e.Cleared("headset-focus", false).kind == rw::WheelEdges::Closed && rw::WheelEdges().Cleared("headset-focus", false).kind == rw::WheelEdges::None, "bug 2: a closed wheel: no line"); }
        { auto e = opened(); e.Wrote("quick-cast"); Check(e.Cleared("headset-focus", true).reason == "quick-cast", "bug 2: the ring's own close one frame before the clear keeps its name"); }
        { auto e = opened(); Check(e.Cleared("headset-focus", true).reason == "upstream", "bug 2: upstream closed the wheel one frame before the clear: not the clear's name"); }
        { auto e = opened(); e.Wrote("center"); e.Frame(1, none, nullptr, 10); Check(e.Cleared("session-end", false).reason == "session-end", "bug 2: a stale write is not the reason"); }
        // Review N1: no Frame runs during a focus blip, so a write upstream ignored on the last focused frame is still there at the
        // clear. The wheel was still open at the Blip (shutBefore false), so the write closed nothing and must not name the clear.
        for (const char* ignored : {"footer-inventory", "center", "quick-cast"}) {
            auto e = opened(); e.Wrote(ignored);
            const std::string got = e.Cleared("headset-focus", false).reason;
            Check(got == "headset-focus", std::string("N1: a write upstream ignored on the last focused frame does not name the clear: ") + ignored + " (got " + got + ")");
            Check(e.Frame(0, none, nullptr, 10).kind == rw::WheelEdges::Opened && e.Frame(-1, none, nullptr, 10).reason == "upstream", std::string("N1: that write is gone after the clear: ") + ignored);
        }
        { auto e = opened(); e.Wrote("center"); Check(e.Cleared("xr-sync-failed", false).reason == "xr-sync-failed" && opened().Cleared("copy-failed", false).reason == "copy-failed", "N1: the same for a failed sync; a clear with no write is unchanged"); }
        Check(rw::ClosedLine("headset-focus", 12.34, "Spells") == "Frame ring wheel: closed reason=headset-focus after 12.3 s, page Spells", "bug 2: the close line's words are unchanged (" + rw::ClosedLine("headset-focus", 12.34, "Spells") + ")");
        {   // the ring's two handlers in transport's order, 11 ms frames: the headset off at 5 s for 1.5 s (Nov 10 4:45 PM), or for
            // good (Nov 8 11:04 AM, the game ended unfocused). Blip: FocusBlip, then the name; Cleared: OnClearInput in ClearInput.
            auto run = [&](double backAtMs, bool sessionStops, std::vector<std::string>& lines, const char* ignoredWrite = nullptr) {
                rw::WheelEdges e; rw::closing::FocusBlip blip; const char* why = nullptr; bool shut = false;
                int wheel = 1; const double openAt = 1000, offAt = 6000;
                auto say = [&](const rw::WheelEdges::Edge& x, double t) { if (x.kind == rw::WheelEdges::Closed) lines.push_back(rw::ClosedLine(x.reason.c_str(), (t - openAt) / 1000, "Spells")); };
                auto cleared = [&](double t) { const char* w = why ? why : "session-end"; const bool s = shut; why = nullptr; shut = false; if (e.Open()) say(e.Cleared(w, s), t); wheel = -1; };
                for (double t = openAt; t < 12000; t += 11) {
                    const bool focused = t < offAt || t >= backAtMs;
                    if (sessionStops && t >= offAt + 55 && t < offAt + 66) cleared(t);   // transport's state handler: STOPPING, ClearInput, no Blip
                    if (!focused) {
                        const bool hold = blip.Step(wheel >= 0, t, false);
                        if (e.Open() && !hold) { why = "headset-focus"; shut = wheel < 0; }
                        if (hold) continue;
                        cleared(t);
                        continue;
                    }
                    blip.Focused();
                    say(e.Frame(wheel, none, nullptr, 10), t);
                    if (ignoredWrite && t < offAt && t + 11 >= offAt) e.Wrote(ignoredWrite);   // N1: the last focused frame's write, which upstream ignored (the wheel stays open)
                }
            };
            std::vector<std::string> back, never, stops, blipOnly, wrote;
            run(7500, false, back); run(1e9, false, never); run(1e9, true, stops); run(6100, false, blipOnly); run(7500, false, wrote, "footer-inventory");
            Check(wrote.size() == 1 && wrote[0] == "Frame ring wheel: closed reason=headset-focus after 5.3 s, page Spells",
                  "N1: a footer press upstream ignored, then the headset off: still headset-focus, one line: " + (wrote.empty() ? std::string("none") : wrote[0]));
            Check(back.size() == 1 && back[0] == "Frame ring wheel: closed reason=headset-focus after 5.3 s, page Spells",
                  "bug 2: the headset off 1.5 s: one line, at the suspend (5.3 s open, not 6.5): " + (back.empty() ? std::string("none") : back[0]) + " x" + std::to_string(back.size()));
            Check(never.size() == 1 && never[0] == back[0], "bug 2: the headset never back on: the line is still written, once (" + std::to_string(never.size()) + ")");
            Check(stops.size() == 1 && stops[0] == "Frame ring wheel: closed reason=session-end after 5.1 s, page Spells", "bug 2: the session stopping with no Blip: session-end, at once: " + (stops.empty() ? std::string("none") : stops[0]));
            Check(blipOnly.empty(), "bug 2: a 100 ms blip writes no close (the wheel stays open)");
        }
    }
    {   // The trace for audit 2026-11-10 bug 6: the switch line carries the stick-click edges and the times
        const bool none[2]{}, left[2]{false, true}, right[2]{true, false}, both[2]{true, true};
        const std::string a = rw::SwitchedLine(1, left, 52.4, -1), b = rw::SwitchedLine(0, right, 300, 61.2);
        Check(a == "Frame ring wheel: switched to the left hand's wheel (stick clicks this frame: left; 52 ms after the open; the first switch of this open)", "bug 6 trace: the first switch: " + a);
        Check(b == "Frame ring wheel: switched to the right hand's wheel (stick clicks this frame: right; 300 ms after the open; 61 ms after the last switch)", "bug 6 trace: a later switch: " + b);
        Check(rw::SwitchedLine(1, both, 1, 1).find("this frame: both;") != std::string::npos && rw::SwitchedLine(1, none, 1, 1).find("this frame: none;") != std::string::npos, "bug 6 trace: both clicks, or none seen");
        Check(a.rfind("Frame ring wheel: switched to the left hand's wheel", 0) == 0 && rw::SwitchedLine(1, both, 3600000, 3600000).size() < 219, "bug 6 trace: the old words lead the line, and it fits its buffer");
    }
    {   // Audit 2026-11-10 bug 1: a skill put on a Draw glyph button says what the button loses
        namespace gc = framevr::glyphcast;
        const int x = rw::SlotOf(1, 1), b = rw::SlotOf(0, 2);   // left X (primary), right B (secondary)
        const std::array<int, 2> draws = gc::DrawSlots(float(x), -1);
        Check(draws[1] == x && draws[0] == -1, "bug 1: upstream's draw slot is the left hand's X (the fixture)");
        Check(rw::HeldBy(-1, draws, 1, 1) == rw::Held::DrawGlyph, "bug 1: no game skill, the hand's Draw glyph button: it holds Draw glyph");
        Check(rw::HeldBy(-1, draws, 1, 2) == rw::Held::None && rw::HeldBy(-1, draws, 0, 1) == rw::Held::None && rw::HeldBy(-1, {-1, -1}, 1, 1) == rw::Held::None, "bug 1: another button, the other hand, no draw button: none");
        Check(rw::HeldBy(40, draws, 1, 1) == rw::Held::Skill && rw::HeldBy(40, {-1, -1}, 0, 0) == rw::Held::Skill, "bug 1: a game skill is named ahead of it");
        Check(rw::HeldBy(-1, gc::DrawSlots(float(x), float(b)), 0, 2) == rw::Held::DrawGlyph && rw::HeldBy(-1, gc::DrawSlots(float(b), float(x)), 1, 1) == rw::Held::DrawGlyph, "bug 1: the second Draw glyph button, either key order");
        {   // against upstream's own test (glyphcast::DrawKeyHolding with the two [glyphs] values), every pair of values and every button
            int differ = 0, sameHand = 0, checked = 0;
            for (int first = -1; first < framevr::skills::Count; ++first) for (int second = -1; second < framevr::skills::Count; ++second) {
                const bool twoOnOne = first >= 0 && second >= 0 && framevr::skills::Hand(first) == framevr::skills::Hand(second) && first != second;
                const auto slots = gc::DrawSlots(float(first), float(second));
                for (int hand = 0; hand < 2; ++hand) for (int k = 0; k < rw::Slots; ++k) {
                    const int slot = rw::SlotOf(hand, k);
                    const bool up = gc::DrawKeyHolding(float(first), float(second), slot) >= 0, mine = rw::HeldBy(-1, slots, hand, k) == rw::Held::DrawGlyph;
                    ++checked;
                    if (up == mine) continue;
                    // the one difference: an INI edited by hand with both keys on one hand; the second key's button never draws
                    // (transport presses and upstream's wheel shows DrawSlots' one), so it reads none
                    if (twoOnOne && slot == second && !mine && slots[size_t(hand)] == first) ++sameHand; else ++differ;
                }
            }
            Check(differ == 0 && checked == 11 * 11 * 10, "bug 1: the same answer as upstream's DrawKeyHolding for every button and every pair of [glyphs] values (" + std::to_string(differ) + " differ)");
            Check(sameHand == 40, "bug 1: but for both keys on one hand, where the second never draws: " + std::to_string(sameHand) + " cases read none");
        }
        {   // the center's words
            const auto w = rw::HoldWordsFor(rw::Held::DrawGlyph, nullptr, 4);
            rw::QuickOut q;
            const auto h = rw::HoldLinesFor(q, w.bound, w.text.c_str());
            Check(h.top && std::string(h.top) == "Replaces" && std::string(h.line) == "Draw glyph" && !h.green, "bug 1: a skill held on a Draw glyph button: Replaces / Draw glyph, two lines");
            const auto none = rw::HoldWordsFor(rw::Held::None, nullptr, 4), skill = rw::HoldWordsFor(rw::Held::Skill, "Fire Wall", 4), lost = rw::HoldWordsFor(rw::Held::Skill, nullptr, 4);
            Check(!none.bound && none.text == "Hold to assign" && skill.bound && skill.text == "Fire Wall" && !lost.bound && lost.text == "Hold to assign", "bug 1: an empty button and a skill's button read as before");
            const auto same = rw::HoldWordsFor(rw::Held::DrawGlyph, nullptr, rw::GlyphsSeat), fresh = rw::HoldWordsFor(rw::Held::None, nullptr, rw::GlyphsSeat);
            Check(!same.bound && same.text == "Draw glyph" && !fresh.bound && fresh.text == "Draw glyph", "bug 1: the Glyphs half held on its own button loses nothing: one line");
            Check(rw::HoldWordsFor(rw::Held::Skill, "Hydra", rw::GlyphsSeat).bound, "bug 1: the Glyphs half held on a skill's button: Replaces, as before");
        }
        Check(rw::WasText(rw::Held::DrawGlyph, nullptr) == "Draw glyph" && rw::WasText(rw::Held::None, nullptr) == "none" && rw::WasText(rw::Held::Skill, "Hydra") == "Hydra" && rw::WasText(rw::Held::Skill, nullptr) == "?",
              "bug 1: the log's (was ...): Draw glyph, none, the skill, ? for a skill not in the list");
        {   // through the real hold: Frozen Armor held on X while X draws glyphs (Nov 8 11:18:34 AM)
            const int per[3] = {10, 10, 10};
            const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
            auto assign = [&](int seatKind, int boundSkill, const std::array<int, 2>& slots) {
                rw::QuickCast q; std::string was = "(no assign)";
                for (double t = 0; t <= 1600; t += 5) {
                    rw::QuickIn in; in.nowMs = t; in.main = true; in.under = seatKind < 0 ? rw::Target{rw::Kind::Glyphs} : rw::Target{rw::Kind::Seat, seatKind};
                    for (auto& hnd : in.bound) hnd.fill(-1);
                    in.bound[1][1] = boundSkill;
                    if (t == 200) in.press[1] = 1;
                    in.down[1][1] = t >= 200;
                    const auto e = q.Step(in, l);
                    if (e.event == rw::Quick::Assign || e.event == rw::Quick::GlyphAssign) was = rw::WasText(rw::HeldBy(e.replaced, slots, e.hand, e.slot), "Hydra");
                }
                return was;
            };
            Check(assign(4, -1, draws) == "Draw glyph", "bug 1: assign onto the Draw glyph button: (was Draw glyph), not none: " + assign(4, -1, draws));
            Check(assign(4, -1, {-1, -1}) == "none" && assign(4, 62, draws) == "Hydra", "bug 1: an empty button: none; a skill's button: the skill");
            Check(assign(-1, -1, draws) == "Draw glyph" && assign(-1, -1, {-1, -1}) == "none", "bug 1: Draw glyph assigned to the button that already draws says so too");
        }
    }
    {   // Audit 2026-11-10 bug 8: every release of a ring swapchain is said once, the session's end too (it said nothing)
        struct Gpu { bool chain = true; bool gpuDone = true; bool exited = false; };
        std::vector<std::string> lines;
        auto destroy = [&](Gpu* g, const char* where, double ms) {
            const bool had = g->chain; g->chain = false;
            rw::SayReleased(had, [&] { return rw::ReleasedLine(where, ms); }, [&](const char* line) { lines.push_back(line); });
        };
        auto retire = [&](rw::Retired<Gpu>& list, Gpu* g, const char* where) {   // the ring's Retire
            const auto r = list.Off(g, 16, [](Gpu* x, unsigned) { return x->gpuDone; }, [] { return 0.0; }, [](Gpu* x) { x->chain = false; }, [](Gpu*) {});
            if (r.destroyed) lines.push_back(rw::ReleasedLine(where, r.ms));
        };
        auto reap = [&](rw::Retired<Gpu>& list, bool wait) {                     // the ring's Reap
            list.DestroyDone([](Gpu* g) { return g->chain; }, [](Gpu* g) { return g->gpuDone; }, [&](Gpu* g) { destroy(g, rw::ReapWhere(wait, rw::ReapStep::GpuDone), -1); });
            list.Reap(wait, [](Gpu* g) { return g->exited; }, [](Gpu*) {}, [&](Gpu* g) { destroy(g, rw::ReapWhere(wait, rw::ReapStep::WorkerGone), -1); }, [](Gpu*) {});
        };
        {   // Review N3: the GPU busy at the session's Retire and done by its Reap; and one kept at an Off edge, freed at the
            // session's end. Both are the session's end, never "kept at its Off edge".
            for (const char* retiredAt : {"session end", "Off edge"}) {
                rw::Retired<Gpu> list; Gpu g; g.gpuDone = false; lines.clear();
                retire(list, &g, retiredAt);
                Check(lines.empty() && g.chain, std::string("N3: a busy GPU keeps the swapchain, nothing said yet: ") + retiredAt);
                g.gpuDone = true; reap(list, true);
                Check(lines.size() == 1 && lines[0] == "Frame ring wheel: swapchain destroyed (session end, the frame thread)" && !g.chain,
                      std::string("N3: freed by the session's Reap once its GPU is done: said as the session's end, retired at the ") + retiredAt + ": " + (lines.empty() ? std::string("nothing") : lines[0]));
            }
            Check(std::string(rw::ReapWhere(true, rw::ReapStep::GpuDone)) == "session end" && std::string(rw::ReapWhere(true, rw::ReapStep::WorkerGone)) == "session end", "N3: Reap waiting is the session's end at both steps");
            Check(std::string(rw::ReapWhere(false, rw::ReapStep::GpuDone)) == "kept at its Off edge, the GPU now done" && std::string(rw::ReapWhere(false, rw::ReapStep::WorkerGone)) == "kept at its Off edge, its worker now gone",
                  "N3: not waiting, a kept swapchain's later release keeps its words");
        }
        {   // the launch's first session ends (upstream makes the session twice): the GPU idle
            rw::Retired<Gpu> list; Gpu g; lines.clear();
            retire(list, &g, "session end"); reap(list, true);
            Check(lines.size() == 1 && lines[0] == "Frame ring wheel: swapchain destroyed ms=0.0 (session end, the frame thread)" && !g.chain, "bug 8: the session's end says its release, once: " + (lines.empty() ? std::string("nothing") : lines[0]));
        }
        {   // the GPU still busy at the session's end: the reap's wait frees it, said once
            rw::Retired<Gpu> list; Gpu g; g.gpuDone = false; lines.clear();
            retire(list, &g, "session end"); reap(list, true);
            Check(lines.size() == 1 && lines[0] == "Frame ring wheel: swapchain destroyed (session end, the frame thread)" && !g.chain, "bug 8: a busy GPU at the session's end: one line from the reap");
        }
        {   // the Off edge's line is unchanged; a kept swapchain says its later release, once; the session's end then says nothing more
            rw::Retired<Gpu> list; Gpu a, b; b.gpuDone = false; lines.clear();
            retire(list, &a, "Off edge");
            Check(lines.size() == 1 && lines[0] == "Frame ring wheel: swapchain destroyed ms=0.0 (Off edge, the frame thread)", "bug 8: the Off edge's line keeps its words");
            retire(list, &b, "Off edge"); reap(list, false);
            Check(lines.size() == 1 && b.chain, "bug 8: a kept swapchain is not said as released while its GPU is busy");
            b.gpuDone = true; reap(list, false); reap(list, false);
            Check(lines.size() == 2 && lines[1] == "Frame ring wheel: swapchain destroyed (kept at its Off edge, the GPU now done, the frame thread)", "bug 8: its release is said when it happens, once");
            Gpu none; none.chain = false;
            list.Add(&none); reap(list, true);
            Check(lines.size() == 2, "bug 8: a side with no swapchain left says nothing at the session's end");
        }
        {   // Perf nit: the line is built only when there is something to say
            int built = 0, said = 0;
            auto line = [&] { ++built; return std::string("x"); };
            Check(!rw::SayReleased(false, line, [&](const char*) { ++said; }) && built == 0 && said == 0 && lines.size() == 2, "bug 8 / perf nit: no swapchain: no line, and its text is never built");
            Check(rw::SayReleased(true, line, [&](const char* text) { said += std::string(text) == "x"; }) && built == 1 && said == 1, "perf nit: a swapchain released: the text built once and said");
        }
    }
    {   // Audit 2026-11-10, the ring's wiring (read from the sources: the ring file needs the game to run). Each line here was
        // missing or different before its fix, and a merge that drops one fails here.
        auto read = [](const char* file) {
            std::string here = __FILE__; here = here.substr(0, here.find_last_of("/\\") + 1);
            std::ifstream in(here + file); std::stringstream ss; ss << in.rdbuf();
            std::string text = ss.str(), out;
            for (const char c : text) if (c != ' ' && c != '\r') out += c;   // spaces never matter
            return out;
        };
        auto has = [](const std::string& text, const char* what) { return text.find(what) != std::string::npos; };
        const std::string lab = read("../vr/ring_wheel.cpp"), table = read("ring_wheel_host.h"), tr = read("transport.cpp");
        Check(!lab.empty() && !table.empty() && !tr.empty(), "wiring: the three sources are read");
        // bug 2: the close line at the clear, through upstream's own OnClearInput hook; one line a close
        Check(has(table, "voidCleared();"), "wiring bug 2: the ring wheel's Cleared door");
        Check(has(lab, "voidCleared(){") && has(lab, "edges.Cleared(why,shut)") && has(lab, "sayKept(rw::ClosedLine("), "wiring bug 2: Cleared writes the close line there");
        Check(has(lab, "edges.Frame(f.wheelHand,click,hold.Safety(),session)"), "wiring bug 2: the focused frame no longer names a cleared input's close (never two lines)");
        {   // every clear passes upstream's hook before the wheel is closed, and each Blip seam is followed by ClearInput in the same statement run
            const size_t at = tr.find("voidClearInput(){"), hook = tr.find("framevr::ringwheel::host::Cleared();", at), suspend = tr.find("handControls.Suspend();", at), end = tr.find("\n}", at);
            Check(at != std::string::npos && hook < suspend && suspend < end, "wiring bug 2: transport's ClearInput calls the ring's Cleared before it closes the wheel");
            int seams = 0, cleared = 0;
            for (size_t i = tr.find("framevr::ringwheel::host::Blip("); i != std::string::npos; i = tr.find("framevr::ringwheel::host::Blip(", i + 1)) {
                ++seams;
                const size_t stop = tr.find('\n', tr.find('\n', i) + 1);   // its line and the next
                const size_t clear = tr.find("ClearInput();", i);
                cleared += clear != std::string::npos && clear < stop;
            }
            Check(seams == 3 && cleared == 3, "wiring bug 2: each of the 3 Blip seams runs into ClearInput (" + std::to_string(cleared) + " of " + std::to_string(seams) + ")");
        }
        // bug 3: upstream's own footer click, read after its engine stepped
        Check(has(lab, "footerClick.Step(EnabledNow()&&in.wheelHand>=0&&!ringActive,pageWas,in.hover,in.trigger,in.snapshot.developer))edges.Wrote(tab);"),
              "wiring bug 3: the Request seam names a footer click on upstream's page");
        // bug 1: the hold's words and both assign lines ask what the button held
        Check(has(lab, "rw::HoldWordsFor(rw::HeldBy(was,glyphDrawNow,e.hand,e.slot)") && has(lab, "rw::WasText(rw::HeldBy(o.replaced,glyphDrawNow,o.hand,o.slot)"), "wiring bug 1: the center's words and the log's (was ...) ask HeldBy");
        Check(has(lab, "assign%s->%s(was%s)held=%.0fms\",name(e.seat).c_str(),slotName(e.hand,e.slot).c_str(),wasText(e).c_str()") &&
              has(lab, "assignDrawglyph->%s(was%s)held=%.0fms\",slotName(e.hand,e.slot).c_str(),wasText(e).c_str()"), "wiring bug 1: both assign lines");
        Check(has(lab, "glyphDrawNow=o.glyphDraw;"), "wiring bug 1: the draw slots are upstream's own (its Output)");
        // bug 8: the session's end says its release
        Check(has(lab, "Retire(sayKept,\"sessionend\");") && has(lab, "Reap(true,sayKept);") && !has(lab, "Retire(nullptr)"), "wiring bug 8: the session's end says its swapchain's release");
        Check(has(lab, "Retire(f.say,\"Offedge\");") && has(lab, "rw::SayReleased(DestroyChain(g)"), "wiring bug 8: the Off edge keeps its line; a kept one says its later release");
        Check(has(lab, "rw::SayReleased(DestroyChain(g)&&say,[where]{returnrw::ReleasedLine(where,-1);},"), "wiring perf nit: Reap builds a release line only for a swapchain that was there, with a log to take it");
        Check(has(lab, "gone(g,rw::ReapWhere(wait,rw::ReapStep::GpuDone));") && has(lab, "gone(g,rw::ReapWhere(wait,rw::ReapStep::WorkerGone));") && !has(lab, "gone(g,\""),
              "wiring N3: both of Reap's steps take their words from ReapWhere (the session's end when it waits)");
        // the trace for bug 6
        Check(has(lab, "f.say(rw::SwitchedLine(f.wheelHand,click,t-openedMs,switchedMs<0?-1:t-switchedMs).c_str());"), "wiring bug 6 trace: the switch line carries the click edges and times");
        // The Commander orders' row (2026-11-14): the chooser draws from the ring's one glyph list, the main ring never does
        Check(has(lab, "chooseList=rw::GlyphChoices(s);") && has(lab, "chooseLayout=rw::ChooserLayout(layout,chooseList);"), "wiring orders: the chooser's seats come from rw::GlyphChoices, the list R7 #20 holds equal to upstream's picker");
        Check(has(lab, "if(rw::ChoiceOf(chooseList,seat.id,seat.owner)||(forQuick&&rw::ItemSeat(seat.id)))chooseDim[i]=0;") && !has(lab, "k&&rw::GlyphPickable(*k))chooseDim"),
              "wiring orders: a chooser seat is lit by that list alone (Quick cast's chooser: its potions and scrolls too)");
        Check(has(lab, "chooseList=rw::QuickChoices(s);") && has(lab, "chooseLayout=rw::ChooserRow(layout,chooseList,row);"), "wiring Quick cast: its chooser's seats from rw::QuickChoices and its row");
        Check(has(lab, "engine.Reset(rp==rw::RingPage::GlyphChoose||rp==rw::RingPage::QuickChoose?chooseLayout:layout);") && has(lab, "constrw::Layout&shown=choosing?chooseLayout:layout;") && has(lab, "quickOut=quick.Step(q,layout);"),
              "wiring orders: the chooser's engine and picture use the chooser's seats; the main ring keeps its own");
        Check(has(lab, "FillQuick(q,f,s,in.nowMs);q.under=rw::OrderUnder(cl,step.under);") && has(lab, "chooseHoldFrac=rw::OrderHoldFrac(e,q.holdMs,q.tapMs);") && has(lab, "HoldStep(e,chooseHoldFrac,q,s,f.layout);"),
              "wiring orders: only an order takes button presses on the chooser; its hold ring from the first frame");
        Check(has(lab, "elseif(step.event==rw::Event::Pick&&rw::EnginePicks(cl,step.picked))pickSeat(step.picked);") && has(lab, "elseif(act==rw::OrderAct::Pick&&rw::OrderSeat(cl,e.seat))pickSeat(e.seat);"),
              "wiring orders: a skill by the engine's pick, an order by the tap rule");
        Check(has(lab, "p.bind={true,rw::SlotOf(e.hand,e.slot),id,-1,e.slot,e.hand,f.layout};Enqueue(p,s,f.say);") && has(lab, "elseif(act==rw::OrderAct::Assign&&rw::OrderSeat(cl,e.seat)){"),
              "wiring orders: an order held to full is queued as upstream's BindSkill");
        {   // Designer gate D1 D2 D3: the ring names the held order, shows an order's blurb whole, and the header keeps no copy of upstream's words
            const std::string hdr = read("ring_wheel_orders.h");
            // Review RN1: a rebuilt chooser is no longer stale; without it the chooser would be rebuilt and its engine reset every frame after an army change.
            Check(has(lab, "chooseCellsOf=nullptr;chooseKey=seatsKey;chooseStale=false;chooseHoldFrac=0;++revision;"), "wiring RN1: BuildChooser clears the stale mark as it sets the chooser's key");
            Check(has(lab, "if(choosing&&rw::OrderSeat(shown,quickOut.holding))scene.holdName=rw::orders::Short(shown.seats[size_t(quickOut.holding)].id);"), "wiring D1: a button held on an order on the chooser: its short name goes to the scene");
            Check(has(lab, "if(order)blurbs.Get(built->metrics,key,tx::WholeBlurb(built->metrics,{k->name,sub,rw::orders::Help(seat.id),rw::orders::ChooserAction()},rw::orders::Short(seat.id)));"),
                  "wiring D3: an order's blurb is laid out whole, with its short name to fall back on");
            Check(has(hdr, "returnIs(id)?HelpCut(skillusage::Line(id,0)):std::string();") && !has(hdr, "Commandsyourarmy") && !has(hdr, "Recallsyour") && !has(hdr, "holdposition") && !has(hdr, "Dismissestactical"),
                  "wiring D2: the orders header cuts upstream's line and keeps no copy of it");
        }
        {   // Review M1: the seats' key mixes no army or roster value; the watch sits beside it
            const size_t a = lab.find("uint64_th=1469598103934665603ull;"), b = a == std::string::npos ? a : lab.find("seatsKey=h;", a);
            const std::string key = b == std::string::npos ? std::string() : lab.substr(a, b - a);
            Check(!key.empty() && !has(key, "companions") && !has(key, "ArmyKey") && !has(key, "HasSummonArmy") && !has(key, "mix(army") && !has(key, "mix(rw::Army"),
                  "wiring M1: the seats' key (the main ring's reset) mixes no army answer and no roster value");
            Check(has(key, "constautoarmy=armyWatch.Step(s,h);") && has(key, "if(army.changed)chooseStale=true;") && has(key, "if(army.iconsNew&&seatsValid){"), "wiring M1: the watch runs beside the key; a changed answer marks the chooser stale, a first army adds the icons");
            Check(has(lab, "if(army.icons)request.n=rw::AddOrderIcons(request.ids.data(),request.n,int(request.ids.size()));") && has(lab, "if(rp==rw::RingPage::GlyphChoose&&(chooseKey!=seatsKey||chooseStale||chooseQuick)){BuildChooser(s);engineOpen=false;}") &&
                  has(lab, "if(rp==rw::RingPage::QuickChoose&&(chooseKey!=seatsKey||chooseStale||!chooseQuick)){BuildChooser(s,true);engineOpen=false;}"),
                  "wiring M1: a rebuilt request keeps the orders' icons; a stale chooser (or the other chooser's seats) is rebuilt before it is used");
        }
        // Review 2026-11-16 (exact items): a kind with tiers on Quick cast's chooser opens its page (2026-11-17: carried or not); its page lives
        // over its place's chooser; a kind's cell is its picture's
        Check(has(lab, "elseif(quickPageOn)rp=quickChoose>=0?(quickKind?rw::RingPage::QuickKind:rw::RingPage::QuickChoose):rw::RingPage::Quick;\nif(quickChoose<0)quickKind=0;") &&
              has(lab, "elseif(intcarried=0;qr::IsFamilyId(seat.id)&&(carried=KindPage(s,seat.id&0xffff))>0){") && has(lab, "constintid=quickKindIds[size_t(pageOut.shape)];") &&
              has(lab, "for(constuint32_tcode:qr::ExactCodes){") && has(lab, "request.n,qr::PictureId(id)):-1;"),
              "wiring: Quick cast's kind page (routing, the chooser's kind, its pick) and the exact items' pictures");
        // Potion choice (2026-11-17, review N1): KindPage opens only for a kind with tiers and never returns early for "none carried"
        // (that was the bug: Mana went straight in as "(best)" with no mana potions); the dim tiers and their pick log; the kind's plain name
        Check(has(lab, "if(!qr::HasTiers(family))return0;") && !has(lab, "if(carried<=0)return0;") && has(lab, "quickKindN=1+tiers;") &&
              has(lab, "quickKindDim[size_t(k)+1]=!carries;") && has(lab, "(exactly;nonecarriednow)") && has(lab, "stringQuickKindName(intfamily){returnqr::HasTiers(family)?std::string(qr::FamilyName(family)):qr::FamilyEntryName(family,true);}") &&
              !has(lab, "qr::FamilyEntryName(seat.id&0xffff,true)"),
              "wiring: Quick cast's kind page always opens for a kind with tiers (carried or not), dim tiers say none carried, the chooser names the plain kind");
        // Reviews S3 / S4 / N1 (2026-11-16): the page shows and edits the radial's ring only when it is this character's and read
        Check(has(lab, "if(!qv.ready||qv.session!=s.session||qv.cls!=s.skillClass||qv.save==lk::Save::Reading)returnrw::QuickRingState::Reading;") &&
              has(lab, "pageOut.event==rw::PageEvent::ChooseSkill&&QuickState(s,qv)==rw::QuickRingState::Reading){") &&
              has(lab, "if(framevr::quickradial::link::Viewqv;QuickState(s,qv)==rw::QuickRingState::Reading){") && has(lab, "framevr::quickradial::link::Ask({s.session,place,id,owner,pointer})") &&
              has(lab, "rw::FillQuick(view,qv.ids.data(),qv.n,quickName,cellOf,qv.fixed);rw::MarkQuick(view,qs);") && has(lab, "(uint64_t(quickChooser?quickChoose+1:0)<<52)"),
              "wiring S3 / S4 / N1: Quick cast's page waits for the character's ring (its layout, 2026-11-17); edits carry the pointing hand; the chooser's words carry the place");
        // Quick cast's Move (2026-11-17; review F8: on the page's center, no chooser seat): Move picks a place, then the target; the
        // ask carries the source place; a Move lives on the Quick cast page only and is dropped when its place empties (review F9)
        Check(!has(lab, "QuickMoveSeat") && !has(lab, "row.push_back(rw::") && has(lab, "framevr::quickradial::link::Ask({s.session,to,-1,-1,pointer,from})") &&
              has(lab, "if(!quickPageOn||quickChoose>=0){quickMoveFrom=-1;quickMovePick=false;}") && has(lab, "if(qs!=rw::QuickRingState::Reading)rw::MarkQuickMove(view,quickMoveFrom,quickMovePick);") &&
              has(lab, "if(quickMoveFrom>=0&&!QuickPlaceFilled(quickMoveFrom)){") && has(lab, "pageOut.event==rw::PageEvent::Move)"),
              "wiring Move: no chooser seat, the page's Move, its target ask, its resets and its view");
        {   // Review S3 (the radial's side): edits wait while the save is read; made before it is read, puts are added to the stored ring, never over it
            const std::string qrc = read("../vr/quick_radial.cpp");
            Check(has(qrc, "if(on&&qr::ClassKnown(st.cls)&&st.session==s.session&&!st.identified&&!st.answered)return;") &&
                  has(qrc, "if(put)st.movedBefore.push_back({*put,-1,\"theringmenu\",true});") && has(qrc, "if(!live.Has(m.it.skill)||st.ring.Has(m.it.skill))continue;") &&
                  has(qrc, "constlk::Savesave=st.identified?lk::Save::Saved:st.answered?lk::Save::ThisGame:lk::Save::Reading;") && !has(qrc, "editedBefore") &&
                  has(qrc, "if(e.hand>=0)Pulses(e.hand,3);"),
                  "wiring S3 (radial): edits wait for the save; earlier puts merged into the stored ring; the page's state published; a drop pulses the pointing hand");
        }
        // Review S1: the chooser's seats are drawn on the chooser page only (the row never on the main ring)
        Check(has(lab, "constboolanyChooser=ringPage==rw::RingPage::GlyphChoose||ringPage==rw::RingPage::QuickChoose;") &&
              has(lab, "constboolchoosing=anyChooser&&chooseQuick==(ringPage==rw::RingPage::QuickChoose)&&chooseKey==seatsKey&&!chooseStale&&chooseLayout.count>0;"),
              "wiring S1: `choosing` holds only on a chooser's page, with that chooser's own seats (the glyph chooser's, or Quick cast's, 2026-11-16)");
        // Review S2: the main ring's quick-cast inputs and hold words, as they were before the move into the two helpers
        Check(has(lab, "q.nowMs=hold.NowMs(nowMs);q.main=true;q.lastHitMs=hold.NowMs(fightNow.lastHitMs);") && has(lab, "constbooldown[3]={hi.trigger[h]>.55f,hi.face[h][0],hi.face[h][1]};") && has(lab, "constboolgrip=hi.grip[h]>.5f;") &&
              has(lab, "q.press[h]=rw::SlotPress(down,grip,quickHeld[h]);") && has(lab, "for(intk=0;k<rw::Slots;++k)q.down[h][k]=down[k==0?0:(k-1)%2+1];") && has(lab, "q.bound[size_t(h)][size_t(k)]=slot>=0?s.skills[size_t(slot)].id:-1;"),
              "wiring S2: FillQuick: the shown clock, the hit time, the trigger at .55, the grip at .5, the slots and their skills");
        Check(has(lab, "if(e.holding>=0&&frac>0){") && has(lab, "if(constintticks=rw::TicksCrossed(holdFracWas,frac))QueuePulses(e.hand,ticks,.15f);") && has(lab, "if(holdFracWas<=0){") && has(lab, "holdFracWas=frac;") && has(lab, "}elseholdFracWas=0;"),
              "wiring S2: HoldStep: ticks and words while the ring shows, the words once at its first frame");
        Check(has(lab, "FillQuick(q,f,s,in.nowMs);q.under=step.under;") && has(lab, "HoldStep(e,e.holdFrac,q,s,f.layout);//thehold,fromthetaplimit"), "wiring S2: the main ring's two calls: every target under the laser, the hold from the Tap limit");
        Check(has(lab, "order=ia::MakeCommanderSkill(id,true);") && has(lab, "rw::orders::Help(seat.id)") && has(lab, "rw::orders::Sub()"), "wiring orders: an order's name is upstream's entry; its help and sub line come from the one header");
    }
    {   // Review R6 X3: the worker reads only the box; the frame thread fills it from upstream's cache until done or 2 s
        using Image = framevr::interaction::icons::Image;
        rw::IconBox box;
        const int ids[3] = {1, 2, 3};
        box.Want(ids, 3, 0);
        int calls = 0;
        auto get = [&](int id) { ++calls; return id == 3 ? std::shared_ptr<const Image>{} : std::make_shared<const Image>(); };
        Check(!box.Poll(get, 10) && !box.Done() && calls == 3, "X3 a first poll asks for all three, one missing: not done");
        Check(box.Take(1) && box.Take(2) && !box.Take(3), "X3 the worker reads what came in");
        calls = 0;
        Check(!box.Poll(get, 500) && calls == 1, "X3 later polls ask only for the missing one");
        Check(box.Poll(get, 2010) && box.Done(), "X3 done after 2 s (the missing icon: the initials disc)");
        calls = 0; box.Poll(get, 3000);
        Check(calls == 0, "X3 once done: no upstream calls");
        auto t0 = std::chrono::steady_clock::now(); box.Settle(3000);
        Check(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(100), "X3 Settle returns at once when done");
        box.Want(ids, 3, 4000);
        std::thread waiter([&] { box.Settle(5000); });
        t0 = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        box.Close();
        waiter.join();
        Check(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1000), "X3 Close wakes a worker waiting in Settle (the Off edge, a reset)");
        Check(box.Take(1) != nullptr, "X3 a new set keeps the icons already in");
    }
    {   // Feedback 1 items 3 and 5 (L4 rows 16-18): the bound-button badges, their shapes, letters, placement and rows
        using L = framevr::controllers::Layout;
        const char wantR[5] = {'R', 'A', 'B', 'A', 'B'}, wantL[5] = {'L', 'X', 'Y', 'X', 'Y'};
        for (int k = 0; k < rw::Slots; ++k) {
            Check(rw::BadgeLetter(L::Touch, 0, k) == wantR[k] && rw::BadgeLetter(L::Touch, 1, k) == wantL[k], "Quest letters: right R A B A B, left L X Y X Y (slot " + std::to_string(k) + ")");
            Check(rw::BadgeKindOf(k) == (k == 0 ? rw::BadgeKind::Square : k >= 3 ? rw::BadgeKind::Coin : rw::BadgeKind::Disc), "slot " + std::to_string(k) + ": trigger square, face disc, grip layer coin");
        }
        Check(rw::BadgeLetter(L::Index, 1, 1) == 'A' && rw::BadgeLetter(L::Frame, 1, 2) == 'U' && rw::BadgeLetter(L::Generic, 0, 4) == '2', "other controllers: one letter each (Index A / B, Frame's D-pad U / D, generic 1 / 2)");
        {   // Job B (2026-11-18): a sleeping controller reads Unknown (Generic); the badge letters must not fall to 1 / 2
            rw::LetterLayoutHold hold;
            Check(hold.Step(0, L::Touch, true) == L::Touch && hold.Step(1, L::Touch, true) == L::Touch, "B hold: both hands Touch at first");
            const L right = hold.Step(0, L::Generic, true), left = hold.Step(1, L::Generic, true);
            Check(right == L::Touch && left == L::Touch && rw::BadgeLetter(left, 1, 1) == 'X' && rw::BadgeLetter(right, 0, 2) == 'B', "B hold: a hand that goes Unknown keeps Touch's X / B, never 1 / 2");
            Check(hold.Step(0, L::Index, true) == L::Index && hold.Step(0, L::Generic, true) == L::Index && hold.Step(1, L::Generic, true) == L::Touch, "B hold: a new real layout replaces the held one, per hand");
            rw::LetterLayoutHold fresh;
            Check(fresh.Step(0, L::Generic, true) == L::Generic && rw::BadgeLetter(fresh.Step(0, L::Generic, true), 0, 1) == '1', "B hold: a hand never seen real stays Generic (1 / 2)");
            rw::LetterLayoutHold chosen; chosen.Step(0, L::Touch, true);
            Check(chosen.Step(0, L::Generic, false) == L::Generic && chosen.Step(0, L::Generic, true) == L::Generic, "B hold: a chosen Generic (Button names row) is shown as chosen, and is the new last layout");
        }
        // The kit's qbauto: 10 slots spread over the class seats (A B RT gA gB X Y LT gX gY), the rest free.
        const int slotOrder[10][2] = {{0, 1}, {0, 2}, {0, 0}, {0, 3}, {0, 4}, {1, 1}, {1, 2}, {1, 0}, {1, 3}, {1, 4}};
        auto spread = [&](const rw::Layout& l) {
            rw::BoundSlots b; std::vector<int> cls;
            for (int i = 0; i < l.count; ++i) if (l.seats[size_t(i)].tree >= 0) cls.push_back(i);
            for (int i = 0; i < 10; ++i) {
                const int seat = cls[std::min(cls.size() - 1, size_t(i * int(cls.size()) / 10 + 1))];
                b.id[size_t(slotOrder[i][0])][size_t(slotOrder[i][1])] = l.seats[size_t(seat)].id; b.owner[size_t(slotOrder[i][0])][size_t(slotOrder[i][1])] = l.seats[size_t(seat)].owner;
            }
            return b;
        };
        const int all[3] = {10, 10, 10};
        float lowest = 1e9f; std::string lowestAt;
        for (int cls = 0; cls < 8; ++cls) for (const auto mode : {rw::TreeLayout::Shown, rw::TreeLayout::Orbs}) {
            const auto l = ClassLayout(cls, all, mode, 2, 1);
            const auto b = spread(l);
            rw::BadgeSet set; rw::PlaceBadges(l, b, L::Touch, {}, set);
            const std::string tag = "class " + std::to_string(cls) + (mode == rw::TreeLayout::Shown ? " Trees shown" : " Tree orbs");
            Check(set.count == 10, tag + ": a badge per bound slot (" + std::to_string(set.count) + ")");
            int onFree = 0;
            for (int i = 0; i < l.count; ++i) {
                bool bound = false;
                for (int h = 0; h < 2; ++h) for (int k = 0; k < rw::Slots; ++k) bound |= b.id[size_t(h)][size_t(k)] == l.seats[size_t(i)].id;
                for (int j = 0; j < set.count; ++j) onFree += !bound && set.b[size_t(j)].seat == i;
            }
            Check(onFree == 0, tag + ": free skills bare");
            for (int j = 0; j < set.count; ++j) {
                const auto& x = set.b[size_t(j)]; const auto& st = l.seats[size_t(x.seat)];
                Check(std::fabs(std::hypot(x.x - st.x, x.y - st.y) - (rw::IconPx / 2 * .98f + 4)) < .01f, tag + ": On outline, 25.6 px from its icon");
                if (x.clear < lowest) { lowest = x.clear; lowestAt = tag + " slot " + std::to_string(x.hand) + "/" + std::to_string(x.slot); }
            }
        }
        std::printf("badges (default rows): lowest clearance %.1f px at %s\n", lowest, lowestAt.c_str());
        Check(lowest >= 2, "the default badges clear every other icon, badge, the name arc and the grown center by 2 px, all 8 classes, both layouts (" + std::to_string(lowest) + " px)");
        // Every row value: each badge at its row's angle and depth (1.5 px), inside the wheel region; Off draws none.
        const auto l = ClassLayout(1, all, rw::TreeLayout::Shown, 2, 1);
        const auto b = spread(l);
        int placeBad = 0, canvasBad = 0, combos = 0;
        for (int ang = -1; ang <= 345; ang += ang < 0 ? 1 : 15) for (int dep = 0; dep < 4; ++dep) for (int size = 16; size <= 40; size += 2) {
            const rw::BadgeLook look{ang, rw::BadgeDepth(dep), size};
            rw::BadgeSet set; rw::PlaceBadges(l, b, L::Touch, look, set); ++combos;
            const float d = rw::BadgeDist(look), R = size / 2.f;
            for (int j = 0; j < set.count; ++j) {
                const auto& x = set.b[size_t(j)]; const auto& st = l.seats[size_t(x.seat)];
                float off = std::fabs(std::hypot(x.x - st.x, x.y - st.y) - d);
                if (ang >= 0 && d > 0) { const float want = (ang - 90) * rw::Pi / 180, a = std::atan2(x.y - st.y, x.x - st.x); off = std::max(off, std::fabs(std::atan2(std::sin(a - want), std::cos(a - want))) * d); }
                placeBad += off > 1.5f;
                canvasBad += x.x - R < 0 || x.y - R < 0 || x.x + R > rw::WheelW || x.y + R > rw::WheelH;
            }
            if (set.count != 10) ++placeBad;
        }
        Check(placeBad == 0, "every angle (Auto, 0-345), depth and size: each badge at its row's angle and depth within 1.5 px (" + std::to_string(combos) + " settings)");
        Check(canvasBad == 0, "every setting: every badge inside the 768 x 800 wheel region");
        Check(rw::BadgeDist({-1, rw::BadgeDepth::Outside, 28}) == 37 && rw::BadgeDist({-1, rw::BadgeDepth::Inside, 28}) == 7 && rw::BadgeDist({-1, rw::BadgeDepth::Center, 28}) == 0 &&
              rw::BadgeDist({-1, rw::BadgeDepth::Inside, 40}) == 1, "depth: Outside 22 + R + 1, Inside 22 - R - 1 (never below 0), Center 0");
        { rw::BadgeSet off; rw::PlaceBadges(l, b, L::Touch, {-1, rw::BadgeDepth::Outline, 0}, off); Check(off.count == 0, "size Off: no badge"); }
        {   // review-badges B1: a seat is its id AND its owner. An item's oskill (Enigma's Teleport, owner = the item) on the shelf
            // shares the class skill's id; a bind of one never badges the other.
            const int tele = 54, item = 4242;
            const rw::SeatIn in[3] = {{tele, -1, 0}, {tele + 1, -1, 0}, {tele, item, -1}};
            const auto twin = rw::Build(in, 3, rw::TreeLayout::Shown);
            int classSeat = -1, itemSeat = -1;
            for (int i = 0; i < twin.count; ++i) if (twin.seats[size_t(i)].id == tele) (twin.seats[size_t(i)].owner == -1 ? classSeat : itemSeat) = i;
            Check(classSeat >= 0 && itemSeat >= 0 && twin.seats[size_t(itemSeat)].tree < 0, "B1 the same id twice: a class seat and the item's shelf seat");
            for (const int owner : {-1, item}) {
                rw::BoundSlots one; one.id[0][1] = tele; one.owner[0][1] = owner;
                rw::BadgeSet set; rw::PlaceBadges(twin, one, L::Touch, {}, set);
                const int want = owner == -1 ? classSeat : itemSeat;
                Check(set.count == 1 && set.b[0].seat == want, std::string("B1 the ") + (owner == -1 ? "class" : "item") + " Teleport bound: one badge, on its own seat, none on its twin (" + std::to_string(set.count) + ")");
            }
        }
        {   // one skill on both hands (right A and left X): two badges, the second 2 px clear of the first, at a fixed angle too
            rw::BoundSlots two; two.id[0][1] = two.id[1][1] = l.seats[3].id;
            for (const int ang : {-1, 90}) {
                rw::BadgeSet set; rw::PlaceBadges(l, two, L::Touch, {ang, rw::BadgeDepth::Outline, 28}, set);
                Check(set.count == 2 && std::hypot(set.b[0].x - set.b[1].x, set.b[0].y - set.b[1].y) >= 28 + 2 - .01f, std::string("one skill on two slots: two badges, apart (") + (ang < 0 ? "Auto" : "90 deg") + ")");
            }
        }
        // The board: a placement only on a change; a slot that takes a skill while watched pops, the first fill never does.
        rw::BadgeBoard board;
        Check(board.Update(l, 7, b, L::Touch, {}, 1000) && board.Set().count == 10, "board: the first update places");
        bool popped = false; for (int j = 0; j < board.Set().count; ++j) popped |= board.Set().b[size_t(j)].bornMs > 0;
        Check(!popped, "board: the first fill does not pop");
        const uint32_t rev = board.Set().revision;
        Check(!board.Update(l, 7, b, L::Touch, {}, 1016) && board.Set().revision == rev, "board: the same binds, rows and seats: no new placement");
        auto b2 = b; b2.id[0][1] = l.seats[0].id; b2.owner[0][1] = l.seats[0].owner;
        Check(board.Update(l, 7, b2, L::Touch, {}, 2000), "board: a bind change places again");
        int pops = 0; for (int j = 0; j < board.Set().count; ++j) { const auto& x = board.Set().b[size_t(j)]; if (x.bornMs == 2000) { ++pops; Check(x.hand == 0 && x.slot == 1, "board: the popping badge is the new bind's"); } }
        Check(pops == 1, "board: one badge pops");
        Check(board.Update(l, 7, b2, L::Touch, {90, rw::BadgeDepth::Outline, 28}, 2010) && board.Update(l, 8, b2, L::Touch, {90, rw::BadgeDepth::Outline, 28}, 2020) &&
              board.Update(l, 8, b2, L::Index, {90, rw::BadgeDepth::Outline, 28}, 2030), "board: a row, the seats or the controller change places again");
        Check(rw::BadgePop(75) > 1.34f && rw::BadgePop(150) == 1 && rw::BadgeTick(0) == 1 && rw::BadgeTick(600) == 0, "pop 1.35x at 75 ms, back at 150; the green tick over 600 ms");
        {   // Perf's badges nit 1: a wheel open never re-places by itself; a bind made while closed shows without a pop
            rw::BadgeBoard ob;
            ob.Update(l, 7, b, L::Touch, {}, 100);
            const uint32_t r0 = ob.Set().revision;
            ob.Opened();
            Check(!ob.Update(l, 7, b, L::Touch, {}, 5000) && ob.Set().revision == r0, "open, nothing changed: no new placement (and so no log line)");
            auto closedBind = b; closedBind.id[1][2] = l.seats[1].id; closedBind.owner[1][2] = l.seats[1].owner;
            ob.Opened();
            bool popped = false;
            Check(ob.Update(l, 7, closedBind, L::Touch, {}, 9000), "open after a bind made while closed: placed again");
            for (int j = 0; j < ob.Set().count; ++j) popped = popped || ob.Set().b[size_t(j)].bornMs > 0;
            Check(!popped, "open after a bind made while closed: no pop");
            auto openBind = closedBind; openBind.id[1][3] = l.seats[2].id; openBind.owner[1][3] = l.seats[2].owner;
            int pops = 0;
            Check(ob.Update(l, 7, openBind, L::Touch, {}, 9500), "a bind while open: placed again");
            for (int j = 0; j < ob.Set().count; ++j) pops += ob.Set().b[size_t(j)].bornMs == 9500;
            Check(pops == 1, "a bind while open (after the open's first update): its badge pops");
        }
        {   // Perf's badges nit 2: the shape cap. Badges come after every seat in the 192-shape pool and Push drops at the cap
            // in silence; 10 badges, each with its tick ring, All effects, the pointed icon with its full effects, on the most
            // crowded wheel the game can show (every class skill learned, a full shelf).
            int dropped = 0, worstFree = 1 << 30; std::string worstAt;
            static rw::Instance list[rw::MaxDraw];
            for (int cls = 0; cls < 8; ++cls) for (const auto mode : {rw::TreeLayout::Shown, rw::TreeLayout::Orbs}) {
                const auto cl = ClassLayout(cls, all, mode, 8, 6);
                const auto cb = spread(cl);
                rw::BadgeSet set; rw::PlaceBadges(cl, cb, L::Touch, {}, set);
                for (int j = 0; j < set.count; ++j) set.b[size_t(j)].bornMs = 4990;   // every badge popping: its tick ring drawn too
                for (int pointed = 0; pointed < cl.count; pointed += 7) {
                    rw::Scene sc; sc.open = true; sc.layout = &cl; sc.anim = rw::PresetAnim(rw::Preset::All); sc.badges = &set; sc.tMs = 5000;
                    sc.pointedSinceMs = 4000; sc.step.under = {rw::Kind::Seat, pointed};
                    if (mode == rw::TreeLayout::Orbs) sc.step.tree = std::max(0, cl.seats[size_t(pointed)].tree);
                    // the same scene with no cap (the whole array as shapes) says how many badge shapes it wants
                    auto badgeShapes = [&](int n) { int c = 0; for (int i = 0; i < n; ++i) c += list[i].recipe == rw::RecipeBadge || (list[i].recipe == rw::RecipeRing && list[i].param == 3 && list[i].g > .8f && list[i].b < .6f); return c; };
                    const int wantAll = rw::Build(sc, list, rw::MaxDraw, 0), want = badgeShapes(wantAll);
                    const int n = rw::Build(sc, list, rw::MaxInstances, 0), got = badgeShapes(n);
                    if (got < want) { ++dropped; if (worstAt.empty()) worstAt = "class " + std::to_string(cls) + (mode == rw::TreeLayout::Shown ? " shown" : " orbs") + " pointed seat " + std::to_string(pointed) + ": " + std::to_string(got) + " of " + std::to_string(want) + " badge shapes, " + std::to_string(wantAll) + " shapes wanted"; }
                    if (rw::MaxInstances - wantAll < worstFree) worstFree = rw::MaxInstances - wantAll;
                }
            }
            std::printf("badge shape cap: %d of %d shapes free at the fullest (10 badges, ticks, All effects, a pointed icon)\n", worstFree, rw::MaxInstances);
            Check(dropped == 0, "shape cap: no badge dropped, every class, both layouts, any pointed icon" + (worstAt.empty() ? std::string() : " (first: " + worstAt + ")"));
        }
        {   // batch 3 merge (Boss: badges and text on top of the effects): every badge body after every effect, incl. the stage 3a
            // FIX's face-cut over quad (RecipeFx + PointedOverFace), every pointed seat of every class; still at most 4 draws
            static rw::Instance list[rw::MaxDraw];
            int scenes = 0, faceCut = 0, badOrder = 0, maxRuns = 0, bodiesMissing = 0; std::string firstBad;
            for (int cls = 0; cls < 8; ++cls) {
                const auto cl = ClassLayout(cls, all, rw::TreeLayout::Shown, 2, 1);
                const auto cb = spread(cl);
                rw::BadgeSet set; rw::PlaceBadges(cl, cb, L::Touch, {}, set);
                for (int pointed = 0; pointed < cl.count; ++pointed) {
                    rw::Scene sc; sc.open = true; sc.layout = &cl; sc.anim = rw::PresetAnim(rw::Preset::All); sc.badges = &set; sc.tMs = 5000;
                    sc.pointedSinceMs = 4000; sc.step.under = {rw::Kind::Seat, pointed};
                    const int n = rw::Build(sc, list);
                    int lastFx = -1, firstBody = n, bodies = 0; bool cut = false;
                    for (int k = 0; k < n; ++k) {
                        if (list[k].recipe == rw::RecipeFx) { lastFx = k; cut |= uint32_t(list[k].param) >= rw::PointedOverFace; }
                        if (list[k].recipe == rw::RecipeBadge) { ++bodies; firstBody = std::min(firstBody, k); }
                    }
                    ++scenes; faceCut += cut; bodiesMissing += bodies != set.count;
                    if (!(lastFx < firstBody)) { if (!badOrder) firstBad = "class " + std::to_string(cls) + " seat " + std::to_string(pointed) + ": last effect " + std::to_string(lastFx) + ", first body " + std::to_string(firstBody); ++badOrder; }
                    maxRuns = std::max(maxRuns, rw::DrawRuns(list, n));
                }
            }
            std::printf("badges over effects: %d scenes, %d with a face-cut over quad, at most %d draws\n", scenes, faceCut, maxRuns);
            Check(faceCut > 0, "batch 3: some pointed skill draws the stage 3a face-cut over quad (so the order check below sees it)");
            Check(badOrder == 0 && bodiesMissing == 0, "batch 3: every badge body after every effect, the face-cut quad included" + (firstBad.empty() ? std::string() : " (first: " + firstBad + ")"));
            Check(maxRuns <= 4, "batch 3: badges on top still cost no draw past 4 (" + std::to_string(maxRuns) + ")");
        }
        {   // the player 2026-11-08 #3 (designer): the wheel hand's Draw glyph button on the Glyphs half
            const std::array<int, 2> slots{rw::SlotOf(0, 1), rw::SlotOf(1, 3)};
            const auto r = rw::DrawBadgeFor(slots, 0, L::Touch, 28), left = rw::DrawBadgeFor(slots, 1, L::Touch, 22);
            Check(r.on && r.kind == rw::BadgeKind::Disc && r.letter == 'A' && r.sizePx == 28, "#3 badge: the right wheel hand's own button (A, a disc)");
            Check(left.on && left.kind == rw::BadgeKind::Coin && left.letter == 'X' && left.sizePx == 22, "#3 badge: it follows the wheel hand (left grip + X, the gold coin, the Size row)");
            const auto trig = rw::DrawBadgeFor({rw::SlotOf(0, 0), -1}, 0, L::Touch, 28);
            Check(trig.on && trig.kind == rw::BadgeKind::Square && trig.letter == 'R', "#3 badge: the trigger is the R square");
            Check(!rw::DrawBadgeFor({-1, slots[1]}, 0, L::Touch, 28).on, "#3 badge: only the other hand has a draw button: none for this wheel");
            Check(!rw::DrawBadgeFor(slots, 0, L::Touch, 0).on && !rw::DrawBadgeFor(slots, -1, L::Touch, 28).on, "#3 badge: Size Off, or no wheel hand: none");
            static rw::text::Metrics fm; fm.ok = true; for (int st = 0; st < rw::text::Styles; ++st) for (auto& g : fm.g[size_t(st)]) { g.advance = 9; g.w = 8; g.h = 10; }
            static rw::Instance dl[rw::MaxDraw];
            rw::Scene sc; sc.open = true; sc.layout = &l; sc.anim = rw::PresetAnim(rw::Preset::All); sc.hasMotions = true; sc.metrics = &fm; sc.drawBadge = r; sc.tMs = 5000;
            sc.pointedSinceMs = 4000; sc.step.under = {rw::Kind::Seat, 0};
            const float rest = rw::CenterR * rw::DrawBadgeRing, grownAt = rw::CenterGrownR * rw::DrawBadgeRing;
            struct Seen { int body = -1, letter = -1, lastFx = -1; float a = 0; };
            auto see = [&](const rw::Scene& x, float off) {
                Seen v; const int n = rw::Build(x, dl);
                const float sx = rw::CanvasCx + off, sy = rw::CanvasCy - off;
                for (int i = 0; i < n; ++i) {
                    const bool at = std::hypot(dl[i].cx - sx, dl[i].cy - sy) < 12;
                    if (dl[i].recipe == rw::RecipeFx) v.lastFx = i;
                    if (at && dl[i].recipe == rw::RecipeBadge) { v.body = i; v.a = dl[i].a; }
                    if (at && dl[i].recipe == rw::RecipeGlyph) v.letter = i;
                }
                return v;
            };
            const auto v = see(sc, rest);
            Check(v.body >= 0 && v.letter > v.body, "#3 badge: a body and its letter on the ring at rest (offset 56.6)");
            Check(v.lastFx >= 0 && v.body > v.lastFx && v.letter > v.lastFx, "#3 badge: body and letter after every effect");
            Check(std::fabs(std::hypot(rest, rest) - rw::CenterR) < .1f && std::fabs(rest - 56.6f) < .05f && std::fabs(grownAt - 96.2f) < .05f, "#3 badge: on the center ring, 45 deg upper right: 56.6 at rest, 96.2 grown");
            auto hidden = [&](rw::Scene x, const char* why) { const auto h = see(x, x.step.centerGrown || x.holdFrac >= 0 ? grownAt : rest); Check(h.body < 0 && h.letter < 0, std::string("#3 badge hidden: ") + why); };
            { auto x = sc; x.step.blurb = 3; hidden(x, "a skill's blurb"); }
            { auto x = sc; x.holdFrac = .5f; x.holdSeat = rw::GlyphsSeat; hidden(x, "the hold's picture"); }
            { auto x = sc; x.holdFrac = 0; x.holdSeat = rw::GlyphsSeat; hidden(x, "the hold before the tap limit shows"); }
            { auto x = sc; x.chooseTitle = "Circle"; hidden(x, "the glyph chooser"); }
            { auto x = sc; x.drawBadge = {}; hidden(x, "no draw button"); }
            {   // the designer's correction: it stays while the center is pointed, riding the ring
                auto x = sc; x.step.centerGrown = true; x.step.under = {rw::Kind::Glyphs};
                const auto g = see(x, grownAt);
                Check(g.body >= 0 && g.letter > g.body && g.a == 1.f && see(x, rest).body < 0, "#3 badge: the Glyphs half pointed: it stays, on the grown ring (96.2), full");
                x.step.under = {rw::Kind::Gestures};
                const auto d = see(x, grownAt);
                Check(d.body >= 0 && std::fabs(d.a - .55f) < .001f, "#3 badge: the Gestures half pointed: it stays at 55%");
            }
            {   // the requirement changed (designer): the Warlock's whole center is Kind::Glyphs, so its draw button shows there
                auto x = sc; x.hasMotions = false;
                const auto w = see(x, rest);
                Check(w.body >= 0 && w.letter > w.body, "#3 badge: shows on the Warlock's one center");
                x.step.centerGrown = true; x.step.under = {rw::Kind::Glyphs};
                Check(see(x, grownAt).body >= 0, "#3 badge: the Warlock's center pointed: it rides the ring");
            }
            { auto x = sc; x.drawBadge.letter = 'B'; Check(!(rw::KeyOf(x) == rw::KeyOf(sc)), "#3 badge: a change redraws (the draw key)"); }
            rw::QuickOut q; const char* status = "Hydra";
            { const auto h = rw::HoldLinesFor(q, true, status); Check(h.top && std::string(h.top) == "Replaces" && h.line == status && !h.green, "#3 hold text: a bound button: Replaces, then the skill alone"); }
            { const auto h = rw::HoldLinesFor(q, false, "Draw glyph"); Check(!h.top && std::string(h.line) == "Draw glyph" && !h.green, "#3 hold text: an empty button: one line"); }
            Check(std::string(rw::EmptyHoldStatus(rw::GlyphsSeat)) == "Draw glyph" && std::string(rw::EmptyHoldStatus(4)) == "Hold to assign", "#3 hold text: an empty button: Draw glyph on the Glyphs half, Hold to assign on a skill");
            q.assigned = true;
            { const auto h = rw::HoldLinesFor(q, true, status); Check(!h.top && std::string(h.line) == "Assigned" && h.green && h.line != status, "#3 hold text: Assigned at full, one line, green (a different pointer: the draw key sees it)"); }
            {   // where the lines land, and their colors
                auto lines = [&](const char* top, const char* line, bool green) {
                    auto x = sc; x.holdFrac = 1; x.holdSeat = rw::GlyphsSeat; x.holdTop = top; x.holdStatus = line; x.holdGreen = green;
                    const int n = rw::Build(x, dl);
                    struct Out { int at42 = 0, at66 = 0, at76 = 0; float r = 0, g = 0, b = 0; } o;
                    for (int i = 0; i < n; ++i) {
                        if (dl[i].recipe != rw::RecipeGlyph) continue;
                        const float y = dl[i].cy - rw::CanvasCy;
                        if (y > 30 && y < 50) ++o.at42;
                        if (y > 54 && y < 72) { ++o.at66; o.r = dl[i].r; o.g = dl[i].g; o.b = dl[i].b; }
                        if (y > 74) ++o.at76;
                    }
                    return o;
                };
                const auto two = lines("Replaces", "Hydra", false);
                Check(two.at42 == 8 && two.at66 == 5 && two.at76 == 0, "#3 hold text: bound: Replaces at C+42, the skill at C+66 (" + std::to_string(two.at42) + ", " + std::to_string(two.at66) + ", " + std::to_string(two.at76) + ")");
                const auto one = lines(nullptr, "Draw glyph", false);
                Check(one.at42 == 0 && one.at66 > 0 && one.at76 == 0, "#3 hold text: one line at C+66, not C+76");
                Check(std::fabs(one.g - rw::Muted.g) < .001f && std::fabs(one.r - rw::Muted.r) < .001f, "#3 hold text: muted");
                const auto done = lines(nullptr, "Assigned", true);
                Check(std::fabs(done.r - rw::TickGreen.r) < .001f && std::fabs(done.g - rw::TickGreen.g) < .001f && std::fabs(done.b - rw::TickGreen.b) < .001f, "#3 hold text: Assigned in the kit's green #9BE07A");
                { auto x = sc, y = sc; x.holdFrac = y.holdFrac = 1; x.holdGreen = true; Check(!(rw::KeyOf(x) == rw::KeyOf(y)), "#3 hold text: the color is in the draw key"); }
                { auto x = sc, y = sc; x.holdFrac = y.holdFrac = 1; x.holdTop = "Replaces"; Check(!(rw::KeyOf(x) == rw::KeyOf(y)), "#3 hold text: the top line is in the draw key"); }
            }
        }
        // The painter: a body per badge on the main page, none on the glyph chooser or with no set; a pointed icon carries its badge out.
        {
            rw::BadgeSet set; rw::PlaceBadges(l, b, L::Touch, {}, set);
            rw::Scene sc; sc.open = true; sc.layout = &l; sc.anim = rw::PresetAnim(rw::Preset::Off); sc.badges = &set; sc.tMs = 5000;
            static rw::Instance list[rw::MaxDraw];
            auto bodies = [&](const rw::Scene& x) { const int n = rw::Build(x, list); int c = 0; for (int i = 0; i < n; ++i) c += list[i].recipe == rw::RecipeBadge; return c; };
            Check(bodies(sc) == 10, "Build: a badge body per bound slot (" + std::to_string(bodies(sc)) + ")");
            int coins = 0, squares = 0;
            { const int n = rw::Build(sc, list); for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeBadge) { coins += list[i].param == 1; squares += list[i].param == 2; } }
            Check(coins == 4 && squares == 2, "Build: four grip coins, two trigger squares");
            auto chooser = sc; chooser.chooseTitle = "Ice Blast";
            Check(bodies(chooser) == 0, "Build: no badges on the glyph chooser (FS7)");
            auto none = sc; none.badges = nullptr;
            Check(bodies(none) == 0, "Build: no set, no badges");
            const auto& first = set.b[0];
            auto pointed = sc; pointed.step.under = {rw::Kind::Seat, first.seat};
            const int n = rw::Build(pointed, list);
            float got = -1; const auto& st = l.seats[size_t(first.seat)];
            for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeBadge) { got = std::hypot(list[i].cx - rw::CanvasX - st.x, list[i].cy - rw::CanvasY - st.y); break; }
            Check(std::fabs(got - (first.dist * 1.3f + 3)) < .01f, "Build: the pointed icon's badge rides out to 1.3x + 3 px (" + std::to_string(got) + ")");
            const auto k0 = rw::KeyOf(sc);
            auto popping = set; popping.b[0].bornMs = 4900; auto sp = sc; sp.badges = &popping;
            Check(!(rw::KeyOf(sp) == k0), "draw key: a running pop redraws");
            auto moved = set; ++moved.revision; auto sm = sc; sm.badges = &moved;
            Check(!(rw::KeyOf(sm) == k0) && rw::KeyOf(sc) == k0, "draw key: a new placement redraws, the same one does not");
        }
    }
    {   // Job B (the design notes item 1, critic FB1 / FS1): pointing with either hand. 90 Hz frames; a hand's ray as a yaw.
        namespace pt = framevr::ringwheel::pointing;
        struct Hand { bool tracked = true, onPanel = true, onTarget = false, trigger = false, anyDown = false; float yaw = 0; };
        struct Sim {
            pt::Owner o; pt::Out out; double t = 0, stepMs = 1000.0 / 90; int wheel = 1; pt::Allow allow = pt::Allow::Either; Hand h[2];
            pt::Out Step(int frames = 1) {
                while (frames--) {
                    pt::In in; in.nowMs = t; in.open = wheel >= 0; in.wheelHand = wheel; in.allow = allow;
                    for (int k = 0; k < 2; ++k) {
                        auto& x = in.hand[k]; const auto& y = h[k];
                        x.tracked = y.tracked; x.onPanel = y.onPanel; x.onTarget = y.onTarget; x.trigger = y.trigger; x.anyDown = y.anyDown || y.trigger;
                        const float r = y.yaw * 3.14159265f / 180; x.dir[0] = std::sin(r); x.dir[1] = 0; x.dir[2] = -std::cos(r);
                    }
                    out = o.Step(in); t += stepMs;
                }
                return out;
            }
            void Turn(int hand, float deg, double ms) {   // a steady turn of deg over ms
                const int n = std::max(1, int(ms * 90 / 1000)); const float step = deg / n;
                for (int i = 0; i < n; ++i) { h[hand].yaw += step; Step(); }
            }
            int Owner() const { return o.Current(); }
        };
        {   // at open: both hands at rest, no owner; a still hand never claims
            Sim s; s.Step(200);
            Check(s.Owner() == -1, "job B: both hands resting, no owner (no laser, no hover)");
        }
        {   // a 2 deg turn within 300 ms on the panel claims it; the same turn off the panel does not
            Sim s; s.Step(5); s.Turn(0, 2, 150);
            Check(s.Owner() == 0, "job B: the right hand turned 2 deg in 150 ms on the panel owns it");
            Sim t; t.h[0].onPanel = false; t.Step(5); t.Turn(0, 2, 150);
            Check(t.Owner() == -1, "job B: a turn off the panel does not claim");
        }
        {   // 1.5 deg within 300 ms: under it, or slower, never claims
            Sim s; s.Step(5); s.Turn(1, 1.0f, 200); s.Step(40);
            Check(s.Owner() == -1, "job B: 1 deg in 200 ms is a rest, not a claim");
            Sim t; t.Step(5); t.Turn(1, 1.6f, 900); t.Step(40);
            Check(t.Owner() == -1, "job B: 1.6 deg drifting over 900 ms (under 1.5 deg in any 300 ms) does not claim");
            Sim u; u.Step(5); u.Turn(1, 1.6f, 250);
            Check(u.Owner() == 1, "job B: 1.6 deg in 250 ms claims");
        }
        {   // a trigger press on the panel claims; a trigger already held at open is not a press
            Sim s; s.Step(5); s.h[1].trigger = true; s.Step();
            Check(s.Owner() == 1 && s.out.why == pt::Why::Trigger, "job B: the left trigger pressed on the panel claims (why=trigger)");
            Sim t; t.h[1].trigger = true; t.Step(60);
            Check(t.Owner() == -1, "job B: a trigger held since the open is not a claim");
        }
        {   // FB1: the owner keeps it on a target, or with a button or hold down, however long it stays still
            Sim s; s.Step(5); s.Turn(0, 3, 100); s.h[0].onTarget = true; s.Step(450);
            Check(s.Owner() == 0, "job B FB1: still on a skill for 5 s (a blurb read), the owner keeps it");
            Sim t; t.Step(5); t.Turn(0, 3, 100); t.h[0].onPanel = false; t.h[0].anyDown = true; t.Step(450);
            Check(t.Owner() == 0, "job B FB1: a button held (a 1 s hold to assign and longer), the owner keeps it even off the panel");
        }
        {   // the 1 s release timer runs only off the panel or on nothing
            // The move keeps counting for MoveMs (1.5 deg within 300 ms stays true until its samples age out), so the 1 s
            // runs from then: hold still off the panel for 300 ms first, then count.
            Sim s; s.Step(5); s.Turn(0, 3, 100); s.h[0].onPanel = false; s.Step(27);
            Check(s.Owner() == 0, "job B: still the owner while its last move counts (300 ms)");
            int frames = 0; while (s.Owner() == 0 && frames < 400) { s.Step(); ++frames; }
            Check(s.Owner() == -1 && s.out.why == pt::Why::Released && frames >= 84 && frames <= 92, "job B: off the panel and still, released 1 s after its last move counted (" + std::to_string(frames) + " frames)");
            Sim t; t.Step(5); t.Turn(0, 3, 100); t.h[0].onTarget = true; t.Step(300); t.h[0].onTarget = false; t.Step(80);
            Check(t.Owner() == 0, "job B: the timer starts when the target is left (0.9 s later still the owner)");
            t.Step(15);
            Check(t.Owner() == -1, "job B: ... and 1 s after it, released");
        }
        {   // a hand change: the other hand's deliberate move takes over unless the owner has a button down (round 2026-11-08 #5: 8 deg, was 3)
            Sim s; s.Step(5); s.Turn(0, 3, 100); s.h[0].onTarget = true; s.Step(30); s.Turn(1, 8, 100);
            Check(s.Owner() == 1, "job B: swapping hands mid-hover: the left takes over within 300 ms");
            Sim t; t.Step(5); t.Turn(0, 3, 100); t.h[0].anyDown = true; t.Step(10); t.Turn(1, 5, 100); t.h[1].trigger = true; t.Step(5);
            Check(t.Owner() == 0, "job B: a held button pins the owner: the other hand's move and trigger do not take it");
            t.h[0].anyDown = false; t.h[1].trigger = false; t.Step(); t.h[1].trigger = true; t.Step();
            Check(t.Owner() == 1, "job B: once let go, the other hand's trigger takes it");
        }
        {   // round 2026-11-08 #5 (the player's log: 361 of 420 owner changes under 50 ms apart): the owner changes only on a deliberate move
            for (const double hz : {90.0, 28.6}) {
                const std::string at = std::to_string(int(hz)) + " Hz";
                auto start = [&](Sim& s) { s.stepMs = 1000 / hz; s.Step(5); s.Turn(0, 3, 150); s.h[0].onTarget = s.h[1].onTarget = true; s.Step(int(hz)); };
                unsigned rnd = 12345;
                auto jitter = [&]() { rnd = rnd * 1664525u + 1013904223u; return float(int(rnd >> 8) % 241 - 120) / 100; };   // -1.2 .. 1.2 deg
                {   // noise on both hands (each frame up to 2.4 deg apart: over the 1.5 deg claim, under the 5 deg take), 60 s
                    Sim s; start(s); const int owner = s.Owner(); int flips = 0;
                    for (int f = 0; f < int(60 * hz); ++f) { s.h[0].yaw = jitter(); s.h[1].yaw = jitter(); s.Step(); flips += s.out.changed; }
                    Check(owner == 0 && s.Owner() == 0 && flips == 0, "#5 " + at + ": noise on both hands for 60 s never flips (" + std::to_string(flips) + ")");
                }
                {   // the body swaying: both rays turn together, 8 deg peak to peak at 1 Hz (over TakeDeg within 300 ms), with noise
                    Sim s; start(s); int flips = 0;
                    for (int f = 0; f < int(30 * hz); ++f) { const float y = 4 * std::sin(float(f) / float(hz) * 6.2831853f); s.h[0].yaw = y + jitter() / 3; s.h[1].yaw = y + jitter() / 3; s.Step(); flips += s.out.changed; }
                    Check(s.Owner() == 0 && flips == 0, "#5 " + at + ": both hands swaying together never flips (" + std::to_string(flips) + ")");
                }
                {   // both hands sweeping alike: the owner keeps it
                    Sim s; start(s); int flips = 0;
                    for (int f = 0; f < int(10 * hz); ++f) { const float y = 40 * float(f) / float(hz); s.h[0].yaw = y; s.h[1].yaw = y; s.Step(); flips += s.out.changed; }
                    Check(s.Owner() == 0 && flips == 0, "#5 " + at + ": both hands sweeping at the same speed never flips (" + std::to_string(flips) + ")");
                }
                {   // a deliberate move by the other hand takes over, within a stated time: 300 ms at 40 deg/s
                    Sim s; start(s); double ms = 0;
                    while (s.Owner() == 0 && ms < 2000) { s.h[1].yaw += 40 * float(s.stepMs) / 1000; s.Step(); ms += s.stepMs; }
                    Check(s.Owner() == 1 && s.out.why == pt::Why::Moved && ms <= 300, "#5 " + at + ": a 40 deg/s move by the other hand takes over within 300 ms (" + std::to_string(int(ms)) + " ms)");
                }
                {   // a trigger press still wins at once, even inside the owner's minimum
                    Sim s; s.stepMs = 1000 / hz; s.Step(5); s.Turn(0, 3, 150); s.h[1].trigger = true; s.Step();
                    Check(s.Owner() == 1 && s.out.why == pt::Why::Trigger, "#5 " + at + ": the other hand's trigger takes it at once");
                }
                {   // a new owner holds it OwnMinMs against the old owner's move, then a deliberate move takes it back
                    Sim s; start(s);
                    double took = 0;
                    while (s.Owner() == 0 && took < 2000) { s.h[1].yaw += 40 * float(s.stepMs) / 1000; s.Step(); took += s.stepMs; }   // review N1: capped
                    double ms = 0;
                    while (s.Owner() == 1 && ms < 2000) { s.h[0].yaw += 80 * float(s.stepMs) / 1000; s.Step(); ms += s.stepMs; }
                    Check(s.Owner() == 0 && ms >= pt::OwnMinMs - s.stepMs && ms <= 700, "#5 " + at + ": a new owner keeps it 300 ms, then a deliberate move takes it back (" + std::to_string(int(ms)) + " ms)");
                }
                {   // review N2: a 4 deg move is over the claim and under TakeDeg: the owner keeps it
                    Sim s; start(s); const int n = std::max(1, int(200 / s.stepMs));
                    for (int f = 0; f < n; ++f) { s.h[1].yaw += 4.f / float(n); s.Step(); }
                    s.Step(int(hz / 2));
                    Check(s.Owner() == 0, "#5 " + at + ": a 4 deg move by the other hand does not take it");
                }
                {   // review S1: both rays on a gap: an idle owner lets go, and the other hand needs a deliberate move to claim
                    auto released = [&](Sim& s) { s.stepMs = 1000 / hz; s.Step(5); s.Turn(0, 3, 150); s.Step(int(hz * 1.5)); };
                    { Sim s; released(s); Check(s.Owner() == -1 && s.out.why == pt::Why::None, "#5 S1 " + at + ": idle on a gap, the owner let go");
                      const int n = std::max(1, int(200 / s.stepMs)); for (int f = 0; f < n; ++f) { s.h[1].yaw += 4.f / float(n); s.Step(); }
                      Check(s.Owner() == -1, "#5 S1 " + at + ": 4 deg by the other hand does not claim"); }
                    { Sim s; released(s); double ms = 0; while (s.Owner() < 0 && ms < 2000) { s.h[1].yaw += 40 * float(s.stepMs) / 1000; s.Step(); ms += s.stepMs; }
                      Check(s.Owner() == 1 && s.out.why == pt::Why::Moved && ms <= 300, "#5 S1 " + at + ": a deliberate move by the other hand claims (" + std::to_string(int(ms)) + " ms)"); }
                    { Sim s; released(s); const int n = std::max(1, int(150 / s.stepMs)); for (int f = 0; f < n; ++f) { s.h[0].yaw += 3.f / float(n); s.Step(); }
                      Check(s.Owner() == 0, "#5 S1 " + at + ": the last hand reclaims on a plain turn"); }
                    { Sim s; released(s); s.h[1].trigger = true; s.Step();
                      Check(s.Owner() == 1 && s.out.why == pt::Why::Trigger, "#5 S1 " + at + ": the other hand's trigger still claims at once"); }
                    { Sim s; released(s); s.h[0].tracked = false; s.Step(); const int n = std::max(1, int(150 / s.stepMs)); for (int f = 0; f < n; ++f) { s.h[1].yaw += 3.f / float(n); s.Step(); }
                      Check(s.Owner() == 1, "#5 S1 " + at + ": the last hand out of view: a plain turn claims"); }
                    { Sim s; released(s); s.wheel = -1; s.Step(); s.wheel = 1; s.Step(5); const int n = std::max(1, int(150 / s.stepMs)); for (int f = 0; f < n; ++f) { s.h[1].yaw += 3.f / float(n); s.Step(); }
                      Check(s.Owner() == 1, "#5 S1 " + at + ": a close forgets the last hand"); }
                    {   // five minutes: 1.2 s still (the owner lets go), 1.3 s of noise on both hands
                        Sim s; released(s); int drops = 0, other = 0; const int cycle = int(2.5 * hz), quiet = int(1.2 * hz);
                        for (int f = 0; f < int(300 * hz); ++f) {
                            if (f % cycle >= quiet) { s.h[0].yaw = jitter(); s.h[1].yaw = jitter(); }
                            s.Step(); drops += s.out.why == pt::Why::Released; other += s.Owner() == 1;
                        }
                        Check(drops > 20 && other == 0, "#5 S1 " + at + ": noise on a gap for 5 min never reaches the other hand (" + std::to_string(drops) + " releases, " + std::to_string(other) + " frames)");
                    }
                }
                {   // an owner whose ray left the panel: no ratio against its turn
                    Sim s; start(s); s.h[0].onPanel = false; s.h[0].onTarget = false; double ms = 0;
                    while (s.Owner() == 0 && ms < 500) { s.h[0].yaw += 100 * float(s.stepMs) / 1000; s.h[1].yaw += 40 * float(s.stepMs) / 1000; s.Step(); ms += s.stepMs; }
                    Check(s.Owner() == 1 && ms <= 300, "#5 " + at + ": the owner off the panel: the other hand's move takes it whatever the owner's turn (" + std::to_string(int(ms)) + " ms)");
                }
            }
        }
        {   // untracked: lost at once
            Sim s; s.Step(5); s.Turn(0, 3, 100); s.h[0].tracked = false; s.Step();
            Check(s.Owner() == -1 && s.out.why == pt::Why::Untracked, "job B: the owner's aim untracked: lost at once");
        }
        {   // the row: Right, Left, Other hand (upstream's rule, the row's Off); a change drops a forbidden owner
            Sim r; r.allow = pt::Allow::Right; r.Step(5); r.Turn(1, 3, 100);
            Check(r.Owner() == -1, "job B row Right: the left hand never points");
            r.Turn(0, 3, 100);
            Check(r.Owner() == 0, "job B row Right: the right hand points");
            Sim l; l.allow = pt::Allow::Left; l.Step(5); l.Turn(0, 3, 100); l.Turn(1, 3, 100);
            Check(l.Owner() == 1, "job B row Left: only the left hand points");
            Sim o; o.allow = pt::Allow::OtherHand; o.wheel = 1; o.Step(5); o.Turn(1, 3, 100);
            Check(o.Owner() == -1, "job B row Other hand: the left wheel's own hand never points");
            o.Turn(0, 3, 100);
            Check(o.Owner() == 0, "job B row Other hand: the right hand (not holding the left wheel) points");
            Sim e; e.Step(5); e.Turn(1, 3, 100); e.allow = pt::Allow::Right; e.Step();
            Check(e.Owner() == -1 && e.out.why == pt::Why::Row, "job B: the row changed to Right drops the left owner (why=row)");
        }
        {   // the wheel closing drops the owner; a reopen starts with none
            Sim s; s.Step(5); s.Turn(0, 3, 100); s.wheel = -1; s.Step();
            Check(s.Owner() == -1 && s.out.why == pt::Why::Closed, "job B: the wheel closed, no owner (why=closed)");
            s.wheel = 1; s.Step(30);
            Check(s.Owner() == -1, "job B: reopened, no owner until a hand moves");
        }
        {   // the player's closing rule (FS1 dropped): B / X with no laser on the wheel closes it, an owner or none
            const int per[3] = {4, 4, 4};
            const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
            rw::QuickCast qc; rw::QuickIn q; q.main = true; q.nowMs = 1000; q.under = {};
            q.press[0] = 2;   // right B, plain
            Check(qc.Step(q, l).event == rw::Quick::Close, "#1: B with no laser on the wheel closes it");
        }
        {   // perf 'Pointing check' must-fix 1: the thin beam keeps 2 layer slots for the blood and strain quads; should-fix 2:
            // it is Off at the ring's Off edge (TabOn false), whatever the frame says.
            using B = pt::Beam;
            Check(pt::BeamFor(true, true, true, 10, 16) == B::Draw, "job B beam: 10 of 16 used, drawn (5 stay spare)");
            Check(pt::BeamFor(true, true, true, 13, 16) == B::Draw, "job B beam: 13 of 16, drawn (14 after it: 2 spare)");
            Check(pt::BeamFor(true, true, true, 14, 16) == B::NoSlot, "job B beam: 14 of 16, skipped (it would leave 1 spare)");
            Check(pt::BeamFor(true, true, true, 16, 16) == B::NoSlot && pt::BeamFor(true, true, true, 0, 0) == B::NoSlot && pt::BeamFor(true, true, true, 0, 2) == B::NoSlot,
                  "job B beam: full, a zero limit, a limit of 2: skipped");
            Check(pt::BeamFor(true, true, true, 0xFFFFFFFFu, 0xFFFFFFFFu) == B::NoSlot, "job B beam: no overflow at the top of the range");
            Check(pt::BeamFor(false, true, true, 0, 32) == B::MenuOff && pt::BeamFor(false, false, false, 0, 32) == B::MenuOff,
                  "job B beam: the ring's Off edge never draws, even with a beam written this frame (should-fix 2)");
            Check(pt::BeamFor(true, false, true, 0, 32) == B::Closed && pt::BeamFor(true, true, false, 0, 32) == B::None,
                  "job B beam: the wheel's laser not showing, or no beam written: none");
        }
    }
    {   // Feedback 1 item 2: grip to move (ring_wheel_grab.h over upstream's panels::Grab), the ring's rules and the critic's FS3 / FS4
        namespace m = framevr::math;
        const ia::Pose head{{0, 1.6f, 0}, {0, 0, 0, 1}};
        rw::Placement pl; pl.distanceM = .9f; pl.widthM = .8f; pl.belowDeg = 8; pl.worldLock = true;
        const ia::Pose base = rw::Spawn(head, pl);
        const m::Vec hand0{.15f, 1.35f, -.45f};
        auto in = [&](m::Vec handAt, bool grip, bool face = false, bool click = false) {
            rw::GrabIn g; g.open = true; g.base = base; g.head = head; g.hand = {handAt, {0, 0, 0, 1}}; g.handTracked = true;
            g.grip = grip; g.face = face; g.click = click; g.hit = true; return g;
        };
        auto same = [](const ia::Pose& a, const ia::Pose& b) { return std::memcmp(&a, &b, sizeof a) == 0; };
        auto dist = [](const ia::Pose& a, const ia::Pose& b) { const m::Vec d = a.p - b.p; return std::sqrt(m::Dot(d, d)); };
        {   // Off: the spawn pose byte for byte, whatever the hand does
            rw::RingGrab g; bool ok = true;
            for (int i = 0; i < 20; ++i) { auto x = in(hand0 + m::Vec{.02f * i, 0, 0}, true); x.on = false; ok &= same(g.Step(x).pose, base) && !g.Moving(); }
            Check(ok, "grab Off: the spawn pose exactly (byte-identical), never moving");
        }
        {   // On, untouched: byte-identical too
            rw::RingGrab g; bool ok = true;
            for (int i = 0; i < 5; ++i) ok &= same(g.Step(in(hand0, false)).pose, base);
            Check(ok, "grab On, no grip: the spawn pose exactly");
        }
        {   // under 3 cm: no move; 4 cm: moving, the wheel follows 1:1
            rw::RingGrab g;
            g.Step(in(hand0, true));
            const auto a = g.Step(in(hand0 + m::Vec{.025f, 0, 0}, true));
            Check(!g.Moving() && same(a.pose, base), "grip with 2.5 cm of travel: no move (upstream's 3 cm slack)");
            const auto b = g.Step(in(hand0 + m::Vec{.04f, 0, 0}, true));
            Check(g.Moving() && b.event == rw::GrabEvent::Start && std::fabs(dist(b.pose, base) - .04f) < .005f, "grip alone with 4 cm of travel: moving, the wheel 4 cm over (" + std::to_string(dist(b.pose, base)) + " m)");
            const auto c = g.Step(in(hand0 + m::Vec{.04f, 0, 0}, false));
            Check(!g.Moving() && c.event == rw::GrabEvent::End && std::fabs(dist(c.pose, base) - .04f) < .005f, "grip released: the move ends, the wheel stays where it was put");
        }
        {   // FS3: grip + A held 1.2 s with a 4 cm drift assigns, the wheel stays; the trigger too
            for (int k = 0; k < 2; ++k) {
                rw::RingGrab g; bool still = true;
                g.Step(in(hand0, true));
                for (int i = 1; i <= 108; ++i) { const auto o = g.Step(in(hand0 + m::Vec{.04f * i / 108, 0, 0}, true, k == 0, k == 1)); still &= same(o.pose, base) && !g.Moving(); }
                Check(still, std::string(k ? "FS3 grip + trigger" : "FS3 grip + A") + " held with a 4 cm drift: never a move (the press still binds or casts)");
                g.Step(in(hand0 + m::Vec{.04f, 0, 0}, false));
                g.Step(in(hand0, true)); const auto o = g.Step(in(hand0 + m::Vec{.05f, 0, 0}, true));
                Check(g.Moving() && !same(o.pose, base), "after the grip is let go, a new grip moves again");
            }
        }
        {   // reopen keeps the offset (Remember On); Remember Off, a Placement change, Reset place each put it back; a new session too
            auto moved = [&](rw::RingGrab& g) { g.Step(in(hand0, true)); g.Step(in(hand0 + m::Vec{0, .1f, 0}, true)); g.Step(in(hand0 + m::Vec{0, .1f, 0}, false)); };
            rw::RingGrab g; moved(g);
            auto closed = in(hand0, false); closed.open = false; g.Step(closed);
            auto re = in(hand0, false); re.spawned = true;
            const auto o = g.Step(re);
            Check(o.event == rw::GrabEvent::None && std::fabs(dist(o.pose, base) - .1f) < .005f, "reopen (Remember On): the moved place, 10 cm up");
            auto off = re; off.remember = false;
            Check(g.Step(off).event == rw::GrabEvent::Cleared && same(g.Step(in(hand0, false)).pose, base), "reopen with Remember Off: back at the default");
            moved(g); auto pc = in(hand0, false); pc.placementChanged = true;
            Check(g.Step(pc).event == rw::GrabEvent::Cleared && same(g.Step(in(hand0, false)).pose, base), "a Placement row change: back at the default");
            moved(g); g.Step(closed); auto pcc = closed; pcc.placementChanged = true; g.Step(pcc);
            Check(same(g.Step(re).pose, base), "a Placement change while closed: the next open at the default");
            moved(g); auto rs = in(hand0, false); rs.reset = true;
            Check(g.Step(rs).event == rw::GrabEvent::Cleared && same(g.Step(in(hand0, false)).pose, base), "Reset place: back at the default");
            moved(g); g.NewSession();
            Check(same(g.Step(re).pose, base), "a new game session: back at the default (nothing saved)");
        }
        {   // FS4: a remembered place over 60 deg from the gaze respawns at the default
            rw::RingGrab g;
            g.Step(in(hand0, true)); g.Step(in(hand0 + m::Vec{2.f, 0, 0}, true)); const auto far = g.Step(in(hand0 + m::Vec{2.f, 0, 0}, false));
            const float deg = rw::GazeDeg(head, far.pose.p);
            auto re = in(hand0, false); re.spawned = true;
            const auto o = g.Step(re);
            Check(deg > 60 && o.event == rw::GrabEvent::Respawned && same(o.pose, base), "FS4 the remembered place " + std::to_string(int(deg)) + " deg from the gaze: respawned at the default");
            const m::Vec d = far.pose.p - head.p;
            Check(std::sqrt(m::Dot(d, d)) <= 1.5f + 1e-3f, "upstream's Limit: never past 1.5 m from the head");
        }
        {   // a page change or a close ends a grab; a lost hand lets go
            rw::RingGrab g;
            g.Step(in(hand0, true)); g.Step(in(hand0 + m::Vec{.05f, 0, 0}, true));
            auto pg = in(hand0 + m::Vec{.1f, 0, 0}, true); pg.pageChanged = true;
            const auto o = g.Step(pg);
            Check(!g.Moving() && o.event == rw::GrabEvent::End, "a page change ends the grab");
            const auto after = g.Step(in(hand0 + m::Vec{.2f, 0, 0}, true));
            Check(!g.Moving() && dist(after.pose, o.pose) < 1e-4f, "after a page change the held grip moves nothing until let go");
            rw::RingGrab h;
            h.Step(in(hand0, true)); h.Step(in(hand0 + m::Vec{.05f, 0, 0}, true));
            auto cl = in(hand0 + m::Vec{.05f, 0, 0}, true); cl.open = false;
            Check(h.Step(cl).event == rw::GrabEvent::End && !h.Moving(), "the wheel closing ends the grab");
            rw::RingGrab k;
            k.Step(in(hand0, true)); k.Step(in(hand0 + m::Vec{.05f, 0, 0}, true));
            auto lost = in(hand0 + m::Vec{.05f, 0, 0}, true); lost.handTracked = false;
            Check(k.Step(lost).event == rw::GrabEvent::End && !k.Moving(), "a hand losing tracking lets go");
        }
        {   // the grip off the panel never grabs (it stays the grip layer)
            rw::RingGrab g; auto x = in(hand0, true); x.hit = false; g.Step(x);
            auto y = in(hand0 + m::Vec{.1f, 0, 0}, true); y.hit = false;
            Check(same(g.Step(y).pose, base) && !g.Moving(), "a grip with the ray off the wheel: no move");
        }
        {   // review-grip S3 (batch1 merge): the grabbing hand is the pointing owner; with none, the hand not holding the wheel
            Check(rw::GrabHand(1, 1) == 1 && rw::GrabHand(0, 0) == 0, "S3: an owner pointing with the wheel's own hand grabs with it");
            Check(rw::GrabHand(0, 1) == 0 && rw::GrabHand(1, 0) == 1, "S3: an owner on the other hand grabs with that hand");
            Check(rw::GrabHand(-1, 1) == 0 && rw::GrabHand(-1, 0) == 1 && rw::GrabHand(-1, -1) == 0, "S3: no owner: the hand not holding the wheel, as before job B (no wheel: 0, the old formula's)");
        }
        {   // review-grip S1: on an upstream page, a trigger squeezed mid-move reaches no cell
            rw::RingGrab g; g.Step(in(hand0, true)); g.Step(in(hand0 + m::Vec{.05f, 0, 0}, true));
            g.Step(in(hand0 + m::Vec{.06f, 0, 0}, true, false, true));
            Check(g.Moving(), "S1: a trigger squeezed while moving keeps the move (Held)");
            int hover = 5; bool hitNow = true;
            rw::HoldUpstreamWhileMoving(g.Moving(), false, hover, hitNow);
            Check(hover == -1 && !hitNow, "S1: an upstream page mid-move: no hover cell, no hit, so the trigger clicks nothing");
            hover = 5; hitNow = true; rw::HoldUpstreamWhileMoving(g.Moving(), true, hover, hitNow);
            Check(hover == 5 && hitNow, "S1: a ring page is untouched (Input drops its events)");
            hover = 5; hitNow = true; rw::HoldUpstreamWhileMoving(false, false, hover, hitNow);
            Check(hover == 5 && hitNow, "S1: not moving: upstream's hover and hit stand");
        }
        {   // review-grip S2 (mutation G8): move, Grab to move Off for one step, On again: the spawn pose, not the old place
            rw::RingGrab g; g.Step(in(hand0, true)); g.Step(in(hand0 + m::Vec{.1f, 0, 0}, true)); g.Step(in(hand0 + m::Vec{.1f, 0, 0}, false));
            Check(!rw::IsZero(g.Kept()), "S2: moved and let go: a kept place");
            auto off = in(hand0, false); off.on = false; g.Step(off);
            Check(same(g.Step(in(hand0, false)).pose, base) && rw::IsZero(g.Kept()), "S2: Off for one step, then On: the spawn pose (the old place is gone)");
        }
        {   // review-grip M1: grip let go first with the trigger still down (upstream's Held): still consumed until the trigger is up
            rw::RingGrab g; g.Step(in(hand0, true)); g.Step(in(hand0 + m::Vec{.05f, 0, 0}, true));
            g.Step(in(hand0 + m::Vec{.06f, 0, 0}, true, false, true));
            Check(g.Moving() && g.Holding(), "M1: moving with the trigger squeezed: holding");
            const auto up = g.Step(in(hand0 + m::Vec{.06f, 0, 0}, false, false, true));
            Check(!g.Moving() && g.Holding(), "M1: the grip let go, the trigger still down: not moving, still holding (Held)");
            int hover = 5; bool hitNow = true; rw::HoldUpstreamWhileMoving(g.Holding(), false, hover, hitNow);
            Check(hover == -1 && !hitNow, "M1: Held on an upstream page: no hover, no hit, so the trigger fires nothing");
            const auto after = g.Step(in(hand0 + m::Vec{.2f, 0, 0}, false));
            Check(!g.Holding() && dist(after.pose, up.pose) < 1e-4f, "M1: the trigger let go: no longer holding, the place kept");
        }
        {   // review-grip S4: the other hand takes over mid-move (both grips down): no jump, no move until a fresh grip
            rw::RingGrab g;
            auto h0 = [&](m::Vec at, bool grip) { auto x = in(at, grip); x.handIndex = 0; return x; };
            auto h1 = [&](m::Vec at, bool grip) { auto x = in(at, grip); x.handIndex = 1; return x; };
            g.Step(h0(hand0, true)); const auto was = g.Step(h0(hand0 + m::Vec{.05f, 0, 0}, true));
            Check(g.Moving(), "S4: the right hand moving the wheel");
            const m::Vec left{-.25f, 1.3f, -.4f};
            const auto sw = g.Step(h1(left, true));
            Check(!g.Moving() && dist(sw.pose, was.pose) < 1e-4f, "S4: the left hand takes over with its grip down: no jump");
            const auto more = g.Step(h1(left + m::Vec{.1f, 0, 0}, true));
            Check(!g.Moving() && dist(more.pose, was.pose) < 1e-4f, "S4: the held left grip moves nothing (a fresh grip is needed)");
            g.Step(h1(left, false)); g.Step(h1(left, true)); const auto fresh = g.Step(h1(left + m::Vec{.05f, 0, 0}, true));
            Check(g.Moving() && dist(fresh.pose, was.pose) > .03f, "S4: a fresh left grip moves it");
            Check(rw::GripDown == .5f, "S4: one grip threshold for the grab and the owner's pin");
        }
    }
    {   // R8 match stage 1 (r8-match-spec 6.1, 7.3): the rest glow per skill, its cap, the panel-wide rest events
        using K = rw::RestKind;
        const auto* tiger = rw::SkillFxOf(254), *fire = rw::SkillFxOf(251), *phoenix = rw::SkillFxOf(280);
        auto named = [](K k, const std::string& n) { return rw::ValidKind(k) && n == rw::RestKindName[uint32_t(k)]; };
        // stage 1b: the class pages' lists (assassin/class.js ov): Tiger Strike = marim + charge:#E8D8B0, Fire Blast = trap +
        // tint:#E8591A, Phoenix Strike = marim + charge:#FFD080 (the page drops the data file's multi)
        Check(tiger && named(tiger->kind[0], "assassin/marim") && named(tiger->kind[1], "assassin/charge") && tiger->rgb[1] == 0xE8D8B0 && tiger->fromSeats, "stage 1b table: Tiger Strike = marim + charge #E8D8B0");
        Check(fire && named(fire->kind[0], "assassin/trap") && named(fire->kind[1], "assassin/tint") && fire->rgb[1] == 0xE8591A, "stage 1b table: Fire Blast = trap + tint #E8591A");
        Check(phoenix && named(phoenix->kind[0], "assassin/marim") && named(phoenix->kind[1], "assassin/charge") && phoenix->rgb[1] == 0xFFD080, "stage 1b table: Phoenix Strike = marim + charge #FFD080");
        int fromSeats = 0, classKinds = 0;
        for (int i = 0; i < rw::SkillFxCount; ++i) { fromSeats += rw::SkillFxTable[i].fromSeats; for (const K k : rw::SkillFxTable[i].kind) classKinds += rw::ValidKind(k) && uint32_t(k) >= 12; }
        Check(fromSeats >= 200 && classKinds > 150, "stage 1b table: the class pages' lists for the seated skills (" + std::to_string(fromSeats) + " skills, " + std::to_string(classKinds) + " class kinds)");
        bool kindsOk = true; for (int i = 0; i < rw::SkillFxCount; ++i) for (const K k : rw::SkillFxTable[i].kind) kindsOk &= k == K::None || rw::ValidKind(k);
        Check(kindsOk && rw::RestKindCount > 12 && rw::RestKindCount < 255, "stage 1b every table kind is a known kind (" + std::to_string(rw::RestKindCount) + ")");
        Check(rw::SkillFxCount == 240 && !rw::SkillFxOf(0) && !rw::SkillFxOf(5000), "stage 1 table: 240 kit skills; ids the kit lacks: none");
        bool sorted = true; for (int i = 1; i < rw::SkillFxCount; ++i) sorted &= rw::SkillFxTable[i - 1].id < rw::SkillFxTable[i].id;
        Check(sorted, "stage 1 table sorted by id (SkillFxOf's search)");
        const auto fb = rw::RestGlowOf(5000, 6, 1);
        Check(fb.kind[0] == K(rw::ElementOf(6, 1)) && fb.kind[1] == K::None, "stage 1 a skill the kit lacks: its tree's element");
        Check(rw::ElementOfGlow(rw::RestGlowOf(254, 6, 2)) == rw::Element::Phys && rw::ElementOfGlow(rw::RestGlowOf(5000, 1, 0)) == rw::ElementOf(1, 0), "stage 1 hover element: charge skipped; a skill the kit lacks: its tree's");
        // The events gate: 40 icons with events over 60 s: at most 3 alive (each event its own length), at most 3 starts in any 1 s,
        // none within 250 ms of another; every icon gets its event each cycle.
        for (int events : {1, 5, 18, 40}) {
            std::vector<double> starts; int maxAlive = 0;
            std::vector<double> was(size_t(events), -1);
            for (int i = 0; i < events; ++i) was[size_t(i)] = rw::RestEventAge(i, events, -1, 8000);
            for (double t = 0; t < 60000; t += 1) {
                int alive = 0;
                for (int i = 0; i < events; ++i) {
                    const double a = rw::RestEventAge(i, events, t, 8000);
                    const double len = i % 3 == 0 ? rw::RestChargeMs : i % 3 == 1 ? rw::RestGlintMs : rw::RestBurstMs;
                    alive += a >= 0 && a < len;
                    if (a >= 0 && was[size_t(i)] < 0) starts.push_back(t);
                    was[size_t(i)] = a;
                }
                maxAlive = std::max(maxAlive, alive);
            }
            std::sort(starts.begin(), starts.end());
            double minGap = 1e9; int maxPerS = 0;
            for (size_t i = 1; i < starts.size(); ++i) minGap = std::min(minGap, starts[i] - starts[i - 1]);
            for (size_t i = 0; i < starts.size(); ++i) { int c = 0; for (size_t j = i; j < starts.size() && starts[j] - starts[i] < 1000; ++j) ++c; maxPerS = std::max(maxPerS, c); }
            const double cycle = rw::RestEventCycleMs(events, 8000);
            Check(maxAlive <= 3 && maxPerS <= 3 && (events == 1 || minGap >= 250) && starts.size() >= size_t(events) * size_t(60000 / cycle),
                  "stage 1 rest events (" + std::to_string(events) + " icons): alive " + std::to_string(maxAlive) + " <= 3, " + std::to_string(maxPerS) + " a second <= 3, gap " + std::to_string(int(minGap)) + " ms >= 250");
        }
        Check(rw::RestEventCycleMs(3, 0) >= 6000 && rw::RestEventCycleMs(3, 12000) >= 12000 && rw::RestEventCycleMs(40, 0) >= 40 * 334, "stage 1 the cycle: 6 s, the crawl row's when longer, a slot each");
        // Build: every rest quad within the cap by geometry; per seat its kinds and colors; the events gated over a 10 s loop.
        const int per[3] = {10, 10, 10};
        const auto l = ClassLayout(6, per, rw::TreeLayout::Shown);
        std::vector<rw::RestGlow> rest(size_t(l.count));
        int overId = -1;   // a skill with a pass over the face (a lightning or steel kind)
        for (int i = 0; i < rw::SkillFxCount && overId < 0; ++i) if (rw::OverFace(rw::SkillFxTable[i].kind[0])) overId = rw::SkillFxTable[i].id;
        Check(overId >= 0, "stage 1b a skill with an over-face pass exists");
        for (int i = 0; i < l.count; ++i) rest[size_t(i)] = rw::RestGlowOf(i % 3 == 2 ? overId : i % 2 ? 254 : 280, 6, l.seats[size_t(i)].tree);   // marim + charge, a lightning or steel kind
        rw::Scene s; s.open = true; s.layout = &l; s.rest = rest.data(); s.anim = rw::PresetAnim(rw::Preset::All); s.skillClass = 6;
        static rw::Instance list[rw::MaxDraw];
        float maxHalf = 0; int quads = 0, tinted = 0, over = 0, maxEvents = 0;
        for (double t = 0; t < 10000; t += 50) {
            s.tMs = t; int ev = 0;
            const int n = rw::Build(s, list);
            for (int i = 0; i < n; ++i) {
                if (list[i].recipe != rw::RecipeFx || list[i].param >= 768 || (list[i].param >= 256 && list[i].param < 512)) continue;
                ++quads; maxHalf = std::max(maxHalf, list[i].w / 2);
                if (std::fabs(list[i].r - .910f) < .01f && std::fabs(list[i].g - .847f) < .01f) ++tinted;   // #E8D8B0
                if (list[i].param >= 512) ++over;
                const uint32_t kind = uint32_t(list[i].param) % 256;
                if (list[i].param < 256 && kind < uint32_t(rw::RestKindCount) && rw::RestKindEvent[kind] && list[i].u1 >= 0 && list[i].u1 < rw::RestChargeMs) ++ev;
            }
            maxEvents = std::max(maxEvents, ev);
        }
        Check(quads > 0 && maxHalf <= rw::RestCapPx + .001f, "stage 1 rest cap by geometry: every rest quad's half-size <= 28.6 px (" + std::to_string(maxHalf) + ")");
        Check(tinted > 0 && over > 0, "stage 1 per seat: the charge color reaches its quad; a lightning or steel kind draws an over-face pass");
        Check(maxEvents >= 1 && maxEvents <= 3, "stage 1 Build: at most 3 rest events alive on a panel of event skills (" + std::to_string(maxEvents) + ")");
        s.anim = rw::PresetAnim(rw::Preset::Off);
        int evOff = 0; const int nOff = rw::Build(s, list);
        for (int i = 0; i < nOff; ++i) if (list[i].recipe == rw::RecipeFx && list[i].u1 >= 0) ++evOff;
        Check(evOff == 0, "stage 1 Off: no rest event (a frozen glow)");
        int seatsDrawn = 0; for (int i = 0; i < nOff; ++i) seatsDrawn += list[i].recipe == rw::RecipeSeat;
        Check(seatsDrawn == l.count, "stage 1 every seat drawn as the kit's icon (RecipeSeat)");
    }
    {   // Review-r6 X1: a retired swapchain is destroyed on the frame thread (Reap's caller), only after its worker exited;
        // the worker only releases the GPU side.
        struct Side { std::thread worker; std::atomic<bool> exited{false}; std::atomic<bool> released{false}; bool chain = true; };
        const auto frame = std::this_thread::get_id();
        std::atomic<bool> go{false};
        auto* a = new Side; auto* b = new Side;
        for (Side* s : {a, b}) s->worker = std::thread([s, &go] { while (!go) std::this_thread::sleep_for(std::chrono::milliseconds(1)); s->released = true; s->exited = true; });
        rw::Retired<Side> list; list.Add(a); list.Add(b);
        std::vector<Side*> freed;
        std::vector<std::thread::id> destroyedOn; std::vector<bool> afterExit;
        auto reap = [&](bool wait) {
            return list.Reap(wait, [](Side* s) { return s->exited.load(); }, [](Side* s) { if (s->worker.joinable()) s->worker.join(); },
                [&](Side* s) { destroyedOn.push_back(std::this_thread::get_id()); afterExit.push_back(s->exited.load() && s->released.load()); s->chain = false; },
                [&](Side* s) { freed.push_back(s); });
        };
        const auto t0 = std::chrono::steady_clock::now();
        Check(reap(false) == 0 && list.Size() == 2 && destroyedOn.empty(), "X1 workers still running: Reap destroys nothing, frees nothing");
        Check(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(50), "X1 Reap never waits for a running worker (the frame thread)");
        go = true;
        for (int i = 0; i < 2000 && !(a->exited && b->exited); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Check(reap(false) == 2 && list.Empty(), "X1 both workers exited: both freed");
        bool sameThread = destroyedOn.size() == 2, ordered = afterExit.size() == 2;
        for (const auto& id : destroyedOn) sameThread = sameThread && id == frame;
        for (const bool x : afterExit) ordered = ordered && x;
        Check(sameThread, "X1 every swapchain destroyed on the frame thread, never on its worker");
        Check(ordered, "X1 each destroyed only after its worker released the GPU side and exited");
        Check(!a->chain && !b->chain && freed.size() == 2, "X1 each swapchain destroyed once, each side freed once");
        delete a; delete b;
        auto* c = new Side;   // OnUiReset: wait joins a running worker first, then destroys on the caller's thread
        c->worker = std::thread([c] { std::this_thread::sleep_for(std::chrono::milliseconds(30)); c->released = true; c->exited = true; });
        list.Add(c); destroyedOn.clear(); afterExit.clear(); freed.clear();
        Check(reap(true) == 1 && destroyedOn.size() == 1 && destroyedOn[0] == frame && afterExit[0], "X1 OnUiReset: joins the worker, then destroys on its own thread");
        delete c;
    }
    {   // Perf's X1 check: the Off edge (Layer's !on drain, the hook's last call) destroys the swapchain itself, on the frame
        // thread, once the last compose is done; the hook then goes dark and nothing waits for the next On.
        struct Gpu { bool chain = true; int destroys = 0; bool retired = false; bool destroyedBeforeRetire = false; uint64_t submitted = 0, completed = 0; };
        double clock = 0;
        int waits = 0; unsigned askedMs = 0;
        auto off = [&](rw::Retired<Gpu>& list, Gpu* g, uint64_t gpuDoneAtMs) {
            waits = 0; askedMs = 0;
            // a fence-event wait: returns when the GPU finishes or the given ms run out, whichever first
            return list.Off(g, 16, [&](Gpu* x, unsigned ms) {
                    ++waits; askedMs = ms;
                    if (x->completed >= x->submitted) return true;
                    const double done = double(gpuDoneAtMs), until = clock + ms;
                    clock = done <= until ? std::max(clock, done) : until;
                    if (clock >= done) x->completed = x->submitted;
                    return x->completed >= x->submitted; },
                [&] { return clock; },
                [](Gpu* x) { if (x->chain) { x->chain = false; ++x->destroys; } x->destroyedBeforeRetire = !x->retired; },
                [](Gpu* x) { x->retired = true; });
        };
        {   // the usual case: the last compose finished a frame ago
            rw::Retired<Gpu> list; Gpu g; g.submitted = g.completed = 41; clock = 0;
            const auto r = off(list, &g, 0);
            Check(r.destroyed && !g.chain && g.destroys == 1 && r.ms == 0 && g.destroyedBeforeRetire && g.retired && list.Size() == 1,
                  "X1 Off edge, GPU idle: the swapchain destroyed in the drain call itself (0 ms), before its side is retired");
            int later = 0;
            list.Reap(true, [](Gpu*) { return true; }, [](Gpu*) {}, [&](Gpu* x) { if (x->chain) ++later; }, [](Gpu*) {});
            Check(later == 0 && g.destroys == 1, "X1 Reap later finds it already destroyed (once only)");
        }
        {   // the last compose still on the GPU for 5 ms: waited, then destroyed
            rw::Retired<Gpu> list; Gpu g; g.submitted = 42; g.completed = 41; clock = 100;
            const auto r = off(list, &g, 105);
            Check(r.destroyed && !g.chain && r.ms >= 5 && r.ms <= 16, "X1 Off edge, a compose in flight: waited " + std::to_string(int(r.ms)) + " ms (at most 16), then destroyed");
        }
        {   // a GPU stuck past 16 ms: the frame thread never waits longer; kept for Reap (said in the log)
            rw::Retired<Gpu> list; Gpu g; g.submitted = 43; g.completed = 41; clock = 200;
            const auto r = off(list, &g, 1000000);
            Check(!r.destroyed && g.chain && r.ms >= 16 && r.ms <= 16 && g.retired && list.Size() == 1, "X1 Off edge, GPU busy past 16 ms: kept for Reap, the wait bounded");
            Check(waits == 1 && askedMs == 16, "X1 the bound: one wait of at most 16 ms (no polling loop)");
            int later = 0;
            list.Reap(true, [](Gpu*) { return true; }, [](Gpu*) {}, [&](Gpu* x) { if (x->chain) { x->chain = false; ++later; } }, [](Gpu*) {});
            Check(later == 1, "X1 then Reap destroys it");
        }
        rw::Retired<Gpu> none;
        Check(!none.Off(static_cast<Gpu*>(nullptr), 16, [](Gpu*, unsigned) { return true; }, [] { return 0.0; }, [](Gpu*) {}, [](Gpu*) {}).destroyed && none.Empty(),
              "X1 Off with nothing made: nothing destroyed, nothing kept");
    }
    {   // Review-r6 X6: rapid On / Off never holds two ring swapchains. Frames run Layer's order: an Off edge = Retired::Off
        // (16 ms bound); an On frame = Reap(false), then Setup's MayCreate gate. The GPU is busy at some Off edges (the
        // swapchain stays for Reap), and a worker takes a few frames to exit.
        struct Side { bool chain = true; int exitAt = 0; bool busy = false; };
        rw::Retired<Side> list;
        Side* live = nullptr;
        int chains = 0, most = 0, made = 0, waited = 0, frame = 0;
        auto holds = [](Side* g) { return g->chain; };
        auto onFrame = [&](bool on) {
            ++frame;
            if (!on) {
                if (live) {
                    Side* g = live; live = nullptr;
                    list.Off(g, 16, [](Side* s, unsigned) { return !s->busy; }, [] { return 0.0; },
                             [&](Side* s) { if (s->chain) { s->chain = false; --chains; } }, [&](Side* s) { s->exitAt = frame + 3; });
                }
                return;
            }
            list.Reap(false, [&](Side* s) { return frame >= s->exitAt; }, [](Side*) {}, [&](Side* s) { if (s->chain) { s->chain = false; --chains; } },
                      [](Side* s) { delete s; });
            if (live) return;
            if (!list.MayCreate(false, holds)) { ++waited; return; }
            live = new Side; live->busy = (made % 3) == 1; ++made; ++chains;   // every third one's GPU still busy at its Off edge
            most = std::max(most, chains);
        };
        for (int i = 0; i < 60; ++i) onFrame(i % 2 == 0);   // On, Off, On, Off ... every frame
        for (int i = 0; i < 10; ++i) onFrame(true);
        most = std::max(most, chains);
        Check(most <= 1, "X6 rapid On / Off with a busy GPU: never two ring swapchains at once (most " + std::to_string(most) + ")");
        Check(waited > 0 && made > 3 && live && chains == 1, "X6 an On edge with a retired swapchain alive waits, then makes its own (" + std::to_string(waited) + " waits, " + std::to_string(made) + " made)");
        list.Reap(true, [](Side*) { return true; }, [](Side*) {}, [&](Side* s) { if (s->chain) { s->chain = false; --chains; } }, [](Side* s) { delete s; });
        delete live;
        Check(list.Empty(), "X6 reset: every retired side freed");
        rw::Retired<Side> empty;
        Check(empty.MayCreate(false, holds) && !empty.MayCreate(true, holds), "X6 the gate: no retired swapchain and none made = make one; one made = never a second");
    }
    {   // Perf's X6 check: the wait ends when the GPU is done, not when the worker exits. Worker 30 frames to leave (a build
        // stage), GPU done three frames after the Off edge (review-watch S1: two On frames see it busy, so ignoring gpuDone
        // fails): On frames run Layer's order (DestroyDone, Reap, then the gate).
        struct Side { bool chain = true; int exitAt = 0, gpuDoneAt = 0; };
        rw::Retired<Side> list;
        Side* live = new Side;
        int frame = 0, chains = 1, most = 1, waitFrames = 0, longest = 0;
        auto holds = [](Side* g) { return g->chain; };
        auto destroy = [&](Side* s) { if (s->chain) { s->chain = false; --chains; } };
        auto onFrame = [&](bool on) {
            ++frame;
            if (!on) {
                if (live) { Side* g = live; live = nullptr;
                    list.Off(g, 16, [](Side*, unsigned) { return false; }, [] { return 0.0; }, destroy,   // busy past the bound: kept
                             [&](Side* s) { s->exitAt = frame + 30; s->gpuDoneAt = frame + 3; }); }
                return;
            }
            list.DestroyDone(holds, [&](Side* s) { return frame >= s->gpuDoneAt; }, destroy);
            list.Reap(false, [&](Side* s) { return frame >= s->exitAt; }, [](Side*) {}, destroy, [](Side* s) { delete s; });
            if (live) return;
            if (!list.MayCreate(false, holds)) { ++waitFrames; longest = std::max(longest, waitFrames); return; }
            waitFrames = 0; live = new Side; ++chains; most = std::max(most, chains);
        };
        onFrame(false);   // Off: GPU busy, the swapchain kept, the worker still running
        Check(chains == 1 && list.Size() == 1, "perf X6: Off with the GPU busy keeps the swapchain for later");
        onFrame(true);    // On, frames 2 and 3: the GPU still busy, so the old swapchain stays and no new one is made
        const bool alive1 = chains == 1 && !live && list.Size() == 1 && list.AnyHolds(holds);
        onFrame(true);
        const bool alive2 = chains == 1 && !live && list.Size() == 1 && list.AnyHolds(holds);
        Check(alive1 && alive2, "perf X6: GPU busy 2 On frames after the Off: the old swapchain stays alive, no second one made");
        onFrame(true);    // frame 4: the GPU is done, the worker is not
        Check(chains == 1 && live && list.Size() == 1, "perf X6: GPU done, worker still running: the old swapchain destroyed and the new one made that frame");
        for (int i = 0; i < 40; ++i) onFrame(i % 3 != 0);   // keeps toggling
        Check(most <= 1, "perf X6: never two at once (" + std::to_string(most) + ")");
        Check(longest <= 2, "perf X6: an On waits at most 2 frames, though each worker takes 30 (longest " + std::to_string(longest) + ")");
        list.Reap(true, [](Side*) { return true; }, [](Side*) {}, destroy, [](Side* s) { delete s; });
        delete live;
    }
    {   // Review-r6 X4: every ring request is answered exactly once (its reply, or refused after 1.5 s), and the next one waits
        using R = ia::Result;
        rw::ReplyWait w;
        Check(w.Step(0) == rw::ReplyEnd::None && !w.Waiting(), "X4 nothing sent: nothing to answer");
        w.Sent(7, 1000);
        Check(w.Reply(6, R::Applied) == rw::ReplyEnd::None && w.Reply(7, R::Pending) == rw::ReplyEnd::None && w.Waiting(), "X4 another request's reply, or Pending: still waiting");
        Check(w.Reply(7, R::Dispatched) == rw::ReplyEnd::Applied && !w.Waiting(), "X4 our reply, Dispatched: applied, the wait ends");
        w.Sent(8, 1000);
        Check(w.Reply(8, R::Refused) == rw::ReplyEnd::Refused && rw::IsRefusal(rw::ReplyEnd::Refused), "X4 our reply, Refused: refused");
        w.Sent(9, 1000);
        Check(w.Step(2499) == rw::ReplyEnd::None && w.Waiting(), "X4 no reply for 1499 ms: still waiting");
        const auto t = w.Step(2500);
        Check(t == rw::ReplyEnd::TimedOut && rw::IsRefusal(t) && !w.Waiting() && std::string(rw::ReplyEndName(t)).find("refused") == 0, "X4 no reply in 1.5 s: refused, said as refused");
        Check(w.Step(9000) == rw::ReplyEnd::None && w.Reply(9, R::Applied) == rw::ReplyEnd::None, "X4 answered once: a late reply or a later step says nothing more");
        // A fake transport queue like interactionstate::Request: while a request waits, a new one is dropped without a reply.
        struct Queue { uint64_t holding = 0; bool Take(uint64_t s) { if (holding) return false; holding = s; return true; } } q;
        rw::ReplyWait bind, glyph;
        int answers = 0, sent = 0, dropped = 0;
        uint64_t seq = 0;
        const int wants = 5;
        int wanted = 0;
        for (double now = 0; now < 20000 && answers < wants; now += 11) {
            if (q.holding && std::fmod(now, 99) < 11) { if (bind.Reply(q.holding, R::Applied) != rw::ReplyEnd::None) ++answers; q.holding = 0; }   // transport answers every 99 ms
            if (bind.Step(now) != rw::ReplyEnd::None) ++answers;
            if (wanted < wants && rw::CanSend(bind, glyph)) { ++seq; ++sent; ++wanted; if (!q.Take(seq)) ++dropped; bind.Sent(seq, now); }
        }
        Check(sent == wants && dropped == 0 && answers == wants, "X4 five binds in a row: each sent only after the one before was answered, none dropped, each answered (" +
              std::to_string(answers) + " of " + std::to_string(wants) + ", " + std::to_string(dropped) + " dropped)");
        rw::ReplyWait other; other.Sent(1, 0);
        Check(!rw::CanSend(bind, other) && !rw::CanSend(other, glyph), "X4 a glyph waiting holds a bind back, and the other way round");
        // Transport never answers (upstream's own request held the queue): refused after 1.5 s, then the next one goes.
        rw::ReplyWait lost; lost.Sent(40, 0);
        int stepsTo = 0; rw::ReplyEnd e = rw::ReplyEnd::None;
        for (double now = 0; e == rw::ReplyEnd::None && now < 5000; now += 11) { e = lost.Step(now); ++stepsTo; }
        Check(e == rw::ReplyEnd::TimedOut && rw::CanSend(lost, glyph), "X4 a dropped request: refused at 1.5 s, the next one may go");
    }
    {   // Perf's X4 check: requests made but not sent wait in order; one slot let a third quick assign drop the second, unsent
        rw::SendQueue<int> q;
        int out = 0;
        Check(!q.Pop(out) && q.Size() == 0, "send queue: empty pops nothing");
        bool pushed = true;
        for (int i = 1; i <= rw::SendDepth; ++i) pushed = pushed && q.Push(i);
        Check(pushed && q.Size() == rw::SendDepth && !q.Push(99) && q.Size() == rw::SendDepth, "send queue: four wait; a fifth is refused (Push false), none overwritten");
        bool order = true;
        for (int i = 1; i <= rw::SendDepth; ++i) order = order && q.Pop(out) && out == i;
        Check(order && !q.Pop(out), "send queue: sent oldest first, each once");
        for (int i = 0; i < 10; ++i) { q.Push(i); q.Pop(out); }   // wraps round
        q.Push(7); q.Push(8);
        Check(q.Pop(out) && out == 7 && q.Pop(out) && out == 8, "send queue: order kept across the wrap");
        q.Push(1); q.Clear();
        Check(q.Size() == 0 && !q.Pop(out), "send queue: Clear (ring Off, reset) empties it");
        // The bug: bind A in flight, then assigns B and C made before A's reply. Each must go out, in order, each answered.
        using R = ia::Result;
        rw::SendQueue<int> pend; rw::ReplyWait bind, glyph;
        struct Queue { uint64_t holding = 0; bool Take(uint64_t s) { if (holding) return false; holding = s; return true; } } transport;
        std::vector<int> sentSkills, answered; int dropped = 0, refusedAtPush = 0; uint64_t seq = 0;
        std::vector<std::pair<int, uint64_t>> inFlight;
        auto make = [&](int skill) { if (!pend.Push(skill)) ++refusedAtPush; };
        make(100);   // A
        for (double now = 0; now < 3000; now += 11) {
            if (now == 33) make(200);    // B, while A waits
            if (now == 44) make(300);    // C, while A still waits
            if (transport.holding && std::fmod(now, 121) < 11) {   // transport answers slowly
                for (auto& f : inFlight) if (f.second == transport.holding && bind.Reply(f.second, R::Applied) != rw::ReplyEnd::None) answered.push_back(f.first);
                transport.holding = 0;
            }
            int skill = 0;
            if (rw::CanSend(bind, glyph) && pend.Pop(skill)) { ++seq; if (!transport.Take(seq)) ++dropped; bind.Sent(seq, now); sentSkills.push_back(skill); inFlight.push_back({skill, seq}); }
        }
        Check(sentSkills == std::vector<int>({100, 200, 300}) && dropped == 0 && refusedAtPush == 0, "perf X4: A in flight, B and C made: all three sent, in order, none dropped");
        Check(answered == std::vector<int>({100, 200, 300}), "perf X4: all three answered (B no longer lost to C)");
    }
    {   // Round 2026-11-12 Job B (audit bug 4: the PNG save sat on the frame): the save queue and the worker's half
        namespace sh = rw::shot;
        using task = sh::SaveQueue::task;
        // A save held on the worker until the test lets it go. 3 s at most: a queue that ran the job on the caller fails, not hangs.
        struct Gate {
            std::mutex m; std::condition_variable cv; bool open = false; std::atomic<int> timedOut{0};
            void Open() { { std::lock_guard l(m); open = true; } cv.notify_all(); }
            void Wait() { std::unique_lock l(m); if (!cv.wait_for(l, std::chrono::seconds(3), [&] { return open; })) ++timedOut; }
        };
        const auto me = std::this_thread::get_id();
        {   // nothing without a screenshot
            sh::SaveQueue q;
            Check(q.Threads() == 0 && q.Idle() && q.Waiting() == 0 && !q.Full(), "save queue: no thread and nothing waiting before the first screenshot");
            q.Drain();
            Check(q.Threads() == 0, "save queue: a drain with nothing to write starts no thread and returns");
        }
        {   // the caller never waits for a save; a second shot behind a pending one is kept, in order
            sh::SaveQueue q; Gate gate;
            std::mutex m; std::vector<int> ran, behind; std::vector<std::thread::id> on;
            bool times = true;
            auto job = [&](int k) { return [&, k](const task& t) { gate.Wait(); std::lock_guard l(m); ran.push_back(k); behind.push_back(t.behind); on.push_back(std::this_thread::get_id()); times = times && t.at > 0 && t.begun >= t.at; }; };
            const bool a = q.Push(job(1)), b = q.Push(job(2));
            bool none; { std::lock_guard l(m); none = ran.empty(); }
            Check(a && b && none && q.Waiting() == 2 && !q.Idle() && !gate.timedOut, "save queue: Push returns with both saves unwritten (the frame thread never writes one)");
            gate.Open(); q.Drain();
            Check(ran == std::vector<int>{1, 2} && behind == std::vector<int>{0, 1}, "save queue: a second shot while one is pending is not lost: both written, in order, the second told it was behind one");
            Check(on.size() == 2 && on[0] != me && on[1] != me && !gate.timedOut, "save queue: both written on the worker, never on the caller");
            Check(times, "save queue: each save is told when it was taken and when the worker began it");
            Check(q.Idle() && q.Waiting() == 0 && q.Threads() == 1, "save queue: one thread for the burst, ended with it");
            Check(q.Push(job(3)), "save queue: a later screenshot is taken"); q.Drain();
            Check(ran.size() == 3 && q.Threads() == 2 && q.Idle(), "save queue: a later screenshot starts a thread of its own");
            Check(q.Push(job(4)), "save queue: the next one is taken");
            while (!q.Idle()) std::this_thread::yield();   // its thread ended, not joined yet
            Check(q.Push(job(5)), "save queue: a shot after an ended, unjoined thread is taken"); q.Drain();
            Check(ran == std::vector<int>({1, 2, 3, 4, 5}) && q.Threads() == 4 && behind[4] == 0, "save queue: the ended thread is joined by the next Push; nothing lost");
        }
        {   // full: one more than MaxWaiting is refused, neither queued nor run; room again once the saves are written
            sh::SaveQueue q; Gate gate; std::atomic<int> ran{0};
            auto job = [&](const task&) { gate.Wait(); ++ran; };
            int taken = 0;
            for (int i = 0; i < sh::SaveQueue::MaxWaiting; ++i) taken += q.Push(job);
            Check(taken == sh::SaveQueue::MaxWaiting && q.Full() && !q.Push(job) && q.Waiting() == sh::SaveQueue::MaxWaiting, "save queue: full at MaxWaiting; one more is refused at once, not queued");
            gate.Open(); q.Drain();
            Check(ran == sh::SaveQueue::MaxWaiting && !q.Full(), "save queue: the refused save never ran; room again after the drain");
            Check(q.Push(job), "save queue: taken again after the drain"); q.Drain();
            Check(ran == sh::SaveQueue::MaxWaiting + 1, "save queue: and written");
        }
        {   // Shutdown: Drain returns only when every save it took is written
            sh::SaveQueue q; std::atomic<int> ran{0};
            for (int i = 0; i < 3; ++i) q.Push([&](const task&) { std::this_thread::sleep_for(std::chrono::milliseconds(15)); ++ran; });
            q.Drain();
            Check(ran == 3 && q.Idle() && q.Waiting() == 0, "save queue: Drain (Shutdown) returns after every pending save is written");
        }
        {   // a save that throws does not take the next one with it
            sh::SaveQueue q; std::atomic<int> ran{0};
            q.Push([](const task&) { throw 1; }); q.Push([&](const task&) { ++ran; }); q.Drain();
            Check(ran == 1 && q.Waiting() == 0 && q.Idle(), "save queue: a save that throws is dropped; the next is written");
        }
        {   // the rows the frame thread hands over
            Check(sh::Raw::Bytes(24, 5, 2) == 44 && sh::Raw::Bytes(24, 0, 2) == 0 && sh::Raw::Bytes(24, 5, 0) == 0, "shot rows: every row a pitch apart, the last one only its texels");
            sh::Raw raw; raw.w = 5; raw.h = 2; raw.rowPitch = 24; raw.rows.assign(44, 0);
            Check(raw.Ok(), "shot rows: whole at 44 bytes");
            raw.rows.resize(43); Check(!raw.Ok() && sh::Picture(raw).empty(), "shot rows: a byte short is not a picture");
            raw.rows.resize(44); raw.rowPitch = 19; Check(!raw.Ok(), "shot rows: a pitch under the row's texels is refused");
        }
        {   // the worker's half: the picture from the rows, the file, one line with both times
            const uint8_t px[] = {10, 20, 30, 255,   0, 0, 0, 0,   60, 30, 15, 128,   200, 0, 0, 100,   9, 9, 9, 9};   // 5 x 1; pitch 24
            sh::Raw raw; raw.w = 5; raw.h = 2; raw.rowPitch = 24; raw.srgb = true; raw.rows.assign(44, 0);
            std::memcpy(raw.rows.data(), px, 20); std::memcpy(raw.rows.data() + 24, px, 20);
            const auto want = sh::ToStraightRgba8(raw.rows.data(), 24, 5, 2, false, true);
            std::vector<uint8_t> got; std::wstring file; unsigned gw = 0, gh = 0; std::string line; int writes = 0; bool ok = true;
            auto write = [&](const wchar_t* f, const std::vector<uint8_t>& p, unsigned w, unsigned h) { ++writes; file = f; got = p; gw = w; gh = h; return ok; };
            auto say = [&](const char* s) { line = s; };
            auto starts = [&](const char* s) { return line.rfind(s, 0) == 0; };
            auto ends = [&](const std::string& s) { return line.size() >= s.size() && line.compare(line.size() - s.size(), s.size(), s) == 0; };
            const std::wstring name = L"C:\\shots\\Frame-pair-1-ring-wheel.png";
            sh::Finish(raw, name, 1000.0, task{1003.5, 0, sh::NowMs()}, write, say);
            Check(writes == 1 && got == want && got == sh::Picture(raw) && file == name && gw == 5 && gh == 2, "shot save: the worker makes the picture from the rows (sRGB, straight alpha) and writes it under the shot's name");
            Check(starts("Frame capture: ring wheel saved C:\\shots\\Frame-pair-1-ring-wheel.png (5x2, 3.5 ms on the frame, ") && ends(" ms on the worker)"), "shot save: the saved line keeps its start and gives the frame's ms and the worker's (" + line + ")");
            { const double now = sh::NowMs(); sh::Finish(raw, name, 1000.0, task{now - 40.25, 1, now}, write, say); }
            Check(line.find(" ms on the worker, after 40.2 ms behind 1 other save)") != std::string::npos || ends(" ms on the worker, after 40.3 ms behind 1 other save)"), "shot save: a save that waited behind another says so, and for how long (" + line + ")");
            { const double now = sh::NowMs(); sh::Finish(raw, name, 1000.0, task{now - 40, 2, now}, write, say); }
            Check(ends(" ms behind 2 other saves)"), "shot save: behind two (" + line + ")");
            {   // the worker's ms run from when it began the save, so its copy of the rows counts
                const double now = sh::NowMs(); sh::Finish(raw, name, 1000.0, task{1003.5, 0, now - 250}, write, say);
                const size_t at = line.find(" ms on the frame, ");
                const double worker = at == std::string::npos ? 0 : std::atof(line.c_str() + at + 18);
                Check(worker >= 250 && worker < 2000, "shot save: the worker's ms count from when it began the save (" + line + ")");
            }
            ok = false;
            sh::Finish(raw, name, 1000.0, task{1003.5, 0, sh::NowMs()}, write, say);
            Check(starts("Frame capture: ring wheel not saved (cannot write the file C:\\shots\\Frame-pair-1-ring-wheel.png; 3.5 ms on the frame, ") && ends(" ms on the worker)"), "shot save: a file that cannot be written is said, with both times (" + line + ")");
            ok = true; writes = 0; line.clear();
            sh::Finish(raw, name, 1000.0, task{1003.5, 0, sh::NowMs()}, [&](const wchar_t*, const std::vector<uint8_t>&, unsigned, unsigned) -> bool { throw 1; }, say);
            Check(starts("Frame capture: ring wheel not saved (cannot write the file "), "shot save: a write that throws is a not-saved line, not a crash");
            raw.rows.resize(43); line.clear();
            sh::Finish(raw, name, 1000.0, task{1003.5, 0, sh::NowMs()}, write, say);
            Check(writes == 0 && starts("Frame capture: ring wheel not saved (the picture could not be read back for C:\\shots\\Frame-pair-1-ring-wheel.png; 3.5 ms on the frame, "), "shot save: rows that are not whole write no file and say so (" + line + ")");
        }
        {   // the frame thread's half, as the ring's hook runs it: take, queue, and nothing of the save on the caller
            const uint8_t px[] = {10, 20, 30, 255,   0, 0, 0, 0,   60, 30, 15, 128,   200, 0, 0, 100,   9, 9, 9, 9};
            auto rowsOf = [&](sh::Raw& raw) { raw = {}; raw.w = 5; raw.h = 2; raw.rowPitch = 24; raw.rows.assign(44, 0); std::memcpy(raw.rows.data(), px, 20); std::memcpy(raw.rows.data() + 24, px, 20); return true; };
            Gate gate;
            std::mutex m; std::vector<std::string> lines; std::vector<std::thread::id> wrote, read, began; int takes = 0;
            auto write = [&](const wchar_t*, const std::vector<uint8_t>& p, unsigned w, unsigned h) { std::lock_guard l(m); wrote.push_back(std::this_thread::get_id()); return p.size() == 40 && w == 5 && h == 2; };
            auto say = [&](const char* s) { std::lock_guard l(m); lines.push_back(s); };
            auto begin = [&] { gate.Wait(); std::lock_guard l(m); began.push_back(std::this_thread::get_id()); };
            auto take = [&](const char*&) -> sh::Reader { ++takes; return [&](sh::Raw& raw) { { std::lock_guard l(m); read.push_back(std::this_thread::get_id()); } return rowsOf(raw); }; };
            sh::SaveQueue* q = nullptr;
            const char* busy = sh::Queue(q, sh::NowMs(), L"a.png", [&](const char*& why) -> sh::Reader { ++takes; why = "the GPU is still drawing the wheel"; return {}; }, write, say, begin);
            Check(busy && std::string(busy) == "the GPU is still drawing the wheel" && !q && takes == 1, "shot hook: a shot the GPU could not take is said at once; no queue, no thread");
            const char* first = sh::Queue(q, sh::NowMs(), L"a.png", take, write, say, begin);
            const char* second = sh::Queue(q, sh::NowMs(), L"b.png", take, write, say, begin);
            bool none; { std::lock_guard l(m); none = lines.empty() && wrote.empty() && read.empty(); }
            Check(!first && !second && q && none && q->Waiting() == 2 && !gate.timedOut, "shot hook: two shots queued; the hook is back with no rows read, no file written, no line said");
            const char* third = sh::Queue(q, sh::NowMs(), L"c.png", take, write, say, begin);
            const char* fourth = sh::Queue(q, sh::NowMs(), L"d.png", take, write, say, begin);
            const int before = takes;
            const char* fifth = sh::Queue(q, sh::NowMs(), L"e.png", take, write, say, begin);
            Check(!third && !fourth && fifth && std::string(fifth) == "the last screenshots are still being written" && takes == before, "shot hook: a fifth shot with four unwritten is refused before any GPU work, and said");
            gate.Open();
            if (q) q->Drain();
            bool onWorker = wrote.size() == 4 && read.size() == 4 && began.size() == 4;
            for (const auto& ids : {wrote, read, began}) for (const auto id : ids) onWorker = onWorker && id != me;
            Check(onWorker && !gate.timedOut, "shot hook: every save's rows read and file written on the worker, never on the caller");
            bool named = lines.size() == 4;
            const char* names[4] = {"a.png", "b.png", "c.png", "d.png"};
            for (int i = 0; named && i < 4; ++i) named = lines[size_t(i)].rfind(std::string("Frame capture: ring wheel saved ") + names[i] + " (5x2, ", 0) == 0;
            Check(named, "shot hook: one saved line a shot, in the order taken");
            const char* threw = sh::Queue(q, sh::NowMs(), L"g.png", [&](const char*&) -> sh::Reader { throw std::bad_alloc(); }, write, say, begin);
            Check(threw && std::string(threw) == "no memory to queue it" && q && q->Waiting() == 0, "shot hook: nothing thrown reaches the frame thread; said as a reason, nothing queued");
            lines.clear();
            const char* unread = sh::Queue(q, sh::NowMs(), L"f.png", [&](const char*&) -> sh::Reader { return [](sh::Raw& raw) { raw = {}; return false; }; }, write, say, begin);
            if (q) q->Drain();
            Check(!unread && lines.size() == 1 && lines[0].rfind("Frame capture: ring wheel not saved (the picture could not be read back for f.png; ", 0) == 0 && wrote.size() == 4, "shot hook: rows the worker could not read: no file, and the line says so");
            delete q;
        }
    }
    {   // The On edge's log line: the ring's state, never a build step
        rw::MenuState st;
        const std::string a = rw::OnLine(st);
        Check(a.find("build step") == std::string::npos && a.find("R7") == std::string::npos, "On line: no build step named");
        Check(a.find("Trees shown") != std::string::npos && a.find("90 cm ahead") != std::string::npos && a.find("80 cm wide") != std::string::npos &&
              a.find("8 deg below") != std::string::npos && a.find("hold to assign 1.0 s") != std::string::npos && a.find("tap limit 350 ms") != std::string::npos &&
              a.find("animation Effect on hover") != std::string::npos && a.find("render 150%") != std::string::npos, "On line: the defaults' values, each named (" + a + ")");
        st.orbs = true; st.holdMs = 0; st.animation = "Custom"; st.renderPct = 100;
        const std::string b = rw::OnLine(st);
        Check(b.find("Tree orbs") != std::string::npos && b.find("hold to assign Off") != std::string::npos && b.find("animation Custom") != std::string::npos &&
              b.find("render 100%") != std::string::npos, "On line: changed rows show their new values (" + b + ")");
        st.worldLock = false;
        Check(rw::OnLine(st).find("upstream's wheel on its hand anchor") != std::string::npos, "On line: Stays in the room Off says the ring is not drawn");
        {   // the quiet log (the player's PR rule): edges, refusals and failures pass; per-open, per-pick, pointer and periodic lines do not
            auto keep = [](const char* l) { return rw::KeepQuietLine(l); };
            Check(keep(rw::OnLine(st).c_str()) && keep(rw::OffLine()) && keep("Frame ring wheel: bind Fireball -> right A (applied)") && keep("Frame ring wheel: bind Fireball -> right A (refused: 3 already waiting to be sent)") && keep("Frame ring wheel: compose setup failed: x") && keep("Frame ring wheel: Quick cast not put on right A: Quick cast is Off") && keep("Frame ring wheel: swapchain kept: the GPU still busy after 10 ms"), "quiet log: edges, binds, refusals and failures are kept");
            Check(!keep("Frame ring wheel: opened right hand's wheel, page Spells") && !keep("Frame ring wheel: page Spells") && !keep("Frame ring wheel: pointer owner=right why=row") && !keep("Frame ring wheel: 10 s: composes=1") && !keep("Frame ring wheel: placed 0.90 m ahead") && !keep("Frame ring wheel: tap cast Fireball slot=A") && !keep("Frame ring wheel: layers icon=x") && !keep("Frame ring wheel: atlas icons=3/3 missing=none glyphs=ok") && !keep("Frame ring wheel: assign Fireball -> A held=500 ms") && !keep("Frame ring wheel: closed reason=back after 1.0 s"), "quiet log: opens, pages, pointing, placement, picks and the 10 s line are not said");
            Check(keep("Frame glyph: x") && !keep(nullptr), "quiet log: a line that is not the ring's passes unchanged");
        }
        Check(a.size() < 400 && a.rfind("Frame ring wheel: On", 0) == 0 && std::string(rw::OffLine()).rfind("Frame ring wheel: Off", 0) == 0,
              "On / Off lines keep the tag and fit the buffer");
    }
    // ---- the center in thirds, the sub-menus, the Quick
    // cast page and its stick select, the clean icon's key, the layers ----
    {   // The thirds' hit test: by angle, Glyphs upper left, Gestures upper right, Quick cast at the bottom; 6 px dead bands on the spokes
        auto at = [](float deg, float r) { const float a = deg * rw::Pi / 180; return std::array<float, 2>{r * std::cos(a), r * std::sin(a)}; };
        for (int t = 0; t < rw::ThirdCount; ++t) for (const float r : {10.f, 40.f, 79.f}) for (const float off : {-55.f, 0.f, 55.f}) {
            const auto p = at(rw::ThirdMidDeg[t] + off, r);
            Check(rw::ThirdAt(p[0], p[1]) == t || (r < 10.1f && std::fabs(off) > 50), "ThirdAt: third " + std::to_string(t) + " at r " + std::to_string(int(r)) + ", " + std::to_string(int(off)) + " deg off its middle");
        }
        for (const float s : rw::SpokeDeg) for (const float r : {20.f, 60.f, 130.f}) {
            const float a = s * rw::Pi / 180, nx = -std::sin(a), ny = std::cos(a);
            for (const float side : {-1.f, 1.f}) {
                Check(rw::ThirdAt(r * std::cos(a) + side * 2.9f * nx, r * std::sin(a) + side * 2.9f * ny) == -1, "2.9 px from a spoke: the dead band");
                Check(rw::ThirdAt(r * std::cos(a) + side * 3.2f * nx, r * std::sin(a) + side * 3.2f * ny) >= 0, "3.2 px from a spoke: a third (the band is 6 px wide)");
            }
        }
        Check(rw::ThirdAt(0, 0) == -1 && rw::ThirdAt(1, 1) == -1, "the very middle is a dead band (every spoke meets there)");
        Check(std::fabs(rw::ThirdReach(true, 11) - 136 * 1.11f) < .01f && rw::ThirdReach(false, 0) == rw::CenterR && rw::ThirdReach(true, -5) == rw::CenterGrownR, "the reach: the center's radius plus the row's percent, never under it");
        // The engine: thirds on (the main page)
        const int per[3] = {10, 10, 10};
        const auto l = ClassLayout(1, per, rw::TreeLayout::Shown, 3, 1);
        rw::Settings s; s.thirds = true; s.growMs = 150;
        rw::Engine e; e.Reset(l);
        auto kindAt = [&](rw::Engine& en, float deg, float r) { const auto p = at(deg, r); return en.HitTest(p[0] + rw::CanvasCx, p[1] + rw::CanvasCy, s).kind; };
        Check(kindAt(e, -150, 50) == rw::Kind::Glyphs && kindAt(e, -30, 50) == rw::Kind::Gestures && kindAt(e, 90, 50) == rw::Kind::Quick, "engine: each third at r 50");
        Check(kindAt(e, 90, 85) != rw::Kind::Quick, "engine: at rest nothing past r 80 is a third (no third pointed yet)");
        {   // pointed and grown: the pointed slice reaches its grow; a spoke keeps it; the others stay at the center's edge
            double t = 0;
            for (; t <= 300; t += 10) { const auto p = at(90, 50); e.Step(At(t, p[0] + rw::C, p[1] + rw::C), s); }
            const auto o = e.Step(At(t, at(90, 50)[0] + rw::C, at(90, 50)[1] + rw::C), s);
            Check(o.under.kind == rw::Kind::Quick && o.centerGrown, "the Quick cast third pointed 300 ms: the center grown");
            Check(kindAt(e, 90, 150) == rw::Kind::Quick && kindAt(e, 90, 152) != rw::Kind::Quick, "grown and pointed: the slice reaches 151 px (11%), not 152");
            Check(kindAt(e, -30, 140) != rw::Kind::Gestures, "another third has no grown reach: past 136 it is not hit");
            const auto sp = at(150, 60);   // on the 8 o'clock spoke, the edge between Glyphs and Quick cast
            Check(e.HitTest(sp[0] + rw::CanvasCx, sp[1] + rw::CanvasCy, s).kind == rw::Kind::Quick, "on a spoke's dead band: the third pointed last stays");
        }
        {
            rw::Engine fresh; fresh.Reset(l);
            const auto sp = at(-90, 40);
            Check(fresh.HitTest(sp[0] + rw::CanvasCx, sp[1] + rw::CanvasCy, s).kind == rw::Kind::Gap, "on a spoke with nothing pointed: a gap (never a guess)");
            rw::Settings noMotions = s; noMotions.hasMotions = false;
            const auto g = at(-30, 40);
            Check(fresh.HitTest(g[0] + rw::CanvasCx, g[1] + rw::CanvasCy, noMotions).kind == rw::Kind::Gap, "a class with no gestures: its Gestures third is shut");
            rw::Settings halves = s; halves.thirds = false;
            const auto q = at(90, 40);
            Check(fresh.HitTest(q[0] + rw::CanvasCx, q[1] + rw::CanvasCy, halves).kind == rw::Kind::Gestures, "thirds off (the glyph chooser): the two halves as before");
        }
        // 11% keeps 6 px from every seat's ring, every class, both layouts (the mockup's measure: the seat's ring outer edge)
        float worst = 1e9f;
        for (int cls = 0; cls < 8; ++cls) for (const auto mode : {rw::TreeLayout::Shown, rw::TreeLayout::Orbs}) {
            const auto cl = ClassLayout(cls, per, mode);
            for (int i = 0; i < cl.count; ++i) if (cl.seats[size_t(i)].tree >= 0)
                worst = std::min(worst, std::hypot(cl.seats[size_t(i)].x - rw::C, cl.seats[size_t(i)].y - rw::C) - rw::IconPx / 2 - 3.5f - rw::ThirdReach(true, rw::ThirdGrowDefault));
        }
        Check(worst >= 6, "the grown third (11%) stays 6 px clear of every seat's ring: " + std::to_string(worst) + " px");
        // Fire on release: Glyphs and Gestures release upstream's cells, Quick cast the ring's OpenQuick (2026-11-16: its page in the ring)
        auto click = [&](float deg) {
            rw::Engine en; en.Reset(l); rw::StepOut last{};
            const auto p = at(deg, 50);
            for (double t = 0; t <= 400; t += 10) { last = en.Step(At(t, p[0] + rw::C, p[1] + rw::C, t >= 100 && t < 300), s); if (last.event != rw::Event::None) break; }
            return last;
        };
        const auto cg = click(-150), cm = click(-30), cq = click(90);
        Check(cg.event == rw::Event::Upstream && cg.cell == ia::GlyphsCell, "the trigger released on Glyphs: upstream's Glyphs cell");
        Check(cm.event == rw::Event::Upstream && cm.cell == ia::MotionsCell, "the trigger released on Gestures: upstream's Motions cell");
        Check(cq.event == rw::Event::OpenQuick, "the trigger released on Quick cast: OpenQuick (its page in the ring, 2026-11-16)");
        {   // a hold on a third that ran past the tap limit opens nothing on its release (the press assigned or did nothing)
            rw::Engine en; en.Reset(l); rw::StepOut last{}; bool any = false;
            const auto p = at(90, 50);
            for (double t = 0; t <= 400; t += 10) { auto in = At(t, p[0] + rw::C, p[1] + rw::C, t >= 100 && t < 300); in.glyphClickSpent = true; last = en.Step(in, s); any |= last.event != rw::Event::None; }
            Check(!any, "a spent click on a third (a hold past the tap limit) opens nothing");
        }
    }
    {   // The thirds' pseudo seats: a tap opens the sub-menu (MenuTap), a hold puts it on the button (MenuAssign); Glyphs keeps Draw glyph
        const int per[3] = {10, 10, 10};
        const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
        auto run = [&](rw::Target at, double heldMs, bool thirds, int slot = 1, double until = -1) {
            rw::QuickCast q; std::vector<rw::QuickOut> ev;
            const double end = until > 0 ? until : 200 + heldMs + 50;
            for (double t = 0; t <= end; t += 1) {
                rw::QuickIn in; in.nowMs = t; in.main = true; in.under = at; in.thirds = thirds;
                in.bound[0] = {36, 39, -1, -1, -1}; in.bound[1] = {-1, -1, -1, -1, -1};
                if (t == 200) in.press[0] = slot;
                in.down[0][slot] = t >= 200 && (heldMs < 0 || t < 200 + heldMs);
                const auto o = q.Step(in, l);
                if (o.event != rw::Quick::None) ev.push_back(o);
            }
            return ev;
        };
        const rw::Target ges{rw::Kind::Gestures}, qc{rw::Kind::Quick}, gl{rw::Kind::Glyphs};
        auto tapG = run(ges, 100, true), tapQ = run(qc, 100, true), tapGl = run(gl, 100, true);
        Check(tapG.size() == 1 && tapG[0].event == rw::Quick::MenuTap && tapG[0].seat == rw::GesturesSeat, "a tap on the Gestures third: MenuTap on its pseudo seat");
        Check(tapQ.size() == 1 && tapQ[0].event == rw::Quick::MenuTap && rw::SeatThird(tapQ[0].seat) == rw::ThirdQuick, "a tap on the Quick cast third: MenuTap");
        Check(tapGl.size() == 1 && tapGl[0].event == rw::Quick::GlyphTap, "Glyphs keeps its GlyphTap");
        auto holdQ = run(qc, -1, true, 1, 200 + 1001), holdGl = run(gl, -1, true, 1, 200 + 1001);
        Check(holdQ.size() == 1 && holdQ[0].event == rw::Quick::MenuAssign && holdQ[0].seat == rw::QuickSeat && holdQ[0].slot == 1 && holdQ[0].replaced == 39,
              "held to full on Quick cast: MenuAssign on that button, the skill it replaces named");
        Check(holdGl.size() == 1 && holdGl[0].event == rw::Quick::GlyphAssign, "held on Glyphs: Draw glyph, as before");
        Check(run(qc, 100, false).empty() && run(ges, -1, false, 1, 200 + 1001).empty(), "thirds off (halves): Gestures and Quick cast take no press");
        Check(std::string(rw::EmptyHoldStatus(rw::QuickSeat)) == "Quick cast" && std::string(rw::EmptyHoldStatus(rw::GesturesSeat)) == "Gestures menu" &&
              std::string(rw::EmptyHoldStatus(rw::GlyphsSeat)) == "Draw glyph", "the hold says what goes on the button");
        rw::QuickOut spent; spent.holding = rw::QuickSeat; spent.slot = 0; spent.hand = 1; spent.heldMs = 400;
        Check(rw::GlyphClickSpent(spent, 1, 1000, 350) && !rw::GlyphClickSpent(spent, 0, 1000, 350), "the pointing trigger's hold on any third spends its click");
    }
    {   // Each sub-menu on a button: the slot kept, one button one thing, dropped when a skill or Draw glyph takes it, its press edge
        rw::MenuButtons b;
        Check(rw::ButtonSlotOf(0) == -1 && rw::ButtonSlotOf(1) == 0 && rw::ButtonSlotOf(10) == 9 && rw::ButtonSlotOf(11) == -1, "the key holds slot + 1, 0 none");
        b = rw::Assigned(b, rw::ThirdQuick, 6);
        Check(b.slot[2] == 6 && b.MenuOf(6) == rw::ThirdQuick && b.MenuOf(5) == -1 && b.Any(), "Quick cast on slot 6");
        b = rw::Assigned(b, rw::ThirdGestures, 6);
        Check(b.slot[1] == 6 && b.slot[2] == -1, "a second sub-menu on the same button takes it (one button, one thing)");
        std::array<int, framevr::skills::Count> game{}; game.fill(-1);
        Check(rw::Kept(b, game, {-1, -1}) == b, "a free button keeps its sub-menu");
        game[6] = 40;
        Check(rw::Kept(b, game, {-1, -1}).slot[1] == -1, "a skill put on the button takes it back");
        game[6] = -1;
        Check(rw::Kept(b, game, {6, -1}).slot[1] == -1, "Draw glyph put on the button takes it back");
        // the press: Quick cast on the left hand's grip + secondary (upstream slot 9)
        rw::MenuButtons q = rw::Assigned({}, rw::ThirdQuick, framevr::skills::HandSlot(1, 4));
        bool held[2][3] = {}, down[2][3] = {}, grip[2] = {false, true};
        down[1][2] = true;
        auto p = rw::MenuPressed(q, down, grip, held);
        Check(p.third == rw::ThirdQuick && p.hand == 1, "left grip + Y: the Quick cast button's press");
        p = rw::MenuPressed(q, down, grip, held);
        Check(p.third == -1, "held down: no second press until it is let go");
        down[1][2] = false; rw::MenuPressed(q, down, grip, held);
        grip[1] = false; down[1][2] = true;
        Check(rw::MenuPressed(q, down, grip, held).third == -1, "the same face without the grip is another slot: no press");
        // the shelf entries' ids never meet a game skill, Draw glyph or Army X menu
        for (int t = 0; t < rw::ThirdCount; ++t) Check(rw::MenuEntryId(t) > 9001 && rw::MenuOfEntry(rw::MenuEntryId(t)) == t && rw::MenuOfEntry(9001) == -1 && rw::MenuOfEntry(40) == -1, "entry ids are their own");
    }
    {   // Where a sub-menu asked for leads from upstream's page, one cell a frame
        using P = ia::MenuPage;
        auto n = rw::NavigateTo(rw::ThirdGlyphs, P::Spells);
        Check(n.nav == rw::Nav::Fire && n.cell == ia::GlyphsCell, "Glyphs from Spells: its cell");
        n = rw::NavigateTo(rw::ThirdGestures, P::Spells);
        Check(n.nav == rw::Nav::Fire && n.cell == ia::MotionsCell, "Gestures from Spells: its cell");
        Check(rw::NavigateTo(rw::ThirdQuick, P::Spells).nav == rw::Nav::Done && rw::NavigateTo(rw::ThirdQuick, P::Glyphs).nav == rw::Nav::Done,
              "2026-11-16 Quick cast's page is the ring's own (over Spells): nothing to navigate upstream, done at once");
        n = rw::NavigateTo(rw::ThirdGlyphs, P::Motions);
        Check(n.nav == rw::Nav::Fire && n.cell == rw::SpellsTabCell, "Glyphs from Gestures: back to Spells first");
        Check(rw::NavigateTo(rw::ThirdGestures, P::Motions).nav == rw::Nav::Done, "already there: done");
        Check(rw::NavigateTo(rw::ThirdGlyphs, P::EditSlots).nav == rw::Nav::Wait, "an editor open: wait (the ring gives up after a second)");
    }
    {   // 2026-11-16: the Quick cast page went (one quick cast: ring Quick cast radial); its shared pieces stay tested here
        constexpr int Eight = 8;
        {   // one stick-select rule (steamvr/stick_select.h) over eight items, with the shared feel's defaults
            namespace ss = framevr::stickselect;
            const ss::Feel feel;
            auto pick = [&](float x, float y) { ss::Selector s; s.Step(x, y, Eight, 0, feel); return s.pick; };
            Check(pick(0, 1) == 0 && pick(1, 0) == 2 && pick(0, -1) == 4 && pick(-1, 0) == 6, "stick: up item 1, right item 3, down item 5, left item 7 (clockwise from 12)");
            Check(pick(.2f, .2f) == -1 && pick(.3f, .3f) == 1, "stick: the shared dead zone (0.35)");
            auto dir = [](float deg) { const float a = deg * rw::Pi / 180; return std::array<float, 2>{std::sin(a), std::cos(a)}; };
            ss::Selector s; s.Step(dir(0)[0], dir(0)[1], Eight, 0, feel);
            s.Step(dir(27)[0], dir(27)[1], Eight, 10, feel); const bool held = s.pick == 0;
            s.Step(dir(33)[0], dir(33)[1], Eight, 20, feel); const bool moved = s.pick == 1;
            Check(held && moved, "stick: no flicker between two items (the shared pick hold, 8 deg)");
            s.Step(0, 0, Eight, 30, feel); const bool kept = s.pick == 1; s.Step(0, 0, Eight, 30 + feel.centerMs, feel);
            Check(kept && s.pick == -1, "stick: the shared center hold (a flick let go as the thumb returns still counts)");
            ss::Hold h; h.Start(.9f, 0); const bool a1 = h.Step(.9f, 0), a2 = h.Step(.6f, 0), a3 = !h.Step(.1f, 0), a4 = !h.Step(.9f, 0);
            Check(a1 && a2 && a3 && a4, "review S3: after a cast or Back the pushed stick stays the menu's until it is back in the middle");
        }
        {   // Review F2: the menu-open click frames (one at 1, then one at 0; none on the opening or hand-change frame)
            rw::FireGate g; double t = 0;
            auto frame = [&](int hand, int page) { t += 11; g.Watch(hand, page, t); return g.Step(t); };
            g.Ask(0);
            const bool f0 = frame(0, 1), f1 = frame(0, 1), f2 = frame(0, 1), f3 = frame(0, 1), f4 = frame(1, 1), f5 = frame(1, 1);
            Check(!f0 && f1 && !f2 && f3 && !f4 && f5, "review F2: no click on the opening frame, then 1/0 alternate, none on a hand change");
            // S-B: clicks that keep failing (a grip held keeps upstream's rearm set) still give up: from the ask, not the last click
            rw::FireGate s; double u = 0; s.Watch(0, 1, u); s.Ask(u); int clicks = 0; bool gaveUp = false, early = false;
            for (int i = 0; i < 200 && !gaveUp; ++i) { u += 11; s.Watch(0, 1, u); clicks += s.Step(u); gaveUp = s.GaveUp(u, 1000); early |= gaveUp && u < 11 * 2 * rw::FireGate::MaxFires - 22; }
            Check(gaveUp && clicks == rw::FireGate::MaxFires && !early && u < 1000, "evening review S-B: failing clicks stop after MaxFires and the ask gives up (within 1 s)");
            rw::FireGate p; double v = 0; p.Watch(0, 1, v); p.Ask(v);
            for (int i = 0; i < 80; ++i) { v += 11; p.Watch(0, 1 + i / 40, v); p.Step(v); }
            Check(p.pageWas == 2 && p.sinceMs > 400, "evening review S-B: a page change is progress: a fresh count and clock");
            rw::FireGate q; double w = 0; q.Watch(1, 1, w); q.Ask(w); w += 11; q.Watch(1, 1, w);
            Check(q.Step(w), "evening review S-B: a wheel already open on that hand clicks on the first frame after the ask (the hand watched every frame)");
            rw::FireGate r; double x = 0; r.Ask(x); x += 300; r.Watch(-1, 0, x);
            for (int i = 0; i < 100; ++i) { x += 11; r.Watch(-1, 0, x); }
            Check(r.GaveUp(x, 1000), "evening review S-B: a wheel that never opened gives up 1 s after the ask");
            Check(!rw::MenuButtonAllowed(0) && rw::MenuButtonAllowed(1), "review F1: a sub-menu never goes on the trigger");
        }
    }
    {   // Headset fixes 2026-11-16 
        // Bug 1: a sub-menu's button with a seat or a third under the open ring's laser is the ring's press (a hold rebinds that button)
        Check(!rw::MenuPressOpens(true, true, rw::Kind::Seat) && !rw::MenuPressOpens(true, true, rw::Kind::Quick) && !rw::MenuPressOpens(true, true, rw::Kind::Glyphs) &&
              !rw::MenuPressOpens(true, true, rw::Kind::Gestures), "bug 1: a seat or a third under the laser takes the menu button's press (tap casts, hold rebinds)");
        Check(rw::MenuPressOpens(false, false, rw::Kind::None) && rw::MenuPressOpens(true, true, rw::Kind::Gap) && rw::MenuPressOpens(true, true, rw::Kind::None) &&
              rw::MenuPressOpens(true, true, rw::Kind::Footer) && rw::MenuPressOpens(true, false, rw::Kind::Seat), "bug 1: the wheel shut, a gap, the footer or another page: the button opens its sub-menu as before");
        {   // ... and through the ring's own hold to assign: a hold of Hold to assign on a seat assigns that button, whatever it held
            const int per[3] = {4, 4, 4};
            const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
            rw::QuickCast q; rw::QuickOut got{};
            for (double t = 0; t <= 1400; t += 1) {
                rw::QuickIn in; in.nowMs = t; in.main = true; in.under = {rw::Kind::Seat, 2}; in.thirds = true;
                in.bound[0] = {-1, -1, -1, -1, -1}; in.bound[1] = {-1, -1, -1, -1, -1};   // the menu's button: no game skill (its key is the ring's)
                if (t == 200) in.press[1] = 1;
                in.down[1][1] = t >= 200;
                const auto o = q.Step(in, l);
                if (o.event != rw::Quick::None) got = o;
            }
            Check(got.event == rw::Quick::Assign && got.hand == 1 && got.slot == 1 && got.seat == 2, "bug 1/2: held about 1 s on a seat, left X (a menu's button) takes that skill");
        }
        // Bug 4: a ring menu over a game skill moves that skill: to the menu's old button when free, else into Quick cast, else said
        using M = rw::MovedTo;
        Check(rw::WhereMoved(-1, 3, 5, -1, false, true) == M::Nothing, "bug 4: nothing on the button: nothing to move");
        Check(rw::WhereMoved(54, 3, 5, -1, false, true) == M::OldButton, "bug 4: the menu's old button is free: the skill swaps there");
        Check(rw::WhereMoved(54, 3, 5, 40, false, true) == M::QuickCast && rw::WhereMoved(54, 3, 5, -1, true, true) == M::QuickCast &&
              rw::WhereMoved(54, 3, -1, -1, false, true) == M::QuickCast && rw::WhereMoved(54, 3, 3, -1, false, true) == M::QuickCast,
              "bug 4: no free old button (a skill there, Draw glyph, none, the same one): into Quick cast's ring");
        Check(rw::WhereMoved(54, 3, -1, -1, false, false) == M::Dropped && rw::WhereMoved(54, 3, 12, -1, false, false) == M::Dropped,
              "bug 4: Quick cast can't take it either: dropped, and the caller says so (never silent)");
        // The retired Quick cast menu button moves to Quick cast's (the radial's) own, once
        Check(rw::MovedQuickButton(4, 0) == 4 && rw::MovedQuickButton(4, 7) == 0 && rw::MovedQuickButton(0, 0) == 0 && rw::MovedQuickButton(0, 7) == 0,
              "one quick cast: the old menu button becomes Quick cast's unless it has one");
        Check(rw::MovedQuickButton(1, 0) == 0 && rw::MovedQuickButton(5, 0) == 0 && rw::MovedQuickButton(2, 0) == 2,
              "review N2: a trigger stored before review F1 (slot 0 or 4) never moves to Quick cast's button; a face button does");
        // Review F1 (2026-11-16): a hold on the Quick cast third with a trigger is refused first, whatever else is true
        Check(rw::QuickBindRefusal(0, true, true, false) && std::string(rw::QuickBindRefusal(0, true, true, false)).find("never a trigger") == 0,
              "review F1: the pointing trigger's hold on the Quick cast third: never a trigger");
        Check(rw::QuickBindRefusal(0, false, false, true) && std::string(rw::QuickBindRefusal(0, false, false, true)).find("never a trigger") == 0,
              "review F1: the trigger is said first (before Off, the class, Draw glyph)");
        Check(rw::QuickBindRefusal(1, false, true, false) && std::string(rw::QuickBindRefusal(1, false, true, false)).find("Quick cast is Off") == 0 &&
              rw::QuickBindRefusal(1, true, false, false) && std::string(rw::QuickBindRefusal(1, true, false, false)).find("this class") == 0 &&
              rw::QuickBindRefusal(2, true, true, true) && std::string(rw::QuickBindRefusal(2, true, true, true)).find("that button draws glyphs") == 0,
              "review F1: then Off, the class, a Draw glyph button");
        Check(!rw::QuickBindRefusal(1, true, true, false) && !rw::QuickBindRefusal(4, true, true, false), "review F1: a free face or grip button takes Quick cast");
        for (int slot = 1; slot < rw::Slots; ++slot) Check(rw::MenuButtonAllowed(slot) == !rw::QuickBindRefusal(slot, true, true, false), "review F1: the same buttons as every sub-menu");
        Check(!rw::MenuButtonAllowed(0) && rw::QuickBindRefusal(0, true, true, false), "review F1: the trigger, as every sub-menu");
        // Review N4: an Applied skill bind lets a ring menu's button go; a sub-menu's clear, Draw glyph or a ring entry never
        Check(rw::SkillTakesButton(54, -1, false) && rw::SkillTakesButton(3901, -1, false), "review N4: a game skill or a commander order takes the button");
        Check(!rw::SkillTakesButton(-1, 1, false) && !rw::SkillTakesButton(-1, -1, false) && !rw::SkillTakesButton(9000, -1, true) && !rw::SkillTakesButton(54, -1, true) &&
              !rw::SkillTakesButton(9001, -1, false) && !rw::SkillTakesButton(9003, -1, false) && !rw::SkillTakesButton(rw::MenuEntryId(0), -1, false),
              "review N4: a clear, a sub-menu, Draw glyph, Army X menu, Quick cast and a menu's entry never let a menu go");
        // The Quick cast third's words: Off in the ring when its switch is Off, Not for this class (review N1); a draw key change redraws
        Check(std::string(rw::QuickThirdWord(false, true)) == "Off" && std::string(rw::QuickThirdWord(false, false)) == "Off" &&
              std::string(rw::QuickThirdWord(true, false)) == "Not for this class" && !rw::QuickThirdWord(true, true), "review N1: the Quick cast third's word");
        rw::Scene a, b, c2; b.quickWord = rw::QuickThirdWord(false, true); c2.quickWord = rw::QuickThirdWord(true, false);
        Check(!(rw::KeyOf(a) == rw::KeyOf(b)) && !(rw::KeyOf(b) == rw::KeyOf(c2)), "Quick cast Off or not this class: the draw key changes (the third says why)");
        Check(std::string(rw::MenuEntryName[rw::ThirdQuick]) == "Quick cast" && std::string(rw::ThirdName[rw::ThirdQuick]) == "Quick cast", "one name: Quick cast");
    }
    {   // The clean icon (ring_wheel_unbake.h): the rust plate keyed out, the silver glyph kept and grayed, speckles cleaned
        namespace ub = rw::unbake;
        Check(ub::Alpha(.45f, .28f, .16f) < .01f && ub::Alpha(.30f, .20f, .12f) < .01f, "key: the rust plate (saturated brown) is out");
        Check(ub::Alpha(.80f, .80f, .82f) > .99f && ub::Alpha(.55f, .53f, .50f) > .99f, "key: the silver glyph stays");
        Check(ub::Alpha(.08f, .08f, .08f) < .01f && ub::Alpha(.19f, .19f, .19f) > .4f && ub::Alpha(.19f, .19f, .19f) < .6f, "key: near-black out, soft from 0.14 to 0.24");
        float r = .7f, g = .6f, bl = .5f; ub::Gray(r, g, bl);
        Check(std::fabs(r - bl) < .2f * .35f + 1e-4f && r > bl, "the glyph's tint pulled 65% toward gray");
        Check(ub::Keyed(40, 0) && !ub::Keyed(9001, 0) && !ub::Keyed(9101, 0) && ub::Keyed(1, 0) && !ub::Keyed(1, 1) && ub::Keyed(2, 1), "keyed: game icons; never our art (9000 up) or Kick while remapped");
        std::vector<float> px(size_t(9 * 9 * 4), 0.f);
        auto A = [&](int x, int y) -> float& { return px[(size_t(y) * 9 + size_t(x)) * 4 + 3]; };
        A(1, 1) = .6f;   // a lone fleck
        for (int y = 4; y < 9; ++y) for (int x = 4; x < 9; ++x) A(x, y) = 1;   // a solid glyph part ...
        A(6, 6) = .1f;   // ... with a pinhole
        const int changed = ub::Despeckle(px, 9, 9);
        Check(A(1, 1) == 0 && A(6, 6) > .9f && A(8, 8) == 1 && changed == 2, "cleanup: the lone fleck gone, the pinhole filled, the rest as it was");
    }
    {   // The layers: each its own switch, Off removes only that layer
        const int per[3] = {4, 4, 4};
        const auto l = ClassLayout(1, per, rw::TreeLayout::Shown, 2, 0);
        static rw::Instance list[rw::MaxDraw];
        auto scene = [&](rw::SeatLayers lay) {
            rw::Scene sc; sc.open = true; sc.layout = &l; sc.tMs = 5000; sc.anim = rw::PresetAnim(rw::Preset::All); sc.layers = lay; sc.hasMotions = true; return sc; };
        auto count = [&](const rw::Scene& sc, auto pred) { int c = 0; const int n = rw::Build(sc, list); for (int i = 0; i < n; ++i) c += pred(list[i]); return c; };
        auto seatFlags = [](const rw::Instance& i) { return int(std::fabs(i.param)) / 16; };
        rw::SeatLayers all{true, true, true, true};
        const auto base = scene(all);
        Check(count(base, [&](const rw::Instance& i) { return i.recipe == rw::RecipeFrame; }) == l.count, "Element frame On: one band per seat");
        Check(count(base, [&](const rw::Instance& i) { return i.recipe == rw::RecipeSeat && seatFlags(i) == 0; }) == l.count, "all On: every seat with its art and ring");
        auto noIcon = all; noIcon.icon = false;
        Check(count(scene(noIcon), [&](const rw::Instance& i) { return i.recipe == rw::RecipeSeat && (seatFlags(i) & rw::SeatNoArt); }) == l.count &&
              count(scene(noIcon), [&](const rw::Instance& i) { return i.recipe == rw::RecipeFrame; }) == l.count, "Icon Off: no art on any seat; the frame stays");
        auto noRing = all; noRing.ring = false;
        Check(count(scene(noRing), [&](const rw::Instance& i) { return i.recipe == rw::RecipeSeat && (seatFlags(i) & rw::SeatNoRing) && !(seatFlags(i) & rw::SeatNoArt); }) == l.count, "Color ring Off: the ring gone, the art kept");
        auto noFrame = all; noFrame.frame = false;
        Check(count(scene(noFrame), [&](const rw::Instance& i) { return i.recipe == rw::RecipeFrame; }) == 0 && count(scene(noFrame), [&](const rw::Instance& i) { return i.recipe == rw::RecipeSeat; }) == l.count, "Element frame Off: no band, the seats as they were");
        auto noFx = all; noFx.fx = false;
        auto restFx = [](const rw::Instance& i) { return i.recipe == rw::RecipeFx && (int(i.param) / 256 == 0 || int(i.param) / 256 == 2) && uint32_t(i.param) < rw::PointedOverFace; };
        Check(count(base, restFx) > 0 && count(scene(noFx), restFx) == 0, "Animation Off: no rest glow and no glow over a face");
        auto neither = all; neither.icon = false; neither.ring = false;
        Check(count(scene(neither), [&](const rw::Instance& i) { return i.recipe == rw::RecipeSeat; }) == 0, "Icon and Color ring Off: nothing of the seat's own drawn");
        // the frame fits the seat pitch: 44 px + its ring + 8 px each side stays inside 70 px
        Check(2 * (rw::IconPx / 2 + 4 + rw::FrameBandPx) <= rw::MinPitch, "a framed seat fits the 70 px pitch");
        // the key changes with each layer (a flip redraws)
        Check(!(rw::KeyOf(scene(all)) == rw::KeyOf(scene(noFx))) && !(rw::KeyOf(scene(all)) == rw::KeyOf(scene(noIcon))), "a layer flip changes the draw key");
        // Float: the Effects rows' floats (the Layers page's Float row): Off, every seat at its rest place
        auto still = scene(all); still.anim.floats = false;
        bool atRest = true; const int n = rw::Build(still, list);
        for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeSeat) { bool on = false; for (int k = 0; k < l.count; ++k) on |= std::fabs(list[i].cx - (l.seats[size_t(k)].x + rw::CanvasX)) < .01f && std::fabs(list[i].cy - (l.seats[size_t(k)].y + rw::CanvasY)) < .01f; atRest &= on; }
        Check(atRest, "Float Off: every seat at its rest place");
    }
    {   // The center's draw in thirds: the pointed third's slice (and only then), the others dimmed, the marks
        const int per[3] = {4, 4, 4};
        const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
        static rw::Instance list[rw::MaxDraw];
        rw::Scene sc; sc.open = true; sc.layout = &l; sc.thirds = true; sc.hasMotions = true; sc.layers = {true, true, false, false};
        auto sectors = [&](const rw::Scene& x) { int c = 0, th = -1; const int n = rw::Build(x, list); for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeSector) { ++c; th = int(list[i].param) & 3; } return std::make_pair(c, th); };
        Check(sectors(sc).first == 0, "rest: no slice lit");
        sc.step.under = {rw::Kind::Quick}; sc.step.centerGrown = true;
        const auto q = sectors(sc);
        Check(q.first == 5 && q.second == rw::ThirdQuick, "Quick cast pointed: its backing, the three fills, its outline last");
        float reach = 0; const int n = rw::Build(sc, list);
        for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeSector) reach = std::max(reach, list[i].w / 2);
        Check(std::fabs(reach - 136 * 1.11f) < .1f, "the slice reaches 151 px grown (11%)");
        sc.thirdGrowPct = 0;
        int nb = rw::Build(sc, list); reach = 0;
        for (int i = 0; i < nb; ++i) if (list[i].recipe == rw::RecipeSector) reach = std::max(reach, list[i].w / 2);
        Check(std::fabs(reach - 136) < .1f, "grow Off: the slice stays at the center's edge");
        sc.thirds = false;
        Check(sectors(sc).first == 0, "thirds off: no slice (the halves' look)");
    }
    {   // 2026-11-16 (the player: the Gestures, Glyphs and Quick cast marks showed gold): each third's mark is its own picture (a seat's
        // icon: RecipeSeat at the mark, 36 px at rest), before every badge body; a picture missing: the drawn mark in silver,
        // never gold. A sub-menu shelf entry with a cell draws it as any icon.
        int per[3] = {4, 4, 4};
        std::vector<rw::SeatIn> in;
        for (int t = 0; t < 3; ++t) for (int i = 0; i < per[t]; ++i) in.push_back({100 + t * 10 + i, -1, t});
        in.push_back({rw::MenuEntryId(rw::ThirdGlyphs), -1, -1});
        const auto l = rw::Build(in.data(), int(in.size()), rw::TreeLayout::Shown);
        static rw::Instance list[rw::MaxDraw];
        std::vector<int16_t> cells(size_t(l.count), int16_t(-1));
        std::vector<uint8_t> mark(size_t(l.count), 0);
        int entry = -1;
        for (int i = 0; i < l.count; ++i) if (rw::MenuOfEntry(l.seats[size_t(i)].id) == rw::ThirdGlyphs) { entry = i; mark[size_t(i)] = 1 + rw::ThirdGlyphs; cells[size_t(i)] = 40; }
        Check(entry >= 0, "pictures: the Glyphs menu entry is on the shelf");
        rw::Scene sc; sc.open = true; sc.layout = &l; sc.thirds = true; sc.hasMotions = true; sc.layers = {true, true, false, false};
        sc.cells = cells.data(); sc.seatMark = mark.data(); sc.atlasPct = 100; sc.thirdCell = {41, 42, 43};
        sc.anim.floats = false;   // every seat at its rest place
        sc.menuBadge[rw::ThirdQuick] = {true, rw::BadgeKind::Coin, 'Y', 28};
        auto isGold = [](const rw::Instance& x) { return std::fabs(x.r - rw::Gold.r) < .01f && std::fabs(x.g - rw::Gold.g) < .01f && std::fabs(x.b - rw::Gold.b) < .01f; };
        auto at = [](const rw::Instance& x, float px, float py) { return std::fabs(x.cx - px) < .01f && std::fabs(x.cy - py) < .01f; };
        int n = rw::Build(sc, list);
        int art = 0, artBeforeBadge = 0, firstBadge = -1, goldStroke = 0, entryArt = 0;
        for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeBadge && firstBadge < 0) firstBadge = i;
        for (int t = 0; t < rw::ThirdCount; ++t) {
            float mx, my; rw::ThirdMarkAt(t, 0, mx, my);
            float u0, v0, u1, v1; rw::CellRect(41 + t, 100, u0, v0, u1, v1);
            for (int i = 0; i < n; ++i) {
                const auto& x = list[i];
                if (x.recipe == rw::RecipeSeat && at(x, mx, my) && std::fabs((x.u0 + x.u1) / 2 - (u0 + u1) / 2) < 1e-4f && std::fabs(x.w - (36 + 2 * rw::SeatRingPx)) < .01f) { ++art; artBeforeBadge += firstBadge < 0 || i < firstBadge; }
                if (x.recipe == rw::RecipeRect && isGold(x) && std::hypot(x.cx - mx, x.cy - my) < 20) ++goldStroke;
            }
        }
        const float ex = l.seats[size_t(entry)].x + rw::CanvasX, ey = l.seats[size_t(entry)].y + rw::CanvasY;
        float eu0, ev0, eu1, ev1; rw::CellRect(40, 100, eu0, ev0, eu1, ev1);
        for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeSeat && at(list[i], ex, ey) && std::fabs((list[i].u0 + list[i].u1) / 2 - (eu0 + eu1) / 2) < 1e-4f && list[i].param > 0) ++entryArt;
        Check(art == 3 && artBeforeBadge == 3 && firstBadge >= 0, "pictures: each third's mark is its picture (a seat's icon, 36 px at rest), under the badges");
        Check(goldStroke == 0, "pictures: no gold stroke on any mark");
        Check(entryArt == 1, "pictures: the Glyphs menu entry draws its picture as an icon");
        auto k1 = rw::KeyOf(sc); auto s2 = sc; s2.thirdCell[1] = -1;
        Check(!(rw::KeyOf(s2) == k1), "pictures: a third's picture coming or going redraws");
        // Missing pictures: the drawn marks (strokes) in silver, never gold; the shelf entry's mark too.
        sc.thirdCell = {-1, -1, -1}; cells[size_t(entry)] = -1;
        rw::text::Metrics m{}; m.ok = true; sc.metrics = &m;   // the marks' fallback draws in the text pass
        n = rw::Build(sc, list);
        int silverStroke = 0; goldStroke = 0;
        for (int t = 0; t < rw::ThirdCount; ++t) {
            float mx, my; rw::ThirdMarkAt(t, 0, mx, my);
            for (int i = 0; i < n; ++i) {
                const auto& x = list[i];
                if ((x.recipe != rw::RecipeRect && x.recipe != rw::RecipeDisc) || std::hypot(x.cx - mx, x.cy - my) > 20) continue;   // strokes and dots
                goldStroke += isGold(x);
                silverStroke += std::fabs(x.r - rw::MarkSilver.r) < .01f && std::fabs(x.b - rw::MarkSilver.b) < .01f;
            }
        }
        int entrySilver = 0;
        for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeRect && std::hypot(list[i].cx - ex, list[i].cy - ey) < 20 && std::fabs(list[i].r - rw::MarkSilver.r) < .01f) ++entrySilver;
        Check(silverStroke > 0 && goldStroke == 0, "no pictures: the thirds' drawn marks in silver, no gold");
        Check(entrySilver > 0, "no pictures: the Glyphs menu entry's drawn mark in silver");
    }
    {   // (the player 2026-11-17: "sizable and have gpt draw them"): the tree marks' painted pictures, the sheet's cell lookup, the size
        // clamp and the fallback to the drawn glyph, with 0, 3 and 24 squares in the sheet.
        namespace ta = framevr::ringwheel::treeart;
        const uint32_t builtMask = ta::MaskRef();
        // the cell lookup: each class x position is one slot, by its skillcategories page, all 24 distinct
        bool distinct = true; std::array<int, ta::Count> seen{};
        for (int c = 0; c < 8; ++c) for (int p = 0; p < 3; ++p) { const int s = ta::Slot(c, p); distinct = distinct && s >= 0 && s < ta::Count && !seen[size_t(s)]++ && s / 3 == c && s % 3 == rw::PageAt(c, p); }
        Check(distinct, "slots: 8 classes x 3 positions are 24 distinct slots, class x 3 + the tree's page");
        Check(ta::Slot(1, 0) == 3 && ta::Slot(1, 1) == 4 && ta::Slot(1, 2) == 5, "slots: Sorceress Fire, Lightning, Cold are slots 3, 4, 5");
        Check(ta::Slot(0, 1) == 2 && ta::Slot(0, 2) == 1, "slots: Amazon's position 1 holds Javelin and Spear (slot 2), position 2 Passive and Magic (slot 1)");
        Check(ta::Slot(3, 0) == 10 && ta::Slot(4, 0) == 13 && ta::Slot(7, 2) == 23, "slots: Paladin's top is Offensive Auras, Barbarian's Combat Masteries, Warlock's last Chaos");
        Check(ta::Slot(-1, 0) == -1 && ta::Slot(8, 0) == -1 && ta::Slot(99, 1) == -1 && ta::Slot(1, -1) == -1 && ta::Slot(1, 3) == -1, "slots: an unknown class or position has none");
        Check(ta::SlotOfArt(ta::ArtId(0)) == 0 && ta::SlotOfArt(ta::ArtId(23)) == 23 && ta::SlotOfArt(ta::ArtFirst - 1) == -1 && ta::SlotOfArt(ta::ArtFirst + ta::Count) == -1 &&
              ta::ArtId(-1) == -1 && ta::ArtId(ta::Count) == -1 && ta::ArtFirst >= 9000 && ta::ArtFirst > rw::MenuEntryFirst + rw::ThirdCount, "ids: slot <-> atlas id, our art (9000 up, clear of the sub-menu entries), none out of range");
        Check(!rw::unbake::Keyed(ta::ArtId(3), 0),"ids: a painted square is never keyed out of its plate (it keeps its colors)");
        {   // the files the art and the script use
            Check(ta::FileName(3) == "tree-sorceress-fire.png" && ta::FileName(7) == "tree-necromancer-poison-and-bone.png" && ta::FileName(0) == "tree-amazon-bow-and-crossbow.png" &&
                  ta::FileName(13) == "tree-barbarian-combat-masteries.png" && ta::FileName(23) == "tree-warlock-chaos.png" && ta::FileName(24).empty() && ta::FileName(-1).empty(), "files: tree-<class>-<tree>.png in kebab case");
            bool kebab = true;
            for (int s = 0; s < ta::Count; ++s) for (const char ch : ta::FileName(s)) kebab = kebab && (ch == '-' || ch == '.' || (ch >= 'a' && ch <= 'z'));
            Check(kebab, "files: lower case letters and hyphens only");
        }
        // the size clamp: 25..300, 0 hides, a value under 25 reads as 25, junk reads as today's 100
        Check(ta::SizePct(0) == 0 && ta::SizePct(-5) == 0 && ta::SizePct(.4f) == 25 && ta::SizePct(24) == 25 && ta::SizePct(25) == 25 && ta::SizePct(100) == 100 &&
              ta::SizePct(300) == 300 && ta::SizePct(301) == 300 && ta::SizePct(1e9f) == 300 && ta::SizePct(NAN) == 100 && ta::SizeScale(100) == 1.f && ta::SizeScale(50) == .5f && ta::SizeScale(0) == 0.f, "size: 25..300%, 0 hides, 100% is today's, out of range clamps");
        Check(ta::ModeOf(0) == ta::Mode::Drawn && ta::ModeOf(1) == ta::Mode::Circle && ta::ModeOf(2) == ta::Mode::Square && ta::ModeOf(-1) == ta::Mode::Drawn, "mode: 0 Drawn, 1 Painted circle, 2 Painted square");
        // the sheet with 0, 3 and 24 squares: which trees draw painted, which ids the atlas is asked for
        std::array<int, 81> ids{};
        auto asked = [&](ta::Mode m, int size, int cls) { return ta::AddIcons(m, size, cls, ids.data(), 0, int(ids.size())); };
        ta::SetMask(0);
        Check(ta::PaintedCount() == 0 && ta::PaintedInClass(1) == 0 && !ta::UsesPainted(ta::Mode::Circle, 1, 0) && asked(ta::Mode::Circle, 100, 1) == 0, "sheet with 0 squares: every tree keeps its drawn glyph, nothing asked of the atlas");
        ta::SetMask(0b111000);   // Sorceress: the pilot three
        Check(ta::PaintedCount() == 3 && ta::PaintedInClass(1) == 3 && ta::PaintedInClass(0) == 0 && ta::PaintedInClass(2) == 0, "sheet with 3 squares: the Sorceress's three");
        Check(ta::UsesPainted(ta::Mode::Circle, 1, 2) && ta::UsesPainted(ta::Mode::Square, 1, 0) && !ta::UsesPainted(ta::Mode::Drawn, 1, 0) && !ta::UsesPainted(ta::Mode::Circle, 0, 0) && !ta::UsesPainted(ta::Mode::Circle, -1, 0) && !ta::UsesPainted(ta::Mode::Circle, 8, 0),
              "sheet with 3 squares: the Sorceress paints, Drawn does not, another class or an unknown one keeps the drawn glyph");
        {
            const int n = asked(ta::Mode::Circle, 100, 1);
            Check(n == 3 && ids[0] == ta::ArtId(3) && ids[1] == ta::ArtId(4) && ids[2] == ta::ArtId(5), "atlas asks: the Sorceress's three ids, in position order");
            Check(asked(ta::Mode::Circle, 100, 0) == 0 && asked(ta::Mode::Drawn, 100, 1) == 0 && asked(ta::Mode::Square, 0, 1) == 0 && asked(ta::Mode::Square, 25, 1) == 3, "atlas asks: another class, Drawn and a hidden mark ask for nothing; a 25% mark does");
            int k = ta::AddIcons(ta::Mode::Circle, 100, 1, ids.data(), 3, int(ids.size()));
            ids[0] = ta::ArtId(3); ids[1] = 7; ids[2] = 8;
            k = ta::AddIcons(ta::Mode::Circle, 100, 1, ids.data(), 3, int(ids.size()));
            Check(k == 5 && ids[3] == ta::ArtId(4) && ids[4] == ta::ArtId(5), "atlas asks: an id already asked is not added twice");
            Check(ta::AddIcons(ta::Mode::Circle, 100, 1, ids.data(), 0, 2) == 2, "atlas asks: never past the request's room");
        }
        ta::SetMask((1u << ta::Count) - 1);
        bool all = ta::PaintedCount() == 24;
        for (int c = 0; c < 8; ++c) all = all && ta::PaintedInClass(c) == 3 && asked(ta::Mode::Circle, 100, c) == 3;
        Check(all, "sheet with 24 squares: every class paints all three trees");
        // the slice: a cell of the decoded sheet, null when empty or the sheet is not the sheet's shape
        {
            ia::icons::Image sheet; sheet.width = ta::SheetW; sheet.height = ta::SheetH; sheet.rgba.assign(size_t(ta::SheetW) * ta::SheetH * 4, 0);
            int x0, y0; ta::CellOrigin(4, x0, y0);
            Check(x0 == 4 * ta::CellPx && y0 == 0, "sheet: slot 4 is the fifth cell of the first row");
            int x6, y6; ta::CellOrigin(6, x6, y6);
            Check(x6 == 0 && y6 == ta::CellPx, "sheet: slot 6 starts the second row");
            for (int y = 0; y < ta::CellPx; ++y) for (int x = 0; x < ta::CellPx; ++x) { uint8_t* p = &sheet.rgba[(size_t(y0 + y) * ta::SheetW + size_t(x0 + x)) * 4]; p[0] = 200; p[1] = 90; p[2] = 30; p[3] = 255; }
            const auto cell = ta::Slice(sheet, 4);
            Check(cell && cell->width == unsigned(ta::CellPx) && cell->height == unsigned(ta::CellPx) && cell->rgba[0] == 200 && cell->rgba[1] == 90 && cell->rgba[3] == 255 && cell->rgba.back() == 255, "slice: a painted cell comes out whole, as painted");
            Check(!ta::Slice(sheet, 3) && !ta::Slice(sheet, 5) && !ta::Slice(sheet, -1) && !ta::Slice(sheet, ta::Count), "slice: an empty cell (no square yet) or a slot out of range gives nothing");
            ia::icons::Image small = sheet; small.width = 100;
            Check(!ta::Slice(small, 4), "slice: a sheet of the wrong shape gives nothing");
        }
        ta::SetMask(0b111000);
        {   // the draw: Sorceress (class 1), painted cells for her three positions
            const int per[3] = {10, 10, 10};
            const auto l = ClassLayout(1, per, rw::TreeLayout::Shown);
            rw::Scene s; s.open = true; s.layout = &l; s.anim = rw::PresetAnim(rw::Preset::Off); s.skillClass = 1; s.atlasPct = 100;
            static rw::Instance list[rw::MaxDraw];
            auto mark = [&](int t, float& x, float& y) { x = rw::CanvasCx + 128 * std::cos(l.dir[size_t(t)]); y = rw::CanvasCy + 128 * std::sin(l.dir[size_t(t)]); };
            auto count = [&](uint32_t recipe, int t, float w = -1) {
                float x, y; mark(t, x, y); int c = 0; const int n = rw::Build(s, list);
                for (int i = 0; i < n; ++i) if (list[i].recipe == recipe && std::fabs(list[i].cx - x) < .01f && std::fabs(list[i].cy - y) < .01f && (w < 0 || std::fabs(list[i].w - w) < .01f)) ++c;
                return c;
            };
            auto anyAt = [&](int t) { float x, y; mark(t, x, y); int c = 0; const int n = rw::Build(s, list);
                for (int i = 0; i < n; ++i) if (std::hypot(list[i].cx - x, list[i].cy - y) < 30 && (list[i].recipe == rw::RecipeSeat || list[i].recipe == rw::RecipeSquare || list[i].recipe == rw::RecipeDisc || list[i].recipe == rw::RecipeRing || list[i].recipe == rw::RecipeTreeGlyph)) ++c;
                return c; };
            const float face = 2 * (rw::MarkR - 3.5f);   // today's outer reach kept: 21 px face, 30 px quad
            // Drawn (the default scene, today's look): the disc, ring and glyph at 28 px, no picture
            Check(count(rw::RecipeDisc, 0, 28) == 1 && count(rw::RecipeRing, 0, 28) == 1 && count(rw::RecipeTreeGlyph, 0) == 1 && count(rw::RecipeSeat, 0) == 0, "Drawn: today's mark exactly (28 px disc and ring, one glyph)");
            s.treeMode = 1; s.treeCell = {10, 11, 12};
            bool circ = true;
            for (int t = 0; t < 3; ++t) {
                float x, y; mark(t, x, y); const auto acc = rw::TreeAccentOf(1, t);
                circ = circ && count(rw::RecipeSeat, t, face + 2 * rw::SeatRingPx) == 1 && count(rw::RecipeTreeGlyph, t) == 0 && count(rw::RecipeDisc, t) == 0 && count(rw::RecipeRing, t) == 0 && count(rw::RecipeSquare, t) == 0;
                const int n = rw::Build(s, list);
                for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeSeat && std::fabs(list[i].cx - x) < .01f && std::fabs(list[i].cy - y) < .01f)
                    circ = circ && std::fabs(list[i].r - acc.r) < .01f && std::fabs(list[i].b - acc.b) < .01f && list[i].param == 2.f;
            }
            Check(circ, "Painted circle: each mark is one seat-style picture (21 px face) with the accent rim, no disc, ring or glyph");
            {   // the circle's picture is the plate's inside (the skill frame's 104 of 132 crop), the square's the whole plate
                float x, y; mark(0, x, y); float u0, v0, u1, v1; rw::CellRect(10, 100, u0, v0, u1, v1);
                const int n = rw::Build(s, list); float halfCircle = 0, halfSquare = 0;
                for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeSeat && std::fabs(list[i].cx - x) < .01f && std::fabs(list[i].cy - y) < .01f) halfCircle = (list[i].u1 - list[i].u0) / 2;
                s.treeMode = 2;
                const int m = rw::Build(s, list);
                for (int i = 0; i < m; ++i) if (list[i].recipe == rw::RecipeSquare && std::fabs(list[i].cx - x) < .01f && std::fabs(list[i].cy - y) < .01f) halfSquare = (list[i].u1 - list[i].u0) / 2;
                const float grow = (face + 2 * rw::SeatRingPx) / face, whole = (u1 - u0) / 2 * grow;
                Check(std::fabs(halfSquare - whole) < 1e-5f && std::fabs(halfCircle - whole * rw::TreeCircleCrop) < 1e-5f && rw::TreeCircleCrop == 1.f && std::fabs(halfCircle - halfSquare) < 1e-5f, "crop: the squares are borderless full-bleed, so the circle and the square both show the whole picture (no bevel crop)");
                s.treeMode = 1;
            }
            s.treeMode = 2;
            bool sq = true;
            for (int t = 0; t < 3; ++t) sq = sq && count(rw::RecipeSquare, t, face + 2 * rw::SeatRingPx) == 1 && count(rw::RecipeSeat, t) == 0 && count(rw::RecipeTreeGlyph, t) == 0 && count(rw::RecipeDisc, t) == 0 && count(rw::RecipeRing, t) == 0;
            Check(sq, "Painted square: each mark is one square picture, no disc, ring or glyph");
            // the fallback: a tree with no cell keeps the drawn glyph, the others paint
            s.treeCell = {10, -1, 12};
            Check(count(rw::RecipeSquare, 0) == 1 && count(rw::RecipeSquare, 1) == 0 && count(rw::RecipeTreeGlyph, 1) == 1 && count(rw::RecipeDisc, 1, 28) == 1 && count(rw::RecipeRing, 1, 28) == 1 && count(rw::RecipeSquare, 2) == 1, "fallback: a tree with no painted cell keeps today's drawn mark beside painted ones");
            s.treeCell = {-1, -1, -1};
            Check(count(rw::RecipeTreeGlyph, 0) == 1 && count(rw::RecipeTreeGlyph, 1) == 1 && count(rw::RecipeTreeGlyph, 2) == 1 && count(rw::RecipeSquare, 0) == 0 && count(rw::RecipeSeat, 0) == 0, "fallback: the sheet with no cells (0 squares) draws the three drawn glyphs");
            s.treeCell = {10, 11, 12}; s.treeMode = 7;
            Check(count(rw::RecipeSeat, 0) == 0 && count(rw::RecipeSquare, 0) == 0 && count(rw::RecipeTreeGlyph, 0) == 1, "mode: a value past the list draws the drawn mark");
            // the size: 100% today's, 200% twice, 50% half, 0 hides the whole mark, in every mode
            s.treeMode = 1;
            s.treeSizePct = 200; Check(count(rw::RecipeSeat, 0, 2 * face + 2 * rw::SeatRingPx) == 1, "size: 200% doubles the painted face");
            s.treeSizePct = 50;  Check(count(rw::RecipeSeat, 0, face / 2 + 2 * rw::SeatRingPx) == 1, "size: 50% halves it");
            s.treeSizePct = 10;  Check(count(rw::RecipeSeat, 0, face / 4 + 2 * rw::SeatRingPx) == 1, "size: under 25 reads as 25");
            s.treeSizePct = 9999; Check(count(rw::RecipeSeat, 0, 3 * face + 2 * rw::SeatRingPx) == 1, "size: over 300 reads as 300");
            s.treeSizePct = 0;
            Check(anyAt(0) == 0 && anyAt(1) == 0 && anyAt(2) == 0, "size 0: the painted mark is hidden");
            s.treeMode = 0; Check(anyAt(0) == 0 && anyAt(2) == 0, "size 0: the drawn mark is hidden too");
            s.treeSizePct = 200; Check(count(rw::RecipeDisc, 0, 56) == 1 && count(rw::RecipeRing, 0, 56) == 1 && std::fabs([&] { std::vector<rw::Instance> g; for (int i = 0, n = rw::Build(s, list); i < n; ++i) if (list[i].recipe == rw::RecipeTreeGlyph) g.push_back(list[i]); return g.empty() ? 0.f : g[0].u0; }() - 2 * 16.f / 1.5f) < .01f, "size 200%: the drawn mark doubles too (disc, ring, glyph)");
            s.treeSizePct = 100; s.treeMode = 1; s.skillClass = -1; s.treeCell = {-1, -1, -1};
            Check(count(rw::RecipeSeat, 0) == 0 && count(rw::RecipeSquare, 0) == 0, "unknown class: no picture, upstream's plain mark");
            s.skillClass = 1; s.treeCell = {10, 11, 12};
            // redraw key: a mode, a size or a cell change composes again
            const auto k0 = rw::KeyOf(s);
            auto s1 = s; s1.treeMode = 2; auto s2 = s; s2.treeSizePct = 150; auto s3 = s; s3.treeCell[1] = 40;
            Check(!(rw::KeyOf(s1) == k0) && !(rw::KeyOf(s2) == k0) && !(rw::KeyOf(s3) == k0) && rw::KeyOf(s) == k0, "key: a new mode, size or picture redraws; nothing changed does not");
            // Tree orbs keep their glyphs (the painted mark is the Trees shown layout's)
            const auto orbs = ClassLayout(1, per, rw::TreeLayout::Orbs);
            rw::Scene so = s; so.layout = &orbs; so.treeMode = 1; so.treeCell = {10, 11, 12};
            int glyphs = 0, seatsAtOrb = 0; const int no = rw::Build(so, list);
            for (int i = 0; i < no; ++i) glyphs += list[i].recipe == rw::RecipeTreeGlyph;
            for (int t = 0; t < 3; ++t) for (int i = 0; i < no; ++i) if (list[i].recipe == rw::RecipeSquare) ++seatsAtOrb;
            Check(glyphs == 3 && seatsAtOrb == 0, "Tree orbs: they keep their three glyphs and draw no painted square");
        }
        ta::SetMask(builtMask);
    }
    {   // Glyph icon (2026-11-18, the player 5:49 PM): the Glyphs page's eight shapes and the center split's four sigils as painted squares
        namespace ga = framevr::ringwheel::glyphart;
        const uint32_t builtMask = ga::MaskRef();
        Check(ga::Count == 12 && ga::Shapes == 8 && ga::Sigils == 4 && ga::SheetW == 576 && ga::SheetH == 432 && ga::SlotOfArt(ga::ArtFirst) == 0 && ga::SlotOfArt(ga::ArtFirst + 11) == 11 && ga::SlotOfArt(ga::ArtFirst + 12) < 0 && ga::SlotOfArt(9223) < 0 && ga::SlotOfArt(9103) < 0,
              "glyph art: 12 slots, a 4 x 3 sheet, ids 9300-9311 that no tree square (9200-9223) or sub-menu picture (9101-9103) owns");
        Check(ga::FileName(0) == "glyph-circle.png" && ga::FileName(3) == "glyph-lightning.png" && ga::FileName(5) == "glyph-v.png" && ga::FileName(7) == "glyph-wave.png" && ga::FileName(8) == "sigil-back.png" &&
              ga::FileName(9) == "sigil-clear.png" && ga::FileName(10) == "sigil-star.png" && ga::FileName(11) == "sigil-hand.png" && ga::FileName(12).empty(), "glyph art files: glyph-<shape>.png and sigil-<name>.png in slot order");
        Check(ga::SizePct(0) == 25 && ga::SizePct(24) == 25 && ga::SizePct(100) == 100 && ga::SizePct(300) == 300 && ga::SizePct(999) == 300 && ga::SizePct(NAN) == 100 && ga::ModeOf(0) == ga::Mode::Drawn && ga::ModeOf(1) == ga::Mode::Circle && ga::ModeOf(2) == ga::Mode::Square,
              "glyph art: size 25..300% (junk reads 100), mode 0 Drawn / 1 circle / 2 square");
        std::array<int, 81> ids{};
        ga::SetMask(0);
        Check(ga::PaintedCount() == 0 && ga::AddIcons(ga::Mode::Circle, ids.data(), 0, int(ids.size())) == 0, "glyph sheet with 0 squares: nothing asked of the atlas, every glyph and sigil stays drawn");
        ga::SetMask(0b101);   // two squares, circle and square
        Check(ga::AddIcons(ga::Mode::Circle, ids.data(), 0, int(ids.size())) == 2 && ids[0] == ga::ArtId(0) && ids[1] == ga::ArtId(2) && ga::AddIcons(ga::Mode::Drawn, ids.data(), 0, int(ids.size())) == 0 && ga::AddIcons(ga::Mode::Circle, ids.data(), 0, 1) == 1,
              "glyph sheet with 2 squares: their two ids, none while Drawn, never past the request's room");
        ga::SetMask(0xFFF);
        Check(ga::PaintedCount() == 12 && ga::AddIcons(ga::Mode::Square, ids.data(), 0, int(ids.size())) == 12, "glyph sheet with 12 squares: all twelve ids");
        ga::SetMask(0xFFFFFFFFu); Check(ga::PaintedCount() == 12 && !ga::Painted(12) && !ga::Painted(-1), "glyph mask: only the twelve slots count");
        {   // the sheet's slice: 12 cells of 144 px, an empty one is null
            ga::Image sheet; sheet.width = unsigned(ga::SheetW); sheet.height = unsigned(ga::SheetH); sheet.rgba.assign(size_t(ga::SheetW) * ga::SheetH * 4, 0);
            int x0, y0; ga::CellOrigin(9, x0, y0);   // slot 9: column 1, row 2
            for (int y = 0; y < ga::CellPx; ++y) for (int x = 0; x < ga::CellPx; ++x) { uint8_t* p = &sheet.rgba[(size_t(y0 + y) * ga::SheetW + size_t(x0 + x)) * 4]; p[0] = 200; p[3] = 255; }
            const auto cell = ga::Slice(sheet, 9);
            Check(x0 == ga::CellPx && y0 == 2 * ga::CellPx && cell && cell->width == unsigned(ga::CellPx) && cell->rgba[0] == 200 && !ga::Slice(sheet, 8) && !ga::Slice(sheet, 12), "glyph slice: slot 9 is column 1, row 2; an empty cell and a slot past 12 give nothing");
        }
        static rw::text::Metrics fm; fm.ok = true; for (int st = 0; st < rw::text::Styles; ++st) for (auto& g : fm.g[size_t(st)]) { g.advance = 9; g.w = 8; g.h = 10; }
        rw::PageView pv; auto sets = ia::NoGlyphs;
        rw::FillGlyphs(pv, sets, 0, [](int) { return std::string("x"); }, [](int) { return -1; });
        static rw::Instance list[rw::MaxDraw];
        rw::Scene s; s.open = true; s.metrics = &fm;
        auto count = [&](uint32_t recipe) { int c = 0; const int n = rw::BuildPage(s, pv, list); for (int i = 0; i < n; ++i) c += list[i].recipe == recipe; return c; };
        const int drawnN = rw::BuildPage(s, pv, list);
        Check(count(rw::RecipeSeat) == 0 && count(rw::RecipeSquare) == 0, "glyph icon Drawn: no painted seat on the Glyphs page");
        s.glyph.mode = 1;
        Check(rw::BuildPage(s, pv, list) == drawnN, "glyph icon Painted with no cells (0 squares): the page is today's, instance for instance");
        for (int slot = 0; slot < ga::Count; ++slot) s.glyph.cell[size_t(slot)] = int16_t(20 + slot);
        Check(count(rw::RecipeSeat) == 10 && count(rw::RecipeSquare) == 0, "glyph icon Painted circle: the eight shapes and the page's two sigils (Back, Hand) are seat-style pictures");
        const int circleN = rw::BuildPage(s, pv, list);
        Check(circleN < drawnN, "glyph icon Painted circle: the pictures replace the faces' strokes (fewer instances)");
        s.glyph.mode = 2;
        Check(count(rw::RecipeSquare) == 10 && count(rw::RecipeSeat) == 0, "glyph icon Painted square: the same ten as plates");
        s.glyph.mode = 1;
        s.glyph.cell.fill(-1); s.glyph.cell[2] = 22; s.glyph.cell[3] = 23;   // two shapes painted, the rest drawn
        Check(count(rw::RecipeSeat) == 2 && rw::BuildPage(s, pv, list) > circleN && rw::BuildPage(s, pv, list) < drawnN, "glyph icon: a shape with no cell keeps its drawn strokes beside painted ones (and its sigil stays drawn)");
        {   // the size: a shape's face is 64 px x size, in the seat quad (face + 2 SeatRingPx)
            s.glyph.cell.fill(-1); s.glyph.cell[0] = 20;
            auto quad = [&] { const int n = rw::BuildPage(s, pv, list); for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeSeat) return list[i].w; return -1.f; };
            s.glyph.sizePct = 100; const float q100 = quad();
            s.glyph.sizePct = 200; const float q200 = quad();
            s.glyph.sizePct = 50; const float q50 = quad();
            Check(std::fabs(q100 - (rw::GlyphIconPx - 7 + 2 * rw::SeatRingPx)) < .01f && std::fabs(q200 - (2 * rw::GlyphIconPx - 7 + 2 * rw::SeatRingPx)) < .01f && std::fabs(q50 - (.5f * rw::GlyphIconPx - 7 + 2 * rw::SeatRingPx)) < .01f,
                  "glyph icon size: 100% is the drawn face (64 px), 200% twice, 50% half");
        }
        {   // a sigil: the main page's split (Glyphs / Gestures), the chooser's (Back / Clear)
            const int per[3] = {10, 10, 10}; const auto lay = ClassLayout(1, per, rw::TreeLayout::Shown);
            static rw::Instance big[rw::MaxDraw];
            rw::Scene m; m.open = true; m.layout = &lay; m.metrics = &fm; m.anim = rw::PresetAnim(rw::Preset::Off); m.hasMotions = true; m.skillClass = 1;
            auto mcount = [&](uint32_t recipe, float w) { int c = 0; const int n = rw::Build(m, big); for (int i = 0; i < n; ++i) c += big[i].recipe == recipe && std::fabs(big[i].w - w) < .01f; return c; };
            const float quadAt100 = ga::SigilPx - 7 + 2 * rw::SeatRingPx;
            Check(mcount(rw::RecipeSeat, quadAt100) == 0, "glyph icon Drawn: the center's split has no painted sigil");
            m.glyph.mode = 1; m.glyph.cell.fill(-1); m.glyph.cell[ga::SlotStar] = 30; m.glyph.cell[ga::SlotHand] = 31;
            m.glyph.cell.fill(-1);
            Check(mcount(rw::RecipeSeat, quadAt100) == 0 && mcount(rw::RecipeSquare, quadAt100) == 0, "glyph icon: a sigil with no cell keeps its drawn strokes");
            m.glyph.cell[ga::SlotStar] = 30; m.glyph.cell[ga::SlotHand] = 31;
            Check(mcount(rw::RecipeSeat, quadAt100) == 2, "glyph icon Painted circle: the Glyphs half's star and the Gestures half's hand are two seat-style pictures");
            m.glyph.sizePct = 200; Check(mcount(rw::RecipeSeat, 2 * ga::SigilPx - 7 + 2 * rw::SeatRingPx) == 2, "glyph icon size 200%: the sigils are twice as big");
            m.glyph.sizePct = 100; m.glyph.mode = 2; Check(mcount(rw::RecipeSquare, quadAt100) == 2 && mcount(rw::RecipeSeat, quadAt100) == 0, "glyph icon Painted square: the sigils are plates");
        }
        {   // the draw key: a row, a size or a cell redraws
            rw::Scene a, b; Check(rw::KeyOf(a) == rw::KeyOf(b), "glyph icon: the same scenes key the same");
            b.glyph.mode = 1; Check(!(rw::KeyOf(a) == rw::KeyOf(b)), "glyph icon: its mode is in the draw key");
            b = a; b.glyph.sizePct = 150; Check(!(rw::KeyOf(a) == rw::KeyOf(b)), "glyph icon: its size is in the draw key");
            b = a; b.glyph.cell[4] = 3; Check(!(rw::KeyOf(a) == rw::KeyOf(b)), "glyph icon: its cells are in the draw key");
        }
        ga::SetMask(builtMask);
    }
    if (g_fail) { std::printf("%d failed\n", g_fail); return 1; }
    std::printf("ring-wheel-test: all passed\n");
    return 0;
}
