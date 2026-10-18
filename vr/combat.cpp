// Offline D2R 3.3.93787 contact resolution. Native range/collision, damage,
// skills and costs stay in charge; only the local player's random misses go.
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <atomic>
#include <cstdio>
#include <mutex>
#include "combat.h"
#include "../steamvr/shield_debug.h"
#include "combat_rules.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include "shield_guard.h"
#include "melee.h"
#include "../steamvr/transport.h"

namespace gamestate {uint64_t LocalPlayer();int32_t LeftDurability();}

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
// Unleashed physical guard. 44B930 (game, attacker, defender, avoid, block):
// block==0 tail-jumps to Dodge; else rate=347DA0(defender) (<=0 skips to
// Dodge, running divides by 3), then the DEFENDER's seed from 34A1E0 returns
// to 44B9D1, is advanced inline exactly like the hit roll, roll=low%100, and
// roll<rate returns 1 (blocked; 44B720 turns that into result flag 0x10).
// Verified in a decrypted 3.3.93847 snapshot; same RVAs as 3.3.93787.
constexpr uint64_t BlockSeedReturn=0x44b9d1,BlockRollAt=0x44b9c9;
constexpr uint8_t BlockRollSig[]={0x48,0x8b,0xcf,0xe8,0x0f,0xe8,0xef,0xff,0x8b,0x08,0x48,0x69,0xd9,0xc5,0x90,0xc6,0x6a};
std::atomic<bool> guardEnabled{false},guardTrace{false};std::atomic<uint32_t> guardParryMs{750};
bool guardChecked=false;
struct GuardShared {framevr::guard::State state;uint64_t session=0,at=0;float upm=1;bool eligible=false;
    framevr::guard::Debug debug{};uint32_t debugMonster=UINT32_MAX;};
std::mutex guardMutex;GuardShared guard;
enum class Forced:uint8_t {None,Block,Open};
enum class Guard:uint8_t {Native,Open,Block,Parry};
thread_local Forced forced=Forced::None;thread_local bool forcedServed=false;thread_local Seed forcedRoll;
std::atomic<unsigned> guardBlocks{0},guardOpen{0},guardParries{0},guardUnserved{0},guardFallback{0},guardLate{0};
std::atomic<unsigned> guardAttackKnown{0};
std::array<std::atomic<unsigned>,size_t(framevr::guard::Rejection::Count)> guardRejections{};
// Unit stats Get/Set (D2Common GetUnitStat / SetUnitStat) are jump stubs
// through the game's own table; Set is a set, not an add (native fills life
// with Set(u,6,Get(u,7))).
constexpr uint64_t GetStatRva=0x2f5020,SetStatRva=0x2f7d10;
constexpr uint8_t GetStatSig[]={0xff,0x25,0xf2,0x51,0xb3,0x03,0x90,0x90,0x90,0x90};
constexpr uint8_t SetStatSig[]={0xff,0x25,0x32,0x25,0xb3,0x03,0x90,0x90,0x90,0x90};
using GetStatFn=int(*)(uintptr_t,int,int);using SetStatFn=void(*)(uintptr_t,int,int,int);
SetStatFn originalSetStat=nullptr;bool setStatIn=false;
// Damage application (44CE80: game, attacker, defender, flag, damage*). Its
// life write - Set(defender,6,max(Get(defender,6)-damage[+0x134],0)) - returns
// to 44D098 (seen for every landed weapon hit, 2026-10-17). 44DF10 can
// recompute +0x134 inside, so damage is scaled at that final write, not at entry.
constexpr uint64_t ApplyDamageRva=0x44ce80,LifeWriteReturn=0x44d098,LifeWriteAt=0x44d08f;
constexpr uint8_t ApplyDamageSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0xe8,0xfe,0xff,0xff};
constexpr uint8_t LifeWriteSig[]={0x41,0x8d,0x51,0x06,0xe8,0x78,0xac,0xea,0xff,0x8b,0x9f,0x24,0x01,0x00,0x00};
using ApplyDamageFn=uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,int,uintptr_t);
ApplyDamageFn originalApplyDamage=nullptr;bool applyDamageIn=false;std::atomic<bool> damageScaleReady{false};
// This game thread is inside a physical swing's native attack on this monster
// (melee.cpp), and inside its damage application with this record.
constexpr uint32_t NoSwing=UINT32_MAX;
thread_local uint32_t swingTarget=NoSwing;thread_local uintptr_t damageRecord=0;
// Riposte: a parry opens a window for one stronger hit on the parried monster.
std::atomic<float> riposteBonus{0};std::atomic<uint32_t> riposteMs{1500};
std::atomic<uint32_t> riposteMonster{NoSwing};std::atomic<uint64_t> riposteUntil{0};
std::atomic<unsigned> ripostesLanded{0};
// Unleashed swing damage: attack rating and attack speed scale a physical
// swing's damage instead of deciding whether it lands. HitSuccessful computes
// the clamped chance (44BD44: edi=min(95,max(5,...))), rolls, then passes both
// to 427EA0 (game, attacker, defender, chance, roll) - its only call site. A
// pass-through hook there records the chance of the swing on this thread.
constexpr uint64_t HitChanceRva=0x427ea0,ChanceClampAt=0x44bd44,ChanceCallAt=0x44bda2;
constexpr uint8_t HitChanceSig[]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x98,0x02,0x00,0x00};
constexpr uint8_t ChanceClampSig[]={0xbf,0x5f,0x00,0x00,0x00,0x41,0x03,0xcd,0x41,0x0f,0xaf,0xd5,0x8d,0x04,0x12,0x99,0xf7,0xf9,
    0xb9,0x05,0x00,0x00,0x00,0x3b,0xc1,0x0f,0x4f,0xc8,0x3b,0xcf,0x0f,0x4c,0xf9};
