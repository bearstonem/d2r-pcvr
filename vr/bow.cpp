// Physical basic arrows, D2R 3.3.93787. Native calls are confined to their
// owning game/UI threads. Rendering consumes immutable release/flight data.
#include <windows.h>
#include <D2RLPlugin/api.h>
#include <array>
#include <atomic>
#include <mutex>
#include <span>
#include <cstdio>
#include <cstring>
#include "bow.h"
#include "crossbow.h"
#include "skeletons.h"
#include "bow_signatures.h"
#include "holy_bolt_signatures.h"
#include "native_ui_signatures.h"
#include "spells.h"
#include "melee.h"
#include "prop_scan.h"
#include "melee_weapon.h"
#include "combat.h"
#include "projectiles.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include "../shared/d2r_vr_state.h"
#include "../steamvr/transport.h"
#include "../steamvr/callback_ticket.h"
#include "../steamvr/client_retirement.h"
#include "../steamvr/quiver_impact.h"
#include "../steamvr/spell_aim_assist.h"
#include "../steamvr/holy_bolt.h"
namespace gamestate {uint64_t LocalPlayer();bool MenuOpen();uint32_t HandsKey();uint32_t WeaponSet();uint32_t WeaponType();framevr::dualwield::Equipment MeleeEquipment();}
namespace bowvr {
namespace {
using namespace framevr::bowflight;
using namespace native;
const D2RL::PluginContext* context=nullptr;
const D2RL::ThreadService* threads=nullptr;
const D2RL::DataTableService* tables=nullptr;
std::atomic<bool> enabled{false},ready{false};
using ShootFn=uintptr_t(*)(uintptr_t,int,uintptr_t,int,int,int,int,int,int,int,int);
using CollisionFn=int(*)(uintptr_t,uintptr_t);
using ClientTickFn=void(*)(uintptr_t,float);
using ClientKillFn=void(*)(uintptr_t);
using TransformFn=uintptr_t(*)(uintptr_t,uint32_t,uintptr_t);
using MissileSoundFn=int(*)(int,uintptr_t,int,int,bool);
ShootFn originalShoot=nullptr;CollisionFn originalCollision=nullptr;
ClientTickFn originalClientTick=nullptr;TransformFn originalTransform=nullptr;
ClientKillFn originalClientKill=nullptr;
MissileSoundFn originalMissileSound=nullptr;
bool soundIn=false;
bool shootIn=false,collisionIn=false,clientIn=false,killIn=false,transformIn=false,checked=false;
std::mutex mutex;
struct Arrow {
    Flight flight{};
    framevr::aimassist::Homing homing{};
    uintptr_t game=0,server=0,client=0,room=0;
    uint32_t serverId=0,clientId=0;
    VisualBinding visualBinding{};
    bool visualDone=false,creating=false,reserved=false;
    uint64_t renderAt=0;
    std::array<float,16> rendered{},previous{};
};
// Two level-capped Charged Bolt volleys can each keep 24 missiles alive for
// 77 ticks, with a seven-tick fastest cast. Leave room for concurrent bolts
// and client cleanup without cutting a paid volley short.
std::array<Arrow,768> arrows{};
std::atomic<size_t> usedSlots{0};
// Rendering visits many unrelated entities. Scan only allocated records, not
// the maximum volley capacity, and shrink that range as native units retire.
auto UsedArrows(){return std::span<Arrow>(arrows.data(),usedSlots.load(std::memory_order_relaxed));}
struct Preview {
    Shot pose{};
    bool active=false,visible=false;
    bool recreate=false; // quiver changed: replace the held client without recovery delay
    uintptr_t client=0;
    uint32_t clientId=0,missile=0; // native id of the held client, 0 = basic arrow
    VisualBinding binding{};
    uint64_t renderAt=0;
    std::array<float,16> rendered{},previous{};
};
Preview preview;
PreviewRenderPose previewRenderPose;
std::atomic<bool> previewClient{false};
std::atomic<uint64_t> previewReadyAt{0};
PreviewRecovery previewRecovery;
ClientRetirements clientRetirements; // mutex; UI requests, native client hooks retire
ImpactVisuals impactVisuals; // visual placement only; native effects own lifetime
std::atomic<bool> impactClients{false};
Gate current{};uintptr_t clientHero=0;
Once once;
Once crossbowOnce;
framevr::crossbow::FireRate crossbowRate; // mutex; UI stat reader -> game thread
framevr::attackspeed::Cadence crossbowCadence; // game thread only
std::atomic<unsigned> crossbowFired{0},crossbowCycleMs{0};
Once spellOnce[2];
framevr::spellcast::CastCue castCue; // UI thread only
framevr::aimassist::History assistHistory[2]; // game thread only
struct AssistStats {
    std::atomic<unsigned> casts{0},bodies{0},heightMisses{0},eligible{0},blocked{0},direct{0},corrected{0};
    std::atomic<int> mode{0};
    std::atomic<unsigned> steered{0},lost{0};
    std::atomic<float> degrees{0};
};
AssistStats assistStats[2];
std::array<Shot,4> pending{};size_t pendingCount=0;
framevr::CallbackTicket gameCallback,uiCallback;
uint64_t maintenanceAt=0;
std::atomic<unsigned> renderCount{0},serverCount{0},clientCount{0},submitted{0},spawned{0},visuals{0},stale{0},refused{0},contacts{0},walls{0},expired{0};
std::atomic<unsigned> lastLatency{0},lastSpeedCm{0};
std::atomic<unsigned> boundMeshes{0},ignoredEntities{0};
std::atomic<unsigned> positionMismatch{0};
std::atomic<unsigned> holyAllowed{0},holyRejected{0};
std::atomic<unsigned> previewCreated{0},previewRefused{0},previewRecovered{0},previewAbandoned{0},uiRecovered{0};
std::atomic<unsigned> gameRecovered{0};
std::atomic<unsigned> quiverShots{0},quiverFallbacks{0},quiverRefused{0};
std::atomic<unsigned> doubleThrows[2]{},throwRefused{0};
struct Launch {
    Shot shot{};uintptr_t game=0,player=0;int slot=-1;
    framevr::aimassist::Homing homing{};
    unsigned created=0,count=0;
    std::array<int,24> slots{};
};
thread_local Launch launch;
thread_local int creatingVisual=-1;
thread_local uint64_t visualSession=0,visualSequence=0;
thread_local uint64_t previewCreatingSession=0;
thread_local uint32_t previewCreatingMissile=0;
thread_local const Flight* finishingImpact=nullptr;
thread_local uint64_t creatingImpact=0;
int MissileSound(int sound,uintptr_t unit,int a,int b,bool c) {
    // The nocked model is not a fired projectile. Suppress only its creation
    // scope's travel sound; genuine releases and other game audio pass through.
    if(previewCreatingSession)return 0;
    return originalMissileSound(sound,unit,a,b,c);
}
template<class T> bool Read(uintptr_t p,T& out) {
    return p>0x10000&&framevr::ReadMemory(&out,reinterpret_cast<const void*>(p),sizeof out);
}
using HolyFilter=framevr::holybolt::CollisionFilter;
HolyFilter CollisionFilter() {
    uintptr_t filter=0;
    // Type 3 selects the native player/monster filter. Its table is initialized
    // by the game; verify the consumer's table stride and argument ABI below.
    if(!Read(d2rsig::Addr(holyabi::TableRva+3*16),filter)||filter!=d2rsig::Addr(holyabi::FilterRva))return nullptr;
    return reinterpret_cast<HolyFilter>(filter);
}
bool TableRow(uintptr_t game,D2RL::DataTables::TableId table,uint32_t id,uint32_t size,D2RL::DataTables::RowView& row) {
    uint8_t bank=0;row={};row.structSize=sizeof row;
    return tables&&tables->getRow&&Read(game+0x106,bank)&&bank>=1&&bank<=3&&
        tables->getRow(context,D2RL::DataTables::Bank(bank),table,id,&row)==D2RL::DataTables::Result::Success&&
        row.row&&row.rowSize==size;
}
bool HolyRow(uintptr_t game,D2RL::DataTables::RowView& row) {
    int16_t hit=-1;uint8_t collision=0,friendly=0;
    return TableRow(game,D2RL::DataTables::TableId::Missiles,55,460,row)&&
        Read(reinterpret_cast<uintptr_t>(row.row)+0x2e,hit)&&hit==7&&
        Read(reinterpret_cast<uintptr_t>(row.row)+0x1ab,collision)&&collision==3&&
        Read(reinterpret_cast<uintptr_t>(row.row)+0x1af,friendly)&&friendly==1;
}
framevr::holybolt::Target HolyTarget(uintptr_t game,uint32_t id,bool hostile) {
    D2RL::DataTables::RowView row{};
    if(!TableRow(game,D2RL::DataTables::TableId::MonStats,id,508,row))return framevr::holybolt::Target::Ignore;
    std::array<uint8_t,508> copy{};
    if(!Read(reinterpret_cast<uintptr_t>(row.row),copy))return framevr::holybolt::Target::Ignore;
    return framevr::holybolt::AssistTarget(copy,hostile);
}
template<class T> void Param(std::array<uint8_t,0x98>& p,size_t offset,T value) {
    std::memcpy(p.data()+offset,&value,sizeof value);
}
bool Position(uintptr_t unit,Vec& out) {
    uintptr_t path=0;uint16_t p[4]{};
    if(!Read(unit+0x38,path)||!Read(path,p))return false;
    out={2*(p[1]+p[0]/65536.f),0,2*(p[3]+p[2]/65536.f)};return true;
}
bool Live(const Shot& s) {
    Gate gate;{std::lock_guard lock(mutex);gate=current;}
    return enabled.load()&&ready.load()&&Allowed(s,gate,GetTickCount64())&&
        s.session==gamestate::LocalPlayer()&&!gamestate::MenuOpen()&&
        s.equipment==(gamestate::HandsKey()^(gamestate::WeaponSet()<<30));
}
bool Same(const Arrow& a,const Arrow& b) {
    return a.flight.shot.kind==b.flight.shot.kind&&a.flight.shot.session==b.flight.shot.session&&a.flight.shot.sequence==b.flight.shot.sequence&&
        a.flight.shot.member==b.flight.shot.member;
}
void Recount();
void Store(size_t slot,const Arrow& a) {
    std::lock_guard lock(mutex);
    if(Same(arrows[slot],a)) {
        // UI may have attached the client/entity while the game was stepping.
        arrows[slot].flight=a.flight;arrows[slot].homing=a.homing;arrows[slot].room=a.room;Recount();
    }
}
void Recount() { // mutex held
    impactClients=impactVisuals.Pending();
    unsigned r=(preview.binding.Bound()?1:0)+(impactClients.load()?1:0),s=0,c=0;
    auto size=usedSlots.load(std::memory_order_relaxed);
    while(size&&!arrows[size-1].flight.alive&&!arrows[size-1].client&&!arrows[size-1].creating&&!arrows[size-1].reserved)--size;
    usedSlots.store(size,std::memory_order_relaxed);
    for(const auto& a:UsedArrows()){if(a.visualBinding.Bound())++r;if(a.flight.alive)++s;if(a.client)++c;}
    renderCount=r;serverCount=s;clientCount=c;
    previewClient=preview.client!=0||clientRetirements.Pending();
}
int End(size_t slot,Arrow& a,uintptr_t game,uintptr_t unit) {
    a.flight.alive=false;a.flight.updatedAt=GetTickCount64();Store(slot,a);
    reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t,int)>(d2rsig::Addr(HitRva))(game,unit,0,1);
    return 2;
}
// Spells and quiver arrows keep their native server missile at the physical
// contact, so splash, explosions and secondary missiles start at the impact.
bool MoveSpell(Arrow& a,Vec point) {
    if(a.flight.shot.kind!=Kind::Spell&&!SkillRelease(a.flight.shot))return true;
    const auto grid=GridPoint(a.flight.shot,point)*a.flight.shot.unitsPerMetre;
    const int x=int(grid.x*.5f),y=int(grid.z*.5f);
    const auto room=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(RoomPointRva))(a.room,x,y);
    if(!room||!spellvr::Position(a.game,a.server,room,x,y))return false;
    a.room=room;return true;
}
bool Body(uintptr_t unit,uint32_t classId,const Shot& shot,Vec& bottom,Vec& top,float& radius) {
    uint8_t bank=0,fields[16]{};Vec p{};
    if(!Position(unit,p)||!Read(unit+0x1bd,bank)||bank>3||classId>4095)return false;
    const auto row=reinterpret_cast<uintptr_t(*)(uint8_t,int)>(d2rsig::Addr(StatsRva))(bank,int(classId));
    if(!Read(row,fields)||fields[8]<1||fields[8]>8||fields[9]<1||fields[9]>8)return false;
    const float upm=shot.unitsPerMetre,height=std::clamp((fields[13]?fields[13]:72)*.1f/upm,.4f,4.f);
    radius=std::min(std::max(.12f,std::max(fields[8],fields[9])*.7f/upm),height*.45f)+.025f;
    bottom=RenderGround(shot,p)+Vec{0,radius,0};top=bottom;top.y=shot.player.y+height-radius;
    return true;
}
void Guide(Arrow& a,uintptr_t game,uintptr_t player) {
    if(!a.homing.active)return;
    const auto target=melee::ServerUnit(game,1,a.homing.target);
    uint32_t h[4]{};Vec bottom{},top{};float radius=0;
    bool visible=target&&Read(target,h)&&h[0]==1&&h[1]==a.homing.classId&&h[2]==a.homing.target&&h[3]!=0&&h[3]!=12&&
        !a.flight.Seen(h[2])&&Body(target,h[1],a.flight.shot,bottom,top,radius)&&
        reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(game,player,target)&&
        (!framevr::holybolt::Skill(a.flight.shot.skill)||HolyTarget(game,h[1],true)==framevr::holybolt::Target::Enemy);
    if(visible) {
        auto room=a.room;
        const auto end=(bottom+top)*.5f;
        visible=WallEntry(GridPoint(a.flight.shot,a.flight.Point()),GridPoint(a.flight.shot,end),a.flight.shot.unitsPerMetre,[&](int x,int z) {
            if(!room)return true;
            room=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(RoomPointRva))(room,x,z);
            return !room||reinterpret_cast<uint16_t(*)(uintptr_t,int,int,int,int)>(d2rsig::Addr(MaskRva))(room,x,z,1,0x804)!=0;
        })>=1;
    }
    Vec direction{};
    if(a.homing.Step(a.flight,bottom,top,radius,visible,.04f,direction)) {
        a.flight.Redirect(direction);++assistStats[a.flight.shot.hand].steered;
    }
    if(!a.homing.active)++assistStats[a.flight.shot.hand].lost;
}
int Collision(uintptr_t game,uintptr_t unit) {
    if(!serverCount.load(std::memory_order_relaxed))return originalCollision(game,unit);
    uint32_t header[3]{};Arrow a;size_t slot=arrows.size();
    if(!Read(unit,header))return originalCollision(game,unit);
    {
        std::lock_guard lock(mutex);
        for(size_t i=0;i<usedSlots.load(std::memory_order_relaxed);++i)if(arrows[i].server==unit&&arrows[i].serverId==header[2]&&
            arrows[i].game==game&&arrows[i].flight.shot.session==current.session){slot=i;a=arrows[i];break;}
    }
    if(slot==arrows.size())return originalCollision(game,unit);
    // Already-fired arrows survive menu/weapon/head movement. Only a session
    // change or disabling motion combat retires them, with no delayed attack.
    if(!enabled.load()||!a.flight.alive||a.flight.shot.session!=gamestate::LocalPlayer())return End(slot,a,game,unit);
    uintptr_t localGame=0,player=0;
    if(!melee::LocalServer(localGame,player)||localGame!=game)return End(slot,a,game,unit);
    combat::ObserveLocalPlayer(player);
    if(a.flight.NativePath()) {
        // Native update retains Charged Bolt's wandering, lifetime, grid
        // collision and damage. Only its visible position is followed here.
        const int result=originalCollision(game,unit);
        Vec position{};
        if(Position(unit,position))a.flight.NativePosition(position,GetTickCount64());
        if(result==2)a.flight.alive=false;
        Store(slot,a);return result;
    }
    const int remaining=reinterpret_cast<int(*)(uintptr_t)>(d2rsig::Addr(FrameGetRva))(unit)-1;
    reinterpret_cast<void(*)(uintptr_t,int)>(d2rsig::Addr(FrameSetRva))(unit,remaining);
    if(remaining<=0){++expired;return End(slot,a,game,unit);}
    uintptr_t path=0;int velocity=0;
    if(!Read(unit+0x38,path)||!Read(path+0xa0,velocity))return End(slot,a,game,unit);
    a.flight.speed=FlightSpeed(a.flight.shot,velocity);
    // Reacquire the proxy room before steering/terrain queries; native rooms
    // can unload between ticks. No native target pointer survives this call.
    a.room=reinterpret_cast<uintptr_t(*)(uintptr_t)>(d2rsig::Addr(RoomRva))(unit);
    Guide(a,game,player);
    const float distance=a.flight.distance,step=a.flight.speed*.04f;
    const Vec from=distance==0?a.flight.shot.origin:a.flight.TipAt(distance),to=a.flight.TipAt(distance+step);
    const float length=ProjectileLength(a.flight.shot);
    const float swept=step+(distance==0?length:0);
    const auto advance=[&](float fraction){return std::max(0.f,distance+swept*fraction-(distance==0?length:0));};
    // Native missile barriers, never monster occupancy: bodies are tested in
    // 3D below. Closed/unloaded room cells are conservative solid barriers.
    float stop=WallEntry(GridPoint(a.flight.shot,from),GridPoint(a.flight.shot,to),a.flight.shot.unitsPerMetre,[&](int x,int z) {
        if(x<1||z<1||x>65534||z>65534||!a.room)return true;
        const auto room=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(RoomPointRva))(a.room,x,z);
        if(!room)return true;
        a.room=room;
        return reinterpret_cast<uint16_t(*)(uintptr_t,int,int,int,int)>(d2rsig::Addr(MaskRva))(room,x,z,1,0x804)!=0;
    });
    // Until terrain-height queries are available, use the launch floor. Walls
    // extend vertically; this deliberately does not permit shooting over them.
    const float floor=a.flight.shot.player.y+FloorClearance;
    if(from.y<=floor)stop=0;
    else if(to.y<=floor)stop=std::min(stop,(from.y-floor)/(from.y-to.y));
    struct Touch {float fraction;uintptr_t unit;uint32_t id;};
    std::array<Touch,64> touches{};size_t n=0;
    const bool holy=a.flight.shot.kind==Kind::Spell&&framevr::holybolt::Skill(a.flight.shot.skill);
    D2RL::DataTables::RowView holyRow{};const auto filter=holy?CollisionFilter():nullptr;
    uint8_t bank=0;
    if(holy&&(!filter||!HolyRow(game,holyRow)||!Read(game+0x106,bank)))return End(slot,a,game,unit);
    const framevr::holybolt::CollisionArgs holyArgs{game,unit,player,reinterpret_cast<uintptr_t>(holyRow.row)};
    const auto canTouch=[&](uintptr_t target) {
        if(!holy)return reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(game,player,target)!=0;
        const bool allowed=framevr::holybolt::CanCollide(filter,bank,target,holyArgs);
        if(allowed)++holyAllowed;else ++holyRejected;
        return allowed;
    };
    uintptr_t buckets[128]{};unsigned visited=0;
    if(!Read(game+0x4c6*8,buckets))return End(slot,a,game,unit);
    for(auto target:buckets)for(unsigned chain=0;target&&chain<256&&visited++<4096;++chain) {
        uintptr_t next=0;uint32_t h[4]{};
        if(!Read(target+0x158,next)||!Read(target,h))break;
        if(h[0]==1&&h[3]!=0&&h[3]!=12&&!a.flight.Seen(h[2])) {
            Vec bottom{},top{};float radius=0,fraction=0;
            Vec position{};
            // The largest capsule radius below is <2m. Reject distant units
            // before any monster-table lookup or detailed intersection work.
            const bool positioned=Position(target,position);
            const auto ground=RenderGround(a.flight.shot,position);
            const bool nearby=positioned&&ground.x>=std::min(from.x,to.x)-2&&
                ground.x<=std::max(from.x,to.x)+2&&ground.z>=std::min(from.z,to.z)-2&&ground.z<=std::max(from.z,to.z)+2;
            if(nearby&&Body(target,h[1],a.flight.shot,bottom,top,radius)&&CapsuleEntry(from,to,bottom,top,radius,fraction)&&
               fraction<stop&&canTouch(target)) {
                const Touch touch{fraction,target,h[2]};
                if(n<touches.size())touches[n++]=touch;
                else {
                    auto worst=std::max_element(touches.begin(),touches.end(),[](const Touch& x,const Touch& y){return x.fraction<y.fraction;});
                    if(fraction<worst->fraction)*worst=touch;
                }
            }
        }
        target=next;
    }
    std::sort(touches.begin(),touches.begin()+n,[](const Touch& x,const Touch& y){return x.fraction<y.fraction;});
    for(size_t i=0;i<n;++i) {
        const auto& t=touches[i];
        uint32_t mode=0;
        // A preceding hit/proc can kill or remove another candidate.
        if(melee::ServerUnit(game,1,t.id)!=t.unit||!Read(t.unit+0xc,mode)||mode==0||mode==12)continue;
        if(!a.flight.Remember(t.id))return End(slot,a,game,unit);
        if(!MoveSpell(a,from+(to-from)*t.fraction))return End(slot,a,game,unit);
        // Native missile owns weapon damage, costs, criticals, leech, procs,
        // immunities and pierce. It does not get a second ground-grid hit.
        static uint64_t logSession=0;static unsigned hitSamples=0;
        if(logSession!=a.flight.shot.session){logSession=a.flight.shot.session;hitSamples=0;}
        const bool logHit=holy&&hitSamples<32;
        using StatFn=int(*)(uintptr_t,int,int);
        const auto stat=reinterpret_cast<StatFn>(d2rsig::Addr(nativeui::abi::StatRva));
        const int before=logHit?stat(t.unit,6,0):0;
        const int result=reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t,int)>(d2rsig::Addr(HitRva))(game,unit,t.unit,0);
        if(logHit) {
            ++hitSamples;
            const int after=melee::ServerUnit(game,1,t.id)==t.unit?stat(t.unit,6,0):-1;
            char line[208];snprintf(line,sizeof line,"Frame Holy Bolt contact: target=%u native_result=%d life_fixed=%d -> %d (-1 means removed)",t.id,result,before,after);
            framevr::logging::Write(line);
        }
        ++contacts;framevr::MeleeImpact();
        if(result==2) {
            a.flight.distance=advance(t.fraction);a.flight.alive=false;
            a.flight.Contact(from+(to-from)*t.fraction);
            a.flight.updatedAt=GetTickCount64();Store(slot,a);return 2;
        }
    }
    a.flight.distance=advance(stop);a.flight.updatedAt=GetTickCount64();
    if(stop<1)a.flight.Contact(from+(to-from)*stop);
    if(!MoveSpell(a,from+(to-from)*std::max(0.f,stop-(stop<1?.0001f:0.f))))return End(slot,a,game,unit);
    if(stop<1){++walls;return End(slot,a,game,unit);}
    Store(slot,a);return 1;
}
uintptr_t Shoot(uintptr_t game,int missile,uintptr_t player,int skill,int level,int extra,
                int offsetX,int offsetY,int targetX,int targetY,int ammo) {
    if(launch.slot>=0&&launch.game==game&&launch.player==player) {
        // A thrown weapon flies as the missile the native Throw names for it
        // (knives 36, axes 35, javelins 1, 37 or 371; #221).
        // Created() then takes this shot for the flight.
        if(missile>=0)ResolveThrown(launch.shot,uint32_t(missile));
        if(missile!=int(MissileId(launch.shot)))return 0; // before native costs
        Vec p{};if(!Position(player,p))return 0;
        const auto& shot=launch.shot;
        const Vec tip=GridPoint(shot,Tip(shot,0))*shot.unitsPerMetre;
        const int x=int(tip.x*.5f),y=int(tip.z*.5f);
        offsetX=x-int(p.x*.5f);offsetY=y-int(p.z*.5f);
        const Vec horizontal=framevr::math::Normalized(Vec{shot.direction.x,0,shot.direction.z});
        // The native path is only a damage/lifetime owner. Actual flight uses
        // the complete frozen vector, including vertical shots.
        targetX=x+int(horizontal.x*60);targetY=y+int(horizontal.z*60);
        if(targetX==x&&targetY==y)++targetY;
        if(x<1||y<1||targetX<1||targetY<1||x>65534||y>65534||targetX>65534||targetY>65534)return 0;
    }
    return originalShoot(game,missile,player,skill,level,extra,offsetX,offsetY,targetX,targetY,ammo);
}
// A basic throw of the hand's weapon: the native Throw, or Left Hand Throw.
void ThrowWeapon(uintptr_t game,uintptr_t player,int hand) {
    if(hand==1)spellvr::ThrowLeft(game,player);
    else reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int)>(d2rsig::Addr(ThrowRva))(game,player,2,1);
}
void __cdecl Apply(const D2RL::PluginContext*,void* token) noexcept {
    std::array<Shot,4> batch;size_t n=0;
    {std::lock_guard lock(mutex);
     if(!gameCallback.Claim(reinterpret_cast<uintptr_t>(token)))return;
     n=pendingCount;std::copy_n(pending.begin(),n,batch.begin());pendingCount=0;}
    // Room unloading/native removal can bypass the missile update hook. Retire
    // those records by authoritative id lookup, never by a wall-clock timeout
    // which would confuse an ordinary paused game with a destroyed projectile.
    uintptr_t checkGame=0,checkPlayer=0;
    if(melee::LocalServer(checkGame,checkPlayer))for(size_t j=0;j<usedSlots.load(std::memory_order_relaxed);++j) {
        Arrow a;{std::lock_guard lock(mutex);a=arrows[j];}
        if(a.flight.alive&&a.game==checkGame&&a.flight.shot.session==gamestate::LocalPlayer()&&
           melee::ServerUnit(checkGame,3,a.serverId)!=a.server) {
            a.flight.alive=false;a.flight.updatedAt=GetTickCount64();Store(j,a);
        }
    }
    for(size_t i=0;i<n;++i) {
        auto shot=batch[i];
        if(!Live(shot)){++stale;continue;}
        const bool crossbow=shot.kind==Kind::Crossbow;
        const auto fireAt=GetTickCount64();uint32_t crossbowCycle=1000;
        if(crossbow) {
            if(!framevr::CrossbowHeld(shot)||!crossbowCadence.Ready(shot.session,fireAt))continue;
            std::lock_guard lock(mutex);crossbowCycle=crossbowRate.Cycle(shot.session,shot.equipment,fireAt);
        }
        uintptr_t game=0,player=0;Vec p{};uint32_t mode=0;
        if(!melee::LocalServer(game,player)||!Read(player+0xc,mode)||mode==0||mode==17||mode==5||mode==6||
           !Position(player,p)) {++refused;continue;}
        if(!framevr::coordinates::MatchesPlayer(p,shot.player,shot.unitsPerMetre)) {
            ++refused;++positionMismatch;continue;
        }
        int level=1;
        const bool javelin=shot.kind==Kind::Javelin;
        const bool spell=shot.kind==Kind::Spell;
        if(spell&&!spellvr::Prepare(shot,game,player))continue;
        // A quiver arrow the game cannot start (skill missing, wrong state)
        // still leaves the bow as the basic arrow that was drawn.
        // A javelin skill flies the javelin's model: the hand holds it at the javelin's grip.
        if(SkillJavelin(shot))Grip(shot,1);
        bool quiverArrow=SkillRelease(shot);
        if(quiverArrow&&!spellvr::PrepareArrow(shot,game,player)) {
            shot.skill=shot.owner=shot.missile=-1;shot.level=0;quiverArrow=false;++quiverRefused;
        }
        // With a throwing weapon in each hand, the native weapon selector is
        // scoped to the throwing hand's weapon: Double Throw throws it, and
        // Shoot uses up that stack (#224). Only Double Throw throws left.
        uintptr_t thrown=0;
        if(javelin) {
            const auto weapons=gamestate::MeleeEquipment();uint32_t id=0;
            if(framevr::dualwield::ThrownType(weapons.Type(shot.hand)))thrown=meleeweapon::Select(player,shot.hand,weapons);
            if(thrown&&(!Read(thrown+8,id)||id!=weapons.displayed[shot.hand]))thrown=0;
        }
        bool doubleThrow=DoubleThrow(shot);
        if(doubleThrow&&(!thrown||!spellvr::PrepareThrow(shot,game,player))) {
            shot.skill=shot.owner=-1;shot.level=0;doubleThrow=false;++quiverRefused;
        }
        if(javelin&&shot.hand!=0&&(shot.hand!=1||!thrown||!spellvr::ThrowReady())){++refused;++throwRefused;continue;}
        if(!spell&&(javelin?gamestate::WeaponType()!=D2RVR_TYPE_JAVELIN&&gamestate::WeaponType()!=D2RVR_TYPE_THROWING:crossbow?
           (gamestate::WeaponType()!=D2RVR_TYPE_CROSSBOW||
            reinterpret_cast<int(*)(uintptr_t,int*)>(d2rsig::Addr(MissileTypeRva))(player,&level)!=31):
           (!reinterpret_cast<int(*)(uintptr_t)>(d2rsig::Addr(IsBowRva))(player)||
            reinterpret_cast<int(*)(uintptr_t,int*)>(d2rsig::Addr(MissileTypeRva))(player,&level)!=0))) {++refused;continue;}
        Launch next;next.shot=shot;next.game=game;next.player=player;
        const unsigned required=spell?framevr::spellcast::LaunchCapacity(shot.skill):1;
        {
            std::lock_guard lock(mutex);
            for(size_t j=0;j<arrows.size()&&next.count<required;++j)
                if(!arrows[j].flight.alive&&!arrows[j].client&&!arrows[j].creating&&!arrows[j].reserved)next.slots[next.count++]=int(j);
            if(next.count==required) {
                next.slot=next.slots[0];
                for(unsigned j=0;j<next.count;++j) {
                    auto& a=arrows[next.slots[j]];a={};a.reserved=true;a.flight.shot=shot;a.flight.shot.member=int(j);
                }
                usedSlots.store(std::max(usedSlots.load(std::memory_order_relaxed),size_t(next.slots[next.count-1]+1)),std::memory_order_relaxed);
            }
        }
        if(next.slot<0){++refused;continue;}
        combat::ObserveLocalPlayer(player);
        const auto saved=launch;launch=next;
        // Native basic attack -> Shoot checks and consumes arrows/bolts once. There
        // is deliberately no controller A press or second client-side attack.
        if(spell) {
            spellvr::Cast(shot,game,player);
            if(launch.created)spellvr::Fired(shot,game,player);
        } else if(doubleThrow) {
            // Its start pays mana; srvdofunc 74 throws the scoped weapon through
            // Shoot. Refused (mana, town), the hand still throws its weapon.
            meleeweapon::Scope selected(player,thrown);
            spellvr::Cast(shot,game,player);
            if(launch.created)++doubleThrows[shot.hand];
            else {
                ++quiverFallbacks;
                launch.shot.skill=launch.shot.owner=-1;launch.shot.level=0;
                ThrowWeapon(game,player,shot.hand);
            }
        } else if(quiverArrow) {
            // The skill's own start/dispatch pays mana and creates its missile
            // through Shoot, which takes the frozen bow vector like a basic arrow.
            spellvr::Cast(shot,game,player);
            if(launch.created)++quiverShots;
            else {
                // Refused (usually mana): the native warning plays, and the
                // drawn arrow flies as a basic attack instead of a dud.
                ++quiverFallbacks;
                launch.shot.skill=launch.shot.owner=launch.shot.missile=-1;launch.shot.level=0;
                // ... and a javelin as a basic throw of the weapon in hand.
                if(javelin)reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int)>(d2rsig::Addr(ThrowRva))(game,player,2,1);
                else reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int)>(d2rsig::Addr(AttackRva))(game,player,0,1);
            }
        } else if(javelin) {
            meleeweapon::Scope selected(player,thrown);   // none unless a weapon in each hand
            ThrowWeapon(game,player,shot.hand);
        } else reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int)>(d2rsig::Addr(AttackRva))(game,player,0,1);
        if(!launch.created)++refused;
        if(crossbow)crossbowCadence.Landed(shot.session,fireAt,launch.created?crossbowCycle:100);
        if(crossbow&&launch.created){++crossbowFired;crossbowCycleMs=crossbowCycle;}
        {
            std::lock_guard lock(mutex);
            for(unsigned j=0;j<next.count;++j) {
                auto& a=arrows[next.slots[j]];
                if(a.flight.shot.session==shot.session&&a.flight.shot.sequence==shot.sequence)a.reserved=false;
            }
            Recount();
        }
        launch=saved;
    }
}
// skill: a fired spell or quiver arrow names its skill and level. The nocked
// preview is purely visual and is requested like a basic arrow of that missile.
void VisualParams(const Shot& shot,float distance,uintptr_t hero,std::array<uint8_t,0x98>& params,bool withSkill=true) {
    const auto tip=GridPoint(shot,Tip(shot,distance))*shot.unitsPerMetre;
    const int x=int(tip.x*.5f),y=int(tip.z*.5f);
    const auto d=framevr::math::Normalized(Vec{shot.direction.x,0,shot.direction.z});
    int tx=x+int(d.x*60),ty=y+int(d.z*60);if(tx==x&&ty==y)++ty;
    Param(params,0,uint32_t(0x21));Param(params,8,hero);Param(params,0x10,hero);
    Param(params,0x20,MissileId(shot));
    Param(params,0x24,x);Param(params,0x28,y);Param(params,0x2c,tx);Param(params,0x30,ty);
    const bool skill=withSkill&&(shot.kind==Kind::Spell||SkillRelease(shot));
    Param(params,0x3c,skill?shot.skill:0);
    Param(params,0x40,skill?std::max(shot.level,1):1);
}
// Requires mutex. Invalidate ownership before native destruction so late
// renderer callbacks cannot revive the old binding or its readiness timestamp.
void ClearPreviewClient() {
    preview.client=0;preview.clientId=0;preview.missile=0;preview.binding.Clear();
    preview.renderAt=0;preview.rendered={};preview.previous={};previewReadyAt=0;
}
void ObservePreview(uint64_t now) {
    previewRecovery.Observe(enabled.load()&&preview.active&&preview.visible&&Allowed(preview.pose,current,now),previewReadyAt.load(),now);
}
void PresentPreview(uintptr_t hero,uint64_t session) {
    Preview snapshot;bool recover=false,swap=false;uint64_t lastRendered=0;
    {
        std::lock_guard lock(mutex);
        const auto now=GetTickCount64();ObservePreview(now);
        // ClientTick owns destruction. Wait for it to finish before creating
        // the latest selection; quick changes coalesce into that one model.
        if(clientRetirements.Blocking(now))return;
        swap=preview.client&&preview.missile!=MissileId(preview.pose);
        const bool recreate=!preview.client&&preview.recreate;
        if(enabled.load()) {
            if(!hero||!session||preview.pose.session!=session||(!swap&&!recreate&&!previewRecovery.Due(preview.client!=0,now)))return;
            recover=preview.client!=0&&!swap;
        } else if(!preview.client)return;
        if(!preview.client&&(!preview.active||!preview.visible||!Allowed(preview.pose,current,now)))return;
        snapshot=preview;lastRendered=previewReadyAt.load();
        if(!swap&&!recreate)previewRecovery.Attempt(now);
        if(snapshot.client) {
            clientRetirements.Request(snapshot.client,snapshot.clientId,snapshot.missile,now);
            previewReadyAt=0;
        }
        preview.recreate=swap&&enabled.load();
        Recount();
    }
    if(snapshot.client) {
        char request[160];snprintf(request,sizeof request,"Frame bow preview: retire queued missile=%u id=%u reason=%s",
            snapshot.missile,snapshot.clientId,swap?"selection":recover?"recovery":"disabled");
        framevr::logging::Write(request);
        if(recover) {
            ++previewRecovered;
            char line[224];const auto now=GetTickCount64();
            snprintf(line,sizeof line,"Frame bow preview recovery: retirement queued mesh_bound=%d rendered_before=%d last_render_age_ms=%llu",
                int(snapshot.binding.Bound()),int(lastRendered!=0),static_cast<unsigned long long>(lastRendered&&now>=lastRendered?now-lastRendered:0));
            framevr::logging::Write(line);
        }
        // Let native cleanup finish; the next attempt creates at the current
        // bow pose, rather than retaining the original client's room/location.
        return;
    }
    // Client visual only: no server missile, attack, ammo or damage request.
    std::array<uint8_t,0x98> params{};VisualParams(snapshot.pose,0,hero,params,false);
    const uint32_t missile=SkillArrow(snapshot.pose)?MissileId(snapshot.pose):0;
    const auto saved=previewCreatingSession;const auto savedMissile=previewCreatingMissile;
    previewCreatingSession=session;previewCreatingMissile=missile;
    const auto client=projectiles::CreateVisual(reinterpret_cast<uintptr_t>(params.data()));
    previewCreatingSession=saved;previewCreatingMissile=savedMissile;
    uint32_t header[3]{};
    const bool readable=client&&Read(client,header);
    bool orphan=false,kept=false;
    {
        std::lock_guard lock(mutex);
        const bool wanted=current.session==session&&preview.active&&(!preview.client||preview.client==client);
        const auto use=CreatedClient(readable,header,missile,wanted);
        if(use==ClientUse::Keep) {
            preview.client=client;preview.clientId=header[2];preview.missile=missile;preview.recreate=false;++previewCreated;
            kept=true;
        } else {
            orphan=use==ClientUse::Destroy;++previewRefused;
            if(client&&preview.client==client)ClearPreviewClient();
        }
        if(orphan)clientRetirements.Request(client,header[2],header[1],GetTickCount64());
        Recount();
    }
    if(kept) {
        char line[144];snprintf(line,sizeof line,"Frame bow preview: created missile=%u id=%u",header[1],header[2]);
        framevr::logging::Write(line);
    }
}
void __cdecl Present(const D2RL::PluginContext*,void* token) noexcept {
    uintptr_t hero=0;uint64_t session=0;
    {std::lock_guard lock(mutex);
     if(!uiCallback.Claim(reinterpret_cast<uintptr_t>(token)))return;
     hero=clientHero;session=current.session;}
    uint32_t h[3]{},id=0;
    if(!ready.load()||!session||session!=gamestate::LocalPlayer())return;
    const bool validHero=combat::LocalPlayerId(id)&&Read(hero,h)&&h[0]==0&&h[2]==id;
    PresentPreview(validHero?hero:0,session);
    if(!enabled.load()||!validHero)return;
    for(size_t i=0;i<usedSlots.load(std::memory_order_relaxed);++i) {
        Arrow a;
        {
            std::lock_guard lock(mutex);a=arrows[i];
            if(!a.flight.alive||a.flight.shot.session!=session||a.visualDone)continue;
            arrows[i].visualDone=true;arrows[i].creating=true;
        }
        // Only values cross game -> UI. No server owner/path/target pointer is
        // supplied to the client allocator, and its id is allocated normally.
        std::array<uint8_t,0x98> params{};
        auto visualShot=a.flight.shot;
        const bool spell=visualShot.kind==Kind::Spell;
        if(spell){visualShot.origin=a.flight.Point();visualShot.direction=a.flight.Direction();}
        VisualParams(visualShot,spell?0:a.flight.distance,hero,params);
        const auto saved=creatingVisual;const auto savedSession=visualSession,savedSequence=visualSequence;
        creatingVisual=int(i);visualSession=a.flight.shot.session;visualSequence=a.flight.shot.sequence;
        const auto client=projectiles::CreateVisual(reinterpret_cast<uintptr_t>(params.data()));
        creatingVisual=saved;visualSession=savedSession;visualSequence=savedSequence;
        uint32_t header[3]{};
        const bool readable=client&&Read(client,header);
        const bool validClient=CreatedClient(readable,header,MissileId(a.flight.shot),true)==ClientUse::Keep;
        bool orphan=false;
        {
            std::lock_guard lock(mutex);
            if(Same(arrows[i],a)) {
                arrows[i].creating=false;
                if(validClient){arrows[i].client=client;arrows[i].clientId=header[2];++visuals;Recount();}
                else {++refused;orphan=CreatedClient(readable,header,0,false)==ClientUse::Destroy;}
            } else orphan=CreatedClient(readable,header,0,false)==ClientUse::Destroy;
        }
        if(orphan) {
            std::lock_guard lock(mutex);clientRetirements.Request(client,header[2],header[1],GetTickCount64());Recount();
        } else if(validClient&&a.flight.shot.kind==Kind::Spell&&a.flight.shot.castSound>0&&
                castCue.Take(session,a.flight.shot.hand,a.flight.shot.sequence,a.flight.shot.at,GetTickCount64())) {
            // Direct casting deliberately skips the character animation that
            // normally plays stsound. Restore that cue once at native volume,
            // on the client caster, while retaining each missile's travel sound.
            originalMissileSound(a.flight.shot.castSound,hero,0,0,false);
        }
    }
}
void ClientTick(uintptr_t unit,float dt) {
    if(!clientCount.load(std::memory_order_relaxed)&&!previewClient.load(std::memory_order_relaxed)){originalClientTick(unit,dt);return;}
    uint32_t header[3]{};size_t slot=arrows.size();bool alive=false;
    bool held=false,spellEnd=false,retiring=false;Flight ended;uint64_t retirement=0;
    if(Read(unit,header)&&header[0]==3) {
        std::lock_guard lock(mutex);
        if(preview.client==unit&&preview.clientId==header[2]&&preview.missile==header[1]) {
            held=true;alive=preview.pose.session==current.session&&enabled.load();
            if(!alive)clientRetirements.Request(unit,header[2],header[1],GetTickCount64());
        }
        retiring=clientRetirements.Contains(unit,header[2],header[1]);
        retirement=clientRetirements.Begin(unit,header);
        if(retirement) {
            if(preview.client==unit)ClearPreviewClient();
            Recount();
        }
        for(size_t i=0;!retirement&&i<usedSlots.load(std::memory_order_relaxed);++i)if(arrows[i].client==unit&&arrows[i].clientId==header[2]) {
            slot=i;alive=arrows[i].flight.alive&&arrows[i].flight.shot.session==current.session&&enabled.load();
            spellEnd=!alive&&(arrows[i].flight.shot.kind==Kind::Spell||SkillRelease(arrows[i].flight.shot))&&
                arrows[i].flight.shot.session==current.session&&enabled.load();
            if(spellEnd)ended=arrows[i].flight;
            if(!alive){arrows[i].client=0;arrows[i].visualBinding.Clear();Recount();}break;
        }
    }
    if(retirement) {
        originalClientKill(unit); // the same native phase that retires fired arrows
        {std::lock_guard lock(mutex);clientRetirements.Finish(retirement);Recount();}
        char line[144];snprintf(line,sizeof line,"Frame bow preview: client-tick retirement finished missile=%u id=%u",header[1],header[2]);
        framevr::logging::Write(line);
        return;
    }
    if(held||retiring)return;
    if(slot==arrows.size()){originalClientTick(unit,dt);return;}
    if(!alive) {
        const auto saved=finishingImpact;
        finishingImpact=spellEnd&&SkillArrow(ended.shot)?&ended:nullptr;
        const bool finished=spellEnd&&spellvr::FinishVisual(unit,ended);
        finishingImpact=saved;
        if(!finished)originalClientKill(unit);
    }
    // Server owns lifetime/collision; the HD transform owns full 3D movement.
    // Running native client prediction here would resurrect flat-grid hits.
}
void ClientKill(uintptr_t unit) {
    if(!clientCount.load(std::memory_order_relaxed)&&!previewClient.load(std::memory_order_relaxed)&&
       !impactClients.load(std::memory_order_relaxed)){originalClientKill(unit);return;}
    uint64_t retirement=0;
    {
        std::lock_guard lock(mutex);
        if(preview.client==unit) {
            clientRetirements.Request(unit,preview.clientId,preview.missile,GetTickCount64());
            ClearPreviewClient();
        }
        uint32_t header[3]{};
        if(Read(unit,header))retirement=clientRetirements.Begin(unit,header);
        for(auto& a:UsedArrows())if(a.client==unit){a.client=0;a.visualBinding.Clear();}
        impactVisuals.Forget(unit);
        Recount();
    }
    originalClientKill(unit);
    if(retirement){std::lock_guard lock(mutex);clientRetirements.Finish(retirement);Recount();}
}
// A thrown potion's own missile, hidden in flight (#292): its visual entities
// are shrunk and sent far below the world (rows 0-2 the scaled basis, 12-14
// the translation, as SpellMatrix writes them). Descendants inherit only the
// root's translation, so the move is what hides the meshes and effects; the
// burst plays where the missile itself lands.
inline constexpr float HiddenDrop=100000;
struct HiddenPotion {uint32_t entity=0;uint64_t until=0;};
std::array<HiddenPotion,12> hiddenPotions{};
struct PotionClaim {uintptr_t unit=0;uint32_t id=0;uint64_t until=0;} potionClaim;   // the client missile whose visual is hidden
std::atomic<unsigned> hiddenPotionCount{0},potionsHidden{0},hiddenWrites{0};   // hiddenWrites: transforms rewritten for the last claim
std::atomic<uint64_t> potionExpected{0};
bool HiddenPotionMatrix(uintptr_t out,uint32_t entity,uintptr_t component) {
    const uint64_t now=GetTickCount64();bool hide=false;unsigned left=0;
    {
        std::lock_guard lock(mutex);
        for(auto& h:hiddenPotions) {
            if(h.entity&&now>h.until)h={};
            if(h.entity==entity&&entity)hide=true;
            left+=h.entity!=0;
        }
    }
    hiddenPotionCount=left;
    std::array<float,16> m{};
    if(!hide||!Read(out,m))return false;
    for(int row=0;row<3;++row)for(int column=0;column<3;++column)m[size_t(row*4+column)]*=1e-4f;
    m[13]-=HiddenDrop;
    ++hiddenWrites;
    framevr::WriteMemory(reinterpret_cast<void*>(out),m.data(),sizeof m);
    framevr::WriteMemory(reinterpret_cast<void*>(component+0x40),m.data(),sizeof m);
    framevr::WriteMemory(reinterpret_cast<void*>(component+0x80),m.data(),sizeof m);
    return true;
}
uintptr_t Transform(uintptr_t out,uint32_t entity,uintptr_t component) {
    const auto result=originalTransform(out,entity,component);
    propscan::Transformed();
    skel::CrossbowRendered(entity,component,out);
    skel::ShieldRendered(entity,component,out);
    if(hiddenPotionCount.load(std::memory_order_relaxed)&&HiddenPotionMatrix(out,entity,component))return result;
    if(!renderCount.load(std::memory_order_relaxed))return result;
    std::array<float,16> matrix{},previous{};bool found=false,held=false,heldVisible=false;
    uint64_t heldSession=0,heldRevision=0;uintptr_t heldClient=0;uint32_t heldId=0;
    const uint64_t at=framevr::BowRenderTime();
    {
        std::lock_guard lock(mutex);
        if(preview.binding.Matches(entity)&&preview.pose.session==current.session) {
            Shot pose=preview.pose;bool visible=false;
            const bool active=enabled.load()&&!clientRetirements.Contains(preview.client,preview.clientId,preview.missile)&&
                previewRenderPose.Read(current.session,at,GetTickCount64(),pose,visible);
            if(preview.renderAt!=at) {
                preview.previous=preview.rendered;
                preview.renderAt=at;
            }
            // View can refresh the world anchor again within this pair. Always
            // use that pose, while keeping previous-frame motion history for
            // both eyes. The timer-owned pose remains for client lifecycle only.
            PreviewMatrix(pose,active&&visible,preview.rendered.data(),preview.missile);
            if(preview.previous[15]!=1)preview.previous=preview.rendered;
            matrix=preview.rendered;previous=preview.previous;found=held=true;heldVisible=active&&visible;
            heldSession=pose.session;heldClient=preview.client;heldId=preview.clientId;heldRevision=previewRenderPose.revision;
        }
        for(auto& a:UsedArrows())if(!held&&a.visualBinding.Matches(entity)&&a.flight.shot.session==current.session) {
            if(a.renderAt!=at||a.rendered[15]!=1) {
                a.previous=a.rendered;
                if(a.flight.shot.kind==Kind::Spell||(SkillJavelin(a.flight.shot)&&ParticleJavelin(a.flight.shot.missile))) {
                    if(!Read(out,a.rendered))continue;
                    SpellMatrix(a.flight,at,a.rendered.data());
                } else if(SkillArrow(a.flight.shot))QuiverMatrix(a.flight,at,a.rendered.data());
                else ArrowMatrix(a.flight,at,a.rendered.data());
                if(a.previous[15]!=1)a.previous=a.rendered;
                a.renderAt=at;
            }
            matrix=a.rendered;previous=a.previous;found=true;break;
        }
        if(!found&&enabled.load()&&impactVisuals.Pending()&&Read(out,matrix)&&
           impactVisuals.Matrix(entity,current.session,matrix.data())) {
            previous=matrix;found=true;
        }
    }
    if(!found)return result;
    // Exact full entity generation, not its reusable 20-bit index. Both the
    // caller's result and the component cache need the same world transform.
    const bool written=framevr::WriteMemory(reinterpret_cast<void*>(out),matrix.data(),sizeof matrix)&&
        framevr::WriteMemory(reinterpret_cast<void*>(component+0x40),matrix.data(),sizeof matrix);
    framevr::WriteMemory(reinterpret_cast<void*>(component+0x80),previous.data(),sizeof previous);
    if(held&&heldVisible&&written) {
        std::lock_guard lock(mutex);
        if(enabled.load()&&previewRenderPose.revision==heldRevision&&preview.binding.Matches(entity)&&
           preview.pose.session==heldSession&&current.session==heldSession&&preview.client==heldClient&&preview.clientId==heldId)
            previewReadyAt=GetTickCount64();
    }
    return result;
}
}
void Init(const D2RL::PluginContext* ctx) {
    context=ctx;
    if(ctx->QueryService(&tables)!=D2RL::ServiceQueryResult::Success||
       !D2RL::HasDataTableServiceField(tables,D2RL::DataTableServiceRequiredSize))tables=nullptr;
    if(ctx->QueryService(&threads)!=D2RL::ServiceQueryResult::Success||
       !D2RL::HasThreadServiceField(threads,D2RL::ThreadServiceRequiredSize))threads=nullptr;
}
void Configure(bool on){enabled=on;}
bool HolyReady() {
    // Optional: a changed layout must not disable the other physical spells.
    return tables&&tables->getRow&&d2rsig::Check(holyabi::ConsumerRva,holyabi::ConsumerSig,sizeof holyabi::ConsumerSig)&&
        d2rsig::Check(holyabi::ArgsRva,holyabi::ArgsSig,sizeof holyabi::ArgsSig)&&
        d2rsig::Check(holyabi::FilterRva,holyabi::FilterSig,sizeof holyabi::FilterSig)&&
        d2rsig::Check(holyabi::FilterArgsRva,holyabi::FilterArgsSig,sizeof holyabi::FilterArgsSig)&&CollisionFilter();
}
bool PrepareHoly(uintptr_t game) {
    D2RL::DataTables::RowView row{};
    return HolyReady()&&HolyRow(game,row);
}
void PublishPreviewPose(const Shot& pose,const Gate& gate,bool visible,uint64_t frame) {
    std::lock_guard lock(mutex);
    previewRenderPose.Publish(pose,gate,visible,frame);
}
bool PreviewReady() {
    const auto at=previewReadyAt.load();
    return enabled.load()&&PreviewRecovery::Rendered(at,GetTickCount64());
}
bool MotionReady(){return enabled.load()&&ready.load();}
bool DirectCrossbow(){return launch.slot>=0&&launch.shot.kind==Kind::Crossbow;}
uint32_t QuiverLaunchMissile(){return launch.slot>=0&&SkillRelease(launch.shot)?MissileId(launch.shot):0;}
void SetCrossbowCycle(uint64_t session,uint32_t equipment,uint32_t milliseconds) {
    std::lock_guard lock(mutex);crossbowRate={session,GetTickCount64(),equipment,milliseconds};
}
bool SpellReady(){return enabled.load()&&ready.load()&&spellvr::Ready();}
bool PlayHeroSound(int sound) {
    uintptr_t hero=0;uint32_t h[3]{},id=0;
    if(!soundIn||!originalMissileSound||sound<=0||!combat::LocalPlayerUnit(hero)||!combat::LocalPlayerId(id)||
       !Read(hero,h)||h[0]!=0||h[2]!=id)return false;
    originalMissileSound(sound,hero,0,0,false);return true;
}
void Update(const Gate& gate,uintptr_t hero) {
    bool visible=false;
    Shot pose;const bool held=gate.active&&enabled.load()&&framevr::BowPreviewPose(pose,visible)&&Allowed(pose,gate,GetTickCount64());
    uintptr_t uiToken=0,gameToken=0;
    uint32_t abandonedMissile=0,abandonedId=0;bool abandoned=false;
    {
        std::lock_guard lock(mutex);
        if(current.session!=gate.session) {
            for(auto& a:UsedArrows())a={};
            usedSlots=0;preview={};previewReadyAt=0;previewRecovery={};clientRetirements.Reset();impactVisuals.Reset();
            pendingCount=0;gameCallback.Cancel();uiCallback.Cancel();Recount();
        }
        current=gate;clientHero=hero;
        preview.active=held;preview.visible=held&&visible;
        if(held)preview.pose=pose;
        if(!held||!visible)previewReadyAt=0;
        const auto now=GetTickCount64();
        if(impactVisuals.Pending()){impactVisuals.Expire(gate.session,now);Recount();}
        ObservePreview(now);
        // The game removed the old preview before its retirement reached it:
        // stop waiting and nock a new one (ClientRetirements::Unreached).
        if(preview.client&&clientRetirements.Abandoned(preview.client,preview.clientId,preview.missile,now)) {
            abandoned=true;abandonedMissile=preview.missile;abandonedId=preview.clientId;
            ClearPreviewClient();preview.recreate=enabled.load();++previewAbandoned;Recount();
        }
        if(gameCallback.Expired(now,500)){gameCallback.Cancel();pendingCount=0;++gameRecovered;}
        if(uiCallback.Expired(now,500)){uiCallback.Cancel();++uiRecovered;}
        const bool needsPreview=!clientRetirements.Blocking(now)&&((!enabled.load()&&preview.client)||previewRecovery.Due(preview.client!=0,now)||
            (enabled.load()&&preview.active&&(preview.recreate||(preview.client&&preview.missile!=MissileId(preview.pose)))));
        bool needsVisual=false;
        if(ready.load())for(const auto& a:UsedArrows())if(a.flight.alive&&!a.visualDone){needsVisual=true;break;}
        if(ready.load()&&(needsPreview||needsVisual)) {
            uiToken=uiCallback.Start(now);
        }
        if(ready.load()&&now-maintenanceAt>=100)
            for(const auto& a:UsedArrows())if(a.flight.alive){gameToken=gameCallback.Start(now);if(gameToken)maintenanceAt=now;break;}
    }
    if(abandoned) {
        char line[160];snprintf(line,sizeof line,"Frame bow preview: retirement unreached missile=%u id=%u (already removed by the game); nocking a new preview",
            abandonedMissile,abandonedId);
        framevr::logging::Write(line);
    }
    if(uiToken&&threads->runOnUiThread(context,&Present,reinterpret_cast<void*>(uiToken))!=D2RL::Threads::Result::Success) {
        std::lock_guard lock(mutex);uiCallback.Claim(uiToken);
    }
    if(gameToken&&threads->runOnGameThread(context,&Apply,reinterpret_cast<void*>(gameToken))!=D2RL::Threads::Result::Success) {
        std::lock_guard lock(mutex);gameCallback.Claim(gameToken);
    }
}
void Submit(const Shot& shot) {
    uintptr_t token=0;
    {
        std::lock_guard lock(mutex);
        if(shot.kind==Kind::Spell&&(shot.hand<0||shot.hand>1))return;
        auto& dedupe=shot.kind==Kind::Spell?spellOnce[shot.hand]:shot.kind==Kind::Crossbow?crossbowOnce:once;
        if(!ready.load()||!enabled.load()||!dedupe.Take(shot,current,GetTickCount64())){++stale;return;}
        // Held spell input is a latest-pose request, never a backlog of casts.
        if(shot.kind==Kind::Spell)for(size_t i=0;i<pendingCount;++i)
            if(pending[i].kind==Kind::Spell&&pending[i].hand==shot.hand){pending[i]=shot;return;}
        // A held trigger supplies current aim, not a queue of delayed bolts.
        if(shot.kind==Kind::Crossbow)for(size_t i=0;i<pendingCount;++i)
            if(pending[i].kind==Kind::Crossbow){pending[i]=shot;return;}
        if(pendingCount==pending.size()){++refused;return;}
        pending[pendingCount++]=shot;++submitted;
        token=gameCallback.Start(GetTickCount64());
    }
    if(token&&threads->runOnGameThread(context,&Apply,reinterpret_cast<void*>(token))!=D2RL::Threads::Result::Success) {
        std::lock_guard lock(mutex);if(gameCallback.Claim(token))pendingCount=0;++refused;
    }
}
void Created(uintptr_t game,uintptr_t params,uintptr_t unit) {
    if(launch.slot<0||launch.created>=launch.count||launch.game!=game)return;
    uintptr_t owner=0,path=0;uint32_t h[3]{};int velocity=0;
    if(!Read(params+8,owner)||owner!=launch.player||!Read(unit,h)||h[0]!=3||h[1]!=MissileId(launch.shot)||
       !Read(unit+0x38,path)||!Read(path+0xa0,velocity))return;
    Arrow a;a.game=game;a.server=unit;a.serverId=h[2];a.flight.shot=launch.shot;a.homing=launch.homing;
    a.flight.shot.member=int(launch.created);
    a.flight.speed=FlightSpeed(launch.shot,velocity);a.flight.alive=true;a.flight.updatedAt=GetTickCount64();
    if(a.flight.NativePath()) {
        Vec start{};if(!Position(unit,start))return;
        a.flight.BeginNativePath(start,a.flight.updatedAt);
    }
    a.room=reinterpret_cast<uintptr_t(*)(uintptr_t)>(d2rsig::Addr(RoomRva))(unit);
    {
        std::lock_guard lock(mutex);
        auto& reserved=arrows[launch.slots[launch.created]];
        if(current.session!=a.flight.shot.session||!reserved.reserved||!Same(reserved,a))return;
        reserved=a;Recount();
    }
    ++launch.created;++spawned;
    if(launch.shot.kind==Kind::Spell&&framevr::holybolt::Skill(launch.shot.skill)) {
        static uint64_t session=0;static unsigned samples=0;
        if(session!=launch.shot.session){session=launch.shot.session;samples=0;}
        if(samples++<16) {
            char line[272];const auto& s=launch.shot;
            snprintf(line,sizeof line,"Frame Holy Bolt: hand=%d planar=%d assist=%d origin=%.3f,%.3f,%.3f direction=%.3f,%.3f,%.3f homing=%d native friendly collision enabled",
                s.hand,int(s.planar),s.aimAssist,s.origin.x,s.origin.y,s.origin.z,s.direction.x,s.direction.y,s.direction.z,int(a.homing.active));
            framevr::logging::Write(line);
        }
    }
    lastLatency=unsigned(GetTickCount64()-launch.shot.at);lastSpeedCm=unsigned(a.flight.speed*100);
}
bool AdoptCrossbow(uintptr_t game,uintptr_t unit,const Shot& shot) {
    // The normal trigger attack has already paid its cost and created exactly
    // one native server bolt. Reuse it; never dispatch a second basic attack.
    if(shot.kind!=Kind::Crossbow||!Live(shot))return false;
    uint32_t h[3]{};uintptr_t path=0;int velocity=0;
    if(!Read(unit,h)||h[0]!=3||h[1]!=31||!Read(unit+0x38,path)||!Read(path+0xa0,velocity))return false;
    Arrow a;a.game=game;a.server=unit;a.serverId=h[2];a.flight.shot=shot;
    a.flight.speed=FlightSpeed(shot,velocity);a.flight.alive=true;a.flight.updatedAt=GetTickCount64();
    a.room=reinterpret_cast<uintptr_t(*)(uintptr_t)>(d2rsig::Addr(RoomRva))(unit);
    a.visualDone=true; // the native attack supplies the client; Present must not duplicate it
    if(!a.room||a.flight.speed<=0)return false;
    std::lock_guard lock(mutex);
    if(current.session!=shot.session)return false;
    for(size_t i=0;i<arrows.size();++i)if(!arrows[i].flight.alive&&!arrows[i].client&&!arrows[i].creating&&!arrows[i].reserved) {
        arrows[i]=a;usedSlots.store(std::max(usedSlots.load(),i+1));Recount();++spawned;
        lastLatency=unsigned(GetTickCount64()-shot.at);lastSpeedCm=unsigned(a.flight.speed*100);return true;
    }
    return false;
}
void CrossbowClient(uintptr_t unit,const Shot& shot) {
    uint32_t h[3]{};if(!Read(unit,h)||h[0]!=3||h[1]!=31||shot.kind!=Kind::Crossbow)return;
    std::lock_guard lock(mutex);
    for(auto& a:UsedArrows())if(a.flight.shot.kind==Kind::Crossbow&&a.flight.shot.session==shot.session&&
       a.flight.shot.sequence==shot.sequence&&current.session==shot.session) {
        if(!a.client||a.client==unit){a.client=unit;a.clientId=h[2];++visuals;Recount();}return;
    }
}
// The spell lock (vrcam BowTick) aimed this cast 1 m above the locked
// monster's floor; aim it at the middle of the same body capsule the flight
// hits (Body), from the same origin. Not found: the 1 m aim stays.
// lockTarget is 0 without a lock, so nothing here runs.
static void LockBody(uintptr_t game,Shot& shot) {
    const uint32_t id=shot.lockTarget;
    if(!id)return;
    const auto target=melee::ServerUnit(game,1,id);
    uint32_t h[4]{};Vec bottom{},top{};float radius=0;
    const bool found=target&&Read(target,h)&&h[0]==1&&h[2]==id&&h[3]!=0&&h[3]!=12&&Body(target,h[1],shot,bottom,top,radius);
    Vec direction{};
    const auto point=framevr::bowflight::LockAimPoint({},found,bottom,top);
    const bool aimed=found&&framevr::bowflight::LockAimDirection(shot.origin,point,shot.planar,direction);
    if(aimed)shot.direction=direction;
    // Telemetry: "Frame lock body:" at most once a second (one call per cast)
    static ULONGLONG told=0;
    if(GetTickCount64()-told<1000)return;
    told=GetTickCount64();
    char line[256];
    if(!found)snprintf(line,sizeof line,"Frame lock body: skill %d monster %u not found, kept the 1 m aim",shot.skill,id);
    else {
        const float floor=bottom.y-radius,height=top.y+radius-floor;
        const float distance=std::hypot(point.x-shot.origin.x,point.z-shot.origin.z);
        snprintf(line,sizeof line,"Frame lock body: skill %d monster %u class %u body %.2f m, aim %.2f m above its floor (was %.2f), %.1f m away%s",
            shot.skill,id,h[1],height,point.y-floor,framevr::bowflight::LockFallbackHeight,distance,
            aimed?(shot.planar?" (planar: flat)":""):" (too close, kept the 1 m aim)");
    }
    framevr::logging::Write(line);
}
void AssistSpell(uintptr_t game,uintptr_t player) {
    if(launch.slot<0||launch.game!=game||launch.player!=player||launch.shot.kind!=Kind::Spell)return;
    auto& shot=launch.shot;
    if(shot.hand<0||shot.hand>1)return;
    LockBody(game,shot);
    auto& history=assistHistory[shot.hand];
    auto selection=history.Begin(shot);
    auto& stats=assistStats[shot.hand];
    ++stats.casts;stats.mode=shot.aimAssist;
    if(!framevr::aimassist::Limit(shot.aimAssist))return;
    const auto originRoom=reinterpret_cast<uintptr_t(*)(uintptr_t)>(d2rsig::Addr(RoomRva))(player);
    uintptr_t buckets[128]{};unsigned visited=0;
    if(!originRoom||!Read(game+0x4c6*8,buckets))return;
    uint32_t selectedClass=0;Vec selectedBody{};bool healingRay=false;
    for(auto target:buckets)for(unsigned chain=0;target&&chain<256&&visited++<4096;++chain) {
        uintptr_t next=0;uint32_t h[4]{};
        if(!Read(target+0x158,next)||!Read(target,h))break;
        if(h[0]==1&&h[3]!=0&&h[3]!=12) {
            Vec position{};
            if(Position(target,position)) {
                const auto delta=RenderGround(shot,position)-shot.origin;
                // Include capsule radius in the broad phase, and avoid native
                // relation/table queries for enemies well outside local range.
                constexpr float broad=framevr::aimassist::Range+2;
                if(delta.x*delta.x+delta.z*delta.z<=broad*broad) {
                    Vec bottom{},top{};float radius=0;
                    if(Body(target,h[1],shot,bottom,top,radius)) {
                        ++stats.bodies;
                        const bool holy=framevr::holybolt::Skill(shot.skill);
                        const bool hostile=holy&&reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(game,player,target)!=0;
                        if(holy) {
                            const auto kind=HolyTarget(game,h[1],hostile);
                            if(kind==framevr::holybolt::Target::Friend) {
                                float entry=0;
                                // Preserve a healing ray instead of pulling it
                                // toward an enemy beside/behind the mercenary.
                                if(CapsuleEntry(shot.origin,shot.origin+shot.direction*framevr::aimassist::Range,bottom,top,radius,entry))healingRay=true;
                            }
                            if(kind!=framevr::holybolt::Target::Enemy){target=next;continue;}
                        }
                        if(shot.planar&&!framevr::spellcast::NativePath(shot.skill)&&
                           (shot.origin.y<=bottom.y-radius||shot.origin.y>=top.y+radius))++stats.heightMisses;
                        const auto candidate=framevr::aimassist::Evaluate(shot,h[2],bottom,top,radius);
                        if(selection.Better(candidate)&&(holy?hostile:
                           reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(game,player,target)!=0)) {
                            ++stats.eligible;
                            auto room=originRoom;
                            const auto end=shot.origin+candidate.direction*candidate.distance;
                            const float barrier=WallEntry(GridPoint(shot,shot.origin),GridPoint(shot,end),shot.unitsPerMetre,[&](int x,int z) {
                                room=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(RoomPointRva))(room,x,z);
                                return !room||reinterpret_cast<uint16_t(*)(uintptr_t,int,int,int,int)>(d2rsig::Addr(MaskRva))(room,x,z,1,0x804)!=0;
                            });
                            if(barrier>=1) {
                                selection.Accept(candidate);selectedClass=h[1];selectedBody=(bottom+top)*.5f;
                            } else ++stats.blocked;
                        }
                    }
                }
            }
        }
        target=next;
    }
    if(healingRay){history.Finish({});return;}
    const auto& best=selection.Best();history.Finish(best);
    launch.homing.Arm(shot,best,selectedClass,selectedBody);
    if(best.valid) {
        if(best.direct)++stats.direct;
        else {shot.direction=best.direction;++stats.corrected;stats.degrees=best.angle/framevr::aimassist::Degrees;}
    }
}
bool SpellTarget(uintptr_t game,uintptr_t player,int* x,int* y) {
    if(launch.slot<0||launch.game!=game||launch.player!=player||
       launch.shot.kind!=Kind::Spell||!framevr::spellcast::NativePath(launch.shot.skill)||!x||!y)return false;
    const auto p=GridPoint(launch.shot,launch.shot.origin)*launch.shot.unitsPerMetre;
    const auto d=launch.shot.direction;
    *x=std::clamp(int(p.x*.5f+d.x*40),1,65534);
    *y=std::clamp(int(p.z*.5f+d.z*40),1,65534);
    return true;
}
int SpellParams(uintptr_t game,uintptr_t params,std::array<uint8_t,0x98>& adjusted) {
    if(launch.slot<0||launch.game!=game||launch.shot.kind!=Kind::Spell||
       !framevr::spellcast::NativePath(launch.shot.skill))return 0;
    uintptr_t owner=0;int missile=-1,skill=-1;
    if(!Read(params+8,owner)||owner!=launch.player||!Read(params+0x20,missile)||
       missile!=launch.shot.missile||!Read(params+0x3c,skill)||skill!=launch.shot.skill)return 0;
    if(launch.created>=launch.count||!Read(params,adjusted))return -1;
    const auto p=GridPoint(launch.shot,launch.shot.origin)*launch.shot.unitsPerMetre;
    const int x=int(p.x*.5f),y=int(p.z*.5f);
    int tx=0,ty=0;
    if(x<1||x>65534||y<1||y>65534||!SpellTarget(game,owner,&tx,&ty))return -1;
    Param(adjusted,0x24,x);Param(adjusted,0x28,y);Param(adjusted,0x2c,tx);Param(adjusted,0x30,ty);
    return 1; // preserve native init selector/ordinal, count, level and owner
}
uintptr_t ImpactVisual(uintptr_t params,int flags,uintptr_t(*create)(uintptr_t,int)) {
    const auto saved=creatingImpact;creatingImpact=0;
    uint64_t ticket=0;int missile=-1;
    if(finishingImpact&&enabled.load()&&Read(params+0x20,missile)) {
        std::lock_guard lock(mutex);
        if(finishingImpact->shot.session==current.session)
            ticket=impactVisuals.Reserve(*finishingImpact,missile,GetTickCount64());
        Recount();
    }
    creatingImpact=ticket;
    const auto unit=create(params,flags);
    if(ticket) {
        uint32_t h[3]{};Read(unit,h);Vec grid{};const bool positioned=unit&&Position(unit,grid);
        {std::lock_guard lock(mutex);impactVisuals.Complete(ticket,unit,h,positioned?&grid:nullptr);Recount();}
        char line[176];snprintf(line,sizeof line,"Frame quiver impact: source=%d effect=%d contact=%.3f,%.3f,%.3f client=%s",
            finishingImpact->shot.missile,missile,finishingImpact->ImpactPoint().x,finishingImpact->ImpactPoint().y,
            finishingImpact->ImpactPoint().z,unit?"yes":"no");
        framevr::logging::Write(line);
    }
    creatingImpact=saved;return unit;
}
bool Visual(uint32_t entity,uintptr_t unit,bool hasReleasePoint,bool isRoot) {
    uint32_t header[3]{};
    if(!Read(unit,header)||header[0]!=3)return false;
    const auto* crossbow=crossbowvr::Creating();
    std::lock_guard lock(mutex);
    if(impactVisuals.Owns(creatingImpact,unit,header)) {
        Vec grid{};const bool positioned=isRoot&&Position(unit,grid);
        impactVisuals.Bind(creatingImpact,unit,header,entity,isRoot,positioned?&grid:nullptr);Recount();return true;
    }
    const bool creatingPreview=preview.active&&previewCreatingSession&&previewCreatingSession==current.session;
    const uint32_t heldMissile=creatingPreview?previewCreatingMissile:preview.missile;
    if(header[1]==heldMissile&&preview.pose.session==current.session&&(creatingPreview||
       (preview.client==unit&&preview.clientId==header[2]))) {
        // A quiver model binds its root; its arrow mesh and particles follow,
        // and those descendants drop the hero release-bone blend.
        const bool rooted=heldMissile!=0;
        if(!(rooted?preview.binding.BindRoot(entity,isRoot):preview.binding.Bind(entity,hasReleasePoint)))return rooted;
        preview.client=unit;preview.clientId=header[2];preview.missile=heldMissile;Recount();return true;
    }
    for(size_t i=0;i<usedSlots.load(std::memory_order_relaxed);++i) {
        auto& a=arrows[i];
        if(header[1]==MissileId(a.flight.shot)&&a.flight.shot.sequence&&a.flight.shot.session==current.session&&((a.client==unit&&a.clientId==header[2])||
            (creatingVisual==int(i)&&a.flight.shot.session==visualSession&&a.flight.shot.sequence==visualSequence)||
            (crossbow&&a.flight.shot.kind==Kind::Crossbow&&a.flight.shot.session==crossbow->session&&a.flight.shot.sequence==crossbow->sequence))) {
            const bool wasBound=a.visualBinding.Bound();
            const bool rooted=a.flight.shot.kind==Kind::Spell||SkillArrow(a.flight.shot)||(SkillJavelin(a.flight.shot)&&ParticleJavelin(a.flight.shot.missile));
            const bool bound=rooted?a.visualBinding.BindRoot(entity,isRoot):a.visualBinding.Bind(entity,hasReleasePoint);
            // Descendant release components still need removal even though
            // only the root owns the spell transform.
            if(!bound){++ignoredEntities;return rooted;}
            if(!wasBound)++boundMeshes;
            a.client=unit;a.clientId=header[2];Recount();return true;
        }
    }
    return false;
}
void ExpectPotion(uint64_t now){potionExpected=now;}
bool HidePotion(uint32_t entity,uintptr_t unit,bool& first) {
    first=false;uint32_t header[3]{};
    if(!entity||(entity&0xfffff)==0xfffff||!Read(unit,header)||header[0]!=3)return false;
    const uint64_t now=GetTickCount64();
    {
        std::lock_guard lock(mutex);
        if(potionClaim.unit!=unit||potionClaim.id!=header[2]||now>potionClaim.until) {
            const uint64_t expected=potionExpected.load();
            if(!expected||now<expected||now-expected>3000)return false;   // the throw animation, and the art loading the first time
            if(potionClaim.unit){char line[120];snprintf(line,sizeof line,"Frame potion throw: the last hidden potion's transforms rewritten %u times",hiddenWrites.exchange(0));framevr::logging::Write(line);}
            else hiddenWrites=0;
            potionClaim={unit,header[2],now+4000};potionExpected=0;first=true;++potionsHidden;   // one missile per throw
        }
        auto* slot=&hiddenPotions[0];
        for(auto& h:hiddenPotions) {
            if(h.entity==entity){slot=&h;break;}
            if(!h.entity||h.until<slot->until)slot=&h;
        }
        *slot={entity,potionClaim.until};
    }
    ++hiddenPotionCount;
    return true;
}
bool OwnsVisual(uintptr_t unit) {
    uint32_t h[3]{};if(!Read(unit,h)||h[0]!=3)return false;
    std::lock_guard lock(mutex);
    if(impactVisuals.Owns(creatingImpact,unit,h))return true;
    if(h[1]&&preview.pose.session==current.session&&((preview.missile==h[1]&&preview.client==unit&&preview.clientId==h[2])||
       (preview.active&&previewCreatingSession==current.session&&previewCreatingMissile==h[1])))return true;
    for(size_t i=0;i<usedSlots.load(std::memory_order_relaxed);++i) {
        const auto& a=arrows[i];
        if((a.flight.shot.kind==Kind::Spell||SkillRelease(a.flight.shot))&&a.flight.shot.sequence&&a.flight.shot.session==current.session&&
           h[1]==MissileId(a.flight.shot)&&((a.client==unit&&a.clientId==h[2])||
           (creatingVisual==int(i)&&a.flight.shot.sequence==visualSequence&&a.flight.shot.session==visualSession)))return true;
    }
    return false;
}
void Install() {
    if(enabled.load())spellvr::Install();
    if(ready.load()||!enabled.load()||!context||!threads||!threads->runOnGameThread||!threads->runOnUiThread||!projectiles::Ready())return;
    if(!checked) {
#define CHECK(name) if(!d2rsig::Check(name##Rva,name##Sig,sizeof name##Sig))return
        CHECK(Attack);CHECK(Throw);CHECK(ThrowCall);CHECK(Shoot);CHECK(Collision);CHECK(Hit);CHECK(ClientTick);CHECK(ClientKill);CHECK(Transform);
        CHECK(MissileSound);CHECK(TravelSoundCall);
        CHECK(AttackCall);CHECK(ShootParams);CHECK(VelocityLayout);CHECK(WorldLayout);
        CHECK(FrameGet);CHECK(FrameSet);CHECK(Room);CHECK(RoomPoint);CHECK(Mask);CHECK(Stats);CHECK(Relation);CHECK(IsBow);CHECK(MissileType);
#undef CHECK
        checked=true;
    }
    if(!shootIn)shootIn=d2rsig::Hook(ShootRva,ShootSig,sizeof ShootSig,reinterpret_cast<void*>(&Shoot),reinterpret_cast<void**>(&originalShoot));
    if(!collisionIn)collisionIn=d2rsig::Hook(CollisionRva,CollisionSig,sizeof CollisionSig,reinterpret_cast<void*>(&Collision),reinterpret_cast<void**>(&originalCollision));
    if(!clientIn)clientIn=d2rsig::Hook(ClientTickRva,ClientTickSig,sizeof ClientTickSig,reinterpret_cast<void*>(&ClientTick),reinterpret_cast<void**>(&originalClientTick));
    if(!killIn)killIn=d2rsig::Hook(ClientKillRva,ClientKillSig,sizeof ClientKillSig,reinterpret_cast<void*>(&ClientKill),reinterpret_cast<void**>(&originalClientKill));
    if(!transformIn)transformIn=d2rsig::Hook(TransformRva,TransformSig,sizeof TransformSig,reinterpret_cast<void*>(&Transform),reinterpret_cast<void**>(&originalTransform));
    if(!soundIn)soundIn=d2rsig::Hook(MissileSoundRva,MissileSoundSig,sizeof MissileSoundSig,reinterpret_cast<void*>(&MissileSound),reinterpret_cast<void**>(&originalMissileSound));
    if(shootIn&&collisionIn&&clientIn&&killIn&&transformIn&&soundIn&&!ready.exchange(true))
        framevr::logging::Write("Frame physical projectiles: tracked native-arrow preview and frozen 6DoF arrow/javelin flight; terrain-correct grid/room queries; mesh-only transforms; native basic attack/throw costs");
}
void Report() {
    const auto accepted=holyAllowed.exchange(0),rejected=holyRejected.exchange(0);
    if(accepted||rejected) {
        char line[144];snprintf(line,sizeof line,"Frame Holy Bolt collision: native filter accepted=%u rejected=%u",accepted,rejected);
        framevr::logging::Write(line);
    }
    crossbowvr::Report();
    if(const auto count=crossbowFired.exchange(0)) {
        char line[128];snprintf(line,sizeof line,"Frame crossbow free fire: native shots=%u, last cycle=%u ms",count,crossbowCycleMs.load());
        framevr::logging::Write(line);
    }
    spellvr::Report();
    {
        const auto shots=quiverShots.exchange(0),fallbacks=quiverFallbacks.exchange(0),refusedStart=quiverRefused.exchange(0);
        if(shots||fallbacks||refusedStart) {
            char line[192];snprintf(line,sizeof line,"Frame bow quiver: skill arrows=%u basic fallbacks after refusal=%u unprepared=%u",shots,fallbacks,refusedStart);
            framevr::logging::Write(line);
        }
        const auto right=doubleThrows[0].exchange(0),left=doubleThrows[1].exchange(0),refusedThrow=throwRefused.exchange(0);
        if(right||left||refusedThrow) {
            char line[160];snprintf(line,sizeof line,"Frame double throw: right=%u left=%u left throws refused=%u",right,left,refusedThrow);
            framevr::logging::Write(line);
        }
    }
    for(int hand=0;hand<2;++hand) {
        auto& a=assistStats[hand];
        const auto casts=a.casts.exchange(0),bodies=a.bodies.exchange(0),height=a.heightMisses.exchange(0);
        const auto eligible=a.eligible.exchange(0),blocked=a.blocked.exchange(0),direct=a.direct.exchange(0),corrected=a.corrected.exchange(0);
        const auto steered=a.steered.exchange(0),lost=a.lost.exchange(0);
        if(casts||corrected||direct||steered||lost) {
            char line[360];snprintf(line,sizeof line,"Frame spell assist: hand=%d mode=%d casts=%u bodies=%u outside_height=%u eligible=%u blocked=%u direct=%u corrected=%u homing_steps=%u homing_ended=%u last_correction=%.2fdeg",
                hand,a.mode.load(),casts,bodies,height,eligible,blocked,direct,corrected,steered,lost,a.degrees.load());
            framevr::logging::Write(line);
        }
    }
    const auto s=submitted.exchange(0),n=spawned.exchange(0),v=visuals.exchange(0),x=stale.exchange(0),r=refused.exchange(0);
    const auto c=contacts.exchange(0),w=walls.exchange(0),e=expired.exchange(0);
    const auto b=boundMeshes.exchange(0),i=ignoredEntities.exchange(0);
    const auto p=positionMismatch.exchange(0);
    const auto pc=previewCreated.exchange(0),pr=previewRefused.exchange(0),rc=previewRecovered.exchange(0),pa=previewAbandoned.exchange(0),ur=uiRecovered.exchange(0),gr=gameRecovered.exchange(0);
    if(!s&&!n&&!v&&!x&&!r&&!c&&!w&&!e&&!b&&!i&&!pc&&!pr&&!rc&&!pa&&!ur&&!gr)return;
    char line[512];snprintf(line,sizeof line,"Frame physical projectiles: releases=%u native=%u visual=%u stale=%u refused=%u contacts=%u barriers=%u expired=%u meshes=%u ignored entities=%u position_mismatch=%u preview_created=%u preview_refused=%u preview_recovered=%u preview_abandoned=%u ui_recovered=%u game_recovered=%u last latency=%ums speed=%.1fm/s",s,n,v,x,r,c,w,e,b,i,p,pc,pr,rc,pa,ur,gr,lastLatency.load(),lastSpeedCm.load()/100.f);
    framevr::logging::Write(line);
}
}
