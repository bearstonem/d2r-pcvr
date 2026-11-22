#pragma once
#include <cstdint>
namespace D2RL { struct PluginContext; }
namespace upperbodyskin {
void Init(const D2RL::PluginContext* ctx);
void Publish(uintptr_t currentPose, uintptr_t previousPose, const float* matrices, int count, uintptr_t skeleton=0, uintptr_t owner=0);
void Clear();
void Shutdown();
}
