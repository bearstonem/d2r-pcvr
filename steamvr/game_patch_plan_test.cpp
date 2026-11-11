// [game_patches]: fingerprints with bytes left out, and the bytes the transparent filter patch writes.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "game_patch_plan.h"
#include "../vr/game_patch_signatures.h"

using namespace gamepatch;

static int32_t At(const uint8_t* p) { int32_t v; std::memcpy(&v, p, 4); return v; }

static void Signatures() {
    const uint8_t live[] = {0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00, 0x48};
    const uint16_t same[] = {0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00, 0x48};
    const uint16_t holes[] = {0x48, Any, 0x86, Any, Any, 0x00, 0x00, 0x48};
    const uint16_t other[] = {0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00, 0x49};   // the last byte differs
    const uint16_t first[] = {0x49, Any, Any, Any, Any, Any, Any, Any};          // only the first is compared, and differs
    const uint16_t bad[] = {0x48, 0x8B, 0x200, 0xA8, 0x00, 0x00, 0x00, 0x48};    // not a byte and not Any
    assert(SigMatches(live, same, 8));
    assert(SigMatches(live, holes, 8));
    assert(!SigMatches(live, other, 8));
    assert(SigMatches(live, other, 7));      // the differing byte is past what was asked
    assert(!SigMatches(live, first, 8));
    assert(!SigMatches(live, bad, 8));
    assert(!SigMatches(live, same, 0));      // nothing compared proves nothing
    assert(!SigMatches(nullptr, same, 8));
    assert(!SigMatches(live, nullptr, 8));

    // The game's fingerprints against themselves, and with any one byte changed: each byte is compared.
    using namespace gamepatches::abi;
    const struct { const uint16_t* sig; size_t size; } all[] = {
        {FilterHeadSig, sizeof FilterHeadSig / 2}, {FilterLoopSig, sizeof FilterLoopSig / 2}, {FilterSkipSig, sizeof FilterSkipSig / 2},
        {AccountCallSig, sizeof AccountCallSig / 2}, {AccountSig, sizeof AccountSig / 2},
    };
    for (const auto& s : all) {
        uint8_t bytes[128];
        assert(s.size <= sizeof bytes);
        for (size_t i = 0; i < s.size; ++i) { assert(s.sig[i] <= 0xFF); bytes[i] = uint8_t(s.sig[i]); }
        assert(SigMatches(bytes, s.sig, s.size));
        for (size_t i = 0; i < s.size; ++i) {
            bytes[i] ^= 0x01;
            assert(!SigMatches(bytes, s.sig, s.size));
            bytes[i] ^= 0x01;
        }
    }
    // The loop's fingerprint begins with the load the jump replaces, then the byte that is kept.
    const uint8_t load[] = {0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00};
    for (size_t i = 0; i < sizeof load; ++i) assert(FilterLoopSig[i] == load[i]);
    assert(FilterLoopRva - FilterHeadRva == sizeof FilterHeadSig / 2);   // the head runs up to the loop: one stretch
    assert(FilterLoopRva % 8 == 0);                                      // the 8 bytes written are one aligned word
}

static void Displacements() {
    int32_t d = 0;
    assert(Rel32(0x1000, 0x1000, d) && d == 0);
    assert(Rel32(0x1000, 0x0FFB, d) && d == -5);
    assert(Rel32(0x100000000ull, 0x100000000ull + 0x7FFFFFFF, d) && d == INT32_MAX);
    assert(!Rel32(0x100000000ull, 0x100000000ull + 0x80000000ull, d));
    assert(Rel32(0x100000000ull, 0x100000000ull - 0x80000000ull, d) && d == INT32_MIN);
    assert(!Rel32(0x100000000ull, 0x100000000ull - 0x80000001ull, d));
}

