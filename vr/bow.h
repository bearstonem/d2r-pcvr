#pragma once
#include "../steamvr/bow_flight.h"
namespace D2RL {struct PluginContext;}
namespace bowvr {
// Optional Holy Bolt adapter: native collision-filter layout and SDK available.
bool HolyReady();
bool PrepareHoly(uintptr_t game);
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
bool MotionReady();
bool DirectCrossbow(); // current game-thread launch already owns this bolt
uint32_t QuiverLaunchMissile(); // game-thread launch's quiver missile id, 0 otherwise
void SetCrossbowCycle(uint64_t session,uint32_t equipment,uint32_t milliseconds);
bool SpellReady();
// UI thread: one native sound (sounds.txt row) at the local client player,
// through the game's own mixer and volume. False when unavailable.
bool PlayHeroSound(int sound);
bool SpellTarget(uintptr_t game,uintptr_t player,int* x,int* y);
// 0: unrelated request; 1: copied/adjusted; -1: reject before native creation.
int SpellParams(uintptr_t game,uintptr_t params,std::array<uint8_t,0x98>& adjusted);
void Created(uintptr_t game,uintptr_t params,uintptr_t unit);
bool AdoptCrossbow(uintptr_t game,uintptr_t unit,const framevr::bowflight::Shot& shot);
void CrossbowClient(uintptr_t unit,const framevr::bowflight::Shot& shot);
bool Visual(uint32_t entity,uintptr_t unit,bool hasReleasePoint,bool isRoot);
bool OwnsVisual(uintptr_t unit);
// A gas or oil potion thrown as a physical bottle (#292): the transport
// expects the game's own potion when the bottle shatters, and the first local
// potion missile (Missiles.txt 44-49) whose visual appears within 3 s is
// claimed: every entity of its visual (root, meshes, effects) then flies
// hidden; its burst is the game's own. HidePotion: one such entity of that
// client missile unit; true if hidden (first: true when it claimed the unit).
void ExpectPotion(uint64_t now);
bool HidePotion(uint32_t entity,uintptr_t unit,bool& first);
// Preserve native client-effect creation/lifetime; capture only effects inside
// the physical quiver arrow's hit callback, including synchronous HD binding.
uintptr_t ImpactVisual(uintptr_t params,int flags,uintptr_t(*create)(uintptr_t,int));
void Report();
}
