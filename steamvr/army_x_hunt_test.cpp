// Army X menu, the hunt of a marked monster: the engine (steamvr/army_x_hunt.h) and its handler
// (vr/army_x_hunt.cpp) on the commander's own coordinator (steamvr/summon_commander.h), ticked as StepSummonArmyServer does
// (the unstuck watchdog, Step, the seam, the moves sent): who hunts; the leash lifted only for the mark (Step's walks back
// dropped, the gates measure from the mark, the watchdog quiet); the army reaches a far mark that the commander never reaches;
// the end (killed or lost: Regroup and the return; an order; a new mark; Off; a gap); Off as before, tick by tick.
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "army_x_hunt.h"
#include "../vr/army_x_orders.h"

// The handlers' game reads (vr/melee.cpp: not linked here): the test's own server.
uintptr_t g_farGame = 0, g_farHero = 0;
std::map<uint32_t, uintptr_t> g_farUnits;                 // server units by id
namespace melee {
bool LocalServer(uintptr_t& game, uintptr_t& player) { game = g_farGame; player = g_farHero; return game && player; }
uintptr_t ServerUnit(uintptr_t, int, uint32_t id) { const auto it = g_farUnits.find(id); return it == g_farUnits.end() ? 0 : it->second; }
}
// The game's addresses (cleanroom/sigscan, not linked here). TestRunRead points StatsRva at FakeRow: a MonStats2 row in this
// process whose mode mask (+0xF0) the test sets; until then no row is read.
alignas(8) uint8_t g_fakeRow[0x100]{};
uintptr_t FakeRow(uint8_t, int) { return reinterpret_cast<uintptr_t>(g_fakeRow); }
bool g_sigOk = false;
namespace melee {   // vr/melee.cpp's read (not linked here): the unit's stats bank at +0x1BD, the row's mask at +0xF0
uint32_t MonsterModes(uintptr_t unit, uint32_t classId) {
    uint32_t mask = 0;
    if (!unit || *reinterpret_cast<const uint8_t*>(unit + 0x1bd) > 3 || classId > 4095) return 0;
    std::memcpy(&mask, reinterpret_cast<const uint8_t*>(FakeRow(0, int(classId))) + 0xf0, sizeof mask);
    return mask;
}
}
namespace d2rsig {
uintptr_t Addr(uint64_t) { return g_sigOk ? reinterpret_cast<uintptr_t>(&FakeRow) : 0; }
bool Check(uint64_t, const void*, uint32_t) { return g_sigOk; }
}
int g_testLevel = 2;   // the hero's level the handler reads (interactionstate's snapshot): Blood Moor by default
namespace gamestate { uint64_t LocalPlayer() { return 1; } }
namespace interactionstate { int CurrentLevel(uint64_t, uint64_t) { return g_testLevel; } }
bool g_menuOn = false;   // the menu is on a hand button (vr/army_x_menu.cpp's, not linked here: the transport glue)
namespace armyxmenu {
bool On() { return g_menuOn; }
void ArmyTick(framevr::armyseam::ArmyMovesFrame&) {}   // nothing queued, nothing to give; its Defend engine is TestDefend's below
}

