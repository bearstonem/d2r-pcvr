#pragma once
#include "../steamvr/melee_dispatch_wire.h"
#include <cstdint>

namespace D2RL { struct PluginContext; }
namespace meleedispatch {
using ApplyFn=bool(*)(uint64_t peer,const framevr::melee::dispatch::Contact&);
bool Init(const D2RL::PluginContext*,ApplyFn);
void Tick(uint64_t session);
bool Send(const framevr::melee::dispatch::Contact&);
void Stop();
bool Alive(uint64_t peer,uint64_t session);
bool Ready(uint64_t session);
}
