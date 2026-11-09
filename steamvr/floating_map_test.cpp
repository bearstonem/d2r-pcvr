// Floating map (steamvr/floating_map.h): the shipped values, never pulled (nothing changes), the pull from the wrist, the
// grab only from a press on a map, the grips and stick it takes, the two-hand resize, look to show, a new game and a gap,
// the 6DoF hold, push / pull, Face me, Aim buzz only with a copy out, the stick's zoom on a pointed map, the whole map at
// every size, point to grab, two-hand size, and the copy always the wrist's kind of map.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "floating_map.h"
#include "vr_settings.h"
#include "wrist_full_map.h"
#include "wrist_hud.h"

namespace fm = framevr::floatmap;
namespace math = framevr::math;
static int g_fail = 0;
static void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; } }
static bool Near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }
static float Len(math::Vec v) { return std::sqrt(math::Dot(v, v)); }
// Two turns alike (q and -q are the same turn).
static bool QNear(math::Quat a, math::Quat b, float tol = 1e-5f) {
    return std::fabs(std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) - 1) <= tol;
}
static math::Quat Roll(float a) { return {0, 0, std::sin(a * .5f), std::cos(a * .5f)}; }
static math::Quat AxisAngle(math::Vec axis, float a) {
    const auto n = math::Normalized(axis); const float s = std::sin(a * .5f);
    return {n.x * s, n.y * s, n.z * s, std::cos(a * .5f)};
}

// A standing player at 1.6 m looking down -Z, the right controller pointing at `at`.
static fm::Input Standing(double t, math::Vec at = {0, 0, -10}) {
    fm::Input in;
    in.time = t; in.session = 7; in.gameplay = true; in.handFree = true;
    in.head = {0, 1.6f, 0}; in.headQ = {0, 0, 0, 1}; in.headValid = true;
    in.hand[0] = {.2f, 1.2f, -.3f}; in.hand[1] = {-.2f, 1.2f, -.3f}; in.handTracked[0] = in.handTracked[1] = true;
    in.aim = in.hand[0]; in.aimTracked = true;
    // aim: -Z of aimQ toward `at`
    const auto d = math::Normalized(at - in.aim);
    const float yaw = std::atan2(-d.x, -d.z), pitch = std::asin(std::fmax(-1.f, std::fmin(1.f, d.y)));
    in.aimQ = math::Multiply(math::Yaw(yaw), framevr::panels::Pitch(pitch));
    return in;
}

// The right controller at `from`, its -Z toward `to`.
static math::Quat PointFrom(math::Vec from, math::Vec to) {
    const auto d = math::Normalized(to - from);
    return math::Multiply(math::Yaw(std::atan2(-d.x, -d.z)), framevr::panels::Pitch(std::asin(std::fmax(-1.f, std::fmin(1.f, d.y)))));
}
static void Aim(fm::Input& in, math::Vec from, math::Vec to) { in.hand[0] = from; in.aim = from; in.aimQ = PointFrom(from, to); }
// The hit test as it was until 2026-11-03 (the laser exactly on either face): Point to grab Off has to stay this.
static bool TodayHit(math::Vec ray, math::Vec dir, fm::Pose at, float size) {
    math::PanelHit hit;
    if (math::IntersectPanel(ray, dir, at.p, at.q, size, size, hit)) return true;
    return math::IntersectPanel(ray, dir, at.p, fm::Unit(math::Multiply(at.q, math::Yaw(3.14159265f))), size, size, hit);
}
// The tests' base: each value at its plain setting (35 cm, put away 15 cm, always shown, Face me Off, exactly on it, Left
// Grip only while held), so a case moves one value at a time; the shipped values have their own block.
static fm::Tuning Plain() {
    fm::Tuning t; t.size = .35f; t.awayM = .15f; t.lookDeg = 0; t.faceOnRelease = false; t.aimBuzz = true; t.aimDeg = 0; t.bothGripsS = 0;
    return t;
}
// A copy of `size` m out at `at`: pulled from a wrist map held there, and let go. Put away Off, so it stays.
static fm::Output PlaceCopy(fm::Engine& e, const fm::Tuning& w, fm::Pose at, double& time) {
    fm::Tuning keep = w; keep.awayM = 0;
    fm::Input in = Standing(time, at.p);
    in.wristShown = true; in.wristP = at.p; in.wristQ = at.q; in.wristSize = .24f;
    Aim(in, at.p + math::Rotate(at.q, {.1f, -.2f, .5f}), at.p);
    e.Step(in, keep);                 // Right Grip up
    in.time = time += .02; in.grip[0] = 1; e.Step(in, keep);
    in.time = time += .02; in.grip[0] = 0;
    const auto o = e.Step(in, keep);
    time += .02;
    return o;
}

// A place to the left and below the eyes (60 cm, 25 deg left, 30 deg down, facing them), where most cases leave the copy.
static fm::Pose HomePose() {
    const auto q = math::Multiply(math::Yaw(25 * fm::Deg), framevr::panels::Pitch(-30 * fm::Deg));
    return {math::Vec{0, 1.6f, 0} + math::Rotate(q, {0, 0, -.6f}), q};
}
// Straight ahead at the eyes' height, 60 cm, `downDeg` below the view, facing them.
static fm::Pose Ahead(float downDeg) {
    const auto q = framevr::panels::Pitch(-downDeg * fm::Deg);
    return {math::Vec{0, 1.6f, 0} + math::Rotate(q, {0, 0, -.6f}), q};
}
// A copy pulled out and let go at `at`, the steps from `start` (0.04 s; the cases go on 0.35 s or more later).
static fm::Output Out(fm::Engine& e, const fm::Tuning& t, fm::Pose at = HomePose(), double start = .6) {
    double time = start;
    return PlaceCopy(e, t, at, time);
}

