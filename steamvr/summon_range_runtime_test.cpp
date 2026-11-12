// Exercise the real startup/configuration path with owned loader boundaries.
#include "../vr/summon_range.h"
#include "../vr/summon_range_native.h"
#include "../vr/settings_menu.h"
#include "../vr/offline_difficulty_signatures.h"
#include <cstdlib>
#include <limits>

namespace {
int nativeMode=0;
unsigned hookCount=0,patchCount=0;
bool modeSignature=true,rangeSignatures=true,failHook=false,failPatch=false;
framevr::settings::Snapshot settings;
int Mode(){return nativeMode;}
void Check(bool ok,const char* message) {
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
void __cdecl Log(const D2RL::PluginContext*,const char*) noexcept {}
bool __cdecl Expected(const D2RL::PluginContext*,uint64_t rva,const void*,uint32_t) noexcept {
    return rva==offlinedifficulty::abi::ModeRva?modeSignature:rangeSignatures;
}
bool __cdecl Hook(const D2RL::PluginContext*,const D2RL::InlineHookRegistration* registration) noexcept {
    ++hookCount;
    if(registration->original)*registration->original=nullptr;
    return !failHook;
}
bool __cdecl Patch(const D2RL::PluginContext*,uint64_t,const void*,uint32_t,const void*,uint32_t) noexcept {
    ++patchCount;return !failPatch;
}
void Reset() {
    summonrange::Shutdown();nativeMode=0;hookCount=patchCount=0;
    modeSignature=rangeSignatures=true;failHook=failPatch=false;
    settings={};
}
}
namespace vrsettings {framevr::settings::Snapshot Read(){return settings;}}
int main() {
    const D2RL::PluginApi api{.apiSize=D2RL::PluginApiSize,.logInfo=Log,.logWarn=Log,.logError=Log,
        .patchBytes=Patch,.checkExpectedBytes=Expected,.installInlineHook=Hook};
    const D2RL::PluginContext context{.contextSize=D2RL::PluginContextSize,.abiVersion=D2RL_PLUGIN_ABI_VERSION,
        .api=&api,.exeBase=reinterpret_cast<uintptr_t>(&Mode)-offlinedifficulty::abi::ModeRva};
    namespace native=framevr::summonrange::native;
    Reset();
    Check(!summonrange::Init(nullptr),"missing loader context refused");
    Check(summonrange::AggroFactor()==1,"uninitialized missile speed remains native");
    Check(summonrange::Init(&context)&&hookCount==0&&patchCount==0,"neutral default installs no summon hooks or patches");
    for(float multiplier:{1.25f,2.f,5.f}) {
        Reset();summonrange::Configure(multiplier,true);
        Check(summonrange::Init(&context)&&native::ready.load(),"shared preference enables verified native adapter");
        Check(native::aggroMultiplier==multiplier&&native::leashMultiplier==multiplier&&summonrange::AggroFactor()==multiplier,
              "one slider drives aggression, leash and mage bolt speed together");
        Check(hookCount==26&&patchCount==1,"complete summon adapter installs without other gameplay adapters");
        summonrange::Configure(3,true);
        Check(native::aggroMultiplier==multiplier&&native::leashMultiplier==multiplier,
              "Apply cannot change startup native ranges before restart");
        nativeMode=1;
        Check(summonrange::AggroFactor()==1,"mage speed remains native outside offline mode");
        nativeMode=0;summonrange::Configure(multiplier,false);
        Check(summonrange::AggroFactor()==1,"mage speed remains native in desktop mode");
        summonrange::Shutdown();
        Check(!native::ready.load()&&summonrange::AggroFactor()==1,"shutdown disables adapter and missile scaling");
    }
    Reset();summonrange::Configure(5,true);nativeMode=1;
    Check(summonrange::Init(&context)&&hookCount==0&&patchCount==0&&!native::ready.load(),"online startup performs no native mutations");
    Reset();summonrange::Configure(5,false);
    Check(summonrange::Init(&context)&&hookCount==0&&patchCount==0,"desktop startup performs no native mutations");
    Reset();summonrange::Configure(5,true);modeSignature=false;
    Check(summonrange::Init(&context)&&hookCount==0&&patchCount==0,"missing offline signature prevents all mutations");
    Reset();summonrange::Configure(5,true);rangeSignatures=false;
    Check(summonrange::Init(&context)&&hookCount==0&&patchCount==0&&summonrange::AggroFactor()==1,"unavailable range signatures retain native behavior");
    for(bool patchFailure:{false,true}) {
        Reset();summonrange::Configure(5,true);failPatch=patchFailure;failHook=!patchFailure;
        Check(!summonrange::Init(&context)&&!native::ready.load()&&summonrange::AggroFactor()==1,
              "any mutation failure rejects loading for complete loader rollback");
    }
    Reset();settings.values[framevr::settings::SummonRangeMultiplier]=2.5f;
    Check(summonrange::Init(&context)&&native::aggroMultiplier==2.5&&native::leashMultiplier==2.5,
          "initialization can read the shared persisted preference");
    Reset();summonrange::Configure(std::numeric_limits<float>::quiet_NaN(),true);
    Check(summonrange::Init(&context)&&hookCount==0&&patchCount==0,"invalid preference restores native default");
    Reset();summonrange::Configure(5,true);
    Check(summonrange::Init(&context)&&native::aggroMultiplier==5&&native::leashMultiplier==5,"restart applies the new shared preference");
    Reset();
    std::puts("Summon ranges: shared preference, restart, mode, signatures and rollback checks passed.");
}
