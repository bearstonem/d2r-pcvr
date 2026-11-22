#include "../vr/aura_filter.h"
#include <map>
#include <cstring>
#include <cassert>
#include <cstdio>
struct Memory {
    std::map<uint64_t,unsigned char> bytes;unsigned reads=0;
    void Put(uint64_t p,const void* v,size_t n){auto b=static_cast<const unsigned char*>(v);for(size_t i=0;i<n;++i)bytes[p+i]=b[i];}
    template<class T>void Put(uint64_t p,const T& value){Put(p,&value,sizeof value);}
    bool Read(void* v,uint64_t p,size_t n){++reads;auto b=static_cast<unsigned char*>(v);for(size_t i=0;i<n;++i){auto it=bytes.find(p+i);if(it==bytes.end())return false;b[i]=it->second;}return true;}
    void Zero(uint64_t p,size_t n){std::vector<unsigned char> zero(n);Put(p,zero.data(),n);}
};
int main(){
    using namespace aurafilter;Memory m;
    auto read=[&](void* v,uint64_t p,size_t n){return m.Read(v,p,n);};
    const uint64_t base=0x140000000,reg=base+0x2677170;
    auto entity=[](unsigned i){return 0x100000u+i;};
    uint64_t range[]={0x10000,0x10000+8*4};m.Put(reg+0x30,range);
    uint32_t generations[8]{};for(unsigned i=1;i<8;++i)generations[i]=entity(i);m.Put(range[0],generations);
    auto storage=[&](uint64_t at,unsigned stride,const std::vector<unsigned>& indices){
        uint64_t h[9]{};h[1]=at+0x100;h[2]=h[1]+8;h[4]=at+0x800;h[5]=h[4]+indices.size()*4;h[7]=at+0x1000;h[8]=h[7]+indices.size()*stride;m.Put(at,h);
        uint64_t page=at+0x200;m.Put(h[1],page);uint32_t sparse[8];std::fill(std::begin(sparse),std::end(sparse),aurarecipient::NoEntity);
        for(unsigned i=0;i<indices.size();++i){sparse[indices[i]]=i;m.Put(h[4]+i*4,entity(indices[i]));}
        m.Put(page,sparse);m.Zero(h[7],indices.size()*stride);return h[7];
    };
    const uint64_t transforms=0x20000,units=0x30000,effects=0x40000;
    auto tr=storage(transforms,208,{1,2,3,4,5,6,7});auto un=storage(units,16,{2,3,4});auto ef=storage(effects,48,{1,5,6,7});
    for(unsigned i=1;i<8;++i){auto parent=i==1||i==7?entity(2):i==5?entity(3):i==6?entity(4):aurarecipient::NoEntity;m.Put(tr+(i-1)*208+192,parent);}
    uint64_t unitPointers[]={0x50000,0x51000,0x52000};
    for(unsigned i=0;i<3;++i){m.Put(un+i*16+8,unitPointers[i]);uint32_t h[]={i?1u:0u,i?420u:4u,i==0?1u:i==1?7u:8u,1};m.Put(unitPointers[i],h);uint64_t nil=0;m.Put(unitPointers[i]+0x158,nil);m.Put(base+0x2a23910+(h[0]*128ull+(h[2]&127))*8,unitPointers[i]);}
    uint64_t poolRange[]={0x60000,0x60000+120};m.Put(reg,poolRange);m.Zero(poolRange[0],120);m.Put(poolRange[0]+32,transforms);m.Put(poolRange[0]+72,units);m.Put(poolRange[0]+112,effects);
    const char* aura="data/hd/vfx/particles/overlays/paladin/aura_defiance_front/fx_defiance_overlay.particles";
    uint64_t emitters[4]{},definitions[4]{};
    for(unsigned i=0;i<4;++i){
        auto comp=0x70000+i*0x1000ull;auto emitter=comp+0x200;auto def=comp+0x400;auto compiled=comp+0x600;auto path=comp+0x800;
        emitters[i]=emitter;definitions[i]=def;m.Put(ef+i*48,comp);m.Put(comp+16,emitter);
        uint64_t h[10]{};h[0]=base+0x1da1118;h[6]=comp;h[8]=def;m.Put(emitter,h);
        h[0]=base+0x1da0ff0;h[9]=compiled;m.Put(def,h);uint64_t c[]={base+0x1da67f0,0,path};m.Put(compiled,c);uint64_t tag=base+0x1d9d258;m.Put(path,tag);m.Zero(path+32,160);
        const auto text=i==3?"data/hd/vfx/particles/overlays/necro/amplifydamage/curse.particles":aura;m.Put(path+32,text,std::strlen(text)+1);
    }
    Friendly roster[]={{0,4,1},{1,420,7}};const auto original=m.bytes;
    auto snapshot=Build(read,base,9,1000,entity(2),tr+208,roster,2);
    assert(snapshot.units==units&&snapshot.transforms==transforms&&snapshot.entries.size()==2);
    Configure(false,true);Publish(snapshot);
    assert(Hidden(read,base,9,1100,emitters[0],definitions[0]));
    assert(!Hidden(read,base,9,1100,emitters[1],definitions[1]));
    Configure(true,false);Publish(snapshot);
    assert(!Hidden(read,base,9,1100,emitters[0],definitions[0]));
    assert(Hidden(read,base,9,1100,emitters[1],definitions[1]));
    assert(!Hidden(read,base,9,1100,emitters[2],definitions[2])); // enemy
    assert(!Hidden(read,base,9,1100,emitters[3],definitions[3])); // curse on hero
    assert(!Hidden(read,base,10,1100,emitters[1],definitions[1])); // other session
    assert(!Hidden(read,base,9,1700,emitters[1],definitions[1])); // expired roster
    assert(!Hidden(read,base,9,1100,emitters[1],definitions[0])); // wrong definition
    assert(m.bytes==original);
    assert(Build(read,base,9,1000,entity(2),tr+208,roster,1).entries.size()==1); // summon removed from roster
    uint64_t replacement=0x90000;m.Put(ef+48,replacement);
    assert(!Hidden(read,base,9,1100,emitters[1],definitions[1])); // effect component replaced
    m.bytes=original;
    auto otherParent=entity(2);m.Put(tr+4*208+192,otherParent);
    assert(!Hidden(read,base,9,1100,emitters[1],definitions[1])); // recipient reattached
    m.bytes=original;
    generations[5]=0x200005;m.Put(range[0],generations);
    assert(!Hidden(read,base,9,1100,emitters[1],definitions[1])); // entity recycled
    m.bytes=original;uint32_t changed=99;m.Put(unitPointers[1]+8,changed);
    assert(!Hidden(read,base,9,1100,emitters[1],definitions[1])); // ownership changed
    m.bytes=original;
    const char* shout="data/hd/vfx/particles/overlays/common/shout/vfx_shout_overlay.particles";
    auto setAsset=[&](unsigned i,const char* path){m.Zero(0x70000+i*0x1000ull+0x800+32,160);m.Put(0x70000+i*0x1000ull+0x800+32,path,std::strlen(path)+1);};
    setAsset(3,shout);setAsset(1,shout);setAsset(2,shout); // hero, owned summon, enemy
    auto buffs=Build(read,base,9,1000,entity(2),tr+208,roster,2);assert(buffs.entries.size()==3);
    Configure(true,true,false,true);Publish(buffs);
    assert(Hidden(read,base,9,1100,emitters[3],definitions[3]));
    assert(!Hidden(read,base,9,1100,emitters[1],definitions[1]));
    assert(!Hidden(read,base,9,1100,emitters[0],definitions[0])); // auras stay on
    Configure(true,true,true,false);Publish(buffs);
    assert(Hidden(read,base,9,1100,emitters[1],definitions[1]));
    assert(!Hidden(read,base,9,1100,emitters[3],definitions[3]));
    assert(!Hidden(read,base,9,1100,emitters[2],definitions[2])); // enemy shout stays
    Configure(false,false,true,true);Publish(buffs);
    assert(Hidden(read,base,9,1100,emitters[0],definitions[0])); // auras off
    assert(!Hidden(read,base,9,1100,emitters[1],definitions[1])); // shouts stay on
    const char* ward="data/hd/vfx/particles/overlays/warlock/psychic_ward/psychic_ward_overlay.particles";
    setAsset(3,ward);setAsset(1,ward);setAsset(2,ward);
    auto wards=Build(read,base,9,1000,entity(2),tr+208,roster,2);
    Configure(true,true,true,true,true);Publish(wards);
    assert(Hidden(read,base,9,1100,emitters[3],definitions[3])); // local ward only
    assert(!Hidden(read,base,9,1100,emitters[1],definitions[1])); // friendly ward stays
    assert(!Hidden(read,base,9,1100,emitters[2],definitions[2])); // enemy ward stays
    assert(!Hidden(read,base,9,1100,emitters[0],definitions[0])); // aura stays
    setAsset(3,"DATA\\HD\\VFX\\PARTICLES\\OVERLAYS\\WARLOCK\\PSYCHIC_WARD\\PSYCHIC_WARD_ONHIT.PARTICLES");
    Publish(Build(read,base,9,1000,entity(2),tr+208,roster,2));
    assert(Hidden(read,base,9,1100,emitters[3],definitions[3]));
    assert(!psychicward::Asset("data/hd/vfx/particles/overlays/common/psychic_hammer_curse/vfx_psychic_hammer_curse.particles"));
    const char* charges="data/hd/vfx/particles/overlays/common/progressive_fire_2/vfx_progressive_fire_2.particles";
    setAsset(3,charges);setAsset(1,charges);setAsset(2,charges);
    Configure(true,true,true,true,false,2);Publish(Build(read,base,9,1000,entity(2),tr+208,roster,2));
    assert(!Hidden(read,base,9,1100,emitters[3],definitions[3],false)); // Arms: unavailable counts keep original
    assert(Hidden(read,base,9,1100,emitters[3],definitions[3],true));
    assert(!Hidden(read,base,9,1100,emitters[1],definitions[1],true)); // friendly preserved
    assert(!Hidden(read,base,9,1100,emitters[2],definitions[2],true)); // enemy preserved
    Configure(true,true,true,true,false,0);Publish(Build(read,base,9,1000,entity(2),tr+208,roster,2));
    assert(Hidden(read,base,9,1100,emitters[3],definitions[3],false)); // Off does not require counters
    assert(!assassincharges::Asset("data/hd/vfx/particles/overlays/common/progressive_fire_2_attack/vfx_progressive_fire_2_attack.particles"));
    assert(assassincharges::Asset("data/hd/vfx/particles/overlays/common/tigerstrike_3/vfx_assassin_tiger_strike_3.particles"));
    assert(assassincharges::Asset("data/hd/vfx/particles/overlays/assassin/cobrastrike_1/vfx_assassin_cobrastrike_1.particles"));
    Configure(true,true);m.reads=0;assert(!Hidden(read,base,9,1100,emitters[0],definitions[0])&&m.reads==0);
    assert(Build(read,base,9,1000,entity(2),tr+208,nullptr,0).entries.empty());
    std::puts("Aura filter: fresh exact recipients, independent switches, enemy and curse preservation, session/expiry/reuse/ownership rejection, no writes passed");
}
