#include "replay_audit.h"
#include "replay_audit_model.h"
#include "native_lock.h"
#include "logging.h"
#include <reshade.hpp>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <set>

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
struct Probes {unsigned bindings=0,reads=0,unreadable=0,unknownSize=0,complete=0,partial=0,viewOnly=0,overflows=0;};
Probes probes;
std::set<std::pair<uint64_t,uint64_t>> seen;
bool D3D12(device* dev){return dev&&dev->get_api()==device_api::d3d12;}
uint64_t Id(command_list* cl){return cl->get_native();}

void Finish(const char* reason) {
    recording.store(false);tracking.store(false);
    char line[180];std::snprintf(line,sizeof line,"Replay audit: %s; %u/%u pairs; collection stopped. No replay was performed; this is not a performance benchmark.",reason,gate.CompletedPairs(),PairLimit);
    logging::Write(line);commands=Commands{};seen.clear();eyeOpen=false;
}
void InitList(command_list* cl){if(!tracking.load()||!D3D12(cl->get_device()))return;std::lock_guard lock(mutex);if(tracking.load())commands.Reset(Id(cl));}
void DestroyList(command_list* cl){if(!tracking.load()||!D3D12(cl->get_device()))return;std::lock_guard lock(mutex);if(tracking.load())commands.Destroy(Id(cl));}
void DestroyResource(device* dev,resource){if(!tracking.load()||!D3D12(dev))return;std::lock_guard lock(mutex);if(tracking.load())commands.RetireResource();}
void DestroyDevice(device* dev){if(!tracking.load()||!D3D12(dev))return;std::lock_guard lock(mutex);if(tracking.load())Finish("device destroyed, incomplete");}
void OnBarrier(command_list* cl,uint32_t count,const resource* resources,const resource_usage* before,const resource_usage* after) {
    if(!tracking.load()||!D3D12(cl->get_device()))return;
    std::lock_guard lock(mutex);if(!tracking.load())return;
    for(uint32_t i=0;i<count;++i)commands.Barrier(Id(cl),resources[i].handle,uint32_t(before[i]),uint32_t(after[i]));
}
void Execute(command_queue* queue,command_list* cl) {
    if(!recording.load()||!D3D12(queue->get_device()))return;
    const auto type=queue->get_type();
    const unsigned kind=(type&command_queue_type::graphics)!=0?0:(type&command_queue_type::compute)!=0?1:(type&command_queue_type::copy)!=0?2:3;
    std::lock_guard lock(mutex);if(recording.load())commands.Submit(queue->get_native(),kind,Id(cl));
}
void Present(command_queue* queue,swapchain*,const rect*,const rect*,uint32_t,const rect*) {
    if(!recording.load()||GetCurrentThreadId()!=drawThread||!D3D12(queue->get_device()))return;
    std::lock_guard lock(mutex);if(!recording.load())return;
    // Stop at the engine's Present boundary, before the eye's effects/copy work.
    recording.store(false);atPresent=commands.EndEye();sealed=true;
}
void Push(command_list* cl,shader_stage,pipeline_layout,uint32_t,const descriptor_table_update& update) {
    if(!recording.load()||update.type!=descriptor_type::constant_buffer||!D3D12(cl->get_device()))return;
    std::lock_guard lock(mutex);if(!recording.load())return;
    auto* ranges=static_cast<const buffer_range*>(update.descriptors);
    if(!ranges)return;
    device* dev=cl->get_device();
    for(uint32_t i=0;i<update.count;++i) {
        ++probes.bindings;const auto& range=ranges[i];
        const auto key=std::pair{range.buffer.handle,range.offset};
        if(seen.contains(key))continue;
        if(seen.size()>=ProbeLimit){++probes.overflows;continue;}
        seen.insert(key);
        if(!range.buffer.handle){++probes.unreadable;continue;}
        const auto desc=dev->get_resource_desc(range.buffer);
        if(desc.type!=resource_type::buffer||desc.heap!=memory_heap::upload){++probes.unreadable;continue;}
        const auto extent=ReadableRange(desc.buffer.size,0,desc.buffer.size,range.offset,range.size);
        if(extent.bytes<64){++probes.unreadable;continue;}
        if(range.size==UINT64_MAX)++probes.unknownSize;
        void* data=nullptr;
        if(!dev->map_buffer_region(range.buffer,range.offset,extent.bytes,map_access::read_only,&data)){++probes.unreadable;continue;}
        std::array<float,1024> words{};SIZE_T copied=0;
        const bool read=data&&ReadProcessMemory(GetCurrentProcess(),data,words.data(),extent.bytes,&copied)&&copied==extent.bytes;
        dev->unmap_buffer_region(range.buffer); // Balanced temporary read-only mapping.
        if(!read){++probes.unreadable;continue;}
        ++probes.reads;
        const auto layout=InspectCamera(std::span(words.data(),extent.bytes/4),camera);
        probes.complete+=layout.completeRecords;probes.partial+=layout.partialRecords;probes.viewOnly+=layout.viewOnly;
    }
}
}

void Register(bool enabled) {
    if(!enabled||tracking.exchange(true))return;
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
    camera={};probes={};seen.clear();currentEye=eye&1;drawThread=GetCurrentThreadId();
    atPresent={};sealed=false;eyeOpen=true;commands.BeginEye();recording.store(true);
}
void View(unsigned eye,const float matrix[16]) {
    if(!recording.load())return;
    std::lock_guard lock(mutex);if(!recording.load()||(eye&1)!=currentEye||camera.haveView)return;
    for(unsigned i=0;i<16;++i)camera.view[i]=matrix[i];camera.haveView=true;
}
void Projection(unsigned eye,const float matrix[16]) {
    if(!recording.load())return;
    std::lock_guard lock(mutex);if(!recording.load()||(eye&1)!=currentEye||camera.haveProjection)return;
    for(unsigned i=0;i<16;++i)camera.projection[i]=matrix[i];camera.haveProjection=true;
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
    std::snprintf(line,sizeof line,
        "Replay audit camera: pair=%u eye=%u camera_valid=%u cbv_bindings=%u cbv_reads=%u cbv_unreadable=%u cbv_unknown_size=%u camera_complete=%u camera_partial=%u view_only=%u probe_overflow=%u",
        gate.CompletedPairs()+1,currentEye,
        unsigned(camera.haveView&&camera.haveProjection),probes.bindings,probes.reads,probes.unreadable,probes.unknownSize,probes.complete,probes.partial,probes.viewOnly,probes.overflows);
    logging::Write(line);
}
void EndPair(){
    if(!tracking.load())return;std::lock_guard lock(mutex);if(!tracking.load())return;
    const auto step=gate.EndPair();if(step==CaptureGate::Step::Complete)Finish("complete");
    else if(step==CaptureGate::Step::Incomplete)Finish("missing eye, incomplete");
}
void SkipPair(){if(!tracking.load())return;std::lock_guard lock(mutex);if(tracking.load()&&gate.SkipPair())Finish("left gameplay, incomplete");}
void Shutdown(){recording.store(false);tracking.store(false);std::lock_guard lock(mutex);commands=Commands{};gate=CaptureGate{};seen.clear();eyeOpen=false;}
}
