// Free-swing contact -> native basic attack, on the authoritative game thread.
// All addresses/signatures are for D2R 3.3.93787. No HP/stat/table/save edits.
#include <windows.h>
#include <intrin.h>
#include <D2RLPlugin/api.h>
#include <atomic>
#include <mutex>
#include <array>
#include <cstdio>
#include <vector>
#include <algorithm>
#include "native_skill_dispatch.h"
#include "bow_signatures.h"
#include "native_ui_signatures.h"
#include "spell_signatures.h"
#include "spells.h"
#include "interactionstate.h"
#include "melee.h"
#include "melee_weapon.h"
#include "breakables.h"
#include "wall_cells.h"
#include "prop_scan.h"
#include "../steamvr/melee_hand_state.h"
#include "../steamvr/target_cone.h"
#include "bow.h"
#include "combat.h"
#include "combat_rules.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include "../steamvr/transport.h"
#include "../steamvr/callback_ticket.h"
#include "../steamvr/world_coordinates.h"
#include "../steamvr/attack_speed.h"
#include "../steamvr/strike_skills.h"
namespace gamestate {uint64_t LocalPlayer();bool MenuOpen();uint32_t HandsKey();uint32_t WeaponSet();
    int32_t LeftDurability();bool RestoreLeftDurability(const D2RL::PluginContext*,int32_t);}
namespace melee {
namespace {
using namespace framevr::melee;
constexpr uint64_t AttackRva=0x42feb0,TargetRva=0x48fe20,LookupRva=0x48fe80,RangeRva=0x348650;
constexpr uint64_t FlagsRva=0x44b720,RelationRva=0x48e460,LineRva=0x3502a0,StatsRva=0x975b0;
constexpr uint8_t AttackSig[]={0x40,0x55,0x56,0x41,0x54,0x41,0x56,0x48,0x8d,0xac,0x24,0xe8,0xfe,0xff,0xff,0x48,0x81,0xec,0x18,0x02,0x00,0x00};
constexpr uint8_t TargetSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0xe8,0x22,0x1d,0x00,0x00};
constexpr uint8_t LookupSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20};
constexpr uint8_t RangeSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57};
constexpr uint8_t RelationSig[]={0x41,0xb1,0x01,0xe9,0x28,0x43,0x00,0x00};
constexpr uint8_t LineSig[]={0x40,0x53,0x57,0x41,0x55,0x48,0x83,0xec,0x50,0x45,0x8b,0xe8};
constexpr uint8_t StatsSig[]={0x48,0x89,0x5c,0x24,0x08,0x55,0x56,0x57,0x48,0x83,0xec,0x30};
// Check the hash layout used below, and the actual attack/flags/finish call
// sites. A matching common prologue alone is insufficient for these layouts.
constexpr uint8_t HashSig[]={0x41,0x8b,0xc0,0x83,0xe0,0x7f,0x48,0x05,0xc6,0x04,0x00,0x00,0x48,0x8d,0x04,0xc2,0xc3};
constexpr uint8_t AttackCallsSig[]={0xe8,0x9c,0xb6,0x01,0x00,0x83,0x4c,0x24,0x70,0x02};
using TargetFn=uintptr_t(*)(uintptr_t,uintptr_t);
using LookupFn=uintptr_t(*)(uintptr_t,int,uint32_t);
using RangeFn=int(*)(uintptr_t,uintptr_t,int,void*);
using FlagsFn=uint16_t(*)(uintptr_t,uintptr_t,uintptr_t,int,bool,int);
TargetFn originalTarget=nullptr;LookupFn originalLookup=nullptr;RangeFn originalRange=nullptr;FlagsFn originalFlags=nullptr;
const D2RL::PluginContext* context=nullptr;const D2RL::ThreadService* threads=nullptr;
std::atomic<bool> ready{false};bool targetIn=false,lookupIn=false,rangeIn=false,flagsIn=false;
struct Server {uintptr_t game=0;uint64_t session=0;uint32_t player=0;};
std::mutex serverMutex;Server server;
// skill: a melee-skill strike cast (strike_skills.h) is running for this contact:
// every native target read and the first hit check of the invocation belong to it.
struct Contact {uintptr_t game=0,attacker=0,defender=0;bool accepted=false,smite=false,resolved=false,skill=false;};
thread_local Contact contact;
std::atomic<unsigned> samples{0},contacts{0},accepted{0},rejected{0},noServer{0},intersections{0},stale{0},walls{0};
std::atomic<unsigned> applied{0},recovered{0},playerMissing{0},positionMismatch{0},nearby{0},bodyMissing{0};
std::atomic<unsigned> shieldSweeps{0},shieldRequests{0},shieldAccepted{0},shieldRejected{0};
std::atomic<unsigned> dualLanded[2]{},dualRefused{0},clawLanded[2]{};
// Attack speed: settings/cycle cross threads; the cadence and the strokes that
// already landed live on the game thread (Apply) only.
std::atomic<bool> attackSpeed{false},trace{false},attackRating{true};
std::atomic<uint64_t> cycleSession{0};std::atomic<uint32_t> cycleMs{0},offCycleMs{0};
std::atomic<unsigned> tooSoon{0},extraTargets{0},missed{0},buttonAttacks{0};
framevr::attackspeed::Cadence cadence; // Classic shares one native attack cycle across hands/buttons.
std::atomic<bool> swingRecovery{false};std::atomic<float> recoveryCurve{2},recoveryGrace{1};
std::atomic<float> cleaveStep{0},cleaveFloor{.25f};
// Unleashed trigger/button melee (ButtonHit): the latest trigger hit, on the game
// thread that owns the swing clocks (ObserveServer records it); swings are measured from it too.
struct TriggerLanded {uint64_t session=0,at=0;} triggerLanded;
std::atomic<DWORD> gameThread{0};
std::atomic<unsigned> triggerPenalised{0},triggerOtherThread{0};
thread_local int smiteDepth=0;   // inside the native Smite (the Smite hook)
// Unleashed on-hit gate, per resolved contact (developer builds report it).
enum OnHitReason {OnHitAllowed,OnHitUnknownCycle,OnHitRiposte,OnHitNotFull,OnHitCleave,OnHitFlurry,OnHitReasons};
std::array<std::atomic<unsigned>,OnHitReasons> onHitWhy{};
std::array<HandCombat,2> hands;
std::mutex queueMutex;std::array<Engine,3> engines;HitSet shieldHits;
using Request=breakables::Strike;
std::array<Request,24> requests;size_t count=0;
std::array<Sample,3> current;uint64_t inputSession=0;framevr::CallbackTicket callback;
using SmiteFn=int(*)(uintptr_t,uintptr_t,int,int);
SmiteFn originalSmite=nullptr;std::atomic<bool> smiteReady{false};
// Perfect parry -> the game's own GetHit event handler 445E60(game, event),
// reached natively through an event table with the monster at event+8. It
// refuses dying/dead monsters (34AB60 mode 0/12) and monsters whose MonStats2
// row (975B0) lacks a GetHit mode (+0xF0 & mask), then calls the server
// set-mode wrapper 491130(game, unit, 3). Returns 1 when applied. Identified
// by call-stack probing (every server GetHit returns to 445EF7).
constexpr uint64_t HitRecoveryRva=0x445e60;
constexpr uint8_t HitRecoverySig[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x5a,0x08,0x48,0x8b,0xf9,0x48};
using HitRecoveryFn=int(*)(uintptr_t game,const uintptr_t* event);
std::atomic<bool> parryReady{false};std::atomic<unsigned> parried{0},parryRefused{0},parryRepaired{0},deflected{0};
std::atomic<int> parrySound{236}; // item_platearmor: Archon Plate's equip sound (HD redirect)
std::atomic<int> blockSound{398};  // block_weapon_1; [gameplay] block_sound
// 34E5F0(unit, mode) is the unit animation-mode setter, the only writer of
// unit+0xC seen while probing. Hooked with the parry to time monster attack starts.
constexpr uint64_t SetModeRva=0x34e5f0;
constexpr uint8_t SetModeSig[]={0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x8b,0xfa,0x48,0x8b,0xd9,0x48,0x85,0xc9};
using SetModeFn=int(*)(uintptr_t,int);
SetModeFn originalSetMode=nullptr;bool setModeIn=false;
// Monster attacks as they begin (server units), for the parry: a swing while
// the attack is incoming. MonMode A1 4, A2 5, S1-S4 8-11.
struct AttackSeen {uint32_t id=0,cls=0;int mode=0;uint64_t at=0,rolled=0;};   // rolled: its to-hit roll (hit or miss)
std::mutex attackMutex;std::array<AttackSeen,64> attacks{};std::atomic<unsigned> attackStarts{0};
bool AttackMode(int mode){return mode==4||mode==5||(mode>=8&&mode<=11);}
// Parry downstroke: a monster's to-hit roll runs at its attack's hit frame, so
// roll - start is that monster type's wind-up for that attack mode (measured
// from its last attack, so auras, slows and Fanaticism follow along). A parry
// swing must start once [gameplay] parry_downstroke % of it has passed: the
// arms coming down at you, not the first frames of the raise (Cal, 2026-10-19).
constexpr uint32_t kWindupClasses=1024;
std::array<uint16_t,kWindupClasses*6> windupMs{};   // attackMutex; 0 = not measured yet
// [gameplay] parry_at_hit (default on): a downstroke swing parries at the hit
// frame - the blow lands on the shield, then the attacker staggers. Staggering
// at the swing itself fired 50-90 ms before the hit (class 5: swing 281 ms,
// hit 330-375 ms, log 2026-10-19) and cut the blow off mid-air. Off: the old early stagger.
std::atomic<bool> parryAtHit{true};
std::atomic<uint32_t> downstrokePct{90};   // 60, then 80, parried as the blow was still coming (2026-10-19 15:18)
int WindupSlot(uint32_t cls,int mode) {
    const int m=mode==4?0:mode==5?1:mode>=8&&mode<=11?mode-6:-1;
    return cls<kWindupClasses&&m>=0?int(cls*6+m):-1;
}
uint64_t OpensAt(const AttackSeen& a) {   // attackMutex held
    const int slot=WindupSlot(a.cls,a.mode);const uint32_t pct=downstrokePct.load(std::memory_order_relaxed);
    if(a.at&&!pct)return a.at;   // 0: the old rule, anywhere after the attack began
    if(!a.at||slot<0||!windupMs[slot])return 0;
    return a.at+uint64_t(windupMs[slot])*pct/100;
}
template<class T> bool Read(uintptr_t p,T& out) {
    return p>0x10000&&framevr::ReadMemory(&out,reinterpret_cast<const void*>(p),sizeof out);
}
// [gameplay] player_flinch (0, the default; Unleashed only, Classic always
// flinches natively; applied live): in Unleashed the local hero never enters
// the game's hit recovery (GetHit, player mode 4)
// or its block animation (mode 9) when struck. Both lock out casts, button
// skills and movement for their FHR/FBR frames: in VR a stun-lock the body
// cannot follow (Cal, 2026-10-22). Refused at the mode setter (SetMode below)
// for the hero's server and client units; the mode stays what it was.
// Knockback (mode 19) stays native and is only counted, but a flinch asked for
// while in knockback ends it in Neutral instead: the game ends every knockback
// by asking for GetHit, and a refused one left the hero in knockback, asking
// again at each animation end, locked until save and exit (Duriel's stunning
// Smite, YouTube report 2026-10-24). 1 = native flinching.
std::atomic<bool> noFlinch{true};
std::atomic<unsigned> flinchRefused[2]{},knockbacks{0},knockbackEnds{0};   // [0] GetHit, [1] Block
#ifdef D2RVR_DEVELOPER
// Developer builds: the game code that asked for a refused flinch, so the
// refusal can move up to the game's own hit-recovery decision. Each distinct
// call chain is logged once, at most 8 a session: every game frame's RVA (this
// build), the function it sits in and that function's first 16 bytes.
std::mutex flinchProbeMutex;std::array<uint64_t,8> flinchChains{};unsigned flinchChainCount=0;
void ProbeFlinch(uintptr_t unit,int mode,uintptr_t caller) {
    {std::lock_guard lock(flinchProbeMutex);if(flinchChainCount>=flinchChains.size())return;}
    const uintptr_t base=d2rsig::Addr(SetModeRva)-d2rsig::Rva(SetModeRva);
    const auto inGame=[&](uintptr_t pc){return base&&pc>=base&&pc-base<0x4000000;};
    void* frames[12]{};
    const unsigned n=RtlCaptureStackBackTrace(0,12,frames,nullptr);
    uint64_t key=uint64_t(mode);unsigned keyed=0;
    for(unsigned k=0;k<n&&keyed<4;++k)if(const auto pc=reinterpret_cast<uintptr_t>(frames[k]);inGame(pc)){key=key*1000003+(pc-base);++keyed;}
    {
        std::lock_guard lock(flinchProbeMutex);
        for(unsigned k=0;k<flinchChainCount;++k)if(flinchChains[k]==key)return;
        if(flinchChainCount>=flinchChains.size())return;
        flinchChains[flinchChainCount++]=key;
    }
    uint32_t prior=0;Read(unit+0xc,prior);uintptr_t back=0;Read(unit+0xd8,back);
    char line[1600];
    int len=snprintf(line,sizeof line,"Frame flinch probe: mode=%s prior_mode=%u unit_d8=%d caller=0x%llx frames:",
        mode==9?"block":"gethit",prior,int(back>0x10000),(unsigned long long)(inGame(caller)?caller-base:0));
    for(unsigned k=0;k<n&&len>0&&len<int(sizeof line)-120;++k) {
        const auto pc=reinterpret_cast<uintptr_t>(frames[k]);
        if(!inGame(pc))continue;
        len+=snprintf(line+len,sizeof line-len," 0x%llx",(unsigned long long)(pc-base));
        DWORD64 image=0;
        if(const auto fe=RtlLookupFunctionEntry(pc,&image,nullptr);fe&&image==base) {
            uint8_t sig[16]{};Read(base+fe->BeginAddress,sig);
            len+=snprintf(line+len,sizeof line-len,"(fn 0x%x:",unsigned(fe->BeginAddress));
            for(uint8_t b:sig)len+=snprintf(line+len,sizeof line-len,"%02x",b);
            len+=snprintf(line+len,sizeof line-len,")");
        }
    }
    framevr::logging::Write(line);
}
#endif
void ObserveServer(uintptr_t game,uintptr_t unit) {
    uint32_t id=0,header[3]{};uintptr_t back=0;
    if(!game||!combat::LocalPlayerId(id)||!Read(unit,header)||header[0]!=0||header[2]!=id||!Read(unit+0xd8,back)||back!=game)return;
    const auto session=gamestate::LocalPlayer();
    if(const DWORD self=GetCurrentThreadId();gameThread.load(std::memory_order_relaxed)!=self)gameThread=self;
    std::lock_guard lock(serverMutex);server={game,session,id};
}
uintptr_t Lookup(uintptr_t game,int type,uint32_t id) {
    const auto unit=originalLookup(game,type,id);
    if(type==0&&unit)ObserveServer(game,unit);
    return unit;
}
uintptr_t Target(uintptr_t game,uintptr_t unit) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(contact.game==game&&contact.attacker==unit&&
       (contact.smite||contact.skill||caller==d2rsig::Addr(0x42ffaa)||caller==d2rsig::Addr(0x43010a)))return contact.defender;
    const auto result=originalTarget(game,unit);
    ObserveServer(game,unit);if(result)ObserveServer(game,result);
    return result;
}
int Range(uintptr_t attacker,uintptr_t defender,int range,void* out) {
    // Only a geometrically confirmed VR contact replaces legacy grid reach.
    // Native line collision remains mandatory, preventing hits through walls.
    if(!contact.smite&&contact.attacker==attacker&&contact.defender==defender) {
        if(out) {const int zero=0;framevr::WriteMemory(out,&zero,sizeof zero);}
        return reinterpret_cast<int(*)(uintptr_t,uintptr_t,int)>(d2rsig::Addr(LineRva))(attacker,defender,0x804)==0;
    }
    return originalRange(attacker,defender,range,out);
}
uint16_t Flags(uintptr_t game,uintptr_t attacker,uintptr_t defender,int accuracy,bool force,int offset) {
    const auto result=originalFlags(game,attacker,defender,accuracy,force,offset);
    // The basic attack's own hit check; or, in a strike cast, the skill's first
    // hit check (its start function's pre-roll).
    if(contact.game==game&&contact.attacker==attacker&&contact.defender==defender&&
       (reinterpret_cast<uintptr_t>(_ReturnAddress())==d2rsig::Addr(0x430084)||(contact.skill&&!contact.resolved))){contact.resolved=true;contact.accepted=(result&1)!=0;}
    return result;
}
bool Position(uintptr_t unit,Vec& out) {
    uintptr_t path=0;uint16_t p[4]{};
    if(!Read(unit+0x38,path)||!Read(path,p))return false;
    out={2*(p[1]+p[0]/65536.f),0,2*(p[3]+p[2]/65536.f)};return true;
}
// Why a queued sweep was dropped as stale (diagnostics).
enum StaleReason {StaleInactive,StaleEpoch,StaleEquipment,StaleSkill,StaleAge,StaleMenu,StaleHands,StaleReasons};
std::array<std::atomic<unsigned>,StaleReasons> staleWhy{};std::atomic<unsigned> staleOldestMs{0};
bool Allowed(const Request& r) {
    std::lock_guard lock(queueMutex);
    const auto& now=current[Lane(r.sample)];
    const auto age=GetTickCount64()-r.at;
    int why=-1;
    if(!now.active||r.session!=inputSession||r.session!=gamestate::LocalPlayer())why=StaleInactive;
    else if(r.sample.epoch!=now.epoch)why=StaleEpoch;
    else if(r.sample.equipment!=now.equipment||r.sample.claws!=now.claws||r.sample.minWeaponSpeed!=now.minWeaponSpeed||r.sample.minShieldBashSpeed!=now.minShieldBashSpeed||r.sample.weapons.id[0]!=now.weapons.id[0]||r.sample.weapons.id[1]!=now.weapons.id[1])why=StaleEquipment;
    else if(r.sample.skillSlot!=now.skillSlot||r.sample.skillOwner!=now.skillOwner||r.sample.skillContext!=now.skillContext||r.sample.strikeSkill!=now.strikeSkill)why=StaleSkill;
    else if(age>150)why=StaleAge;
    else if(gamestate::MenuOpen())why=StaleMenu;
    else if(r.sample.equipment!=(gamestate::HandsKey()^(gamestate::WeaponSet()<<30)))why=StaleHands;
    if(why>=0){++staleWhy[why];if(why==StaleAge&&age>staleOldestMs.load())staleOldestMs=unsigned(age);}
    return why<0;
}