int main() {
    const fm::Tuning t = Plain();
    {   // the shipped values (tuned in the headset 2026-11-06), one constexpr each
        const fm::Tuning s;
        Check(Near(s.size, .20f) && Near(s.awayM, .25f) && Near(s.lookDeg, 30) && s.faceOnRelease && Near(s.aimDeg, 15) && Near(s.bothGripsS, .25f) && s.aimBuzz,
              "shipped: 20 cm, put away within 25 cm, shown within 30 deg, Face me On, point to grab 15 deg, two-hand size 250 ms, Aim buzz On");
        Check(fm::SizeM == s.size && fm::AwayM == s.awayM && fm::LookDeg == s.lookDeg && fm::FaceMe == s.faceOnRelease && fm::PointDeg == s.aimDeg &&
              fm::BothGripsS == s.bothGripsS && fm::AimBuzz == s.aimBuzz, "the struct's defaults are the constexpr block");
    }
    {   // never pulled: nothing drawn or captured, no buzz, the stick and the grips untouched, whatever the hands do away
        // from the raised wrist map; pointed at it, only the stick stays upstream's (no zoom by pointing, no buzz)
        const fm::Tuning s;   // shipped
        const math::Vec wristP{-.2f, 1.2f, -.35f};
        const auto wristQ = fm::FaceEyes(wristP, {0, 1.6f, 0});
        const auto normal = math::Rotate(wristQ, {0, 0, 1});
        fm::Engine e;
        bool quiet = true, onWrist = true;
        double time = 1;
        for (int i = 0; i < 400; ++i, time += .011) {
            fm::Input in = Standing(time, {0, 0, -10});   // pointing far ahead
            in.wristShown = (i / 50) % 2 == 0; in.wristP = wristP; in.wristQ = wristQ; in.wristSize = .24f;
            in.leftFree = true; in.aimLeft = in.hand[1]; in.aimLeftQ = PointFrom(in.hand[1], {0, 0, -10}); in.aimLeftTracked = true;
            in.stickY = std::sin(i * .3f); in.stickX = std::cos(i * .2f);
            in.grip[0] = (i / 7) % 2 ? 1.f : 0.f; in.grip[1] = (i / 11) % 2 ? 1.f : 0.f;
            const auto o = e.Step(in, s);
            quiet = quiet && !o.shown && !o.want && o.pulse < 0 && !o.aimEdge && !o.awayEdge && !o.pointed && !o.own[0] && !o.own[1] && !o.ownStick &&
                    o.event == fm::Event::None && !e.Placed();
            // pointed right at the raised wrist map, Right Grip up: no buzz, no zoom by pointing, the stick the game's
            fm::Input at = Standing(time + .005, wristP); at.hand[0] = wristP + normal * .5f; at.aim = at.hand[0]; at.aimQ = PointFrom(at.aim, wristP);
            at.wristShown = true; at.wristP = wristP; at.wristQ = wristQ; at.wristSize = .24f; at.stickY = 1; at.grip[0] = 0; at.grip[1] = 0;
            fm::Engine w; w.Step(at, s); at.time += .01;
            const auto p = w.Step(at, s);
            onWrist = onWrist && !p.aimEdge && p.pulse < 0 && !p.pointed && !p.ownStick && !p.own[0] && !p.own[1] && !p.shown && !p.want;
        }
        Check(quiet, "never pulled, hands away from the wrist map: nothing drawn, captured or buzzed; no grip or stick taken; no event");
        Check(onWrist, "never pulled, pointing at the raised wrist map with Right Grip up: no buzz, the stick never the map's, nothing drawn");
    }
    {   // a pulled copy shows and wants the map; out of the world it hides and nothing is captured
        fm::Engine e;
        auto o = Out(e, t);
        Check(o.event == fm::Event::Released && o.shown && o.want && Near(o.size, .35f), "pulled and let go: shown, the map wanted, its size");
        Check(Near(Len(o.p - HomePose().p), 0, 1e-3f), "where it was let go");
        fm::Input out = Standing(1.02); out.gameplay = false;
        o = e.Step(out, t);
        Check(!o.shown && !o.want, "out of the world (a wheel, VR Settings): hidden and nothing captured");
    }
    {   // the grab: a press that starts pointing at it; never a press that starts elsewhere and sweeps on
        fm::Engine e;
        auto o = Out(e, t);
        const math::Vec center = o.p;
        fm::Input in = Standing(1.01, {0, 1.6f, -5}); in.grip[0] = 1;   // pressed pointing away
        o = e.Step(in, t);
        Check(!o.held && !o.own[0], "a press pointing elsewhere: not ours");
        in = Standing(1.02, center); in.grip[0] = 1;   // still held, now sweeping onto it
        o = e.Step(in, t);
        Check(!o.held && !o.own[0], "a held press sweeping onto it never grabs");
        in.grip[0] = 0; in.time = 1.03; e.Step(in, t);
        in.grip[0] = 1; in.time = 1.04;
        o = e.Step(in, t);
        Check(o.held && o.own[0] && o.event == fm::Event::Grabbed && o.pulse == 0, "a fresh press on it grabs, takes the grip, buzzes the right hand");
        // move the hand 20 cm right: the map follows
        fm::Input m = in; m.time = 1.05; m.hand[0].x += .2f; m.aim.x += .2f;
        o = e.Step(m, t);
        Check(o.held && o.p.x > center.x + .15f, "it follows the hand");
        m.grip[0] = 0; m.time = 1.06;
        o = e.Step(m, t);
        Check(!o.held && !o.own[0] && o.event == fm::Event::Released, "let go");
        const auto kept = o.p;
        o = e.Step(Standing(1.07), t);
        Check(Near(o.p.x, kept.x) && Near(o.p.z, kept.z), "it stays where it was let go");
        fm::Input busy = Standing(1.08, kept); busy.grip[0] = 1; busy.handFree = false;
        o = e.Step(busy, t);
        Check(!o.held && !o.own[0], "a hand holding a bottle or drawing a bow never grabs");
    }
    {   // two hands: Left Grip while the right holds it; the size follows the hands' spread
        fm::Engine e;
        auto o = Out(e, t);
        fm::Input in = Standing(1.01, o.p); in.grip[0] = 1;
        e.Step(in, t);
        in.time = 1.02; in.grip[1] = 1;
        o = e.Step(in, t);
        Check(o.resizing && o.own[1] && o.event == fm::Event::ResizeStart, "Left Grip while holding: a resize starts, the left grip is ours");
        in.time = 1.03; in.hand[0].x = .4f; in.hand[1].x = -.4f;   // the spread doubles (0.4 -> 0.8 m)
        o = e.Step(in, t);
        Check(Near(o.size, .70f, 1e-3f), "twice the spread: twice the size");
        in.time = 1.04; in.hand[0].x = 5; in.hand[1].x = -5;
        o = e.Step(in, t);
        Check(Near(o.size, fm::MaxSize), "never past the largest");
        in.time = 1.05; in.hand[0].x = .2f; in.hand[1].x = -.2f;
        o = e.Step(in, t);
        Check(Near(o.size, .35f, 1e-3f), "back to the start spread: the start size");
        in.time = 1.06; in.grip[1] = 0;
        o = e.Step(in, t);
        Check(!o.resizing && !o.own[1] && o.event == fm::Event::ResizeEnd && o.held && Near(o.size, .35f), "left let go: resize done at that size, the right still holds");
        Check(Near(fm::Resized(.35f, .4f, .04f), fm::MinSize), "never under the smallest");
        fm::Input left = Standing(1.1); left.grip[1] = 1;
        fm::Engine e2; Out(e2, t);
        o = e2.Step(left, t);
        Check(!o.own[1] && !o.resizing, "Left Grip alone is never ours");
    }
    {   // review M1: a grip the map held stays swallowed until it is let go, whatever ends the grab or the resize
        fm::Engine e;
        auto o = Out(e, t);
        fm::Input in = Standing(1.01, o.p); in.grip[0] = 1; e.Step(in, t);
        in.time = 1.02; in.grip[1] = 1; o = e.Step(in, t);
        Check(o.resizing && o.own[0] && o.own[1], "both hands hold it");
        in.time = 1.03; in.grip[0] = 0; o = e.Step(in, t);
        Check(!o.held && !o.resizing && !o.own[0] && o.own[1], "the right let go first: the left, still squeezed, stays swallowed");
        in.time = 1.04; o = e.Step(in, t);
        Check(o.own[1], "and the next frame too");
        in.time = 1.05; in.grip[1] = 0; o = e.Step(in, t);
        Check(!o.own[1], "the left let go: passed again");
        in.time = 1.06; in.grip[1] = 1; o = e.Step(in, t);
        Check(!o.own[1], "a later Left Grip alone is the game's");
        fm::Engine g;
        o = Out(g, t, HomePose(), 1.6);
        fm::Input h = Standing(2.01, o.p); h.grip[0] = 1; o = g.Step(h, t);
        Check(o.held && o.own[0], "held");
        h.time = 2.02; h.gameplay = false; o = g.Step(h, t);
        Check(!o.held && o.own[0] && o.event == fm::Event::Released, "a wheel opens while held: the map lets go, the grip stays swallowed");
        h.time = 2.03; h.gameplay = true; o = g.Step(h, t);
        Check(!o.held && o.own[0], "back in the world, still squeezed: never a fresh press, never a new grab");
        h.time = 2.04; h.aimTracked = false; o = g.Step(h, t);
        Check(o.own[0], "tracking lost while squeezed: still swallowed");
        h.time = 2.05; h.grip[0] = 0; o = g.Step(h, t);
        Check(!o.own[0], "let go: the game's again");
    }
    {   // a two-hand size is kept by the copy after letting go
        fm::Engine e;
        auto o = Out(e, t);
        fm::Input in = Standing(1.01, o.p); in.grip[0] = 1; e.Step(in, t);
        in.time = 1.02; in.grip[1] = 1; e.Step(in, t);
        in.time = 1.03; in.hand[0].x = .4f; in.hand[1].x = -.4f; o = e.Step(in, t);
        in.time = 1.04; in.grip[0] = in.grip[1] = 0; o = e.Step(in, t);
        Check(Near(o.size, .70f), "kept after letting go");
        o = e.Step(Standing(1.05), t);
        Check(Near(o.size, .70f), "and the next frames");
    }
    {   // it stays in the room: turning the head moves nothing
        fm::Engine e;
        const auto rest = Out(e, t);
        fm::Input in = Standing(1.01); in.headQ = math::Yaw(90 * fm::Deg);
        const auto o = e.Step(in, t);
        Check(Near(Len(o.p - rest.p), 0, 1e-5f) && QNear(o.q, rest.q), "a 90 deg head turn: it stays where it was let go");
    }
    {   // look to show: shown within the angle, hidden past it plus 5; the capture starts 15 deg sooner
        fm::Tuning l = t; l.lookDeg = 20;
        fm::Engine e;
        auto o = Out(e, l, Ahead(30));   // it sits 30 deg below the view
        Check(!o.shown && o.want && Near(o.lookDeg, 30, .1f), "30 deg off: hidden, the capture warm (within 35)");
        fm::Input in = Standing(1.01); in.headQ = framevr::panels::Pitch(-15 * fm::Deg);   // look 15 deg down: 15 deg off
        o = e.Step(in, l);
        Check(o.shown, "15 deg off: shown");
        in.time = 1.02; in.headQ = framevr::panels::Pitch(-8 * fm::Deg);   // 22 deg off: within 20 + 5
        o = e.Step(in, l);
        Check(o.shown, "22 deg off: still shown (no flicker at the edge)");
        in.time = 1.03; in.headQ = framevr::panels::Pitch(40 * fm::Deg);   // 70 deg off
        o = e.Step(in, l);
        Check(!o.shown && !o.want, "70 deg off: hidden, nothing captured");
    }
    {   // a load screen inside the same game (a waypoint, an Act change: a gap, the same session) lets go of a grab and keeps
        // the copy where it is
        fm::Engine e;
        auto o = Out(e, t);
        fm::Input in = Standing(1.01, o.p); in.grip[0] = 1; e.Step(in, t);
        in.time = 1.02; in.hand[0].x += .3f; in.aim.x += .3f; o = e.Step(in, t);
        Check(o.held, "held");
        in.time = 5; o = e.Step(in, t);
        Check(!o.held && !o.own[0], "after a gap (a load screen): not held, the grip passes");
        const auto kept = o;
        o = e.Step(Standing(5.01, {0, 0, -10}), t);
        Check(o.event == fm::Event::None && e.Placed() && Near(Len(o.p - kept.p), 0, 1e-4f) && o.shown, "the same game after the load: the copy stays where it was, shown");
    }
    {   // Save and Exit, then a game (a new session): the copy goes back to the wrist, nothing remembered (Jeff 2026-11-07:
        // "have it not save the map location on save and exit, should default back to the wrist")
        const fm::Tuning s;   // shipped: 20 cm
        const math::Vec wristP{-.2f, 1.2f, -.35f};
        const auto wristQ = fm::FaceEyes(wristP, {0, 1.6f, 0});
        fm::Engine e;
        auto o = Out(e, s, Ahead(10));
        // grab it, move it, resize it with both hands to 40 cm, let go
        fm::Input in = Standing(1.01, o.p); in.leftFree = true; in.aimLeft = in.hand[1]; in.aimLeftTracked = true; in.aimLeftQ = PointFrom(in.hand[1], o.p);
        e.Step(in, s);
        in.time = 1.03; in.grip[0] = in.grip[1] = 1; o = e.Step(in, s);
        Check(o.held && o.resizing, "both hands have it");
        in.time = 1.05; in.hand[0].x += .2f; in.hand[1].x -= .2f; o = e.Step(in, s);
        in.time = 1.07; in.grip[0] = in.grip[1] = 0; o = e.Step(in, s);
        Check(e.Placed() && o.size > .3f, "resized past 30 cm and let go");
        // Save and Exit: the menus (no player), then a new game
        fm::Input menu = Standing(2); menu.session = 0; menu.gameplay = false; e.Step(menu, s);
        fm::Input next = Standing(9); next.session = 8;
        o = e.Step(next, s);
        Check(o.event == fm::Event::PutAwayNewGame && !e.Placed() && !o.shown && !o.want && !o.held && o.pulse < 0 && !o.own[0] && !o.own[1] && !o.ownStick,
              "a new game: put away, nothing drawn or captured, no buzz, no grip or stick held");
        o = e.Step(Standing(9.02, {0, 0, -10}), s);
        Check(!o.shown && !o.want && !e.Placed() && o.event == fm::Event::None, "and it stays gone");
        // the next pull starts at the wrist, at the shipped 20 cm
        fm::Input pull = Standing(9.5, wristP); pull.session = 8;
        pull.wristShown = true; pull.wristP = wristP; pull.wristQ = wristQ; pull.wristSize = .24f;
        Aim(pull, wristP + math::Rotate(wristQ, {0, 0, .5f}), wristP);
        e.Step(pull, s);
        pull.time = 9.52; pull.grip[0] = 1; o = e.Step(pull, s);
        Check(o.event == fm::Event::Pulled && Near(Len(o.p - wristP), 0, 1e-3f) && Near(o.size, .20f), "the next pull: at the wrist map, 20 cm (no two-hand size kept)");
        // held while the game changes: let go and put away too
        fm::Input again = Standing(9.55); again.session = 9; again.grip[0] = 1;   // no gap: the session changes mid-hold
        o = e.Step(again, s);
        Check(o.event == fm::Event::PutAwayNewGame && !o.held && !e.Placed() && o.own[0], "held at a new game: put away; the grip still squeezed stays swallowed until let go");
        again.time = 9.57; again.grip[0] = 0; o = e.Step(again, s);
        Check(!o.own[0], "let go: the game's again");
        // nothing out: a new game is nothing
        fm::Engine h; h.Step(Standing(1), s);
        fm::Input g = Standing(6); g.session = 8;
        o = h.Step(g, s);
        Check(o.event == fm::Event::None && !o.shown && !h.Placed(), "nothing out: a new game is nothing");
    }
    // ---- Map grab (2026-10-29 headset: "rotate, move, pitch the map in any 3D axis") ----
    {   // the turn read back for the log: yaw, pitch, roll as Yaw * Pitch * Roll; flat ones
        const auto q = math::Multiply(math::Multiply(math::Yaw(40 * fm::Deg), framevr::panels::Pitch(-25 * fm::Deg)), Roll(70 * fm::Deg));
        const auto a = fm::AnglesOf(q);
        Check(Near(a.yaw, 40, .05f) && Near(a.pitch, -25, .05f) && Near(a.roll, 70, .05f), "angles: yaw, pitch, roll read back");
        const auto table = fm::AnglesOf(math::Multiply(math::Yaw(30 * fm::Deg), framevr::panels::Pitch(-90 * fm::Deg)));
        Check(Near(table.pitch, -90, .1f) && Near(table.yaw, 30, .5f) && Near(table.roll, 0), "flat like a table: pitch -90, yaw from its top edge");
        const auto ceiling = fm::AnglesOf(framevr::panels::Pitch(90 * fm::Deg));
        Check(Near(ceiling.pitch, 90, .1f), "overhead facing down: pitch +90");
    }
    {   // a grab at an angle keeps the map's pose in the hand's frame: no jump, then position and turn follow 1:1
        fm::Engine e;
        auto o = Out(e, t);
        fm::Input in = Standing(1.01, o.p); in.grip[0] = 1;
        const fm::Pose before{o.p, o.q};
        o = e.Step(in, t);
        Check(o.held && Near(Len(o.p - before.p), 0, 1e-4f) && QNear(o.q, before.q), "grabbed at an angle: it does not move at the grab");
        const fm::Pose aim0{in.aim, in.aimQ};
        const auto rel0 = fm::Relative(aim0, {o.p, o.q});
        // move the hand 30 cm up and left, turn it 50 deg left and pitch it 35 deg up
        in.time = 1.02; in.aim = in.aim + math::Vec{-.3f, .3f, .05f};
        in.aimQ = math::Multiply(math::Multiply(math::Yaw(50 * fm::Deg), framevr::panels::Pitch(35 * fm::Deg)), in.aimQ);
        o = e.Step(in, t);
        const auto rel1 = fm::Relative({in.aim, in.aimQ}, {o.p, o.q});
        Check(o.held && Near(Len(rel1.p - rel0.p), 0, 1e-3f) && QNear(rel1.q, rel0.q), "the offset in the hand's frame is kept (position and turn)");
        // pitch the hand far up (past panels::MaxTilt's 60 deg): the map follows, no clamp
        in.time = 1.03; in.aimQ = math::Multiply(framevr::panels::Pitch(80 * fm::Deg), in.aimQ);
        o = e.Step(in, t);
        const auto rel2 = fm::Relative({in.aim, in.aimQ}, {o.p, o.q});
        Check(Near(Len(rel2.p - rel0.p), 0, 1e-3f) && QNear(rel2.q, rel0.q), "past 60 deg of tilt: still 1:1");
        in.time = 1.04; in.grip[0] = 0;
        const auto held = o;
        o = e.Step(in, t);
        Check(!o.held && o.event == fm::Event::Released && Near(Len(o.p - held.p), 0, 1e-4f) && QNear(o.q, held.q), "let go: it stays exactly there, turn and all");
        o = e.Step(Standing(1.05), t);
        Check(Near(Len(o.p - held.p), 0, 1e-4f) && QNear(o.q, held.q), "and the next frames (the room keeps it)");
    }
    {   // roll follows the wrist: a turn about the laser turns the map about the laser, by the same angle
        fm::Engine e;
        auto o = Out(e, t);
        fm::Input in = Standing(1.01, o.p); in.grip[0] = 1;
        o = e.Step(in, t);
        const auto back = math::Rotate(in.aimQ, {0, 0, 1});   // the controller's own +Z: Roll turns about it
        const auto up0 = math::Rotate(o.q, {0, 1, 0}), c0 = o.p;
        in.time = 1.02; in.aimQ = math::Multiply(in.aimQ, Roll(90 * fm::Deg));   // the wrist rolls 90 deg
        o = e.Step(in, t);
        const auto turn = AxisAngle(back, 90 * fm::Deg);
        const auto want = math::Rotate(turn, up0), up1 = math::Rotate(o.q, {0, 1, 0});
        const auto wantC = in.aim + math::Rotate(turn, c0 - in.aim);
        Check(o.held && Near(Len(want - up1), 0, 2e-3f) && Near(Len(wantC - o.p), 0, 2e-3f), "a 90 deg wrist roll turns the map 90 deg about the laser (VR Settings' grab never rolls)");
        // face it to the eyes and roll the wrist: its own roll follows
        fm::Engine r;
        fm::Tuning ahead = t;
        o = Out(r, ahead, Ahead(0), 1.6);
        fm::Input eye = Standing(2.01); eye.aim = {0, 1.6f, -.2f}; eye.aimQ = {0, 0, 0, 1}; eye.grip[0] = 1;   // pointing straight through it
        o = r.Step(eye, ahead);
        eye.time = 2.02; eye.aimQ = Roll(-45 * fm::Deg);
        o = r.Step(eye, ahead);
        Check(o.held && Near(fm::AnglesOf(o.q).roll, -45, .1f) && Near(fm::AnglesOf(o.q).pitch, 0, .1f), "pointed along its normal: a 45 deg wrist roll is its roll");
    }
    {   // the laser on its back face grabs too (overhead or rolled, its back may face you)
        fm::Engine e;
        auto o = Out(e, t);
        const auto normal = math::Rotate(o.q, {0, 0, 1});
        fm::Input in = Standing(1.01); in.grip[0] = 1;
        in.aim = o.p - normal * .4f;   // behind it, pointing back at its center
        const auto d = math::Normalized(o.p - in.aim);
        in.aimQ = math::Multiply(math::Yaw(std::atan2(-d.x, -d.z)), framevr::panels::Pitch(std::asin(d.y)));
        o = e.Step(in, t);
        Check(o.held, "pointed at its back: grabbed");
    }
    {   // push / pull with the right stick along the laser, 0.25 to 3 m; the stick is the map's while held and until at rest
        Check(Near(fm::Pushed(2.9f, .5f), fm::MaxReach) && Near(fm::Pushed(.3f, -.5f), fm::MinReach), "push stops at 3 m, pull at 0.25 m");
        Check(Near(fm::Pushed(.1f, -.1f), .1f) && Near(fm::Pushed(.1f, .05f), .15f) && Near(fm::Pushed(4, .1f), 4) && Near(fm::Pushed(4, -.5f), 3.5f),
              "a reach already outside is never jumped back, only moved toward the range");
        Check(fm::StickPush(.15f, .02f) == 0 && fm::StickPush(1, .02f) > 0 && fm::StickPush(-1, .02f) < 0, "a dead zone; forward pushes, back pulls");
        fm::Engine e;
        auto o = Out(e, t);
        fm::Input in = Standing(1.01, o.p); in.grip[0] = 1;
        o = e.Step(in, t);
        const float start = o.reach;
        const auto laser = math::Rotate(in.aimQ, {0, 0, -1});
        const auto at0 = o.p;
        Check(o.held && start > .2f && start < 1 && o.ownStick, "held: the reach measured, the stick is the map's");
        in.stickY = 1;
        for (int i = 0; i < 40; ++i) {in.time += .1; o = e.Step(in, t);}
        Check(Near(o.reach, fm::MaxReach) && o.pushes == 1 && o.ownStick, "a long push: 3 m at most, one push counted");
        Check(Near(Len((o.p - at0) - laser * (fm::MaxReach - start)), 0, 2e-3f), "it moved along the laser by the reach gained");
        in.stickY = 0; in.time += .1; e.Step(in, t);
        in.stickY = -1;
        for (int i = 0; i < 40; ++i) {in.time += .1; o = e.Step(in, t);}
        Check(Near(o.reach, fm::MinReach) && o.pushes == 2, "a long pull: 0.25 m at least, a second push counted");
        in.time += .1; in.grip[0] = 0;
        o = e.Step(in, t);
        Check(o.event == fm::Event::Released && Near(o.reach, fm::MinReach) && Near(o.reachStart, start) && o.pushes == 2 && o.ownStick,
              "let go: the summary (start, end, pushes); the stick still pulled stays the map's");
        in.time += .1; in.stickY = 0;
        o = e.Step(in, t);
        Check(!o.ownStick, "the stick at rest: the game's again");
        in.time += .1; in.stickY = 1; o = e.Step(in, t);
        Check(!o.ownStick, "a later push, not holding: the game's (turn, run toggle, zoom)");
    }
    {   // Face me on let go: On turns it upright toward the eyes on release; Off keeps the hand's turn
        fm::Tuning f = t; f.faceOnRelease = true;
        for (int face = 0; face < 2; ++face) {
            const fm::Tuning& u = face ? f : t;
            fm::Engine e;
            auto o = Out(e, u);
            fm::Input in = Standing(1.01, o.p); in.grip[0] = 1; e.Step(in, u);
            in.time = 1.02; in.aimQ = math::Multiply(in.aimQ, Roll(60 * fm::Deg)); in.aim.x -= .2f;
            const auto held = e.Step(in, u);
            in.time = 1.03; in.grip[0] = 0; o = e.Step(in, u);
            if (face) {
                const auto n = math::Rotate(o.q, {0, 0, 1}), toEyes = math::Normalized(in.head - o.p);
                Check(Near(Len(o.p - held.p), 0, 1e-4f) && math::Dot(n, toEyes) > .999f && Near(fm::AnglesOf(o.q).roll, 0, .1f),
                      "Face me On: same place, its face to the eyes, no roll");
            } else Check(QNear(o.q, held.q), "Face me Off: the hand's turn kept");
        }
    }
    {   // a grab never starts outside the world (a panel, VR Settings, a wheel: gameplay false), nor
        // while Look to show has it hidden; looked at, it grabs again
        fm::Engine e;
        auto o = Out(e, t);
        fm::Input in = Standing(1.01, o.p); in.grip[0] = 1; in.gameplay = false;
        o = e.Step(in, t);
        Check(!o.held && !o.own[0], "out of the world: no grab");
        fm::Tuning l = t; l.lookDeg = 15;
        fm::Engine h;
        o = Out(h, l, Ahead(30), 1.6);
        const auto center = o.p;
        fm::Input away = Standing(2.01, center); away.grip[0] = 1;   // looking ahead: 30 deg off, hidden
        o = h.Step(away, l);
        Check(!o.shown && !o.held, "hidden by Look to show: a press on it does not grab");
        away.time = 2.02; away.grip[0] = 0; h.Step(away, l);
        fm::Input look = Standing(2.03, center); look.headQ = framevr::panels::Pitch(-28 * fm::Deg);
        o = h.Step(look, l);
        Check(o.shown, "looked at: shown");
        look.time = 2.04; look.grip[0] = 1;
        o = h.Step(look, l);
        Check(o.held && o.event == fm::Event::Grabbed, "and grabbed again");
    }
    {   // Aim buzz (2026-10-31): once when the laser comes onto it with Right Grip up; not on jitter, not while a grip is
        // held for something else, not just after a let go; Off: nothing tested, the grab the same
        fm::Engine e;
        auto o = Out(e, t);
        o = e.Step(Standing(1), t);   // pointing far ahead, not at it
        const auto center = o.p;
        Check(!o.aimed && !o.aimEdge, "pointing away: no buzz");
        o = e.Step(Standing(1.01, center), t);
        Check(o.aimed && o.aimEdge, "the laser onto it: a buzz");
        o = e.Step(Standing(1.02, center), t);
        Check(o.aimed && !o.aimEdge, "still on it: only once");
        e.Step(Standing(1.03), t); o = e.Step(Standing(1.1, center), t);
        Check(o.aimed && !o.aimEdge, "off for 70 ms and back (jitter at its edge): no second buzz");
        e.Step(Standing(1.2), t); o = e.Step(Standing(1.6, center), t);
        Check(o.aimEdge, "off for 0.4 s, back on: a buzz again");
        fm::Input in = Standing(1.61, center); in.grip[0] = 1; o = e.Step(in, t);
        Check(o.held && !o.aimEdge && o.pulse == 0, "the grab: its own buzz, not the aim's");
        in.time = 1.62; in.grip[0] = 0; o = e.Step(in, t);
        in.time = 1.63; o = e.Step(in, t);
        Check(!o.held && o.aimed && !o.aimEdge, "just let go, the laser still on it: no aim buzz");
        fm::Engine h; o = Out(h, t);
        fm::Input sweep = Standing(1.01); sweep.grip[0] = 1; h.Step(sweep, t);
        sweep = Standing(1.4, o.p); sweep.grip[0] = 1; o = h.Step(sweep, t);
        Check(!o.aimEdge && !o.held, "Right Grip held for something else, swept onto it: no buzz, no grab");
        fm::Tuning quiet = t; quiet.aimBuzz = false;
        fm::Engine q; o = Out(q, quiet);
        o = q.Step(Standing(1.01, o.p), quiet);
        Check(!o.aimEdge && o.aimed, "Aim buzz Off: no buzz (the laser is still measured, for the stick's zoom)");
        fm::Input p = Standing(1.02, o.p); p.grip[0] = 1; o = q.Step(p, quiet);
        Check(o.held, "and the grab works the same");
    }
    {   // anywhere, any turn, any distance (Jeff 2026-10-31): flat overhead, upside down, far out, close in, and grabbed again
        fm::Tuning ahead = t;
        fm::Engine e;
        auto o = Out(e, ahead, Ahead(0));
        // pointing straight through it (the laser along its normal): roll the wrist 180 in two grabs of 90, as a hand can
        fm::Input in = Standing(1.01); in.aim = {0, 1.6f, -.2f}; in.aimQ = {0, 0, 0, 1}; in.grip[0] = 1; o = e.Step(in, ahead);
        in.time = 1.02; in.aimQ = Roll(90 * fm::Deg); o = e.Step(in, ahead);
        in.time = 1.03; in.grip[0] = 0; o = e.Step(in, ahead);
        in.time = 1.04; in.aimQ = {0, 0, 0, 1}; o = e.Step(in, ahead);   // the wrist turns back, not holding
        in.time = 1.05; in.grip[0] = 1; o = e.Step(in, ahead);
        Check(o.held, "grabbed again after the first roll");
        in.time = 1.06; in.aimQ = Roll(90 * fm::Deg); o = e.Step(in, ahead);
        Check(Near(std::fabs(fm::AnglesOf(o.q).roll), 180, .5f), "two 90 deg rolls: upside down");
        in.stickY = 1;
        for (int i = 0; i < 30; ++i) {in.time += .1; o = e.Step(in, ahead);}
        const float far = Len(o.p - in.head);
        in.time += .1; in.stickY = 0; in.grip[0] = 0; o = e.Step(in, ahead);
        Check(o.event == fm::Event::Released && far > 3.f && Near(std::fabs(fm::AnglesOf(o.q).roll), 180, .5f), "pushed out past 3 m and let go upside down: it stays so");
        const auto kept = o;
        o = e.Step(Standing(in.time + .01), ahead);
        Check(Near(Len(o.p - kept.p), 0, 1e-4f) && QNear(o.q, kept.q), "and stays the next frame");
        fm::Input back = Standing(in.time + .02, o.p); back.grip[0] = 1; o = e.Step(back, ahead);
        Check(o.held && o.reach > 2.f, "grabbed again out there (aim and Right Grip)");
        back.stickY = -1;
        for (int i = 0; i < 40; ++i) {back.time += .1; o = e.Step(back, ahead);}
        Check(Near(o.reach, fm::MinReach) && Len(o.p - back.aim) < .5f, "and pulled right in");
    }
    // ---- Pull from the wrist (2026-11-01: "the wrist map stays ... grab one copy of the map ... drag the floating map
    // back close to the wrist, the floating map goes away") ----
    {
        fm::Tuning w = t;
        const math::Vec wristP{-.2f, 1.2f, -.35f};
        const auto wristQ = fm::FaceEyes(wristP, {0, 1.6f, 0});
        auto atWrist = [&](double time, math::Vec aimAt) {
            fm::Input in = Standing(time, aimAt);
            in.wristShown = true; in.wristP = wristP; in.wristQ = wristQ; in.wristSize = .24f;
            return in;
        };
        fm::Engine e;
        auto o = e.Step(atWrist(1, {0, 0, -10}), w);
        Check(o.event == fm::Event::None && !o.shown && !o.want && !e.Placed(), "nothing floats until pulled (no home)");
        fm::Input in = atWrist(1.01, wristP); in.grip[0] = 1; in.wristShown = false;
        o = e.Step(in, w);
        Check(!o.held && !o.shown && !o.own[0], "the wrist map not shown: a press there pulls nothing");
        in.time = 1.02; in.grip[0] = 0; e.Step(in, w);
        in = atWrist(1.03, wristP); in.grip[0] = 1;
        o = e.Step(in, w);
        Check(o.event == fm::Event::Pulled && o.held && o.own[0] && o.shown && o.want && o.pulse == 0 && Near(Len(o.p - wristP), 0, 1e-3f) && Near(o.size, .35f),
              "Right Grip with the laser on the wrist map: a copy pulled out there, held, the grip ours, a buzz, the Size row's size");
        Check(o.atWrist && !o.awayEdge, "just pulled: at the wrist, no put-away buzz");
        in.time = 1.04; in.aim = in.aim + math::Vec{.1f, .3f, -.3f};
        o = e.Step(in, w);
        Check(o.held && !o.atWrist && Len(o.p - wristP) > .3f, "drawn out of the wrist's reach");
        in.time = 1.05; in.grip[0] = 0;
        o = e.Step(in, w);
        Check(o.event == fm::Event::Released && o.shown && e.Placed(), "let go out there: it stays, shown");
        const auto kept = o.p;
        o = e.Step(atWrist(1.06, {0, 0, -10}), w);
        Check(o.shown && Near(Len(o.p - kept), 0, 1e-4f), "and the next frames");
        fm::Input re = atWrist(1.07, kept); re.grip[0] = 1;
        o = e.Step(re, w);
        Check(o.event == fm::Event::Grabbed && o.held, "grabbed again where it floats (not a new pull)");
        re.time = 1.08; re.aim = re.aim + (wristP - kept);
        o = e.Step(re, w);
        Check(o.held && o.atWrist && o.awayEdge, "brought back to the wrist: a light buzz says a let go puts it away");
        re.time = 1.09;
        o = e.Step(re, w);
        Check(o.atWrist && !o.awayEdge, "the buzz once");
        re.time = 1.10; re.grip[0] = 0;
        o = e.Step(re, w);
        Check(o.event == fm::Event::PutAway && !o.shown && !o.want && !o.held && !e.Placed(), "let go at the wrist: put away, gone");
        o = e.Step(atWrist(1.11, kept), w);
        Check(!o.shown && !o.aimed, "and stays gone (nothing to aim at)");
        // the hand on the wrist map pulls too (pointing anywhere)
        fm::Input touch = atWrist(1.2, {0, 0, -10}); touch.hand[0] = wristP + math::Vec{.05f, 0, 0}; touch.aim = touch.hand[0]; touch.grip[0] = 1;
        o = e.Step(touch, w);
        Check(o.event == fm::Event::Pulled && o.held, "the right hand on the wrist map: pulled");
        // a let go at the wrist with Put away Off keeps it there; a wheel opening there never puts it away
        touch.time = 1.21; touch.grip[0] = 0;
        fm::Tuning keep = w; keep.awayM = 0;
        o = e.Step(touch, keep);
        Check(o.event == fm::Event::Released && o.shown && e.Placed(), "Put away Off: a let go at the wrist leaves it there");
        fm::Engine g;
        fm::Input p = atWrist(2, wristP); p.grip[0] = 1; g.Step(p, w);
        p.time = 2.01; p.gameplay = false;
        o = g.Step(p, w);
        Check(o.event == fm::Event::Released && e.Placed() && g.Placed(), "a wheel opening while held at the wrist: let go, not put away");
        // one copy at a time: a pull with a copy out moves it to the wrist
        fm::Engine one;
        fm::Input a = atWrist(3, wristP); a.grip[0] = 1; one.Step(a, w);
        a.time = 3.01; a.aim = a.aim + math::Vec{0, .4f, -.4f}; one.Step(a, w);
        a.time = 3.02; a.grip[0] = 0; o = one.Step(a, w);
        const auto out1 = o.p;
        fm::Input b = atWrist(3.03, wristP); b.grip[0] = 1;
        o = one.Step(b, w);
        Check(o.event == fm::Event::Pulled && Near(Len(o.p - wristP), 0, 1e-3f) && Len(out1 - wristP) > .3f, "pulled again: the one copy comes to the wrist");
    }
    // ---- The whole map at every size (2026-11-03, Jeff: "it should consider the whole map when deciding what is grabable")
    {   // the new measure is exactly the old laser test where it says "on it", and its angle is the real nearest angle
        unsigned seed = 12345; int compared = 0, onIt = 0;
        const auto rnd = [&](float lo, float hi) { seed = seed * 1103515245u + 12345u; return lo + (hi - lo) * float((seed >> 8) & 0xffff) / 65535.f; };
        bool same = true, nearest = true;
        for (int i = 0; i < 30000; ++i) {
            const float size = rnd(.1f, 1.5f);
            const fm::Pose at{{rnd(-1, 1), rnd(.5f, 2), rnd(-2, 0)}, fm::Unit({rnd(-1, 1), rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)})};
            const math::Vec from{rnd(-1, 1), rnd(.5f, 2), rnd(-1, 1)};
            const auto q = PointFrom(from, at.p + math::Rotate(at.q, {rnd(-1.2f, 1.2f) * size, rnd(-1.2f, 1.2f) * size, 0}));
            const auto dir = math::Rotate(q, {0, 0, -1});
            const auto m = fm::AimAt(from, q, at, size);
            const auto o = math::Rotate(math::Conjugate(at.q), from - at.p), d = math::Rotate(math::Conjugate(at.q), dir);
            const float h = -o.z / d.z, px = std::fabs(o.x + d.x * h) - size * .5f, py = std::fabs(o.y + d.y * h) - size * .5f;
            if (std::fabs(px) > 1e-3f && std::fabs(py) > 1e-3f) {   // never a ray on the very edge: rounding, either way
                ++compared; if (m.miss == 0) ++onIt;
                same = same && (m.miss == 0) == TodayHit(from, dir, at, size);
            }
            if (i >= 6000) continue;
            float best = 180;
            for (int k = 0; k < 400; ++k) {   // walk the edge
                const float f = (k % 100) / 100.f * size - size * .5f, half = size * .5f;
                const math::Vec corner[4] = {{f, half, 0}, {f, -half, 0}, {half, f, 0}, {-half, f, 0}};
                best = std::min(best, fm::AngleTo(dir, math::Normalized(at.p + math::Rotate(at.q, corner[k / 100]) - from)));
            }
            const float stepDeg = size / 100 / std::max(m.distance, .02f) / fm::Deg;   // the walk's own step, seen from the hand
            if (m.miss > 0) nearest = nearest && m.miss <= best + .02f && best - m.miss < std::max(.6f, stepDeg);
        }
        Check(same && compared > 20000 && onIt > 3000 && onIt < compared - 3000, "AimAt's miss 0 is exactly the old laser test, either face, over random rays and sizes");
        Check(nearest, "and off it, the angle to the nearest point of the edge (against a walk along the edge)");
    }
    {   // every corner, every edge's middle and the center, sizes 10 to 150 cm, the turns Jeff's log shows, from close, from
        // arm's length and from behind: what is drawn (the engine's out.p, q, size go to the quad unchanged) is what takes
        // the press, with no jump at the grab. Point to grab Off and On alike.
        const math::Vec center{-.1f, 1.35f, -.55f}, head{0, 1.6f, 0};
        const math::Quat turns[] = {fm::FaceEyes(center, head),
            math::Multiply(math::Yaw(105 * fm::Deg), framevr::panels::Pitch(-19 * fm::Deg)),                    // his let go at 13:15:31
            math::Multiply(math::Multiply(math::Yaw(63 * fm::Deg), framevr::panels::Pitch(-23 * fm::Deg)), Roll(45 * fm::Deg)),
            math::Multiply(math::Yaw(30 * fm::Deg), framevr::panels::Pitch(-90 * fm::Deg))};                    // flat like a table
        int presses = 0; bool taken = true, noJump = true, letGo = true, drawnSize = true;
        for (float tolerance : {0.f, 15.f}) for (int cm = 10; cm <= 150; cm += 5) for (const auto& turn : turns) {
            fm::Tuning w = Plain(); w.awayM = 0; w.aimBuzz = false; w.aimDeg = tolerance; w.size = cm * .01f;
            fm::Engine e; double time = 1;
            auto rest = PlaceCopy(e, w, {center, turn}, time);
            const float half = rest.size * .5f - .001f;
            drawnSize = drawnSize && Near(rest.size, cm * .01f) && rest.shown;
            const math::Vec points[9] = {{0, 0, 0}, {-half, -half, 0}, {half, -half, 0}, {half, half, 0}, {-half, half, 0}, {half, 0, 0}, {-half, 0, 0}, {0, half, 0}, {0, -half, 0}};
            const math::Vec hands[3] = {{.25f, -.3f, .35f}, {.1f, -.2f, .7f}, {-.2f, .1f, -.4f}};   // close by, arm's length, behind it
            for (const auto& point : points) for (const auto& hand : hands) {
                const auto target = rest.p + math::Rotate(rest.q, point);
                fm::Input in = Standing(time);
                Aim(in, rest.p + math::Rotate(rest.q, hand), target);
                in.grip[0] = 1;
                const auto o = e.Step(in, w);
                ++presses;
                taken = taken && o.held && o.event == fm::Event::Grabbed && o.own[0];
                noJump = noJump && Len(o.p - rest.p) < 1e-3f && QNear(o.q, rest.q, 1e-4f);
                in.time = time += .02; in.grip[0] = 0;
                const auto r = e.Step(in, w);
                letGo = letGo && r.event == fm::Event::Released && Len(r.p - rest.p) < 2e-3f;
                time += .02;
            }
        }
        Check(presses == 2 * 29 * 4 * 9 * 3, "every press counted");
        Check(drawnSize, "the size drawn is the Size row's, 10 to 150 cm");
        Check(taken, "the center, all four corners and every edge's middle grab, at every size, from close, arm's length and behind");
        Check(noJump, "and it does not move at the grab");
        Check(letGo, "and stays when let go");
    }
    {   // the grab area scales with the map (his log: 40 cm grabbed at 0.23-0.57 m along the laser, 20 cm at 0.06-0.27 m): just
        // inside the edge takes it and just outside does not, at every size, with Point to grab Off; On, the band is the same
        // part of the map at every size and distance (Jeff 2026-11-07: "the grabable corners ... change with scale")
        bool inside = true, outside = true, band = true, far = true, measured = true; int capped = 0, shared = 0;
        const math::Vec center{0, 1.4f, -.8f};
        const auto face = fm::FaceEyes(center, {0, 1.6f, 0});
        const auto normal = math::Rotate(face, {0, 0, 1}), side = math::Rotate(face, {1, 0, 0}), upward = math::Rotate(face, {0, 1, 0});
        for (int cm = 10; cm <= 150; cm += 5) for (const float d : {.25f, .6f, 1.2f}) {
            const float s = cm * .01f, half = s * .5f;
            const auto hand = center + normal * d;
            for (int dirs = 0; dirs < 4; ++dirs) {
                const auto out = dirs == 0 ? side : dirs == 1 ? side * -1 : dirs == 2 ? upward : upward * -1;
                const auto press = [&](float past, float tolerance, fm::Output* rest = nullptr) {   // past: m beyond the edge
                    fm::Tuning w = Plain(); w.awayM = 0; w.aimBuzz = false; w.aimDeg = tolerance; w.size = s;
                    fm::Engine e; double time = 1;
                    const auto r = PlaceCopy(e, w, {center, face}, time);
                    if (rest) *rest = r;
                    fm::Input in = Standing(time); Aim(in, hand, center + out * (half + past)); in.grip[0] = 1;
                    return e.Step(in, w);
                };
                inside = inside && press(-.002f, 0).held;
                outside = outside && !press(.01f, 0).held && !press(.01f, 0).own[0];
                // On: PointMargin of its side past the edge, whatever its size and the hand's distance, but never more than
                // 15 deg past it seen from the hand (a large copy takes no presses aimed only near it)
                const auto at = [&](float deg) { return d * std::tan(std::atan(half / d) + deg * fm::Deg) - half; };
                const float band0 = std::min(fm::PointMargin * s, at(15));
                if (fm::PointMargin * s > at(15)) ++capped; else ++shared;
                band = band && press(band0 * .97f, 15).held && !press(band0 * 1.03f, 15).held;
                far = far && !press(2 * band0, 15).own[0];
                const auto m = fm::AimAt(hand, PointFrom(hand, center + out * (half + at(12))), {center, face}, s);
                measured = measured && Near(m.miss, 12, .3f);
            }
        }
        Check(inside, "just inside every edge takes it, at every size and distance");
        Check(outside, "just outside does not, Point to grab Off: the area IS the drawn map");
        Check(band && capped > 50 && shared > 50, "Point to grab On: 0.65 of its side past an edge or 15 deg, whichever is less, takes it; just past does not; 10 to 150 cm, 25 cm to 1.2 m");
        Check(far, "twice that band past: the grip stays the game's");
        Check(measured, "the angle measured past the edge is the angle aimed");
    }
    // ---- Point to grab (2026-11-03: "it should grab just from pointing at the direction of the wrist map")
    {
        const math::Vec wristP{-.25f, 1.2f, -.35f}, head{0, 1.6f, 0};
        const auto wristQ = fm::FaceEyes(wristP, head);
        const auto normal = math::Rotate(wristQ, {0, 0, 1}), side = math::Rotate(wristQ, {1, 0, 0});
        fm::Tuning off = Plain(); off.aimBuzz = false;
        fm::Tuning on = off; on.aimDeg = 15;
        const auto scene = [&](double time, math::Vec hand, math::Vec aimAt) {
            fm::Input in = Standing(time);
            in.wristShown = true; in.wristP = wristP; in.wristQ = wristQ; in.wristSize = .24f;
            Aim(in, hand, aimAt);
            return in;
        };
        const auto hand = wristP + normal * .5f;   // arm's length from the wrist's map, far from a touch
        const auto past = [&](float deg) { return wristP + side * (.5f * std::tan(std::atan(.12f / .5f) + deg * fm::Deg)); };
        {   // from arm's length, roughly at it
            fm::Engine e; e.Step(scene(1, hand, past(10)), on);
            fm::Input in = scene(1.02, hand, past(10)); in.grip[0] = 1;
            auto o = e.Step(in, on);
            Check(o.event == fm::Event::Pulled && o.held && o.own[0] && Near(Len(o.p - wristP), 0, 1e-3f) && o.pulse == 0, "On: 10 deg off the wrist map from arm's length: pulled, at the wrist map, the grip ours, a buzz");
            fm::Engine g; g.Step(scene(1, hand, past(10)), off);
            in.time = 1.02; o = g.Step(in, off);
            Check(!o.held && !o.own[0] && o.event == fm::Event::None, "Off: the same press takes nothing (exact only, as before)");
            fm::Engine h; h.Step(scene(1, hand, past(20)), on);
            in = scene(1.02, hand, past(20)); in.grip[0] = 1; o = h.Step(in, on);
            Check(!o.held && !o.own[0] && o.pressed && o.pressWrist.known && Near(o.pressWrist.miss, 20, .5f) && o.refused == fm::Refused::Away, "On: 20 deg off: the game's, and the log has how far it was");
            fm::Input away = scene(1.04, hand, hand + normal); away.grip[0] = 0; h.Step(away, on);
            away.time = 1.06; away.grip[0] = 1; o = h.Step(away, on);
            Check(!o.held && !o.own[0], "pointed the other way: the game's (skills, potions, holsters)");
        }
        {   // the closer of the two; both exactly on, the one nearer the hand
            fm::Engine e; double time = 1;
            const fm::Pose beside{wristP + side * .5f, wristQ};
            fm::Tuning w = on; w.size = .24f; w.awayM = 0;
            PlaceCopy(e, w, beside, time);
            const auto between = wristP + side * .25f;   // from the hand: just past the wrist map's edge, nearer it than the copy's
            auto in = scene(time, hand, wristP + side * .13f); e.Step(in, w);
            in.time = time += .02; in.grip[0] = 1;
            auto o = e.Step(in, w);
            Check(o.event == fm::Event::Pulled && Near(Len(o.p - wristP), 0, 1e-3f), "just past the wrist map, the copy farther off: the wrist map (the copy comes)");
            in.time = time += .02; in.grip[0] = 0; e.Step(in, w);
            fm::Engine c; time = 1; PlaceCopy(c, w, beside, time);
            in = scene(time, hand, beside.p - side * .13f); c.Step(in, w);
            in.time = time += .02; in.grip[0] = 1; o = c.Step(in, w);
            Check(o.event == fm::Event::Grabbed && Near(Len(o.p - beside.p), 0, 1e-3f), "just past the copy, the wrist map farther off: the copy");
            (void)between;
            // one behind the other, the ray on both: the one nearer the hand
            fm::Engine b; time = 1; const fm::Pose behind{wristP - normal * .4f, wristQ};
            PlaceCopy(b, w, behind, time);
            in = scene(time, hand, wristP); b.Step(in, w);
            in.time = time += .02; in.grip[0] = 1; o = b.Step(in, w);
            Check(o.event == fm::Event::Pulled, "the wrist map in front of the copy: the wrist map");
            fm::Engine f; time = 1; const fm::Pose front{wristP + normal * .2f, wristQ};
            PlaceCopy(f, w, front, time);
            in = scene(time, hand, wristP); f.Step(in, w);
            in.time = time += .02; in.grip[0] = 1; o = f.Step(in, w);
            Check(o.event == fm::Event::Grabbed && Near(Len(o.p - front.p), 0, 1e-3f), "the copy in front of the wrist map: the copy");
            fm::Engine g; time = 1; PlaceCopy(g, off, front, time);
            in = scene(time, hand, wristP); g.Step(in, off);
            in.time = time += .02; in.grip[0] = 1; o = g.Step(in, off);
            Check(o.event == fm::Event::Grabbed, "Off: on both, the floating one first, as before");
        }
        {   // the wrist's fixed target no longer steals a press: the hand beside the wrist, aimed elsewhere
            const auto touch = wristP + side * .05f + normal * .03f;
            const fm::Pose out{wristP + math::Vec{.6f, .4f, -.3f}, wristQ};
            fm::Engine e; double time = 1; fm::Tuning w = on; w.awayM = 0;
            PlaceCopy(e, w, out, time);
            const auto elsewhere = touch + normal - side;   // well away from the copy too
            auto in = scene(time, touch, elsewhere); e.Step(in, w);
            in.time = time += .02; in.grip[0] = 1;
            auto o = e.Step(in, w);
            Check(!o.held && !o.own[0] && Len(o.p - out.p) < 1e-3f, "On, a copy out: a press beside the wrist aimed elsewhere leaves the copy where it is, the grip the game's");
            fm::Engine g; time = 1; fm::Tuning k = off; k.awayM = 0; PlaceCopy(g, k, out, time);
            in = scene(time, touch, elsewhere); g.Step(in, k);
            in.time = time += .02; in.grip[0] = 1; o = g.Step(in, k);
            Check(o.event == fm::Event::Pulled && Len(o.p - wristP) < 1e-3f, "Off: it pulls the copy to the wrist, as before (the 24 cm target at the left hand, whatever the copy's size)");
            fm::Engine n;
            in = scene(1, touch, elsewhere); n.Step(in, on);
            in.time = 1.02; in.grip[0] = 1; o = n.Step(in, on);
            Check(o.event == fm::Event::Pulled, "On, nothing out: the hand on the wrist map still pulls");
        }
        {   // what must stay the game's: a hand at a bottle or holster (only exactly on it), a busy hand, a hidden map
            fm::Engine e; e.Step(scene(1, hand, past(10)), on);
            fm::Input in = scene(1.02, hand, past(10)); in.grip[0] = 1; in.nearGrab = true;
            auto o = e.Step(in, on);
            Check(!o.held && !o.own[0] && o.refused == fm::Refused::AtBottle, "at a bottle or holster: within the band is not enough");
            fm::Engine x; x.Step(scene(1, hand, wristP), on);
            in = scene(1.02, hand, wristP); in.grip[0] = 1; in.nearGrab = true; o = x.Step(in, on);
            Check(o.held, "exactly on it still takes it, as before");
            fm::Engine b; b.Step(scene(1, hand, past(10)), on);
            in = scene(1.02, hand, past(10)); in.grip[0] = 1; in.handFree = false; o = b.Step(in, on);
            Check(!o.held && !o.own[0] && o.refused == fm::Refused::Busy, "a hand holding a bottle or drawing a bow: never");
            fm::Tuning look = on; look.lookDeg = 30; look.awayM = 0;
            fm::Engine l; double time = 1;
            auto rest = PlaceCopy(l, look, {head + math::Vec{0, -1.2f, -1}, wristQ}, time);   // far below the view
            Check(!rest.shown, "Look to show: hidden");
            in = Standing(time, rest.p); in.grip[0] = 1; o = l.Step(in, look);
            Check(!o.held && !o.own[0] && o.refused == fm::Refused::Hidden, "hidden by Look to show: nothing to take");
        }
        {   // Aim buzz within the band, on the wrist map too, only while a copy is out; never pulled: none (upstream's wrist)
            fm::Tuning buzz = on; buzz.aimBuzz = true;
            fm::Engine n; auto o = n.Step(scene(1, hand, hand + normal), buzz);
            o = n.Step(scene(1.4, hand, past(10)), buzz);
            Check(!o.aimEdge && o.pulse < 0, "never pulled: 10 deg off the wrist map: no buzz (the wrist map feels as upstream)");
            fm::Tuning keep = buzz; keep.awayM = 0;
            const fm::Pose aside{wristP + math::Vec{.6f, .4f, -.3f}, wristQ};
            fm::Engine e; Out(e, keep, aside);
            o = e.Step(scene(1.4, hand, hand + normal), keep);
            Check(!o.aimed && !o.aimEdge, "a copy out, pointing away: no buzz");
            o = e.Step(scene(1.8, hand, past(10)), keep);
            Check(o.aimed && o.aimEdge, "a copy out: 10 deg off the wrist map: a buzz, Right Grip would take it");
            fm::Tuning quiet = off; quiet.aimBuzz = true; quiet.awayM = 0;
            fm::Engine g; Out(g, quiet, aside);
            g.Step(scene(1.4, hand, hand + normal), quiet);
            o = g.Step(scene(1.8, hand, wristP), quiet);
            Check(!o.aimed && !o.aimEdge, "point to grab Off, a copy out: no buzz on the wrist map, as before");
        }
        {   // a pull let go on the very next frame stays at the wrist (the room frame was stale on the first pull)
            fm::Tuning keep = on; keep.awayM = 0;
            fm::Engine e; e.Step(scene(1, hand, wristP), keep);
            fm::Input in = scene(1.02, hand, wristP); in.grip[0] = 1; e.Step(in, keep);
            in.time = 1.03; in.grip[0] = 0;
            const auto o = e.Step(in, keep);
            Check(o.event == fm::Event::Released && Len(o.p - wristP) < 2e-3f, "a one-frame pull: it stays at the wrist map");
        }
    }
    // ---- The stick zooms the map you point at (2026-11-06, Jeff: "zooming on the map if pointing at the map and using the up
    // and down, thats a deliberate movement"): only with a copy out; the stick is never taken by it (the map zoom has it then)
    {
        const math::Vec wristP{-.25f, 1.2f, -.35f};
        const auto wristQ = fm::FaceEyes(wristP, {0, 1.6f, 0});
        const auto normal = math::Rotate(wristQ, {0, 0, 1});
        fm::Tuning s = Plain(); s.aimDeg = 15; s.awayM = 0;
        const fm::Pose copy{{.3f, 1.5f, -.7f}, fm::FaceEyes({.3f, 1.5f, -.7f}, {0, 1.6f, 0})};
        const auto scene = [&](double time, math::Vec hand, math::Vec aimAt, bool wrist) {
            fm::Input in = Standing(time);
            in.wristShown = wrist; in.wristP = wristP; in.wristQ = wristQ; in.wristSize = .24f;
            Aim(in, hand, aimAt); in.stickY = 1;
            return in;
        };
        const auto hand = wristP + normal * .5f;
        fm::Engine e; Out(e, s, copy);
        auto o = e.Step(scene(1, hand, copy.p, true), s);
        Check(o.pointed && o.pointedMap == 0 && !o.ownStick, "a copy out, pointing at it, stick up: pointed at the floating map; the stick not taken (the zoom's)");
        o = e.Step(scene(1.01, hand, wristP, true), s);
        Check(!o.pointed && o.pointedMap < 0 && !o.ownStick, "pointing at the raised wrist map: not pointed (it zooms by looking, as upstream)");
        o = e.Step(scene(1.02, hand, copy.p, false), s);
        Check(o.pointed && o.pointedMap == 0, "the wrist lowered: the floating map still");
        o = e.Step(scene(1.03, hand, hand + normal, true), s);
        Check(!o.pointed && o.pointedMap < 0, "pointing away: not pointed (the stick upstream's: look-to-zoom, run toggle, turn)");
        {   // 5 deg past the copy's edge: inside point to grab's 15 deg band (a press takes it), but the stick stays the game's
            math::Vec near5{};
            for (float x = .18f; x < 1.f; x += .001f) {
                near5 = copy.p + math::Rotate(copy.q, {x, 0, 0});
                if (fm::AimAt(hand, PointFrom(hand, near5), copy, s.size).miss >= 5) break;
            }
            const float miss = fm::AimAt(hand, PointFrom(hand, near5), copy, s.size).miss;
            fm::Engine b; Out(b, s, copy);
            auto in = scene(1, hand, near5, false); b.Step(in, s);
            in.time = 1.02; o = b.Step(in, s);
            Check(miss >= 5 && miss < 5.5f && !o.pointed && !o.ownStick && o.aimed, "5 deg off the copy: not pointed, the stick not taken (run toggle, Show Items)");
            in.time = 1.04; in.grip[0] = 1; o = b.Step(in, s);
            Check(o.event == fm::Event::Grabbed && o.held, "but a Right Grip there takes it (the grab's band)");
        }
        fm::Input busy = scene(1.04, hand, copy.p, true); busy.handFree = false; o = e.Step(busy, s);
        Check(!o.pointed, "a hand holding a bottle or drawing a bow: not pointed");
        fm::Input g = scene(1.05, hand, hand + normal, true); g.grip[0] = 1; e.Step(g, s);
        g = scene(1.06, hand, copy.p, true); g.grip[0] = 1; o = e.Step(g, s);
        Check(!o.pointed && !o.held, "Right Grip held for something else, swept onto it: not pointed");
        g.time = 1.07; g.grip[0] = 0; e.Step(g, s);
        g = scene(1.08, hand, copy.p, true); g.grip[0] = 1; o = e.Step(g, s);
        Check(o.held && !o.pointed && o.ownStick, "held: not pointed; the stick is the map's (push / pull)");
        fm::Tuning look = s; look.lookDeg = 10;
        fm::Engine h; const auto rest = Out(h, look, Ahead(40));
        Check(!rest.shown, "hidden by Look to show");
        o = h.Step(scene(1, hand, rest.p, false), look);
        Check(!o.pointed, "a hidden copy is not pointed at");
        // put away: nothing out, not pointed again
        fm::Engine p; fm::Tuning away = s; away.awayM = .15f;
        fm::Input in = scene(1, hand, wristP, true); p.Step(in, away);
        in.time = 1.02; in.grip[0] = 1; o = p.Step(in, away);
        Check(o.event == fm::Event::Pulled, "pulled");
        in.time = 1.04; in.grip[0] = 0; o = p.Step(in, away);
        Check(o.event == fm::Event::PutAway && !p.Placed(), "let go at once: put away");
        in.time = 1.06; in.stickY = 0; p.Step(in, away);
        in.time = 1.08; in.stickY = 1; o = p.Step(in, away);
        Check(!o.pointed, "put away: pointing at the wrist map is upstream's again");
    }
    // ---- Two-hand size (2026-11-03: "both grab buttons ... pulling apart to make bigger or squeezing inward to make smaller")
    {
        const math::Vec center{0, 1.4f, -.8f};
        const auto face = fm::FaceEyes(center, {0, 1.6f, 0});
        fm::Tuning w = Plain(); w.awayM = 0; w.aimBuzz = false; w.aimDeg = 15; w.bothGripsS = .25f; w.size = .4f;
        const math::Vec away{5, 5, 5};
        // A frame: where each controller points (`away` for nowhere near it), both tracked and free
        const auto frame = [&](double time, math::Vec rightAt, math::Vec leftAt) {
            fm::Input in = Standing(time, rightAt);
            in.leftFree = true; in.aimLeft = in.hand[1]; in.aimLeftTracked = true; in.aimLeftQ = PointFrom(in.hand[1], leftAt);
            return in;
        };
        const auto ready = [&](fm::Engine& e, const fm::Tuning& u, double& time) { fm::Tuning k = u; const auto o = PlaceCopy(e, k, {center, face}, time); return o; };
        {   // left first, the right pointing at it: no frame of the left reaches the game
            fm::Engine e; double time = 1; const auto rest = ready(e, w, time);
            auto in = frame(time, center, away); e.Step(in, w);
            in.time = time += .02; in.grip[1] = 1;
            auto o = e.Step(in, w);
            Check(o.own[1] && !o.own[0] && !o.held && !o.resizing, "Left Grip first, the right pointing at it: the left held back, nothing taken yet");
            in.time = time += .1; o = e.Step(in, w);
            Check(o.own[1] && !o.held, "still within the window: still held back");
            in.time = time += .02; in.grip[0] = 1; o = e.Step(in, w);
            Check(o.held && o.resizing && o.own[0] && o.own[1] && o.event == fm::Event::ResizeStart && o.pulse == 1 && o.bothFirst == 2 && o.bothGapMs > 100 && o.bothGapMs < 150,
                  "Right Grip within the window: both hands have it, both grips ours, a buzz; the log has left first, by 120 ms");
            Check(Near(Len(o.p - rest.p), 0, 2e-3f) && Near(o.size, .4f), "no jump at the start");
            // pulling apart grows, squeezing shrinks, the map's center stays between the hands, its turn kept
            const float spread0 = Len(in.hand[0] - in.hand[1]);
            const auto middle0 = (in.hand[0] + in.hand[1]) * .5f, offset = o.p - middle0;
            const auto q0 = o.q;
            in.time = time += .02; in.hand[0].x += .2f; in.hand[1].x -= .2f;
            o = e.Step(in, w);
            Check(Near(o.size, .4f * Len(in.hand[0] - in.hand[1]) / spread0, 1e-3f) && o.size > .4f, "apart: bigger, by the spread");
            Check(Near(Len(o.p - rest.p), 0, 2e-3f), "spread evenly: its center does not move");
            in.time = time += .02; in.hand[0].x -= .35f; in.hand[1].x += .35f;
            o = e.Step(in, w);
            Check(o.size < .4f && Near(o.size, .4f * Len(in.hand[0] - in.hand[1]) / spread0, 1e-3f), "squeezed: smaller");
            in.time = time += .02; in.hand[0] = in.hand[0] + math::Vec{.1f, .05f, -.2f}; in.hand[1] = in.hand[1] + math::Vec{.1f, .05f, -.2f};
            in.aimQ = math::Multiply(in.aimQ, Roll(70 * fm::Deg));   // a hand's twist turns nothing while both hold it
            o = e.Step(in, w);
            Check(Len(o.p - ((in.hand[0] + in.hand[1]) * .5f + offset)) < 2e-3f && QNear(o.q, q0, 1e-4f), "both hands moved: it moves with their middle; a twist turns nothing");
            in.time = time += .02; in.hand[0].x += 5; in.hand[1].x -= 5; o = e.Step(in, w);
            Check(Near(o.size, fm::MaxSize), "never past the largest");
            in.time = time += .02; in.hand[0].x -= 5; in.hand[1].x += 5; in.hand[0].x = in.hand[1].x + .001f; o = e.Step(in, w);
            Check(Near(o.size, fm::MinSize), "never under the smallest");
            // the left let go: the right holds it alone from where it is, 1:1 again
            in.hand[0] = {.3f, 1.2f, -.3f}; in.hand[1] = {-.3f, 1.2f, -.3f};
            in.time = time += .02; o = e.Step(in, w);
            const auto last = o;
            in.time = time += .02; in.grip[1] = 0; o = e.Step(in, w);
            Check(o.held && !o.resizing && !o.own[1] && o.event == fm::Event::ResizeEnd && Len(o.p - last.p) < 2e-3f, "left let go: resize done, no jump, the right still holds, the left the game's again");
            in.time = time += .02; in.aim.x += .1f; in.hand[0].x += .1f; o = e.Step(in, w);
            Check(Near(Len(o.p - last.p), .1f, 2e-3f), "and it follows the right hand 1:1");
            in.time = time += .02; in.grip[0] = 0; o = e.Step(in, w);
            Check(o.event == fm::Event::Released && !o.own[0], "let go");
        }
        {   // either hand pointing: only the left at it, left first
            fm::Engine e; double time = 1; ready(e, w, time);
            auto in = frame(time, away, center); e.Step(in, w);
            in.time = time += .02; in.grip[1] = 1; auto o = e.Step(in, w);
            Check(o.own[1] && !o.held, "Left Grip first, only the left pointing at it: held back");
            in.time = time += .05; in.grip[0] = 1; o = e.Step(in, w);
            Check(o.held && o.resizing && o.own[0] && o.own[1] && o.bothFirst == 2, "Right Grip, pointing elsewhere: taken for the left's aim");
        }
        {   // right first, only the left pointing at it: the right held back, then both
            fm::Engine e; double time = 1; const auto rest = ready(e, w, time);
            auto in = frame(time, away, center); e.Step(in, w);
            in.time = time += .02; in.grip[0] = 1; auto o = e.Step(in, w);
            Check(o.own[0] && !o.held && !o.own[1], "Right Grip first, only the left pointing at it: the right held back");
            in.time = time += .1; in.grip[1] = 1; o = e.Step(in, w);
            Check(o.held && o.resizing && o.own[0] && o.own[1] && o.bothFirst == 1 && Len(o.p - rest.p) < 2e-3f, "Left Grip within the window: both hands have it, no jump");
        }
        {   // together, either hand pointing
            fm::Engine e; double time = 1; ready(e, w, time);
            auto in = frame(time, center, away); e.Step(in, w);
            in.time = time += .02; in.grip[0] = in.grip[1] = 1; auto o = e.Step(in, w);
            Check(o.held && o.resizing && o.own[0] && o.own[1] && o.bothFirst == 3, "both on one frame, the right pointing: both hands have it");
            fm::Engine g; time = 1; ready(g, w, time);
            in = frame(time, away, center); g.Step(in, w);
            in.time = time += .02; in.grip[0] = in.grip[1] = 1; o = g.Step(in, w);
            Check(o.held && o.resizing && o.bothFirst == 3, "both on one frame, only the left pointing: both hands have it");
            fm::Engine n; time = 1; ready(n, w, time);
            in = frame(time, away, away); n.Step(in, w);
            in.time = time += .02; in.grip[0] = in.grip[1] = 1; o = n.Step(in, w);
            Check(!o.held && !o.own[0] && !o.own[1], "both on one frame, neither pointing: both the game's");
        }
        {   // the right first and pointing: a grab at once; the left any time later resizes (it holds it), as before
            fm::Engine e; double time = 1; ready(e, w, time);
            auto in = frame(time, center, away); e.Step(in, w);
            in.time = time += .02; in.grip[0] = 1; auto o = e.Step(in, w);
            Check(o.held && !o.resizing && o.event == fm::Event::Grabbed, "Right Grip pointing at it: a grab at once");
            for (int i = 0; i < 10; ++i) {in.time = time += .2; o = e.Step(in, w);}
            in.time = time += .02; in.grip[1] = 1; o = e.Step(in, w);
            Check(o.resizing && o.own[1], "Left Grip seconds later: it holds it, so it resizes");
        }
        {   // no second grip: the held-back grip goes to the game, the window late; never lost while squeezed
            fm::Engine e; double time = 1; ready(e, w, time);
            auto in = frame(time, center, away); e.Step(in, w);
            in.time = time += .02; in.grip[1] = 1; auto o = e.Step(in, w);
            Check(o.own[1], "held back");
            in.time = time += .2; o = e.Step(in, w);
            Check(o.own[1] && o.gaveBack == 0, "0.2 s: still");
            in.time = time += .06; o = e.Step(in, w);
            Check(!o.own[1] && o.gaveBack == 2, "0.26 s, no Right Grip: the game's now");
            in.time = time += .02; o = e.Step(in, w);
            Check(!o.own[1] && o.gaveBack == 0, "and stays the game's");
            in.time = time += .02; in.grip[0] = 1; o = e.Step(in, w);
            Check(o.held && !o.resizing && !o.own[1], "a late Right Grip pointing at it: a plain grab; the left, already the game's, stays the game's");
            fm::Engine r; time = 1; ready(r, w, time);
            in = frame(time, away, center); r.Step(in, w);
            in.time = time += .02; in.grip[0] = 1; o = r.Step(in, w);
            Check(o.own[0], "the right held back");
            in.time = time += .3; o = r.Step(in, w);
            Check(!o.own[0] && o.gaveBack == 1 && !o.held, "no Left Grip: the right is the game's, nothing taken");
            fm::Engine q; time = 1; ready(q, w, time);
            in = frame(time, center, away); q.Step(in, w);
            in.time = time += .02; in.grip[1] = 1; q.Step(in, w);
            in.time = time += .05; in.grip[1] = 0; o = q.Step(in, w);
            Check(!o.own[1] && o.gaveBack == 0, "a quick tap, let go within the window: over, nothing held");
            fm::Engine v; time = 1; ready(v, w, time);
            in = frame(time, center, away); v.Step(in, w);
            in.time = time += .02; in.grip[1] = 1; v.Step(in, w);
            in.time = time += .05; in.gameplay = false; o = v.Step(in, w);
            Check(!o.own[1], "out of the world (a wheel, VR Settings): the wait is over");
        }
        {   // the game keeps what is not aimed at the map: never held back
            fm::Engine e; double time = 1; ready(e, w, time);
            auto in = frame(time, away, away); e.Step(in, w);
            in.time = time += .02; in.grip[1] = 1; auto o = e.Step(in, w);
            Check(!o.own[1] && !o.resizing, "Left Grip with neither hand pointing at it: the game's at once");
            in.time = time += .02; in.grip[1] = 0; e.Step(in, w);
            in = frame(time += .02, center, center); in.nearGrab = true; e.Step(in, w);
            in.time = time += .02; in.grip[1] = 1; in.nearGrab = true; o = e.Step(in, w);
            Check(!o.own[1], "at a bottle or holster: the game's at once");
            in.time = time += .02; in.grip[1] = 0; e.Step(in, w);
            in = frame(time += .02, center, center); in.leftFree = false; e.Step(in, w);
            in.time = time += .02; in.grip[1] = 1; in.leftFree = false; o = e.Step(in, w);
            Check(!o.own[1], "the left hand holding something: the game's at once");
            fm::Engine h; time = 1; fm::Tuning look = w; look.lookDeg = 10;
            const auto rest = ready(h, look, time);
            Check(!rest.shown, "hidden by Look to show");
            in = frame(time, center, center); h.Step(in, look);
            in.time = time += .02; in.grip[1] = 1; o = h.Step(in, look);
            Check(!o.own[1], "a hidden map holds nothing back");
        }
        {   // Off: today's way, exactly: Left Grip only while Right Grip holds it, the map on the right hand
            fm::Tuning today = w; today.bothGripsS = 0;
            fm::Engine e; double time = 1; ready(e, today, time);
            auto in = frame(time, center, center); e.Step(in, today);
            in.time = time += .02; in.grip[1] = 1; auto o = e.Step(in, today);
            Check(!o.own[1], "Off: a left press first is the game's");
            in.time = time += .02; in.grip[0] = 1; o = e.Step(in, today);
            Check(o.held && !o.resizing && !o.own[1], "Off: the right then grabs alone, no resize");
            in.time = time += .02; in.grip[1] = 0; e.Step(in, today);
            in.time = time += .02; in.grip[1] = 1; o = e.Step(in, today);
            Check(o.resizing && o.own[1] && o.bothFirst == 0, "Off: Left Grip while held resizes");
            const auto was = o.p;
            in.time = time += .02; in.aim.x += .2f; in.hand[0].x += .2f; o = e.Step(in, today);
            Check(Near(Len(o.p - was), .2f, 2e-3f), "Off: the map stays on the right hand (1:1)");
        }
        {   // On: the right hand moved alone moves the center half as far (the middle moves); with the map between the hands it is centered
            fm::Engine e; double time = 1; ready(e, w, time);
            auto in = frame(time, center, away); e.Step(in, w);
            in.time = time += .02; in.grip[0] = in.grip[1] = 1; auto o = e.Step(in, w);
            const auto was = o.p;
            in.time = time += .02; in.hand[0].x += .2f; in.aim.x += .2f; o = e.Step(in, w);
            Check(Near(Len(o.p - was), .1f, 2e-3f), "On: the right hand alone moved 20 cm: the map 10 cm");
            // grabbed by its sides: its center is the hands' middle
            fm::Engine s; time = 1; ready(s, w, time);
            const auto sideways = math::Rotate(face, {1, 0, 0});
            in = frame(time, center, center); in.hand[0] = center + sideways * .2f; in.aim = in.hand[0]; in.aimQ = PointFrom(in.aim, center);
            in.hand[1] = center - sideways * .2f; in.aimLeft = in.hand[1]; in.aimLeftQ = PointFrom(in.hand[1], center);
            s.Step(in, w);
            in.time = time += .02; in.grip[0] = in.grip[1] = 1; o = s.Step(in, w);
            Check(Near(Len(o.p - (in.hand[0] + in.hand[1]) * .5f), 0, 2e-3f), "held by its left and right sides: centered between the hands");
            in.time = time += .02; in.hand[0] = in.hand[0] + sideways * .1f + math::Vec{0, .1f, 0}; in.hand[1] = in.hand[1] - sideways * .1f + math::Vec{0, .1f, 0};
            o = s.Step(in, w);
            Check(Near(Len(o.p - (in.hand[0] + in.hand[1]) * .5f), 0, 2e-3f) && Near(o.size, .6f, 2e-3f), "spread and raised: still centered; 40 -> 60 cm with the hands 40 -> 60 cm apart");
        }
        {   // both hands on it never put it away, whatever the left wrist's reach says; the right let go first keeps the left swallowed
            const math::Vec wristP{-.2f, 1.2f, -.35f};
            fm::Tuning near = w; near.awayM = .4f;
            fm::Engine e; double time = 1; ready(e, near, time);
            auto in = frame(time, center, away); in.wristShown = true; in.wristP = wristP; in.wristQ = face; e.Step(in, near);
            in.time = time += .02; in.grip[0] = in.grip[1] = 1; auto o = e.Step(in, near);
            Check(o.resizing && !o.atWrist, "both hands: no put-away reach");
            in.time = time += .02; in.hand[0] = wristP + math::Vec{.1f, 0, 0}; in.hand[1] = wristP - math::Vec{.1f, 0, 0}; o = e.Step(in, near);
            const auto at = o.p;
            in.time = time += .02; in.grip[0] = 0; o = e.Step(in, near);
            Check(o.event == fm::Event::Released && o.shown && e.Placed() && Len(o.p - at) < 2e-3f && o.own[1] && !o.own[0], "the right let go first: it stays, not put away; the left, still squeezed, stays swallowed");
            in.time = time += .02; in.grip[1] = 0; o = e.Step(in, near);
            Check(!o.own[1], "the left let go: the game's again");
        }
        {   // a pull with both grips: from the wrist's map, the left first (nothing out yet)
            const math::Vec wristP{-.2f, 1.2f, -.35f};
            const auto wristQ = fm::FaceEyes(wristP, {0, 1.6f, 0});
            fm::Engine e;
            auto in = frame(1, wristP, away); in.wristShown = true; in.wristP = wristP; in.wristQ = wristQ; in.wristSize = .24f; e.Step(in, w);
            in.time = 1.02; in.grip[1] = 1; auto o = e.Step(in, w);
            Check(o.own[1] && !o.held, "Left Grip first with the right pointing at the wrist map: held back");
            in.time = 1.08; in.grip[0] = 1; o = e.Step(in, w);
            Check(o.event == fm::Event::Pulled && o.held && o.resizing && o.own[1], "Right Grip: pulled, both hands have it");
        }
    }
    // ---- The copy is what the wrist's setting says, wrist in view or not (2026-11-03: "i normally do top full, but it was
    // showing top mini"; his log: the only Map view click is 1:11:24 PM, value 3; before it, Top-down, and the Map row's full ask
    // flipped the one picture between full and corner; after it, no corner share for 37 minutes).
    {
        // The chain as it runs: the copy's want -> the capture's lease (gamestate MapLease) -> the layout lease (MaintainMapLayout,
        // fullmap::g_on from Map view alone) -> the eyes' redirect (fullmap::Active, what the picture holds). The wrist never in
        // view; Look to show hides and shows it as the head turns. Each Map view; the game's layout starting corner or full.
        for (int view = 0; view < 4; ++view) for (int gameLayout : {1, 0}) {
            const bool viewFull = framevr::settings::MapViewFull(float(view));
            fm::Tuning w = Plain(); w.awayM = 0; w.aimBuzz = false; w.lookDeg = 30; w.size = .2f;
            fm::Engine e; double time = 1;
            PlaceCopy(e, w, {{-.1f, 1.45f, -.6f}, fm::FaceEyes({-.1f, 1.45f, -.6f}, {0, 1.6f, 0})}, time);
            framevr::wrist::MapLease lease; framevr::wrist::MapLayoutLease layout;
            int current = gameLayout; bool panel = false, wrong = false, hiddenThenShown = false, everHidden = false; int shownFrames = 0, redirected = 0;
            uint64_t askedAt = 0;
            for (int i = 0; i < 900; ++i) {
                fm::Input in = Standing(time);
                // look at it, then far away for a while (hidden, the capture let go), then back
                const float away = (i % 300) < 150 ? 0.f : 90.f;
                in.headQ = math::Yaw(away * fm::Deg);
                const auto o = e.Step(in, w);
                time += .011;
                const uint64_t now = uint64_t(time * 1000);
                if (o.want) askedAt = now;                                            // transport: gamestate::MenuMap(... || floatMapWant)
                const bool request = askedAt && now >= askedAt && now - askedAt < 400;   // gamestate: mapWanted
                const int action = lease.Step(7, now, request, false, panel);
                const bool capture = lease.Active(now, false);
                const int act = layout.Step(capture, current, viewFull ? 0 : 1);        // MaintainMapLayout: g_on is MapFullOn() alone
                if (act >= 0) current = act;
                if (action >= 0) panel = action != 0;
                if (o.shown && capture) {
                    ++shownFrames;
                    const bool active = framevr::fullmap::Active(viewFull, capture, current, panel);
                    if (active) ++redirected;
                    if (active != viewFull) wrong = true;
                    if (everHidden) hiddenThenShown = true;
                }
                if (!o.shown) everHidden = true;
            }
            Check(shownFrames > 100 && hiddenThenShown, "the copy was shown, hidden by Look to show and shown again, the wrist never in view");
            Check(!wrong, "every frame it shows, its picture is the kind Map view says: full for Full map and Full top-down, corner else");
            Check(viewFull ? redirected == shownFrames : redirected == 0, "the full map's redirect follows Map view alone");
        }
        // the log's watch: a line when what it shows changes and holds; loud when it differs from Map view
        fm::KindWatch k;
        Check(!k.Step(true, true, true, 1) && !k.Step(true, true, true, 1.1), "settling: no line yet");
        Check(k.Step(true, true, true, 1.3) && k.Full() && k.Matches(), "held 0.25 s: one line, full as Map view says");
        Check(!k.Step(true, true, true, 1.4) && !k.Step(false, false, false, 1.5), "unchanged or hidden: no line");
        Check(!k.Step(true, false, true, 2) && !k.Step(true, false, true, 2.1), "a change: settling");
        Check(k.Step(true, false, true, 2.3) && !k.Full() && k.ViewFull() && !k.Matches(), "the corner map with Map view Full, held: a line that says they differ");
        Check(!k.Step(true, true, false, 3) && !k.Step(true, true, false, 3.1) && k.Step(true, true, false, 3.3) && !k.Matches(), "the full map with Map view corner: they differ too");
    }
    // ---- The shipped values end to end, and the review's gaps (floatmap-test-lead, 2026-11-06)
    {
        const fm::Tuning s;   // shipped: 20 cm, put away 25 cm, shown within 30 deg, Face me On, point 15 deg, two hands 250 ms
        const math::Vec head{0, 1.6f, 0}, wristP{-.2f, 1.2f, -.35f};
        const auto wristQ = fm::FaceEyes(wristP, head);
        const auto normal = math::Rotate(wristQ, {0, 0, 1}), side = math::Rotate(wristQ, {1, 0, 0});
        const auto hand = wristP + normal * .5f;
        // A frame: the wrist map raised, the right controller at `from` pointing at `at`, the left free and pointing at `left`,
        // the head looking toward `lookAt` (ahead, or down at the wrist: Look to show hides a copy past 30 deg).
        math::Vec lookAt{0, 1.6f, -10};
        const auto scene = [&](double time, math::Vec from, math::Vec at, math::Vec left) {
            fm::Input in = Standing(time); in.headQ = PointFrom(head, lookAt);
            in.wristShown = true; in.wristP = wristP; in.wristQ = wristQ; in.wristSize = .24f;
            Aim(in, from, at);
            in.leftFree = true; in.aimLeft = in.hand[1]; in.aimLeftTracked = true; in.aimLeftQ = PointFrom(in.hand[1], left);
            return in;
        };
        const math::Vec nowhere{5, 5, 5};
        {   // gap 1: pull, let go out in view (shown, facing you), grab again, 30 cm from the wrist (kept), two-hand size,
            // 20 cm from the wrist (put away)
            fm::Engine e; double time = 1;
            auto in = scene(time, hand, wristP, nowhere); e.Step(in, s);
            in.time = time += .02; in.grip[0] = 1;
            auto o = e.Step(in, s);
            Check(o.event == fm::Event::Pulled && o.held && Near(o.size, .20f) && Near(Len(o.p - wristP), 0, 1e-3f), "shipped: pulled at the wrist map, 20 cm");
            const math::Vec out{0, 1.5f, -.6f};   // in front, about 10 deg below the view: within 30, shown
            const auto delta = out - o.p;
            in.time = time += .02; in.aim = in.aim + delta; in.hand[0] = in.hand[0] + delta; o = e.Step(in, s);
            Check(o.held && !o.atWrist && Near(Len(o.p - out), 0, 2e-3f), "moved out in front, held");
            in.time = time += .02; in.grip[0] = 0; o = e.Step(in, s);
            const auto toEyes = math::Normalized(head - o.p);
            Check(o.event == fm::Event::Released && o.shown && e.Placed() && math::Dot(math::Rotate(o.q, {0, 0, 1}), toEyes) > .999f,
                  "let go out there: kept, shown (within 30 deg), turned to face you (Face me On)");
            // grab again by pointing at it, bring it 30 cm from the wrist map: not in the put-away reach (25 cm)
            const auto from = out + math::Vec{.2f, -.2f, .4f};
            in = scene(time += .4, from, out, nowhere); e.Step(in, s);
            in.time = time += .02; in.grip[0] = 1; o = e.Step(in, s);
            Check(o.event == fm::Event::Grabbed && o.held, "pointed at it: grabbed again");
            const auto at30 = wristP + side * .30f, step30 = at30 - o.p;
            in.time = time += .02; in.aim = in.aim + step30; in.hand[0] = in.hand[0] + step30; o = e.Step(in, s);
            Check(o.held && !o.atWrist && !o.awayEdge, "30 cm from the wrist map: outside the 25 cm reach, no buzz");
            in.time = time += .02; in.grip[0] = 0; o = e.Step(in, s);
            Check(o.event == fm::Event::Released && e.Placed() && !o.atWrist, "let go at 30 cm: kept");
            const auto kept = o;
            lookAt = wristP + side * .25f;   // from here on look down toward the wrist, so the copy beside it shows
            // both grips together, the right pointing at it: two-hand size from 20 cm
            in = scene(time += .4, kept.p + normal * .4f, kept.p, nowhere);
            in.hand[1] = in.hand[0] - side * .3f; in.aimLeft = in.hand[1]; e.Step(in, s);
            in.time = time += .02; in.grip[0] = in.grip[1] = 1; o = e.Step(in, s);
            Check(o.held && o.resizing && o.bothFirst == 3 && o.event == fm::Event::ResizeStart && Near(o.size, .20f), "shipped: both grips at once while pointing: two-hand size from 20 cm");
            in.time = time += .02; in.hand[0] = in.hand[0] + side * .15f; in.hand[1] = in.hand[1] - side * .15f; o = e.Step(in, s);
            Check(Near(o.size, .40f, 2e-3f), "the hands twice as far apart: 40 cm");
            in.time = time += .02; in.grip[0] = in.grip[1] = 0; o = e.Step(in, s);
            Check(e.Placed() && Near(o.size, .40f, 2e-3f), "let go with both: kept at 40 cm, not put away");
            // grab it again, 20 cm from the wrist map: the buzz, a let go puts it away
            in = scene(time += .4, o.p + normal * .4f, o.p, nowhere); e.Step(in, s);
            in.time = time += .02; in.grip[0] = 1; o = e.Step(in, s);
            Check(o.held && o.event == fm::Event::Grabbed, "grabbed again");
            const auto at20 = wristP + side * .20f, step20 = at20 - o.p;
            in.time = time += .02; in.aim = in.aim + step20; in.hand[0] = in.hand[0] + step20; o = e.Step(in, s);
            Check(o.held && o.atWrist && o.awayEdge, "20 cm from the wrist map: inside the 25 cm reach, a light buzz");
            in.time = time += .02; in.grip[0] = 0; o = e.Step(in, s);
            Check(o.event == fm::Event::PutAway && !e.Placed() && !o.shown && !o.want, "let go there: put away");
        }
        {   // gap 2: a second pull with point to grab On: the copy beside the wrist map, the ray within 15 deg of both
            fm::Engine e; double time = 1;
            const fm::Pose beside{wristP + side * .30f, wristQ};   // 30 cm off: outside the 25 cm put-away reach
            lookAt = wristP + side * .25f;   // looking down at them, so the copy shows
            const auto rest = Out(e, s, beside, time);
            const fm::Pose copy{rest.p, rest.q};   // turned to face you on release (Face me)
            auto in = scene(time += .4, hand, wristP + side * .13f, nowhere); e.Step(in, s);
            const auto both = fm::Closest(fm::AimAt(in.aim, in.aimQ, copy, s.size), fm::AimAt(in.aim, in.aimQ, {wristP, wristQ}, .24f), 1e9f);
            Check(fm::AimAt(in.aim, in.aimQ, copy, s.size).miss <= 15 && fm::AimAt(in.aim, in.aimQ, {wristP, wristQ}, .24f).miss <= 15 && both == 1,
                  "the ray within 15 deg of both maps, the wrist map's edge nearer");
            in.time = time += .02; in.grip[0] = 1; auto o = e.Step(in, s);
            Check(o.event == fm::Event::Pulled && o.held && Near(Len(o.p - wristP), 0, 1e-3f), "the wrist map takes it: the one copy comes to the wrist, held");
            in.time = time += .02; in.aim = in.aim + side * .3f; in.hand[0] = in.hand[0] + side * .3f; e.Step(in, s);
            in.time = time += .02; in.grip[0] = 0; o = e.Step(in, s);
            Check(o.event == fm::Event::Released && e.Placed(), "let go beside the wrist again");
            in = scene(time += .4, hand, o.p - side * .11f, nowhere); e.Step(in, s);   // just past the copy's near edge
            in.time = time += .02; in.grip[0] = 1; o = e.Step(in, s);
            Check(o.event == fm::Event::Grabbed && Len(o.p - wristP) > .25f, "nearer the copy's edge: the copy is grabbed where it is (still one copy)");
        }
        {   // gap 3: a copy out but hidden by Look to show: pointing at the raised wrist map takes nothing (it zooms by looking)
            lookAt = {0, 1.6f, -10};
            fm::Engine e; const auto rest = Out(e, s, Ahead(60));   // 60 deg below the view: hidden
            Check(!rest.shown && e.Placed(), "the copy is out but hidden");
            auto in = scene(1, hand, wristP, nowhere); in.stickY = 1;
            auto o = e.Step(in, s);
            Check(!o.pointed && o.pointedMap < 0 && !o.ownStick, "pointing at the wrist map: not pointed, the stick upstream's");
            in = scene(1.02, hand, rest.p, nowhere); in.wristShown = false; o = e.Step(in, s);
            Check(!o.pointed, "pointing at the hidden copy: nothing");
        }
        {   // gap 4: a push / pull still held at the let go is never a zoom: pointed may come back, the stick stays the map's until at rest
            fm::Tuning k = s; k.awayM = 0; k.faceOnRelease = false;
            fm::Engine e; const auto rest = Out(e, k, Ahead(0));
            auto in = scene(1, rest.p + math::Vec{.1f, -.2f, .4f}, rest.p, nowhere); in.wristShown = false; e.Step(in, k);
            in.time = 1.02; in.grip[0] = 1; auto o = e.Step(in, k);
            Check(o.held && o.ownStick, "held: the stick is the map's");
            in.stickY = 1; for (int i = 0; i < 5; ++i) {in.time += .05; o = e.Step(in, k);}
            Check(o.pushes == 1 && o.ownStick, "pushed out along the laser");
            in.time += .02; in.grip[0] = 0; o = e.Step(in, k);
            Check(o.event == fm::Event::Released && !o.pointed && o.ownStick, "let go with the stick still pushed: not pointed that frame, the stick still the map's");
            in.time += .02; o = e.Step(in, k);
            Check(o.pointed && o.pointedMap == 0 && o.ownStick, "next frame: pointed at it, but the stick stays the map's (transport zeroes it before the zoom)");
            in.time += .02; in.stickY = 0; o = e.Step(in, k);
            Check(o.pointed && !o.ownStick, "the stick at rest: the game's again");
            in.time += .02; in.stickY = 1; o = e.Step(in, k);
            Check(o.pointed && !o.ownStick, "a new push while pointing: the zoom's (not taken by the map)");
        }
        {   // gap 5: nothing out, shipped: Left Grip with the right pointing at the raised wrist map is held back 250 ms, then the game's
            fm::Engine e;
            auto in = scene(1, hand, wristP, nowhere); e.Step(in, s);
            in.time = 1.02; in.grip[1] = 1; auto o = e.Step(in, s);
            Check(o.own[1] && !o.own[0] && !o.held && o.event == fm::Event::None, "held back for a Right Grip (the accepted blast radius)");
            in.time = 1.2; o = e.Step(in, s);
            Check(o.own[1] && o.gaveBack == 0, "0.18 s: still held back");
            in.time = 1.28; o = e.Step(in, s);
            Check(!o.own[1] && o.gaveBack == 2 && !e.Placed() && o.event == fm::Event::None && !o.shown, "0.26 s: the game's, nothing pulled");
            in.time = 1.3; o = e.Step(in, s);
            Check(!o.own[1] && o.gaveBack == 0, "and stays the game's");
            fm::Engine a; in = scene(2, hand, hand + normal, nowhere); a.Step(in, s);
            in.time = 2.02; in.grip[1] = 1; o = a.Step(in, s);
            Check(!o.own[1], "nothing out, the right pointing away: Left Grip is the game's at once");
        }
    }
    // ---- Every hit area on the floating map follows its current size (Jeff 2026-11-07: "if the map scales in size, so does
    // the selection area to be considered it being slected to zoom in and out as does the grabable corners")
    {
        const fm::Tuning s;   // shipped
        const math::Vec head{0, 1.6f, 0}, center{0, 1.5f, -.9f};
        const auto face = fm::FaceEyes(center, head);
        // A copy at `center`, resized with both hands to the smallest or the largest size, then let go (Face me keeps it
        // facing the eyes, so `face` stays its turn).
        const auto sized = [&](fm::Engine& e, bool large) {
            fm::Tuning k = s; k.awayM = 0;
            Out(e, k, {center, face});
            fm::Input in = Standing(1, center); in.headQ = PointFrom(head, center);
            in.leftFree = true; in.aimLeft = in.hand[1]; in.aimLeftTracked = true; in.aimLeftQ = PointFrom(in.hand[1], center);
            e.Step(in, k);
            in.time = 1.02; in.grip[0] = in.grip[1] = 1; e.Step(in, k);
            in.time = 1.04; in.hand[0] = in.hand[0] + math::Vec{large ? 5.f : -.199f, 0, 0}; in.hand[1] = in.hand[1] - math::Vec{large ? 5.f : -.199f, 0, 0};
            e.Step(in, k);
            in.time = 1.06; in.grip[0] = in.grip[1] = 0;
            return e.Step(in, k);
        };
        for (const bool large : {false, true}) {
            fm::Engine e;
            const auto rest = sized(e, large);
            const float size = rest.size, half = size * .5f;
            Check(Near(size, large ? fm::MaxSize : fm::MinSize, 1e-3f) && e.Placed(), large ? "resized to the largest, 150 cm" : "resized to the smallest, 10 cm");
            const fm::Pose at{rest.p, rest.q};
            const auto side = math::Rotate(rest.q, {1, 0, 0}), up = math::Rotate(rest.q, {0, 1, 0}), normal = math::Rotate(rest.q, {0, 0, 1});
            const auto hand = rest.p + normal * .6f + side * .1f - up * .2f;
            fm::Input in = Standing(2); in.headQ = PointFrom(head, rest.p); in.stickY = 1;
            e.Step(in, s);   // after the gap: the first frame finds it looked at again
            bool zoomIn = true, zoomOut = true, grabBand = true;
            const math::Vec dirs[4] = {side, side * -1, up, up * -1};
            for (const auto& d : dirs) {
                // the stick's zoom: anywhere on it at this size (just inside each edge), never just outside
                Aim(in, hand, rest.p + d * (half * .98f)); in.time += .02; zoomIn = zoomIn && e.Step(in, s).pointed;
                Aim(in, hand, rest.p + d * (half * 1.02f)); in.time += .02; zoomOut = zoomOut && !e.Step(in, s).pointed;
                // point to grab: within PointMargin of this size's side past the edge
                grabBand = grabBand && fm::InBand(hand, PointFrom(hand, rest.p + d * (half + fm::PointMargin * size * .95f)), at, size, fm::PointMargin) &&
                           !fm::InBand(hand, PointFrom(hand, rest.p + d * (half + fm::PointMargin * size * 1.05f)), at, size, fm::PointMargin);
            }
            // a corner, just inside: zoom and a grab; just outside both edges past the band: the game's
            Aim(in, hand, rest.p + (side + up) * (half * .98f)); in.time += .02;
            Check(e.Step(in, s).pointed, large ? "150 cm: a corner, just inside: the stick zooms it" : "10 cm: a corner, just inside: the stick zooms it");
            Check(zoomIn, large ? "150 cm: the laser just inside every edge zooms it" : "10 cm: the laser just inside every edge zooms it");
            Check(zoomOut, large ? "150 cm: just outside every edge: not pointed" : "10 cm: just outside every edge: not pointed");
            Check(grabBand, large ? "150 cm: the grab band is 0.65 of its side past each edge" : "10 cm: the grab band is 0.65 of its side past each edge");
            in.stickY = 0;
            const float near = std::min(fm::PointMargin * size * .9f, .05f);   // in its band, and within 15 deg from the hand
            if (large) {   // 0.9 m past its edge: inside 0.65 of its 150 cm side (0.975 m), but about 20 deg past it from the hand: the game's (F1)
                Aim(in, hand, rest.p + side * (half + .9f)); in.time += .02; e.Step(in, s);
                in.time += .02; in.grip[0] = 1;
                const float wMiss = fm::AimAt(in.aim, in.aimQ, {rest.p, rest.q}, size).miss;
                const auto w = e.Step(in, s);
                Check(wMiss > 15 && fm::InBand(in.aim, in.aimQ, {rest.p, rest.q}, size, fm::PointMargin) && !w.held && !w.own[0] && w.event == fm::Event::None, "150 cm: a press 0.9 m past its edge (inside the share, past 15 deg): the game's");
                in.time += .02; in.grip[0] = 0; e.Step(in, s);
            }
            Aim(in, hand, rest.p + side * (half + near)); in.time += .02; e.Step(in, s);
            in.time += .02; in.grip[0] = 1;
            const auto g = e.Step(in, s);
            Check(g.held && g.event == fm::Event::Grabbed && Near(g.size, size), large ? "150 cm: a press in its band grabs it" : "10 cm: a press in its band grabs it");
            in.time += .02; in.grip[0] = 0; e.Step(in, s);
            // the left hand's aim for two-hand size: the same band, at this size
            fm::Input two = Standing(in.time + .4); two.headQ = PointFrom(head, rest.p);
            two.leftFree = true; two.aimLeftTracked = true; Aim(two, hand, rest.p + normal * 5.f);   // the right points away
            two.aimLeft = two.hand[1]; two.aimLeftQ = PointFrom(two.hand[1], rest.p - side * (half + near));
            e.Step(two, s);
            two.time += .02; two.grip[1] = 1;
            auto o = e.Step(two, s);
            Check(o.own[1] && !o.held, large ? "150 cm: Left Grip aimed in its band: held back for two hands" : "10 cm: Left Grip aimed in its band: held back for two hands");
            two.time += .05; two.grip[0] = 1; o = e.Step(two, s);
            Check(o.held && o.resizing, large ? "150 cm: and Right Grip: both hands size it" : "10 cm: and Right Grip: both hands size it");
        }
    }
    if (g_fail) { std::printf("floating map: %d failed\n", g_fail); return 1; }
    std::puts("floating map: ok");
    return 0;
}
