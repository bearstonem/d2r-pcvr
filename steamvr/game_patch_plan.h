#pragma once
// [game_patches]: small changes to the game's own render code (vr/game_patches.cpp, steamvr/PERFORMANCE.md
// "Render task patches"). What can be worked out without the game is here, and tested (game_patch_plan_test.cpp):
// does the code at a place look as this mod knows it, and the bytes a patch writes for given addresses.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gamepatch {

// A fingerprint is a row of these: a byte value, or Any for a byte that is not compared.
inline constexpr uint16_t Any = 0x100;

// True when every compared byte of `live` is the fingerprint's. Nothing to compare is never a match.
inline bool SigMatches(const uint8_t* live, const uint16_t* sig, size_t size) noexcept {
    if (!live || !sig || !size) return false;
    for (size_t i = 0; i < size; ++i) {
        if (sig[i] == Any) continue;
        if (sig[i] > 0xFF || live[i] != uint8_t(sig[i])) return false;
    }
    return true;
}

// The 32-bit displacement of a relative jump whose next instruction is at `next` and which goes to `to`.
// False when `to` is farther than a relative jump reaches.
inline bool Rel32(uint64_t next, uint64_t to, int32_t& out) noexcept {
    const int64_t d = int64_t(to - next);
    if (d < INT32_MIN || d > INT32_MAX) return false;
    out = int32_t(d);
    return true;
}

// How many relative jumps or calls in `code` (which sits at address `at`) would land in [lo, hi]. Every
// byte offset is read as the start of a jump, short and near, conditional or not, so data and the insides of
// other instructions can be counted too: it can say "one" where there is none, never "none" where there is one.
inline size_t BranchesInto(const uint8_t* code, size_t size, uint64_t at, uint64_t lo, uint64_t hi) noexcept {
    size_t found = 0;
    if (!code) return 0;
    const auto in = [&](uint64_t target) { return target >= lo && target <= hi; };
    for (size_t i = 0; i < size; ++i) {
        const uint8_t b = code[i];
        if (i + 2 <= size && (b == 0xEB || (b >= 0x70 && b <= 0x7F) || (b >= 0xE0 && b <= 0xE3)) &&
            in(at + i + 2 + int64_t(int8_t(code[i + 1])))) ++found;
        int32_t d = 0;
        if (i + 5 <= size && (b == 0xE8 || b == 0xE9)) {
            std::memcpy(&d, code + i + 1, 4);
            if (in(at + i + 5 + int64_t(d))) ++found;
        }
        if (i + 6 <= size && b == 0x0F && code[i + 1] >= 0x80 && code[i + 1] <= 0x8F) {
            std::memcpy(&d, code + i + 2, 4);
            if (in(at + i + 6 + int64_t(d))) ++found;
        }
    }
    return found;
}

// The transparent pass's record filter, flags first.
//
// The game's loop over draw records begins with a 7-byte load of the pointer at record + 0xA8 (rsi is the
// record), reads a number through it, and only then tests the record's flags at record + 8 against
// 0x21800000 and leaves for the next record when none is set. The plan puts the flag test in front:
//
//   site:  jmp cave ; 2 bytes of filler (the load's last two; nothing lands on them)
//   cave:  test dword ptr [rsi + 8], 0x21800000
//          jz   skip          ; where the game's own test sends a record without the flags
//          mov  rax, [rsi + 0xA8]   ; the load the jump replaced
//          jmp  site + 7      ; the game's code goes on as it was, its own flag test included
//
// A record with a flag set runs exactly the game's instructions; one without runs none of them, and
// arrives where it always did.
inline constexpr size_t FilterLoadBytes = 7;    // the instruction the jump replaces
inline constexpr size_t FilterSiteBytes = 8;    // what is written: one aligned 8 bytes, the eighth byte unchanged
inline constexpr size_t FilterCaveBytes = 25;
inline constexpr uint32_t FilterFlags = 0x21800000;

struct FilterPlan {
    bool ok = false;                      // false: something is out of a relative jump's reach - nothing may be written
    uint8_t cave[FilterCaveBytes] = {};
    uint8_t site[FilterSiteBytes] = {};
};

