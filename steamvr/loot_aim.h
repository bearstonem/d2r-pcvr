#pragma once
#include "frame_math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace framevr::lootaim {
// Loot pick: which dropped item Interact (A) takes. The game's own answer
// (InteractTarget 0x18D960) is the item nearest the hero, so with several on
// the floor A kept taking the closest one, never the one the player wanted.
// Here the player chooses: the item nearest where the aiming hand's ray meets
// the floor while the arm is held out to point, else the one nearest where the
// look meets the floor. Values tuned in the headset (2026-10-18/19).
constexpr int MaxItems=96;
constexpr float CellUnits=2.f;           // world units per grid cell (render = grid*2)
constexpr float PickRadiusCells=1.f;     // an item this far from the aim's floor point can be picked
constexpr float KeepFactor=1.3f;         // the current pick stays while inside the radius times this
constexpr float SwitchShare=.25f;        // another item takes over when nearer by this share of the radius
constexpr float ReachCells=25.f;         // never an item farther than this from the hero
constexpr float LookDownDeg=8.f;         // the eyes look a little below the head's forward
constexpr float DefaultPickupCells=5.5f; // [interactions] loot_pick_reach_cells: A picks up without walking
// The aiming hand points while held out from the head (horizontal reach, m);
// it lets go 6 cm nearer so the answer does not flicker. Pointing steeply down
// at an item by the feet the arm cannot reach as far: 3/4 of the distance counts.
constexpr float ArmOutM=.20f,ArmReleaseM=.06f,DownDip=-.75f,DownShare=.75f;

struct Item {uintptr_t unit;math::Vec at;float heroCells;};   // at: the item's floor point in the world
struct Ray {math::Vec origin,direction;bool valid=false;};
struct Choice {uintptr_t unit=0;bool byHand=false;float distance=0;};   // distance from the aim's floor point (world units)

// How far the item lies on the floor from where the ray meets the floor at the
// item's height; a ray level or upward never meets it.
inline float FloorDistance(const Ray& ray,math::Vec at) {
    const auto& d=ray.direction;
    if(!(d.y<-1e-4f)||!(ray.origin.y>at.y))return 1e9f;
    const float t=(at.y-ray.origin.y)/d.y;
    return std::hypot(ray.origin.x+d.x*t-at.x,ray.origin.z+d.z*t-at.z);
}

// The forward ray turned down by degrees about the head's right axis (up is the head's up).
inline math::Vec Lower(math::Vec forward,math::Vec up,float degrees) {
    const float r=degrees*.01745329f,c=std::cos(r),s=std::sin(r);
    return {forward.x*c-up.x*s,forward.y*c-up.y*s,forward.z*c-up.z*s};
}

// The item nearest the ray's floor point inside radius, keeping current while it
// is inside radius*KeepFactor and nothing beats it by the switch margin, so two
// items side by side do not flicker.
inline Choice Best(const Item* items,int count,const Ray& ray,float radius,uintptr_t current) {
    Choice best{},kept{};
    if(!ray.valid||!items)return {};
    float bestValue=radius,keptValue=-1;
    for(int i=0;i<count;++i) {
        const auto& item=items[i];
        if(!item.unit||!(item.heroCells<=ReachCells))continue;
        const float a=FloorDistance(ray,item.at);
        if(item.unit==current&&a<=radius*KeepFactor){keptValue=a;kept={item.unit,false,a};}
        if(a<bestValue){bestValue=a;best={item.unit,false,a};}
    }
    if(keptValue>=0&&(!best.unit||best.distance>keptValue-radius*SwitchShare))return kept;
    return best;
}

// The pointing hand first while it is on an item, else the look. held (Interact
// down) keeps the current item while it is still on the ground and in reach, so
// the hand drifting during the press never changes what A acts on.
inline Choice Select(const Item* items,int count,const Ray& hand,const Ray& look,uintptr_t current,bool held) {
    if(held&&current)
        for(int i=0;i<count;++i)if(items[i].unit==current&&items[i].heroCells<=ReachCells)return {current,false,0};
    const float radius=PickRadiusCells*CellUnits;
    Choice c=Best(items,count,hand,radius,current);
    if(c.unit){c.byHand=true;return c;}
    return Best(items,count,look,radius,current);
}

