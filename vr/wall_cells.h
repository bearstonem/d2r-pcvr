#pragma once
#include "../steamvr/dropped_bottles.h"
#include "../steamvr/grip_aim.h"
namespace D2RL {struct PluginContext;}
// The game's wall cells around a point, for dropped bottles and held items. Native
// room and collision calls run only on the game thread; other threads see copies.
namespace wallcells {
void Init(const D2RL::PluginContext*);
// Any thread: the cells wanted around this point. A sample is queued when the
// point moved several cells from the last one, or that one has aged.
void Want(const framevr::bottles::WallRequest&,uint64_t now);
bool Latest(framevr::bottles::Patch&); // the newest sample, if any
// The same for the held items (steamvr/held_world.h), around the hero: a stream of
// its own, so a rolling bottle and the hero never recentre each other's patch.
void WantHeld(const framevr::bottles::WallRequest&,uint64_t now);
bool LatestHeld(framevr::bottles::Patch&);
// Exact-cell solid-ground check, sampled on the game thread. Clear is not a
// guarantee of skill success (mana, transient units and native skill rules remain).
framevr::gripaim::Landing TeleportLanding(uint64_t session,int x,int y,uint64_t now);
void Report();                          // ten-second log line when used
}
