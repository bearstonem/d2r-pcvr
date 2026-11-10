#pragma once
#include "../steamvr/party_health.h"
#include <D2RLPlugin/context.h>
namespace partyhealthreports {
using LogFn=void(*)(const char*);
bool Init(const D2RL::PluginContext*,LogFn=nullptr);
void Stop();
void Tick(bool inWorld,uint32_t unit,uint64_t session);
void PublishLocal(const framevr::partyhealth::Report&);
framevr::partyhealth::Report Local(uint64_t session,uint64_t now);
size_t Remote(uint64_t session,uint64_t now,framevr::partyhealth::Report*out,size_t capacity);
}
