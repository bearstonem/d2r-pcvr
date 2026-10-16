#pragma once
#include <cstdint>

namespace combat {
void Configure(bool enabled);
// Calling thread only: true restores native hit/block/dodge rolls until reset.
// Returns the previous value so the caller can restore it.
bool NativeRolls(bool on);
// Native to-hit rolls for every local-player non-missile hit check (swings,
// trigger/button melee, melee skills). Missiles keep contact_hits.
void ConfigureMeleeRolls(bool native);
// Latest trigger/button melee hit check (outside physical swings), consumed
// once: its session and GetTickCount64 time. False when there is none.
bool TakeButtonMelee(uint64_t& session,uint64_t& at);
void Install();
void Report();
// The UI-thread local-player accessor is authoritative across area changes;
// controller aim can also refresh identity. SDK handles are never pointers.
void ObserveLocalPlayer(uintptr_t unit);
bool ObserveUiPlayer(uintptr_t unit,uint64_t session);
bool LocalPlayerUnit(uintptr_t& unit);
bool LocalPlayerId(uint32_t& id);
}
