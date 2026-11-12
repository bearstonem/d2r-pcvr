#include <windows.h>
#include <intrin.h>
#include <D2RLPlugin/api.h>
#include "../vr/summon_range_native.h"
#include <thread>
#include <limits>
#include <cstdlib>
#include <vector>
using namespace framevr::summonrange;
namespace {
int RangeTaken(){return 17;}
int RangeNext(){return 23;}
// Execute the actual mid-function stubs against owned destinations. Preserve
// Win64 nonvolatile registers and provide the normal shadow space for the call.
__attribute__((naked,noinline)) int ProbeR12(int,void(*)()) {
    __asm__ volatile("push %r12\nmov %ecx,%r12d\nsub $32,%rsp\ncall *%rdx\nadd $32,%rsp\npop %r12\nret");
}
__attribute__((naked,noinline)) int ProbeR13(int,void(*)()) {
    __asm__ volatile("push %r13\nmov %ecx,%r13d\nsub $32,%rsp\ncall *%rdx\nadd $32,%rsp\npop %r13\nret");
}
__attribute__((naked,noinline)) int ProbeRax(int,void(*)()) {
    __asm__ volatile("mov %ecx,%eax\nsub $40,%rsp\ncall *%rdx\nadd $40,%rsp\nret");
}
uintptr_t petOwner=0;
uintptr_t PetOwner(uintptr_t){return petOwner;}
int capturedCap=0;
uintptr_t PetRange(uintptr_t,uintptr_t,uintptr_t,uintptr_t distance,uintptr_t combat,int cap,uint8_t flags) {
    capturedCap=cap;*reinterpret_cast<int*>(distance)=37;*reinterpret_cast<int*>(combat)=0;return flags+100;
}
__attribute__((naked,noinline)) int PetComparisonResult(){__asm__ volatile("setl %al\nmovzbl %al,%eax\nret");}
__attribute__((naked,noinline)) int PetLessEqualResult(){__asm__ volatile("setle %al\nmovzbl %al,%eax\nret");}
__attribute__((naked,noinline)) int ProbePetGate(int,void(*)(),uintptr_t) {
    // Simulate the native mid-function stack alignment, with room for replayed locals.
    __asm__ volatile("push %rbx\nmov %r8,%rbx\nmov %ecx,%eax\nsub $184,%rsp\nmov %eax,56(%rsp)\nmov %eax,152(%rsp)\ncall *%rdx\nadd $184,%rsp\npop %rbx\nret");
}
unsigned checks=0,hooks=0,patchCount=0,nativeCalls=0;
uint64_t mismatch=0,failHook=0,failPatch=0;
int expectedRange=35;
uintptr_t nativeTick=0;
bool cloneExpected=false;
std::vector<uint64_t> checked;
void Check(bool ok,const char* message) {
    ++checks;if(!ok){std::fprintf(stderr,"FAILED: %s\n",message);std::exit(1);}
}
void __cdecl Log(const D2RL::PluginContext*,const char*) noexcept {}
void Think(uintptr_t game,uintptr_t unit,uintptr_t tick) {
    ++nativeCalls;
    Check(native::scope.game==game&&native::scope.unit==unit,"native AI has matching thread scope");
    Check((tick!=nativeTick)==cloneExpected,"only valid wolf AI receives local tick copy");
    if(cloneExpected) {
        std::array<uint8_t,64> local{};std::array<uint8_t,508> row{};
        Check(native::Read(tick,local)&&native::Read(native::Get<uintptr_t>(local,0x28),row),"native AI can read local row");
        Check(native::Get<int16_t>(row,0xa2)==40,"native wolf AI receives expanded aggroMultiplier");
    }
}
uintptr_t Enumerate(uintptr_t,uintptr_t,uintptr_t query,uintptr_t,int) {
    ++nativeCalls;
    int range=0;Check(native::Read(query+0x20,range)&&range==expectedRange,"native search receives correct cap");
    return 123;
}
bool __cdecl Expected(const D2RL::PluginContext*,uint64_t rva,const void*,uint32_t) noexcept {
    checked.push_back(rva);return rva!=mismatch;
}
bool __cdecl Hook(const D2RL::PluginContext*,const D2RL::InlineHookRegistration* hook) noexcept {
    ++hooks;if(hook->rva==failHook)return false;
    if(hook->rva==native::WolfRva||hook->rva==native::BearRva)*hook->original=reinterpret_cast<void*>(&Think);
    else if(hook->rva==native::EnumerateRva)*hook->original=reinterpret_cast<void*>(&Enumerate);
    else {
        for(const auto& site:native::ranges)if(hook->rva==site.rva) {
            Check(hook->expectedSize==site.size&&std::memcmp(hook->expected,site.bytes.data(),site.size)==0,"range hook guards whole CMP/Jcc instructions");
            *hook->original=nullptr;return true;
        }
        for(const auto& site:native::other::sites)if(hook->rva==site.rva) {
            Check(hook->expectedSize==site.size&&std::memcmp(hook->expected,site.bytes.data(),site.size)==0,"other summon hook guards original instructions");
            *hook->original=nullptr;return true;
        }
        for(const auto& g:native::other::gates)if(hook->rva==g.rva) {
            Check(hook->expectedSize==g.size&&std::memcmp(hook->expected,g.bytes.data(),g.size)==0,"scoped pet gate guards complete original instructions");
            *hook->original=nullptr;return true;
        }
        return false;
    }
    return true;
}
bool __cdecl Patch(const D2RL::PluginContext*,uint64_t rva,const void* original,uint32_t n,const void* replacement,uint32_t m) noexcept {
    ++patchCount;
    Check(n==m,"patch preserves instruction length");
    for(const auto& p:native::patches) if(p.rva==rva) {
        const auto* bytes=static_cast<const uint8_t*>(replacement);
        Check(n==p.size&&std::memcmp(original,p.bytes.data(),n)==0,"patch uses verified complete original instruction");
        int actual=0;
        if(p.width==1)actual=static_cast<int8_t>(bytes[p.immediate]);
        else std::memcpy(&actual,bytes+p.immediate,4);
        const int value=native::Scale(p.bytes[p.immediate],p.owner?native::leashMultiplier:native::aggroMultiplier,32767);
        Check(actual==value,"distance immediate scaled with correct signed encoding");
        for(unsigned i=0;i<n;++i) if(i<p.immediate||i>=p.immediate+p.width)
            Check(bytes[i]==p.bytes[i],"patch leaves opcodes and operands intact");
        return rva!=failPatch;
    }
    return false;
}
void OtherSummonChecks() {
    using namespace native;
    struct Buffer {std::array<uint8_t,512> bytes{};} pet,master;
    std::array<uint8_t,508> row{};
    const auto u=reinterpret_cast<uintptr_t>(pet.bytes.data()),o=reinterpret_cast<uintptr_t>(master.bytes.data());
    const uintptr_t game=0x20000,rowAddress=reinterpret_cast<uintptr_t>(row.data());
    const uintptr_t data=reinterpret_cast<uintptr_t>(&rowAddress);
    Put(pet.bytes,0,uint32_t(1));Put(pet.bytes,0x10,data);Put(pet.bytes,0xd8,game);
    Put(master.bytes,0,uint32_t(0));Put(master.bytes,0xd8,game);
    ready=true;aggroMultiplier=5;leashMultiplier=3;other::owner=PetOwner;petOwner=o;
    for(int ai:{67,105,106,107,109,110,111,143}) {
        Put(row,0x52,int16_t(ai));scope={};other::context={};
        Check(other::Enter(game,u)&&other::context.owner==o&&scope.unit==u,"supported player summon establishes scoped owner identity");
    }
    Put(row,0x52,int16_t(61));other::context={};scope={};
    Check(!other::Enter(game,u)&&!other::Enter(game,u,true),"mercenaries excluded even from special pet AI");
    Put(row,0x52,int16_t(2));
    Check(!other::Enter(game,u)&&other::Enter(game,u,true),"revived monster uses explicit Necro AI scope only");
    Put(master.bytes,0,uint32_t(1));other::context={};
    Check(!other::Enter(game,u,true),"monster-owned summons excluded");Put(master.bytes,0,uint32_t(0));
    petOwner=0;Check(!other::Enter(game,u,true),"missing pet owner fails closed");petOwner=o;
    Check(!other::Enter(game+1,u,true),"wrong server game rejected");
    Put(row,0x52,int16_t(67));other::Enter(game,u);
    Check(other::OwnerDistance(150,u,o)==50,"owner leashMultiplier uses shared factor");
    Check(other::OwnerDistance(1,u,o)==1&&other::OwnerDistance(0,u,o)==0,"owner proximity and zero distance preserved");
    Check(other::OwnerDistance(150,u,u+1)==150,"enemy target distance never scaled");
    Check(other::OwnerDistance(150,u+1,o)==150,"another unit's distance never scaled");
    std::thread foreign([&]{Check(other::OwnerDistance(150,u,o)==150,"pet scope cannot leak onto another thread");});foreign.join();
    int distance=0,combat=1;other::range=PetRange;
    Check(other::Range(game,u,0,reinterpret_cast<uintptr_t>(&distance),reinterpret_cast<uintptr_t>(&combat),24,7)==107&&capturedCap==120,
          "shared aggression expands native search limit to 5x and preserves return value");
    Check(distance==37&&combat==0,"search keeps actual target distance and native combat eligibility");
    other::context={};scope={};Put(row,0x52,int16_t(61));
    other::Range(game,u,0,reinterpret_cast<uintptr_t>(&distance),reinterpret_cast<uintptr_t>(&combat),24,0);
    Check(capturedCap==24,"mercenary search limit remains native");
    for(unsigned classId:{289u,290u,291u,292u,363u,364u,357u,420u,0u}) {
        Put(pet.bytes,4,classId);Put(row,0x52,int16_t(67));
        const bool squared=(classId>=289&&classId<=292)||classId==363||classId==364;
        for(double factor:{1.,1.5,4.,5.}) {
            aggroMultiplier=factor;other::context={};scope={};
            Check(other::Enter(game,u),"skeleton/golem test enters native AI owner scope");
            other::Range(game,u,0,reinterpret_cast<uintptr_t>(&distance),reinterpret_cast<uintptr_t>(&combat),24,0);
            const double effective=squared?factor*factor:factor;
            Check(capturedCap==int(std::round(24*effective)),"only skeletons and golems square the search multiplier, including fractional settings");
            Check(distance==37&&combat==0&&other::OwnerDistance(150,u,o)==50,"squared aggression preserves target distance, combat and leashMultiplier");
            for(size_t i=0;i<std::size(other::gates);++i) {
                const auto& gate=other::gates[i];const int native=gate.bytes[gate.immediate];
                other::summonPetThresholds[i]=Scale(native,factor);
                const int threshold=int(std::round(native*effective));
                other::summonPetNext[i]=reinterpret_cast<uintptr_t>(i<2?&PetLessEqualResult:&PetComparisonResult);
                for(int value:{threshold-1,threshold,threshold+1})
                    Check(ProbePetGate(value,gate.detour,u)==int(i<2?value<=threshold:value<threshold),"actual skeleton/golem pursuit gates use squared range with native comparison semantics");
            }
            Check(AggroRange(35,game,u)==int(std::round(35*effective)),"pet-centered acquisition cap uses the same aggression curve");
            Check(AggroRange(35,game,o)==Scale(35,factor),"owner-centered fallback does not inherit squared pet range");
        }
    }
    aggroMultiplier=5;Put(pet.bytes,4,uint32_t(0));other::context={};scope={};
    for(int ai:{67,105,106,107,109,110,111,143}) {
        std::array<uint8_t,508> baseline{};baseline.fill(0x17);
        for(size_t param=0;param<8;++param)for(size_t difficulty=0;difficulty<3;++difficulty)Put(baseline,0x90+param*6+difficulty*2,int16_t(10));
        auto actual=baseline,expected=baseline;other::PrepareParameters(ai,actual);
        const int parameter=ai==105?0:ai==107?4:(ai==110||ai==111)?1:-1;
        if(parameter>=0)for(size_t difficulty=0;difficulty<3;++difficulty)Put(expected,0x90+parameter*6+difficulty*2,int16_t(50));
        if(ai==106||ai==143)Put(expected,0xa0,int16_t(30));
        Check(actual==expected,"pet parameter copy preserves all unrelated attack, skill, chance and difficulty values");
    }
    for(size_t i=0;i<std::size(other::gates);++i) {
        const auto& gate=other::gates[i];const int native=gate.bytes[gate.immediate],expanded=Scale(native,5);
        other::summonPetThresholds[i]=expanded;
        other::summonPetNext[i]=reinterpret_cast<uintptr_t>(i<2?&PetLessEqualResult:&PetComparisonResult);
        other::context={};
        Check(ProbePetGate(native+1,gate.detour,u)==0,"unscoped pet helpers retain native pursuit limits");
        other::context={game,u,o};
        for(int value:{native,expanded-1,expanded,expanded+1,INT_MAX})
            Check(ProbePetGate(value,gate.detour,u)==int(i<2?value<=expanded:value<expanded),
                  "actual scoped gate preserves strict/inclusive flags and replayed stack operands at 5x");
    }
    other::context={};scope={};aggroMultiplier=leashMultiplier=1;ready=false;
}
}
int main() {
    const D2RL::PluginApi api{.apiSize=D2RL::PluginApiSize,.logInfo=Log,.logWarn=Log,.logError=Log,
        .patchBytes=Patch,.checkExpectedBytes=Expected,.installInlineHook=Hook};
    const D2RL::PluginContext ctx{.contextSize=D2RL::PluginContextSize,.abiVersion=D2RL_PLUGIN_ABI_VERSION,.api=&api,.exeBase=0x140000000};
    native::aggroMultiplier=1;native::leashMultiplier=1;
    Check(native::Install(&ctx)==native::InstallResult::Unavailable&&hooks==0&&patchCount==0,"neutral defaults install no summon hooks or patches");
    native::aggroMultiplier=2;native::leashMultiplier=2;
    Check(native::Install(&ctx)==native::InstallResult::Ready&&hooks==26&&patchCount==1,"all shared summon hooks and range patches install");
    const auto guards=checked;
    for(const auto address:guards) {
        mismatch=address;hooks=patchCount=0;
        Check(native::Install(&ctx)==native::InstallResult::Unavailable&&!native::ready.load()&&hooks==0&&patchCount==0,"every signature mismatch prevents all summon mutations");
    }
    mismatch=0;failHook=native::BearRva;hooks=patchCount=0;
    Check(native::Install(&ctx)==native::InstallResult::Failed&&!native::ready.load()&&patchCount==0,"partial hook failure requires loader rollback");
    failHook=0;failPatch=native::patches[0].rva;
    Check(native::Install(&ctx)==native::InstallResult::Failed&&!native::ready.load(),"partial patch failure requires loader rollback");
    failPatch=0;native::ready=true;
    Check(native::Scale(50,5)==250&&native::Scale(28,5)==140,"5x distances exceed imm8 without truncation");
    for(size_t i=0;i<std::size(native::ranges);++i) {
        const auto& site=native::ranges[i];
        const auto state=native::StateOf(site,ctx.exeBase,5);
        Check(state.next==ctx.exeBase+site.rva+site.size&&state.taken>state.next,"range destinations follow verified forward native branches");
        auto& live=native::summonRangeBranches[i];live=state;
        live.taken=reinterpret_cast<uintptr_t>(&RangeTaken);live.next=reinterpret_cast<uintptr_t>(&RangeNext);
        for(int distance:{INT_MIN,-1,0,127,128,state.threshold-1,state.threshold,state.threshold+1,INT_MAX}) {
            const int result=(i==3||i==5)?ProbeRax(distance,site.detour):i==6?ProbeR13(distance,site.detour):ProbeR12(distance,site.detour);
            const bool taken=i==3?distance>=state.threshold:distance<=state.threshold;
            Check(result==(taken?17:23),"actual native comparison detour preserves signed branches at 5x");
        }
    }
    native::aggroMultiplier=5;native::leashMultiplier=5;
    Check(native::Install(&ctx)==native::InstallResult::Ready,"full 5x range installation accepted");
    native::aggroMultiplier=2;native::leashMultiplier=2;
    Check(native::Scale(0,2)==0&&native::Scale(-1,2)==-1,"nonpositive sentinel parameters preserved");
    Check(native::Scale(10,0)==10&&native::Scale(10,NAN)==10,"invalid factors fail closed");
    std::array<uint8_t,508> source{},row{};
    std::array<uint8_t,64> tick{},local{};
    uint32_t unit[3]={1,420,7};
    native::Put(source,0x52,int16_t(108));
    for(size_t i=0;i<24;++i)native::Put(source,0x90+i*2,int16_t(20+i%3));
    native::Put(tick,0x28,reinterpret_cast<uintptr_t>(source.data()));
    const auto savedTick=tick;
    const uintptr_t u=reinterpret_cast<uintptr_t>(unit),t=reinterpret_cast<uintptr_t>(tick.data());
    for(auto classId:{420u,421u}) {
        unit[1]=classId;
        Check(native::PrepareWolf(u,t,local,row),"both wolf variants prepare valid local data");
        for(size_t i=0;i<source.size();++i) {
            const bool scaled=(i>=0xa2&&i<0xae)||(classId==420&&i>=0x9c&&i<0xa2);
            if(!scaled)Check(row[i]==source[i],"all attack, skill, animation and unrelated table bytes unchanged");
        }
        Check(native::Get<int16_t>(row,0x9c)==(classId==420?40:20),"Dire Wolf corpse-eating chance is not mistaken for follow range");
        Check(tick==savedTick&&native::Get<int16_t>(source,0xa2)==20,"shared game table and caller tick stay unchanged");
    }
    native::aggroMultiplier=5;native::leashMultiplier=5;
    for(auto classId:{420u,421u}) {
        unit[1]=classId;Check(native::PrepareWolf(u,t,local,row),"5x wolf parameter copy prepared");
        Check(native::Get<int16_t>(row,0xa2)==100&&native::Get<int16_t>(row,0xa8)==100,"wolf search and owner distances reach full 5x");
        Check(native::Get<int16_t>(row,0x9c)==(classId==420?100:20),"5x preserves Dire Wolf corpse chance");
    }
    native::aggroMultiplier=2;native::leashMultiplier=2;
    nativeTick=t;cloneExpected=true;native::scope={11,22};
    native::Wolf(100,u,t);
    Check(native::scope.game==11&&native::scope.unit==22,"nested native AI scope restored");
    cloneExpected=false;native::Put(source,0x52,int16_t(112));
    native::Wolf(100,u,t);
    Check(!native::PrepareWolf(1,t,local,row),"invalid pointer rejected safely");
    native::Bear(100,u,t);
    std::array<uint8_t,64> query{};
    native::Put(query,0x20,35);native::Put(query,0x18,7); // actual measured distance
    const uintptr_t q=reinterpret_cast<uintptr_t>(query.data());
    native::scope={100,u};expectedRange=70;
    Check(native::EnumerateAt(100,u,q,0,4,ctx.exeBase+0x595d91)==123,"scoped query forwards return value");
    Check(native::Get<int>(query,0x18)==7,"actual distance remains exact for attack reach decisions");
    native::aggroMultiplier=5;native::Put(query,0x20,35);expectedRange=175;
    Check(native::EnumerateAt(100,u,q,0,4,ctx.exeBase+0x595d91)==123,"native shared search cap reaches full 5x");
    Check(native::Get<int>(query,0x18)==7,"5x search preserves measured attack distance");
    native::aggroMultiplier=2;
    for(int mode=0;mode<4;++mode) {
        native::Put(query,0x20,35);expectedRange=35;
        native::EnumerateAt(mode==0?101:100,mode==1?u+1:u,q,0,mode==2?5:4,ctx.exeBase+(mode==3?0x595d92:0x595d91));
    }
    native::ready=false;native::EnumerateAt(100,u,q,0,4,ctx.exeBase+0x595d91);
    OtherSummonChecks();
    std::printf("%u summon range checks passed.\n",checks);
}
