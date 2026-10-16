// Offline D2R 3.3.93787 contact resolution. Native range/collision, damage,
// skills and costs stay in charge; only the local player's random misses go.
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include "combat.h"
#include "combat_rules.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"

namespace gamestate {uint64_t LocalPlayer();}

namespace combat {
namespace {
constexpr uint64_t HitRva=0x44ba60,SeedRva=0x34a1e0,BlockRva=0x44b930,DodgeRva=0x44ef70;
constexpr uint64_t HitSeedReturn=0x44bd6d;
constexpr uint8_t HitSig[]={0x4c,0x8b,0xdc,0x53,0x55,0x56,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x90,0x04,0x00,0x00};
constexpr uint8_t SeedSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x85,0xc9,0x75,0x1d};
constexpr uint8_t BlockSig[]={0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x30};
constexpr uint8_t DodgeSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x30};
// Verify the particular roll as well as the function prologues. Its seed is
// consumed inline, then the remainder of HitSuccessful applies native on-hit
// behavior (including Prevent Monster Heal). Returning 1 from the entry hook
// would silently skip that behavior.
constexpr uint8_t RollSig[]={0x48,0x8b,0xce,0xe8,0x73,0xe4,0xef,0xff,0x8b,0x08,0x48,0x69,0xd9,0xc5,0x90,0xc6,0x6a};
using HitFn=int(*)(uintptr_t,uintptr_t,int,int);
using SeedFn=Seed*(*)(uintptr_t);
using BlockFn=int(*)(uintptr_t,uintptr_t,uintptr_t,int,int);
using DodgeFn=int(*)(uintptr_t,uintptr_t,int);
HitFn originalHit=nullptr;SeedFn originalSeed=nullptr;BlockFn originalBlock=nullptr;DodgeFn originalDodge=nullptr;
std::atomic<bool> enabled{false},ready{false};
bool hitIn=false,seedIn=false,blockIn=false,dodgeIn=false,rollChecked=false;
// One atomic value keeps freshness and id coherent, including native id zero.
std::atomic<uint64_t> localIdentity{0},localSession{0};
std::mutex localPlayerMutex;
struct UiPlayer {uintptr_t unit=0;uint64_t session=0,at=0;uint32_t id=0;} uiPlayer;
std::atomic<unsigned> meleeChecks{0},missileChecks{0},rolls{0},blocks{0},dodges{0},failed{0},nativeMelee{0},nativeGuards{0};
// Melee attack rating: the local player's non-missile hit checks (physical
// swings, trigger/button attacks, melee skills) keep the native roll.
// Missile hit checks keep the contact_hits override.
std::atomic<bool> meleeRolls{false};
thread_local bool meleeHit=false; // inside such a native melee hit check
// Classic: trigger/button melee shares the physical swing's attack cycle. The
// latest native melee hit check made outside a swing, read by melee.cpp.
std::atomic<uint64_t> buttonMeleeAt{0},buttonMeleeSession{0};
template<class T> bool Read(uintptr_t p,T& out) {
    return p>0x10000&&framevr::ReadMemory(&out,reinterpret_cast<const void*>(p),sizeof out);
}
// Set only by melee.cpp around ONE physical swing's native basic attack
// (synchronous, game thread). Missiles and button attacks never set it.
thread_local bool nativeRolls=false;
bool Qualifies(uintptr_t attacker,uintptr_t defender) {
    if(nativeRolls||!enabled.load(std::memory_order_relaxed)||!ready.load(std::memory_order_relaxed))return false;
    const uint64_t identity=localIdentity.load();
    const auto session=localSession.load();
    if(!identity||!session||session!=gamestate::LocalPlayer()||uint32_t(GetTickCount())-uint32_t(identity>>32)>1500)return false;
    uint32_t a[3]{},d=~0u;
    return Read(attacker,a)&&Read(defender,d)&&PlayerContact(uint32_t(identity),a[0],a[2],d);
}
struct RollContext {uintptr_t attacker=0;bool missile=false;};
thread_local RollContext roll;
thread_local Seed acceptedRoll;
int Hit(uintptr_t attacker,uintptr_t defender,int accuracy,int missile) {
    const auto saved=roll;const bool savedMelee=meleeHit;
    roll={};
    const bool local=Qualifies(attacker,defender);
    // A Classic swing's own attack runs under NativeRolls, so local is false
    // there: only trigger/button melee reaches this native melee branch.
    meleeHit=local&&!missile&&meleeRolls.load(std::memory_order_relaxed);
    if(meleeHit) {
        nativeMelee.fetch_add(1,std::memory_order_relaxed);
        buttonMeleeSession=gamestate::LocalPlayer();buttonMeleeAt=GetTickCount64();
    } else if(local) {
        roll={attacker,missile!=0};
        (missile?missileChecks:meleeChecks).fetch_add(1,std::memory_order_relaxed);
    }
    const int result=originalHit(attacker,defender,accuracy,missile);
    roll=saved;meleeHit=savedMelee;
    return result;
}
Seed* GetSeed(uintptr_t unit) {
    Seed* native=originalSeed(unit);
    // Every unrelated seed consumer is an unchanged fast path. Only this exact
    // call site inside an eligible HitSuccessful evaluation receives a local
    // zero roll, below the game's minimum 5% threshold.
    if(!roll.attacker||unit!=roll.attacker||reinterpret_cast<uintptr_t>(_ReturnAddress())!=d2rsig::Addr(HitSeedReturn))return native;
    Seed current{};
    if(!framevr::ReadMemory(&current,native,sizeof current)) {++failed;return native;}
    const Seed next=AdvanceSeed(current);
    // Advance the real stream exactly once as the original inline roll would,
    // preserving subsequent damage/critical/loot randomness. The forced roll
    // writes only thread-local scratch. No stats or save data are altered.
    if(!framevr::WriteMemory(native,&next,sizeof next)) {++failed;return native;}
    acceptedRoll={};++rolls;
    return &acceptedRoll;
}
// A block/dodge check made inside a native melee hit check stays native.
// Outside one (or after it returns) the contact_hits bypass still applies.
int Block(uintptr_t game,uintptr_t attacker,uintptr_t defender,int avoid,int block) {
    if(meleeHit)++nativeGuards;
    else if(Qualifies(attacker,defender)) {++blocks;return 0;}
    return originalBlock(game,attacker,defender,avoid,block);
}
int Dodge(uintptr_t attacker,uintptr_t defender,int avoid) {
    if(meleeHit)++nativeGuards;
    else if(Qualifies(attacker,defender)) {++dodges;return 0;}
    return originalDodge(attacker,defender,avoid);
}
}
void ObserveLocalPlayer(uintptr_t unit) {
    uint32_t header[3]{};
    if(!Read(unit,header)||header[0]!=0||header[2]==UINT32_MAX)return;
    localSession=gamestate::LocalPlayer();
    localIdentity=(uint64_t(GetTickCount())<<32)|header[2];
}
bool ObserveUiPlayer(uintptr_t unit,uint64_t session) {
    uint32_t header[3]{};
    if(!session||session!=gamestate::LocalPlayer()||!Read(unit,header)||header[0]!=0||header[2]==UINT32_MAX)return false;
    {std::lock_guard lock(localPlayerMutex);uiPlayer={unit,session,GetTickCount64(),header[2]};}
    ObserveLocalPlayer(unit);return true;
}
bool LocalPlayerUnit(uintptr_t& unit) {
    UiPlayer player;{std::lock_guard lock(localPlayerMutex);player=uiPlayer;}
    uint32_t header[3]{};
    if(!player.unit||!player.session||player.session!=gamestate::LocalPlayer()||GetTickCount64()-player.at>650||
       !Read(player.unit,header)||header[0]!=0||header[2]!=player.id)return false;
    unit=player.unit;return true;
}
bool LocalPlayerId(uint32_t& id) {
    const auto identity=localIdentity.load();const auto session=localSession.load();
    if(!identity||!session||session!=gamestate::LocalPlayer()||uint32_t(GetTickCount())-uint32_t(identity>>32)>1500)return false;
    id=uint32_t(identity);return true;
}
void Configure(bool on) {enabled=on;}
bool NativeRolls(bool on) {const bool was=nativeRolls;nativeRolls=on;return was;}
void ConfigureMeleeRolls(bool native) {meleeRolls=native;}
bool TakeButtonMelee(uint64_t& session,uint64_t& at) {
    at=buttonMeleeAt.exchange(0);session=buttonMeleeSession.load();
    return at!=0;
}
void Install() {
    if(!enabled.load()||ready.load())return;
    if(!rollChecked)rollChecked=d2rsig::Check(0x44bd65,RollSig,sizeof RollSig);
    if(!rollChecked)return;
    if(!seedIn)seedIn=d2rsig::Hook(SeedRva,SeedSig,sizeof SeedSig,reinterpret_cast<void*>(&GetSeed),reinterpret_cast<void**>(&originalSeed));
    if(!hitIn)hitIn=d2rsig::Hook(HitRva,HitSig,sizeof HitSig,reinterpret_cast<void*>(&Hit),reinterpret_cast<void**>(&originalHit));
    if(!blockIn)blockIn=d2rsig::Hook(BlockRva,BlockSig,sizeof BlockSig,reinterpret_cast<void*>(&Block),reinterpret_cast<void**>(&originalBlock));
    if(!dodgeIn)dodgeIn=d2rsig::Hook(DodgeRva,DodgeSig,sizeof DodgeSig,reinterpret_cast<void*>(&Dodge),reinterpret_cast<void**>(&originalDodge));
    if(seedIn&&hitIn&&blockIn&&dodgeIn&&!ready.exchange(true))
        framevr::logging::Write("Frame combat: deterministic local-player hit checks ready; native collision/range and damage retained");
}
void Report() {
    const auto m=meleeChecks.exchange(0),p=missileChecks.exchange(0),r=rolls.exchange(0),b=blocks.exchange(0),d=dodges.exchange(0),f=failed.exchange(0);
    const auto n=nativeMelee.exchange(0),g=nativeGuards.exchange(0);
    if(!m&&!p&&!r&&!b&&!d&&!f&&!n&&!g)return;
    char line[320];snprintf(line,sizeof line,"Frame combat: melee checks=%u missile checks=%u accepted rolls=%u block bypass=%u dodge bypass=%u failures=%u native melee checks=%u native block/dodge in melee=%u",m,p,r,b,d,f,n,g);
    framevr::logging::Write(line);
}
}
