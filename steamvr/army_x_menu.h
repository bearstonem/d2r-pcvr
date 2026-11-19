#pragma once
// Army X menu: a cross of summon orders on one held hand button. Pure rules and layout; the glue is vr/army_x_menu.cpp.
// Hold the button: four circles appear in front of you (Attack my target up, Defend me down, Regroup left, Hold here
// right). Move the hand into one and its orders open beside it; let go on a circle or an order to give it; let go in the
// center to give nothing. The pointer is the hand's movement since the press, in the head's frame at the press.
// A circle, once open, stays open until the pointer is back in the center zone (Tuning::viaCenter).
// Orders go through the commander's own paths: its commander skills (formations, Regroup, Hold ground, Fall out), and
// for Attack my target and Move and hold the coordinator's priority target and anchor, summons only.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "interactions.h"
#include "skill_slots.h"
#include "summon_commander.h"
#include "target_cone.h"
#include "toss.h"

namespace framevr::xmenu {

// The bindable item's id on a hand button (as the commander's Draw glyph is GlyphDrawId 9000): never a
// game skill, outside the commander range (3901-3907) and below 4095 is not needed: it is never
// written to the game's bindings (the button's game skill is cleared, as for Draw glyph).
constexpr int ItemId = 9001;
// Its icon's key in the commander's icon cache ("s" + the id, as every skill's): no manifest has it, so the glue's icon maker
// paints it (atlas::BindIcon below) for every painter that asks (the Edit Slots list).
inline constexpr const char* IconKey = "s9001";

// ---- The bind ----
// Army X menu goes on a hand button through the hand wheel: Edit Slots lists it (General / Items, with an army out) and sends
// the commander's own BindSkill with skill ItemId; Take makes it the commander's clear of that button's game skill (as Draw glyph's pick does) and
// remembers it; the reply's Applied makes that slot the X menu's button in that weapon set ([wheel] army_x_menu_button = slot + 1).
struct BindWatch {
    uint64_t seq = 0; int slot = -1;
    enum class Done { None, Applied, Refused };
    // This frame's request: true when it was ours (it is a clear now; the caller logs it).
    bool Take(interaction::Request& r) noexcept {
        if (r.action != interaction::Action::BindSkill || r.skill != ItemId) return false;
        seq = r.sequence; slot = r.index;
        r.skill = -1; r.skillOwner = -1;   // the commander's own clear (interactionstate's BindSkill)
        return true;
    }
    // The latest reply: Applied (slotOut: the button) or Refused once for the request taken, else None.
    Done Reply(const interaction::Reply& reply, int& slotOut) noexcept {
        if (!seq || reply.sequence != seq || reply.result == interaction::Result::Pending || reply.result == interaction::Result::None) return Done::None;
        slotOut = slot;
        const Done d = reply.result == interaction::Result::Applied ? Done::Applied : Done::Refused;
        Forget();
        return d;
    }
    void Forget() noexcept { seq = 0; slot = -1; }
};
// The slot the menu is on, from the stored button (slot + 1; 0: none).
inline int SlotOfButton(float stored, int slots) noexcept { const int b = int(stored + .5f) - 1; return b >= 0 && b < slots ? b : -1; }
// The buttons the menu is on: one a weapon set, as the game keeps each button's skill (Snapshot::bindingBank: 0 is set I,
// 16 set II). A weapon swap shows the other set's buttons, so the menu's button in one set says nothing about the other.
struct Buttons {
    int slot[2] = {-1, -1};
    static constexpr int Set(int bindingBank) noexcept { return bindingBank == 0 ? 0 : bindingBank == 16 ? 1 : -1; }
    int Of(int bindingBank) const noexcept { const int k = Set(bindingBank); return k < 0 ? -1 : slot[k]; }
    bool Any() const noexcept { return slot[0] >= 0 || slot[1] >= 0; }
};
// The Edit Slots entry: one more skill in the wheel engine's catalog while a hand wheel is open with an army out, whether
// the menu is on a button yet or not (this is how a player first binds it). True: added, as the catalog's last entry.
inline bool Offer(interaction::Snapshot& s, int wheelHand) noexcept {
    if (wheelHand < 0 || !interaction::HasSummonArmy(s) || s.skillCount >= s.catalog.size()) return false;
    for (unsigned i = 0; i < s.skillCount && i < s.catalog.size(); ++i) if (s.catalog[i].id == ItemId) return false;
    interaction::Skill item{};
    item.id = ItemId; item.owner = -1; item.level = 1; item.known = item.available = true;
    item.directed = true;   // never in the GLYPHS page's picker (its skills need no target)
    std::snprintf(item.name, sizeof item.name, "Army X menu");
    s.catalog[s.skillCount++] = item;
    return true;
}
inline void Withdraw(interaction::Snapshot& s) noexcept {
    if (s.skillCount > 0 && s.skillCount <= s.catalog.size() && s.catalog[s.skillCount - 1].id == ItemId) s.catalog[--s.skillCount] = {};
}
// The bind from start to end, on the thread that steps the wheel engine: Before its Step, After it.
struct Binder {
    Buttons buttons;
    BindWatch watch;
    int watchSet = -1;            // the weapon set of the bind awaited
    bool listed = false;          // the entry is in the engine's snapshot right now
    double boundAtMs = -1e12;     // the last bind: a snapshot from just before it may still show the skill it cleared
    static constexpr double SettleMs = 2000;
    struct Result {
        bool took = false; int tookSlot = -1, cleared = -1;      // this frame's request was the entry's pick (a clear now)
        BindWatch::Done done = BindWatch::Done::None; int slot = -1, set = -1;   // the game's answer to it
    };
    void Before(interaction::Input& in) noexcept {
        listed = Offer(in.snapshot, in.wheelHand);
    }
    Result After(interaction::Input* in, interaction::Request* request, const interaction::Reply* reply, double nowMs) noexcept {
        Result o;
        if (in && listed) Withdraw(in->snapshot);
        listed = false;
        if (!request || !reply) return o;
        const int set = Buttons::Set(request->bank), cleared = request->previousSkill;
        if (watch.Take(*request)) { watchSet = set; o.took = true; o.tookSlot = request->index; o.cleared = cleared; }
        o.done = watch.Reply(*reply, o.slot);
        if (o.done == BindWatch::Done::None) return o;
        o.set = watchSet; watchSet = -1;
        if (o.done == BindWatch::Done::Applied) {
            if (o.set < 0 || o.slot < 0 || o.slot >= skills::Count) o.done = BindWatch::Done::Refused;   // no weapon set known: nothing kept
            else { buttons.slot[o.set] = o.slot; boundAtMs = nowMs; }
        }
        return o;
    }
    // A game skill or a Draw glyph put on the menu's button, in the weapon set in hand, takes that button back (the other
    // set's button stays). s: a fresh snapshot. Skill or Glyph: unbound now (slotOut, setOut).
    enum class Taken { No, Skill, Glyph };
    Taken Takeover(const interaction::Snapshot& s, const int* glyphDraw, double nowMs, int& slotOut, int& setOut) noexcept {
        const int set = Buttons::Set(s.bindingBank);
        if (set < 0 || nowMs - boundAtMs <= SettleMs) return Taken::No;
        const int was = buttons.slot[set];
        if (was < 0 || was >= skills::Count) return Taken::No;
        const bool skill = s.skills[size_t(was)].known && s.skills[size_t(was)].id >= 0;
        if (!skill && !(glyphDraw && (glyphDraw[0] == was || glyphDraw[1] == was))) return Taken::No;
        buttons.slot[set] = -1; slotOut = was; setOut = set;
        return skill ? Taken::Skill : Taken::Glyph;
    }
};

enum Dir : int { Up = 0, Down = 1, Left = 2, Right = 3, DirCount = 4 };
constexpr int MaxFan = 2;

// "Attack my target": an order only the X menu gives, never a game skill and never in a binding. The menu
// turns it into the summon army's priority target on the monster the holding hand aimed at the press (summons only since
//: the coordinator's ClearAnchor + SetPriorityTarget, the army half of the commander's PrioritizeEnemy, never the mercenary's
// order); with no monster, Spearhead wedge (the Aggressive stance) instead.
constexpr int AttackTarget = 9002;
constexpr int AttackFallback = commander::SkillWedgeAgg;
// "Move and hold": Hold here's second order, also only the X menu's. Let go on it: the hand that held the button
// points at the ground (the laser's floor cell, transport's own mapping: FloorCell below) and its trigger sets the spot; the summon
// army anchors there (SummonArmyCoordinator::AnchorAt, the call the commander's MoveAndHold makes for the army) and holds it. Summons
// only.
constexpr int MoveHold = 9003;

struct Branch {
    const char* name;            // the circle's name (the log)
    int order;                   // the circle's own order: let go on the circle
    int count;                   // its fan's orders (0: none)
    int orders[MaxFan];          // fanned out from it, centered on its direction: slot 0 counter-clockwise, slot 1 clockwise
};
// Changed ("the parent icons aren't usable as a command ... up would be attack my target, down is defend
// me"): every circle is an order of its own, at most two in its fan.
constexpr std::array<Branch, DirCount> Layout{{
    {"Attack my target", AttackTarget,              2, {commander::SkillWedgeAgg, commander::SkillMarchBal}},
    {"Defend me",        commander::SkillRingDef,    1, {commander::SkillPhalanxDef, -1}},
    {"Regroup",          commander::SkillRegroup,    0, {-1, -1}},
    {"Hold here",        commander::SkillHoldGround, 2, {commander::SkillFallOut, MoveHold}},   // Move and hold since
}};
// The circle's outward axis, degrees (x right, y up).
constexpr float AxisDeg(int dir) noexcept {
    return dir == Up ? 90.f : dir == Down ? 270.f : dir == Left ? 180.f : 0.f;
}
// A fan slot's direction from its parent's center: the fan's count orders spreadDeg apart, centered on the axis (one: on
// it; two: half a spread either side, slot 0 counter-clockwise).
constexpr float SlotDeg(int dir, int slot, int count, float spreadDeg) noexcept {
    return AxisDeg(dir) + (float(count - 1) * .5f - float(slot)) * spreadDeg;
}
// The plain order names for the toast and the hover label (the commander's names without "Army: ").
inline const char* OrderLabel(int id) noexcept {
    switch (id) {
        case commander::SkillPhalanxDef: return "Phalanx";
        case commander::SkillRingDef:    return "Protective ring";
        case commander::SkillMarchBal:   return "Marching crescent";
        case commander::SkillWedgeAgg:   return "Spearhead wedge";
        case commander::SkillHoldGround: return "Hold ground";
        case commander::SkillRegroup:    return "Regroup";
        case commander::SkillFallOut:    return "Fall out";
        case AttackTarget:               return "Attack my target";
        case MoveHold:                   return "Move and hold";
        default:                         return "";
    }
}
// A hover's sub past the open circle's reach along its axis (Parent depth): nothing is pointed there.
constexpr int SubPast = -2;
// The order a hover gives on a let go: the pointed fan order, else the open circle's own (-1: the center, or past its reach).
inline int Pointed(int parent, int sub) noexcept {
    if (parent < 0 || parent >= DirCount || sub == SubPast) return -1;
    const Branch& b = Layout[size_t(parent)];
    return sub >= 0 && sub < b.count ? b.orders[sub] : b.order;
}

// ---- Attack my target: the monster the holding hand aims at, by the commander's aim rules (vrcam's hand picks, the outline's) ----
struct Aimed { uint32_t id = 0; float x = 0, y = 0; double atMs = 0; bool eyes = false; };   // id 0: none; x, y: its cells; atMs: when picked; eyes: the eyes' pick
constexpr double AimedFreshMs = 300;   // as old as the aim rules let a hand's pick be (vrcam aimtargets::Resolve)
inline bool Fresh(const Aimed& a, double nowMs) noexcept { return a.id && nowMs >= a.atMs && nowMs - a.atMs <= AimedFreshMs; }
// The press's monster (coordinator, the outline's rule): the holding hand's fresh pick, else the eyes' (eyes is
// vrcam's eyes pick only while the outline would show it, aimrules::EyesOutlined; 0 else), else none.
inline Aimed PickFor(const Aimed& hand, const Aimed& eyes, double nowMs) noexcept {
    if (Fresh(hand, nowMs)) { Aimed a = hand; a.eyes = false; return a; }
    if (Fresh(eyes, nowMs)) { Aimed a = eyes; a.eyes = true; return a; }
    return {};
}
enum class AttackHow { Focus, NoTarget, TacticalOff, Targeting };
// a: the hand's pick copied at the press, nowMs: the press (the same clock); tacticalOn: VR Settings' Tactical commands, the
// switch the commander's priority target needs (companionai::IssueTacticalOrder refuses without it; with it Off the aim rules may
// not even pick, so it is the reason named first). targeting: Attack my target's reach is above 0: with no
// monster outlined the hand picks one (Targeting, below) instead of the wedge; 0: the wedge, as before.
inline AttackHow PlanAttack(const Aimed& a, double nowMs, bool tacticalOn, bool targeting = false) noexcept {
    if (!tacticalOn) return AttackHow::TacticalOff;
    if (Fresh(a, nowMs)) return AttackHow::Focus;
    return targeting ? AttackHow::Targeting : AttackHow::NoTarget;
}

// ---- Attack my target's targeting mode ----
// Let go on Attack my target with no monster outlined: the cross closes and the hand that held the button points a long ray;
// the monster nearest it (by angle, out to the reach) is the candidate, an arrow over it that jumps as the hand sweeps (a
// tick each change), and that hand's trigger locks it. While the mode is on it OWNS that hand's trigger: the trigger's own
// action (attack, skill) never fires, the locking pull included; after the lock or a cancel the trigger is the game's again on
// its next fresh press (one held through the end stays muted until it comes up). Cancel: the grip (a fresh press) or the time
// limit; nothing is ordered then (no wedge in this mode).
constexpr float CatchDeg = 10.f;      // the ray picks within this of a monster's body: the tactical chord's AimDegrees (transport)
constexpr float StickDeg = 1.f;       // the candidate holds until another is this much nearer the ray (no flicker between two)
constexpr float BodyM = 1.f;          // the body point the angle is measured to, above the feet (the chord's degreesOff: 1 m)
constexpr float TriggerDown = .2f;    // the X menu's own press threshold (the bound trigger's: Press, trigger > .2)
constexpr float GripDown = .6f;       // the commander's grip threshold (the tactical chord's, room steps')
// The hostile scan's reach (vr/target_lock.cpp: melee::Hostiles out to aimreach::CornerCells + 4): no monster farther is known,
// so the reach stops there.
constexpr float ScanCells = framevr::aimreach::CornerCells + 4.f;
// transport's cell <-> world mapping (toss::World / Cell, the tactical chord's and the aimed throw's): the stage, the world floor
// under the hero (stage.anchor.y + floor - stage.origin.y, as transport's tossFloor) and stageWorld.upm.
struct WorldMap {
    interaction::Stage stage; float floor = 0, upm = 0; bool known = false;
    bool Valid() const noexcept { return known && stage.known && std::isfinite(upm) && upm >= 1.f && upm <= 12.f; }
};
inline WorldMap MapOf(const interaction::Input& in, float upm) noexcept {
    WorldMap w; w.stage = in.stage; w.upm = upm; w.known = in.floorKnown;
    w.floor = in.stage.anchor.y + (in.floor - in.stage.origin.y);
    return w;
}
// A hand's ray in the world (transport's laser: stage.World(handAim.p), the stage's turn of the hand's -z).
struct WorldRay { math::Vec p, d; };
inline WorldRay RayOf(const WorldMap& w, math::Vec roomP, math::Quat roomQ) noexcept {
    return {w.stage.World(roomP), math::Normalized(math::Rotate(w.stage.base, math::Rotate(roomQ, {0, 0, -1})))};
}
// Where the ray meets the floor, in cells (Move and hold's spot): transport's own laser-to-floor step (resolveHandSubtile and the
// tactical chord's laserGround: the floor plane under the hero, then toss::Cell). false: no mapping, the ray level or rising, or
// the hand under the floor (nothing aimed; never the hero's own cell as resolveHandSubtile's fallback).
inline bool FloorCell(const WorldMap& w, const WorldRay& r, float& x, float& y) noexcept {
    if (!w.Valid() || !(r.d.y < -1e-4f) || !(r.p.y > w.floor)) return false;
    const float k = (w.floor - r.p.y) / r.d.y;
    return toss::Cell(r.p + r.d * k, w.floor, w.upm, x, y);
}
// Move and hold's reach: the commander's own refusal distance for a commanded spot (companionai::ElasticCombatLeashDistance, 40
// sub-tiles from the hero; the tactical chord's red "beyond the leash" ring), passed in by the glue. Past it the spot is refused.
inline bool InReach(float x, float y, float heroX, float heroY, float reach) noexcept {
    return std::hypot(x - heroX, y - heroY) <= reach;
}
// Degrees between the ray and the monster's body at (x, y) cells (180: behind the hand or nonsense).
inline float DegreesOff(const WorldMap& w, const WorldRay& r, float x, float y) noexcept {
    const math::Vec to = toss::World(x, y, w.floor, w.upm) + math::Vec{0, BodyM, 0} - r.p;
    const float len = std::sqrt(math::Dot(to, to));
    if (!(len >= .3f)) return 180.f;
    return std::acos(std::clamp(math::Dot(to, r.d) / len, -1.f, 1.f)) * 57.29578f;
}
// The candidate: the scanned monster nearest the ray within CatchDeg and within reachCells of the hero (straight line, cells);
// current (its id) keeps it unless another is StickDeg nearer. -1: none (no mapping, reach Off, nothing near the ray).
inline int RayPick(const targetcone::Target* m, int n, float heroX, float heroY, const WorldMap& w, const WorldRay& r,
                   float reachCells, uint32_t current) noexcept {
    if (!m || !w.Valid() || !(reachCells > 0)) return -1;
    int best = -1, cur = -1; float bestDeg = CatchDeg, curDeg = 180.f;
    for (int i = 0; i < n; ++i) {
        if (!m[i].id || std::hypot(m[i].x - heroX, m[i].y - heroY) > reachCells) continue;
        const float deg = DegreesOff(w, r, m[i].x, m[i].y);
        if (deg > CatchDeg) continue;
        if (deg < bestDeg || best < 0) { best = i; bestDeg = deg; }
        if (m[i].id == current) { cur = i; curDeg = deg; }
    }
    return cur >= 0 && curDeg <= bestDeg + StickDeg ? cur : best;
}
enum class TargetEnd { None, Locked, Grip, Timeout, Off, Replaced };
struct TargetIn {
    double nowMs = 0; bool on = false;
    float trigger = 0, grip = 0;   // the targeting hand's raw trigger and grip (HandInput), 0-1
    uint32_t candidate = 0;        // RayPick's this frame (0: none)
    float timeoutS = 4;            // the time limit (0: none)
};
struct TargetOut {
    bool mute = false;             // the targeting hand's trigger does nothing else this frame (the glue clears its slot's request)
    bool tick = false;             // a new candidate: a light tick
    bool missed = false;           // a pull with no candidate: swallowed, still aiming (let go to pull again)
    TargetEnd end = TargetEnd::None;
};
struct Targeting {
    bool active = false; int hand = 0; double startMs = 0; uint32_t candidate = 0;
    bool triggerArmed = false, gripArmed = false;   // seen up since the start: only a fresh press counts
    bool waitUp = false;           // after the mode: its trigger stays muted until it comes up (no carried-over press)
    void Start(int h, double nowMs, float trigger, float grip) noexcept {
        *this = {};
        active = true; hand = h; startMs = nowMs;
        triggerArmed = trigger <= TriggerDown; gripArmed = grip <= GripDown;
    }
    bool Busy() const noexcept { return active || waitUp; }
    // Ends the mode now (a new press of the X menu: Replaced); the held trigger stays muted until it comes up.
    void End(float trigger) noexcept { active = false; candidate = 0; waitUp = trigger > TriggerDown; }
    TargetOut Step(const TargetIn& in) noexcept {
        TargetOut o;
        const bool down = in.trigger > TriggerDown;
        if (!active) { if (waitUp && !down) waitUp = false; o.mute = waitUp; return o; }
        o.mute = true;
        const auto finish = [&](TargetEnd e) { End(in.trigger); o.end = e; };
        if (!in.on) { finish(TargetEnd::Off); waitUp = false; o.mute = false; return o; }   // Off: the commander's trigger at once
        if (in.grip <= GripDown) gripArmed = true;
        else if (gripArmed) { finish(TargetEnd::Grip); return o; }
        if (in.candidate != candidate) { o.tick = in.candidate != 0; candidate = in.candidate; }
        if (!down) triggerArmed = true;
        else if (triggerArmed) {
            if (candidate) { finish(TargetEnd::Locked); return o; }
            triggerArmed = false; o.missed = true;
        }
        if (in.timeoutS > 0 && in.nowMs - startMs >= double(in.timeoutS) * 1000.0) { finish(TargetEnd::Timeout); return o; }
        return o;
    }
};
// The press the commander decided this frame (transport's slot, requested and interact, as Press passes them) while the
// mode owns its hand's trigger: that trigger's slot asks for nothing (the X menu's own mute: no cast, no attack, no interact);
// the other hand and that hand's other buttons are untouched.
inline void OwnTrigger(const TargetOut& o, int hand, int slot, int& requested, bool& interact) noexcept {
    if (o.mute && slot == skills::TriggerSlot(hand)) { requested = -1; interact = false; }
}
// After the lock (or an outlined monster at the let go): the arrow stays over it while the commander's priority target is it. It
// ends when the commander drops it (a death, a lost unit, an area change, an order that clears it: the game thread's
// priorityTarget), at a new X menu order, Off, or if the commander never took it within ConfirmMs.
// Stale: the game thread's tick returns early on a load, an unread hero or no game, and then nothing
// updates priorityTarget; with no tick for StaleMs the mark is dropped rather than trusted.
struct Mark {
    static constexpr double ConfirmMs = 1500;   // the game thread ticks about every 160 ms
    static constexpr double StaleMs = 2000;     // no game tick for this long: the priority read is stale (the caller's test)
    uint32_t id = 0; double sinceMs = 0; bool confirmed = false;
    enum class End { None, Dropped, NeverTaken, Stale };
    void Set(uint32_t monster, double nowMs) noexcept { id = monster; sinceMs = nowMs; confirmed = false; }
    void Clear() noexcept { id = 0; confirmed = false; }
    // priority: the game thread's last read; fresh: a game tick wrote it within StaleMs.
    End Step(uint32_t priority, double nowMs, bool fresh = true) noexcept {
        if (!id) return End::None;
        if (!fresh) { Clear(); return End::Stale; }
        if (priority == id) { confirmed = true; return End::None; }
        if (confirmed) { Clear(); return End::Dropped; }
        if (nowMs - sinceMs > ConfirmMs) { Clear(); return End::NeverTaken; }
        return End::None;
    }
};

// Attack my target's hand-off to the game thread (coordinator): the commander's IssueTacticalOrder reads the game's
// server units when no mercenary is cached (melee::LocalServer, HirelingVitals), so it is given where companion_ai's own tick
// runs (CompanionGameThreadTick, the GameTick seam), never from transport's thread. One at a time: transport posts
// it at the let go and polls its outcome each frame; the game thread's Drain is the ONLY place it is given.
// Summons only: since then the game thread's side is the army step's own seam (Moves, under
// g_tacticalLock), which gives it to the summon army's coordinator alone (Focus: ClearAnchor + SetPriorityTarget; Anchor: AnchorAt,
// Move and hold's spot), the calls the commander's IssueTacticalOrder makes for the army, with no mercenary order. That seam runs only
// with an army out and not in Fall out: else it times out (TimeoutMs) and the glue says why.
struct FocusQueue {
    enum State : int { Idle, Queued, Taking, Took, Refused };
    enum class Outcome { None, Took, Refused, TimedOut };
    enum class Kind { Focus, Anchor };          // Attack my target (target: the monster), Move and hold (target: the spot, id 0)
    static constexpr double TimeoutMs = 1000;   // not taken by then (a load, a menu, companion AI idle): canceled, never given
    std::atomic<int> state{Idle};
    Aimed target;          // written before Queued is published, read by Drain after it took it
    Kind kind = Kind::Focus;   // the same
    double postedMs = 0;   // transport's clock
    // Transport, at the let go: false while one is still out (the first stays).
    bool Post(const Aimed& a, double nowMs, Kind k = Kind::Focus) noexcept {
        if (state.load(std::memory_order_acquire) != Idle) return false;
        target = a; kind = k; postedMs = nowMs; quiet = false;
        state.store(Queued, std::memory_order_release);
        return true;
    }
    // The game thread: a queued one given through issue (bool(const Aimed&): the commander's order); true when one was given.
    template <class Issue> bool Drain(Issue&& issue) {
        int s = Queued;
        if (!state.compare_exchange_strong(s, Taking, std::memory_order_acquire)) return false;
        const bool ok = issue(target);
        state.store(ok ? Took : Refused, std::memory_order_release);
        return true;
    }
    // Transport, each frame while Waiting: the outcome once; TimedOut when the game thread had not taken it by TimeoutMs.
    Outcome Poll(double nowMs) noexcept {
        int s = state.load(std::memory_order_acquire);
        if (s == Took || s == Refused) { state.store(Idle, std::memory_order_release); return s == Took ? Outcome::Took : Outcome::Refused; }
        if (s == Queued && nowMs - postedMs > TimeoutMs && state.compare_exchange_strong(s, Idle, std::memory_order_acq_rel)) return Outcome::TimedOut;
        return Outcome::None;
    }
    // Transport, the menu taken off its button: a queued one is dropped (true), never given; one already taken still reports.
    bool Cancel() noexcept { int s = Queued; return state.compare_exchange_strong(s, Idle, std::memory_order_acq_rel); }
    bool Waiting() const noexcept { return state.load(std::memory_order_relaxed) != Idle; }
    // The switch going Off: a queued one is dropped (Dropped); one the game thread is giving right now
    // (Taking) or has given cannot be undone, so it reports quietly (Quiet): at the next poll, after the next On, its outcome is
    // only logged, never a buzz, toast, mark or wedge.
    enum class OffResult { None, Dropped, Quiet };
    bool quiet = false;   // transport's own: the outstanding one reports quietly
    OffResult Off() noexcept {
        if (Cancel()) return OffResult::Dropped;
        if (!Waiting()) return OffResult::None;
        quiet = true;
        return OffResult::Quiet;
    }
    // With an outcome from Poll: whether it reports quietly (once; the next Post starts loud again).
    bool TakeQuiet() noexcept { const bool q = quiet; quiet = false; return q; }
};

// Every number of the cross, in degrees of view unless said. These defaults are the reference layout the tests are
// written against; the mod runs Shipped() below.
struct Tuning {
    float distM = 1.0f;          // the cross's distance from the head at open
    float downDeg = 8.f;         // its center below eye level
    float sideDeg = 3.5f;        // shifted toward the holding hand
    float gainDegPerCm = 1.4f;   // cursor degrees per cm of hand travel
    float clampDeg = 22.f;       // the cursor's radius limit (clamping never cancels)
    float latchMs = 80.f;        // the hand's origin: its average over this long after the press
    float parentDiamDeg = 7.5f, subDiamDeg = 5.5f;
    float parentAtDeg = 9.f;     // parent centers from the cross center
    float fanRadiusDeg = 9.f;    // sub centers from their parent's center
    float fanSpreadDeg = 45.f;   // between neighboring subs (a fan is centered on its parent's direction; Straight only)
    // Child layout (a strong flick up ran past Attack my target into its orders, which sat further
    // along the same axis). true, T: each circle's orders sit square across its axis, fanRadiusDeg either side of its center (slot 0
    // counter-clockwise, slot 1 clockwise; one order counter-clockwise), so nothing lies beyond a circle along its own axis and an
    // overshoot stays on it. false, Straight: the fan outward, as before.
    bool tee = false;
    // Parent depth: an open circle's reach along its own axis, degrees past its edge; farther out nothing is pointed and a let go
    // gives nothing (a release safety for a wild flick). 0: Off, no limit (as before: the circle holds out to the pointer's limit).
    float parentDepthDeg = 0.f;
    float centerInDeg = 3.0f;    // back to the center (all four return) below this
    float parentInDeg = 4.5f;    // a parent opens beyond this radius ...
    float axisInDeg = 50.f;      // ... and within this of its axis
    float axisOutDeg = 65.f;     // the open parent holds until the cursor is this far off its axis
    // Switch arms. true, Back
    // through center: an open circle holds until the pointer is back in the center zone; a move toward another circle changes
    // nothing. false, Anywhere: a clear move (axisOutDeg off its line) opens the other circle, as before.
    bool viaCenter = false;
    float subInDeg = 3.5f, subOutDeg = 4.5f;   // a sub's hit radius in and out
    float pickPastDeg = 3.f;     // past the parent's center by this, the sub is chosen by angle
    float pickAngleDeg = 30.f;   // ... within this of a sub's direction
    float gapWinDeg = 5.f;       // between parent and fan (outside the parent's circle): the nearest sub within this is pointed
    float hoverMs = 30.f, hoverAmp = .3f;     // hover tick (0 ms: none)
    float issueMs = 80.f, issueAmp = .7f;     // the order's firm buzz
    float toastS = 1.f;                       // the order's name, shown this long (0: none)
    float reachCells = ScanCells;             // Attack my target's targeting ray, cells from the hero (0: no targeting mode, the wedge)
    float targetS = 4.f;                      // the targeting mode ends unlocked after this long (0: no limit, only the grip)
};

// The values the mod runs with, as tuned in the headset (Tuning's own defaults are the reference layout the tests are
// written against). Derived: an order holds 1 degree past its reach; the direction pick takes two thirds of the fan's
// spread; the pointer stops 1 degree past the farthest order.
inline Tuning Shipped() noexcept {
    Tuning t;
    t.gainDegPerCm = 0.9f;
    t.parentDiamDeg = 6.f; t.parentAtDeg = 8.f;
    t.tee = true; t.viaCenter = true;
    t.centerInDeg = 3.5f; t.parentInDeg = 4.5f;
    t.gapWinDeg = 3.f;
    t.subInDeg = 2.5f; t.subOutDeg = t.subInDeg + 1.f;
    t.pickPastDeg = 7.f; t.pickAngleDeg = t.fanSpreadDeg * 2.f / 3.f;
    t.clampDeg = t.parentAtDeg + t.fanRadiusDeg + t.subDiamDeg * .5f + 1.f;
    return t;
}

struct Vec2f { float x = 0, y = 0; };
inline float Len(Vec2f v) noexcept { return std::sqrt(v.x * v.x + v.y * v.y); }
inline float AngDiff(float a, float b) noexcept {   // |a-b| in degrees, 0..180
    float d = std::fmod(std::fabs(a - b), 360.f);
    return d > 180.f ? 360.f - d : d;
}
inline float AngOf(Vec2f v) noexcept {
    float a = std::atan2(v.y, v.x) * 57.29578f;
    return a < 0 ? a + 360.f : a;
}
inline Vec2f Polar(float deg, float r) noexcept {
    const float a = deg * 0.01745329f;
    return {std::cos(a) * r, std::sin(a) * r};
}
inline Vec2f ParentCenter(int dir, const Tuning& t) noexcept { return Polar(AxisDeg(dir), t.parentAtDeg); }
// A fan slot's direction from its parent's center: Straight, the fan (SlotDeg); T, square across the parent's axis, slot 0 on the
// counter-clockwise side, slot 1 on the clockwise side (a single order counter-clockwise).
inline float ChildDeg(int dir, int slot, int count, const Tuning& t) noexcept {
    return t.tee ? AxisDeg(dir) + (slot == 0 ? 90.f : -90.f) : SlotDeg(dir, slot, count, t.fanSpreadDeg);
}
inline Vec2f SubCenter(int dir, int slot, const Tuning& t) noexcept {
    const Vec2f p = ParentCenter(dir, t), o = Polar(ChildDeg(dir, slot, Layout[size_t(dir)].count, t), t.fanRadiusDeg);
    return {p.x + o.x, p.y + o.y};
}
// The open circle's reach along its own axis (Parent depth; 0: none, no limit): the pointer farther out than this, measured from
// the cross's center along that axis, points nothing. wasPast: it already did (1 deg of stickiness to come back).
inline bool PastReach(int dir, Vec2f c, bool wasPast, const Tuning& t) noexcept {
    if (t.parentDepthDeg <= 0 || dir < 0 || dir >= DirCount) return false;
    const Vec2f ax = Polar(AxisDeg(dir), 1.f);
    return c.x * ax.x + c.y * ax.y > t.parentAtDeg + t.parentDiamDeg * .5f + t.parentDepthDeg - (wasPast ? 1.f : 0.f);
}

// The cursor from the hand: its movement since the latched origin, in the head-yaw frame at open
// (right: the head's right on the floor plane; up: world up), cm -> degrees, clamped.
inline Vec2f Cursor(const float hand[3], const float origin[3], const float right[3], const Tuning& t) noexcept {
    const float dx = hand[0] - origin[0], dy = hand[1] - origin[1], dz = hand[2] - origin[2];
    Vec2f c{(dx * right[0] + dz * right[2]) * 100.f * t.gainDegPerCm, dy * 100.f * t.gainDegPerCm};
    const float r = Len(c);
    if (t.clampDeg > 0 && r > t.clampDeg) { c.x *= t.clampDeg / r; c.y *= t.clampDeg / r; }
    return c;
}

// Hover: which parent is open (-1: the center, all four shown) and which of its subs is pointed (-1).
struct Hover {
    int parent = -1, sub = -1;
    bool operator==(const Hover&) const = default;
};
// One frame's hover from the last one (hysteresis: entering is stricter than staying). The open circle (h.parent) is the whole
// state of "which arm is held": it is set from the center, and with Switch arms Back through center (t.viaCenter) only the
// center zone lets it go. refused: the other circle a clear move pointed at and did not open (-1: none).
inline Hover Step(Hover h, Vec2f c, const Tuning& t, int* refused = nullptr) noexcept {
    if (refused) *refused = -1;
    const float r = Len(c), a = AngOf(c);
    int nearest = 0;
    for (int d = 1; d < DirCount; ++d) if (AngDiff(a, AxisDeg(d)) < AngDiff(a, AxisDeg(nearest))) nearest = d;
    const bool entersNearest = r > t.parentInDeg && AngDiff(a, AxisDeg(nearest)) <= t.axisInDeg;
    if (h.parent < 0) {
        if (!entersNearest) return {};
        h = {nearest, -1};
    } else if (r < t.centerInDeg) {
        return {};
    } else if (nearest != h.parent && entersNearest && AngDiff(a, AxisDeg(h.parent)) > t.axisOutDeg) {
        if (!t.viaCenter) h = {nearest, -1};   // a clear move to another parent (the diagonals stay on the old one)
        else if (refused) *refused = nearest;  // Back through center: the open one holds; the pointer moves within it
    }
    const Branch& b = Layout[size_t(h.parent)];
    const bool wasPast = h.sub == SubPast;
    if (b.count < 1) { h.sub = PastReach(h.parent, c, wasPast, t) ? SubPast : -1; return h; }
    const Vec2f p = ParentCenter(h.parent, t), fromP{c.x - p.x, c.y - p.y};
    const auto dist = [&](int s) { const Vec2f q = SubCenter(h.parent, s, t); return Len({c.x - q.x, c.y - q.y}); };
    if (h.sub >= 0 && dist(h.sub) <= t.subOutDeg) return h;   // sticky
    int pick = -1;
    for (int s = 0; s < b.count; ++s) if (dist(s) <= t.subInDeg && (pick < 0 || dist(s) < dist(pick))) pick = s;
    if (pick < 0 && Len(fromP) >= t.pickPastDeg) {
        // Outward from the parent's center: the sub whose direction is nearest, within pickAngleDeg.
        const float fa = AngOf(fromP);
        float best = t.pickAngleDeg;
        for (int s = 0; s < b.count; ++s) {
            const float d = AngDiff(fa, ChildDeg(h.parent, s, b.count, t));
            if (d <= best) { best = d; pick = s; }
        }
    }
    if (pick < 0 && Len(fromP) > t.parentDiamDeg * .5f) {
        // Between the parent and its fan, outside the parent's own circle: the nearest sub within gapWinDeg. Pointed here, not
        // only at the let go, so the lit circle is always what a let go gives: the parent's order now, else.
        float best = t.gapWinDeg;
        for (int s = 0; s < b.count; ++s) if (const float d = dist(s); d <= best) { best = d; pick = s; }
    }
    h.sub = pick >= 0 ? pick : PastReach(h.parent, c, wasPast, t) ? SubPast : -1;   // an order wins over the reach
    return h;
}

// What a release does. Order: the id to issue (-1 none); why: the reason when none.
//
enum class Release { Issue, Center, Early, NoArmy, Past };   // Past: let go past the open circle's reach (Parent depth)
struct ReleaseResult { Release what = Release::Center; int order = -1; };
// heldMs: since the press (a release inside the latch does nothing); army: read at release. The hover already holds the
// pointed order (Step): a sub, else the open circle's own order.
inline ReleaseResult Resolve(Hover h, double heldMs, bool army, const Tuning& t) noexcept {
    if (heldMs < t.latchMs) return {Release::Early, -1};
    if (h.parent < 0) return {Release::Center, -1};
    const int order = Pointed(h.parent, h.sub);
    if (order < 0) return {Release::Past, -1};
    if (!army) return {Release::NoArmy, -1};
    return {Release::Issue, order};
}

// What is drawn this frame: the parents shown (all four in the center, only the open one else), the
// fan of the open one, and the faint center ring while a branch is open.
struct Visible {
    bool parent[DirCount]{};
    int fanOf = -1, fanCount = 0;
    bool centerRing = false;
};
inline Visible Show(Hover h) noexcept {
    Visible v;
    if (h.parent < 0) { for (bool& p : v.parent) p = true; return v; }
    v.parent[h.parent] = true;
    v.centerRing = true;
    const int n = Layout[size_t(h.parent)].count;
    if (n > 0) { v.fanOf = h.parent; v.fanCount = n; }
    return v;
}

// The press's life: the origin latch (the average of the hand over latchMs), then the hover.
struct Menu {
    bool open = false, latched = false;
    double pressMs = 0;
    float sum[3]{}, origin[3]{}, right[3]{1, 0, 0};
    int samples = 0;
    Hover hover;
    Vec2f cursor;
    int refused = -1;   // Back through center: the other circle the pointer is clearly on now, not opened (-1: none)
    void Open(double nowMs, const float hand[3], const float headRight[3]) noexcept {
        *this = {};
        open = true; pressMs = nowMs;
        const float n = std::sqrt(headRight[0] * headRight[0] + headRight[2] * headRight[2]);
        if (n > 1e-4f) { right[0] = headRight[0] / n; right[1] = 0; right[2] = headRight[2] / n; }
        Sample(hand);
    }
    void Sample(const float hand[3]) noexcept { for (int i = 0; i < 3; ++i) sum[i] += hand[i]; ++samples; }
    // One held frame; returns true when the hover changed (a tick).
    bool Held(double nowMs, const float hand[3], const Tuning& t) noexcept {
        if (!open) return false;
        if (!latched) {
            Sample(hand);
            if (nowMs - pressMs < t.latchMs) return false;
            for (int i = 0; i < 3; ++i) origin[i] = sum[i] / float(samples);
            latched = true;
        }
        cursor = Cursor(hand, origin, right, t);
        const Hover was = hover;
        hover = Step(hover, cursor, t, &refused);
        return !(hover == was);
    }
    ReleaseResult Let(double nowMs, bool army, const Tuning& t) noexcept {
        const auto r = Resolve(latched ? hover : Hover{}, nowMs - pressMs, army, t);
        open = false;
        return r;
    }
    void Cancel() noexcept { open = false; }
};

// ---- The atlas: where the art sits inside the X menu's region of transport's static atlas ----
// (the region itself: steamvr/panel_atlas.h; the sheet is made offline by tools\army_x_menu_sheet.py from the
// icons, the same cells). Icons 96 texels, 4 a row, then the labels 208 x 40, 2 a row; Gap 8 between all.
// (every circle an order): only the orders a fan shows keep an order plate (Protective ring, Hold ground and Regroup
// are circles now, drawn with the circle's own icon), which frees an icon row for the two new names.
namespace atlas {
constexpr int Width = 432, Height = 616, Gap = 8, Icon = 96, LabelW = 208, LabelH = 40;
// (Move and hold): the region is full (the menus' column, panelatlas::ArmyXMenu, ends at the compass), so Move and hold's
// round plate (the painted army-p-place: a path to a marked spot) took the No army shade's cell; the No army cross draws the
// faint center ring at each circle instead.
enum Cell : int { CellUp, CellDown, CellLeft, CellRight, CellPhalanx, CellCrescent, CellWedge, CellFallOut,
                  CellRim, CellRing, CellMoveHold, CellRimSquare, CellCount };   // CellRimSquare: clear and unused since (every item is a circle, made from its square master; the cell keeps the layout)
// Labels: the seven commander orders by id (3901..3907), then "No army", "Attack my target", "No target", "Canceled", "Move and hold".
constexpr int LabelNoArmy = 7, LabelAttackTarget = 8, LabelNoTarget = 9, LabelCanceled = 10, LabelMoveHold = 11, LabelCount = 12;
struct Rect { int x, y, w, h; };
// The target mark's frames (Attack my target's arrow): a region of their own in the static image (panelatlas::ArmyXMark, the glue checks
// they agree), MarkFrames of MarkFrame px one under the other, MarkGap apart; the art is tools/army_x_mark_strip.py's
// steamvr/assets/army-x-target-mark.png (straight alpha, MarkFrame x MarkStripH).
constexpr int MarkFrame = 64, MarkFrames = 8, MarkGap = Gap, MarkStripH = MarkFrames * MarkFrame + (MarkFrames - 1) * MarkGap;
constexpr Rect MarkRect(int frame) noexcept { return {0, (frame % MarkFrames) * (MarkFrame + MarkGap), MarkFrame, MarkFrame}; }
constexpr int IconRows = (CellCount + 3) / 4;
constexpr Rect IconRect(int cell) noexcept { return {Gap + (cell % 4) * (Icon + Gap), Gap + (cell / 4) * (Icon + Gap), Icon, Icon}; }
constexpr int LabelTop = Gap + IconRows * (Icon + Gap);
constexpr Rect LabelRect(int i) noexcept { return {Gap + (i % 2) * (LabelW + Gap), LabelTop + (i / 2) * (LabelH + Gap), LabelW, LabelH}; }
// The four circles show their own order's art. (every item a square icon, the ring
// shows circles made from it): each item's master is its square plate (tools/army_x_squares), and tools/army_x_menu_sheet.py cuts
// the circle from it the way the skill wheel does (RecipeSeat), so every item is a circle and is lit with the round rim; no square
// is drawn.
constexpr bool CircleSquare(int) noexcept { return false; }
// A fan order's plate (-1: no plate; that order is a circle's own).
constexpr int OrderCell(int id) noexcept {
    return id == commander::SkillPhalanxDef ? CellPhalanx : id == commander::SkillMarchBal ? CellCrescent :
           id == commander::SkillWedgeAgg ? CellWedge : id == commander::SkillFallOut ? CellFallOut : id == MoveHold ? CellMoveHold : -1;
}
// A fan order is drawn round (its hover rim round, CellRim): every one, since (a circle cut from its square master).
constexpr bool OrderRound(int) noexcept { return true; }
// An order's name (-1: none).
constexpr int LabelOf(int id) noexcept {
    return commander::IsCommanderSkill(id) ? id - commander::SkillPhalanxDef : id == AttackTarget ? LabelAttackTarget :
           id == MoveHold ? LabelMoveHold : -1;
}
constexpr bool EveryOrderDrawn() noexcept {   // each fan order has a plate, each order (circle or fan) a name
    for (const Branch& b : Layout) {
        if (LabelOf(b.order) < 0) return false;
        for (int s = 0; s < b.count; ++s) if (OrderCell(b.orders[s]) < 0 || LabelOf(b.orders[s]) < 0) return false;
    }
    return true;
}
static_assert(EveryOrderDrawn(), "every order in Layout has its plate and its name in the sheet");
static_assert(IconRect(CellCount - 1).y + Icon + Gap <= LabelTop && IconRect(3).x + Icon <= Width, "icons fit their rows");
static_assert(LabelRect(LabelCount - 1).y + LabelH <= Height && LabelRect(1).x + LabelW <= Width, "labels fit the region");
// The paint's two checks, pure so the tests reach them. Clear: every byte of the region grown by gap
// (clipped to the image) is zero, so nobody else painted there. Premultiply: straight RGBA to premultiplied, in linear
// light when the image is sRGB (linear color times alpha, encoded again), with a 256-entry table; fully clear texels stay 0.
inline bool RegionClear(const uint8_t* px, int width, int height, int rx, int ry, int rw, int rh, int gap) noexcept {
    const int x0 = std::max(0, rx - gap), y0 = std::max(0, ry - gap), x1 = std::min(width, rx + rw + gap), y1 = std::min(height, ry + rh + gap);
    for (int y = y0; y < y1; ++y) {
        const uint8_t* p = px + (size_t(y) * size_t(width) + size_t(x0)) * 4;
        for (int i = 0, n = (x1 - x0) * 4; i < n; ++i) if (p[i]) return false;
    }
    return true;
}
struct Premul {
    float toLinear[256];
    explicit Premul() noexcept {
        for (int i = 0; i < 256; ++i) { const float c = i / 255.f; toLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
    }
    static uint8_t Encode(float lin) noexcept {
        const float c = lin <= 0.0031308f ? lin * 12.92f : 1.055f * std::pow(lin, 1 / 2.4f) - 0.055f;
        return uint8_t(std::lround(std::clamp(c, 0.f, 1.f) * 255.f));
    }
    // One texel: in straight, out premultiplied.
    void Texel(const uint8_t* in, uint8_t* out, bool srgb) const noexcept {
        const uint8_t a = in[3];
        if (a == 0) { out[0] = out[1] = out[2] = out[3] = 0; return; }
        if (a == 255) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; out[3] = 255; return; }
        const float k = a / 255.f;
        for (int c = 0; c < 3; ++c) out[c] = srgb ? Encode(toLinear[in[c]] * k) : uint8_t(std::lround(in[c] * k));
        out[3] = a;
    }
};
// The bind entry's icon: the cross
// itself, its four circles (CellUp .. CellRight, as the menu opens) shrunk to BindCircle px round the middle of a picture with
// the commander's skill frame proportions (132 x 130), inside the frame's inner 104 px (from (14, 13)).
// sheet: the sheet, straight RGBA, w x h; out: straight RGBA, BindW x BindH (all clear when the sheet is missing).
constexpr int BindW = 132, BindH = 130, BindCircle = 40, BindReach = 32;
inline void BindIcon(const uint8_t* sheet, int w, int h, std::vector<uint8_t>& out) {
    out.assign(size_t(BindW) * BindH * 4, 0);
    if (!sheet || w < Width || h < Height) return;
    constexpr int cx = 14 + 52, cy = 13 + 52;                  // the inner area's center
    constexpr int step[DirCount][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};   // Up, Down, Left, Right (picture y down)
    const float k = float(Icon) / BindCircle;
    for (int d = 0; d < DirCount; ++d) {
        const Rect r = IconRect(CellUp + d);
        const int ox = cx + step[d][0] * BindReach - BindCircle / 2, oy = cy + step[d][1] * BindReach - BindCircle / 2;
        for (int y = 0; y < BindCircle; ++y) for (int x = 0; x < BindCircle; ++x) {
            const int sx0 = int(x * k), sx1 = std::max(sx0 + 1, int((x + 1) * k)), sy0 = int(y * k), sy1 = std::max(sy0 + 1, int((y + 1) * k));
            float sum[4] = {0, 0, 0, 0}; int n = 0;   // premultiplied box average, then straight again
            for (int sy = sy0; sy < sy1 && sy < Icon; ++sy) for (int sx = sx0; sx < sx1 && sx < Icon; ++sx, ++n) {
                const uint8_t* p = sheet + (size_t(r.y + sy) * size_t(w) + size_t(r.x + sx)) * 4;
                const float a = p[3] / 255.f;
                for (int c = 0; c < 3; ++c) sum[c] += p[c] * a;
                sum[3] += a;
            }
            uint8_t* q = &out[(size_t(oy + y) * BindW + size_t(ox + x)) * 4];
            if (n == 0 || sum[3] <= 0) continue;
            for (int c = 0; c < 3; ++c) q[c] = uint8_t(std::clamp(std::lround(sum[c] / sum[3]), 0L, 255L));
            q[3] = uint8_t(std::clamp(std::lround(sum[3] / n * 255.f), 0L, 255L));
        }
    }
}
}  // namespace atlas

// ---- Placement: a body-locked plane, fixed in the room while held ----
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
struct Vec3 { float x = 0, y = 0, z = 0; };
inline Quat Mul(Quat a, Quat b) noexcept {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Vec3 Rotate(Quat q, Vec3 v) noexcept {
    const Vec3 u{q.x, q.y, q.z};
    const auto cross = [](Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    const Vec3 t = cross(u, v); const Vec3 t2{t.x * 2, t.y * 2, t.z * 2};
    const Vec3 c = cross(u, t2);
    return {v.x + q.w * t2.x + c.x, v.y + q.w * t2.y + c.y, v.z + q.w * t2.z + c.z};
}
inline Quat AxisAngle(Vec3 axis, float deg) noexcept {
    const float h = deg * 0.00872665f, s = std::sin(h);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(h)};
}
// The head's yaw (radians about +y; 0 = facing -z, OpenXR's forward) from its orientation.
inline float HeadYaw(Quat head) noexcept {
    const Vec3 f = Rotate(head, {0, 0, -1});
    return std::atan2(-f.x, -f.z);
}
struct Plane {
    Vec3 center; Quat q;        // the cross's center and its plane's turn (+z toward the eyes, +y up)
    float dist = 1;             // m from the eyes, for degrees -> m
    Vec3 right{1, 0, 0};        // the head's flat right at open: the cursor's x
};
// hand: 0 right, 1 left (the shift toward it is mirrored).
inline Plane Place(Vec3 eye, Quat head, int hand, const Tuning& t) noexcept {
    const float yawDeg = HeadYaw(head) * 57.29578f + (hand == 1 ? t.sideDeg : -t.sideDeg);
    Plane p;
    p.q = Mul(AxisAngle({0, 1, 0}, yawDeg), AxisAngle({1, 0, 0}, -t.downDeg));
    p.dist = t.distM;
    const Vec3 f = Rotate(p.q, {0, 0, -t.distM});
    p.center = {eye.x + f.x, eye.y + f.y, eye.z + f.z};
    const float yaw = HeadYaw(head);
    p.right = {std::cos(yaw), 0, -std::sin(yaw)};
    return p;
}
// A point on the plane at (x, y) degrees from its center, and a width in degrees as meters there.
inline Vec3 At(const Plane& p, Vec2f deg) noexcept {
    const Vec3 o = Rotate(p.q, {p.dist * std::tan(deg.x * 0.01745329f), p.dist * std::tan(deg.y * 0.01745329f), 0});
    return {p.center.x + o.x, p.center.y + o.y, p.center.z + o.z};
}
inline float Meters(const Plane& p, float deg) noexcept { return 2 * p.dist * std::tan(deg * 0.00872665f); }

// ---- One frame's draw list: quads from the atlas, back to front ----
// rect: inside the quad's region of the static image: SheetXMenu (panelatlas::ArmyXMenu) or SheetMark (panelatlas::ArmyXMark).
enum Sheet : int { SheetXMenu = 0, SheetMark = 1 };
struct Quad { float pose[7]; float w, h; atlas::Rect rect; int sheet = SheetXMenu; };
// The largest cross is 6 (the center ring, the open circle, its two orders, one hover rim, the name), plus the target mark (1);
// the toast (2) and the targeting hint (1) never show with an open cross (army_x_menu_test checks the sum).
constexpr int MaxQuads = 8;
struct DrawList {
    Quad q[MaxQuads]; int n = 0;
    void Add(const Plane& p, Vec2f at, float wDeg, float hDeg, atlas::Rect r) noexcept {
        if (n >= MaxQuads) return;
        const Vec3 c = At(p, at);
        q[n++] = {{c.x, c.y, c.z, p.q.x, p.q.y, p.q.z, p.q.w}, Meters(p, wDeg), Meters(p, hDeg), r};
    }
    void Add(const Quad& quad) noexcept { if (n < MaxQuads) q[n++] = quad; }
};

// ---- The target mark: the arrow over a monster, in the room (Attack my target) ----
// Placed through transport's own mapping: the monster's cells (the outline's scan, targetlock) to the render world with
// toss::World (RenderGround(cells * 2 / upm), the aimed throw's and the tactical chord's), MarkAboveM over its feet, then into the
// room with the stage (Stage::Room, the inverse of the laser's Stage::World): the quads' space, as the hands' and the head's.
// It turns about the vertical to face the eye, keeps at least MarkDeg of view (far monsters stay readable) and bobs.
constexpr float MarkAboveM = 2.2f;     // over the feet (the chord's body is 0 to 2 m tall)
constexpr float MarkDeg = 2.5f, MarkMinM = .35f;   // its width: this much of view, never under this
constexpr float CandidateScale = .8f;  // the candidate's arrow is smaller than the locked one
constexpr float BobM = .06f, BobHz = .8f;
constexpr double MarkFps = 12;         // the strip's frames a second
inline int MarkFrameAt(double nowMs) noexcept {
    const double f = std::fmod(std::max(0.0, nowMs) / 1000.0 * MarkFps, double(atlas::MarkFrames));
    return std::clamp(int(f), 0, atlas::MarkFrames - 1);
}
// The arrow's quad over the monster at (x, y) cells; eye: the head in the room; false without a mapping.
inline bool MarkQuad(const WorldMap& w, float x, float y, Vec3 eye, double nowMs, bool locked, Quad& out) noexcept {
    if (!w.Valid()) return false;
    const float bob = BobM * std::sin(float(nowMs / 1000.0 * BobHz * 6.2831853));
    const math::Vec room = w.stage.Room(toss::World(x, y, w.floor, w.upm) + math::Vec{0, MarkAboveM + bob, 0});
    const float dx = eye.x - room.x, dz = eye.z - room.z, dist = std::sqrt(dx * dx + (eye.y - room.y) * (eye.y - room.y) + dz * dz);
    if (!std::isfinite(dist) || dist < .1f) return false;
    const Quat q = AxisAngle({0, 1, 0}, std::atan2(dx, dz) * 57.29578f);   // +z (the picture's face) toward the eye
    const float size = std::max(MarkMinM, 2.f * dist * std::tan(MarkDeg * 0.00872665f)) * (locked ? 1.f : CandidateScale);
    out = {{room.x, room.y, room.z, q.x, q.y, q.z, q.w}, size, size, atlas::MarkRect(MarkFrameAt(nowMs)), SheetMark};
    return true;
}
// While targeting: the mode's order name where the cross was, under where its center stood (the mode is on): "Attack my target",
// or "Move and hold" while its hand picks the spot (label).
inline void TargetingHint(DrawList& d, const Plane& p, const Tuning& t, int label = atlas::LabelAttackTarget) noexcept {
    const float labelH = 1.6f;
    d.Add(p, {0, -(t.parentAtDeg + t.parentDiamDeg * .5f + labelH)}, labelH * float(atlas::LabelW) / float(atlas::LabelH), labelH,
          atlas::LabelRect(label));
}
inline float LabelWidthDeg(float hDeg) noexcept { return hDeg * float(atlas::LabelW) / float(atlas::LabelH); }
// The cross while held: the hover, whether an army is out (read every frame, for the look only).
inline DrawList Draw(const Plane& p, Hover h, bool army, const Tuning& t) noexcept {
    DrawList d;
    const float labelH = 1.6f;
    if (!army) {   // a dim cross: four faint rings and "No army" (the shade's cell is Move and hold's plate since)
        for (int dir = 0; dir < DirCount; ++dir) d.Add(p, ParentCenter(dir, t), t.parentDiamDeg, t.parentDiamDeg, atlas::IconRect(atlas::CellRing));
        d.Add(p, {0, 0}, LabelWidthDeg(labelH), labelH, atlas::LabelRect(atlas::LabelNoArmy));
        return d;
    }
    const Visible v = Show(h);
    if (v.centerRing) d.Add(p, {0, 0}, t.centerInDeg * 2, t.centerInDeg * 2, atlas::IconRect(atlas::CellRing));
    for (int dir = 0; dir < DirCount; ++dir) {
        if (!v.parent[dir]) continue;
        const bool lit = h.parent == dir && h.sub == -1;   // past its reach (SubPast): drawn, not lit
        const float s = t.parentDiamDeg * (lit ? 1.1f : 1.f);
        d.Add(p, ParentCenter(dir, t), s, s, atlas::IconRect(atlas::CellUp + dir));
        if (lit) d.Add(p, ParentCenter(dir, t), s, s, atlas::IconRect(atlas::CircleSquare(dir) ? atlas::CellRimSquare : atlas::CellRim));
    }
    for (int s = 0; s < v.fanCount; ++s) {
        const int id = Layout[size_t(v.fanOf)].orders[s];
        const bool lit = h.sub == s;
        const float w = t.subDiamDeg * (lit ? 1.1f : 1.f);
        d.Add(p, SubCenter(v.fanOf, s, t), w, w, atlas::IconRect(atlas::OrderCell(id)));
        if (lit) d.Add(p, SubCenter(v.fanOf, s, t), w, w, atlas::IconRect(atlas::OrderRound(id) ? atlas::CellRim : atlas::CellRimSquare));
    }
    // The pointed order's name, under the cross: a fan order, else the open circle's own (what a let go gives).
    if (const int label = atlas::LabelOf(Pointed(h.parent, h.sub)); label >= 0)
        d.Add(p, {0, -(t.parentAtDeg + t.parentDiamDeg * .5f + labelH)}, LabelWidthDeg(labelH), labelH, atlas::LabelRect(label));
    return d;
}

// ---- The press's whole life, one call a frame (the handler is glue around it) ----
struct FrameIn {
    double nowMs = 0;
    bool on = false;            // the menu is on a hand button
    int bound = -1;             // the slot Army X menu is on (-1: none)
    int held = -1, hand = 0;    // the slot held this frame (the commander's handOutput.slot) and its hand (0 right, 1 left)
    bool down = false;          // the bound button physically down (the raw pad: its trigger or face button), whatever the commander's
                                // arbitration did with the slot (it drops to -1 on a grip change, a tracking loss, a 100 ms gap)
    bool occupied = false;      // the hand took a potion or a holster: the cross closes, nothing given
    bool slotFree = false;      // the bound slot has no game skill and is no Draw glyph button: the X menu owns it
    bool wheelOpen = false;     // the skill wheel or the Ring wheel is open
    bool tracked = false;       // the holding hand and the head are tracked
    Vec3 handPos, eye; Quat head;
    bool army = false;          // a summon army now (the commander's HasSummonArmy)
};
enum class Event { None, Opened, Issue, Canceled };
struct FrameOut {
    bool mute = false;          // the holding hand's press does nothing else (the commander's cast and attack skipped)
    Event event = Event::None;
    int order = -1;             // with Issue: the commander id to give, once (the caller gives it and reports ok)
    Release why = Release::Center;   // with Canceled: why
    bool tick = false;          // the hover changed: a light tick
    int hand = 0;
    DrawList draw;              // what to show this frame (empty: nothing)
};
struct Controller {
    Menu menu; Plane plane; int hand = 0; int heldSlot = -1;
    bool waitUp = false;        // closed without a release (a wheel, Off, occupied): the button must come up before it opens again
    // The toast: a label (the last order's name, or Canceled), until toastUntil; toastOver: one over it ("No target" when
    // Attack my target fell back; -1 none).
    int toastLabel = -1, toastOver = -1; double toastUntil = 0; Plane toastPlane;
    FrameOut Step(const FrameIn& in, const Tuning& t) noexcept {
        FrameOut out; out.hand = hand;
        const bool owns = in.on && in.bound >= 0 && in.slotFree;
        if (!menu.open) {
            if (waitUp && !in.down) waitUp = false;
            if (owns && !waitUp && in.held == in.bound && in.down && !in.wheelOpen && in.tracked) {
                hand = out.hand = in.hand; heldSlot = in.held;
                plane = Place(in.eye, in.head, in.hand, t);
                const float hp[3] = {in.handPos.x, in.handPos.y, in.handPos.z};
                const float rt[3] = {plane.right.x, plane.right.y, plane.right.z};
                menu.Open(in.nowMs, hp, rt);
                out.event = Event::Opened; out.mute = true;
                toastLabel = -1;
            }
        } else {
            out.mute = true;
            if (!owns || in.wheelOpen || in.occupied) {   // taken off its button, unbound, a wheel opened, a potion or holster: nothing given
                menu.Cancel(); out.event = Event::Canceled; out.why = Release::Center;
                waitUp = in.down;
                return out;
            }
            if (in.down) {
                // Still held: never an order. The pointer follows the hand while it is tracked, even after the commander dropped
                // the slot (a grip change, a tracking blip or a hitch blocks the button until it comes up, so the commander never
                // names this press again); only an untracked hand freezes the hover.
                if (in.tracked) {
                    const float hp[3] = {in.handPos.x, in.handPos.y, in.handPos.z};
                    out.tick = menu.Held(in.nowMs, hp, t) && (menu.hover.parent >= 0);
                }
            } else {   // the physical up edge: the release decides
                const auto r = menu.Let(in.nowMs, in.army, t);
                if (r.what == Release::Issue) {
                    out.event = Event::Issue; out.order = r.order;
                } else {
                    out.event = Event::Canceled; out.why = r.what;
                }
                return out;
            }
        }
        if (menu.open) out.draw = Draw(plane, menu.latched ? menu.hover : Hover{}, in.army, t);
        else if (toastLabel >= 0 && in.nowMs < toastUntil) {
            const float y = -(t.parentAtDeg + t.parentDiamDeg * .5f + 1.6f);
            if (toastOver >= 0) out.draw.Add(toastPlane, {0, y}, LabelWidthDeg(1.6f), 1.6f, atlas::LabelRect(toastOver));
            out.draw.Add(toastPlane, {0, toastOver >= 0 ? y - 1.6f * 1.15f : y}, LabelWidthDeg(1.6f), 1.6f, atlas::LabelRect(toastLabel));
        }
        return out;
    }
    // The caller gave the order (ok: as before took it): the toast shows its name; noTarget: Attack my target found no monster
    // and gave its fallback (order), so "No target" shows over that name.
    void Given(int order, bool ok, double nowMs, const Tuning& t, bool noTarget = false) noexcept {
        if (ok) Toast(atlas::LabelOf(order), nowMs, t, noTarget ? atlas::LabelNoTarget : -1);
    }
    // A label as the toast (Canceled: the targeting mode ended with nothing ordered), where the last cross was.
    void Toast(int label, double nowMs, const Tuning& t, int over = -1) noexcept {
        if (t.toastS > 0 && label >= 0 && label < atlas::LabelCount) {
            toastLabel = label; toastOver = over; toastUntil = nowMs + t.toastS * 1000.0; toastPlane = plane;
        }
    }
    void Reset() noexcept { *this = {}; }
};

}  // namespace framevr::xmenu
