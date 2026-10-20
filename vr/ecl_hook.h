// A look at every ExecuteCommandLists of a D3D12 queue: which lists the game
// put in one call (steamvr/replay_groups.h). Adapted from
// natpoh/diablo2-vr-port 8bd1d3b vr/ecl_hook.h (MIT, Copyright (c) 2026
// BodyWalkVR). The observer is told before the call goes on to the real
// function, on the calling thread; it must be quick and must not call the
// queue's ExecuteCommandLists itself.
#pragma once

struct ID3D12CommandQueue;
struct ID3D12CommandList;

namespace eclhook {

using Observer = void (*)(ID3D12CommandQueue* q, unsigned n, ID3D12CommandList* const* lists);

// Hooks the ExecuteCommandLists of q's vtable (once for each of the three queue
// types, once for each function: the types may share one). The observer is
// global (the last one set). True when the hook is in. MinHook must be
// initialised.
bool Install(ID3D12CommandQueue* q, Observer obs);

}  // namespace eclhook
