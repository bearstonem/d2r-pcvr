// Private depth channel adapted from natpoh/diablo2-vr-port 4b6284c,
// vr/vrcam.cpp namespace scenedepth. MIT, Copyright (c) 2026 BodyWalkVR.
#include "scene_depth.h"
#include "resource_descriptions.h"
#include "native_lock.h"
#include "../vr/game_device_only.h"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace framevr::scenedepth {
namespace {
using namespace reshade::api;
NativeMutex mutex;
Candidates candidates;
std::atomic<bool> enabled{false};
thread_local bool own=false;
void (*logger)(const char*)=nullptr;
effect_runtime* runtime=nullptr;
resource copy{};
resource_view view{};
resource_desc description{};
bool bound=false;
Kept latest;   // this present's copy, until taken
Candidate lastPick;   // the last eye's, for the replay's right eye
struct OwnScope {OwnScope(){own=true;}~OwnScope(){own=false;}};
format SrvFormat(format typeless) {
    switch(typeless) {
        case format::r32_g8_typeless:return format::r32_float_x8_uint;
        case format::r24_g8_typeless:return format::r24_unorm_x8_uint;
        case format::r32_typeless:return format::r32_float;
        case format::r16_typeless:return format::r16_unorm;
        default:return format::unknown;
    }
}
// Effects and teardown run on the runtime thread. Queue completion protects
// copies still sampled by the GPU before resize, runtime destruction or unload.
void Release() {
    if(!runtime)return;
    if(bound)runtime->update_texture_bindings("D2RVR_DEPTH",{},{});
    bound=false;
    if(copy.handle)runtime->get_command_queue()->wait_idle();
    auto* device=runtime->get_device();
    if(view.handle)device->destroy_resource_view(view);
    if(copy.handle)device->destroy_resource(copy);
    copy={};view={};description={};runtime=nullptr;
}
void Destroyed(device* device,resource resource) {
    if(own)return;
    std::lock_guard lock(mutex);candidates.Destroy(uintptr_t(device),resource.handle);
    if(lastPick.resource==resource.handle)lastPick={};
}
void DeviceDestroyed(device* device) {
    if(own)return;
    std::lock_guard lock(mutex);candidates.Destroy(uintptr_t(device));lastPick={};
}
void RuntimeDestroyed(effect_runtime* rt) {
    std::lock_guard lock(mutex);OwnScope scope;
    candidates.Destroy(uintptr_t(rt->get_device()));
    if(runtime==rt){enabled=false;Release();}
}
void Reloaded(effect_runtime* rt) {
    std::lock_guard lock(mutex);if(runtime==rt)bound=false;
}
void Barrier(command_list* cl,uint32_t count,const resource* resources,const resource_usage*,const resource_usage* after) {
    if(own||!enabled.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);
    for(uint32_t i=0;i<count;++i)candidates.Barrier(uintptr_t(cl->get_device()),resources[i].handle,uint32_t(after[i]));
}
}
void Observe(reshade::api::command_list* cl,uint32_t count,const reshade::api::resource_view* rtvs,
             reshade::api::resource depth,const reshade::api::resource_desc& desc) {
    if(own||!enabled.load(std::memory_order_relaxed)||!depth.handle||!count||!rtvs)return;
    if(desc.type!=resource_type::texture_2d||desc.texture.samples!=1||desc.texture.depth_or_layers!=1||
       desc.texture.levels!=1||(desc.usage&resource_usage::depth_stencil)==resource_usage::undefined||
       SrvFormat(format_to_typeless(desc.texture.format))==format::unknown)return;
    // Shadow-only passes cannot become the scene, even at an exact 4096 square
    // window. A matching color target is required before counting this binding.
    auto* device=cl->get_device();bool color=false;
    for(uint32_t i=0;i<count&&!color;++i) {
        const auto d=resources::Describe(device,resources::Resolve(device,rtvs[i]));
        color=d.type==resource_type::texture_2d&&d.texture.width==desc.texture.width&&d.texture.height==desc.texture.height;
    }
    if(!color)return;
    std::lock_guard lock(mutex);
    candidates.Bind(uintptr_t(device),depth.handle,desc.texture.width,desc.texture.height,color);
}
void Bind(reshade::api::effect_runtime* rt,reshade::api::command_list* cl,bool wanted,bool keep,bool reuse,float scale[2]) {
    std::lock_guard lock(mutex);OwnScope scope;
    latest={};
    const bool track=wanted||keep;
    enabled.store(track,std::memory_order_relaxed);
    if(runtime&&runtime!=rt)Release();
    runtime=rt;
    auto* device=rt->get_device();uint32_t w=0,h=0;rt->get_screenshot_width_and_height(&w,&h);
    auto best=candidates.Select(uintptr_t(device),w,h);
    candidates.EndEye(uintptr_t(device));
    if(best.resource)lastPick=best;
    else if(reuse&&lastPick.device==uintptr_t(device))best=lastPick;
    if(!track||!cl||!best.resource) {
        if(bound)rt->update_texture_bindings("D2RVR_DEPTH",{},{});
        bound=false;
        if(!track)candidates.Destroy(uintptr_t(device));
        return;
    }
    const auto source=resource{best.resource};
    const auto original=resources::Describe(device,source);
    const auto typeless=format_to_typeless(original.texture.format);
    if(SrvFormat(typeless)==format::unknown) {
        if(bound)rt->update_texture_bindings("D2RVR_DEPTH",{},{});
        bound=false;return;
    }
    if(!copy.handle||description.texture.width!=best.width||description.texture.height!=best.height||description.texture.format!=typeless) {
        Release();runtime=rt;
        description=resource_desc(resource_type::texture_2d,best.width,best.height,1,1,typeless,1,memory_heap::gpu_only,
            resource_usage::shader_resource|resource_usage::copy_dest);
        if(!device->create_resource(description,nullptr,resource_usage::shader_resource,&copy)||
           !device->create_resource_view(copy,resource_usage::shader_resource,resource_view_desc(SrvFormat(typeless)),&view)) {
            Release();if(logger)logger("VR scene depth: private copy unavailable; depth effects suppressed");return;
        }
        if(logger){char line[224];std::snprintf(line,sizeof line,
            "VR scene depth: private copy %ux%u format=%u source=%llx color-paired binds=%u; D2RVR_DEPTH ready",
            best.width,best.height,unsigned(original.texture.format),(unsigned long long)best.resource,best.binds);logger(line);}
    }
    const auto before=resource_usage(best.state);
    cl->barrier(source,before,resource_usage::copy_source);
    cl->barrier(copy,resource_usage::shader_resource,resource_usage::copy_dest);
    cl->copy_resource(source,copy);
    cl->barrier(source,resource_usage::copy_source,before);
    cl->barrier(copy,resource_usage::copy_dest,resource_usage::shader_resource);
    if(wanted&&!bound){rt->update_texture_bindings("D2RVR_DEPTH",view,view);bound=true;}
    else if(!wanted&&bound){rt->update_texture_bindings("D2RVR_DEPTH",{},{});bound=false;}
    // Scaled allocations use their entire own texture, unlike DLSS rendering
    // into the corner of a full-window allocation (existing DepthScale).
    const bool whole=best.width!=w||best.height!=h;
    if(wanted&&whole)scale[0]=scale[1]=1;
    latest={copy.handle,uint32_t(SrvFormat(typeless)),{whole?1.f:scale[0],whole?1.f:scale[1]}};
}
bool Take(Kept& out) {
    std::lock_guard lock(mutex);
    if(!latest.resource)return false;
    out=latest;latest={};return true;
}
void Register(void (*log)(const char*)) {
    logger=log;
    reshade::register_event<reshade::addon_event::barrier>(&d2rvr::D3D12Only<&Barrier>::Call);
    reshade::register_event<reshade::addon_event::destroy_resource>(&d2rvr::D3D12Only<&Destroyed>::Call);
    reshade::register_event<reshade::addon_event::destroy_device>(&d2rvr::D3D12Only<&DeviceDestroyed>::Call);
    reshade::register_event<reshade::addon_event::destroy_effect_runtime>(&d2rvr::D3D12Only<&RuntimeDestroyed>::Call);
    reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(&d2rvr::D3D12Only<&Reloaded>::Call);
}
void Shutdown(){std::lock_guard lock(mutex);OwnScope scope;enabled=false;latest={};lastPick={};Release();candidates={};}
}
