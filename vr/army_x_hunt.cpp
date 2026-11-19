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
// The far mark trace's reads, each the commander's own, the owning game thread only: the local server game and hero and a
// unit by id (vr/melee.h, as army_x_menu.cpp's Defend scan uses them), a unit's stat (vr/combat.h: life 6, max life 7), and
// the Summon range multiplier in effect (vr/summon_range.h: 1 = the commander's pet AI adapter is not installed)
namespace melee { bool LocalServer(uintptr_t& game, uintptr_t& player); uintptr_t ServerUnit(uintptr_t game, int type, uint32_t id); }

namespace {
namespace seam = framevr::armyseam;
namespace cfg = framevr::armyseam::cfg;
void Say(const seam::ArmyMovesFrame& f, const char* line);

// The hunt's roster for the leash veto and the trace: written by Moves after each engine call (the ids first, then the count and the
// tick's time with release, so a reader that sees a count sees its ids), read by Leash and Ask (atomics: those seams are the game's
// own calls). A publish older than GapMs is none.
constexpr size_t RosterMax = framevr::commander::MaxArmyUnits;
std::array<std::atomic<uint32_t>, RosterMax> g_hunters{};
std::atomic<uint32_t> g_hunterCount{0}, g_huntMark{0};
std::atomic<uint64_t> g_huntAtMs{0};
// Per roster slot: the hunter runs (Summons run's rule on its own game data) and is still on its way (not beside the
// mark, not given up). Published with the ids. The mark's place, for the cast lines.
constexpr uint8_t FlagRunner = 1, FlagAway = 2;
std::array<std::atomic<uint8_t>, RosterMax> g_hunterFlags{};
// The leash roster: the hunters Hold the leash may keep this tick (PickLeash: within Leash reach of the hero, the level
// known, not a town, no level change), and the hero's level it was picked on. Published the same way, after the trace's roster.
std::array<std::atomic<uint32_t>, RosterMax> g_leash{};
std::atomic<uint32_t> g_leashCount{0};
std::atomic<int> g_leashLevel{0};
int g_lastLevel = 0;                                   // game thread only (Moves): the hero's level at the last publish
xhunt::LeashWhy g_leashWhy = xhunt::LeashWhy::Held;   // the last said (a change of why is logged)
int g_leashBeyond = -1;
// The hero's level (interactionstate's snapshot, 0 unknown). Read under g_tacticalLock in Moves: interactionstate takes only its own
// mutex and never calls companion_ai while it holds it, so no lock order is reversed.
int HeroLevel() { return interactionstate::CurrentLevel(gamestate::LocalPlayer(), GetTickCount64()); }
const char* LeashWhyText(xhunt::LeashWhy w) {
    switch (w) {
        case xhunt::LeashWhy::Held: return "held: hunters are kept until an order, Off or an area change";
        case xhunt::LeashWhy::NoLevel: return "your area is not known now: no hunter kept";
        case xhunt::LeashWhy::Town: return "you are in town: no hunter kept (the game brings them)";
        case xhunt::LeashWhy::LevelChanged: return "your area changed (a portal, waypoint or stairs): the roster dropped, the game brings them";
    }
    return "";
}
// runners: per f.units[i], its moves run (Summons run's rule; empty: Summons run Off).
void Publish(const seam::ArmyMovesFrame& f, const xhunt::Hunt& h, uint64_t nowMs, bool started, std::span<const uint8_t> runners) {
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
    if (n && (cfg::rules.huntLeash && armyxmenu::On()) && (started || pick.why != g_leashWhy || (pick.why == xhunt::LeashWhy::Held && (pick.beyond > 0) != (g_leashBeyond > 0)))) {
        char was[32] = "", reach[64] = "no distance limit", line[320];
        if (pick.why == xhunt::LeashWhy::LevelChanged) std::snprintf(was, sizeof was, ", was %d", g_lastLevel);
        if (const float r = cfg::Gated(cfg::rules.leashReach); r > 0) std::snprintf(reach, sizeof reach, "within %.0f sub-tiles of you", double(r));
        std::snprintf(line, sizeof line, "Frame army x menu: hunt leash: %s (area %d%s; %zu of %zu hunters held, %s)", LeashWhyText(pick.why), level,
                      was, pick.n, n, reach);
        Say(f, line);
        g_leashWhy = pick.why; g_leashBeyond = pick.beyond;
    }
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
// A published hunter's roster slot (-1: not a hunter, or no army tick refreshed the roster for GapMs). The seams that
// count for the trace call it on the game's own calls.
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
    xhunt::Mode told = xhunt::Mode::Off;   // the mode last said
    bool toldAny = false;
    uint64_t arriveLineMs = 0;
    uint64_t retryLineMs = 0; unsigned retriedSince = 0;   // Retry at the mark: since its last line
    uint32_t retryId = 0; float retryDist = 0;             // the last hunter retried since then, and how far it stood
};
State g;
constexpr uint64_t ArriveLineMs = 1000;

const char* ModeName(xhunt::Mode m) {
    return m == xhunt::Mode::All ? "Melee and ranged" : m == xhunt::Mode::Melee ? "Melee only" : "Off";
}
void Say(const seam::ArmyMovesFrame& f, const char* line) { if (f.say) f.say(line); }

// Summons run: each monster class's MonStats2 mode mask (0: unread), read on the game thread the way melee.cpp ModeMask reads it
// (the unit's stats bank at +0x1BD, the row from StatsRva 0x975B0, signature-checked, its mask at +0xF0). ReadModes is a copy of
// melee.cpp ModeMask: keep the two in step; a PR
// would expose ModeMask instead. The cache is keyed by class only : a class's modes are its MonStats2 row's, whatever bank the
// unit's stats come from.
// ok: an accepted mask (walk bit set) is cached for good; a failed read is tried again at most once a second.
struct ClassModes { uint32_t classId = 0; uint32_t modes = 0; bool ok = false; bool toldFail = false; uint64_t triedMs = 0; };
std::array<ClassModes, 24> g_classModes{};
size_t g_classModesN = 0, g_classModesNext = 0;
constexpr uint64_t ModesRetryMs = 1000;
int g_statsOk = -1;   // StatsRva's signature: -1 not checked yet
uint32_t ReadModes(uintptr_t unit, uint32_t classId) {
    if (g_statsOk < 0) g_statsOk = d2rsig::Check(bowvr::native::StatsRva, bowvr::native::StatsSig, sizeof bowvr::native::StatsSig) ? 1 : 0;
    uint8_t bank = 0; uint32_t mask = 0;
    if (!g_statsOk || classId > 4095 || !Read(unit + 0x1bd, bank) || bank > 3) return 0;
    const auto fn = reinterpret_cast<uintptr_t (*)(uint8_t, int)>(d2rsig::Addr(bowvr::native::StatsRva));
    const uintptr_t row = fn ? fn(bank, int(classId)) : 0;
    return Read(row + 0xf0, mask) ? mask : 0;
}
// The mask of this summon's class (cached once accepted), and its class id. Told once per class: its accepted read, or its first
// failed one (and the accepted read after it).
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
    if (f.say && (e->ok || !e->toldFail)) {
        char line[300];
        std::snprintf(line, sizeof line, "Frame army x menu: run check: summon %u, %s (type %u, class %u): MonStats2 modes 0x%08x%s",
                      f.unitId, xhunt::PetName(f.petType), unsigned(f.petType), classId, modes,
                      !e->ok ? " (unread: no walk bit or no stats row; tried again once a second; wolves still run, the others walk)" :
                      (modes & xhunt::RunModeBit) ? " (walk and run: it can run)" : " (walk, no run: it walks)");
        f.say(line);
        e->toldFail = !e->ok;
    }
    return e->modes;
}
struct RunState {
    xhunt::RunMode told = xhunt::RunMode::Off; bool toldAny = false;
};
RunState g_run;
// A class's accepted mask (0: none read yet), for the army tick's runners: the same decision Walk makes for that summon's move.
uint32_t CachedModes(uint32_t classId) {
    for (size_t k = 0; k < g_classModesN; ++k) if (g_classModes[k].classId == classId && g_classModes[k].ok) return g_classModes[k].modes;
    return 0;
}

