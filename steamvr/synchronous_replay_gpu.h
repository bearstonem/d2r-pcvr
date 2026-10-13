#pragma once
#include "native_replay_packet.h"
namespace framevr::synchronousgpu {
bool Initialize(ID3D12CommandQueue* direct);
bool Drain(const nativecapture::ReplayPacket&);
// Keeps native batch boundaries intact. State restoration and replay are
// submitted with GPU dependencies, then fully completed before this returns
// (including failure exits unless the device has been removed).
bool Execute(const nativecapture::ReplayPacket&);
bool Complete();
bool CopyDepth(ID3D12Resource* source,ID3D12Resource* destination,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to);
void Shutdown();
}
