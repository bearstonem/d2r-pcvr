#pragma once
#include "loot_beam_scan.h"
#include <D2RLPlugin/context.h>
namespace lootreports {
using SampleFn=bool(*)(uint32_t requestedPlayer,const framevr::lootbeams::Wanted&,lootscan::Shown&);
bool Init(const D2RL::PluginContext*,SampleFn);
void Tick(uint64_t session,uint32_t localUnit,bool requestLoot,const framevr::lootbeams::Wanted&);
lootscan::Shown Get(uint64_t session,uint32_t localUnit,uint64_t now);
void Stop();
}
