// Native interception fixture with private D3D12 objects and synthetic bytes.
#include "native_submission_capture.h"
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace audit=framevr::nativecapture;
using framevr::stereoreplay::PlanStatus;
namespace {
void Check(HRESULT hr,const char* name) {
    if(FAILED(hr)){std::fprintf(stderr,"%s: %08lx\n",name,(unsigned long)hr);throw std::runtime_error(name);}
}
struct Event {HANDLE h=CreateEventW(nullptr,FALSE,FALSE,nullptr);~Event(){if(h)CloseHandle(h);}};
struct Queue {
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
};
// An API wrapper that forwards to another intercepted native implementation.
// Its original calls must execute, while its internal calls must not become
// duplicate application submissions/dependencies in the journal.
class QueueAlias final:public ID3D12CommandQueue {
    std::atomic<ULONG> references{1};
    ComPtr<ID3D12CommandQueue> target;
public:
    explicit QueueAlias(ID3D12CommandQueue* q):target(q){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(ID3D12Object)||iid==__uuidof(ID3D12DeviceChild)||
           iid==__uuidof(ID3D12Pageable)||iid==__uuidof(ID3D12CommandQueue)){*out=this;AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references;}
    ULONG STDMETHODCALLTYPE Release()override {const auto n=--references;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID key,UINT* size,void* data)override{return target->GetPrivateData(key,size,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID key,UINT size,const void* data)override{return target->SetPrivateData(key,size,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID key,const IUnknown* data)override{return target->SetPrivateDataInterface(key,data);}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR name)override{return target->SetName(name);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID iid,void** out)override{return target->GetDevice(iid,out);}
    void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource* resource,UINT n,const D3D12_TILED_RESOURCE_COORDINATE* starts,
        const D3D12_TILE_REGION_SIZE* sizes,ID3D12Heap* heap,UINT ranges,const D3D12_TILE_RANGE_FLAGS* flags,
        const UINT* offsets,const UINT* counts,D3D12_TILE_MAPPING_FLAGS mapping)override {
        target->UpdateTileMappings(resource,n,starts,sizes,heap,ranges,flags,offsets,counts,mapping);
    }
    void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource* dst,const D3D12_TILED_RESOURCE_COORDINATE* dstStart,
        ID3D12Resource* src,const D3D12_TILED_RESOURCE_COORDINATE* srcStart,const D3D12_TILE_REGION_SIZE* size,D3D12_TILE_MAPPING_FLAGS flags)override {
        target->CopyTileMappings(dst,dstStart,src,srcStart,size,flags);
    }
    __declspec(noinline) void STDMETHODCALLTYPE ExecuteCommandLists(UINT count,ID3D12CommandList*const* lists)override {target->ExecuteCommandLists(count,lists);}
    void STDMETHODCALLTYPE SetMarker(UINT metadata,const void* data,UINT size)override {target->SetMarker(metadata,data,size);}
    void STDMETHODCALLTYPE BeginEvent(UINT metadata,const void* data,UINT size)override {target->BeginEvent(metadata,data,size);}
    void STDMETHODCALLTYPE EndEvent()override {target->EndEvent();}
    __declspec(noinline) HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* fence,UINT64 value)override{return target->Signal(fence,value);}
    __declspec(noinline) HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* fence,UINT64 value)override{return target->Wait(fence,value);}
    HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* frequency)override{return target->GetTimestampFrequency(frequency);}
    HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* gpu,UINT64* cpu)override{return target->GetClockCalibration(gpu,cpu);}
#ifdef WIDL_EXPLICIT_AGGREGATE_RETURNS
    D3D12_COMMAND_QUEUE_DESC* STDMETHODCALLTYPE GetDesc(D3D12_COMMAND_QUEUE_DESC* out)override {*out=target->GetDesc();return out;}
#else
    D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc()override{return target->GetDesc();}
