#pragma once
#include "upper_body_pose.h"
#include <D2RLPlugin/context.h>
namespace upperbody {
using LogFn=void(*)(const char*);
bool Init(const D2RL::PluginContext*,LogFn);
void Tick(bool inGame,const Pose* own,uint32_t ownUnit);
size_t Remotes(Remote*,size_t);
void Shutdown();
}
