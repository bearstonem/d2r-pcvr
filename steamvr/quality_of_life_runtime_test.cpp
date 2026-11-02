// Exercise the built-in settings/lifecycle adapter without opening a game.
#include <windows.h>
#include <D2RLPlugin/api.h>
#include "../vr/quality_of_life.h"
#include "../vr/settings_menu.h"
#include "../vr/offline_difficulty_signatures.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace {
int nativeMode=0;uint8_t availability=0;
std::vector<D2RL::Lifecycle::GameplayEventListener> listeners;
int Mode(){return nativeMode;}
void Check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
void __cdecl Log(const D2RL::PluginContext*,const char*) noexcept {}
bool __cdecl Expected(const D2RL::PluginContext*,uint64_t rva,const void*,uint32_t) noexcept {
    return rva==offlinedifficulty::abi::ModeRva;
}
D2RL::Lifecycle::Result __cdecl Register(const D2RL::PluginContext*,const D2RL::Lifecycle::GameplayEventListener* l,D2RL::Lifecycle::ListenerHandle* handle) noexcept {
    listeners.push_back(*l);*handle=listeners.size();return D2RL::Lifecycle::Result::Success;
}
D2RL::LifecycleService service{.serviceSize=D2RL::LifecycleServiceSize,.serviceVersion=D2RL::LifecycleService::AbiVersion,.registerGameplayEventListener=Register};
D2RL::ServiceQueryResult __cdecl Query(const D2RL::PluginContext*,D2RL::ServiceId id,uint32_t,const void** out) noexcept {
    if(id!=D2RL::ServiceId::Lifecycle)return D2RL::ServiceQueryResult::Unavailable;
    *out=&service;return D2RL::ServiceQueryResult::Success;
}
}
namespace vrsettings {
framevr::settings::Snapshot Read(){return {};}
void SetQualityOfLifeAvailable(uint8_t mask){availability=mask;}
}
namespace framevr::logging {void Write(const char*){}}
int main(){
    const D2RL::PluginApi api{.apiSize=D2RL::PluginApiSize,.logInfo=Log,.logWarn=Log,.logError=Log,.checkExpectedBytes=Expected,.queryService=Query};
    const D2RL::PluginContext ctx{.contextSize=D2RL::PluginContextSize,.abiVersion=D2RL_PLUGIN_ABI_VERSION,.api=&api,
        .exeBase=reinterpret_cast<uintptr_t>(&Mode)-offlinedifficulty::abi::ModeRva};
    qualityoflife::Configure(.5f,1,1,1,true);
    Check(qualityoflife::DamageFactor()==1,"disabled until initialization");
    Check(qualityoflife::SummonAggroFactor()==1,"mage speed disabled until initialization");
    Check(qualityoflife::Init(&ctx)&&listeners.size()==4,"register lifecycle events during startup");
    Check(qualityoflife::DamageFactor()==.5f,"offline configured damage factor active");
    nativeMode=1;Check(qualityoflife::DamageFactor()==1,"online damage is native");nativeMode=0;
    qualityoflife::Configure(.25f,.4f,2,2,true);
    Check(qualityoflife::DamageFactor()==.25f&&qualityoflife::NeedsMovement(),"damage and movement settings update live");
    qualityoflife::DamageReady(true);Check(availability==1,"damage availability published independently");
    qualityoflife::VelocityReady(true);qualityoflife::LookupReady(true);
    Check(availability==1,"unverified movement relation never reports ready");
    Check(qualityoflife::AdjustVelocity(1,100)==100,"unavailable movement adapter retains native speed");
    Check(qualityoflife::SummonAggroFactor()==1,"unverified summon adapter retains native missile speed");
    qualityoflife::Configure(.25f,.4f,2,2,false);
    Check(qualityoflife::DamageFactor()==1&&!qualityoflife::NeedsMovement(),"desktop platform keeps native behavior");
    qualityoflife::Configure(1,1,5,5,true);
    Check(!qualityoflife::NeedsMovement(),"summon ranges alone do not request movement hooks");
    qualityoflife::Shutdown();Check(availability==0&&qualityoflife::DamageFactor()==1,"shutdown disables adapters");
    std::puts("Built-in QoL runtime lifecycle, live settings, platform and capability tests passed.");
}
