// [game_patches]: see game_patches.h. Everything here but Configure runs on the draw thread, between frames.
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>
#include "game_patches.h"
#include "game_patch_signatures.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include "../steamvr/game_patch_plan.h"

namespace gamepatches {
namespace {
using namespace abi;
using gamepatch::SigMatches;

enum class State : uint8_t { Off, Waiting, Applied, Refused };
const char* Name(State s) {
    return s == State::Off ? "off" : s == State::Applied ? "applied" : s == State::Refused ? "refused" : "waiting for the game's code";
}

void Say(const std::string& text) { framevr::logging::Write(("Frame game patches: " + text).c_str()); }

// The game's code is decrypted as it is first run. A place whose bytes differ is looked at again on later
// frames, and given up after this many looks (the code of a frame's draw has run after the first frames).
constexpr uint32_t MaxLooks = 40, LookEvery = 16;

struct Patch {
    const char* key;                    // the name in [game_patches]
    std::atomic<bool> wanted{false};
    State state = State::Off;           // draw thread only
    uint32_t looks = 0;
    explicit Patch(const char* k) : key(k) {}
};

// One stretch of the game's code as it is now. False (and `differs` set) when it cannot be read or is not
// what the fingerprint says.
template <size_t N>
bool Holds(uint64_t rva, const uint16_t (&sig)[N], uint8_t* live, uint64_t& differs) {
    const uintptr_t at = d2rsig::Addr(rva);
    if (at && framevr::ReadMemory(live, reinterpret_cast<const void*>(at), N) && SigMatches(live, sig, N)) return true;
    differs = rva;
    return false;
}

std::string Hex(uint64_t v) {
    char text[24];
    snprintf(text, sizeof text, "0x%llX", static_cast<unsigned long long>(v));
    return text;
}

// A place that did not hold: looked at again later, or given up (said once).
void NotYet(Patch& p, uint64_t differs) {
    if (++p.looks < MaxLooks) { p.state = State::Waiting; return; }
    p.state = State::Refused;
    Say(std::string(p.key) + " refused: bytes differ at RVA " + Hex(differs) + " (the game's code there is not what this mod knows; the game is left as it is)");
}
void Refuse(Patch& p, const std::string& why) {
    p.state = State::Refused;
    Say(std::string(p.key) + " refused: " + why + " (nothing more is written)");
}

// --- transparent_filter -----------------------------------------------------------------------------------
// One page for the cave, within a relative jump of `near`: free address space is looked for above the
// place, then below it, as hook libraries do. Never freed: a few bytes, for the life of the game.
void* PageNear(uintptr_t around) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    const uintptr_t reach = 0x7FE00000;   // 2 GB less 2 MB: the jumps are a few bytes from `around` and the page's start
    const uintptr_t lowest = std::max<uintptr_t>(reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress), around > reach ? around - reach : 0);
    const uintptr_t highest = std::min<uintptr_t>(reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress), around + reach);
    const SIZE_T size = 0x1000;
    const auto up = [gran](uintptr_t v) { return (v + gran - 1) / gran * gran; };
    MEMORY_BASIC_INFORMATION mbi{};
    int steps = 0;
    for (uintptr_t at = up(around); at + size <= highest && steps < 4096; ++steps) {
        if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof mbi)) break;
        if (mbi.State == MEM_FREE) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return p;
            at += gran;   // refused here: the next place an allocation may start
            continue;
        }
        at = up(reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize);   // past this used stretch
    }
    steps = 0;
    for (uintptr_t at = around / gran * gran; at >= lowest + gran && steps < 4096; ++steps) {
        at -= gran;
        if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof mbi)) break;
        if (mbi.State == MEM_FREE) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return p;
            continue;
        }
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.AllocationBase ? mbi.AllocationBase : mbi.BaseAddress);
        at = base / gran * gran;   // below this used stretch next
    }
    return nullptr;
}

Patch filter{"transparent_filter"};
uint8_t* filterCave = nullptr;                              // the page; its code is written once
uint8_t filterWas[gamepatch::FilterSiteBytes] = {};         // the game's 8 bytes
uint8_t filterNow[gamepatch::FilterSiteBytes] = {};         // ours

