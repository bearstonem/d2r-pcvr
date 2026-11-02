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
    // A hand 6 units up pointing along +z; items on the floor (y 0), with their distance from the hero in cells.
    const Ray hand{{0,6,0},{0,-.5f,1},true};   // meets the floor at z 12
    const Item items[]={{1,{0,0,12},6},{2,{3,0,12},6.5f},{3,{0,0,100},50},{4,{-.4f,0,3},1.5f}};
    auto at=[](float x){return Ray{{0,6,0},{x/12,-.5f,1},true};};   // the hand pointed at x on the floor at z 12
    Check(Select(items,4,hand,0).unit==1,"the hand picks the item where its ray meets the floor");
    Check(Select(items,4,at(3),0).unit==2,"pointing over at the neighbour picks it");
    Check(Select(items,4,{{0,6,0},{0,.2f,1},true},0).unit==0,"pointing up: no pick");
    Check(Select(items,4,{{0,6,0},{0,-.06f,1},true},0).unit==0,"an item past 40 cells is never picked, even dead on the aim");
    Check(Select(items,4,at(1.4f),0).unit==1&&Select(items,4,at(-3.5f),0).unit==0,"a circle of 1.5 cells (3 units) round the pointed spot");
    Check(Select(items,4,Ray{},0).unit==0&&Select(items,4,Ray{},1).unit==0,"no ray (the arm not out): no pick, the current one let go");
    Check(std::fabs(FloorDistance(hand,{0,0,12}))<1e-3f&&std::fabs(FloorDistance(hand,{3,0,12})-3)<1e-3f,"floor distance from the pointed spot");
    Check(FloorDistance({{0,6,0},{0,.1f,1},true},{0,0,12})>1e8f,"a ray pointing up never meets the floor");

    // Between two items the current one stays until another is nearer by a quarter cell (0.5 units).
    Check(Select(items,4,at(1.6f),1).unit==1&&Select(items,4,at(1.6f),2).unit==2,"between two items the current pick stays, either way");
    Check(Select(items,4,at(1.74f),1).unit==1,"1.26 units against 1.74: not yet a quarter cell nearer");
    Check(Select(items,4,at(1.76f),1).unit==2,"1.24 against 1.76: the neighbour takes over");
    Check(Select(items,4,at(-3.5f),1).unit==1,"the current pick stays out to 130% of the circle");
    Check(Select(items,4,at(4.5f),1).unit==2,"and past it, 4.5 units off, is let go");

    // Arm out to point (12 cm), let go 6 cm nearer.
    Check(!Pointing(false,.11f)&&Pointing(false,.12f),"12 cm out starts pointing");
    Check(Pointing(true,.07f)&&!Pointing(true,.05f),"it lets go 6 cm nearer");
    Check(!Pointing(true,NAN),"no tracking: not pointing");

    Check(InReach(4,DefaultPickupCells)&&!InReach(4.1f,DefaultPickupCells)&&!InReach(-1,4)&&InReach(30,0),"pick-up reach 4 cells, 0 = any distance");

    float rgb[3];
    RarityColor(4,0,rgb);Check(rgb[2]==1&&rgb[0]<.5f,"magic is blue");
    RarityColor(5,0,rgb);Check(rgb[1]==1&&rgb[0]<.5f,"set is green");
    RarityColor(6,0,rgb);Check(rgb[0]==1&&rgb[1]==1&&rgb[2]<.5f,"rare is yellow");
    RarityColor(7,0,rgb);Check(rgb[0]>.8f&&rgb[2]<.5f,"unique is gold");
    RarityColor(8,0,rgb);Check(rgb[0]==1&&rgb[1]==.6f&&rgb[2]<.2f,"crafted is orange");
    RarityColor(2,0x800,rgb);Check(rgb[0]==.6f&&rgb[1]==.6f,"a socketed normal item is gray");
    RarityColor(2,0,rgb);Check(rgb[0]==1&&rgb[1]==1&&rgb[2]==1,"a normal item is white");

    // The pointed name's look: in reach a rarity rim on a rarity shade at 180%, out of reach a red rim on the game's box at 130%.
    {
        const float blue[3]={.41f,.41f,1},gameBox[4]={.05f,.05f,.05f,.8f};
        float rim[4],box[4];
        for(uint64_t ms=0;ms<3000;ms+=37) {
            RimRgba(LookFor(true),blue,ms,rim);
            Check(rim[0]==blue[0]&&rim[2]==1&&rim[3]>=.2f-1e-4f&&rim[3]<=1+1e-4f,"in reach: the rarity rim pulses between 20% and full (1 Hz, 40%)");
            RimRgba(LookFor(false),blue,ms,rim);
            Check(rim[0]==RedRgb[0]&&rim[1]==RedRgb[1]&&rim[3]>=-1e-4f&&rim[3]<=.9f+1e-4f,"out of reach: a red rim up to 90% (1.5 Hz, 50%)");
        }
        BoxRgba(LookFor(true),gameBox,blue,box);
        Check(std::fabs(box[0]-.41f*RarityShade)<1e-5f&&std::fabs(box[2]-RarityShade)<1e-5f&&box[3]==1,"in reach: a dark shade of the rarity color, solid");
        BoxRgba(LookFor(false),gameBox,blue,box);
        Check(box[0]==gameBox[0]&&box[3]==.9f,"out of reach: the game's own fill at 90%");
        BoxRgba(LookFor(false),nullptr,blue,box);
        Check(box[0]==0&&box[1]==0&&box[2]==0,"no fill from the game: black");
        Check(LookFor(true).nameScale==1.8f&&LookFor(false).nameScale==1.3f&&MaxNameScale==2&&RimPx==1,"names 180% and 130%, at most twice the game's; a 1 px rim");
        Check(Pulse(1234,0,.5f)==1,"no pulse speed: steady");
        const int32_t label[4]={10,20,30,8};
        int32_t ring[4][4];
        Check(Ring(label,1,ring)==4&&Ring(label,0,ring)==0&&Ring(nullptr,1,ring)==0,"four strips, none without a width or a box");
        Ring(label,1,ring);
        Check(ring[0][0]==9&&ring[0][1]==19&&ring[0][2]==32&&ring[0][3]==1&&ring[1][1]==28&&ring[2][0]==9&&ring[2][3]==8&&ring[3][0]==40,
              "top and bottom across the outer width, left and right the box's height");
        for(int i=0;i<4;++i){
            const bool apart=ring[i][0]+ring[i][2]<=label[0]||ring[i][0]>=label[0]+label[2]||ring[i][1]+ring[i][3]<=label[1]||ring[i][1]>=label[1]+label[3];
            Check(apart,"the rim never covers the name's box");
        }
    }

    // The label paint's owner (2026-10-29 log): every name laid out in one scratch box, painted from its own record.
    {
        int scratch[4]={},paint[8][4]={};char names[8][8]={};
        LabelOwners<64> owners;
        for(int i=0;i<8;++i)owners.Remember(scratch,names[i],0x1000+i);   // one eye's pass: 8 shown names
        for(int i=0;i<8;++i)Check(owners.Unit(paint[i],names[i])==uintptr_t(0x1000+i),"each paint finds its own item, not the last laid out");
        for(int i=0;i<8;++i)owners.Remember(scratch,names[i],0x1000+i);   // the other eye: the same slots again
        int used=0;for(const auto& s:owners.slot)used+=s.seq!=0;
        Check(used==8,"the second eye's pass reuses the first's slots");
        Check(owners.Unit(scratch,nullptr)==0,"the shared scratch box alone names no item");
        Check(owners.Unit(scratch,names[3])==0x1003,"the exact pair still finds it");
        char unknown[8]={};
        Check(owners.Unit(paint[0],unknown)==0,"a name never laid out: no item (an ordinary paint)");
        owners.Remember(scratch,names[2],0x2002);   // the record now holds another item
        Check(owners.Unit(paint[2],names[2])==0x2002,"a record reused by another item: the latest");
        Check(owners.Find(0x2002)>=0&&owners.Find(0x1002)<0&&owners.Find(0)<0,"Find: the item's slot, a replaced item none");
    }
    {   // the opposite case, still right: one shared name buffer, a box of its own per label
        char shared[8]={};int boxes[3][4]={};
        LabelOwners<8> owners;
        for(int i=0;i<3;++i)owners.Remember(boxes[i],shared,0x10+i);
        for(int i=0;i<3;++i)Check(owners.Unit(boxes[i],shared)==uintptr_t(0x10+i),"own box: its item");
        int other[4]={};
        Check(owners.Unit(other,shared)==0,"a shared name alone names no item");
        Check(owners.Unit(boxes[1],nullptr)==0x11,"a box held by one item: that item");
    }
    {   // full: the least recently written slot goes, a refreshed one stays
        int scratch[4]={};char names[5][4]={};
        LabelOwners<4> owners;
        for(int i=0;i<4;++i)owners.Remember(scratch,names[i],0x100+i);
        owners.Remember(scratch,names[0],0x100);   // names[0] laid out again: now the newest
        owners.Remember(scratch,names[4],0x104);   // evicts names[1], the oldest
        Check(owners.Unit(nullptr,names[0])==0x100&&owners.Unit(nullptr,names[4])==0x104,"refreshed and new kept");
        Check(owners.Unit(nullptr,names[1])==0,"the oldest evicted");
        Check(owners.Unit(nullptr,names[2])==0x102&&owners.Unit(nullptr,names[3])==0x103,"the rest kept");
    }

    // The pointed item's name when the game lays out none: Show items held for it alone.
    {
        Check(HoldsForPointed(true,true,false),"an item pointed at, in the world, the stick not holding: held for it");
        Check(!HoldsForPointed(false,true,false)&&!HoldsForPointed(true,false,false),"nothing pointed at, or out of the world: no hold");
        Check(!HoldsForPointed(true,true,true),"the stick holding Show items: not ours, every name as before");
        Check(PointedNow(1000,1000)&&PointedNow(1000,1000+PointedFreshMs-1)&&!PointedNow(1000,1000+PointedFreshMs)&&
              !PointedNow(0,50)&&!PointedNow(2000,1000),"a pointing is fresh for 200 ms; none or a clock step back is not");
        Check(OnlyPointedUntil(true,false,0,1000,300)==1300,"holding: the other names cut a lease ahead");
        Check(OnlyPointedUntil(false,false,1300,1001,300)==1001+PointedTailMs,"the hold just ended: a short tail");
        Check(OnlyPointedUntil(false,false,1001+PointedTailMs,1100,300)==1001+PointedTailMs,"the tail does not move on");
        Check(OnlyPointedUntil(false,false,0,1100,300)==0,"never held: no cut");
        Check(OnlyPointedUntil(true,true,1300,1100,300)==0&&OnlyPointedUntil(false,true,1300,1100,300)==0,"the stick holding: no cut at once");
        Check(OnlyPointed(1300,1299)&&!OnlyPointed(1300,1300)&&!OnlyPointed(0,5),"cut while before the end only");
    }

    // The pointing hand stays awake in the world while the motion layer is off, never in menus or the wheel.
    {
        Check(AwakeWorld(0,false)&&AwakeWorld(GateInteraction|GateBlood,false),"in the world (an interaction starting, a blood wipe): the hand points");
        for(uint32_t g:{GateControls,GateChain,GateHead,GatePanel,GateSpatial,GateSettings,GateChord,GateInventory,GateMenu,GateWorld})
            Check(!AwakeWorld(g,false),"a menu, panel, VR Settings or no world: the hand does not point");
        Check(!AwakeWorld(0,true),"the skill wheel open: the hand does not point");
        Check(HandLive(true,true,HandFreshMs)&&!HandLive(true,true,HandFreshMs+1)&&!HandLive(true,false,0)&&!HandLive(false,true,0),
              "a tracked hand read just now, in the world");
        char text[160];
        GateText(0,text,sizeof text);Check(std::strcmp(text,"none")==0,"no reason: none");
        GateText(GateInteraction|GateSettings,text,sizeof text);Check(std::strcmp(text,"VR Settings, interaction")==0,"the reasons as words");
        char tiny[8];GateText(GateInteraction|GateSettings,tiny,sizeof tiny);Check(std::strlen(tiny)<sizeof tiny,"a short buffer is cut, never overrun");
    }

    // The headset gate (2026-10-29: A's target is asked between our frame pairs, where Active is false).
    Check(InHeadset(true,0,1000)&&InHeadset(true,5000,1000),"inside a headset frame: in the headset");
    Check(InHeadset(false,1000,1000+16)&&InHeadset(false,1000,1000+HeadsetGapMs),
          "between two headset frames (A's press in the game's logic): still in the headset, so the pick is handed");
    Check(!InHeadset(false,1000,1000+HeadsetGapMs+1),"no headset frame for longer than the gap: out (headset off, flat panel)");
    Check(!InHeadset(false,0,0)&&!InHeadset(false,0,100)&&!InHeadset(false,0,HeadsetGapMs),"no headset frame ever (desktop play): never, the game's own pick");
    Check(InHeadset(false,1001,1000),"a stamp just after this thread read the clock: a frame just now");
    Check(InHeadset(false,1000,1100,100)&&!InHeadset(false,1000,1101,100),"the gap is a parameter");

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

    // An A press followed for its log line, by unit ids only.
    {
        const uint32_t floor[3]={11,12,13},afterTake[2]={11,13},afterBoth[1]={11};
        PressWatch w;
        Check(!w.Active()&&w.Walked(floor,3)==-1&&!w.Expired(5000),"no press: nothing to tell");
        w.Press(floor,3,12,1000);
        Check(w.Walked(floor,3)==-1,"the same walk: nothing taken yet");
        Check(w.Walked(afterTake,2)==1&&w.ids[1]==w.chosen,"the chosen item gone from a later walk: taken, a match");
        Check(w.Walked(afterTake,2)==-1,"each taken item is told once");
        Check(!w.Expired(1000+PressWatchMs-1)&&!w.Expired(1000+PressWatchMs)&&!w.Active(),"after a take the watch ends without a 'nothing' line");
        w.Press(floor,3,12,2000);
        Check(w.Walked(afterTake,2)==1&&w.Walked(afterBoth,1)==2&&w.ids[2]!=w.chosen,"held on: a second item taken is told too, a mismatch");
        w.Press(floor,3,12,3000);
        Check(w.Walked(afterBoth,1)==-2&&!w.Active()&&!w.Expired(9000),"two gone at once: no verdict, the watch ends");
        w.Press(floor,3,12,4000);
        Check(!w.Expired(4000+PressWatchMs-1)&&w.Expired(4000+PressWatchMs)&&!w.Expired(4000+PressWatchMs+1),"nothing taken with an item chosen: told once at 3 s");
        w.Press(floor,3,0,5000);
        Check(!w.Expired(5000+PressWatchMs),"nothing chosen and nothing taken: no line");
        w.Press(floor,3,12,6000);w.Cancel();
        Check(!w.Active()&&w.Walked(afterTake,2)==-1&&!w.Expired(9999),"cancelled (Off, another area, no walk): silent");
    }
    std::puts("loot aim tests passed");
}