// site: the address of the load. skip: where a record without the flags goes. cave: where the cave's bytes
// will be. eighth: the byte that follows the load in the game (kept).
inline FilterPlan PlanFilter(uint64_t site, uint64_t skip, uint64_t cave, uint8_t eighth) noexcept {
    FilterPlan plan;
    int32_t toCave = 0, toSkip = 0, toResume = 0;
    if (!site || !skip || !cave) return plan;
    if (!Rel32(site + 5, cave, toCave)) return plan;                        // jmp cave
    if (!Rel32(cave + 13, skip, toSkip)) return plan;                       // jz skip, ends 13 bytes into the cave
    if (!Rel32(cave + FilterCaveBytes, site + FilterLoadBytes, toResume)) return plan;   // jmp back
    uint8_t* c = plan.cave;
    const uint8_t test[] = {0xF7, 0x46, 0x08};                              // test dword ptr [rsi + 8], imm32
    std::memcpy(c, test, 3); std::memcpy(c + 3, &FilterFlags, 4);
    c[7] = 0x0F; c[8] = 0x84; std::memcpy(c + 9, &toSkip, 4);               // jz rel32
    const uint8_t load[] = {0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00};      // mov rax, [rsi + 0xA8]
    std::memcpy(c + 13, load, 7);
    c[20] = 0xE9; std::memcpy(c + 21, &toResume, 4);                        // jmp rel32
    plan.site[0] = 0xE9; std::memcpy(plan.site + 1, &toCave, 4);
    plan.site[5] = 0x66; plan.site[6] = 0x90;                               // a two-byte nop
    plan.site[7] = eighth;
    plan.ok = true;
    return plan;
}

// The debug event level: above 1 the game names every draw, 1 keeps the passes' own events, 0 has none.
enum class Level { Leave, Lower, Refuse };
inline constexpr int LevelWithoutDrawNames = 1;
// What to do with the level found: nothing at 0 or 1, lower a small one, and leave alone a number that is
// not a level at all (the place holds something else).
inline Level LevelDecision(int32_t found) noexcept {
    if (found == 0 || found == 1) return Level::Leave;
    if (found >= 2 && found <= 16) return Level::Lower;
    return Level::Refuse;
}

// A relative call that is no longer made: its five bytes become one five-byte no-operation, so the code
// after it is reached as it was, with nothing called.
inline constexpr size_t CallBytes = 5;

// How many jumps in `code` (at address `at`) would land inside the instruction that is `own` bytes into it
// and `ownSize` long, past its first byte. As BranchesInto, but the instruction's own bytes are not read as
// jumps: a call's distance can look like one that lands in the call itself.
inline size_t BranchesIntoInstruction(const uint8_t* code, size_t size, uint64_t at, size_t own, size_t ownSize) noexcept {
    if (!code || !ownSize || own > size || ownSize > size - own) return 0;
    const uint64_t lo = at + own + 1, hi = at + own + ownSize - 1;
    if (ownSize < 2) return 0;
    return BranchesInto(code, own, at, lo, hi) + BranchesInto(code + own + ownSize, size - own - ownSize, at + own + ownSize, lo, hi);
}
struct CallNop {
    bool ok = false;                      // false: the first byte is not a relative call - nothing may be written
    uint8_t bytes[CallBytes] = {};
};
inline CallNop PlanCallNop(const uint8_t* live) noexcept {
    CallNop plan;
    if (!live || live[0] != 0xE8) return plan;
    const uint8_t nop[CallBytes] = {0x0F, 0x1F, 0x44, 0x00, 0x00};   // nop dword ptr [rax + rax]
    std::memcpy(plan.bytes, nop, sizeof nop);
    plan.ok = true;
    return plan;
}

// Once per pair (shadows_once, cull_once). The mod draws one game frame twice, an eye a time: what does not
// depend on the eye can be done in the first pass and kept for the second. Alone is every other draw (a
// menu, a loading screen, an eye by turns, a pass drawn inside another): the game runs as it is.
enum class Pass : uint8_t { Alone, First, Second };

// What the first pass of a pair left for the second.
struct ShadowPair {
    bool fitted = false;      // the first pass fitted the two views, by the camera's look-at point
    uint64_t passes = 0;      // the shadow passes object it fitted
};
// The shadow update of a pass: Fit = the game's own update (and, in a first pass, `pair` says what it left);
// Keep = a second pass that keeps the first's views, shadow map and culled casters.
enum class Shadow : uint8_t { Fit, Keep };
inline Shadow ShadowUpdate(bool on, Pass pass, uint64_t passes, bool lookAt, ShadowPair& pair) noexcept {
    if (!on || pass == Pass::Alone || !passes) { pair = {}; return Shadow::Fit; }
    if (pass == Pass::First) { pair.fitted = lookAt; pair.passes = passes; return Shadow::Fit; }
    // Second: only what the first pass of this same pair fitted, for the same object, with the fit still by
    // the look-at point (a fit by the visible set would be this eye's own).
    const bool keep = pair.fitted && pair.passes == passes && lookAt;
    pair = {};
    return keep ? Shadow::Keep : Shadow::Fit;
}