static void Branches() {
    // 0x1000: jne back to 0x1000 (near) ; jmp +2 (short) ; call 0x2000 ; nops
    uint8_t code[32] = {};
    std::memset(code, 0x90, sizeof code);
    code[0] = 0x0F; code[1] = 0x85; const int32_t back = -6; std::memcpy(code + 2, &back, 4);   // -> 0x1000
    code[6] = 0xEB; code[7] = 0x02;                                                             // -> 0x100A
    code[8] = 0xE8; const int32_t call = 0x2000 - (0x1008 + 5); std::memcpy(code + 9, &call, 4);   // -> 0x2000
    code[13] = 0x74; code[14] = 0xF8;                                                           // je -> 0x1007
    assert(BranchesInto(code, sizeof code, 0x1000, 0x1000, 0x1000) == 1);
    assert(BranchesInto(code, sizeof code, 0x1000, 0x100A, 0x100A) == 1);
    assert(BranchesInto(code, sizeof code, 0x1000, 0x2000, 0x2000) == 1);
    assert(BranchesInto(code, sizeof code, 0x1000, 0x1001, 0x1006) == 0);   // nothing lands inside the first instruction
    assert(BranchesInto(code, sizeof code, 0x1000, 0x1001, 0x1007) == 1);   // the je does, into the short jump's second byte
    assert(BranchesInto(code, sizeof code, 0x1000, 0x1000, 0x2000) == 4);
    assert(BranchesInto(code, 6, 0x1000, 0x100A, 0x100A) == 0);             // only what is given is read
    assert(BranchesInto(code, 7, 0x1000, 0x100A, 0x100A) == 0);             // a short jump cut off after its first byte
    assert(BranchesInto(nullptr, 8, 0x1000, 0, ~0ull) == 0);
    // The game's loop: from the skip stretch, the way back lands on the load's first byte and nowhere inside it.
    using namespace gamepatches::abi;
    uint8_t skip[sizeof FilterSkipSig / 2];
    for (size_t i = 0; i < sizeof skip; ++i) skip[i] = uint8_t(FilterSkipSig[i]);
    assert(BranchesInto(skip, sizeof skip, FilterSkipRva, FilterLoopRva, FilterLoopRva) == 1);
    assert(BranchesInto(skip, sizeof skip, FilterSkipRva, FilterLoopRva + 1, FilterLoopRva + 6) == 0);
    // ... and the loop's own flag test leaves for the skip stretch's first byte.
    uint8_t loop[sizeof FilterLoopSig / 2];
    for (size_t i = 0; i < sizeof loop; ++i) loop[i] = uint8_t(FilterLoopSig[i]);
    assert(BranchesInto(loop, sizeof loop, FilterLoopRva, FilterSkipRva, FilterSkipRva) == 1);
    assert(BranchesInto(loop, sizeof loop, FilterLoopRva, FilterLoopRva + 1, FilterLoopRva + 6) == 0);
    assert(FilterFunctionRva < FilterHeadRva && FilterSkipRva + sizeof skip < FilterFunctionEnd);
}

// The profiler accounting call: what the fingerprint itself proves about the jump that is changed.
static void Accounting() {
    using namespace gamepatches::abi;
    uint8_t call[sizeof AccountCallSig / 2];
    for (size_t i = 0; i < sizeof call; ++i) call[i] = uint8_t(AccountCallSig[i]);
    assert(call[AccountJumpAt] == 0x74);                                   // je, one byte of distance
    const uint64_t target = AccountCallRva + AccountJumpAt + 2 + call[AccountJumpAt + 1];
    assert(target == AccountCallRva + 25);                                 // past both calls ...
    const uint8_t end[] = {0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3};   // ... at the function's end: restore, return
    assert(sizeof call == 25 + sizeof end && !std::memcmp(call + 25, end, sizeof end));
    assert(call[AccountSecondCallAt] == 0xE8);
    assert(AccountCallRva + AccountSecondCallAt + 5 + At(call + AccountSecondCallAt + 1) == AccountRva);   // the call skipped is the function proven
    assert(AccountJumpAlways == 0xEB);                                     // jmp rel8: same length, same distance
}

