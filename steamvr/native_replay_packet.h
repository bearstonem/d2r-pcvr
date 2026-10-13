#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

namespace framevr::nativecapture {
struct ReplayBatch {
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    std::vector<Microsoft::WRL::ComPtr<ID3D12CommandList>> lists;
};
struct ReplayState {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    UINT subresource;
    D3D12_RESOURCE_STATES first,last;
};
struct ReplayPacket {
    std::vector<ReplayBatch> batches;
    std::vector<ReplayState> states;
};
// Opt-in extension of the observer. Packet references prevent destruction;
// synchronous execution before returning Present prevents next-frame reuse.
// This does not provide an asynchronous resource/descriptor lifetime lease.
void EnableReplayPackets()noexcept;
bool SnapshotReplay(ReplayPacket&)noexcept;
const char* ReplayPacketFailure()noexcept;
// Excludes foreign native queue/list/allocator/barrier mutation until the
// synchronous replay and its GPU completion have finished. Owning calls pass.
bool BeginReplayLease()noexcept;
void EndReplayLease()noexcept;
void InitialResourceState(ID3D12Resource*,D3D12_RESOURCE_STATES)noexcept;
void ForgetResourceState(ID3D12Resource*)noexcept;
bool ResourceState(ID3D12Resource*,UINT,D3D12_RESOURCE_STATES&)noexcept;
}
