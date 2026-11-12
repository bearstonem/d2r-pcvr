#pragma once
#include "target_lock.h"
#include <D2RLPlugin/context.h>
namespace targetreports {
using SampleFn=bool(*)(uint32_t requestedPlayer,targetlock::Snapshot&);
bool Init(const D2RL::PluginContext*,SampleFn);
void Tick(uint64_t session,uint32_t unit,bool requestTargets);
targetlock::Snapshot Get(uint64_t session,uint32_t unit,uint64_t now);
void Stop();
}
