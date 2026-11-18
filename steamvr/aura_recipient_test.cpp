#include "../vr/aura_recipient.h"
#include "../vr/aura_assets.h"
#include <map>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
int main(){
    using namespace aurarecipient;
    assert(AuraAsset("data/hd/vfx/particles/overlays/paladin/aura_defiance_front/FX_Defiance_Overlay.particles"));
    assert(AuraAsset("data/hd/vfx/particles/overlays/common/oak_sage/vfx_oaksage_overlay.particles"));
    assert(ShoutAsset("data/hd/vfx/particles/overlays/common/shout/vfx_shout_overlay.particles"));
    assert(ShoutAsset("data/hd/vfx/particles/overlays/common/battleorders/vfx_battle_orders.particles"));
    assert(ShoutAsset("data/hd/vfx/particles/overlays/common/battlecommandcast/vfx_battle_command_start.particles"));
    assert(!ShoutAsset("data/hd/vfx/particles/overlays/necro/amplifydamage/curse.particles"));
    assert(!ShoutAsset("data/hd/vfx/particles/overlays/paladin/aura_defiance_front/fx_defiance_overlay.particles"));
    assert(!AuraAsset("data/hd/vfx/particles/overlays/paladin/charge/vfx_charge_overlay.particles"));
    assert(!AuraAsset("data/hd/vfx/particles/overlays/paladin/hit_thorns/fx_hit_thorn_overlay.particles"));
    assert(!AuraAsset("data/hd/vfx/particles/overlays/common/shout/vfx_shout_overlay.particles"));
    assert(!AuraAsset("data/hd/vfx/particles/overlays/necro/amplifydamage/test.particles"));
    std::map<uint64_t,std::vector<unsigned char>> memory;
    auto write=[&](uint64_t p,const void* v,size_t n){memory[p]=std::vector<unsigned char>((const unsigned char*)v,(const unsigned char*)v+n);};
    auto read=[&](void* v,uint64_t p,size_t n){
        auto it=memory.upper_bound(p);if(it==memory.begin())return false;--it;
        const auto offset=p-it->first;if(offset>it->second.size()||n>it->second.size()-offset)return false;
        std::memcpy(v,it->second.data()+offset,n);return true;
    };
    const uint64_t registry=0x10000,transforms=0x20000,units=0x21000;
    const uint32_t child=0x100001,parent=0x200002;
    uint64_t range[]={0x30000,0x3000c};write(registry+0x30,range,sizeof range);
    uint32_t generations[]={0,child,parent};write(0x30000,generations,sizeof generations);
    auto storage=[&](uint64_t at,uint64_t base,unsigned stride,bool onlyParent){
        uint64_t h[9]{};h[1]=base;h[2]=base+8;h[4]=base+0x1000;h[5]=h[4]+(onlyParent?4:8);h[7]=base+0x2000;h[8]=h[7]+(onlyParent?1:2)*stride;write(at,h,sizeof h);
        uint64_t page=base+0x100;write(base,&page,8);
        uint32_t sparse[]={NoEntity,onlyParent?NoEntity:0,onlyParent?0:1};write(page,sparse,sizeof sparse);
        if(onlyParent)write(h[4],&parent,4);else {uint32_t ids[]={child,parent};write(h[4],ids,sizeof ids);}
        std::vector<unsigned char> payload((onlyParent?1:2)*stride);write(h[7],payload.data(),payload.size());return h[7];
    };
    const auto tr=storage(transforms,0x40000,208,false),un=storage(units,0x50000,16,true);
    std::vector<unsigned char> matrices(416);std::memcpy(matrices.data()+192,&parent,4);uint32_t none=NoEntity;std::memcpy(matrices.data()+208+192,&none,4);write(tr,matrices.data(),matrices.size());
    uint64_t unit=0x60000;unsigned char owner[16]{};std::memcpy(owner+8,&unit,8);write(un,owner,sizeof owner);
    uint32_t header[]={1,420,7,1};write(unit,header,sizeof header);
    const auto original=memory;Unit result{};
    assert(Resolve(read,registry,transforms,units,child,result)&&result.id==7);
    Friendly friends[]={{0,4,1},{1,420,7}};
    assert(Classify(result,friends,2)==Kind::Summon);
    result.id=8;assert(Classify(result,friends,2)==Kind::Unknown); // enemy/unknown
    result={unit,0,4,1};assert(Classify(result,friends,2)==Kind::Player);
    assert(memory==original);
    generations[1]=0x200001;write(0x30000,generations,sizeof generations);
    assert(!Resolve(read,registry,transforms,units,child,result)); // recycled generation
    generations[1]=child;write(0x30000,generations,sizeof generations);
    memory.erase(un);assert(!Resolve(read,registry,transforms,units,child,result));
    memory=original;std::memcpy(matrices.data()+192,&child,4);write(tr,matrices.data(),matrices.size());
    assert(!Resolve(read,registry,transforms,units,child,result)); // cycle
    memory=original;header[0]=4;write(unit,header,sizeof header);
    assert(!Resolve(read,registry,transforms,units,child,result)); // item is not recipient
    std::puts("Aura recipient: exact hierarchy, friendly IDs, enemies unknown, stale generations, missing reads, cycles, types, read-only passed");
}
