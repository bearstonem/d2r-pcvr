#include "native_submission_capture.h"
#include "native_replay_packet.h"
#include "native_lock.h"
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace framevr::nativecapture {
namespace {
using Microsoft::WRL::ComPtr;
using namespace stereoreplay;
constexpr size_t HookSlots=8,AllocatorLimit=4096,FenceLimit=128,QueueLimit=16;
NativeRecursiveMutex stateMutex;
NativeMutex hookMutex,executionGate;
std::atomic<DWORD> leaseOwner{0};
thread_local unsigned gateDepth=0;
std::atomic<bool> tracking{false},installAllowed{false};
std::atomic<uint32_t> invocations{0},queueCalls{0},hookErrors{0},observerFaults{0},capacityFailures{0};
bool started=false,hooksActive=false,collecting=false,eyeOpen=false,wantTrace=false;
bool replayPackets=false;
const char* packetFailure="not captured";
struct ListBarriers {std::vector<D3D12_RESOURCE_BARRIER> barriers;std::vector<ComPtr<ID3D12Resource>> resources;bool unsupported=false;};
std::map<uint64_t,ListBarriers> listBarriers;
std::map<uint64_t,ComPtr<ID3D12CommandList>> packetLists;
std::map<uint64_t,ComPtr<ID3D12CommandQueue>> packetQueues;
std::map<uint64_t,std::map<UINT,D3D12_RESOURCE_STATES>> resourceStates;
thread_local unsigned barrierDepth=0;
thread_local uint32_t queueDepth=0;
uint32_t ownerThread=0;
Recordings recordings;
Capture capture;
Report report;
ComPtr<ID3D12Device> primaryDevice;
bool deviceObserved=false;
std::set<uint64_t> observedLists;
std::map<uint64_t,ComPtr<ID3D12CommandAllocator>> allocators;
std::map<uint64_t,ComPtr<ID3D12Fence>> fences;
std::map<uint64_t,D3D12_COMMAND_LIST_TYPE> queues;
std::vector<void*> ownedHooks;
template<class T> uint64_t Id(T* p){return reinterpret_cast<uint64_t>(p);}
template<class Fn> void Guard(Fn&& fn)noexcept {
    try{fn();}catch(...){observerFaults.fetch_add(1);}
}
struct Invocation {
    bool queue,topLevel=true,gateHeld=false;
    explicit Invocation(bool isQueue=false):queue(isQueue) {
        if(gateDepth++==0&&tracking.load()&&replayPackets&&leaseOwner.load()!=GetCurrentThreadId()){executionGate.lock_shared();gateHeld=true;}
        invocations.fetch_add(1);
        if(queue) {
            topLevel=queueDepth++==0;
            const auto before=topLevel?queueCalls.fetch_add(1):0;
            if(tracking.load())Guard([&]{std::lock_guard lock(stateMutex);if(collecting){
                if(!topLevel)++report.queueReentries;
                else {if(before)++report.queueOverlap;if(GetCurrentThreadId()!=ownerThread)++report.foreignCalls;}
            }});
        }
    }
    ~Invocation(){if(queue){--queueDepth;if(topLevel)queueCalls.fetch_sub(1);}invocations.fetch_sub(1);--gateDepth;if(gateHeld)executionGate.unlock_shared();}
};
using ExecuteFn=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
using QueueFenceFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,ID3D12Fence*,UINT64);
using CpuSignalFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Fence*,UINT64);
using ListResetFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
using AllocatorResetFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandAllocator*);
using CreateListFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,D3D12_COMMAND_LIST_TYPE,ID3D12CommandAllocator*,ID3D12PipelineState*,REFIID,void**);
using BarrierFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
using EnhancedBarrierFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList7*,UINT,const D3D12_BARRIER_GROUP*);
using CopyDescriptorsFn=void(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,D3D12_DESCRIPTOR_HEAP_TYPE);
using CopyDescriptorsSimpleFn=void(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_DESCRIPTOR_HEAP_TYPE);
template<class Fn> struct Hooks {std::array<void*,HookSlots> targets{};std::array<Fn,HookSlots> originals{};};
Hooks<ExecuteFn> executeHooks;
Hooks<QueueFenceFn> signalHooks,waitHooks;
Hooks<CpuSignalFn> cpuSignalHooks;
Hooks<ListResetFn> listResetHooks;
Hooks<AllocatorResetFn> allocatorResetHooks;
Hooks<CreateListFn> createListHooks;
Hooks<BarrierFn> barrierHooks;
Hooks<EnhancedBarrierFn> enhancedBarrierHooks;
Hooks<CopyDescriptorsFn> descriptorHooks;
Hooks<CopyDescriptorsSimpleFn> simpleDescriptorHooks;

