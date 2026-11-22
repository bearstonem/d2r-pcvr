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
// Feedback only: a host-validated contact, correlated to an actual sent sweep.
struct Feedback {
    uint64_t session=0,sequence=0,stroke=0,epoch=0;
    uint32_t hero=0,target=0,equipment=0;int hand=0;bool shield=false;
};
void NotifyContact(const framevr::melee::dispatch::Contact&,uint32_t target);
bool Predicted(const framevr::melee::dispatch::Contact&,uint32_t target);
bool TakeFeedback(uint64_t session,Feedback&);
}