// ---- Far mark trace ----
// Every army pet the army step sampled (hunting or not), kept by id on the game thread (Moves writes, the sampler reads: the same
// thread), and what the game's own seams saw of each since the sampler's last look (atomics, in a slot the pet keeps by its unit
// id for as long as it is sampled: slots by roster position lost their counts whenever a summon died or
// was cast, and a pet that had swung then read IDLE WHILE HIT).
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
// Refuse, don't swallow / Move to the mark instead: what Refused did since the last line (the game's own calls: atomics).
std::atomic<uint32_t> g_refFail{0}, g_refUnit{0};
std::atomic<int> g_refMode{-1}, g_refX{0}, g_refY{0};
uint64_t g_refLineMs = 0;
void RefusedLine(const seam::ArmyMovesFrame& f, uint32_t mark) {
    if (g_refLineMs && f.nowMs >= g_refLineMs && f.nowMs - g_refLineMs < 1000) return;
    const uint32_t fail = g_refFail.exchange(0, std::memory_order_relaxed);
    const double sinceS = g_refLineMs && f.nowMs > g_refLineMs && f.nowMs - g_refLineMs < 60000 ? double(f.nowMs - g_refLineMs) / 1000.0 : 0.0;
    g_refLineMs = f.nowMs;
    if (!fail) return;
    const int mode = g_refMode.load(std::memory_order_relaxed);
    char line[420];
    std::snprintf(line, sizeof line, "Frame army x menu: hunt refused: in the last %.1f s, %u refused moves of hunters beside monster %u were answered failed to the game, not swallowed (the last: summon %u, %s to %d %d)",
                  sinceS, fail, mark, g_refUnit.load(std::memory_order_relaxed), mode >= 0 && mode < 16 ? xhunt::ModeName(uint8_t(mode)) : "mode ?",
                  g_refX.load(std::memory_order_relaxed), g_refY.load(std::memory_order_relaxed));
    Say(f, line);
}

