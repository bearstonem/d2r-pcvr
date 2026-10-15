#include "transparent_fog.h"
#include "transparent_fog_shader.h"
#include "transparent_fog_state.h"
#include "resource_descriptions.h"
#include "native_lock.h"
#include "../vr/game_device_only.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

namespace framevr::transparentfog {
namespace {
using namespace reshade::api;
struct Copy {device* owner=nullptr;pipeline original{},mask{};};
struct State {
    Copy pipeline{};
    resource_view rtv{};
    viewport area{};
    bool target=false,viewportKnown=false,renderPass=false;
    command_queue* queue=nullptr;
    uint64_t captured=0;
};
constexpr uint8_t StateId[16]={0x29,0x9d,0xd8,0xc2,0x3e,0xbb,0x4c,0x58,0x98,0xa6,0x14,0xfe,0x77,0xa4,0x62,0x91};
NativeMutex mutex;
std::vector<Copy> copies;
std::atomic<uint64_t> generation{1};
std::atomic<bool> enabled{false};
thread_local bool ours=false;
struct OwnScope {bool previous=ours;OwnScope(){ours=true;}~OwnScope(){ours=previous;}};
void (*logger)(const char*)=nullptr;
std::once_flag compileOnce;
std::vector<uint8_t> code;
effect_runtime* runtime=nullptr;
resource mask{};
resource_view srv{},rtv{};
uint32_t width=0,height=0,wantedWidth=0,wantedHeight=0;
EyeCapture capture;
float transform[4]={1,1,0,0};
bool bound=false,reported=false;

State* Get(command_list* cl) {
    uint64_t value=0;cl->get_private_data(StateId,&value);
    return reinterpret_cast<State*>(value);
}
void Initialized(command_list* cl) {cl->set_private_data(StateId,reinterpret_cast<uint64_t>(new State));}
void Reset(command_list* cl) {
    if(auto* s=Get(cl)){auto* queue=s->queue;*s={};s->queue=queue;}
}
void CommandDestroyed(command_list* cl) {delete Get(cl);cl->set_private_data(StateId,0);}
uint64_t Hash(const void* data,size_t size) {
    uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<size;++i)h=(h^static_cast<const uint8_t*>(data)[i])*1099511628211ull;
    return h;
}
void Compile() {
    const auto lib=LoadLibraryW(L"d3dcompiler_47.dll");
    const auto compile=lib?reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(lib,"D3DCompile")):nullptr;
    ID3DBlob *shader=nullptr,*errors=nullptr;
    if(compile&&SUCCEEDED(compile(Shader,sizeof Shader-1,"vr-transparent-fog",nullptr,nullptr,"PS","ps_5_0",
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors))) {
        auto* begin=static_cast<const uint8_t*>(shader->GetBufferPointer());code.assign(begin,begin+shader->GetBufferSize());
    }
    if(shader)shader->Release();if(errors)errors->Release();if(lib)FreeLibrary(lib);
    if(code.empty()&&logger)logger("VR transparent fog: mask shader unavailable; retaining ordinary fog");
}
void Created(device* dev,pipeline_layout layout,uint32_t count,const pipeline_subobject* parts,pipeline original) {
    if(ours)return;
    for(uint32_t i=0;i<count;++i) {
        if(parts[i].type!=pipeline_subobject_type::pixel_shader||!parts[i].data)continue;
        const auto& native=*static_cast<const shader_desc*>(parts[i].data);
        // Exact D2R 3.3.93787 ps_transparent_resolve, verified from local assets.
        // Unknown shaders/builds are never replaced or sampled speculatively.
        if(!native.code||native.code_size!=756||Hash(native.code,native.code_size)!=0x53183967e9589edbull)return;
        std::call_once(compileOnce,Compile);if(code.empty())return;
        auto shader=native;shader.code=code.data();shader.code_size=code.size();
        std::vector<pipeline_subobject> desc(parts,parts+count);desc[i].data=&shader;
        format colour=format::r8_unorm,depth=format::unknown;
        blend_desc blend{};depth_stencil_desc depthState{};depthState.depth_enable=false;depthState.depth_write_mask=false;
        bool hasColour=false,hasBlend=false,hasDepth=false,hasDepthFormat=false;
        for(auto& p:desc) {
            if(p.type==pipeline_subobject_type::render_target_formats){p.count=1;p.data=&colour;hasColour=true;}
            if(p.type==pipeline_subobject_type::blend_state){p.count=1;p.data=&blend;hasBlend=true;}
            if(p.type==pipeline_subobject_type::depth_stencil_state){p.count=1;p.data=&depthState;hasDepth=true;}
            if(p.type==pipeline_subobject_type::depth_stencil_format){p.count=1;p.data=&depth;hasDepthFormat=true;}
            if(p.type==pipeline_subobject_type::sample_count&&*static_cast<const uint32_t*>(p.data)!=1)return;
        }
        if(!hasColour)desc.push_back({pipeline_subobject_type::render_target_formats,1,&colour});
        if(!hasBlend)desc.push_back({pipeline_subobject_type::blend_state,1,&blend});
        if(!hasDepth)desc.push_back({pipeline_subobject_type::depth_stencil_state,1,&depthState});
        if(!hasDepthFormat)desc.push_back({pipeline_subobject_type::depth_stencil_format,1,&depth});
        pipeline replacement{};OwnScope own;
        if(!dev->create_pipeline(layout,uint32_t(desc.size()),desc.data(),&replacement)) {
            if(logger)logger("VR transparent fog: mask pipeline failed; retaining ordinary fog");return;
        }
        {std::lock_guard lock(mutex);copies.push_back({dev,original,replacement});++generation;}
        if(logger)logger("VR transparent fog: verified native transparency resolve matched; mask pipeline ready");
        return;
    }
}
void PipelineDestroyed(device* dev,pipeline original) {
    if(ours)return;
    pipeline replacement{};
    {std::lock_guard lock(mutex);
        for(auto it=copies.begin();it!=copies.end();++it)if(it->owner==dev&&it->original.handle==original.handle) {
            replacement=it->mask;copies.erase(it);++generation;break;
        }
    }
    if(replacement.handle){OwnScope own;dev->destroy_pipeline(replacement);}
}
void PipelineBound(command_list* cl,pipeline_stage stages,pipeline original) {
    if(ours||uint32_t(stages&pipeline_stage::all_graphics)==0)return;
    auto* s=Get(cl);if(!s)return;s->pipeline={};
    if(!enabled.load(std::memory_order_relaxed))return;
    static thread_local GenerationCache<Copy> cache;
    Copy found{};const auto version=generation.load(std::memory_order_acquire);auto* dev=cl->get_device();
    if(!cache.Get(original.handle,version,found)) {
        std::lock_guard lock(mutex);
        for(const auto& c:copies)if(c.owner==dev&&c.original.handle==original.handle){found=c;break;}
        cache.Put(original.handle,version,found);
    }
    if(found.owner==dev)s->pipeline=found;
}
void Targets(command_list* cl,uint32_t count,const resource_view* views,resource_view depth) {
    if(ours)return;if(auto* s=Get(cl)) {
        s->target=count==1&&views&&views[0].handle&&!depth.handle;
        s->rtv=s->target?views[0]:resource_view{};
    }
}
void Viewports(command_list* cl,uint32_t first,uint32_t count,const viewport* areas) {
    if(ours)return;if(auto* s=Get(cl)) {
        s->viewportKnown=first==0&&count==1&&areas;
        if(s->viewportKnown)s->area=areas[0];
    }
}
// OM changes inside a native render pass are illegal; fail closed there.
bool BeginPass(command_list* cl,uint32_t,const render_pass_render_target_desc*,const render_pass_depth_stencil_desc*,render_pass_flags) {
    if(!ours)if(auto* s=Get(cl))s->renderPass=true;return false;
}
bool EndPass(command_list* cl) {if(!ours)if(auto* s=Get(cl)){s->renderPass=false;s->target=false;}return false;}
template<class Draw> void Capture(command_list* cl,Draw draw) {
    if(ours||!enabled.load(std::memory_order_relaxed))return;
    auto* s=Get(cl);
    if(!s||!s->pipeline.mask.handle||!s->target||!s->viewportKnown||s->renderPass)return;
    std::lock_guard lock(mutex);
    if(!runtime||cl->get_device()!=runtime->get_device()||s->queue!=runtime->get_command_queue())return;
    const auto d=resources::Describe(cl->get_device(),resources::Resolve(cl->get_device(),s->rtv));
    const auto& a=s->area;
    if(d.type!=resource_type::texture_2d||d.texture.samples!=1||d.texture.depth_or_layers!=1||
       d.texture.width<16||d.texture.height<16||d.texture.width>8192||d.texture.height>8192||
       !std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.width)||!std::isfinite(a.height)||
       a.x<0||a.y<0||a.width<16||a.height<16||a.x+a.width>d.texture.width||a.y+a.height>d.texture.height)return;
    wantedWidth=d.texture.width;wantedHeight=d.texture.height;
    // Allocate/resize only at the effects boundary, after old GPU work completes.
    if(!mask.handle||width!=wantedWidth||height!=wantedHeight)return;
    OwnScope own;
    cl->barrier(mask,resource_usage::shader_resource,resource_usage::render_target);
    const float empty[4]={};cl->clear_render_target_view(rtv,empty,0,nullptr);
    cl->bind_render_targets_and_depth_stencil(1,&rtv,{});
    cl->bind_pipeline(pipeline_stage::all_graphics,s->pipeline.mask);
    draw();
    cl->bind_pipeline(pipeline_stage::all_graphics,s->pipeline.original);
    cl->bind_render_targets_and_depth_stencil(1,&s->rtv,{});
    cl->barrier(mask,resource_usage::render_target,resource_usage::shader_resource);
    transform[0]=a.width/width;transform[1]=a.height/height;transform[2]=a.x/width;transform[3]=a.y/height;
    s->captured=capture.Record();
}
bool Draw(command_list* cl,uint32_t vertices,uint32_t instances,uint32_t first,uint32_t firstInstance) {
    if(instances==1&&(vertices==3||vertices==4||vertices==6))Capture(cl,[&]{cl->draw(vertices,instances,first,firstInstance);});
    return false; // Original game draw still executes exactly once, untouched.
}
bool DrawIndexed(command_list* cl,uint32_t indices,uint32_t instances,uint32_t first,int32_t offset,uint32_t firstInstance) {
    if(instances==1&&(indices==3||indices==6))Capture(cl,[&]{cl->draw_indexed(indices,instances,first,offset,firstInstance);});
    return false;
}
void Executed(command_queue* queue,command_list* cl) {
    if(ours)return;if(auto* s=Get(cl)) {
        std::lock_guard lock(mutex);s->queue=queue;
        capture.Submit(s->captured,runtime&&queue==runtime->get_command_queue());
        s->captured=0;
    }
}
void Release() {
    if(!runtime)return;
    if(bound)runtime->update_texture_bindings("D2RVR_TRANSPARENT",{},{});
    bound=false;
    if(mask.handle)runtime->get_command_queue()->wait_idle();
    auto* dev=runtime->get_device();
    if(srv.handle)dev->destroy_resource_view(srv);if(rtv.handle)dev->destroy_resource_view(rtv);
    if(mask.handle)dev->destroy_resource(mask);
    mask={};srv={};rtv={};width=height=0;capture.Invalidate();
}
void RuntimeDestroyed(effect_runtime* rt) {
    std::lock_guard lock(mutex);OwnScope own;
    if(runtime==rt){enabled=false;Release();runtime=nullptr;wantedWidth=wantedHeight=0;}
}
void Reloaded(effect_runtime* rt) {std::lock_guard lock(mutex);if(runtime==rt){bound=false;capture.Invalidate();}}
void DeviceDestroyed(device* dev) {
    std::vector<pipeline> pending;
    {std::lock_guard lock(mutex);
        for(auto it=copies.begin();it!=copies.end();) {
            if(it->owner==dev){pending.push_back(it->mask);it=copies.erase(it);}else ++it;
        }
        ++generation;
    }
    OwnScope own;for(auto p:pending)dev->destroy_pipeline(p);
}
}
bool Bind(effect_runtime* rt,bool wanted,float scale[4]) {
    std::lock_guard lock(mutex);OwnScope own;
    const bool wasEnabled=enabled.exchange(wanted,std::memory_order_relaxed);
    if(runtime&&runtime!=rt){Release();wantedWidth=wantedHeight=0;}
    runtime=rt;
    bool fresh=wanted&&wasEnabled&&mask.handle&&capture.Fresh();
    if(wanted&&wantedWidth&&wantedHeight&&(!mask.handle||width!=wantedWidth||height!=wantedHeight)) {
        Release();fresh=false;
        auto* dev=rt->get_device();
        const resource_desc desc(resource_type::texture_2d,wantedWidth,wantedHeight,1,1,format::r8_unorm,1,
            memory_heap::default_,resource_usage::shader_resource|resource_usage::render_target);
        if(!dev->create_resource(desc,nullptr,resource_usage::shader_resource,&mask)||
           !dev->create_resource_view(mask,resource_usage::shader_resource,resource_view_desc(format::r8_unorm),&srv)||
           !dev->create_resource_view(mask,resource_usage::render_target,resource_view_desc(format::r8_unorm),&rtv)) {
            Release();if(logger)logger("VR transparent fog: private mask allocation failed; retaining ordinary fog");
        } else {
            width=wantedWidth;height=wantedHeight;
            if(logger){char message[160];std::snprintf(message,sizeof message,"VR transparent fog: private effect mask %ux%u ready",width,height);logger(message);}
        }
    }
    if(fresh) {
        if(!bound){rt->update_texture_bindings("D2RVR_TRANSPARENT",srv,srv);bound=true;}
        for(unsigned i=0;i<4;++i)scale[i]=transform[i];
        if(!reported&&logger){logger("VR transparent fog: submitted current-eye native effect mask active");reported=true;}
    } else if(bound){rt->update_texture_bindings("D2RVR_TRANSPARENT",{},{});bound=false;}
    capture.Invalidate();return fresh;
}
void Register(void (*log)(const char*)) {
    logger=log;
#define EVENT(name,fn) reshade::register_event<reshade::addon_event::name>(&d2rvr::D3D12Only<&fn>::Call)
    EVENT(init_command_list,Initialized);EVENT(reset_command_list,Reset);EVENT(destroy_command_list,CommandDestroyed);
    EVENT(init_pipeline,Created);EVENT(destroy_pipeline,PipelineDestroyed);EVENT(bind_pipeline,PipelineBound);
    EVENT(bind_render_targets_and_depth_stencil,Targets);EVENT(bind_viewports,Viewports);
    EVENT(begin_render_pass,BeginPass);EVENT(end_render_pass,EndPass);
    EVENT(draw,Draw);EVENT(draw_indexed,DrawIndexed);EVENT(execute_command_list,Executed);
    EVENT(destroy_effect_runtime,RuntimeDestroyed);EVENT(reshade_reloaded_effects,Reloaded);EVENT(destroy_device,DeviceDestroyed);
#undef EVENT
}
void Shutdown(){std::lock_guard lock(mutex);OwnScope own;enabled=false;Release();runtime=nullptr;}
}
