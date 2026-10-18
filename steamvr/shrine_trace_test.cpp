#include "../vr/shrine_trace.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <vector>
using namespace billboards;
void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct Memory {
    std::map<uint64_t,std::vector<unsigned char>> blocks;
    bool Read(void* out,uint64_t address,size_t n) const {
        auto i=blocks.upper_bound(address);if(i==blocks.begin())return false;--i;
        const uint64_t offset=address-i->first;
        if(offset>i->second.size()||n>i->second.size()-offset)return false;
        std::memcpy(out,i->second.data()+offset,n);return true;
    }
    template<class T>void Put(uint64_t at,const T& value){std::vector<unsigned char> b(sizeof value);std::memcpy(b.data(),&value,sizeof value);blocks[at]=b;}
};
int main() {
    constexpr char forest[]="data/hd/vfx/particles/objects/env_stone/forest_altar/vfx_forest_altar.particles";
    Check(shrine::Asset(forest),"forest altar lives outside the two shrine directories");
    Check(shrine::Asset("DATA\\HD\\VFX\\PARTICLES\\OBJECTS\\SHRINES_OTHER\\STELE_SHRINE\\VFX_STELE_SHRINE.PARTICLES"),"case and separators");
    Check(shrine::Asset("data/hd/vfx/particles/objects/shrines_healthmana/shrine_mana/vfx_shrine_mana.particles"),"health/mana comparison can be traced");
    constexpr char mana[]="data/hd/vfx/particles/overlays/common/shrine_mana_regen/vfx_shrine_mana_regen.particles";
    for(auto suffix:{"shrine_armor/vfx_shrine_armor_looping.particles",
        "shrine_combat/vfx_shrine_combat_looping.particles",
        "shrine_experience/vfx_shrine_experience_looping.particles",
        "shrine_mana_regen/vfx_shrine_mana_regen.particles",
        "shrine_resist_cold/vfx_shrine_resist_cold_overlay.particles",
        "shrine_resist_fire/vfx_shrine_resist_fire.particles",
        "shrine_resist_lightning/vfx_shrine_resist_lightning.particles",
        "shrine_resist_poison/vfx_shrine_resist_poison_looping.particles",
        "shrine_skill/vfx_shrine_skill_looping.particles",
        "shrine_stamina/vfx_shrine_stamina_looping.particles"}) {
        char path[160];std::snprintf(path,sizeof path,"data/hd/vfx/particles/overlays/common/%s",suffix);
        shrine::Layer layer{};
        Check(shrine::Classify(path,layer)&&layer==shrine::Layer::Symbol,"floating shrine symbols included despite overlay directory");
        Check(!portal::Asset(path)&&!portal::CenterAsset(path),"symbol capture never enables portal writes");
    }
    shrine::Layer layer=shrine::Layer::Symbol;
    Check(shrine::Classify(forest,layer)&&layer==shrine::Layer::Environment,"built-in shrine effects identified separately");
    Check(shrine::Classify("DATA\\HD\\VFX\\PARTICLES\\OVERLAYS\\COMMON\\SHRINE_MANA_REGEN\\VFX_SHRINE_MANA_REGEN.PARTICLES",layer)&&layer==shrine::Layer::Symbol,"symbol case and separators");
    for(auto p:{"data/hd/vfx/particles/overlays/common/shrine_armor/other.particles",
        "data/hd/vfx/particles/overlays/common/shrine_unknown/vfx_shrine_unknown.particles",
        "data/hd/vfx/particles/overlays/common/shrine_armor/vfx_shrine_armor_looping.particles.extra",
        "data/hd/vfx/particles/overlays/common/shrine_armor/../vfx_shrine_armor_looping.particles",
        "data/hd/vfx/particles/overlays/common/poison/vfx_poison.particles",
        "data/hd/vfx/particles/objects/env_stone/other/vfx_other.particles",
        "data/hd/vfx/particles/objects/env_stone/forest_altar/new_variant.particles",
        "data/hd/vfx/particles/objects/shrines_other/../other.particles",
        "data/hd/vfx/particles/objects/shrines_other/a/b/c.particles",
        "data/hd/vfx/particles/objects/shrines_other/a/.particles",
        "data/hd/vfx/particles/objects/shrines_other/a/b.particles\n",
        "data/hd/vfx/particles/objects/vfx_only/town_portal/vfx_town_portal_NewStuff.particles"})
        Check(!shrine::Asset(p),"unrelated overlays, paths and malformed names excluded");
    Check(!portal::Asset(forest)&&!portal::CenterAsset(forest),"shrine trace never widens portal corrections");
    constexpr uint64_t base=0x140000000,scene=0x10000,pages=0x20000,page=0x30000,instance=0x40000,definition=0x50000,transform=0x60000,compiled=0x70000,path=0x80000;
    Memory m;auto read=[&](void* out,uint64_t at,size_t n){return m.Read(out,at,n);};
    std::array<uint64_t,3> pool{pages,0,(uint64_t(512)<<32)|1};m.Put(scene+0x230,pool);
    m.Put(pages,page);std::array<uint64_t,512> slots{};slots[7]=instance;m.Put(page,slots);
    std::array<uint64_t,10> ih{};ih[0]=base+0x1da1118;ih[8]=definition;ih[9]=transform;m.Put(instance,ih);
    std::array<uint64_t,10> dh{};dh[0]=base+0x1da0ff0;dh[9]=compiled;m.Put(definition,dh);
    std::array<uint64_t,3> ch{base+0x1da67f0,1,path};m.Put(compiled,ch);
    m.Put(path,base+0x1d9d258);std::array<char,160> name{};std::memcpy(name.data(),forest,sizeof forest);m.Put(path+32,name);
    std::array<float,16> matrix{0,0,-1,0,0,1,0,0,1,0,0,0,100,2,200,1};m.Put(transform,matrix);
    portal::Context c{};c.scene=scene;c.instance=7;
    shrine::Stream out{0x90000,2,12},in{0xa0000,2,4};std::array<int32_t,2> indices{0,1};m.Put(in.data,indices);
    struct Views {uint64_t data;uint32_t count,capacity;} views{0xb0000,2,2};m.Put(scene+0x310,views);
    m.Put(views.data+0x70,portal::Vec{10,6,20});m.Put(views.data+0x1f8+0x70,portal::Vec{99,99,99});
    shrine::Sample s;
    const auto before=m.blocks;
    Check(shrine::Capture(read,c,base,shrine::Kind::Direction,out,in,0,{10,6,20},s)&&s.view==0&&s.cameraMatches&&s.matrix[12]==100&&s.output==out.data,"captures identity, transform and matched native camera");
    Check(shrine::Capture(read,c,base,shrine::Kind::Position,out,in,1,{10,6,20},s)&&s.view==1&&!s.cameraMatches,"mismatched cameras remain visible as diagnostic evidence");
    Check(m.blocks==before,"capture never writes native memory or script values");
    name.fill(0);std::memcpy(name.data(),mana,sizeof mana);m.Put(path+32,name);
    const auto symbolBefore=m.blocks;
    Check(shrine::Capture(read,c,base,shrine::Kind::Direction,out,in,0,{10,6,20},s)&&s.layer==shrine::Layer::Symbol&&s.cameraMatches,"captures floating mana symbol with native camera and origin");
    Check(m.blocks==symbolBefore,"symbol tracing remains read-only even for player-attached instance");
    name.fill(0);std::memcpy(name.data(),forest,sizeof forest);m.Put(path+32,name);
    indices[1]=8;m.Put(in.data,indices);Check(!shrine::Capture(read,c,base,shrine::Kind::Position,out,in,1,{10,6,20},s),"invalid native view rejected");
    in.stride=0;Check(shrine::Capture(read,c,base,shrine::Kind::Direction,out,in,1,{10,6,20},s)&&s.view==0,"constant view stream");
    in={0xa0000,1,0};m.Put(in.data,portal::Vec{3,4,5});
    Check(shrine::Capture(read,c,base,shrine::Kind::LocalPosition,out,in,1,{10,6,20},s)&&s.local.x==3&&s.local.y==4&&s.local.z==5&&s.view==-1,"constant local input copied before native call");
    m.Put(in.data,portal::Vec{6,7,8});Check(s.local.x==3,"snapshot survives native input/output aliasing");
    in={0xa0000,2,12};std::array<portal::Vec,2> local{portal::Vec{3,4,5},portal::Vec{6,7,8}};m.Put(in.data,local);
    Check(shrine::Capture(read,c,base,shrine::Kind::LocalPosition,out,in,1,{10,6,20},s)&&s.local.x==6,"strided local input");
    c.instance=-1;c.count=2;c.indices=0xc0000;std::array<int32_t,2> ids{7,8};m.Put(c.indices,ids);
    auto other=ih;other[8]=definition+0x1000;m.Put(instance+0x1000,other);slots[8]=instance+0x1000;m.Put(page,slots);
    Check(shrine::Capture(read,c,base,shrine::Kind::LocalPosition,out,in,0,{10,6,20},s)&&
        !shrine::Capture(read,c,base,shrine::Kind::LocalPosition,out,in,1,{10,6,20},s),"mixed batch classification never leaks to unrelated lane");
    Check(!shrine::Capture(read,c,base,shrine::Kind::LocalPosition,out,in,2,{10,6,20},s),"lane bounds");
    out.count=65;Check(!shrine::Streams(shrine::Kind::LocalPosition,out,in),"local batch bound");out.count=2;
    in.stride=8;Check(!shrine::Streams(shrine::Kind::LocalPosition,out,in),"unknown stride");in.stride=12;
    in.count=1;Check(!shrine::Streams(shrine::Kind::LocalPosition,out,in),"truncated input");in.count=2;
    matrix[15]=0;m.Put(transform,matrix);Check(!shrine::Capture(read,c,base,shrine::Kind::LocalPosition,out,in,0,{10,6,20},s),"non-affine transform rejected");matrix[15]=1;m.Put(transform,matrix);
    local[0].x=std::numeric_limits<float>::infinity();m.Put(in.data,local);Check(!shrine::Capture(read,c,base,shrine::Kind::LocalPosition,out,in,0,{10,6,20},s),"nonfinite input rejected");
    local[0].x=3;m.Put(in.data,local);ih[0]=0;m.Put(instance,ih);Check(!shrine::Capture(read,c,base,shrine::Kind::LocalPosition,out,in,0,{10,6,20},s),"wrong instance vtable rejected");ih[0]=base+0x1da1118;m.Put(instance,ih);
    name.fill('x');m.Put(path+32,name);Check(!shrine::Capture(read,c,base,shrine::Kind::LocalPosition,out,in,0,{10,6,20},s),"unterminated path rejected");
    m.blocks.erase(compiled);char buffer[160];Check(!portal::DefinitionPath(read,definition,base,buffer),"unreadable path fails closed");
    shrine::Budget budget;s.instance.address=instance;s.stream=0x90000;s.lane=0;s.kind=shrine::Kind::LocalPosition;
    Check(budget.Claim(s,1000)&&s.serial==1&&!budget.Claim(s,1499)&&budget.Claim(s,1500),"per-stream sample rate bound");
    s.stream++;Check(budget.Claim(s,1500),"distinct orb/glow streams retained");
    s.lane++;Check(budget.Claim(s,1500),"distinct lanes retained");
    s.kind=shrine::Kind::Direction;Check(budget.Claim(s,1500),"view and local samples independent");
    for(uint32_t i=budget.used;i<shrine::MaxEnvironmentSamples;++i){s.stream++;Check(budget.Claim(s,2000),"bounded environment table eviction");}
    s.stream++;Check(!budget.Claim(s,9999)&&budget.used==shrine::MaxEnvironmentSamples,"smoke cannot consume symbol reserve");
    s.layer=shrine::Layer::Symbol;
    for(uint32_t i=budget.used;i<shrine::MaxSamples;++i){s.stream++;Check(budget.Claim(s,2000),"symbols use remaining capture budget");}
    s.stream++;Check(!budget.Claim(s,99999)&&budget.used==shrine::MaxSamples,"process lifetime budget never wraps or refills");
    shrine::Budget symbolsOnly;
    for(uint32_t i=0;i<shrine::MaxSamples;++i){s.stream++;Check(symbolsOnly.Claim(s,2000),"unused environment budget available to symbols");}
    Check(symbolsOnly.environmentUsed==0&&!symbolsOnly.Claim(s,99999),"symbol-only capture remains bounded");
    std::puts("Shrine tracing: asset scope, guarded read-only captures, mixed lanes and bounded sampling passed");
}
