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

}  // namespace gamepatch