// Fight back when hit: the engine (game thread, Moves), the attackers it has summons on (for the look on the game tick that
// follows) and that look's answer (each attacker's place, or dead), read by the next army tick.
xhunt::HitBack g_hb;
bool g_hbWas = false;
std::array<uint32_t, RosterMax> g_hbLook{};
size_t g_hbLookN = 0;
std::array<xhunt::HitBack::Foe, RosterMax> g_hbFoes{};
size_t g_hbFoeN = 0;
uint64_t g_hbSecondMs = 0;
int g_hbLines = 0;
constexpr int HitBackLinesPerSecond = 4;
const char* WalkText(uint8_t walk) {
    return walk == 1 ? "its walk was sent" : walk == 2 ? "no walk sent this tick (the move list was full: it goes next)" : "no walk needed (it stands beside it)";
}
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
    const auto o = g_hb.Step(f.coord, std::span(f.units, f.unitCount), f.nowMs, on, cfg::Gated(cfg::rules.hitBackReach), forMs, retryMs,
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
    if (f.nowMs < g_hbSecondMs || f.nowMs - g_hbSecondMs >= 1000) { g_hbSecondMs = f.nowMs; g_hbLines = 0; }
    char line[400];
    for (int k = 0; k < o.startedN && k < int(xhunt::HitBack::Notes) && g_hbLines < HitBackLinesPerSecond; ++k, ++g_hbLines) {
        const auto& s = o.started[size_t(k)];
        std::snprintf(line, sizeof line, "Frame army x menu: hit back: summon %u (%s) was hit by monster %u, %.1f sub-tiles away (%.1f from its spot): it goes for it, the others keep formation (%d fighting back now); %s",
                      s.pet, xhunt::PetName(s.petType), s.foe, s.dist, s.fromSpot, o.engaged, WalkText(s.walk));
        Say(f, line);
    }
    for (int k = 0; k < o.endedN && k < int(xhunt::HitBack::Notes) && g_hbLines < HitBackLinesPerSecond; ++k, ++g_hbLines) {
        const auto& e = o.ended[size_t(k)];
        std::snprintf(line, sizeof line, "Frame army x menu: hit back: over for summon %u and monster %u after %.1f s: %s; it %s (%d fighting back now)",
                      e.pet, e.foe, double(e.afterMs) / 1000.0, xhunt::HitBack::EndText(e.end), e.fought ? "fought it" : "did not get to fight it", o.engaged);
        Say(f, line);
    }
}
// The look at each fought attacker (Sample: the game tick, no lock): found by id (the commander's lookup), its header checked (a live
// monster), its place read; dead or not found: said so, and the engine lets it go.
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
    const xhunt::Mode mode = xhunt::ModeOf(cfg::Gated(cfg::rules.hunt));   // Off with Army X menu, Summons or Ideas Off
    char line[360];
    if (mode != g.told && (g.toldAny || mode != xhunt::Mode::Off)) {
        std::snprintf(line, sizeof line, "Frame army x menu: hunt the mark: %s%s", ModeName(mode),
                      mode == xhunt::Mode::Off ? " (as before: a priority target only re-aims the army; Step keeps each summon on its leash)" :
                      mode == xhunt::Mode::Melee ? " (melee summons walk to the mark; Skeleton Mages as before)" : " (melee beside the mark, Skeleton Mages to their stand-off)");
        Say(f, line);
        g.told = mode; g.toldAny = true;
    }
    if (mode == xhunt::Mode::Off && !g.hunt.Mark() && !g.hunt.Returning()) return;   // Off, nothing under way: one branch
    // Summons run's decision for each sampled summon (the same as Walk's for its move: its type and its class's mask, once read),
    // and Run the whole approach: Step sends a runner seen walking again (runners), the AI's own walks for it go out as runs (Move)
    const auto runMode = xhunt::RunModeOf(cfg::Gated(cfg::rules.run));   // Off with Army X menu, Summons or Ideas Off
    const bool approach = runMode != xhunt::RunMode::Off && (cfg::rules.runApproach && armyxmenu::On());
    std::array<uint8_t, framevr::commander::MaxArmyUnits> runners{};
    const size_t rn = runMode == xhunt::RunMode::Off ? 0 : std::min(f.unitCount, runners.size());
    for (size_t i = 0; i < rn; ++i) runners[i] = xhunt::Runs(runMode, f.units[i].petType, CachedModes(f.units[i].classId)) ? 1 : 0;
    const auto t = g.hunt.Step(f.coord, std::span(f.units, f.unitCount), {f.heroX, f.heroY}, f.nowMs, mode,
                               std::span(f.moves, f.capacity), f.emitted, std::span<const uint8_t>(runners.data(), approach ? rn : 0),
                               uint64_t(std::max(0.0f, cfg::Gated(cfg::rules.farRetryS)) * 1000.0f));   // the retry beside the mark (0: never)
    Publish(f, g.hunt, f.nowMs, t.started, std::span<const uint8_t>(runners.data(), rn));   // the leash veto's and the trace's rosters (none once the hunt ended); each hunt says its leash once
    if (t.end != xhunt::End::None) {
        const char* why = "";
        switch (t.end) {
            case xhunt::End::Gone: why = t.regrouped ? "gone with no order (killed or lost: the commander's liveness check let it go): Regroup, back to formation" :
                                                       "gone with no order (killed or lost); no Regroup (Hold ground or Fall out is on)"; break;
            case xhunt::End::NewOrder: why = "a new order cleared it (the commander re-forms the army)"; break;
            case xhunt::End::NewMark: why = "a new mark"; break;
            case xhunt::End::Off: why = "Hunt the mark taken off its button (as before from the next tick)"; break;
            case xhunt::End::Gap: why = "no army tick for 2 s (a load, or the army gone): forgotten"; break;
            case xhunt::End::Stalled: why = t.regrouped ? "gave up: no progress for 15 s (no hunter got a new best distance or fought it; unreachable or unkillable): its focus let go, Regroup, back to formation" :
                                                          "gave up: no progress for 15 s (no hunter got a new best distance or fought it): its focus let go; no Regroup (Hold ground or Fall out is on)"; break;
            default: break;
        }
        std::snprintf(line, sizeof line, "Frame army x menu: hunt over: monster %u after %.1f s, %d of %d hunters reached it, %d attacked it; %s",
                      t.endedMark, double(t.endedAfterMs) / 1000.0, t.endedReached, t.endedHunters, t.endedFought, why);
        Say(f, line);
        // The proof they went: each hunter's distance to the mark at the start > its best > at the end, and
        // whether it fought beside it; 8 to a line.
        for (int i0 = 0; i0 < t.summaryN; i0 += 8) {
            int len = std::snprintf(line, sizeof line, "Frame army x menu: hunt over: monster %u hunters %d-%d of %d (start > best > end sub-tiles):",
                                    t.endedMark, i0 + 1, i0 + 8 < t.summaryN ? i0 + 8 : t.summaryN, t.summaryN);
            for (int i = i0; i < t.summaryN && i < i0 + 8 && len > 0 && len < int(sizeof line); ++i) {
                const auto& s = t.summary[size_t(i)];
                len += std::snprintf(line + len, sizeof line - size_t(len), " %u: %.1f > %.1f > %.1f%s%s;", s.id, s.start, s.best, s.end,
                                     s.fought ? " attacked" : "", s.gaveUp ? " gave up" : "");
            }
            Say(f, line);
        }
    }
    if (t.started) {
        std::snprintf(line, sizeof line,
                      "Frame army x menu: hunt: monster %u, %.1f sub-tiles from you: %d summons sent (%d melee beside it, %d ranged to %.0f), %d kept in formation; nearest %.1f, farthest %.1f sub-tiles; %d moves now (%d walks back dropped)",
                      t.mark, t.heroDist, t.hunters, t.melee, t.ranged, xhunt::RangedKeep, t.kept, t.nearest, t.farthest, t.sent, t.dropped);
        Say(f, line);
        g.arriveLineMs = 0;
    }
    if (t.mark) {
        if (t.jumped) {   // the game's own leash (DetourRelocateUnit; vetoed while Hold the leash is On) or a relocation moved a hunter
            std::snprintf(line, sizeof line, "Frame army x menu: hunt: %d hunter%s moved, not walked (summon %u: %.1f -> %.1f sub-tiles from monster %u in one tick; the game's own leash? Hold the leash %s)",
                          t.jumped, t.jumped == 1 ? "" : "s", t.jumpId, t.jumpFrom, t.jumpTo, t.mark, (cfg::rules.huntLeash && armyxmenu::On()) ? "On" : "Off");
            Say(f, line);
        }
        g.retriedSince += unsigned(t.retried);
        if (t.retried) { g.retryId = t.retryId; g.retryDist = t.retryDist; }
        if (g.retriedSince && (!g.retryLineMs || f.nowMs < g.retryLineMs || f.nowMs - g.retryLineMs >= 1000)) {   // Retry at the mark: at most a line a second
            std::snprintf(line, sizeof line, "Frame army x menu: hunt retry: %u hunter moves sent again beside monster %u (standing idle for %.2f s; the last: summon %u, %.1f sub-tiles from it): a short step beside it, so the game thinks for it again",
                          g.retriedSince, t.mark, double(cfg::Gated(cfg::rules.farRetryS)), g.retryId, g.retryDist);
            Say(f, line);
            g.retryLineMs = f.nowMs; g.retriedSince = 0;
        }
        RefusedLine(f, t.mark);
        if (t.gaveUpNew) {   // a hunter that got no closer gives up alone (back to its spot, the commander's watchdog again)
            std::snprintf(line, sizeof line, "Frame army x menu: hunt: %d hunter%s gave up on monster %u (no new best distance in 15 s): back to %s spot; %d still hunting",
                          t.gaveUpNew, t.gaveUpNew == 1 ? "" : "s", t.mark, t.gaveUpNew == 1 ? "its" : "their", t.hunters);
            Say(f, line);
        }
        if (t.arrivedNew && (f.nowMs - g.arriveLineMs >= ArriveLineMs || t.arrived == t.hunters)) {
            g.arriveLineMs = f.nowMs;
            std::snprintf(line, sizeof line, "Frame army x menu: hunt: %d of %d at monster %u (%d just now; nearest %.1f sub-tiles); the game's AI attacks it",
                          t.arrived, t.hunters, t.mark, t.arrivedNew, t.nearest);
            Say(f, line);
        }
    }
    if (t.returned) {
        std::snprintf(line, sizeof line, "Frame army x menu: hunt return: %d of %d former hunters back on their spots %.1f s after monster %u%s",
                      t.returnFormed, t.returnOf, double(t.returnMs) / 1000.0, t.returnMark,
                      t.returnTimedOut ? " (the rest still on their way after 10 s: the commander's watchdog may move them now)" : "");
        Say(f, line);
    }
}

