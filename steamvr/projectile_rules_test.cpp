#include "../vr/projectile_rules.h"
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <limits>
#include <cstring>
#include "bow_flight.h"
int main() {
    using namespace projectiles;
    // Synthetic native instance: the VFX child is visited BEFORE the root;
    // neither has a TrackReleasePoint component (as in Ice Bolt).
    std::array<unsigned char,0x2000> memory{};
    const auto put=[&](uintptr_t at,auto value){std::memcpy(memory.data()+at,&value,sizeof value);};
    const auto read=[&](uintptr_t at,auto& value){
        if(at>memory.size()||sizeof value>memory.size()-at)return false;
        std::memcpy(&value,memory.data()+at,sizeof value);return true;
    };
    constexpr uintptr_t definition=0x100,instance=0x200,storage=0x300,entities=0x400,pages=0x500,page=0x600,packed=0xb00,components=0xc00;
    constexpr uint32_t root=12,child=13;
    put(definition,instance);put(instance+0x10,entities);put(instance+0x18,uintptr_t(2));
    put(entities,child);put(entities+4,root);
    put(storage+8,pages);put(storage+16,pages+8);put(pages,page);
    put(page+root*4,uint32_t(0));put(page+child*4,uint32_t(1));
    put(storage+0x20,packed);put(storage+0x28,packed+8);put(storage+0x38,components);
    put(packed,root);put(packed+4,child);
    put(components+0xc0,UINT32_MAX);put(components+0xd0+0xc0,root);
    assert(!VisualRoot(definition,storage,child,read)&&VisualRoot(definition,storage,root,read));
    framevr::bowflight::VisualBinding binding;
    assert(!binding.BindRoot(child,VisualRoot(definition,storage,child,read)));
    assert(binding.BindRoot(root,VisualRoot(definition,storage,root,read))&&binding.Matches(root));
    assert(!binding.BindRoot(child,false)&&!binding.Matches(child));
    put(components+0xc0,uint32_t(30)); // game wrapper outside the asset instance
    assert(VisualRoot(definition,storage,root,read));
    put(packed,root|0x100000u);assert(!VisualRoot(definition,storage,root,read));
    put(packed,root);put(instance+0x18,uintptr_t(65));assert(!VisualRoot(definition,storage,root,read));
    put(instance+0x18,uintptr_t(0));assert(!VisualRoot(definition,storage,root,read));
    put(instance+0x18,uintptr_t(2));assert(!VisualRoot(definition,storage,14,read));
    put(storage+0x38,uintptr_t(0x2000));assert(!VisualRoot(definition,storage,root,read));
    assert(!VisualRoot(definition,storage,UINT32_MAX,read));
    // Zero is a legitimate local player id. Monster ids may collide with it.
    assert(SamePlayer(0,0,0,0));
    assert(!SamePlayer(0,0,1,0));
    assert(!SamePlayer(0,7,0,8));
    assert(!SamePlayer(0,UINT32_MAX,0,UINT32_MAX));
    // Normal native arrow velocity is fixed-point. Retain a slowed baseline;
    // don't replace it with a magic hard-coded speed or accelerate stopped paths.
    assert(ScaledVelocity(24<<8,2)==48<<8);
    assert(ScaledVelocity(12<<8,2)==24<<8);
    assert(ScaledVelocity(0,2)==0);
    assert(ScaledVelocity(-1,2)==-1);
    assert(ScaledVelocity(6000,1)==6000);
    assert(ScaledVelocity(6000,std::numeric_limits<float>::quiet_NaN())==6000);
    assert(ScaledVelocity(6000,4)==6000);
    assert(ScaledVelocity(INT32_MAX,2)==INT32_MAX);
    assert(ScaledVelocity(14<<8,5,5)==70<<8);
    assert(ScaledVelocity(7<<8,3,5)==21<<8); // retain native slowing
    assert(ScaledVelocity(6000,6,5)==6000);
    constexpr uintptr_t params=0x100,mageOwner=0x200;
    put(params+8,mageOwner);put(mageOwner,uint32_t(1));put(mageOwner+4,uint32_t(364));
    for(uint32_t id=208;id<=211;++id){put(params+0x20,id);assert(MageMissile(params,read)==id);}
    put(params+0x20,uint32_t(207));assert(MageMissile(params,read)==UINT32_MAX);
    put(params+0x20,uint32_t(212));assert(MageMissile(params,read)==UINT32_MAX);
    put(params+0x20,uint32_t(208));
    put(mageOwner+4,uint32_t(359));assert(MageMissile(params,read)==UINT32_MAX); // merc
    put(mageOwner+4,uint32_t(274));assert(MageMissile(params,read)==UINT32_MAX); // hostile mage
    put(mageOwner+4,uint32_t(364));put(mageOwner,uint32_t(0));assert(MageMissile(params,read)==UINT32_MAX);
    put(params+8,uintptr_t(0));assert(MageMissile(params,read)==UINT32_MAX);
    put(params+8,uintptr_t(0x2000));assert(MageMissile(params,read)==UINT32_MAX);
    puts("Projectile ownership/velocity tests passed");
}
