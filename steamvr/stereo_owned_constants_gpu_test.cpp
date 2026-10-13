// Standalone architecture fixture: one closed native draw list can consume
// different owned constants through GPU-ordered copies, without changing the
// game's upload bytes or placing a CPU wait between eyes. This does not enable
// CBV substitution in the mod and does not establish a D2R performance gain.
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT hr,const char* operation){
    if(FAILED(hr)){std::fprintf(stderr,"%s: %08lx\n",operation,static_cast<unsigned long>(hr));throw std::runtime_error(operation);}
}
struct Event {
    HANDLE handle=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    Event(){if(!handle)throw std::runtime_error("CreateEvent");}
    ~Event(){CloseHandle(handle);}
};
struct Commands {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    explicit Commands(ID3D12Device* device){
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"create allocator");
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"create list");
    }
    void Close(){Check(list->Close(),"close list");}
};
ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,D3D12_HEAP_TYPE type,UINT64 bytes,D3D12_RESOURCE_STATES state){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)),"create buffer");
    return resource;
}
void Upload(ID3D12Resource* resource,const void* data,size_t bytes){
    void* mapped=nullptr;D3D12_RANGE none{};Check(resource->Map(0,&none,&mapped),"map upload");
    std::memcpy(mapped,data,bytes);D3D12_RANGE written{0,bytes};resource->Unmap(0,&written);
}
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return barrier;
}
ComPtr<ID3DBlob> Shader(const char* source,const char* entry,const char* profile){
    ComPtr<ID3DBlob> code,errors;
    const HRESULT hr=D3DCompile(source,std::strlen(source),"owned-constants-fixture",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
    if(FAILED(hr)&&errors)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
    Check(hr,"compile synthetic shader");return code;
}
// This scope models ownership of all allocator/resource storage used by the
// queued pair. A single completion at the end covers every prior submission;
// unwinding after an injected failure performs the same drain before release.
struct Timeline {
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12Device> device;
    Event completed;
    UINT64 next=0;
    unsigned cpuCompletions=0;
    explicit Timeline(ID3D12Device* d):device(d){
        D3D12_COMMAND_QUEUE_DESC desc{};Check(d->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)),"create queue");
        Check(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"create completion fence");
    }
    bool Reusable(UINT64 ticket)const {const auto value=fence->GetCompletedValue();return value!=UINT64_MAX&&value>=ticket;}
    bool Complete(UINT64 ticket)noexcept {
        ++cpuCompletions;
        // A submission already owns the buffers. If signalling transiently
        // fails, keep ownership until a successful fence or device removal.
        while(FAILED(queue->Signal(fence.Get(),ticket))){if(FAILED(device->GetDeviceRemovedReason()))return false;Sleep(1);}
        const bool haveEvent=SUCCEEDED(fence->SetEventOnCompletion(ticket,completed.handle));
        for(;;){
            const auto value=fence->GetCompletedValue();
            if(value==UINT64_MAX||FAILED(device->GetDeviceRemovedReason()))return false;
            if(value>=ticket)return true;
            if(haveEvent)WaitForSingleObject(completed.handle,100);else Sleep(1);
        }
    }
};
struct PairLease {
    Timeline& timeline;
    const UINT64 ticket;
    bool pending=false;
    std::vector<ID3D12CommandList*> submitted;
    explicit PairLease(Timeline& t):timeline(t),ticket(++t.next){submitted.reserve(6);}
    ~PairLease(){if(pending)timeline.Complete(ticket);}
    void Submit(ID3D12GraphicsCommandList* list){
        ID3D12CommandList* native=list;pending=true;timeline.queue->ExecuteCommandLists(1,&native);submitted.push_back(native);
    }
    void Finish(){const bool ok=timeline.Complete(ticket);pending=false;Check(ok?S_OK:E_FAIL,"pair GPU completion");}
};
HRESULT ReleaseBlocker(ID3D12Fence* fence,UINT64 value,ID3D12Device* device)noexcept {
    const HRESULT first=fence->Signal(value);
    if(FAILED(first))while(SUCCEEDED(device->GetDeviceRemovedReason())){
        if(SUCCEEDED(fence->Signal(value)))break;Sleep(1);
    }
    return first;
}
// Construct after PairLease so unexpected fixture failures release the private
// blocker before the lease drains. Only a successfully started delayed-release
// worker may take over this responsibility in the intentional failure case.
struct QueueBlock {
    Timeline& timeline;
    ID3D12Fence* fence;
    const UINT64 value;
    bool armed=true;
    QueueBlock(Timeline& t,ID3D12Fence* f,UINT64 v):timeline(t),fence(f),value(v){Check(t.queue->Wait(f,v),"block pair on GPU");}
    ~QueueBlock(){if(armed)ReleaseBlocker(fence,value,timeline.device.Get());}
    void Release(){const HRESULT hr=ReleaseBlocker(fence,value,timeline.device.Get());armed=false;Check(hr,"release pair");}
};
void VerifyNoGpuErrors(ID3D12InfoQueue* info){
    if(!info)return;
    for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
        SIZE_T size=0;Check(info->GetMessage(i,nullptr,&size),"debug message size");std::vector<unsigned char> bytes(size);
        auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Check(info->GetMessage(i,message,&size),"debug message");
        if(message->Severity==D3D12_MESSAGE_SEVERITY_ERROR||message->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION){
            std::fprintf(stderr,"D3D12 validation: %s\n",message->pDescription);throw std::runtime_error("D3D12 validation error");
        }
    }
}
}
int main(int argc,char** argv){
    try{
        const bool warp=argc==2&&!std::strcmp(argv[1],"--warp");if(argc>1&&!warp)return 2;
        ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
        ComPtr<IDXGIAdapter> adapter;
        if(warp){ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"create DXGI factory");Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"select WARP");}
        ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"create device");
        ComPtr<ID3D12InfoQueue> info;device.As(&info);
        Timeline timeline(device.Get());
        // Root CBV has a stable, 256-byte-aligned DEFAULT-heap address. The
        // original UPLOAD buffer is populated once and remains unchanged.
        auto original=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,256,D3D12_RESOURCE_STATE_GENERIC_READ);
        auto patch=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,256,D3D12_RESOURCE_STATE_GENERIC_READ);
        auto owned=Buffer(device.Get(),D3D12_HEAP_TYPE_DEFAULT,256,D3D12_RESOURCE_STATE_COMMON);
        auto eyes=Buffer(device.Get(),D3D12_HEAP_TYPE_READBACK,1024,D3D12_RESOURCE_STATE_COPY_DEST);
        std::array<unsigned char,256> originalBytes;originalBytes.fill(0x5a);
        constexpr UINT head=0x12345678,tail=0x76543210;const std::array<float,4> leftColor{1,0,0,1};
        std::memcpy(originalBytes.data(),&head,sizeof head);std::memcpy(originalBytes.data()+252,&tail,sizeof tail);
        std::memcpy(originalBytes.data()+32,leftColor.data(),sizeof leftColor);Upload(original.Get(),originalBytes.data(),originalBytes.size());
        assert(owned->GetGPUVirtualAddress()%D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT==0);
        D3D12_HEAP_PROPERTIES ownedHeap{};Check(owned->GetHeapProperties(&ownedHeap,nullptr),"owned heap type");assert(ownedHeap.Type==D3D12_HEAP_TYPE_DEFAULT);

        D3D12_HEAP_PROPERTIES defaultHeap{};defaultHeap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC textureDesc{};textureDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width=textureDesc.Height=textureDesc.DepthOrArraySize=textureDesc.MipLevels=textureDesc.SampleDesc.Count=1;
        textureDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;textureDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ComPtr<ID3D12Resource> target;Check(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&textureDesc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target)),"create eye target");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
        ComPtr<ID3D12DescriptorHeap> descriptors;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&descriptors)),"create RTV heap");
        const auto rtv=descriptors->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(target.Get(),nullptr,rtv);
        D3D12_ROOT_PARAMETER root{};root.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;root.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.NumParameters=1;rootDesc.pParameters=&root;
        ComPtr<ID3DBlob> serialized;Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,nullptr),"serialize root signature");
        ComPtr<ID3D12RootSignature> signature;Check(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&signature)),"create root signature");
        constexpr char source[]=R"(
