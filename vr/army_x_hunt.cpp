// Army X menu: the summons hunt a marked monster. Glue around the pure engine steamvr/army_x_hunt.h (its header says why a
// far mark was ignored before, and the rules), called from vr/companion_ai.cpp and vr/combat.cpp (vr/army_x_orders.h):
//   - Moves (StepSummonArmyServer, the game thread, about every 250 ms with an army out; right after the coordinator's Step,
//     under its lock): while a priority target is set (the menu's Attack my target, or the tactical chord's), each hunting
//     summon's walk back to its spot is dropped and a move to the mark goes into the same list, which the host's own loop
//     sends; a hunter standing idle beside the mark is sent again; when the mark dies or is lost, the coordinator's own
//     Regroup re-forms them. The menu's own orders are given first (armyxmenu::ArmyTick), then a hit summon's fight back.
//   - Leash (DetourRelocateUnit, after the anchored-summon suppression): the game's own leash does not pull a hunter back.
//   - Walk (where an army move is sent): a summon whose game data has a run goes running (its MonStats2 mode mask, read once
//     a monster class the way melee.cpp reads it for hit reactions: StatsRva, signature-checked).
//   - Move (DetourServerMoveUnit, a move the gate let through): a hunting runner's own walk to a place goes out as a run,
//     the call ServerMoveToCoord makes for every run (mode 15, path type 1, flags 0; companion_ai_signatures.h).
//   - Refused (the same, a move the gate refused): a hunter beside its mark is answered "failed", not "done", so the game's
//     AI thinks again and attacks.
//   - Roll (combat.cpp Hit, every to-hit roll) and Target (TryOverrideCombatTarget): a summon a monster rolls to hit, with no
//     mark set, is handed that monster; Sample (the game tick after the army step, no lock) looks each attacker up.
// Every handler returns at its first test while the menu is on no hand button. No native function of its own: unit lookup
// by id (melee::LocalServer / ServerUnit) and plain reads, as companion_ai.cpp makes them.
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <span>
#include "bow_signatures.h"
#include "army_x_orders.h"
#include "../cleanroom/sigscan/sigscan.h"
#include "../steamvr/army_x_hunt.h"
#include "../steamvr/memory.h"

namespace gamestate { uint64_t LocalPlayer(); }                          // steamvr/gamestate.cpp: the local hero's session
namespace interactionstate { int CurrentLevel(uint64_t session, uint64_t now); }   // vr/interactionstate.h: the hero's level, 0 unknown
// The commander's own reads, the owning game thread only: the local server game and hero, a unit by id, and a monster
// class's mode mask (vr/melee.h).
namespace melee { bool LocalServer(uintptr_t& game, uintptr_t& player); uintptr_t ServerUnit(uintptr_t game, int type, uint32_t id); uint32_t MonsterModes(uintptr_t unit, uint32_t classId); }

