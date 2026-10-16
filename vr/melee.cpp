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
#include "native_skill_dispatch.h"
#include "bow_signatures.h"
#include "melee.h"
#include "combat.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include "../steamvr/transport.h"
#include "../steamvr/callback_ticket.h"
#include "../steamvr/world_coordinates.h"
#include "../steamvr/attack_speed.h"
namespace gamestate {uint64_t LocalPlayer();bool MenuOpen();uint32_t HandsKey();uint32_t WeaponSet();}
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
// Attack speed: settings/cycle cross threads; the cadence and the strokes that
// already landed live on the game thread (Apply) only.
std::atomic<bool> attackSpeed{false},trace{false},attackRating{true};
std::atomic<uint64_t> cycleSession{0};std::atomic<uint32_t> cycleMs{0};
std::atomic<unsigned> tooSoon{0},extraTargets{0},missed{0},buttonAttacks{0};
framevr::attackspeed::Cadence cadence;HitSet landed,felt;
std::mutex queueMutex;std::array<Engine,2> engines;HitSet hits,shieldHits;
struct Request {Sweep sweep;Sample sample;uint64_t session=0,at=0;};
std::array<Request,24> requests;size_t count=0;
std::array<Sample,2> current;uint64_t inputSession=0;framevr::CallbackTicket callback;
using SmiteFn=int(*)(uintptr_t,uintptr_t,int,int);
SmiteFn originalSmite=nullptr;std::atomic<bool> smiteReady{false};
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
bool Allowed(const Request& r) {
    std::lock_guard lock(queueMutex);
    const auto& now=current[r.sample.shield?1:0];
    return now.active&&r.session==inputSession&&r.sample.epoch==now.epoch&&r.sample.equipment==now.equipment&&
        r.sample.minWeaponSpeed==now.minWeaponSpeed&&
        r.sample.skillSlot==now.skillSlot&&r.sample.skillOwner==now.skillOwner&&r.sample.skillContext==now.skillContext&&
        GetTickCount64()-r.at<=150&&r.session==gamestate::LocalPlayer()&&!gamestate::MenuOpen()&&
        r.sample.equipment==(gamestate::HandsKey()^(gamestate::WeaponSet()<<30));
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
uint64_t tracedStroke[2]{};   // last stroke logged per refusal kind (game thread)
void TraceRefusal(int kind,const Request& r,uint32_t target) {
    if(!trace.load()||tracedStroke[kind]==r.sweep.stroke)return;
    tracedStroke[kind]=r.sweep.stroke;
    const auto since=cadence.Since(r.session,r.at);
    char line[240];snprintf(line,sizeof line,"Frame melee trace: stroke=%llu target=%u REFUSED (%s) since_last_landed=%lld ms remaining=%llu ms cycle=%u ms",
        (unsigned long long)r.sweep.stroke,target,kind?"one creature per stroke":"too soon",since==UINT64_MAX?-1LL:(long long)since,
        (unsigned long long)cadence.Remaining(r.session,r.at),cycleMs.load());
    framevr::logging::Write(line);
}
void TraceHit(const Request& r,uint32_t target,const char* outcome,uint64_t since,uint32_t cycle) {
    if(!trace.load())return;
    char line[260];snprintf(line,sizeof line,"Frame melee trace: stroke=%llu target=%u %s since_last_landed=%lld ms cycle=%u ms %s=%.1f m/s",
        (unsigned long long)r.sweep.stroke,target,outcome,since==UINT64_MAX?-1LL:(long long)since,cycle,r.sweep.thrust?"thrust":"swing",r.sweep.speed);
    framevr::logging::Write(line);
}
// Every weapon contact with a hostile creature feels struck: one tap per
// stroke and creature, whether native then lands, misses or refuses it.
void Feel(const Request& r,uint32_t target) {
    if(felt.Insert(r.session,r.sweep.stroke,target))framevr::MeleeImpact();
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
                        Feel(r,header[2]);
                        if(hits.Contains(r.session,r.sweep.stroke,header[2]))continue;
                        // Attack speed: a basic attack strikes one creature, and
                        // no sooner than its native attack cycle (IAS, WSM, class)
                        // after the last attack. A rolled miss spends the cycle
                        // as a native swing would; a geometric whiff never does.
                        uint32_t cycle=0;
                        if(attackSpeed.load()) {
                            if(landed.Contains(r.session,r.sweep.stroke,0)){++extraTargets;TraceRefusal(1,r,header[2]);break;}
                            if(!cadence.Ready(r.session,r.at)){++tooSoon;TraceRefusal(0,r,header[2]);break;}
                            cycle=cycleSession.load()==r.session?cycleMs.load():0;
                        }
                        const auto since=cadence.Since(r.session,r.at);
                        if(!hits.Insert(r.session,r.sweep.stroke,header[2]))continue;
                        ++contacts;
                        const auto saved=contact;contact={local.game,player,unit,false};
                        // Native basic attack owns its nontrivial damage object,
                        // proc events, criticals, resistances, leech, XP and item
                        // durability. TLS redirects only THIS invocation's target.
                        const bool rolls=combat::NativeRolls(attackRating.load());
                        reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int)>(d2rsig::Addr(AttackRva))(local.game,player,0,1);
                        combat::NativeRolls(rolls);
                        if(contact.accepted)++accepted;else if(contact.resolved)++missed;else ++rejected;
                        if(contact.resolved&&attackSpeed.load()){cadence.Landed(r.session,r.at,cycle);landed.Insert(r.session,r.sweep.stroke,0);}
                        TraceHit(r,header[2],contact.accepted?"LANDED":contact.resolved?"MISSED (roll)":"native rejected",since,cycle);
                        contact=saved;
                        break;
                    }
                }
            }
            unit=next;
        }
    }
}
}
void Init(const D2RL::PluginContext* ctx) {
    context=ctx;
    if(ctx->QueryService(&threads)!=D2RL::ServiceQueryResult::Success)threads=nullptr;
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
void Install() {
    if(ready.load()){InstallSmite();return;}
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
void CancelWeapon() {std::lock_guard lock(queueMutex);current[0].active=false;engines[0].Reset();}
bool ShieldReady(){return smiteReady.load();}
void ConfigureAttackSpeed(bool enabled,bool traceHits){attackSpeed=enabled;trace=traceHits;}
void ConfigureAttackRating(bool native){attackRating=native;combat::ConfigureMeleeRolls(native);}
void SetAttackCycle(uint64_t session,uint32_t milliseconds){cycleMs=milliseconds;cycleSession=session;}
void Submit(const Sample& sample) {
    uintptr_t token=0;
    {
        std::lock_guard lock(queueMutex);
        current[sample.shield?1:0]=sample;auto& engine=engines[sample.shield?1:0];
        const auto session=gamestate::LocalPlayer();
        if(session!=inputSession){inputSession=session;for(auto& e:engines)e.Reset();count=0;callback.Cancel();}
        if(callback.Expired(GetTickCount64(),500)){callback.Cancel();count=0;for(auto& e:engines)e.Reset();++recovered;}
        Sweep sweep;
        if(!session||!ready.load()||!sample.active){current[sample.shield?1:0].active=false;engine.Reset();return;}
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
    const auto ss=shieldSweeps.exchange(0),sr=shieldRequests.exchange(0),sa=shieldAccepted.exchange(0),sf=shieldRejected.exchange(0);
    if(ss||sr||sa||sf) {
        char line[180];snprintf(line,sizeof line,"Frame shield: energetic sweeps=%u cast requests=%u native completed=%u refused=%u",ss,sr,sa,sf);
        framevr::logging::Write(line);
    }
    const auto s=samples.exchange(0),c=contacts.exchange(0),a=accepted.exchange(0),r=rejected.exchange(0),g=noServer.exchange(0);
    const auto i=intersections.exchange(0),x=stale.exchange(0),w=walls.exchange(0);
    const auto callbacks=applied.exchange(0),retries=recovered.exchange(0),missing=playerMissing.exchange(0);
    const auto mismatch=positionMismatch.exchange(0),monsters=nearby.exchange(0),bodies=bodyMissing.exchange(0);
    Diagnostics d;uint64_t age=0;{std::lock_guard lock(queueMutex);for(auto& e:engines){
        const auto one=e.TakeDiagnostics();d.discontinuities+=one.discontinuities;d.recovering+=one.recovering;
        d.invalidPose+=one.invalidPose;d.belowSpeed+=one.belowSpeed;d.onsetRejected+=one.onsetRejected;d.peakSpeed=std::max(d.peakSpeed,one.peakSpeed);
    }age=callback.Age(GetTickCount64());}
    const auto early=tooSoon.exchange(0),extra=extraTargets.exchange(0),m=missed.exchange(0),button=buttonAttacks.exchange(0);
    if(!s&&!c&&!g&&!d.discontinuities&&!d.recovering&&!d.invalidPose&&!d.belowSpeed&&!d.onsetRejected&&!retries&&!early&&!extra&&!m&&!button)return;
    char line[880];snprintf(line,sizeof line,"Frame melee: sweep samples=%u physical contacts=%u native accepted=%u missed=%u rejected=%u missing server=%u intersections=%u stale=%u walls=%u pose resets=%u recovering=%u callbacks=%u abandoned=%u pending_ms=%llu missing_player=%u position_mismatch=%u nearby=%u missing_body=%u attack_speed=%d cycle_ms=%u too_soon=%u extra_targets=%u button_cycles=%u invalid_pose=%u below_speed=%u onset_rejected=%u peak_mps=%.2f",s,c,a,m,r,g,i,x,w,d.discontinuities,d.recovering,callbacks,retries,(unsigned long long)age,missing,mismatch,monsters,bodies,
        int(attackSpeed.load()),cycleMs.load(),early,extra,button,d.invalidPose,d.belowSpeed,d.onsetRejected,d.peakSpeed);
    framevr::logging::Write(line);
}
}
