// Physical container contact -> native object operation, D2R 3.3.93787.
#include "breakables.h"
#include "breakables_signatures.h"
#include "melee.h"
#include "melee_weapon.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include "../steamvr/world_coordinates.h"
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
namespace framevr {void MeleeImpact(int hand);}

namespace breakables {
namespace {
using namespace framevr::melee;
using namespace abi;
using ModeFn=int(*)(uintptr_t,uintptr_t,uintptr_t,int,int,uint32_t,int,int);
ModeFn originalMode=nullptr;
std::atomic<bool> ready{false};
std::atomic<unsigned> broken[2]{},blocked{0},refused{0};
std::array<HitSet,2> attempted; // separate from monster hits, cleave and cadence
struct Context {uintptr_t game=0,player=0;uint32_t object=0;};
thread_local Context physical;
struct Scope {
    Context saved=physical;
    Scope(uintptr_t game,uintptr_t player,uint32_t object){physical={game,player,object};}
    ~Scope(){physical=saved;}
};
int StartModeAt(uintptr_t caller,uintptr_t game,uintptr_t player,uintptr_t skill,int mode,int type,uint32_t id,int a,int b) {
    // Only the extra kick requested inside this physically struck barrel.
    // Button kicks, other skills, other players and other threads stay native.
    if(physical.player&&physical.game==game&&physical.player==player&&type==2&&id==physical.object&&
       caller==d2rsig::Addr(BarrelKickReturnRva))return 1;
    return originalMode(game,player,skill,mode,type,id,a,b);
}
int StartMode(uintptr_t game,uintptr_t player,uintptr_t skill,int mode,int type,uint32_t id,int a,int b) {
    return StartModeAt(reinterpret_cast<uintptr_t>(_ReturnAddress()),game,player,skill,mode,type,id,a,b);
}
template<class T>bool Read(uintptr_t address,T& out) {
    return address>0x10000&&address<=UINTPTR_MAX-sizeof out&&
        framevr::ReadMemory(&out,reinterpret_cast<void*>(address),sizeof out);
}
struct Object {uint32_t id=0,classId=0;int x=0,y=0,sizeX=0,sizeY=0;uint8_t operation=0;};
bool Inspect(uintptr_t game,uintptr_t unit,Object& o) {
    uint32_t header[4]{},flags=0;uintptr_t back=0,data=0,row=0,path=0,room=0;
    std::array<uint8_t,0x168> fields{};
    if(!Read(unit,header)||header[0]!=2||header[3]!=0||header[1]>4095||
       !Read(unit+0x124,flags)||!(flags&2)||
       !Read(unit+0xd8,back)||back!=game||!Read(unit+0x10,data)||!Read(data,row)||!Read(row,fields)||
       !Read(unit+0x38,path)||!Read(path,room)||room<0x10000||
       !Read(path+0x10,o.x)||!Read(path+0x14,o.y)||o.x<=0||o.y<=0||o.x>65535||o.y>65535)return false;
    // Compiled Objects rows: attackable, selectable neutral state and one of
    // the native urn/basket, barrel or exploding-barrel handlers. No corpses,
    // ordinary chests, doors, shrines, portals, racks or special quest objects.
    o.operation=fields[0x15e];
    if(fields[0xe8]!=1||fields[0x85]!=1||(o.operation!=3&&o.operation!=5&&o.operation!=7))return false;
    std::memcpy(&o.sizeX,fields.data()+0x90,4);std::memcpy(&o.sizeY,fields.data()+0x94,4);
    if(o.sizeX<1||o.sizeY<1||o.sizeX>8||o.sizeY>8)return false;
    o.id=header[2];o.classId=header[1];return true;
}
bool Player(uintptr_t game,uintptr_t player,Vec& pos) {
    uintptr_t liveGame=0,livePlayer=0,back=0,path=0,room=0;uint32_t header[4]{};uint16_t xy[4]{};
    if(!melee::LocalServer(liveGame,livePlayer)||game!=liveGame||player!=livePlayer||
       !Read(player,header)||header[0]!=0||header[3]==0||header[3]==17||header[3]==5||header[3]==6||
       !Read(player+0xd8,back)||back!=game||!Read(player+0x38,path)||!Read(path,xy)||
       !Read(path+0x20,room)||room<0x10000)return false;
    pos={2*(xy[1]+xy[0]/65536.f),0,2*(xy[3]+xy[2]/65536.f)};return true;
}
bool Weapon(uintptr_t player,const Sample& s) {
    if(s.handIndex<0||s.handIndex>1||s.shield||!s.active)return false;
    const bool dual=s.weapons.id[0]&&s.weapons.id[1];
    if(!dual)return s.handIndex==0||s.claws;   // a form's two claws: both strike, no weapon to select
    const uintptr_t weapon=meleeweapon::Select(player,s.handIndex,s.weapons);
    uint32_t id=0;return Read(weapon+8,id)&&id==s.weapons.displayed[s.handIndex];
}
Body Volume(const Object& o,const Sample& s) {
    const float scale=s.unitsPerMetre;
    // Native footprint, approximate vertical volume in game world units.
    // Large urns are taller; remaining pots/baskets are lower than barrels.
    const bool large=o.classId==4||o.classId==94||o.classId==95;
    const float radius=std::clamp(float(std::max(o.sizeX,o.sizeY))/scale,.16f,.6f);
    const float height=std::max(2*radius,(large?4.8f:o.operation==3?3.2f:4.f)/scale);
    const Vec ground=framevr::coordinates::RenderGround({2.f*o.x/scale,0,2.f*o.y/scale},s.player.y);
    return {ground+Vec{0,radius,0},ground+Vec{0,height-radius,0},radius};
}
}
void Install() {
    if(ready.load())return;
    for(const auto& g:Guards)if(!d2rsig::Check(g.rva,g.bytes,g.size))return;
    if(!d2rsig::Hook(ModeRva,ModeSig,sizeof ModeSig,reinterpret_cast<void*>(&StartMode),reinterpret_cast<void**>(&originalMode)))return;
    ready=true;
    framevr::logging::Write("Frame breakables: physical pots/barrels ready; button kick retained");
}
void Apply(uintptr_t game,uintptr_t player,std::span<const Strike> strikes,bool (*allowed)(const Strike&)) {
    if(!ready.load()||strikes.empty()||!allowed)return;
    Vec playerPos{};if(!Player(game,player,playerPos))return;
    bool any=false;for(const auto& r:strikes)if(!r.sample.shield&&allowed(r)&&Weapon(player,r.sample)){any=true;break;}
    if(!any)return;
    // Snapshot nearby IDs before invoking native operations: exploding barrels
    // can change the unit list. Re-resolve each object before every contact.
    uintptr_t buckets[128]{};if(!Read(game+0x546*8,buckets))return;
    std::array<uint32_t,256> ids{};size_t count=0;unsigned visited=0;
    for(uintptr_t unit:buckets)for(unsigned chain=0;unit&&chain<256&&visited++<4096&&count<ids.size();++chain) {
        uintptr_t next=0;if(!Read(unit+0x158,next))break;
        Object o;
        if(Inspect(game,unit,o)&&std::hypot(2.f*o.x-playerPos.x,2.f*o.y-playerPos.z)<24)ids[count++]=o.id;
        unit=next;
    }
    for(size_t i=0;i<count;++i)for(const auto& r:strikes) {
        const auto& s=r.sample;
        if(!allowed(r)||!Weapon(player,s)||!r.sweep.stroke)continue;
        if(!Player(game,player,playerPos))return; // a native trap may have killed us
        if(!framevr::coordinates::MatchesPlayer(playerPos,s.player,s.unitsPerMetre))continue;
        const auto unit=melee::ServerUnit(game,2,ids[i]);Object o;
        if(!Inspect(game,unit,o)||o.id!=ids[i])continue;
        if(attempted[s.handIndex].Contains(r.session,r.sweep.stroke,o.id))continue;
        if(!Touches(r.sweep.before,r.sweep.after,Volume(o,s)))continue;
        // Native interaction line test temporarily excludes endpoint collision,
        // so the pot's own footprint cannot masquerade as an intervening wall.
        if(reinterpret_cast<int(*)(uintptr_t,uintptr_t,int)>(d2rsig::Addr(LineRva))(player,unit,0x804)) {++blocked;continue;}
        if(!allowed(r))continue;
        if(!attempted[s.handIndex].Insert(r.session,r.sweep.stroke,o.id))continue;
        {Scope scope(game,player,o.id);
         reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(OperateRva))(game,player,unit);}
        uint32_t after[4]{};
        const auto resolved=melee::ServerUnit(game,2,o.id);
        if(Read(resolved,after)&&after[0]==2&&after[2]==o.id&&after[3]!=0) {
            ++broken[s.handIndex];framevr::MeleeImpact(s.handIndex);
        } else ++refused;
    }
}
void Report() {
    const auto r=broken[0].exchange(0),l=broken[1].exchange(0),w=blocked.exchange(0),f=refused.exchange(0);
    if(!r&&!l&&!w&&!f)return;
    char line[180];std::snprintf(line,sizeof line,"Frame breakables: right broken=%u left broken=%u walls=%u native refused=%u",r,l,w,f);
    framevr::logging::Write(line);
}
}
