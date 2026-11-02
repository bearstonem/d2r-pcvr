// Built into PCVR. Combat, projectiles and melee own the shared native hooks.
#include <windows.h>
#include <intrin.h>
#include <D2RLPlugin/api.h>
#include "quality_of_life.h"
#include "quality_of_life_movement.h"
#include "quality_of_life_summons.h"
#include "offline_difficulty_signatures.h"
#include "settings_menu.h"
#include "../steamvr/vr_settings.h"

namespace qualityoflife {
namespace {
using namespace framevr::qol;
std::atomic<float> damage{1};
std::atomic<bool> vrEnabled{false},running{false},damageIn{false},velocityIn{false},lookupIn{false};
using ModeFn=int(*)();
ModeFn mode=nullptr;
float startAggro=1,startLeash=1;
bool configured=false;
void Publish() {
    vrsettings::SetQualityOfLifeAvailable(uint8_t((mode&&damageIn.load()?1:0)|
        (movement::ready.load()&&velocityIn.load()&&lookupIn.load()?2:0)|(summons::ready.load()?4:0)));
}
bool Offline(){return running.load(std::memory_order_acquire)&&vrEnabled.load()&&mode&&mode()==0;}
void __cdecl OnGameplay(const D2RL::PluginContext*,const D2RL::Lifecycle::GameplayEvent* event,void*) noexcept {
    if(!D2RL::Lifecycle::HasGameplayEventField(event,D2RL::Lifecycle::GameplayEventRequiredSize))return;
    using K=D2RL::Lifecycle::GameplayEventKind;
    if(event->kind==K::GameJoined||event->kind==K::GameLeft)movement::ResetPlayer();
    else if(event->kind==K::LocalPlayerReady||event->kind==K::PlayerResurrected)movement::ResetPlayer(event->playerId);
}
}
void Configure(float d,float m,float a,float l,bool vr) {
    using namespace framevr::settings;
    damage=Normalize(IncomingDamage,d);framevr::qol::movement::multiplier=Normalize(MonsterMovement,m);vrEnabled=vr;
    // Startup is captured before the plugin initializes. Later Apply operations
    // persist summon choices; executable patches remain unchanged until restart.
    if(!running.load()) {startAggro=Normalize(DruidAggro,a);startLeash=Normalize(DruidLeash,l);configured=true;}
}
bool Init(const D2RL::PluginContext* ctx) {
    using namespace framevr::qol;
    if(!ctx)return false;
    if(!configured){const auto v=vrsettings::Read().values;using namespace framevr::settings;
        Configure(v[IncomingDamage],v[MonsterMovement],v[DruidAggro],v[DruidLeash],true);}
    using namespace offlinedifficulty::abi;
    if(ctx->CheckExpectedBytes(ModeRva,ModeSig,sizeof ModeSig))mode=reinterpret_cast<ModeFn>(ctx->exeBase+ModeRva);
    const D2RL::LifecycleService* lifecycle=nullptr;
    bool events=ctx->QueryService(&lifecycle)==D2RL::ServiceQueryResult::Success&&
        D2RL::HasLifecycleServiceField(lifecycle,D2RL::LifecycleServiceRequiredSize)&&lifecycle->registerGameplayEventListener;
    if(events)for(const auto kind:{D2RL::Lifecycle::GameplayEventKind::GameJoined,D2RL::Lifecycle::GameplayEventKind::GameLeft,
        D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,D2RL::Lifecycle::GameplayEventKind::PlayerResurrected}) {
        const D2RL::Lifecycle::GameplayEventListener listener{.structSize=D2RL::Lifecycle::GameplayEventListenerSize,.kind=kind,.callback=OnGameplay};
        D2RL::Lifecycle::ListenerHandle handle=0;
        if(lifecycle->registerGameplayEventListener(ctx,&listener,&handle)!=D2RL::Lifecycle::Result::Success)events=false;
    }
    const bool valid=events&&mode&&ctx->CheckExpectedBytes(movement::RelationRva,movement::RelationSig,sizeof movement::RelationSig)&&
        ctx->CheckExpectedBytes(movement::VelocityLayoutRva,movement::VelocityLayoutSig,sizeof movement::VelocityLayoutSig);
    if(valid)movement::relation=reinterpret_cast<movement::RelationFn>(ctx->exeBase+movement::RelationRva);
    movement::ready=valid;movement::enabled=valid;
    if(!valid)ctx->LogWarn("Quality of Life: monster movement adapter unavailable; native speed retained.");
    summons::aggro=startAggro;summons::leash=startLeash;
    // Summon patches are startup-only, intended for local offline mod sessions.
    if(vrEnabled.load()&&mode&&mode()==0&&summons::Install(ctx)==summons::InstallResult::Failed) {
        // A partial patch must fail plugin loading so the loader rolls it back.
        ctx->LogError("Quality of Life: summon installation failed; loader rollback required.");return false;
    }
    running.store(true,std::memory_order_release);Publish();return true;
}
void Shutdown() {
    running.store(false,std::memory_order_release);vrEnabled=false;
    framevr::qol::movement::enabled=false;framevr::qol::movement::ResetPlayer();
    framevr::qol::summons::ready=false;vrsettings::SetQualityOfLifeAvailable(0);
}
float DamageFactor(){const float factor=damage.load(std::memory_order_relaxed);return factor!=1&&Offline()?factor:1.f;}
float SummonAggroFactor(){return framevr::qol::summons::ready.load(std::memory_order_acquire)&&Offline()?float(framevr::qol::summons::aggro):1.f;}
bool NeedsDamage(){return vrEnabled.load()&&damage.load()!=1;}
bool NeedsMovement(){return vrEnabled.load()&&framevr::qol::movement::multiplier.load()!=1;}
void DamageReady(bool ready){if(damageIn.exchange(ready)!=ready)Publish();}
void VelocityReady(bool ready){if(velocityIn.exchange(ready)!=ready)Publish();}
void LookupReady(bool ready){if(lookupIn.exchange(ready)!=ready)Publish();}
int AdjustVelocity(uintptr_t path,int native){return NeedsMovement()&&Offline()?framevr::qol::movement::Adjust(path,native):native;}
void ObserveLookup(uintptr_t game,int type,uint32_t id,uintptr_t unit){
    if(NeedsMovement()&&Offline())framevr::qol::movement::ObserveLookup(game,type,id,unit);
}
}
