#pragma once
// Army X menu: the summons hunt a marked monster. Pure rules; the glue is vr/army_x_hunt.cpp.
// Before: a priority target (the tactical chord's, or the menu's Attack my target) only re-aimed the army. The coordinator's
// Step kept every summon on its leash round the hero, so a mark more than a few sub-tiles away was never reached.
//   - Who hunts: every tracked summon of a known army type (companions::ArmyTypeMask): melee summons walk beside the mark,
//     Skeleton Mages to their stand-off (Mode::All; Mode::Melee leaves the mages where they are).
//   - How: each tick, a hunter's walk back to its formation spot is dropped from Step's moves and a move to the mark goes
//     into the same list; the leash is lifted for the mark only. A swing in progress is never sent a new move.
//   - The end: the mark dead or lost (the coordinator's own liveness check clears it: Regroup, back to formation), a new
//     order, a new mark, or no progress: a hunter that gets no new best distance for HunterStallMs gives up alone, and the
//     hunt ends once every hunter gave up or NoProgressMs passed with no new best and no fight. No distance ends a hunt.
//   - The game's own leash (DetourRelocateUnit) does not pull a hunter back (JudgeLeash): released at once by any order, a
//     changed mark, Fall out, town or an area change.
//   - Run: a summon whose MonStats2 mode mask has the run bit runs (Runs; the wolves always, Necromancer summons never: they
//     have no run animation and would slide). A hunting runner seen walking on its way is sent again, running.
//   - Beside the mark: a hunter standing idle for the retry time is sent a short step beside it, so the game's AI thinks
//     for it again (RetryGoal); its own move the companion gate refuses is answered "failed", not "done" (JudgeRefused).
//   - Defend: a held spot's nearest monster within the radius becomes the priority target (Defend), hunted the same way.
//   - Fight back when hit: with no mark set, a summon a monster rolls to hit goes for that monster (HitBack), within a reach
//     of its spot, until it dies, runs or a time passes with no new hit and no fight.
// No attack is ever started here: the game's AI swings once a summon stands beside its target.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include "companions.h"
#include "summon_commander.h"

