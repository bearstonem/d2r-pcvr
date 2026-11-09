#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "world_coordinates.h"

namespace framevr::immunity {
// Order shared with D2R_Immunity.fxh: physical, magic, fire, lightning, cold, poison.
inline constexpr std::array<int,6> Stats{36,37,39,41,43,45};
inline constexpr size_t MaxMonsters=24;
inline constexpr float RangeCells=32.0f;
inline constexpr uint64_t ScanMs=100,MaxAgeMs=500;
struct Result {bool known=false;uint8_t mask=0;};
// Full current unit stats, not MonStats defaults. Unknown is never treated as
// a confirmed immunity. No skill states or resistance values are changed.
template<class Read> Result Detect(Read read) {
    Result result{true,0};
    for(size_t i=0;i<Stats.size();++i) {
        int value=0;
        if(!read(Stats[i],value))return {};
        if(value>=100)result.mask|=uint8_t(1u<<i);
    }
    return result;
}
struct Monster {
    uint32_t id=0;
    float x=0,y=0,previousX=0,previousY=0;
    uint8_t mask=0;
    float height=0,previousHeight=0; // rendered root height, independent of the player
};
struct Snapshot {
    std::array<Monster,MaxMonsters> monsters{};
    size_t count=0;
    uint64_t session=0,at=0,interval=ScanMs;
};
inline bool Fresh(const Snapshot& s,uint64_t session,uint64_t now) {
    return session&&s.session==session&&s.at&&now>=s.at&&now-s.at<=MaxAgeMs;
}
inline void Insert(Snapshot& s,Monster m,float heroX,float heroY) {
    if(!m.mask||(m.mask&~63u)||!std::isfinite(m.x)||!std::isfinite(m.y)||!std::isfinite(m.height))return;
    const auto distance=[&](const Monster& a){const float x=a.x-heroX,y=a.y-heroY;return x*x+y*y;};
    const float d=distance(m);
    if(!std::isfinite(d)||d>RangeCells*RangeCells)return;
    size_t at=std::min(s.count,MaxMonsters);
    while(at&&distance(s.monsters[at-1])>d)--at;
    if(at>=MaxMonsters)return;
    for(size_t i=std::min(s.count,MaxMonsters-1);i>at;--i)s.monsters[i]=s.monsters[i-1];
    s.monsters[at]=m;
    s.count=std::min(s.count+1,MaxMonsters);
}
inline void Connect(Snapshot& current,const Snapshot& previous) {
    current.interval=ScanMs;
    const bool continuous=Fresh(previous,current.session,current.at)&&current.at>previous.at;
    if(continuous)current.interval=std::clamp(current.at-previous.at,uint64_t(1),MaxAgeMs);
    for(size_t i=0;i<current.count;++i) {
        auto& m=current.monsters[i];m.previousX=m.x;m.previousY=m.y;m.previousHeight=m.height;
        if(!continuous)continue;
        for(size_t j=0;j<previous.count;++j) {
            const auto& p=previous.monsters[j];
            // A teleport or an ID reused far away must not sweep an orbit across the map.
            if(p.id==m.id&&std::hypot(m.x-p.x,m.y-p.y)<=6.0f&&std::abs(m.height-p.height)<=12.0f) {
                m.previousX=p.x;m.previousY=p.y;m.previousHeight=p.height;break;
            }
        }
    }
}
inline std::array<float,2> Position(const Monster& m,const Snapshot& s,uint64_t now) {
    const float t=now>=s.at?std::min(1.0f,float(now-s.at)/float(std::max(uint64_t(1),s.interval))):1.0f;
    return {m.previousX+(m.x-m.previousX)*t,m.previousY+(m.y-m.previousY)*t};
}
inline coordinates::Vec FootPosition(const Monster& m,const Snapshot& s,uint64_t now,coordinates::Vec hero) {
    const auto xy=Position(m,s,now);
    const float t=now>=s.at?std::min(1.0f,float(now-s.at)/float(std::max(uint64_t(1),s.interval))):1.0f;
    return coordinates::RenderGround({xy[0]*2,0,xy[1]*2},m.previousHeight+(m.height-m.previousHeight)*t)-hero;
}
}