static void Filter() {
    // The game at 0x7FF6'0000'0000, the cave 0x10000 below it.
    const uint64_t base = 0x7FF600000000ull, site = base + 0xFDC740, skip = base + 0xFDCAAD, cave = base - 0x10000;
    const FilterPlan p = PlanFilter(site, skip, cave, 0x48);
    assert(p.ok);
    // cave: test dword ptr [rsi+8], 0x21800000 / jz skip / mov rax,[rsi+0xA8] / jmp site+7
    const uint8_t test[] = {0xF7, 0x46, 0x08, 0x00, 0x00, 0x80, 0x21};
    assert(!std::memcmp(p.cave, test, 7));
    assert(p.cave[7] == 0x0F && p.cave[8] == 0x84);
    assert(cave + 13 + At(p.cave + 9) == skip);
    assert(At(p.cave + 9) == int32_t(0xFDCAAD + 0x10000 - 13));
    const uint8_t load[] = {0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00};
    assert(!std::memcmp(p.cave + 13, load, 7));
    assert(p.cave[20] == 0xE9);
    assert(cave + 25 + At(p.cave + 21) == site + 7);
    // site: jmp cave, a two-byte nop, the eighth byte as it was
    assert(p.site[0] == 0xE9);
    assert(site + 5 + At(p.site + 1) == cave);
    assert(At(p.site + 1) == -int32_t(0xFDC740 + 0x10000 + 5));
    assert(p.site[5] == 0x66 && p.site[6] == 0x90 && p.site[7] == 0x48);
    // The exact bytes for these addresses.
    const uint8_t wantCave[25] = {0xF7, 0x46, 0x08, 0x00, 0x00, 0x80, 0x21, 0x0F, 0x84, 0xA0, 0xCA, 0xFE, 0x00,
                                  0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00, 0xE9, 0x2E, 0xC7, 0xFE, 0x00};
    const uint8_t wantSite[8] = {0xE9, 0xBB, 0x38, 0x01, 0xFF, 0x66, 0x90, 0x48};
    assert(!std::memcmp(p.cave, wantCave, sizeof wantCave));
    assert(!std::memcmp(p.site, wantSite, sizeof wantSite));

    // A cave above the game works the same.
    const FilterPlan up = PlanFilter(site, skip, base + 0x40000000, 0x48);
    assert(up.ok && site + 5 + At(up.site + 1) == base + 0x40000000 && base + 0x40000000 + 13 + At(up.cave + 9) == skip);

    // Out of a relative jump's reach, either way: no plan, nothing to write.
    const FilterPlan farUp = PlanFilter(site, skip, site + 0x80000010ull, 0x48);
    const FilterPlan farDown = PlanFilter(site, skip, site - 0x80000010ull, 0x48);
    assert(!farUp.ok && !farDown.ok);
    for (uint8_t b : farUp.site) assert(b == 0);
    for (uint8_t b : farUp.cave) assert(b == 0);
    // The very edge: the jump to the cave reaches, the way back does not (the cave's end is 20 bytes farther).
    const FilterPlan edge = PlanFilter(site, skip, site + 5 + 0x7FFFFFFFull, 0x48);
    assert(!edge.ok);
    // No address, no plan.
    assert(!PlanFilter(0, skip, cave, 0x48).ok && !PlanFilter(site, 0, cave, 0x48).ok && !PlanFilter(site, skip, 0, 0x48).ok);
}

static void Levels() {
    assert(LevelDecision(0) == Level::Leave && LevelDecision(1) == Level::Leave);
    assert(LevelDecision(2) == Level::Lower && LevelDecision(3) == Level::Lower && LevelDecision(16) == Level::Lower);
    assert(LevelDecision(17) == Level::Refuse && LevelDecision(-1) == Level::Refuse && LevelDecision(0x3F800000) == Level::Refuse);
    assert(LevelWithoutDrawNames == 1);
}

int main() {
    Signatures();
    Displacements();
    Branches();
    Accounting();
    Filter();
    Levels();
    std::puts("game patch plan: fingerprints, displacements, jumps into a place, the filter's cave and jump, out-of-reach refusal and levels passed");
    return 0;
}
