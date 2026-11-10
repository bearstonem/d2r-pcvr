#pragma once
#include "../steamvr/interactions.h"
#include "../steamvr/party_roster.h"
namespace D2RL {struct PluginContext;}
namespace nativeui {
void Init(const D2RL::PluginContext*);
void Capture(framevr::interaction::Snapshot&); // UI thread only
// Thread-safe copy, unknown on session mismatch or after 1000 ms without capture.
framevr::partyroster::Snapshot PartyRoster(uint64_t session,uint64_t now);
bool Assign(const framevr::interaction::Request&,const framevr::interaction::Snapshot&); // UI thread; native setter + readback
// Glyph casting: the skill onto the record the right trigger answers to (Snapshot::glyphRecord). UI thread.
bool Park(int skill,int owner,const framevr::interaction::Snapshot&,uint64_t session);
}
