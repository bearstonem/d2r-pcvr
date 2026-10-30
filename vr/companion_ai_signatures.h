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

// SkillLevel RVA in D2R 3.3.93787 / 3.3.93847 (0x33d1e0):
// int __fastcall SkillLevel(uintptr_t unit, uintptr_t skillNode, int bIncludeBonuses, int unk)
inline constexpr uint64_t SkillLevelRva = 0x33d1e0;
inline constexpr uint8_t SkillLevelSig[] = {
    0xff, 0x25, 0x8a, 0xd1, 0xae, 0x03, 0x48, 0x8b, 0xf9, 0x48, 0x85, 0xc9, 0x75, 0x1b, 0x88, 0x4c
};

// Inventory RVA in D2R 3.3.93787 / 3.3.93847 (0x34a360):
// uintptr_t __fastcall Inventory(uintptr_t unit, const char* file, int line)
inline constexpr uint64_t InventoryRva = 0x34a360;
inline constexpr uint8_t InventorySig[] = {
    0x48, 0x89, 0x5c, 0x24, 0x18, 0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf1, 0x48, 0x85, 0xc9, 0x75, 0x13
};

// EquippedItem RVA in D2R 3.3.93787 / 3.3.93847 (0x3886d0):
// uintptr_t __fastcall Equipped(uintptr_t inventory, int bodyLocation)
inline constexpr uint64_t EquippedRva = 0x3886d0;
inline constexpr uint8_t EquippedSig[] = {
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x63, 0xfa, 0x48, 0x8b, 0xd9, 0x48, 0x85
};

// GetStat RVA in D2R 3.3.93787 / 3.3.93847 (0x2f5020):
// int __fastcall GetStat(uintptr_t unit, int stat, int param)
inline constexpr uint64_t GetStatRva = 0x2f5020;
inline constexpr uint8_t GetStatSig[] = {
    0xff, 0x25, 0xf2, 0x51, 0xb3, 0x03, 0x90, 0x90, 0x90, 0x90
};

// StartSkill RVA in D2R 3.3.93787 / 3.3.93847 (0x43b3e0):
// int __fastcall StartSkill(uintptr_t game, uintptr_t unit, int skillId, int level, int arg5)
inline constexpr uint64_t StartRva = 0x43b3e0;
inline constexpr uint8_t StartSig[] = {
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57
};

// DispatchSkill RVA in D2R 3.3.93787 / 3.3.93847 (0x43acb0):
// int __fastcall DispatchSkill(uintptr_t game, uintptr_t unit, int skillId, int level, int flag, int targetX, int targetY)
inline constexpr uint64_t DispatchRva = 0x43acb0;
inline constexpr uint8_t DispatchSig[] = {
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x44, 0x89, 0x4c, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18
};

// SetStat RVA in D2R 3.3.93787 / 3.3.93847 (0x2f7d10):
// void __fastcall SetStat(uintptr_t unit, int stat, int value, int param)
inline constexpr uint64_t SetStatRva = 0x2f7d10;
inline constexpr uint8_t SetStatSig[] = {
    0xff, 0x25, 0x32, 0x25, 0xb3, 0x03, 0x90, 0x90, 0x90, 0x90
};

// Skill (SKILLS_GetSkillNodeById) RVA in D2R 3.3.93787 / 3.3.93847 (0x33dcd0):
// uintptr_t __fastcall Skill(uintptr_t unit, int skillId, int owner)
inline constexpr uint64_t SkillRva = 0x33dcd0;
inline constexpr uint8_t SkillSig[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x41, 0x8b, 0xd8, 0x8b, 0xfa, 0xe8, 0xfc, 0xd9, 0x00
};
} // namespace companionai::abi
