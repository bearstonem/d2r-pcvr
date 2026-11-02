#pragma once
#include "frame_math.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace framevr::lootaim {
// Loot pick: which dropped item Interact (A) takes. The game's own answer
// (InteractTarget 0x18D960) is the item nearest the hero, so with several on
// the floor A kept taking the closest one, never the one the player wanted.
// Here the player chooses by pointing: the item nearest where the dominant
// hand's ray meets the floor while that arm is held out. Values tuned in the
// headset (2026-10-18 to 10-29).
constexpr int MaxItems=60;               // the most dropped items one walk keeps
constexpr float CellUnits=2.f;           // world units per grid cell (render = grid*2)
constexpr float PickRadiusCells=1.5f;    // an item this far from the pointed floor spot can be picked
constexpr float KeepFactor=1.3f;         // the current pick stays while inside the radius times this
constexpr float SwitchCells=.25f;        // another item takes over when nearer by this much (cells)
constexpr float ReachCells=40.f;         // never an item farther than this from the hero
constexpr float DefaultPickupCells=4.f;  // [interactions] loot_pick_reach_cells: A picks up without walking
// The hand points while held out from the head (horizontal reach, m); it lets
// go 6 cm nearer so the answer does not flicker.
constexpr float ArmOutM=.12f,ArmReleaseM=.06f;

struct Item {uintptr_t unit;math::Vec at;float heroCells;};   // at: the item's floor point in the world
struct Ray {math::Vec origin,direction;bool valid=false;};
struct Choice {uintptr_t unit=0;float distance=0;};   // distance from the pointed floor spot (world units)

// How far the item lies on the floor from where the ray meets the floor at the
// item's height; a ray level or upward never meets it.
inline float FloorDistance(const Ray& ray,math::Vec at) {
    const auto& d=ray.direction;
    if(!(d.y<-1e-4f)||!(ray.origin.y>at.y))return 1e9f;
    const float t=(at.y-ray.origin.y)/d.y;
    return std::hypot(ray.origin.x+d.x*t-at.x,ray.origin.z+d.z*t-at.z);
}

// The item nearest the ray's floor point inside radius, keeping current while it
// is inside radius*KeepFactor and nothing beats it by margin, so two items side
// by side do not flicker. Radius and margin in world units.
inline Choice Best(const Item* items,int count,const Ray& ray,float radius,float margin,uintptr_t current) {
    Choice best{},kept{};
    if(!ray.valid||!items)return {};
    float bestValue=radius,keptValue=-1;
    for(int i=0;i<count;++i) {
        const auto& item=items[i];
        if(!item.unit||!(item.heroCells<=ReachCells))continue;
        const float a=FloorDistance(ray,item.at);
        if(item.unit==current&&a<=radius*KeepFactor){keptValue=a;kept={item.unit,a};}
        if(a<bestValue){bestValue=a;best={item.unit,a};}
    }
    if(keptValue>=0&&(!best.unit||best.distance>keptValue-margin))return kept;
    return best;
}

// The item the pointing hand is on, or none (an invalid ray: the arm is not out).
inline Choice Select(const Item* items,int count,const Ray& hand,uintptr_t current) {
    return Best(items,count,hand,PickRadiusCells*CellUnits,SwitchCells*CellUnits,current);
}

// Is the hand held out to point? reachM: its horizontal distance from the head;
// on is the previous answer.
inline bool Pointing(bool on,float reachM) {
    if(!std::isfinite(reachM))return false;
    return reachM>=(on?ArmOutM-ArmReleaseM:ArmOutM);
}

// Close enough for A to pick it up without walking? limit 0 = any distance.
inline bool InReach(float cells,float limit) {return limit<=0||(cells>=0&&cells<=limit);}

// The item's name color, as the game draws it: white normal (gray when
// socketed or ethereal), blue magic, green set, yellow rare, gold unique,
// orange crafted/tempered. Item data (unit +0x10): quality +0x00 (1 low ..
// 8 crafted), flags +0x18 (0x800 socketed, 0x400000 ethereal).
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

