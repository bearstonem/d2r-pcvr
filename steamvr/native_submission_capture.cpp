#include "native_submission_capture.h"
#include "native_lock.h"
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>

namespace framevr::nativecapture {
namespace {
using Microsoft::WRL::ComPtr;
using namespace stereoreplay;
constexpr size_t HookSlots=8,AllocatorLimit=4096,FenceLimit=128,QueueLimit=16;
NativeMutex stateMutex,hookMutex;
std::atomic<bool> tracking{false},installAllowed{false};
std::atomic<uint32_t> invocations{0},queueCalls{0},hookErrors{0},observerFaults{0},capacityFailures{0};
bool started=false,collecting=false,eyeOpen=false,wantTrace=false;
thread_local uint32_t queueDepth=0;
uint32_t ownerThread=0;
Recordings recordings;
Capture capture;
Report report;
ComPtr<ID3D12Device> primaryDevice;
std::map<uint64_t,ComPtr<ID3D12CommandAllocator>> allocators;
std::map<uint64_t,ComPtr<ID3D12Fence>> fences;
std::map<uint64_t,D3D12_COMMAND_LIST_TYPE> queues;
std::vector<void*> ownedHooks;
template<class T> uint64_t Id(T* p){return reinterpret_cast<uint64_t>(p);}
template<class Fn> void Guard(Fn&& fn)noexcept {
    try{fn();}catch(...){observerFaults.fetch_add(1);}
}
struct Invocation {
    bool queue,topLevel=true;
    explicit Invocation(bool isQueue=false):queue(isQueue) {
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
    ~Invocation(){if(queue){--queueDepth;if(topLevel)queueCalls.fetch_sub(1);}invocations.fetch_sub(1);}
};
using ExecuteFn=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
using QueueFenceFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,ID3D12Fence*,UINT64);
using CpuSignalFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Fence*,UINT64);
using ListResetFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
using AllocatorResetFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandAllocator*);
using CreateListFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,D3D12_COMMAND_LIST_TYPE,ID3D12CommandAllocator*,ID3D12PipelineState*,REFIID,void**);
template<class Fn> struct Hooks {std::array<void*,HookSlots> targets{};std::array<Fn,HookSlots> originals{};};
Hooks<ExecuteFn> executeHooks;
Hooks<QueueFenceFn> signalHooks,waitHooks;
Hooks<CpuSignalFn> cpuSignalHooks;
Hooks<ListResetFn> listResetHooks;
Hooks<AllocatorResetFn> allocatorResetHooks;
Hooks<CreateListFn> createListHooks;

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
        std::lock_guard lock(stateMutex);if(!collecting||!queues.contains(Id(queue)))return;
        ++report.batches;report.lists+=count;
        if(!count||count>64||!lists){++report.capacityFailures;return;}
        std::array<uint64_t,64> ids{};
        for(UINT i=0;i<count;++i) {
            // Normalize native interfaces to the same base as ObserveList.
            ComPtr<ID3D12GraphicsCommandList> graphics;
            if(!lists[i]||FAILED(lists[i]->QueryInterface(IID_PPV_ARGS(&graphics)))){++report.unknownLists;continue;}
            ids[i]=Id(graphics.Get());if(!recordings.Snapshot(ids[i]))++report.unknownLists;
        }
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
        if(allocators.contains(Id(allocator))){recordings.ListReset(Id(list),Id(allocator));if(eyeOpen)++report.listResets;}
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
constexpr auto Slots=std::make_index_sequence<HookSlots>{};
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
        if(!SameDevice(allocator))return;
        {std::lock_guard lock(stateMutex);if(allocators.contains(Id(allocator)))return;}
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
        if(!SameDevice(fence))return;
        {std::lock_guard lock(stateMutex);if(fences.contains(Id(fence)))return;}
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
        !hookErrors&&!observerFaults&&!capacityFailures;
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
    bool result=false;Guard([&]{std::lock_guard lock(stateMutex);if(started)return;
        const auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED){++hookErrors;return;}
        started=true;installAllowed=true;tracking=true;result=true;
    });return result;
}
void ObserveDevice(ID3D12Device* device)noexcept {
    if(!tracking.load()||!device)return;
    Guard([&]{
        {std::lock_guard lock(stateMutex);if(!tracking.load()||(primaryDevice&&primaryDevice.Get()!=device))return;primaryDevice=device;}
        Install(createListHooks,Table(device)[12],CreateDetours(Slots));
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
        ComPtr<ID3D12Device> device;if(FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return;
        ObserveDevice(device.Get());if(!SameDevice(list))return;
        Install(listResetHooks,Table(list)[10],ListDetours(Slots));
        if(initialAllocator){ObserveAllocator(initialAllocator);std::lock_guard lock(stateMutex);
            if(allocators.contains(Id(initialAllocator)))recordings.ListReset(Id(list),Id(initialAllocator));}
    });
}
void ForgetList(uint64_t nativeList)noexcept {
    if(tracking.load())Guard([&]{std::lock_guard lock(stateMutex);recordings.ListDestroyed(nativeList);});
}
void ForgetQueue(uint64_t nativeQueue)noexcept {
    if(tracking.load())Guard([&]{std::lock_guard lock(stateMutex);queues.erase(nativeQueue);});
}
void BeginEye(uint32_t owningThread,bool diagnosticTrace)noexcept {
    if(!tracking.load())return;
    Guard([&]{std::lock_guard lock(stateMutex);if(!tracking.load())return;
        collecting=false;capture=Capture{};report={};ownerThread=owningThread;eyeOpen=true;wantTrace=diagnosticTrace;
        if(invocations.load())++report.boundaryCalls;
        for(const auto& [id,fence]:fences)capture.FenceAtStart(id,fence->GetCompletedValue());
        collecting=true;
    });
}
void SealPresent()noexcept {
    if(!tracking.load())return;
    Guard([&]{std::lock_guard lock(stateMutex);if(!eyeOpen||report.sealed)return;
        collecting=false;if(invocations.load())++report.boundaryCalls;
        report.atPresent=capture.Plan(recordings).status;report.sealed=true;
    });
}
Report EndEye()noexcept {
    Report result;
    if(!tracking.load())return result;
    Guard([&]{std::lock_guard lock(stateMutex);if(!eyeOpen)return;
        collecting=false;if(invocations.load())++report.boundaryCalls;
        const auto plan=capture.Plan(recordings);report.atEnd=plan.status;
        report.internalWaits=plan.internalWaits;report.externalWaits=plan.externalWaits;
        report.multiSignalFences=plan.multiSignalFences;report.rewoundFences=plan.rewoundFences;report.belowStartFences=plan.belowStartFences;
        if(wantTrace)report.trace=capture.DiagnosticTrace();
        ReadGlobals();result=report;eyeOpen=false;
    });return result;
}
void Stop()noexcept {
    tracking=false;installAllowed=false;
    Guard([&]{std::lock_guard lock(stateMutex);collecting=eyeOpen=false;capture=Capture{};recordings=Recordings{};
        fences.clear();allocators.clear();queues.clear();primaryDevice.Reset();
    });
}
uint32_t InstalledHooks()noexcept {
    uint32_t result=0;Guard([&]{std::lock_guard lock(hookMutex);result=uint32_t(ownedHooks.size());});return result;
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
