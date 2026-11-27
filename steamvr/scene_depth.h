#pragma once
#include "active_keys.h"
#include <reshade.hpp>
#include <array>
#include <cmath>
#include <cstdint>

namespace framevr::scenedepth {
// Selection policy adapted from natpoh/diablo2-vr-port 4b6284c (MIT,
// Copyright (c) 2026 BodyWalkVR). Track current-eye color/depth pairs only;
// never carry a previous eye's counts or a destroyed resource into selection.
struct Candidate {
    uintptr_t device=0;
    uint64_t resource=0;
    uint32_t width=0,height=0,binds=0,state=0;
    bool known=false;
};
class Candidates {
    std::array<Candidate,32> entries{};
    ActiveKeys tracked;
    void Erase(Candidate& entry) {
        tracked.Remove(entry.resource);
        entry = {};
    }
public:
    // Recording threads check one published mask for the whole barrier batch.
    // Mutations and the authoritative device/resource lookup use the scene mutex.
    bool MayTransition(uint32_t count, const reshade::api::resource* resources) const {
        return tracked.MaybeContainsAny(count, [&](uint32_t i) { return resources[i].handle; });
    }
    void Bind(uintptr_t device,uint64_t resource,uint32_t w,uint32_t h,bool matchingColor) {
        if(!resource||!matchingColor)return;
        Candidate* slot=nullptr;
        for(auto& e:entries) {
            if(e.device==device&&e.resource==resource){++e.binds;return;}
            if(!e.resource&&!slot)slot=&e;
        }
        if(slot) {
            *slot={device,resource,w,h,1,0,false};
            tracked.Add(resource);
        }
    }
    void Barrier(uintptr_t device,uint64_t resource,uint32_t state) {
        for(auto& e:entries)if(e.device==device&&e.resource==resource){e.state=state;e.known=true;return;}
    }
    void Destroy(uintptr_t device,uint64_t resource=0) {
        for(auto& e:entries)if(e.resource&&e.device==device&&(!resource||e.resource==resource))Erase(e);
    }
    void Clear() {
        for(auto& e:entries)if(e.resource)Erase(e);
    }
    void EndEye(uintptr_t device) {
        for(auto& e:entries)if(e.resource&&e.device==device) {if(!e.binds)Erase(e);else e.binds=0;}
    }
    Candidate Select(uintptr_t device,uint32_t w,uint32_t h)const {
        Candidate exact{},scaled{};
        if(!w||!h)return {};
        for(const auto& e:entries) {
            if(e.device!=device||!e.binds||!e.known)continue;
            if(e.width==w&&e.height==h){if(e.binds>exact.binds)exact=e;continue;}
            const double scale=double(e.width)/w;
            const bool shape=std::abs(double(e.width)*h-double(e.height)*w)<=.005*e.width*h;
            if(shape&&scale>=.49&&scale<=2.01&&e.binds>scaled.binds)scaled=e;
        }
        return exact.resource?exact:scaled;
    }
};
void Register(void (*log)(const char*));
// Called by the existing depth-target observer, without another duplicate hook.
void Observe(reshade::api::command_list*,uint32_t,const reshade::api::resource_view*,
             reshade::api::resource,const reshade::api::resource_desc&);
// OwnDepth should remain true when enabled but no valid source is available:
// its unbound private semantic suppresses effects instead of using a shadow map.
// keep: copy the depth every present even when no effect wants it, for Take
// (Stereo rendering: By turns); the effects' binding still follows `enabled`.
// reuse: the replay's right eye - its lists ran again past ReShade, so none of
// their binds were seen: the left eye's pick, which now holds its depth.
void Bind(reshade::api::effect_runtime*,reshade::api::command_list*,bool enabled,bool keep,bool reuse,float scale[2]);
// This present's copy from Bind, once: in a shader-resource state, with its
// readable format and the part of it the scene was drawn into (DLSS: a corner).
struct Kept {uint64_t resource=0;uint32_t format=0;float scale[2]{1,1};};
bool Take(Kept& out);
void Shutdown();
}