// The pointed item's name, as it is drawn: in reach (A takes it from here) a rim
// in its rarity color, a dark shade of that color behind the name and the name
// at 180%; out of reach (A takes nothing) a red rim on the game's own box and the
// name at 130%. Each rim pulses. Values tuned in the headset (2026-10-29).
struct NameLook {
    bool rarityRim;        // the rim in the item's rarity color, else red
    float rimAlpha;        // the rim's opacity at the pulse's peak
    float pulseHz,pulseDepth;
    bool rarityBox;        // the box a dark shade of the rarity color, else the game's own fill
    float boxAlpha;
    float nameScale;       // the name's size, times the game's (capped at MaxNameScale)
};
inline constexpr NameLook InReachLook{true,1.f,1.f,.4f,true,1.f,1.8f};
inline constexpr NameLook OutOfReachLook{false,.9f,1.5f,.5f,false,.9f,1.3f};
constexpr float MaxNameScale=2.f;
constexpr int RimPx=1;
constexpr float RedRgb[3]={1.f,.22f,.18f};
// A box in the name's own rarity color would hide the name (drawn in that color):
// this dark a shade of it keeps the hue and the contrast of the game's dark box.
constexpr float RarityShade=.3f;
inline const NameLook& LookFor(bool inReach) {return inReach?InReachLook:OutOfReachLook;}

// The pulse as an alpha: steady (0 Hz) is full; else between 1-2*depth and 1.
inline float Pulse(uint64_t ms,float hz,float depth) {
    if(!(hz>0))return 1;
    return 1-depth+depth*std::sin(float(ms%100000)*.0062831853f*hz);
}
// The rim's color and alpha now.
inline void RimRgba(const NameLook& look,const float rarity[3],uint64_t ms,float out[4]) {
    for(int i=0;i<3;++i)out[i]=std::clamp(look.rarityRim?rarity[i]:RedRgb[i],0.f,1.f);
    out[3]=look.rimAlpha*Pulse(ms,look.pulseHz,look.pulseDepth);
}
// The box's fill: the game's (null when it passed none: black) or the rarity shade.
inline void BoxRgba(const NameLook& look,const float* game,const float rarity[3],float out[4]) {
    for(int i=0;i<3;++i)out[i]=look.rarityBox?std::clamp(rarity[i],0.f,1.f)*RarityShade:game?game[i]:0.f;
    out[3]=look.boxAlpha;
}
// The rim round box {x, y, w, h} (the game's label box), pad pixels thick: top,
// bottom (each across the full outer width), then left and right. Returns the
// strips written: 0 for no pad or an empty box. The strips never cover the box.
inline int Ring(const int32_t box[4],int32_t pad,int32_t out[4][4]) {
    if(!box||pad<=0||box[2]<=0||box[3]<=0)return 0;
    const int32_t x=box[0],y=box[1],w=box[2],h=box[3];
    const int32_t strips[4][4]={{x-pad,y-pad,w+2*pad,pad},{x-pad,y+h,w+2*pad,pad},{x-pad,y,pad,h},{x+w,y,pad,h}};
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)out[i][j]=strips[i][j];
    return 4;
}

