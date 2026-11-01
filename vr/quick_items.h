#pragma once
#include "../steamvr/interactions.h"
namespace D2RL {struct PluginContext;}
namespace quickitems {
// UI-thread only. The snapshot contains values, never native pointers.
void Init(const D2RL::PluginContext*,const wchar_t* settingsPath=nullptr);
void Capture(framevr::interaction::Snapshot&);
bool Assign(const framevr::interaction::Request&,const framevr::interaction::Snapshot&);
bool Use(const framevr::interaction::Request&,const framevr::interaction::Snapshot&);
// Reset Skills & Stats (vr/respec.cpp): one item in the client's own
// inventory list, found by the identity the game gave it (runtime id, class,
// code: read on the game thread when it was made). UI thread.
struct ClientItem {uint32_t runtimeId=0,classId=0,code=0;};
enum class UseResult {Used,NotReady,NoPlayer,ListUnreadable,Missing,WrongCode,Unusable};
const char* UseResultName(UseResult);
// The native use, as a right-click on it, with the menu open; Used means
// dispatched once, not proof of consumption. Missing fills `seen` with the
// runtime ids of the client's items of the same code (up to `capacity`).
UseResult UseClientItem(uint64_t session,const ClientItem&,uint32_t* seen=nullptr,unsigned capacity=0,unsigned* count=nullptr);
// Whether the client's inventory list still holds it; false when the list can't be read.
bool HoldsClientItem(uint64_t session,const ClientItem&,bool& holds);
}