int Smite(uintptr_t game,uintptr_t player,int skill,int level) {
    uint32_t target=0,id=0,header[3]{};uintptr_t back=0;
    if(skill!=framevr::weaponskills::Smite||!combat::LocalPlayerId(id)||!Read(player,header)||
       header[0]!=0||header[2]!=id||!Read(player+0xd8,back)||back!=game)return originalSmite(game,player,skill,level);
    struct Depth {Depth(){++smiteDepth;}~Depth(){--smiteDepth;}} inSmite;   // trigger and shield bash alike
    const int physical=framevr::TakeShieldTarget(target);
    if(physical==0)return originalSmite(game,player,skill,level);
    if(physical<0){++shieldRejected;framevr::FinishShieldSkill(false);return 0;}
    const auto defender=originalLookup(game,1,target);uint32_t mode=0;
    if(!defender||!Read(defender+0xc,mode)||mode==0||mode==12||
       !reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(game,player,defender)||
       reinterpret_cast<int(*)(uintptr_t,uintptr_t,int)>(d2rsig::Addr(LineRva))(player,defender,0x804)!=0) {
        ++shieldRejected;framevr::FinishShieldSkill(false);return 0;
    }
    // Scope the exact physical target to THIS native Smite invocation. Its
    // original range, damage, stun, knockback and proc logic all still run.
    const auto saved=contact;contact={game,player,defender,false,true};
    const int result=originalSmite(game,player,skill,level);contact=saved;
    if(result)++shieldAccepted;else ++shieldRejected;
    framevr::FinishShieldSkill(result!=0);return result;
}