// Is the aiming hand held out to point? reachM: its horizontal distance from
// the head; rayY: the ray's vertical component. on is the previous answer.
inline bool Pointing(bool on,float reachM,float rayY) {
    if(!std::isfinite(reachM)||!std::isfinite(rayY))return false;
    const float need=(on?ArmOutM-ArmReleaseM:ArmOutM)*(rayY<DownDip?DownShare:1.f);
    return reachM>=need;
}

// Close enough for A to pick it up without walking? limit 0 = any distance.
inline bool InReach(float cells,float limit) {return limit<=0||(cells>=0&&cells<=limit);}

// The item's name color, as the game draws it, for its highlight: white normal
// (gray when socketed or ethereal), blue magic, green set, yellow rare, gold
// unique, orange crafted/tempered. Item data (unit +0x10): quality +0x00
// (1 low .. 8 crafted), flags +0x18 (0x800 socketed, 0x400000 ethereal).
inline void RarityColor(uint32_t quality,uint32_t flags,float rgb[3]) {
    float c[3]={1,1,1};
    switch(quality) {
        case 4: c[0]=.41f;c[1]=.41f;c[2]=1;break;   // magic
        case 5: c[0]=.1f;c[1]=1;c[2]=.1f;break;     // set
        case 6: c[0]=1;c[1]=1;c[2]=.39f;break;      // rare
        case 7: c[0]=.9f;c[1]=.75f;c[2]=.45f;break; // unique
        case 8: case 9: c[0]=1;c[1]=.6f;c[2]=.1f;break;   // crafted, tempered
        default: if(flags&(0x800u|0x400000u))c[0]=c[1]=c[2]=.6f;break;   // socketed / ethereal
    }
    std::copy_n(c,3,rgb);
}

// The highlight's gentle pulse (1.5 Hz, between 60% and full), as an alpha.
inline float Pulse(uint64_t ms) {return .8f+.2f*std::sin(float(ms%100000)*.0062831853f*1.5f);}

// An item (unit type 4) on the ground (mode 3) or dropping (mode 5): picked up
// (stored, on the cursor, in the belt) it is no longer one.
inline bool OnFloor(uint32_t type,uint32_t mode) {return type==4&&(mode==3||mode==5);}

// A pick is handed to the game only while it is still an item on the floor, in
// the area the room walk saw it in, chosen at most MaxPickAgeMs ago. The game
// frees an area's units when the hero leaves it, and a gold pile's unit when it
// is picked up, so an old pointer must never reach it.
constexpr uint64_t MaxPickAgeMs=200;
inline bool Usable(uint32_t type,uint32_t mode,bool sameArea,uint64_t ageMs) {return OnFloor(type,mode)&&sameArea&&ageMs<MaxPickAgeMs;}
// The handover rule: usable as above, the same unit id as when it was picked (a
// freed address reused by another unit is not it), and found on the floor by a
// walk of the hero's rooms made in this same call. The walk is the guarantee: a
// freed unit keeps its old header bytes, but it is gone from the room lists.
inline bool HandOver(const uint32_t* header,uint32_t pickId,bool sameArea,uint64_t ageMs,bool walkFound) {
    return walkFound&&Usable(header[0],header[3],sameArea,ageMs)&&header[2]==pickId;
}

// What tells areas apart for the pick: the hero unit, the game's area id (0 while
// unknown: not compared) and the biome generation (bumped on each new area's look).
// Defense in depth only: the area id reads 0 when the game state is stale, and two
// neighboring areas may share a look. The fresh walk (HandOver) is the guarantee.
struct Area {uintptr_t hero=0;int level=0;uint32_t generation=0;};
inline bool SameArea(const Area& a,const Area& b) {
    return a.hero==b.hero&&a.generation==b.generation&&(!a.level||!b.level||a.level==b.level);
}

// The walk's limits, the sewer warp search's: 256 units a room, 1024 in all.
constexpr int MaxChain=256,MaxVisited=1024;

