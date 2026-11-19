// The summon army forms where you face: the engine (steamvr/army_face.h) and its glue (vr/army_face.cpp) on the commander's
// own coordinator: a backpedal keeps the formation ahead, a strafe, a real turn, a head glance, a forward walk as before.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <vector>
#include "army_face.h"

namespace {
namespace cmd = framevr::commander;
using armyface::Latch;
using armyface::Tuning;
using armyface::Vec2;
using armyface::Why;

int g_checks = 0, g_failed = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
bool Same(Vec2 a, Vec2 b, float e = 1e-3f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e); }
Vec2 Dir(float deg) {   // 0 deg = north (grid -y, the commander's default forward), clockwise toward +x
    const float r = deg * 0.0174532925f;
    return {std::sin(r), -std::cos(r)};
}
const Vec2 North = Dir(0), East = Dir(90), South = Dir(180);

// ---- The engine ----
void TestUpstreamNumbers() {
    Check(Near(armyface::WalkStep, 1.5f), "the walk check: the commander's WalkTriggerDist, 1.5 sub-tiles");
    Check(Near(std::cos(Tuning{}.toleranceDeg * 0.0174532925f), armyface::CommanderToleranceCos, 1e-5f), "Turn tolerance's default is the commander's HysteresisCos (22.5 deg)");
    Check(Tuning{}.backpedalDeg == 60 && Tuning{}.toleranceDeg == 22.5f && Tuning{}.holdMs == 400, "the tuning the mod runs with");
}

void TestArming() {
    Latch l; const Tuning t;
    auto s = l.Update({100, 100}, South, false, {}, 1000, t);
    Check(!s.apply && !l.Armed() && s.why == Why::NoFacing, "no facing yet: nothing applied, the commander's value");
    s = l.Update({100, 100}, South, true, North, 1250, t);
    Check(s.apply && l.Armed() && s.why == Why::First && Same(s.heading, North), "armed on the facing, not the model's forward");
    s = l.Update({100, 100}, South, true, {0, 0}, 1500, t);
    Check(s.apply && s.why == Why::NoFacing && Same(s.heading, North), "armed, the facing lost: the heading held");
}

// The bug: facing north at a monster, walking backward (south) four walk checks; the game turns the model south.
void TestBackpedalKeepsFormationForward() {
    Latch l; const Tuning t;
    cmd::HeadingFilter stock;
    Vec2 hero{100, 100};
    uint64_t now = 1000;
    (void)l.Update(hero, North, true, North, now, t);
    (void)stock.Update(hero, North.x, North.y);
    int holds = 0, started = 0;
    Vec2 stockHeading{};
    for (int i = 0; i < 6; ++i) {
        hero.y += 1.6f; now += 250;
        const auto s = l.Update(hero, South, true, North, now, t);
        stockHeading = stock.Update(hero, South.x, South.y);
        holds += s.why == Why::Backpedal;
        started += s.holdStarted;
        Check(Same(s.heading, North), "backpedal: the army's heading stays where you face");
        Check(Near(s.walkDeg, 180, .5f) && Near(s.stepDeg, 180, .5f), "the walk (the model) and the hero's step both 180 deg from the facing");
    }
    Check(holds == 6 && started == 1, "every walk check a held backpedal, one hold started (one log line)");
    Check(Same(stockHeading, South), "the commander (Off): the same walk swings the formation behind (the model's forward)");
    // A pause, then backpedal again: a new hold
    now += armyface::Latch::PauseMs + 1;
    hero.y += 1.6f;
    Check(l.Update(hero, South, true, North, now, t).holdStarted, "after a pause, the next backpedal is a new hold");
    // Off (180): never a backpedal, the walk followed as before
    Tuning never = t; never.backpedalDeg = 180;
    Latch o; (void)o.Update({0, 0}, North, true, North, 0, never);
    const auto s = o.Update({0, 1.6f}, South, true, North, 250, never);
    Check(s.why == Why::Walk && Same(s.heading, South), "Backpedal angle Off (180): the walk turns the army, as before");
}