void InstallSmite() {
    static uint64_t retryAt=0;
    if(smiteReady.load()||GetTickCount64()<retryAt)return;
    retryAt=GetTickCount64()+5000;
    const auto unavailable=[](const char* reason,uintptr_t detail=0) {
        static const char* previous=nullptr;
        if(previous==reason)return;
        previous=reason;char line[240];snprintf(line,sizeof line,"Frame shield: contact casting unavailable (%s; detail=0x%llx); native trigger fallback retained",reason,(unsigned long long)detail);
        framevr::logging::Write(line);
    };
    using namespace bowvr::native;
    if(!d2rsig::Check(ThrowRva,ThrowSig,sizeof ThrowSig)){unavailable("throw signature");return;}
    const auto base=context->exeBase;
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 pe{};
    if(!Read(base,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>0x100000||
       !Read(base+dos.e_lfanew,pe)||pe.Signature!=IMAGE_NT_SIGNATURE||pe.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||
       pe.FileHeader.NumberOfSections>32){unavailable("game image headers");return;}
    const auto sections=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+pe.FileHeader.SizeOfOptionalHeader;
    uintptr_t candidate=0;
    for(unsigned i=0;i<pe.FileHeader.NumberOfSections;++i) {
        IMAGE_SECTION_HEADER section{};if(!Read(sections+i*sizeof section,section)){unavailable("section headers");return;}
        if(std::memcmp(section.Name,".rdata",6)&&std::memcmp(section.Name,".data",5))continue;
        const auto size=section.Misc.VirtualSize;
        if(size<151*sizeof(uintptr_t)||size>64*1024*1024||section.VirtualAddress>pe.OptionalHeader.SizeOfImage||
           size>pe.OptionalHeader.SizeOfImage-section.VirtualAddress)continue;
        std::vector<uintptr_t> entries(size/sizeof(uintptr_t));
        if(!framevr::ReadMemory(entries.data(),reinterpret_cast<const void*>(base+section.VirtualAddress),entries.size()*sizeof(uintptr_t)))continue;
        for(size_t at=0;at+151<=entries.size();++at) {
            const auto found=native::SmiteEntry(std::span(entries).subspan(at,151),d2rsig::Addr(AttackRva),d2rsig::Addr(ThrowRva));
            if(!found)continue;
            if(candidate&&candidate!=found){unavailable("ambiguous dispatch table");return;}
            candidate=found;
        }
    }
    if(candidate<=base||candidate-base>=pe.OptionalHeader.SizeOfImage){unavailable("dispatch table not found");return;}
    const auto& exception=pe.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if(!exception.VirtualAddress||!exception.Size||exception.Size>32*1024*1024||exception.Size%sizeof(native::FunctionRange)||
       exception.VirtualAddress>=pe.OptionalHeader.SizeOfImage||exception.Size>pe.OptionalHeader.SizeOfImage-exception.VirtualAddress) {
        unavailable("game function directory bounds");return;
    }
    std::vector<native::FunctionRange> functions(exception.Size/sizeof(native::FunctionRange));
    if(!framevr::ReadMemory(functions.data(),reinterpret_cast<const void*>(base+exception.VirtualAddress),exception.Size)) {
        unavailable("game function directory unreadable");return;
    }
    const auto function=native::FindFunction(functions,uint32_t(candidate-base),pe.OptionalHeader.SizeOfImage);
    if(!function.begin){unavailable("game function entry",candidate-base);return;}
    std::vector<uint8_t> code(function.end-function.begin);
    if(code.size()<16||!framevr::ReadMemory(code.data(),reinterpret_cast<const void*>(candidate),code.size())||
       !native::CallsTarget(code,candidate,d2rsig::Addr(TargetRva))){unavailable("decoded target call",candidate-base);return;}
    if(d2rsig::Hook(candidate-base,code.data(),16,reinterpret_cast<void*>(&Smite),reinterpret_cast<void**>(&originalSmite))) {
        smiteReady=true;framevr::logging::Write("Frame shield: native Smite identified by audited attack/throw dispatch table, game function directory and target call; contact casting ready");
    } else unavailable("native hook installation",candidate-base);
}
// Melee-skill contact adapter (strike_skills.h): a landed strike on a hand
// armed with a strike skill casts that skill natively on the struck creature,
// through the same pipeline as the hand spells (spells.cpp Cast), without an
// animation. In order, on the game thread, inside the stroke's contact scope:
//  - the packet handler's own use check 33F360 (D2Common SKILLS_GetUseState):
//    weapon type, mana, shapeshift form and skill delay, which the server start
//    function does not repeat;
//  - the hand's skill made the used skill (skill list +0x18) for the call;
//  - server start 43B3E0: mana/town/line-of-sight checks, srvstfunc (Maul's 56
//    pre-rolls the hit on Target(), here the struck creature), then the cost;
//  - server do dispatcher 43ACB0: srvdofunc (Maul's 120 resolves the hit
//    through 44B2B0, adds the stack), cooldown, srvmissile.
// The tables are checked where the start and dispatcher index them, and each
// supported skill's entries against the functions studied for it.
constexpr uint64_t UseStateRva=0x33f360;
constexpr uint8_t UseStateSig[]={0x40,0x56,0x57,0x48,0x83,0xec,0x28,0x48,0x8b,0xfa,0x48,0x8b,0xf1,0x48,0x85,0xd2,0x75,0x13};
constexpr uint64_t UseStateCallRva=0x4fdd95;   // the server's skill request: UseState(unit, [skill])
constexpr uint8_t UseStateCallSig[]={0x48,0x8b,0x16,0x48,0x8b,0xcb,0xe8,0xc0,0x15,0xe4,0xff};
constexpr uint64_t StartTableRefRva=0x43b558,StartFunctionsRva=0x238e710;   // movsx srvstfunc; lea r10, table
constexpr uint8_t StartTableRefSig[]={0x48,0x0f,0xbf,0x47,0x4c,0x66,0x83,0xf8,0x5a,0x0f,0x87,0x55,0xff,0xff,0xff,0x4c,0x8d,0x15,0xa2,0x31,0xf5,0x01};
constexpr uint64_t DoTableRefRva=0x43aef2;    // lea rcx, image; mov r10, [rcx+rax*8+table]
constexpr uint8_t DoTableRefSig[]={0x48,0x8d,0x0d,0x07,0x51,0xbc,0xff,0x4c,0x8b,0x94,0xc1,0x00,0xea,0x38,0x02};
// Entry bytes from the game image (2026-10-15), equal in build-frame\game-code.bin.
// startRva 0: the skill has no start function; its table entry must be empty.
constexpr size_t EntrySig=10;
constexpr uint8_t St56Sig[EntrySig]={0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57};
constexpr uint8_t St57Sig[EntrySig]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
constexpr uint8_t St58Sig[EntrySig]={0x40,0x55,0x53,0x57,0x41,0x54,0x41,0x56,0x41,0x57};
constexpr uint8_t Do2Sig[EntrySig]={0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20};
constexpr uint8_t Do120Sig[EntrySig]={0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54};
constexpr uint8_t Do121Sig[EntrySig]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41};
constexpr uint8_t Do122Sig[EntrySig]={0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x57};
constexpr uint8_t St37Sig[EntrySig]={0x48,0x89,0x5c,0x24,0x20,0x55,0x57,0x41,0x55,0x41};
constexpr uint8_t Do13Sig[EntrySig]={0x40,0x55,0x56,0x57,0x41,0x55,0x41,0x57,0x48,0x8d};
// The weapon skills' entries (#223), read live from the decrypted tables by
// the dev build (2026-10-23, D2R 3.3.94995, which keeps 3.3.93787's RVAs).
constexpr uint8_t St6Sig[EntrySig]={0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57};
constexpr uint8_t St7Sig[EntrySig]={0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57};
constexpr uint8_t St10Sig[EntrySig]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x48};
constexpr uint8_t St16Sig[EntrySig]={0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
constexpr uint8_t St29Sig[EntrySig]={0x40,0x55,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57};
constexpr uint8_t St32Sig[EntrySig]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
constexpr uint8_t St35Sig[EntrySig]={0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x57};
constexpr uint8_t St39Sig[EntrySig]={0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
constexpr uint8_t Do11Sig[EntrySig]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41};
constexpr uint8_t Do14Sig[EntrySig]={0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41};
constexpr uint8_t Do32Sig[EntrySig]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30};
constexpr uint8_t Do64Sig[EntrySig]={0x40,0x55,0x53,0x41,0x54,0x41,0x55,0x41,0x56,0x41};
constexpr uint8_t Do79Sig[EntrySig]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18};   // Conversion's, read the same way later that day
// Fury (multi-hit, strike_skills.h Flurry). Start 37 writes the used skill node:
// hit count +0x24, target type +0x2c, target id +0x30. Do 13 (one call per hit)
// reads the target from +0x2c/+0x30 (556965), lands the hit, then counts +0x24
// down (556B56) and, only while hits remain, picks the next creature and rewinds
// the hero's animation (4914E0) by the row's Param2 (+0x1BC, read at 556BF5).
// With no attack animation that rewind would re-time the hero's walk/neutral
// events, so each call is given a count of 1: one hit, and the jle skips the
// next-target search and the rewind. These three sites are checked for Fury
// and Zeal, the same functions.
constexpr uint64_t Do13TargetRva=0x556965,Do13CountRva=0x556b56,Do13PaceRva=0x556bf5;
constexpr uint8_t Do13TargetSig[]={0x41,0x8b,0x56,0x2c,0x48,0x8b,0xce,0x89,0x44,0x24,0x44,0x41,0x8b,0x46,0x30};   // mov edx,[r14+2Ch] .. mov eax,[r14+30h]
constexpr uint8_t Do13CountSig[]={0x41,0xff,0x4e,0x24,0x41,0x83,0x7e,0x24,0x00,0x0f,0x8e,0xc3,0x00,0x00,0x00};    // dec [r14+24h]; cmp; jle 556C28 (the exit)
constexpr uint8_t Do13PaceSig[]={0x8b,0x80,0xbc,0x01,0x00,0x00};                                                   // mov eax,[rax+1BCh]
constexpr uintptr_t NodeHitsOffset=0x24,NodeTargetTypeOffset=0x2c,NodeTargetIdOffset=0x30,RowParam2Offset=0x1bc;
struct StrikeNative {const char* name;int skill,start,action;uint64_t startRva,actionRva;const uint8_t* startSig;const uint8_t* actionSig;};
constexpr StrikeNative StrikeNatives[]={
    {"Maul",framevr::strikeskills::Maul,56,120,0x5779e0,0x576160,St56Sig,Do120Sig},
    {"Feral Rage",framevr::strikeskills::FeralRage,56,120,0x5779e0,0x576160,St56Sig,Do120Sig},
    {"Rabies",framevr::strikeskills::Rabies,57,121,0x577f00,0x576bb0,St57Sig,Do121Sig},
    {"Fire Claws",framevr::strikeskills::FireClaws,58,2,0x577c90,0x430240,St58Sig,Do2Sig},
    {"Hunger",framevr::strikeskills::Hunger,0,122,0,0x576630,nullptr,Do122Sig},
    {"Fury",framevr::strikeskills::Fury,37,13,0x5582a0,0x5568e0,St37Sig,Do13Sig},
    {"Zeal",framevr::strikeskills::Zeal,37,13,0x5582a0,0x5568e0,St37Sig,Do13Sig},
    {"Bash",framevr::strikeskills::Bash,32,2,0x5695d0,0x430240,St32Sig,Do2Sig},
    {"Stun",framevr::strikeskills::Stun,32,2,0x5695d0,0x430240,St32Sig,Do2Sig},
    {"Concentrate",framevr::strikeskills::Concentrate,32,2,0x5695d0,0x430240,St32Sig,Do2Sig},
    {"Berserk",framevr::strikeskills::Berserk,39,2,0x569670,0x430240,St39Sig,Do2Sig},
    {"Sacrifice",framevr::strikeskills::Sacrifice,29,64,0x565240,0x5645c0,St29Sig,Do64Sig},
    {"Vengeance",framevr::strikeskills::Vengeance,35,2,0x565520,0x430240,St35Sig,Do2Sig},
    {"Conversion",framevr::strikeskills::Conversion,32,79,0x5695d0,0x562990,St32Sig,Do79Sig},
    {"Power Strike",framevr::strikeskills::PowerStrike,6,2,0x558c70,0x430240,St6Sig,Do2Sig},
    {"Impale",framevr::strikeskills::Impale,7,2,0x558590,0x430240,St7Sig,Do2Sig},
    {"Charged Strike",framevr::strikeskills::ChargedStrike,6,11,0x558c70,0x5562b0,St6Sig,Do11Sig},
    {"Lightning Strike",framevr::strikeskills::LightningStrike,10,14,0x558930,0x557510,St10Sig,Do14Sig},
    {"Poison Dagger",framevr::strikeskills::PoisonDagger,16,32,0x5601a0,0x55cc50,St16Sig,Do32Sig},
};
std::atomic<bool> strikeReady{false};
std::array<bool,std::size(StrikeNatives)> strikeVerified{};
using framevr::strikeskills::Refusal;
std::array<std::atomic<unsigned>,size_t(Refusal::Count)> strikeRefused{};
std::atomic<unsigned> strikeArmed{0},strikeCast{0},strikeBasic{0},strikeStartRefused{0},strikeFallback{0};
std::atomic<int> strikeSound{0};
// Fury's or Zeal's flurry (game thread): the open cast whose hits later strokes deliver.
framevr::strikeskills::Flurry flurry;
std::atomic<unsigned> flurryCasts{0},flurryHits{0},flurryEarly{0},flurryForfeit{0},flurrySpent{0},flurryRefused{0},flurrySpread{0};
bool StrikeSkillVerified(int skill) {
    for(size_t i=0;i<std::size(StrikeNatives);++i)if(StrikeNatives[i].skill==skill)return strikeVerified[i];
    return false;
}
void InstallStrikes() {
    if(strikeReady.load())return;
    using namespace spellvr::native;
    if(!d2rsig::Check(UseStateRva,UseStateSig,sizeof UseStateSig)||!d2rsig::Check(UseStateCallRva,UseStateCallSig,sizeof UseStateCallSig)||
       !d2rsig::Check(StartRva,StartSig,sizeof StartSig)||!d2rsig::Check(DispatchRva,DispatchSig,sizeof DispatchSig)||
       !d2rsig::Check(SkillLevelRva,SkillLevelSig,sizeof SkillLevelSig)||!d2rsig::Check(UsedLayoutRva,UsedLayoutSig,sizeof UsedLayoutSig)||
       !d2rsig::Check(nativeui::abi::SkillRva,nativeui::abi::SkillSig,sizeof nativeui::abi::SkillSig)||
       !d2rsig::Check(StartTableRefRva,StartTableRefSig,sizeof StartTableRefSig)||!d2rsig::Check(DoTableRefRva,DoTableRefSig,sizeof DoTableRefSig)) {
        static bool said=false;
        if(!said){said=true;framevr::logging::Write("Frame melee skills: native skill start/do/use-check signatures differ; strikes stay basic attacks");}
        return;
    }
    unsigned verified=0;
    for(size_t i=0;i<std::size(StrikeNatives);++i) {
        const auto& n=StrikeNatives[i];uintptr_t start=0,action=0;
        const bool startOk=Read(d2rsig::Addr(StartFunctionsRva+uint64_t(n.start)*8),start)&&
            (n.startRva?start==d2rsig::Addr(n.startRva)&&d2rsig::Check(n.startRva,n.startSig,EntrySig):start==0);
        strikeVerified[i]=startOk&&Read(d2rsig::Addr(DoFunctionsRva+uint64_t(n.action)*8),action)&&action==d2rsig::Addr(n.actionRva)&&
            d2rsig::Check(n.actionRva,n.actionSig,EntrySig);
        if(n.action==13)strikeVerified[i]=strikeVerified[i]&&d2rsig::Check(Do13TargetRva,Do13TargetSig,sizeof Do13TargetSig)&&
            d2rsig::Check(Do13CountRva,Do13CountSig,sizeof Do13CountSig)&&d2rsig::Check(Do13PaceRva,Do13PaceSig,sizeof Do13PaceSig);
        verified+=strikeVerified[i];
    }
    strikeReady=true;
    char line[640];int used=snprintf(line,sizeof line,"Frame melee skills: native use check, server start and do dispatch verified; %u of %zu strike skills ready (",
        verified,std::size(StrikeNatives));
    for(size_t i=0;i<std::size(StrikeNatives)&&used>0&&size_t(used)<sizeof line;++i)
        used+=snprintf(line+used,sizeof line-size_t(used),"%s%s %d",i?", ":"",StrikeNatives[i].name,int(strikeVerified[i]));
    if(used>0&&size_t(used)+1<sizeof line){line[used]=')';line[used+1]=0;}
    framevr::logging::Write(line);
}
// The skill a hand's strike is about to cast, made the player's used skill
// (skill list +0x18) for the native checks, cost and do functions; restored.
struct UsedSkill {
    uintptr_t address=0,previous=0;bool changed=false;
    UsedSkill(uintptr_t player,uintptr_t skill) {
        uintptr_t list=0;
        if(Read(player+0x100,list)&&Read(list+0x18,previous)) {
            address=list+0x18;changed=framevr::WriteMemory(reinterpret_cast<void*>(address),&skill,sizeof skill);
        }
    }
    ~UsedSkill(){if(changed)framevr::WriteMemory(reinterpret_cast<void*>(address),&previous,sizeof previous);}
};
void __cdecl StrikeSound(const D2RL::PluginContext*,void*) noexcept {
    if(const int sound=strikeSound.exchange(0);sound>0)bowvr::PlayHeroSound(sound);
}
// One sounds.txt row at the hero, on the UI thread (the latest wins if two come in one tick).
void QueueHeroSound(int sound) {
    if(sound>0&&context&&threads&&threads->runOnUiThread){strikeSound=sound;threads->runOnUiThread(context,&StrikeSound,nullptr);}
}
std::atomic<unsigned> formVoices{0};
uint32_t voiceSeed=0x9e3779b9u;   // game thread: the growl chance roll
int VoiceRoll(){voiceSeed=voiceSeed*1664525u+1013904223u+uint32_t(GetTickCount64());return int((voiceSeed>>16)%100);}
enum class StrikeOutcome {Basic,Cast,StartRefused,FollowUp};
// Game thread, inside the stroke's contact scope (contact.defender = the struck
// creature). Basic: refused before the start (the caller lands a basic attack).
// A multi-hit skill's cast lands its first hit only; hits/pace receive its whole
// hit count and its Param2 for the flurry.
StrikeOutcome CastStrike(uintptr_t game,uintptr_t player,uint64_t session,int skill,int owner,int* hits=nullptr,int* pace=nullptr) {
    if(!strikeReady.load()||!StrikeSkillVerified(skill))return StrikeOutcome::Basic;
    using namespace spellvr::native;
    const auto node=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(nativeui::abi::SkillRva))(player,skill,owner);
    uintptr_t row=0;int16_t id=-1,start=-1,action=-1,sound=0;
    if(!node||!Read(node,row)||!Read(row,id)||id!=skill||!Read(row+0x4c,start)||!Read(row+0x4e,action)||
       !framevr::strikeskills::Matches(skill,start,action)){++strikeRefused[size_t(Refusal::Other)];return StrikeOutcome::Basic;}
    const auto refusal=framevr::strikeskills::FromUseState(reinterpret_cast<int(*)(uintptr_t,uintptr_t)>(d2rsig::Addr(UseStateRva))(player,node));
    if(refusal!=Refusal::None) {
        ++strikeRefused[size_t(refusal)];
        if(uint32_t id=0;framevr::strikeskills::Voice(refusal)&&Read(player+8,id))spellvr::WarnMana(session,id);
        return StrikeOutcome::Basic;
    }
    const int level=reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int)>(d2rsig::Addr(SkillLevelRva))(player,node,1,0);
    if(level<=0){++strikeRefused[size_t(Refusal::Other)];return StrikeOutcome::Basic;}
    UsedSkill used(player,node);
    if(!used.changed){++strikeRefused[size_t(Refusal::Other)];return StrikeOutcome::Basic;}
    contact.skill=true;
    const bool started=reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int,int)>(d2rsig::Addr(StartRva))(game,player,skill,level,0)!=0;
    int hitCount=0;
    if(started&&framevr::strikeskills::MultiHit(skill)) {
        // Start 37 set the cast's hit count; this call lands one of them (see Do13CountSig).
        const int one=1;
        if(!Read(node+NodeHitsOffset,hitCount)||!framevr::WriteMemory(reinterpret_cast<void*>(node+NodeHitsOffset),&one,sizeof one))hitCount=0;
        if(pace&&!Read(row+RowParam2Offset,*pace))*pace=0;
    }
    if(started&&(!framevr::strikeskills::MultiHit(skill)||hitCount>0))
        reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int,int,int,int)>(d2rsig::Addr(DispatchRva))(game,player,skill,level,1,0,0);
    contact.skill=false;
    if(hits)*hits=hitCount;
    // A multi-hit start that set no hits found no target (natively the cast lands nothing).
    if(!started||(framevr::strikeskills::MultiHit(skill)&&hitCount<=0)){++strikeStartRefused;return StrikeOutcome::StartRefused;}
    ++strikeCast;
    // No client cast event runs: play the skill's own cast sound (stsound) at the hero, as hand spells do.
    if(Read(row+0x152,sound)&&sound>0)QueueHeroSound(sound);
    return StrikeOutcome::Cast;
}
// The next hit of the open flurry (strike_skills.h Flurry) on the creature this
// stroke struck: game thread, inside the stroke's contact scope. Only the do
// function runs, as natively for a cast's later hits: no start, no use check,
// no mana. The used skill node is aimed at the creature (type 1, its id) and
// given a count of 1, so do 13 lands this one hit and never rewinds. False: not run.
bool FollowUpStrike(uintptr_t game,uintptr_t player,int skill,int owner,uint32_t target) {
    if(!strikeReady.load()||!StrikeSkillVerified(skill)||!framevr::strikeskills::MultiHit(skill))return false;
    using namespace spellvr::native;
    const auto node=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(nativeui::abi::SkillRva))(player,skill,owner);
    uintptr_t row=0;int16_t id=-1,start=-1,action=-1;
    if(!node||!Read(node,row)||!Read(row,id)||id!=skill||!Read(row+0x4c,start)||!Read(row+0x4e,action)||
       !framevr::strikeskills::Matches(skill,start,action))return false;
    const int level=reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int)>(d2rsig::Addr(SkillLevelRva))(player,node,1,0);
    if(level<=0)return false;
    UsedSkill used(player,node);
    if(!used.changed)return false;
    const int one=1,monster=1;
    if(!framevr::WriteMemory(reinterpret_cast<void*>(node+NodeTargetTypeOffset),&monster,sizeof monster)||
       !framevr::WriteMemory(reinterpret_cast<void*>(node+NodeTargetIdOffset),&target,sizeof target)||
       !framevr::WriteMemory(reinterpret_cast<void*>(node+NodeHitsOffset),&one,sizeof one))return false;
    contact.skill=true;
    reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int,int,int,int)>(d2rsig::Addr(DispatchRva))(game,player,skill,level,1,0,0);
    contact.skill=false;
    return true;
}
// Body volumes are deliberately approximate: native ground footprint, with
// height inferred from MonStats2's legacy artwork height. Units are world
// metres; a typical 80px monster is 8 game world units tall. No copied assets.
bool BodyFor(uintptr_t unit,uint32_t classId,float upm,float floor,Body& body) {
    uint8_t bank=0,fields[16]{};Vec p{};
    if(!Position(unit,p)||!Read(unit+0x1bd,bank)||bank>3||classId>4095)return false;
    const auto row=reinterpret_cast<uintptr_t(*)(uint8_t,int)>(d2rsig::Addr(StatsRva))(bank,int(classId));
    if(!Read(row,fields)||fields[8]<1||fields[8]>8||fields[9]<1||fields[9]>8)return false;
    const float radius=std::max(.12f,float(std::max(fields[8],fields[9]))*.7f/upm);
    const float height=std::clamp((fields[13]?fields[13]:72)*.10f/upm,.4f,4.f);
    body.radius=std::min(radius,height*.45f)+.05f;
    body.bottom=framevr::coordinates::RenderGround(p*(1/upm),floor)+Vec{0,body.radius,0};body.top=body.bottom;
    body.top.y=floor+height-body.radius;return true;
}
// [debug] melee_trace=1: one line per native attack and per refused stroke,
// on the game thread, with the time since the previous landed hit. Landed
// hits should never be closer together than cycle_ms.
uint64_t tracedStroke[2][2]{};   // last stroke logged per refusal kind (game thread)
void TraceRefusal(int kind,const Request& r,uint32_t target) {
    if(!trace.load()||tracedStroke[r.sample.handIndex][kind]==r.sweep.stroke)return;
    tracedStroke[r.sample.handIndex][kind]=r.sweep.stroke;
    const auto since=cadence.Since(r.session,r.at);
    char line[240];snprintf(line,sizeof line,"Frame melee trace: hand=%s stroke=%llu target=%u REFUSED (%s) since_last_landed=%lld ms remaining=%llu ms cycle=%u ms",
        r.sample.handIndex?"left":"right",(unsigned long long)r.sweep.stroke,target,kind?"one creature per stroke":"too soon",since==UINT64_MAX?-1LL:(long long)since,
        (unsigned long long)cadence.Remaining(r.session,r.at),cycleMs.load());
    framevr::logging::Write(line);
}
void TraceHit(const Request& r,uint32_t target,const char* outcome,uint64_t since,uint32_t cycle,bool onHit,const char* skill="basic") {
    if(!trace.load())return;
    char line[340];snprintf(line,sizeof line,"Frame melee trace: hand=%s stroke=%llu target=%u %s since_last_landed=%lld ms cycle=%u ms %s=%.1f m/s onhit=%d would_hit=%d strike=%d %s",
        r.sample.handIndex?"left":"right",(unsigned long long)r.sweep.stroke,target,outcome,since==UINT64_MAX?-1LL:(long long)since,cycle,r.sweep.thrust?"thrust":"swing",r.sweep.speed,int(onHit),
        combat::LastSwingWouldHit(),r.sample.strikeSkill,skill);
    framevr::logging::Write(line);
}
// Every weapon contact with a hostile creature feels struck: one tap per
// stroke and creature, whether native then lands, misses or refuses it.
void Feel(const Request& r,uint32_t target) {
    if(hands[r.sample.handIndex].felt.Insert(r.session,r.sweep.stroke,target))framevr::MeleeImpact(r.sample.handIndex);
}
void __cdecl Apply(const D2RL::PluginContext*,void* token) noexcept {
    std::array<Request,24> batch;size_t n=0;
    {std::lock_guard lock(queueMutex);
     if(!callback.Claim(reinterpret_cast<uintptr_t>(token)))return;
     n=count;std::copy_n(requests.begin(),n,batch.begin());count=0;}
    if(!n||!ready.load())return;
    ++applied;gameThread=GetCurrentThreadId();   // the swing clocks' thread (ButtonHit), also seen by ObserveServer
    // Trigger/button melee shares the attack cycle: its latest native melee
    // hit check starts the cycle, so alternating it with swings is no faster.
    if(uint64_t s=0,at=0;attackSpeed.load()&&combat::TakeButtonMelee(s,at)&&s==batch[0].session) {
        cadence.Landed(s,at,cycleSession.load()==s?cycleMs.load():0);++buttonAttacks;
    }
    Server local;{std::lock_guard lock(serverMutex);local=server;}
    uint32_t id=0;
    if(!combat::LocalPlayerId(id)||!local.game||local.session!=batch[0].session||local.player!=id) {++noServer;return;}
    const auto player=originalLookup(local.game,0,id);
    uint32_t playerMode=0;Vec playerPos{};
    if(!player||!Read(player+0xc,playerMode)||playerMode==0||playerMode==17||playerMode==5||playerMode==6||!Position(player,playerPos)){++playerMissing;return;}
    // A bounded server-side hash walk, only during energetic swings. Native
    // units stay on the owning thread; no raw target pointer crosses threads.
    uintptr_t buckets[128]{};
    if(!Read(local.game+0x4c6*8,buckets))return;
    unsigned visited=0;
    for(uintptr_t unit:buckets) {
        for(unsigned chain=0;unit&&chain<256&&visited++<4096;++chain) {
            uintptr_t next=0;uint32_t header[4]{};
            if(!Read(unit+0x158,next)||!Read(unit,header))break;
            if(header[0]==1&&header[3]!=0&&header[3]!=12) {
                Vec pos{};
                if(Position(unit,pos)&&Length(Vec{pos.x-playerPos.x,0,pos.z-playerPos.z})<24) {
                    ++nearby;
                    for(size_t j=0;j<n;++j) {
                        const auto& r=batch[j];Body body{};
                        if(!Allowed(r)){++stale;continue;}
                        // Native paths are flat grid coordinates; the rendered
                        // pose includes terrain-height shear on X/Z. Compare in
                        // the saved pose's grid plane, retaining the warp guard.
                        if(!framevr::coordinates::MatchesPlayer(playerPos,r.sample.player,r.sample.unitsPerMetre)){
                            ++positionMismatch;
                            static ULONGLONG reportAt=0;const auto now=GetTickCount64();
                            if(now>=reportAt){
                                const auto grid=framevr::coordinates::GridPlane(r.sample.player)*r.sample.unitsPerMetre;
                                char line[360];snprintf(line,sizeof line,
                                    "Frame melee coordinates: server_grid=%.3f,%.3f render_player=%.3f,%.3f,%.3f render_grid=%.3f,%.3f units_per_m=%.4f (world units; warp guard retained)",
                                    playerPos.x,playerPos.z,r.sample.player.x*r.sample.unitsPerMetre,
                                    r.sample.player.y*r.sample.unitsPerMetre,r.sample.player.z*r.sample.unitsPerMetre,grid.x,grid.z,r.sample.unitsPerMetre);
                                framevr::logging::Write(line);reportAt=now+3000;
                            }
                            continue;
                        }
                        if(!BodyFor(unit,header[1],r.sample.unitsPerMetre,r.sample.player.y,body)){++bodyMissing;continue;}
                        if(!Touches(r.sweep.before,r.sweep.after,body))continue;
                        ++intersections;
                        if(!reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(local.game,player,unit))continue;
                        if(reinterpret_cast<int(*)(uintptr_t,uintptr_t,int)>(d2rsig::Addr(LineRva))(player,unit,0x804)!=0){++walls;continue;}
                        if(r.sample.shield) {
                            if(!smiteReady.load()||shieldHits.Contains(r.session,r.sweep.stroke,UINT32_MAX))continue;
                            if(framevr::RequestShieldSkill(r.sample,header[2],pos*.5f)) {
                                shieldHits.Insert(r.session,r.sweep.stroke,UINT32_MAX);++contacts;++shieldRequests;
                            }
                            break;
                        }
                        const int hand=r.sample.handIndex;
                        auto& state=hands[hand];
                        auto& hits=state.hits;auto& landed=state.landed;
                        // A shapeshift form's two claws: each its own strokes, hits and cleave, one
                        // shared recovery (Cal, 2026-10-21) and the native form attack with the
                        // druid's one weapon (no weapon selection); the cycle is the form's.
                        const bool claws=r.sample.claws;
                        auto& recoveryState=claws?hands[0].recovery:state.recovery;
                        auto& recoveryClock=attackSpeed.load()?cadence:recoveryState;
                        const bool dual=!claws&&r.sample.weapons.id[0]&&r.sample.weapons.id[1];
                        const uintptr_t weapon=dual?meleeweapon::Select(player,hand,r.sample.weapons):0;
                        const int speedHand=dual?r.sample.weapons.BodyHand(hand):0;
                        if((dual&&(!weapon||speedHand<0))||(hand==1&&!dual&&!claws)){++dualRefused;continue;}
                        uint32_t selectedId=0;
                        if(dual&&(!Read(weapon+8,selectedId)||selectedId!=r.sample.weapons.displayed[hand])){++dualRefused;continue;}
                        Feel(r,header[2]);
                        if(hits.Contains(r.session,r.sweep.stroke,header[2]))continue;
                        // Fury's or Zeal's flurry (strike_skills.h Flurry): a landed stroke of a claw or weapon carrying
                        // the open cast's skill, in its form, may be its next hit, timed from the
                        // flurry's last hit: Unleashed any stroke but one swipe's follow-through;
                        // Classic at the skill's own pace (sooner than the shared cadence would allow).
                        using Feed=framevr::strikeskills::Flurry::Feed;
                        const uint32_t handCycle=cycleSession.load()==r.session?(speedHand?offCycleMs.load():cycleMs.load()):0;
                        const int shape=claws?interactionstate::ShapeForm(r.session,GetTickCount64()):0;
                        if(const int lapsed=flurry.Lapse(r.session,r.at,handCycle,shape))flurryForfeit+=unsigned(lapsed);
                        const uint64_t strokeKey=framevr::strikeskills::Flurry::StrokeKey(hand,r.sweep.stroke);
                        const auto feed=r.sample.strikeSkill>=0?flurry.Feeds(r.session,r.sample.strikeSkill,shape,r.at,handCycle,attackSpeed.load(),strokeKey):Feed::None;
                        const bool spread=flurry.SameStroke(strokeKey);   // the stroke already landed a Fury hit (rev6)
                        // Attack speed: a basic attack strikes one creature, and
                        // no sooner than its native attack cycle (IAS, WSM, class)
                        // after the last attack. A rolled miss spends the cycle
                        // as a native swing would; a geometric whiff never does.
                        uint32_t cycle=0;
                        if(attackSpeed.load()) {
                            if(landed.Contains(r.session,r.sweep.stroke,0)){
                                ++extraTargets;TraceRefusal(1,r,header[2]);if(dual||claws)continue;break;
                            }
                            if(feed!=Feed::Hit&&!cadence.Ready(r.session,r.at)){
                                ++tooSoon;TraceRefusal(0,r,header[2]);if(dual||claws)continue;break;
                            }
                            cycle=handCycle;
                        }
                        // Unleashed: measured from the latest trigger/button melee hit too (ButtonHit).
                        const auto since=attackSpeed.load()?recoveryClock.Since(r.session,r.at):
                            combat::SwingSince(recoveryClock.Since(r.session,r.at),triggerLanded.session==r.session?triggerLanded.at:0,r.at);
                        // Unleashed: the stroke's first landed contact measures how
                        // recovered it is; every creature that stroke reaches shares it.
                        // Kept with swing_recovery_damage off too: the on-hit gate reads it.
                        float recovery=1;bool strokeStart=false;
                        if(!attackSpeed.load()) {
                            if(state.strokeSession==r.session&&state.strokeId==r.sweep.stroke)recovery=state.strokeRecovery;
                            else {
                                cycle=handCycle;
                                if(swingRecovery.load(std::memory_order_relaxed))
                                    recovery=combat::RecoveryFactor(since,cycle,recoveryCurve.load(std::memory_order_relaxed),
                                                                    recoveryGrace.load(std::memory_order_relaxed));
                                strokeStart=true;
                            }
                        }
                        // A flurry hit: a stroke's first landed contact, or (Unleashed, rev6) the next creature of
                        // a later stroke that already landed one (Flurry: up to two per stroke; Classic lands one
                        // creature per stroke). It lands the skill's own damage and on-hit effects; other creatures
                        // that stroke reaches keep the stroke's true recovery, as cleave follow-ups.
                        const bool followUp=feed==Feed::Hit&&(attackSpeed.load()||strokeStart||spread);
                        const bool early=feed==Feed::TooSoon&&(attackSpeed.load()||strokeStart);   // an early stroke, not a cleave contact
                        if(!hits.Insert(r.session,r.sweep.stroke,header[2]))continue;
                        ++contacts;
                        const auto saved=contact;contact={local.game,player,unit,false};
                        // Native basic attack owns its nontrivial damage object,
                        // proc events, criticals, resistances, leech, XP and item
                        // durability. TLS redirects only THIS invocation's target.
                        const bool rolls=combat::NativeRolls(attackRating.load());
                        const uint32_t swing=combat::PhysicalSwing(header[2]);   // its damage may be scaled (riposte, attack rating, recovery)
                        // Unleashed: each further creature this stroke lands on takes less.
                        float cleave=1;bool cleaveTracked=false;
                        if(const float step=cleaveStep.load(std::memory_order_relaxed);step>0&&!attackSpeed.load()) {
                            if(state.cleaveSession!=r.session||state.cleaveStroke!=r.sweep.stroke){state.cleaveSession=r.session;state.cleaveStroke=r.sweep.stroke;state.cleaveLanded=0;}
                            cleave=combat::CleaveFactor(state.cleaveLanded,step,cleaveFloor.load(std::memory_order_relaxed));cleaveTracked=true;
                        }
                        const float swingRecoverySaved=combat::SwingRecovery(followUp?1.f:recovery*cleave);
                        // Unleashed on-hit gate (Cal, 2026-10-21): on-hit effects only on a full-cycle
                        // stroke's first landed contact; a riposte has earned them. Classic: native.
                        // A flurry hit is a whole native hit of the cast: it has them.
                        auto onHit=combat::OnHit::Allowed;bool riposte=false;
                        if(!attackSpeed.load()&&!followUp) {
                            onHit=combat::OnHitGate(strokeStart,since,cycle);
                            riposte=onHit!=combat::OnHit::Allowed&&combat::RiposteDue(header[2]);
                        }
                        const bool effects=onHit==combat::OnHit::Allowed||riposte;
                        const auto onHitSaved=combat::GateOnHit(effects?combat::OnHitScope{}:combat::OnHitScope{player,unit});
                        // Melee-skill contact adapter (Cal, 2026-10-22): a hand armed with a strike
                        // skill casts it on a full-cycle stroke's first landed contact (Unleashed) or on
                        // every paced contact (Classic); everything else, and a refused cast, stays the
                        // basic attack. The cast keeps this contact's scope, scaling and on-hit gate.
                        // A multi-hit cast (Fury, Zeal) lands its first hit and opens the flurry; while it is
                        // open, the hand's paced strokes land its next hits and early ones stay basic.
                        const int strikeSkill=r.sample.strikeSkill;
                        auto strike=StrikeOutcome::Basic;int flurryHit=0;const uint64_t sinceFlurryHit=flurry.SinceHit(r.at);
                        {
                            meleeweapon::Scope selected(player,weapon);
                            if(strikeSkill>=0) {
                                ++strikeArmed;
                                if(followUp) {
                                    if(FollowUpStrike(local.game,player,strikeSkill,r.sample.skillOwner,header[2])) {
                                        strike=StrikeOutcome::FollowUp;flurryHit=flurry.Hit(r.at,strokeKey);++flurryHits;if(spread)++flurrySpread;
                                        if(!flurry.Left())++flurrySpent;
                                    } else {flurry.End();++flurryRefused;}
                                } else if(feed!=Feed::TooSoon&&framevr::strikeskills::Casts(strikeSkill,attackSpeed.load(),onHit==combat::OnHit::Allowed)) {
                                    int castHits=0,pace=0;
                                    strike=CastStrike(local.game,player,r.session,strikeSkill,r.sample.skillOwner,&castHits,&pace);
                                    if(strike==StrikeOutcome::Basic)++strikeFallback;
                                    if(strike==StrikeOutcome::Cast&&framevr::strikeskills::MultiHit(strikeSkill)) {
                                        flurry.Begin(r.session,strikeSkill,shape,castHits,pace,r.at,strokeKey);flurryHit=1;++flurryCasts;
                                    } else if(strike!=StrikeOutcome::Basic)flurry.End();   // another skill's cast ends an open flurry
                                } else {++strikeBasic;if(early)++flurryEarly;}
                            }
                            if(strike==StrikeOutcome::Basic)
                                reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int)>(d2rsig::Addr(AttackRva))(local.game,player,0,1);
                        }
                        // A cast that ran spends the stroke like a resolved swing, even if its
                        // hit check was not seen (the clocks must move on).
                        if(strike!=StrikeOutcome::Basic&&!contact.resolved){
                            contact.resolved=true;contact.accepted=strike==StrikeOutcome::Cast||strike==StrikeOutcome::FollowUp;
                        }
                        // A form's attack voice (growl, or Rabies/Hunger's bite) on each stroke that
                        // natively is one attack (strike_skills.h FormVoice); the animation that plays it is skipped.
                        // A flurry's later hits are the same native attack as its cast: silent.
                        if(claws&&contact.resolved&&strike!=StrikeOutcome::FollowUp) {
                            const int voice=framevr::strikeskills::FormVoice(shape,attackSpeed.load(),
                                onHit==combat::OnHit::Allowed,strike==StrikeOutcome::Cast?strikeSkill:-1,VoiceRoll());
                            if(voice){QueueHeroSound(voice);++formVoices;}
                        }
                        combat::GateOnHit(onHitSaved);
                        if(contact.resolved&&!attackSpeed.load())
                            ++onHitWhy[strike==StrikeOutcome::FollowUp?OnHitFlurry:riposte?OnHitRiposte:onHit==combat::OnHit::CleaveFollowUp?OnHitCleave:
                                       onHit==combat::OnHit::NotFullCycle?OnHitNotFull:cycle?OnHitAllowed:OnHitUnknownCycle];
                        combat::SwingRecovery(swingRecoverySaved);
                        combat::PhysicalSwing(swing);
                        combat::NativeRolls(rolls);
                        if(contact.accepted)++accepted;else if(contact.resolved)++missed;else ++rejected;
                        if(dual&&contact.accepted)++dualLanded[hand];
                        if(claws&&contact.accepted)++clawLanded[hand];
                        if(contact.resolved&&attackSpeed.load()){cadence.Landed(r.session,r.at,cycle);landed.Insert(r.session,r.sweep.stroke,0);}
                        if(contact.resolved&&strokeStart){recoveryState.Landed(r.session,r.at,cycle);state.strokeSession=r.session;state.strokeId=r.sweep.stroke;state.strokeRecovery=recovery;}
                        if(contact.resolved&&cleaveTracked)++state.cleaveLanded;
                        if(trace.load()) {
                            // A flurry's hits: "cast 1/5", "flurry 3/5 after 352 ms" (since its previous hit);
                            // an early stroke inside it: "basic_strike flurry_early after 40 ms".
                            char tag[64];
                            if(strike==StrikeOutcome::FollowUp&&spread)snprintf(tag,sizeof tag,"flurry %d/%d spread",flurryHit,flurry.Total());
                            else if(strike==StrikeOutcome::FollowUp)snprintf(tag,sizeof tag,"flurry %d/%d after %llu ms",flurryHit,flurry.Total(),(unsigned long long)sinceFlurryHit);
                            else if(flurryHit)snprintf(tag,sizeof tag,"cast %d/%d",flurryHit,flurry.Total());
                            else if(strikeSkill>=0&&early)snprintf(tag,sizeof tag,"basic_strike flurry_early after %llu ms",(unsigned long long)sinceFlurryHit);
                            else snprintf(tag,sizeof tag,"%s",strike==StrikeOutcome::Cast?"cast":strike==StrikeOutcome::StartRefused?"cast_start_refused":
                                          strikeSkill<0?"basic":"basic_strike");
                            TraceHit(r,header[2],contact.accepted?"LANDED":contact.resolved?"MISSED (roll)":"native rejected",since,cycle,effects,tag);
                        }
                        contact=saved;
                        if(!dual&&!claws)break; // Both blades (or claws) may strike this creature in the same batch.
                    }
                }
            }
            unit=next;
        }
    }
    breakables::Apply(local.game,player,{batch.data(),n},&Allowed);
}
}
void Init(const D2RL::PluginContext* ctx) {
    context=ctx;
    if(ctx->QueryService(&threads)!=D2RL::ServiceQueryResult::Success)threads=nullptr;
}
void QueueAttackParry(uint32_t monster);
int SetMode(uintptr_t unit,int mode) {
    // The local hero's flinch (player modes GetHit 4, Block 9; Knockback 19 counted).
    if(mode==4||mode==9||mode==19) {
        uint32_t header[3]{},id=0;
        if(Read(unit,header)&&header[0]==0&&combat::LocalPlayerId(id)&&header[2]==id) {
            if(mode==19)++knockbacks;
            else if(noFlinch.load(std::memory_order_relaxed)) {
                // Leaving knockback: Neutral, as every other animation ends.
                if(uint32_t prior=0;Read(unit+0xc,prior)&&prior==19){++knockbackEnds;return originalSetMode(unit,1);}
                ++flinchRefused[mode==9];
#ifdef D2RVR_DEVELOPER
                ProbeFlinch(unit,mode,reinterpret_cast<uintptr_t>(_ReturnAddress()));
#endif
                return 0;   // refused: the hero keeps the mode it had
            }
        }
    }
    if(AttackMode(mode)&&parryReady.load(std::memory_order_relaxed)) {
        uint32_t header[3]{};uintptr_t back=0;
        if(Read(unit,header)&&header[0]==1&&Read(unit+0xd8,back)&&back>0x10000) { // a server monster starting an attack
            const auto now=GetTickCount64();
            {
            std::lock_guard lock(attackMutex);
            AttackSeen* slot=&attacks[0];
            for(auto& a:attacks){if(a.id==header[2]){slot=&a;break;}if(a.at<slot->at)slot=&a;}
            slot->id=header[2];slot->cls=header[1];slot->mode=mode;slot->at=now;slot->rolled=0;++attackStarts;
            }
            // A parry swing already made: stagger it as its attack begins.
            if(combat::ParryArmed())QueueAttackParry(header[2]);
        }
    }
    return originalSetMode(unit,mode);
}
// UI thread: the game's own mixer at the client hero (bow.cpp's verified hook).
void __cdecl ParrySound(const D2RL::PluginContext*,void*) noexcept {bowvr::PlayHeroSound(parrySound.load());}
// One sound for every block, so the player learns it: [gameplay] block_sound.
void __cdecl BlockSound(const D2RL::PluginContext*,void*) noexcept {bowvr::PlayHeroSound(blockSound.load());}
void QueueParrySound() {
    if(parrySound.load()>0&&context&&threads&&threads->runOnUiThread)threads->runOnUiThread(context,&ParrySound,nullptr);
}
void __cdecl Parry(const D2RL::PluginContext*,void* token) noexcept {
    const auto packed=reinterpret_cast<uintptr_t>(token);
    const auto monster=uint32_t(packed);
    // Free parry: the parried hit has been processed by now; undo any shield
    // wear it caused (durability as cached just before it, +1 so 0 = unknown).
    if(const int32_t before=int32_t(packed>>32)-1;before>0&&gamestate::RestoreLeftDurability(context,before))++parryRepaired;
    uintptr_t game=0,player=0;
    if(!parryReady.load()||!LocalServer(game,player)){++parryRefused;return;}
    const auto unit=originalLookup(game,1,monster);uint32_t mode=0;
    if(!unit||!Read(unit+0xc,mode)||mode==0||mode==12){++parryRefused;return;}   // dying or dead
    // Deflect: a monster already recovering (mode 3), or one with no GetHit
    // mode to stagger into (Griswold and the like: native refuses its
    // MonStats2 row), is still parried - the sound and the riposte, no stagger.
    // Act bosses never get here: they are not parryable (combat.cpp).
    const uintptr_t event[2]{0,unit};
    if(mode!=3&&reinterpret_cast<HitRecoveryFn>(d2rsig::Addr(HitRecoveryRva))(game,event))++parried;
    else ++deflected;
    QueueParrySound();combat::NoteRiposte(monster);
}
void QueueBlockSound() {
    if(blockSound.load()>0&&context&&threads&&threads->runOnUiThread)threads->runOnUiThread(context,&BlockSound,nullptr);
}
void __cdecl ClientDurability(const D2RL::PluginContext*,void* token) noexcept {
    const auto packed=reinterpret_cast<uintptr_t>(token);
    combat::MirrorClientDurability(uint32_t(packed),int(uint32_t(packed>>32)));
}
void QueueClientDurability(uint32_t itemId,int value) {
    if(value<0||!context||!threads||!threads->runOnUiThread)return;
    threads->runOnUiThread(context,&ClientDurability,reinterpret_cast<void*>((uintptr_t(uint32_t(value))<<32)|itemId));
}
// Game thread: a parry before the hit frame, once the swing came during this
// monster's downstroke. Never once its to-hit roll has run, and never for an
// attack whose wind-up is not measured yet: that one is judged at its hit.
// Off by default ([gameplay] parry_at_hit): the hit frame resolves the parry.
bool EarlyStagger(uintptr_t game,uintptr_t player,const AttackSeen& seen,uint64_t opensAt,const char* trigger) {
    if(parryAtHit.load(std::memory_order_relaxed))return false;
    if(!seen.at||seen.rolled>=seen.at||!opensAt)return false;
    const auto unit=originalLookup(game,1,seen.id);uint32_t mode=0;
    if(!unit||!Read(unit+0xc,mode)||!AttackMode(int(mode)))return false;   // its attack already ended
    const uint64_t swing=combat::EarlyParry(unit,player,seen.at,opensAt,trigger);
    if(!swing)return false;
    const uintptr_t event[2]{0,unit};
    // No GetHit mode (Griswold and the like): the swing stays armed and the
    // hit-frame parry deflects it (Parry above).
    if(!reinterpret_cast<HitRecoveryFn>(d2rsig::Addr(HitRecoveryRva))(game,event))return false;
    ++parried;combat::ConfirmParry(swing);QueueParrySound();combat::NoteRiposte(seen.id);
    return true;
}
void __cdecl AttackParry(const D2RL::PluginContext*,void* token) noexcept {
    const auto monster=uint32_t(reinterpret_cast<uintptr_t>(token));
    uintptr_t game=0,player=0;
    if(!parryReady.load()||!LocalServer(game,player))return;
    AttackSeen seen{};uint64_t opens=0;
    {std::lock_guard lock(attackMutex);for(const auto& a:attacks)if(a.id==monster){seen=a;opens=OpensAt(a);break;}}
    EarlyStagger(game,player,seen,opens,"attack_start");
}
void __cdecl SwingParry(const D2RL::PluginContext*,void*) noexcept {
    uintptr_t game=0,player=0;
    if(!parryReady.load()||!LocalServer(game,player))return;
    std::array<AttackSeen,64> seen;std::array<uint64_t,64> opens{};
    {std::lock_guard lock(attackMutex);seen=attacks;for(size_t i=0;i<seen.size();++i)opens[i]=OpensAt(seen[i]);}
    const auto now=GetTickCount64();
    for(size_t i=0;i<seen.size();++i) {
        const auto& a=seen[i];
        if(a.at&&now>=a.at&&now-a.at<=3000&&EarlyStagger(game,player,a,opens[i],"swing"))break;   // one parry per swing
    }
}
void QueueAttackParry(uint32_t monster) {
    if(parryReady.load()&&context&&threads&&threads->runOnGameThread)threads->runOnGameThread(context,&AttackParry,reinterpret_cast<void*>(uintptr_t(monster)));
}
void QueueParrySwing() {
    if(parryReady.load()&&context&&threads&&threads->runOnGameThread)threads->runOnGameThread(context,&SwingParry,nullptr);
}
void NoteAttackRolled(uint32_t monster) {
    const auto now=GetTickCount64();
    std::lock_guard lock(attackMutex);
    for(auto& a:attacks)if(a.id==monster) {
        // The first roll of this attack is its hit frame: learn the wind-up.
        if(const int slot=WindupSlot(a.cls,a.mode);slot>=0&&a.at&&a.rolled<a.at&&now>=a.at&&now-a.at<=3000)
            windupMs[slot]=uint16_t(std::max<uint64_t>(1,now-a.at));
        a.rolled=now;break;
    }
}
uint64_t AttackStart(uint32_t monster) {
    const auto now=GetTickCount64();
    std::lock_guard lock(attackMutex);
    for(const auto& a:attacks)if(a.id==monster&&a.at&&now>=a.at&&now-a.at<=3000)return a.at;
    return 0;
}
uint64_t ParryOpens(uint32_t monster) {
    const auto now=GetTickCount64();
    std::lock_guard lock(attackMutex);
    for(const auto& a:attacks)if(a.id==monster&&a.at&&now>=a.at&&now-a.at<=3000)return OpensAt(a);
    return 0;
}
void ConfigureParryDownstroke(uint32_t percent) {downstrokePct=std::min<uint32_t>(percent,95);}
void ConfigureParryAtHit(bool on) {parryAtHit=on;}
bool QueueParry(uint32_t monster,int before) {
    if(!parryReady.load()||!threads||!threads->runOnGameThread)return false;
    const int32_t durability=before==-2?gamestate::LeftDurability():int32_t(before);
    const uintptr_t token=(uintptr_t(uint32_t(durability+1))<<32)|monster;
    return threads->runOnGameThread(context,&Parry,reinterpret_cast<void*>(token))==D2RL::Threads::Result::Success;
}
bool LocalServer(uintptr_t& game,uintptr_t& player) {
    Server local;{std::lock_guard lock(serverMutex);local=server;}
    uint32_t id=0;
    if(!ready.load()||!combat::LocalPlayerId(id)||!local.game||local.player!=id||
       local.session!=gamestate::LocalPlayer())return false;
    player=originalLookup(local.game,0,id);game=local.game;
    uint32_t header[3]{};uintptr_t back=0;
    return player&&Read(player,header)&&header[0]==0&&header[2]==id&&Read(player+0xd8,back)&&back==game;
}
uintptr_t ServerUnit(uintptr_t game,int type,uint32_t id) {
    return ready.load()&&game?originalLookup(game,type,id):0;
}
// Thrown potions. Vanilla hireling.txt classes: roguehire, act2hire, act3hire,
// act5hire1/2. Offline, the only live, non-hostile one is the player's own.
constexpr uint32_t HirelingClasses[]={271,338,359,560,561};
constexpr float HirelingFeetRadius=1.5f; // metres from the hireling's ground position
bool IsHireling(uint32_t classId) {
    return std::find(std::begin(HirelingClasses),std::end(HirelingClasses),classId)!=std::end(HirelingClasses);
}
// Bounded walk of live, non-hostile monsters on the local server (owning game
// thread). f(unit,classId,gridPosition) for each; no pointer escapes the call.
template<class F> bool EachFriendly(uintptr_t game,uintptr_t self,F f) {
    uintptr_t buckets[128]{};
    if(!Read(game+0x4c6*8,buckets))return false;
    unsigned visited=0;
    for(uintptr_t unit:buckets) {
        for(unsigned chain=0;unit&&chain<256&&visited++<4096;++chain) {
            uintptr_t next=0;uint32_t header[4]{};Vec pos{};
            if(!Read(unit+0x158,next)||!Read(unit,header))break;
            if(header[0]==1&&header[3]!=0&&header[3]!=12&&Position(unit,pos)&&
               !reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(game,self,unit))
                f(unit,header[1],pos);
            unit=next;
        }
    }
    return true;
}
bool NearHireling(Vec world,Vec player,float upm,float& distance,uint32_t& classId) {
    distance=-1;classId=0;
    uintptr_t game=0,self=0;uint32_t mode=0;Vec selfPos{};
    if(!LocalServer(game,self)||!Read(self+0xc,mode)||mode==0||mode==17||!Position(self,selfPos)||
       !framevr::coordinates::MatchesPlayer(selfPos,player,upm))return false;
    // Same grid plane as arrows: remove only the launch floor's shear.
    const auto landing=framevr::coordinates::GridPlane({world.x,player.y,world.z});
    float friendly=-1;uint32_t friendlyClass=0;
    if(!EachFriendly(game,self,[&](uintptr_t,uint32_t id,Vec pos){
        const float d=std::hypot(pos.x/upm-landing.x,pos.z/upm-landing.z);
        if(IsHireling(id)){if(distance<0||d<distance){distance=d;classId=id;}}
        else if(friendly<0||d<friendly){friendly=d;friendlyClass=id;}
    }))return false;
    if(distance<0)classId=friendlyClass; // log the nearest friendly class when no hireling class matched
    return distance>=0&&distance<=HirelingFeetRadius;
}
// 3.3.93787 native unit effects, the same calls monster AI uses on itself:
// STATES_ToggleState(unit,state,on) and UNITS_SetOverlay(unit,overlay,0).
// Both end in the unit-update call, so the server syncs them to the client.
constexpr uint64_t ToggleStateRva=0x3354c0,SetOverlayRva=0x349020;
constexpr uint8_t ToggleStateSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,
    0x48,0x83,0xec,0x20,0x41,0x8b,0xe8,0x8b,0xda,0x48,0x8b,0xf1,0x85,0xd2,0x78,0x38};