namespace {
namespace seam = framevr::armyseam;
namespace cfg = framevr::armyseam::cfg;
namespace cmd = framevr::commander;
using xhunt::End;
using xhunt::Mode;
using xhunt::Vec2;

int g_checks = 0, g_failed = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
float Dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

std::vector<std::string> g_lines;
void Say(const char* line) { g_lines.push_back(line); }
size_t Lines(const char* has) { size_t n = 0; for (const auto& l : g_lines) n += l.find(has) != std::string::npos; return n; }

constexpr uint32_t MarkId = 999;
const Vec2 Hero{100, 100};
const Vec2 FarMark{100, 150};   // 50 sub-tiles from the hero: past every leash, no hostile near the hero

// An army ticked as StepSummonArmyServer: the watchdog (its relocation done at once), Step, then the engine or the
// handler on Step's moves, then each move "sent": the unit walks toward it at `speed` sub-tiles a tick, mode 2, then stands.
struct Sim {
    enum class Via { None, Engine, Handler } via = Via::Engine;
    Mode mode = Mode::All;
    cmd::SummonArmyCoordinator c;
    std::vector<cmd::ArmyUnitSample> units;
    std::vector<Vec2> dest;
    std::vector<bool> going;
    std::vector<bool> blocked;   // units that cannot walk (an unreachable mark: across water, behind a wall)
    xhunt::Defend* defend = nullptr;   // Defend the X menu's spot (TestDefend), before the hunt as ArmyTick
    float defendR = 7.5f;
    xhunt::Defend::Out defendOut;
    std::vector<xhunt::Foe> foes;      // the hostiles its scan sees
    xhunt::Hunt hunt;
    xhunt::Tick last;
    std::array<cmd::ArmyMoveOrder, cmd::MaxMovesPerTick> moves{};
    size_t stepEmitted = 0, emitted = 0;
    uint64_t now = 100000;
    float speed = 2.0f;
    Vec2 hero = Hero;            // the hero (a test may walk it)
    int relocations = 0;
    std::vector<uint8_t> runners;   // Run the whole approach's runners for the engine (empty: Off)
    uint8_t moveMode = 2;           // the mode a unit takes when a move is sent (2 walk; 15: our run made it run)
    int runAgain = 0;               // the engine's runAgain, summed
    uint64_t retryMs = 0;           // Retry at the mark for the engine (0: Off)
    // Fight back when hit (TestHitBack), as HitBackTick: after Defend, before the hunt; hbHits are this tick's (cleared after)
    xhunt::HitBack* hitBack = nullptr;
    bool hbOn = true;
    float hbReach = 25.0f;
    uint64_t hbFor = 4000, hbRetry = 0;
    std::vector<xhunt::HitBack::Hit> hbHits;
    std::vector<xhunt::HitBack::Foe> hbFoes;
    xhunt::HitBack::Out hbOut;
    Sim() {
        c.SetStanceAndFormation(cmd::ArmyStance::Balanced, cmd::ArmyFormation::PhalanxArc);
        uint32_t id = 200;
        for (int i = 0; i < 4; ++i) units.push_back({id++, 363, 4, 1, {100.0f + i, 101}});   // skeleton warriors
        for (int i = 0; i < 2; ++i) units.push_back({id++, 364, 5, 1, {100.0f + i, 99}});    // skeleton mages
        units.push_back({id++, 289, 3, 1, {101, 100}});                                     // a golem
        dest.assign(units.size(), {});
        going.assign(units.size(), false);
        for (int i = 0; i < 40; ++i) Tick();   // formed on the hero
    }
    void Tick(bool walk = true) {
        for (auto& u : units) {   // the watchdog first, as the host (CheckArmyUnstuck, then the relocation)
            Vec2 r{};
            if (c.CheckArmyUnstuck(u.unitId, u.pos, hero, now, r)) { ++relocations; u.pos = r; }
        }
        emitted = stepEmitted = c.Step(std::span(units.data(), units.size()), hero, 0.0f, -1.0f, now, moves);
        if (defend) {   // as ArmyTick: Defend first, then the hunt; the scan handed in: the foes within r of the spot (HostilesAround)
            xhunt::Foe seen[xhunt::DefendScanMax];
            int n = 0;
            for (const auto& f : foes) if (n < xhunt::DefendScanMax && Dist({f.x, f.y}, defend->Spot()) <= defendR) seen[n++] = f;
            defendOut = defend->Step(c, defendR, mode != Mode::Off, now, seen, n);
        }
        if (hitBack) {
            hbOut = hitBack->Step(c, units, now, hbOn, hbReach, hbFor, hbRetry, hbHits, hbFoes, moves, emitted);
            hbHits.clear();
        }
        if (via == Via::Engine) { last = hunt.Step(c, units, hero, now, mode, moves, emitted, runners, retryMs); runAgain += last.runAgain; }
        if (via == Via::Handler)
            armyxhunt::Moves(seam::ArmyMovesFrame{c, units.data(), units.size(), moves.data(), moves.size(), emitted, hero.x, hero.y, now, &Say});
        for (size_t m = 0; m < emitted; ++m)
            for (size_t i = 0; i < units.size(); ++i)
                if (units[i].unitId == moves[m].unitId) { dest[i] = {float(moves[m].targetX), float(moves[m].targetY)}; going[i] = true; units[i].animMode = moveMode; }
        for (size_t i = 0; walk && i < units.size(); ++i) {
            if (!going[i] || (i < blocked.size() && blocked[i])) continue;   // blocked: its moves go out, it never gets anywhere
            const float d = Dist(units[i].pos, dest[i]);
            if (d <= speed) { units[i].pos = dest[i]; going[i] = false; units[i].animMode = 1; }
            else { units[i].pos.x += (dest[i].x - units[i].pos.x) / d * speed; units[i].pos.y += (dest[i].y - units[i].pos.y) / d * speed; }
        }
        now += 250;
    }
    bool Ranged(size_t i) const { return units[i].petType == 5; }
    bool InMoves(uint32_t id) const { for (size_t m = 0; m < emitted; ++m) if (moves[m].unitId == id) return true; return false; }
    int Near(Vec2 at, float r, bool melee) const {
        int n = 0;
        for (size_t i = 0; i < units.size(); ++i) n += (!melee || !Ranged(i)) && Dist(units[i].pos, at) <= r;
        return n;
    }
};

void TestWhoHunts() {
    Check(xhunt::ModeOf(0) == Mode::Off && xhunt::ModeOf(1) == Mode::Melee && xhunt::ModeOf(2) == Mode::All, "the hunt rule's values: off, melee only, melee and ranged");
    Check(xhunt::ModeOf(cfg::Rules{}.hunt) == Mode::All,
          "On (Melee and ranged) by default, Off 0 under any master");
    const auto melee = cmd::SummonRole::FrontlineMelee, ranged = cmd::SummonRole::RangedArtillery;
    Check(xhunt::Hunts(Mode::Melee, 4, melee) && xhunt::Hunts(Mode::Melee, 3, cmd::SummonRole::TacticalAnchor) &&
          xhunt::Hunts(Mode::Melee, 6, cmd::SummonRole::HeavyFlanker) && !xhunt::Hunts(Mode::Melee, 5, ranged), "Melee only: skeletons, golems, revives; not mages");
    Check(xhunt::Hunts(Mode::All, 5, ranged) && xhunt::Hunts(Mode::All, 4, melee), "Melee and ranged: mages too");
    Check(!xhunt::Hunts(Mode::Off, 4, melee) && !xhunt::Hunts(Mode::Off, 5, ranged), "Off: nobody");
    Check(!xhunt::Hunts(Mode::All, 7, melee) && !xhunt::Hunts(Mode::All, 21, melee) && !xhunt::Hunts(Mode::All, 0, melee),
          "a pet type outside the commander's army mask (an unknown class's) never hunts");
    // Where a hunter goes
    const Vec2 g = xhunt::Goal({100, 100}, {100, 104}, false);
    Check(Dist(g, {100, 102.5f}) < 1e-3f, "melee: beside the mark, on its side");
    const Vec2 r = xhunt::Goal({100, 100}, {100, 112}, true);
    Check(Dist(r, {100, 102}) < 1e-3f, "ranged: RangedKeep from the mark");
    const Vec2 leg = xhunt::Goal({100, 100}, {100, 160}, false);
    Check(Dist(leg, {100, 100}) <= xhunt::StepMax + 1e-3f && leg.y > 100, "far: one StepMax leg toward it");
}

void TestUpstreamIgnoresFarMark() {
    Sim up; up.via = Sim::Via::None;
    up.c.SetPriorityTarget(MarkId, FarMark);
    for (int i = 0; i < 80; ++i) up.Tick();
    Check(up.Near(FarMark, 10, false) == 0, "the commander: 20 s with a far mark, no summon comes within 10 sub-tiles of it");
    Check(!up.c.ShouldAllowSummonCombatTarget(up.units[0].unitId, up.units[0].pos, FarMark), "the commander (Balanced): the attack gate refuses a monster that far from the spot");
}

void TestHuntReachesFarMark() {
    Sim s;
    s.c.SetPriorityTarget(MarkId, FarMark);
    s.Tick();
    Check(s.last.started && s.last.mark == MarkId && s.last.hunters == 7 && s.last.melee == 5 && s.last.ranged == 2 && s.last.kept == 0,
          "a mark: a hunt starts, all seven sent (five melee, two mages)");
    Check(s.last.sent == 7 && s.last.heroDist > 49 && s.last.nearest > 45, "every hunter's move goes out at once; the distances");
    bool legs = true;
    for (size_t m = 0; m < s.emitted; ++m)
        for (size_t i = 0; i < s.units.size(); ++i)
            if (s.units[i].unitId == s.moves[m].unitId) legs = legs && Dist(s.dest[i], FarMark) < Dist(s.units[i].pos, FarMark);
    Check(legs, "each move heads toward the mark");
    // The leash lifted for the mark only
    bool lifted = true, gates = true;
    for (size_t i = 0; i < s.c.trackedCount; ++i) {
        lifted = lifted && s.c.tracked[i].slotPos.x == FarMark.x && s.c.tracked[i].slotPos.y == FarMark.y && s.c.tracked[i].farSinceMs == 0 && s.c.tracked[i].stuckSinceMs == 0;
        gates = gates && s.c.ShouldAllowSummonCombatTarget(s.units[i].unitId, s.units[i].pos, FarMark);
    }
    Check(lifted, "every hunter's spot is the mark until the next Step, its watchdog timers zero");
    Check(gates, "the commander's own attack gate now allows the mark (Balanced, far from the formation)");
    // Walk there
    int dropped = 0;
    uint64_t arrivedAt = 0;
    for (int i = 0; i < 60; ++i) {
        s.Tick();
        dropped += s.last.dropped;
        if (!arrivedAt && s.last.arrived == s.last.hunters) arrivedAt = s.now;
    }
    Check(s.Near(FarMark, xhunt::MeleeArrive + .01f, true) == 5, "every melee summon reaches the far mark");
    Check(s.Near(FarMark, xhunt::RangedArrive + .01f, false) == 7, "the mages stand inside their firing window too");
    bool magesOff = true;
    for (size_t i = 0; i < s.units.size(); ++i) if (s.Ranged(i)) magesOff = magesOff && Dist(s.units[i].pos, FarMark) >= xhunt::RangedKeep - 2.1f;
    Check(magesOff, "the mages keep their stand-off (not in melee)");
    Check(arrivedAt != 0 && dropped > 0, "all arrived; Step's walks back were dropped on the way");
    Check(s.relocations == 0, "the watchdog never moved a hunter back");
    // Arrived: no more moves (the game's AI swings)
    s.Tick();
    Check(s.last.sent == 0 && s.emitted == 0, "arrived and standing: no moves at all");
    // A swing in progress is never re-sent, even when the mark steps away
    for (auto& u : s.units) u.animMode = 4;   // attacking
    s.c.priorityTargetPos = {FarMark.x + 6, FarMark.y};
    s.Tick(false);
    Check(s.last.sent == 0, "the mark moved, the hunters mid-swing: no move cuts the swing");
    for (auto& u : s.units) u.animMode = 1;
    s.Tick(false);
    Check(s.last.sent > 0, "standing again with the mark farther: re-sent");
    s.c.priorityTargetPos = FarMark;
    for (int i = 0; i < 10; ++i) s.Tick();
}

void TestDeathRegroupsAndReturns() {
    Sim s;
    s.c.SetPriorityTarget(MarkId, FarMark);
    for (int i = 0; i < 50; ++i) s.Tick();
    // companion_ai's liveness check lets it go (it died)
    s.c.ClearPriorityTarget();
    s.Tick();
    Check(s.last.end == End::Gone && s.last.endedMark == MarkId && s.last.regrouped && s.last.endedReached == 7 && s.last.endedHunters == 7,
          "the mark gone with no order: the hunt ends (all seven had reached it), the coordinator's Regroup given");
    Check(s.c.orderStampPending && !s.c.anchorActive && s.c.priorityTargetId == 0, "Regroup: a new order's re-forming (the next Step stamps it)");
    s.Tick();
    Check(s.last.dropped == 0 && s.emitted > 0 && s.c.orderedAtMs != 0, "after it: Step's walks back go out untouched");
    int ticks = 0;
    while (!s.last.returned && ticks < 60) { s.Tick(); ++ticks; }
    Check(s.last.returned && !s.last.returnTimedOut && s.last.returnFormed == 7 && s.last.returnOf == 7 && s.last.returnMark == MarkId,
          "the return: all seven back on their spots");
    bool home = true;
    for (size_t i = 0; i < s.c.trackedCount; ++i) home = home && Dist(s.units[i].pos, s.c.tracked[i].slotPos) <= cmd::SummonArmyCoordinator::ArrivalTolerance && Dist(s.units[i].pos, Hero) < 20;
    Check(home, "the army is back in formation at the hero, each on its spot");
    Check(s.relocations == 0, "walked back, never moved by the watchdog");
}

void TestOrderAndNewMarkEnd() {
    Sim s;
    s.c.SetPriorityTarget(MarkId, FarMark);
    for (int i = 0; i < 8; ++i) s.Tick();
    s.c.SetPriorityTarget(MarkId + 1, {130, 100});
    s.Tick();
    Check(s.last.end == End::NewMark && s.last.endedMark == MarkId && s.last.started && s.last.mark == MarkId + 1 && !s.last.regrouped,
          "a new mark: the old hunt ends, the new one starts in the same tick, no Regroup");
    for (int i = 0; i < 4; ++i) s.Tick();
    Check(s.c.ApplyCommanderSkill(cmd::SkillWedgeAgg, Hero), "an order (Spearhead wedge)");
    s.Tick();
    Check(s.last.end == End::NewOrder && s.last.endedMark == MarkId + 1 && !s.last.regrouped && s.hunt.Returning(),
          "an order cleared it: ended as an order (the commander re-forms), no Regroup of ours; the return watched");
    Check(s.last.dropped == 0, "the order's walks to the new spots go out");
    // Hold ground (anchored) clears the mark too: an order, never a Regroup over the anchor
    Sim h;
    h.c.SetPriorityTarget(MarkId, FarMark);
    for (int i = 0; i < 4; ++i) h.Tick();
    h.c.ApplyCommanderSkill(cmd::SkillHoldGround, {110, 110});
    h.Tick();
    Check(h.last.end == End::NewOrder && !h.last.regrouped && h.c.anchorActive, "Hold ground: an order, its anchor kept");
}

void TestMeleeOnlyKeepsMagesUpstream() {
    Sim s; Sim up; up.via = Sim::Via::None;
    s.mode = Mode::Melee;
    s.c.SetPriorityTarget(MarkId, FarMark);
    up.c.SetPriorityTarget(MarkId, FarMark);
    s.Tick(); up.Tick();
    Check(s.last.hunters == 5 && s.last.kept == 2 && s.last.ranged == 0, "Melee only: five hunt, the two mages kept");
    bool magesUp = true;
    for (size_t i = 0; i < s.units.size(); ++i) {
        if (!s.Ranged(i)) continue;
        magesUp = magesUp && s.c.tracked[i].slotPos == up.c.tracked[i].slotPos && s.c.tracked[i].stuckSinceMs == up.c.tracked[i].stuckSinceMs &&
                  s.c.tracked[i].farSinceMs == up.c.tracked[i].farSinceMs;
    }
    Check(magesUp, "a kept mage's spot and watchdog timers are the commander's");
    for (int i = 0; i < 50; ++i) s.Tick();
    bool magesHome = true;
    for (size_t i = 0; i < s.units.size(); ++i) if (s.Ranged(i)) magesHome = magesHome && Dist(s.units[i].pos, Hero) < 8;
    Check(magesHome && s.Near(FarMark, xhunt::MeleeArrive + .01f, true) == 5, "the melee went, the mages stayed in formation");
}

void TestWatchdogQuietWhileHunting() {
    // A hunter that cannot move (blocked) past 28 sub-tiles from the hero for 12 s: the commander's watchdog would move it back.
    Sim up; up.via = Sim::Via::None;
    Sim s;
    for (Sim* x : {&up, &s}) { x->c.SetPriorityTarget(MarkId, FarMark); for (auto& u : x->units) u.pos = {100, 135}; }
    for (int i = 0; i < 48; ++i) { up.Tick(false); s.Tick(false); }
    Check(up.relocations > 0, "the commander: the stuck summons are moved back by the watchdog");
    Check(s.relocations == 0, "hunting: never");
}

// a hunt it can't finish ends; one that is still getting somewhere never does.
void TestUnreachableGivesUp() {
    // Every summon blocked (the mark across water): given up after NoProgressMs with no new best, never before.
    Sim s;
    s.blocked.assign(s.units.size(), true);
    const std::vector<cmd::ArmyUnitSample> formed = s.units;   // where each stood, in formation, before the mark
    s.c.SetPriorityTarget(MarkId, FarMark);
    const uint64_t t0 = s.now;
    s.Tick();
    int ticks = 0;
    while (s.last.end == End::None && ticks < 200) { s.Tick(); ++ticks; }
    const uint64_t took = s.now - 250 - t0;
    Check(s.last.end == End::Stalled && took == xhunt::NoProgressMs, "unreachable: given up exactly 15 s after the last new best, not before");
    Check(s.last.regrouped && s.c.priorityTargetId == 0 && s.hunt.Mark() == 0, "given up: its focus let go, Regroup given");
    bool sums = s.last.summaryN == 7;
    for (int i = 0; i < s.last.summaryN; ++i) {
        const auto& h = s.last.summary[size_t(i)];
        sums = sums && h.gaveUp && !h.fought && std::fabs(h.start - h.best) < .01f && std::fabs(h.best - h.end) < .01f && h.start > 45;
    }
    Check(sums, "the end's per-hunter summary: seven, each gave up, start = best = end (never got closer), none attacked");
    Check(!s.hunt.Returning(), "no return watch for hunters that gave up: the commander's watchdog has them");
    s.Tick();
    Check(!s.last.started && s.last.mark == 0, "not hunted again (the focus is gone)");
    bool home = true;
    for (size_t i = 0; i < s.units.size(); ++i) home = home && Dist(s.units[i].pos, formed[i].pos) < .01f;
    Check(home && s.relocations == 0, "nobody was moved: each summon is where it stood in formation (no watchdog relocation)");

    // One hunter blocked, the rest walk there and fight: that one gives up alone after HunterStallMs, the hunt goes on.
    const Vec2 spot{100, 130};
    Sim o;
    o.blocked.assign(o.units.size(), false);
    o.blocked[0] = true;
    o.c.SetPriorityTarget(MarkId, spot);
    const uint64_t o0 = o.now;
    uint64_t gaveAt = 0;
    int gaveUpNew = 0;
    for (int i = 0; i < 100; ++i) {   // 25 s
        o.Tick();
        for (size_t k = 1; k < o.units.size(); ++k)   // beside it: swinging (a fight mode), as the game's AI does
            if (Dist(o.units[k].pos, spot) <= (o.Ranged(k) ? xhunt::RangedArrive : xhunt::MeleeArrive)) o.units[k].animMode = 4;
        gaveUpNew += o.last.gaveUpNew;
        if (o.last.gaveUpNew && !gaveAt) gaveAt = o.now - 250;
    }
    Check(gaveUpNew == 1 && gaveAt - o0 == xhunt::HunterStallMs, "the blocked hunter gives up alone, 15 s after its last new best");
    Check(o.last.end == End::None && o.hunt.Mark() == MarkId && o.last.hunters == 6 && o.last.gaveUp == 1,
          "the others fighting beside it: the hunt goes on (a fight is progress)");
    Check(!(o.c.tracked[0].slotPos == spot), "the one that gave up: its leash is Step's again (its spot is no longer the mark)");
}

void TestDetourNotGivenUp() {
    // A long way round (a wall between): every summon first walks AWAY from the mark for 11 s, then comes back to it. A new best
    // comes 12.5 s in, inside the window: never given up, and they get there.
    const Vec2 m{100, 130};
    Sim s;
    s.c.SetPriorityTarget(MarkId, m);
    bool ended = false;
    for (int i = 0; i < 44; ++i) {   // away: 0.25 sub-tiles a tick
        for (auto& u : s.units) { u.pos.y -= .25f; u.animMode = 2; }
        s.Tick(false);
        ended = ended || s.last.end != End::None || s.last.gaveUpNew;
    }
    Check(!ended && s.last.nearest > 38, "walking away round the wall for 11 s: still hunting");
    for (int i = 0; i < 40; ++i) {   // back toward it: 2 sub-tiles a tick, then beside it, swinging
        for (size_t k = 0; k < s.units.size(); ++k) {
            auto& u = s.units[k];
            const float keep = s.Ranged(k) ? xhunt::RangedKeep : xhunt::MeleeKeep, d = Dist(u.pos, m);
            if (d > keep + .1f) { const float step = std::min(2.f, d - keep); u.pos.x += (m.x - u.pos.x) / d * step; u.pos.y += (m.y - u.pos.y) / d * step; u.animMode = 2; }
            else u.animMode = 4;
        }
        s.Tick(false);
        ended = ended || s.last.end != End::None || s.last.gaveUpNew;
    }
    Check(!ended && s.hunt.Mark() == MarkId && s.last.gaveUp == 0, "the detour (distance up for 11 s, then down): never given up");
    Check(s.Near(m, xhunt::MeleeArrive + .01f, true) == 5 && s.last.arrived == 7, "and every hunter got there");
}

void TestNoDistanceCutoff() {
    // "shouldn't matter the distance, if I mark the mob, they should try to go for it." A mark 60 sub-tiles away
    // with the hero walking AWAY from it the whole time: still hunted while the hunters get closer, and they get there.
    const Vec2 far60{100, 160};
    Sim s;
    s.c.SetPriorityTarget(MarkId, far60);
    bool ended = false;
    for (int i = 0; i < 60; ++i) {   // 15 s, the hero 0.5 sub-tiles a tick the other way: 90 sub-tiles from the mark at the end
        s.hero.y -= .5f;
        s.Tick();
        ended = ended || s.last.end != End::None || s.last.gaveUpNew;
    }
    Check(!ended && s.hunt.Mark() == MarkId && s.last.heroDist > 85, "the hero 90 sub-tiles from the mark and still walking away: hunted, never given up");
    Check(s.Near(far60, xhunt::MeleeArrive + .01f, true) == 5, "every melee summon got there");
}

// Hold here and Move and hold defend their spot.
void TestDefend() {
    using Act = xhunt::Defend::Act;
    const Vec2 spot{100, 125};   // 25 sub-tiles from the hero: no hostile near the hero
    xhunt::Defend d;
    Sim s; s.defend = &d;
    s.c.AnchorAt(spot); d.Own(spot);   // the X menu's own anchor (Apply / Hold here's claim)
    s.foes = {{777, 104, 129}};        // 5.7 sub-tiles from the spot
    s.Tick();
    Check(s.defendOut.act == Act::Picked && s.defendOut.id == 777 && s.c.priorityTargetId == 777 && s.c.anchorActive,
          "a monster within 7.5 of the X menu's spot: the army's target, the anchor kept");
    Check(s.last.started && s.last.mark == 777, "hunted from that very tick");
    for (int i = 0; i < 40; ++i) s.Tick();
    Check(s.Near({104, 129}, xhunt::MeleeArrive + .01f, true) == 5, "the melee summons went to it");
    s.foes.clear(); s.c.ClearPriorityTarget();   // it died: companion_ai's liveness check lets it go
    s.Tick();
    Check(s.defendOut.act == Act::Lost && s.defendOut.id == 777 && s.last.end == End::Gone && !s.last.regrouped && s.c.anchorActive,
          "killed: let go, the hunt over, no Regroup over the anchor");
    for (int i = 0; i < 40; ++i) s.Tick();
    bool back = true;
    for (size_t i = 0; i < s.c.trackedCount; ++i)
        back = back && Dist(s.units[i].pos, s.c.tracked[i].slotPos) <= cmd::SummonArmyCoordinator::ArrivalTolerance && Dist(s.units[i].pos, spot) < 12;
    Check(back, "back on their spots round the held spot");
    s.foes = {{778, 100, 140}};   // 15 from the spot: outside the radius
    s.Tick();
    Check(s.defendOut.act == Act::None && s.c.priorityTargetId == 0, "a monster outside the radius: left alone");
    s.foes = {{779, 103, 128}};
    s.Tick();
    Check(s.defendOut.act == Act::Picked && s.c.priorityTargetId == 779, "the next one inside: picked");
    s.c.priorityTargetPos = {100, 140};   // it runs off: 15 from the spot, past 7.5 + 6
    s.Tick();
    Check(s.defendOut.act == Act::LetGo && s.c.priorityTargetId == 0 && s.c.anchorActive, "it ran past the radius and the slack: let go, back to the spot");
    s.foes = {{780, 103, 128}};
    s.c.ApplyCommanderSkill(cmd::SkillRegroup, Hero);   // another order takes the anchor
    s.Tick();
    Check(s.defendOut.act == Act::Ended && !d.Owns() && s.c.priorityTargetId == 0, "another order: the spot is no longer the X menu's, nothing defended");
    s.Tick();
    Check(s.c.priorityTargetId == 0, "and stays so");
    // The commander's own Hold Ground (the game's button, the tactical chord): never defended (unknown unchanged)
    xhunt::Defend d2;
    Sim u; u.defend = &d2;
    u.c.ApplyCommanderSkill(cmd::SkillHoldGround, spot);
    u.foes = {{781, 103, 128}};
    for (int i = 0; i < 4; ++i) u.Tick();
    Check(u.c.priorityTargetId == 0 && !d2.Owns(), "the commander's Hold Ground: no defense of ours");
    // The defend radius 0 and the hunt rule off: nothing picked; a monster already picked is let go
    xhunt::Defend d3;
    Sim o; o.defend = &d3; o.defendR = 0;
    o.c.AnchorAt(spot); d3.Own(spot);
    o.foes = {{782, 103, 128}};
    o.Tick();
    Check(o.c.priorityTargetId == 0 && o.defendOut.act == Act::None, "radius Off: the commander's hold");
    o.defendR = 7.5f; o.Tick();
    Check(o.c.priorityTargetId == 782, "radius On again: picked");
    o.mode = Mode::Off; o.Tick();
    Check(o.defendOut.act == Act::LetGo && o.c.priorityTargetId == 0, "the hunt rule off: the monster let go (nothing would walk them to it)");

    // every summon blocked, a monster by the spot that never dies (across water). Picked once; the hunt gives
    // up after 15 s; the next scan finds it alive: given up, skipped, never picked again (until the cooldown or a new spot).
    {
        xhunt::Defend g;
        Sim b; b.defend = &g;
        b.blocked.assign(b.units.size(), true);
        b.c.AnchorAt(spot); g.Own(spot);
        b.foes = {{790, 104, 129}};
        int picks = 0, gaveUp = 0, stalls = 0;
        for (int i = 0; i < 4 * 35; ++i) {   // 35 s: the give-up at 15 s, then 20 s of skipping
            b.Tick();
            picks += b.defendOut.act == Act::Picked && b.defendOut.id == 790;
            gaveUp += b.defendOut.gaveUp == 790;
            stalls += b.last.end == End::Stalled;
        }
        Check(picks == 1 && stalls == 1 && gaveUp == 1, "an unreachable monster by the spot: picked once, given up once by the hunt, then skipped (no 15 s loop)");
        Check(b.c.priorityTargetId == 0 && b.c.anchorActive && g.Owns() && g.Skipping(b.now) == 790, "they stay on the spot, the monster skipped");
        int again = 0;
        for (int i = 0; i < 8; ++i) { b.Tick(); again += b.defendOut.act == Act::Picked && b.defendOut.id == 790; }   // the 20 s run out
        Check(again == 1, "after the cooldown: tried once more (at most once per 35 s)");
        g.Own(spot);   // a new spot (Move and hold again): nothing skipped
        Check(g.Skipping(b.now) == 0, "a new spot clears the skip");
        // A killed monster is not "given up": gone from the next scan
        xhunt::Defend k;
        Sim kk; kk.defend = &k;
        kk.c.AnchorAt(spot); k.Own(spot);
        kk.foes = {{791, 104, 129}};
        kk.Tick();
        kk.foes.clear(); kk.c.ClearPriorityTarget();
        kk.Tick(); kk.Tick();
        Check(k.Skipping(kk.now) == 0 && kk.defendOut.gaveUp == 0, "killed (gone from the scan): not marked given up");
    }
    // the player's own Attack my target on the very monster being defended (ClearAnchor + SetPriorityTarget): Defend
    // ends without touching that focus; the hunt goes on with it; no Regroup.
    {
        xhunt::Defend g;
        Sim a; a.defend = &g;
        a.c.AnchorAt(spot); g.Own(spot);
        a.foes = {{792, 104, 129}};
        a.Tick();
        Check(a.c.priorityTargetId == 792 && a.last.mark == 792, "defending 792");
        a.c.ClearAnchor(); a.c.SetPriorityTarget(792, {104, 129});   // the X menu's Apply, or the tactical chord's army half
        a.Tick();
        Check(a.defendOut.act == Act::Ended && a.c.priorityTargetId == 792 && a.last.end == End::None && a.last.mark == 792 && !a.last.regrouped,
              "Attack on the defended monster: Defend ends, the player's focus kept, still hunted, no Regroup");
    }
}

void TestOffIsUpstream() {
    // Off from the start: the coordinator, the moves and every position exactly the commander's, tick by tick, with a mark set.
    Sim s; Sim up; up.via = Sim::Via::None;
    s.mode = Mode::Off;
    s.c.SetPriorityTarget(MarkId, FarMark); up.c.SetPriorityTarget(MarkId, FarMark);
    bool same = true;
    for (int i = 0; i < 40; ++i) {
        s.Tick(); up.Tick();
        same = same && s.emitted == up.emitted && s.emitted == s.stepEmitted && !s.last.started && s.last.end == End::None;
        for (size_t m = 0; m < s.emitted; ++m) same = same && s.moves[m].unitId == up.moves[m].unitId && s.moves[m].targetX == up.moves[m].targetX && s.moves[m].targetY == up.moves[m].targetY;
        for (size_t k = 0; k < s.units.size(); ++k) {
            same = same && s.units[k].pos == up.units[k].pos && s.c.tracked[k].slotPos == up.c.tracked[k].slotPos &&
                   s.c.tracked[k].stuckSinceMs == up.c.tracked[k].stuckSinceMs && s.c.tracked[k].farSinceMs == up.c.tracked[k].farSinceMs &&
                   s.c.tracked[k].lastMoveDispatchMs == up.c.tracked[k].lastMoveDispatchMs;
        }
    }
    Check(same, "Off: Step's moves, the spots, the timers and every position exactly the commander's, tick by tick");
    // No mark: the engine touches nothing either
    Sim n; Sim nu; nu.via = Sim::Via::None;
    bool idle = true;
    for (int i = 0; i < 12; ++i) {
        n.Tick(); nu.Tick();
        idle = idle && n.emitted == n.stepEmitted && n.last.mark == 0;
        for (size_t k = 0; k < n.units.size(); ++k) idle = idle && n.units[k].pos == nu.units[k].pos && n.c.tracked[k].slotPos == nu.c.tracked[k].slotPos;
    }
    Check(idle, "On with no mark: exactly the commander's");
    // Off mid-hunt: it ends there; from the next tick nothing is touched
    Sim m;
    m.c.SetPriorityTarget(MarkId, FarMark);
    for (int i = 0; i < 6; ++i) m.Tick();
    m.mode = Mode::Off;
    m.Tick();
    Check(m.last.end == End::Off && m.last.endedMark == MarkId && !m.last.regrouped && m.hunt.Mark() == 0 && !m.hunt.Returning(), "Off mid-hunt: ended, no Regroup, no return watch");
    Check(m.emitted == m.stepEmitted, "and Step's moves untouched that very tick");
    m.Tick();
    bool unlifted = true;
    for (size_t k = 0; k < m.c.trackedCount; ++k) unlifted = unlifted && !(m.c.tracked[k].slotPos == FarMark);
    Check(unlifted && m.last.end == End::None, "the next tick: every spot is Step's again");
    // A gap (a load): forgotten, no Regroup
    Sim gp;
    gp.c.SetPriorityTarget(MarkId, FarMark);
    for (int i = 0; i < 4; ++i) gp.Tick();
    gp.now += 3000;
    gp.Tick();
    Check(gp.last.end == End::Gap && !gp.last.regrouped, "no call for 3 s: the hunt forgotten");
    gp.Tick();
    Check(gp.last.started && gp.last.mark == MarkId, "then armed again on the same mark");
}

void TestHandler() {
    g_lines.clear();
    g_menuOn = false;
    cfg::rules.hunt = 2;
    Sim s; s.via = Sim::Via::Handler;
    Sim up; up.via = Sim::Via::None;
    s.c.SetPriorityTarget(MarkId, FarMark); up.c.SetPriorityTarget(MarkId, FarMark);
    bool same = true;
    for (int i = 0; i < 8; ++i) {
        s.Tick(); up.Tick();
        same = same && s.emitted == s.stepEmitted;
        for (size_t k = 0; k < s.units.size(); ++k) same = same && s.units[k].pos == up.units[k].pos && s.c.tracked[k].slotPos == up.c.tracked[k].slotPos;
    }
    Check(same && g_lines.empty(), "the menu on no button: as before exactly, no line");
    const int formed = s.Near(Hero, 6.0f, false);
    g_menuOn = true;
    s.Tick();
    bool lifted = true;
    for (size_t k = 0; k < s.c.trackedCount; ++k) lifted = lifted && s.c.tracked[k].slotPos == FarMark;
    Check(s.emitted > 0 && lifted, "on a button: the hunt starts that tick (moves go out, every hunter's spot is the mark)");
    for (int i = 0; i < 60; ++i) s.Tick();
    Check(s.Near(FarMark, xhunt::MeleeArrive + .01f, true) == 5 && s.Near(FarMark, xhunt::RangedArrive + .01f, false) == 7, "all seven reach the far mark through the handler");
    s.c.ClearPriorityTarget();
    for (int i = 0; i < 60; ++i) s.Tick();
    Check(formed > 0 && s.Near(Hero, 6.0f, false) == formed && s.Near(FarMark, 30, false) == 0, "the mark gone: Regroup, every summon back in formation on the hero");
    // Melee only, then the rule off with a hunt under way
    cfg::rules.hunt = 1;
    s.c.SetPriorityTarget(MarkId + 5, FarMark);
    for (int i = 0; i < 60; ++i) s.Tick();
    Check(s.Near(FarMark, xhunt::MeleeArrive + .01f, true) == 5 && s.Near(FarMark, 10, false) == 5, "melee only: the five melee summons go, the mages stay");
    cfg::rules.hunt = 0;
    s.Tick();
    Check(s.emitted == s.stepEmitted, "the rule off: the hunt over, Step's moves untouched");
    bool untouched = true;
    for (int i = 0; i < 4; ++i) { s.Tick(); untouched = untouched && s.emitted == s.stepEmitted; }
    Check(untouched, "off: nothing touched");
    Check(g_lines.empty(), "a hunt that starts, arrives, ends and returns logs nothing (ordinary play)");
    cfg::rules.hunt = cfg::Rules{}.hunt;
    g_menuOn = false;
}

void TestHandlerGiveUp() {   // the one line of a hunt: it gave up
    g_lines.clear();
    g_menuOn = true;
    Sim s; s.via = Sim::Via::Handler;
    s.blocked.assign(s.units.size(), true);
    s.c.SetPriorityTarget(MarkId + 9, FarMark);
    for (int i = 0; i < 70; ++i) s.Tick();
    Check(Lines("Frame army x menu: hunt gave up: monster 1008 after 15.0 s, no progress for 15 s (0 of 7 hunters reached it, 0 attacked it): its focus let go; Regroup, back to formation") == 1,
          "a hunt that gave up: its line");
    Check(g_lines.size() == 1 && g_lines[0].size() < 300, "and no other line");
    Check(s.c.priorityTargetId == 0, "its focus let go");
    g_menuOn = false;
}

// Summons run: who runs, from the type and the summon's own MonStats2 mode mask (bit 2 walk, bit 15 run).
void TestRunRules() {
    using xhunt::RunMode;
    constexpr uint32_t walkRun = xhunt::WalkModeBit | xhunt::RunModeBit | (1u << 1) | (1u << 4), walkOnly = xhunt::WalkModeBit | (1u << 1) | (1u << 4);
    Check(xhunt::RunModeOf(0) == RunMode::Off && xhunt::RunModeOf(1) == RunMode::Wolves && xhunt::RunModeOf(2) == RunMode::Runners, "the run rule's three values");
    for (uint8_t t : {uint8_t(3), uint8_t(4), uint8_t(5), uint8_t(6)})
        for (RunMode m : {RunMode::Off, RunMode::Wolves, RunMode::Runners})
            Check(!xhunt::Runs(m, t, walkRun) && !xhunt::Runs(m, t, 0), "a Necromancer summon always walks, whatever its data says (as before)");
    for (uint8_t t : {uint8_t(2), uint8_t(11), uint8_t(12), uint8_t(15), uint8_t(16)})
        Check(!xhunt::Runs(RunMode::Off, t, walkRun), "Off: every summon walks (as before)");
    Check(xhunt::Runs(RunMode::Wolves, 11, 0) && xhunt::Runs(RunMode::Wolves, 12, 0) && xhunt::Runs(RunMode::Wolves, 11, walkRun), "Wolves: Spirit and Dire wolves run (their mask unread or with a run)");
    Check(!xhunt::Runs(RunMode::Wolves, 11, walkOnly), "a wolf whose own data has no run walks (it would slide)");
    for (uint8_t t : {uint8_t(2), uint8_t(15), uint8_t(16)})
        Check(!xhunt::Runs(RunMode::Wolves, t, walkRun), "Wolves: the Valkyrie, Grizzly and Shadows walk");
    for (uint8_t t : {uint8_t(2), uint8_t(15), uint8_t(16)}) {
        Check(xhunt::Runs(RunMode::Runners, t, walkRun), "Every runner: the Valkyrie, Grizzly, Shadow run when their data has a run");
        Check(!xhunt::Runs(RunMode::Runners, t, walkOnly) && !xhunt::Runs(RunMode::Runners, t, 0), "... and walk with no run, or unread (never guessed)");
        Check(!xhunt::Runs(RunMode::Runners, t, xhunt::RunModeBit), "a mask with no walk bit is no read: walk");
    }
    Check(!xhunt::Runs(RunMode::Runners, 7, walkRun) && !xhunt::Runs(RunMode::Runners, 0, walkRun) && !xhunt::Runs(RunMode::Runners, 20, walkRun),
          "a type outside the army (the hireling, an unknown class's pet) walks: unknown unchanged");
}

// The leash rule, and the roster the veto reads.
void TestLeashRule() {
    const uint32_t ids[3] = {200, 201, 205};
    // JudgeLeash at the relocation itself: held, or the reason it lets the game move the summon ("recall brings
    // minions back, town portal brings minions back")
    xhunt::LeashNow k;
    k.on = true; k.hunting = true; k.unit = 201; k.nowMs = 2000; k.atMs = 1000; k.levelNow = k.levelAt = 2; k.priorityTarget = k.mark = 999;
    using R = xhunt::LeashRelease;
    Check(xhunt::JudgeLeash(k, ids, 3) == R::Held, "a hunter in a fresh roster, same area, its mark, no order: kept");
    auto with = [&](auto edit) { xhunt::LeashNow c = k; edit(c); return xhunt::JudgeLeash(c, ids, 3); };
    Check(with([](auto& c) { c.on = false; }) == R::Off, "the leash rule off: as the game's leash was before");
    Check(with([](auto& c) { c.unit = 202; }) == R::NotHunter && with([](auto& c) { c.unit = 0; }) == R::NotHunter, "not a hunter: the game's leash as before");
    Check(with([](auto& c) { c.nowMs = 1000 + xhunt::GapMs + 1; }) == R::NotHunter && with([](auto& c) { c.atMs = 0; }) == R::NotHunter && xhunt::JudgeLeash(k, ids, 0) == R::NotHunter,
          "a stale roster (no army tick for GapMs), none, or empty: nothing kept");
    Check(with([](auto& c) { c.hunting = false; }) == R::HuntOff, "the hunt rule off: released");
    Check(with([](auto& c) { c.freeRoam = true; }) == R::FallOut, "Fall out: released at once");
    Check(with([](auto& c) { c.orderPending = true; }) == R::Order, "an order since the last army tick (Regroup, a formation, Hold): released at once");
    Check(with([](auto& c) { c.orderPending = true; c.priorityTarget = 0; }) == R::Order, "Regroup (an order that also clears the mark): said as the order");
    Check(with([](auto& c) { c.priorityTarget = 0; }) == R::MarkChanged && with([](auto& c) { c.priorityTarget = 1234; }) == R::MarkChanged,
          "the mark cleared (killed, lost) or another mark: released at once");
    Check(with([](auto& c) { c.levelNow = 3; }) == R::AreaChanged, "the area changed since the roster (a portal, waypoint, stairs): released at once");
    Check(with([](auto& c) { c.levelNow = 0; }) == R::NoArea, "the area unknown: released");
    Check(with([](auto& c) { c.levelNow = c.levelAt = 1; }) == R::Town && with([](auto& c) { c.levelNow = c.levelAt = 40; }) == R::Town, "in town: released");
    // PickLeash: which hunters the leash may keep this tick
    {
        const uint32_t hid[3] = {300, 301, 302};
        const Vec2 pos[3] = {{100, 150}, {100, 179}, {100, 181}};   // 50, 79 and 81 sub-tiles from the hero
        uint32_t out[3]{};
        auto p = xhunt::PickLeash(80, 2, 2, hid, pos, 3, Hero, out);
        Check(p.why == xhunt::LeashWhy::Held && p.n == 2 && out[0] == 300 && out[1] == 301 && p.beyond == 1, "within Leash reach (80) of the hero: kept; 81 away: not");
        p = xhunt::PickLeash(0, 2, 2, hid, pos, 3, Hero, out);
        Check(p.why == xhunt::LeashWhy::Held && p.n == 3 && p.beyond == 0, "Leash reach 0 (the default): no distance limit, all three held");
        p = xhunt::PickLeash(300, 2, 2, hid, pos, 3, Hero, out);
        Check(p.n == 3, "the full range (300): all three");
        p = xhunt::PickLeash(80, 0, 2, hid, pos, 3, Hero, out);
        Check(p.why == xhunt::LeashWhy::NoLevel && p.n == 0, "the area unknown: nobody kept");
        p = xhunt::PickLeash(80, 75, 75, hid, pos, 3, Hero, out);
        Check(p.why == xhunt::LeashWhy::Town && p.n == 0, "in town (Kurast Docks): nobody kept");
        p = xhunt::PickLeash(80, 3, 2, hid, pos, 3, Hero, out);
        Check(p.why == xhunt::LeashWhy::LevelChanged && p.n == 0, "the area changed: the roster dropped");
        p = xhunt::PickLeash(80, 3, 0, hid, pos, 3, Hero, out);
        Check(p.why == xhunt::LeashWhy::Held && p.n == 2, "the first area seen: held");
    }
    Sim s;
    s.blocked.assign(s.units.size(), false);
    s.blocked[0] = true;   // one skeleton cannot walk: it gives up alone
    s.c.SetPriorityTarget(MarkId, FarMark);
    s.Tick();
    uint32_t got[cmd::MaxArmyUnits];
    Check(s.hunt.Hunters(got, cmd::MaxArmyUnits) == 7 && got[0] == 200, "the hunt's roster: all seven");
    for (int i = 0; i < 70 && s.hunt.Hunters(got, cmd::MaxArmyUnits) == 7; ++i) s.Tick();
    const size_t n = s.hunt.Hunters(got, cmd::MaxArmyUnits);
    bool has200 = false; for (size_t i = 0; i < n; ++i) has200 = has200 || got[i] == 200;
    Check(n == 6 && !has200, "a hunter that gave up leaves the roster (the game's leash may move it again)");
    s.c.ClearPriorityTarget(); s.Tick();
    Check(s.hunt.Hunters(got, cmd::MaxArmyUnits) == 0, "the hunt over: no roster");
}

// Army X menu taken off its button lets go of the X menu's own anchor, never another order's.
void TestDefendRelease() {
    Sim s;
    xhunt::Defend d;
    const int formed = s.Near(Hero, 6.0f, false);
    const Vec2 spot{130, 100};
    s.c.AnchorAt(spot); d.Own(spot);
    for (int i = 0; i < 40; ++i) s.Tick();
    Check(formed > 0 && s.Near(spot, 6.0f, false) == formed && s.Near(Hero, 6.0f, false) == 0, "Move and hold: the army holds the spot");
    Check(d.Release(s.c) && !s.c.anchorActive && !d.Owns(), "the X menu's spot: cleared (ClearAnchor), forgotten");
    for (int i = 0; i < 40; ++i) s.Tick();
    Check(s.Near(Hero, 6.0f, false) == formed, "the army re-formed on the hero");
    s.c.AnchorAt(spot); d.Own(spot);
    s.c.AnchorAt({90, 100});   // another order (the game's Hold Ground) moved the anchor
    Check(!d.Release(s.c) && s.c.anchorActive && !d.Owns(), "another order's anchor: untouched, ours forgotten");
    s.c.ClearAnchor();
    Check(!d.Release(s.c) && !s.c.anchorActive, "no anchor, not owned: nothing");
}

// The three new handlers as the game calls them, on the army tick's clock (GetTickCount64).
void TestHandlerLeashRunAsk() {
    g_lines.clear();
    g_menuOn = true;
    cfg::rules.hunt = 2; cfg::rules.huntLeash = true; cfg::rules.run = 1;
    // Each phase runs on its own clock, set so its army ticks sit just before the real clock the leash veto reads
    // (GetTickCount64; a roster older than GapMs counts for nothing); each new Sim's first tick is a gap for the handler (re-armed).
    const auto clockFor = [](Sim& s, int ticks) { s.now = GetTickCount64() - 500 - uint64_t(ticks) * 250; };
    const Vec2 mark{100, 120};
    g_testLevel = 2;
    const auto at = [](Sim& s, uint32_t id) { return seam::SummonRelocateFrame{id, 100, 100, {s.c.orderStampPending, s.c.priorityTargetId, s.c.freeRoam}}; };
    {   // The leash rule: a hunter kept, a stranger not; the rule off or the hunt rule off releases it
        Sim s; s.via = Sim::Via::Handler; clockFor(s, 2);
        s.c.SetPriorityTarget(MarkId + 20, mark);
        s.Tick(); s.Tick();
        Check(armyxhunt::Leash(at(s, 200)), "the leash rule: a hunter is kept");
        Check(!armyxhunt::Leash(at(s, 777)), "not a hunter: the game's leash as before");
        cfg::rules.huntLeash = false;
        Check(!armyxhunt::Leash(at(s, 200)), "the leash rule off: the game's leash as before");
        cfg::rules.huntLeash = true;
        cfg::rules.hunt = 0;
        Check(!armyxhunt::Leash(at(s, 200)), "the hunt rule off: released at once");
        cfg::rules.hunt = 2;
    }
    {   // Recall and every other order release the leash at once
        Sim s; s.via = Sim::Via::Handler; clockFor(s, 6);   // six ticks in all
        s.c.SetPriorityTarget(MarkId + 24, mark);
        s.Tick(); s.Tick();
        Check(armyxhunt::Leash(at(s, 201)), "hunting: kept");
        s.c.ApplyCommanderSkill(cmd::SkillRegroup, Hero);   // the X menu's Regroup or the commander skill: the same coordinator call
        Check(!armyxhunt::Leash(at(s, 201)), "Regroup: released at once (before the next army tick)");
        s.Tick();
        Check(!armyxhunt::Leash(at(s, 201)) && s.c.priorityTargetId == 0, "the next tick: the hunt over (a new order), nothing kept");
        s.c.SetPriorityTarget(MarkId + 25, mark); s.Tick();
        Check(armyxhunt::Leash(at(s, 201)), "a new mark: hunting, kept");
        s.c.SetStanceAndFormation(cmd::ArmyStance::Defensive, cmd::ArmyFormation::PhalanxArc);   // a formation order
        Check(!armyxhunt::Leash(at(s, 201)), "a formation order: released at once");
        s.Tick(); s.c.SetPriorityTarget(MarkId + 26, mark); s.Tick();
        Check(armyxhunt::Leash(at(s, 201)), "a new mark: kept");
        s.c.SetPriorityTarget(MarkId + 27, {100, 130});   // another mark (the X menu's Attack my target, the tactical chord)
        Check(!armyxhunt::Leash(at(s, 201)), "another mark, or the mark cleared (killed, lost): released at once");
        s.c.SetPriorityTarget(MarkId + 26, mark);
        s.c.ApplyCommanderSkill(cmd::SkillFallOut, Hero);
        Check(!armyxhunt::Leash(at(s, 201)), "Fall out: released at once");
    }
    {   // Leash reach, when set: hunters fighting a mark forever (a fight is progress: the hunt never gives up) while you walk 100
        // sub-tiles away. No limit (the default) keeps them; Leash reach 80 lets the game bring them home. The hunt goes on either way.
        Sim s; s.via = Sim::Via::Handler; clockFor(s, 38);
        s.c.SetPriorityTarget(MarkId + 21, mark);
        for (int i = 0; i < 30; ++i) s.Tick();
        s.hero = {100, 220};
        auto swing = [&] { for (size_t k = 0; k < s.units.size(); ++k) if (!s.Ranged(k) && Dist(s.units[k].pos, mark) <= xhunt::MeleeArrive) s.units[k].animMode = 4; };
        for (int i = 0; i < 6; ++i) { swing(); s.Tick(); }
        Check(s.c.priorityTargetId == MarkId + 21 && g_lines.empty(), "the hunt is still under way (no distance ends it)");
        Check(armyxhunt::Leash(at(s, 200)), "No limit (the default): kept 100 sub-tiles from you");
        cfg::rules.leashReach = 80;
        swing(); s.Tick();
        Check(!armyxhunt::Leash(at(s, 200)), "a leash reach of 80: the hunter is not kept");
        cfg::rules.leashReach = cfg::Rules{}.leashReach;
    }
    {   // A portal, waypoint or stairs (the area changes), then a Town Portal: released at once
        Sim s; s.via = Sim::Via::Handler; clockFor(s, 7);
        s.c.SetPriorityTarget(MarkId + 22, mark);
        s.Tick(); s.Tick();
        Check(armyxhunt::Leash(at(s, 200)), "same area: kept");
        g_testLevel = 3;   // the hero took the stairs: before the next army tick
        Check(!armyxhunt::Leash(at(s, 200)), "the area changed since the roster: released at once");
        s.Tick();
        Check(!armyxhunt::Leash(at(s, 200)), "the next tick drops the roster");
        s.Tick();
        Check(armyxhunt::Leash(at(s, 200)), "the tick after, in the new area beside you: held again (you are together)");
        g_testLevel = 1;   // a Town Portal: the Rogue Encampment
        Check(!armyxhunt::Leash(at(s, 200)), "Town Portal: released at once");
        s.Tick();
        Check(!armyxhunt::Leash(at(s, 200)), "in town: no roster");
        g_testLevel = 0;
        s.Tick();
        Check(!armyxhunt::Leash(at(s, 200)), "the area unknown: nobody kept");
        g_testLevel = 2;
    }
    Sim s; s.via = Sim::Via::Handler; clockFor(s, 42);
    s.c.SetPriorityTarget(MarkId + 20, mark);
    for (int i = 0; i < 42; ++i) s.Tick();
    s.c.ClearPriorityTarget(); s.Tick();
    Check(!armyxhunt::Leash(at(s, 200)), "the hunt over: nothing kept");
    // Summons run: the move's walk flag (no game here: every mask unread)
    auto walk = [](uint8_t type) { bool w = true; armyxhunt::Walk({0, 300, type, w, &Say}); return w; };
    Check(!walk(11) && !walk(12) && walk(4) && walk(5) && walk(3) && walk(2) && walk(16), "Wolves: the wolves run; Necromancer summons, the Valkyrie and Shadows walk");
    cfg::rules.run = 2;
    Check(!walk(11) && walk(2) && walk(15) && walk(16) && walk(4), "Every runner, masks unread: only the wolves run (never guessed)");
    cfg::rules.run = 0;
    Check(walk(11) && walk(12) && walk(2), "the run rule off: every summon walks");
    cfg::rules.run = 1; g_menuOn = false;
    Check(walk(11) && walk(12), "Army X menu Off: every summon walks (as before)");
   
    cfg::rules.hunt = cfg::Rules{}.hunt; g_menuOn = false;
}

// Summons run's read of a summon's MonStats2 mode mask: a unit and its stats row in this process.
void TestRunRead() {
    g_lines.clear();
    g_menuOn = true; cfg::rules.run = 1;
    g_sigOk = true;   // StatsRva's signature matches: the row is FakeRow's
    struct Unit { alignas(8) uint8_t b[0x200]{}; };
    auto unit = [](Unit& u, uint32_t classId, uint32_t id) { const uint32_t h[4] = {1, classId, id, 1}; std::memcpy(u.b, h, sizeof h); u.b[0x1bd] = 0; };
    Unit wolf, skel; unit(wolf, 359, 900); unit(skel, 363, 901);
    auto setMask = [](uint32_t m) { std::memcpy(g_fakeRow + 0xf0, &m, sizeof m); };
    auto walk = [](Unit& u, uint32_t id, uint8_t type) { bool w = true; armyxhunt::Walk({reinterpret_cast<uintptr_t>(u.b), id, type, w, &Say}); return w; };
    constexpr uint32_t walkOnly = 0x16, walkRun = 0x8016;   // bits 1, 2, 4 (+ 15)
    setMask(0);   // a failed read (a load, no row yet)
    Check(!walk(wolf, 900, 11) && Lines("run check: summon 900, Spirit wolf (type 11, class 359): MonStats2 modes 0x00000000 (unread:") == 1,
          "a failed read: said, the wolf still runs (Wolves)");
    setMask(walkOnly);
    Check(!walk(wolf, 900, 11), "within a second: not read again (still unread)");
    Sleep(1100);
    Check(walk(wolf, 900, 11), "read again a second later, accepted: no run in its data, it walks");
    setMask(walkRun);
    Check(walk(wolf, 900, 11), "an accepted mask is kept for good (not read again)");
    Check(walk(skel, 901, 4), "a Skeleton walks whatever its mask says");
    Check(Lines("run check:") == 1 && g_lines.size() == 1, "only the failed read was said, once");
    g_sigOk = false;
    cfg::rules.run = cfg::Rules{}.run; g_menuOn = false; g_menuOn = false;
}

// Run the whole approach, the engine: a runner the AI slowed to a walk is sent again after MinResendMs; one running
// is not; with no runners (the rule off) Step is exactly as before, tick for tick.
void TestRunApproachEngine() {
    auto run = [](Sim& s, bool inject, int ticks) {
        s.moveMode = xhunt::RunMode15;   // our runs make them run
        s.c.SetPriorityTarget(MarkId, FarMark);
        for (int i = 0; i < ticks; ++i) { s.Tick(); if (inject) s.units[0].animMode = xhunt::WalkMode; }   // the AI's own walk replaced our run on 200
    };
    Sim off, zeros, on, onRunning;
    zeros.runners.assign(zeros.units.size(), 0);
    on.runners.assign(on.units.size(), 1);
    onRunning.runners.assign(onRunning.units.size(), 1);
    run(off, true, 12); run(zeros, true, 12); run(on, true, 12); run(onRunning, false, 12);
    bool same = off.units.size() == zeros.units.size();
    for (size_t i = 0; same && i < off.units.size(); ++i) same = Dist(off.units[i].pos, zeros.units[i].pos) < 1e-4f;
    Check(same && off.runAgain == 0 && zeros.runAgain == 0, "Off (no runners, or every runner flag 0): no move sent again, every summon where the commander's Step and the hunt put it");
    Check(onRunning.runAgain == 0, "a runner seen running: never sent again early");
    Check(on.runAgain >= 3, "a runner seen walking (the AI's own walk): sent running again after 300 ms, every time");
    std::printf("  run approach: %d moves sent again in 12 ticks (a runner walking)\n", on.runAgain);
    // Beside the mark (arrived) a walking runner gets no move: the game's AI has it
    Sim beside; beside.runners.assign(beside.units.size(), 1); beside.moveMode = xhunt::RunMode15;
    beside.c.SetPriorityTarget(MarkId, {100, 106});
    for (int i = 0; i < 30; ++i) beside.Tick();
    for (auto& u : beside.units) u.animMode = xhunt::WalkMode;
    const int before = beside.runAgain;
    beside.Tick();
    Check(beside.last.hunters > 0 && beside.last.arrived == beside.last.hunters && beside.runAgain == before && beside.last.sent == 0,
          "every hunter beside the mark: walking, it is sent nothing (no run again)");
}

// Fake game memory for the seams' reads: a unit's header [type, class, id, mode], +0x38 its path (uint16[4]: x fraction, x, y
// fraction, y), +0xd8 its game (non-zero: a server unit).
struct FakeUnit {
    alignas(8) uint8_t b[0x200]{};
    alignas(8) uint16_t path[8]{};
    FakeUnit(uint32_t type, uint32_t id, float x = 0, float y = 0) {
        const uint32_t h[4] = {type, 0, id, 1};
        std::memcpy(b, h, sizeof h);
        const uintptr_t p = reinterpret_cast<uintptr_t>(path), game = reinterpret_cast<uintptr_t>(b);
        std::memcpy(b + 0x38, &p, sizeof p);
        std::memcpy(b + 0xd8, &game, sizeof game);
        path[0] = uint16_t((x - std::floor(x)) * 65536.0f); path[1] = uint16_t(x); path[2] = uint16_t((y - std::floor(y)) * 65536.0f); path[3] = uint16_t(y);
    }
    uintptr_t at() const { return reinterpret_cast<uintptr_t>(b); }
};
// A handler Sim whose army ticks sit just before the real clock the seams read (GetTickCount64; a roster older than GapMs counts
// for nothing): `ticks` ticks, then the seam calls (fresh). Its first tick is a gap (re-armed).
void Ready(Sim& s, uint32_t mark, Vec2 at, int ticks) {
    s.via = Sim::Via::Handler;
    s.now = GetTickCount64() - 500 - uint64_t(ticks) * 250;
    s.c.SetPriorityTarget(mark, at);
    for (int i = 0; i < ticks; ++i) s.Tick();
}
void TestRunApproachHandler() {
    g_lines.clear();
    g_menuOn = true;
    cfg::rules.hunt = 2;
    cfg::rules.run = cfg::Rules{}.run;                 // Every runner (the default)
    cfg::rules.runApproach = cfg::Rules{}.runApproach != 0; // On (the default)
    Check(xhunt::RunModeOf(cfg::Rules{}.run) == xhunt::RunMode::Runners, "Summons run: Every runner by default");
    Sim s;
    for (int k = 0; k < 4; ++k) s.units[size_t(k)].petType = 12;   // 200-203 Dire wolves (they run: wolves, mask unread); 204-205 mages, 206 a golem walk
    Ready(s, MarkId + 40, FarMark, 2);
    // The AI's own place walk for a hunting wolf on its way: sent as a run, the call ServerMoveToCoord makes
    auto move = [](uint32_t id, uintptr_t target, int mode, int x = 120, int y = 130) {
        int m = mode; uint8_t p = 3; uint16_t f = 0x10;
        armyxhunt::Move({id, target, x, y, m, p, f});
        return std::array<int, 3>{m, int(p), int(f)};
    };
    const std::array<int, 3> asCame{2, 3, 0x10};
    Check(move(200, 0, 2) == std::array<int, 3>{15, 1, 0}, "a hunting wolf's own walk to a place: ServerMoveUnit mode 15, path type 1, flags 0");
    Check(move(201, 0, 2) == std::array<int, 3>{15, 1, 0}, "and the next wolf's");
    Check(move(204, 0, 2) == asCame && move(206, 0, 2) == asCame, "a mage, a golem (no run): the game's call as it came");
    Check(move(777, 0, 2) == asCame, "not a hunter (any other monster): untouched");
    Check(move(200, 0x12345678, 2) == asCame, "a unit-aimed walk: the game's shape kept");
    Check(move(200, 0, 2, 0, 130) == asCame && move(200, 0, 2, 120, 0) == asCame && move(200, 0, 2, -5, -5) == asCame,
          "a place walk with no place (x or y not above 0): the game's call as it came, not counted");
    Check(move(200, 0, 4) == std::array<int, 3>{4, 3, 0x10} && move(200, 0, 15) == std::array<int, 3>{15, 3, 0x10}, "an attack, or a run already: untouched");
    cfg::rules.runApproach = false;
    Check(move(200, 0, 2) == asCame, "Run the whole approach Off: the AI's walk stays a walk (as before)");
    cfg::rules.runApproach = true;
    cfg::rules.run = 0;
    Check(move(200, 0, 2) == asCame, "Summons run Off: untouched");
    cfg::rules.run = 2;
    cfg::rules.run = cfg::Rules{}.run;
    cfg::rules.hunt = cfg::Rules{}.hunt; g_menuOn = false;
}

// ---- Beside a far mark: the retry, the refused move answered failed; and the fight back when hit ----
void TestFarMarkRetryAndRefusedRules() {
    using xhunt::RetryDue; using xhunt::RetryGoal;
    Check(RetryDue(1000, true, 1, 5000, 4000) && !RetryDue(1000, true, 1, 4999, 4000), "Retry at the mark: due once an arrived hunter stood idle for the time, not a millisecond before");
    Check(!RetryDue(0, true, 1, 9000, 4000), "0 (Off): never");
    Check(!RetryDue(1000, false, 1, 9000, 4000) && !RetryDue(1000, true, 1, 9000, 0) && !RetryDue(1000, true, 1, 3000, 4000), "not arrived, no idle clock, or a clock ahead of now: never");
    Check(!RetryDue(1000, true, 4, 9000, 4000) && !RetryDue(1000, true, 5, 9000, 4000) && !RetryDue(1000, true, 8, 9000, 4000) && !RetryDue(1000, true, 7, 9000, 4000) &&
          !RetryDue(1000, true, 3, 9000, 4000), "mid-fight (attack, skill, cast, hit): never");
    Check(!RetryDue(1000, true, 2, 9000, 4000) && !RetryDue(1000, true, 15, 9000, 4000), "walking or running: never");
    const Vec2 mark{100, 100}, unit{100, 102};
    const Vec2 l = RetryGoal(unit, mark, false, true), r = RetryGoal(unit, mark, false, false);
    Check(std::fabs(Dist(l, mark) - xhunt::MeleeKeep) < 1e-3f && std::fabs(Dist(r, mark) - xhunt::MeleeKeep) < 1e-3f, "a retried melee hunter is sent to its keep distance from the mark");
    Check(Dist(l, unit) > 1.0f && Dist(r, unit) > 1.0f && Dist(l, unit) < xhunt::MeleeArrive + xhunt::MeleeKeep, "a real step: never onto its own cell, never away from the mark");
    Check(Dist(l, r) > 1.0f && std::fabs(l.y - r.y) < 1e-3f && std::fabs((l.x - mark.x) + (r.x - mark.x)) < 1e-3f, "left and right: mirror places round the mark");
    const Vec2 rg = RetryGoal({100, 112}, mark, true, true);
    Check(std::fabs(Dist(rg, mark) - xhunt::RangedKeep) < 1e-3f && std::fabs(Dist(rg, Vec2{100, 110}) - 2.0f * xhunt::RangedKeep * std::sin(xhunt::RetryStep / xhunt::RangedKeep / 2)) < 1e-2f,
          "ranged: RetryStep round the mark at its stand-off");
    const Vec2 on = RetryGoal(mark, mark, false, true);
    Check(std::isfinite(on.x) && std::isfinite(on.y) && std::fabs(Dist(on, mark) - xhunt::MeleeKeep) < 1e-3f, "standing on the mark: still a place at its keep distance");

    using xhunt::JudgeRefused;
    Check(JudgeRefused(true, true, false), "a hunter beside its mark: answered failed, not swallowed");
    Check(!JudgeRefused(true, false, false), "not a hunter: the swallow, as before");
    Check(!JudgeRefused(true, true, true), "a hunter still on its way: the swallow (our own moves drive it)");
    Check(!JudgeRefused(false, true, false), "the rule off: the swallow");
}

void TestFarMarkRetryEngine() {
    const Vec2 at{100, 112};
    {   // Off (0): an arrived hunter is sent nothing, as before
        Sim s; s.c.SetPriorityTarget(MarkId, at);
        for (int i = 0; i < 20; ++i) s.Tick();
        int sent = 0, retried = 0;
        for (int i = 0; i < 24; ++i) { s.Tick(); sent += s.last.sent; retried += s.last.retried; }
        Check(s.last.hunters == 7 && s.last.arrived == 7 && sent == 0 && retried == 0, "Retry at the mark Off: every hunter beside the mark, standing, is sent nothing (as before)");
    }
    {   // On: an arrived hunter standing for the time is sent a short step beside the mark, by turns left and right
        Sim s; s.retryMs = 1000; s.c.SetPriorityTarget(MarkId, at);
        int firstIdle = -1, firstRetry = -1, afterArrival = 0, afterRetried = 0;
        std::vector<float> side;
        bool beside = true, real = true, counted = true, who = true;
        for (int i = 0; i < 44; ++i) {
            const bool idle = Dist(s.units[0].pos, at) <= xhunt::MeleeArrive && s.units[0].animMode == 1;
            const Vec2 was = s.units[0].pos;
            if (idle && firstIdle < 0) firstIdle = i;
            s.Tick();
            counted = counted && s.last.sent >= s.last.retried;
            if (s.last.retried) who = who && s.last.retryId >= 200 && s.last.retryId <= 206 && s.last.retryDist <= xhunt::RangedArrive;
            if (i >= 20) { afterArrival += s.last.sent; afterRetried += s.last.retried; }
            if (firstIdle >= 0 && idle && s.last.retried && s.InMoves(200)) {
                if (firstRetry < 0) firstRetry = i;
                const Vec2 g = s.dest[0];
                beside = beside && Dist(g, at) >= 0.7f && Dist(g, at) <= xhunt::MeleeKeep + 0.75f;
                real = real && Dist(g, was) >= 1.0f;
                side.push_back((was.x - at.x) * (g.y - at.y) - (was.y - at.y) * (g.x - at.x));
            }
        }
        Check(firstIdle >= 0 && firstRetry - firstIdle == 4, "On (1 s): summon 200 is sent again on the tick it has stood beside the mark for a second, not before");
        Check(side.size() >= 3 && beside && real, "each retry: a place at its keep distance from the mark, a real step from where it stands");
        Check(side.size() >= 3 && (side[0] > 0) != (side[1] > 0) && (side[1] > 0) != (side[2] > 0), "by turns left and right round the mark");
        Check(counted && who && afterArrival == afterRetried && afterRetried > 0, "retries are hunt moves like any other (counted in sent; beside the mark nothing else is sent), the first one named");
        Check(s.last.hunters == 7 && s.last.arrived == 7 && s.relocations == 0, "a retried hunter stays arrived (it never leaves the mark's side)");
    }
    {   // Never mid-fight; the idle clock starts over after a fight
        Sim s; s.retryMs = 1000; s.c.SetPriorityTarget(MarkId, at);
        for (int i = 0; i < 20; ++i) s.Tick();
        for (auto& u : s.units) u.animMode = 4;
        int retried = 0, sent = 0;
        for (int i = 0; i < 12; ++i) { s.Tick(false); retried += s.last.retried; sent += s.last.sent; }
        Check(retried == 0 && sent == 0, "hunters swinging beside the mark: never retried, never sent a move");
        for (auto& u : s.units) u.animMode = 1;
        int early = 0;
        for (int i = 0; i < 4; ++i) { s.Tick(false); early += s.last.retried; for (auto& u : s.units) u.animMode = 1; }
        s.Tick(false);
        Check(early == 0 && s.last.retried == 7, "after the fight the idle clock starts over: a second standing, then every one of them is sent again");
    }
}

// A summon 200 that a monster (5000) just hit, `away` sub-tiles from its formation spot: one tick in
struct HitSim {
    Sim s; xhunt::HitBack hb; Vec2 spot{}, foe{};
    explicit HitSim(float away = 12.0f) {
        s.via = Sim::Via::None; s.hitBack = &hb;
        spot = s.c.tracked[0].slotPos; foe = {spot.x, spot.y + away};
        s.hbHits.push_back({200, 5000, foe, s.now});
        s.Tick();
    }
    HitSim(const HitSim&) = delete;
    size_t Fights() const { uint32_t p[cmd::MaxArmyUnits], f[cmd::MaxArmyUnits]; return hb.Fighting(p, f, cmd::MaxArmyUnits); }
    uint32_t FoeOf(uint32_t pet) const {
        uint32_t p[cmd::MaxArmyUnits], f[cmd::MaxArmyUnits];
        const size_t n = hb.Fighting(p, f, cmd::MaxArmyUnits);
        for (size_t k = 0; k < n; ++k) if (p[k] == pet) return f[k];
        return 0;
    }
};
void TestHitBack() {
    using HB = xhunt::HitBack;
    constexpr uint32_t Foe = 5000;
    {   // A hit: that summon alone goes for its attacker; the others keep formation
        Sim up; up.via = Sim::Via::None;
        const Vec2 spot0 = up.c.tracked[0].slotPos, from = up.units[0].pos;
        Check(up.c.tracked[0].unitId == 200 && (spot0.x != 0 || spot0.y != 0), "(the test's summon 200 has a formation spot)");
        Check(!up.c.ShouldAllowSummonCombatTarget(200, from, {spot0.x, spot0.y + 20}),
              "the commander (Balanced): a monster 20 sub-tiles from its spot is refused as its target: hit from there, it stands");
        HitSim h(20); Sim& s = h.s;
        up.Tick();
        Check(s.hbOut.startedN == 1 && s.hbOut.started[0].pet == 200 && s.hbOut.started[0].foe == Foe && s.hbOut.started[0].petType == 4 &&
              std::fabs(s.hbOut.started[0].dist - Dist(from, h.foe)) < 1e-3f && std::fabs(s.hbOut.started[0].fromSpot - 20.0f) < 1e-3f && s.hbOut.engaged == 1,
              "Fight back when hit: the summon a monster hit takes it on (who, how far, how far from its spot)");
        Check(h.Fights() == 1 && h.FoeOf(200) == Foe && h.FoeOf(201) == 0, "that summon only: the attacker is its own target, nobody else's");
        Check(s.hbOut.sent == 1 && s.InMoves(200) && Dist(s.dest[0], h.foe) < Dist(from, h.foe), "it is sent toward its attacker");
        Check(s.c.tracked[0].slotPos == h.foe && s.c.tracked[0].farSinceMs == 0 && s.c.tracked[0].stuckSinceMs == 0,
              "its spot is its attacker's place until the next Step, its watchdog timers zero");
        Check(s.c.ShouldAllowSummonCombatTarget(200, s.units[0].pos, h.foe) && !s.c.ShouldAllowSummonCombatTarget(201, s.units[1].pos, h.foe),
              "the commander's own attack gate now allows the attacker for it, and still refuses it for the others");
        bool others = true, lifted = true;
        int dropped = 0;
        for (int i = 0; i < 24; ++i) {
            if (i % 8 == 7) s.hbHits.push_back({200, Foe, h.foe, s.now});   // hit again every 2 s
            s.Tick(); up.Tick();
            dropped += s.hbOut.dropped;
            lifted = lifted && s.c.tracked[0].slotPos == h.foe;
            for (size_t k = 1; k < s.units.size(); ++k) others = others && s.units[k].pos == up.units[k].pos && s.c.tracked[k].slotPos == up.c.tracked[k].slotPos;
        }
        Check(Dist(s.units[0].pos, h.foe) <= xhunt::MeleeArrive && h.Fights() == 1, "it walks to its attacker and stays on it while the hits keep coming");
        Check(others, "the others keep formation: their places and spots exactly the commander's, tick by tick");
        Check(dropped > 0 && lifted && s.relocations == 0, "Step's walks back for it are dropped, and the watchdog never moves it back");
        // No new hit, no fight: let go after the keep time; it walks back (slowly here) with the watchdog held off
        int quietAt = -1;
        for (int i = 0; i < 20 && quietAt < 0; ++i) { s.Tick(); if (s.hbOut.endedN) quietAt = i; }
        Check(quietAt == 15 && s.hbOut.ended[0].pet == 200 && s.hbOut.ended[0].foe == Foe && s.hbOut.ended[0].end == HB::End::Quiet && !s.hbOut.ended[0].fought &&
              s.hbOut.ended[0].afterMs >= 4000 && h.Fights() == 0, "no new hit and no fight for the keep time (4 s to the tick): the attacker is let go");
        s.speed = 1.0f;
        bool back = false;
        for (int i = 0; i < 32; ++i) { s.Tick(); back = back || s.InMoves(200); }
        Check(back && Dist(s.units[0].pos, h.spot) < 2.5f && s.c.tracked[0].inSlot && s.relocations == 0,
              "then Step walks it back to its spot (its move no longer dropped), the watchdog held off for the walk: never teleported");
    }
    {   // Who is never taken
        Sim s; s.via = Sim::Via::None; HB hb; s.hitBack = &hb;
        const Vec2 spot = s.c.tracked[2].slotPos, foe{spot.x, spot.y + 12};
        const auto hit = [&](uint32_t pet, uint32_t by, Vec2 where, uint64_t ago = 0) { s.hbHits.push_back({pet, by, where, s.now - ago}); s.Tick(); return s.hbOut.startedN; };
        Check(hit(777, Foe, foe) == 0, "not a tracked summon: never");
        Check(hit(202, 203, foe) == 0, "hit by one of the army's own summons: never");
        s.units[2].animMode = 4;
        Check(hit(202, Foe, foe) == 0, "already swinging: it fights already, nothing changes");
        s.units[2].animMode = 1;
        Check(hit(202, Foe, foe, 4000) == 0, "a hit older than the keep time: never");
        Check(hit(202, 0, foe) == 0 && hit(0, Foe, foe) == 0, "no attacker, or no summon: never");
        s.hbFor = 0;
        Check(hit(202, Foe, foe) == 0, "Keep the attacker 0 (Off): no summon takes an attacker");
        s.hbFor = 4000;
        Check(hit(202, Foe, {spot.x, spot.y + 26}) == 0, "past Fight back reach from its spot (25): never");
        s.c.SetPriorityTarget(MarkId, FarMark);
        Check(hit(202, Foe, foe) == 0, "the army has a mark: never (the mark comes first)");
        s.c.ClearPriorityTarget();
        s.hbReach = 0;
        Check(hit(202, Foe, {spot.x, spot.y + 26}) == 1, "Fight back reach 0: no limit");
        uint32_t p[cmd::MaxArmyUnits], f[cmd::MaxArmyUnits];
        Check(hit(202, Foe + 1, foe) == 0 && hb.Fighting(p, f, cmd::MaxArmyUnits) == 1 && p[0] == 202 && f[0] == Foe, "hit by another while on its attacker: it keeps the first");
        Sim t; t.via = Sim::Via::None; HB hb2; t.hitBack = &hb2;
        t.units[1].petType = 21;
        t.Tick();
        t.hbHits.push_back({201, Foe, {100, 110}, t.now}); t.Tick();
        Check(t.hbOut.startedN == 0 && t.emitted == t.stepEmitted, "a summon of an unknown type (outside the commander's army mask): never, the commander's path");
    }
    {   // How it ends
        { HitSim h; h.s.hbFoes = {{Foe, h.foe, false}}; h.s.Tick();
          Check(h.s.hbOut.endedN == 1 && h.s.hbOut.ended[0].end == HB::End::Gone && h.Fights() == 0 && h.s.emitted == h.s.stepEmitted && !(h.s.c.tracked[0].slotPos == h.foe),
                "the attacker dead or gone: over at once, Step's moves and its spot untouched from that tick"); }
        { HitSim h; const Vec2 moved{h.foe.x + 8, h.foe.y};
          h.s.hbFoes = {{Foe, moved, true}};
          for (int i = 0; i < 12; ++i) h.s.Tick();
          Check(h.Fights() == 1 && h.s.c.tracked[0].slotPos == moved && Dist(h.s.units[0].pos, moved) <= xhunt::MeleeArrive, "the attacker moves: it follows it (its place as last looked up)"); }
        { HitSim h; h.s.hbFoes = {{Foe, {h.spot.x, h.spot.y + 27}, true}}; h.s.Tick();
          const bool kept = h.Fights() == 1 && h.s.hbOut.endedN == 0;
          h.s.hbFoes = {{Foe, {h.spot.x, h.spot.y + 30}, true}}; h.s.Tick();
          Check(kept && h.s.hbOut.endedN == 1 && h.s.hbOut.ended[0].end == HB::End::Far && h.Fights() == 0,
                "the attacker past the reach from its spot: kept within the slack (27 of 25 + 4), let go past it (30)"); }
        { HitSim h; h.s.c.SetStanceAndFormation(cmd::ArmyStance::Defensive, cmd::ArmyFormation::PhalanxArc); h.s.Tick();
          Check(h.s.hbOut.endedN == 1 && h.s.hbOut.ended[0].end == HB::End::Order && h.Fights() == 0 && h.s.emitted == h.s.stepEmitted, "a formation order: over, the commander re-forms the army"); }
        { HitSim h; h.s.c.ApplyCommanderSkill(cmd::SkillRegroup, Hero); h.s.Tick();
          Check(h.s.hbOut.endedN == 1 && h.s.hbOut.ended[0].end == HB::End::Order, "Regroup: over"); }
        { HitSim h; h.s.c.SetPriorityTarget(MarkId, FarMark); h.s.Tick();
          Check(h.s.hbOut.endedN == 1 && h.s.hbOut.ended[0].end == HB::End::Mark && h.Fights() == 0, "a mark: over (the hunt has the army)"); }
        { HitSim h; h.s.hbOn = false; h.s.Tick();
          const bool off = h.s.hbOut.endedN == 1 && h.s.hbOut.ended[0].end == HB::End::Off && h.Fights() == 0 && h.s.emitted == h.s.stepEmitted;
          h.s.hbHits.push_back({200, Foe, h.foe, h.s.now}); h.s.Tick();
          Check(off && h.s.hbOut.endedN == 0 && h.s.hbOut.startedN == 0 && h.s.emitted == h.s.stepEmitted, "Off mid-fight: over, then nothing is touched, hit or not"); }
        { HitSim h; h.s.units[0].unitId = 260; h.s.Tick();
          Check(h.s.hbOut.endedN == 1 && h.s.hbOut.ended[0].end == HB::End::Left && h.s.hbOut.ended[0].pet == 200 && h.Fights() == 0, "the summon left the army (died, cast again): over"); }
        { HitSim h; h.s.now += 3000; h.s.Tick();
          Check(h.Fights() == 0 && h.s.hbOut.endedN == 0 && h.s.emitted == h.s.stepEmitted, "no call for 3 s (a load): forgotten"); }
        { HitSim h;   // it fights beside its attacker: kept with no new hit; the keep time runs from its last swing
          h.s.units[0].pos = {h.foe.x, h.foe.y - 1.5f}; h.s.going[0] = false; h.s.units[0].animMode = 4;
          for (int i = 0; i < 24; ++i) h.s.Tick(false);
          const bool kept = h.Fights() == 1;
          h.s.units[0].animMode = 1;
          int endAt = -1;
          for (int i = 0; i < 20 && endAt < 0; ++i) { h.s.Tick(false); if (h.s.hbOut.endedN) endAt = i; }
          Check(kept && endAt == 15 && h.s.hbOut.ended[0].end == HB::End::Quiet && h.s.hbOut.ended[0].fought, "fighting beside its attacker keeps it on (6 s, no new hit); the keep time runs from its last swing; said as fought"); }
    }
    {   // the host takes few moves a tick and Step's come first: a fighter with no room is said so and goes next
        Sim s; s.via = Sim::Via::None; HB hb;
        s.Tick();
        const Vec2 spot = s.c.tracked[0].slotPos, foe{spot.x, spot.y + 12};
        const HB::Hit hits[2] = {{200, Foe, foe, s.now}, {201, Foe + 1, foe, s.now}};
        size_t e = s.stepEmitted;
        auto o = hb.Step(s.c, s.units, s.now, true, 25, 4000, 0, hits, {}, std::span(s.moves.data(), e), e);   // a full move list
        Check(o.startedN == 2 && o.sent == 0 && o.unsent == 2 && o.started[0].walk == 2 && o.started[1].walk == 2 && e == s.stepEmitted,
              "no room in the move list: both take their attacker, no walk sent, said so (not sent: 2)");
        uint32_t first = 0, second = 0;
        s.now += 400; e = s.stepEmitted;
        o = hb.Step(s.c, s.units, s.now, true, 25, 4000, 0, {}, {}, std::span(s.moves.data(), s.stepEmitted + 1), e);   // room for one
        if (e == s.stepEmitted + 1) first = s.moves[s.stepEmitted].unitId;
        const bool one = o.sent == 1 && o.unsent == 1;
        s.now += 400; e = s.stepEmitted;
        o = hb.Step(s.c, s.units, s.now, true, 25, 4000, 0, {}, {}, std::span(s.moves.data(), s.stepEmitted + 1), e);
        if (e == s.stepEmitted + 1) second = s.moves[s.stepEmitted].unitId;
        Check(one && o.sent == 1 && first && second && first != second && (first == 200 || first == 201) && (second == 200 || second == 201),
              "room for one a tick: the two take turns (the cursor), neither waits behind the other");
        HitSim h;
        Check(h.s.hbOut.started[0].walk == 1 && h.s.hbOut.unsent == 0, "room: its walk was sent at once");
        HitSim beside(1.0f);
        Check(beside.s.hbOut.startedN == 1 && beside.s.hbOut.started[0].walk == 0 && beside.s.hbOut.sent == 0, "already beside its attacker: no walk needed");
    }
    {   // Retry at the mark's rule beside an attacker too
        HitSim h; h.s.hbRetry = 1000;
        int retried = 0, sent = 0;
        for (int i = 0; i < 28; ++i) { if (i % 8 == 0) h.s.hbHits.push_back({200, Foe, h.foe, h.s.now}); h.s.Tick(); retried += h.s.hbOut.retried; sent += h.s.hbOut.sent; }
        Check(retried >= 1 && sent == retried && Dist(h.s.units[0].pos, h.foe) <= xhunt::MeleeArrive + xhunt::ArriveSlack,
              "standing idle beside its attacker for Retry at the mark's time: sent a short step again (beside it nothing else is sent)");
        HitSim n;
        int none = 0;
        for (int i = 0; i < 28; ++i) { if (i % 8 == 0) n.s.hbHits.push_back({200, Foe, n.foe, n.s.now}); n.s.Tick(); none += n.s.hbOut.retried; }
        Check(none == 0, "Retry at the mark Off: none");
    }
    {   // A Skeleton Mage goes to its stand-off, not into melee
        Sim s; s.via = Sim::Via::None; HB hb; s.hitBack = &hb;
        const Vec2 spot = s.c.tracked[4].slotPos, foe{spot.x, spot.y + 24};
        Check(s.units[4].unitId == 204 && s.units[4].petType == 5, "(the test's mage 204)");
        for (int i = 0; i < 16; ++i) { if (i % 8 == 0) s.hbHits.push_back({204, Foe, foe, s.now}); s.Tick(); }
        const float d = Dist(s.units[4].pos, foe);
        Check(d <= xhunt::RangedArrive && d >= xhunt::RangedKeep - 1.5f, "a mage hit from 24 sub-tiles walks to its stand-off from the attacker");
    }
    {   // Under a hold order (Hold here, Move and hold: the commander's anchor: the hold wins, and it must
        // walk back to its held spot when the fight ends): the anchor is never touched, and the summon ends in its held spot
        Sim s; s.via = Sim::Via::None; HB hb; s.hitBack = &hb;
        const Vec2 held{110, 110};
        s.c.ApplyCommanderSkill(cmd::SkillHoldGround, held);
        for (int i = 0; i < 40; ++i) s.Tick();
        const Vec2 spot = s.c.tracked[0].slotPos, foe{spot.x, spot.y + 20};
        Check(s.c.anchorActive && Dist(spot, held) < 10.0f && Dist(s.units[0].pos, spot) < 2.5f && s.c.tracked[0].inSlot, "(the army holds the spot, summon 200 in its place there)");
        bool anchored = true;
        for (int i = 0; i < 16; ++i) {
            if (i % 8 == 0) s.hbHits.push_back({200, Foe, foe, s.now});
            s.Tick();
            anchored = anchored && s.c.anchorActive && s.c.anchorPos == held;
        }
        uint32_t pets[cmd::MaxArmyUnits], foes[cmd::MaxArmyUnits];
        Check(hb.Fighting(pets, foes, cmd::MaxArmyUnits) == 1 && Dist(s.units[0].pos, foe) <= xhunt::MeleeArrive, "holding a spot, hit from 20 sub-tiles: it still goes for its attacker");
        s.hbFoes = {{Foe, foe, false}};   // the attacker dies
        s.Tick();
        const bool over = s.hbOut.endedN == 1 && s.hbOut.ended[0].end == HB::End::Gone;
        s.speed = 1.0f;
        bool back = false;
        for (int i = 0; i < 32; ++i) { s.Tick(); back = back || s.InMoves(200); anchored = anchored && s.c.anchorActive && s.c.anchorPos == held; }
        Check(over && back && Dist(s.units[0].pos, spot) < 2.5f && s.c.tracked[0].inSlot && Dist(s.c.tracked[0].slotPos, spot) < 0.01f && s.relocations == 0,
              "the fight over: Step walks it back to its held spot (the same spot as before), never teleported");
        Check(anchored, "the hold's anchor is never touched, before, during or after the fight");
    }
    for (int on = 0; on < 2; ++on) {   // Off with hits, and On with none: exactly the commander's
        Sim s; s.via = Sim::Via::None; HB hb; s.hitBack = &hb; s.hbOn = on != 0;
        Sim up; up.via = Sim::Via::None;
        bool same = true;
        for (int i = 0; i < 24; ++i) {
            if (!on) s.hbHits.push_back({200, Foe, {100, 115}, s.now});
            s.Tick(); up.Tick();
            same = same && s.emitted == up.emitted && s.emitted == s.stepEmitted && s.hbOut.startedN == 0 && s.hbOut.endedN == 0 && s.hbOut.engaged == 0;
            for (size_t m = 0; m < s.emitted; ++m) same = same && s.moves[m].unitId == up.moves[m].unitId && s.moves[m].targetX == up.moves[m].targetX && s.moves[m].targetY == up.moves[m].targetY;
            for (size_t k = 0; k < s.units.size(); ++k)
                same = same && s.units[k].pos == up.units[k].pos && s.c.tracked[k].slotPos == up.c.tracked[k].slotPos && s.c.tracked[k].stuckSinceMs == up.c.tracked[k].stuckSinceMs &&
                       s.c.tracked[k].farSinceMs == up.c.tracked[k].farSinceMs && s.c.tracked[k].lastMoveDispatchMs == up.c.tracked[k].lastMoveDispatchMs && s.c.tracked[k].inSlot == up.c.tracked[k].inSlot;
        }
        Check(same, on ? "Fight back when hit On with no hit: Step's moves, the spots, the timers and every position exactly the commander's, tick by tick"
                       : "Fight back when hit Off, hits coming: Step's moves, the spots, the timers and every position exactly the commander's, tick by tick");
    }
}

// The refused-move seam's trampoline for the tests: the call the handler made, answered 7
struct MoveCall { int calls = 0; uintptr_t game = 0, unit = 0, target = 0; int mode = 0, x = -1, y = -1, path = 0, flags = 0; } g_moved;
int FakeMove(uintptr_t game, uintptr_t unit, uintptr_t target, int mode, int x, int y, uint8_t path, uint16_t flags) {
    g_moved = {g_moved.calls + 1, game, unit, target, mode, x, y, int(path), int(flags)};
    return 7;
}
// A handler Sim like Ready's whose last army tick sits `spare` more ticks behind the real clock: that many army ticks can follow
// with the seams still reading a fresh roster (one ahead of the real clock, or older than GapMs, counts for nothing). mark 0: none.
void ReadyBehind(Sim& s, uint32_t mark, Vec2 at, int ticks, int spare) {
    s.via = Sim::Via::Handler;
    s.now = GetTickCount64() - 500 - uint64_t(ticks + spare) * 250;
    if (mark) s.c.SetPriorityTarget(mark, at);
    for (int i = 0; i < ticks; ++i) s.Tick();
}
void TestFarMarkHandlers() {
    const cfg::Rules d;
    Check(d.farRetryS == 1 && d.farRefuse && d.hitBack && d.hitBackReach == 25 && d.hitBackForS == 4,
          "the rules as shipped: a retry after 1 s, refused moves answered failed, a hit summon fights back (reach 25, kept 4 s)");
    g_menuOn = true; cfg::rules.hunt = 2;
    const Vec2 at{100, 112};
    {   // Retry at the mark, through the handler
        g_lines.clear();
        cfg::rules.farRetryS = 1; cfg::rules.hitBack = false;
        Sim s; Ready(s, MarkId + 100, at, 30);
        const auto besideMoves = [&](int ticks) {   // moves for the melee hunters, all beside the mark by now
            int n = 0;
            for (int i = 0; i < ticks; ++i) { s.Tick(); for (size_t k = 0; k < 5; ++k) n += s.InMoves(s.units[k == 4 ? 6 : k].unitId); }
            return n;
        };
        Check(besideMoves(12) > 0, "Retry at the mark: a melee hunter standing idle beside the mark is sent again");
        cfg::rules.farRetryS = 0;
        for (int i = 0; i < 5; ++i) s.Tick();
        Check(besideMoves(12) == 0, "Retry at the mark off: no move for a melee hunter beside the mark");
        Check(g_lines.empty(), "a retry logs nothing");
    }
    {   // A hunter's refused move beside its mark is answered as failed, through the refused-move handler
        g_lines.clear();
        cfg::rules.farRetryS = 0; cfg::rules.hitBack = false; cfg::rules.farRefuse = true;
        const uint32_t mark = MarkId + 110;
        Sim s; s.blocked.assign(s.units.size(), false); s.blocked[3] = true;   // 203 cannot walk: a hunter still on its way
        ReadyBehind(s, mark, at, 30, 3);
        unsigned fails = 0;
        const auto refused = [&](uint32_t id, int mode = 2) { int ret = 1; armyxhunt::Refused({id, mode, 100, 101, ret}); fails += ret == 0; return ret; };
        Check(refused(200) == 0, "a hunter beside its mark: the game is answered failed, not done");
        Check(refused(777) == 1 && refused(203) == 1, "not a hunter, or a hunter still on its way: the swallow, as before");
        cfg::rules.farRefuse = false;
        Check(refused(200) == 1, "the rule off: the swallow");
        cfg::rules.farRefuse = true;
        Check(refused(206) == 0 && refused(204, 15) == 0, "a Golem, a Skeleton Mage beside the mark: failed too");
        g_menuOn = false;
        Check(refused(200) == 1, "the menu on no button: the swallow");
        g_menuOn = true;
        for (int i = 0; i < 6; ++i) s.Tick();
        Check(fails == 3 && g_lines.empty(), "three answers failed in all; none of them logged");
    }
    {   // Fight back when hit, through the seams: the to-hit roll, the army tick, the target hook, the look at the attacker
        g_lines.clear();
        cfg::rules.farRetryS = 0; cfg::rules.hitBack = true;
        cfg::rules.hitBackReach = cfg::Rules{}.hitBackReach; cfg::rules.hitBackForS = cfg::Rules{}.hitBackForS;
        g_farGame = 0x1000; g_farHero = 0x2000;
        Sim s; ReadyBehind(s, 0, {}, 3, 4);
        const Vec2 spot = s.c.tracked[0].slotPos;
        const float sx = std::floor(spot.x), sy = std::floor(spot.y);
        FakeUnit pet(1, 200), pet1(1, 201), archer(1, 5000, sx, sy + 15), archer2(1, 5001, sx, sy + 12), archer3(1, 5002, sx, sy + 10);
        g_farUnits[5000] = archer.at(); g_farUnits[5001] = archer2.at(); g_farUnits[5002] = archer3.at();
        const auto target = [](uint32_t id, bool managed = true, uint32_t pri = 0) { uint32_t t = pri; armyxhunt::Target({id, managed, t}); return t; };
        Check(target(200) == 0, "no hit yet: the target hook hands nothing of ours");
        armyxhunt::Roll({pet1.at(), pet.at(), 0});   // a summon of the army "rolls" on 200: no attacker
        s.Tick();
        Check(target(200) == 0 && s.emitted == s.stepEmitted, "a roll by one of the army's own summons: nothing");
        armyxhunt::Roll({archer.at(), pet.at(), 0});
        s.Tick();
        Check(target(200) == 5000, "the target hook: that summon is handed its attacker (the commander then looks it up and hands it only alive)");
        Check(target(201) == 0 && target(777) == 0 && target(200, false) == 0 && target(200, true, 77) == 77,
              "another summon, a stranger, an untracked unit: nothing; a priority target of the army stays (the mark comes first)");
        Check(s.InMoves(200) && Dist(s.c.tracked[0].slotPos, Vec2{sx, sy + 15}) < 0.01f && s.c.ShouldAllowSummonCombatTarget(200, s.units[0].pos, {sx, sy + 15}),
              "it is sent toward the attacker's place as the roll read it, and the commander's attack gate allows it");
        armyxhunt::Sample();
        archer.path[1] = uint16_t(sx + 6);   // it moves; the next look reads its new place
        armyxhunt::Sample();
        s.Tick();
        Check(Dist(s.c.tracked[0].slotPos, Vec2{sx + 6, sy + 15}) < 0.01f && target(200) == 5000, "the look each game tick: the attacker's place follows it");
        g_farUnits.erase(5000);
        armyxhunt::Sample();
        s.Tick();
        Check(target(200) == 0 && s.emitted == s.stepEmitted, "the attacker gone from the game: over; the target hook hands nothing again; Step's moves untouched");
        armyxhunt::Roll({archer2.at(), pet.at(), 0});
        s.Tick();
        const bool again = target(200) == 5001;
        const uint32_t dead = 12;
        std::memcpy(archer2.b + 0xc, &dead, 4);
        armyxhunt::Sample();
        s.Tick();
        Check(again && target(200) == 0,
              "hit again by another (in the same clock step as the first roll: a hit is told by its count, not its time): taken on; that one dead (its mode): over");
        armyxhunt::Roll({archer3.at(), pet.at(), 0});
        s.Tick();
        const bool third = target(200) == 5002;
        cfg::rules.hitBack = false;
        armyxhunt::Roll({archer3.at(), pet1.at(), 0});   // Off: not recorded
        s.Tick();
        Check(third && target(200) == 0 && s.emitted == s.stepEmitted, "the fight back rule off mid-fight: over; nothing handed, Step's moves untouched");
        bool untouched = true;
        for (int i = 0; i < 4; ++i) { s.Tick(); untouched = untouched && s.emitted == s.stepEmitted; }
        Check(target(201) == 0 && untouched, "off: no target, no move changed (the roll made while off counted for nothing)");
        Check(g_lines.empty(), "a fight back logs nothing, from the first hit to its end");
    }
    {   // Many summons hit at once: each its own attacker
        g_lines.clear();
        cfg::rules.hitBack = true;
        Sim s; ReadyBehind(s, 0, {}, 3, 2);
        const Vec2 spot = s.c.tracked[0].slotPos;
        FakeUnit archer(1, 5010, std::floor(spot.x), std::floor(spot.y) + 10);
        std::vector<std::unique_ptr<FakeUnit>> pets;
        for (uint32_t id = 200; id < 207; ++id) { pets.push_back(std::make_unique<FakeUnit>(1, id)); armyxhunt::Roll({archer.at(), pets.back()->at(), 0}); }
        s.Tick();
        const auto handed = [](uint32_t want) { int n = 0; for (uint32_t id = 200; id < 207; ++id) { uint32_t t = 0; armyxhunt::Target({id, true, t}); n += t == want; } return n; };
        Check(handed(5010) == 7, "seven hit at once: all seven are handed the attacker");
        for (int i = 0; i < 4; ++i) s.Tick();
        s.c.SetPriorityTarget(MarkId + 130, FarMark);
        s.Tick();
        Check(handed(0) == 7, "a mark: every one of them lets go (the hunt has the army)");
        Check(g_lines.empty(), "none of it logged");
        s.c.ClearPriorityTarget();
        cfg::rules.hitBack = false;
        s.Tick(); s.Tick();
    }
    g_farUnits.clear(); g_farGame = g_farHero = 0;
    cfg::rules = cfg::Rules{}; g_menuOn = false;
}

}  // namespace

int main() {
    cfg::rules = cfg::Rules{};
    TestWhoHunts();
    TestUpstreamIgnoresFarMark();
    TestHuntReachesFarMark();
    TestDeathRegroupsAndReturns();
    TestOrderAndNewMarkEnd();
    TestMeleeOnlyKeepsMagesUpstream();
    TestWatchdogQuietWhileHunting();
    TestUnreachableGivesUp();
    TestDetourNotGivenUp();
    TestNoDistanceCutoff();
    TestDefend();
    TestOffIsUpstream();
    TestHandler();
    TestHandlerGiveUp();
    TestRunRules();
    TestLeashRule();
    TestDefendRelease();
    TestHandlerLeashRunAsk();
    TestRunRead();
    TestRunApproachEngine();
    TestRunApproachHandler();
    TestFarMarkRetryAndRefusedRules();
    TestFarMarkRetryEngine();
    TestHitBack();
    TestFarMarkHandlers();
    std::printf("army-x-hunt-test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