void TestSidewaysStrafe() {
    const Tuning t;   // Backpedal angle 60
    Check(t.backpedalDeg == 60.0f, "the default Backpedal angle: 60 deg");
    // Strafing east while facing north: the model turns east (90 deg from the facing): more than 60, held ahead
    Latch l; (void)l.Update({100, 100}, North, true, North, 0, t);
    auto s = l.Update({101.6f, 100}, East, true, North, 250, t);
    Check(s.why == Why::Backpedal && Same(s.heading, North) && Near(s.walkDeg, 90, .5f), "default 60: a sideways strafe holds the army ahead");
    // A 45 deg forward diagonal: within 60, the walk followed as before
    s = l.Update({102.8f, 98.8f}, Dir(45), true, North, 500, t);
    Check(s.why == Why::Walk && Same(s.heading, Dir(45)), "default 60: a 45 deg forward diagonal follows the walk");
    // Backpedal angle 90: a strafe at exactly 90 is not MORE than 90, a walk as before; 100 deg holds
    Tuning ninety = t; ninety.backpedalDeg = 90;
    Latch c; (void)c.Update({100, 100}, North, true, North, 0, ninety);
    s = c.Update({101.6f, 100}, East, true, North, 250, ninety);
    Check(s.why == Why::Walk && Same(s.heading, East), "Backpedal angle 90: a strafe at 90 deg follows the walk as before");
    Latch b; (void)b.Update({100, 100}, North, true, North, 0, ninety);
    s = b.Update({101.5f, 100.3f}, Dir(100), true, North, 250, ninety);
    Check(s.why == Why::Backpedal && Same(s.heading, North), "Backpedal angle 90: a strafe 100 deg from the facing holds");
    // Backpedal angle 0: every walk holds (only turning moves the army)
    Tuning zero = t; zero.backpedalDeg = 0;
    Latch z; (void)z.Update({100, 100}, North, true, North, 0, zero);
    s = z.Update({100, 98.4f}, Dir(10), true, North, 250, zero);
    Check(s.why == Why::Backpedal && Same(s.heading, North), "Backpedal angle 0: even a forward walk holds");
}

void TestRealTurnRelatches() {
    const Tuning t;   // 22.5 deg, 400 ms
    Latch l; (void)l.Update({100, 100}, North, true, North, 1000, t);
    auto s = l.Update({100, 100}, North, true, East, 1100, t);
    Check(s.why == Why::Kept && Same(s.heading, North) && Near(s.turnDeg, 90, .5f), "a 90 deg turn: not yet (the hold)");
    s = l.Update({100, 100}, North, true, East, 1300, t);
    Check(s.why == Why::Kept && Same(s.heading, North), "200 ms in: still the old heading");
    s = l.Update({100, 100}, North, true, East, 1500, t);
    Check(s.why == Why::Turn && Same(s.heading, East), "400 ms past the tolerance: the army turns to the facing");
    s = l.Update({100, 100}, North, true, East, 1750, t);
    Check(s.why == Why::Kept && Same(s.heading, East), "and stays");
    // Turn hold Off (0): at once
    Tuning now0 = t; now0.holdMs = 0;
    Latch a; (void)a.Update({0, 0}, North, true, North, 0, now0);
    s = a.Update({0, 0}, North, true, Dir(30), 250, now0);
    Check(s.why == Why::Turn && Same(s.heading, Dir(30)), "Turn hold Off: a 30 deg turn re-latches at once");
    // Turn tolerance 0: any turn, after the hold
    Tuning any = now0; any.toleranceDeg = 0;
    Latch b; (void)b.Update({0, 0}, North, true, North, 0, any);
    s = b.Update({0, 0}, North, true, Dir(5), 250, any);
    Check(s.why == Why::Turn && Same(s.heading, Dir(5)), "Turn tolerance 0: a 5 deg turn re-latches");
    // Turn tolerance Off (180): turning never moves it
    Tuning never = now0; never.toleranceDeg = 180;
    Latch c; (void)c.Update({0, 0}, North, true, North, 0, never);
    s = c.Update({0, 0}, North, true, South, 250, never);
    Check(s.why == Why::Kept && Same(s.heading, North), "Turn tolerance Off (180): even facing the other way keeps it");
    // Backpedaling and turning at once: the turn rule still runs on the facing
    Latch d; (void)d.Update({100, 100}, North, true, North, 0, now0);
    s = d.Update({100, 101.6f}, South, true, Dir(60), 250, now0);
    Check(s.why == Why::Turn && Same(s.heading, Dir(60)), "a backpedal while turning 60 deg: the walk held, the turn taken");
}

