#pragma once
#include <string>
#include "../steamvr/interactions.h"
namespace D2RL {struct PluginContext;}
namespace nativeui {
void Init(const D2RL::PluginContext*);
void Capture(framevr::interaction::Snapshot&); // UI thread only
bool Assign(const framevr::interaction::Request&,const framevr::interaction::Snapshot&); // UI thread; native setter + readback
std::string SkillName(int id);
const void* GetSkillRow(int id);
}
