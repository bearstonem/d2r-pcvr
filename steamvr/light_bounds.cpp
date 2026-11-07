#include "light_bounds.h"
#include "light_bounds_shader.h"
#include "native_lock.h"
#include "generation_cache.h"
#include "../vr/game_device_only.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace framevr::lightbounds {
namespace {
using namespace reshade::api;
struct Copy {device* owner=nullptr;pipeline original{},replacement{};};
NativeMutex mutex;
std::vector<Copy> copies;
std::atomic<uint64_t> generation{1};
std::atomic<bool> active{false},scene{false},reported{false};
thread_local bool ours=false;
void (*logger)(const char*)=nullptr;
std::once_flag compileOnce;
std::vector<uint8_t> code;
uint64_t Hash(const void* data,size_t size) {
    uint64_t hash=1469598103934665603ull;
    for(size_t i=0;i<size;++i)hash=(hash^static_cast<const uint8_t*>(data)[i])*1099511628211ull;
    return hash;
}
void Compile() {
    const auto library=LoadLibraryW(L"d3dcompiler_47.dll");
    const auto compile=library?reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(library,"D3DCompile")):nullptr;
    ID3DBlob *shader=nullptr,*errors=nullptr;
    if(compile&&SUCCEEDED(compile(Shader,sizeof Shader-1,"vr-light-depth-bounds",nullptr,nullptr,"main","cs_5_0",
                                 D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors))) {
        const auto* start=static_cast<const uint8_t*>(shader->GetBufferPointer());code.assign(start,start+shader->GetBufferSize());
    }
    if(shader)shader->Release();if(errors)errors->Release();if(library)FreeLibrary(library);
    if(code.empty()&&logger)logger("VR light bounds: compilation failed; retaining native lighting");
}
void Created(device* dev,pipeline_layout layout,uint32_t count,const pipeline_subobject* parts,pipeline original) {
    if(ours)return;
    for(uint32_t i=0;i<count;++i) {
        if(parts[i].type!=pipeline_subobject_type::compute_shader||!parts[i].data)continue;
        const auto& native=*static_cast<const shader_desc*>(parts[i].data);
        // Exact D2R 3.3.93787 light_bin_depth_bounds_c; other shaders/builds stay native.
        if(!native.code||native.code_size!=2164||Hash(native.code,native.code_size)!=0x7c22d74c89a5305cull)return;
        std::call_once(compileOnce,Compile);if(code.empty())return;
        auto shader=native;shader.code=code.data();shader.code_size=code.size();
        std::vector<pipeline_subobject> description(parts,parts+count);description[i].data=&shader;description[i].count=1;
        pipeline replacement{};ours=true;
        const bool ok=dev->create_pipeline(layout,count,description.data(),&replacement);ours=false;
        if(!ok){if(logger)logger("VR light bounds: replacement pipeline failed; retaining native lighting");return;}
        {std::lock_guard lock(mutex);copies.push_back({dev,original,replacement});++generation;}
        if(logger)logger("VR light bounds: verified native depth-reduction shader matched; rectangular-tile correction ready");
        return;
    }
}
void Destroyed(device* dev,pipeline original) {
    if(ours)return;
    pipeline replacement{};
    {std::lock_guard lock(mutex);
        for(auto it=copies.begin();it!=copies.end();++it)if(it->owner==dev&&it->original.handle==original.handle) {
            replacement=it->replacement;copies.erase(it);++generation;break;
        }
    }
    if(replacement.handle){ours=true;dev->destroy_pipeline(replacement);ours=false;}
}
void DeviceDestroyed(device* dev) {
    std::vector<pipeline> pending;
    {std::lock_guard lock(mutex);
        for(auto it=copies.begin();it!=copies.end();) {
            if(it->owner==dev){pending.push_back(it->replacement);it=copies.erase(it);}else ++it;
        }
        ++generation;
    }
    ours=true;for(auto p:pending)dev->destroy_pipeline(p);ours=false;
}
void Bind(command_list* commands,pipeline_stage stages,pipeline original) {
    if(ours||!(active.load(std::memory_order_relaxed)||scene.load(std::memory_order_relaxed))||uint32_t(stages&pipeline_stage::compute_shader)==0)return;
    static thread_local GenerationCache<Copy> cache;
    Copy found{};const auto version=generation.load(std::memory_order_acquire);auto* dev=commands->get_device();
    if(!cache.Get(original.handle,version,found)) {
        std::lock_guard lock(mutex);
        for(const auto& entry:copies)if(entry.owner==dev&&entry.original.handle==original.handle){found=entry;break;}
        cache.Put(original.handle,version,found);
    }
    if(found.owner!=dev||!found.replacement.handle)return;
    ours=true;commands->bind_pipeline(stages,found.replacement);ours=false;
    if(!reported.exchange(true)&&logger)logger("VR light bounds: rectangular-tile depth correction active during native VR eye rendering");
}
}
void Register(void (*log)(const char*)) {
    logger=log;
    reshade::register_event<reshade::addon_event::init_pipeline>(&d2rvr::D3D12Only<&Created>::Call);
    reshade::register_event<reshade::addon_event::destroy_pipeline>(&d2rvr::D3D12Only<&Destroyed>::Call);
    reshade::register_event<reshade::addon_event::destroy_device>(&d2rvr::D3D12Only<&DeviceDestroyed>::Call);
    reshade::register_event<reshade::addon_event::bind_pipeline>(&d2rvr::D3D12Only<&Bind>::Call);
}
void SetActive(bool on){active.store(on,std::memory_order_relaxed);}
void SetScene(bool on){
    if(scene.exchange(on,std::memory_order_relaxed)!=on&&logger)
        logger(on?"VR light bounds: rectangular-tile correction on for the campfire scene":"VR light bounds: campfire scene left; correction follows the eye rendering again");
}
}
