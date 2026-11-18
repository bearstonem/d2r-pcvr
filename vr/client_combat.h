#pragma once
#include <cstdint>
namespace D2RL {struct PluginContext;}
namespace clientcombat {
void Init(const D2RL::PluginContext* context);
void Tick(uint64_t session,uint64_t now);
bool NativeOnly(uint64_t session);
}