// Which item a label paint belongs to, from the layouts that showed it. The game
// lays every name out through one scratch box and paints from each label's own
// record (2026-10-29 log: 2707 of 2707 paints matched their layout by name, none
// by box), so a table keyed by box found only the last name laid out. A slot is
// the pair (box, name); a lookup takes the exact pair, else a name, else a box
// that every slot holding it agrees on; anything ambiguous is 0 (an ordinary
// name). The least recently written slot is reused.
template<int N> struct LabelOwners {
    static_assert(N>0);
    struct Slot {const void* box=nullptr;const void* name=nullptr;uintptr_t unit=0;uint64_t seq=0;};
    Slot slot[N]{};
    uint64_t seq=0;
    // The slot written; the caller may keep more (the name's text) at the same index.
    int Remember(const void* box,const void* name,uintptr_t unit) {
        int at=-1;
        for(int i=0;i<N&&at<0;++i)if(slot[i].seq&&slot[i].box==box&&slot[i].name==name)at=i;
        if(at<0){at=0;for(int i=1;i<N;++i)if(slot[i].seq<slot[at].seq)at=i;}   // an empty slot (seq 0) first, else the oldest
        slot[at]={box,name,unit,++seq};
        return at;
    }
    uintptr_t Unit(const void* box,const void* name) const {
        if(box&&name)for(const auto& s:slot)if(s.seq&&s.box==box&&s.name==name)return s.unit;
        if(const uintptr_t u=Agreed(&Slot::name,name))return u;
        return Agreed(&Slot::box,box);
    }
    // The newest slot holding unit, or -1.
    int Find(uintptr_t unit) const {
        int at=-1;
        for(int i=0;i<N;++i)if(unit&&slot[i].seq&&slot[i].unit==unit&&(at<0||slot[i].seq>slot[at].seq))at=i;
        return at;
    }
private:
    uintptr_t Agreed(const void* Slot::*key,const void* v) const {
        if(!v)return 0;
        uintptr_t u=0;
        for(const auto& s:slot){
            if(!s.seq||!s.unit||s.*key!=v)continue;
            if(u&&s.unit!=u)return 0;   // two items on this address: not knowable from it
            u=s.unit;
        }
        return u;
    }
};

// The pointed item's name when the game lays out none (2026-10-29 headset: an
// item a few cells off showed its name only while the right stick held Show
// items). While an item is pointed at, Show items is held for it (transport),
// so the game lays its name out, and every other name is cut in the label
// layout: the pointed one shows by itself. The right stick holding Show items
// shows every name, as before. vrcam stamps pointedAt (GetTickCount64) each
// frame an item is pointed at; transport writes onlyPointedUntil.
inline std::atomic<uint64_t> pointedAt{0};
inline std::atomic<uint64_t> onlyPointedUntil{0};
constexpr uint64_t PointedFreshMs=200;   // a pointing older than this is over (no frame published it)
inline bool PointedNow(uint64_t at,uint64_t now) {return at&&now>=at&&now-at<PointedFreshMs;}
inline bool HoldsForPointed(bool pointed,bool inWorld,bool stickHolds) {return pointed&&inWorld&&!stickHolds;}
// Until when the other names are cut: while held, the hold's lease ahead; once it
// ends, a short tail while the game lets Show items go, so the other names do not
// flash up; the stick holding: no cut at all.
constexpr uint64_t PointedTailMs=150;
inline uint64_t OnlyPointedUntil(bool holding,bool stickHolds,uint64_t was,uint64_t now,uint64_t leaseMs) {
    if(stickHolds)return 0;
    if(holding)return now+leaseMs;
    return was?std::min(was,now+PointedTailMs):0;
}
inline bool OnlyPointed(uint64_t until,uint64_t now) {return until&&now<until;}

