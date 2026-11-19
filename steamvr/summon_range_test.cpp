#include <windows.h>
#include <intrin.h>
#include <D2RLPlugin/api.h>
#include "../vr/summon_range_native.h"
#include <thread>
#include <limits>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <chrono>
#include <cmath>
#include <set>
#include <string>
using namespace framevr::summonrange;
namespace nl=framevr::summonrange::native::live;
// The target search stub (live::BearSearchStore), run for real. The next instruction is SearchNext, which records rax and the carry flag;
// ProbeSearch lays a sentinel in the stack slot the site writes ([site rsp+0x28], a call pushes 8) and in its neighbors.
extern "C" {
uint64_t probeRax=0;
uint8_t probeCarry=0;
int probeSlot=0,probeLow=0,probeHigh=0;
}
__attribute__((naked,noinline)) void SearchNext() {
    __asm__ volatile("mov %rax, probeRax(%rip)\nsetc probeCarry(%rip)\nret\n");
}
__attribute__((naked,noinline)) void ProbeSearch(void(*)()) {
    __asm__ volatile("push %rbx\nsub $0x50,%rsp\n"
                     "movl $0x7777,0x20(%rsp)\nmovl $0x5151,0x1c(%rsp)\nmovl $0x5252,0x24(%rsp)\n"
                     "movabs $0x1122334455667788,%rax\nstc\ncall *%rcx\n"
                     "mov 0x20(%rsp),%eax\nmov %eax,probeSlot(%rip)\n"
                     "mov 0x1c(%rsp),%eax\nmov %eax,probeLow(%rip)\n"
                     "mov 0x24(%rsp),%eax\nmov %eax,probeHigh(%rip)\n"
                     "add $0x50,%rsp\npop %rbx\nret\n");
}
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
uint64_t mismatch=0,failHook=0;
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
        for(const auto& p:native::patches)if(hook->rva==p.rva) {
            Check(hook->expectedSize==p.size&&std::memcmp(hook->expected,p.bytes.data(),p.size)==0&&hook->target==reinterpret_cast<void*>(&native::live::BearSearchStore),
                  "the target search hook guards the whole original instruction and runs the live stub");
            *hook->original=nullptr;return true;
        }
        return false;
    }
    return true;
}
// The adapter hooks the target search instead of patching its immediate: a byte patch would be a baked number again.
bool __cdecl Patch(const D2RL::PluginContext*,uint64_t,const void*,uint32_t,const void*,uint32_t) noexcept {
    ++patchCount;return false;
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
// A sink that records every write, for the order and the "never mixed" checks.
struct Recorder {
    std::vector<int> order;   // 0 branch, 1 gate, 2 search, 3 multipliers, 4 ready
    int branch[nl::Branches]{},gate[nl::Gates]{},search[nl::Searches]{};
    double aggro=1,leash=1;
    bool ready=false;
    void Branch(size_t i,int v){branch[i]=v;order.push_back(0);}
    void Gate(size_t i,int v){gate[i]=v;order.push_back(1);}
    void Search(size_t i,int v){search[i]=v;order.push_back(2);}
    void Multipliers(double a,double l){aggro=a;leash=l;order.push_back(3);}
    void Ready(bool on){ready=on;order.push_back(4);}
    bool Matches(double m)const {
        const nl::Cells c=nl::Compute(m,m);
        for(size_t i=0;i<nl::Branches;++i)if(branch[i]!=c.branch[i])return false;
        for(size_t i=0;i<nl::Gates;++i)if(gate[i]!=c.gate[i])return false;
        for(size_t i=0;i<nl::Searches;++i)if(search[i]!=c.search[i])return false;
        return aggro==m&&leash==m&&ready==(m>1);
    }
};
std::vector<std::string> logged;
int Logged(const char* part){int n=0;for(const auto& l:logged)if(l.find(part)!=std::string::npos)++n;return n;}
// Summon range applies live: what the tables hold at every step, the target search stub run for real, and the handoff between the
// settings thread (Post) and the AI thread (Drain).
void LiveChecks(const D2RL::PluginContext& ctx) {
    // At 100% every range site, gate and the search read the game's own number.
    {
        const nl::Cells one=nl::Compute(1,1);
        const int branches[7]={50,28,18,28,16,50,50},gates[4]={6,15,20,36};
        bool same=one.search[0]==28;
        for(int i=0;i<7;++i)same=same&&one.branch[i]==branches[i];
        for(int i=0;i<4;++i)same=same&&one.gate[i]==gates[i];
        Check(same,"at 100% every range site, gate and the search read the game's own number (50 28 18 28 16 50 50, 6 15 20 36, 28)");
        Check(nl::Branches==7&&nl::Gates==4&&nl::Searches==1&&nl::Sites==12,"twelve numbers: seven CMP stubs, four gates, one search");
        bool equal=true;   // every 10% step and every quarter step: upstream's own StateOf and Scale, site by site
        std::vector<double> steps;
        for(int tenths=0;tenths<=40;++tenths)steps.push_back(std::round((1+tenths/10.)*100)/100);
        for(int quarters=0;quarters<=16;++quarters)steps.push_back(1+quarters*.25);
        for(double m:steps) {
            const nl::Cells c=nl::Compute(m,m);
            for(size_t i=0;i<nl::Branches;++i)equal=equal&&native::StateOf(native::ranges[i],0,m).threshold==c.branch[i];
            for(size_t i=0;i<nl::Gates;++i)equal=equal&&native::Scale(native::other::gates[i].bytes[native::other::gates[i].immediate],m)==c.gate[i];
            equal=equal&&native::Scale(native::patches[0].bytes[native::patches[0].immediate],m,32767)==c.search[0];
        }
        Check(equal,"every step from 100% to 500%: the cells are upstream's own StateOf and Scale, site by site");
        Check(nl::Compute(3,3).search[0]==84&&nl::Compute(5,5).search[0]==140&&nl::Compute(3,3).branch[0]==150&&nl::Compute(3,3).gate[3]==108,
              "spot values: search 28 x 3 = 84, x 5 = 140; teleport 50 x 3 = 150; the last gate 36 x 3 = 108");
        const nl::Cells apart=nl::Compute(2,4);
        Check(apart.branch[0]==200&&apart.branch[3]==56&&apart.gate[0]==12&&apart.search[0]==56,"aggro and leash apart: owner sites follow the leash, target sites and gates the aggro");
    }
    // The handoff: nothing posted writes nothing; a post is applied once, by the next drain, in the safe order.
    {
        nl::Mailbox box;box.Arm(1);
        Recorder r;nl::Change ch;
        Check(!box.Drain(r,ch)&&r.order.empty(),"nothing posted: nothing written");
        double at=1;
        for(double m:{3.,1.,5.,1.,2.5,4.75,1.35}) {
            box.Post(m);
            Check(box.Drain(r,ch)&&ch.from==at&&ch.to==m,"the next tick drains it, naming old and new");
            Check(r.Matches(m),"every table, both multipliers and ready now say the new number");
            at=m;
            Check(!box.Drain(r,ch),"a second tick has nothing more to do");
        }
        nl::Mailbox b2;b2.Arm(1);Recorder up;
        b2.Post(3);b2.Drain(up,ch);
        Check(up.order.front()==0&&up.order[up.order.size()-2]==3&&up.order.back()==4,"above 100%: the tables, then the multipliers, then ready last");
        up.order.clear();b2.Post(1);b2.Drain(up,ch);
        Check(up.order.front()==4&&!up.ready&&up.order.back()==3,"back to 100%: ready drops first, the multipliers last");
        nl::Mailbox b3;b3.Arm(1);Recorder q;
        b3.Post(1);
        Check(!b3.Drain(q,ch)&&q.order.empty(),"100% posted at 100%: nothing");
        for(double bad:{0.,.5,5.01,6.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
            b3.Post(bad);
            Check(!b3.Drain(q,ch)&&q.order.empty(),"a value outside 1 to 5 or not a number is ignored");
        }
        b3.Post(3);b3.Drain(q,ch);b3.Post(3);q.order.clear();
        Check(!b3.Drain(q,ch)&&q.order.empty(),"300% posted again at 300%: nothing written");
        nl::Mailbox b4;b4.Arm(1);Recorder w;
        b4.Post(2);b4.Post(4);
        Check(b4.Drain(w,ch)&&ch.to==4&&w.Matches(4),"two posts between ticks: the latest is applied, the old one never half-written");
        nl::Mailbox b5;b5.Arm(3);Recorder s5;
        b5.Post(3);
        Check(!b5.Drain(s5,ch)&&s5.order.empty(),"armed at 300%: a post of 300% changes nothing");
        b5.Post(1);
        Check(b5.Drain(s5,ch)&&ch.from==3&&ch.to==1&&s5.Matches(1),"armed at 300%, then 100%: one change from 3 to 1");
        // not armed (before Install, after a failed install, after Shutdown): nothing is posted and nothing applies
        nl::Mailbox cold;Recorder c0;
        cold.Post(3);
        Check(!cold.Drain(c0,ch)&&c0.order.empty(),"never armed: a post applies nothing");
        cold.Arm(1);cold.Post(3);cold.Disarm();
        Check(!cold.Drain(c0,ch)&&c0.order.empty(),"disarmed with a post waiting: the drain writes nothing");
        cold.Post(5);
        cold.Arm(1);
        Check(!cold.Drain(c0,ch)&&c0.order.empty(),"a post from before the arm is not applied after it");
    }
    // The real tables, written and read back; the identity at 100%.
    {
        nl::RealSink sink;
        for(int tenths=0;tenths<=40;++tenths) {
            const double m=std::round((1+tenths/10.)*100)/100;
            nl::Write(sink,m,m);
            const nl::Cells c=nl::Compute(m,m);
            bool ok=true;
            for(size_t i=0;i<nl::Branches;++i)ok=ok&&native::summonRangeBranches[i].threshold==c.branch[i];
            for(size_t i=0;i<nl::Gates;++i)ok=ok&&native::other::summonPetThresholds[i]==c.gate[i];
            Check(ok&&nl::summonBearSearch==c.search[0]&&native::aggroMultiplier==m&&native::leashMultiplier==m&&native::ready.load()==(m>1),
                  "the real tables, the search cell, both multipliers and ready follow every 10% step");
        }
        nl::Write(sink,1,1);
        Check(!native::ready.load()&&nl::summonBearSearch==28&&native::other::summonPetThresholds[3]==36&&native::summonRangeBranches[0].threshold==50,
              "100% again: ready false, the game's own numbers");
    }
    // The target search stub: the hook stores the baked value, only that slot moves, flags and registers survive.
    {
        nl::summonBearSearchNext=reinterpret_cast<uintptr_t>(&SearchNext);
        nl::RealSink sink;
        for(int tenths=0;tenths<=40;++tenths) {
            const double m=std::round((1+tenths/10.)*100)/100;
            nl::Write(sink,m,m);
            ProbeSearch(&nl::BearSearchStore);
            Check(probeSlot==native::Scale(28,m,32767),"the stub stores what the baked immediate held, every step");
            Check(probeLow==0x5151&&probeHigh==0x5252,"the slots beside it are untouched");
            Check(probeRax==0x1122334455667788ull&&probeCarry==1,"rax and the carry flag survive the stub");
        }
        nl::Write(sink,1,1);
        ProbeSearch(&nl::BearSearchStore);
        Check(probeSlot==28,"at 100% the stub stores 28, the original instruction's immediate");
        const auto& b=native::patches[0].bytes;
        Check(b[0]==0xc7&&b[1]==0x44&&b[2]==0x24&&b[3]==0x28&&b[4]==28&&native::patches[0].size==8,"the site is c7 44 24 28 1c 00 00 00: mov dword [rsp+0x28], 28");
    }
    // ApplyPosted, the real call every pet hook makes first: one line per change, none per tick.
    {
        logged.clear();
        native::aggroMultiplier=1;native::leashMultiplier=1;
        Check(native::Install(&ctx)==native::InstallResult::Ready&&Logged("Frame summon range: live: installed at 100%")==1&&!native::ready.load(),
              "the install arms the real mailbox at 100% and says so once");
        logged.clear();
        for(int i=0;i<1000;++i)native::ApplyPosted();
        Check(Logged("Frame summon range")==0,"a thousand hooks with nothing posted: no line");
        nl::mailbox.Post(3);
        native::ApplyPosted();native::ApplyPosted();
        Check(Logged("Frame summon range: live: 100% > 300%; 12 sites rewritten (7 branches, 4 gates, 1 search); ready=1")==1&&logged.size()==1,"one change, one line, in percent");
        nl::mailbox.Post(1.1);
        native::ApplyPosted();
        Check(Logged("300% > 110%")==1&&Logged("ready=1")==2,"a 10% step is named as 110%");
        nl::mailbox.Post(1);
        native::ApplyPosted();
        Check(Logged("110% > 100%")==1&&Logged("ready=0")==1&&!native::ready.load(),"and back to 100%: ready=0");
        nl::mailbox.Disarm();native::aggroMultiplier=1;native::leashMultiplier=1;native::ready=false;
    }
    // No mixed state: a reader on another thread only ever sees a number that belongs to one of the multipliers.
    {
        nl::Mailbox box;nl::RealSink sink;
        nl::Write(sink,1,1);box.Arm(1);
        const std::vector<double> pool={1,1.25,2,3,4.5,5};
        std::set<int> okBranch[nl::Branches],okGate[nl::Gates],okSearch;
        std::set<double> okMult;
        for(double m:pool) {
            const nl::Cells c=nl::Compute(m,m);
            for(size_t i=0;i<nl::Branches;++i)okBranch[i].insert(c.branch[i]);
            for(size_t i=0;i<nl::Gates;++i)okGate[i].insert(c.gate[i]);
            okSearch.insert(c.search[0]);okMult.insert(m);
        }
        std::atomic<bool> stop{false},posted{false};
        std::atomic<long> torn{0},reads{0};
        std::thread reader([&]{
            while(!stop.load(std::memory_order_relaxed)) {
                for(size_t i=0;i<nl::Branches;++i)if(!okBranch[i].count(__atomic_load_n(&native::summonRangeBranches[i].threshold,__ATOMIC_RELAXED)))++torn;
                for(size_t i=0;i<nl::Gates;++i)if(!okGate[i].count(__atomic_load_n(&native::other::summonPetThresholds[i],__ATOMIC_RELAXED)))++torn;
                if(!okSearch.count(__atomic_load_n(&nl::summonBearSearch,__ATOMIC_RELAXED)))++torn;
                if(!okMult.count(native::aggroMultiplier.load(std::memory_order_acquire)))++torn;
                ++reads;
            }
        });
        std::thread poster([&]{
            unsigned seed=7;
            for(int i=0;i<200000;++i){seed=seed*1103515245u+12345u;box.Post(pool[(seed>>16)%pool.size()]);}
            posted=true;
        });
        nl::Change ch;int applied=0;
        while(!posted.load()||reads.load()<2000)if(box.Drain(sink,ch))++applied;   // the "AI thread": the only writer, until the reader has really read
        poster.join();
        while(box.Drain(sink,ch))++applied;
        stop=true;reader.join();
        Check(applied>0&&reads.load()>0,"the stress ran: applies and reads happened");
        Check(torn.load()==0,"a reader on another thread only ever sees a number that belongs to one of the multipliers (no torn or mixed cell)");
        nl::Mailbox b2;nl::Write(sink,1,1);b2.Arm(1);
        bool consistent=true;
        for(double m:{3.,1.,5.,2.,1.,4.25}) {
            b2.Post(m);b2.Drain(sink,ch);
            const nl::Cells c=nl::Compute(m,m);
            for(size_t i=0;i<nl::Branches;++i)consistent=consistent&&native::summonRangeBranches[i].threshold==c.branch[i];
            for(size_t i=0;i<nl::Gates;++i)consistent=consistent&&native::other::summonPetThresholds[i]==c.gate[i];
            consistent=consistent&&nl::summonBearSearch==c.search[0];
        }
        Check(consistent,"at a tick boundary every table is one multiplier's, never a mix");
        nl::Write(sink,1,1);native::ready=false;
    }
    // Costs (reported, not a benchmark): what the hooks add at 100%, where the adapter idles.
    {
        using Clock=std::chrono::steady_clock;
        nl::Mailbox box;nl::RealSink sink;nl::Change ch;
        box.Arm(1);
        constexpr long N=50'000'000;
        auto t0=Clock::now();
        long sink2=0;
        for(long i=0;i<N;++i)sink2+=box.Drain(sink,ch);
        const double idleNs=std::chrono::duration<double,std::nano>(Clock::now()-t0).count()/N;
        constexpr long M=2'000'000;
        t0=Clock::now();
        for(long i=0;i<M;++i){box.Post((i&1)?1.0:3.0);sink2+=box.Drain(sink,ch);}
        const double applyNs=std::chrono::duration<double,std::nano>(Clock::now()-t0).count()/M;
        native::ready.store(false);native::leashMultiplier.store(1);
        static int(*volatile gameDistance)(uintptr_t,uintptr_t)=[](uintptr_t a,uintptr_t b)->int{return int((a^b)&63)+1;};
        native::other::fullDistance=gameDistance;native::other::unitDistance=gameDistance;
        constexpr long K=100'000'000;
        long acc=0;
        t0=Clock::now();
        for(long i=0;i<K;++i)acc+=gameDistance(uintptr_t(i),5);
        const double rawNs=std::chrono::duration<double,std::nano>(Clock::now()-t0).count()/K;
        t0=Clock::now();
        for(long i=0;i<K;++i)acc+=native::other::FullDistance(uintptr_t(i),5);
        const double fullNs=std::chrono::duration<double,std::nano>(Clock::now()-t0).count()/K;
        t0=Clock::now();
        for(long i=0;i<K;++i)acc+=native::other::UnitDistance(uintptr_t(i),5);
        const double unitNs=std::chrono::duration<double,std::nano>(Clock::now()-t0).count()/K;
        std::printf("cost: a pet hook with nothing posted %.2f ns; a real apply (12 stores) %.1f ns; the game's distance call %.2f ns, through FullDistance at 100%% %.2f ns (+%.2f), through UnitDistance %.2f ns (+%.2f) [%ld %ld]\n",
                    idleNs,applyNs,rawNs,fullNs,fullNs-rawNs,unitNs,unitNs-rawNs,sink2&1,acc&1);
        Check(idleNs<50&&fullNs<200,"the costs are small (sanity bound, not a benchmark)");
        native::other::fullDistance=native::other::unitDistance=nullptr;
    }
}
}
namespace framevr::logging {void Write(const char* line){logged.emplace_back(line);}}
int main() {
    const D2RL::PluginApi api{.apiSize=D2RL::PluginApiSize,.logInfo=Log,.logWarn=Log,.logError=Log,
        .patchBytes=Patch,.checkExpectedBytes=Expected,.installInlineHook=Hook};
    const D2RL::PluginContext ctx{.contextSize=D2RL::PluginContextSize,.abiVersion=D2RL_PLUGIN_ABI_VERSION,.api=&api,.exeBase=0x140000000};
    native::aggroMultiplier=1;native::leashMultiplier=1;
    Check(native::Install(&ctx)==native::InstallResult::Ready&&hooks==27&&patchCount==0&&!native::ready.load(),
          "neutral defaults install the whole adapter idle: 27 hooks (every distance hook too), no byte patch, not ready");
    hooks=0;checked.clear();
    native::aggroMultiplier=2;native::leashMultiplier=2;
    Check(native::Install(&ctx)==native::InstallResult::Ready&&hooks==27&&patchCount==0&&native::ready.load(),"all shared summon hooks install, the target search as a hook (no byte patch)");
    const auto guards=checked;
    for(const auto address:guards) {
        mismatch=address;hooks=patchCount=0;
        Check(native::Install(&ctx)==native::InstallResult::Unavailable&&!native::ready.load()&&hooks==0&&patchCount==0,"every signature mismatch prevents all summon mutations");
    }
    mismatch=0;failHook=native::BearRva;hooks=patchCount=0;
    Check(native::Install(&ctx)==native::InstallResult::Failed&&!native::ready.load()&&patchCount==0,"partial hook failure requires loader rollback");
    failHook=native::patches[0].rva;
    Check(native::Install(&ctx)==native::InstallResult::Failed&&!native::ready.load(),"a failing target search hook requires loader rollback");
    failHook=0x324fc0;
    Check(native::Install(&ctx)==native::InstallResult::Failed&&!native::ready.load(),"a failing distance hook requires loader rollback, also at the end");
    logged.clear();
    native::live::mailbox.Post(3);native::ApplyPosted();
    Check(!native::ready.load()&&logged.empty(),"after a failed install nothing is armed: a post applies nothing");
    failHook=0;native::ready=true;
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
    LiveChecks(ctx);
    std::printf("%u summon range checks passed.\n",checks);
}
