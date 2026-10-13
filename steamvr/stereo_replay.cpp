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
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <shlobj.h>
#include <fstream>
#include <filesystem>
#include <string>

namespace framevr::stereoreplay {
namespace {
using namespace reshade::api;
using Microsoft::WRL::ComPtr;
// Manual eye/effect setup reenters our camera and resource callbacks.
NativeRecursiveMutex mutex;
std::atomic<bool> requested{false},collecting{false};
std::atomic<bool> graphicsSupported{false};
bool registered=false,done=false,invalid=false,temporal=false,inReplay=false;
unsigned settled=0;
uint32_t owner=0,pairs=0,replays=0,fallbacks=0;
uint64_t reportAt=0;
EyeFn eyeFn=nullptr;EffectsFn effectsFn=nullptr;
effect_runtime* runtime=nullptr;
replayaudit::Camera cameras[2];
using CameraKey=std::pair<uint64_t,uint32_t>;
struct Binding {resource buffer;uint64_t offset,size;CameraKey cameraKey;};
struct Probe {ComPtr<ID3D12Resource> buffer;uint64_t offset;std::vector<float> original,right;};
std::map<uint64_t,std::map<std::pair<uint64_t,uint64_t>,Binding>> bindings;
std::map<CameraKey,bool> cameraKeys;
std::set<CameraKey> seenKeys;
std::map<std::pair<uint64_t,uint64_t>,Probe> probes;
struct Depth {ComPtr<ID3D12Resource> resource;unsigned binds=0;};
std::map<uint64_t,Depth> depths;
resource_view rtv{},rtvSrgb{};resource target{};reshade::api::device* apiDevice=nullptr;
const char* reason="settling";
bool D3D12(reshade::api::device* d){return d&&d->get_api()==device_api::d3d12;}
template<class T> uint64_t Id(T* p){return reinterpret_cast<uint64_t>(p);}
void InitDevice(reshade::api::device* d){if(D3D12(d))nativecapture::ObserveDevice(reinterpret_cast<ID3D12Device*>(d->get_native()));}
void InitQueue(command_queue* q){if(D3D12(q->get_device()))nativecapture::ObserveQueue(reinterpret_cast<ID3D12CommandQueue*>(q->get_native()));}
void InitList(command_list* cl){if(D3D12(cl->get_device()))nativecapture::ObserveList(reinterpret_cast<ID3D12GraphicsCommandList*>(cl->get_native()));}
void DestroyList(command_list* cl){if(!D3D12(cl->get_device()))return;nativecapture::ForgetList(cl->get_native());std::lock_guard lock(mutex);bindings.erase(cl->get_native());}
void ResetList(command_list* cl){if(!D3D12(cl->get_device()))return;std::lock_guard lock(mutex);bindings.erase(cl->get_native());}
void InitResource(reshade::api::device* d,const resource_desc&,const subresource_data*,resource_usage state,resource r){
    if(!D3D12(d))return;
    uint32_t native=uint32_t(state);
    if(state==resource_usage::general||state==resource_usage::present)native=D3D12_RESOURCE_STATE_COMMON;
    else if(native&uint32_t(resource_usage::constant_buffer))native=(native&~uint32_t(resource_usage::constant_buffer))|D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    nativecapture::InitialResourceState(reinterpret_cast<ID3D12Resource*>(r.handle),D3D12_RESOURCE_STATES(native));
}
void DestroyResource(reshade::api::device* d,resource r){if(!D3D12(d))return;nativecapture::ForgetResourceState(reinterpret_cast<ID3D12Resource*>(r.handle));if(collecting.load()){std::lock_guard lock(mutex);invalid=true;}}
void Runtime(effect_runtime* rt){std::lock_guard lock(mutex);runtime=rt;}
void DestroyRuntime(effect_runtime* rt){std::lock_guard lock(mutex);if(runtime==rt){runtime=nullptr;settled=0;}}
void Push(command_list* cl,shader_stage,pipeline_layout layout,uint32_t parameter,const descriptor_table_update& update){
    if(!collecting.load()||update.type!=descriptor_type::constant_buffer||!D3D12(cl->get_device()))return;
    std::lock_guard lock(mutex);const auto* ranges=static_cast<const buffer_range*>(update.descriptors);
    const CameraKey cameraKey{layout.handle,parameter};
    if(const auto known=cameraKeys.find(cameraKey);known!=cameraKeys.end()&&!known->second)return;
    seenKeys.insert(cameraKey);
    if(!ranges||update.count>64){invalid=true;return;}
    auto& list=bindings[cl->get_native()];
    for(unsigned i=0;i<update.count;++i){const auto& b=ranges[i];
        if(list.size()>=16384){invalid=true;return;}
        list.try_emplace(std::pair{b.buffer.handle,b.offset},Binding{b.buffer,b.offset,b.size,cameraKey});
    }
}
void Execute(command_queue* q,command_list* cl){
    InitQueue(q);if(!collecting.load()||!D3D12(q->get_device()))return;
    std::lock_guard lock(mutex);const auto list=bindings.find(cl->get_native());if(list==bindings.end())return;
    for(const auto& [bindingKey,b]:list->second){
        if(!b.buffer.handle)continue;
        const auto desc=q->get_device()->get_resource_desc(b.buffer);
        if(desc.type!=resource_type::buffer||desc.heap!=memory_heap::upload||b.offset>=desc.buffer.size)continue;
        const auto bytes=std::min({uint64_t(4096),desc.buffer.size-b.offset,b.size})&~uint64_t(3);
        if(bytes<448)continue;
        auto* buffer=reinterpret_cast<ID3D12Resource*>(b.buffer.handle);
        void* data=nullptr;D3D12_RANGE read{SIZE_T(b.offset),SIZE_T(b.offset+bytes)};
        if(FAILED(buffer->Map(0,&read,&data))||!data){invalid=true;continue;}
        std::vector<float> words(size_t(bytes/4));SIZE_T copied=0;
        const bool ok=ReadProcessMemory(GetCurrentProcess(),static_cast<const uint8_t*>(data)+b.offset,words.data(),bytes,&copied)&&copied==bytes;
        D3D12_RANGE none{};buffer->Unmap(0,&none);
        if(!ok){invalid=true;continue;}
        const auto layout=replayaudit::InspectCamera(words,cameras[0]);
        if(!layout.completeRecords)continue;
        cameraKeys[b.cameraKey]=true;
        const auto key=std::pair{b.buffer.handle,b.offset};const auto old=probes.find(key);
        if(old!=probes.end()){
            if(old->second.original.size()!=words.size()||std::memcmp(old->second.original.data(),words.data(),words.size()*4))invalid=true;
        }else if(probes.size()<256)probes.emplace(key,Probe{buffer,b.offset,std::move(words),{}});
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
        // Write only changed words; overlapping CBV windows then agree and
        // unrelated engine constants are never replaced from an old snapshot.
        const auto& values=right?p.right:p.original;
        for(size_t i=0;i<values.size();){
            if(!std::memcmp(&p.right[i],&p.original[i],4)){++i;continue;}
            const size_t first=i++;while(i<values.size()&&std::memcmp(&p.right[i],&p.original[i],4))++i;
            SIZE_T copied=0;const SIZE_T bytes=(i-first)*4;
            if(!WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(mapped)+p.offset+first*4,&values[first],bytes,&copied)||copied!=bytes)ok=false;
        }
        D3D12_RANGE written{SIZE_T(p.offset),SIZE_T(p.offset+p.original.size()*4)};p.buffer->Unmap(0,&written);
    }return ok;
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
    std::lock_guard lock(mutex);nativecapture::SealPresent();nativecapture::ReplayPacket packet;
    // Qualify each shader root binding from ALL its submitted ranges before
    // caching its camera role. Relearn after menus/area changes or a toggle.
    for(const auto key:seenKeys)cameraKeys.try_emplace(key,false);
    struct Lease {bool held=nativecapture::BeginReplayLease();~Lease(){if(held)nativecapture::EndReplayLease();}} lease;
    if(!lease.held||invalid||temporal||!runtime||probes.empty()||!nativecapture::SnapshotReplay(packet)){reason=invalid?"mutable constants/resources":temporal?"temporal upscaling":probes.empty()?"camera buffers missing":!runtime?"effects runtime missing":nativecapture::ReplayPacketFailure();nativecapture::EndEye();++fallbacks;return;}
    nativecapture::EndEye();
    // Replaying foreign techniques would require their own eye histories.
    bool other=false;runtime->enumerate_techniques(nullptr,+[](effect_runtime* rt,effect_technique t,void* data){
        if(!rt->get_technique_state(t))return;char name[256]{};size_t length=sizeof name;rt->get_technique_name(t,name,&length);
        if(std::strcmp(name,"D2R_DepthFog")&&std::strcmp(name,"D2R_DepthFog_R"))*static_cast<bool*>(data)=true;
    },&other);
    const auto rightTech=runtime->find_technique("D2R_DepthFog.fx","D2R_DepthFog_R");
    if(other||!rightTech.handle||!runtime->get_effects_state()||!Setup(q,swap)){reason="effects/setup unsupported";++fallbacks;return;}
    const auto texture=runtime->find_texture_variable("D2R_DepthFog.fx","FogDepthTex");resource_view depthView{},depthViewSrgb{};
    if(texture.handle)runtime->get_texture_binding(texture,&depthView,&depthViewSrgb);
    if(!depthView.handle){reason="depth binding missing";++fallbacks;return;}
    const auto dest=q->get_device()->get_resource_from_view(depthView);
    ComPtr<ID3D12Resource> depthDestination=reinterpret_cast<ID3D12Resource*>(dest.handle),depthSource;
    unsigned most=0;
    if(depthDestination){const auto d=depthDestination->GetDesc();for(const auto& [id,candidate]:depths){const auto s=candidate.resource->GetDesc();
        if(candidate.binds>most&&s.Width==d.Width&&s.Height==d.Height&&s.MipLevels==1&&s.DepthOrArraySize==1&&d.MipLevels==1&&d.DepthOrArraySize==1&&s.SampleDesc.Count==d.SampleDesc.Count&&format_to_typeless(format(s.Format))==format_to_typeless(format(d.Format))){depthSource=candidate.resource;most=candidate.binds;}
    }}
    D3D12_RESOURCE_STATES sourceState,destState;
    if(!depthSource||!depthDestination||!UniformDepthState(depthSource.Get(),sourceState)||!UniformDepthState(depthDestination.Get(),destState)){reason="depth state incomplete";++fallbacks;return;}
    if(!eyeFn(1)){reason="right XR camera unavailable";++fallbacks;return;}
    unsigned records=0;for(auto& [key,p]:probes){p.right=p.original;const auto n=FoldCamera(p.right,cameras[0],cameras[1]);if(!n){eyeFn(0);reason="camera layout changed";++fallbacks;return;}records+=n;}
    eyeFn(0);
    if(!synchronousgpu::Drain(packet)){reason="original GPU completion failed";requested=false;++fallbacks;return;}
    inReplay=true;Effects(q,false);
    if(!synchronousgpu::Complete()||!framevr::CaptureReplay(q,swap,0)||!synchronousgpu::Complete()){inReplay=false;reason="left copy failed";requested=false;++fallbacks;return;}
    const bool written=WriteProbes(true);
    const bool rendered=written&&synchronousgpu::Execute(packet);
    // Borrowed constants/recordings never escape this synchronous completion.
    const bool restored=WriteProbes(false);
    if(!rendered||!restored){inReplay=false;reason="GPU replay/restore failed";requested=false;++fallbacks;return;}
    eyeFn(1);
    if(!UniformDepthState(depthSource.Get(),sourceState)||!UniformDepthState(depthDestination.Get(),destState)||!synchronousgpu::CopyDepth(depthSource.Get(),depthDestination.Get(),sourceState,destState)){inReplay=false;reason="right depth copy failed";requested=false;++fallbacks;return;}
    Effects(q,true);
    if(!synchronousgpu::Complete()||!framevr::CaptureReplay(q,swap,1)||!synchronousgpu::Complete()){inReplay=false;reason="right copy failed";requested=false;++fallbacks;return;}
    framevr::FinishReplayCapture();done=true;inReplay=false;++replays;reason="active";
    if(replays==1){char line[200];std::snprintf(line,sizeof line,"Stereo replay: first actual pair; %u batches, %u camera ranges, %u records; second engine draw skipped",unsigned(packet.batches.size()),unsigned(probes.size()),records);logging::Write(line);}
}
}
void Register(bool enabled,EyeFn eye,EffectsFn effects){
    if(!enabled||registered)return;registered=true;eyeFn=eye;effectsFn=effects;nativecapture::EnableReplayPackets();nativecapture::Start();
    reshade::register_event<reshade::addon_event::init_device>(&InitDevice);
    reshade::register_event<reshade::addon_event::init_command_queue>(&InitQueue);
    reshade::register_event<reshade::addon_event::init_command_list>(&InitList);
    reshade::register_event<reshade::addon_event::destroy_command_list>(&DestroyList);
    reshade::register_event<reshade::addon_event::reset_command_list>(&ResetList);
    reshade::register_event<reshade::addon_event::init_resource>(&InitResource);
    reshade::register_event<reshade::addon_event::destroy_resource>(&DestroyResource);
    reshade::register_event<reshade::addon_event::init_effect_runtime>(&Runtime);
    reshade::register_event<reshade::addon_event::destroy_effect_runtime>(&DestroyRuntime);
    reshade::register_event<reshade::addon_event::push_descriptors>(&Push);
    reshade::register_event<reshade::addon_event::execute_command_list>(&Execute);
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&BindDepth);
    reshade::register_event<reshade::addon_event::present>(&Present);
    logging::Write("Stereo replay: synchronous experimental renderer registered; ordinary two-pass fallback remains available");
}
void Configure(bool enabled){
    requested=enabled;if(!enabled)return;
    // Read existing settings; never change graphics to qualify for replay.
    PWSTR directory=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_SavedGames,0,nullptr,&directory))){graphicsSupported=false;return;}
    const auto file=std::filesystem::path(directory)/L"Diablo II Resurrected"/L"Settings.json";CoTaskMemFree(directory);
    std::ifstream stream(file);const std::string json((std::istreambuf_iterator<char>(stream)),{});
    auto number=[&](const char* key){const auto start=json.find(std::string("\"")+key+"\"");if(start==std::string::npos)return -1;
        const auto colon=json.find(':',start);if(colon==std::string::npos)return -1;const auto value=json.find_first_not_of(" \r\n\t",colon+1);
        if(value==std::string::npos||json[value]<'0'||json[value]>'9'||(value+1<json.size()&&json[value+1]>='0'&&json[value+1]<='9'))return -1;return json[value]-'0';};
    graphicsSupported=number("NVIDIA DLSS")==0&&number("Anti Aliasing")>=0&&number("Anti Aliasing")<=1&&number("Dynamic Resolution Scaling")==0;
}
void BeginPair(bool supported){
    done=false;
    if(!registered)return;
    if(GetTickCount64()-reportAt>=5000){reportAt=GetTickCount64();char line[200];std::snprintf(line,sizeof line,"Stereo replay: pairs=%u replayed=%u fallback=%u reason=%s",pairs,replays,fallbacks,reason);logging::Write(line);}
    if(!requested.load()||!supported||!graphicsSupported.load()){settled=0;std::lock_guard lock(mutex);if(collecting.exchange(false))nativecapture::EndEye();cameraKeys.clear();reason=!graphicsSupported.load()?"graphics unsupported":"disabled/inactive";return;}
    if(settled++<90){reason="settling";return;}
    ++pairs;std::lock_guard lock(mutex);owner=GetCurrentThreadId();invalid=temporal=false;cameras[0]={};cameras[1]={};probes.clear();depths.clear();bindings.clear();seenKeys.clear();
    nativecapture::BeginEye(owner);collecting=true;
}
bool Replayed(){return done;}
void View(unsigned eye,const float m[16]){if(!registered)return;std::lock_guard lock(mutex);cameras[eye&1].View(m);}
void Projection(unsigned eye,const float m[16]){if(!registered)return;std::lock_guard lock(mutex);cameras[eye&1].Projection(m);}
void TemporalEvaluation(){if(collecting.load()){std::lock_guard lock(mutex);temporal=true;}}
void Shutdown(){collecting=false;if(!registered)return;synchronousgpu::Shutdown();nativecapture::Shutdown();probes.clear();depths.clear();}
}
