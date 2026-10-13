#include "synchronous_replay_gpu.h"
#include <map>
#include <set>

namespace framevr::synchronousgpu {
namespace {
using Microsoft::WRL::ComPtr;
struct Queue {ComPtr<ID3D12CommandQueue> native;ComPtr<ID3D12Fence> fence;uint64_t value=0;};
std::map<ID3D12CommandQueue*,Queue> queues;
ComPtr<ID3D12Device> device;
ComPtr<ID3D12CommandAllocator> allocator;
ComPtr<ID3D12GraphicsCommandList> commands;
ID3D12CommandQueue* direct=nullptr;
HANDLE completion=nullptr;
Queue* GetQueue(ID3D12CommandQueue* q){
    if(const auto found=queues.find(q);found!=queues.end())return &found->second;
    if(queues.size()>=16)return nullptr;
    Queue own;own.native=q;if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&own.fence))))return nullptr;
    return &queues.emplace(q,std::move(own)).first->second;
}
bool Signal(Queue& q){
    const auto next=++q.value;
    while(FAILED(q.native->Signal(q.fence.Get(),next))){if(FAILED(device->GetDeviceRemovedReason()))return false;Sleep(1);}
    return true;
}
bool Wait(Queue& q){
    const bool event=SUCCEEDED(q.fence->SetEventOnCompletion(q.value,completion));
    for(;;){
        const auto completed=q.fence->GetCompletedValue();
        if(completed==UINT64_MAX||FAILED(device->GetDeviceRemovedReason()))return false;
        if(completed>=q.value)return true;
        // Completion is mandatory before returning borrowed recordings. Event
        // failure/timeout is never interpreted as completed GPU work.
        if(event)WaitForSingleObject(completion,100);else Sleep(1);
    }
}
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r,UINT sub,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,sub,before,after};return b;
}
bool Reset(){return SUCCEEDED(allocator->Reset())&&SUCCEEDED(commands->Reset(allocator.Get(),nullptr));}
bool Submit(bool wait=true){
    if(FAILED(commands->Close()))return false;auto* q=GetQueue(direct);if(!q)return false;
    ID3D12CommandList* list=commands.Get();direct->ExecuteCommandLists(1,&list);return Signal(*q)&&(!wait||Wait(*q));
}
}
bool Initialize(ID3D12CommandQueue* q){
    if(!q||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
    if(direct)return q==direct;
    if(FAILED(q->GetDevice(IID_PPV_ARGS(&device)))||FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||
       FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)))||FAILED(commands->Close()))return false;
    completion=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!completion)return false;
    direct=q;return GetQueue(q)!=nullptr;
}
bool Complete(){if(!direct||!device)return false;auto* q=GetQueue(direct);return q&&Signal(*q)&&Wait(*q);}
bool Drain(const nativecapture::ReplayPacket& packet){
    std::set<ID3D12CommandQueue*> used;
    for(const auto& b:packet.batches)if(used.insert(b.queue.Get()).second){auto* q=GetQueue(b.queue.Get());if(!q||!Signal(*q))return false;}
    for(const auto q:used)if(!Wait(queues.at(q)))return false;
    return true;
}
bool Execute(const nativecapture::ReplayPacket& packet){
    if(!direct||packet.batches.empty())return false;
    // Prepare every queue and list array before submitting borrowed work. A
    // later allocation failure must not escape with replay still using the
    // camera constants that Present is about to restore.
    struct Batch {Queue* queue;std::vector<ID3D12CommandList*> lists;};
    std::vector<Batch> batches;batches.reserve(packet.batches.size());
    for(const auto& batch:packet.batches){
        if(!batch.queue||batch.lists.empty())return false;
        auto* q=GetQueue(batch.queue.Get());if(!q)return false;
        Batch prepared{q,{}};prepared.lists.reserve(batch.lists.size());
        for(const auto& list:batch.lists){if(!list)return false;prepared.lists.push_back(list.Get());}
        batches.push_back(std::move(prepared));
    }
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    for(const auto& s:packet.states)if(s.first!=s.last)barriers.push_back(Transition(s.resource.Get(),s.subresource,s.last,s.first));
    Queue* previous=GetQueue(direct);
    if(!previous)return false;
    // Each native batch is ordered after the preceding one by a private GPU
    // fence. Waiting for the final batch also completes the state prelude, so
    // a separate CPU wait between prelude and replay only creates a GPU bubble.
    struct Completion {
        Queue* last=nullptr;
        ~Completion(){if(last)Wait(*last);}
        bool Finish(){const bool done=Wait(*last);last=nullptr;return done;}
    } completion;
    if(!barriers.empty()){
        if(!Reset())return false;
        commands->ResourceBarrier(UINT(barriers.size()),barriers.data());completion.last=previous;
        if(!Submit(false))return false;
    }else if(batches.front().queue!=previous){
        // There is no prelude to submit, but earlier direct-queue effects/copy
        // work must still precede replay beginning on a copy/compute queue.
        completion.last=previous;if(!Signal(*previous))return false;
    }
    for(const auto& batch:batches){auto* q=batch.queue;
        if(q!=previous&&FAILED(q->native->Wait(previous->fence.Get(),previous->value))&&!Wait(*previous))return false;
        q->native->ExecuteCommandLists(UINT(batch.lists.size()),batch.lists.data());completion.last=q;
        if(!Signal(*q))return false;previous=q;
    }return completion.Finish();
}
bool CopyDepth(ID3D12Resource* source,ID3D12Resource* destination,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
    if(source==destination)return true;
    if(!source||!destination||!Reset())return false;
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    if(from!=D3D12_RESOURCE_STATE_COPY_SOURCE)barriers.push_back(Transition(source,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,D3D12_RESOURCE_STATE_COPY_SOURCE));
    if(to!=D3D12_RESOURCE_STATE_COPY_DEST)barriers.push_back(Transition(destination,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,to,D3D12_RESOURCE_STATE_COPY_DEST));
    if(!barriers.empty())commands->ResourceBarrier(UINT(barriers.size()),barriers.data());commands->CopyResource(destination,source);
    for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
    if(!barriers.empty())commands->ResourceBarrier(UINT(barriers.size()),barriers.data());return Submit();
}
void Shutdown(){if(direct&&SUCCEEDED(device->GetDeviceRemovedReason()))Complete();queues.clear();commands.Reset();allocator.Reset();device.Reset();direct=nullptr;if(completion)CloseHandle(completion);completion=nullptr;}
}
