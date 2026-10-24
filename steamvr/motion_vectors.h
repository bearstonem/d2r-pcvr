#pragma once
#include <d3d12.h>
#include <cstdint>

namespace framevr::motionvectors {
// The game's motion vectors: after the colour pass it draws the scene again
// into one R16G16_FLOAT target of the scene's size, over the scene's depth
// (the owner's draw survey, 2026-10-20: 2359 draws as the colour pass had,
// with DLSS off). It is the target DLSS gets as its vectors (vr/dlss_mv.cpp):
// in the drawn part's uv, where each point is less where it was a render
// back: the objects' own motion and the camera's against that render (the
// other eye's). Frame generation's game motion takes the points' own motion
// from it (framemotion::Build); this finds the target. (First written for By
// turns, #281, where it stayed off.)
void Register(void (*log)(const char*));
// Watched only while wanted (frame generation's game motion): the hooks see
// every target bound and every barrier, and otherwise return at once.
void Watch(bool on);
// This frame's target and the state the game's barriers last left it in.
// False: none seen, or its state not yet known.
struct Target {ID3D12Resource* texture=nullptr;D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;uint32_t width=0,height=0;};
bool Latest(Target& out);
}
