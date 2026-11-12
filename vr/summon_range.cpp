// One persisted preference drives both summon target and owner-follow ranges.
#include "summon_range.h"
#include "summon_range_native.h"
#include "offline_difficulty_signatures.h"
#include "settings_menu.h"

namespace summonrange {
namespace {
using ModeFn=int(*)();
ModeFn offlineMode=nullptr;
std::atomic<bool> initialized{false},vrEnabled{false};
float startupRangeMultiplier=1;
bool configured=false;
}
void Configure(float multiplier,bool enableVr) {
    vrEnabled.store(enableVr,std::memory_order_release);
    // Native comparisons and patches cannot be changed by a live Apply.
    if(!initialized.load(std::memory_order_acquire)) {
        startupRangeMultiplier=framevr::settings::Normalize(framevr::settings::SummonRangeMultiplier,multiplier);
        configured=true;
    }
}
bool Init(const D2RL::PluginContext* context) {
    if(!context)return false;
    if(!configured)Configure(vrsettings::Read().values[framevr::settings::SummonRangeMultiplier],true);
    using namespace offlinedifficulty::abi;
    if(context->CheckExpectedBytes(ModeRva,ModeSig,sizeof ModeSig))
        offlineMode=reinterpret_cast<ModeFn>(context->exeBase+ModeRva);
    else {
        offlineMode=nullptr;
        context->LogWarn("Summon ranges: offline-mode signature unavailable; native ranges retained.");
    }
    namespace native=framevr::summonrange::native;
    native::aggroMultiplier=startupRangeMultiplier;
    native::leashMultiplier=startupRangeMultiplier;
    if(vrEnabled.load(std::memory_order_acquire)&&offlineMode&&offlineMode()==0&&
       native::Install(context)==native::InstallResult::Failed) {
        // The loader owns all mutations and rolls them back when loading fails.
        context->LogError("Summon ranges: installation failed; loader rollback required.");
        return false;
    }
    initialized.store(true,std::memory_order_release);
    return true;
}
void Shutdown() {
    framevr::summonrange::native::ready.store(false,std::memory_order_release);
    initialized.store(false,std::memory_order_release);
    vrEnabled.store(false,std::memory_order_release);
    offlineMode=nullptr;
    configured=false;
}
float AggroFactor() {
    return initialized.load(std::memory_order_acquire)&&vrEnabled.load(std::memory_order_acquire)&&
        offlineMode&&offlineMode()==0&&framevr::summonrange::native::ready.load(std::memory_order_acquire)?
        float(framevr::summonrange::native::aggroMultiplier):1.f;
}
}