namespace {
namespace seam = framevr::armyseam;
namespace cfg = framevr::armyseam::cfg;

// The hunt's roster: written by Moves after each engine call (the ids first, then the count and the tick's time with
// release, so a reader that sees a count sees its ids), read by Leash, Move and Refused (atomics: those are the game's own
// calls). A publish older than GapMs is none.
constexpr size_t RosterMax = framevr::commander::MaxArmyUnits;
std::array<std::atomic<uint32_t>, RosterMax> g_hunters{};
std::atomic<uint32_t> g_hunterCount{0}, g_huntMark{0};
std::atomic<uint64_t> g_huntAtMs{0};
// Per roster slot: the hunter runs (Summons run's rule on its own game data) and is still on its way (not beside the
// mark, not given up). Published with the ids.
constexpr uint8_t FlagRunner = 1, FlagAway = 2;
std::array<std::atomic<uint8_t>, RosterMax> g_hunterFlags{};
// The leash roster: the hunters the leash rule may keep this tick (PickLeash: within the leash reach of the hero, the level
// known, not a town, no level change), and the hero's level it was picked on. Published the same way.
std::array<std::atomic<uint32_t>, RosterMax> g_leash{};
std::atomic<uint32_t> g_leashCount{0};
std::atomic<int> g_leashLevel{0};
int g_lastLevel = 0;                                   // game thread only (Moves): the hero's level at the last publish
// The hero's level (interactionstate's snapshot, 0 unknown). Read under g_tacticalLock in Moves: interactionstate takes only its own
// mutex and never calls companion_ai while it holds it, so no lock order is reversed.
int HeroLevel() { return interactionstate::CurrentLevel(gamestate::LocalPlayer(), GetTickCount64()); }
// runners: per f.units[i], its moves run (Summons run's rule; empty: Summons run Off).
void Publish(const seam::ArmyMovesFrame& f, const xhunt::Hunt& h, uint64_t nowMs, std::span<const uint8_t> runners) {
    uint32_t ids[RosterMax];
    const size_t n = h.Hunters(ids, RosterMax);
    for (size_t i = 0; i < n; ++i) {
        g_hunters[i].store(ids[i], std::memory_order_relaxed);
        uint8_t flags = h.Away(ids[i]) ? FlagAway : 0;
        for (size_t u = 0; u < f.unitCount && u < runners.size(); ++u)
            if (f.units[u].unitId == ids[i]) { if (runners[u]) flags |= FlagRunner; break; }
        g_hunterFlags[i].store(flags, std::memory_order_relaxed);
    }
    // The leash roster: each hunter's place from this tick's samples (a hunter not sampled is not kept)
    const int level = HeroLevel();
    xhunt::Vec2 pos[RosterMax];
    uint32_t sampled[RosterMax], kept[RosterMax];
    size_t m = 0;
    for (size_t i = 0; i < n; ++i)
        for (size_t u = 0; u < f.unitCount; ++u)
            if (f.units[u].unitId == ids[i]) { sampled[m] = ids[i]; pos[m++] = f.units[u].pos; break; }
    const auto pick = n ? xhunt::PickLeash(cfg::Gated(cfg::rules.leashReach), level, g_lastLevel, sampled, pos, m, {f.heroX, f.heroY}, kept)
                        : xhunt::LeashPick{};
    if (level) g_lastLevel = level;
    for (size_t i = 0; i < pick.n; ++i) g_leash[i].store(kept[i], std::memory_order_relaxed);
    g_leashLevel.store(level, std::memory_order_relaxed);
    g_huntMark.store(h.Mark(), std::memory_order_relaxed);
    g_huntAtMs.store(nowMs, std::memory_order_relaxed);
    g_leashCount.store(uint32_t(pick.n), std::memory_order_release);
    g_hunterCount.store(uint32_t(n), std::memory_order_release);
}
// The leash roster now (empty when stale): ids into out, the count.
size_t LeashRoster(uint32_t* out, uint64_t nowMs) {
    const uint32_t n = g_leashCount.load(std::memory_order_acquire);
    const uint64_t at = g_huntAtMs.load(std::memory_order_relaxed);
    if (!n || !at || nowMs < at || nowMs - at > xhunt::GapMs) return 0;
    for (uint32_t i = 0; i < n && i < RosterMax; ++i) out[i] = g_leash[i].load(std::memory_order_relaxed);
    return n < RosterMax ? n : RosterMax;
}
// A published hunter's roster slot (-1: not a hunter, or no army tick refreshed the roster for GapMs).
int SlotOf(uint32_t id) {
    const uint32_t n = g_hunterCount.load(std::memory_order_acquire);
    if (!n || !id) return -1;
    const uint64_t at = g_huntAtMs.load(std::memory_order_relaxed), now = GetTickCount64();
    if (!at || now < at || now - at > xhunt::GapMs) return -1;
    for (uint32_t k = 0; k < n && k < RosterMax; ++k)
        if (g_hunters[k].load(std::memory_order_relaxed) == id) return int(k);
    return -1;
}
template <class T> bool Read(uintptr_t p, T& out) {
    return p >= 0x10000 && p <= UINTPTR_MAX - sizeof(out) && framevr::ReadMemory(&out, reinterpret_cast<const void*>(p), sizeof(out));
}
// A unit's place from its path (unit +0x38 -> path, uint16[4]: x fraction, x sub-tile, y fraction, y sub-tile: companion_ai's
// ReadPosition layout). False: unreadable.
bool Place(uintptr_t unit, float& x, float& y) {
    uintptr_t path = 0;
    uint16_t p[4]{};
    if (!Read(unit + 0x38, path) || !Read(path, p)) return false;
    x = p[1] + p[0] / 65536.0f;
    y = p[3] + p[2] / 65536.0f;
    return true;
}

// Game thread only (Moves and its drain call both run in StepSummonArmyServer).
struct State {
    xhunt::Hunt hunt;
};
State g;

// Summons run: each monster class's MonStats2 mode mask (0: unread), read on the game thread by melee::MonsterModes (the mask
// melee reads for hit reactions), behind its own check of that function's signature. The cache is keyed by class only: a
// class's modes are its MonStats2 row's, whatever bank the unit's stats come from.
// ok: an accepted mask (walk bit set) is cached for good; a failed read is tried again at most once a second.
struct ClassModes { uint32_t classId = 0; uint32_t modes = 0; bool ok = false; bool toldFail = false; uint64_t triedMs = 0; };
std::array<ClassModes, 24> g_classModes{};
size_t g_classModesN = 0, g_classModesNext = 0;
constexpr uint64_t ModesRetryMs = 1000;
int g_statsOk = -1;   // StatsRva's signature: -1 not checked yet
uint32_t ReadModes(uintptr_t unit, uint32_t classId) {
    if (g_statsOk < 0) g_statsOk = d2rsig::Check(bowvr::native::StatsRva, bowvr::native::StatsSig, sizeof bowvr::native::StatsSig) ? 1 : 0;
    return g_statsOk ? melee::MonsterModes(unit, classId) : 0;
}
// The mask of this summon's class (cached once accepted), and its class id. A class whose mask cannot be read is said
// once.
uint32_t ModesOf(const seam::ArmyWalkFrame& f, uint32_t& classId, uint64_t nowMs) {
    uint32_t header[4]{};
    classId = 0;
    if (!f.unit || !Read(f.unit, header) || header[0] != 1 || header[2] != f.unitId) return 0;
    classId = header[1];
    ClassModes* e = nullptr;
    for (size_t k = 0; k < g_classModesN && !e; ++k) if (g_classModes[k].classId == classId) e = &g_classModes[k];
    if (e && e->ok) return e->modes;
    if (e && nowMs >= e->triedMs && nowMs - e->triedMs < ModesRetryMs) return 0;
    if (!e) {
        e = &g_classModes[g_classModesNext];
        *e = {classId};
        g_classModesNext = (g_classModesNext + 1) % g_classModes.size();
        if (g_classModesN < g_classModes.size()) ++g_classModesN;
    }
    const uint32_t modes = ReadModes(f.unit, classId);
    e->triedMs = nowMs;
    e->ok = xhunt::ModesRead(modes) != 0;
    e->modes = e->ok ? modes : 0;
    if (!e->ok && !e->toldFail && f.say) {
        char line[300];
        std::snprintf(line, sizeof line, "Frame army x menu: run check: summon %u, %s (type %u, class %u): MonStats2 modes 0x%08x (unread: no walk bit or no stats row; tried again once a second; wolves still run, the others walk)",
                      f.unitId, xhunt::PetName(f.petType), unsigned(f.petType), classId, modes);
        f.say(line);
        e->toldFail = true;
    }
    return e->modes;
}
// A class's accepted mask (0: none read yet), for the army tick's runners: the same decision Walk makes for that summon's move.
uint32_t CachedModes(uint32_t classId) {
    for (size_t k = 0; k < g_classModesN; ++k) if (g_classModes[k].classId == classId && g_classModes[k].ok) return g_classModes[k].modes;
    return 0;
}

// ---- The army's pets ----
// Every army pet the army step sampled (hunting or not), kept by id on the game thread (Moves writes, the sampler reads: the same
// thread), and what the game's own seams saw of each since the sampler's last look (atomics, in a slot the pet keeps by its unit
// id for as long as it is sampled, so a summon dying or being cast never moves
// another pet's counts).
struct Pet {
    uint32_t id = 0; uint16_t classId = 0; uint8_t petType = 0, mode = 1;
    int slot = -1;                 // its PetSeen slot (-1: none free)
    xhunt::Vec2 pos{};
    int life = -1;                 // at the sampler's last look (-1: none yet)
    uint32_t hitSeenN = 0;         // the hits on it (PetSeen::hitN) Fight back when hit has taken already
};
struct PetSeen {
    std::atomic<uint32_t> who{0};
    std::atomic<uint8_t> petType{0xFF};                    // its army type (for the seams that see only its id)
    // Fight back when hit: the last monster's to-hit roll on it (who, where it stood, when; hitN, their count, written
    // last: a new hit is a new count, never a new clock value, which two hits in one 16 ms clock step would share), and the
    // attacker the engine has it on now (0: none), which the target hook hands it
    std::atomic<uint32_t> hitBy{0}; std::atomic<float> hitX{0}, hitY{0}; std::atomic<uint64_t> hitMs{0}; std::atomic<uint32_t> hitN{0};
    std::atomic<uint32_t> foe{0};
};
std::array<Pet, RosterMax> g_pets{};
size_t g_petN = 0;
uint64_t g_petsMs = 0;
std::array<PetSeen, RosterMax> g_petSeen{};
std::atomic<uint32_t> g_petCount{0};
std::atomic<uint64_t> g_petsAtMs{0};
// A published pet's slot (-1: not one, or no army tick for GapMs). Every slot is looked at: a pet's slot is its own, wherever
// it stands in the roster.
int PetSlot(uint32_t id) {
    if (!g_petCount.load(std::memory_order_acquire) || !id) return -1;
    const uint64_t at = g_petsAtMs.load(std::memory_order_relaxed), now = GetTickCount64();
    if (!at || now < at || now - at > xhunt::GapMs) return -1;
    for (size_t k = 0; k < RosterMax; ++k) if (g_petSeen[k].who.load(std::memory_order_relaxed) == id) return int(k);
    return -1;
}
// Every slot let go (the rule off, or a gap): the next pets start with clean counts.
void FreePetSlots() {
    g_petCount.store(0, std::memory_order_release);
    for (auto& s : g_petSeen) s.who.store(0, std::memory_order_relaxed);
}
// Each army tick (Moves, first): the pets as sampled, each one's gait (its mode and how far it moved since the last tick).
void PublishPets(const seam::ArmyMovesFrame& f) {
    if (!(cfg::rules.hitBack && armyxmenu::On())) {
        if (g_petN) { FreePetSlots(); g_petN = 0; g_petsMs = 0; }
        return;
    }
    static std::array<Pet, RosterMax> next;   // game thread only (64 pets: off the stack)
    if (g_petsMs && (f.nowMs < g_petsMs || f.nowMs - g_petsMs > xhunt::GapMs)) {   // a gap (a load, the army gone, Off and On): nothing carried over
        FreePetSlots();
        g_petN = 0; g_petsMs = 0;
    }
    size_t n = 0;
    for (size_t u = 0; u < f.unitCount && n < RosterMax; ++u) {
        const auto& s = f.units[u];
        if (!s.unitId || (framevr::companions::Bit(s.petType) & framevr::companions::ArmyTypeMask) == 0) continue;
        Pet p{}; p.id = s.unitId;
        for (size_t k = 0; k < g_petN; ++k)
            if (g_pets[k].id == s.unitId) { p = g_pets[k]; break; }
        p.classId = s.classId; p.petType = s.petType; p.mode = s.animMode; p.pos = s.pos;
        next[n++] = p;
    }
    // each pet keeps its slot by unit id; only a new pet takes a slot, one no pet of this tick holds (its counts start
    // over); a slot whose pet is gone is let go
    uint32_t ids[RosterMax], who[RosterMax];
    int slot[RosterMax];
    bool fresh[RosterMax], held[RosterMax]{};
    for (size_t i = 0; i < n; ++i) ids[i] = next[i].id;
    for (size_t k = 0; k < RosterMax; ++k) who[k] = g_petSeen[k].who.load(std::memory_order_relaxed);
    xhunt::AssignSlots(ids, n, who, RosterMax, slot, fresh);
    for (size_t i = 0; i < n; ++i) if (slot[i] >= 0) held[size_t(slot[i])] = true;
    for (size_t k = 0; k < RosterMax; ++k) if (!held[k] && who[k]) g_petSeen[k].who.store(0, std::memory_order_relaxed);
    for (size_t i = 0; i < n; ++i) {
        next[i].slot = slot[i];
        if (slot[i] < 0) continue;
        auto& s = g_petSeen[size_t(slot[i])];
        s.petType.store(next[i].petType, std::memory_order_relaxed);
        if (!fresh[i]) continue;
        s.hitBy = 0; s.hitMs = 0; s.hitN = 0; s.foe = 0;
        next[i].hitSeenN = 0;
        s.who.store(next[i].id, std::memory_order_relaxed);
    }
    for (size_t i = 0; i < n; ++i) g_pets[i] = next[i];
    g_petN = n; g_petsMs = f.nowMs;
    g_petsAtMs.store(f.nowMs, std::memory_order_relaxed);
    g_petCount.store(uint32_t(n), std::memory_order_release);
}
// Fight back when hit: the engine (game thread, Moves), the attackers it has summons on (for the look on the game tick that
// follows) and that look's answer (each attacker's place, or dead), read by the next army tick.
xhunt::HitBack g_hb;
bool g_hbWas = false;
std::array<uint32_t, RosterMax> g_hbLook{};
size_t g_hbLookN = 0;
std::array<xhunt::HitBack::Foe, RosterMax> g_hbFoes{};
size_t g_hbFoeN = 0;
void HitBackTick(seam::ArmyMovesFrame& f) {
    const bool on = cfg::rules.hitBack && armyxmenu::On();
    if (!on && !g_hbWas) return;   // Off, nothing under way: nothing is touched
    g_hbWas = on;                  // going Off: one more call, which lets every attacker go
    static std::array<xhunt::HitBack::Hit, RosterMax> hits;   // game thread only
    size_t hn = 0;
    for (size_t i = 0; i < g_petN && hn < hits.size(); ++i) {
        Pet& p = g_pets[i];
        if (p.slot < 0) continue;
        auto& s = g_petSeen[size_t(p.slot)];
        const uint32_t hitN = s.hitN.load(std::memory_order_acquire);
        if (hitN == p.hitSeenN) continue;
        p.hitSeenN = hitN;
        const uint64_t at = s.hitMs.load(std::memory_order_relaxed);
        hits[hn++] = {p.id, s.hitBy.load(std::memory_order_relaxed), {s.hitX.load(std::memory_order_relaxed), s.hitY.load(std::memory_order_relaxed)},
                      std::min<uint64_t>(at, f.nowMs)};   // the tick's time was read at its start: a hit since then is "now"
    }
    const uint64_t forMs = uint64_t(std::max(0.0f, cfg::Gated(cfg::rules.hitBackForS)) * 1000.0f);
    const uint64_t retryMs = uint64_t(std::max(0.0f, cfg::Gated(cfg::rules.farRetryS)) * 1000.0f);
    g_hb.Step(f.coord, std::span(f.units, f.unitCount), f.nowMs, on, cfg::Gated(cfg::rules.hitBackReach), forMs, retryMs,
                             std::span<const xhunt::HitBack::Hit>(hits.data(), hn), std::span<const xhunt::HitBack::Foe>(g_hbFoes.data(), g_hbFoeN),
                             std::span(f.moves, f.capacity), f.emitted);
    // Each summon's own attacker for the target hook, and the attackers for the next look
    uint32_t pets[RosterMax], foes[RosterMax];
    const size_t n = g_hb.Fighting(pets, foes, RosterMax);
    for (size_t i = 0; i < g_petN; ++i) {
        if (g_pets[i].slot < 0) continue;
        uint32_t foe = 0;
        for (size_t k = 0; k < n && !foe; ++k) if (pets[k] == g_pets[i].id) foe = foes[k];
        g_petSeen[size_t(g_pets[i].slot)].foe.store(foe, std::memory_order_relaxed);
    }
    for (size_t k = 0; k < n; ++k) g_hbLook[k] = foes[k];
    g_hbLookN = n;
}
// The look at each fought attacker (Sample: the game tick, no lock): found by id (the commander's lookup), its header checked (a live
// monster), its place read; dead or not found: the engine lets it go.
void HitBackLook() {
    if (!g_hbLookN) { g_hbFoeN = 0; return; }
    uintptr_t game = 0, hero = 0;
    if (!melee::LocalServer(game, hero)) return;   // no server now: the last look stands
    size_t n = 0;
    for (size_t i = 0; i < g_hbLookN && n < g_hbFoes.size(); ++i) {
        const uint32_t id = g_hbLook[i];
        uint32_t h[4]{}; float x = 0, y = 0;
        const uintptr_t unit = melee::ServerUnit(game, 1, id);
        const bool alive = unit && Read(unit, h) && h[0] == 1 && h[2] == id && h[3] != 0 && h[3] != 12 && Place(unit, x, y);
        g_hbFoes[n++] = {id, {x, y}, alive};
    }
    g_hbFoeN = n;
}
}  // namespace

