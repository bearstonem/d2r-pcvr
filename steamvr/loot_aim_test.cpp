#include "loot_aim.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
using namespace framevr::lootaim;
static void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}

// A little fake game memory for the room walk.
struct Memory {
    std::map<uintptr_t,uint8_t> bytes;
    template<class T> void Put(uintptr_t at,const T& value) {
        uint8_t raw[sizeof(T)];std::memcpy(raw,&value,sizeof raw);
        for(size_t i=0;i<sizeof raw;++i)bytes[at+i]=raw[i];
    }
    template<class T> bool Read(uintptr_t at,T& value) const {
        uint8_t raw[sizeof(T)];
        for(size_t i=0;i<sizeof raw;++i){auto it=bytes.find(at+i);if(it==bytes.end())return false;raw[i]=it->second;}
        std::memcpy(&value,raw,sizeof raw);return true;
    }
    // A unit: type, class, id, mode; its path at +0x38; the room list's link at +0x160.
    void Unit(uintptr_t at,uint32_t type,uint32_t mode,uintptr_t path,uintptr_t next,uint32_t id=0) {
        const uint32_t header[4]={type,0,id?id:uint32_t(at),mode};
        Put(at,header);Put(at+0x38,path);Put(at+0x160,next);
        Put(at+0x158,uintptr_t(0x990000));   // the hash-list link leads somewhere else entirely
    }
    void Cell(uintptr_t staticPath,uint32_t x,uint32_t y) {const uint32_t cell[2]={x,y};Put(staticPath+0x10,cell);}
};

