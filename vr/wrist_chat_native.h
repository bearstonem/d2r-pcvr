#pragma once
#include <cstdint>
#include <string>
namespace D2RL {struct PluginContext;}
namespace wristchatnative {
void Init(const D2RL::PluginContext*,bool (*typing)(),bool (*supported)());
void Want(bool visible,uint64_t session);
void Tick();
void Stop();
bool Ready();
bool Typing();
bool Supported();
std::string Draft();
}
