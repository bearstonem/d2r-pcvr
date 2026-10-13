#include "replay_audit_model.h"
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace framevr::replayaudit;
namespace {
void Check(HRESULT hr,const char* name){if(FAILED(hr)){std::fprintf(stderr,"%s: %08lx\n",name,(unsigned long)hr);throw std::runtime_error(name);}}
struct Event {HANDLE h=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Event(){if(h)CloseHandle(h);}};
}
int main() {
    try {
        ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
        D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;Check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"queue");
        D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=256;rd.Height=rd.DepthOrArraySize=rd.MipLevels=1;
        rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;ComPtr<ID3D12Resource> upload,readback;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"upload");
        heap.Type=D3D12_HEAP_TYPE_READBACK;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"readback");
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
        Event event;if(!event.h)throw std::runtime_error("event");
        UINT64 value=0;
        auto run=[&](){ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
            Check(queue->Signal(fence.Get(),++value),"signal");Check(fence->SetEventOnCompletion(value,event.h),"completion");
            if(WaitForSingleObject(event.h,10000)!=WAIT_OBJECT_0||fence->GetCompletedValue()!=value)throw std::runtime_error("GPU completion");};
        auto store=[&](uint32_t first,uint32_t second){void* p=nullptr;D3D12_RANGE empty{};Check(upload->Map(0,&empty,&p),"map upload");
            const uint32_t data[]={first,second};std::memcpy(p,data,sizeof data);D3D12_RANGE written{0,sizeof data};upload->Unmap(0,&written);};
        auto result=[&](){void* p=nullptr;D3D12_RANGE range{0,4};Check(readback->Map(0,&range,&p),"map readback");
            uint32_t v;std::memcpy(&v,p,4);D3D12_RANGE empty{};readback->Unmap(0,&empty);return v;};
        Commands audit;const auto id=reinterpret_cast<uint64_t>(list.Get());audit.Reset(id);
        audit.BeginEye();list->CopyBufferRegion(readback.Get(),0,upload.Get(),0,4);Check(list->Close(),"close");audit.Submit(1,0,id);
        store(0x11111111,0x22222222);run();if(result()!=0x11111111)throw std::runtime_error("first execution");
        if(audit.EndEye().invalidatedLists)throw std::runtime_error("stable list falsely invalidated");
        // Reusing the SAME closed recording works after GPU completion and data update.
        store(0x33333333,0x22222222);run();if(result()!=0x33333333)throw std::runtime_error("same-list replay");
        // Re-recording the SAME COM object makes an old captured pointer unusable.
        Check(allocator->Reset(),"allocator reset after completion");Check(list->Reset(allocator.Get(),nullptr),"list reset");audit.Reset(id);
        list->CopyBufferRegion(readback.Get(),0,upload.Get(),4,4);Check(list->Close(),"close changed list");
        if(audit.EndEye().invalidatedLists!=1)throw std::runtime_error("re-recorded list not rejected");
        run();if(result()!=0x22222222)throw std::runtime_error("changed recording");
        std::puts("Native D3D12: closed-list replay with updated private data succeeds; resetting the same list changes its work and is detected by the audit");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
