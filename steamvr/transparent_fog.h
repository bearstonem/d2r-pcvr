#pragma once
#include <reshade.hpp>

namespace framevr::transparentfog {
void Register(void (*log)(const char*));
// A mask is usable only after a matching native draw was submitted for this
// eye. scale = viewport scale.xy / offset.xy in the mask's allocation.
bool Bind(reshade::api::effect_runtime*,bool wanted,float scale[4]);
void Shutdown();
}
