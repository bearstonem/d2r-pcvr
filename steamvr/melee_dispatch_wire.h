#pragma once
#include "melee_contact.h"
#include <bit>
#include <cstddef>

namespace framevr::melee::dispatch {
// Values from PCVR's existing detector, not a replacement detector or client damage.
// Session/sequence are opaque sender identities; timestamps are never compared across PCs.
struct Contact {
    uint32_t hero=0;uint64_t sequence=0,session=0;
    Sample sample{};Sweep sweep{};
    uint32_t cycle[2]{};int32_t shape=0;uint64_t dualSpeed=0;
};
inline constexpr uint32_t Version=1;
inline constexpr size_t Size=204;
using Bytes=std::array<uint8_t,Size>;
inline bool Valid(const Contact& c) {
    const auto finite=[](Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
    const auto blade=[&](Blade b){return finite(b.base)&&finite(b.tip)&&std::isfinite(b.radius)&&b.radius>=0&&b.radius<=1;};
    const auto& s=c.sample;
    return c.hero&&c.sequence&&c.session&&c.sweep.stroke&&s.active&&s.handIndex>=0&&s.handIndex<2&&
        blade(c.sweep.before)&&blade(c.sweep.after)&&finite(s.player)&&std::isfinite(s.unitsPerMetre)&&s.unitsPerMetre>0&&
        std::isfinite(c.sweep.speed)&&c.sweep.speed>=0&&std::isfinite(c.sweep.travel)&&c.sweep.travel>=0&&
        std::isfinite(s.reach)&&s.reach>=0&&s.reach<=.5f&&std::isfinite(s.heft)&&s.heft>=0&&
        std::isfinite(s.weight)&&s.weight>=0&&s.weight<=1&&!(s.reactions&~3u)&&
        s.skillSlot>=-1&&s.skillSlot<32&&s.strikeSkill>=-1&&c.shape>=0&&c.shape<=2;
}
inline bool Encode(const Contact& c,Bytes& out) {
    if(!Valid(c))return false;out={};size_t n=0;
    const auto u32=[&](uint32_t x){for(int k=0;k<4;++k)out[n++]=uint8_t(x>>(8*k));};
    const auto u64=[&](uint64_t x){u32(uint32_t(x));u32(uint32_t(x>>32));};
    const auto f=[&](float x){u32(std::bit_cast<uint32_t>(x));};
    const auto v=[&](Vec x){f(x.x);f(x.y);f(x.z);};
    const auto b=[&](Blade x){v(x.base);v(x.tip);f(x.radius);};
    const auto& s=c.sample;
    u32(Version);u32(c.hero);u64(c.sequence);u64(c.session);u64(c.sweep.stroke);
    b(c.sweep.before);b(c.sweep.after);f(c.sweep.speed);f(c.sweep.travel);
    u64(s.epoch);u32(s.equipment);v(s.player);f(s.unitsPerMetre);
    for(auto x:s.weapons.id)u32(x);for(auto x:s.weapons.type)u32(x);for(auto x:s.weapons.displayed)u32(x);
    u32(uint32_t(s.skillSlot));u32(uint32_t(s.skillOwner));u32(uint32_t(s.strikeSkill));u64(s.skillContext);
    f(s.reach);f(s.heft);f(s.weight);u32(c.cycle[0]);u32(c.cycle[1]);u32(uint32_t(c.shape));
    u32(uint32_t(s.active)|(uint32_t(s.shield)<<1)|(uint32_t(s.claws)<<2)|(uint32_t(c.sweep.thrust)<<3)|
        (uint32_t(s.bowBash)<<4)|(uint32_t(s.handIndex)<<5)|(uint32_t(s.reactions)<<8));
    u64(c.dualSpeed);return n==Size;
}
inline bool Decode(const void* data,size_t size,Contact& result) {
    if(!data||size!=Size)return false;const auto* p=static_cast<const uint8_t*>(data);size_t n=0;Contact c;
    const auto u32=[&](){uint32_t x=0;for(int k=0;k<4;++k)x|=uint32_t(p[n++])<<(8*k);return x;};
    const auto u64=[&](){const uint64_t lo=u32();return lo|(uint64_t(u32())<<32);};
    const auto f=[&](){return std::bit_cast<float>(u32());};
    const auto v=[&](){const float x=f(),y=f(),z=f();return Vec{x,y,z};};
    const auto b=[&](){const Vec base=v(),tip=v();const float radius=f();return Blade{base,tip,radius};};
    if(u32()!=Version)return false;c.hero=u32();c.sequence=u64();c.session=u64();c.sweep.stroke=u64();
    c.sweep.before=b();c.sweep.after=b();c.sweep.speed=f();c.sweep.travel=f();auto& s=c.sample;
    s.epoch=u64();s.equipment=u32();s.player=v();s.unitsPerMetre=f();
    for(auto& x:s.weapons.id)x=u32();for(auto& x:s.weapons.type)x=u32();for(auto& x:s.weapons.displayed)x=u32();
    s.skillSlot=std::bit_cast<int32_t>(u32());s.skillOwner=std::bit_cast<int32_t>(u32());s.strikeSkill=std::bit_cast<int32_t>(u32());s.skillContext=u64();
    s.reach=f();s.heft=f();s.weight=f();c.cycle[0]=u32();c.cycle[1]=u32();c.shape=std::bit_cast<int32_t>(u32());
    const auto flags=u32();if(flags&~0x33fu)return false;
    s.active=flags&1;s.shield=flags&2;s.claws=flags&4;c.sweep.thrust=flags&8;s.bowBash=flags&16;s.handIndex=(flags>>5)&1;s.reactions=uint8_t(flags>>8);
    c.dualSpeed=u64();s.blade=c.sweep.after;
    if(n!=Size||!Valid(c))return false;result=c;return true;
}
}
