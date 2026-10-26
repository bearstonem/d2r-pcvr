#pragma once
namespace D2RL {struct PluginContext;}
namespace coop {
void Init(const D2RL::PluginContext* context,const wchar_t* ini);
void Tick();
void Shutdown();
}