constexpr uint8_t SetOverlaySig[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x83,0xec,0x30,0x45,0x8b,0xf0,
    0x8b,0xfa,0x48,0x8b,0xd9,0x48,0x85,0xc9,0x75,0x18,0x88,0x4c,0x24,0x58,0x48,0x8d};
// The native colour update (0x21b470, run when a unit's states change) keeps
// the tint of the state with the highest States.txt colorpri: cold 100 and
// poison 95 beat "red" 70. Hook it so a tint we set on a hireling wins: let the
// game pick as usual, then, only for a hireling that has our state, apply that
// state's own colorshift through the native setter. Other units, and hirelings
// without our state, are untouched.
constexpr uint64_t UpdateColorRva=0x21b470,CheckStateRva=0x3351b0,ColorShiftRva=0x342a40,UnitVersionRva=0x34a0e0,TablesRva=0x300a90;
constexpr uint8_t UpdateColorSig[]={0x40,0x53,0x55,0x57,0x41,0x54,0x41,0x57,0x48,0x83,0xec,0x30,0x4c,0x89,0x74,0x24,0x70,
    0x48,0x8b,0xe9,0xe8,0x57,0xec,0x12,0x00,0x45,0x32,0xf6};
constexpr uint8_t CheckStateSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x8b,0xda,0x48,0x8b,0xf1};
constexpr uint8_t ColorShiftSig[]={0x89,0x91,0xb4,0x00,0x00,0x00,0xc3};
constexpr uint8_t UnitVersionSig[]={0x48,0x83,0xec,0x28,0x48,0x85,0xc9,0x75,0x1a,0x88,0x4c,0x24,0x30,0x48,0x8d,0x4c};
constexpr uint8_t TablesSig[]={0x48,0x83,0xec,0x28,0x0f,0xb6,0xc1,0x48,0x89,0x44,0x24,0x38,0x48,0x83,0xf8,0x04};
using UpdateColorFn=void(*)(uintptr_t);
UpdateColorFn originalUpdateColor=nullptr;
bool colorIn=false;
std::atomic<int> priorityTint{-1};
void OnUpdateColor(uintptr_t unit) {
    originalUpdateColor(unit);
    const int state=priorityTint.load(std::memory_order_relaxed);
    uint32_t header[2]{};
    if(state<0||!Read(unit,header)||header[0]!=1||!IsHireling(header[1])||
       !reinterpret_cast<int(*)(uintptr_t,int)>(d2rsig::Addr(CheckStateRva))(unit,state))return;
    // That state's own colorshift: States rows are 0x44 bytes at tables+0x290
    // (count at +0x298); colorpri is +0x20, colorshift +0x21.
    const auto version=reinterpret_cast<uint8_t(*)(uintptr_t)>(d2rsig::Addr(UnitVersionRva))(unit);
    const auto tables=reinterpret_cast<uintptr_t(*)(uint8_t)>(d2rsig::Addr(TablesRva))(version);
    uintptr_t rows=0;uint64_t count=0;uint8_t shift=0;
    if(!tables||!Read(tables+0x290,rows)||!Read(tables+0x298,count)||uint64_t(state)>=count||
       !Read(rows+uint64_t(state)*0x44+0x21,shift)||!shift)return;
    reinterpret_cast<void(*)(uintptr_t,int)>(d2rsig::Addr(ColorShiftRva))(unit,shift);
}
void InstallTint() {
    if(colorIn||!d2rsig::Check(CheckStateRva,CheckStateSig,sizeof CheckStateSig)||!d2rsig::Check(ColorShiftRva,ColorShiftSig,sizeof ColorShiftSig)||
       !d2rsig::Check(UnitVersionRva,UnitVersionSig,sizeof UnitVersionSig)||!d2rsig::Check(TablesRva,TablesSig,sizeof TablesSig))return;
    colorIn=d2rsig::Hook(UpdateColorRva,UpdateColorSig,sizeof UpdateColorSig,reinterpret_cast<void*>(&OnUpdateColor),reinterpret_cast<void**>(&originalUpdateColor));
    if(colorIn)framevr::logging::Write("Frame merc: native colour update hooked; our hireling tint outranks cold/poison");
}
// The hireling with this id, or 0 (gone or another unit). alive: also refuse
// a dead hireling; clearing a cosmetic state must still reach its corpse.
uintptr_t Hireling(uintptr_t game,uintptr_t self,uint32_t id,bool alive) {
    const auto unit=originalLookup(game,1,id);uint32_t header[4]{};
    if(!unit||!Read(unit,header)||header[0]!=1||header[2]!=id||!IsHireling(header[1]))return 0;
    if(alive&&(header[3]==0||header[3]==12))return 0;
    return reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(game,self,unit)?0:unit;
}
// Target lock: the same bounded walk, keeping live monsters the native
// relation check calls hostile, in grid cells, with whether a wall is between.
// MonStats flags of a monster (unit +0x10 monster data, whose first field is
// its MonStats record; D2R's record, as monstats.bin, has the flags dword at
// +0x3C, matching every row of monstats.txt). 0x100
// npc, 0x400 inTown: town residents such as the Rogue camp's cows (class 179),
// which the relation check still calls attackable. Unread: 0.
uint32_t MonsterFlags(uintptr_t unit) {
    uintptr_t data=0,stats=0;uint32_t flags=0;
    if(!Read(unit+0x10,data)||data<0x10000||!Read(data,stats)||stats<0x10000||!Read(stats+0x3c,flags))return 0;
    return flags;
}
constexpr uint32_t kMonNpc=0x100,kMonInTown=0x400;
int Hostiles(Hostile* out,int max,float range,float& heroX,float& heroY) {
    uintptr_t game=0,self=0;Vec selfPos{};
    if(!out||max<=0||!LocalServer(game,self)||!Position(self,selfPos))return -1;
    heroX=selfPos.x*.5f;heroY=selfPos.z*.5f;
    uintptr_t buckets[128]{};
    if(!Read(game+0x4c6*8,buckets))return -1;
    // The NEAREST max within range, not the first in hash order: in a crowd a
    // far monster must not push out a near one. Line of sight only for the
    // ones kept.
    constexpr int Cap=128;
    max=std::min(max,Cap);
    uintptr_t units[Cap]{};float distance[Cap]{};
    int n=0,inRange=0;unsigned visited=0;
    for(uintptr_t unit:buckets) {
        for(unsigned chain=0;unit&&chain<256&&visited++<4096;++chain) {
            uintptr_t next=0;uint32_t header[4]{};Vec pos{};
            if(!Read(unit+0x158,next)||!Read(unit,header))break;
            const float d=header[0]==1&&header[3]!=0&&header[3]!=12&&Position(unit,pos)?std::hypot(pos.x*.5f-heroX,pos.z*.5f-heroY):-1.f;
            if(d>=0&&d<=range&&
               reinterpret_cast<int(*)(uintptr_t,uintptr_t,uintptr_t)>(d2rsig::Addr(RelationRva))(game,self,unit)&&
               !(MonsterFlags(unit)&(kMonNpc|kMonInTown))) {   // not town residents
                ++inRange;
                const int slot=framevr::targetcone::NearestSlot(distance,n,max,d);
                if(slot>=0){units[slot]=unit;distance[slot]=d;out[slot]={header[2],header[1],pos.x*.5f,pos.z*.5f,false};if(slot==n)++n;}
            }
            unit=next;
        }
    }
    for(int i=0;i<n;++i)out[i].clear=reinterpret_cast<int(*)(uintptr_t,uintptr_t,int)>(d2rsig::Addr(LineRva))(self,units[i],0x804)==0;
    // Telemetry: a crowd bigger than the list, at most every 5 s.
    static ULONGLONG crowdLogged=0;
    if(inRange>n&&GetTickCount64()-crowdLogged>5000) {
        crowdLogged=GetTickCount64();
        char line[128];snprintf(line,sizeof line,"Frame target lock: kept the nearest %d of %d hostiles in range",n,inRange);
        framevr::logging::Write(line);
    }
    return n;
}
bool HirelingVitals(uint32_t hint,float& health,float& maxHealth,uint32_t& classId,uint32_t& unitId) {
    health=maxHealth=0;classId=unitId=0;
    const bool statReady=d2rsig::Check(nativeui::abi::StatRva,nativeui::abi::StatSig,sizeof nativeui::abi::StatSig);
    uintptr_t game=0,self=0;uint32_t mode=0;Vec selfPos{};
    if(!statReady||!LocalServer(game,self)||!Read(self+0xc,mode)||mode==0||mode==17||!Position(self,selfPos))return false;
    // Known hireling: one direct lookup. Otherwise (first sample, death,
    // rehire) walk for it, at most once a second while none is found.
    // Offline the only live, non-hostile hireling is the player's own; the
    // walk takes the nearest if a mod ever adds more.
    static uint32_t cachedHirelingId=0;
    if(!hint&&cachedHirelingId)hint=cachedHirelingId;
    uintptr_t best=hint?Hireling(game,self,hint,true):0;
    if(best){unitId=hint;cachedHirelingId=hint;}
    else {
        static uint64_t walkedAt=0;const auto now=GetTickCount64();
        if(now-walkedAt<1000)return false;
        walkedAt=now;float bestDistance=-1;
        if(!EachFriendly(game,self,[&](uintptr_t unit,uint32_t id,Vec pos){
            const float d=std::hypot(pos.x-selfPos.x,pos.z-selfPos.z);uint32_t header[3]{};
            if(IsHireling(id)&&(bestDistance<0||d<bestDistance)&&Read(unit,header)){bestDistance=d;best=unit;unitId=header[2];}
        })||!best){cachedHirelingId=0;return false;}
        walkedAt=0; // found: later samples use the direct lookup
        cachedHirelingId=unitId;
    }
    uint32_t header[2]{};
    if(!Read(best,header))return false;
    classId=header[1];
    auto stat=reinterpret_cast<int(*)(uintptr_t,int,int)>(d2rsig::Addr(nativeui::abi::StatRva));
    health=stat(best,6,0)/256.f;maxHealth=stat(best,7,0)/256.f; // life, max life (8.8 fixed point)
    return maxHealth>0&&health>=0&&health<=maxHealth*2&&maxHealth<1000000;
}
bool HirelingState(uint32_t unitId,int state,bool on) {
    uintptr_t game=0,self=0;
    if(!d2rsig::Check(ToggleStateRva,ToggleStateSig,sizeof ToggleStateSig)||!LocalServer(game,self))return false;
    const auto unit=Hireling(game,self,unitId,on);
    if(!unit)return false;
    // Before the toggle: its own colour update must already see the priority.
    if(on)priorityTint=state;
    reinterpret_cast<void(*)(uintptr_t,int,int)>(d2rsig::Addr(ToggleStateRva))(unit,state,on?1:0);
    if(!on){int expected=state;priorityTint.compare_exchange_strong(expected,-1);}
    return true;
}
bool HirelingOverlay(uint32_t unitId,int overlay) {
    uintptr_t game=0,self=0;
    if(!d2rsig::Check(SetOverlayRva,SetOverlaySig,sizeof SetOverlaySig)||!LocalServer(game,self))return false;
    const auto unit=Hireling(game,self,unitId,true);
    if(!unit)return false;
    reinterpret_cast<void(*)(uintptr_t,int,int)>(d2rsig::Addr(SetOverlayRva))(unit,overlay,0);
    return true;
}
void Install() {
    meleeweapon::Install();
    breakables::Install();
    InstallTint();
    if(ready.load()) {
        InstallSmite();
        InstallStrikes();
        if(!parryReady.load()&&HitRecoveryRva&&d2rsig::Check(HitRecoveryRva,HitRecoverySig,sizeof HitRecoverySig)) {
            parryReady=true;framevr::logging::Write("Frame guard: native hit recovery verified; perfect parry ready");
        }
        // The mode setter: monster attack starts (the parry) and the hero's
        // refused flinch (player_flinch), so it goes in without the parry too.
        if(!setModeIn) {
            setModeIn=d2rsig::Hook(SetModeRva,SetModeSig,sizeof SetModeSig,reinterpret_cast<void*>(&SetMode),reinterpret_cast<void**>(&originalSetMode));
            framevr::logging::Write(setModeIn?"Frame guard: unit mode setter hooked; monster attack starts timed for the parry; hero flinch refusable":
                                              "Frame guard: mode setter signature mismatch; parry falls back to its window alone; the hero flinches natively");
        }
        return;
    }
    if(!context||!threads||!threads->runOnGameThread)return;
    if(!d2rsig::Check(AttackRva,AttackSig,sizeof AttackSig)||!d2rsig::Check(RelationRva,RelationSig,sizeof RelationSig)||
       !d2rsig::Check(LineRva,LineSig,sizeof LineSig)||!d2rsig::Check(StatsRva,StatsSig,sizeof StatsSig)||
       !d2rsig::Check(0x4925fe,HashSig,sizeof HashSig)||!d2rsig::Check(0x43007f,AttackCallsSig,sizeof AttackCallsSig))return;
    if(!targetIn)targetIn=d2rsig::Hook(TargetRva,TargetSig,sizeof TargetSig,reinterpret_cast<void*>(&Target),reinterpret_cast<void**>(&originalTarget));
    if(!lookupIn)lookupIn=d2rsig::Hook(LookupRva,LookupSig,sizeof LookupSig,reinterpret_cast<void*>(&Lookup),reinterpret_cast<void**>(&originalLookup));
    if(!rangeIn)rangeIn=d2rsig::Hook(RangeRva,RangeSig,sizeof RangeSig,reinterpret_cast<void*>(&Range),reinterpret_cast<void**>(&originalRange));
    // Filled from the verified code snapshot, same signature used in tests.
    constexpr uint8_t flagSig[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x55,0x41,0x56};
    if(!flagsIn)flagsIn=d2rsig::Hook(FlagsRva,flagSig,sizeof flagSig,reinterpret_cast<void*>(&Flags),reinterpret_cast<void**>(&originalFlags));
    if(targetIn&&lookupIn&&rangeIn&&flagsIn&&!ready.exchange(true))
        framevr::logging::Write("Frame melee: free-swing contact hooks ready; swept capsules -> exact native target; no grip/button/auto-aim");
}
void Cancel() {
    std::lock_guard lock(queueMutex);for(auto& s:current)s.active=false;count=0;for(auto& e:engines)e.Reset();callback.Cancel();
}
void CancelShield() {std::lock_guard lock(queueMutex);current[1].active=false;engines[1].Reset();}
void CancelWeapon(int hand) {std::lock_guard lock(queueMutex);const int lane=hand==1?2:0;current[lane].active=false;engines[lane].Reset();}
bool ShieldReady(){return smiteReady.load();}
void ConfigureAttackSpeed(bool enabled,bool traceHits){attackSpeed=enabled;trace=traceHits;}
void ConfigureSwingRecovery(bool on,float curve,float grace){swingRecovery=on;recoveryCurve=curve;recoveryGrace=grace;}
void ConfigureCleave(float step,float floor){cleaveStep=step;cleaveFloor=floor;}
void ConfigureAttackRating(bool native){attackRating=native;combat::ConfigureMeleeRolls(native);}
void ConfigurePlayerFlinch(bool native){noFlinch=!native;}
void ConfigureBlockSound(int sound) {
    static bool loaded=false; // settings thread only
    const int previous=blockSound.exchange(sound);
    if(loaded&&previous!=sound)QueueBlockSound(); // audition a change live
    loaded=true;
}
void ConfigureParrySound(int sound) {
    static bool loaded=false; // settings thread only
    const int previous=parrySound.exchange(sound);
    if(loaded&&previous!=sound)QueueParrySound();
    loaded=true;
}
void SetAttackCycle(uint64_t session,uint32_t milliseconds,int hand){if(hand==1)offCycleMs=milliseconds;else cycleMs=milliseconds;cycleSession=session;}
ButtonStroke ButtonHit(uint32_t monster) {
    ButtonStroke out;out.smite=smiteDepth>0;
    // The swing clocks belong to the game thread (Apply); anywhere else nothing is read or stamped.
    if(GetCurrentThreadId()!=gameThread.load(std::memory_order_relaxed)){++triggerOtherThread;return out;}
    const auto session=gamestate::LocalPlayer(),now=GetTickCount64();
    const uint32_t cycle=cycleSession.load()==session?cycleMs.load():0;
    const uint64_t sinceSwing=std::min(hands[0].recovery.Since(session,now),hands[1].recovery.Since(session,now));
    const auto v=combat::TriggerRecovery(sinceSwing,cycle,swingRecovery.load(std::memory_order_relaxed),
                                         recoveryCurve.load(std::memory_order_relaxed),recoveryGrace.load(std::memory_order_relaxed));
    out.recovery=v.recovery;out.onHit=v.onHit==combat::OnHit::Allowed;
    triggerLanded={session,now};++buttonAttacks;
    if(out.recovery<1||!out.onHit)++triggerPenalised;
    if(trace.load()) {
        char line[220];snprintf(line,sizeof line,"Frame melee trace: trigger target=%u%s since_last_swing=%lld ms cycle=%u ms recovery=%.2f onhit=%d",
            monster,out.smite?" smite":"",sinceSwing==UINT64_MAX?-1LL:(long long)sinceSwing,cycle,out.recovery,int(out.onHit));
        framevr::logging::Write(line);
    }
    return out;
}
void Submit(const Sample& sample) {
    if(sample.handIndex<0||sample.handIndex>1)return;
    uintptr_t token=0;
    {
        std::lock_guard lock(queueMutex);
        current[Lane(sample)]=sample;auto& engine=engines[Lane(sample)];
        const auto session=gamestate::LocalPlayer();
        if(session!=inputSession){inputSession=session;for(auto& e:engines)e.Reset();count=0;callback.Cancel();}
        if(callback.Expired(GetTickCount64(),500)){callback.Cancel();count=0;for(auto& e:engines)e.Reset();++recovered;}
        Sweep sweep;
        if(!session||!ready.load()||!sample.active){current[Lane(sample)].active=false;engine.Reset();return;}
        if(engine.Step(sample,sweep)) {
            if(count==requests.size()){count=0;engine.Reset();return;} // never replay a backlog
            requests[count++]={sweep,sample,session,GetTickCount64()};++samples;
            if(sample.shield)++shieldSweeps;
        }
        if(count)token=callback.Start(GetTickCount64());
    }
    if(token&&threads->runOnGameThread(context,&Apply,reinterpret_cast<void*>(token))!=D2RL::Threads::Result::Success) {
        std::lock_guard lock(queueMutex);if(callback.Claim(token))count=0;
    }
}
void Report() {
    breakables::Report();
    wallcells::Report();
    propscan::Report();
    if(const auto right=clawLanded[0].exchange(0),left=clawLanded[1].exchange(0);right||left) {
        char line[160];snprintf(line,sizeof line,"Frame claws: right landed=%u left landed=%u (one shared recovery, cycle %u ms)",right,left,cycleMs.load());
        framevr::logging::Write(line);
    }
    const auto right=dualLanded[0].exchange(0),left=dualLanded[1].exchange(0),invalid=dualRefused.exchange(0);
    if(right||left||invalid) {
        char line[180];snprintf(line,sizeof line,"Frame dual wield: right landed=%u left landed=%u stale/unavailable weapon=%u",right,left,invalid);
        framevr::logging::Write(line);
    }
    if(const auto pd=parried.exchange(0),pr=parryRefused.exchange(0),pf=parryRepaired.exchange(0),as=attackStarts.exchange(0),dv=deflected.exchange(0);pd||pr||pf||dv) {
        char line[220];snprintf(line,sizeof line,"Frame guard: parry hit recovery applied=%u deflected=%u refused=%u shield_wear_refunded=%u monster_attack_starts=%u",pd,dv,pr,pf,as);
        framevr::logging::Write(line);
    }
    {
        const auto gh=flinchRefused[0].exchange(0),bl=flinchRefused[1].exchange(0),kb=knockbacks.exchange(0),ke=knockbackEnds.exchange(0);
#ifdef D2RVR_DEVELOPER
        if(gh||bl||kb||ke) {
            char line[160];snprintf(line,sizeof line,"Frame flinch: refused gethit=%u block=%u knockback_native=%u knockback_to_neutral=%u",gh,bl,kb,ke);
            framevr::logging::Write(line);
        }
#else
        (void)gh;(void)bl;(void)kb;(void)ke;
#endif
    }
    const auto ss=shieldSweeps.exchange(0),sr=shieldRequests.exchange(0),sa=shieldAccepted.exchange(0),sf=shieldRejected.exchange(0);
    if(ss||sr||sa||sf) {
        char line[180];snprintf(line,sizeof line,"Frame shield: energetic sweeps=%u cast requests=%u native completed=%u refused=%u",ss,sr,sa,sf);
        framevr::logging::Write(line);
    }
    const auto s=samples.exchange(0),c=contacts.exchange(0),a=accepted.exchange(0),r=rejected.exchange(0),g=noServer.exchange(0);
    const auto i=intersections.exchange(0),x=stale.exchange(0),w=walls.exchange(0);
    const auto callbacks=applied.exchange(0),retries=recovered.exchange(0),missing=playerMissing.exchange(0);
    const auto mismatch=positionMismatch.exchange(0),monsters=nearby.exchange(0),bodies=bodyMissing.exchange(0);
    Diagnostics d,side[3];uint64_t age=0;
    {std::lock_guard lock(queueMutex);for(int k=0;k<3;++k){side[k]=engines[k].TakeDiagnostics();d.discontinuities+=side[k].discontinuities;d.recovering+=side[k].recovering;}age=callback.Age(GetTickCount64());}
    // Why the swing paths restarted, per hand, and why sweeps were dropped as stale.
    for(int k=0;k<3;++k)if(side[k].discontinuities) {
        const auto& q=side[k];char why[260];
        snprintf(why,sizeof why,"Frame melee resets: hand=%s resets=%u state=%u timing=%u hand_travel=%u hand_turn=%u player_jump=%u blade_jump=%u fastest_hand=%.1f m/s fastest_turn=%.1f rad/s",
            k==2?"left weapon":k==1?"shield":"right weapon",q.discontinuities,q.reasons[ResetState],q.reasons[ResetTiming],q.reasons[ResetHandTravel],q.reasons[ResetHandTurn],q.reasons[ResetPlayer],q.reasons[ResetBlade],q.fastestHand,q.fastestTurn);
        framevr::logging::Write(why);
    }
    {
        unsigned w[StaleReasons];bool any=false;for(int k=0;k<StaleReasons;++k){w[k]=staleWhy[k].exchange(0);any|=w[k]!=0;}
        if(any) {
            char why[220];snprintf(why,sizeof why,"Frame melee stale: inactive=%u epoch=%u equipment=%u skill=%u age=%u (oldest %u ms) menu=%u hands_key=%u",
                w[0],w[1],w[2],w[3],w[4],staleOldestMs.exchange(0),w[5],w[6]);
            framevr::logging::Write(why);
        }
    }
#ifdef D2RVR_DEVELOPER
    {   // Unleashed on-hit gate: resolved contacts with their effects, and gated ones by reason.
        unsigned w[OnHitReasons];bool any=false;for(int k=0;k<OnHitReasons;++k){w[k]=onHitWhy[k].exchange(0);any|=w[k]!=0;}
        if(any) {
            char why[260];snprintf(why,sizeof why,"Frame on-hit gate: with effects=%u (unknown_cycle=%u riposte=%u flurry=%u) gated not_full_cycle=%u cleave_follow_up=%u",
                w[OnHitAllowed]+w[OnHitUnknownCycle]+w[OnHitRiposte]+w[OnHitFlurry],w[OnHitUnknownCycle],w[OnHitRiposte],w[OnHitFlurry],w[OnHitNotFull],w[OnHitCleave]);
            framevr::logging::Write(why);
        }
    }
    if(const auto tp=triggerPenalised.exchange(0),to=triggerOtherThread.exchange(0);tp||to) {
        char line[200];snprintf(line,sizeof line,"Frame trigger melee: hits inside a swing's cycle (recovery / on-hit gated)=%u off the game thread (not timed)=%u",tp,to);
        framevr::logging::Write(line);
    }
    {   // Melee-skill contact adapter: armed contacts, casts, and why the rest stayed basic.
        const auto armed=strikeArmed.exchange(0),cast=strikeCast.exchange(0),basic=strikeBasic.exchange(0),
                   fallback=strikeFallback.exchange(0),start=strikeStartRefused.exchange(0);
        unsigned w[size_t(Refusal::Count)];for(size_t k=0;k<std::size(w);++k)w[k]=strikeRefused[k].exchange(0);
        const auto voices=formVoices.exchange(0);
        if(armed||cast||fallback||start||voices) {
            char why[340];snprintf(why,sizeof why,"Frame melee skills: armed contacts=%u cast=%u basic (not full cycle / cleave)=%u refused->basic=%u "
                "(mana=%u weapon=%u form=%u delay=%u other=%u) start refused=%u form voices=%u",
                armed,cast,basic,fallback,w[size_t(Refusal::Mana)],w[size_t(Refusal::Weapon)],w[size_t(Refusal::Form)],
                w[size_t(Refusal::Delay)],w[size_t(Refusal::Other)],start,voices);
            framevr::logging::Write(why);
        }
        const auto casts=flurryCasts.exchange(0),hits=flurryHits.exchange(0),early=flurryEarly.exchange(0),forfeit=flurryForfeit.exchange(0),
                   spent=flurrySpent.exchange(0),refused=flurryRefused.exchange(0),spreadHits=flurrySpread.exchange(0);
        if(casts||hits||early||forfeit||refused) {
            char line[260];snprintf(line,sizeof line,"Frame melee flurries: casts=%u follow-up hits=%u (spread to a second creature=%u) early strokes (basic)=%u spent=%u hits forfeited=%u follow-up refused=%u",
                casts,hits,spreadHits,early,spent,forfeit,refused);
            framevr::logging::Write(line);
        }
    }
#endif
    const auto early=tooSoon.exchange(0),extra=extraTargets.exchange(0),m=missed.exchange(0),button=buttonAttacks.exchange(0);
    if(!s&&!c&&!g&&!d.discontinuities&&!d.recovering&&!retries&&!early&&!extra&&!m&&!button)return;
    char line[760];snprintf(line,sizeof line,"Frame melee: sweep samples=%u physical contacts=%u native accepted=%u missed=%u rejected=%u missing server=%u intersections=%u stale=%u walls=%u pose resets=%u recovering=%u callbacks=%u abandoned=%u pending_ms=%llu missing_player=%u position_mismatch=%u nearby=%u missing_body=%u attack_speed=%d cycle_ms=%u too_soon=%u extra_targets=%u button_cycles=%u",s,c,a,m,r,g,i,x,w,d.discontinuities,d.recovering,callbacks,retries,(unsigned long long)age,missing,mismatch,monsters,bodies,
        int(attackSpeed.load()),cycleMs.load(),early,extra,button);
    framevr::logging::Write(line);
}
}
