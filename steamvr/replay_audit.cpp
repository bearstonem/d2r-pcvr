#include "replay_audit.h"
#include "replay_audit_model.h"
#include "native_submission_capture.h"
#include "native_lock.h"
#include "logging.h"
#include <reshade.hpp>
#include <atomic>
#include <d3d12.h>
#include <cstdio>
#include <mutex>

namespace framevr::replayaudit {
namespace {
using namespace reshade::api;
constexpr unsigned PairLimit=8,ProbeLimit=32;
NativeMutex mutex;
std::atomic<bool> tracking{false},recording{false};
Commands commands;
CaptureGate gate;
Camera camera;
unsigned currentEye=0;
std::atomic<DWORD> drawThread{0};
bool eyeOpen=false,sealed=false;
EyeReport atPresent;
struct Probes {unsigned bindings=0,reads=0,unreadable=0,unknownSize=0,complete=0,partial=0,viewOnly=0;};
Probes probes;
Bindings bindings{ProbeLimit};
bool D3D12(device* dev){return dev&&dev->get_api()==device_api::d3d12;}
uint64_t Id(command_list* cl){return cl->get_native();}

void Finish(const char* reason) {
    recording.store(false);tracking.store(false);
    nativecapture::Stop();
    char line[180];std::snprintf(line,sizeof line,"Replay audit: %s; %u/%u pairs; collection stopped. No replay was performed; this is not a performance benchmark.",reason,gate.CompletedPairs(),PairLimit);
    logging::Write(line);commands=Commands{};bindings=Bindings{ProbeLimit};eyeOpen=false;
}
void InitDevice(device* dev){if(tracking.load()&&D3D12(dev))nativecapture::ObserveDevice(reinterpret_cast<ID3D12Device*>(dev->get_native()));}
void InitQueue(command_queue* q){if(tracking.load()&&D3D12(q->get_device()))nativecapture::ObserveQueue(reinterpret_cast<ID3D12CommandQueue*>(q->get_native()));}
void DestroyQueue(command_queue* q){if(tracking.load()&&D3D12(q->get_device()))nativecapture::ForgetQueue(q->get_native());}
void InitList(command_list* cl){
    if(!tracking.load()||!D3D12(cl->get_device()))return;
    nativecapture::ObserveList(reinterpret_cast<ID3D12GraphicsCommandList*>(Id(cl)));
    std::lock_guard lock(mutex);if(tracking.load()){commands.Reset(Id(cl));bindings.ResetList(Id(cl));}
}
void DestroyList(command_list* cl){
    if(!tracking.load()||!D3D12(cl->get_device()))return;
    nativecapture::ForgetList(Id(cl));
    std::lock_guard lock(mutex);if(tracking.load()){commands.Destroy(Id(cl));bindings.ResetList(Id(cl));}
}
void DestroyResource(device* dev,resource r){if(!tracking.load()||!D3D12(dev))return;std::lock_guard lock(mutex);if(tracking.load()){commands.RetireResource();bindings.DestroyResource(r.handle);}}
void DestroyDevice(device* dev){if(!tracking.load()||!D3D12(dev))return;std::lock_guard lock(mutex);if(tracking.load())Finish("device destroyed, incomplete");}
void OnBarrier(command_list* cl,uint32_t count,const resource* resources,const resource_usage* before,const resource_usage* after) {
    if(!tracking.load()||!D3D12(cl->get_device()))return;
    std::lock_guard lock(mutex);if(!tracking.load())return;
    for(uint32_t i=0;i<count;++i)commands.Barrier(Id(cl),resources[i].handle,uint32_t(before[i]),uint32_t(after[i]));
}
void Probe(device* dev,const Binding& binding);
void Execute(command_queue* queue,command_list* cl) {
    // Also discover a queue that predates add-on registration during settling.
    InitQueue(queue);
    if(!recording.load()||!D3D12(queue->get_device()))return;
    const auto type=queue->get_type();
    const unsigned kind=(type&command_queue_type::graphics)!=0?0:(type&command_queue_type::compute)!=0?1:(type&command_queue_type::copy)!=0?2:3;
    std::lock_guard lock(mutex);if(!recording.load())return;
    commands.Submit(queue->get_native(),kind,Id(cl));
    // Upload constants and the engine camera can still change after root binding.
    // Read just before the ORIGINAL submission, while the game's resources must
    // be valid. Destroy/reset callbacks discard stale pending ranges.
    for(const auto& binding:bindings.Submit(Id(cl)))Probe(queue->get_device(),binding);
}
void Present(command_queue* queue,swapchain*,const rect*,const rect*,uint32_t,const rect*) {
    if(!recording.load()||GetCurrentThreadId()!=drawThread||!D3D12(queue->get_device()))return;
    std::lock_guard lock(mutex);if(!recording.load())return;
    // Stop at the engine's Present boundary, before the eye's effects/copy work.
    nativecapture::SealPresent();
    recording.store(false);atPresent=commands.EndEye();sealed=true;
}
void Push(command_list* cl,shader_stage,pipeline_layout,uint32_t,const descriptor_table_update& update) {
    if(!recording.load()||update.type!=descriptor_type::constant_buffer||!D3D12(cl->get_device()))return;
    std::lock_guard lock(mutex);if(!recording.load())return;
    auto* ranges=static_cast<const buffer_range*>(update.descriptors);
    if(!ranges)return;
    for(uint32_t i=0;i<update.count;++i) {
        ++probes.bindings;const auto& range=ranges[i];
        bindings.Add({Id(cl),range.buffer.handle,range.offset,range.size});
    }
}
void Probe(device* dev,const Binding& binding) {
        const resource buffer{binding.resource};
        if(!buffer.handle){++probes.unreadable;return;}
        const auto desc=dev->get_resource_desc(buffer);
        if(desc.type!=resource_type::buffer||desc.heap!=memory_heap::upload){++probes.unreadable;return;}
        const auto extent=ReadableRange(desc.buffer.size,0,desc.buffer.size,binding.offset,binding.size);
        if(extent.bytes<64){++probes.unreadable;return;}
        if(binding.size==UINT64_MAX)++probes.unknownSize;
        void* data=nullptr;
        if(!dev->map_buffer_region(buffer,binding.offset,extent.bytes,map_access::read_only,&data)){++probes.unreadable;return;}
        std::array<float,1024> words{};SIZE_T copied=0;
        const bool read=data&&ReadProcessMemory(GetCurrentProcess(),data,words.data(),extent.bytes,&copied)&&copied==extent.bytes;
        dev->unmap_buffer_region(buffer); // Balanced temporary read-only mapping.
        if(!read){++probes.unreadable;return;}
        ++probes.reads;
        const auto layout=InspectCamera(std::span(words.data(),extent.bytes/4),camera);
        probes.complete+=layout.completeRecords;probes.partial+=layout.partialRecords;probes.viewOnly+=layout.viewOnly;
}
}

void Register(bool enabled) {
    if(!enabled||tracking.exchange(true))return;
    if(!nativecapture::Start())logging::Write("Replay audit: native capture initialization failed; dependency metadata will be incomplete.");
    reshade::register_event<reshade::addon_event::init_device>(&InitDevice);
    reshade::register_event<reshade::addon_event::init_command_queue>(&InitQueue);
    reshade::register_event<reshade::addon_event::destroy_command_queue>(&DestroyQueue);
    reshade::register_event<reshade::addon_event::init_command_list>(&InitList);
    reshade::register_event<reshade::addon_event::reset_command_list>(&InitList);
    reshade::register_event<reshade::addon_event::destroy_command_list>(&DestroyList);
    reshade::register_event<reshade::addon_event::destroy_resource>(&DestroyResource);
    reshade::register_event<reshade::addon_event::destroy_device>(&DestroyDevice);
    reshade::register_event<reshade::addon_event::barrier>(&OnBarrier);
    reshade::register_event<reshade::addon_event::execute_command_list>(&Execute);
    reshade::register_event<reshade::addon_event::present>(&Present);
    reshade::register_event<reshade::addon_event::push_descriptors>(&Push);
    logging::Write("Replay audit: armed for 8 gameplay pairs after 90 active XR pairs; metadata only. Diagnostic callbacks and upload-buffer reads add overhead; restart with replay_audit=0 before benchmarking.");
}
void BeginPair(bool xrActive) {
    if(!tracking.load())return;
    std::lock_guard lock(mutex);if(!tracking.load())return;
    const auto step=gate.BeginPair(xrActive);
    if(step==CaptureGate::Step::Incomplete)Finish("XR inactive, incomplete");
    if(step==CaptureGate::Step::Started)logging::Write("Replay audit: starting 8 gameplay pairs; both eyes still use normal engine rendering.");
}
void BeginEye(unsigned eye) {
    if(!tracking.load())return;
    std::lock_guard lock(mutex);if(!tracking.load()||!gate.PairOpen())return;
    camera={};probes={};bindings=Bindings{ProbeLimit};currentEye=eye&1;drawThread=GetCurrentThreadId();
    nativecapture::BeginEye(drawThread.load());
    atPresent={};sealed=false;eyeOpen=true;commands.BeginEye();recording.store(true);
}
void View(unsigned eye,const float matrix[16]) {
    if(!recording.load())return;
    std::lock_guard lock(mutex);if(!recording.load()||(eye&1)!=currentEye)return;
    camera.View(matrix);
}
void Projection(unsigned eye,const float matrix[16]) {
    if(!recording.load())return;
    std::lock_guard lock(mutex);if(!recording.load()||(eye&1)!=currentEye)return;
    camera.Projection(matrix);
}
void EndEye() {
    if(!tracking.load())return;
    std::lock_guard lock(mutex);if(!tracking.load()||!eyeOpen)return;
    recording.store(false);const auto end=commands.EndEye();eyeOpen=false;gate.EyeDone(currentEye);
    char line[560];std::snprintf(line,sizeof line,
        "Replay audit commands: pair=%u eye=%u present=%u lists=%u queues=%u direct=%u compute=%u copy=%u unknown=%u invalid_at_present=%u invalid_at_draw_end=%u resets=%u state_restorations=%u state_chain_mismatches=%u retired_resources=%u unknown_barriers=%u metadata_missing=%u overflow=%u",
        gate.CompletedPairs()+1,currentEye,unsigned(sealed),end.submissions,end.queues,end.queueKinds[0],end.queueKinds[1],end.queueKinds[2],end.queueKinds[3],
        sealed?atPresent.invalidatedLists:end.invalidatedLists,end.invalidatedLists,end.resets,end.stateRestorations,end.stateChainMismatches,end.resourceRetirements,end.unknownBarriers,end.metadataMissing,end.overflows);
    logging::Write(line);
    const auto native=nativecapture::EndEye();
    std::snprintf(line,sizeof line,
        "Replay audit native: pair=%u eye=%u batches=%u lists=%u signals=%u waits=%u cpu_signals=%u unknown_lists=%u queue_overlap=%u boundary_calls=%u foreign_calls=%u api_failures=%u hook_errors=%u observer_faults=%u capacity_failures=%u new_fences=%u",
        gate.CompletedPairs()+1,currentEye,native.batches,native.lists,native.signals,native.waits,native.cpuSignals,native.unknownLists,
        native.queueOverlap,native.boundaryCalls,native.foreignCalls,native.apiFailures,native.hookErrors,native.observerFaults,native.capacityFailures,native.newFences);
    logging::Write(line);
    std::snprintf(line,sizeof line,
        "Replay audit dependencies: pair=%u eye=%u sealed=%u complete=%u plan_present=%s plan_end=%s internal_waits=%u external_waits=%u list_resets=%u allocator_resets=%u retained_allocators=%u retained_fences=%u",
        gate.CompletedPairs()+1,currentEye,unsigned(native.sealed),unsigned(native.DependenciesComplete()),nativecapture::StatusName(native.atPresent),nativecapture::StatusName(native.atEnd),
        native.internalWaits,native.externalWaits,native.listResets,native.allocatorResets,native.retainedAllocators,native.retainedFences);
    logging::Write(line);
    std::snprintf(line,sizeof line,
        "Replay audit camera: pair=%u eye=%u camera_valid=%u view_updates=%u projection_updates=%u cbv_bindings=%u cbv_reads=%u cbv_unreadable=%u cbv_unknown_size=%u camera_complete=%u camera_partial=%u view_only=%u probe_overflow=%u probe_pending=%u probe_discarded=%u",
        gate.CompletedPairs()+1,currentEye,
        unsigned(camera.haveView&&camera.haveProjection),camera.viewUpdates,camera.projectionUpdates,probes.bindings,probes.reads,probes.unreadable,probes.unknownSize,probes.complete,probes.partial,probes.viewOnly,bindings.Overflow(),unsigned(bindings.Pending()),bindings.Discarded());
    logging::Write(line);
}
void EndPair(){
    if(!tracking.load())return;std::lock_guard lock(mutex);if(!tracking.load())return;
    const auto step=gate.EndPair();if(step==CaptureGate::Step::Complete)Finish("complete");
    else if(step==CaptureGate::Step::Incomplete)Finish("missing eye, incomplete");
}
void SkipPair(){if(!tracking.load())return;std::lock_guard lock(mutex);if(tracking.load()&&gate.SkipPair())Finish("left gameplay, incomplete");}
void Shutdown(){
    recording.store(false);tracking.store(false);
    if(!nativecapture::Shutdown())logging::Write("Replay audit: native detour shutdown incomplete; diagnostic module retained until process exit.");
    std::lock_guard lock(mutex);commands=Commands{};gate=CaptureGate{};bindings=Bindings{ProbeLimit};eyeOpen=false;
}
}
