#pragma once
namespace D2RL {struct PluginContext;}
namespace summonrange {
// Summon range applies live: Init installs the adapter at startup (idle at 100%), and a later Configure posts the new multiplier,
// which the next pet AI hook applies (vr/summon_range_native.h, namespace live). No restart.
void Configure(float multiplier,bool vrEnabled);
bool Init(const D2RL::PluginContext* context);
void Shutdown();
float AggroFactor();
}