// The pointing hand keeps working while the motion layer is off for reasons that
// leave you in the world (an interaction starting, a buttons rearm, a slow frame),
// as long as it is tracked; never in menus, VR Settings, panels or with the skill
// wheel open. Without it, an A press could find no pointing hand (2026-10-25).
// transport's reasons for the motion layer being off, one bit each.
enum Gate : uint32_t {
    GateControls=1u<<0,GateChain=1u<<1,GateHead=1u<<2,GatePanel=1u<<3,GateSpatial=1u<<4,GateSettings=1u<<5,
    GateChord=1u<<6,GateInventory=1u<<7,GateMenu=1u<<8,GateWorld=1u<<9,GateInteraction=1u<<10,GateBlood=1u<<11,
};
constexpr uint32_t WorldGates=GateControls|GateChain|GateHead|GatePanel|GateSpatial|GateSettings|GateChord|GateInventory|GateMenu|GateWorld;
inline bool AwakeWorld(uint32_t gate,bool wheelOpen) {return (gate&WorldGates)==0&&!wheelOpen;}
constexpr uint64_t HandFreshMs=150;   // the hand's pose read at most this long ago
inline bool HandLive(bool world,bool handTracked,uint64_t ageMs) {return handTracked&&ageMs<=HandFreshMs&&world;}
// "interaction, blood wipe"; "none" while the motion layer is on.
inline void GateText(uint32_t gate,char* out,size_t size) {
    if(!out||!size)return;
    out[0]=0;
    static const char* const parts[]={"motion controls off","no gameplay layer","head not tracked","flat menu panel","menu in the world",
        "VR Settings","settings chord","inventory button","menu button","not in the world","interaction","blood wipe"};
    size_t n=0;
    for(int i=0;i<12;++i) {
        if(!(gate&(1u<<i)))continue;
        const int w=std::snprintf(out+n,size-n,"%s%s",n?", ":"",parts[i]);
        if(w<0||size_t(w)>=size-n){out[size-1]=0;return;}
        n+=size_t(w);
    }
    if(!n)std::snprintf(out,size,"none");
}

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

// In the headset: the pick works only there (desktop play keeps the game's own).
// framevr::Active is true only inside our frame pair (xrBeginFrame to xrEndFrame
// around the game's draw), but the game reads A and asks what Interact takes in
// its own logic, between two pairs, where Active reads false (2026-10-29 log: 10
// A presses, every one handed nothing). So a headset frame up to HeadsetGapMs ago
// counts too; with no headset frame ever (desktop) it is false. A stamp a little
// after now (another thread stamped it after this one read the clock) is a frame just now.
constexpr uint64_t HeadsetGapMs=500;
inline bool InHeadset(bool frameNow,uint64_t lastFrameAt,uint64_t now,uint64_t gapMs=HeadsetGapMs) {
    return frameNow||(lastFrameAt!=0&&(lastFrameAt>=now||now-lastFrameAt<=gapMs));
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
// the same unit id, its cell now and the hero's (for the reach).
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

// One A press followed for PressWatchMs, for the headset log: the unit ids of
// the items on the floor at the press (no pointer is kept) and the chosen one's.
// An item the pick's own later walks no longer find was taken. A walk that loses
// more than one of them at once took none (a failed walk, the hero moved on): the
// watch ends there without a verdict. A new press starts a new watch.
constexpr uint64_t PressWatchMs=3000;
struct PressWatch {
    uint32_t ids[MaxItems]{};bool gone[MaxItems]{};
    int count=0;uint32_t chosen=0;uint64_t at=0;bool took=false;
    bool Active() const {return at!=0;}
    void Cancel() {at=0;}
    void Press(const uint32_t* now,int n,uint32_t chosenId,uint64_t ms) {
        count=std::clamp(n,0,MaxItems);std::copy_n(now,count,ids);std::fill_n(gone,count,false);
        chosen=chosenId;at=ms?ms:1;took=false;
    }
    // After a walk (its ids): the index of the one press item it no longer finds,
    // -1 none, -2 more than one (the watch ends).
    int Walked(const uint32_t* now,int n) {
        if(!at)return -1;
        int lost=-1,lostCount=0;
        for(int i=0;i<count;++i)if(!gone[i]&&std::find(now,now+n,ids[i])==now+n){lost=i;++lostCount;}
        if(lostCount>1){at=0;return -2;}
        if(lost>=0){gone[lost]=true;took=true;}
        return lost;
    }
    // True once when the watch runs out with nothing taken while an item was chosen.
    bool Expired(uint64_t ms) {
        if(!at||ms-at<PressWatchMs)return false;
        at=0;return !took&&chosen;
    }
};
}