void ApplyFilter() {
    uint8_t head[sizeof FilterHeadSig / 2], loop[sizeof FilterLoopSig / 2], skip[sizeof FilterSkipSig / 2];
    uint64_t differs = 0;
    if (!Holds(FilterHeadRva, FilterHeadSig, head, differs) || !Holds(FilterLoopRva, FilterLoopSig, loop, differs) ||
        !Holds(FilterSkipRva, FilterSkipSig, skip, differs)) return NotYet(filter, differs);
    // Nothing in the function may jump into the middle of the instruction the jump replaces.
    const uintptr_t function = d2rsig::Addr(FilterFunctionRva), site = d2rsig::Addr(FilterLoopRva), skipAt = d2rsig::Addr(FilterSkipRva);
    std::vector<uint8_t> body(size_t(FilterFunctionEnd - FilterFunctionRva));
    if (!function || !framevr::ReadMemory(body.data(), reinterpret_cast<const void*>(function), body.size())) return NotYet(filter, FilterFunctionRva);
    if (gamepatch::BranchesInto(body.data(), body.size(), function, site + 1, site + gamepatch::FilterLoadBytes - 1))
        return Refuse(filter, "a jump in the game's function at RVA " + Hex(FilterFunctionRva) + " may land inside the instruction at RVA " + Hex(FilterLoopRva));
    if (!filterCave) {
        auto* page = static_cast<uint8_t*>(PageNear(site));
        if (!page) return Refuse(filter, "no free memory within a relative jump of the game's code for the few bytes of new code");
        const gamepatch::FilterPlan plan = gamepatch::PlanFilter(site, skipAt, reinterpret_cast<uintptr_t>(page), loop[gamepatch::FilterLoadBytes]);
        DWORD old = 0;
        if (!plan.ok) { VirtualFree(page, 0, MEM_RELEASE); return Refuse(filter, "the new code would be out of a relative jump's reach"); }
        std::memset(page, 0xCC, 0x1000);
        std::memcpy(page, plan.cave, sizeof plan.cave);
        if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return Refuse(filter, "the new code's page could not be made runnable"); }
        FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
        std::memcpy(filterWas, loop, sizeof filterWas);
        std::memcpy(filterNow, plan.site, sizeof filterNow);
        filterCave = page;
    }
    // The loader's checked write: the 8 bytes must still be the game's. One aligned 8 bytes, between frames.
    if (!d2rsig::Patch(FilterLoopRva, filterWas, sizeof filterWas, filterNow, sizeof filterNow)) return Refuse(filter, "the loader refused the write at RVA " + Hex(FilterLoopRva));
    uint8_t back[sizeof filterNow] = {};
    if (!framevr::ReadMemory(back, reinterpret_cast<const void*>(site), sizeof back) || std::memcmp(back, filterNow, sizeof back))
        return Refuse(filter, "the loader took the write at RVA " + Hex(FilterLoopRva) + " but the bytes there are not what was written");
    filter.state = State::Applied;
    char text[320];
    snprintf(text, sizeof text, "transparent_filter applied: the transparent pass's Ghost filter tests a draw record's flags before it reads "
             "the record's constants (jump at RVA 0x%llX to %u bytes of new code at 0x%llX; %u bytes of the game's code proven first)",
             static_cast<unsigned long long>(FilterLoopRva), unsigned(gamepatch::FilterCaveBytes), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(filterCave)),
             unsigned(sizeof head + sizeof loop + sizeof skip));
    Say(text);
}

void RemoveFilter() {
    if (d2rsig::Patch(FilterLoopRva, filterNow, sizeof filterNow, filterWas, sizeof filterWas)) {
        filter.state = State::Off;
        Say("transparent_filter taken out: the game's own bytes are back at RVA " + Hex(FilterLoopRva));
    } else {
        filter.state = State::Refused;   // stays as it is; not tried again
        Say("transparent_filter could NOT be taken out: the loader refused the write at RVA " + Hex(FilterLoopRva) + "; it stays in until the game is closed");
    }
}