// The units near the hero, walked the way the sewer warp search does
// (sewer_interaction.h): hero path +0x38 -> room +0x20 -> the room and its near
// rooms (+0x00 list, +0x40 count) -> units +0xA8, next Unit+0x160 (not the
// hash-list link +0x158). visit(unit, header) returns false to stop. heroX/heroY
// are the hero's cell (16.16 on its dynamic path). Returns the units visited.
template<class Read,class Visit>
int Walk(uintptr_t hero,Read read,Visit visit,float& heroX,float& heroY) {
    uintptr_t path=0,room=0,nearby=0;uint32_t xy[2]{},nearCount=0;
    if(!read(hero+0x38,path)||!read(path,xy)||!read(path+0x20,room)||room<0x10000)return 0;
    if(!read(room,nearby)||!read(room+0x40,nearCount)||nearCount>64||(!nearby&&nearCount))return 0;
    heroX=xy[0]/65536.f;heroY=xy[1]/65536.f;
    uintptr_t rooms[65]{room};int roomCount=1;
    for(uint32_t i=0;i<nearCount;++i) {
        uintptr_t next=0;
        if(!read(nearby+i*8,next))break;
        if(next>=0x10000&&std::find(rooms,rooms+roomCount,next)==rooms+roomCount)rooms[roomCount++]=next;
    }
    int visited=0;
    for(int r=0;r<roomCount&&visited<MaxVisited;++r) {
        uintptr_t unit=0;
        if(!read(rooms[r]+0xa8,unit))continue;
        for(int chain=0;unit&&chain<MaxChain&&visited<MaxVisited;++chain) {
            ++visited;
            uintptr_t next=0;uint32_t header[4]{};
            if(!read(unit+0x160,next)||!read(unit,header))break;
            if(!visit(unit,header))return visited;
            if(next==unit)break;
            unit=next;
        }
    }
    return visited;
}

// A floor item's cell on its static path (+0x10/+0x14).
template<class Read>
bool ItemCell(uintptr_t unit,Read read,uint32_t (&cell)[2]) {
    uintptr_t staticPath=0;
    return read(unit+0x38,staticPath)&&read(staticPath+0x10,cell)&&cell[0]&&cell[1]&&cell[0]<=65535&&cell[1]<=65535;
}

// Dropped items near the hero (on the ground or dropping), each with its unit id
// (header +0x08) and cell. Returns the count.
template<class Read>
int Collect(uintptr_t hero,Read read,uintptr_t* units,uint32_t* ids,float* cells,int most,float& heroX,float& heroY) {
    int count=0;
    if(most<=0)return 0;
    Walk(hero,read,[&](uintptr_t unit,const uint32_t* header) {
        uint32_t cell[2]{};
        if(OnFloor(header[0],header[3])&&ItemCell(unit,read,cell)&&std::find(units,units+count,unit)==units+count) {
            units[count]=unit;ids[count]=header[2];cells[count*2]=float(cell[0]);cells[count*2+1]=float(cell[1]);++count;
        }
        return count<most;
    },heroX,heroY);
    return count;
}

// A fresh walk's answer for the pick: still on the floor in the hero's rooms with
// the same unit id, its cell now and the hero's (for the reach and the glow).
struct Live {bool found=false;uint32_t header[4]{};float x=0,y=0,heroX=0,heroY=0;};
template<class Read>
Live Find(uintptr_t hero,Read read,uintptr_t item,uint32_t id) {
    Live live;
    if(!item)return live;
    Walk(hero,read,[&](uintptr_t unit,const uint32_t* header) {
        if(unit!=item)return true;
        uint32_t cell[2]{};
        if(OnFloor(header[0],header[3])&&header[2]==id&&ItemCell(unit,read,cell)) {
            live.found=true;std::copy_n(header,4,live.header);live.x=float(cell[0]);live.y=float(cell[1]);
        }
        return false;
    },live.heroX,live.heroY);
    return live;
}
template<class Read>
bool Contains(uintptr_t hero,Read read,uintptr_t item,uint32_t id) {return Find(hero,read,item,id).found;}
}
