#include "stereo_replay.h"
#include "stereo_replay_camera.h"
#include "native_submission_capture.h"
#include "native_replay_packet.h"
#include "synchronous_replay_gpu.h"
#include "native_lock.h"
#include "transport.h"
#include "logging.h"
#include <reshade.hpp>
#include <atomic>
#include <array>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

namespace framevr::stereoreplay {
namespace {
using namespace reshade::api;
using Microsoft::WRL::ComPtr;
// Manual eye/effect setup reenters our camera and resource callbacks.
NativeRecursiveMutex mutex;
NativeRecursiveMutex uploadMutex;
std::atomic<bool> requested{false},collecting{false};
std::atomic<bool> running{false};
std::atomic<unsigned> configuration{0};
std::atomic<const char*> status{"Standard"};
bool registered=false,done=false,invalid=false,temporal=false,inReplay=false;
bool failed=false,layoutResolved=false;
unsigned warmup=0,appliedConfiguration=0,lookupCount=0,bindingCount=0;
uint32_t owner=0,pairs=0,replays=0,fallbacks=0;
EyeFn eyeFn=nullptr;EffectsFn effectsFn=nullptr;
effect_runtime* runtime=nullptr;
replayaudit::Camera cameras[2];
using CameraKey=std::pair<uint64_t,uint32_t>;
struct Binding {resource buffer;uint64_t offset,size;CameraKey cameraKey;};
struct Probe {ComPtr<ID3D12Resource> buffer;uint64_t offset;std::vector<float> original,right;std::vector<size_t> records;replayaudit::Camera left;};
std::map<uint64_t,std::map<std::pair<uint64_t,uint64_t>,Binding>> bindings;
std::map<CameraKey,std::vector<size_t>> cameraKeys;
std::atomic<bool> cameraFilterReady{false};
std::atomic<unsigned> cameraFilterCount{0};
std::array<std::atomic<uint64_t>,64> cameraFilterSignatures{};
std::array<std::atomic<uint32_t>,64> cameraFilterParameters{};
std::atomic<uint64_t> bindingEpoch{0};
struct RecentBinding {uint64_t epoch=0,address=0;};
thread_local std::array<RecentBinding,64> recentBindings;
std::set<std::pair<uint64_t,uint64_t>> seenBindings;
std::map<std::pair<uint64_t,uint64_t>,Probe> probes;
// Resource creation/destruction only, never per-draw work in Standard. Borrow
// handles until an active pair retains its camera ranges; do not prolong game
// upload allocations while the experimental mode is off.
struct Upload {resource buffer;uint64_t size;};
std::map<uint64_t,Upload> uploads;
std::map<uint64_t,uint64_t> uploadAddresses;
struct Depth {ComPtr<ID3D12Resource> resource;unsigned binds=0;};
std::map<uint64_t,Depth> depths;
resource_view rtv{},rtvSrgb{};resource target{};reshade::api::device* apiDevice=nullptr;
void Fallback(const char* reason){
    ++fallbacks;failed=true;status="Unavailable";
    char line[512];std::snprintf(line,sizeof line,"Stereo replay: stopped after pair %u: %s; Standard rendering restored; select Standard then Single pass to retry",pairs,reason);logging::Write(line);
}
bool D3D12(reshade::api::device* d){return d&&d->get_api()==device_api::d3d12;}
template<class T> uint64_t Id(T* p){return reinterpret_cast<uint64_t>(p);}
void InitDevice(reshade::api::device* d){if(!D3D12(d))return;apiDevice=d;if(running.load())nativecapture::ObserveDevice(reinterpret_cast<ID3D12Device*>(d->get_native()));}
void InitQueue(command_queue* q){if(running.load()&&D3D12(q->get_device()))nativecapture::ObserveQueue(reinterpret_cast<ID3D12CommandQueue*>(q->get_native()));}
void InitList(command_list* cl){if(running.load()&&D3D12(cl->get_device()))nativecapture::ObserveList(reinterpret_cast<ID3D12GraphicsCommandList*>(cl->get_native()));}
void DestroyList(command_list* cl){if(!running.load()||!D3D12(cl->get_device()))return;nativecapture::ForgetList(cl->get_native());std::lock_guard lock(mutex);bindings.erase(cl->get_native());}
void ResetList(command_list* cl){if(!running.load()||!D3D12(cl->get_device()))return;InitList(cl);std::lock_guard lock(mutex);bindings.erase(cl->get_native());}
void InitResource(reshade::api::device* d,const resource_desc& desc,const subresource_data*,resource_usage state,resource r){
    if(!D3D12(d))return;
    if(desc.type==resource_type::buffer&&desc.heap==memory_heap::upload){
        const auto address=reinterpret_cast<ID3D12Resource*>(r.handle)->GetGPUVirtualAddress();
        std::lock_guard lock(uploadMutex);if(address&&uploads.size()<8192){uploads[address]={r,desc.buffer.size};uploadAddresses[r.handle]=address;}
    }
    if(!running.load())return;
    uint32_t native=uint32_t(state);
    if(state==resource_usage::general||state==resource_usage::present)native=D3D12_RESOURCE_STATE_COMMON;
    else if(native&uint32_t(resource_usage::constant_buffer))native=(native&~uint32_t(resource_usage::constant_buffer))|D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    nativecapture::InitialResourceState(reinterpret_cast<ID3D12Resource*>(r.handle),D3D12_RESOURCE_STATES(native));
}
void DestroyResource(reshade::api::device* d,resource r){
    if(!D3D12(d))return;
    {std::lock_guard lock(uploadMutex);const auto found=uploadAddresses.find(r.handle);if(found!=uploadAddresses.end()){uploads.erase(found->second);uploadAddresses.erase(found);}}
    if(!running.load())return;
    nativecapture::ForgetResourceState(reinterpret_cast<ID3D12Resource*>(r.handle));if(collecting.load()){std::lock_guard lock(mutex);invalid=true;}
}
void Runtime(effect_runtime* rt){std::lock_guard lock(mutex);runtime=rt;}
void DestroyRuntime(effect_runtime* rt){std::lock_guard lock(mutex);if(runtime==rt){runtime=nullptr;warmup=0;}}
void Push(uint64_t listId,uint64_t signature,uint32_t parameter,uint64_t address){
    if(!collecting.load())return;
    RecentBinding* recent=nullptr;uint64_t epoch=0;
    // Immutable roles are published once per manual activation. Most of the
    // tens of thousands of material/object CBVs never take a callback lock.
    if(cameraFilterReady.load(std::memory_order_acquire)){
        bool camera=false;const auto count=cameraFilterCount.load(std::memory_order_relaxed);
        for(unsigned i=0;i<count;++i)if(cameraFilterSignatures[i].load(std::memory_order_relaxed)==signature&&cameraFilterParameters[i].load(std::memory_order_relaxed)==parameter){camera=true;recent=&recentBindings[i];break;}
        if(!camera)return;
        epoch=bindingEpoch.load(std::memory_order_relaxed);
        if(recent->epoch==epoch&&recent->address==address)return;
    }
    std::lock_guard lock(mutex);const CameraKey cameraKey{signature,parameter};
    if(layoutResolved&&!cameraKeys.contains(cameraKey))return;
    buffer_range b{};
    {std::lock_guard uploadLock(uploadMutex);auto found=uploads.upper_bound(address);if(found==uploads.begin())return;--found;
        if(address-found->first>=found->second.size)return;
        b={found->second.buffer,address-found->first,found->second.size-(address-found->first)};
    }
    // Discovery is bounded once on enable. Established camera roots retain
    // only a bounded set of unique ranges, rather than all draw CBVs.
    const auto key=std::pair{b.buffer.handle,b.offset};
    if(seenBindings.contains(key)){if(recent)*recent={epoch,address};return;}
    if(bindingCount>=(layoutResolved?512u:64u)){if(layoutResolved)invalid=true;return;}
    seenBindings.insert(key);++bindingCount;
    bindings[listId].try_emplace(key,Binding{b.buffer,b.offset,b.size,cameraKey});
    if(recent)*recent={epoch,address};
}
void Execute(command_queue* q,command_list* cl){
    if(!running.load())return;InitQueue(q);InitList(cl);if(!collecting.load()||!D3D12(q->get_device()))return;
    std::lock_guard lock(mutex);const auto list=bindings.find(cl->get_native());if(list==bindings.end())return;
    for(const auto& [bindingKey,b]:list->second){
        if(!b.buffer.handle)continue;
        const auto known=cameraKeys.find(b.cameraKey);
        if(layoutResolved&&known==cameraKeys.end())continue;
        if(known==cameraKeys.end()&&lookupCount++>=64)continue;
        const auto desc=q->get_device()->get_resource_desc(b.buffer);
        if(desc.type!=resource_type::buffer||desc.heap!=memory_heap::upload||b.offset>=desc.buffer.size)continue;
        const auto bytes=std::min({uint64_t(4096),desc.buffer.size-b.offset,b.size})&~uint64_t(3);
        if(bytes<448)continue;
        auto* buffer=reinterpret_cast<ID3D12Resource*>(b.buffer.handle);
        void* data=nullptr;D3D12_RANGE read{SIZE_T(b.offset),SIZE_T(b.offset+bytes)};
        if(FAILED(buffer->Map(0,&read,&data))||!data){invalid=true;continue;}
        // Upload mappings may be write-combined. Copy once into cached CPU
        // memory before matrix inspection instead of repeatedly reading the
        // uncached mapping for each element and inverse calculation.
        std::vector<float> words(size_t(bytes/4));std::memcpy(words.data(),static_cast<const uint8_t*>(data)+b.offset,SIZE_T(bytes));
        D3D12_RANGE none{};buffer->Unmap(0,&none);const auto wordsIn=std::span<const float>(words);
        std::vector<size_t> records=known==cameraKeys.end()?FindCameraRecords(wordsIn,cameras[0]):SelectCameraRecords(wordsIn,known->second,cameras[0]);
        // A root may bind overlapping windows or a smaller record group. Use
        // cached offsets only where that actual submitted range contains the
        // complete camera record; never borrow another range's layout blindly.
        if(records.empty())continue;
        if(!layoutResolved)cameraKeys.try_emplace(b.cameraKey,records);
        const auto key=std::pair{b.buffer.handle,b.offset};const auto old=probes.find(key);
        if(old!=probes.end()){
            for(const size_t o:records)if(o+112>old->second.original.size()||std::memcmp(old->second.original.data()+o,words.data()+o,112*4))invalid=true;
        }else if(probes.size()<64)probes.emplace(key,Probe{buffer,b.offset,std::move(words),{},std::move(records),cameras[0]});
        else invalid=true;
    }
}
void BindDepth(command_list* cl,uint32_t,const resource_view*,resource_view dsv){
    if(!collecting.load()||!dsv.handle||!D3D12(cl->get_device()))return;
    std::lock_guard lock(mutex);const auto r=cl->get_device()->get_resource_from_view(dsv);if(!r.handle)return;
    auto& d=depths[r.handle];d.resource=reinterpret_cast<ID3D12Resource*>(r.handle);++d.binds;
}
bool Setup(command_queue* q,swapchain* swap){
    if(!synchronousgpu::Initialize(reinterpret_cast<ID3D12CommandQueue*>(q->get_native())))return false;
    const auto back=swap->get_current_back_buffer();
    if(target!=back){
        if(rtv.handle&&apiDevice)apiDevice->destroy_resource_view(rtv);
        if(rtvSrgb.handle&&apiDevice)apiDevice->destroy_resource_view(rtvSrgb);
        rtv=rtvSrgb={};target={};apiDevice=q->get_device();
        const auto desc=apiDevice->get_resource_desc(back);
        if(!apiDevice->create_resource_view(back,resource_usage::render_target,resource_view_desc(format_to_default_typed(desc.texture.format,0)),&rtv)||
           !apiDevice->create_resource_view(back,resource_usage::render_target,resource_view_desc(format_to_default_typed(desc.texture.format,1)),&rtvSrgb))return false;
        target=back;
    }return true;
}
bool WriteProbes(bool right){
    bool ok=true;
    for(auto& [key,p]:probes){
        void* mapped=nullptr;D3D12_RANGE none{};
        if(FAILED(p.buffer->Map(0,&none,&mapped))||!mapped){ok=false;continue;}
        // Only the five camera matrices following each retained view pair are
        // writable. No whole-buffer scan or process-memory syscall is needed
        // for a successfully mapped D3D12 upload resource.
        const auto& values=right?p.right:p.original;
        for(const size_t o:p.records)std::memcpy(static_cast<uint8_t*>(mapped)+p.offset+(o+32)*4,values.data()+o+32,80*4);
        D3D12_RANGE written{SIZE_T(p.offset),SIZE_T(p.offset+p.original.size()*4)};p.buffer->Unmap(0,&written);
    }return ok;
}
bool CameraUnchanged(){
    for(const auto& [key,p]:probes){
        void* data=nullptr;D3D12_RANGE read{SIZE_T(p.offset),SIZE_T(p.offset+p.original.size()*4)};
        if(FAILED(p.buffer->Map(0,&read,&data))||!data)return false;
        const auto* values=reinterpret_cast<const float*>(static_cast<const uint8_t*>(data)+p.offset);bool same=true;
        for(const size_t o:p.records)if(std::memcmp(values+o,p.original.data()+o,112*4)){same=false;break;}
        D3D12_RANGE none{};p.buffer->Unmap(0,&none);if(!same)return false;
    }return true;
}
void Effects(command_queue* q,bool right){
    auto* cl=q->get_immediate_command_list();
    cl->barrier(target,resource_usage::present,resource_usage::render_target);
    if(right){effectsFn(runtime,true);const auto tech=runtime->find_technique("D2R_DepthFog.fx","D2R_DepthFog_R");runtime->render_technique(tech,cl,rtv,rtvSrgb);effectsFn(runtime,false);}
    else runtime->render_effects(cl,rtv,rtvSrgb);
    cl->barrier(target,resource_usage::render_target,resource_usage::present);q->flush_immediate_command_list();
}
bool UniformDepthState(ID3D12Resource* resource,D3D12_RESOURCE_STATES& state){
    if(!nativecapture::ResourceState(resource,0,state))return false;
    ComPtr<ID3D12Device> owner;if(FAILED(resource->GetDevice(IID_PPV_ARGS(&owner))))return false;
    D3D12_FEATURE_DATA_FORMAT_INFO info{resource->GetDesc().Format,0};
    if(FAILED(owner->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,&info,sizeof info))||!info.PlaneCount)return false;
    for(UINT plane=1;plane<info.PlaneCount;++plane){D3D12_RESOURCE_STATES other;if(!nativecapture::ResourceState(resource,plane,other)||other!=state)return false;}
    return true;
}
void Present(command_queue* q,swapchain* swap,const rect*,const rect*,uint32_t,const rect*){
    if(!collecting.exchange(false)||GetCurrentThreadId()!=owner||!D3D12(q->get_device()))return;
    // Drain foreign native calls before taking the callback mutex: a worker
    // already inside a queue call may itself be finishing a camera callback.
    struct Lease {bool held=nativecapture::BeginReplayLease();~Lease(){if(held)nativecapture::EndReplayLease();}} lease;
    std::lock_guard lock(mutex);nativecapture::SealPresent(true);nativecapture::ReplayPacket packet;
    layoutResolved=!cameraKeys.empty();
    if(layoutResolved&&!cameraFilterReady.load()){
        unsigned count=0;for(const auto& [key,offsets]:cameraKeys){if(count==cameraFilterSignatures.size())break;cameraFilterSignatures[count]=key.first;cameraFilterParameters[count++]=key.second;}
        cameraFilterCount=count;cameraFilterReady.store(true,std::memory_order_release);
    }
    if(!lease.held||invalid||temporal||!runtime||probes.empty()||!nativecapture::SnapshotReplay(packet)){
        Fallback(!lease.held?"GPU lifetime lease unavailable":invalid?"mutable constants/resources":temporal?"temporal upscaling":probes.empty()?"camera buffers missing":!runtime?"effects runtime missing":nativecapture::ReplayPacketFailure());
        const auto r=nativecapture::EndEye(true);char detail[400];
        std::snprintf(detail,sizeof detail,"Stereo replay stop details: camera_ranges=%u bindings=%u lookup_reads=%u batches=%u lists=%u plan=%s unknown=%u overlap=%u foreign=%u boundary=%u api=%u cpu_signals=%u hooks=%u faults=%u capacity=%u",unsigned(probes.size()),bindingCount,lookupCount,r.batches,r.lists,nativecapture::StatusName(r.atPresent),r.unknownLists,r.queueOverlap,r.foreignCalls,r.boundaryCalls,r.apiFailures,r.cpuSignals,r.hookErrors,r.observerFaults,r.capacityFailures);logging::Write(detail);return;
    }
    nativecapture::EndEye(true);
    const auto rightTech=runtime->find_technique("D2R_DepthFog.fx","D2R_DepthFog_R");
    if(!rightTech.handle||!runtime->get_effects_state()||!Setup(q,swap)){Fallback("effects/setup unavailable");return;}
    const auto texture=runtime->find_texture_variable("D2R_DepthFog.fx","FogDepthTex");resource_view depthView{},depthViewSrgb{};
    if(texture.handle)runtime->get_texture_binding(texture,&depthView,&depthViewSrgb);
    if(!depthView.handle){Fallback("depth binding missing");return;}
    const auto dest=q->get_device()->get_resource_from_view(depthView);
    ComPtr<ID3D12Resource> depthDestination=reinterpret_cast<ID3D12Resource*>(dest.handle),depthSource;
    unsigned most=0;
    if(depthDestination){const auto d=depthDestination->GetDesc();for(const auto& [id,candidate]:depths){const auto s=candidate.resource->GetDesc();
        if(candidate.binds>most&&s.Width==d.Width&&s.Height==d.Height&&s.MipLevels==1&&s.DepthOrArraySize==1&&d.MipLevels==1&&d.DepthOrArraySize==1&&s.SampleDesc.Count==d.SampleDesc.Count&&format_to_typeless(format(s.Format))==format_to_typeless(format(d.Format))){depthSource=candidate.resource;most=candidate.binds;}
    }}
    D3D12_RESOURCE_STATES sourceState,destState;
    if(!depthSource||!depthDestination||!UniformDepthState(depthSource.Get(),sourceState)||!UniformDepthState(depthDestination.Get(),destState)){Fallback("depth state incomplete");return;}
    if(!eyeFn(1)){Fallback("right XR camera unavailable");return;}
    if(!cameras[1].haveView||!cameras[1].haveProjection){eyeFn(0);Fallback("right camera update incomplete");return;}
    unsigned records=0;for(auto& [key,p]:probes){p.right=p.original;const auto n=FoldCameraAt(p.right,p.records,p.left,cameras[1]);if(!n){eyeFn(0);Fallback("captured camera layout changed");return;}records+=n;}
    eyeFn(0);
    if(!synchronousgpu::Drain(packet)){Fallback("original GPU completion failed");return;}
    if(!CameraUnchanged()){Fallback("camera constants changed before replay");return;}
    inReplay=true;Effects(q,false);
    if(!synchronousgpu::Complete()||!framevr::CaptureReplay(q,swap,0)||!synchronousgpu::Complete()){inReplay=false;Fallback("left copy failed");return;}
    const bool written=WriteProbes(true);
    const bool rendered=written&&synchronousgpu::Execute(packet);
    // Borrowed constants/recordings never escape this synchronous completion.
    const bool restored=WriteProbes(false);
    if(!rendered||!restored){inReplay=false;Fallback("GPU replay/restore failed");return;}
    eyeFn(1);
    if(!UniformDepthState(depthSource.Get(),sourceState)||!UniformDepthState(depthDestination.Get(),destState)||!synchronousgpu::CopyDepth(depthSource.Get(),depthDestination.Get(),sourceState,destState)){inReplay=false;Fallback("right depth copy failed");return;}
    Effects(q,true);
    if(!synchronousgpu::Complete()||!framevr::CaptureReplay(q,swap,1)||!synchronousgpu::Complete()){inReplay=false;Fallback("right copy failed");return;}
    framevr::FinishReplayCapture();done=true;inReplay=false;++replays;status="Single pass";
    if(replays==1){char line[240];unsigned restores=0;for(const auto& state:packet.states)restores+=state.first!=state.last;
        std::snprintf(line,sizeof line,"Stereo replay: first actual pair; %u batches, %u camera ranges, %u records, %u texture boundary restorations; second engine draw skipped",unsigned(packet.batches.size()),unsigned(probes.size()),records,restores);logging::Write(line);}
}
}
void Register(bool enabled,EyeFn eye,EffectsFn effects){
    if(!enabled||registered)return;registered=true;eyeFn=eye;effectsFn=effects;
    reshade::register_event<reshade::addon_event::init_device>(&InitDevice);
    reshade::register_event<reshade::addon_event::init_command_queue>(&InitQueue);
    reshade::register_event<reshade::addon_event::init_command_list>(&InitList);
    reshade::register_event<reshade::addon_event::destroy_command_list>(&DestroyList);
    reshade::register_event<reshade::addon_event::reset_command_list>(&ResetList);
    reshade::register_event<reshade::addon_event::init_resource>(&InitResource);
    reshade::register_event<reshade::addon_event::destroy_resource>(&DestroyResource);
    reshade::register_event<reshade::addon_event::init_effect_runtime>(&Runtime);
    reshade::register_event<reshade::addon_event::destroy_effect_runtime>(&DestroyRuntime);
    nativecapture::SetRootCbvObserver(&Push);
    reshade::register_event<reshade::addon_event::execute_command_list>(&Execute);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&BindDepth);
    reshade::register_event<reshade::addon_event::present>(&Present);
    logging::Write("Stereo rendering: manual Standard / experimental Single pass switch registered; native replay hooks inactive until selected");
}
void Configure(bool enabled){
    if(requested.exchange(enabled)!=enabled)++configuration;
}
void BeginPair(bool supported){
    done=false;
    if(!registered)return;
    const auto revision=configuration.load();
    if(appliedConfiguration!=revision){appliedConfiguration=revision;failed=false;cameraFilterReady=false;cameraFilterCount=0;cameraKeys.clear();layoutResolved=false;lookupCount=0;}
    const bool enable=requested.load()&&!failed;
    if(!enable){
        if(running.exchange(false)){
            if(collecting.exchange(false))nativecapture::EndEye();nativecapture::Stop();
            std::lock_guard lock(mutex);probes.clear();depths.clear();bindings.clear();cameraFilterReady=false;cameraFilterCount=0;cameraKeys.clear();
            logging::Write(nativecapture::ActiveHooks()==0?"Stereo rendering: Standard; native replay hooks disabled and tracking cleared":"Stereo rendering: Standard; replay hooks could not all be disabled");
        }
        if(!requested.load())status="Standard";return;
    }
    if(!running.load()){
        nativecapture::EnableReplayPackets();
        if(!nativecapture::Start()){Fallback("native renderer setup failed");return;}
        running=true;warmup=0;
        if(apiDevice)nativecapture::ObserveDevice(reinterpret_cast<ID3D12Device*>(apiDevice->get_native()));
        logging::Write("Stereo rendering: experimental Single pass selected; warming native recordings for two frames");status="Starting";
    }
    if(!supported){if(collecting.exchange(false))nativecapture::EndEye();warmup=0;return;}
    // A fresh Reset must supply the existing lists' allocator generations after
    // hooks were disabled in Standard. This is setup, not live qualification.
    if(warmup++<2)return;
    ++pairs;std::lock_guard lock(mutex);owner=GetCurrentThreadId();invalid=temporal=false;cameras[0]={};cameras[1]={};probes.clear();depths.clear();bindings.clear();seenBindings.clear();bindingCount=0;++bindingEpoch;
    nativecapture::BeginEye(owner);collecting=true;
}
bool Replayed(){return done;}
const char* Status(){return status.load();}
void View(unsigned eye,const float m[16]){if(!running.load())return;std::lock_guard lock(mutex);cameras[eye&1].View(m);}
void Projection(unsigned eye,const float m[16]){if(!running.load())return;std::lock_guard lock(mutex);cameras[eye&1].Projection(m);}
void TemporalEvaluation(){if(collecting.load()){std::lock_guard lock(mutex);temporal=true;}}
void Shutdown(){collecting=false;if(!registered)return;synchronousgpu::Shutdown();nativecapture::Shutdown();probes.clear();depths.clear();}
}
