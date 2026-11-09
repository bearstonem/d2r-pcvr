#pragma once
namespace D2RL {struct PluginContext;}
namespace summonrange {
// Startup-only native range changes. Applying a new preference requires restart.
void Configure(float multiplier,bool vrEnabled);
bool Init(const D2RL::PluginContext* context);
void Shutdown();
float AggroFactor();
}
