#pragma once
#include "portal_billboard.h"
#include <array>

// Read-only developer evidence. This is deliberately not a correction allowlist.
// A fixed offset can be authored or computed once when an effect is created.
namespace billboards::shrine {
using portal::Vec;
enum class Kind : uint32_t { Direction, Position, LocalPosition };
enum class Layer : uint32_t { Environment, Symbol };
struct Stream {uint64_t data=0;uint32_t count=0,stride=0;};
inline constexpr uint32_t MaxLanes=8,MaxSamples=1024;
// Reserve most of the capture for floating symbols. A shrine's unrelated flame
// or smoke can otherwise exhaust the trace before its symbol becomes visible.
inline constexpr uint32_t MaxEnvironmentSamples=128;
inline bool Classify(std::string_view path,Layer& layer) {
    if(path.empty()||path.size()>159)return false;
    char normalized[160]{};
    for(size_t i=0;i<path.size();++i) {
        char c=path[i];if(c>='A'&&c<='Z')c=char(c+'a'-'A');if(c=='\\')c='/';
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='/'||c=='_'||c=='.'))return false;
        normalized[i]=c;
    }
    const std::string_view p{normalized};
    constexpr std::string_view symbols[]={
        "shrine_armor/vfx_shrine_armor_looping.particles",
        "shrine_combat/vfx_shrine_combat_looping.particles",
        "shrine_experience/vfx_shrine_experience_looping.particles",
        "shrine_mana_regen/vfx_shrine_mana_regen.particles",
        "shrine_resist_cold/vfx_shrine_resist_cold_overlay.particles",
        "shrine_resist_fire/vfx_shrine_resist_fire.particles",
        "shrine_resist_lightning/vfx_shrine_resist_lightning.particles",
        "shrine_resist_poison/vfx_shrine_resist_poison_looping.particles",
        "shrine_skill/vfx_shrine_skill_looping.particles",
        "shrine_stamina/vfx_shrine_stamina_looping.particles"};
    constexpr std::string_view overlayRoot="data/hd/vfx/particles/overlays/common/";
    if(p.substr(0,overlayRoot.size())==overlayRoot) {
        for(auto symbol:symbols)if(p.substr(overlayRoot.size())==symbol) {
            layer=Layer::Symbol;return true;
        }
        return false;
    }
    layer=Layer::Environment;
    if(p=="data/hd/vfx/particles/objects/env_stone/forest_altar/vfx_forest_altar.particles")return true;
    constexpr std::string_view roots[]={"data/hd/vfx/particles/objects/shrines_other/",
        "data/hd/vfx/particles/objects/shrines_healthmana/"};
    if(p.find("..")!=p.npos)return false;
    for(auto root:roots)if(p.substr(0,root.size())==root) {
        const auto tail=p.substr(root.size());const auto slash=tail.find('/');
        return slash!=tail.npos&&slash>0&&tail.find('/',slash+1)==tail.npos&&
            tail.size()>slash+1+10&&tail.substr(tail.size()-10)==".particles";
    }
    return false;
}
inline bool Asset(std::string_view path) {Layer layer{};return Classify(path,layer);}
inline bool Streams(Kind kind,const Stream& out,const Stream& in) {
    const bool local=kind==Kind::LocalPosition;
    if(!out.data||!in.data||!out.count||out.count>(local?64u:1024u)||out.stride!=12||!in.count)return false;
    if(local)return (in.stride==0||in.stride==12)&&(!in.stride||in.count>=out.count);
    return in.count==out.count&&(in.stride==0||in.stride==4);
}
struct Sample {
    portal::Instance instance{};
    uint64_t output=0,stream=0;
    uint32_t lane=0,count=0,stride=0,serial=0;
    Kind kind=Kind::Direction;
    Layer layer=Layer::Environment;
    int32_t view=-1;
    Vec head{},nativeEye{},local{};
    bool cameraMatches=false;
    float matrix[16]{};
    char path[160]{};
};
// No native writes, allocation, unbounded strings or speculative offsets. Reads
// use the same guarded instance/path layout as the signature-checked portal hook.
template<class Read> bool Capture(Read read,const portal::Context& c,uint64_t base,
        Kind kind,const Stream& out,const Stream& in,uint32_t lane,Vec head,Sample& sample) {
    if(!Streams(kind,out,in)||lane>=out.count||!portal::Finite(head))return false;
    Sample s{};s.kind=kind;s.lane=lane;s.count=out.count;s.stride=in.stride;s.stream=in.data;s.head=head;
    if(!portal::Resolve(read,c,lane,base,s.instance)||
       !portal::DefinitionPath(read,s.instance.definition,base,s.path)||!Classify(s.path,s.layer))return false;
    if(!read(s.matrix,s.instance.transform,sizeof s.matrix))return false;
    for(float f:s.matrix)if(!std::isfinite(f))return false;
    if(std::fabs(s.matrix[3])>1e-5f||std::fabs(s.matrix[7])>1e-5f||
       std::fabs(s.matrix[11])>1e-5f||std::fabs(s.matrix[15]-1)>1e-5f)return false;
    s.output=out.data+12ull*lane;
    if(kind==Kind::LocalPosition) {
        if(!read(&s.local,in.data+uint64_t(in.stride)*lane,sizeof s.local)||!portal::Finite(s.local))return false;
    } else {
        struct Views {uint64_t data;uint32_t count,capacity;} views{};
        if(!read(&s.view,in.data+uint64_t(in.stride)*lane,4)||s.view<0||
           !read(&views,c.scene+0x310,sizeof views)||!views.data||!views.count||views.count>8||uint32_t(s.view)>=views.count||
           !read(&s.nativeEye,views.data+uint64_t(s.view)*0x1f8+0x70,sizeof s.nativeEye)||!portal::Finite(s.nativeEye))return false;
        const float x=s.nativeEye.x-head.x,y=s.nativeEye.y-head.y,z=s.nativeEye.z-head.z;
        s.cameraMatches=std::isfinite(x*x+y*y+z*z)&&x*x+y*y+z*z<=4.f;
    }
    sample=s;return true;
}
// Caller serializes this bounded table with a try-lock. Distinct streams/lanes
// retain separate samples (a one-lane orb must not hide a two-lane glow).
struct Budget {
    struct Entry {uint64_t instance=0,stream=0,at=0;uint32_t lane=0;Kind kind=Kind::Direction;};
    std::array<Entry,64> entries{};uint32_t used=0,environmentUsed=0,next=0;
    bool Claim(Sample& s,uint64_t now) {
        if(used>=MaxSamples||(s.layer==Layer::Environment&&environmentUsed>=MaxEnvironmentSamples))return false;
        Entry* slot=nullptr;
        for(auto& e:entries)if(e.instance==s.instance.address&&e.stream==s.stream&&e.kind==s.kind&&e.lane==s.lane) {
            if(now<e.at||now-e.at<500)return false;
            slot=&e;break;
        }
        if(!slot)slot=&entries[next++%entries.size()];
        *slot={s.instance.address,s.stream,now,s.lane,s.kind};s.serial=++used;
        if(s.layer==Layer::Environment)++environmentUsed;
        return true;
    }
};
}
