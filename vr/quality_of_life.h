#pragma once
#include <cstdint>
namespace D2RL {struct PluginContext;}
namespace qualityoflife {
bool Init(const D2RL::PluginContext*);
void Shutdown();
void Configure(float damage,float movement,float aggro,float leash,bool vr);
float DamageFactor();
float SummonAggroFactor();
bool NeedsDamage();
bool NeedsMovement();
void DamageReady(bool);
void VelocityReady(bool);
void LookupReady(bool);
int AdjustVelocity(uintptr_t path,int native);
void ObserveLookup(uintptr_t game,int type,uint32_t id,uintptr_t unit);
}
