#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <span>

namespace framevr::companions {
// Native pet-type IDs, shared across classes and item-granted summons. Values
// only: native pointers never leave the owning game-thread sample.
constexpr int Types=22,Columns=7,Width=768,RowHeight=144,Height=3*RowHeight;
struct AlivePet {
    uint32_t unitId=0;
    uint8_t petType=0;
    bool operator==(const AlivePet&) const=default;
};
constexpr size_t MaxAlivePets=64;
struct Roster {
    bool known=false;
    std::array<uint16_t,Types> alive{};
    uint32_t relevant=0; // learned/used groups remain visible at zero
    uint8_t stance=1; // 0=Defensive, 1=Balanced, 2=Aggressive
    uint8_t formation=0; // 0=PhalanxArc, 1=ProtectiveRing, 2=SpearheadWedge, 3=CorridorColumn
    bool anchorActive=false;
    bool freeRoam=false;
    bool commanderBadge=false;
    bool operator==(const Roster&) const=default;
};
struct Definition {int type,skill;const char* name;};
inline constexpr Definition Definitions[]={
    {4,70,"Skeletons"},{5,80,"Mages"},{3,75,"Golem"},{6,95,"Revives"},
    {2,32,"Valkyrie"},{8,28,"Decoy"},
    {10,221,"Ravens"},{11,227,"Sp. wolves"},{12,237,"Dire wolves"},
    {15,247,"Bear"},{13,226,"Spirit"},{14,222,"Vine"},
    {16,268,"Shadow"},{17,271,"Sentries"},{18,-1,"Traps"},{19,62,"Hydras"},
    {20,373,"Demons"},{21,382,"Bound"},{7,-1,"Mercenary"}
};
constexpr uint32_t Bit(int type){return type>0&&type<Types?1u<<type:0;}
inline int SkillType(int skill) {
    switch(skill) {
    case 70:return 4;case 80:return 5;case 75:case 85:case 90:case 94:return 3;case 95:return 6;
    case 32:return 2;case 28:return 8;case 221:return 10;case 227:return 11;case 237:return 12;
    case 247:return 15;case 226:case 236:case 246:return 13;case 222:case 231:case 241:return 14;
    case 268:case 279:return 16;case 257:case 261:case 262:case 271:case 272:case 276:return 17;
    case 62:return 19;case 373:case 376:case 377:return 20;case 382:return 21;
    default:return 0;
    }
}
inline int Count(const Roster& r) {
    int n=0;if(r.known)for(const auto& d:Definitions)if(r.relevant&Bit(d.type))++n;return n;
}
inline int Rows(const Roster& r){return (Count(r)+Columns-1)/Columns;}
inline int UsedHeight(const Roster& r){return Rows(r)*RowHeight;}

// Steam 3.3.93787: unit+0x10 -> player data; +0x98 -> pets; pets[0]
// points to 32-byte per-type lists. Node flags bit 0 marks a dead hireling.
// Layout guards live in vr/companions.cpp. Never call a native pet accessor:
// several assert on absent data. Read complete bounded lists or report unknown.
template<class Read>
Roster ReadRoster(uintptr_t player,unsigned typeCount,Read read) {
    Roster result;
    if(typeCount<20||typeCount>64)return result;
    uint32_t header[4]{};uintptr_t data=0,pets=0,lists=0;
    if(!read(player,header)||header[0]!=0||header[1]>7||header[3]==0||header[3]==17||
       !read(player+0x10,data)||!data||!read(data+0x98,pets))return result;
    if(!pets){result.known=true;return result;}
    if(!read(pets,lists)||!lists||lists>UINTPTR_MAX-typeCount*32)return result;
    struct List {uintptr_t head;int32_t current,maximum;};
    struct Node {uint32_t flags,id;uint8_t reserved[16];uintptr_t next;};
    static_assert(sizeof(List)==16&&offsetof(Node,next)==24&&sizeof(Node)==32);
    std::array<uintptr_t,512> seen{};unsigned visited=0;
    for(unsigned type=1;type<typeCount;++type) {
        List list{};
        if(!read(lists+type*32,list)||list.current<0||list.current>512||list.maximum<0||list.maximum>4096)return {};
        unsigned members=0,alive=0;
        for(auto node=list.head;node;) {
            if(visited==seen.size()||members>=unsigned(list.current))return {};
            for(unsigned i=0;i<visited;++i)if(seen[i]==node)return {};
            seen[visited++]=node;
            Node value{};if(!read(node,value)||value.id==UINT32_MAX)return {};
            ++members;if(!(value.flags&1))++alive;node=value.next;
        }
        if(members!=unsigned(list.current))return {};
        if(type<Types) {
            result.alive[type]=uint16_t(alive);
            // Hireling's base maximum is 1 even if never hired. A dead hireling
            // retains its roster entry, so it can still display a useful zero.
            if(members||(type!=7&&list.maximum>0))result.relevant|=Bit(type);
        }
    }
    result.known=true;return result;
}

// Mobile combat summons that participate in Commander of the Dead squad formations:
// Necromancer (3=Golem, 4=Skeletons, 5=Mages, 6=Revives), Druid (11=Sp. wolves,
// 12=Dire wolves, 15=Bear), Amazon (2=Valkyrie), Assassin (16=Shadow).
constexpr uint32_t ArmyTypeMask=
    Bit(3)|Bit(4)|Bit(5)|Bit(6)|Bit(11)|Bit(12)|Bit(15)|Bit(2)|Bit(16);

template<class Read>
size_t CollectAlivePets(uintptr_t player,unsigned typeCount,uint32_t typeMask,std::span<AlivePet> out,Read read) {
    if(typeCount<20||typeCount>64||out.empty())return 0;
    uint32_t header[4]{};uintptr_t data=0,pets=0,lists=0;
    if(!read(player,header)||header[0]!=0||header[1]>7||header[3]==0||header[3]==17||
       !read(player+0x10,data)||!data||!read(data+0x98,pets)||!pets)return 0;
    if(!read(pets,lists)||!lists||lists>UINTPTR_MAX-typeCount*32)return 0;
    struct List {uintptr_t head;int32_t current,maximum;};
    struct Node {uint32_t flags,id;uint8_t reserved[16];uintptr_t next;};
    static_assert(sizeof(List)==16&&offsetof(Node,next)==24&&sizeof(Node)==32);
    std::array<uintptr_t,512> seen{};unsigned visited=0;
    size_t count=0;
    for(unsigned type=1;type<typeCount&&type<Types;++type) {
        if(!(typeMask&Bit(int(type))))continue;
        List list{};
        if(!read(lists+type*32,list)||list.current<0||list.current>512||list.maximum<0||list.maximum>4096)return 0;
        unsigned members=0;
        for(auto node=list.head;node;) {
            if(visited==seen.size()||members>=unsigned(list.current))return 0;
            for(unsigned i=0;i<visited;++i)if(seen[i]==node)return 0;
            seen[visited++]=node;
            Node value{};if(!read(node,value)||value.id==UINT32_MAX||value.id==0)return 0;
            ++members;
            if(!(value.flags&1)&&count<out.size())out[count++]={value.id,uint8_t(type)};
            node=value.next;
        }
        if(members!=unsigned(list.current))return 0;
    }
    return count;
}
}