namespace xhunt {
namespace cmd = framevr::commander;
using Vec2 = cmd::Vec2;
using Coord = cmd::SummonArmyCoordinator;

enum class Mode : uint8_t { Off = 0, Melee = 1, All = 2 };   // cfg::Rules::hunt
inline Mode ModeOf(float v) { return v >= 1.5f ? Mode::All : v >= .5f ? Mode::Melee : Mode::Off; }

inline constexpr float MeleeKeep = 1.5f;       // sub-tiles from the mark a melee hunter is sent to (beside it)
inline constexpr float MeleeArrive = 3.0f;     // within this: arrived, the game's AI swings
inline constexpr float RangedKeep = 10.0f;     // a ranged hunter is sent this far from the mark
inline constexpr float RangedArrive = 14.0f;   // within this: arrived
inline constexpr float ArriveSlack = 1.0f;     // an arrived hunter is re-sent once the mark is this much past its arrive distance
inline constexpr float StepMax = 16.0f;        // one move's longest leg (DispatchServerMoveToCoord's own waypoint, 16 sub-tiles)
inline constexpr float MarkMoved = 2.0f;       // the mark moved this far since a hunter's last move: a new one (after MinResendMs)
inline constexpr uint64_t MinResendMs = Coord::MinRedispatchMs;   // the commander's per-unit cooldown between moves (300 ms)
inline constexpr uint64_t RefreshMs = 1200;    // a hunter's move again after this anyway (Step's own same-destination hold)
inline constexpr uint64_t GapMs = 2000;        // no call for this long (a load, the army gone): armed again, nothing carried over
inline constexpr uint64_t ReturnWatchMs = 10000;
// Giving up:
// progress is a new best distance to the mark (ProgressStep under the best so far, walk jitter aside), or a fight beside it. A
// detour round a wall that first leads away is no loss: only NoProgressMs with no new best by anyone ends the hunt.
inline constexpr float ProgressStep = 0.5f;
inline constexpr uint64_t HunterStallMs = 15000;   // one hunter with no new best this long gives up (back to its spot)
inline constexpr uint64_t NoProgressMs = 15000;    // no hunter with a new best this long: the hunt gives up
// A hunter whose distance to the mark changes this much between two ticks was moved, not walked (the game's own leash relocates a
// summon far from the hero, DetourRelocateUnit, vetoed for a hunter only while Hold the leash is On): only logged.
inline constexpr float JumpCells = 10.0f;
inline constexpr float TraceNear = 3.0f;           // the hunt's hunters: within this of the mark (MeleeArrive)
static_assert(HunterStallMs <= NoProgressMs, "a hunter never gives up before the hunt would");
// The arrive distances sit inside the commander's own gates, so an arrived hunter's attack is never refused by them: melee inside the
// attack gate's self-defense radius, ranged inside the native-move gate's mage window [RangedMinStandOff, 18].
static_assert(MeleeArrive < Coord::SelfDefenseRadius, "a melee hunter arrives inside the commander's self-defense radius");
static_assert(RangedKeep >= Coord::RangedMinStandOff && RangedArrive + ArriveSlack <= 18.0f, "a ranged hunter stays in the commander's mage window");

inline bool Ranged(cmd::SummonRole r) { return r == cmd::SummonRole::RangedArtillery; }
// Whether a tracked summon hunts the mark under this mode: a known army type only (unknown unchanged), ranged only in All.
inline bool Hunts(Mode m, uint8_t petType, cmd::SummonRole role) {
    if (m == Mode::Off || (framevr::companions::Bit(petType) & framevr::companions::ArmyTypeMask) == 0) return false;
    return !Ranged(role) || m == Mode::All;
}
inline constexpr uint8_t WalkMode = 2, RunMode15 = 15;   // MONMODE_WALK, MONMODE_RUN (unit +0xC)
inline bool Walking(uint8_t mode) { return mode == WalkMode || mode == RunMode15; }   // MONMODE_WALK, run
// the hunt and its friends: a log row holds about 597 characters of text (steamvr/logging.cpp Row.text[640] less
// its prefix). Every line the hunt builds stays within LineMax: a header within HeaderMax, each hunter's clause within ClauseMax
// (cut there, never past it), and hunters packed into a line only while it fits (the next starts a new line).
inline constexpr int LineMax = 590, HeaderMax = 170, ClauseMax = LineMax - HeaderMax;
static_assert(HeaderMax + ClauseMax <= LineMax && ClauseMax >= 300, "a header and one whole clause always fit a line");
// A ServerMoveUnit (0x4a8a10) mode that is an attack or a skill: the commander's own isCombatAction set in DetourServerMoveUnit
// (attack 1 and 2, cast, skills 1-4). The hunt counts these; nothing here makes one.
inline bool AttackMode(int mode) { return mode == 4 || mode == 5 || mode == 7 || (mode >= 8 && mode <= 11); }
// The hunt swing trace: a mode the game asks its mode setter for (melee.cpp SetMode, 0x34e5f0, which the commander hooks
// to time monster attack starts for the parry), as counted per hunter: attack 1 or 2, a skill (S1-S4), a cast, a get-hit.
enum class Swing : uint8_t { None, Attack, Skill, Cast, GetHit };
inline Swing SwingOf(int mode) {
    return mode == 4 || mode == 5 ? Swing::Attack : mode >= 8 && mode <= 11 ? Swing::Skill : mode == 7 ? Swing::Cast : mode == 3 ? Swing::GetHit : Swing::None;
}
// Where a hunter at `unit` is sent for a mark at `mark`: beside it (melee) or its stand-off (ranged), on the line from the mark to
// the hunter, at most StepMax from the hunter.
inline Vec2 Goal(Vec2 unit, Vec2 mark, bool ranged) {
    const float dx = unit.x - mark.x, dy = unit.y - mark.y, d = std::hypot(dx, dy);
    const float ux = d > 1e-3f ? dx / d : 1.0f, uy = d > 1e-3f ? dy / d : 0.0f;
    const float keep = ranged ? RangedKeep : MeleeKeep;
    Vec2 g{mark.x + ux * keep, mark.y + uy * keep};
    const float gx = g.x - unit.x, gy = g.y - unit.y, gl = std::hypot(gx, gy);
    if (gl > StepMax) g = {unit.x + gx / gl * StepMax, unit.y + gy / gl * StepMax};
    return g;
}

// Retry at the mark: a hunter beside the mark that only stands (not walking, not in a fight mode) since idleSinceMs.
inline bool RetryDue(uint64_t retryMs, bool arrived, uint8_t mode, uint64_t nowMs, uint64_t idleSinceMs) {
    return retryMs > 0 && arrived && !cmd::IsFightMode(mode) && !Walking(mode) && idleSinceMs != 0 && nowMs >= idleSinceMs &&
           nowMs - idleSinceMs >= retryMs;
}
// Where a retried hunter is sent: still at its keep distance from the mark, RetryStep round it from where it stands (left or
// right, by turns), so the move is a real walk of a few sub-tiles and never a step onto its own cell.
inline constexpr float RetryStep = 2.5f;
inline Vec2 RetryGoal(Vec2 unit, Vec2 mark, bool ranged, bool left) {
    const float dx = unit.x - mark.x, dy = unit.y - mark.y, d = std::hypot(dx, dy);
    const float ux = d > 1e-3f ? dx / d : 1.0f, uy = d > 1e-3f ? dy / d : 0.0f;
    const float keep = ranged ? RangedKeep : MeleeKeep;
    const float a = std::min(RetryStep / keep, 1.5708f) * (left ? 1.0f : -1.0f);
    const float co = std::cos(a), si = std::sin(a);
    return {mark.x + (ux * co - uy * si) * keep, mark.y + (ux * si + uy * co) * keep};
}
// A summon's own move the companion gate refused: swallowed as before (the game is told the move was made, and nothing
// starts), or answered "the move failed" (true), so the game's AI thinks again. Only for a hunter of the hunt under way that
// stands beside its mark (away: still on its way; there our own moves drive it).
inline bool JudgeRefused(bool refuseOn, bool hunter, bool away) { return refuseOn && hunter && !away; }

enum class End : uint8_t { None, NewMark, NewOrder, Gone, Off, Gap, Stalled };

// What one call did, for the handler's lines.
struct Tick {
    bool started = false;           // a hunt began on `mark`
    uint32_t mark = 0;              // the hunt's monster now (0: none)
    int hunters = 0, melee = 0, ranged = 0, kept = 0;   // this call's roster (kept: tracked, not hunting)
    float nearest = 0, farthest = 0, heroDist = 0;      // hunters' distances to the mark, the hero's (sub-tiles)
    int arrivedNew = 0, arrived = 0;                    // reached it for the first time this call / arrived now
    int sent = 0, dropped = 0;      // hunt moves added / Step's moves back dropped, this call
    int gaveUp = 0, gaveUpNew = 0;  // hunters that gave up (HunterStallMs), all / this call
    int runAgain = 0;               // Run the whole approach: hunt moves sent because a runner was seen walking (else not due yet)
    int retried = 0;                // Retry at the mark: arrived hunters standing idle sent again this call (counted in sent too)
    uint32_t retryId = 0; float retryDist = 0;   // the first of them, and how far it stood from the mark
    int jumped = 0;                 // hunters whose distance to the mark jumped by JumpCells since the last call (moved, not walked)
    uint32_t jumpId = 0; float jumpFrom = 0, jumpTo = 0;   // the first of them
    // Hunters that left the army mid-hunt (, the evening audit's anomaly: a Shadow cast again is a new unit, and the end
    // line listed only the new one): this call's, the first of them and how near it got. Bookkeeping only: the glue says them
    // only with the hunt On, and every other field of this struct is as it was before.
    int left = 0; uint32_t leftId = 0; float leftBest = 0; bool leftReached = false;
    // The hunt: each hunter (not given up) within TraceNear of the mark this call, its type and mode as sampled
    struct Beside { uint32_t id = 0; uint8_t petType = 0, mode = 0; float dist = 0; };
    std::array<Beside, cmd::MaxArmyUnits> beside{};
    int besideN = 0;
    // A hunt that ended this call
    End end = End::None;
    uint32_t endedMark = 0;
    uint64_t endedAfterMs = 0;
    int endedHunters = 0, endedReached = 0, endedFought = 0;
    int endedLeft = 0, endedLeftReached = 0, endedLeftFought = 0;   // those that left mid-hunt and never came back (not in the three above)
    // Each former hunter of the ended hunt (the proof they went): its distance to the mark at the start, its best, at the end;
    // fought: seen in a fight beside it; gaveUp: it gave up alone (HunterStallMs).
    struct HunterSum { uint32_t id = 0; float start = 0, best = 0, end = 0; bool fought = false, gaveUp = false; };
    std::array<HunterSum, cmd::MaxArmyUnits> summary{};
    int summaryN = 0;
    bool regrouped = false;         // End::Gone: the coordinator's Regroup was given
    // The return after an ended hunt, when it finished this call
    bool returned = false, returnTimedOut = false;
    uint32_t returnMark = 0;
    int returnFormed = 0, returnOf = 0;
    uint64_t returnMs = 0;
};

class Hunt {
public:
    // One call per army tick, right after the coordinator's Step: units are Step's samples (c.tracked[i] is units[i]); moves[0..emitted)
    // are Step's moves, edited in place (capacity moves.size()). Off with no hunt: returns at once, nothing touched.
    // runners (Run the whole approach; empty: Off): runners[i] non-zero when units[i]'s moves run, so a hunter of it seen walking
    // (the pet AI's own walk replaced our run) is due again after MinResendMs instead of RefreshMs.
    Tick Step(Coord& c, std::span<const cmd::ArmyUnitSample> units, Vec2 hero, uint64_t nowMs, Mode mode,
              std::span<cmd::ArmyMoveOrder> moves, size_t& emitted, std::span<const uint8_t> runners = {}, uint64_t retryMs = 0) {
        Tick t;
        const bool gap = lastMs_ != 0 && (nowMs < lastMs_ || nowMs - lastMs_ > GapMs);
        lastMs_ = nowMs;
        if (gap || mode == Mode::Off) {
            if (mark_) EndAs(t, gap ? End::Gap : End::Off, nowMs);
            n_ = backN_ = 0; mark_ = 0;
            return t;
        }
        const uint32_t mark = c.priorityTargetId;
        const Vec2 at = c.priorityTargetPos;
        if (mark_ && mark != mark_) {
            // An order: Step stamped it this tick, or it came after this tick's Step (the X menu's own, given on this seam before the
            // hunt: stamped by the next Step)
            const End why = mark ? End::NewMark : (c.orderedAtMs == nowMs || c.orderStampPending) ? End::NewOrder : End::Gone;
            EndAs(t, why, nowMs);
            if (why == End::Gone && !c.anchorActive && !c.freeRoam) t.regrouped = c.ApplyCommanderSkill(cmd::SkillRegroup, hero);
            backN_ = 0;
            if (why == End::NewOrder || t.regrouped) {   // the return: each former hunter until it is formed
                for (size_t i = 0; i < n_; ++i) back_[backN_++] = h_[i].id;
                backFromMs_ = nowMs; backMark_ = t.endedMark;
            }
            n_ = 0; mark_ = 0;
        }
        if (!mark) { WatchReturn(t, c, nowMs); return t; }
        if (!mark_) { mark_ = mark; startMs_ = progressMs_ = nowMs; n_ = 0; cursor_ = 0; backN_ = 0; t.started = true; goneN_ = 0; }
        t.mark = mark_;
        t.heroDist = std::hypot(hero.x - at.x, hero.y - at.y);

        // The roster: tracked summons that hunt, their state carried by id; each one's leash lifted for the mark (not once it
        // gave up: then Step walks it back to its spot and the watchdog sees it again).
        std::array<Hunter, cmd::MaxArmyUnits> next{};
        std::array<bool, cmd::MaxArmyUnits> want{}, again{}, retry{};
        std::array<size_t, cmd::MaxArmyUnits> index{};
        size_t n = 0;
        bool progress = false;   // a hunter got ProgressStep closer, or fights beside the mark
        const size_t count = std::min({c.trackedCount, units.size(), cmd::MaxArmyUnits});
        t.nearest = 1e9f;
        for (size_t i = 0; i < count; ++i) {
            auto& tu = c.tracked[i];
            const auto& u = units[i];
            if (tu.unitId != u.unitId) continue;
            if (!Hunts(mode, u.petType, tu.role)) { ++t.kept; continue; }
            Hunter h{}; h.id = u.unitId;
            bool known = false;
            for (size_t k = 0; k < n_; ++k) if (h_[k].id == u.unitId) { h = h_[k]; known = true; break; }
            const bool ranged = Ranged(tu.role);
            const float d = std::hypot(u.pos.x - at.x, u.pos.y - at.y);
            const float arrive = ranged ? RangedArrive : MeleeArrive;
            if (!known) { h.start = h.best = d; h.progressMs = nowMs; Returned(h.id); }
            else if (std::fabs(d - h.last) > JumpCells && !t.jumped++) { t.jumpId = h.id; t.jumpFrom = h.last; t.jumpTo = d; }   // moved, not walked
            h.last = d;
            if (d <= arrive + ArriveSlack && cmd::IsFightMode(u.animMode)) h.fought = true;
            if (!h.gaveUp) {
                const bool fighting = d <= arrive && cmd::IsFightMode(u.animMode);
                if (d < h.best - ProgressStep || fighting) { progress = true; h.progressMs = nowMs; }
                h.best = std::min(h.best, d);
                if (d <= arrive) h.progressMs = nowMs;   // beside it: never given up alone (the hunt's own clock still runs)
                else if (nowMs - h.progressMs >= HunterStallMs) { h.gaveUp = true; ++t.gaveUpNew; }
            }
            index[n] = i;
            if (h.gaveUp) { ++t.gaveUp; want[n] = false; next[n++] = h; continue; }   // its Step move and its watchdog kept
            tu.slotPos = at;                                // the commander's gates measure from the mark this tick
            tu.farSinceMs = tu.stuckSinceMs = 0;            // the watchdog never moves it back mid-hunt
            tu.lastMoveDispatchMs = 0; tu.inSlot = false;   // Step's move back is dropped below: its record cleared
            ++(ranged ? t.ranged : t.melee);
            t.nearest = std::min(t.nearest, d); t.farthest = std::max(t.farthest, d);
            if (d <= TraceNear) t.beside[size_t(t.besideN++)] = {u.unitId, u.petType, u.animMode, d};
            if (d <= arrive) {
                if (!h.arrived && !h.reached) ++t.arrivedNew;
                h.arrived = h.reached = true;
            } else if (d > arrive + ArriveSlack) {
                h.arrived = false;
            }
            t.arrived += h.arrived;
            const bool due = !h.sent || nowMs - h.sentMs >= RefreshMs ||
                             (nowMs - h.sentMs >= MinResendMs &&
                              (std::hypot(at.x - h.sentFor.x, at.y - h.sentFor.y) > MarkMoved || !Walking(u.animMode)));
            // Run the whole approach: a runner the AI slowed to a walk gets our run again (its mode as sampled)
            const bool runAgain = !due && i < runners.size() && runners[i] && u.animMode == WalkMode && nowMs - h.sentMs >= MinResendMs;
            want[n] = !h.arrived && !cmd::IsFightMode(u.animMode) && (due || runAgain);
            again[n] = want[n] && runAgain;
            // Retry at the mark: beside the mark and only standing since idleMs: sent again after retryMs (0: never)
            if (h.arrived && !cmd::IsFightMode(u.animMode) && !Walking(u.animMode)) { if (!h.idleMs) h.idleMs = nowMs; } else h.idleMs = 0;
            retry[n] = RetryDue(retryMs, h.arrived, u.animMode, nowMs, h.idleMs);
            want[n] = want[n] || retry[n];
            next[n++] = h;
        }
        t.hunters = int(n) - t.gaveUp;
        if (!t.hunters) t.nearest = 0;
        if (progress) progressMs_ = nowMs;
        // no hunter left, or none with progress for NoProgressMs: given up (with no hunters at all, none of this: the
        // summons that hunt are not out, the commander's priority target only re-aims them, as before).
        NoteLeft(t, next, n);
        if (n > 0 && (t.hunters == 0 || nowMs - progressMs_ >= NoProgressMs)) {
            h_ = next; n_ = n;
            GiveUp(t, c, hero, End::Stalled, nowMs);
            return t;
        }
        // Step's moves for hunters out (its walks back to their spots); every other summon's kept, in order (one that gave up too).
        size_t w = 0;
        for (size_t r = 0; r < emitted && r < moves.size(); ++r) {
            bool hunter = false;
            for (size_t k = 0; k < n && !hunter; ++k) hunter = next[k].id == moves[r].unitId && !next[k].gaveUp;
            if (hunter) ++t.dropped; else moves[w++] = moves[r];
        }
        emitted = w;
        // The hunt moves, round robin from the last call's cursor while there is room.
        size_t sentNow = 0;
        for (size_t s = 0; s < n && emitted < moves.size(); ++s) {
            const size_t k = (cursor_ + s) % n;
            if (!want[k]) continue;
            const auto& tu = c.tracked[index[k]];
            const auto& u = units[index[k]];
            const Vec2 g = retry[k] ? RetryGoal(u.pos, at, Ranged(tu.role), next[k].retryLeft) : Goal(u.pos, at, Ranged(tu.role));
            moves[emitted++] = {u.unitId, u.petType, tu.role, tu.slotIndex, int(std::lround(g.x)), int(std::lround(g.y))};
            next[k].sent = true; next[k].sentMs = nowMs; next[k].sentFor = at;
            ++sentNow;
            t.runAgain += again[k];
            if (retry[k]) {   // its idle clock starts over; the other side next time
                if (!t.retried++) { t.retryId = u.unitId; t.retryDist = std::hypot(u.pos.x - at.x, u.pos.y - at.y); }
                next[k].idleMs = nowMs; next[k].retryLeft = !next[k].retryLeft;
            }
        }
        t.sent = int(sentNow);
        cursor_ = n ? (cursor_ + std::max<size_t>(1, sentNow)) % n : 0;
        h_ = next; n_ = n;
        return t;
    }
    uint32_t Mark() const { return mark_; }
    bool Returning() const { return backN_ != 0; }
    // The hunt's hunters now (not given up; none without a hunt), for the leash veto and the trace: ids into out, the count.
    size_t Hunters(uint32_t* out, size_t cap) const {
        size_t n = 0;
        for (size_t i = 0; mark_ && i < n_ && n < cap; ++i) if (!h_[i].gaveUp) out[n++] = h_[i].id;
        return n;
    }
    // A hunter of the hunt under way still on its way (not given up, not arrived): Run the whole approach's turn of the AI's walks.
    bool Away(uint32_t id) const {
        for (size_t i = 0; mark_ && i < n_; ++i) if (h_[i].id == id) return !h_[i].gaveUp && !h_[i].arrived;
        return false;
    }

private:
    struct Hunter {
        uint32_t id = 0;
        bool arrived = false;   // inside its arrive distance (with ArriveSlack): no moves
        bool reached = false;   // has arrived once this hunt
        bool sent = false;
        uint64_t sentMs = 0;
        Vec2 sentFor{};         // the mark's place its last move was for
        float best = 0;         // its nearest to the mark this hunt
        uint64_t progressMs = 0;   // when it last got ProgressStep closer, fought beside it, or stood beside it
        bool gaveUp = false;    // no progress for HunterStallMs: back to its spot for the rest of this hunt
        float start = 0, last = 0;   // its distance to the mark when the hunt first saw it / now (the end line)
        bool fought = false;    // seen in a fight mode beside the mark (within its arrive distance + ArriveSlack)
        uint64_t idleMs = 0;    // Retry at the mark: beside the mark and only standing since then (0: not)
        bool retryLeft = false; // its next retry step goes left round the mark (by turns)
    };
    // the hunt ends as if the mark were gone: the commander's priority target let go (so it is not hunted again at once),
    // Regroup with no anchor and no Fall out, and the return watch for every former hunter.
    void GiveUp(Tick& t, Coord& c, Vec2 hero, End why, uint64_t nowMs) {
        t.started = false;   // begun and given up in one call (too far at once): only the end is said
        EndAs(t, why, nowMs);
        c.ClearPriorityTarget();
        if (!c.anchorActive && !c.freeRoam) t.regrouped = c.ApplyCommanderSkill(cmd::SkillRegroup, hero);
        backN_ = 0;   // the return watch: hunters that were still trying (one that gave up has the watchdog back already)
        for (size_t i = 0; i < n_; ++i) if (!h_[i].gaveUp) back_[backN_++] = h_[i].id;
        backFromMs_ = nowMs; backMark_ = t.endedMark;
        n_ = 0; mark_ = 0;
    }
    // Hunters of the last call missing from this call's roster (died, unsummoned, or cast again as a new unit): counted for the
    // end line (they hunted, and may have reached and fought), the first one handed out for its own line.
    void NoteLeft(Tick& t, const std::array<Hunter, cmd::MaxArmyUnits>& next, size_t n) {
        for (size_t k = 0; k < n_; ++k) {
            bool still = false;
            for (size_t i = 0; i < n && !still; ++i) still = next[i].id == h_[k].id;
            if (still) continue;
            if (goneN_ < gone_.size()) gone_[goneN_++] = {h_[k].id, h_[k].reached, h_[k].fought};
            if (!t.left++) { t.leftId = h_[k].id; t.leftBest = h_[k].best; t.leftReached = h_[k].reached; }
        }
    }
    // A hunter seen again after it left (the army step skipped it for a tick: its sample and its tracked slot disagreed): it is a
    // hunter again, counted once, as the one it is now.
    void Returned(uint32_t id) {
        for (size_t k = 0; k < goneN_; ++k) if (gone_[k].id == id) { gone_[k] = gone_[--goneN_]; return; }
    }
    void EndAs(Tick& t, End why, uint64_t nowMs) {
        t.end = why; t.endedMark = mark_;
        t.endedAfterMs = nowMs >= startMs_ ? nowMs - startMs_ : 0;
        t.endedHunters = int(n_);
        t.endedLeft = int(goneN_);
        for (size_t k = 0; k < goneN_; ++k) { t.endedLeftReached += gone_[k].reached; t.endedLeftFought += gone_[k].fought; }
        for (size_t i = 0; i < n_; ++i) {
            t.endedReached += h_[i].reached;
            t.endedFought += h_[i].fought;
            t.summary[i] = {h_[i].id, h_[i].start, h_[i].best, h_[i].last, h_[i].fought, h_[i].gaveUp};
        }
        t.summaryN = int(n_);
    }
    // After an ended hunt (no mark now): the former hunters' watchdog timers kept at zero until each is formed, or ReturnWatchMs.
    void WatchReturn(Tick& t, Coord& c, uint64_t nowMs) {
        if (!backN_) return;
        int present = 0, formed = 0;
        for (size_t b = 0; b < backN_; ++b)
            for (size_t i = 0; i < c.trackedCount && i < cmd::MaxArmyUnits; ++i) {
                auto& tu = c.tracked[i];
                if (tu.unitId != back_[b]) continue;
                ++present;
                if (tu.formed) ++formed; else tu.farSinceMs = tu.stuckSinceMs = 0;
                break;
            }
        const uint64_t ms = nowMs >= backFromMs_ ? nowMs - backFromMs_ : 0;
        if (present == formed || ms >= ReturnWatchMs) {
            t.returned = true; t.returnTimedOut = formed < present;
            t.returnMark = backMark_; t.returnFormed = formed; t.returnOf = present; t.returnMs = ms;
            backN_ = 0;
        }
    }
    uint32_t mark_ = 0;
    uint64_t startMs_ = 0, lastMs_ = 0, progressMs_ = 0;
    std::array<Hunter, cmd::MaxArmyUnits> h_{};
    size_t n_ = 0, cursor_ = 0;
    std::array<uint32_t, cmd::MaxArmyUnits> back_{};
    size_t backN_ = 0;
    uint64_t backFromMs_ = 0;
    uint32_t backMark_ = 0;
    struct Gone { uint32_t id = 0; bool reached = false, fought = false; };
    std::array<Gone, cmd::MaxArmyUnits> gone_{};   // hunters that left the army during this hunt and are not back (NoteLeft, Returned)
    size_t goneN_ = 0;
};

// ---- Defend the spot ----
// What the commander's Hold Ground does (code read, summon_commander.h / companion_ai.cpp): AnchorAt centers the formation on
// the spot and holds each summon to the Defensive leash from its own spot (7.5 sub-tiles melee, 3.5 mages). The attack gate lets a
// summon hit a monster within 7.5 of its spot (mages 20) or within 3.8 of itself; the native-move gate counts a monster as an enemy
// only while coordinator.inCombat, which is "hostiles within 22 sub-tiles of the HERO". So with the hero away from the spot a melee
// summon may not walk even 4 sub-tiles to a monster (only moves back toward its spot pass) and hits only what touches it; the commander
// has no sentry scan for summons (the mercenary has one: g_activeHoldTargetId, HostilesAround round the anchor).
// The rule here, only for an anchor the X menu set (Hold here, Move and hold; the game's Hold Ground button and the tactical chord
// unchanged): while that anchor holds and nothing is focused, the monster nearest the spot within the radius becomes the army's
// priority target (SetPriorityTarget: the coordinator's own focus); the hunt above walks the hunters to it (the hunt mode's
// mode: Off, no defense); when it dies the commander's liveness check clears it and, the anchor still on, Step walks them back to their
// spots; then the next one. A defended monster that runs DefendSlack past the radius is let go (the summons go back).
// A monster the hunt gave up on (unreachable: its focus cleared while it lives)
// is not picked again until the spot changes (Own) or GivenUpMs; F2, "another order took the anchor" never clears the priority
// target (the only order that keeps one is an Attack on that very monster); S1/S2, the scan is not made here (under g_tacticalLock):
// the glue scans on GameTick, no lock held, from a summon near the spot, and hands the result in.
struct Foe { uint32_t id = 0; float x = 0, y = 0; };
inline constexpr float DefendSlack = 6.0f;
inline constexpr int DefendScanMax = 16;          // as the mercenary's hold sentry scans (HostilesAround, 16)
inline constexpr uint64_t GivenUpMs = 20000;      // a monster the hunt gave up on is skipped this long (or until a new spot)
// The monster to defend against (-1 none): nearest to the spot within radius, never skip (0: none); current (its id) kept while
// it is within radius.
inline int DefendPick(const Foe* f, int n, Vec2 spot, float radius, uint32_t current, uint32_t skip = 0) {
    int best = -1; float bestD = 0;
    for (int i = 0; i < n; ++i) {
        if (!f[i].id || (skip && f[i].id == skip)) continue;
        const float d = std::hypot(f[i].x - spot.x, f[i].y - spot.y);
        if (d > radius) continue;
        if (f[i].id == current) return i;
        if (best < 0 || d < bestD) { best = i; bestD = d; }
    }
    return best;
}
class Defend {
public:
    enum class Act : uint8_t { None, Picked, Lost, LetGo, Ended };
    // gaveUp: a monster found alive in this scan after its focus was cleared (the hunt gave up on it): skipped from now (logged).
    struct Out { Act act = Act::None; uint32_t id = 0; float dist = 0; int seen = 0; uint32_t gaveUp = 0; };
    // The X menu just set the anchor at spot (the coordinator's anchorPos as set): a new spot, nothing skipped.
    void Own(Vec2 spot) { owned_ = true; spot_ = spot; defending_ = 0; lost_ = 0; skip_ = 0; }
    bool Owns() const { return owned_; }
    Vec2 Spot() const { return spot_; }
    uint32_t Defending() const { return defending_; }
    uint32_t Skipping(uint64_t nowMs) const { return skip_ && nowMs < skipUntil_ ? skip_ : 0; }
    // Each army tick, before the hunt (Moves, under g_tacticalLock, no game call): radius 0 or hunting false (Hunt the
    // mark Off): no defense (a monster it set is let go). foes / n: the last scan round the spot (n < 0: none fresh: no pick).
    Out Step(Coord& c, float radius, bool hunting, uint64_t nowMs, const Foe* foes, int n) {
        Out o;
        if (!owned_) return o;
        if (!c.anchorActive || std::hypot(c.anchorPos.x - spot_.x, c.anchorPos.y - spot_.y) > .05f) {   // another order took the anchor
            owned_ = false; o.act = Act::Ended; o.id = defending_;   // its priority target is that order's (F2): never cleared here
            defending_ = lost_ = skip_ = 0;
            return o;
        }
        if (defending_ && c.priorityTargetId != defending_) {   // died, lost, or the hunt gave up on it (the next scan tells)
            o.act = Act::Lost; o.id = defending_; lost_ = defending_; defending_ = 0;
            return o;
        }
        if (!(radius > 0) || !hunting) {
            if (defending_) { c.ClearPriorityTarget(); o.act = Act::LetGo; o.id = defending_; defending_ = 0; }
            return o;
        }
        if (defending_) {
            o.dist = std::hypot(c.priorityTargetPos.x - spot_.x, c.priorityTargetPos.y - spot_.y);
            if (o.dist > radius + DefendSlack) { c.ClearPriorityTarget(); o.act = Act::LetGo; o.id = defending_; defending_ = 0; }
            return o;
        }
        if (c.priorityTargetId || n < 0 || !foes) return o;   // someone else's focus (theirs), or no fresh scan
        n = std::min(n, DefendScanMax);
        if (lost_) {   // the one let go last: alive in this scan, so not killed: the hunt gave up on it (F1)
            for (int k = 0; k < n; ++k) if (foes[k].id == lost_) { skip_ = lost_; skipUntil_ = nowMs + GivenUpMs; o.gaveUp = lost_; break; }
            lost_ = 0;
        }
        const int i = DefendPick(foes, n, spot_, radius, 0, Skipping(nowMs));
        if (i < 0) return o;
        c.SetPriorityTarget(foes[i].id, {foes[i].x, foes[i].y});
        defending_ = foes[i].id;
        o.act = Act::Picked; o.id = foes[i].id; o.seen = n;
        o.dist = std::hypot(foes[i].x - spot_.x, foes[i].y - spot_.y);
        return o;
    }
    void Forget() { owned_ = false; defending_ = lost_ = skip_ = 0; }
    // Army X menu taken off its button (N3, recommended yes): the X menu's own anchor (Move and hold, or Hold here's spot it
    // claimed) is let go, the coordinator's ClearAnchor (the army re-forms on the hero, as after any order that ends a hold), and
    // forgotten. Another order's anchor (moved, or none) is never touched. True: an anchor was cleared.
    bool Release(Coord& c) {
        const bool ours = owned_ && c.anchorActive && std::hypot(c.anchorPos.x - spot_.x, c.anchorPos.y - spot_.y) <= .05f;
        if (ours) c.ClearAnchor();
        Forget();
        return ours;
    }
private:
    bool owned_ = false;
    Vec2 spot_{};
    uint32_t defending_ = 0, lost_ = 0, skip_ = 0;
    uint64_t skipUntil_ = 0;
};

// ---- Fight back when hit ----
// "Minions should never stand idle getting hit by a mob." What the commander does (summon_commander.h): in Balanced or
// Defensive a summon may attack a monster only within 14 or 7.5 sub-tiles of its formation spot, or within 3.8 of itself, and its
// own moves toward a monster are held to the same leash; so an archer or a shaman can hit it from outside that and it stands.
// The rule: a tracked summon of a known army type that a monster hits (the to-hit roll seam),
// while the army has no priority target (a mark is the hunt's; an order re-forms the army) and it is not already swinging, takes
// that monster as its OWN target: for that summon alone the hunt's own lifts apply (its spot is the attacker's place for
// the commander's gates, Step's walk back dropped, the watchdog held off), it is walked to the attacker by the host's own dispatch
// (as a hunter to a mark), and the glue hands it that monster in the commander's target hook (Target). The others keep
// formation. It ends when the attacker is dead or gone, no new hit came and it did not fight for the keep time, the attacker is
// past the reach from its spot (+ HitBackSlack), any order, a priority target, or Off; then Step walks it back and the watchdog
// stays held off until it stands in its spot again (Step's inSlot; `formed` stays true from the last order, so it says nothing
// here) or ReturnWatchMs. Off: nothing is touched, tick by tick the commander.
inline constexpr float HitBackSlack = 4.0f;   // an attacker this far past the reach is let go (not at the reach's edge: no flutter)
inline bool Swinging(uint8_t mode);   // below (the far mark trace): in an attack, skill or cast
class HitBack {
public:
    enum class End : uint8_t { None, Gone, Quiet, Far, Order, Mark, Off, Left };
    struct Hit { uint32_t pet = 0, foe = 0; Vec2 foePos{}; uint64_t atMs = 0; };       // a monster's to-hit roll on a summon
    struct Foe { uint32_t id = 0; Vec2 pos{}; bool alive = false; };                    // an attacker as the glue last looked it up
    // walk (a start): 0 none needed (it stands beside its attacker), 1 its walk was sent this call, 2 not sent (no room)
    struct Note { uint32_t pet = 0, foe = 0; uint8_t petType = 0; float dist = 0, fromSpot = 0; End end = End::None; uint64_t afterMs = 0; bool fought = false; uint8_t walk = 0; };
    static constexpr size_t Notes = 8;
    struct Out {
        std::array<Note, Notes> started{}, ended{};
        int startedN = 0, endedN = 0;   // this call's (the first Notes of each are listed)
        int engaged = 0, sent = 0, dropped = 0, retried = 0;
        int unsent = 0;   // walks wanted this call that found no room in the move list (they go first next call: the cursor)
    };
    // One call per army tick, after the coordinator's Step (units are Step's samples, c.tracked[i] is units[i]; moves[0..emitted)
    // Step's moves, edited in place). reach <= 0: no limit. hits: the hits since the last call; foes: each fought attacker as
    // last looked up (one not listed keeps its last place).
    Out Step(Coord& c, std::span<const cmd::ArmyUnitSample> units, uint64_t nowMs, bool on, float reach, uint64_t forMs, uint64_t retryMs,
             std::span<const Hit> hits, std::span<const Foe> foes, std::span<cmd::ArmyMoveOrder> moves, size_t& emitted) {
        Out o;
        const bool gap = lastMs_ != 0 && (nowMs < lastMs_ || nowMs - lastMs_ > GapMs);
        lastMs_ = nowMs;
        if (gap) { n_ = backN_ = 0; }
        const End stop = !on ? End::Off : c.priorityTargetId != 0 ? End::Mark : (c.orderedAtMs == nowMs || c.orderStampPending) ? End::Order : End::None;
        if (stop != End::None) {   // nothing of ours from here: the commander's order or the hunt has the army
            for (size_t k = 0; k < n_; ++k) Close(o, e_[k], stop, nowMs, 0, 0);
            n_ = backN_ = 0;
            return o;
        }
        const size_t count = std::min({c.trackedCount, units.size(), cmd::MaxArmyUnits});
        const auto indexOf = [&](uint32_t pet) -> int {
            for (size_t i = 0; i < count; ++i) if (units[i].unitId == pet && c.tracked[i].unitId == pet) return int(i);
            return -1;
        };
        // New hits: a known army type, not swinging, the attacker no summon of ours and within reach of its spot
        for (const Hit& h : hits) {
            if (!h.pet || !h.foe || nowMs < h.atMs || nowMs - h.atMs >= forMs || c.IsTrackedUnit(h.foe)) continue;
            const int i = indexOf(h.pet);
            if (i < 0 || !Hunts(Mode::All, units[size_t(i)].petType, c.tracked[size_t(i)].role)) continue;
            Fight* e = nullptr;
            for (size_t k = 0; k < n_ && !e; ++k) if (e_[k].pet == h.pet) e = &e_[k];
            if (e) { e->lastMs = std::max(e->lastMs, h.atMs); continue; }   // already on an attacker: it keeps that one, its clock renewed
            if (Swinging(units[size_t(i)].animMode) || n_ >= e_.size()) continue;   // it fights already
            const Vec2 spot = c.tracked[size_t(i)].slotPos;
            const float fromSpot = (spot.x != 0 || spot.y != 0) ? std::hypot(h.foePos.x - spot.x, h.foePos.y - spot.y) : 0;
            if (reach > 0 && fromSpot > reach) continue;
            e_[n_++] = {h.pet, h.foe, h.foePos, nowMs, h.atMs};
            if (size_t(o.startedN++) < Notes)
                o.started[size_t(o.startedN - 1)] = {h.pet, h.foe, units[size_t(i)].petType, std::hypot(units[size_t(i)].pos.x - h.foePos.x, units[size_t(i)].pos.y - h.foePos.y), fromSpot};
        }
        // Each summon on its attacker
        std::array<bool, cmd::MaxArmyUnits> want{}, retry{};
        std::array<int, cmd::MaxArmyUnits> index{};
        size_t w = 0;
        for (size_t k = 0; k < n_; ++k) {
            Fight e = e_[k];
            const int i = indexOf(e.pet);
            if (i < 0) { Close(o, e, End::Left, nowMs, 0, 0); continue; }
            auto& tu = c.tracked[size_t(i)];
            const auto& u = units[size_t(i)];
            bool dead = false;
            for (const Foe& f : foes) if (f.id == e.foe) { if (f.alive) e.foePos = f.pos; else dead = true; break; }
            const bool ranged = Ranged(tu.role);
            const float arrive = ranged ? RangedArrive : MeleeArrive;
            const float d = std::hypot(u.pos.x - e.foePos.x, u.pos.y - e.foePos.y);
            const Vec2 spot = tu.slotPos;   // its formation spot, as Step just computed it
            const float fromSpot = (spot.x != 0 || spot.y != 0) ? std::hypot(e.foePos.x - spot.x, e.foePos.y - spot.y) : 0;
            if (d <= arrive + ArriveSlack && cmd::IsFightMode(u.animMode)) { e.fought = true; e.lastMs = std::max(e.lastMs, nowMs); }
            const End end = dead ? End::Gone : (reach > 0 && fromSpot > reach + HitBackSlack) ? End::Far :
                            (nowMs >= e.lastMs && nowMs - e.lastMs >= forMs) ? End::Quiet : End::None;
            if (end != End::None) {
                Close(o, e, end, nowMs, d, fromSpot);
                if (backN_ < back_.size()) back_[backN_++] = {e.pet, nowMs};   // the walk back: its watchdog held off until it is formed
                continue;
            }
            tu.slotPos = e.foePos;                          // the commander's gates measure from its attacker this tick
            tu.farSinceMs = tu.stuckSinceMs = 0;            // the watchdog never moves it back while it fights
            tu.lastMoveDispatchMs = 0; tu.inSlot = false;   // Step's move back is dropped below: its record cleared
            const bool arrived = d <= arrive;
            const bool due = !e.sent || nowMs - e.sentMs >= RefreshMs ||
                             (nowMs - e.sentMs >= MinResendMs && (std::hypot(e.foePos.x - e.sentFor.x, e.foePos.y - e.sentFor.y) > MarkMoved || !Walking(u.animMode)));
            if (arrived && !cmd::IsFightMode(u.animMode) && !Walking(u.animMode)) { if (!e.idleMs) e.idleMs = nowMs; } else e.idleMs = 0;
            retry[w] = RetryDue(retryMs, arrived, u.animMode, nowMs, e.idleMs);
            want[w] = (!arrived && !cmd::IsFightMode(u.animMode) && due) || retry[w];
            index[w] = i;
            e_[w++] = e;
        }
        n_ = w;
        o.engaged = int(n_);
        // Step's moves for those summons out (its walks back to their spots); every other summon's kept, in order
        size_t keep = 0;
        for (size_t r = 0; r < emitted && r < moves.size(); ++r) {
            bool ours = false;
            for (size_t k = 0; k < n_ && !ours; ++k) ours = e_[k].pet == moves[r].unitId;
            if (ours) ++o.dropped; else moves[keep++] = moves[r];
        }
        emitted = keep;
        // Round robin from the last call's cursor while there is room (the host takes few moves a tick and Step's come
        // first, so with no room a fighter's walk waits; the cursor gives it the next free place, never the same one twice)
        std::array<bool, cmd::MaxArmyUnits> went{};
        size_t sentNow = 0;
        for (size_t s = 0; s < n_ && emitted < moves.size(); ++s) {
            const size_t k = (cursor_ + s) % n_;
            if (!want[k]) continue;
            const auto& tu = c.tracked[size_t(index[k])];
            const auto& u = units[size_t(index[k])];
            const Vec2 g = retry[k] ? RetryGoal(u.pos, e_[k].foePos, Ranged(tu.role), e_[k].retryLeft) : Goal(u.pos, e_[k].foePos, Ranged(tu.role));
            moves[emitted++] = {u.unitId, u.petType, tu.role, tu.slotIndex, int(std::lround(g.x)), int(std::lround(g.y))};
            e_[k].sent = true; e_[k].sentMs = nowMs; e_[k].sentFor = e_[k].foePos;
            went[k] = true; ++sentNow;
            ++o.sent;
            if (retry[k]) { ++o.retried; e_[k].idleMs = nowMs; e_[k].retryLeft = !e_[k].retryLeft; }
        }
        cursor_ = n_ ? (cursor_ + std::max<size_t>(1, sentNow)) % n_ : 0;
        for (size_t k = 0; k < n_; ++k) {
            if (want[k] && !went[k]) ++o.unsent;
            for (int q = 0; q < o.startedN && q < int(Notes); ++q)
                if (o.started[size_t(q)].pet == e_[k].pet) o.started[size_t(q)].walk = !want[k] ? 0 : went[k] ? 1 : 2;
        }
        // The walk back after a fight: the watchdog held off until the summon is in its spot, or ReturnWatchMs (as the hunt's return)
        size_t b = 0;
        for (size_t k = 0; k < backN_; ++k) {
            bool again = false;
            for (size_t q = 0; q < n_ && !again; ++q) again = e_[q].pet == back_[k].pet;
            const int i = again ? -1 : indexOf(back_[k].pet);
            if (i < 0 || c.tracked[size_t(i)].inSlot || nowMs - back_[k].sinceMs >= ReturnWatchMs) continue;
            c.tracked[size_t(i)].farSinceMs = c.tracked[size_t(i)].stuckSinceMs = 0;
            back_[b++] = back_[k];
        }
        backN_ = b;
        return o;
    }
    // The summons on an attacker now: pets and their attackers into out (capacity cap), the count.
    size_t Fighting(uint32_t* pets, uint32_t* foes, size_t cap) const {
        size_t n = 0;
        for (size_t k = 0; k < n_ && n < cap; ++k) { pets[n] = e_[k].pet; foes[n++] = e_[k].foe; }
        return n;
    }
    static const char* EndText(End e) {
        switch (e) {
            case End::Gone: return "the attacker is dead or gone";
            case End::Quiet: return "no new hit and no fight for the keep time";
            case End::Far: return "the attacker is past its reach from its spot";
            case End::Order: return "an order (the commander re-forms the army)";
            case End::Mark: return "the army has a mark or a priority target now";
            case End::Off: return "Fight back when hit is Off";
            case End::Left: return "the summon left the army";
            default: return "";
        }
    }

private:
    struct Fight {
        uint32_t pet = 0, foe = 0;
        Vec2 foePos{};
        uint64_t startMs = 0, lastMs = 0;   // when it began; the last hit on it or the last time it fought beside the attacker
        bool sent = false; uint64_t sentMs = 0; Vec2 sentFor{};
        uint64_t idleMs = 0; bool retryLeft = false;
        bool fought = false;
    };
    struct Back { uint32_t pet = 0; uint64_t sinceMs = 0; };
    void Close(Out& o, const Fight& e, End why, uint64_t nowMs, float d, float fromSpot) {
        if (size_t(o.endedN++) < Notes)
            o.ended[size_t(o.endedN - 1)] = {e.pet, e.foe, 0, d, fromSpot, why, nowMs >= e.startMs ? nowMs - e.startMs : 0, e.fought};
    }
    std::array<Fight, cmd::MaxArmyUnits> e_{};
    size_t n_ = 0, cursor_ = 0;
    std::array<Back, cmd::MaxArmyUnits> back_{};
    size_t backN_ = 0;
    uint64_t lastMs_ = 0;
};

// ---- Hold the leash ----
// DetourRelocateUnit (RelocateUnit 0x491ca0) is a general teleport, not only the leash: a portal, a
// waypoint or an area change may move pets through it too. "recall brings minions back, town portal brings
// minions back": the veto has no distance limit by default (Leash reach 0), and it ends at once on any recall or order (Regroup,
// a formation, Hold, Fall out: the coordinator's order stamp since the last Step), a changed or cleared mark, Hunt the mark or Hold
// the leash Off, and on any area change, town or unknown area (the game brings them along). The leash roster (the hunters it may
// keep) is picked each army tick: within Leash reach of the hero when one is set, the hero's level known, not a town, the same level
// as the last publish (a level change drops it for that tick). The hunt itself has no distance limit either.
inline constexpr int TownLevels[5] = {1, 40, 75, 103, 109};   // the five act towns
inline bool TownLevel(int level) { for (int t : TownLevels) if (t == level) return true; return false; }
enum class LeashWhy : uint8_t { Held, NoLevel, Town, LevelChanged };
struct LeashPick { size_t n = 0; int beyond = 0; LeashWhy why = LeashWhy::Held; };
// hunters ids[0..count) at pos[0..count); the leash roster into out (capacity count). reach <= 0: no distance limit. lastLevel: the
// level of the last publish.
inline LeashPick PickLeash(float reach, int level, int lastLevel, const uint32_t* ids, const Vec2* pos, size_t count, Vec2 hero, uint32_t* out) {
    LeashPick p;
    if (!level) { p.why = LeashWhy::NoLevel; return p; }
    if (TownLevel(level)) { p.why = LeashWhy::Town; return p; }
    if (lastLevel && level != lastLevel) { p.why = LeashWhy::LevelChanged; return p; }
    p.why = LeashWhy::Held;
    for (size_t i = 0; i < count; ++i) {
        if (!(reach > 0) || std::hypot(pos[i].x - hero.x, pos[i].y - hero.y) <= reach) out[p.n++] = ids[i];
        else ++p.beyond;
    }
    return p;
}
// The game's own relocation of this summon (DetourRelocateUnit): Held (skipped), or why not. Checked at the relocation itself, so
// each release is at once, not at the next army tick.
enum class LeashRelease : uint8_t {
    Held,
    Off,          // Hold the leash Off (or Army X menu, Summons, Ideas Off)
    NotHunter,    // not in the leash roster (not hunting, gave up, beyond Leash reach), or no fresh roster
    HuntOff,      // Hunt the mark Off
    FallOut,      // Fall out
    Order,        // an order since the last army tick: Regroup (the X menu's or the commander's), a formation, Hold
    MarkChanged,  // the army's priority target is no longer the hunt's mark (killed, lost, cleared, another mark)
    NoArea,       // the hero's area not known now
    Town,         // the hero is in town (a Town Portal, a waypoint to town)
    AreaChanged,  // the hero's area is not the roster's (a portal, waypoint or stairs since the last army tick)
};
struct LeashNow {
    bool on = false, hunting = false;   // Hold the leash, Hunt the mark (not Off)
    uint32_t unit = 0;
    uint64_t nowMs = 0, atMs = 0;       // now, the roster's publish
    int levelNow = 0, levelAt = 0;      // the hero's level now, the roster's
    bool orderPending = false, freeRoam = false;
    uint32_t priorityTarget = 0, mark = 0;   // the coordinator's now, the roster's hunt
};
inline LeashRelease JudgeLeash(const LeashNow& s, const uint32_t* ids, size_t n) {
    if (!s.on) return LeashRelease::Off;
    if (!s.unit || !n || !ids || !s.atMs || s.nowMs < s.atMs || s.nowMs - s.atMs > GapMs) return LeashRelease::NotHunter;
    bool listed = false;
    for (size_t i = 0; i < n && !listed; ++i) listed = ids[i] == s.unit;
    if (!listed) return LeashRelease::NotHunter;
    if (!s.hunting) return LeashRelease::HuntOff;
    if (s.freeRoam) return LeashRelease::FallOut;
    if (s.orderPending) return LeashRelease::Order;
    if (!s.mark || s.priorityTarget != s.mark) return LeashRelease::MarkChanged;
    if (!s.levelNow) return LeashRelease::NoArea;
    if (TownLevel(s.levelNow)) return LeashRelease::Town;
    if (s.levelNow != s.levelAt) return LeashRelease::AreaChanged;
    return LeashRelease::Held;
}
inline const char* LeashReleaseText(LeashRelease r) {
    switch (r) {
        case LeashRelease::HuntOff: return "Hunt the mark is Off";
        case LeashRelease::FallOut: return "Fall out";
        case LeashRelease::Order: return "a new order since the last army tick (Regroup, a formation, Hold)";
        case LeashRelease::MarkChanged: return "the mark is no longer the army's target (killed, lost, cleared or another mark)";
        case LeashRelease::NoArea: return "your area is not known now";
        case LeashRelease::Town: return "you are in town (a Town Portal or waypoint)";
        case LeashRelease::AreaChanged: return "your area changed (a portal, waypoint or stairs)";
        default: return "";
    }
}

// ---- Summons run ----
// The commander sends every army move walking: the Necromancer's skeletons, mages and golems have no run animation (monstats2 mRN empty)
// and slid run-moved (owner). The rule reads each summon's own game data: the MonStats2 row's mode mask (+0xF0, one bit
// per mode, as melee.cpp ModeMask reads it for hit reactions: bit 3 GetHit, bit 13 knockback), so bit 15 is RN, the run mode and
// bit 2 WL, the walk mode (a mask without a walk is no read: every army summon walks).
enum class RunMode : uint8_t { Off = 0, Wolves = 1, Runners = 2 };   // cfg::Rules::run
inline RunMode RunModeOf(float v) { return v >= 1.5f ? RunMode::Runners : v >= .5f ? RunMode::Wolves : RunMode::Off; }
inline constexpr uint32_t WalkModeBit = 1u << 2, RunModeBit = 1u << 15;
inline constexpr bool NecroType(uint8_t t) { return t >= 3 && t <= 6; }   // Golem, Skeletons, Mages, Revives
inline constexpr bool WolfType(uint8_t t) { return t == 11 || t == 12; }  // Spirit wolves, Dire wolves
inline uint32_t ModesRead(uint32_t mask) { return (mask & WalkModeBit) ? mask : 0; }   // 0: unread
// True: this move runs. Off, a Necromancer summon, or a type outside the army: walk (as before). A mask read without a run: walk
// (it would slide). Wolves: wolves run (their mask unread too). Every runner: wolves, and the
// Valkyrie, Grizzly and Shadows only with a run read in their own data (unread: walk, never guessed).
inline bool Runs(RunMode m, uint8_t petType, uint32_t modes) {
    if (m == RunMode::Off || NecroType(petType) || (framevr::companions::Bit(petType) & framevr::companions::ArmyTypeMask) == 0) return false;
    modes = ModesRead(modes);
    if (modes && !(modes & RunModeBit)) return false;
    if (WolfType(petType)) return true;
    return m == RunMode::Runners && (modes & RunModeBit) != 0;
}
// In an attack, skill or cast: a swing in progress is never sent a new move.
inline bool Swinging(uint8_t mode) { const Swing s = SwingOf(mode); return s == Swing::Attack || s == Swing::Skill || s == Swing::Cast; }
// Each pet's slot for what the to-hit hook records about it, kept by its unit id for as long as it is sampled (a slot by
// roster position would lose its record whenever a summon died or was cast). who[s]: the unit id slot s holds (0: free).
// ids[0..n): this tick's pets. out[i]: ids[i]'s slot (its own, else a free one; -1: none left); fresh[i]: newly its own.
inline void AssignSlots(const uint32_t* ids, size_t n, const uint32_t* who, size_t slots, int* out, bool* fresh) {
    std::array<bool, cmd::MaxArmyUnits> taken{};
    slots = std::min(slots, taken.size());
    for (size_t i = 0; i < n; ++i) {
        out[i] = -1; fresh[i] = false;
        for (size_t s = 0; ids[i] && s < slots; ++s) if (who[s] == ids[i] && !taken[s]) { out[i] = int(s); taken[s] = true; break; }
    }
    size_t next = 0;
    for (size_t i = 0; i < n; ++i) {
        if (out[i] >= 0 || !ids[i]) continue;
        while (next < slots && taken[next]) ++next;
        if (next >= slots) break;
        out[i] = int(next); fresh[i] = true; taken[next] = true;
    }
}
inline const char* PetName(uint8_t t) {
    switch (t) {
        case 2: return "Valkyrie"; case 3: return "Golem"; case 4: return "Skeleton"; case 5: return "Skeleton Mage"; case 6: return "Revive";
        case 11: return "Spirit wolf"; case 12: return "Dire wolf"; case 15: return "Grizzly"; case 16: return "Shadow";
        default: return "summon";
    }
}
inline const char* ModeName(uint8_t m) {
    static constexpr const char* names[16] = {"death", "standing", "walking", "hit", "attack 1", "attack 2", "block", "cast",
                                              "skill 1", "skill 2", "skill 3", "skill 4", "dead", "knock-back", "sequence", "running"};
    return m < 16 ? names[m] : "other";
}
}  // namespace xhunt
