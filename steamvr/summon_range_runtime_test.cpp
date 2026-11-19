// Exercise the real startup/configuration path with owned loader boundaries.
// Summon range applies live: the adapter installs at startup even at 100% (idle), a reload's new multiplier is posted by
// summonrange::Configure and applied at the next pet AI hook, and every table then equals what the startup install bakes.
#include "../vr/summon_range.h"
#include "../vr/summon_range_native.h"
#include "../vr/settings_menu.h"
#include "../vr/offline_difficulty_signatures.h"
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {
int nativeMode=0;
unsigned hookCount=0,patchCount=0;
bool modeSignature=true,rangeSignatures=true,failHook=false,failPatch=false;
uint64_t failRva=0;
std::vector<uint64_t> hookedRvas;
std::vector<std::string> lines;
framevr::settings::Snapshot settings;
int Mode(){return nativeMode;}
void Nop3(uintptr_t,uintptr_t,uintptr_t){}   // the game's own pet AI, behind the hooks
void Check(bool ok,const char* message) {
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
void __cdecl Log(const D2RL::PluginContext*,const char*) noexcept {}
bool __cdecl Expected(const D2RL::PluginContext*,uint64_t rva,const void*,uint32_t) noexcept {
    return rva==offlinedifficulty::abi::ModeRva?modeSignature:rangeSignatures;
}
bool __cdecl Hook(const D2RL::PluginContext*,const D2RL::InlineHookRegistration* registration) noexcept {
    ++hookCount;hookedRvas.push_back(registration->rva);
    if(registration->original)*registration->original=reinterpret_cast<void*>(&Nop3);
    return !failHook&&registration->rva!=failRva;
}
bool __cdecl Patch(const D2RL::PluginContext*,uint64_t,const void*,uint32_t,const void*,uint32_t) noexcept {
    ++patchCount;return !failPatch;
}
namespace native=framevr::summonrange::native;
struct Tables {int branch[7],gate[4],search;};
Tables Capture() {
    Tables t{};
    for(int i=0;i<7;++i)t.branch[i]=native::summonRangeBranches[i].threshold;
    for(int i=0;i<4;++i)t.gate[i]=native::other::summonPetThresholds[i];
    t.search=native::live::summonBearSearch;
    return t;
}
bool Same(const Tables& a,const Tables& b){return std::memcmp(&a,&b,sizeof a)==0;}
// What the startup install bakes for multiplier m: upstream's own StateOf and Scale, site by site.
Tables Baked(double m,uintptr_t image=0) {
    Tables t{};
    for(int i=0;i<7;++i)t.branch[i]=native::StateOf(native::ranges[i],image,m).threshold;
    for(int i=0;i<4;++i){const auto& g=native::other::gates[i];t.gate[i]=native::Scale(g.bytes[g.immediate],m);}
    t.search=native::Scale(native::patches[0].bytes[native::patches[0].immediate],m,32767);
    return t;
}
Tables Native(){return Baked(1);}
void PetTick(int which) {   // each of the three pet hooks, with the fake game's nothing behind them
    if(which==0)native::Wolf(1,2,3);
    else if(which==1)native::Bear(1,2,3);
    else native::other::Think(0,1,2,3);
}
int Lines(const char* part) {
    int n=0;
    for(const auto& l:lines)if(l.find(part)!=std::string::npos)++n;
    return n;
}
void Reset() {
    summonrange::Shutdown();nativeMode=0;hookCount=patchCount=0;
    modeSignature=rangeSignatures=true;failHook=failPatch=false;failRva=0;
    hookedRvas.clear();lines.clear();
    for(auto& b:native::summonRangeBranches)b.threshold=0;
    for(auto& g:native::other::summonPetThresholds)g=0;
    native::live::summonBearSearch=0;
    native::aggroMultiplier=1;native::leashMultiplier=1;
    native::originalWolf=native::originalBear=&Nop3;
    for(auto& original:native::other::originals)original=&Nop3;
    settings={};
}
}
namespace vrsettings {framevr::settings::Snapshot Read(){return settings;}}
namespace framevr::logging {void Write(const char* line){lines.emplace_back(line);}}
int main() {
    const D2RL::PluginApi api{.apiSize=D2RL::PluginApiSize,.logInfo=Log,.logWarn=Log,.logError=Log,
        .patchBytes=Patch,.checkExpectedBytes=Expected,.installInlineHook=Hook};
    const D2RL::PluginContext context{.contextSize=D2RL::PluginContextSize,.abiVersion=D2RL_PLUGIN_ABI_VERSION,
        .api=&api,.exeBase=reinterpret_cast<uintptr_t>(&Mode)-offlinedifficulty::abi::ModeRva};
    Reset();
    Check(!summonrange::Init(nullptr),"missing loader context refused");
    Check(summonrange::AggroFactor()==1,"uninitialized missile speed remains native");
    // Applying live needs the adapter in place before the first change: at 100% it installs and idles.
    Check(summonrange::Init(&context)&&hookCount==27&&patchCount==0,"neutral default installs the idle adapter: 27 hooks (the 26 and the search stub), no byte patch");
    Check(!native::ready.load()&&summonrange::AggroFactor()==1,"idle at 100%: not ready, so every hook passes straight to the game and missile speed stays native");
    Check(Same(Capture(),Native()),"at 100% every range site, gate and the search read the game's own number");
    {
        bool distance=false,distance2=false,search=false;
        for(uint64_t rva:hookedRvas){distance|=rva==0x596720;distance2|=rva==0x324fc0;search|=rva==native::patches[0].rva;}
        Check(distance&&distance2,"the two distance hooks install at 100% too, so a later change can reach them");
        Check(search,"the Grizzly's target search is a hook (a data read), not a baked immediate");
    }
    Check(Lines("Frame summon range: live: installed at 100%")==1,"one install line");
    for(float multiplier:{1.25f,2.f,5.f}) {
        Reset();summonrange::Configure(multiplier,true);
        Check(summonrange::Init(&context)&&native::ready.load(),"shared preference enables verified native adapter");
        Check(native::aggroMultiplier==multiplier&&native::leashMultiplier==multiplier&&summonrange::AggroFactor()==multiplier,
              "one slider drives aggression, leash and mage bolt speed together");
        Check(hookCount==27&&patchCount==0,"complete summon adapter installs without other gameplay adapters");
        Check(Same(Capture(),Baked(multiplier)),"startup tables are the multiplier's own numbers");
        nativeMode=1;
        Check(summonrange::AggroFactor()==1,"mage speed remains native outside offline mode");
        nativeMode=0;summonrange::Configure(multiplier,false);
        Check(summonrange::AggroFactor()==1,"mage speed remains native in desktop mode");
        summonrange::Shutdown();
        Check(!native::ready.load()&&summonrange::AggroFactor()==1,"shutdown disables adapter and missile scaling");
    }
    // The change itself: posted by the settings reload, applied by the next pet hook, no restart.
    Reset();summonrange::Configure(1,true);
    Check(summonrange::Init(&context),"startup at 100%");
    {
        const Tables before=Capture();lines.clear();
        summonrange::Configure(3,true);
        Check(Same(Capture(),before)&&!native::ready.load()&&summonrange::AggroFactor()==1&&native::aggroMultiplier==1,
              "posted, no pet hook yet: not a single table moved");
        PetTick(0);
        Check(native::ready.load()&&summonrange::AggroFactor()==3&&native::aggroMultiplier==3&&native::leashMultiplier==3,"the next pet hook applies it: ready, 300%");
        Check(Same(Capture(),Baked(3)),"every table and the search equal the startup bake at 300%");
        Check(Lines("Frame summon range: live: 100% > 300%")==1,"one change line, in percent");
    }
    // 100% > each step > 100% again, whichever hook ticks: every table equals the startup bake at that multiplier.
    {
        int hook=0;
        for(int round=0;round<3;++round)for(float m:{1.25f,1.5f,2.f,3.f,4.5f,5.f}) {
            summonrange::Configure(m,true);PetTick(hook++%3);
            Check(Same(Capture(),Baked(m))&&native::ready.load()&&summonrange::AggroFactor()==m,"after the tick every table equals the startup bake at that multiplier");
            summonrange::Configure(1,true);PetTick(hook++%3);
            Check(Same(Capture(),Native())&&!native::ready.load()&&summonrange::AggroFactor()==1,"and back to 100%: the game's own numbers, idle");
        }
        lines.clear();
        for(float m:{3.f,1.f,5.f}){summonrange::Configure(m,true);PetTick(0);}
        Check(Lines("Frame summon range: live: ")==3&&Lines("100% > 300%")==1&&Lines("300% > 100%")==1&&Lines("100% > 500%")==1,"three changes, three lines (never per tick)");
        Check(Lines("12 sites rewritten")==3&&Lines("ready=1")==2&&Lines("ready=0")==1,"sites rewritten and whether the adapter is active are in each line");
        lines.clear();
        for(int i=0;i<1000;++i)PetTick(i%3);
        summonrange::Configure(5,true);
        for(int i=0;i<1000;++i)PetTick(i%3);
        Check(lines.empty(),"a thousand ticks and a reload of the same value: no line at all");
        summonrange::Configure(std::numeric_limits<float>::quiet_NaN(),true);PetTick(1);
        Check(Same(Capture(),Native())&&summonrange::AggroFactor()==1,"an invalid reload is normalized to 100% (the game's numbers)");
    }
    // Started above 100%, a reload down to 100% applies too.
    Reset();summonrange::Configure(4,true);
    Check(summonrange::Init(&context)&&native::ready.load(),"startup at 400%");
    summonrange::Configure(1,true);PetTick(2);
    Check(!native::ready.load()&&Same(Capture(),Native())&&summonrange::AggroFactor()==1&&native::aggroMultiplier==1,"400% to 100% at runtime: idle, the game's numbers");
    // Online and desktop: nothing installs, and a reload changes nothing.
    Reset();summonrange::Configure(5,true);nativeMode=1;
    Check(summonrange::Init(&context)&&hookCount==0&&patchCount==0&&!native::ready.load(),"online startup performs no native mutations");
    summonrange::Configure(2,true);PetTick(0);
    Check(!native::ready.load()&&summonrange::AggroFactor()==1&&lines.empty(),"online: a reload changes nothing and logs nothing");
    Reset();summonrange::Configure(5,false);
    Check(summonrange::Init(&context)&&hookCount==0&&patchCount==0,"desktop startup performs no native mutations");
    summonrange::Configure(2,false);PetTick(1);
    Check(!native::ready.load()&&summonrange::AggroFactor()==1&&lines.empty(),"desktop: a reload changes nothing and logs nothing");
    Reset();summonrange::Configure(5,true);modeSignature=false;
    Check(summonrange::Init(&context)&&hookCount==0&&patchCount==0,"missing offline signature prevents all mutations");
    Reset();summonrange::Configure(5,true);rangeSignatures=false;
    Check(summonrange::Init(&context)&&hookCount==0&&patchCount==0&&summonrange::AggroFactor()==1,"unavailable range signatures retain native behavior");
    Reset();rangeSignatures=false;
    Check(summonrange::Init(&context)&&hookCount==0&&patchCount==0,"unavailable range signatures at 100% too: nothing installed");
    summonrange::Configure(3,true);PetTick(0);
    Check(!native::ready.load()&&summonrange::AggroFactor()==1,"and a reload there changes nothing");
    // A hook that fails (the first, or the Grizzly's search stub, the last) fails the load so the loader rolls everything back.
    for(int failure=0;failure<3;++failure) {
        Reset();summonrange::Configure(5,true);failHook=failure==0;failRva=failure==1?native::patches[0].rva:failure==2?0x324fc0:0;
        Check(!summonrange::Init(&context)&&!native::ready.load()&&summonrange::AggroFactor()==1,
              "any mutation failure rejects loading for complete loader rollback");
        summonrange::Configure(2,true);PetTick(0);
        Check(!native::ready.load()&&summonrange::AggroFactor()==1,"and a reload after a failed install applies nothing");
    }
    Reset();failRva=native::patches[0].rva;
    Check(!summonrange::Init(&context)&&!native::ready.load(),"a failing search stub fails the load at 100% too");
    Check(patchCount==0,"the adapter never patches bytes");
    // The plugin ends with a post still waiting: nothing applies after Shutdown.
    Reset();
    Check(summonrange::Init(&context),"startup at 100%");
    summonrange::Configure(3,true);summonrange::Shutdown();lines.clear();
    PetTick(0);
    Check(!native::ready.load()&&summonrange::AggroFactor()==1&&Lines("Frame summon range: live")==0,"a pet hook after Shutdown does not make ready true again, and says nothing");
    Reset();settings.values[framevr::settings::SummonRangeMultiplier]=2.5f;
    Check(summonrange::Init(&context)&&native::aggroMultiplier==2.5&&native::leashMultiplier==2.5,
          "initialization can read the shared persisted preference");
    Reset();summonrange::Configure(std::numeric_limits<float>::quiet_NaN(),true);
    Check(summonrange::Init(&context)&&hookCount==27&&!native::ready.load()&&summonrange::AggroFactor()==1,"invalid preference restores native default (idle adapter)");
    Reset();summonrange::Configure(5,true);
    Check(summonrange::Init(&context)&&native::aggroMultiplier==5&&native::leashMultiplier==5,"a new startup reads the new shared preference");
    Reset();
    std::puts("Summon ranges: shared preference, live apply, mode, signatures and rollback checks passed.");
}