void ObserveAllocator(ID3D12CommandAllocator*)noexcept;
void ObserveFence(ID3D12Fence*)noexcept;
void Failed(HRESULT hr)noexcept {
    if(FAILED(hr)&&tracking.load())Guard([&]{std::lock_guard lock(stateMutex);if(eyeOpen)++report.apiFailures;});
}
bool KnownQueue(ID3D12CommandQueue* queue) {
    std::lock_guard lock(stateMutex);return queues.contains(Id(queue));
}
template<size_t S> void STDMETHODCALLTYPE HookExecute(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* lists)noexcept {
    Invocation call(true);
    if(tracking.load()&&call.topLevel)Guard([&]{
        if(!KnownQueue(queue))ObserveQueue(queue);
        std::lock_guard lock(stateMutex);
        if(replayPackets&&lists&&count<=64)for(UINT i=0;i<count;++i){
            const auto saved=listBarriers.find(Id(lists[i]));if(saved==listBarriers.end())continue;
            for(const auto& b:saved->second.barriers){const auto& t=b.Transition;
                if(b.Flags==D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY)continue;
                if(t.Subresource==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)resourceStates[Id(t.pResource)].clear();
                resourceStates[Id(t.pResource)][t.Subresource]=t.StateAfter;
            }
        }
        if(!collecting||!queues.contains(Id(queue)))return;
        ++report.batches;report.lists+=count;
        if(!count||count>64||!lists){++report.capacityFailures;return;}
        std::array<uint64_t,64> ids{};
        for(UINT i=0;i<count;++i) {
            // Normalize native interfaces to the same base as ObserveList.
            ComPtr<ID3D12GraphicsCommandList> graphics;
            if(!lists[i]||FAILED(lists[i]->QueryInterface(IID_PPV_ARGS(&graphics)))){++report.unknownLists;continue;}
            ids[i]=Id(graphics.Get());if(!recordings.Snapshot(ids[i]))++report.unknownLists;
            if(replayPackets)packetLists.emplace(ids[i],lists[i]);
        }
        if(replayPackets)packetQueues.emplace(Id(queue),queue);
        capture.Batch(Id(queue),report.batches,std::span(ids.data(),count),recordings);
    });
    executeHooks.originals[S](queue,count,lists);
}
template<size_t S,bool Signal> HRESULT STDMETHODCALLTYPE HookQueueFence(ID3D12CommandQueue* queue,ID3D12Fence* fence,UINT64 value)noexcept {
    Invocation call(true);
    if(tracking.load()&&call.topLevel)Guard([&]{
        if(!KnownQueue(queue))ObserveQueue(queue);ObserveFence(fence);
        std::lock_guard lock(stateMutex);if(!collecting||!queues.contains(Id(queue)))return;
        if constexpr(Signal){++report.signals;capture.Signal(Id(queue),Id(fence),value);}
        else {++report.waits;capture.Wait(Id(queue),Id(fence),value);}
    });
    const HRESULT hr=(Signal?signalHooks.originals[S]:waitHooks.originals[S])(queue,fence,value);if(call.topLevel)Failed(hr);return hr;
}
template<size_t S> HRESULT STDMETHODCALLTYPE HookCpuSignal(ID3D12Fence* fence,UINT64 value)noexcept {
    Invocation call;
    if(tracking.load()&&!queueDepth)Guard([&]{ObserveFence(fence);std::lock_guard lock(stateMutex);if(eyeOpen&&fences.contains(Id(fence)))++report.cpuSignals;});
    const HRESULT hr=cpuSignalHooks.originals[S](fence,value);Failed(hr);return hr;
}
template<size_t S> HRESULT STDMETHODCALLTYPE HookListReset(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,ID3D12PipelineState* pipeline)noexcept {
    Invocation call;
    if(tracking.load())ObserveAllocator(allocator);
    const HRESULT hr=listResetHooks.originals[S](list,allocator,pipeline);Failed(hr);
    if(SUCCEEDED(hr)&&tracking.load())Guard([&]{
        std::lock_guard lock(stateMutex);
        if(allocators.contains(Id(allocator))){recordings.ListReset(Id(list),Id(allocator));if(replayPackets)listBarriers[Id(list)]={};if(eyeOpen)++report.listResets;}
    });return hr;
}
template<size_t S> HRESULT STDMETHODCALLTYPE HookAllocatorReset(ID3D12CommandAllocator* allocator)noexcept {
    Invocation call;const HRESULT hr=allocatorResetHooks.originals[S](allocator);Failed(hr);
    if(SUCCEEDED(hr)&&tracking.load())Guard([&]{std::lock_guard lock(stateMutex);
        if(allocators.contains(Id(allocator))){recordings.AllocatorReset(Id(allocator));if(eyeOpen)++report.allocatorResets;}
    });return hr;
}
template<size_t S> HRESULT STDMETHODCALLTYPE HookCreateList(ID3D12Device* device,UINT node,D3D12_COMMAND_LIST_TYPE type,
        ID3D12CommandAllocator* allocator,ID3D12PipelineState* pipeline,REFIID iid,void** output)noexcept {
    Invocation call;
    bool ours=false;
    if(tracking.load())Guard([&]{std::lock_guard lock(stateMutex);ours=device==primaryDevice.Get();});
    if(ours)ObserveAllocator(allocator);
    const HRESULT hr=createListHooks.originals[S](device,node,type,allocator,pipeline,iid,output);Failed(hr);
    if(ours&&SUCCEEDED(hr)&&output&&*output)Guard([&]{
        ComPtr<ID3D12GraphicsCommandList> list;
        if(SUCCEEDED(static_cast<IUnknown*>(*output)->QueryInterface(IID_PPV_ARGS(&list))))ObserveList(list.Get(),allocator);
    });return hr;
}
template<size_t... S> auto ExecuteDetours(std::index_sequence<S...>){return std::array<ExecuteFn,HookSlots>{&HookExecute<S>...};}
template<bool Signal,size_t... S> auto FenceDetours(std::index_sequence<S...>){return std::array<QueueFenceFn,HookSlots>{&HookQueueFence<S,Signal>...};}
template<size_t... S> auto CpuDetours(std::index_sequence<S...>){return std::array<CpuSignalFn,HookSlots>{&HookCpuSignal<S>...};}
template<size_t... S> auto ListDetours(std::index_sequence<S...>){return std::array<ListResetFn,HookSlots>{&HookListReset<S>...};}
template<size_t... S> auto AllocatorDetours(std::index_sequence<S...>){return std::array<AllocatorResetFn,HookSlots>{&HookAllocatorReset<S>...};}
template<size_t... S> auto CreateDetours(std::index_sequence<S...>){return std::array<CreateListFn,HookSlots>{&HookCreateList<S>...};}
template<size_t S> void STDMETHODCALLTYPE HookBarrier(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers)noexcept {
    Invocation call;const bool outer=barrierDepth++==0;
    if(outer&&tracking.load())Guard([&]{std::lock_guard lock(stateMutex);if(!replayPackets)return;
        auto& saved=listBarriers[Id(list)];
        if(!barriers||count>2048||saved.barriers.size()+count>2048){saved.unsupported=true;return;}
        for(UINT i=0;i<count;++i){const auto& b=barriers[i];
            if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING){saved.unsupported=true;continue;}
            ID3D12Resource* resource=b.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION?b.Transition.pResource:b.UAV.pResource;
            if(resource)saved.resources.emplace_back(resource);
            if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)saved.barriers.push_back(b);
        }
    });
    barrierHooks.originals[S](list,count,barriers);--barrierDepth;
}
template<size_t S> void STDMETHODCALLTYPE HookEnhancedBarrier(ID3D12GraphicsCommandList7* list,UINT count,const D3D12_BARRIER_GROUP* groups)noexcept {
    Invocation call;
    if(count&&tracking.load())Guard([&]{std::lock_guard lock(stateMutex);if(replayPackets)listBarriers[Id(list)].unsupported=true;});
    enhancedBarrierHooks.originals[S](list,count,groups);
}
template<size_t... S> auto BarrierDetours(std::index_sequence<S...>){return std::array<BarrierFn,HookSlots>{&HookBarrier<S>...};}
template<size_t... S> auto EnhancedDetours(std::index_sequence<S...>){return std::array<EnhancedBarrierFn,HookSlots>{&HookEnhancedBarrier<S>...};}
template<size_t S> void STDMETHODCALLTYPE HookDescriptors(ID3D12Device* device,UINT nd,const D3D12_CPU_DESCRIPTOR_HANDLE* d,const UINT* ds,UINT ns,const D3D12_CPU_DESCRIPTOR_HANDLE* s,const UINT* ss,D3D12_DESCRIPTOR_HEAP_TYPE type)noexcept {
    Invocation call;descriptorHooks.originals[S](device,nd,d,ds,ns,s,ss,type);
}
template<size_t S> void STDMETHODCALLTYPE HookSimpleDescriptors(ID3D12Device* device,UINT count,D3D12_CPU_DESCRIPTOR_HANDLE d,D3D12_CPU_DESCRIPTOR_HANDLE s,D3D12_DESCRIPTOR_HEAP_TYPE type)noexcept {
    Invocation call;simpleDescriptorHooks.originals[S](device,count,d,s,type);
}
template<size_t... S> auto DescriptorDetours(std::index_sequence<S...>){return std::array<CopyDescriptorsFn,HookSlots>{&HookDescriptors<S>...};}
template<size_t... S> auto SimpleDescriptorDetours(std::index_sequence<S...>){return std::array<CopyDescriptorsSimpleFn,HookSlots>{&HookSimpleDescriptors<S>...};}
constexpr auto Slots=std::make_index_sequence<HookSlots>{};
template<class Fn> struct DescriptorGate;
template<class... Args> struct DescriptorGate<void(STDMETHODCALLTYPE*)(ID3D12Device*,Args...)> {
    using Fn=void(STDMETHODCALLTYPE*)(ID3D12Device*,Args...);
    inline static Hooks<Fn> hooks;
    template<size_t S> static void STDMETHODCALLTYPE Call(ID3D12Device* device,Args... args)noexcept {Invocation invocation;hooks.originals[S](device,args...);}
    template<size_t... S> static auto Detours(std::index_sequence<S...>){return std::array<Fn,HookSlots>{&Call<S>...};}
};
template<class Fn> bool Install(Hooks<Fn>& hooks,void* target,const std::array<Fn,HookSlots>& detours) {
    std::lock_guard lock(hookMutex);if(!installAllowed.load())return false;
    for(const auto existing:hooks.targets)if(existing==target)return true;
    for(size_t i=0;i<HookSlots;++i)if(!hooks.targets[i]) {
        // Reserve bookkeeping before enabling the detour. Native calls remain
        // unchanged even if registration cannot complete.
        ownedHooks.reserve(ownedHooks.size()+1);
        if(MH_CreateHook(target,reinterpret_cast<void*>(detours[i]),reinterpret_cast<void**>(&hooks.originals[i]))!=MH_OK){++hookErrors;return false;}
        hooks.targets[i]=target;
        if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);hooks.targets[i]=nullptr;++hookErrors;return false;}
        ownedHooks.push_back(target);return true;
    }
    ++hookErrors;return false;
}
template<class T> void** Table(T* object){return *reinterpret_cast<void***>(object);}
bool SameDevice(ID3D12DeviceChild* child) {
    ComPtr<ID3D12Device> device;if(!child||FAILED(child->GetDevice(IID_PPV_ARGS(&device))))return false;
    std::lock_guard lock(stateMutex);return device.Get()==primaryDevice.Get();
}
void ObserveAllocator(ID3D12CommandAllocator* allocator)noexcept {
    if(!tracking.load()||!allocator)return;
    Guard([&]{
        {std::lock_guard lock(stateMutex);if(allocators.contains(Id(allocator)))return;}
        if(!SameDevice(allocator))return;
        if(!Install(allocatorResetHooks,Table(allocator)[8],AllocatorDetours(Slots)))return;
        std::lock_guard lock(stateMutex);if(!tracking.load()||allocators.contains(Id(allocator)))return;
        if(allocators.size()>=AllocatorLimit){++capacityFailures;return;}
        // Retention prevents allocator destruction/address reuse during the
        // audit. Reset is still observed. It does not protect game resources.
        allocators.emplace(Id(allocator),allocator);recordings.AllocatorCreated(Id(allocator));
    });
}
void ObserveFence(ID3D12Fence* fence)noexcept {
    if(!tracking.load()||!fence)return;
    Guard([&]{
        {std::lock_guard lock(stateMutex);if(fences.contains(Id(fence)))return;}
        if(!SameDevice(fence))return;
        if(!Install(cpuSignalHooks,Table(fence)[10],CpuDetours(Slots)))return;
        std::lock_guard lock(stateMutex);if(!tracking.load()||fences.contains(Id(fence)))return;
        if(fences.size()>=FenceLimit){++capacityFailures;return;}
        fences.emplace(Id(fence),fence);if(collecting)++report.newFences;
    });
}
void ReadGlobals() {
    report.hookErrors=hookErrors.load();report.observerFaults=observerFaults.load();report.capacityFailures+=capacityFailures.load();
    report.retainedAllocators=uint32_t(allocators.size());report.retainedFences=uint32_t(fences.size());
}
void PinIfUnsafe()noexcept {
    // An unload cannot free a still-running detour or an enabled hook. If
    // shutdown cannot drain it, retain this diagnostic module until process exit.
    HMODULE self=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&PinIfUnsafe),&self);
}
}
bool Report::DependenciesComplete()const {
    return sealed&&atPresent==stereoreplay::PlanStatus::Ready&&atEnd==stereoreplay::PlanStatus::Ready&&
        !unknownLists&&!queueOverlap&&!boundaryCalls&&!foreignCalls&&!apiFailures&&!cpuSignals&&
        !hookErrors&&!observerFaults&&!capacityFailures&&!unorderedSignalFences;
}
const char* StatusName(stereoreplay::PlanStatus status)noexcept {
    switch(status){
    case stereoreplay::PlanStatus::Ready:return "ready";
    case stereoreplay::PlanStatus::Empty:return "empty";
    case stereoreplay::PlanStatus::Overflow:return "overflow";
    case stereoreplay::PlanStatus::InvalidMetadata:return "invalid_metadata";
    case stereoreplay::PlanStatus::StaleRecording:return "stale_recording";
    case stereoreplay::PlanStatus::AmbiguousFence:return "ambiguous_fence";
    case stereoreplay::PlanStatus::MissingDependency:return "missing_dependency";
    case stereoreplay::PlanStatus::CyclicDependency:return "cyclic_dependency";
    }return "unknown";
}
bool Start()noexcept {
    bool result=false;Guard([&]{std::lock_guard lock(hookMutex);if(tracking.load()){result=true;return;}
        if(!started){const auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED){++hookErrors;return;}started=true;}
        // Patch once at a manual mode transition, outside stateMutex. MinHook
        // suspends threads while applying patches; holding journal locks here
        // could suspend a thread that the caller is waiting for.
        for(void* target:ownedHooks)if(MH_QueueEnableHook(target)!=MH_OK){++hookErrors;return;}
        if(MH_ApplyQueued()!=MH_OK){++hookErrors;return;}
        hooksActive=true;installAllowed=true;tracking=true;result=true;
    });return result;
}
void ObserveDevice(ID3D12Device* device)noexcept {
    if(!tracking.load()||!device)return;
    Guard([&]{
        {std::lock_guard lock(stateMutex);if(!tracking.load()||(primaryDevice&&primaryDevice.Get()!=device)||deviceObserved)return;primaryDevice=device;deviceObserved=true;}
        Install(createListHooks,Table(device)[12],CreateDetours(Slots));
        if(replayPackets){
            Install(descriptorHooks,Table(device)[23],DescriptorDetours(Slots));Install(simpleDescriptorHooks,Table(device)[24],SimpleDescriptorDetours(Slots));
            using Cbv=DescriptorGate<void(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_CONSTANT_BUFFER_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE)>;
            using Srv=DescriptorGate<void(STDMETHODCALLTYPE*)(ID3D12Device*,ID3D12Resource*,const D3D12_SHADER_RESOURCE_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE)>;
            using Uav=DescriptorGate<void(STDMETHODCALLTYPE*)(ID3D12Device*,ID3D12Resource*,ID3D12Resource*,const D3D12_UNORDERED_ACCESS_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE)>;
            using Rtv=DescriptorGate<void(STDMETHODCALLTYPE*)(ID3D12Device*,ID3D12Resource*,const D3D12_RENDER_TARGET_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE)>;
            using Dsv=DescriptorGate<void(STDMETHODCALLTYPE*)(ID3D12Device*,ID3D12Resource*,const D3D12_DEPTH_STENCIL_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE)>;
            using Sampler=DescriptorGate<void(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_SAMPLER_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE)>;
            Install(Cbv::hooks,Table(device)[17],Cbv::Detours(Slots));Install(Srv::hooks,Table(device)[18],Srv::Detours(Slots));
            Install(Uav::hooks,Table(device)[19],Uav::Detours(Slots));Install(Rtv::hooks,Table(device)[20],Rtv::Detours(Slots));
            Install(Dsv::hooks,Table(device)[21],Dsv::Detours(Slots));Install(Sampler::hooks,Table(device)[22],Sampler::Detours(Slots));
        }
    });
}
void ObserveQueue(ID3D12CommandQueue* queue)noexcept {
    if(!tracking.load()||!queue)return;
    Guard([&]{
        if(KnownQueue(queue))return;
        ComPtr<ID3D12Device> device;if(FAILED(queue->GetDevice(IID_PPV_ARGS(&device))))return;
        ObserveDevice(device.Get());if(!SameDevice(queue))return;
        const auto desc=queue->GetDesc();
        Install(executeHooks,Table(queue)[10],ExecuteDetours(Slots));
        Install(signalHooks,Table(queue)[14],FenceDetours<true>(Slots));
        Install(waitHooks,Table(queue)[15],FenceDetours<false>(Slots));
        std::lock_guard lock(stateMutex);if(!tracking.load())return;
        if(!queues.contains(Id(queue))&&queues.size()>=QueueLimit){++capacityFailures;return;}
        queues[Id(queue)]=desc.Type;
    });
}
void ObserveList(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* initialAllocator)noexcept {
    if(!tracking.load()||!list)return;
    Guard([&]{
        {std::lock_guard lock(stateMutex);if(!initialAllocator&&observedLists.contains(Id(list)))return;}
        ComPtr<ID3D12Device> device;if(FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return;
        ObserveDevice(device.Get());if(!SameDevice(list))return;
        Install(listResetHooks,Table(list)[10],ListDetours(Slots));
        if(replayPackets){
            Install(barrierHooks,Table(list)[26],BarrierDetours(Slots));
            ComPtr<ID3D12GraphicsCommandList7> enhanced;
            if(SUCCEEDED(list->QueryInterface(IID_PPV_ARGS(&enhanced))))Install(enhancedBarrierHooks,Table(enhanced.Get())[80],EnhancedDetours(Slots));
        }
        {std::lock_guard lock(stateMutex);observedLists.insert(Id(list));}
        if(initialAllocator){ObserveAllocator(initialAllocator);std::lock_guard lock(stateMutex);
            if(allocators.contains(Id(initialAllocator))){recordings.ListReset(Id(list),Id(initialAllocator));if(replayPackets)listBarriers[Id(list)]={};}}
    });
}
void ForgetList(uint64_t nativeList)noexcept {
    if(tracking.load())Guard([&]{std::lock_guard lock(stateMutex);recordings.ListDestroyed(nativeList);listBarriers.erase(nativeList);observedLists.erase(nativeList);});
}
void ForgetQueue(uint64_t nativeQueue)noexcept {
    if(tracking.load())Guard([&]{std::lock_guard lock(stateMutex);queues.erase(nativeQueue);});
}
void BeginEye(uint32_t owningThread,bool diagnosticTrace)noexcept {
    if(!tracking.load())return;
    Guard([&]{std::lock_guard lock(stateMutex);if(!tracking.load())return;
        collecting=false;capture=Capture{};report={};packetLists.clear();packetQueues.clear();ownerThread=owningThread;eyeOpen=true;wantTrace=diagnosticTrace;
        if(invocations.load())++report.boundaryCalls;
        for(const auto& [id,fence]:fences)capture.FenceAtStart(id,fence->GetCompletedValue());
        collecting=true;
    });
}
void SealPresent(bool renderingOnly)noexcept {
    if(!tracking.load())return;
    Guard([&]{std::lock_guard lock(stateMutex);if(!eyeOpen||report.sealed)return;
        collecting=false;if(invocations.load())++report.boundaryCalls;
        if(!renderingOnly)report.atPresent=capture.Plan(recordings).status;report.sealed=true;
    });
}
Report EndEye(bool renderingOnly)noexcept {
    Report result;
    if(!tracking.load())return result;
    Guard([&]{std::lock_guard lock(stateMutex);if(!eyeOpen)return;
        collecting=false;if(invocations.load())++report.boundaryCalls;
        if(!renderingOnly){
            const auto plan=capture.Plan(recordings);report.atEnd=plan.status;
            report.internalWaits=plan.internalWaits;report.externalWaits=plan.externalWaits;
            report.multiSignalFences=plan.multiSignalFences;report.unorderedSignalFences=plan.unorderedSignalFences;report.rewoundFences=plan.rewoundFences;report.belowStartFences=plan.belowStartFences;
            if(wantTrace)report.trace=capture.DiagnosticTrace();
        }else report.atEnd=report.atPresent;
        ReadGlobals();result=report;eyeOpen=false;
    });return result;
}
void Stop()noexcept {
    tracking=false;installAllowed=false;
    Guard([&]{std::lock_guard lock(hookMutex);
        bool disabled=true;
        for(void* target:ownedHooks)if(MH_QueueDisableHook(target)!=MH_OK)disabled=false;
        if(MH_ApplyQueued()!=MH_OK)disabled=false;
        if(disabled)hooksActive=false;else ++hookErrors;
    });
    Guard([&]{std::lock_guard lock(stateMutex);collecting=eyeOpen=false;capture=Capture{};recordings=Recordings{};
        fences.clear();allocators.clear();queues.clear();packetLists.clear();packetQueues.clear();listBarriers.clear();resourceStates.clear();observedLists.clear();primaryDevice.Reset();deviceObserved=false;
    });
}
void EnableReplayPackets()noexcept {Guard([&]{std::lock_guard lock(stateMutex);replayPackets=true;});}
bool BeginReplayLease()noexcept {
    if(gateDepth||leaseOwner.load()==GetCurrentThreadId())return false;
    executionGate.lock();leaseOwner=GetCurrentThreadId();return true;
}
void EndReplayLease()noexcept {if(leaseOwner.load()==GetCurrentThreadId()){leaseOwner=0;executionGate.unlock();}}
void InitialResourceState(ID3D12Resource* resource,D3D12_RESOURCE_STATES state)noexcept {
    if(tracking.load())Guard([&]{std::lock_guard lock(stateMutex);if(replayPackets&&resourceStates.size()<65536)resourceStates[Id(resource)][D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES]=state;});
}
void ForgetResourceState(ID3D12Resource* resource)noexcept {
    if(tracking.load())Guard([&]{std::lock_guard lock(stateMutex);resourceStates.erase(Id(resource));});
}
bool ResourceState(ID3D12Resource* resource,UINT subresource,D3D12_RESOURCE_STATES& state)noexcept {
    bool found=false;Guard([&]{std::lock_guard lock(stateMutex);const auto r=resourceStates.find(Id(resource));if(r==resourceStates.end())return;
        auto s=r->second.find(subresource);if(s==r->second.end())s=r->second.find(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
        if(s!=r->second.end()){state=s->second;found=true;}
    });return found;
}
bool SnapshotReplay(ReplayPacket& output)noexcept {
    bool ready=false;
    Guard([&]{std::lock_guard lock(stateMutex);
        const auto plan=capture.Plan(recordings);report.atPresent=plan.status;ReadGlobals();
        packetFailure="dependency/lifetime metadata incomplete";
        if(!replayPackets||!report.sealed||!plan.Ready()||report.unknownLists||report.queueOverlap||report.foreignCalls||
           report.boundaryCalls||report.apiFailures||report.cpuSignals||report.hookErrors||report.observerFaults||report.capacityFailures||invocations.load())return;
        ReplayPacket packet;
        std::map<std::pair<uint64_t,UINT>,size_t> states;
        std::map<std::pair<uint64_t,UINT>,std::pair<D3D12_RESOURCE_STATES,D3D12_RESOURCE_STATES>> split;
        for(const auto index:plan.order){const auto& batch=capture.Submissions()[index];
            if(!packetQueues.contains(batch.queue))return;
            ReplayBatch b;b.queue=packetQueues.at(batch.queue);
            for(const auto& recording:batch.lists){
                if(!packetLists.contains(recording.list)||!listBarriers.contains(recording.list)){packetFailure="unobserved native recording";return;}
                const auto& saved=listBarriers.at(recording.list);if(saved.unsupported){packetFailure="aliasing/enhanced barriers";return;}
                b.lists.push_back(packetLists.at(recording.list));
                for(const auto& barrier:saved.barriers){const auto& t=barrier.Transition;if(!t.pResource)return;
                    const auto desc=t.pResource->GetDesc();
                    // Buffers decay between ExecuteCommandLists calls. Keep
                    // native batches intact; no texture-state prelude for them.
                    if(desc.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER||(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)){
                        if(barrier.Flags!=D3D12_RESOURCE_BARRIER_FLAG_NONE){packetFailure="split barrier on decaying resource";return;}
                        continue;
                    }
                    D3D12_FEATURE_DATA_FORMAT_INFO info{desc.Format,0};
                    if(FAILED(primaryDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,&info,sizeof info))||!info.PlaneCount)return;
                    const UINT total=UINT(desc.MipLevels)*(desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE3D?1:desc.DepthOrArraySize)*info.PlaneCount;
                    const UINT first=t.Subresource==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES?0:t.Subresource;
                    const UINT count=t.Subresource==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES?total:1;
                    if(!total||total>8192||first>=total)return;
                    for(UINT sub=first;sub<first+count;++sub){
                        const auto key=std::pair{Id(t.pResource),sub};const auto found=states.find(key);
                        if(barrier.Flags==D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY){
                            if(split.contains(key)){packetFailure="nested split barrier";return;}
                            split.emplace(key,std::pair{t.StateBefore,t.StateAfter});
                            if(found==states.end()){states.emplace(key,packet.states.size());packet.states.push_back({t.pResource,sub,t.StateBefore,t.StateBefore});}
                            else if(packet.states[found->second].last!=t.StateBefore){packetFailure="split state chain";return;}
                            continue;
                        }
                        if(barrier.Flags==D3D12_RESOURCE_BARRIER_FLAG_END_ONLY){
                            const auto pending=split.find(key);if(pending==split.end()||pending->second!=std::pair{t.StateBefore,t.StateAfter}){packetFailure="unmatched split barrier";return;}
                            split.erase(pending);
                        }else if(split.contains(key)){packetFailure="transition during split";return;}
                        if(found==states.end()){
                            if(states.size()>=8192)return;
                            states.emplace(key,packet.states.size());packet.states.push_back({t.pResource,sub,t.StateBefore,t.StateAfter});
                        }else{
                            auto& state=packet.states[found->second];if(state.last!=t.StateBefore){packetFailure="texture state chain";return;}state.last=t.StateAfter;
                        }
                    }
                }
            }packet.batches.push_back(std::move(b));
        }
        if(!split.empty()){packetFailure="incomplete split barrier";return;}
        output=std::move(packet);packetFailure="ready";ready=true;
    });return ready;
}
const char* ReplayPacketFailure()noexcept {return packetFailure;}
uint32_t InstalledHooks()noexcept {
    uint32_t result=0;Guard([&]{std::lock_guard lock(hookMutex);result=uint32_t(ownedHooks.size());});return result;
}
uint32_t ActiveHooks()noexcept {
    uint32_t result=0;Guard([&]{std::lock_guard lock(hookMutex);if(hooksActive)result=uint32_t(ownedHooks.size());});return result;
}
bool Shutdown()noexcept {
    Stop();bool removed=true;
    try {
        std::vector<void*> targets;{std::lock_guard lock(hookMutex);targets=ownedHooks;}
        for(void* target:targets){const auto hr=MH_DisableHook(target);if(hr!=MH_OK&&hr!=MH_ERROR_DISABLED)removed=false;}
        const ULONGLONG deadline=GetTickCount64()+1000;
        while(invocations.load()&&GetTickCount64()<deadline)Sleep(1);
        if(invocations.load()||!removed){PinIfUnsafe();return false;}
        std::lock_guard lock(hookMutex);
        for(void* target:targets)if(MH_RemoveHook(target)!=MH_OK)removed=false;
        if(removed)ownedHooks.clear();else PinIfUnsafe();
    }catch(...){++observerFaults;removed=false;PinIfUnsafe();}
    return removed;
}
}
