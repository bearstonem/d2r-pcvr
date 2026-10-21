#pragma once
#include <d3d12.h>
#include <cstdint>

namespace framevr::effectslayer {
// The game's effects layer: spells, flames, Tyrael's wings, drawn additively
// into two R16G16B16A16_FLOAT targets of the scene's size, tested against the
// scene's depth but never writing it, and laid over the picture near the
// frame's end (the owner's draw survey, 2026-10-20: the same in every area
// surveyed). By turns, with no depth of their own, they were moved across
// with the depth behind them and stood where the drawn eye saw them; this
// finds the layer so the eye not drawn knows where they are (TurnWarp::Effects).
void Register(void (*log)(const char*));
// This frame's layer: both targets and the state the game's barriers last
// left each in. False: none seen, or its states not yet known.
struct Layer {ID3D12Resource* targets[2]{};D3D12_RESOURCE_STATES states[2]{};uint32_t width=0,height=0;};
bool Latest(Layer& out);
// ReShade's resource usage as a D3D12 state: the same bits but for the
// general (COMMON) and constant-buffer ones.
inline D3D12_RESOURCE_STATES State(uint32_t usage) {
    if(usage&0x80000000u)return D3D12_RESOURCE_STATE_COMMON;
    return D3D12_RESOURCE_STATES((usage&~0x8000u)|((usage&0x8000u)?D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER:0));
}
}
