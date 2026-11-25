// Whirlwind on, steered while a whirl is under way: the game's side (steamvr/whirl_turn.h says when and where).
//
// The server steps the hero through a whirl in SUNIT_UpdateUnitPath (0x491ac0, game, unit: the player modes'
// path update, also every walk's) and the client steps its units in 0xf9a00 (unit, flag); both move him with
// PathStep (0x3812d0), which re-aims him toward his path's current point after every step. Just before a step of
// the server's hero in a whirl, a new end is laid the way Whirlwind's start (SrvSt38, 0x56a420) lays the first:
// its collision mask 0xC01, the target (0x342a50), PathCompute (0x340e70: from where he stands, his own footprint
// moved, the current point reset), the moving mask 0x401. The footprint the start reserved at the end (0x80, a
// player's) is moved to the new end (0x3636d0 off the old, 0x363cf0 on the new) and the skill's end (+0x24 /
// +0x28, where Whirlwind's remove clears it) follows. A compute refused: the old end is laid again. The client's
// hero, stepped on the client's thread, then takes the same end the same way (whirlturn::Follow), so the two end
// together and the server's position never has to snap him.
//
// All static RE of 3.3.93787 (2026-11-25); every function is checked byte for byte at its address before it is
// called or hooked, and nothing goes in unless all of them match. Read-only otherwise: the unit, its used skill
// (+0x100 -> +0x18: 0x34ba40; id: [[skill]] int16, 0x33e080; flags +0x14, 0x33cc00; &3 == 1: a whirl under way)
// and its path (+0x38; x, y 16.16 at +0 / +4; room +0x20; type +0x50 (7: straight); collision pattern +0x5C;
// points +0x34, the points at +0xC8: 0x341ae0).
#include "whirl_turn_native.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include "combat.h"
#include "melee.h"
#include "../steamvr/whirl_turn.h"
#include "../steamvr/transport.h"

// vrcam.cpp: the walk stick (the view's frame) as a unit way on the grid now, as Whirlwind's aim takes it.
bool WhirlGridWayNow(float stickX,float stickY,float& gx,float& gy);

