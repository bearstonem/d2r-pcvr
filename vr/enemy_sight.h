#pragma once
#include <D2RLPlugin/api.h>

namespace enemysight {
void Init(const D2RL::PluginContext* ctx);
// 100 uses the game's own MonStats detection distances; 20..200 scales hostile
// monsters' AI activation and awareness distances in offline VR only.
void Configure(int percent, bool vr);
void Install();
void Tick();
void Report();
void Shutdown();
int AppliedPercent();
}