void TestHeadGlanceDoesNotRelatch() {
    const Tuning t;
    Latch l; (void)l.Update({100, 100}, North, true, North, 1000, t);
    uint64_t now = 1000;
    for (int i = 0; i < 40; ++i) {   // 20 deg off for 10 s: under the tolerance
        now += 250;
        Check(Same(l.Update({100, 100}, North, true, Dir(20), now, t).heading, North), "a 20 deg look for 10 s: under the tolerance, the army stays");
    }
    Check(Same(l.Update({100, 100}, North, true, Dir(22.4f), now + 250, t).heading, North), "22.4 deg: still under");
    // A 60 deg glance for 300 ms (shorter than the 400 ms hold), then back: nothing
    now += 500;
    auto s = l.Update({100, 100}, North, true, Dir(60), now, t);
    s = l.Update({100, 100}, North, true, Dir(60), now + 300, t);
    Check(Same(s.heading, North), "a 60 deg glance, 300 ms: shorter than Turn hold, the army stays");
    s = l.Update({100, 100}, North, true, Dir(5), now + 350, t);
    s = l.Update({100, 100}, North, true, Dir(60), now + 450, t);
    s = l.Update({100, 100}, North, true, Dir(60), now + 750, t);
    Check(Same(s.heading, North), "looking back starts the hold over: a second short glance does not add up");
    s = l.Update({100, 100}, North, true, Dir(60), now + 850, t);
    Check(s.why == Why::Turn && Same(s.heading, Dir(60)), "but held past the hold, it is a turn");
}

void TestForwardWalkIsUpstream() {
    // Walking where you face, a glance aside meanwhile: each walk check re-latches to the model's forward, as the commander's filter.
    const Tuning t;
    Latch l; cmd::HeadingFilter stock;
    Vec2 hero{100, 100};
    (void)l.Update(hero, North, true, North, 0, t);
    (void)stock.Update(hero, North.x, North.y);
    const Vec2 model = Dir(10);
    for (int i = 1; i <= 4; ++i) {
        hero = {hero.x + model.x * 1.6f, hero.y + model.y * 1.6f};
        const Vec2 face = i == 2 ? Dir(50) : Dir(5);   // a 40 deg glance meanwhile, within the 60 deg Backpedal angle
        const auto s = l.Update(hero, model, true, face, uint64_t(i) * 250, t);
        const Vec2 u = stock.Update(hero, model.x, model.y);
        Check(s.why == Why::Walk && Same(s.heading, u), "walking toward the facing: the same heading as the commander's walk re-latch");
    }
    // No model forward: the hero's own step is the walk
    Latch n; (void)n.Update({0, 0}, {0, 0}, true, North, 0, t);
    const auto s = n.Update({0, -1.6f}, {0, 0}, true, Dir(20), 250, t);
    Check(s.why == Why::Walk && Same(s.heading, North), "no model forward: the hero's step");
}

// ---- The glue, on the commander's coordinator ----
Vec2 g_face = North; bool g_faceOk = true;
std::vector<std::string> g_lines;
void Say(const char* line) { g_lines.push_back(line); }
size_t Lines(const char* has) { size_t n = 0; for (const auto& l : g_lines) n += l.find(has) != std::string::npos; return n; }

