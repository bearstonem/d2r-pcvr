#pragma once
#include "../steamvr/bow_flight.h"
namespace spellvr {
void Install();
bool Ready();
void Report();
// Consume feedback on the UI thread with a freshly resolved local player.
void Feedback(uint64_t session,uintptr_t player,uint32_t id,bool active);
// A melee-skill strike refused for mana (melee.cpp, game thread): the same
// native "I need mana" voice as a refused hand spell, coalesced the same way.
void WarnMana(uint64_t session,uint32_t playerId);
bool Prepare(framevr::bowflight::Shot& shot,uintptr_t game,uintptr_t player);
bool Cast(const framevr::bowflight::Shot& shot,uintptr_t game,uintptr_t player);
// Quiver arrow: confirm the player's own single-missile arrow skill and level.
bool PrepareArrow(framevr::bowflight::Shot& shot,uintptr_t game,uintptr_t player);
// Double Throw: confirm the skill's srvdofunc 74 and its level. The caller
// scopes the native weapon selector to the throwing hand's weapon.
bool PrepareThrow(framevr::bowflight::Shot& shot,uintptr_t game,uintptr_t player);
// The native Left Hand Throw: a basic throw of the other hand's weapon.
bool ThrowLeft(uintptr_t game,uintptr_t player);
bool ThrowReady();
void Fired(const framevr::bowflight::Shot& shot,uintptr_t game,uintptr_t player);
bool Position(uintptr_t game,uintptr_t missile,uintptr_t room,int x,int y);
bool FinishVisual(uintptr_t client,const framevr::bowflight::Flight& flight);
}