// DetourRelocateUnit (Leash): the game's own leash would move this summon (the commander's
// anchored-summon suppression already let it through). A hunter in the leash roster stays where it is, until any order or recall,
// a changed mark, Off, Fall out, town or an area change ("recall brings minions back, town portal brings minions
// back"): each checked here, at the relocation, so the release is at once; each release of a hunter is said (once a second a reason).
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
    const auto verdict = xhunt::JudgeLeash(s, ids, n);
    if (verdict != xhunt::LeashRelease::Held) {
        if (verdict != xhunt::LeashRelease::Off && verdict != xhunt::LeashRelease::NotHunter && f.say) {   // a hunter let go: why
            static std::array<std::atomic<uint64_t>, 16> saidMs{};
            auto& last = saidMs[size_t(verdict) & 15];
            if (const uint64_t l = last.load(std::memory_order_relaxed); !l || now < l || now - l >= 1000) {
                last.store(now, std::memory_order_relaxed);
                char line[300];
                std::snprintf(line, sizeof line, "Frame army x menu: hunt leash: released: the game moves summon %u (%s; area %d, the roster's %d)",
                              f.unitId, xhunt::LeashReleaseText(verdict), s.levelNow, s.levelAt);
                f.say(line);
            }
        }
        return false;
    }
    static std::atomic<uint64_t> toldMs{0};
    static std::atomic<unsigned> held{0};
    const unsigned count = held.fetch_add(1, std::memory_order_relaxed) + 1;
    if (const uint64_t last = toldMs.load(std::memory_order_relaxed); f.say && (now < last || now - last >= 1000)) {
        toldMs.store(now, std::memory_order_relaxed);
        held.store(0, std::memory_order_relaxed);
        char line[260];
        std::snprintf(line, sizeof line, "Frame army x menu: hunt leash: the game's own leash would move summon %u to (%d, %d): kept, it hunts monster %u (%u kept since the last line; at most one line a second)",
                      f.unitId, f.x, f.y, g_huntMark.load(std::memory_order_relaxed), count);
        f.say(line);
    }
    return true;
}

