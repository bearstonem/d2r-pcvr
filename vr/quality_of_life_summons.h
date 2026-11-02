// MPL-2.0. Native AI boundaries verified read-only on D2R build 93847.
#pragma once
#include <array>
#include <cstddef>
#include <atomic>
#include <cmath>
#include <cstring>

namespace framevr::qol::summons {
using ThinkFn=void(*)(uintptr_t,uintptr_t,uintptr_t);
using EnumerateFn=uintptr_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,int);
inline ThinkFn originalWolf=nullptr,originalBear=nullptr;
inline EnumerateFn originalEnumerate=nullptr;
inline std::atomic<bool> ready{false};
inline std::atomic<uint64_t> wolfCalls{0},bearCalls{0},searchCalls{0};
// Written during startup only; reload refuses changes until restart.
inline double aggro=1,leash=1;
inline uintptr_t base=0;
struct Scope {uintptr_t game=0,unit=0;};
inline thread_local Scope scope;
struct Restore {Scope saved=scope;~Restore(){scope=saved;}};
constexpr uint64_t WolfRva=0x5ca610,BearRva=0x5c95d0,EnumerateRva=0x596340;
constexpr uint8_t WolfSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
constexpr uint8_t BearSig[]={0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x60};
constexpr uint8_t EnumerateSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xe1};
template<class T> bool Read(uintptr_t address,T& value) {
    SIZE_T copied=0;
    return address>=0x10000&&address<=UINTPTR_MAX-sizeof value&&ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(address),&value,sizeof value,&copied)&&copied==sizeof value;
}
inline int Scale(int value,double factor,int maximum=32767,double maxFactor=5) {
    if(value<=0||!std::isfinite(factor)||factor<1||factor>maxFactor) return value;
    return static_cast<int>(std::fmin(std::round(value*factor),maximum));
}
template<class T,size_t N> T Get(const std::array<uint8_t,N>& bytes,size_t offset) {
    T value{};std::memcpy(&value,bytes.data()+offset,sizeof value);return value;
}
template<class T,size_t N> void Put(std::array<uint8_t,N>& bytes,size_t offset,T value) {
    std::memcpy(bytes.data()+offset,&value,sizeof value);
}
inline void ScaleParam(std::array<uint8_t,508>& row,int parameter,double factor) {
    for(size_t difficulty=0;difficulty<3;++difficulty) {
        const auto offset=0x90+parameter*6+difficulty*2;
        Put(row,offset,static_cast<int16_t>(Scale(Get<int16_t>(row,offset),factor)));
    }
}
inline bool PrepareWolf(uintptr_t unit,uintptr_t tick,std::array<uint8_t,64>& localTick,std::array<uint8_t,508>& row) {
    uint32_t header[3]{};
    if(!Read(unit,header)||header[0]!=1||!Read(tick,localTick)||
       !Read(Get<uintptr_t>(localTick,0x28),row)||Get<int16_t>(row,0x52)!=108) return false;
    // aip4 = acquisition/pursuit distance, aip5 = owner distance. Spirit
    // Wolf aip3 is follow distance; Dire Wolf aip3 is corpse-eating chance!
    ScaleParam(row,3,aggro);ScaleParam(row,4,leash);
    if(header[1]==420) ScaleParam(row,2,leash);
    Put(localTick,0x28,reinterpret_cast<uintptr_t>(row.data()));
    return true;
}
inline void Wolf(uintptr_t game,uintptr_t unit,uintptr_t tick) {
    if(!ready.load(std::memory_order_acquire)) {originalWolf(game,unit,tick);return;}
    Restore restore;scope={game,unit};
    std::array<uint8_t,64> localTick{};
    std::array<uint8_t,508> row{};
    if(PrepareWolf(unit,tick,localTick,row)) {
        wolfCalls.fetch_add(1,std::memory_order_relaxed);
        originalWolf(game,unit,reinterpret_cast<uintptr_t>(localTick.data()));
    } else originalWolf(game,unit,tick);
}
inline void Bear(uintptr_t game,uintptr_t unit,uintptr_t tick) {
    if(!ready.load(std::memory_order_acquire)) {originalBear(game,unit,tick);return;}
    Restore restore;scope={game,unit};
    bearCalls.fetch_add(1,std::memory_order_relaxed);
    originalBear(game,unit,tick);
}
inline int AggroRange(int value,uintptr_t game,uintptr_t unit);
inline uintptr_t EnumerateAt(uintptr_t game,uintptr_t unit,uintptr_t query,uintptr_t callback,int kind,uintptr_t caller) {
    // Exact native friendly-target query, on the same synchronous AI thread.
    // Unscoped monsters, owner queries, and eligibility modes pass through.
    int range=0;
    if(ready.load(std::memory_order_acquire)&&aggro!=1&&scope.game==game&&scope.unit==unit&&
       kind==4&&caller==base+0x595d91&&Read(query+0x20,range)&&range==35) {
        const int expanded=AggroRange(range,game,unit);
        SIZE_T copied=0;
        // Native caller owns this writable stack query. Change its search cap,
        // never its actual target distance, combat flag, or collision checks.
        if(ReadProcessMemory(GetCurrentProcess(),&expanded,reinterpret_cast<void*>(query+0x20),sizeof expanded,&copied)&&copied==sizeof expanded) {
            searchCalls.fetch_add(1,std::memory_order_relaxed);
            return originalEnumerate(game,unit,query,callback,kind);
        }
    }
    return originalEnumerate(game,unit,query,callback,kind);
}
inline uintptr_t Enumerate(uintptr_t game,uintptr_t unit,uintptr_t query,uintptr_t callback,int kind) {
    return EnumerateAt(game,unit,query,callback,kind,reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

// The original comparisons use sign-extended imm8 operands. At 5x their
// thresholds exceed 127, so retain the complete CMP/Jcc semantics in tiny
// register-preserving detours instead of truncating or clamping the range.
struct RangeState {int threshold;int reserved;uintptr_t next,taken;};
static_assert(sizeof(RangeState)==24&&offsetof(RangeState,next)==8&&offsetof(RangeState,taken)==16);
extern "C" {inline RangeState qolSummonBranches[7]{};}
#define QOL_RANGE_STUB(name,reg,condition,offset) \
    __attribute__((naked,noinline)) inline void name(){ \
        __asm__ volatile("cmpl qolSummonBranches+" #offset "(%rip), %" #reg "\n" \
            #condition " 1f\n" \
            "jmp *qolSummonBranches+" #offset "+8(%rip)\n" \
            "1: jmp *qolSummonBranches+" #offset "+16(%rip)\n"); \
    }
QOL_RANGE_STUB(BearTeleport,r12d,jle,0)
QOL_RANGE_STUB(BearFollow,r12d,jle,24)
QOL_RANGE_STUB(BearMovingFollow,r12d,jle,48)
QOL_RANGE_STUB(BearTarget,eax,jge,72)
QOL_RANGE_STUB(BearIdleFollow,r12d,jle,96)
QOL_RANGE_STUB(SpiritTeleport,eax,jle,120)
QOL_RANGE_STUB(DireTeleport,r13d,jle,144)
#undef QOL_RANGE_STUB
struct RangeSite {uint64_t rva;std::array<uint8_t,10> bytes;uint32_t size;int native;bool owner;void(*detour)();};
constexpr RangeSite ranges[]={
    {0x5c96aa,{0x41,0x83,0xfc,0x32,0x7e,0x26},6,50,true,BearTeleport},
    {0x5c9703,{0x41,0x83,0xfc,0x1c,0x7e,0x27},6,28,true,BearFollow},
    {0x5c9730,{0x41,0x83,0xfc,0x12,0x7e,0x61},6,18,true,BearMovingFollow},
    {0x5c982c,{0x83,0xf8,0x1c,0x7d,0x14},5,28,false,BearTarget},
    {0x5c9957,{0x41,0x83,0xfc,0x10,0x7e,0x1d},6,16,true,BearIdleFollow},
    {0x5ca304,{0x83,0xf8,0x32,0x0f,0x8e,0xc8,0,0,0},9,50,true,SpiritTeleport},
    {0x5c9cca,{0x41,0x83,0xfd,0x32,0x0f,0x8e,0xc6,0,0,0},10,50,true,DireTeleport},
};
inline void* rangeOriginals[7]{}; // Loader-owned trampolines; stubs jump to verified native successors.
inline RangeState StateOf(const RangeSite& site,uintptr_t image,double factor) {
    int32_t relative=0;
    if(site.size<=6)relative=static_cast<int8_t>(site.bytes[site.size-1]);
    else std::memcpy(&relative,site.bytes.data()+site.size-4,sizeof relative);
    const uintptr_t next=image+site.rva+site.size;
    return {Scale(site.native,factor),0,next,uintptr_t(intptr_t(next)+relative)};
}
struct Patch {uint64_t rva;std::array<uint8_t,8> bytes;uint32_t size,immediate,width;bool owner;};
// Bear target search already takes a full 32-bit immediate.
constexpr Patch patches[]={
    {0x5c97e2,{0xc7,0x44,0x24,0x28,0x1c,0,0,0},8,4,4,false},
};
// MPL-2.0. Scoped player-pet adapters for D2R build 93847.
// Additional player-owned summon adapters.
namespace other {
using OwnerFn=uintptr_t(*)(uintptr_t);
using DistanceFn=int(*)(uintptr_t,uintptr_t);
using TargetFn=uintptr_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uint8_t);
using RangeFn=uintptr_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,int,uint8_t);
using SelectFn=uintptr_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
inline OwnerFn owner=nullptr;
inline DistanceFn fullDistance=nullptr,unitDistance=nullptr;
inline TargetFn target=nullptr;
inline RangeFn range=nullptr;
inline SelectFn select=nullptr;
struct Context {uintptr_t game=0,unit=0,owner=0;bool squaredAggro=false;};
inline thread_local Context context;
struct RestoreContext {Context saved=context;~RestoreContext(){context=saved;}};
inline bool Supported(int ai) {return ai==67||ai==105||ai==106||ai==107||ai==109||ai==110||ai==111||ai==143;}
inline bool Enter(uintptr_t game,uintptr_t unit,bool necro=false) {
    if(ready.load(std::memory_order_acquire)&&context.game==game&&context.unit==unit&&context.owner)return true;
    uint32_t h[3]{},master[3]{};uintptr_t back=0,data=0,row=0;int16_t ai=0;
    if(!ready.load(std::memory_order_acquire)||!owner||!Read(unit,h)||h[0]!=1||
       !Read(unit+0xd8,back)||back!=game||!Read(unit+0x10,data)||!Read(data,row)||!Read(row+0x52,ai)||
       ai==61||(!necro&&!Supported(ai)))return false;
    const uintptr_t parent=owner(unit);
    if(!Read(parent,master)||master[0]!=0||!Read(parent+0xd8,back)||back!=game)return false;
    // Dedicated summoned skeleton/mage and Clay/Blood/Iron/Fire Golem rows.
    // Keep Valkyrie, revives, other pets and mercenaries on their existing curve.
    const bool squared=ai==67&&((h[1]>=289&&h[1]<=292)||h[1]==363||h[1]==364);
    context={game,unit,parent,squared};scope={game,unit};return true;
}
inline int OwnerDistance(int native,uintptr_t a,uintptr_t b) {
    // Only owner separation is transformed. Target distance/combat eligibility
    // must remain native. Round upward so a positive separation never becomes 0.
    if(native<=0||leash==1||!ready.load(std::memory_order_acquire)||
       !context.unit||!((a==context.unit&&b==context.owner)||(b==context.unit&&a==context.owner)))return native;
    return int(std::ceil(native/leash));
}
inline int FullDistance(uintptr_t a,uintptr_t b){return OwnerDistance(fullDistance(a,b),a,b);}
inline int UnitDistance(uintptr_t a,uintptr_t b){return OwnerDistance(unitDistance(a,b),a,b);}
inline uintptr_t Target(uintptr_t game,uintptr_t unit,uintptr_t control,uintptr_t distance,uintptr_t combat,uint8_t flags) {
    Restore restore;RestoreContext saved;Enter(game,unit);
    return target(game,unit,control,distance,combat,flags);
}
inline uintptr_t Select(uintptr_t game,uintptr_t unit,uintptr_t distance,uintptr_t combat) {
    Restore restore;RestoreContext saved;Enter(game,unit);
    return select(game,unit,distance,combat);
}
inline uintptr_t Range(uintptr_t game,uintptr_t unit,uintptr_t control,uintptr_t distance,uintptr_t combat,int cap,uint8_t flags) {
    Restore restore;RestoreContext saved;
    if(Enter(game,unit)||(context.game==game&&context.unit==unit))cap=AggroRange(cap,game,unit);
    return range(game,unit,control,distance,combat,cap,flags);
}
inline void PrepareParameters(int ai,std::array<uint8_t,508>& row) {
    if(ai==105)ScaleParam(row,0,aggro); // Shadow Warrior target pursuit only.
    if(ai==107)ScaleParam(row,4,aggro); // Raven acquisition, not bCombat.
    if(ai==110||ai==111)ScaleParam(row,1,aggro); // Vine target/corpse search.
    if(ai==106||ai==143) {
        // Shadow Master reuses the three difficulty slots for unrelated values:
        // random selection, ignore-range chance, and squared owner-leash input.
        Put(row,0xa0,int16_t(Scale(Get<int16_t>(row,0xa0),leash)));
    }
}
inline ThinkFn originals[7]{};
inline void Think(unsigned index,uintptr_t game,uintptr_t unit,uintptr_t tick) {
    Restore restore;RestoreContext saved;
    std::array<uint8_t,64> local{};std::array<uint8_t,508> row{};
    if(Enter(game,unit,index==0)&&Read(tick,local)&&Read(Get<uintptr_t>(local,0x28),row)) {
        PrepareParameters(Get<int16_t>(row,0x52),row);
        Put(local,0x28,reinterpret_cast<uintptr_t>(row.data()));
        originals[index](game,unit,reinterpret_cast<uintptr_t>(local.data()));
    }else originals[index](game,unit,tick);
}
#define QOL_PET_THINK(name,index) inline void name(uintptr_t g,uintptr_t u,uintptr_t t){Think(index,g,u,t);}
QOL_PET_THINK(Necro,0) QOL_PET_THINK(Warrior,1) QOL_PET_THINK(Master,2)
QOL_PET_THINK(Raven,3) QOL_PET_THINK(Spirit,4) QOL_PET_THINK(Vine,5) QOL_PET_THINK(CorpseVine,6)
#undef QOL_PET_THINK
struct HookSite {uint64_t rva;std::array<uint8_t,16> bytes;uint32_t size;void* detour;void** original;};
inline const HookSite sites[]={
    {0x5c1400,{0x48,0x89,0x5c,0x24,8},5,(void*)&Necro,(void**)&originals[0]},
    {0x5ceea0,{0x48,0x89,0x5c,0x24,8},5,(void*)&Warrior,(void**)&originals[1]},
    {0x5cd740,{0x48,0x89,0x5c,0x24,0x20},5,(void*)&Master,(void**)&originals[2]},
    {0x5cca50,{0x40,0x53,0x56,0x57,0x41,0x54},6,(void*)&Raven,(void**)&originals[3]},
    {0x5cf810,{0x40,0x55,0x56,0x57,0x41,0x55},6,(void*)&Spirit,(void**)&originals[4]},
    {0x5d0630,{0x40,0x53,0x55,0x56,0x57},5,(void*)&Vine,(void**)&originals[5]},
    {0x5c83c0,{0x40,0x53,0x55,0x56,0x57},5,(void*)&CorpseVine,(void**)&originals[6]},
    {0x596720,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18},10,(void*)&FullDistance,(void**)&fullDistance},
    {0x324fc0,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18},10,(void*)&UnitDistance,(void**)&unitDistance},
    {0x595750,{0x40,0x55,0x53,0x56,0x57,0x41,0x57},7,(void*)&Target,(void**)&target},
    {0x595f80,{0x4c,0x8b,0xdc,0x53,0x41,0x54},6,(void*)&Select,(void**)&select},
    {0x4a6720,{0x40,0x53,0x48,0x83,0xec,0x30},6,(void*)&Range,(void**)&range},
};
// Necro melee/ranged pursuit gates (also used by Valkyrie/revive pet AI).
// These are acquisition choices, never native attack/combat-range calculations.
extern "C" {
inline int qolPetThresholds[4]={6,15,20,36};
inline uintptr_t qolPetNext[4]{};
__attribute__((noinline,used)) inline int QolPetThreshold(unsigned index,uintptr_t unit) {
    constexpr int native[]={6,15,20,36};
    if(!ready.load(std::memory_order_acquire)||context.unit!=unit||!context.owner)return native[index];
    return context.squaredAggro?AggroRange(native[index],context.game,unit):qolPetThresholds[index];
}
}
// Preserve every volatile register and XMM argument across the scope lookup.
// Native mid-function RSP is 16-byte aligned at all four verified sites.
#define QOL_PET_GATE(name,index,operand,replay,nextOffset) \
__attribute__((naked,noinline)) inline void name() { \
 __asm__ volatile( \
 "push %rax\npush %rcx\npush %rdx\npush %r8\npush %r9\npush %r10\npush %r11\nsub $136,%rsp\n" \
 "movdqu %xmm0,32(%rsp)\nmovdqu %xmm1,48(%rsp)\nmovdqu %xmm2,64(%rsp)\n" \
 "movdqu %xmm3,80(%rsp)\nmovdqu %xmm4,96(%rsp)\nmovdqu %xmm5,112(%rsp)\n" \
 "mov $" #index ",%ecx\nmov %rbx,%rdx\ncall QolPetThreshold\ncmpl %eax," operand "\n" \
 "movdqu 32(%rsp),%xmm0\nmovdqu 48(%rsp),%xmm1\nmovdqu 64(%rsp),%xmm2\n" \
 "movdqu 80(%rsp),%xmm3\nmovdqu 96(%rsp),%xmm4\nmovdqu 112(%rsp),%xmm5\n" \
 "lea 136(%rsp),%rsp\npop %r11\npop %r10\npop %r9\npop %r8\npop %rdx\npop %rcx\npop %rax\n" \
 replay "jmp *qolPetNext+" #nextOffset "(%rip)\n"); \
}
QOL_PET_GATE(NecroNearby,0,"256(%rsp)","",0)
QOL_PET_GATE(NecroRanged,1,"352(%rsp)","",8)
QOL_PET_GATE(NecroRangedOwnerTarget,2,"184(%rsp)","mov %eax,0xa0(%rsp)\n",16)
QOL_PET_GATE(NecroTargetGate,3,"184(%rsp)","mov %eax,0x40(%rsp)\n",24)
#undef QOL_PET_GATE
struct Gate {uint64_t rva;std::array<uint8_t,10> bytes;uint32_t size,immediate;void(*detour)();};
inline constexpr Gate gates[]={
    {0x5c123c,{0x83,0x7c,0x24,0x40,6},5,4,NecroNearby},
    {0x5c1c4c,{0x83,0xbc,0x24,0xa0,0,0,0,15},8,7,NecroRanged},
    {0x5c1c7e,{0x83,0xf8,20,0x89,0x84,0x24,0xa0,0,0,0},10,2,NecroRangedOwnerTarget},
    {0x5c1256,{0x83,0xf8,0x24,0x89,0x44,0x24,0x40},7,2,NecroTargetGate},
};
inline void* gateOriginals[4]{};
inline constexpr uint8_t ownerBytes[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0x02,0x66,0xea,0xff,0x83,0xf8,0x01};
struct Layout {uint64_t rva;std::array<uint8_t,9> bytes;uint32_t size;};
inline constexpr Layout layouts[]={
    {0x5cef77,{0x0f,0xbf,0x8c,0x75,0x90,0,0,0},8},
    {0x5cda24,{0x41,0x0f,0xbf,0x86,0xa0,0,0,0},8},
    {0x5cccf6,{0x41,0x0f,0xbf,0x94,0x4f,0xa8,0,0,0},9},
    {0x5d0762,{0x42,0x0f,0xbf,0x84,0x76,0x96,0,0,0},9},
};
inline bool Check(const D2RL::PluginContext* ctx) {
    constexpr uint8_t ownerLayout[]={0x48,0x8b,0x4a,0x60};
    bool valid=ctx->CheckExpectedBytes(0x4a53c0,ownerBytes,sizeof ownerBytes)&&
        ctx->CheckExpectedBytes(0x4a545b,ownerLayout,sizeof ownerLayout);
    for(const auto& s:sites)if(!ctx->CheckExpectedBytes(s.rva,s.bytes.data(),s.size))valid=false;
    for(const auto& g:gates)if(!ctx->CheckExpectedBytes(g.rva,g.bytes.data(),g.size))valid=false;
    for(const auto& l:layouts)if(!ctx->CheckExpectedBytes(l.rva,l.bytes.data(),l.size))valid=false;
    return valid;
}
inline bool Install(const D2RL::PluginContext* ctx) {
    owner=reinterpret_cast<OwnerFn>(ctx->exeBase+0x4a53c0);
    for(const auto& s:sites) {
        if(leash==1&&(s.rva==0x596720||s.rva==0x324fc0))continue;
        if(!ctx->InstallInlineHook(s.rva,s.bytes.data(),s.size,s.detour,s.original))return false;
    }
    for(size_t i=0;i<std::size(gates);++i) {
        const auto& g=gates[i];qolPetThresholds[i]=Scale(g.bytes[g.immediate],aggro);
        qolPetNext[i]=ctx->exeBase+g.rva+g.size;
        if(!ctx->InstallInlineHook(g.rva,g.bytes.data(),g.size,(void*)g.detour,&gateOriginals[i]))return false;
    }
    return true;
}
}
inline int AggroRange(int value,uintptr_t game,uintptr_t unit) {
    if(other::context.game==game&&other::context.unit==unit&&other::context.owner&&
       other::context.squaredAggro&&std::isfinite(aggro)&&aggro>=1&&aggro<=5)
        return Scale(value,aggro*aggro,32767,25);
    return Scale(value,aggro);
}

