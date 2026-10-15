#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace framevr::navigation {
// Value-only UI snapshot. Never retain a native Levels row or player pointer.
struct Location {
    int level=0,act=0; // act is displayed as 1..5; zero means unavailable
    char name[256]{};
    bool operator==(const Location&) const=default;
};
constexpr int AtlasX=768,AtlasY=400,Width=256,Height=80;
inline const char* ActLabel(int act) {
    constexpr const char* labels[]={"ACT --","ACT I","ACT II","ACT III","ACT IV","ACT V"};
    return labels[act>=1&&act<=5?act:0];
}
inline const char* AreaLabel(const Location& location) {
    return location.name[0]?location.name:"Location unavailable";
}
// D2R 3.3.93787's LIVE Levels rows differ from the archived 404-byte BIN.
// The native compiler's field descriptors at RVA 0x3300d4,
// 0x33024d..0x330261 and row-size argument at 0x33038c use a 396-byte row
// with a 40-byte LevelName at 253. D2RCore's DataTableService also reports
// 396, and its level validator reads a uint16 Id at 4 and Act byte at 13.
// Read only the service's live layout; never reinterpret an archived BIN row.
constexpr uint32_t LevelRowSize=396,LevelNameOffset=253;
template<class Read,class Localize>
Location ReadLocation(int level,uintptr_t row,uint32_t rowSize,Read read,Localize localize) {
    Location result;
    if(level<=0||level>UINT16_MAX||row<0x10000||row>UINTPTR_MAX-LevelRowSize||rowSize!=LevelRowSize)return result;
    uint16_t id=0;uint8_t act=0;char key[40]{};
    if(!read(row+4,id)||id!=level||!read(row+13,act)||act>=5||
       !read(row+LevelNameOffset,key)||!key[0]||!std::memchr(key,0,sizeof key))return result;
    result.level=level;result.act=act+1;
    char qualified[48]{};std::snprintf(qualified,sizeof qualified,"d2r:%s",key);
    if(!localize(qualified,result.name,sizeof result.name)||!std::memchr(result.name,0,sizeof result.name))
        std::memset(result.name,0,sizeof result.name);
    return result;
}
}
