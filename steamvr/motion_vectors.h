#pragma once
#include <d3d12.h>
#include <cstdint>

namespace framevr::motionvectors {
// The game's motion vectors: after the colour pass it draws the scene again
// into one R16G16_FLOAT target of the scene's size, over the scene's depth
// (the owner's draw survey, 2026-10-20: 2359 draws as the colour pass had,
// with DLSS off). It is the target DLSS gets as its vectors (vr/dlss_mv.cpp):
// in the drawn part's uv, where each point is less where it was a render
// back - the objects' own motion, and nearer than some distance the camera's
// against the other eye too. By turns, the eye not drawn moves its own older
// picture on by it (TurnDepth::motion); this finds the target.
void Register(void (*log)(const char*));
// This frame's target and the state the game's barriers last left it in.
// False: none seen, or its state not yet known.
struct Target {ID3D12Resource* texture=nullptr;D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;uint32_t width=0,height=0;};
bool Latest(Target& out);
}