enum class InstallResult {Unavailable,Ready,Failed};
inline InstallResult Install(const D2RL::PluginContext* ctx) {
    ready.store(false,std::memory_order_release);base=ctx->exeBase;
    if(aggro==1&&leash==1) return InstallResult::Unavailable;
    bool valid=true;
    const auto check=[&](uint64_t rva,const uint8_t* bytes,uint32_t size) {
        if(ctx->CheckExpectedBytes(rva,bytes,size)) return;
        char message[180];std::snprintf(message,sizeof message,
            "Druid summons: signature mismatch at 0x%llx; summon changes disabled.",static_cast<unsigned long long>(rva));
        ctx->LogWarn(message);valid=false;
    };
    check(WolfRva,WolfSig,sizeof WolfSig);check(BearRva,BearSig,sizeof BearSig);
    check(EnumerateRva,EnumerateSig,sizeof EnumerateSig);
    // Verify local MonStats loads and the scoped friendly query's stack layout.
    constexpr uint8_t spirit[]={0x43,0x0f,0xbf,0x84,0x67,0xa2,0,0,0};
    constexpr uint8_t dire[]={0x45,0x0f,0xbf,0xac,0x6f,0xa2,0,0,0};
    constexpr uint8_t query[]={0x44,0x89,0x75,0xd0};
    constexpr uint8_t call[]={0xe8,0xaf,0x05,0,0};
    constexpr uint8_t callbackRange[]={0x3b,0x46,0x20};
    constexpr uint8_t wolfClass[]={0x3d,0xa4,0x01,0,0};
    check(0x5ca221,spirit,sizeof spirit);check(0x5c9ba4,dire,sizeof dire);
    check(0x595d76,query,sizeof query);check(0x595d8c,call,sizeof call);
    check(0x597e3d,callbackRange,sizeof callbackRange);check(0x5ca646,wolfClass,sizeof wolfClass);
    for(const auto& site:ranges) check(site.rva,site.bytes.data(),site.size);
    for(const auto& patch:patches) check(patch.rva,patch.bytes.data(),patch.size);
    if(!other::Check(ctx)){
        ctx->LogWarn("Summons: additional pet AI signatures unavailable; shared ranges disabled.");valid=false;
    }
    if(!valid) return InstallResult::Unavailable;
    // A failure after the first mutation fails plugin loading so the loader
    // rolls back EVERY owned patch/hook. Never retain a partial summon update.
    if(!ctx->InstallInlineHook(WolfRva,WolfSig,sizeof WolfSig,reinterpret_cast<void*>(&Wolf),reinterpret_cast<void**>(&originalWolf))||
       !ctx->InstallInlineHook(BearRva,BearSig,sizeof BearSig,reinterpret_cast<void*>(&Bear),reinterpret_cast<void**>(&originalBear))||
       !ctx->InstallInlineHook(EnumerateRva,EnumerateSig,sizeof EnumerateSig,reinterpret_cast<void*>(&Enumerate),reinterpret_cast<void**>(&originalEnumerate))) return InstallResult::Failed;
    for(size_t i=0;i<std::size(ranges);++i) {
        const auto& site=ranges[i];
        qolSummonBranches[i]=StateOf(site,base,site.owner?leash:aggro);
        if(!ctx->InstallInlineHook(site.rva,site.bytes.data(),site.size,reinterpret_cast<void*>(site.detour),&rangeOriginals[i]))return InstallResult::Failed;
    }
    for(const auto& patch:patches) {
        auto bytes=patch.bytes;
        const int value=Scale(bytes[patch.immediate],patch.owner?leash:aggro,32767);
        if(patch.width==1) bytes[patch.immediate]=static_cast<uint8_t>(value);
        else std::memcpy(bytes.data()+patch.immediate,&value,sizeof value);
        if(bytes!=patch.bytes&&!ctx->PatchBytes(patch.rva,patch.bytes.data(),patch.size,bytes.data(),patch.size)) return InstallResult::Failed;
    }
    if(!other::Install(ctx))return InstallResult::Failed;
    ready.store(true,std::memory_order_release);
    ctx->LogInfo("Shared summon aggression/leash enabled. Player-owned summons only; mercenaries and native attack reach retained. Offline gameplay test required.");
    return InstallResult::Ready;
}
}
