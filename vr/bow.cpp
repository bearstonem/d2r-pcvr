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
#include "bow_signatures.h"
#include "spells.h"
#include "melee.h"
#include "combat.h"
#include "projectiles.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include "../shared/d2r_vr_state.h"
#include "../steamvr/transport.h"
#include "../steamvr/callback_ticket.h"
#include "../steamvr/spell_aim_assist.h"
namespace gamestate {uint64_t LocalPlayer();bool MenuOpen();uint32_t HandsKey();uint32_t WeaponSet();uint32_t WeaponType();}
namespace bowvr {
namespace {
using namespace framevr::bowflight;
using namespace native;
const D2RL::PluginContext* context=nullptr;
const D2RL::ThreadService* threads=nullptr;
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
    uintptr_t client=0;
    uint32_t clientId=0;
    VisualBinding binding{};
    uint64_t renderAt=0;
    std::array<float,16> rendered{},previous{};
};
Preview preview;
PreviewRenderPose previewRenderPose;
std::atomic<bool> previewClient{false};
std::atomic<uint64_t> previewReadyAt{0};
PreviewRecovery previewRecovery;
Gate current{};uintptr_t clientHero=0;
Once once;
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
std::atomic<unsigned> previewCreated{0},previewRefused{0},previewRecovered{0},uiRecovered{0};
std::atomic<unsigned> gameRecovered{0};
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
int MissileSound(int sound,uintptr_t unit,int a,int b,bool c) {
    // The nocked model is not a fired projectile. Suppress only its creation
    // scope's travel sound; genuine releases and other game audio pass through.
    if(previewCreatingSession)return 0;
    return originalMissileSound(sound,unit,a,b,c);
}
template<class T> bool Read(uintptr_t p,T& out) {
    return p>0x10000&&framevr::ReadMemory(&out,reinterpret_cast<const void*>(p),sizeof out);
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
    return a.flight.shot.session==b.flight.shot.session&&a.flight.shot.sequence==b.flight.shot.sequence&&
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
    unsigned r=preview.binding.Bound()?1:0,s=0,c=0;
    auto size=usedSlots.load(std::memory_order_relaxed);
    while(size&&!arrows[size-1].flight.alive&&!arrows[size-1].client&&!arrows[size-1].creating&&!arrows[size-1].reserved)--size;
    usedSlots.store(size,std::memory_order_relaxed);
    for(const auto& a:UsedArrows()){if(a.visualBinding.Bound())++r;if(a.flight.alive)++s;if(a.client)++c;}
    renderCount=r;serverCount=s;clientCount=c;
    previewClient=preview.client!=0;
}
int End(size_t slot,Arrow& a,uintptr_t game,uintptr_t unit) {
    a.flight.alive=false;a.flight.updatedAt=GetTickCount64();Store(slot,a);
    reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t,int)>(d2rsig::Addr(HitRva))(game,unit,0,1);
    return 2;
}
bool MoveSpell(Arrow& a,Vec point) {
    if(a.flight.shot.kind!=Kind::Spell)return true;
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
        reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(game,player,target);
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
    const float floor=a.flight.shot.player.y+.015f;
    if(from.y<=floor)stop=0;
    else if(to.y<=floor)stop=std::min(stop,(from.y-floor)/(from.y-to.y));
    struct Touch {float fraction;uintptr_t unit;uint32_t id;};
    std::array<Touch,64> touches{};size_t n=0;
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
               fraction<stop&&reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(game,player,target)) {
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
        const int result=reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t,int)>(d2rsig::Addr(HitRva))(game,unit,t.unit,0);
        ++contacts;framevr::MeleeImpact();
        if(result==2) {
            a.flight.distance=advance(t.fraction);a.flight.alive=false;
            a.flight.updatedAt=GetTickCount64();Store(slot,a);return 2;
        }
    }
    a.flight.distance=advance(stop);a.flight.updatedAt=GetTickCount64();
    if(!MoveSpell(a,from+(to-from)*std::max(0.f,stop-(stop<1?.0001f:0.f))))return End(slot,a,game,unit);
    if(stop<1){++walls;return End(slot,a,game,unit);}
    Store(slot,a);return 1;
}
uintptr_t Shoot(uintptr_t game,int missile,uintptr_t player,int skill,int level,int extra,
                int offsetX,int offsetY,int targetX,int targetY,int ammo) {
    if(launch.slot>=0&&launch.game==game&&launch.player==player) {
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
        if(!spell&&(javelin?gamestate::WeaponType()!=D2RVR_TYPE_JAVELIN:
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
        // Native basic attack -> Shoot checks and consumes arrows once. There
        // is deliberately no controller A press or second client-side attack.
        if(spell) {
            spellvr::Cast(shot,game,player);
            if(launch.created)spellvr::Fired(shot,game,player);
        } else reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int)>(d2rsig::Addr(javelin?ThrowRva:AttackRva))(game,player,javelin?2:0,1);
        if(!launch.created)++refused;
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
void VisualParams(const Shot& shot,float distance,uintptr_t hero,std::array<uint8_t,0x98>& params) {
    const auto tip=GridPoint(shot,Tip(shot,distance))*shot.unitsPerMetre;
    const int x=int(tip.x*.5f),y=int(tip.z*.5f);
    const auto d=framevr::math::Normalized(Vec{shot.direction.x,0,shot.direction.z});
    int tx=x+int(d.x*60),ty=y+int(d.z*60);if(tx==x&&ty==y)++ty;
    Param(params,0,uint32_t(0x21));Param(params,8,hero);Param(params,0x10,hero);
    Param(params,0x20,MissileId(shot));
    Param(params,0x24,x);Param(params,0x28,y);Param(params,0x2c,tx);Param(params,0x30,ty);
    Param(params,0x3c,shot.kind==Kind::Spell?shot.skill:0);
    Param(params,0x40,shot.kind==Kind::Spell?shot.level:1);
}
// Requires mutex. Invalidate ownership before native destruction so late
// renderer callbacks cannot revive the old binding or its readiness timestamp.
void ClearPreviewClient() {
    preview.client=0;preview.clientId=0;preview.binding.Clear();
    preview.renderAt=0;preview.rendered={};preview.previous={};previewReadyAt=0;
}
void ObservePreview(uint64_t now) {
    previewRecovery.Observe(enabled.load()&&preview.active&&preview.visible&&Allowed(preview.pose,current,now),previewReadyAt.load(),now);
}
void PresentPreview(uintptr_t hero,uint64_t session) {
    Preview snapshot;bool recover=false;uint64_t lastRendered=0;
    {
        std::lock_guard lock(mutex);
        const auto now=GetTickCount64();ObservePreview(now);
        if(enabled.load()) {
            if(!hero||!session||preview.pose.session!=session||!previewRecovery.Due(preview.client!=0,now))return;
            recover=preview.client!=0;
        } else if(!preview.client)return;
        snapshot=preview;lastRendered=previewReadyAt.load();previewRecovery.Attempt(now);
        if(snapshot.client){ClearPreviewClient();Recount();}
    }
    if(snapshot.client) {
        uint32_t header[3]{};
        const bool owned=Read(snapshot.client,header)&&header[0]==3&&header[1]==0&&header[2]==snapshot.clientId;
        if(owned)originalClientKill(snapshot.client);
        if(recover) {
            ++previewRecovered;
            char line[224];const auto now=GetTickCount64();
            snprintf(line,sizeof line,"Frame bow preview recovery: client_valid=%d mesh_bound=%d rendered_before=%d last_render_age_ms=%llu",
                int(owned),int(snapshot.binding.Bound()),int(lastRendered!=0),static_cast<unsigned long long>(lastRendered&&now>=lastRendered?now-lastRendered:0));
            framevr::logging::Write(line);
        }
        // Let native cleanup finish; the next attempt creates at the current
        // bow pose, rather than retaining the original client's room/location.
        return;
    }
    // Client visual only: no server missile, attack, ammo or damage request.
    std::array<uint8_t,0x98> params{};VisualParams(snapshot.pose,0,hero,params);
    const auto saved=previewCreatingSession;previewCreatingSession=session;
    const auto client=projectiles::CreateVisual(reinterpret_cast<uintptr_t>(params.data()));
    previewCreatingSession=saved;
    uint32_t header[3]{};
    const bool valid=client&&Read(client,header)&&header[0]==3&&header[1]==0;
    bool orphan=false;
    {
        std::lock_guard lock(mutex);
        if(valid&&current.session==session&&preview.active&&(!preview.client||preview.client==client)) {
            preview.client=client;preview.clientId=header[2];++previewCreated;
        } else {
            orphan=valid;++previewRefused;
            if(preview.client==client)ClearPreviewClient();
        }
        Recount();
    }
    if(orphan)originalClientKill(client);
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
        const bool validClient=client&&Read(client,header)&&header[0]==3&&header[1]==MissileId(a.flight.shot);
        bool orphan=false;
        {
            std::lock_guard lock(mutex);
            if(Same(arrows[i],a)) {
                arrows[i].creating=false;
                if(validClient){arrows[i].client=client;arrows[i].clientId=header[2];++visuals;Recount();}
                else ++refused;
            } else orphan=validClient;
        }
        if(orphan)originalClientKill(client);
        else if(validClient&&a.flight.shot.kind==Kind::Spell&&a.flight.shot.castSound>0&&
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
    bool held=false,spellEnd=false;Flight ended;
    if(Read(unit,header)&&header[0]==3) {
        std::lock_guard lock(mutex);
        if(preview.client==unit&&preview.clientId==header[2]) {
            held=true;alive=preview.pose.session==current.session&&enabled.load();
            if(!alive){ClearPreviewClient();Recount();}
        }
        for(size_t i=0;i<usedSlots.load(std::memory_order_relaxed);++i)if(arrows[i].client==unit&&arrows[i].clientId==header[2]) {
            slot=i;alive=arrows[i].flight.alive&&arrows[i].flight.shot.session==current.session&&enabled.load();
            spellEnd=!alive&&arrows[i].flight.shot.kind==Kind::Spell&&arrows[i].flight.shot.session==current.session&&enabled.load();
            if(spellEnd)ended=arrows[i].flight;
            if(!alive){arrows[i].client=0;arrows[i].visualBinding.Clear();Recount();}break;
        }
    }
    if(held){if(!alive)originalClientKill(unit);return;}
    if(slot==arrows.size()){originalClientTick(unit,dt);return;}
    if(!alive&&(!spellEnd||!spellvr::FinishVisual(unit,ended)))originalClientKill(unit);
    // Server owns lifetime/collision; the HD transform owns full 3D movement.
    // Running native client prediction here would resurrect flat-grid hits.
}
void ClientKill(uintptr_t unit) {
    if(!clientCount.load(std::memory_order_relaxed)&&!previewClient.load(std::memory_order_relaxed)){originalClientKill(unit);return;}
    {
        std::lock_guard lock(mutex);
        if(preview.client==unit)ClearPreviewClient();
        for(auto& a:UsedArrows())if(a.client==unit){a.client=0;a.visualBinding.Clear();}
        Recount();
    }
    originalClientKill(unit);
}
uintptr_t Transform(uintptr_t out,uint32_t entity,uintptr_t component) {
    const auto result=originalTransform(out,entity,component);
    if(!renderCount.load(std::memory_order_relaxed))return result;
    std::array<float,16> matrix{},previous{};bool found=false,held=false,heldVisible=false;
    uint64_t heldSession=0,heldRevision=0;uintptr_t heldClient=0;uint32_t heldId=0;
    const uint64_t at=framevr::BowRenderTime();
    {
        std::lock_guard lock(mutex);
        if(preview.binding.Matches(entity)&&preview.pose.session==current.session) {
            Shot pose=preview.pose;bool visible=false;
            const bool active=enabled.load()&&previewRenderPose.Read(current.session,at,GetTickCount64(),pose,visible);
            if(preview.renderAt!=at) {
                preview.previous=preview.rendered;
                preview.renderAt=at;
            }
            // View can refresh the world anchor again within this pair. Always
            // use that pose, while keeping previous-frame motion history for
            // both eyes. The timer-owned pose remains for client lifecycle only.
            PreviewMatrix(pose,active&&visible,preview.rendered.data());
            if(preview.previous[15]!=1)preview.previous=preview.rendered;
            matrix=preview.rendered;previous=preview.previous;found=held=true;heldVisible=active&&visible;
            heldSession=pose.session;heldClient=preview.client;heldId=preview.clientId;heldRevision=previewRenderPose.revision;
        }
        for(auto& a:UsedArrows())if(!held&&a.visualBinding.Matches(entity)&&a.flight.shot.session==current.session) {
            if(a.renderAt!=at||a.rendered[15]!=1) {
                a.previous=a.rendered;
                if(a.flight.shot.kind==Kind::Spell) {
                    if(!Read(out,a.rendered))continue;
                    SpellMatrix(a.flight,at,a.rendered.data());
                } else ArrowMatrix(a.flight,at,a.rendered.data());
                if(a.previous[15]!=1)a.previous=a.rendered;
                a.renderAt=at;
            }
            matrix=a.rendered;previous=a.previous;found=true;break;
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
    if(ctx->QueryService(&threads)!=D2RL::ServiceQueryResult::Success||
       !D2RL::HasThreadServiceField(threads,D2RL::ThreadServiceRequiredSize))threads=nullptr;
}
void Configure(bool on){enabled=on;}
void PublishPreviewPose(const Shot& pose,const Gate& gate,bool visible,uint64_t frame) {
    std::lock_guard lock(mutex);
    previewRenderPose.Publish(pose,gate,visible,frame);
}
bool PreviewReady() {
    const auto at=previewReadyAt.load();
    return enabled.load()&&PreviewRecovery::Rendered(at,GetTickCount64());
}
bool SpellReady(){return enabled.load()&&ready.load()&&spellvr::Ready();}
void Update(const Gate& gate,uintptr_t hero) {
    bool visible=false;
    Shot pose;const bool held=gate.active&&enabled.load()&&framevr::BowPreviewPose(pose,visible)&&Allowed(pose,gate,GetTickCount64());
    uintptr_t uiToken=0,gameToken=0;
    {
        std::lock_guard lock(mutex);
        if(current.session!=gate.session) {
            for(auto& a:UsedArrows())a={};
            usedSlots=0;preview={};previewReadyAt=0;previewRecovery={};
            pendingCount=0;gameCallback.Cancel();uiCallback.Cancel();Recount();
        }
        current=gate;clientHero=hero;
        preview.active=held;preview.visible=held&&visible;
        if(held)preview.pose=pose;
        if(!held||!visible)previewReadyAt=0;
        const auto now=GetTickCount64();
        ObservePreview(now);
        if(gameCallback.Expired(now,500)){gameCallback.Cancel();pendingCount=0;++gameRecovered;}
        if(uiCallback.Expired(now,500)){uiCallback.Cancel();++uiRecovered;}
        const bool needsPreview=(!enabled.load()&&preview.client)||previewRecovery.Due(preview.client!=0,now);
        bool needsVisual=false;
        if(ready.load())for(const auto& a:UsedArrows())if(a.flight.alive&&!a.visualDone){needsVisual=true;break;}
        if(ready.load()&&(needsPreview||needsVisual)) {
            uiToken=uiCallback.Start(now);
        }
        if(ready.load()&&now-maintenanceAt>=100)
            for(const auto& a:UsedArrows())if(a.flight.alive){gameToken=gameCallback.Start(now);if(gameToken)maintenanceAt=now;break;}
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
        auto& dedupe=shot.kind==Kind::Spell?spellOnce[shot.hand]:once;
        if(!ready.load()||!enabled.load()||!dedupe.Take(shot,current,GetTickCount64())){++stale;return;}
        // Held spell input is a latest-pose request, never a backlog of casts.
        if(shot.kind==Kind::Spell)for(size_t i=0;i<pendingCount;++i)
            if(pending[i].kind==Kind::Spell&&pending[i].hand==shot.hand){pending[i]=shot;return;}
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
    lastLatency=unsigned(GetTickCount64()-launch.shot.at);lastSpeedCm=unsigned(a.flight.speed*100);
}
void AssistSpell(uintptr_t game,uintptr_t player) {
    if(launch.slot<0||launch.game!=game||launch.player!=player||launch.shot.kind!=Kind::Spell)return;
    auto& shot=launch.shot;
    if(shot.hand<0||shot.hand>1)return;
    auto& history=assistHistory[shot.hand];
    auto selection=history.Begin(shot);
    auto& stats=assistStats[shot.hand];
    ++stats.casts;stats.mode=shot.aimAssist;
    if(!framevr::aimassist::Limit(shot.aimAssist))return;
    const auto originRoom=reinterpret_cast<uintptr_t(*)(uintptr_t)>(d2rsig::Addr(RoomRva))(player);
    uintptr_t buckets[128]{};unsigned visited=0;
    if(!originRoom||!Read(game+0x4c6*8,buckets))return;
    uint32_t selectedClass=0;Vec selectedBody{};
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
                        if(shot.planar&&!framevr::spellcast::NativePath(shot.skill)&&
                           (shot.origin.y<=bottom.y-radius||shot.origin.y>=top.y+radius))++stats.heightMisses;
                        const auto candidate=framevr::aimassist::Evaluate(shot,h[2],bottom,top,radius);
                        if(selection.Better(candidate)&&
                           reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(game,player,target)) {
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
bool Visual(uint32_t entity,uintptr_t unit,bool hasReleasePoint,bool isRoot) {
    uint32_t header[3]{};
    if(!Read(unit,header)||header[0]!=3)return false;
    std::lock_guard lock(mutex);
    if(header[1]==0&&preview.pose.session==current.session&&((preview.active&&previewCreatingSession&&previewCreatingSession==current.session)||
       (preview.client==unit&&preview.clientId==header[2]))) {
        if(!preview.binding.Bind(entity,hasReleasePoint))return false;
        preview.client=unit;preview.clientId=header[2];Recount();return true;
    }
    for(size_t i=0;i<usedSlots.load(std::memory_order_relaxed);++i) {
        auto& a=arrows[i];
        if(header[1]==MissileId(a.flight.shot)&&a.flight.shot.sequence&&a.flight.shot.session==current.session&&((a.client==unit&&a.clientId==header[2])||
            (creatingVisual==int(i)&&a.flight.shot.session==visualSession&&a.flight.shot.sequence==visualSequence))) {
            const bool wasBound=a.visualBinding.Bound();
            const bool bound=a.flight.shot.kind==Kind::Spell?a.visualBinding.BindRoot(entity,isRoot):a.visualBinding.Bind(entity,hasReleasePoint);
            // Descendant release components still need removal even though
            // only the root owns the spell transform.
            if(!bound){++ignoredEntities;return a.flight.shot.kind==Kind::Spell;}
            if(!wasBound)++boundMeshes;
            a.client=unit;a.clientId=header[2];Recount();return true;
        }
    }
    return false;
}
bool OwnsVisual(uintptr_t unit) {
    uint32_t h[3]{};if(!Read(unit,h)||h[0]!=3)return false;
    std::lock_guard lock(mutex);
    for(size_t i=0;i<usedSlots.load(std::memory_order_relaxed);++i) {
        const auto& a=arrows[i];
        if(a.flight.shot.kind==Kind::Spell&&a.flight.shot.sequence&&a.flight.shot.session==current.session&&
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
    spellvr::Report();
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
    const auto pc=previewCreated.exchange(0),pr=previewRefused.exchange(0),rc=previewRecovered.exchange(0),ur=uiRecovered.exchange(0),gr=gameRecovered.exchange(0);
    if(!s&&!n&&!v&&!x&&!r&&!c&&!w&&!e&&!b&&!i&&!pc&&!pr&&!rc&&!ur&&!gr)return;
    char line[512];snprintf(line,sizeof line,"Frame physical projectiles: releases=%u native=%u visual=%u stale=%u refused=%u contacts=%u barriers=%u expired=%u meshes=%u ignored entities=%u position_mismatch=%u preview_created=%u preview_refused=%u preview_recovered=%u ui_recovered=%u game_recovered=%u last latency=%ums speed=%.1fm/s",s,n,v,x,r,c,w,e,b,i,p,pc,pr,rc,ur,gr,lastLatency.load(),lastSpeedCm.load()/100.f);
    framevr::logging::Write(line);
}
}
