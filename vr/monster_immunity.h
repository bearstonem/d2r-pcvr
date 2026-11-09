#pragma once
#include "../steamvr/monster_immunity.h"
namespace D2RL {struct PluginContext;}
namespace monsterimmunity {
void Init(const D2RL::PluginContext* context);
// Update thread: posts read-only work to the game's thread. False clears all
// published results and cancels queued work (menus, desktop and leaving play).
void Tick(uint64_t now,bool wanted);
framevr::immunity::Snapshot Latest(uint64_t now);
}
