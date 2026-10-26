#pragma once
#include "../steamvr/interactions.h"
namespace D2RL {struct PluginContext;}
namespace interactionstate {
void Init(const D2RL::PluginContext*);
void Capture(); // SDK UI thread, after equipment refresh
framevr::interaction::Snapshot Read();
int CurrentLevel(uint64_t session,uint64_t now); // Fresh location only; avoids copying inventory/skills in target queries.
// The druid shapeshift form the player is in (framevr::shapeshift: 1 Werewolf, 2 Werebear;
// states 139/140, sampled with the hireling every 200 ms); 0 none, not known or stale.
int ShapeForm(uint64_t session,uint64_t now);
framevr::interaction::Reply Status();
void Request(const framevr::interaction::Request&);
void Cancel();
}
