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
#include "melee.h"
#include "melee_weapon.h"
#include "breakables.h"
#include "../steamvr/melee_hand_state.h"
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
struct Contact {uintptr_t game=0,attacker=0,defender=0;bool accepted=false,smite=false,resolved=false;};
thread_local Contact contact;
std::atomic<unsigned> samples{0},contacts{0},accepted{0},rejected{0},noServer{0},intersections{0},stale{0},walls{0};
std::atomic<unsigned> applied{0},recovered{0},playerMissing{0},positionMismatch{0},nearby{0},bodyMissing{0};
std::atomic<unsigned> shieldSweeps{0},shieldRequests{0},shieldAccepted{0},shieldRejected{0};
std::atomic<unsigned> dualLanded[2]{},dualRefused{0};
// Attack speed: settings/cycle cross threads; the cadence and the strokes that
// already landed live on the game thread (Apply) only.
std::atomic<bool> attackSpeed{false},trace{false},attackRating{true};
std::atomic<uint64_t> cycleSession{0};std::atomic<uint32_t> cycleMs{0},offCycleMs{0};
std::atomic<unsigned> tooSoon{0},extraTargets{0},missed{0},buttonAttacks{0};
framevr::attackspeed::Cadence cadence; // Classic shares one native attack cycle across hands/buttons.
std::atomic<bool> swingRecovery{false};std::atomic<float> recoveryCurve{2},recoveryGrace{1};
std::atomic<float> cleaveStep{0},cleaveFloor{.25f};
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
std::atomic<bool> parryReady{false};std::atomic<unsigned> parried{0},parryRefused{0},parryRepaired{0};
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
struct AttackSeen {uint32_t id=0;uint64_t at=0,rolled=0;};   // rolled: its to-hit roll (hit or miss)
std::mutex attackMutex;std::array<AttackSeen,64> attacks{};std::atomic<unsigned> attackStarts{0};
bool AttackMode(int mode){return mode==4||mode==5||(mode>=8&&mode<=11);}
template<class T> bool Read(uintptr_t p,T& out) {
    return p>0x10000&&framevr::ReadMemory(&out,reinterpret_cast<const void*>(p),sizeof out);
}
void ObserveServer(uintptr_t game,uintptr_t unit) {
    uint32_t id=0,header[3]{};uintptr_t back=0;
    if(!game||!combat::LocalPlayerId(id)||!Read(unit,header)||header[0]!=0||header[2]!=id||!Read(unit+0xd8,back)||back!=game)return;
    const auto session=gamestate::LocalPlayer();
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
       (contact.smite||caller==d2rsig::Addr(0x42ffaa)||caller==d2rsig::Addr(0x43010a)))return contact.defender;
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
    if(contact.game==game&&contact.attacker==attacker&&contact.defender==defender&&
       reinterpret_cast<uintptr_t>(_ReturnAddress())==d2rsig::Addr(0x430084)){contact.resolved=true;contact.accepted=(result&1)!=0;}
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
    else if(r.sample.equipment!=now.equipment||r.sample.minWeaponSpeed!=now.minWeaponSpeed||r.sample.minShieldBashSpeed!=now.minShieldBashSpeed||r.sample.weapons.id[0]!=now.weapons.id[0]||r.sample.weapons.id[1]!=now.weapons.id[1])why=StaleEquipment;
    else if(r.sample.skillSlot!=now.skillSlot||r.sample.skillOwner!=now.skillOwner||r.sample.skillContext!=now.skillContext)why=StaleSkill;
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
void TraceHit(const Request& r,uint32_t target,const char* outcome,uint64_t since,uint32_t cycle) {
    if(!trace.load())return;
    char line[260];snprintf(line,sizeof line,"Frame melee trace: hand=%s stroke=%llu target=%u %s since_last_landed=%lld ms cycle=%u ms %s=%.1f m/s",
        r.sample.handIndex?"left":"right",(unsigned long long)r.sweep.stroke,target,outcome,since==UINT64_MAX?-1LL:(long long)since,cycle,r.sweep.thrust?"thrust":"swing",r.sweep.speed);
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
    ++applied;
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
                        auto& recoveryClock=attackSpeed.load()?cadence:state.recovery;
                        const bool dual=r.sample.weapons.id[0]&&r.sample.weapons.id[1];
                        const uintptr_t weapon=dual?meleeweapon::Select(player,hand,r.sample.weapons):0;
                        const int speedHand=dual?r.sample.weapons.BodyHand(hand):0;
                        if((dual&&(!weapon||speedHand<0))||(hand==1&&!dual)){++dualRefused;continue;}
                        uint32_t selectedId=0;
                        if(dual&&(!Read(weapon+8,selectedId)||selectedId!=r.sample.weapons.displayed[hand])){++dualRefused;continue;}
                        Feel(r,header[2]);
                        if(hits.Contains(r.session,r.sweep.stroke,header[2]))continue;
                        // Attack speed: a basic attack strikes one creature, and
                        // no sooner than its native attack cycle (IAS, WSM, class)
                        // after the last attack. A rolled miss spends the cycle
                        // as a native swing would; a geometric whiff never does.
                        uint32_t cycle=0;
                        if(attackSpeed.load()) {
                            if(landed.Contains(r.session,r.sweep.stroke,0)){
                                ++extraTargets;TraceRefusal(1,r,header[2]);if(dual)continue;break;
                            }
                            if(!cadence.Ready(r.session,r.at)){
                                ++tooSoon;TraceRefusal(0,r,header[2]);if(dual)continue;break;
                            }
                            cycle=cycleSession.load()==r.session?(speedHand?offCycleMs.load():cycleMs.load()):0;
                        }
                        const auto since=recoveryClock.Since(r.session,r.at);
                        // Unleashed: the stroke's first landed contact measures how
                        // recovered it is; every creature that stroke reaches shares it.
                        float recovery=1;bool strokeStart=false;
                        if(swingRecovery.load(std::memory_order_relaxed)&&!attackSpeed.load()) {
                            if(state.strokeSession==r.session&&state.strokeId==r.sweep.stroke)recovery=state.strokeRecovery;
                            else {
                                cycle=cycleSession.load()==r.session?(speedHand?offCycleMs.load():cycleMs.load()):0;
                                recovery=combat::RecoveryFactor(since,cycle,recoveryCurve.load(std::memory_order_relaxed),
                                                                recoveryGrace.load(std::memory_order_relaxed));strokeStart=true;
                            }
                        }
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
                        const float swingRecoverySaved=combat::SwingRecovery(recovery*cleave);
                        {
                            meleeweapon::Scope selected(player,weapon);
                            reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int)>(d2rsig::Addr(AttackRva))(local.game,player,0,1);
                        }
                        combat::SwingRecovery(swingRecoverySaved);
                        combat::PhysicalSwing(swing);
                        combat::NativeRolls(rolls);
                        if(contact.accepted)++accepted;else if(contact.resolved)++missed;else ++rejected;
                        if(dual&&contact.accepted)++dualLanded[hand];
                        if(contact.resolved&&attackSpeed.load()){cadence.Landed(r.session,r.at,cycle);landed.Insert(r.session,r.sweep.stroke,0);}
                        if(contact.resolved&&strokeStart){state.recovery.Landed(r.session,r.at,cycle);state.strokeSession=r.session;state.strokeId=r.sweep.stroke;state.strokeRecovery=recovery;}
                        if(contact.resolved&&cleaveTracked)++state.cleaveLanded;
                        TraceHit(r,header[2],contact.accepted?"LANDED":contact.resolved?"MISSED (roll)":"native rejected",since,cycle);
                        contact=saved;
                        if(!dual)break; // Both blades may strike this creature in the same batch.
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
    if(AttackMode(mode)) {
        uint32_t header[3]{};uintptr_t back=0;
        if(Read(unit,header)&&header[0]==1&&Read(unit+0xd8,back)&&back>0x10000) { // a server monster starting an attack
            const auto now=GetTickCount64();
            {
            std::lock_guard lock(attackMutex);
            AttackSeen* slot=&attacks[0];
            for(auto& a:attacks){if(a.id==header[2]){slot=&a;break;}if(a.at<slot->at)slot=&a;}
            slot->id=header[2];slot->at=now;slot->rolled=0;++attackStarts;
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
    // Already recovering: nothing to interrupt. Native gates the rest.
    if(!unit||!Read(unit+0xc,mode)||mode==3){++parryRefused;return;}
    const uintptr_t event[2]{0,unit};
    if(reinterpret_cast<HitRecoveryFn>(d2rsig::Addr(HitRecoveryRva))(game,event)){++parried;QueueParrySound();combat::NoteRiposte(monster);}
    else ++parryRefused;
}
void QueueBlockSound() {
    if(blockSound.load()>0&&context&&threads&&threads->runOnUiThread)threads->runOnUiThread(context,&BlockSound,nullptr);
}
// Game thread: a parry before the hit frame, as soon as the swing and this
// monster's attack have both begun. Never once its to-hit roll has run.
bool EarlyStagger(uintptr_t game,uintptr_t player,const AttackSeen& seen,const char* trigger) {
    if(!seen.at||seen.rolled>=seen.at)return false;
    const auto unit=originalLookup(game,1,seen.id);uint32_t mode=0;
    if(!unit||!Read(unit+0xc,mode)||!AttackMode(int(mode)))return false;   // its attack already ended
    const uint64_t swing=combat::EarlyParry(unit,player,seen.at,trigger);
    if(!swing)return false;
    const uintptr_t event[2]{0,unit};
    if(!reinterpret_cast<HitRecoveryFn>(d2rsig::Addr(HitRecoveryRva))(game,event)){++parryRefused;return false;}
    ++parried;combat::ConfirmParry(swing);QueueParrySound();combat::NoteRiposte(seen.id);
    return true;
}
void __cdecl AttackParry(const D2RL::PluginContext*,void* token) noexcept {
    const auto monster=uint32_t(reinterpret_cast<uintptr_t>(token));
    uintptr_t game=0,player=0;
    if(!parryReady.load()||!LocalServer(game,player))return;
    AttackSeen seen{};
    {std::lock_guard lock(attackMutex);for(const auto& a:attacks)if(a.id==monster){seen=a;break;}}
    EarlyStagger(game,player,seen,"attack_start");
}
void __cdecl SwingParry(const D2RL::PluginContext*,void*) noexcept {
    uintptr_t game=0,player=0;
    if(!parryReady.load()||!LocalServer(game,player))return;
    std::array<AttackSeen,64> seen;{std::lock_guard lock(attackMutex);seen=attacks;}
    const auto now=GetTickCount64();
    for(const auto& a:seen)
        if(a.at&&now>=a.at&&now-a.at<=3000&&EarlyStagger(game,player,a,"swing"))break;   // one parry per swing
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
    for(auto& a:attacks)if(a.id==monster){a.rolled=now;break;}
}
uint64_t AttackStart(uint32_t monster) {
    const auto now=GetTickCount64();
    std::lock_guard lock(attackMutex);
    for(const auto& a:attacks)if(a.id==monster&&a.at&&now>=a.at&&now-a.at<=3000)return a.at;
    return 0;
}
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
bool HirelingVitals(uint32_t hint,float& health,float& maxHealth,uint32_t& classId,uint32_t& unitId) {
    health=maxHealth=0;classId=unitId=0;
    const bool statReady=d2rsig::Check(nativeui::abi::StatRva,nativeui::abi::StatSig,sizeof nativeui::abi::StatSig);
    uintptr_t game=0,self=0;uint32_t mode=0;Vec selfPos{};
    if(!statReady||!LocalServer(game,self)||!Read(self+0xc,mode)||mode==0||mode==17||!Position(self,selfPos))return false;
    // Known hireling: one direct lookup. Otherwise (first sample, death,
    // rehire) walk for it, at most once a second while none is found.
    // Offline the only live, non-hostile hireling is the player's own; the
    // walk takes the nearest if a mod ever adds more.
    uintptr_t best=hint?Hireling(game,self,hint,true):0;
    if(best)unitId=hint;
    else {
        static uint64_t walkedAt=0;const auto now=GetTickCount64();
        if(now-walkedAt<1000)return false;
        walkedAt=now;float bestDistance=-1;
        if(!EachFriendly(game,self,[&](uintptr_t unit,uint32_t id,Vec pos){
            const float d=std::hypot(pos.x-selfPos.x,pos.z-selfPos.z);uint32_t header[3]{};
            if(IsHireling(id)&&(bestDistance<0||d<bestDistance)&&Read(unit,header)){bestDistance=d;best=unit;unitId=header[2];}
        })||!best)return false;
        walkedAt=0; // found: later samples use the direct lookup
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
        if(!parryReady.load()&&HitRecoveryRva&&d2rsig::Check(HitRecoveryRva,HitRecoverySig,sizeof HitRecoverySig)) {
            parryReady=true;framevr::logging::Write("Frame guard: native hit recovery verified; perfect parry ready");
        }
        // The mode setter: monster attack starts (the parry).
        if(parryReady.load()&&!setModeIn) {
            setModeIn=d2rsig::Hook(SetModeRva,SetModeSig,sizeof SetModeSig,reinterpret_cast<void*>(&SetMode),reinterpret_cast<void**>(&originalSetMode));
            framevr::logging::Write(setModeIn?"Frame guard: unit mode setter hooked; monster attack starts timed for the parry":
                                              "Frame guard: mode setter signature mismatch; parry falls back to its window alone");
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
    const auto right=dualLanded[0].exchange(0),left=dualLanded[1].exchange(0),invalid=dualRefused.exchange(0);
    if(right||left||invalid) {
        char line[180];snprintf(line,sizeof line,"Frame dual wield: right landed=%u left landed=%u stale/unavailable weapon=%u",right,left,invalid);
        framevr::logging::Write(line);
    }
    if(const auto pd=parried.exchange(0),pr=parryRefused.exchange(0),pf=parryRepaired.exchange(0),as=attackStarts.exchange(0);pd||pr||pf) {
        char line[200];snprintf(line,sizeof line,"Frame guard: parry hit recovery applied=%u refused=%u shield_wear_refunded=%u monster_attack_starts=%u",pd,pr,pf,as);
        framevr::logging::Write(line);
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
    const auto early=tooSoon.exchange(0),extra=extraTargets.exchange(0),m=missed.exchange(0),button=buttonAttacks.exchange(0);
    if(!s&&!c&&!g&&!d.discontinuities&&!d.recovering&&!retries&&!early&&!extra&&!m&&!button)return;
    char line[760];snprintf(line,sizeof line,"Frame melee: sweep samples=%u physical contacts=%u native accepted=%u missed=%u rejected=%u missing server=%u intersections=%u stale=%u walls=%u pose resets=%u recovering=%u callbacks=%u abandoned=%u pending_ms=%llu missing_player=%u position_mismatch=%u nearby=%u missing_body=%u attack_speed=%d cycle_ms=%u too_soon=%u extra_targets=%u button_cycles=%u",s,c,a,m,r,g,i,x,w,d.discontinuities,d.recovering,callbacks,retries,(unsigned long long)age,missing,mismatch,monsters,bodies,
        int(attackSpeed.load()),cycleMs.load(),early,extra,button);
    framevr::logging::Write(line);
}
}
