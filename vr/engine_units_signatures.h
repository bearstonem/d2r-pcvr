#pragma once
#include <cstdint>
// The game's functions the game-drawn ceiling calls or hooks (vr/engine_units.cpp), as D2R 3.3.93787 has
// them under D2RLoader 1.3.1 - their RVAs and first bytes, adapted from natpoh/diablo2-vr-port (MIT, see
// THIRD_PARTY.md). Each prologue checked at its RVA in a 3.3.93787 .text snapshot (2026-11-04); not in
// game_sigs.h (no pattern: a build that moves them turns the ceiling off and keeps the shader's).
// steamvr/audit_live_game.py reads this file too: a read-only check against a running game.
namespace engineunits {
// blz::string(const char*), called: the unit's path for its load request
inline constexpr uint64_t StringCtorRva=0xbf810;
inline constexpr uint8_t StringCtorSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x33};
// EntityContainerDefinition::GetOrLoadDefinition<UnitDefinition>(out, request, mode, unitType), called
inline constexpr uint64_t LoadUnitDefRva=0x669e70;
inline constexpr uint8_t LoadUnitDefSig[]={0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
// UnitDefinition::PlaceInstance(def, out, registry, part, filter, pos, rot, v3, bool), called
inline constexpr uint64_t PlaceInstanceRva=0x9661c0;
inline constexpr uint8_t PlaceInstanceSig[]={0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48};
// the renderer registry's set-mutable setter (registry, allowed), hooked: our units are laid while it is open
inline constexpr uint64_t SetMutableRva=0x912c90;
inline constexpr uint8_t SetMutableSig[]={0x40,0x53,0x55,0x41,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xe9};
// the world's teardown (leaving a game), hooked: our units let go at its entry, as the game lets go of its own
inline constexpr uint64_t TeardownRva=0xd72610;
inline constexpr uint8_t TeardownSig[]={0x40,0x53,0x48,0x81,0xec,0x80,0x00,0x00,0x00};
// the renderer's world-matrix function (out, entity, TransformComponent*), called; the bow hooks it
// (vr/bow_signatures.h TransformSig, the same bytes)
inline constexpr uint64_t UpdateWorldRva=0xe3a8c0;
inline constexpr uint8_t UpdateWorldSig[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
// the renderer's entt registry (data; game_sigs.h EcsRegistry)
inline constexpr uint64_t RegistryRva=0x2677170;
// entt type_seq<ModelComponent>::value() (game_sigs.h TypeSeqModel)
inline constexpr uint64_t TypeSeqModelRva=0x1dbe40;
}
