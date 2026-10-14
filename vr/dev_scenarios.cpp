#include "dev_scenarios.h"

#ifdef D2RVR_DEVELOPER
#include <windows.h>
#include <D2RLPlugin/api.h>
#include <D2RLPlugin/core_exports.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstdio>
#include "interactionstate.h"
#include "../steamvr/dev_scenarios.h"
#include "../steamvr/logging.h"
namespace gamestate {uint64_t LocalPlayer();bool MenuOpen();}

namespace devscenarios {
namespace {
using namespace D2RL;
using namespace framevr::interaction;
const PluginContext* context=nullptr;
const ThreadService* threads=nullptr;
const ItemService* items=nullptr;
CoreExports::ExecuteConsoleCommandFn execute=nullptr;
bool enabled=false;
std::mutex mutex;
framevr::interaction::Request queued;
Reply reply;
bool scheduled=false;
uintptr_t ticket=0;

bool Metadata(const PluginContext* ctx) {
    if(!ctx||!ctx->activeMod||!ctx->modDirectory)return false;
    try {
        // PluginContext::modDirectory is the mounted .mpq data directory, not
        // its enclosing mods/<name> folder (observed on D2RLoader 1.3.1).
        const auto path=std::filesystem::path(ctx->modDirectory)/L"modinfo.json";
        std::ifstream input(path,std::ios::binary);
        std::string text(sizeof(framevr::dev::ModInfo),0);
        input.read(text.data(),text.size());text.resize(size_t(input.gcount()));
        return framevr::dev::Isolated(ctx->activeMod,text);
    } catch(...) {return false;}
}
bool Valid(const framevr::interaction::Request& r) {
    const auto s=interactionstate::Read();const auto now=GetTickCount64();
    return enabled&&r.action==Action::Developer&&framevr::dev::ValidChoice(r.index)&&
        Fresh(s,r.session,now)&&s.devPaladin&&s.developer&&r.session==gamestate::LocalPlayer()&&
        now>=r.at&&now-r.at<1000&&!gamestate::MenuOpen()&&s.actionKnown&&
        (s.actionMode==1||s.actionMode==2||s.actionMode==3);
}
void Finish(const framevr::interaction::Request& r,Result result) {
    reply={r.sequence,r.session,result};
    char line[180];std::snprintf(line,sizeof line,"Frame development: choice=%d result=%d (console dispatch does not confirm gameplay success)",r.index,int(result));
    framevr::logging::Write(line);
}
void __cdecl Commands(const PluginContext*,void* token) noexcept {
    std::lock_guard lock(mutex);if(reinterpret_cast<uintptr_t>(token)!=ticket)return;scheduled=false;
    const auto r=queued;queued={};if(r.action==Action::None)return;
    if(!Valid(r)||!execute){Finish(r,Result::Refused);return;}
    bool accepted=true;
    if(r.index==0) {
        for(const auto* command:framevr::dev::Preparation) {
            if(r.session!=gamestate::LocalPlayer()||!execute(command)){accepted=false;break;}
        }
    } else {
        const auto* command=framevr::dev::Choices[r.index].command;
        accepted=command&&execute(command);
    }
    Finish(r,accepted?Result::Dispatched:Result::Refused);
}
void __cdecl Gear(const PluginContext*,void* token) noexcept {
    std::lock_guard lock(mutex);if(reinterpret_cast<uintptr_t>(token)!=ticket)return;scheduled=false;
    const auto r=queued;queued={};if(r.action==Action::None)return;
    // r.session is the SDK handle captured on the UI thread by Collect().
    // getLocalPlayer itself is UI-thread-only; the transaction revalidates this
    // captured handle against the authoritative game on this callback thread.
    const PlayerHandle player=r.session;
    if(!Valid(r)||!items) {
        Finish(r,Result::Refused);return;
    }
    const auto& codes=r.index==1?framevr::dev::CombatKit:framevr::dev::CasterKit;
    std::array<Items::ItemCreateSpec,10> specs{};
    std::array<ItemHandle,10> handles{};
    for(size_t i=0;i<specs.size();++i) {
        auto& spec=specs[i];spec.structSize=sizeof spec;
        spec.code=Items::MakeItemCode(codes[i][0],codes[i][1],codes[i][2]);
        spec.quality=Items::Quality::Unique;spec.qualityRecordId=Items::RandomQualityRecord;
        spec.flags=Items::ItemCreateFlagBit(Items::ItemCreateFlag::AllowDuplicateUnique);
        spec.itemLevel=99;spec.seedMode=Items::SeedMode::Random;
        spec.quantity=spec.durability=Items::DefaultValue;spec.stateFlags=Items::ItemStateIdentified;
        spec.destination.structSize=sizeof spec.destination;
        spec.destination.container=Items::ItemContainer::Inventory;
        spec.destination.placement=Items::Placement::Automatic;
    }
    Items::Transaction transaction{};transaction.structSize=sizeof transaction;
    transaction.player=player;transaction.outputCount=uint32_t(specs.size());transaction.outputs=specs.data();
    transaction.outputItems=handles.data();transaction.outputCapacity=uint32_t(handles.size());
    Items::TransactionResult result{};result.structSize=sizeof result;
    const auto status=items->executeTransaction(context,&transaction,&result);
    // The SDK rolls back the entire kit when validation or placement fails.
    const bool ok=status==Items::Result::Success&&result.outputCount==specs.size();
    if(!ok) {
        char line[128];std::snprintf(line,sizeof line,"Frame development: gear transaction refused, SDK result=%u; free inventory space and retry",unsigned(status));
        framevr::logging::Write(line);
    }
    Finish(r,ok?Result::Applied:Result::Refused);
}
}
void Init(const D2RL::PluginContext* ctx) {
    context=ctx;enabled=Metadata(ctx);
    if(!enabled) {
        framevr::logging::Write("Frame development: DEV hidden; active mod must be pcvr-dev with matching modinfo.json in the loader's mod data directory");
        return;
    }
    if(ctx->QueryService(&threads)!=ServiceQueryResult::Success)threads=nullptr;
    if(ctx->QueryService(&items)!=ServiceQueryResult::Success)items=nullptr;
    if(const auto core=GetModuleHandleW(L"D2RCore.dll"))
        execute=reinterpret_cast<CoreExports::ExecuteConsoleCommandFn>(GetProcAddress(core,MAKEINTRESOURCEA(CoreExports::ExecuteConsoleCommandInfo.ordinal)));
    framevr::logging::Write("Frame development: isolated pcvr-dev environment enabled; use offline local Paladins only");
}
void Capture(framevr::interaction::Snapshot& s){s.developer=enabled;}
void Request(const framevr::interaction::Request& r) {
    std::lock_guard lock(mutex);
    if(scheduled||queued.action!=Action::None)return;
    if(!Valid(r)||!threads){Finish(r,Result::Refused);return;}
    const auto run=framevr::dev::IsKit(r.index)?threads->runOnGameThread:threads->runOnUiThread;
    if(!run){Finish(r,Result::Refused);return;}
    queued=r;scheduled=true;reply={r.sequence,r.session,Result::Pending};++ticket;
    if(run(context,framevr::dev::IsKit(r.index)?Gear:Commands,reinterpret_cast<void*>(ticket))!=D2RL::Threads::Result::Success) {
        queued={};scheduled=false;Finish(r,Result::Refused);
    }
}
framevr::interaction::Reply Status(){std::lock_guard lock(mutex);return reply;}
void Cancel(){std::lock_guard lock(mutex);++ticket;scheduled=false;queued={};reply={};}
}
#else
namespace devscenarios {
void Init(const D2RL::PluginContext*){}
void Capture(framevr::interaction::Snapshot&){}
void Request(const framevr::interaction::Request&){}
framevr::interaction::Reply Status(){return {};}
void Cancel(){}
}
#endif
