#include "motion_vectors.h"
#include "effects_layer.h"
#include "resource_descriptions.h"
#include "native_lock.h"
#include "../vr/game_device_only.h"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace framevr::motionvectors {
namespace {
using namespace reshade::api;
void (*logger)(const char*)=nullptr;
// The target as seen bound (Bind); its state, from the game's barriers on it
// (Barrier). Read without a lock on every barrier.
std::atomic<uint64_t> target{0};
std::atomic<uint32_t> state{0};
std::atomic<bool> known{false};
std::atomic<uint32_t> width{0},height{0};
NativeMutex found;   // a new target
bool Vectors(const resource_desc& d) {
    return d.type==resource_type::texture_2d&&d.texture.samples==1&&d.texture.depth_or_layers==1&&
        (d.texture.format==format::r16g16_float||d.texture.format==format::r16g16_typeless);
}
void Bind(command_list* cl,uint32_t count,const resource_view* rtvs,resource_view dsv) {
    if(count!=1||!rtvs||!rtvs[0].handle||!dsv.handle)return;
    auto* dev=cl->get_device();
    const auto r=resources::Resolve(dev,rtvs[0]);
    if(r.handle==target.load(std::memory_order_relaxed))return;
    const auto d=resources::Describe(dev,r),dd=resources::Describe(dev,resources::Resolve(dev,dsv));
    if(!Vectors(d)||dd.texture.width!=d.texture.width||dd.texture.height!=d.texture.height)return;
    std::lock_guard lock(found);
    known.store(false);target.store(r.handle);
    width.store(d.texture.width);height.store(d.texture.height);
    // a few times only: were two such targets bound by turns, every frame would say so
    static int told=0;
    if(logger&&told<4){++told;char line[160];std::snprintf(line,sizeof line,"Frame OpenXR: by turns - the game's motion vectors found (%ux%u)",d.texture.width,d.texture.height);logger(line);}
}
bool BeginPass(command_list* cl,uint32_t count,const render_pass_render_target_desc* rts,const render_pass_depth_stencil_desc* ds,render_pass_flags) {
    if(count!=1||!rts||!ds)return false;
    Bind(cl,1,&rts[0].view,ds->view);return false;
}
void Barrier(command_list*,uint32_t count,const resource* resources,const resource_usage*,const resource_usage* after) {
    const uint64_t t=target.load(std::memory_order_relaxed);
    if(!t)return;
    for(uint32_t i=0;i<count;++i) {
        if(resources[i].handle!=t)continue;
        state.store(uint32_t(after[i]),std::memory_order_relaxed);known.store(true,std::memory_order_release);
    }
}
void Destroyed(device*,resource r) {
    if(!r.handle||r.handle!=target.load())return;
    std::lock_guard lock(found);
    target.store(0);known.store(false);
}
}
void Register(void (*log)(const char*)) {
    logger=log;
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&Bind>::Call);
    reshade::register_event<reshade::addon_event::begin_render_pass>(&d2rvr::D3D12Only<&BeginPass>::Call);
    reshade::register_event<reshade::addon_event::barrier>(&d2rvr::D3D12Only<&Barrier>::Call);
    reshade::register_event<reshade::addon_event::destroy_resource>(&d2rvr::D3D12Only<&Destroyed>::Call);
}
bool Latest(Target& out) {
    std::lock_guard lock(found);
    if(!target.load()||!known.load(std::memory_order_acquire))return false;
    out.texture=reinterpret_cast<ID3D12Resource*>(target.load());out.state=effectslayer::State(state.load());
    out.width=width.load();out.height=height.load();
    return true;
}
}