namespace armyxmenu {   // vr/army_x_menu.cpp: the X menu's orders given on the game thread, then Defend (summons only)
void ArmyTick(framevr::armyseam::ArmyMovesFrame& f);
}

namespace armyxhunt {

void Moves(seam::ArmyMovesFrame f) {
    // The X menu's own game-thread step first, so an order it gives (Attack my target, Move and hold) or a monster Defend picks is hunted this tick.
    armyxmenu::ArmyTick(f);
    PublishPets(f);   // the army's pets by id, for the fight back
    HitBackTick(f);   // Fight back when hit: a hit summon's own attacker, only while the army has no priority target
    const xhunt::Mode mode = xhunt::ModeOf(cfg::Gated(cfg::rules.hunt));   // Off while the menu is on no button
    if (mode == xhunt::Mode::Off && !g.hunt.Mark() && !g.hunt.Returning()) return;   // Off, nothing under way: one branch
    // Summons run's decision for each sampled summon (the same as Walk's for its move: its type and its class's mask, once read),
    // and Run the whole approach: Step sends a runner seen walking again (runners), the AI's own walks for it go out as runs (Move)
    const auto runMode = xhunt::RunModeOf(cfg::Gated(cfg::rules.run));   // Off while the menu is on no button
    const bool approach = runMode != xhunt::RunMode::Off && (cfg::rules.runApproach && armyxmenu::On());
    std::array<uint8_t, framevr::commander::MaxArmyUnits> runners{};
    const size_t rn = runMode == xhunt::RunMode::Off ? 0 : std::min(f.unitCount, runners.size());
    for (size_t i = 0; i < rn; ++i) runners[i] = xhunt::Runs(runMode, f.units[i].petType, CachedModes(f.units[i].classId)) ? 1 : 0;
    const auto t = g.hunt.Step(f.coord, std::span(f.units, f.unitCount), {f.heroX, f.heroY}, f.nowMs, mode,
                               std::span(f.moves, f.capacity), f.emitted, std::span<const uint8_t>(runners.data(), approach ? rn : 0),
                               uint64_t(std::max(0.0f, cfg::Gated(cfg::rules.farRetryS)) * 1000.0f));   // the retry beside the mark (0: never)
    Publish(f, g.hunt, f.nowMs, std::span<const uint8_t>(runners.data(), rn));   // the rosters the game's own calls read (none once the hunt ended)
    // The one line of a hunt: it gave up (no hunter got closer or fought the mark for 15 s: unreachable or unkillable).
    if (t.end == xhunt::End::Stalled && f.say) {
        char line[300];
        std::snprintf(line, sizeof line, "Frame army x menu: hunt gave up: monster %u after %.1f s, no progress for 15 s (%d of %d hunters reached it, %d attacked it): its focus let go; %s",
                      t.endedMark, double(t.endedAfterMs) / 1000.0, t.endedReached, t.endedHunters, t.endedFought,
                      t.regrouped ? "Regroup, back to formation" : "no Regroup (Hold ground or Fall out is on)");
        f.say(line);
    }
}

// DetourRelocateUnit (Leash): the game's own leash would move this summon (the commander's
// anchored-summon suppression already let it through). A hunter in the leash roster stays where it is, until any order or recall,
// a changed mark, Fall out, town or an area change ("recall brings minions back, town portal brings minions
// back"): each checked here, at the relocation, so the release is at once.
bool Leash(seam::SummonRelocateFrame f) {
    const uint64_t now = GetTickCount64();   // the army tick's clock (StepSummonArmyServer is handed GetTickCount64)
    uint32_t ids[RosterMax];
    const size_t n = LeashRoster(ids, now);
    xhunt::LeashNow s;
    s.on = (cfg::rules.huntLeash && armyxmenu::On()) && true;
    s.hunting = xhunt::ModeOf(cfg::Gated(cfg::rules.hunt)) != xhunt::Mode::Off;
    s.unit = f.unitId; s.nowMs = now; s.atMs = g_huntAtMs.load(std::memory_order_relaxed);
    s.levelNow = n ? HeroLevel() : 0; s.levelAt = g_leashLevel.load(std::memory_order_relaxed);
    s.orderPending = f.army.orderPending; s.freeRoam = f.army.freeRoam;
    s.priorityTarget = f.army.priorityTarget; s.mark = g_huntMark.load(std::memory_order_relaxed);
    return xhunt::JudgeLeash(s, ids, n) == xhunt::LeashRelease::Held;
}

// StepSummonArmyServer's moves (Walk; the game thread, no lock): walk or run.
void Walk(seam::ArmyWalkFrame f) {
    const auto mode = xhunt::RunModeOf(cfg::Gated(cfg::rules.run));   // Off while the menu is on no button
    if (mode == xhunt::RunMode::Off || !f.walk) return;
    uint32_t classId = 0;
    const uint64_t now = GetTickCount64();
    // The mask of every army type (Runs never lets a Necromancer summon run, whatever its mask). An unknown type walks
    // without a read.
    const bool army = (framevr::companions::Bit(f.petType) & framevr::companions::ArmyTypeMask) != 0;
    const uint32_t modes = army ? ModesOf(f, classId, now) : 0;
    const bool run = xhunt::Runs(mode, f.petType, modes);
    if (run) f.walk = false;
}

// DetourServerMoveUnit (Move; any monster's own move the commander's gate let through):
// a hunting runner still on its way whose own AI walks to a place goes out running, the call ServerMoveToCoord makes for every
// hunt run (mode 15, path type 1, flags 0; companion_ai_signatures.h). Anything else: the game's call as it came.
void Move(seam::SummonMoveFrame f) {
    if (f.targetUnit || f.mode != xhunt::WalkMode) return;   // only a walk to a place (a unit-aimed move keeps the game's shape)
    if (f.x <= 0 || f.y <= 0) return;   // a place walk with no place (ServerMoveToCoord never makes one): as it came
    const int k = SlotOf(f.unitId);
    if (k < 0) return;
    if ((g_hunterFlags[size_t(k)].load(std::memory_order_relaxed) & (FlagRunner | FlagAway)) != (FlagRunner | FlagAway)) return;
    if (!(cfg::rules.runApproach && armyxmenu::On()) || xhunt::RunModeOf(cfg::Gated(cfg::rules.run)) == xhunt::RunMode::Off) return;
    f.mode = xhunt::RunMode15; f.pathType = 1; f.flags = 0;
}

// armyxmenu::GameTick (the game thread, right after the army step, no lock held): the look at each attacker a summon
// fights back at.
void Sample() { HitBackLook(); }

// DetourServerMoveUnit (Refused, don't swallow and Move to the mark instead; a summon's own move
// the commander's gate refused, before its swallow): only for a hunter of the hunt under way that stands beside its mark. The pure
// rule is xhunt::JudgeRefused; anything else leaves the frame as it came (the commander's swallow).
void Refused(seam::SummonRefusedFrame f) {
    const int k = SlotOf(f.unitId);
    if (k < 0) return;   // no hunter of a hunt under way (or a stale roster): the swallow, as before
    const bool away = (g_hunterFlags[size_t(k)].load(std::memory_order_relaxed) & FlagAway) != 0;
    if (!xhunt::JudgeRefused(cfg::rules.farRefuse && armyxmenu::On(), true, away)) return;
    f.ret = 0;
}

// TryOverrideCombatTarget (Target; per monster target query): a tracked summon the
// engine has on an attacker of its own is handed that monster, only while the army has no priority target (a mark comes first).
// The commander then looks it up by id and hands it only while it is a live monster.
void Target(seam::SummonTargetFrame f) {
    if (f.target || !f.managed) return;
    const int p = PetSlot(f.unitId);
    if (p < 0) return;
    if (const uint32_t foe = g_petSeen[size_t(p)].foe.load(std::memory_order_relaxed)) f.target = foe;
}

// combat.cpp Hit (Roll; every to-hit roll): a monster's roll on one of the army's pets, for Fight back when hit (who, where
// it stood, when).
void Roll(seam::HitRollFrame f) {
    if (!g_petCount.load(std::memory_order_acquire)) return;   // no army out, or the menu on no button (PublishPets)
    uint32_t h[3]{}, d[3]{};
    if (!Read(f.attacker, h) || h[0] != 1 || !Read(f.defender, d) || d[0] != 1) return;   // a monster's roll on a monster
    const int p = PetSlot(d[2]);
    if (p < 0) return;
    float x = 0, y = 0;
    if (!h[2] || PetSlot(h[2]) >= 0 || !Place(f.attacker, x, y)) return;   // no pet of ours; its place read
    auto& s = g_petSeen[size_t(p)];
    s.hitBy.store(h[2], std::memory_order_relaxed); s.hitX.store(x, std::memory_order_relaxed); s.hitY.store(y, std::memory_order_relaxed);
    s.hitMs.store(GetTickCount64(), std::memory_order_relaxed);
    s.hitN.fetch_add(1, std::memory_order_release);
}

}  // namespace armyxhunt