// --- profiler_accounting ---------------------------------------------------------------------------------------
Patch account{"profiler_accounting"};

void ApplyAccount() {
    uint8_t call[sizeof AccountCallSig / 2], body[sizeof AccountSig / 2];
    uint64_t differs = 0;
    if (!Holds(AccountCallRva, AccountCallSig, call, differs) || !Holds(AccountRva, AccountSig, body, differs)) return NotYet(account, differs);
    const uint8_t was[2] = {call[AccountJumpAt], call[AccountJumpAt + 1]}, now[2] = {AccountJumpAlways, call[AccountJumpAt + 1]};
    if (!d2rsig::Patch(AccountCallRva + AccountJumpAt, was, sizeof was, now, sizeof now)) return Refuse(account, "the loader refused the write at RVA " + Hex(AccountCallRva + AccountJumpAt));
    account.state = State::Applied;
    Say("profiler_accounting applied: a draw no longer takes the game's GPU profiler lock to be counted (the jump at RVA " + Hex(AccountCallRva + AccountJumpAt) +
        " is taken always; " + std::to_string(sizeof call + sizeof body) + " bytes of the game's code proven first). The game's own profiling windows would show no draws or primitives per scope");
}

void RemoveAccount() {
    const uint8_t now[2] = {AccountJumpAlways, uint8_t(AccountCallSig[AccountJumpAt + 1])}, was[2] = {uint8_t(AccountCallSig[AccountJumpAt]), uint8_t(AccountCallSig[AccountJumpAt + 1])};
    if (d2rsig::Patch(AccountCallRva + AccountJumpAt, now, sizeof now, was, sizeof was)) {
        account.state = State::Off;
        Say("profiler_accounting taken out: the game's own bytes are back at RVA " + Hex(AccountCallRva + AccountJumpAt));
    } else {
        account.state = State::Refused;
        Say("profiler_accounting could NOT be taken out: the loader refused the write at RVA " + Hex(AccountCallRva + AccountJumpAt) + "; it stays in until the game is closed");
    }
}

// --- draw_debug_names ------------------------------------------------------------------------------------------
Patch names{"draw_debug_names"};
int32_t levelWas = 0;          // the game's level before it was lowered
bool levelWritten = false;

void ApplyNames() {
    uint8_t get[sizeof LevelGetSig / 2], set[sizeof LevelSetSig / 2], begin[sizeof DrawNameBeginSig / 2], end[sizeof DrawNameEndSig / 2],
        eventBegin[sizeof EventBeginSig / 2], eventEnd[sizeof EventEndSig / 2];
    uint64_t differs = 0;
    if (!Holds(LevelGetRva, LevelGetSig, get, differs) || !Holds(LevelSetRva, LevelSetSig, set, differs) ||
        !Holds(DrawNameBeginRva, DrawNameBeginSig, begin, differs) || !Holds(DrawNameEndRva, DrawNameEndSig, end, differs) ||
        !Holds(EventBeginRva, EventBeginSig, eventBegin, differs) || !Holds(EventEndRva, EventEndSig, eventEnd, differs)) return NotYet(names, differs);
    const uintptr_t at = d2rsig::Addr(LevelRva);
    int32_t level = 0;
    if (!at || !framevr::ReadMemory(&level, reinterpret_cast<const void*>(at), sizeof level)) return NotYet(names, LevelRva);
    switch (gamepatch::LevelDecision(level)) {
    case gamepatch::Level::Refuse:
        return Refuse(names, "the number at RVA " + Hex(LevelRva) + " is " + std::to_string(level) + ", not a debug event level");
    case gamepatch::Level::Leave:
        names.state = State::Applied;
        levelWritten = false;
        Say("draw_debug_names applied: nothing to change, the game's debug event level is " + std::to_string(level) + " - it does not name its draws");
        return;
    case gamepatch::Level::Lower: break;
    }
    const int32_t lowered = gamepatch::LevelWithoutDrawNames;
    int32_t back = 0;
    if (!framevr::WriteMemory(reinterpret_cast<void*>(at), &lowered, sizeof lowered) ||
        !framevr::ReadMemory(&back, reinterpret_cast<const void*>(at), sizeof back) || back != lowered)
        return Refuse(names, "the debug event level at RVA " + Hex(LevelRva) + " could not be written");
    levelWas = level;
    levelWritten = true;
    names.state = State::Applied;
    Say("draw_debug_names applied: the game's debug event level " + std::to_string(level) + " -> " + std::to_string(lowered) +
        " (the int at RVA " + Hex(LevelRva) + "): a draw is no longer given a formatted name and its own debug event, the passes keep theirs");
}

