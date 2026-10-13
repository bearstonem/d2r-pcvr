// Standalone fixture: all objects/data belong to this process. No game assets,
// headset, runtime hooks or game fences are used.
#include "stereo_submission_plan.h"
#include "replay_audit_model.h"
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace framevr::stereoreplay;
namespace {
void Check(HRESULT hr,const char* name) {
    if(FAILED(hr)){std::fprintf(stderr,"%s: %08lx\n",name,(unsigned long)hr);throw std::runtime_error(name);}
}
template<class T> uint64_t Id(T* p){return reinterpret_cast<uint64_t>(p);}
struct Event {HANDLE h=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Event(){if(h)CloseHandle(h);}};
struct Queue {ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;UINT64 value=0;};
struct List {ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;};
struct Fixture {
    ComPtr<ID3D12Device> device;
    std::array<Queue,3> queues;
    std::array<List,4> lists;
    ComPtr<ID3D12Resource> gameInput,staging,camera,positions,output,readback;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    Event completed;
    Recordings recordings;
    Capture capture;
    unsigned executions=0;
    bool pending=false;
    Fixture() {
        Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
        const std::array types{D3D12_COMMAND_LIST_TYPE_COPY,D3D12_COMMAND_LIST_TYPE_COMPUTE,D3D12_COMMAND_LIST_TYPE_DIRECT};
        for(size_t i=0;i<queues.size();++i) {
            D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=types[i];
            Check(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queues[i].queue)),"queue");
            Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&queues[i].fence)),"fence");
        }
        for(size_t i=0;i<lists.size();++i) {
            const auto type=types[i<2?0:i-1];
            Check(device->CreateCommandAllocator(type,IID_PPV_ARGS(&lists[i].allocator)),"allocator");
            Check(device->CreateCommandList(0,type,lists[i].allocator.Get(),nullptr,IID_PPV_ARGS(&lists[i].list)),"list");
            recordings.AllocatorCreated(Id(lists[i].allocator.Get()));
            recordings.ListReset(Id(lists[i].list.Get()),Id(lists[i].allocator.Get()));
        }
        if(!completed.h)throw std::runtime_error("completion event");
        gameInput=Buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        staging=Buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        camera=Buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON);
        positions=Buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON);
        output=Buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        readback=Buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        const std::array<float,4> position{.75f,.25f,-5.f,1.f};
        Store(gameInput.Get(),position.data(),sizeof position);
        MakePipeline();Record();MakeCapture();
    }
    ~Fixture() {
        // Never unwind/free in-flight fixture buffers after a completion failure.
        // This exits only this standalone test process, never an injected game.
        if(pending){std::fputs("Fixture GPU work did not complete\n",stderr);ExitProcess(2);}
    }
    ComPtr<ID3D12Resource> Buffer(D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;
        desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
        desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=flags;
        ComPtr<ID3D12Resource> resource;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)),"buffer");return resource;
    }
    static void Store(ID3D12Resource* resource,const void* data,size_t size) {
        void* mapped=nullptr;D3D12_RANGE empty{};Check(resource->Map(0,&empty,&mapped),"upload map");
        std::memcpy(mapped,data,size);D3D12_RANGE written{0,size};resource->Unmap(0,&written);
    }
    void MakePipeline() {
        const char shader[]=R"(
cbuffer Eye : register(b0) {row_major float4x4 ViewProjection;}
StructuredBuffer<float4> Position : register(t0);
RWStructuredBuffer<float4> Projected : register(u0);
[numthreads(1,1,1)] void main() {Projected[0]=mul(Position[0],ViewProjection);}
)";
        ComPtr<ID3DBlob> code,errors,signature;
        const HRESULT compiled=D3DCompile(shader,sizeof shader-1,"stereo-fixture",nullptr,nullptr,"main","cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
        if(FAILED(compiled)&&errors)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
        Check(compiled,"compile shader");
        std::array<D3D12_ROOT_PARAMETER,3> parameters{};
        parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;
        for(auto& p:parameters){p.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;p.Descriptor.ShaderRegister=0;}
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=UINT(parameters.size());desc.pParameters=parameters.data();
        Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors),"serialize root");
        Check(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root)),"root signature");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root.Get();pso.CS={code->GetBufferPointer(),code->GetBufferSize()};
        Check(device->CreateComputePipelineState(&pso,IID_PPV_ARGS(&pipeline)),"compute pipeline");
    }
    void Record() {
        // Copy-queue buffer promotions decay to COMMON after this batch.
        lists[0].list->CopyBufferRegion(camera.Get(),0,staging.Get(),0,256);
        lists[1].list->CopyBufferRegion(positions.Get(),0,gameInput.Get(),0,16);
        auto* compute=lists[2].list.Get();
        compute->SetComputeRootSignature(root.Get());compute->SetPipelineState(pipeline.Get());
        compute->SetComputeRootConstantBufferView(0,camera->GetGPUVirtualAddress());
        compute->SetComputeRootShaderResourceView(1,positions->GetGPUVirtualAddress());
        compute->SetComputeRootUnorderedAccessView(2,output->GetGPUVirtualAddress());compute->Dispatch(1,1,1);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON};
        compute->ResourceBarrier(1,&barrier);
        lists[3].list->CopyBufferRegion(readback.Get(),0,output.Get(),0,16);
        for(auto& list:lists)Check(list.list->Close(),"close");
    }
    void MakeCapture() {
        const auto copy=Id(queues[0].queue.Get()),compute=Id(queues[1].queue.Get()),direct=Id(queues[2].queue.Get());
        const std::array computeList{Id(lists[2].list.Get())},directList{Id(lists[3].list.Get())};
        const std::array copyLists{Id(lists[0].list.Get()),Id(lists[1].list.Get())};
        // Native GPU waits may reference a Signal submitted later on another queue.
        capture.Wait(compute,1000,1);capture.Batch(compute,2,computeList,recordings);capture.Signal(compute,2000,1);
        capture.Wait(direct,2000,1);capture.Batch(direct,3,directList,recordings);
        capture.Batch(copy,1,copyLists,recordings);capture.Signal(copy,1000,1);
    }
    bool Run(const std::array<float,16>& matrix,std::array<float,4>& result) {
        const auto plan=capture.Plan(recordings);
        if(!plan.Ready())return false; // Reject changed recordings BEFORE GPU submission.
        if(pending)throw std::runtime_error("staging reused before completion");
        Store(staging.Get(),matrix.data(),sizeof matrix);
        Queue* previous=nullptr;
        for(const size_t index:plan.order) {
            const auto& batch=capture.Submissions().at(index);
            auto q=std::find_if(queues.begin(),queues.end(),[&](const Queue& q){return Id(q.queue.Get())==batch.queue;});
            if(q==queues.end())throw std::runtime_error("unknown queue");
            if(previous&&previous!=&*q)Check(q->queue->Wait(previous->fence.Get(),previous->value),"private queue wait");
            std::vector<ID3D12CommandList*> native;
            for(const auto& recording:batch.lists)native.push_back(reinterpret_cast<ID3D12CommandList*>(recording.list));
            q->queue->ExecuteCommandLists(UINT(native.size()),native.data());pending=true;++executions;
            Check(q->queue->Signal(q->fence.Get(),++q->value),"private queue signal");previous=&*q;
        }
        if(!previous)throw std::runtime_error("empty ready plan");
        Check(previous->fence->SetEventOnCompletion(previous->value,completed.h),"completion");
        if(WaitForSingleObject(completed.h,10000)!=WAIT_OBJECT_0||previous->fence->GetCompletedValue()<previous->value||
           FAILED(device->GetDeviceRemovedReason()))throw std::runtime_error("GPU completion");
        pending=false;
        void* mapped=nullptr;D3D12_RANGE read{0,sizeof result};Check(readback->Map(0,&read,&mapped),"readback map");
        std::memcpy(result.data(),mapped,sizeof result);D3D12_RANGE empty{};readback->Unmap(0,&empty);return true;
    }
};
std::array<float,16> Eye(float x,float offAxis) {
    using namespace framevr::replayaudit;
    Matrix view{1,0,0,0, 0,1,0,0, 0,0,1,0, -x,-1.6,0,1};
    const Matrix projection{1.2,0,0,0, 0,1.6,0,0, offAxis,.03,0,-1, 0,0,.2,0};
    const auto vp=Multiply(view,projection);std::array<float,16> out{};
    for(size_t i=0;i<out.size();++i)out[i]=float(vp[i]);return out;
}
void Verify(const std::array<float,4>& result,const std::array<float,16>& matrix) {
    const std::array<double,4> position{.75,.25,-5,1};
    for(size_t c=0;c<4;++c){double expected=0;for(size_t r=0;r<4;++r)expected+=position[r]*matrix[r*4+c];
        if(!std::isfinite(result[c])||std::abs(result[c]-expected)>1e-5)throw std::runtime_error("per-eye GPU projection mismatch");}
}
}
int main() {
    try {
        Fixture f;const auto left=Eye(-.032f,.11f),right=Eye(.032f,-.06f);
        std::array<float,4> l{},r{};
        if(!f.Run(left,l)||!f.Run(right,r))throw std::runtime_error("valid plan rejected");
        Verify(l,left);Verify(r,right);
        if(std::abs(l[0]-r[0])<.01f)throw std::runtime_error("eyes are not distinct");
        void* mapped=nullptr;D3D12_RANGE read{0,16};Check(f.gameInput->Map(0,&read,&mapped),"input map");
        const std::array<float,4> original{.75f,.25f,-5.f,1.f};
        const bool unchanged=std::memcmp(mapped,original.data(),sizeof original)==0;
        D3D12_RANGE empty{};f.gameInput->Unmap(0,&empty);if(!unchanged)throw std::runtime_error("game input changed");
        const auto before=f.executions;
        Check(f.lists[2].allocator->Reset(),"allocator reset after GPU completion");
        f.recordings.AllocatorReset(Id(f.lists[2].allocator.Get()));
        if(f.Run(right,r)||f.executions!=before)throw std::runtime_error("stale recording executed");
        std::puts("Native D3D12: copy/compute/direct batches follow fence dependencies; same recordings project distinct eyes through private constants; allocator reset rejects replay before submission");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
