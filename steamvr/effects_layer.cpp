#include "effects_layer.h"
#include "resource_descriptions.h"
#include "native_lock.h"
#include "../vr/game_device_only.h"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace framevr::effectslayer {
namespace {
using namespace reshade::api;
void (*logger)(const char*)=nullptr;
// The layer's two targets, as seen bound (Bind); their states, from the
// game's barriers on them (Barrier). Read without a lock on every barrier.
std::atomic<uint64_t> targets[2]{};
std::atomic<uint32_t> states[2]{};
std::atomic<bool> known[2]{};
std::atomic<uint32_t> width{0},height{0};
NativeMutex found;   // a new pair
bool Layered(const resource_desc& d) {
    return d.type==resource_type::texture_2d&&d.texture.samples==1&&d.texture.depth_or_layers==1&&
        (d.texture.format==format::r16g16b16a16_float||d.texture.format==format::r16g16b16a16_typeless);
}
void Bind(command_list* cl,uint32_t count,const resource_view* rtvs,resource_view dsv) {
    if(count<2||!rtvs||!rtvs[0].handle||!rtvs[1].handle||!dsv.handle)return;
    auto* dev=cl->get_device();
    const auto a=resources::Resolve(dev,rtvs[0]),b=resources::Resolve(dev,rtvs[1]);
    if(a.handle==targets[0].load(std::memory_order_relaxed)&&b.handle==targets[1].load(std::memory_order_relaxed))return;
    const auto da=resources::Describe(dev,a),db=resources::Describe(dev,b),dd=resources::Describe(dev,resources::Resolve(dev,dsv));
    if(!Layered(da)||!Layered(db)||db.texture.width!=da.texture.width||db.texture.height!=da.texture.height||
       dd.texture.width!=da.texture.width||dd.texture.height!=da.texture.height)return;
    std::lock_guard lock(found);
    for(int i=0;i<2;++i)known[i].store(false);
    targets[0].store(a.handle);targets[1].store(b.handle);
    width.store(da.texture.width);height.store(da.texture.height);
    if(logger){char line[160];std::snprintf(line,sizeof line,"Frame OpenXR: by turns - the game's effects layer found (%ux%u, two half-float targets)",da.texture.width,da.texture.height);logger(line);}
}
bool BeginPass(command_list* cl,uint32_t count,const render_pass_render_target_desc* rts,const render_pass_depth_stencil_desc* ds,render_pass_flags) {
    if(count<2||!rts||!ds)return false;
    resource_view rtvs[2]{rts[0].view,rts[1].view};
    Bind(cl,2,rtvs,ds->view);return false;
}
void Barrier(command_list*,uint32_t count,const resource* resources,const resource_usage*,const resource_usage* after) {
    const uint64_t a=targets[0].load(std::memory_order_relaxed),b=targets[1].load(std::memory_order_relaxed);
    if(!a)return;
    for(uint32_t i=0;i<count;++i) {
        const int k=resources[i].handle==a?0:resources[i].handle==b?1:-1;
        if(k<0)continue;
        states[k].store(uint32_t(after[i]),std::memory_order_relaxed);known[k].store(true,std::memory_order_release);
    }
}
void Destroyed(device*,resource r) {
    if(!r.handle||(r.handle!=targets[0].load()&&r.handle!=targets[1].load()))return;
    std::lock_guard lock(found);
    for(int i=0;i<2;++i){targets[i].store(0);known[i].store(false);}
}
}
void Register(void (*log)(const char*)) {
    logger=log;
    reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(&d2rvr::D3D12Only<&Bind>::Call);
    reshade::register_event<reshade::addon_event::begin_render_pass>(&d2rvr::D3D12Only<&BeginPass>::Call);
    reshade::register_event<reshade::addon_event::barrier>(&d2rvr::D3D12Only<&Barrier>::Call);
    reshade::register_event<reshade::addon_event::destroy_resource>(&d2rvr::D3D12Only<&Destroyed>::Call);
}
bool Latest(Layer& out) {
    std::lock_guard lock(found);
    for(int i=0;i<2;++i) {
        if(!targets[i].load()||!known[i].load(std::memory_order_acquire))return false;
        out.targets[i]=reinterpret_cast<ID3D12Resource*>(targets[i].load());out.states[i]=State(states[i].load());
    }
    out.width=width.load();out.height=height.load();
    return true;
}
}
