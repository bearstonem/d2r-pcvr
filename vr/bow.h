#pragma once
#include "../steamvr/bow_flight.h"
namespace D2RL {struct PluginContext;}
namespace bowvr {
// Game-thread launch scope only; never changes projectiles already in flight.
void AssistSpell(uintptr_t game,uintptr_t player);
void Init(const D2RL::PluginContext* context);
void Configure(bool enabled);
void Install();
void Update(const framevr::bowflight::Gate& gate,uintptr_t clientPlayer);
void Submit(const framevr::bowflight::Shot& shot);
bool PreviewReady(); // lock-free; native mesh replaced the tracked draw guide
// Render-side values only; never creates or destroys a native client unit.
void PublishPreviewPose(const framevr::bowflight::Shot& pose,const framevr::bowflight::Gate& gate,bool visible,uint64_t frame);
bool SpellReady();
bool SpellTarget(uintptr_t game,uintptr_t player,int* x,int* y);
// 0: unrelated request; 1: copied/adjusted; -1: reject before native creation.
int SpellParams(uintptr_t game,uintptr_t params,std::array<uint8_t,0x98>& adjusted);
void Created(uintptr_t game,uintptr_t params,uintptr_t unit);
bool Visual(uint32_t entity,uintptr_t unit,bool hasReleasePoint,bool isRoot);
bool OwnsVisual(uintptr_t unit);
void Report();
}