struct Army {
    cmd::SummonArmyCoordinator c;
    std::array<cmd::ArmyUnitSample, 8> units{};
    Army() {
        c.SetStanceAndFormation(cmd::ArmyStance::Balanced, cmd::ArmyFormation::PhalanxArc);
        for (uint32_t i = 0; i < units.size(); ++i) units[i] = {100 + i, 363, 4, 1, {100, 100}};   // skeleton warriors
    }
    // One army tick as StepSummonArmyServer: each of its two blocks starts from the commander's raw forward and, when glue, the glue's
    // call (step false: the corridor check's; step true: Step's), then Step. Returns how far the slots' center sits along
    // `along` from the hero (positive: that side).
    float Tick(Vec2 hero, Vec2 model, uint64_t now, bool glue, Vec2 along) {
        float cx = model.x, cy = model.y;
        if (glue) armyface::Heading(armyface::HeadingFrame{cx, cy, hero.x, hero.y, now, false, c.headingFilter, &Say});
        corridor = {cx, cy};
        float fx = model.x, fy = model.y;
        if (glue) armyface::Heading(armyface::HeadingFrame{fx, fy, hero.x, hero.y, now, true, c.headingFilter, &Say});
        stepped = {fx, fy};
        std::array<cmd::ArmyMoveOrder, cmd::MaxMovesPerTick> moves{};
        std::array<cmd::FormationSlot, cmd::MaxArmyUnits> slots{};
        (void)c.Step(std::span(units.data(), units.size()), hero, fx, fy, now, moves, std::span(slots.data(), units.size()));
        Vec2 mid{};
        for (size_t i = 0; i < units.size(); ++i) { mid.x += slots[i].worldPos.x; mid.y += slots[i].worldPos.y; }
        mid.x /= float(units.size()); mid.y /= float(units.size());
        last = slots;
        return (mid.x - hero.x) * along.x + (mid.y - hero.y) * along.y;
    }
    std::array<cmd::FormationSlot, cmd::MaxArmyUnits> last{};
    Vec2 corridor{}, stepped{};   // what the corridor check and Step were handed
};

void TestHandlerOnKeepsArmyAhead() {
    g_lines.clear();
    armyface::tuning.holdMs = 0;   // the handler test runs on the real clock: no hold to wait out
    g_face = North; g_faceOk = true;
    armyface::Facing(g_faceOk, g_face.x, g_face.y);
    Army ours, up;
    Vec2 hero{100, 100};
    uint64_t now = GetTickCount64();
    Check(ours.Tick(hero, North, now, true, North) > 1 && Lines("Frame army face: armed") == 1,
          "On: the army forms ahead, one armed line");
    (void)up.Tick(hero, North, now, false, North);
    bool ahead = true, behind = true, both = true;
    for (int i = 0; i < 5; ++i) {   // backpedal south, facing north; the game turns the model south
        hero.y += 1.6f;
        armyface::Facing(g_faceOk, g_face.x, g_face.y);
        ahead = ahead && ours.Tick(hero, South, now, true, North) > 1;
        both = both && ours.corridor == ours.stepped && Same(ours.stepped, North);
        behind = behind && up.Tick(hero, South, now, false, North) < -1;
    }
    Check(ahead, "On, backpedaling: every tick the army's slots sit ahead of you (where you face)");
    Check(behind, "the commander, the same walk: the army's slots behind you (the bug)");
    Check(both, "the corridor check and Step are handed the same heading, where you face (the decide call, then Step's)");
    Check(up.corridor == up.stepped && Same(up.stepped, South), "the commander: both handed the model's forward");
    Check(ours.c.headingFilter.latchedForward == North, "the coordinator's own filter carries our heading");
    // A real turn (hold 0): the army turns to face east
    g_face = East;
    armyface::Facing(g_faceOk, g_face.x, g_face.y);
    Check(ours.Tick(hero, South, now, true, East) > 1, "a real turn: the army ahead to the east");
    // No facing (stale or lost): the heading held
    g_faceOk = false;
    armyface::Facing(g_faceOk, g_face.x, g_face.y);
    Check(g_lines.size() == 1, "nothing else is logged: a backpedal and a turn are ordinary play");
    armyface::tuning = armyface::Tuning{};
}
}  // namespace

int main() {
    TestUpstreamNumbers();
    TestArming();
    TestBackpedalKeepsFormationForward();
    TestSidewaysStrafe();
    TestRealTurnRelatches();
    TestHeadGlanceDoesNotRelatch();
    TestForwardWalkIsUpstream();
    TestHandlerOnKeepsArmyAhead();
    std::printf("army-face-test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
