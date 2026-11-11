#pragma once
#include <cstdint>
// [game_patches]: the places in the game's render code that vr/game_patches.cpp changes, and the bytes that
// must be there (steamvr/PERFORMANCE.md "Render task patches"). D2R 3.3.93787 native ABI fingerprints (the
// functions start at the same addresses in 3.4.95041), whole instructions with their jump distances: at these
// addresses nothing in them moves. A game whose bytes differ anywhere in a patch's fingerprints does not get
// that patch: the game's version is never asked. Each entry is a byte, or gamepatch::Any
// (steamvr/game_patch_plan.h) for one that is not compared; none is left out today.
namespace gamepatches::abi {

// --- transparent_filter -------------------------------------------------------------------------------
// The "Ghost" mode of the transparent render pass walks every 0x100-byte draw record of a bucket, once
// per transparency layer. Three stretches prove the loop:
//
// Head: the loop's set-up, up to its first instruction. rsi = the first record, r13 = the end
// (first + count * 0x100), the record counter cleared, an empty list leaves, and the code falls into the
// loop: rsi is the record there on the first turn.
inline constexpr uint64_t FilterHeadRva = 0xfdc70f;
inline constexpr uint16_t FilterHeadSig[] = {
    0x49,0x8b,0x75,0x00,0x33,0xff,0x4d,0x8b,0x6d,0x08,0x44,0x8b,0xe7,0x49,0xc1,0xe5,0x08,0x4c,0x03,0xee,0x49,0x3b,0xf5,0x0f,
    0x84,0xa3,0x03,0x00,0x00,0x48,0x8b,0x5c,0x24,0x50,0x44,0x8b,0x74,0x24,0x40,0x66,0x66,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00};
// Loop: its first twelve instructions. The 7-byte load of the pointer at record + 0xA8 (what the patch's jump
// replaces), the number read through it at + 0x14C and made into a yes/no in r15b, and only then the
// record's flags at + 8 tested against 0x21800000, with the jump to Skip when none is set. Between the load
// and that jump nothing is written but rax, xmm0, r15b and the flags register.
inline constexpr uint64_t FilterLoopRva = 0xfdc740;
inline constexpr uint16_t FilterLoopSig[] = {
    0x48,0x8b,0x86,0xa8,0x00,0x00,0x00,0x48,0x85,0xc0,0x74,0x14,0xf3,0x0f,0x10,0x80,0x4c,0x01,0x00,0x00,0x0f,0x2e,0xc7,0x7a,0x02,
    0x74,0x05,0x41,0xb7,0x01,0xeb,0x03,0x45,0x32,0xff,0x8b,0x46,0x08,0x48,0xa9,0x00,0x00,0x80,0x21,0x0f,0x84,0x3b,0x03,0x00,0x00};
// Skip: where a record without the flags goes, to the loop's end. r15 is loaded anew from the stack, the
// counter and the record step on, and the loop's only way back is to its first instruction: rsi is the
// record there on every later turn, and nothing jumps into the middle of the load. rax, xmm0, r15b and the
// flags register are not read here before they are written again.
inline constexpr uint64_t FilterSkipRva = 0xfdcaad;
inline constexpr uint16_t FilterSkipSig[] = {
    0x4c,0x8b,0x7c,0x24,0x48,0x41,0xff,0xc4,0x48,0x81,0xc6,0x00,0x01,0x00,0x00,0x49,0x3b,0xf5,0x0f,0x85,0x7b,0xfc,0xff,0xff};

// The function the loop is in, start and end: before the jump goes in, its bytes in the running game are
// searched for any jump that would land inside the 7-byte load (gamepatch::BranchesInto); one found = no patch.
inline constexpr uint64_t FilterFunctionRva = 0xfdc510, FilterFunctionEnd = 0xfdcdb3;

// --- profiler_accounting ---------------------------------------------------------------------------------
// The command list's draw accounting ends with: is the list counted by the GPU profiler (a byte of the list,
// always 1)? no -> the function's end; yes -> get the profiler, hand it the list's scope index and the
// draw's primitives. That call takes the profiler's one lock, adds 1 and the primitives to two counters of
// the scope's record and unlocks: nothing is returned and nothing else is written. The stretch runs from
// the test to the function's last byte; the patch makes the 2-byte conditional jump at AccountJumpAt
// unconditional (its distance is kept), so neither call is made.
inline constexpr uint64_t AccountCallRva = 0xf11845;
inline constexpr uint16_t AccountCallSig[] = {
    0x80,0x7f,0x15,0x00,0x74,0x13,0xe8,0x30,0x1f,0x00,0x00,0x8b,0x57,0x18,0x44,0x8b,0xc3,0x48,0x8b,0xc8,0xe8,0x62,0x27,0x00,0x00,
    0x48,0x8b,0x5c,0x24,0x30,0x48,0x83,0xc4,0x20,0x5f,0xc3};
inline constexpr uint32_t AccountJumpAt = 4;              // in AccountCallSig: 74 13, je to the function's end
inline constexpr uint32_t AccountSecondCallAt = 20;       // in AccountCallSig: the call that is no longer made
inline constexpr uint8_t AccountJumpAlways = 0xeb;        // jmp with the same one-byte distance
// The function that is no longer called, up to its first branch: "not inside a profiled frame" (a byte of
// the profiler) leaves at once; it keeps the scope index and the primitives and takes the lock after this.
inline constexpr uint64_t AccountRva = 0xf13fc0;
inline constexpr uint16_t AccountSig[] = {
    0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x56,0x48,0x83,0xec,0x30,0x80,0xb9,0xbc,0x03,0x00,0x00,0x00,0x41,0x8b,0xe8,
    0x48,0x63,0xf2,0x48,0x8b,0xd9,0x74,0x6e};

}  // namespace gamepatches::abi