cbuffer Camera : register(b0) {
    uint4 head : packoffset(c0);
    float4 eyeColor : packoffset(c2);
    uint4 tail : packoffset(c15);
};
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p = id == 0 ? float2(-1,-1) : id == 1 ? float2(-1,3) : float2(3,-1);
    return float4(p,0,1);
}
float4 ps() : SV_Target {
    return head.x == 0x12345678 && tail.w == 0x76543210 ? eyeColor : float4(0,0,1,1);
}
)";
        const auto vs=Shader(source,"vs","vs_5_0"),ps=Shader(source,"ps","ps_5_0");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=signature.Get();
        pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
        pd.DepthStencilState.DepthEnable=FALSE;pd.DepthStencilState.StencilEnable=FALSE;
        pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=textureDesc.Format;pd.SampleDesc.Count=1;
        ComPtr<ID3D12PipelineState> pipeline;Check(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pipeline)),"create draw pipeline");

        Commands left(device.Get()),right(device.Get()),draw(device.Get()),leftEye(device.Get()),rightEye(device.Get());
        auto uploadList=[&](Commands& command,ID3D12Resource* from,UINT64 offset,UINT64 bytes){
            auto barrier=Transition(owned.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
            command.list->ResourceBarrier(1,&barrier);command.list->CopyBufferRegion(owned.Get(),offset,from,0,bytes);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);command.list->ResourceBarrier(1,&barrier);command.Close();
        };
        uploadList(left,original.Get(),0,256);uploadList(right,patch.Get(),32,16);
        draw.list->SetPipelineState(pipeline.Get());draw.list->SetGraphicsRootSignature(signature.Get());
        draw.list->SetGraphicsRootConstantBufferView(0,owned->GetGPUVirtualAddress());
        D3D12_VIEWPORT viewport{0,0,1,1,0,1};D3D12_RECT scissor{0,0,1,1};
        draw.list->RSSetViewports(1,&viewport);draw.list->RSSetScissorRects(1,&scissor);draw.list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
        draw.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);draw.list->DrawInstanced(3,1,0,0);draw.Close();
        auto copyEye=[&](Commands& command,UINT64 offset){
            auto barrier=Transition(target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);command.list->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=target.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=eyes.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Offset=offset;dst.PlacedFootprint.Footprint={textureDesc.Format,1,1,1,256};command.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);command.list->ResourceBarrier(1,&barrier);command.Close();
        };
        copyEye(leftEye,0);copyEye(rightEye,512);
        auto verify=[&](const std::array<unsigned char,4>& rightPixel,bool both=true){
            void* data=nullptr;D3D12_RANGE read{0,516};Check(eyes->Map(0,&read,&data),"map eye pixels");
            constexpr std::array<unsigned char,4> expectedLeft{255,0,0,255};const auto* bytes=static_cast<const unsigned char*>(data);
            assert(!std::memcmp(bytes,expectedLeft.data(),4));if(both)assert(!std::memcmp(bytes+512,rightPixel.data(),4));
            D3D12_RANGE none{};eyes->Unmap(0,&none);
            read={0,256};Check(original->Map(0,&read,&data),"verify borrowed upload");assert(!std::memcmp(data,originalBytes.data(),256));original->Unmap(0,&none);
        };
        ComPtr<ID3D12Fence> blocker;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&blocker)),"create queue blocker");
        UINT64 blockedValue=0,lastUse=0;
        const std::array<std::array<float,4>,3> rightColors{{{0,1,0,1},{1,1,0,1},{1,0,1,1}}};
        const std::array<std::array<unsigned char,4>,3> rightPixels{{{0,255,0,255},{255,255,0,255},{255,0,255,255}}};
        for(size_t frame=0;frame<rightColors.size();++frame){
            // This is the only point where a frame's staging slot is reused.
            assert(timeline.Reusable(lastUse));Upload(patch.Get(),rightColors[frame].data(),16);
            const auto priorCompletions=timeline.cpuCompletions;
            PairLease pair(timeline);lastUse=pair.ticket;
            QueueBlock blocked(timeline,blocker.Get(),++blockedValue);
            pair.Submit(left.list.Get());pair.Submit(draw.list.Get());pair.Submit(leftEye.list.Get());
            pair.Submit(right.list.Get());pair.Submit(draw.list.Get());pair.Submit(rightEye.list.Get());
            // The queue is still blocked: all six submissions have returned
            // with no CPU wait, and the same closed list is used for both eyes.
            assert(!timeline.Reusable(lastUse)&&timeline.cpuCompletions==priorCompletions);
            assert(pair.submitted.size()==6&&pair.submitted[1]==draw.list.Get()&&pair.submitted[4]==draw.list.Get());
            blocked.Release();pair.Finish();
            assert(timeline.Reusable(lastUse)&&timeline.cpuCompletions==priorCompletions+1);verify(rightPixels[frame]);
        }
        // A failure after submitting the left eye must still drain queued work
        // before the slot becomes reusable. Hold that work behind a fence until
        // another thread releases it; the exception path must wait for release.
        const auto beforeFailure=timeline.cpuCompletions;
        std::atomic<bool> released=false;HRESULT releaseResult=E_PENDING;
        std::thread release;
        struct Join {std::thread& worker;~Join(){if(worker.joinable())worker.join();}} join{release};
        struct InjectedFailure {};
        try{
            PairLease pair(timeline);lastUse=pair.ticket;
            QueueBlock blocked(timeline,blocker.Get(),++blockedValue);
            pair.Submit(left.list.Get());pair.Submit(draw.list.Get());pair.Submit(leftEye.list.Get());
            assert(!timeline.Reusable(lastUse));
            const auto value=blocked.value;
            release=std::thread([&,value]{Sleep(20);released=true;releaseResult=ReleaseBlocker(blocker.Get(),value,device.Get());});
            blocked.armed=false;throw InjectedFailure{};
        }catch(const InjectedFailure&){}
        release.join();Check(releaseResult,"release failed pair");
        assert(released.load()&&timeline.Reusable(lastUse)&&timeline.cpuCompletions==beforeFailure+1);verify({},false);
        VerifyNoGpuErrors(info.Get());
        std::printf("Owned constant GPU fixture (%s): one closed draw list, exact distinct eye pixels across three fence-protected frames, unchanged original upload, one final CPU completion per pair and failure drain passed (debug layer %s)\n",warp?"WARP":"hardware",debug?"enabled":"unavailable");
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"Owned constants fixture failed: %s\n",error.what());return 1;}
}