// StepSummonArmyServer's moves (Walk; the game thread, no lock): walk or run.
void Walk(seam::ArmyWalkFrame f) {
    const auto mode = xhunt::RunModeOf(cfg::Gated(cfg::rules.run));   // Off with Army X menu, Summons or Ideas Off
    if (mode != g_run.told && (g_run.toldAny || mode != xhunt::RunMode::Off) && f.say) {
        static constexpr const char* names[3] = {"Off (every summon walks, as before)", "Wolves (Spirit and Dire wolves run; the rest walk)",
                                                  "Every runner (wolves, and the Valkyrie, Grizzly and Shadows whose game data has a run; Necromancer summons walk)"};
        char line[200];
        std::snprintf(line, sizeof line, "Frame army x menu: run: %s", names[int(mode)]);
        f.say(line);
        g_run.told = mode; g_run.toldAny = true;
    }
    if (mode == xhunt::RunMode::Off || !f.walk) return;
    uint32_t classId = 0;
    const uint64_t now = GetTickCount64();
    // The mask of every army type (a Necromancer summon's too, logged only, so the run check line shows the run bit clear
    // for a skeleton, the proof of bit 15; Runs never lets one run). An unknown type walks without a read.
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
    const unsigned pathType = f.pathType, flags = f.flags;
    f.mode = xhunt::RunMode15; f.pathType = 1; f.flags = 0;
    static std::atomic<bool> told{false};
    if (f.say && !told.exchange(true, std::memory_order_relaxed)) {
        char line[300];
        std::snprintf(line, sizeof line, "Frame army x menu: run approach: summon %u's own walk to a place (path type %u, flags 0x%04x) sent as a run: ServerMoveUnit mode 15, path type 1, flags 0, as ServerMoveToCoord's (once a session)",
                      f.unitId, pathType, flags);
        f.say(line);
    }
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
    g_refUnit.store(f.unitId, std::memory_order_relaxed); g_refMode.store(f.mode, std::memory_order_relaxed);
    g_refX.store(f.x, std::memory_order_relaxed); g_refY.store(f.y, std::memory_order_relaxed);
    g_refFail.fetch_add(1, std::memory_order_relaxed);
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

// combat.cpp Hit (Roll; every to-hit roll): a hunter's roll and on whom, for
// the swing line; and a monster's roll on a pet, for the far mark trace and for Fight back when hit (who, where it stood, when).
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
