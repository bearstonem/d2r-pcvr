// Army X menu: the grouping and slot positions, hover hides the other
// three and the center brings them back, the hysteresis, release in the center does nothing and on a
// circle gives that circle's own order, Attack my target's no-target fallback, Regroup is
// one flick, no army, the army gone at release, left-handed (head-yaw frame).
#include "army_x_menu.h"
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace framevr;
using namespace framevr::xmenu;

static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

// Walk the cursor straight from a to b in steps, as a hand would.
static Hover Walk(Hover h, Vec2f a, Vec2f b, const Tuning& t, int steps = 20) {
    for (int i = 1; i <= steps; ++i) {
        const float k = float(i) / steps;
        h = Step(h, {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k}, t);
    }
    return h;
}

int main() {
    const Tuning t;
    int checks = 0;

    // 1. The layout: Up Attack my target (fan: Spearhead wedge, Marching crescent),
    //    Down Defend me = Protective ring (fan: Phalanx), Left Regroup (no fan), Right Hold here = Hold ground (fan: Fall out).
    //    Every one of the commander's 7 orders once, plus Attack my target and Move and hold; at most 2 in a fan.
    assert(Layout[Up].order == AttackTarget && Layout[Up].count == 2 && Layout[Up].orders[0] == commander::SkillWedgeAgg && Layout[Up].orders[1] == commander::SkillMarchBal);
    assert(Layout[Down].order == commander::SkillRingDef && Layout[Down].count == 1 && Layout[Down].orders[0] == commander::SkillPhalanxDef);
    assert(Layout[Left].order == commander::SkillRegroup && Layout[Left].count == 0);
    assert(Layout[Right].order == commander::SkillHoldGround && Layout[Right].count == 2 && Layout[Right].orders[0] == commander::SkillFallOut &&
           Layout[Right].orders[1] == MoveHold);   // Hold here: Fall out on one side, Move and hold on the other
    assert(MoveHold != AttackTarget && MoveHold != ItemId && !commander::IsCommanderSkill(MoveHold) && std::string(OrderLabel(MoveHold)) == "Move and hold");
    {
        int seen = 0, total = 0, attack = 0, moveHold = 0;
        const auto note = [&](int id) {
            assert(*OrderLabel(id) && atlas::LabelOf(id) >= 0 && atlas::LabelOf(id) < atlas::LabelCount);
            if (id == AttackTarget) ++attack; else if (id == MoveHold) ++moveHold; else { assert(commander::IsCommanderSkill(id)); seen |= 1 << (id - commander::SkillPhalanxDef); }
            ++total;
        };
        for (const auto& b : Layout) {
            assert(b.count >= 0 && b.count <= MaxFan);
            note(b.order);
            for (int s = 0; s < b.count; ++s) {   // a plate of its own: never a rim, the ring or a circle's cell
                note(b.orders[s]);
                const int c = atlas::OrderCell(b.orders[s]);
                assert((c >= atlas::CellPhalanx && c <= atlas::CellFallOut) || c == atlas::CellMoveHold);
            }
        }
        assert(seen == 0x7f && attack == 1 && moveHold == 1 && total == 9);
        assert(atlas::OrderRound(MoveHold) && atlas::OrderRound(commander::SkillFallOut) && atlas::OrderRound(commander::SkillPhalanxDef));   //: every fan order a circle cut from its square master
    }
    assert(!commander::IsCommanderSkill(ItemId) && ItemId != 9000);
    assert(!commander::IsCommanderSkill(AttackTarget) && AttackTarget != ItemId && AttackTarget != 9000 && commander::IsCommanderSkill(AttackFallback));
    ++checks;

    // 2. Positions: parents on the axes; a fan centered on its parent's direction, 45 apart: two at +-22.5 deg (slot 0
    //    counter-clockwise), one straight out.
    {
        const Vec2f up = ParentCenter(Up, t), s0 = SubCenter(Up, 0, t), s1 = SubCenter(Up, 1, t);
        assert(Near(up.x, 0) && Near(up.y, 9));
        const float off = 9.f * std::sin(22.5f * 0.01745329f), out = 9.f + 9.f * std::cos(22.5f * 0.01745329f);
        assert(Near(s0.x, -off) && Near(s0.y, out) && Near(s1.x, off) && Near(s1.y, out));   // up-left, up-right, mirror images
        assert(Near(SlotDeg(Up, 0, 2, 45.f), 112.5f) && Near(SlotDeg(Up, 1, 2, 45.f), 67.5f) && Near(SlotDeg(Down, 0, 1, 45.f), 270.f));
        const Vec2f d0 = SubCenter(Down, 0, t), r0 = SubCenter(Right, 0, t);
        assert(Near(d0.x, 0) && Near(d0.y, -18));   // Phalanx straight below Defend me
        // Hold here's two orders (Move and hold since): Fall out up-right, Move and hold down-right, mirror images
        const Vec2f r1 = SubCenter(Right, 1, t);
        assert(Near(r0.x, out) && Near(r0.y, off) && Near(r1.x, out) && Near(r1.y, -off));
        // The furthest point stays inside the clamp (1 deg past the farthest order).
        const float clamp = t.parentAtDeg + t.fanRadiusDeg + t.subDiamDeg * .5f + 1.f;
        for (int d = 0; d < DirCount; ++d) for (int s = 0; s < Layout[size_t(d)].count; ++s) assert(Len(SubCenter(d, s, t)) + t.subDiamDeg / 2 < clamp);
        // The two Up orders never overlap (centers 2*9*sin(22.5) = 6.9 apart, 5.5 wide), nor touch their parent.
        assert(Len({s1.x - s0.x, s1.y - s0.y}) > t.subDiamDeg);
        assert(Len({s0.x - up.x, s0.y - up.y}) > (t.parentDiamDeg + t.subDiamDeg) / 2);
    }
    ++checks;

    // 3. Hover: in a circle the other three hide; the center brings all four back.
    {
        Hover h;
        auto v = Show(h);
        assert(v.parent[0] && v.parent[1] && v.parent[2] && v.parent[3] && !v.centerRing && v.fanOf < 0);
        h = Walk(h, {0, 0}, {0, 7}, t);
        assert(h.parent == Up && h.sub < 0);
        v = Show(h);
        assert(v.parent[Up] && !v.parent[Down] && !v.parent[Left] && !v.parent[Right]);
        assert(v.centerRing && v.fanOf == Up && v.fanCount == 2);
        h = Walk(h, {0, 7}, {0, 0.5f}, t);
        assert(h.parent < 0);
        v = Show(h);
        assert(v.parent[0] && v.parent[1] && v.parent[2] && v.parent[3] && !v.centerRing && v.fanOf < 0);
        // Regroup has no fan; Defend me and Hold here a fan of one; each hides the other three.
        h = Walk({}, {0, 0}, {-8, 0}, t);
        v = Show(h);
        assert(h.parent == Left && v.fanOf < 0 && v.parent[Left] && !v.parent[Up] && !v.parent[Down] && !v.parent[Right] && v.centerRing);
        h = Walk({}, {0, 0}, {0, -8}, t);
        v = Show(h);
        assert(h.parent == Down && v.fanOf == Down && v.fanCount == 1 && !v.parent[Up] && !v.parent[Left] && !v.parent[Right]);
        h = Walk(h, {0, -8}, {0, 0}, t);
        v = Show(h);
        assert(h.parent < 0 && v.parent[0] && v.parent[1] && v.parent[2] && v.parent[3]);   // back: all four
        h = Walk({}, {0, 0}, {8, 0}, t);
        v = Show(h);
        assert(h.parent == Right && v.fanOf == Right && v.fanCount == 2 && !v.parent[Up] && !v.parent[Down] && !v.parent[Left]);   // Fall out, Move and hold
    }
    ++checks;

    // 4. Hysteresis: in at 4.5, out below 3.0; a diagonal stays on the open parent; a clear move switches.
    {
        Hover h = Step({}, {0, 4.0f}, t);
        assert(h.parent < 0);                        // inside the enter radius: still the center
        h = Step(h, {0, 4.6f}, t);
        assert(h.parent == Up);
        h = Step(h, {0, 3.2f}, t);
        assert(h.parent == Up);                      // between 3.0 and 4.5: stays open
        h = Step(h, {0, 2.9f}, t);
        assert(h.parent < 0);
        h = Step({}, {0, 8}, t);
        h = Step(h, Polar(45.f, 8), t);              // the diagonal: stays Up
        assert(h.parent == Up);
        h = Step(h, Polar(20.f, 8), t);              // 70 deg off Up, within 50 of Right: Right
        assert(h.parent == Right);
        // From the center a circle opens within Circle angle (50, VR UX note) of its line, so the wedges overlap at the
        // diagonals: 45 deg opens one; 55 deg off every line never happens with four axes, so try a narrower angle.
        h = Step({}, Polar(45.f, 8), t);
        assert(h.parent == Up || h.parent == Right);
        assert(Step({}, Polar(44.f, 8), t).parent == Right && Step({}, Polar(46.f, 8), t).parent == Up);   // the nearest line wins
        Tuning narrow = t; narrow.axisInDeg = 40.f;
        h = Step({}, Polar(45.f, 8), narrow);          // under 45: the diagonal opens nothing
        assert(h.parent < 0);
        // Sub stickiness: in at 3.5 of its center, out past 4.5.
        h = Walk({}, {0, 0}, SubCenter(Up, 0, t), t);
        assert(h.parent == Up && h.sub == 0);
        const Vec2f s0 = SubCenter(Up, 0, t);
        h = Step(h, {s0.x + 4.2f, s0.y}, t);         // 4.2 off: still slot 0 (sticky)
        assert(h.sub == 0);
    }
    ++checks;

    // 5. Release rules: the center gives nothing; a circle gives its own order; a fan order gives itself.
    {
        // In the center: nothing (the cancel).
        auto r = Resolve({}, 500, true, t);
        assert(r.what == Release::Center && r.order < 0);
        // Let go on each circle: its own order (Up Attack my target, Down Protective ring, Left Regroup, Right Hold ground).
        const int parentOrder[DirCount] = {AttackTarget, commander::SkillRingDef, commander::SkillRegroup, commander::SkillHoldGround};
        for (int d = 0; d < DirCount; ++d) {
            const Hover h = Walk({}, {0, 0}, ParentCenter(d, t), t);
            r = Resolve(h, 500, true, t);
            assert(h.parent == d && h.sub < 0 && r.what == Release::Issue && r.order == parentOrder[d] && Pointed(h.parent, h.sub) == parentOrder[d]);
            // Anywhere on the circle's inner half, and beside it (not toward its fan), the same.
            const Vec2f c = ParentCenter(d, t), in = Polar(AxisDeg(d) + 180.f, 2.5f), side = Polar(AxisDeg(d) + 90.f, 3.f);
            assert(Resolve(Step(h, {c.x + in.x, c.y + in.y}, t), 500, true, t).order == parentOrder[d]);
            assert(Resolve(Step(h, {c.x + side.x, c.y + side.y}, t), 500, true, t).order == parentOrder[d]);
        }
        // Up, then up-left: Spearhead wedge; up, then up-right: Marching crescent.
        Hover h = Walk(Walk({}, {0, 0}, ParentCenter(Up, t), t), ParentCenter(Up, t), SubCenter(Up, 0, t), t);
        r = Resolve(h, 500, true, t);
        assert(h.sub == 0 && r.what == Release::Issue && r.order == commander::SkillWedgeAgg);
        h = Walk(Walk({}, {0, 0}, ParentCenter(Up, t), t), ParentCenter(Up, t), SubCenter(Up, 1, t), t);
        assert(Resolve(h, 500, true, t).order == commander::SkillMarchBal);
        // A straight push through Down: its one fan order, straight out (Phalanx); through Right to its first order (Fall out).
        assert(Resolve(Walk({}, {0, 0}, SubCenter(Down, 0, t), t), 500, true, t).order == commander::SkillPhalanxDef);
        assert(Resolve(Walk({}, {0, 0}, SubCenter(Right, 0, t), t), 500, true, t).order == commander::SkillFallOut);
        // Every order reachable from the center in one or two straight strokes: each circle's own, then each fan order.
        int reached = 0;
        for (int d = 0; d < DirCount; ++d) {
            const Hover p = Walk({}, {0, 0}, ParentCenter(d, t), t);
            const auto rp = Resolve(p, 500, true, t);
            assert(rp.what == Release::Issue && rp.order == Layout[size_t(d)].order); ++reached;
            for (int s = 0; s < Layout[size_t(d)].count; ++s) {
                const Vec2f target = SubCenter(d, s, t);
                const Hover g = Walk(p, ParentCenter(d, t), target, t);
                const auto rr = Resolve(g, 500, true, t);
                assert(rr.what == Release::Issue && rr.order == Layout[size_t(d)].orders[s]); ++reached;
            }
        }
        assert(reached == 9);   // the four circles and five fan orders (Move and hold since)
        assert(Resolve(Walk(Walk({}, {0, 0}, ParentCenter(Right, t), t), ParentCenter(Right, t), SubCenter(Right, 1, t), t), 500, true, t).order == MoveHold);
        // Regroup is one flick left: no second step.
        h = Walk({}, {0, 0}, {-7, 0}, t);
        r = Resolve(h, 300, true, t);
        assert(r.what == Release::Issue && r.order == commander::SkillRegroup);
        // Between Up's circle and its fan, off every order's direction: the nearest order within Between wins (5 deg) is
        // POINTED (lit, so the let go gives what shows); without it (0), the circle's own order.
        const Vec2f p = ParentCenter(Up, t), q = SubCenter(Up, 1, t);
        const Vec2f gap{p.x + 6.226f, p.y + 4.359f};   // 7.6 out of Up's center at 35 deg: outside its circle, 32.5 deg off Marching crescent's line, 4.8 from it
        assert(Len({gap.x - q.x, gap.y - q.y}) < 5.f && Len({gap.x - q.x, gap.y - q.y}) > t.subInDeg && AngDiff(AngOf({gap.x - p.x, gap.y - p.y}), SlotDeg(Up, 1, 2, 45.f)) > t.pickAngleDeg);
        Hover g = Step(Hover{Up, -1}, gap, t);
        assert(g.parent == Up && g.sub == 1 && Resolve(g, 500, true, t).order == commander::SkillMarchBal);
        Tuning noGap = t; noGap.gapWinDeg = 0;
        g = Step(Hover{Up, -1}, gap, noGap);
        assert(g.sub < 0 && Resolve(g, 500, true, noGap).order == AttackTarget);
        // A release inside the latch does nothing.
        assert(Resolve(h, 50, true, t).what == Release::Early);
        // No army (or the army gone at release): nothing issued, said why; on a circle too.
        r = Resolve(h, 300, false, t);
        assert(r.what == Release::NoArmy && r.order < 0);
        assert(Resolve(Walk({}, {0, 0}, ParentCenter(Up, t), t), 300, false, t).what == Release::NoArmy);
    }
    ++checks;

    // 6. The press: the origin is latched after latchMs (the press's jolt does not leak); the army is read at release.
    {
        Menu m;
        const float right[3] = {1, 0, 0};
        float hand[3] = {0.2f, 1.0f, -0.3f};
        m.Open(1000, hand, right);
        float jolt[3] = {0.2f, 1.015f, -0.3f};       // 1.5 cm press jolt within the latch
        m.Held(1030, jolt, t);
        m.Held(1060, hand, t);
        m.Held(1090, hand, t);                       // latched here
        assert(m.latched && m.hover.parent < 0 && Len(m.cursor) < t.centerInDeg);
        float left[3] = {hand[0] - 0.06f, hand[1], hand[2]};   // 6 cm left = 8.4 deg
        assert(m.Held(1200, left, t));               // hover changed: a tick
        assert(m.hover.parent == Left);
        auto r = m.Let(1300, /*army read now*/ false, t);
        assert(r.what == Release::NoArmy && !m.open);
        m.Open(2000, hand, right);
        r = m.Let(2040, true, t);
        assert(r.what == Release::Early);
    }
    ++checks;

    // 7. Left-handed and head-turned: the cursor is in the head-yaw frame, not the controller's, the
    //    same on either hand; the layout is never mirrored.
    {
        const float origin[3] = {0, 1, 0};
        const float facingMinusZ[3] = {1, 0, 0};           // facing -z: right is +x
        const float facingPlusX[3] = {0, 0, 1};            // turned 90 deg right (facing +x): right is +z
        const float handR[3] = {0.05f, 1, 0}, handT[3] = {0, 1, 0.05f};
        const Vec2f a = Cursor(handR, origin, facingMinusZ, t), b = Cursor(handT, origin, facingPlusX, t);
        assert(Near(a.x, 7.f) && Near(a.y, 0) && Near(b.x, 7.f) && Near(b.y, 0));
        Menu m;const float tilted[3] = {3.f, 0.4f, 0.f};   // a tilted head's right vector: flattened and normalized
        m.Open(0, origin, tilted);
        assert(Near(m.right[0], 1.f) && Near(m.right[1], 0) && Near(m.right[2], 0));
        // Clamp: never past 22 deg, and clamping is no cancel.
        const float far[3] = {0.5f, 1, 0};
        const Vec2f c = Cursor(far, origin, facingMinusZ, t);
        assert(Near(Len(c), 22.f, 1e-2f));
        assert(Step({}, c, t).parent == Right);
    }
    ++checks;

    // 8. The controller, fed what transport sends: the commander's slot (handOutput.slot) AND the bound button's raw state. Only
    //    the physical up edge gives an order. HandControls drops the slot to -1 while the button is still down on a grip
    //    change, a tracking loss (handInput.active) or a 100 ms gap: those freeze the hover, never give.
    {
        FrameIn in;
        in.on = true; in.bound = 3; in.slotFree = true; in.tracked = true; in.army = true;
        in.eye = {0, 1.6f, 0}; in.head = {}; in.hand = 0;
        // held: the commander's slot this frame; down: the raw button.
        const auto frame = [&](Controller& c, double ms, int held, bool down, Vec3 hand) {
            in.nowMs = ms; in.held = held; in.down = down; in.handPos = hand; return c.Step(in, t); };
        const Vec3 rest{0.25f, 1.1f, -0.3f}, left{rest.x - 0.07f, rest.y, rest.z};
        // Spearhead wedge's center (up-left of Attack my target) as a hand place: degrees / gain, cm -> m (facing -z: right is +x).
        const Vec2f w0 = SubCenter(Up, 0, t);
        const Vec3 wedge{rest.x + w0.x / t.gainDegPerCm / 100.f, rest.y + w0.y / t.gainDegPerCm / 100.f, rest.z};
        double ms = 0;
        // Open, wait past the latch, flick left onto Recall (Regroup).
        const auto openOnRecall = [&](Controller& c) {
            auto o = frame(c, ms += 11, 3, true, rest);
            assert(o.event == Event::Opened && o.mute);
            for (int i = 0; i < 10; ++i) frame(c, ms += 11, 3, true, rest);
            for (int i = 0; i < 12; ++i) frame(c, ms += 11, 3, true, left);
            assert(c.menu.open && c.menu.hover.parent == Left);
        };
        Controller c;
        // Not held: nothing, nothing drawn, no mute.
        auto o = frame(c, ms += 11, -1, false, rest);
        assert(o.event == Event::None && !o.mute && o.draw.n == 0);
        // Another slot held (its own button down, ours up): not ours.
        o = frame(c, ms += 11, 2, false, rest);
        assert(o.event == Event::None && !o.mute);
        // Held: opens, mutes the press, draws the four circles.
        o = frame(c, ms += 11, 3, true, rest);
        assert(o.event == Event::Opened && o.mute && o.draw.n == 4);
        for (int i = 0; i < 16; ++i) { o = frame(c, ms += 11, 3, true, rest); assert(o.mute && o.event == Event::None); }
        // Flick up and a little left, 12.4 cm (17.6 deg): Attack my target, then Spearhead wedge.
        bool ticked = false;
        for (int i = 1; i <= 10; ++i) {
            const float k = float(i) / 10;
            o = frame(c, ms += 11, 3, true, {rest.x + (wedge.x - rest.x) * k, rest.y + (wedge.y - rest.y) * k, rest.z});
            ticked |= o.tick;
        }
        assert(ticked && c.menu.hover.parent == Up && c.menu.hover.sub == 0);
        assert(o.draw.n == 6);   // ring, Attack my target, both orders, the rim on the pointed one, its name
        // The real let go: the commander's slot -1 AND the button up.
        o = frame(c, ms += 11, -1, false, wedge);
        assert(o.event == Event::Issue && o.order == commander::SkillWedgeAgg);
        c.Given(o.order, true, ms, t);
        o = frame(c, ms += 11, -1, false, rest);
        assert(o.event == Event::None && !o.mute && o.draw.n == 1);   // the toast
        o = frame(c, ms += 1100, -1, false, rest);
        assert(o.draw.n == 0);   // the toast gone after 1 s

        // M1: each way the commander drops the slot with the button still down gives nothing; the real let go then decides.
        // (a) A grip change on a face button (hand_controls.h: castGrip flips, the cast ends, the button is blocked).
        openOnRecall(c);
        for (int i = 0; i < 5; ++i) { o = frame(c, ms += 11, -1, true, left); assert(o.event == Event::None && o.mute && c.menu.open); }
        // The slot is gone (the commander keeps the button blocked until it comes up), but the hand is tracked: the pointer still
        // follows it. Back to the center: all four return, and the real let go there gives nothing.
        for (int i = 0; i < 8; ++i) { o = frame(c, ms += 11, -1, true, rest); assert(o.event == Event::None); }
        assert(c.menu.hover.parent < 0);
        o = frame(c, ms += 11, -1, false, rest);
        assert(o.event == Event::Canceled && o.order < 0 && o.why == Release::Center);
        frame(c, ms += 11, -1, false, rest);
        // The same drop, then on to another order: the real let go gives that one, not the hover passed on the way.
        openOnRecall(c);
        for (int i = 0; i < 3; ++i) frame(c, ms += 11, -1, true, left);
        for (int i = 1; i <= 10; ++i) {
            const float k = float(i) / 10;
            o = frame(c, ms += 11, -1, true, {left.x + (wedge.x - left.x) * k, left.y + (wedge.y - left.y) * k, rest.z});
        }
        assert(o.event == Event::None && c.menu.hover.parent == Up && c.menu.hover.sub == 0);
        o = frame(c, ms += 11, -1, false, wedge);
        assert(o.event == Event::Issue && o.order == commander::SkillWedgeAgg);
        frame(c, ms += 11, -1, false, rest);
        // And let go where it dropped: that order.
        openOnRecall(c);
        for (int i = 0; i < 5; ++i) frame(c, ms += 11, -1, true, left);
        o = frame(c, ms += 11, -1, false, left);
        assert(o.event == Event::Issue && o.order == commander::SkillRegroup);
        frame(c, ms += 11, -1, false, rest);
        // (b) A tracking loss: handInput.active false drops the slot; the hand untracked as well.
        openOnRecall(c);
        in.tracked = false;
        for (int i = 0; i < 8; ++i) { o = frame(c, ms += 11, -1, true, {rest.x - 0.4f, rest.y, rest.z}); assert(o.event == Event::None && c.menu.open && c.menu.hover.parent == Left); }   // untracked: frozen
        in.tracked = true;
        // Tracking back; the commander keeps the button blocked (slot -1) until a real release: still nothing.
        for (int i = 0; i < 3; ++i) { o = frame(c, ms += 11, -1, true, left); assert(o.event == Event::None); }
        o = frame(c, ms += 11, -1, false, left);
        assert(o.event == Event::Issue && o.order == commander::SkillRegroup);
        frame(c, ms += 11, -1, false, rest);
        // (c) A hitch: a 150 ms gap between frames with the slot dropped.
        openOnRecall(c);
        o = frame(c, ms += 150, -1, true, left);
        assert(o.event == Event::None && c.menu.open);
        o = frame(c, ms += 11, -1, false, left);
        assert(o.event == Event::Issue);
        frame(c, ms += 11, -1, false, rest);
        // (d) occupied (a potion or holster taken by that hand): closes, nothing given, and it waits for the button to come up.
        openOnRecall(c);
        in.occupied = true;
        o = frame(c, ms += 11, -1, true, left);
        assert(o.event == Event::Canceled && o.order < 0);
        in.occupied = false;
        o = frame(c, ms += 11, 3, true, left);
        assert(o.event == Event::None && !o.mute);   // still down from before: no reopen
        o = frame(c, ms += 11, -1, false, left);
        assert(o.event == Event::None);
        // A wheel open: the press is ignored (the commander's wheel keeps it).
        in.wheelOpen = true;
        o = frame(c, ms += 11, 3, true, rest);
        assert(o.event == Event::None && !o.mute);
        in.wheelOpen = false;
        frame(c, ms += 11, -1, false, rest);
        // Held, then a wheel opens: canceled, nothing given; the wheel closing with the button still down does not reopen it.
        o = frame(c, ms += 11, 3, true, rest);
        assert(o.event == Event::Opened);
        in.wheelOpen = true;
        o = frame(c, ms += 11, 3, true, rest);
        assert(o.event == Event::Canceled && o.order < 0);
        in.wheelOpen = false;
        o = frame(c, ms += 11, 3, true, rest);
        assert(o.event == Event::None);
        frame(c, ms += 11, -1, false, rest);
        // The slot given a game skill (or a Draw glyph): the X menu never opens; the commander's press stays.
        in.slotFree = false;
        o = frame(c, ms += 11, 3, true, rest);
        assert(o.event == Event::None && !o.mute);
        in.slotFree = true;
        frame(c, ms += 11, -1, false, rest);
        // The off its button while held: canceled; Off, the bound button does nothing new.
        o = frame(c, ms += 11, 3, true, rest);
        assert(o.event == Event::Opened);
        in.on = false;
        o = frame(c, ms += 11, 3, true, rest);
        assert(o.event == Event::Canceled);
        o = frame(c, ms += 11, 3, true, rest);
        assert(o.event == Event::None && !o.mute);
        in.on = true;
        frame(c, ms += 11, -1, false, rest);
        // No summon class at release: No army, nothing given; the dim cross while there is none.
        openOnRecall(c);
        in.army = false;
        o = frame(c, ms += 11, 3, true, left);
        assert(o.draw.n == 5);   // four faint rings and "No army" (the shade's cell is Move and hold's plate since)
        for (int q = 0; q < 4; ++q) assert(o.draw.q[q].rect.x == atlas::IconRect(atlas::CellRing).x && o.draw.q[q].rect.y == atlas::IconRect(atlas::CellRing).y);
        o = frame(c, ms += 11, -1, false, left);
        assert(o.event == Event::Canceled && o.why == Release::NoArmy);
        in.army = true;
        // Let go back in the center: nothing.
        o = frame(c, ms += 11, 3, true, rest);
        for (int i = 0; i < 10; ++i) frame(c, ms += 11, 3, true, rest);
        o = frame(c, ms += 11, -1, false, rest);
        assert(o.event == Event::Canceled && o.why == Release::Center);
    }
    ++checks;

    // 9. Placement: about 1 m ahead, 8 deg down, toward the holding hand, mirrored for the left; turns with the head's yaw only.
    {
        const Vec3 eye{0, 1.6f, 0};
        Plane r = Place(eye, {}, 0, t), l = Place(eye, {}, 1, t);
        assert(Near(r.center.z, -t.distM * std::cos(t.downDeg * 0.01745329f) * std::cos(t.sideDeg * 0.01745329f), 2e-3f));
        assert(r.center.y < eye.y && Near(eye.y - r.center.y, t.distM * std::sin(t.downDeg * 0.01745329f), 2e-3f));
        assert(r.center.x > 0 && l.center.x < 0 && Near(r.center.x, -l.center.x));
        // The plane faces the eyes: its +z points back toward them.
        const Vec3 n = Rotate(r.q, {0, 0, 1});
        const Vec3 toEye{eye.x - r.center.x, eye.y - r.center.y, eye.z - r.center.z};
        assert(n.x * toEye.x + n.y * toEye.y + n.z * toEye.z > 0.95f * std::sqrt(toEye.x * toEye.x + toEye.y * toEye.y + toEye.z * toEye.z));
        // A head pitched down 30 deg opens the cross at the same height (yaw only).
        const Plane p = Place(eye, AxisAngle({1, 0, 0}, -30.f), 0, t);
        assert(Near(p.center.y, r.center.y, 1e-3f));
        // Turned 90 deg left: ahead is -x, the cursor's right is -z.
        const Plane q = Place(eye, AxisAngle({0, 1, 0}, 90.f), 0, t);
        assert(q.center.x < -0.9f && Near(q.right.z, -1.f, 1e-3f));
        // Sizes: 7.5 deg at 1 m is 13 cm.
        assert(Near(Meters(r, 7.5f), 0.131f, 2e-3f));
        // Atlas cells inside the region and apart.
        for (int a = 0; a < atlas::CellCount; ++a) {
            const auto ra = atlas::IconRect(a);
            assert(ra.x >= 0 && ra.y >= 0 && ra.x + ra.w <= atlas::Width && ra.y + ra.h <= atlas::LabelTop);
        }
        for (int a = 0; a < atlas::LabelCount; ++a) { const auto ra = atlas::LabelRect(a); assert(ra.x + ra.w <= atlas::Width && ra.y + ra.h <= atlas::Height); }
    }
    ++checks;

    // 10. Cost (printed, for the perf box): a closed frame and an open, latched frame of the controller. No allocation
    //     anywhere (no heap types in the engine); the first open decodes nothing (the sheet is painted at session start).
    {
        FrameIn in; in.on = true; in.bound = 3; in.slotFree = true; in.tracked = true; in.army = true; in.eye = {0, 1.6f, 0};
        Controller c; volatile int sink = 0;
        const int N = 2000000;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i) { in.nowMs = i; in.held = -1; sink = sink + c.Step(in, t).draw.n; }
        auto t1 = std::chrono::steady_clock::now();
        in.held = 3; in.down = true; c.Step(in, t);
        for (int i = 0; i < 20; ++i) { in.nowMs += 11; c.Step(in, t); }
        auto t2 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i) { in.nowMs += 0.01; in.handPos = {0.25f + 0.0001f * float(i % 100), 1.1f, -0.3f}; sink = sink + c.Step(in, t).draw.n; }
        auto t3 = std::chrono::steady_clock::now();
        std::printf("army-x-menu-test: cost closed %.1f ns a frame, open %.1f ns a frame (Controller::Step incl. the draw list)\n",
                    std::chrono::duration<double, std::nano>(t1 - t0).count() / N, std::chrono::duration<double, std::nano>(t3 - t2).count() / N);
    }
    ++checks;

    // 11. The paint's checks: the region and its gap must be all zero; premultiply in linear for sRGB.
    {
        const int W = 64, H = 48;
        std::vector<uint8_t> px(size_t(W) * H * 4, 0);
        assert(atlas::RegionClear(px.data(), W, H, 20, 10, 16, 16, 8));
        px[(size_t(5) * W + 14) * 4 + 1] = 1;   // one byte inside the gap (x 12..44, y 2..34)
        assert(!atlas::RegionClear(px.data(), W, H, 20, 10, 16, 16, 8));
        px[(size_t(5) * W + 14) * 4 + 1] = 0; px[(size_t(40) * W + 60) * 4] = 9;   // outside the gap
        assert(atlas::RegionClear(px.data(), W, H, 20, 10, 16, 16, 8));
        assert(atlas::RegionClear(px.data(), W, H, 0, 0, 8, 8, 8));   // a gap past the image's edge is clipped
        const atlas::Premul pm;
        uint8_t in[4] = {255, 128, 0, 128}, out[4];
        pm.Texel(in, out, false);
        assert(out[0] == 128 && out[1] == 64 && out[2] == 0 && out[3] == 128);
        pm.Texel(in, out, true);   // sRGB: 255 at half alpha encodes to about 186, 128 to about 92
        assert(out[0] >= 184 && out[0] <= 188 && out[1] >= 90 && out[1] <= 94 && out[3] == 128);
        uint8_t clear[4] = {200, 200, 200, 0}; pm.Texel(clear, out, true);
        assert(!out[0] && !out[1] && !out[2] && !out[3]);
    }
    ++checks;

    // 23. A fresh player (the menu on no button yet) with an army out: the hand wheel's Edit Slots lists "Army X menu" and
    //     its pick binds it, through the real wheel engine and the calls transport makes round its Step (Binder::Before,
    //     Binder::After). Then one button a weapon set: a swap or a skill in the other set never takes this set's button.
    {
        namespace ia = framevr::interaction;
        struct Wheel {
            ia::Engine engine; ia::Input in; ia::Output out; ia::Reply reply{};
            Binder b; Binder::Result last;
            explicit Wheel(int hand) {
                in.active = true; in.session = 10; in.epoch = 1; in.tracked[0] = in.tracked[1] = true;
                in.head.p = {0, 1.7f, 0}; in.hand[0].p = {.25f, 1.3f, -.4f}; in.hand[1].p = {-.25f, 1.3f, -.4f};
                in.wheelHand = hand; in.snapshot.session = 10; in.snapshot.skillClass = 2;   // a Necromancer
                in.snapshot.bindingsEditable = true; in.snapshot.bindingBank = 0; in.snapshot.activeSet = 1;
                for (auto& skill : in.snapshot.skills) skill.known = true;
                for (int id : {0, 70, 66}) { auto& k = in.snapshot.catalog[in.snapshot.skillCount++]; k.id = id; k.owner = -1; k.known = k.available = true; }   // Raise Skeleton: an army
                Step(); Step(); Step();
            }
            ia::Output Step() {
                in.time += .02; in.at = 1000000 + uint64_t(in.time * 1000); in.snapshot.at = in.at;
                b.Before(in);                      // transport: armyxmenu::Input, before interactions.Step
                out = engine.Step(in);
                last = b.After(&in, &out.request, &reply, in.time * 1000);   // transport: armyxmenu::Request, after it
                return out;
            }
            ia::Output Click(int cell) { in.trigger = 0; Step(); in.hover = cell; in.trigger = 1; return Step(); }
            int Entry() const { for (int i = 0; i < out.editCount; ++i) if (out.editOptions[i].id == ItemId) return i; return -1; }
        };
        for (int hand : {0, 1}) {
            Wheel w(hand);
            assert(!w.b.buttons.Any());                                   // fresh: on no button
            const unsigned before = w.in.snapshot.skillCount;
            w.in.wheel = true; w.Click(0);
            assert(w.out.editor == 2);                                    // the hand slot's picker
            w.Click(ia::SkillCategoryCell(framevr::skillcategories::General));
            const int entry = w.Entry();
            assert(entry >= 0 && "a fresh player with an army out sees Army X menu under General / Items");
            assert(w.in.snapshot.skillCount == before);                   // the entry is out of the snapshot again after each Step
            const ia::Output picked = w.Click(entry);
            const int slot = framevr::skills::HandSlot(hand, 0);
            assert(w.last.took && w.last.tookSlot == slot);
            assert(picked.request.action == ia::Action::BindSkill && picked.request.skill == -1 && picked.request.index == slot && picked.request.bank == 0);   // the game's own clear
            assert(!w.b.buttons.Any());                                   // not ours until the game says so
            w.reply = ia::Reply{picked.request.sequence, 10, ia::Result::Applied};
            w.in.trigger = 0; w.Step();
            assert(w.last.done == BindWatch::Done::Applied && w.last.slot == slot && w.last.set == 0);
            assert(w.b.buttons.Of(0) == slot && w.b.buttons.Of(16) == -1 && w.b.buttons.Any());
            // The other weapon set: its buttons are its own. A game skill on that slot there takes nothing.
            const int none[2] = {-1, -1};
            int tookSlot = -9, tookSet = -9;
            auto other = w.in.snapshot; other.bindingBank = 16; other.skills[size_t(slot)].id = 70;
            assert(w.b.Takeover(other, none, w.in.time * 1000 + 5000, tookSlot, tookSet) == Binder::Taken::No && w.b.buttons.Of(0) == slot);
            auto unknown = other; unknown.bindingBank = -1;   // no set known: nothing taken
            assert(w.b.Takeover(unknown, none, w.in.time * 1000 + 5000, tookSlot, tookSet) == Binder::Taken::No);
            // This set: right after the bind an older snapshot may still show the cleared skill (waited out); later a skill takes it.
            auto same = w.in.snapshot; same.skills[size_t(slot)].id = 70;
            assert(w.b.Takeover(same, none, w.in.time * 1000 + 100, tookSlot, tookSet) == Binder::Taken::No);
            assert(w.b.Takeover(w.in.snapshot, none, w.in.time * 1000 + 5000, tookSlot, tookSet) == Binder::Taken::No);   // nothing on it
            const int glyph[2] = {-1, slot};
            Binder g = w.b;
            assert(g.Takeover(w.in.snapshot, glyph, w.in.time * 1000 + 5000, tookSlot, tookSet) == Binder::Taken::Glyph && tookSlot == slot && tookSet == 0 && !g.buttons.Any());
            assert(w.b.Takeover(same, none, w.in.time * 1000 + 5000, tookSlot, tookSet) == Binder::Taken::Skill && tookSlot == slot && tookSet == 0);
            assert(!w.b.buttons.Any());
        }
        {   // set II binds its own button; set I's stays
            Wheel w(0);
            w.b.buttons.slot[0] = 3;
            w.in.snapshot.bindingBank = 16; w.Step(); w.Step();
            w.in.wheel = true; w.Click(0);
            assert(w.out.editor == 2);
            w.Click(ia::SkillCategoryCell(framevr::skillcategories::General));
            const int entry = w.Entry();
            assert(entry >= 0);
            const ia::Output picked = w.Click(entry);
            assert(picked.request.bank == 16);
            w.reply = ia::Reply{picked.request.sequence, 10, ia::Result::Refused};
            w.in.trigger = 0; w.Step();
            assert(w.last.done == BindWatch::Done::Refused && w.b.buttons.Of(16) == -1 && w.b.buttons.Of(0) == 3);   // refused: nothing kept
        }
        {   // no army, or no hand wheel: no entry
            Wheel w(0);
            ia::Snapshot s = w.in.snapshot;
            assert(Offer(s, 0) && s.catalog[s.skillCount - 1].id == ItemId);
            assert(!Offer(s, 0));                                         // once
            Withdraw(s);
            assert(s.skillCount == w.in.snapshot.skillCount && !Offer(s, -1));
            ia::Snapshot bare = w.in.snapshot; bare.catalog[1].id = 66;   // no summon skill, no summon out
            assert(!Offer(bare, 0));
        }
    }
    ++checks;

    // 12. The bind: Edit Slots' pick of "Army X menu" is a BindSkill with ItemId; BindWatch makes it the game's own clear of
    //     that button's skill (as Draw glyph's pick does) and keeps the slot from the reply. Then that button's press opens
    //     the cross; and the entry's icon is the cross cut from the sheet.
    {
        namespace ia = framevr::interaction;
        const auto bind = [](int slot, int skill, uint64_t sequence) {
            ia::Request r{}; r.action = ia::Action::BindSkill; r.skill = skill; r.skillOwner = -1; r.index = slot; r.sequence = sequence; return r; };
        ia::Request r = bind(1, ItemId, 5);
        BindWatch w;
        assert(w.Take(r) && r.action == ia::Action::BindSkill && r.skill == -1 && r.skillOwner == -1 && r.index == 1);   // the commander's own clear
        assert(!w.Take(r));   // a clear is not ours
        int slot = -9;
        assert(w.Reply(ia::Reply{r.sequence, 7, ia::Result::Pending}, slot) == BindWatch::Done::None && slot == -9);
        assert(w.Reply(ia::Reply{r.sequence + 1, 7, ia::Result::Applied}, slot) == BindWatch::Done::None);   // another request's reply
        assert(w.Reply(ia::Reply{r.sequence, 7, ia::Result::Applied}, slot) == BindWatch::Done::Applied && slot == 1);
        assert(w.Reply(ia::Reply{r.sequence, 7, ia::Result::Applied}, slot) == BindWatch::Done::None);   // answered once
        const float stored = float(slot + 1);   // what the glue keeps in the ini
        assert(SlotOfButton(stored, framevr::skills::Count) == 1 && SlotOfButton(0, framevr::skills::Count) == -1 && SlotOfButton(11, 10) == -1);
        ia::Request refused = bind(3, ItemId, 6);
        assert(w.Take(refused) && w.Reply(ia::Reply{refused.sequence, 7, ia::Result::Refused}, slot) == BindWatch::Done::Refused && slot == 3);
        ia::Request other = bind(2, 75, 7);
        assert(!w.Take(other) && other.skill == 75);   // a real skill's bind passes untouched
        // That button's press: the cross opens (lit with an army, dim with No army without one), as with Edit Slots' bind.
        FrameIn fi; fi.on = true; fi.bound = SlotOfButton(stored, framevr::skills::Count); fi.held = 1; fi.down = true; fi.slotFree = true;
        fi.tracked = true; fi.army = true; fi.eye = {0, 1.6f, 0}; fi.handPos = {.2f, 1.1f, -.3f}; fi.nowMs = 1000;
        Controller c;
        const FrameOut o = c.Step(fi, t);
        assert(o.event == Event::Opened && o.mute && o.draw.n == 4);   // four circles
        Controller dim; fi.army = false;
        const FrameOut d = dim.Step(fi, t);
        const atlas::Rect na = atlas::LabelRect(atlas::LabelNoArmy);
        assert(d.event == Event::Opened && d.draw.n == 5 && d.draw.q[4].rect.x == na.x && d.draw.q[4].rect.y == na.y);   // dim, "No army"
        // The entry's icon: the cross from the sheet, inside a skill frame's crop.
        assert(std::string(IconKey) == "s" + std::to_string(ItemId));
        std::vector<uint8_t> sheet(size_t(atlas::Width) * atlas::Height * 4, 0), icon;
        for (int dir = 0; dir < DirCount; ++dir) {
            const atlas::Rect rc = atlas::IconRect(atlas::CellUp + dir);
            for (int y = 0; y < rc.h; ++y) for (int x = 0; x < rc.w; ++x) {
                uint8_t* p = &sheet[(size_t(rc.y + y) * atlas::Width + size_t(rc.x + x)) * 4];
                p[0] = uint8_t(60 * (dir + 1)); p[1] = 10; p[2] = 20; p[3] = 255;
            }
        }
        atlas::BindIcon(sheet.data(), atlas::Width, atlas::Height, icon);
        assert(icon.size() == size_t(atlas::BindW) * atlas::BindH * 4);
        auto px = [&](int x, int y) { return &icon[(size_t(y) * atlas::BindW + size_t(x)) * 4]; };
        assert(px(66, 33)[3] == 255 && px(66, 33)[0] == 60 && px(66, 97)[0] == 120 && px(34, 65)[0] == 180 && px(98, 65)[0] == 240);   // up, down, left, right
        assert(px(66, 65)[3] == 0 && px(0, 0)[3] == 0 && px(131, 129)[3] == 0);   // the middle and the corners clear
        bool inside = true;
        for (int y = 0; y < atlas::BindH; ++y) for (int x = 0; x < atlas::BindW; ++x)
            if (px(x, y)[3] && (x < 14 || x >= 118 || y < 13 || y >= 117)) inside = false;
        assert(inside);   // all of it where a skill frame's crop shows it
        atlas::BindIcon(nullptr, 0, 0, icon);
        bool clear = icon.size() == size_t(atlas::BindW) * atlas::BindH * 4;
        for (uint8_t b : icon) clear = clear && !b;
        assert(clear);   // no sheet: an empty picture (the entry then shows its letters)
    }
    ++checks;

    // 13. Every circle an order, through the controller as transport feeds it: a flick onto each circle and
    //     a let go gives its own order, lit and named while pointed; the center still cancels. Attack my target: as before's
    //     priority target on the hand's fresh pick, else Spearhead wedge with "No target" over its name in the toast.
    {
        FrameIn in;
        in.on = true; in.bound = 3; in.slotFree = true; in.tracked = true; in.army = true; in.eye = {0, 1.6f, 0}; in.hand = 0;
        const Vec3 rest{0.25f, 1.1f, -0.3f};
        double ms = 10000;
        const auto frame = [&](Controller& c, int held, bool down, Vec3 hand) { in.nowMs = (ms += 11); in.held = held; in.down = down; in.handPos = hand; return c.Step(in, t); };
        const int parentOrder[DirCount] = {AttackTarget, commander::SkillRingDef, commander::SkillRegroup, commander::SkillHoldGround};
        Controller c;
        for (int d = 0; d < DirCount; ++d) {
            const Vec2f p = ParentCenter(d, t);
            const Vec3 to{rest.x + p.x / t.gainDegPerCm / 100.f, rest.y + p.y / t.gainDegPerCm / 100.f, rest.z};
            FrameOut o = frame(c, 3, true, rest);
            assert(o.event == Event::Opened);
            for (int i = 0; i < 10; ++i) frame(c, 3, true, rest);
            for (int i = 1; i <= 10; ++i) { const float k = float(i) / 10; o = frame(c, 3, true, {rest.x + (to.x - rest.x) * k, rest.y + (to.y - rest.y) * k, rest.z}); }
            assert(c.menu.hover.parent == d && c.menu.hover.sub < 0);
            // Lit (its rim) and named under the cross: the circle's own order is what a let go gives.
            // The circle's own art is its order's, cut round from its square master: every one lit with the round rim.
            assert(!atlas::CircleSquare(d));
            const atlas::Rect name = atlas::LabelRect(atlas::LabelOf(parentOrder[d])),
                              rim = atlas::IconRect(atlas::CellRim);
            bool named = false, lit = false;
            for (int q = 0; q < o.draw.n; ++q) {
                named |= o.draw.q[q].rect.x == name.x && o.draw.q[q].rect.y == name.y;
                lit |= o.draw.q[q].rect.x == rim.x && o.draw.q[q].rect.y == rim.y;
            }
            assert(named && lit);
            o = frame(c, -1, false, to);
            assert(o.event == Event::Issue && o.order == parentOrder[d]);
            frame(c, -1, false, rest);
        }
        // The center: nothing, every time.
        FrameOut o = frame(c, 3, true, rest);
        for (int i = 0; i < 12; ++i) frame(c, 3, true, rest);
        o = frame(c, -1, false, rest);
        assert(o.event == Event::Canceled && o.order < 0 && o.why == Release::Center);
        // Attack my target's plan: a fresh pick of the holding hand -> the commander's priority target; none, stale (past 300 ms) or
        // from the future -> no target (wedge); Tactical commands off -> its own reason (wedge too).
        const Aimed fresh{4242, 101.5f, 202.5f, 1000};
        assert(PlanAttack(fresh, 1000, true) == AttackHow::Focus && PlanAttack(fresh, 1300, true) == AttackHow::Focus);
        assert(PlanAttack(fresh, 1301, true) == AttackHow::NoTarget && PlanAttack(fresh, 999, true) == AttackHow::NoTarget);
        assert(PlanAttack(Aimed{0, 0, 0, 1000}, 1000, true) == AttackHow::NoTarget);
        assert(PlanAttack(fresh, 1000, false) == AttackHow::TacticalOff && PlanAttack(Aimed{}, 1000, false) == AttackHow::TacticalOff);
        // Its toast: "Attack my target" when the commander took the target; with no target the wedge, "No target" over its name.
        c.Given(AttackTarget, true, in.nowMs, t);
        o = frame(c, -1, false, rest);
        const atlas::Rect at = atlas::LabelRect(atlas::LabelAttackTarget);
        assert(o.draw.n == 1 && o.draw.q[0].rect.x == at.x && o.draw.q[0].rect.y == at.y);
        c.Given(AttackFallback, true, in.nowMs, t, true);
        o = frame(c, -1, false, rest);
        const atlas::Rect nt = atlas::LabelRect(atlas::LabelNoTarget), sw = atlas::LabelRect(atlas::LabelOf(commander::SkillWedgeAgg));
        assert(o.draw.n == 2 && o.draw.q[0].rect.y == nt.y && o.draw.q[0].rect.x == nt.x && o.draw.q[1].rect.x == sw.x && o.draw.q[1].rect.y == sw.y);
        assert(o.draw.q[0].pose[1] > o.draw.q[1].pose[1]);   // "No target" above the order's name
        c.Given(AttackFallback, false, in.nowMs, t, true);   // refused by the commander: no new toast
        o = frame(c, -1, false, rest);
        assert(o.draw.n == 2);
        o = frame(c, -1, false, rest); in.nowMs = (ms += 1100); o = c.Step(in, t);
        assert(o.draw.n == 0);   // gone after Name time
        // The sheet's cells: the four circles, the four fan plates, the rims, ring and Move and hold's plate (the shade's cell
        // until) in 3 rows; 12 names under them (Canceled since, the targeting mode's toast; Move and hold).
        assert(atlas::CellCount == 12 && atlas::IconRows == 3 && atlas::LabelCount == 12 && atlas::LabelCanceled == 10 && atlas::LabelTop == 8 + 3 * 104);
        assert(atlas::CellMoveHold == 10 && atlas::LabelMoveHold == 11 && atlas::OrderCell(MoveHold) == atlas::CellMoveHold && atlas::LabelOf(MoveHold) == atlas::LabelMoveHold);
        assert(atlas::LabelOf(AttackTarget) == atlas::LabelAttackTarget && atlas::LabelOf(commander::SkillPhalanxDef) == 0 && atlas::LabelOf(ItemId) < 0);
        assert(atlas::OrderCell(commander::SkillRingDef) < 0 && atlas::OrderCell(commander::SkillHoldGround) < 0 && atlas::OrderCell(commander::SkillRegroup) < 0);
    }
    ++checks;

    // 14. Attack my target on the game thread (coordinator): the let go only queues it; the commander's order is given
    //     ONLY by the game thread's Drain (GameTick), once; transport reads the outcome; a queue the game thread
    //     never took times out and is never given. And the press's monster: the holding hand's fresh pick, else the eyes'.
    {
        FocusQueue q;
        int given = 0; uint32_t lastId = 0;
        bool accept = true;
        const auto issue = [&](const Aimed& a) { ++given; lastId = a.id; return accept; };
        const Aimed m{77, 10.f, 20.f, 0, false};
        // Nothing queued: the drain gives nothing; transport sees nothing.
        assert(!q.Drain(issue) && given == 0 && !q.Waiting() && q.Poll(0) == FocusQueue::Outcome::None);
        // Posted: still not given (transport never calls the commander), waiting, no outcome yet.
        assert(q.Post(m, 1000) && given == 0 && q.Waiting());
        assert(q.Poll(1100) == FocusQueue::Outcome::None && given == 0);
        assert(!q.Post(Aimed{88, 0, 0, 0, false}, 1100) && q.target.id == 77);   // one at a time: the first stays
        // The game thread's drain gives it, once, with the posted monster; the outcome reaches transport once.
        assert(q.Drain(issue) && given == 1 && lastId == 77);
        assert(!q.Drain(issue) && given == 1);   // never twice
        assert(q.Poll(1200) == FocusQueue::Outcome::Took && !q.Waiting() && q.Poll(1210) == FocusQueue::Outcome::None);
        // Refused the commander: reported as such (transport then gives the wedge itself).
        accept = false;
        assert(q.Post(m, 2000) && q.Drain(issue) && given == 2 && q.Poll(2010) == FocusQueue::Outcome::Refused);
        // Not taken within TimeoutMs: canceled at the poll, and a late drain gives nothing.
        accept = true;
        assert(q.Post(m, 3000) && q.Poll(3000 + FocusQueue::TimeoutMs) == FocusQueue::Outcome::None);
        assert(q.Poll(3001 + FocusQueue::TimeoutMs) == FocusQueue::Outcome::TimedOut && !q.Waiting());
        assert(!q.Drain(issue) && given == 2);
        // The off its button before the drain: dropped, never given; after the drain, Cancel cannot undo it (it still reports).
        assert(q.Post(m, 4000) && q.Cancel() && !q.Waiting() && !q.Drain(issue) && given == 2);
        assert(q.Post(m, 5000) && q.Drain(issue) && given == 3 && !q.Cancel() && q.Poll(5010) == FocusQueue::Outcome::Took);
        // Off while the game thread is inside Drain (Taking): it cannot be undone, so its outcome reports quietly
        // (logged only, no buzz, toast, mark or wedge after the next On); the next Post is loud again. Off with one queued:
        // dropped; with nothing out: nothing.
        FocusQueue::OffResult inside = FocusQueue::OffResult::None;
        assert(q.Post(m, 6000));
        assert(q.Drain([&](const Aimed& a) { inside = q.Off(); return issue(a); }) && given == 4);
        assert(inside == FocusQueue::OffResult::Quiet && q.Waiting());
        assert(q.Poll(9000) == FocusQueue::Outcome::Took && q.TakeQuiet() && !q.TakeQuiet() && !q.Waiting());
        assert(q.Post(m, 9100) && !q.quiet && q.Drain(issue) && q.Poll(9110) == FocusQueue::Outcome::Took && !q.TakeQuiet());
        assert(q.Post(m, 9200) && q.Off() == FocusQueue::OffResult::Dropped && !q.Drain(issue) && given == 5);
        assert(q.Off() == FocusQueue::OffResult::None && !q.quiet);
        accept = false;   // refused while taken off its button: quiet too (no wedge)
        assert(q.Post(m, 9300) && q.Drain([&](const Aimed& a) { inside = q.Off(); return issue(a); }) && inside == FocusQueue::OffResult::Quiet);
        assert(q.Poll(9310) == FocusQueue::Outcome::Refused && q.TakeQuiet());
        accept = true;
        // The press's monster: the hand's fresh pick wins; none (or stale) -> the eyes' (the outline's rule, flagged); none.
        const Aimed hand{11, 1, 2, 1000, false}, eyes{22, 3, 4, 1000, false}, none{0, 0, 0, 1000, false};
        Aimed p = PickFor(hand, eyes, 1100);
        assert(p.id == 11 && !p.eyes);
        p = PickFor(none, eyes, 1100);
        assert(p.id == 22 && p.eyes && p.x == 3 && PlanAttack(p, 1100, true) == AttackHow::Focus);
        p = PickFor(Aimed{11, 1, 2, 500, false}, eyes, 1100);   // the hand's pick 600 ms old: the eyes'
        assert(p.id == 22 && p.eyes);
        p = PickFor(none, none, 1100);
        assert(p.id == 0 && PlanAttack(p, 1100, true) == AttackHow::NoTarget);
        p = PickFor(none, Aimed{22, 3, 4, 500, false}, 1100);   // both stale: no target
        assert(p.id == 0);
    }
    ++checks;

    // 15. Attack my target's targeting mode, the plan at the let go: a monster already outlined (the hand's
    //     fresh pick, else the eyes') is focused at once, as before; none -> the targeting mode when the reach is above 0;
    //     reach Off -> the wedge exactly as before; Tactical commands off -> the wedge (the reason named first).
    {
        const Aimed outlined{11, 1, 2, 1000, false}, none{0, 0, 0, 1000, false};
        assert(PlanAttack(outlined, 1100, true, true) == AttackHow::Focus);        // skip-if-outlined: no targeting mode
        assert(PlanAttack(PickFor(none, Aimed{22, 3, 4, 1000, true}, 1100), 1100, true, true) == AttackHow::Focus);   // the eyes' too
        assert(PlanAttack(none, 1100, true, true) == AttackHow::Targeting);
        assert(PlanAttack(none, 1100, true, false) == AttackHow::NoTarget);       // reach Off: as before (the wedge)
        assert(PlanAttack(outlined, 1100, true, false) == AttackHow::Focus);
        assert(PlanAttack(none, 1100, false, true) == AttackHow::TacticalOff && PlanAttack(outlined, 1100, false, true) == AttackHow::TacticalOff);
        // The reach: 0 (none) to 68 = the hostile scan's reach rounded up.
        assert(ScanCells > 67.f && ScanCells <= 68.f && Tuning{}.reachCells == ScanCells && Tuning{}.targetS == 4.f);
    }
    ++checks;

    // 16. The targeting ray: transport's own mapping (toss::World / Cell, Stage::World / Room) round-trips; the candidate is the
    //     monster nearest the ray within CatchDeg and the reach; the current one holds within StickDeg; nothing when the mapping
    //     is unknown, the reach is Off, or the monsters are behind the hand or off the ray.
    {
        WorldMap w;
        w.known = true; w.floor = .2f; w.upm = 4.f;
        w.stage.known = true; w.stage.origin = {.3f, 0, -.4f}; w.stage.anchor = {48.f, .2f, 51.f};
        w.stage.base = {0, std::sin(37.f * 0.00872665f), 0, std::cos(37.f * 0.00872665f)};   // the room turned 37 deg in the world
        // Round trips: cells -> world -> cells; room -> world -> room.
        const float cells[3][2] = {{100, 100}, {163.5f, 87.25f}, {12, 300}};
        for (const auto& c : cells) {
            float x = 0, y = 0;
            assert(toss::Cell(toss::World(c[0], c[1], w.floor, w.upm), w.floor, w.upm, x, y) && Near(x, c[0], 1e-2f) && Near(y, c[1], 1e-2f));
        }
        const math::Vec rp{.7f, 1.4f, -.9f};
        const math::Vec back = w.stage.Room(w.stage.World(rp));
        assert(Near(back.x, rp.x) && Near(back.y, rp.y) && Near(back.z, rp.z));
        // A hand 1.2 m over the hero's feet; monsters by cells around the hero at (100, 100).
        const float hx = 100, hy = 100;
        const math::Vec hand = toss::World(hx, hy, w.floor, w.upm) + math::Vec{0, 1.2f, 0};
        const auto body = [&](float x, float y) { return toss::World(x, y, w.floor, w.upm) + math::Vec{0, BodyM, 0}; };
        const auto toward = [&](float x, float y) { return WorldRay{hand, math::Normalized(body(x, y) - hand)}; };
        targetcone::Target m[5] = {{1, 110, 100, true}, {2, 100, 130, true}, {3, 150, 140, false}, {4, 90, 100, true}, {5, 110, 101, true}};
        // Pointed straight at monster 1: it (5 is beside it, a little off); at 2: it; at 3, 64 cells off: it (a wall between does
        // not matter: the army walks there), unless the reach is shorter.
        assert(RayPick(m, 4, hx, hy, w, toward(110, 100), ScanCells, 0) == 0);
        assert(RayPick(m, 5, hx, hy, w, toward(110, 100), ScanCells, 0) == 0);
        assert(RayPick(m, 5, hx, hy, w, toward(100, 130), ScanCells, 0) == 1);
        assert(RayPick(m, 5, hx, hy, w, toward(150, 140), ScanCells, 0) == 2);
        assert(RayPick(m, 5, hx, hy, w, toward(150, 140), 45.f, 0) == -1);       // past the reach: nothing
        assert(RayPick(m, 5, hx, hy, w, toward(150, 140), 0.f, 0) == -1);        // reach Off: no candidates
        assert(DegreesOff(w, toward(110, 100), 110, 100) < .01f && DegreesOff(w, toward(110, 100), 90, 100) > 170.f);   // 4 is behind
        // The current candidate holds against one less than StickDeg nearer the ray; one clearly nearer takes over.
        const WorldRay between = toward(110, 100.5f);
        const float d1 = DegreesOff(w, between, 110, 100), d5 = DegreesOff(w, between, 110, 101);
        assert(std::fabs(d1 - d5) < StickDeg);
        const int fresh = RayPick(m, 5, hx, hy, w, between, ScanCells, 0);
        assert(fresh == 0 || fresh == 4);
        assert(RayPick(m, 5, hx, hy, w, between, ScanCells, 5) == 4 && RayPick(m, 5, hx, hy, w, between, ScanCells, 1) == 0);
        assert(RayPick(m, 5, hx, hy, w, toward(100, 130), ScanCells, 1) == 1);   // swept clearly onto another: it takes over
        // Off the ray by more than CatchDeg: nothing. A ray turned 15 deg away from monster 1 (about the vertical).
        const WorldRay r1 = toward(110, 100);
        const float a = 15.f * 0.01745329f;
        const WorldRay off{hand, {r1.d.x * std::cos(a) - r1.d.z * std::sin(a), r1.d.y, r1.d.x * std::sin(a) + r1.d.z * std::cos(a)}};
        assert(DegreesOff(w, off, 110, 100) > CatchDeg && RayPick(m, 1, hx, hy, w, off, ScanCells, 0) == -1);
        // No mapping (the floor unknown, upm out of range): nothing.
        WorldMap bad = w; bad.known = false;
        assert(RayPick(m, 5, hx, hy, bad, r1, ScanCells, 0) == -1);
        bad = w; bad.upm = 0;
        assert(RayPick(m, 5, hx, hy, bad, r1, ScanCells, 0) == -1);
        // The hand's ray from its room pose (RayOf): the laser's -z through the stage's turn. A hand pose pointing at monster 1.
        const math::Vec roomHand = w.stage.Room(hand);
        const math::Vec roomDir = math::Rotate(math::Conjugate(w.stage.base), r1.d);
        // The turn that takes -z to roomDir.
        const math::Vec from{0, 0, -1};
        const math::Vec ax = math::Cross(from, roomDir);
        const float cw = std::sqrt((1 + math::Dot(from, roomDir)) * 2);
        const math::Quat q{ax.x / cw, ax.y / cw, ax.z / cw, cw * .5f};
        const WorldRay viaPose = RayOf(w, roomHand, q);
        assert(Near(viaPose.p.x, hand.x) && Near(viaPose.p.y, hand.y) && Near(viaPose.p.z, hand.z));
        assert(DegreesOff(w, viaPose, 110, 100) < .05f && RayPick(m, 5, hx, hy, w, viaPose, ScanCells, 0) == 0);

        // The arrow: over the monster's feet in the room (Stage::Room of toss::World, the inverse of the laser's mapping), MarkAboveM
        // up (+ the bob), facing the eye about the vertical, at least MarkMinM wide, the candidate's smaller, a frame of the strip.
        const Vec3 eye{roomHand.x, roomHand.y + .4f, roomHand.z};
        Quad lockedQ{}, candQ{};
        assert(MarkQuad(w, 110, 100, eye, 0, true, lockedQ) && MarkQuad(w, 110, 100, eye, 0, false, candQ));
        const math::Vec feetRoom = w.stage.Room(toss::World(110, 100, w.floor, w.upm));
        assert(Near(lockedQ.pose[0], feetRoom.x, 1e-3f) && Near(lockedQ.pose[2], feetRoom.z, 1e-3f) && Near(lockedQ.pose[1], feetRoom.y + MarkAboveM, 1e-3f));
        float cx = 0, cy = 0;   // the arrow's own spot, back to cells: the monster's
        assert(toss::Cell(w.stage.World({lockedQ.pose[0], lockedQ.pose[1], lockedQ.pose[2]}), w.floor, w.upm, cx, cy));
        assert(Near(cx, 110, .05f) && Near(cy, 100, .05f));
        const Vec3 face = Rotate(Quat{lockedQ.pose[3], lockedQ.pose[4], lockedQ.pose[5], lockedQ.pose[6]}, {0, 0, 1});
        const float tx = eye.x - lockedQ.pose[0], tz = eye.z - lockedQ.pose[2], tn = std::sqrt(tx * tx + tz * tz);
        assert(Near(face.y, 0) && Near(face.x, tx / tn) && Near(face.z, tz / tn));
        assert(lockedQ.w >= MarkMinM && lockedQ.w == lockedQ.h && Near(candQ.w, lockedQ.w * CandidateScale) && lockedQ.sheet == SheetMark);
        assert(lockedQ.rect.x == 0 && lockedQ.rect.w == atlas::MarkFrame && lockedQ.rect.y % (atlas::MarkFrame + atlas::MarkGap) == 0);
        // Far monsters keep MarkDeg of view; the frames cycle through the strip at MarkFps and stay inside it.
        Quad farQ{};
        assert(MarkQuad(w, 150, 140, eye, 0, true, farQ) && farQ.w > lockedQ.w);
        for (int i = 0; i < 40; ++i) {
            const int f = MarkFrameAt(i * 1000.0 / MarkFps);
            assert(f == i % atlas::MarkFrames && atlas::MarkRect(f).y + atlas::MarkFrame <= atlas::MarkStripH);
        }
        assert(atlas::MarkStripH == 568 && !MarkQuad(bad, 110, 100, eye, 0, true, farQ));
        // The draw list holds the largest cross plus the mark: 6 + 1 <= MaxQuads; a toast (2), the hint and two arrows too.
        DrawList d = Draw(Place({0, 1.6f, 0}, {}, 0, t), Hover{Up, 0}, true, t);
        assert(d.n <= 6);
        d.Add(lockedQ);
        assert(d.n <= MaxQuads && d.q[d.n - 1].sheet == SheetMark && d.q[0].sheet == SheetXMenu);
        assert(2 + 1 + 2 <= MaxQuads);
    }
    ++checks;

    // 17. The mode owns its hand's trigger: while it is on, that trigger's own action never fires (the commander's
    //     request for its slot is cleared, as the X menu mutes its hand), the locking pull included; after the lock or a cancel
    //     the trigger is the game's again on its next fresh press (a held one must come up first). Cancel: a fresh grip press, or
    //     the time limit (0: none); nothing is locked then. Off: the commander's at once.
    {
        using framevr::skills::TriggerSlot;
        const auto passes = [](const TargetOut& o, int hand, int slot) {   // what transport still requests after the menu's handler
            int requested = slot; bool interact = true;
            OwnTrigger(o, hand, slot, requested, interact);
            return requested == slot && interact;   // true: the game's own action fires
        };
        TargetIn in; in.on = true; in.timeoutS = 4;
        Targeting g;
        g.Start(0, 1000, 0, 0);
        assert(g.active && g.Busy());
        in.nowMs = 1010;
        TargetOut o = g.Step(in);
        assert(o.mute && !o.tick && o.end == TargetEnd::None);
        assert(!passes(o, 0, TriggerSlot(0)) && passes(o, 0, TriggerSlot(1)) && passes(o, 0, framevr::skills::HandSlot(0, 1)));
        // A candidate: a tick; the same one: none; another: a tick; none: no tick.
        in.candidate = 5; in.nowMs = 1020; assert(g.Step(in).tick);
        in.nowMs = 1030; assert(!g.Step(in).tick);
        in.candidate = 6; in.nowMs = 1040; assert(g.Step(in).tick);
        in.candidate = 0; in.nowMs = 1050; assert(!g.Step(in).tick && g.candidate == 0);
        // A pull on nothing: swallowed (muted), still targeting, said once; held: nothing more; let go and pull on 6: locked.
        in.trigger = 1; in.nowMs = 1060; o = g.Step(in);
        assert(o.missed && o.mute && g.active && !passes(o, 0, TriggerSlot(0)));
        in.nowMs = 1070; o = g.Step(in);
        assert(!o.missed && g.active && o.mute);
        in.candidate = 6; in.nowMs = 1080; o = g.Step(in);
        assert(o.end == TargetEnd::None && g.active);   // still the same held pull: no lock
        in.trigger = 0; in.nowMs = 1090; g.Step(in);
        in.trigger = 1; in.nowMs = 1100; o = g.Step(in);
        assert(o.end == TargetEnd::Locked && g.candidate == 0 && !g.active && o.mute && !passes(o, 0, TriggerSlot(0)));   // the lock's pull: swallowed
        // Still held after the lock: muted until it comes up (no carried-over press); up; the next fresh press is the game's.
        in.nowMs = 1110; o = g.Step(in);
        assert(o.mute && g.Busy() && !passes(o, 0, TriggerSlot(0)));
        in.trigger = 0; in.nowMs = 1120; o = g.Step(in);
        assert(!o.mute && !g.Busy());
        in.trigger = 1; in.nowMs = 1130; o = g.Step(in);
        assert(!o.mute && passes(o, 0, TriggerSlot(0)) && o.end == TargetEnd::None);
        // Started with the trigger held (the X menu on that trigger, or still squeezed): it must come up before a pull locks.
        g.Start(1, 2000, 1, 0);
        in = {}; in.on = true; in.timeoutS = 4; in.candidate = 9; in.trigger = 1; in.nowMs = 2010;
        o = g.Step(in);
        assert(o.end == TargetEnd::None && o.mute && !passes(o, 1, TriggerSlot(1)) && passes(o, 1, TriggerSlot(0)));
        in.trigger = 0; in.nowMs = 2020; g.Step(in);
        in.trigger = 1; in.nowMs = 2030; assert(g.Step(in).end == TargetEnd::Locked);
        // Grip: a fresh press cancels (nothing locked, the candidate dropped); one held at the start must come up first.
        g.Start(0, 3000, 0, 1);
        in = {}; in.on = true; in.timeoutS = 4; in.candidate = 9; in.grip = 1; in.nowMs = 3010;
        assert(g.Step(in).end == TargetEnd::None && g.active);
        in.grip = 0; in.nowMs = 3020; g.Step(in);
        in.grip = 1; in.nowMs = 3030; o = g.Step(in);
        assert(o.end == TargetEnd::Grip && !g.Busy() && o.mute);
        in.nowMs = 3040; assert(!g.Step(in).mute);   // the trigger was up: the game's at once
        // The time row: no lock in 4 s ends it; 0: no limit.
        g.Start(0, 4000, 0, 0);
        in = {}; in.on = true; in.timeoutS = 4; in.candidate = 9; in.nowMs = 7999;
        assert(g.Step(in).end == TargetEnd::None);
        in.nowMs = 8000; assert(g.Step(in).end == TargetEnd::Timeout && !g.Busy());
        g.Start(0, 4000, 0, 0);
        in.timeoutS = 0; in.nowMs = 64000; assert(g.Step(in).end == TargetEnd::None && g.active);
        // Ended from outside (the X menu opened again) with the trigger held: muted until it comes up.
        g.End(1);
        assert(!g.active && g.waitUp);
        // Off: ended, and the trigger is the commander's at once, held or not (the seam goes dark after its Off call).
        g.Start(0, 5000, 0, 0);
        in = {}; in.on = false; in.trigger = 1; in.nowMs = 5010;
        o = g.Step(in);
        assert(o.end == TargetEnd::Off && !o.mute && !g.Busy() && passes(o, 0, TriggerSlot(0)));
        // Never started (reach Off): nothing muted, ever.
        Targeting idle;
        in = {}; in.on = true; in.trigger = 1; in.grip = 1; in.candidate = 3; in.nowMs = 10;
        o = idle.Step(in);
        assert(!o.mute && !o.tick && o.end == TargetEnd::None && passes(o, 0, TriggerSlot(0)) && passes(o, 1, TriggerSlot(1)));
    }
    ++checks;

    // 18. The mark's life: from the lock (or the outlined let go) while the commander's priority target is it; gone when the commander lets it
    //     go (a death, a lost unit, an area change, an order that clears it), never taken in time, or cleared (a new X menu order).
    {
        Mark k;
        assert(k.Step(0, 0) == Mark::End::None && !k.id);
        k.Set(9, 1000);
        assert(k.Step(4, 1100) == Mark::End::None && k.id == 9 && !k.confirmed);   // the old focus, the game thread not yet ticked
        assert(k.Step(9, 1260) == Mark::End::None && k.confirmed);
        assert(k.Step(9, 60000) == Mark::End::None && k.id == 9);                  // as long as before keeps it
        assert(k.Step(0, 60160) == Mark::End::Dropped && !k.id);                   // died / lost / area change / Regroup
        k.Set(9, 1000); k.Step(9, 1100);
        assert(k.Step(12, 1300) == Mark::End::Dropped && !k.id);                   // another focus (the chord, a new lock)
        k.Set(9, 1000);
        assert(k.Step(0, 1000 + Mark::ConfirmMs) == Mark::End::None);
        assert(k.Step(0, 1001 + Mark::ConfirmMs) == Mark::End::NeverTaken && !k.id);
        k.Set(9, 1000); k.Step(9, 1100); k.Clear();                                // a new X menu order
        assert(!k.id && k.Step(9, 1200) == Mark::End::None && !k.id);
        // the game tick stops (a load, an unread hero: it returns before the seam) and its last read still says 9:
        // once no tick has written it for StaleMs the caller says not fresh, and the confirmed mark is dropped, not trusted.
        k.Set(9, 1000);
        assert(k.Step(9, 1100, true) == Mark::End::None && k.confirmed);
        assert(k.Step(9, 5000, true) == Mark::End::None && k.id == 9);
        assert(k.Step(9, 5100, false) == Mark::End::Stale && !k.id);
        k.Set(9, 1000);
        assert(k.Step(0, 1100, false) == Mark::End::Stale && !k.id);               // unconfirmed and stale: dropped too
        assert(Mark::StaleMs >= 2000 && Mark::StaleMs > Mark::ConfirmMs);
    }
    ++checks;

    // 19. Child layout T and Parent depth (a strong flick up ran past Attack my target into its
    //     orders). T: each circle's orders square across its axis, Fan reach from its center, slot 0 counter-clockwise; a flick
    //     along the axis, however strong, stays on the circle; sideways reaches the order; Straight is the fan exactly as before;
    //     Parent depth: past the circle's edge + depth along its axis points nothing and a let go there gives nothing.
    {
        Tuning tt = t; tt.tee = true;
        tt.clampDeg = tt.parentAtDeg + tt.fanRadiusDeg + tt.subDiamDeg * .5f + 1.f;   // the glue's derived clamp
        // Positions: Up's two beside it (left counter-clockwise, right clockwise); one order on the counter-clockwise side.
        const Vec2f up = ParentCenter(Up, tt), w = SubCenter(Up, 0, tt), m = SubCenter(Up, 1, tt);
        assert(Near(w.x, -9) && Near(w.y, 9) && Near(m.x, 9) && Near(m.y, 9) && Near(up.y, w.y));
        const Vec2f ph = SubCenter(Down, 0, tt), fo = SubCenter(Right, 0, tt);
        assert(Near(ph.x, 9) && Near(ph.y, -9));    // Phalanx: Down's counter-clockwise side (its right as drawn)
        assert(Near(fo.x, 9) && Near(fo.y, 9));     // Fall out: above Hold here
        assert(Near(ChildDeg(Up, 0, 2, tt), 180) && Near(ChildDeg(Up, 1, 2, tt), 0) && Near(ChildDeg(Left, 0, 0, tt), 270));
        for (int d = 0; d < DirCount; ++d) for (int s = 0; s < Layout[size_t(d)].count; ++s) {
            const Vec2f q = SubCenter(d, s, tt), p = ParentCenter(d, tt);
            assert(Len({q.x - p.x, q.y - p.y}) > (tt.parentDiamDeg + tt.subDiamDeg) / 2);   // never touching its circle
            assert(Len(q) + tt.subDiamDeg / 2 < tt.clampDeg);                               // reachable inside the clamp
            const Vec2f ax = Polar(AxisDeg(d), 1.f);
            assert(Near(q.x * ax.x + q.y * ax.y, tt.parentAtDeg));                          // level with its circle: nothing beyond it
        }
        // Straight is the fan exactly as before (the same centers as SlotDeg's, every slot).
        for (int d = 0; d < DirCount; ++d) for (int s = 0; s < Layout[size_t(d)].count; ++s) {
            const Vec2f p = ParentCenter(d, t), o = Polar(SlotDeg(d, s, Layout[size_t(d)].count, t.fanSpreadDeg), t.fanRadiusDeg), q = SubCenter(d, s, t);
            assert(Near(q.x, p.x + o.x) && Near(q.y, p.y + o.y) && Near(ChildDeg(d, s, Layout[size_t(d)].count, t), SlotDeg(d, s, Layout[size_t(d)].count, t.fanSpreadDeg)));
        }
        // The overshoot: a straight flick from the center along each axis, out to the clamp, never points an order (T); the same
        // flick up in Straight lands on the fan's side when it wanders (the overshoot), and in T a 10 deg wobble stays too.
        for (int d = 0; d < DirCount; ++d) {
            Hover h;
            for (int i = 1; i <= 60; ++i) {
                const float r = tt.clampDeg * float(i) / 60.f;
                for (const float wob : {-10.f, 0.f, 10.f}) {
                    Hover g = Step(h, Polar(AxisDeg(d) + wob, r), tt);
                    if (r > tt.parentInDeg + .5f) assert(g.parent == d && g.sub == -1);
                    if (wob == 0.f) h = g;
                }
            }
            assert(Resolve(h, 500, true, tt).order == Layout[size_t(d)].order);   // let go at full stretch: the circle's own order
        }
        {   // Straight, the same strong flick up past the circle: it reaches Attack my target's fan (the overshoot)
            Hover h = Walk({}, {0, 0}, {-2.f, t.parentAtDeg + t.fanRadiusDeg}, t, 40);
            assert(h.parent == Up && h.sub == 0);
        }
        // Sideways from the circle reaches each order (a flick up, then left or right).
        for (int d = 0; d < DirCount; ++d) for (int s = 0; s < Layout[size_t(d)].count; ++s) {
            const Vec2f p = ParentCenter(d, tt), q = SubCenter(d, s, tt);
            const Hover h = Walk(Walk({}, {0, 0}, p, tt), p, q, tt);
            assert(h.parent == d && h.sub == s && Resolve(h, 500, true, tt).order == Layout[size_t(d)].orders[s]);
            // Half way across, past pickPastDeg: the order is already chosen by direction (the side, not the line).
            const Hover half = Walk(Walk({}, {0, 0}, p, tt), p, {p.x + (q.x - p.x) * .5f, p.y + (q.y - p.y) * .5f}, tt);
            assert(half.parent == d && half.sub == s);
        }
        // Drawn: the open circle and its orders beside it, the pointed one lit.
        {
            const Plane pl = Place({0, 0, 0}, Quat{}, 0, tt);
            const Hover h = Walk(Walk({}, {0, 0}, up, tt), up, w, tt);
            const DrawList d = Draw(pl, h, true, tt);
            int plates = 0;
            for (int i = 0; i < d.n; ++i) plates += d.q[i].rect.w == atlas::Icon && d.q[i].rect.h == atlas::Icon;
            assert(d.n == 6 && plates >= 4);   // ring, circle, two orders, the lit order's rim; the name
        }
        // Parent depth: past the edge + depth along the axis, nothing; Off (0) no limit; an order still wins; 1 deg to come back.
        Tuning dp = tt; dp.parentDepthDeg = 3.f;
        dp.clampDeg = std::max(dp.clampDeg, dp.parentAtDeg + dp.parentDiamDeg * .5f + dp.parentDepthDeg + 2.f);
        const float edge = dp.parentAtDeg + dp.parentDiamDeg * .5f + dp.parentDepthDeg;   // 15.75 along the axis
        for (int d = 0; d < DirCount; ++d) {
            Hover h = Walk({}, {0, 0}, Polar(AxisDeg(d), edge - .3f), dp);
            assert(h.parent == d && h.sub == -1);
            h = Walk(h, Polar(AxisDeg(d), edge - .3f), Polar(AxisDeg(d), edge + .5f), dp, 4);
            assert(h.parent == d && h.sub == SubPast && Pointed(h.parent, h.sub) == -1);
            assert(Resolve(h, 500, true, dp).what == Release::Past && Resolve(h, 500, true, dp).order == -1);
            h = Step(h, Polar(AxisDeg(d), edge - .5f), dp);
            assert(h.sub == SubPast);                                  // sticky by 1 deg
            h = Step(h, Polar(AxisDeg(d), edge - 1.2f), dp);
            assert(h.sub == -1 && Resolve(h, 500, true, dp).order == Layout[size_t(d)].order);
            const Plane pl = Place({0, 0, 0}, Quat{}, 0, dp);
            const Hover past = Walk(Walk({}, {0, 0}, Polar(AxisDeg(d), edge - .3f), dp), Polar(AxisDeg(d), edge - .3f), Polar(AxisDeg(d), edge + 1), dp, 4);
            const DrawList dl = Draw(pl, past, true, dp);
            for (int i = 0; i < dl.n; ++i) assert(!(dl.q[i].rect.w == atlas::LabelW && dl.q[i].rect.h == atlas::LabelH));   // no name: nothing to give
        }
        {   // Off: no limit, the clamp's edge is still the circle
            const Hover h = Walk({}, {0, 0}, Polar(AxisDeg(Up), tt.clampDeg), tt);
            assert(h.sub == -1 && Resolve(h, 500, true, tt).order == AttackTarget);
        }
        {   // The controller: a let go past the reach is Canceled with Release::Past, nothing given
            Controller c;
            FrameIn in; in.on = true; in.bound = 3; in.held = 3; in.slotFree = true; in.tracked = true; in.army = true; in.down = true;
            in.nowMs = 1000; in.handPos = {0, 1, 0};
            (void)c.Step(in, dp);
            for (int i = 1; i <= 6; ++i) { in.nowMs = 1000 + 20.0 * i; (void)c.Step(in, dp); }   // still through the press settle
            for (int i = 1; i <= 12; ++i) {   // then the hand up past the reach: cm = deg / gain
                in.nowMs = 1120 + 20.0 * i;
                in.handPos = {0, 1.f + (edge + 1.5f) / dp.gainDegPerCm / 100.f * float(i) / 12.f, 0};
                (void)c.Step(in, dp);
            }
            assert(c.menu.hover.parent == Up && c.menu.hover.sub == SubPast);
            in.down = false; in.nowMs += 20;
            const FrameOut o = c.Step(in, dp);
            assert(o.event == Event::Canceled && o.why == Release::Past);
        }
    }
    ++checks;

    // 20. Move and hold: Hold here's second order. T layout: Fall out above Hold here, Move and hold below; picked
    //     and let go it gives MoveHold (the glue starts the spot pick); its plate is round, lit with the round rim. The spot: the
    //     hand's laser on the floor through transport's own mapping (FloorCell: toss::Cell round-trips the aimed cell; nothing
    //     with the ray level or rising, the hand under the floor or no mapping); the commander's 40 sub-tiles from the hero. The queue
    //     carries its kind; the targeting hint names it.
    {
        Tuning tt; tt.tee = true;
        const Vec2f fall = SubCenter(Right, 0, tt), hold = SubCenter(Right, 1, tt), p = ParentCenter(Right, tt);
        assert(Near(fall.x, p.x) && fall.y > p.y && Near(hold.x, p.x) && hold.y < p.y);   // square across Right's axis: above, below
        Hover h = Walk(Walk({}, {0, 0}, p, tt), p, hold, tt);
        assert(h.parent == Right && h.sub == 1 && Pointed(h.parent, h.sub) == MoveHold);
        assert(Resolve(h, 500, true, tt).what == Release::Issue && Resolve(h, 500, true, tt).order == MoveHold);
        assert(Resolve(h, 500, false, tt).what == Release::NoArmy);   // no army: nothing, as every order
        const DrawList dl = Draw(Place({0, 0, 0}, Quat{}, 0, tt), h, true, tt);
        bool plate = false, roundRim = false, squareRim = false, name = false;
        for (int i = 0; i < dl.n; ++i) {
            const auto r = dl.q[i].rect;
            plate = plate || (r.x == atlas::IconRect(atlas::CellMoveHold).x && r.y == atlas::IconRect(atlas::CellMoveHold).y);
            roundRim = roundRim || (r.x == atlas::IconRect(atlas::CellRim).x && r.y == atlas::IconRect(atlas::CellRim).y);
            squareRim = squareRim || (r.x == atlas::IconRect(atlas::CellRimSquare).x && r.y == atlas::IconRect(atlas::CellRimSquare).y);
            name = name || (r.x == atlas::LabelRect(atlas::LabelMoveHold).x && r.y == atlas::LabelRect(atlas::LabelMoveHold).y);
        }
        assert(plate && roundRim && !squareRim && name);   // Move and hold lit: its round plate, the round rim, its name
        Tuning ts; ts.tee = false;   // Straight: the fan of two round Right's axis, Fall out counter-clockwise (above)
        assert(SubCenter(Right, 0, ts).y > 0 && SubCenter(Right, 1, ts).y < 0);
        // The floor cell under the laser.
        WorldMap w;
        w.known = true; w.floor = .2f; w.upm = 4.f;
        w.stage.known = true; w.stage.origin = {.3f, 0, -.4f}; w.stage.anchor = {48.f, .2f, 51.f};
        w.stage.base = {0, std::sin(37.f * 0.00872665f), 0, std::cos(37.f * 0.00872665f)};
        const math::Vec hand = toss::World(100, 100, w.floor, w.upm) + math::Vec{0, 1.2f, 0};
        const math::Vec ground = toss::World(118, 107, w.floor, w.upm);
        float x = 0, y = 0;
        assert(FloorCell(w, WorldRay{hand, math::Normalized(ground - hand)}, x, y) && Near(x, 118, .05f) && Near(y, 107, .05f));
        assert(!FloorCell(w, WorldRay{hand, {1, 0, 0}}, x, y));                          // level: never meets the floor
        assert(!FloorCell(w, WorldRay{hand, math::Normalized(math::Vec{1, .3f, 0})}, x, y));   // rising
        assert(!FloorCell(w, WorldRay{ground - math::Vec{0, .5f, 0}, {0, -1, 0}}, x, y));  // the hand under the floor
        WorldMap bad = w; bad.known = false;
        assert(!FloorCell(bad, WorldRay{hand, math::Normalized(ground - hand)}, x, y));
        // The commander's reach for a commanded spot: 40 sub-tiles from the hero, inclusive.
        assert(companionai::ElasticCombatLeashDistance == 40);
        assert(InReach(140, 100, 100, 100, 40) && !InReach(140.1f, 100, 100, 100, 40) && InReach(100, 60, 100, 100, 40));
        // The game thread's queue carries the kind: Move and hold's spot (id 0) as an Anchor, Attack my target as a Focus.
        FocusQueue q;
        assert(q.Post(Aimed{0, 118, 107, 0, false}, 100, FocusQueue::Kind::Anchor) && q.kind == FocusQueue::Kind::Anchor);
        float gotX = 0; FocusQueue::Kind gotK = FocusQueue::Kind::Focus;
        assert(q.Drain([&](const Aimed& a) { gotX = a.x; gotK = q.kind; return true; }) && gotX == 118 && gotK == FocusQueue::Kind::Anchor);
        assert(q.Poll(110) == FocusQueue::Outcome::Took);
        assert(q.Post(Aimed{77, 1, 2, 0, false}, 200) && q.kind == FocusQueue::Kind::Focus);   // the default
        // The spot pick's hint where the cross was: "Move and hold".
        DrawList hint;
        TargetingHint(hint, Place({0, 0, 0}, Quat{}, 0, tt), tt, atlas::LabelMoveHold);
        assert(hint.n == 1 && hint.q[0].rect.x == atlas::LabelRect(atlas::LabelMoveHold).x && hint.q[0].rect.y == atlas::LabelRect(atlas::LabelMoveHold).y);
        // Its toast when not taken: "Move and hold" over "No army" (or "Canceled").
        Controller c;
        c.Toast(atlas::LabelNoArmy, 1000, tt, atlas::LabelMoveHold);
        assert(c.toastLabel == atlas::LabelNoArmy && c.toastOver == atlas::LabelMoveHold);
    }
    ++checks;

    // Switch arms ("I should not be able to do up, left, down to be in the same menu as the left from center
    // menu"): Back through center, an open circle holds until the pointer is back in the center zone; Anywhere, as before.
    {
        const Vec2f up = ParentCenter(Up, t), left = ParentCenter(Left, t), down = ParentCenter(Down, t), right = ParentCenter(Right, t);
        for (const bool tee : {false, true}) {
            Tuning any = t; any.tee = tee; any.viaCenter = false;
            Tuning via = any; via.viaCenter = true;
            // up > left > down, never through the center: the up circle's own menu all the way.
            Hover h = Walk({}, {0, 0}, up, via);
            assert(h.parent == Up);
            h = Walk(h, up, left, via);
            assert(h.parent == Up);                       // on the left circle's place: still Up's
            h = Walk(h, left, down, via);
            assert(h.parent == Up);
            h = Walk(h, down, right, via);
            assert(h.parent == Up);                       // all the way round
            assert(Show(h).fanOf == Up && !Show(h).parent[Left] && !Show(h).parent[Down] && !Show(h).parent[Right]);   // only Up is drawn
            // It moves within the open one: back on Up's first order, that order is pointed.
            h = Walk(h, right, SubCenter(Up, 0, via), via);
            assert(h.parent == Up && h.sub == 0);
            // center > left reaches left.
            h = Walk({}, {0, 0}, left, via);
            assert(h.parent == Left);
            // up > center > left reaches left: the center lets it go, the next push picks a new arm.
            h = Walk({}, {0, 0}, up, via);
            h = Walk(h, up, {0, 0}, via);
            assert(h.parent < 0);
            h = Walk(h, {0, 0}, left, via);
            assert(h.parent == Left);
            // The center zone is the only way out: just outside it (3.2 of 3.0), on another circle's side, it still holds.
            h = Step(Hover{Up, -1}, {-3.2f, 0}, via);
            assert(h.parent == Up);
            h = Step(h, {-2.9f, 0}, via);
            assert(h.parent < 0);
            // Step says which circle a clear move pointed at and was not given; none on the diagonal or on its own line.
            int refused = -2;
            assert(Step(Hover{Up, -1}, left, via, &refused).parent == Up && refused == Left);
            assert(Step(Hover{Up, -1}, Polar(45.f, 8), via, &refused).parent == Up && refused == -1);
            assert(Step(Hover{Up, -1}, up, via, &refused).parent == Up && refused == -1);
            assert(Step({}, left, via, &refused).parent == Left && refused == -1);
            // Anywhere keeps today's behavior: the same path lands in Left's menu, then Down's; nothing is ever refused.
            h = Walk({}, {0, 0}, up, any);
            h = Walk(h, up, left, any);
            assert(h.parent == Left);
            h = Walk(h, left, down, any);
            assert(h.parent == Down);
            assert(Step(Hover{Up, -1}, left, any, &refused).parent == Left && refused == -1);
            assert(Step(Hover{Up, -1}, Polar(45.f, 8), any).parent == Up);   // the diagonal still holds (Circle hold)
        }
        assert(!Tuning{}.viaCenter);   // the rule's own default is as before; Shipped() turns it on
        // The controller, each hand: up, left, down with the button held, then the let go. Back through center: Up stays the
        // held arm and its own order is given; the menu knows the circle the pointer is on and did not open. Then up > center > left.
        for (const int hand : {0, 1}) {
            for (const bool viaCenter : {true, false}) {
                Tuning u = t; u.tee = true; u.viaCenter = viaCenter;
                FrameIn in;
                in.on = true; in.bound = 3; in.slotFree = true; in.tracked = true; in.army = true;
                in.eye = {0, 1.6f, 0}; in.head = {}; in.hand = hand;
                const Vec3 rest{0.25f, 1.1f, -0.3f};
                const auto at = [&](Vec2f deg) { return Vec3{rest.x + deg.x / u.gainDegPerCm / 100.f, rest.y + deg.y / u.gainDegPerCm / 100.f, rest.z}; };
                Controller c; double ms = 0; Vec3 pos = rest;
                const auto frame = [&](Vec3 p, bool down) { in.nowMs = (ms += 11); in.held = down ? 3 : -1; in.down = down; in.handPos = p; pos = p; return c.Step(in, u); };
                const auto sweep = [&](Vec2f to) { const Vec3 a = pos, b = at(to); for (int i = 1; i <= 12; ++i) { const float k = float(i) / 12; frame({a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, rest.z}, true); } };
                assert(frame(rest, true).event == Event::Opened && c.hand == hand);
                for (int i = 0; i < 10; ++i) frame(rest, true);
                sweep(up); assert(c.menu.hover.parent == Up);
                sweep(left); assert(c.menu.hover.parent == (viaCenter ? Up : Left));
                sweep(down); assert(c.menu.hover.parent == (viaCenter ? Up : Down));
                assert(c.menu.refused == (viaCenter ? Down : -1));   // pointed at, not opened
                // Back to the center lets the held circle go; then left opens Left.
                sweep({0, 0}); assert(c.menu.hover.parent < 0);
                sweep(left); assert(c.menu.hover.parent == Left && c.menu.refused == -1);
                const auto o = frame(pos, false);   // the let go on Left's circle: Left's own order
                assert(o.event == Event::Issue && o.order == Pointed(Left, -1));
                // A new open starts from the center again.
                frame(rest, false);
                assert(frame(rest, true).event == Event::Opened && c.menu.hover.parent < 0);
            }
        }
    }
    ++checks;

    // The tuning the mod runs with: its numbers, what they derive, and the paths a player takes through it.
    {
        const Tuning u = Shipped();
        assert(u.tee && u.viaCenter && Near(u.gainDegPerCm, 0.9f) && Near(u.parentDiamDeg, 6.f) && Near(u.parentAtDeg, 8.f) && Near(u.centerInDeg, 3.5f));
        assert(Near(u.parentInDeg, 4.5f) && u.parentInDeg >= u.centerInDeg && Near(u.gapWinDeg, 3.f) && Near(u.subInDeg, 2.5f) && Near(u.subOutDeg, 3.5f));
        assert(Near(u.pickPastDeg, 7.f) && Near(u.pickAngleDeg, u.fanSpreadDeg * 2.f / 3.f) && Near(u.clampDeg, u.parentAtDeg + u.fanRadiusDeg + u.subDiamDeg * .5f + 1.f));
        assert(u.parentDepthDeg == 0 && Near(u.distM, 1.f) && Near(u.latchMs, 80.f) && Near(u.targetS, 4.f) && Near(u.reachCells, ScanCells));
        for (int d = 0; d < DirCount; ++d) {
            // Onto each circle from the center: it opens, and a let go there gives its own order.
            Hover h = Walk({}, {0, 0}, ParentCenter(d, u), u);
            assert(h.parent == d && h.sub == -1 && Resolve(h, 500, true, u).order == Pointed(d, -1));
            // Each of its orders can be pointed, inside the pointer's limit.
            for (int k = 0; k < Layout[size_t(d)].count; ++k) {
                const Vec2f c = SubCenter(d, k, u);
                assert(Len(c) <= u.clampDeg);
                const Hover on = Walk(h, ParentCenter(d, u), c, u);
                assert(on.parent == d && on.sub == k);
            }
            // Another circle only back through the center.
            int other = 0;   // a circle square to this one (the straight way to the opposite one runs through the center)
            while (!Near(AngDiff(AxisDeg(other), AxisDeg(d)), 90.f)) ++other;
            h = Walk(h, ParentCenter(d, u), ParentCenter(other, u), u);
            assert(h.parent == d);
            h = Walk(h, ParentCenter(other, u), {0, 0}, u);
            assert(h.parent < 0);
            assert(Walk(h, {0, 0}, ParentCenter(other, u), u).parent == other);
        }
    }
    ++checks;

    std::printf("army-x-menu-test: %d groups passed\n", checks);
    return 0;
}