int main() {
    // Eye 6 units up looking along +z; items on the floor (y 0), with their distance from the hero in cells.
    const Ray look{{0,6,0},{0,-.5f,1},true};   // meets the floor at z 12
    const Item items[]={{1,{0,0,12},6},{2,{3,0,12},6.5f},{3,{0,0,60},30},{4,{-.4f,0,3},1.5f}};
    const Ray none{};
    Check(Select(items,4,none,look,0,false).unit==1,"the look picks the item where it meets the floor");
    Check(Select(items,4,none,{{0,6,0},{.25f,-.5f,1},true},0,false).unit==2,"looking over at the neighbour picks it");
    Check(Select(items,4,none,{{0,6,0},{0,.2f,1},true},0,false).unit==0,"looking up: no pick");
    Check(Select(items,4,none,{{0,6,0},{0,-.1f,1},true},0,false).unit==0,"an item past reach is never picked, even dead on the aim");
    Check(Select(items,4,none,{{0,6,0},{-.25f,-.5f,1},true},0,false).unit==0,"3 units beside every item: outside the one-cell radius");
    Check(std::fabs(FloorDistance(look,{0,0,12}))<1e-3f&&std::fabs(FloorDistance(look,{3,0,12})-3)<1e-3f,"floor distance from the aim's floor point");
    Check(FloorDistance({{0,6,0},{0,.1f,1},true},{0,0,12})>1e8f,"a ray looking up never meets the floor");

    // Between two items the current one stays until another is clearly nearer.
    const Ray between{{0,6,0},{1.6f/12,-.5f,1},true};   // 1.6 units from item 1, 1.4 from item 2
    Check(Select(items,4,none,between,1,false).unit==1,"between two items the current pick stays");
    Check(Select(items,4,none,between,2,false).unit==2,"either way");
    Check(Select(items,4,none,{{0,6,0},{2.f/12,-.5f,1},true},1,false).unit==2,"a clearly nearer item takes over");
    Check(Select(items,4,none,{{0,6,0},{2.4f/12,-.5f,1},true},1,false).unit==2,"and the old one, now 2.4 units off, is let go");

    // The pointing hand wins while it is on an item, even a farther one; off any item the look picks.
    const Ray hand{{.3f,4,.3f},{-.3f,-4,11.7f},true};   // at item 1
    const Ray lookAtFeet{{0,6,0},{-.4f,-6,3},true};     // at item 4
    const auto byHand=Select(items,4,hand,lookAtFeet,0,false);
    Check(byHand.unit==1&&byHand.byHand,"the pointing hand beats the look");
    const auto byLook=Select(items,4,{{.3f,4,.3f},{1,0,0},true},lookAtFeet,0,false);
    Check(byLook.unit==4&&!byLook.byHand,"a hand on no item leaves the pick to the look");

    // Held (Interact down) keeps the item whatever the aim does, while it is on the ground.
    Check(Select(items,4,none,{{0,6,0},{0,1,0},true},2,true).unit==2,"held keeps the pick");
    Check(Select(items,1,none,{{0,6,0},{0,1,0},true},2,true).unit==0,"held: picked up already, nothing");

    // The look's ray: level with 30 degrees down lands ahead and below.
    const auto down=Lower({0,0,1},{0,1,0},30);
    Check(std::fabs(down.y+.5f)<1e-3f&&std::fabs(down.z-.866f)<1e-3f,"lower turns the forward down about the right axis");

    // Arm out to point, with a release margin and a shorter reach pointing steeply down.
    Check(!Pointing(false,.19f,0)&&Pointing(false,.20f,0),"20 cm out starts pointing");
    Check(Pointing(true,.15f,0)&&!Pointing(true,.13f,0),"it lets go 6 cm nearer");
    Check(Pointing(false,.15f,-.9f)&&!Pointing(false,.14f,-.9f),"pointing down at the feet needs 3/4 of the reach");
    Check(!Pointing(true,NAN,0),"no tracking: not pointing");

    Check(InReach(5,5.5f)&&!InReach(6,5.5f)&&!InReach(-1,5.5f)&&InReach(30,0),"pick-up reach, 0 = any distance");

    float rgb[3];
    RarityColor(4,0,rgb);Check(rgb[2]==1&&rgb[0]<.5f,"magic is blue");
    RarityColor(7,0,rgb);Check(rgb[0]>.8f&&rgb[2]<.5f,"unique is gold");
    RarityColor(2,0x800,rgb);Check(rgb[0]==.6f&&rgb[1]==.6f,"a socketed normal item is gray");
    RarityColor(2,0,rgb);Check(rgb[0]==1&&rgb[1]==1&&rgb[2]==1,"a normal item is white");
    for(uint64_t ms=0;ms<2000;ms+=37)Check(Pulse(ms)>=.6f-1e-4f&&Pulse(ms)<=1+1e-4f,"the pulse stays between 60% and full");

    // The room walk: the hero's room and its near rooms, units linked at +0x160.
    Memory m;
    const uintptr_t hero=0x100000,heroPath=0x110000,room=0x300000,list=0x310000,room2=0x500000;
    m.Put(hero+0x38,heroPath);
    const uint32_t heroXy[2]={100u<<16,200u<<16};m.Put(heroPath,heroXy);m.Put(heroPath+0x20,room);
    m.Put(room,list);m.Put(room+0x40,uint32_t(2));
    m.Put(list,room);m.Put(list+8,room2);   // the near list holds the room itself too: walked once
    m.Put(room+0xa8,uintptr_t(0x600000));m.Put(room2+0xa8,uintptr_t(0x800000));
    m.Unit(0x600000,4,3,0x610000,0x700000);m.Cell(0x610000,101,200);   // an item on the ground
    m.Unit(0x700000,1,1,0x710000,0x900000);                            // a monster
    m.Unit(0x900000,4,4,0x910000,0);m.Cell(0x910000,102,200);          // an item in another mode (not on the ground)
    m.Unit(0x800000,4,5,0x810000,0x800000);m.Cell(0x810000,99,201);    // an item dropping; its link ends at itself
    auto read=[&](uintptr_t at,auto& value){return m.Read(at,value);};
    uintptr_t units[MaxItems];uint32_t ids[MaxItems];float cells[MaxItems*2];float hx=0,hy=0;
    const int n=Collect(hero,read,units,ids,cells,MaxItems,hx,hy);
    Check(n==2&&units[0]==0x600000&&units[1]==0x800000,"items on the ground or dropping, each once");
    Check(ids[0]==0x600000&&ids[1]==0x800000,"with their unit ids");
    Check(cells[0]==101&&cells[1]==200&&cells[2]==99&&cells[3]==201&&hx==100&&hy==200,"their cells and the hero's");
    Check(Collect(hero,read,units,ids,cells,1,hx,hy)==1,"never more than asked for");
    Check(Collect(0x123000,read,units,ids,cells,MaxItems,hx,hy)==0,"no hero path: nothing");

    // A fresh walk confirms a pick before it goes to the game.
    Check(Contains(hero,read,0x600000,0x600000)&&Contains(hero,read,0x800000,0x800000),"items still on the floor are there");
    Check(!Contains(hero,read,0x900000,0x900000),"an item no longer on the floor is not");
    Check(!Contains(hero,read,0x700000,0x700000)&&!Contains(hero,read,0x123456,0x123456)&&!Contains(hero,read,0,0),"a monster, a unit not in the rooms, none: not");
    const Live live=Find(hero,read,0x800000,0x800000);
    Check(live.found&&live.x==99&&live.y==201&&live.heroX==100&&live.heroY==200&&live.header[2]==0x800000,"the walk gives its cell now and the hero's");

    // The handover rule: only a unit the fresh walk found, with the picked id.
    uint32_t header[4]={4,0,0x600000,3};
    Check(HandOver(header,0x600000,true,0,true),"found on the floor with its id: handed");
    Check(!HandOver(header,0x600000,true,0,false),"freed: the header still reads as a floor item, but the walk misses it");
    Check(!HandOver(header,0x600001,true,0,true),"the same address, another unit's id: not handed");
    Check(!HandOver(header,0x600000,false,0,true)&&!HandOver(header,0x600000,true,MaxPickAgeMs,true),"another area or too old: not handed");

    // Freed: dropped out of the room list with its header intact.
    m.Put(room+0xa8,uintptr_t(0x700000));   // the room's list now starts past it
    Check(!Contains(hero,read,0x600000,0x600000),"gone from the room list: not there, whatever its bytes say");
    m.Put(room+0xa8,uintptr_t(0x600000));
    // Reused: the same address, now another item (another id).
    m.Unit(0x600000,4,3,0x610000,0x700000,0x5555);
    Check(!Contains(hero,read,0x600000,0x600000)&&Contains(hero,read,0x600000,0x5555),"a reused address is another unit");
    m.Unit(0x600000,4,4,0x610000,0x700000);   // picked up: on the cursor now
    Check(!Contains(hero,read,0x600000,0x600000),"picked up: not there any more");
    Check(Collect(hero,read,units,ids,cells,MaxItems,hx,hy)==1,"nor collected");

    // The walk stops at the sewer search's limits, even on a list that loops.
    Memory loop;
    loop.Put(hero+0x38,heroPath);loop.Put(heroPath,heroXy);loop.Put(heroPath+0x20,room);
    loop.Put(room,list);loop.Put(room+0x40,uint32_t(1));loop.Put(list,room);
    loop.Put(room+0xa8,uintptr_t(0x600000));
    loop.Unit(0x600000,1,1,0x610000,0x601000);loop.Unit(0x601000,1,1,0x611000,0x600000);   // two units linked to each other
    auto readLoop=[&](uintptr_t at,auto& value){return loop.Read(at,value);};
    Check(Walk(hero,readLoop,[](uintptr_t,const uint32_t*){return true;},hx,hy)==MaxChain,"a looping list stops at 256 a room");

    // ... and at 1024 units across rooms: five rooms of 256 each.
    Memory many;
    many.Put(hero+0x38,heroPath);many.Put(heroPath,heroXy);many.Put(heroPath+0x20,room);
    many.Put(room,list);many.Put(room+0x40,uint32_t(5));
    for(uintptr_t r=0;r<5;++r) {
        const uintptr_t rm=0x2000000+r*0x100000;
        many.Put(list+r*8,rm);many.Put(rm+0xa8,rm+0x1000);
        for(uintptr_t k=0;k<MaxChain;++k)many.Unit(rm+0x1000+k*0x200,1,1,0,k+1<MaxChain?rm+0x1000+(k+1)*0x200:0);
    }
    many.Put(room+0xa8,uintptr_t(0));
    auto readMany=[&](uintptr_t at,auto& value){return many.Read(at,value);};
    Check(Walk(hero,readMany,[](uintptr_t,const uint32_t*){return true;},hx,hy)==MaxVisited,"1024 units in all, however many rooms");

    // The pick is usable only while on the floor, in the same area, and fresh.
    Check(Usable(4,3,true,0)&&Usable(4,5,true,MaxPickAgeMs-1),"on the ground or dropping, same area, fresh");
    Check(!Usable(4,4,true,0)&&!Usable(4,0,true,0)&&!Usable(1,3,true,0),"on the cursor, stored, a monster: no");
    Check(!Usable(4,3,false,0)&&!Usable(4,3,true,MaxPickAgeMs),"another area or too old: no");
    const Area here{0x100000,2,7};
    Check(SameArea(here,here)&&SameArea(here,{0x100000,0,7})&&SameArea({0x100000,0,7},here),"an unknown area id is not compared");
    Check(!SameArea(here,{0x100000,3,7})&&!SameArea(here,{0x100000,2,8})&&!SameArea(here,{0x200000,2,7}),"another id, look or hero: another area");
    std::puts("loot aim tests passed");
}
