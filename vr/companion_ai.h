#pragma once
#include <D2RLPlugin/api.h>

namespace companionai {
void Init(const D2RL::PluginContext* ctx);
void Configure(bool enabled);
void Install();
void Tick(float forwardX = 0.0f, float forwardY = -1.0f);
void Report();
void Shutdown();
bool IsEnabled();
unsigned UnstuckCount();
unsigned TacticalMoveCount();
void ConfigureDistressSound(int soundId);
void ConfigurePeel(bool enabled);
void OnPotionReceived();
bool IsInTacticalRetreat();
unsigned PeelCount();
}

