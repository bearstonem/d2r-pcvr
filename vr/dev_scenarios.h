#pragma once
#include "../steamvr/interactions.h"
namespace D2RL {struct PluginContext;}
namespace devscenarios {
void Init(const D2RL::PluginContext*);
void Capture(framevr::interaction::Snapshot&);
void Request(const framevr::interaction::Request&);
framevr::interaction::Reply Status();
void Cancel();
bool DebugDraws(); // isolated developer session only; starts Off
bool Isolated(); // the isolated pcvr-dev session (its diagnostics log more)
}
