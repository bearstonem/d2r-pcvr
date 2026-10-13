#pragma once
#include <cstdint>
namespace reshade::api {struct effect_runtime;struct command_queue;struct swapchain;}
namespace framevr::stereoreplay {
using EyeFn=bool(*)(unsigned);
using EffectsFn=void(*)(reshade::api::effect_runtime*,bool);
void Register(bool enabled,EyeFn eye,EffectsFn effects);
void Configure(bool requested);
void BeginPair(bool supported);
bool Replayed();
void View(unsigned eye,const float matrix[16]);
void Projection(unsigned eye,const float matrix[16]);
void TemporalEvaluation();
void Shutdown();
}
