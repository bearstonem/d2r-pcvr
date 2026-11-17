#pragma once
#include "../steamvr/dropped_bottles.h"
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
void Report();                          // ten-second log line when used
}