namespace whirlturnnative {
namespace {
namespace wt=framevr::whirlturn;

template<class T> bool Read(uintptr_t a,T& v){return a>=0x10000&&framevr::ReadMemory(&v,reinterpret_cast<const void*>(a),sizeof v);}
template<class T> bool Write(uintptr_t a,const T& v){return a>=0x10000&&framevr::WriteMemory(reinterpret_cast<void*>(a),&v,sizeof v);}

// 3.3.93787's code, as it must be found.
struct Code {const char* name;uint64_t rva;const uint8_t* bytes;uint32_t size;};
constexpr uint64_t ServerStepRva=0x491ac0,ClientStepRva=0xf9a00;
// The hooks take whole instructions only: 5 + 5 + 1 + 4 and 2 + 1 + 4 + 5 + 3 bytes, nothing relative among them.
constexpr uint8_t ServerStepHook[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20};
constexpr uint8_t ClientStepHook[]={0x40,0x55,0x57,0x48,0x83,0xec,0x28,0x48,0x89,0x74,0x24,0x20,0x0f,0xb6,0xea};
constexpr uint8_t ServerStep[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,
                                0x8b,0xfa,0x48,0x8b,0xd9,0x48,0x85,0xc9,0x75,0x13,0x88,0x4c,0x24,0x30,0x48,0x8d};
constexpr uint8_t ClientStep[]={0x40,0x55,0x57,0x48,0x83,0xec,0x28,0x48,0x89,0x74,0x24,0x20,0x0f,0xb6,0xea,0x48,
                                0x8b,0xf9,0x48,0x85,0xc9,0x75,0x13,0x88,0x4c,0x24,0x40,0x48,0x8d,0x4c,0x24,0x40};
constexpr uint64_t ComputeRva=0x340e70,TargetRva=0x342a50,MaskRva=0x342740,ResetRva=0x3636d0,ReserveRva=0x363cf0;
constexpr uint8_t Compute[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x55,
                             0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x70,0x45};
constexpr uint8_t Target[]={0x48,0x85,0xc9,0x74,0x11,0x66,0x89,0x51,0x10,0x66,0x44,0x89,0x41,0x12,0x48,0xc7,
                            0x41,0x70,0x00,0x00,0x00,0x00,0xc3};
constexpr uint8_t Mask[]={0x89,0x51,0x64,0xc3};
constexpr uint8_t Reset[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
                           0x48,0x83,0xec,0x30,0x49,0x63,0xd9,0x41,0x8b,0xf0,0x8b,0xea,0x48,0x8b,0xf9,0x83};
constexpr uint8_t Reserve[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
                             0x48,0x83,0xec,0x30,0x49,0x63,0xd9,0x41,0x8b,0xf8,0x8b,0xf2,0x48,0x8b,0xe9,0x83};
// The fields read, as the game's own accessors read them.
constexpr uint8_t UsedSkill[]={0x48,0x8b,0x8b,0x00,0x01,0x00,0x00,0x48,0x83,0xc4,0x20,0x5b};   // 0x34ba61: unit+0x100, then
constexpr uint8_t SkillOfList[]={0x48,0x85,0xc9,0x75,0x03,0x33,0xc0,0xc3,0x48,0x8b,0x41,0x18,0xc3};   // 0x33e090: +0x18
constexpr uint8_t SkillId[]={0x48,0x85,0xc9,0x75,0x03,0x33,0xc0,0xc3,0x48,0x8b,0x01,0x0f,0xbf,0x00,0xc3};   // 0x33e080
constexpr uint8_t SkillFlags[]={0x48,0x85,0xc9,0x74,0x04,0x8b,0x41,0x14,0xc3};   // 0x33cc00
constexpr uint8_t PathPoints[]={0x48,0x8d,0x81,0xc8,0x00,0x00,0x00,0x48,0x89,0x02,0x8b,0x41,0x34,0xc3};   // 0x341ae0
constexpr uint8_t PlayerStraight[]={0xb8,0x07,0x00,0x00,0x00,0xc3};   // 0x3412e0: a player's whirl path type, 7
constexpr Code kCode[]={
    {"server unit step",ServerStepRva,ServerStep,sizeof ServerStep},{"client unit step",ClientStepRva,ClientStep,sizeof ClientStep},
    {"path compute",ComputeRva,Compute,sizeof Compute},{"path target",TargetRva,Target,sizeof Target},{"move mask",MaskRva,Mask,sizeof Mask},
    {"footprint off",ResetRva,Reset,sizeof Reset},{"footprint on",ReserveRva,Reserve,sizeof Reserve},
    {"used skill",0x34ba61,UsedSkill,sizeof UsedSkill},{"skill of list",0x33e090,SkillOfList,sizeof SkillOfList},
    {"skill id",0x33e080,SkillId,sizeof SkillId},{"skill flags",0x33cc00,SkillFlags,sizeof SkillFlags},
    {"path points",0x341ae0,PathPoints,sizeof PathPoints},{"straight path type",0x3412e0,PlayerStraight,sizeof PlayerStraight},
};
constexpr int Whirlwind=151;
constexpr uint32_t StraightPath=7,WhirlMask=0xC01,MovingMask=0x401,PlayerFootprint=0x80,MostPoints=78;

using ServerStepFn=uint64_t(__fastcall*)(uintptr_t game,uintptr_t unit);
using ClientStepFn=uint64_t(__fastcall*)(uintptr_t unit,uint64_t flag);
using ComputeFn=int(__fastcall*)(uintptr_t path,uintptr_t unit,int allowInTown,int flag);
using TargetFn=void(__fastcall*)(uintptr_t path,int x,int y);
using MaskFn=void(__fastcall*)(uintptr_t path,uint32_t mask);
using FootprintFn=void(__fastcall*)(uintptr_t room,int x,int y,uint32_t pattern,uint32_t mask);
ServerStepFn originalServerStep=nullptr;
ClientStepFn originalClientStep=nullptr;
ComputeFn compute=nullptr;TargetFn target=nullptr;MaskFn mask=nullptr;FootprintFn footprintOff=nullptr,footprintOn=nullptr;
std::atomic<bool> armed{false};

double Now(){return double(GetTickCount64())/1000.0;}

// The server's last re-aim, for the client's hero to take (server thread writes, client thread reads).
struct Laid {uint32_t seq=0,activation=0,whirl=0;int originX=0,originY=0,fromX=0,fromY=0,toX=0,toY=0;double at=0;};
SRWLOCK laidLock=SRWLOCK_INIT;
Laid laid;
std::atomic<uint32_t> laidSeq{0};
uint32_t clientSeq=0,clientWhirl=0;   // client thread only: the last taken, the server whirl it followed

// The server's own: the end it saw or laid last (a different one is a new whirl), its last step and re-aim.
struct Side {int endX=0,endY=0,originX=0,originY=0;uint32_t whirl=0;double stepAt=-1,aimAt=-1;uint32_t activation=0;int told=0;};
Side server;

// The unit's whirl under way: its used skill Whirlwind with flags &3 == 1 on a straight path.
bool UnderWay(uintptr_t unit,uintptr_t& skill,uintptr_t& path,wt::Whirl& w,int& endX,int& endY) {
    uintptr_t skills=0,txt=0;int16_t id=0;uint32_t flags=0,type=0,pos[2]{};int32_t end[2]{};
    if(!Read(unit+0x100,skills)||!Read(skills+0x18,skill)||!Read(skill,txt)||!Read(txt,id)||id!=Whirlwind)return false;
    if(!Read(skill+0x14,flags)||(flags&3)!=1)return false;
    if(!Read(unit+0x38,path)||!Read(path+0x50,type)||type!=StraightPath)return false;
    if(!Read(path,pos)||!Read(skill+0x24,end)||end[0]<=0||end[1]<=0||end[0]>65535||end[1]>65535)return false;
    w={float(pos[0])/65536.f,float(pos[1])/65536.f,float(end[0]),float(end[1])};endX=end[0];endY=end[1];
    return true;
}
// The local player's unit (type 0, the local id); server: the server's one, else the client's.
bool Hero(uintptr_t unit,bool server) {
    uint32_t h[3]{};uint32_t id=0;
    if(!Read(unit,h)||h[0]!=0||!combat::LocalPlayerId(id)||h[2]!=id)return false;
    uintptr_t game=0,self=0;
    const bool isServer=melee::LocalServer(game,self)&&self==unit;
    return isServer==server;
}
// Lays the whirl's path to (x, y) the start's way and moves the end's footprint along. False: refused, the old
// end laid again.
bool Lay(uintptr_t unit,uintptr_t path,uintptr_t skill,int x,int y,int& endX,int& endY) {
    int32_t oldX=0,oldY=0;uintptr_t room=0;uint32_t pattern=0;
    if(!Read(skill+0x24,oldX)||!Read(skill+0x28,oldY)||!Read(path+0x20,room)||room<0x10000||!Read(path+0x5c,pattern))return false;
    mask(path,WhirlMask);target(path,x,y);
    const int made=compute(path,unit,0,1);
    mask(path,MovingMask);
    uint32_t count=0;uint16_t last[2]{};
    if(made<=0||!Read(path+0x34,count)||count==0||count>MostPoints||!Read(path+0xc8+uintptr_t(count-1)*4,last)||!last[0]||!last[1]) {
        mask(path,WhirlMask);target(path,oldX,oldY);compute(path,unit,0,1);mask(path,MovingMask);
        return false;
    }
    endX=last[0];endY=last[1];
    if(endX!=oldX||endY!=oldY) {
        footprintOff(room,oldX,oldY,pattern,PlayerFootprint);
        footprintOn(room,endX,endY,pattern,PlayerFootprint);
        Write(skill+0x24,int32_t(endX));Write(skill+0x28,int32_t(endY));
    }
    return true;
}
void Tell(const char* side,const char* what,int fromX,int fromY,int toX,int toY,const wt::Whirl& w) {
    char line[260];
    std::snprintf(line,sizeof line,"Frame whirlwind turn: %s %s - end %d,%d -> %d,%d, the hero at %.1f,%.1f (%.1f cells left before)",
        side,what,fromX,fromY,toX,toY,w.heroX,w.heroY,std::hypot(w.endX-w.heroX,w.endY-w.heroY));
    framevr::logging::Write(line);
}

void ServerTurn(uintptr_t unit) {
    if(!Hero(unit,true))return;
    uintptr_t skill=0,path=0;wt::Whirl w;int endX=0,endY=0;
    if(!UnderWay(unit,skill,path,w,endX,endY))return;
    const double now=Now();
    const bool started=endX!=server.endX||endY!=server.endY||server.stepAt<0||now-server.stepAt>.25;
    server.stepAt=now;server.endX=endX;server.endY=endY;
    if(started){++server.whirl;server.originX=endX;server.originY=endY;}
    framevr::WhirlOnTurn t;
    if(!framevr::WhirlOnStep(t,w.heroX,w.heroY,started))return;
    if(t.activation!=server.activation){server.activation=t.activation;server.told=0;server.aimAt=-1;}
    if(server.aimAt>=0&&now-server.aimAt<wt::IntervalSeconds)return;
    wt::Steer s;s.stop=t.stop;
    if(t.pushed&&WhirlGridWayNow(t.stickX,t.stickY,s.gx,s.gy))s.pushed=true;
    const wt::Aim a=wt::Decide(w,s);
    if(!a.turn)return;
    server.aimAt=now;
    int newX=0,newY=0;
    if(!Lay(unit,path,skill,a.x,a.y,newX,newY)) {
        framevr::WhirlOnTurned(t.activation,2);
        if(server.told++<8)Tell("server","refused (laid back as it was)",endX,endY,a.x,a.y,w);
        return;
    }
    server.endX=newX;server.endY=newY;
    AcquireSRWLockExclusive(&laidLock);
    laid={laid.seq+1,t.activation,server.whirl,server.originX,server.originY,endX,endY,newX,newY,now};
    const uint32_t seq=laid.seq;
    ReleaseSRWLockExclusive(&laidLock);
    laidSeq.store(seq);
    framevr::WhirlOnTurned(t.activation,a.stop?1:0);
    if(server.told++<8)Tell("server",a.stop?"ended short (the stick let go)":"turned with the stick",endX,endY,newX,newY,w);
}
// The client's hero takes the server's last end. Waits (up to 0.3 s) for his own whirl to be under way.
void ClientTurn(uintptr_t unit) {
    if(!Hero(unit,false))return;
    AcquireSRWLockShared(&laidLock);const Laid l=laid;ReleaseSRWLockShared(&laidLock);
    uintptr_t skill=0,path=0;wt::Whirl w;int endX=0,endY=0;
    const bool under=UnderWay(unit,skill,path,w,endX,endY);
    if(!under) {
        if(Now()-l.at>.3){clientSeq=l.seq;framevr::WhirlOnTurned(l.activation,4);}
        return;
    }
    clientSeq=l.seq;
    if(!wt::Follow(w.endX,w.endY,l.originX,l.originY,l.fromX,l.fromY,l.toX,l.toY,clientWhirl==l.whirl)) {
        if(endX!=l.toX||endY!=l.toY)framevr::WhirlOnTurned(l.activation,4);
        return;
    }
    int newX=0,newY=0;
    const bool ok=Lay(unit,path,skill,l.toX,l.toY,newX,newY);
    if(ok)clientWhirl=l.whirl;
    framevr::WhirlOnTurned(l.activation,ok?3:4);
    static int told=0;
    if(!ok&&told++<8)Tell("client","could not follow",endX,endY,l.toX,l.toY,w);
}
uint64_t __fastcall DetourServerStep(uintptr_t game,uintptr_t unit) {
    if(armed.load(std::memory_order_relaxed)&&framevr::WhirlOnActive())ServerTurn(unit);
    return originalServerStep(game,unit);
}
uint64_t __fastcall DetourClientStep(uintptr_t unit,uint64_t flag) {
    if(armed.load(std::memory_order_relaxed)&&laidSeq.load(std::memory_order_relaxed)!=clientSeq)ClientTurn(unit);
    return originalClientStep(unit,flag);
}
}   // namespace

void Install() {
    static std::atomic<int> state{0};   // 0 not yet, 1 in, -1 not in this build
    if(state.load()!=0)return;
    for(const Code& c:kCode) {
        if(!d2rsig::Check(c.rva,c.bytes,c.size)) {
            state.store(-1);
            char line[200];std::snprintf(line,sizeof line,"Frame whirlwind turn: not in this build - the game's %s (0x%llX) is not as expected; whirls turn where the next starts",
                c.name,(unsigned long long)c.rva);
            framevr::logging::Write(line);
            return;
        }
    }
    compute=reinterpret_cast<ComputeFn>(d2rsig::Addr(ComputeRva));target=reinterpret_cast<TargetFn>(d2rsig::Addr(TargetRva));
    mask=reinterpret_cast<MaskFn>(d2rsig::Addr(MaskRva));
    footprintOff=reinterpret_cast<FootprintFn>(d2rsig::Addr(ResetRva));footprintOn=reinterpret_cast<FootprintFn>(d2rsig::Addr(ReserveRva));
    if(!compute||!target||!mask||!footprintOff||!footprintOn){state.store(-1);framevr::logging::Write("Frame whirlwind turn: not in - an address not found");return;}
    // The client's first: with only it in, nothing is ever laid for it to follow.
    if(!d2rsig::Hook(ClientStepRva,ClientStepHook,sizeof ClientStepHook,reinterpret_cast<void*>(&DetourClientStep),reinterpret_cast<void**>(&originalClientStep))) {
        state.store(-1);framevr::logging::Write("Frame whirlwind turn: not in - the client's unit step was not hooked");return;
    }
    if(!d2rsig::Hook(ServerStepRva,ServerStepHook,sizeof ServerStepHook,reinterpret_cast<void*>(&DetourServerStep),reinterpret_cast<void**>(&originalServerStep))) {
        state.store(-1);framevr::logging::Write("Frame whirlwind turn: not in - the server's unit step was not hooked (the client's stays idle)");return;
    }
    armed.store(true);state.store(1);
    framevr::logging::Write("Frame whirlwind turn: in - whirls under way turn with the walk stick (server unit step 0x491ac0, client unit step 0xf9a00, 13 code checks)");
}
}