#endif
};
struct Fixture {
    ComPtr<ID3D12Device> device;
    std::array<Queue,3> queues;
    ComPtr<ID3D12CommandQueue> aliasedCopy;
    ComPtr<ID3D12GraphicsCommandList> extraCopy,preexisting;
    ComPtr<ID3D12CommandAllocator> preexistingAllocator;
    ComPtr<ID3D12Fence> cpuFence;
    ComPtr<ID3D12Resource> upload,a,b,readback;
    Event event;
    bool pending=false;
    Fixture() {
        Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,IID_PPV_ARGS(&preexistingAllocator)),"preexisting allocator");
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COPY,preexistingAllocator.Get(),nullptr,IID_PPV_ARGS(&preexisting)),"preexisting list");
        Check(preexisting->Close(),"close preexisting list");
        // Default-off observation must install nothing and begin no capture.
        audit::ObserveDevice(device.Get());audit::BeginEye(GetCurrentThreadId());
        assert(audit::InstalledHooks()==0&&audit::EndEye().atEnd==PlanStatus::Empty);
        assert(audit::Start());audit::ObserveDevice(device.Get());
        const std::array types{D3D12_COMMAND_LIST_TYPE_COPY,D3D12_COMMAND_LIST_TYPE_COMPUTE,D3D12_COMMAND_LIST_TYPE_DIRECT};
        for(size_t i=0;i<queues.size();++i) {
            auto& q=queues[i];D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=types[i];
            Check(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&q.queue)),"queue");audit::ObserveQueue(q.queue.Get());
            Check(device->CreateCommandAllocator(types[i],IID_PPV_ARGS(&q.allocator)),"allocator");
            // The native CreateCommandList interception learns the allocator.
            Check(device->CreateCommandList(0,types[i],q.allocator.Get(),nullptr,IID_PPV_ARGS(&q.list)),"list");
            Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&q.fence)),"fence");
            Check(q.queue->Signal(q.fence.Get(),0),"warm fence");
        }
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&cpuFence)),"CPU fence");
        Check(queues[2].queue->Wait(cpuFence.Get(),0),"observe CPU fence before capture");
        if(!event.h)throw std::runtime_error("event");
        upload=Buffer(D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        a=Buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON);
        b=Buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON);
        readback=Buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        void* data=nullptr;D3D12_RANGE empty{};Check(upload->Map(0,&empty,&data),"upload map");
        const std::array<uint32_t,2> values{0x11223344,0x55667788};std::memcpy(data,values.data(),sizeof values);
        D3D12_RANGE written{0,sizeof values};upload->Unmap(0,&written);
        queues[0].list->CopyBufferRegion(a.Get(),0,upload.Get(),0,4);
        queues[1].list->CopyBufferRegion(b.Get(),0,a.Get(),0,4);
        queues[2].list->CopyBufferRegion(readback.Get(),0,b.Get(),0,4);
        for(auto& q:queues)Check(q.list->Close(),"close");
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COPY,queues[0].allocator.Get(),nullptr,IID_PPV_ARGS(&extraCopy)),"second list in copy batch");
        extraCopy->CopyBufferRegion(a.Get(),4,upload.Get(),4,4);Check(extraCopy->Close(),"close second copy list");
        aliasedCopy.Attach(new QueueAlias(queues[0].queue.Get()));audit::ObserveQueue(aliasedCopy.Get());
        assert(audit::InstalledHooks()>0);
    }
    ~Fixture(){if(pending){std::fputs("Fixture work did not complete\n",stderr);ExitProcess(2);}audit::Shutdown();}
    ComPtr<ID3D12Resource> Buffer(D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC desc{};
        desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;
        desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> result;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result)),"buffer");return result;
    }
    void Submit(size_t index,ID3D12CommandQueue* overrideQueue=nullptr) {
        ID3D12CommandList* lists[]={queues[index].list.Get(),extraCopy.Get()};
        pending=true;(overrideQueue?overrideQueue:queues[index].queue.Get())->ExecuteCommandLists(index==0?2:1,lists);
    }
    void Wait(size_t index,uint64_t value) {
        auto* fence=queues[index].fence.Get();Check(fence->SetEventOnCompletion(value,event.h),"completion event");
        if(WaitForSingleObject(event.h,10000)!=WAIT_OBJECT_0||fence->GetCompletedValue()<value||
           FAILED(device->GetDeviceRemovedReason()))throw std::runtime_error("GPU completion");
        pending=false;
    }
    uint32_t Value() {
        void* data=nullptr;D3D12_RANGE range{0,4};Check(readback->Map(0,&range,&data),"readback map");
        uint32_t value;std::memcpy(&value,data,4);D3D12_RANGE empty{};readback->Unmap(0,&empty);return value;
    }
    audit::Report Chain(uint64_t value,bool wrapped=false) {
        audit::BeginEye(GetCurrentThreadId(),wrapped);
        // Actual API calls put a consumer's GPU wait before its producer's CPU
        // submission. The native journal must recover both cross-queue edges.
        Check(queues[1].queue->Wait(queues[0].fence.Get(),value),"compute wait");
        Submit(1);Check(queues[1].queue->Signal(queues[1].fence.Get(),value),"compute signal");
        Check(queues[2].queue->Wait(queues[1].fence.Get(),value),"direct wait");
        Submit(2);Check(queues[2].queue->Signal(queues[2].fence.Get(),value),"direct signal");
        auto* copyQueue=wrapped?aliasedCopy.Get():queues[0].queue.Get();
        Submit(0,copyQueue);Check(copyQueue->Signal(queues[0].fence.Get(),value),"copy signal");
        audit::SealPresent();Wait(2,value);return audit::EndEye();
    }
};
void Print(const audit::Report& r) {
    std::printf("native: batches=%u lists=%u signals=%u waits=%u unknown=%u overlap=%u reentries=%u boundary=%u foreign=%u hooks=%u faults=%u capacity=%u present=%s end=%s internal=%u external=%u\n",
        r.batches,r.lists,r.signals,r.waits,r.unknownLists,r.queueOverlap,r.queueReentries,r.boundaryCalls,r.foreignCalls,r.hookErrors,r.observerFaults,r.capacityFailures,
        audit::StatusName(r.atPresent),audit::StatusName(r.atEnd),r.internalWaits,r.externalWaits);
}
}
int main() {
    try {
        Fixture f;
        const auto first=f.Chain(1);Print(first);
        assert(first.DependenciesComplete()&&first.batches==3&&first.lists==4&&first.signals==3&&first.waits==2&&first.internalWaits==2);
        assert(first.retainedAllocators==3&&first.retainedFences==4&&f.Value()==0x11223344);
        // Reset the allocator only after ORIGINAL work completed. The saved
        // recording must become stale between Present and draw return.
        audit::BeginEye(GetCurrentThreadId());f.Submit(0);
        Check(f.queues[0].queue->Signal(f.queues[0].fence.Get(),2),"copy signal 2");audit::SealPresent();f.Wait(0,2);
        Check(f.queues[0].allocator->Reset(),"allocator reset");
        const auto stale=audit::EndEye();Print(stale);
        assert(stale.atPresent==PlanStatus::Ready&&stale.atEnd==PlanStatus::StaleRecording&&!stale.DependenciesComplete()&&stale.allocatorResets==1);
        // Re-record the same native object, now with different work. No address
        // change is needed for the journal to recognize a fresh generation.
        Check(f.queues[0].list->Reset(f.queues[0].allocator.Get(),nullptr),"list reset");
        f.queues[0].list->CopyBufferRegion(f.a.Get(),0,f.upload.Get(),4,4);Check(f.queues[0].list->Close(),"close changed copy");
        Check(f.extraCopy->Reset(f.queues[0].allocator.Get(),nullptr),"second copy reset");
        f.extraCopy->CopyBufferRegion(f.a.Get(),4,f.upload.Get(),0,4);Check(f.extraCopy->Close(),"close changed second copy");
        const auto changed=f.Chain(3,true);Print(changed);assert(changed.DependenciesComplete()&&f.Value()==0x55667788);
        assert(changed.batches==3&&changed.lists==4&&changed.signals==3&&changed.waits==2&&changed.queueReentries>=2&&!changed.queueOverlap);
        assert(changed.trace.events.size()==8&&!changed.trace.truncated);
        audit::BeginEye(GetCurrentThreadId());Check(f.cpuFence->Signal(1),"original CPU signal");
        f.Submit(0);Check(f.queues[0].queue->Signal(f.queues[0].fence.Get(),4),"copy signal 4");audit::SealPresent();
        Check(f.cpuFence->Signal(2),"CPU signal after Present boundary");f.Wait(0,4);
        const auto cpu=audit::EndEye();assert(cpu.cpuSignals==2&&!cpu.DependenciesComplete()&&f.cpuFence->GetCompletedValue()==2);
        // A list made before the observer starts is incomplete until a real
        // Reset supplies its allocator; observing its pointer is insufficient.
        audit::ObserveList(f.preexisting.Get());audit::BeginEye(GetCurrentThreadId());
        ID3D12CommandList* old[]={f.preexisting.Get()};f.pending=true;
        f.queues[0].queue->ExecuteCommandLists(1,old);Check(f.queues[0].queue->Signal(f.queues[0].fence.Get(),5),"old list signal");
        audit::SealPresent();f.Wait(0,5);const auto unknown=audit::EndEye();
        assert(unknown.unknownLists==1&&!unknown.DependenciesComplete()&&unknown.atEnd==PlanStatus::InvalidMetadata);
        Check(f.preexistingAllocator->Reset(),"preexisting allocator reset after completion");
        Check(f.preexisting->Reset(f.preexistingAllocator.Get(),nullptr),"preexisting list reset");Check(f.preexisting->Close(),"close observed list");
        audit::BeginEye(GetCurrentThreadId());f.pending=true;f.queues[0].queue->ExecuteCommandLists(1,old);
        Check(f.queues[0].queue->Signal(f.queues[0].fence.Get(),6),"observed list signal");audit::SealPresent();f.Wait(0,6);
        assert(audit::EndEye().DependenciesComplete());
        audit::Stop();audit::BeginEye(GetCurrentThreadId());
        Check(f.queues[0].queue->Signal(f.queues[0].fence.Get(),7),"pass-through signal after stop");f.Wait(0,7);
        assert(audit::EndEye().atEnd==PlanStatus::Empty);
        assert(audit::Shutdown()&&audit::InstalledHooks()==0);
        std::puts("Native capture: original three-queue GPU results/order preserved, allocator/list generations observed, CPU signals flagged, default-off and clean hook removal verified");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());audit::Shutdown();return 1;}
}
