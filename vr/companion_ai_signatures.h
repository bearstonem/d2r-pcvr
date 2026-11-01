#pragma once
#include <cstdint>

namespace companionai::abi {
// RelocateUnit RVA in D2R 3.3.93787 / 3.3.93847: relocates unit, updates path, room membership and view refresh transactionally.
inline constexpr uint64_t RelocateRva = 0x491ca0;
inline constexpr uint8_t RelocateSig[] = {
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c
};

// RoomPoint RVA in D2R 3.3.93787 / 3.3.93847: finds the exact D2RoomStrc pointer for given sub-tile coordinates.
inline constexpr uint64_t RoomPointRva = 0x38dfb0;
inline constexpr uint8_t RoomPointSig[] = {
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x20, 0x41, 0x8b, 0xf0, 0x8b
};

// ClientMode RVA in D2R 3.3.93787 / 3.3.93847: returns 0 for offline play.
inline constexpr uint64_t ModeRva = 0x8af90;
inline constexpr uint8_t ModeSig[] = {
    0x8b, 0x05, 0x5e, 0x84, 0x99, 0x02, 0xc3
};

// PathMove RVA in D2R 3.3.93787 / 3.3.93847 (0x381890): internal D2DynamicPathStrc coordinate/room setter
// called by RelocateUnit (0x491ca0 at 0x491e42) and client missile cleanup (spells.cpp).
// NOTE: Retained for signature verification tests only. Do NOT call 0x381890 to initiate walking movement,
// as it snaps/teleports path coordinates instead of building an A* server path.
inline constexpr uint64_t PathMoveRva = 0x381890;
inline constexpr uint8_t PathMoveSig[] = {
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x0f, 0xb7, 0x44, 0x24, 0x50, 0x49, 0x8b, 0xd0, 0x66, 0x89, 0x44
};

// ServerMoveToCoord RVA in D2R 3.3.93787 / 3.3.93847 (0x4a77e0):
// Native 4-argument server coordinate walk/run wrapper:
// int __fastcall ServerMoveToCoord(uintptr_t game, uintptr_t unit, int x, int y)
// Calls ServerMoveUnit (0x4a8a10) with targetUnit=0, mode=15 (converted to MONMODE_WALK=2 for hirelings), pathType=1, flags=0.
inline constexpr uint64_t ServerMoveToCoordRva = 0x4a77e0;
inline constexpr uint8_t ServerMoveToCoordSig[] = {
    0x48, 0x83, 0xec, 0x48, 0x33, 0xc0, 0x66, 0x89, 0x44, 0x24, 0x38, 0xc6, 0x44, 0x24, 0x30, 0x01
};

// ServerMoveUnit RVA in D2R 3.3.93787 / 3.3.93847 (0x4a8a10):
// Core server AI movement dispatcher:
// int __fastcall ServerMoveUnit(uintptr_t game, uintptr_t unit, uintptr_t targetUnit, int moveMode, int x, int y, uint8_t pathType, uint16_t flags)
// First 16 bytes are 4 complete non-RIP-relative instructions (5 + 5 + 5 + 1 = 16 bytes).
inline constexpr uint64_t ServerMoveUnitRva = 0x4a8a10;
inline constexpr uint8_t ServerMoveUnitSig[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57
};

// ServerStartWalk RVA (0x445cc0) & ServerStartRun RVA (0x445db0) in D2R 3.3.93787 / 3.3.93847:
// Mode-transition gatekeepers called by D2GAME_ExecuteMoveUnit (0x4475c0 at 0x4477c9):
// int __fastcall ServerStartMoveMode(uintptr_t game, uintptr_t moveStruct)
// where [moveStruct + 0x08] = unit, [moveStruct + 0x10] = targetUnit, [moveStruct + 0x18] = targetX, [moveStruct + 0x1c] = targetY.
// Returning 0 causes 0x4475c0 to abort the path and keep the unit in MONMODE_NEUTRAL (1).
// First 17 bytes are 5 complete non-RIP-relative instructions (5 + 1 + 4 + 4 + 3 = 17 bytes).
inline constexpr uint64_t ServerStartWalkRva = 0x445cc0;
inline constexpr uint8_t ServerStartWalkSig[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x5a, 0x08, 0x48, 0x8b, 0xf9
};

inline constexpr uint64_t ServerStartRunRva = 0x445db0;
inline constexpr uint8_t ServerStartRunSig[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x5a, 0x08, 0x48, 0x8b, 0xf9
};

// Attack RVA in D2R 3.3.93787 / 3.3.93847 (0x42feb0):
// Native attack execution function:
// int __fastcall Attack(uintptr_t game, uintptr_t unit, int skillId, int arg4)
// Resolves combat target via Target (0x48fe20) and triggers native attack animation / missile spawn.
inline constexpr uint64_t AttackRva = 0x42feb0;
inline constexpr uint8_t AttackSig[] = {
    0x40, 0x55, 0x56, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8d, 0xac, 0x24, 0xe8, 0xfe, 0xff, 0xff, 0x48
};
} // namespace companionai::abi