// One present a pair (present_once). Each eye pass ends in the game's Present of the desktop window's swap
// chain; the headset's picture is copied out before it, in ReShade's present handling. The first pass's
// Present is held back: it returns as if made, the swap chain's back buffer stays the current one, and the
// second pass draws into it and presents it. The game waits on the swap chain's frame latency handle before
// each pass's present target, once for each present: the wait that follows a held present is left out too.
struct PresentPair {
    bool hold = false;        // the next Present on the draw thread is the first pass's: held back
    bool waitOwed = false;    // a present was held back: the game's next wait for the swap chain is left out
};
// Before each draw of the game's frame. Only a pair's first pass has its present held; what a held present
// left owed stays owed until the game's next wait, whatever pass that is in and whether the patch is still on.
inline void PresentPass(bool on, Pass pass, PresentPair& pair) noexcept { pair.hold = on && pass == Pass::First; }
enum class Present : uint8_t { Make, Hold };
// The swap chain's Present was called. At most one is held for each first pass.
inline Present PresentCalled(PresentPair& pair) noexcept {
    if (!pair.hold) return Present::Make;
    pair.hold = false;
    pair.waitOwed = true;
    return Present::Hold;
}
// The game is about to wait for the swap chain. False: this wait is left out (once for each held present).
inline bool PresentWaits(PresentPair& pair) noexcept {
    if (!pair.waitOwed) return true;
    pair.waitOwed = false;
    return false;
}

// The swap chain's function table with one entry replaced. `copied` holds `before` entries from in front of
// the table (what a compiler keeps there) and then `slots` entries of the table itself; `out` gets the same
// with entry `slot` of the table = `shim`, and `original` what was there. False when the table is too short
// for the slot or the entry is empty: nothing may be installed then.
inline constexpr size_t PresentSlot = 8;          // IDXGISwapChain::Present
inline constexpr size_t SwapChain3Slots = 40;     // IDXGISwapChain3's table (ReShade holds the swap chain as that): each entry must be code
inline constexpr size_t SwapChainSlots = 41;      // IDXGISwapChain4's table: the least that is copied
inline constexpr size_t TableBefore = 2;
inline bool ShimTable(const uintptr_t* copied, size_t before, size_t slots, size_t slot, uintptr_t shim, uintptr_t* out, uintptr_t& original) noexcept {
    if (!copied || !out || !shim || slot >= slots || !copied[before + slot] || copied[before + slot] == shim) return false;
    for (size_t i = 0; i < before + slots; ++i) out[i] = copied[i];
    original = copied[before + slot];
    out[before + slot] = shim;
    return true;
}

// effect_listener_guard (vr/game_patch_signatures.h): the game's effect-ended listener is called for a handle
// only when the handle could be read and holds an effect; with none it would take a lock at address 0x20.
enum class Ended : uint8_t { Call, Skip };
inline Ended EndedCall(bool handleRead, uint64_t heldEffect) noexcept { return handleRead && heldEffect ? Ended::Call : Ended::Skip; }
// For the log only: a handle called for an effect it does not hold (it is called all the same, as the game does).
inline bool EndedOther(uint64_t heldEffect, uint64_t endingEffect) noexcept { return heldEffect && endingEffect && heldEffect != endingEffect; }
// lea r9,[rip+d32] (4C 8D 0D d32) at `at` of a fingerprint whose first byte is a function's: true when the
// instruction's address is that first byte - the function names itself there.
inline bool LeaSelf(const uint16_t* sig, size_t size, size_t at) noexcept {
    if (!sig || at + 7 > size || sig[at] != 0x4C || sig[at + 1] != 0x8D || sig[at + 2] != 0x0D) return false;
    uint8_t d[4];
    for (size_t i = 0; i < 4; ++i) {
        if (sig[at + 3 + i] > 0xFF) return false;
        d[i] = uint8_t(sig[at + 3 + i]);
    }
    int32_t disp = 0;
    std::memcpy(&disp, d, 4);
    return int64_t(at) + 7 + disp == 0;
}

}  // namespace gamepatch