void RemoveNames() {
    names.state = State::Off;
    if (!levelWritten) { Say("draw_debug_names off: nothing had been changed"); return; }
    levelWritten = false;
    const uintptr_t at = d2rsig::Addr(LevelRva);
    int32_t level = 0;
    if (at && framevr::ReadMemory(&level, reinterpret_cast<const void*>(at), sizeof level) && level == gamepatch::LevelWithoutDrawNames &&
        framevr::WriteMemory(reinterpret_cast<void*>(at), &levelWas, sizeof levelWas))
        Say("draw_debug_names taken out: the game's debug event level is " + std::to_string(levelWas) + " again");
    else
        Say("draw_debug_names off: the game's debug event level is no longer what this mod wrote (" + std::to_string(level) + "); left as it is");
}

// --- all of them ----------------------------------------------------------------------------------------------
struct Entry { Patch* patch; void (*apply)(); void (*remove)(); };
const Entry entries[] = {
    {&filter, &ApplyFilter, &RemoveFilter},
    {&account, &ApplyAccount, &RemoveAccount},
    {&names, &ApplyNames, &RemoveNames},
};

std::atomic<bool> dirty{false};       // the settings changed, or something still waits for the game's code
std::atomic<bool> configured{false};
bool firstFrame = true;
uint32_t frames = 0;

std::string Summary() {
    std::string all;
    for (const Entry& e : entries) {
        if (!all.empty()) all += ", ";
        all += std::string(e.patch->key) + " " + Name(e.patch->state);
    }
    return all;
}
}  // namespace

void Configure(const Wanted& wanted) {
    const bool want[] = {wanted.transparentFilter, wanted.profilerAccounting, wanted.drawDebugNames};
    bool changed = false;
    for (size_t i = 0; i < std::size(entries); ++i)
        if (entries[i].patch->wanted.exchange(want[i]) != want[i]) changed = true;
    if (!configured.exchange(true)) {
        std::string all;
        for (size_t i = 0; i < std::size(entries); ++i) all += std::string(i ? ", " : "") + entries[i].patch->key + "=" + (want[i] ? "1" : "0");
        Say("[game_patches] " + all + " - what is 1 goes in between frames, from the first frame drawn in the world; each says so here");
        changed = true;
    }
    if (changed) dirty.store(true);
}

void BetweenFrames() {
    if (!dirty.load(std::memory_order_relaxed)) return;
    if (!configured.load()) return;
    const bool look = firstFrame || frames++ % LookEvery == 0;   // what waits for the game's code: not every frame
    bool waiting = false;
    dirty.store(false);   // before the settings are read: a change after this point is seen on the next frame
    for (const Entry& e : entries) {
        Patch& p = *e.patch;
        const bool want = p.wanted.load();
        if (want && p.state == State::Off) { p.looks = 0; p.state = State::Waiting; }
        if (want && p.state == State::Waiting && look) e.apply();
        else if (!want && p.state == State::Applied) e.remove();
        else if (!want && p.state == State::Waiting) p.state = State::Off;
        if (firstFrame && !want) Say(std::string(p.key) + " off ([game_patches] " + p.key + "=0)");
        if (p.state == State::Waiting) waiting = true;
    }
    if (firstFrame) {
        firstFrame = false;
        Say("first frame: " + Summary());
    }
    if (waiting) dirty.store(true);
}

}  // namespace gamepatches