constexpr uint8_t ChanceCallSig[]={0x89,0x5c,0x24,0x20,0x44,0x8b,0xcf,0x4d,0x8b,0xc6,0x48,0x8b,0xd6,0xe8,0xec,0xc0,0xfd,0xff};
using HitChanceFn=uint64_t(*)(uintptr_t,uintptr_t,uintptr_t,int,int);
HitChanceFn originalHitChance=nullptr;bool hitChanceIn=false;
std::atomic<bool> arDamage{false};std::atomic<float> arFloor{0};
thread_local int swingChance=-1;     // this swing's native to-hit %, -1 = not seen
thread_local float swingRecovery=1;  // this stroke's recovery factor (melee.cpp)
std::atomic<unsigned> scaledHits{0},chanceUnseen{0};
std::atomic<int> lastChance{-1};std::atomic<float> lastRecovery{1},lastFactor{1};
// The riposte's own sound: a WAV next to d2r_vr.ini, played from memory at
// the game's own volume (Settings.json: Master x the active preset's Combat).
std::mutex riposteSoundMutex;std::vector<std::vector<uint8_t>> riposteWavs; // every buffer kept alive: one may be playing
std::vector<uint8_t> riposteSource;float riposteGain=-1,riposteExtra=1;
std::atomic<const uint8_t*> riposteWav{nullptr};
// 16-bit PCM WAV at this gain (the source untouched). riposteSoundMutex held.
void PublishRiposteWav(float gain) {
    if(riposteSource.empty()){riposteWav=nullptr;return;}
    std::vector<uint8_t> w=riposteSource;
    for(size_t i=12;i+8<=w.size();) {   // walk the chunks to "data"
        uint32_t n=0;memcpy(&n,w.data()+i+4,4);
        if(!memcmp(w.data()+i,"data",4)) {
            for(size_t k=i+8;k+1<i+8+n&&k+1<w.size();k+=2) {
                int16_t v;memcpy(&v,w.data()+k,2);
                v=int16_t(std::clamp(float(v)*gain,-32768.f,32767.f));memcpy(w.data()+k,&v,2);
            }
            break;
        }
        i+=8+n+(n&1);
    }
    riposteWavs.push_back(std::move(w));riposteWav=riposteWavs.back().data();riposteGain=gain;
}
std::wstring GameSettingsPath() {
    wchar_t home[MAX_PATH];if(!GetEnvironmentVariableW(L"USERPROFILE",home,MAX_PATH))return {};
    return std::wstring(home)+L"\\Saved Games\\Diablo II Resurrected\\Settings.json";
}
// D2R's Settings.json: "Master Volume" x the active "Advanced Audio Preset"'s Combat slider.
float GameCombatVolume() {
    std::string text;
    if(FILE* f=_wfopen(GameSettingsPath().c_str(),L"rb")){char buf[4096];size_t n;while((n=fread(buf,1,sizeof buf,f))>0&&text.size()<256*1024)text.append(buf,n);fclose(f);}
    auto number=[&](const std::string& key,float fallback){
        const auto at=text.find("\""+key+"\"");if(at==std::string::npos)return fallback;
        const auto colon=text.find(':',at);if(colon==std::string::npos)return fallback;
        return float(atof(text.c_str()+colon+1));
    };
    const int preset=int(number("Advanced Audio Preset",0));
    const float master=number("Master Volume",100),combat=number("Audio AdvancedCombat Preset "+std::to_string(preset),100);
    return std::clamp(master/100.f,0.f,1.f)*std::clamp(combat/100.f,0.f,1.f);
}
// UI thread, every 2 s: follow the game's volume sliders when it saves them.
void FollowGameVolume() {
    static ULONGLONG checkAt=0;static FILETIME seen{};
    const auto now=GetTickCount64();if(now<checkAt)return;checkAt=now+2000;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    const bool known=GetFileAttributesExW(GameSettingsPath().c_str(),GetFileExInfoStandard,&a);
    std::lock_guard lock(riposteSoundMutex);
    if(riposteSource.empty())return;
    if(riposteGain>=0&&(!known||!CompareFileTime(&a.ftLastWriteTime,&seen)))return;
    if(known)seen=a.ftLastWriteTime;
    const float gain=GameCombatVolume()*riposteExtra;
    if(gain!=riposteGain) {
        PublishRiposteWav(gain);
        char line[120];snprintf(line,sizeof line,"Frame riposte: sound at %.0f%% (game Master x Combat x riposte_volume)",gain*100);
        framevr::logging::Write(line);
    }
}
void PlayRiposteSound() {
    using PlaySoundFn=BOOL(WINAPI*)(LPCWSTR,HMODULE,DWORD);
    static const PlaySoundFn play=[]{HMODULE m=LoadLibraryW(L"winmm.dll");return m?reinterpret_cast<PlaySoundFn>(GetProcAddress(m,"PlaySoundW")):nullptr;}();
    if(const uint8_t* wav=riposteWav.load();wav&&play)play(reinterpret_cast<LPCWSTR>(wav),nullptr,0x0004|0x0001|0x0002); // SND_MEMORY|SND_ASYNC|SND_NODEFAULT
}
// [gameplay] enemies_always_hit: monsters' to-hit rolls against the local
// player always connect (same scratch-seed path as the player's own hits), so
// only the physical shield decides. Off by default; applied live.
std::atomic<bool> enemiesAlwaysHit{false};std::atomic<unsigned> forcedHits{0};
framevr::guard::Tuning GuardTuning() {framevr::guard::Tuning t;t.parryMs=guardParryMs.load(std::memory_order_relaxed);return t;}
template<class T> bool Read(uintptr_t p,T& out) {
    return p>0x10000&&framevr::ReadMemory(&out,reinterpret_cast<const void*>(p),sizeof out);
}
void SetStat(uintptr_t unit,int stat,int value,int layer) {
    // The life write of a physical swing's damage: a riposte on the parried
    // monster is stronger, once.
    if(stat==6&&layer==0&&swingTarget!=NoSwing&&damageRecord&&damageScaleReady.load(std::memory_order_relaxed)&&
       reinterpret_cast<uintptr_t>(_ReturnAddress())==d2rsig::Addr(LifeWriteReturn)) {
        uint32_t header[3]{};int damage=0;
        if(Read(unit,header)&&header[0]==1&&header[2]==swingTarget&&Read(damageRecord+0x134,damage)&&damage>0) {
            float factor=1;
            const auto now=GetTickCount64();
            if(riposteMonster.load()==header[2]&&now<riposteUntil.load()) {
                // A riposte is a clean hit: no attack-rating or recovery penalty.
                factor=1+riposteBonus.load();riposteMonster=NoSwing;riposteUntil=0;++ripostesLanded;PlayRiposteSound();
            } else {
                // Unleashed: attack rating and recovery decide how hard it lands.
                if(arDamage.load(std::memory_order_relaxed)) {
                    if(swingChance>=0)factor*=ArFactor(swingChance,arFloor.load(std::memory_order_relaxed));
                    else ++chanceUnseen;
                }
                factor*=swingRecovery;
                if(factor!=1){++scaledHits;lastChance=swingChance;lastRecovery=swingRecovery;lastFactor=factor;}
            }
            if(factor!=1) {
                const int before=reinterpret_cast<GetStatFn>(d2rsig::Addr(GetStatRva))(unit,6,0);
                const int64_t life=int64_t(before)-int64_t(double(damage)*factor);
                value=life>=0x100?int(life):0;   // native: under one point of life is death
            }
        }
    }
    originalSetStat(unit,stat,value,layer);
}
uint64_t HitChance(uintptr_t game,uintptr_t attacker,uintptr_t defender,int chance,int roll) {
    // Only the hit check of the physical swing running on this thread.
    if(swingTarget!=NoSwing) {
        uint32_t a=~0u,d[3]{};
        if(Read(attacker,a)&&a==0&&Read(defender,d)&&d[0]==1&&d[2]==swingTarget)swingChance=chance;
    }
    return originalHitChance(game,attacker,defender,chance,roll);
}
uint64_t ApplyDamage(uintptr_t game,uintptr_t attacker,uintptr_t defender,int flag,uintptr_t damage) {
    const auto saved=damageRecord;damageRecord=damage;
    const auto result=originalApplyDamage(game,attacker,defender,flag,damage);
    damageRecord=saved;return result;
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
    } else if(enemiesAlwaysHit.load(std::memory_order_relaxed)&&enabled.load(std::memory_order_relaxed)&&ready.load(std::memory_order_relaxed)) {
        uint32_t id=0,a[3]{},d[3]{};
        if(LocalPlayerId(id)&&Read(attacker,a)&&Read(defender,d)&&IncomingContact(id,a[0],d[0],d[2])) {
            roll={attacker,missile!=0};forcedHits.fetch_add(1,std::memory_order_relaxed);
        }
    }
    const int result=originalHit(attacker,defender,accuracy,missile);
    roll=saved;meleeHit=savedMelee;
    return result;
}
Seed* GetSeed(uintptr_t unit) {
    Seed* native=originalSeed(unit);
    // Physical guard: only the block roll inside one judged incoming hit.
    // Advance the real stream once, exactly as the inline roll would.
    if(forced!=Forced::None&&!forcedServed&&reinterpret_cast<uintptr_t>(_ReturnAddress())==d2rsig::Addr(BlockSeedReturn)) {
        Seed current{};
        if(!framevr::ReadMemory(&current,native,sizeof current)) {++failed;return native;}
        const Seed next=AdvanceSeed(current);
        if(!framevr::WriteMemory(native,&next,sizeof next)) {++failed;return native;}
        forcedRoll=GuardRoll(forced==Forced::Block);forcedServed=true;
        return &forcedRoll;
    }
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
bool GridPosition(uintptr_t unit,float& x,float& z) {
    uintptr_t path=0;uint16_t p[4]{};
    if(!Read(unit+0x38,path)||!Read(path,p))return false;
    x=2*(p[1]+p[0]/65536.f);z=2*(p[3]+p[2]/65536.f);return true;
}
// A monster (or its missile's owner) whose native to-hit roll already passed
// against the local player. Native = keep the stat block roll.
Guard Incoming(uintptr_t attacker,uintptr_t defender,uint32_t& monster,bool& inReach,uint64_t& parryAt) {
    parryAt=0;
    if(!guardChecked||!guardEnabled.load(std::memory_order_relaxed)||!enabled.load(std::memory_order_relaxed)||!ready.load(std::memory_order_relaxed))return Guard::Native;
    uint32_t id=0,a[3]{},d[3]{};
    if(!LocalPlayerId(id)||!Read(attacker,a)||!Read(defender,d)||!IncomingContact(id,a[0],d[0],d[2]))return Guard::Native;
    GuardShared g;{std::lock_guard lock(guardMutex);g=guard;}
    const auto now=GetTickCount64();
    if(!g.eligible||g.session!=gamestate::LocalPlayer()||now-g.at>250)return Guard::Native;
    float ax=0,az=0,px=0,pz=0;
    if(!GridPosition(attacker,ax,az)||!GridPosition(defender,px,pz)) {++guardFallback;return Guard::Native;}
    monster=a[2];
    using framevr::guard::Verdict;
    const framevr::math::Vec to{(ax-px)/g.upm,0,(az-pz)/g.upm};
    // when this attacker's attack began (melee.cpp's mode hook; 0 = not seen)
    const uint64_t attackAt=melee::AttackStart(monster);if(attackAt)++guardAttackKnown;
    framevr::guard::Rejection reason;
    const auto v=framevr::guard::Judge(g.state,to,now,GuardTuning(),attackAt,&reason);
    {
        std::lock_guard lock(guardMutex);
        if(guard.session==g.session) {
            const auto& p=g.state.surface;
            guard.debug.hitFrom=p.player+framevr::math::Vec{to.x,p.center.y,to.z}*g.upm;
            guard.debug.hitTo=p.player+framevr::math::Vec{p.eye.x,p.center.y,p.eye.z}*g.upm;
            guard.debug.hitAt=now;guard.debug.hit=0;guard.debugMonster=monster;
        }
    }
    if(v==Verdict::Open)++guardRejections[size_t(reason)];
    // Walking/running does not invalidate a raised physical guard. Native
    // block/dodge handling still runs below; keep the mode for diagnostics.
    uint32_t playerMode=UINT32_MAX;
    Read(defender+0xc,playerMode);
    inReach=std::hypot(to.x,to.z)<=GuardTuning().parryRange;   // melee range (for the late-swing diagnostic)
    if(v==Verdict::Parry)parryAt=g.state.snapAt;
    if(guardTrace.load(std::memory_order_relaxed)) {
        const float d=std::hypot(to.x,to.z);const auto& s=g.state;
        float cover=0;framevr::guard::Coverage(s.surface,to,GuardTuning(),&cover);
        const auto parry=framevr::guard::ParryCheck(s,to,now,GuardTuning(),attackAt);   // why a raised guard did not parry
        char line[860];snprintf(line,sizeof line,
            "Frame guard trace: monster=%u class=%u verdict=%s reason=%s parry=%s player_mode=%u raised=%d forward=%.2f up=%.2f side=%.2f m cover_cos=%.2f attacker=%.2f m swing=%.2f m swing_cos=%.2f snap_age=%lld ms snap_spent=%d attack_age=%lld ms pose_age=%llu ms center=%.2f,%.2f,%.2f normal=%.2f,%.2f,%.2f eye=%.2f,%.2f,%.2f attacker_xz=%.2f,%.2f upm=%.3f",
            a[2],a[1],v==Verdict::Parry?"PARRY":v==Verdict::Block?"BLOCK":"OPEN",framevr::guard::ReasonName(reason),framevr::guard::ParryMissName(parry),playerMode,int(s.raised),s.forward,s.up,s.side,cover,d,s.swing,s.swingCos,
            s.snapAt&&now>=s.snapAt?(long long)(now-s.snapAt):-1LL,int(s.snapUsed),
            attackAt&&now>=attackAt?(long long)(now-attackAt):-1LL,
            (unsigned long long)(s.seenAt&&now>=s.seenAt?now-s.seenAt:0),
            s.surface.center.x,s.surface.center.y,s.surface.center.z,s.surface.normal.x,s.surface.normal.y,s.surface.normal.z,
            s.surface.eye.x,s.surface.eye.y,s.surface.eye.z,to.x,to.z,g.upm);
        const auto& p=s.surface;
        snprintf(line+strlen(line),sizeof line-strlen(line)," mesh=%d size=%.3f,%.3f m",
            int(framevr::guard::Measured(p)),2*std::sqrt(framevr::math::Dot(p.halfRight,p.halfRight)),2*std::sqrt(framevr::math::Dot(p.halfUp,p.halfUp)));
        framevr::logging::Write(line);
    }
    return v==Verdict::Parry?Guard::Parry:v==Verdict::Block?Guard::Block:Guard::Open;
}
// Native still runs in full (avoid/dodge, block animation, FBR, sounds); only
// its block roll is replaced by the physical verdict.
uint64_t NativeCallerRva(uintptr_t caller) {
    // Addr(0) means unresolved, not the image base. Subtract the resolved
    // block offset so diagnostics also work when signatures relocate it.
    const auto address=d2rsig::Addr(BlockRva),offset=d2rsig::Rva(BlockRva);
    const auto base=address>offset?address-offset:0;
    return base&&caller>=base&&caller-base<0x4000000?caller-base:0;
}
int GuardedBlock(uintptr_t game,uintptr_t attacker,uintptr_t defender,int avoid,int block,Guard g,uint32_t monster,bool inReach,uint64_t parryAt,uintptr_t caller) {
    const int32_t durability=gamestate::LeftDurability();   // before this block wears the shield
    const auto savedForced=forced;const bool savedServed=forcedServed;
    forced=g==Guard::Open?Forced::Open:Forced::Block;forcedServed=false;
    const int result=originalBlock(game,attacker,defender,avoid,block);
    const bool served=forcedServed;forced=savedForced;forcedServed=savedServed;
    {
        std::lock_guard lock(guardMutex);
        if(guard.debugMonster==monster&&guard.session==gamestate::LocalPlayer())
            guard.debug.hit=!served?3:result==1?1:2;
    }
    if(guardTrace.load(std::memory_order_relaxed)) {
        const auto rva=NativeCallerRva(caller);
        char line[280];snprintf(line,sizeof line,
            "Frame guard native: monster=%u verdict=%s block_flag=%d avoid_flag=%d result=%d roll_served=%d durability=%d caller=0x%llx",
            monster,g==Guard::Parry?"PARRY":g==Guard::Block?"BLOCK":"OPEN",block,avoid,result,int(served),durability,(unsigned long long)rva);
        framevr::logging::Write(line);
    }
    if(!served)++guardUnserved; // native never reached its block roll (dodged/avoided first, or cannot block)
    else if(g==Guard::Open)++guardOpen;
    else {
        ++guardBlocks;
        // Unblockable damage can visit this adapter too. Do not consume the
        // pending parry unless the native shield roll actually took place.
        if(g==Guard::Parry){std::lock_guard lock(guardMutex);framevr::guard::SpendParry(guard.state,parryAt);}
        const bool parried=g==Guard::Parry&&melee::QueueParry(monster,durability);
        framevr::ShieldGuardImpact(parried);   // a block strains the guard; a parry does not
        if(parried)++guardParries;
        else {
            // The native block's own sound never plays in first person (only the
            // haptics did, 2026-10-16): the game's block sound at the hero.
            melee::QueueBlockSound();
            if(g==Guard::Block&&inReach){std::lock_guard lock(guardMutex);framevr::guard::NoteBlock(guard.state,GetTickCount64());}
        }
    }
    return result;
}
// A block/dodge check made inside a native melee hit check stays native.
// Outside one (or after it returns) the contact_hits bypass still applies.
int Block(uintptr_t game,uintptr_t attacker,uintptr_t defender,int avoid,int block) {
    if(meleeHit)++nativeGuards;
    else if(Qualifies(attacker,defender)) {++blocks;return 0;}
    else {
        uint32_t monster=0;bool inReach=false;uint64_t parryAt=0;
        if(const auto g=Incoming(attacker,defender,monster,inReach,parryAt);g!=Guard::Native)
            return GuardedBlock(game,attacker,defender,avoid,block,g,monster,inReach,parryAt,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }
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
void ConfigureGuard(bool on,uint32_t parryMs,bool trace) {guardEnabled=on;guardParryMs=parryMs;guardTrace=trace;}
void ConfigureEnemiesAlwaysHit(bool on) {enemiesAlwaysHit=on;}
float GameVolume() {
    static std::mutex m;static ULONGLONG checkAt=0;static FILETIME seen{};static float volume=-1;
    std::lock_guard lock(m);
    const auto now=GetTickCount64();
    if(volume>=0&&now<checkAt)return volume;
    checkAt=now+2000;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    const bool known=GetFileAttributesExW(GameSettingsPath().c_str(),GetFileExInfoStandard,&a);
    if(volume<0||!known||CompareFileTime(&a.ftLastWriteTime,&seen)){if(known)seen=a.ftLastWriteTime;volume=GameCombatVolume();}
    return volume;
}
uint32_t PhysicalSwing(uint32_t target) {const auto was=swingTarget;swingTarget=target;swingChance=-1;return was;}
float SwingRecovery(float factor) {const float was=swingRecovery;swingRecovery=factor;return was;}
void ConfigureArDamage(bool on,float floor) {arDamage=on;arFloor=std::clamp(floor,0.f,1.f);}
void NoteRiposte(uint32_t monster) {
    if(riposteMs.load()==0)return;   // a riposte is worth having with no bonus: it is a clean hit
    riposteMonster=monster;riposteUntil=GetTickCount64()+riposteMs.load();
}
void ConfigureRiposte(float bonus,uint32_t windowMs,float volume,const wchar_t* soundFile) {
    riposteBonus=std::clamp(bonus,0.f,4.f);riposteMs=windowMs;
    {std::lock_guard lock(riposteSoundMutex);const float v=std::clamp(volume,0.f,4.f);if(riposteExtra!=v){riposteExtra=v;riposteGain=-1;}}
    static std::wstring loaded=L"?"; // settings thread only
    if(soundFile&&loaded!=soundFile) {
        loaded=soundFile;std::vector<uint8_t> bytes;
        if(!*soundFile) {   // the default: the WAV embedded in this plugin (steamvr/assets/riposte-impact.wav)
            HMODULE module=nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&PlayRiposteSound),&module);
            if(HRSRC res=module?FindResourceW(module,L"D2RVR_RIPOSTE_SOUND",MAKEINTRESOURCEW(10)):nullptr)
                if(HGLOBAL h=LoadResource(module,res))
                    if(const auto* p=static_cast<const uint8_t*>(LockResource(h)))bytes.assign(p,p+SizeofResource(module,res));
        } else if(FILE* f=_wfopen(soundFile,L"rb")) {
            fseek(f,0,SEEK_END);const long n=ftell(f);fseek(f,0,SEEK_SET);
            if(n>44&&n<8*1024*1024){bytes.resize(size_t(n));if(fread(bytes.data(),1,bytes.size(),f)!=bytes.size())bytes.clear();}
            fclose(f);
        }
        const bool wav=bytes.size()>12&&!memcmp(bytes.data(),"RIFF",4)&&!memcmp(bytes.data()+8,"WAVE",4);
        std::lock_guard lock(riposteSoundMutex);
        riposteSource=wav?std::move(bytes):std::vector<uint8_t>{};riposteGain=-1;   // published at the game's volume next tick
        if(!wav)riposteWav=nullptr;
        char line[160];snprintf(line,sizeof line,"Frame riposte: %s sound %s",*soundFile?"ini":"built-in",wav?"loaded":"missing or not a WAV; silent");
        framevr::logging::Write(line);
    }
}
void PublishGuard(uint64_t session,bool eligible,bool posed,const framevr::guard::Pose& pose,
                  bool handOk,bool leftShield) {
    const auto now=GetTickCount64();const auto t=GuardTuning();
    uint64_t lateAfter=0;bool late=false,raised=false;
    {
    std::lock_guard lock(guardMutex);
    if(guard.session!=session||!eligible||guard.state.surface.epoch!=pose.epoch){guard.state={};guard.debug={};}
    const uint64_t armed=guard.state.snapAt;
    if(eligible)framevr::guard::Update(guard.state,pose,handOk,leftShield,now,posed,t);
    // Trace: each newly armed parry swing, to time it against the next hit.
    if(eligible&&guardTrace.load(std::memory_order_relaxed)&&guard.state.snapAt&&guard.state.snapAt!=armed) {
        const auto& s=guard.state;
        char line[200];snprintf(line,sizeof line,"Frame guard swing: armed swing=%.2f m swing_cos=%.2f forward=%.2f up=%.2f side=%.2f m",
            s.swing,s.swingCos,s.forward,s.up,s.side);
        framevr::logging::Write(line);
    }
    if(eligible&&framevr::guard::SwingAfterBlock(guard.state,now,t,lateAfter)){late=true;++guardLate;}
    guard.session=session;guard.eligible=eligible;guard.upm=std::max(1.f,guard.state.surface.unitsPerMetre);guard.at=now;
    raised=eligible&&guard.state.raised;
    // Trace: where the posed shield sits relative to the body, every 2 s, so a
    // held guard can be checked against the thresholds without being hit.
    static uint64_t traceAt=0;
    if(guardTrace.load(std::memory_order_relaxed)&&now>=traceAt) {
        traceAt=now+2000;const auto& s=guard.state;
        char line[300];snprintf(line,sizeof line,"Frame guard pose: eligible=%d posed=%d hand=%d raised=%d reason=%s forward=%.2f up=%.2f side=%.2f m swing=%.2f m swing_cos=%.2f",
            int(eligible),int(posed),int(handOk),int(s.raised),framevr::guard::ReasonName(s.rejection),s.forward,s.up,s.side,s.swing,s.swingCos);
        framevr::logging::Write(line);
    }
    }
    framevr::ShieldGuardState(eligible,raised);   // the strain light drains only while the shield is down
    FollowGameVolume();
    // A parry swing that came after the hit it was meant for: how late (no parry).
    if(late&&guardTrace.load(std::memory_order_relaxed)) {
        char line[160];snprintf(line,sizeof line,"Frame guard swing too late: %llu ms after the blocked hit (no parry)",(unsigned long long)lateAfter);
        framevr::logging::Write(line);
    }
}
bool ReadGuardDebug(framevr::guard::Debug& out) {
    std::lock_guard lock(guardMutex);out={};
    const auto now=GetTickCount64();
    if(!guardEnabled.load()||!guard.eligible||guard.session!=gamestate::LocalPlayer()||!guard.state.seenAt||
       now<guard.state.seenAt||now-guard.state.seenAt>GuardTuning().staleMs)return false;
    out=guard.debug;out.pose=guard.state.surface;out.raised=guard.state.raised;return framevr::guard::Valid(out.pose);
}
void Install() {
    if(!enabled.load()||ready.load())return;
    if(!rollChecked)rollChecked=d2rsig::Check(0x44bd65,RollSig,sizeof RollSig);
    if(!rollChecked)return;
    if(!guardChecked&&BlockSeedReturn&&BlockRollAt) {
        guardChecked=d2rsig::Check(BlockRollAt,BlockRollSig,sizeof BlockRollSig);
        framevr::logging::Write(guardChecked?"Frame guard: block roll verified; Unleashed physical shield blocking ready":
                                             "Frame guard: block roll signature mismatch; native block roll retained");
    }
    // The stat setter and damage application: riposte damage
    // scaling on physical swings.
    if(!setStatIn&&d2rsig::Check(GetStatRva,GetStatSig,sizeof GetStatSig))
        setStatIn=d2rsig::Hook(SetStatRva,SetStatSig,sizeof SetStatSig,reinterpret_cast<void*>(&SetStat),reinterpret_cast<void**>(&originalSetStat));
    if(setStatIn&&!applyDamageIn&&d2rsig::Check(LifeWriteAt,LifeWriteSig,sizeof LifeWriteSig))
        applyDamageIn=d2rsig::Hook(ApplyDamageRva,ApplyDamageSig,sizeof ApplyDamageSig,reinterpret_cast<void*>(&ApplyDamage),reinterpret_cast<void**>(&originalApplyDamage));
    if(applyDamageIn&&!damageScaleReady.exchange(true))
        framevr::logging::Write("Frame riposte: damage application verified; riposte damage ready");
    // Unleashed attack-rating damage: the native to-hit chance, read where
    // HitSuccessful hands it to 427EA0. Without it swings keep full damage.
    if(damageScaleReady.load()&&!hitChanceIn&&d2rsig::Check(ChanceClampAt,ChanceClampSig,sizeof ChanceClampSig)&&
       d2rsig::Check(ChanceCallAt,ChanceCallSig,sizeof ChanceCallSig)) {
        hitChanceIn=d2rsig::Hook(HitChanceRva,HitChanceSig,sizeof HitChanceSig,reinterpret_cast<void*>(&HitChance),reinterpret_cast<void**>(&originalHitChance));
        framevr::logging::Write(hitChanceIn?"Frame swing damage: native hit chance verified; attack-rating damage ready":
                                            "Frame swing damage: hit chance hook failed; swings keep full damage");
    }
    if(!seedIn)seedIn=d2rsig::Hook(SeedRva,SeedSig,sizeof SeedSig,reinterpret_cast<void*>(&GetSeed),reinterpret_cast<void**>(&originalSeed));
    if(!hitIn)hitIn=d2rsig::Hook(HitRva,HitSig,sizeof HitSig,reinterpret_cast<void*>(&Hit),reinterpret_cast<void**>(&originalHit));
    if(!blockIn)blockIn=d2rsig::Hook(BlockRva,BlockSig,sizeof BlockSig,reinterpret_cast<void*>(&Block),reinterpret_cast<void**>(&originalBlock));
    if(!dodgeIn)dodgeIn=d2rsig::Hook(DodgeRva,DodgeSig,sizeof DodgeSig,reinterpret_cast<void*>(&Dodge),reinterpret_cast<void**>(&originalDodge));
    if(seedIn&&hitIn&&blockIn&&dodgeIn&&!ready.exchange(true))
        framevr::logging::Write("Frame combat: deterministic local-player hit checks ready; native collision/range and damage retained");
}
void Report() {
    const auto gb=guardBlocks.exchange(0),go=guardOpen.exchange(0),gp=guardParries.exchange(0),gu=guardUnserved.exchange(0),gf=guardFallback.exchange(0),gl=guardLate.exchange(0);
    const auto ga=guardAttackKnown.exchange(0);
    std::string rejected;
    for(size_t i=1;i<guardRejections.size();++i)if(const auto n=guardRejections[i].exchange(0)) {
        if(rejected.empty())rejected="Frame guard rejected:";
        rejected+=" ";rejected+=framevr::guard::ReasonName(framevr::guard::Rejection(i));rejected+="=";rejected+=std::to_string(n);
    }
    if(!rejected.empty())framevr::logging::Write(rejected.c_str());
    const auto fh=forcedHits.exchange(0);
    if(uint32_t m=riposteMonster.load(),seen=m;m!=NoSwing&&GetTickCount64()>riposteUntil.load()&&riposteMonster.compare_exchange_strong(m,NoSwing)) {
        char line[120];snprintf(line,sizeof line,"Frame riposte: window expired unused (monster=%u)",seen);framevr::logging::Write(line);
    }
    if(const auto sh=scaledHits.exchange(0),cu=chanceUnseen.exchange(0);sh||cu) {
        char line[200];snprintf(line,sizeof line,"Frame swing damage: scaled hits=%u last chance=%d%% last recovery x cleave=%.2f last factor=%.2f chance unseen=%u",
            sh,lastChance.load(),lastRecovery.load(),lastFactor.load(),cu);
        framevr::logging::Write(line);
    }
    if(const auto rl=ripostesLanded.exchange(0)) {
        char line[160];snprintf(line,sizeof line,"Frame riposte: landed=%u",rl);
        framevr::logging::Write(line);
    }
    if(gb||go||gp||gu||gf||fh||gl) {
        char guardLine[260];snprintf(guardLine,sizeof guardLine,"Frame guard: blocked=%u open=%u parries=%u swings_too_late=%u attack_start_known=%u no_block_roll=%u position_fallback=%u enemy_forced_hits=%u",gb,go,gp,gl,ga,gu,gf,fh);
        framevr::logging::Write(guardLine);
    }
    const auto m=meleeChecks.exchange(0),p=missileChecks.exchange(0),r=rolls.exchange(0),b=blocks.exchange(0),d=dodges.exchange(0),f=failed.exchange(0);
    const auto n=nativeMelee.exchange(0),g=nativeGuards.exchange(0);
    if(!m&&!p&&!r&&!b&&!d&&!f&&!n&&!g)return;
    char line[320];snprintf(line,sizeof line,"Frame combat: melee checks=%u missile checks=%u accepted rolls=%u block bypass=%u dodge bypass=%u failures=%u native melee checks=%u native block/dodge in melee=%u",m,p,r,b,d,f,n,g);
    framevr::logging::Write(line);
}
}
