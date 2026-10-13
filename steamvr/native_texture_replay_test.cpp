#include "native_submission_capture.h"
#include "native_replay_packet.h"
#include "synchronous_replay_gpu.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace audit=framevr::nativecapture;
namespace gpu=framevr::synchronousgpu;
int main(){
    ComPtr<ID3D12Device> device;assert(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
    audit::EnableReplayPackets();assert(audit::Start());audit::ObserveDevice(device.Get());
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC qd{};assert(SUCCEEDED(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue))));audit::ObserveQueue(queue.Get());
    ComPtr<ID3D12CommandAllocator> allocator;assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
    ComPtr<ID3D12GraphicsCommandList> list;assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> texture;assert(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&texture))));
    audit::InitialResourceState(texture.Get(),D3D12_RESOURCE_STATE_COMMON);
    desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;desc.Format=DXGI_FORMAT_UNKNOWN;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=D3D12_RESOURCE_FLAG_NONE;heap.Type=D3D12_HEAP_TYPE_READBACK;
    ComPtr<ID3D12Resource> readback;assert(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))));
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> descriptors;assert(SUCCEEDED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&descriptors))));const auto rtv=descriptors->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(texture.Get(),nullptr,rtv);
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={texture.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET};list->ResourceBarrier(1,&b);
    const float color[]{.25f,.5f,.75f,1};list->ClearRenderTargetView(rtv,color,0,nullptr);
    b.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Flags=D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;list->ResourceBarrier(1,&b);b.Flags=D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=texture.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,1,1,1,256};list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    assert(SUCCEEDED(list->Close()));
    audit::BeginEye(GetCurrentThreadId());ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);audit::SealPresent();
    assert(audit::BeginReplayLease());audit::ReplayPacket packet;assert(audit::SnapshotReplay(packet));assert(audit::EndEye().DependenciesComplete());
    assert(packet.batches.size()==1&&packet.states.size()==1&&packet.states[0].first==D3D12_RESOURCE_STATE_COMMON&&packet.states[0].last==D3D12_RESOURCE_STATE_COPY_SOURCE);
    assert(gpu::Initialize(queue.Get())&&gpu::Drain(packet));
    unsigned char original[4]{};bool first=true;
    auto check=[&](){void* data=nullptr;D3D12_RANGE range{0,4};assert(SUCCEEDED(readback->Map(0,&range,&data)));const auto* pixel=static_cast<const unsigned char*>(data);
        if(first){assert(pixel[0]>=63&&pixel[0]<=64&&pixel[1]>=127&&pixel[1]<=128&&pixel[2]>=191&&pixel[2]<=192&&pixel[3]==255);std::memcpy(original,pixel,4);first=false;}
        else assert(!std::memcmp(original,pixel,4));
        D3D12_RANGE none{};readback->Unmap(0,&none);};
    check();assert(gpu::Execute(packet));check();assert(gpu::Execute(packet));check();
    audit::EndReplayLease();gpu::Shutdown();audit::Stop();assert(audit::Shutdown());
    std::puts("Native texture replay: original pixels preserved through two production replays, exact texture state restoration, matched split barriers and completion passed");
}
