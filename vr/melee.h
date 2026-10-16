#pragma once
#include "../steamvr/melee_contact.h"
namespace D2RL {struct PluginContext;}
namespace melee {
void Init(const D2RL::PluginContext* context);
void Install();
void Submit(const framevr::melee::Sample& sample);
void Cancel();
void CancelShield();
void CancelWeapon();
bool ShieldReady();
void Report();
// Attack speed: one landed weapon hit per native attack cycle, one creature
// per stroke. Off restores unlimited free-swing contacts. Cycle 0 = unknown,
// which never gates. SetAttackCycle comes from the UI thread's stat read.
void ConfigureAttackSpeed(bool enabled,bool traceHits); // trace: [debug] melee_trace
void SetAttackCycle(uint64_t session,uint32_t milliseconds);
// Physical swings and trigger/button melee use the native to-hit/block/level
// rolls. Off restores the contact_hits policy for them. Missiles unaffected.
void ConfigureAttackRating(bool native); // [gameplay] melee_rules=1 (Classic)
// Owning game thread only; shared with physical arrows. Re-resolves the unit
// from its session/id rather than passing a native pointer across threads.
bool LocalServer(uintptr_t& game,uintptr_t& player);
uintptr_t ServerUnit(uintptr_t game,int type,uint32_t id); // owning game thread only
}
