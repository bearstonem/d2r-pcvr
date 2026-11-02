#pragma once
#include "../steamvr/interactions.h"
namespace D2RL {struct PluginContext;}
namespace nativeui {
void Init(const D2RL::PluginContext*);
void Capture(framevr::interaction::Snapshot&); // UI thread only
bool Assign(const framevr::interaction::Request&,const framevr::interaction::Snapshot&); // UI thread; native setter + readback
// Glyph casting: the skill onto the record the right trigger answers to (Snapshot::glyphRecord). UI thread.
bool Park(int skill,int owner,const framevr::interaction::Snapshot&,uint64_t session);
}
