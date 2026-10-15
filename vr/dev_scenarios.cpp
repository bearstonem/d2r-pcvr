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
    // PlrMode also has separate town-neutral (5) and town-walk (6) states.
    // Development preparation must work where a new character starts.
    const bool idle=s.actionMode==1||s.actionMode==2||s.actionMode==3||s.actionMode==5||s.actionMode==6;
    const bool valid=enabled&&r.action==Action::Developer&&framevr::dev::ValidChoice(r.index)&&
        Fresh(s,r.session,now)&&framevr::dev::ValidClass(s.devClass)&&s.developer&&r.session==gamestate::LocalPlayer()&&
        now>=r.at&&now-r.at<1000&&!gamestate::MenuOpen()&&s.actionKnown&&
        idle;
    if(!valid) {
        char line[240];std::snprintf(line,sizeof line,"Frame development: action blocked choice=%d enabled=%d class=%d fresh=%d same_session=%d menu=%d mode_known=%d mode=%u request_age_ms=%llu",
            r.index,int(enabled),s.devClass,int(Fresh(s,r.session,now)),int(r.session==gamestate::LocalPlayer()),
            int(gamestate::MenuOpen()),int(s.actionKnown),s.actionMode,(unsigned long long)(now>=r.at?now-r.at:0));
        framevr::logging::Write(line);
    }
    return valid;
}
bool Current(uintptr_t claim) {
    std::lock_guard lock(mutex);return claim==ticket;
}
bool Claim(uintptr_t claim,framevr::interaction::Request& r) {
    std::lock_guard lock(mutex);
    if(claim!=ticket||!scheduled||queued.action==Action::None)return false;
    r=queued;queued={};return true;
}
void Finish(const framevr::interaction::Request& r,Result result,uintptr_t claim) {
    {
        std::lock_guard lock(mutex);if(claim!=ticket)return;
        scheduled=false;reply={r.sequence,r.session,result};
    }
    char line[180];std::snprintf(line,sizeof line,"Frame development: choice=%d result=%d (console dispatch does not confirm gameplay success)",r.index,int(result));
    framevr::logging::Write(line);
}
void __cdecl Commands(const PluginContext*,void* token) noexcept {
    const auto claim=reinterpret_cast<uintptr_t>(token);
    framevr::interaction::Request r;if(!Claim(claim,r))return;
    // Native console dispatch can render/pump messages while waiting. Those
    // frames call Status()/Cancel(), so no developer lock may cross this call.
    if(!Valid(r)||!execute){
        if(!execute)framevr::logging::Write("Frame development: console command export unavailable");
        Finish(r,Result::Refused,claim);return;
    }
    bool accepted=true;
    if(r.index==0) {
        const int cls=interactionstate::Read().devClass;
        if(!framevr::dev::ValidClass(cls)){Finish(r,Result::Refused,claim);return;}
        const auto send=[&](const char* command) {
            if(!Current(claim))return false;
            if(r.session!=gamestate::LocalPlayer()||!execute(command)) {
                char line[160];std::snprintf(line,sizeof line,"Frame development: preparation dispatch refused at %s",command);
                framevr::logging::Write(line);return false;
            }
            return true;
        };
        for(const auto* command:framevr::dev::Preparation) {
            if(!(accepted=send(command)))break;
        }
        if(accepted)for(const auto* skill:framevr::dev::Presets[cls].skills) {
            char command[128];std::snprintf(command,sizeof command,"cheat sklvl %s,20",skill);
            if(!(accepted=send(command)))break;
        }
        if(accepted)accepted=send("heal");
    } else {
        const auto* command=framevr::dev::Choices[r.index].command;
        if(!Current(claim))return;
        char line[128];std::snprintf(line,sizeof line,"Frame development: dispatching destination %s",command?command:"unavailable");
        framevr::logging::Write(line);
        accepted=command&&execute(command);
    }
    Finish(r,accepted?Result::Dispatched:Result::Refused,claim);
}
void __cdecl Gear(const PluginContext*,void* callbackToken) noexcept {
    const auto claim=reinterpret_cast<uintptr_t>(callbackToken);
    framevr::interaction::Request r;if(!Claim(claim,r))return;
    // r.session is the SDK handle captured on the UI thread by Collect().
    // getLocalPlayer itself is UI-thread-only; the transaction revalidates this
    // captured handle against the authoritative game on this callback thread.
    const PlayerHandle player=r.session;
    if(!Valid(r)||!items||!items->createItem||!items->destroyItem||!items->executeTransaction) {
        Finish(r,Result::Refused,claim);return;
    }
    const int cls=interactionstate::Read().devClass;
    if(!framevr::dev::ValidClass(cls)){Finish(r,Result::Refused,claim);return;}
    const auto codes=framevr::dev::Kit(cls,r.index);
    std::array<Items::ItemCreateSpec,10> specs{};
    std::array<ItemHandle,10> handles{};
    size_t count=0;
    for(size_t i=0;i<codes.size();++i) {
        if(!codes[i])continue;
        const bool ammo=framevr::dev::Ammo(codes[i]);
        auto& spec=specs[count++];spec.structSize=sizeof spec;
        spec.code=Items::MakeItemCode(codes[i][0],codes[i][1],codes[i][2]);
        spec.quality=ammo?Items::Quality::Normal:Items::Quality::Unique;spec.qualityRecordId=Items::RandomQualityRecord;
        spec.flags=ammo?0:Items::ItemCreateFlagBit(Items::ItemCreateFlag::AllowDuplicateUnique);
        spec.itemLevel=99;spec.seedMode=Items::SeedMode::Random;
        spec.quantity=spec.durability=Items::DefaultValue;spec.stateFlags=Items::ItemStateIdentified;
        if(ammo)spec.quantity=250;
        spec.destination.structSize=sizeof spec.destination;
        spec.destination.container=Items::ItemContainer::Inventory;
        spec.destination.placement=Items::Placement::Automatic;
    }
    // D2RLoader 1.3.1's exchange API requires at least one input. Use a newly
    // created inventory potion as that input, never one of the player's items.
    // Its cell is available to the output-placement transaction. The successful
    // exchange consumes it; a failed exchange leaves it for us to remove.
    Items::ItemCreateSpec tokenSpec{};tokenSpec.structSize=sizeof tokenSpec;
    tokenSpec.code=Items::MakeItemCode("hp1");tokenSpec.quality=Items::Quality::Normal;
    tokenSpec.qualityRecordId=Items::RandomQualityRecord;tokenSpec.itemLevel=1;
    tokenSpec.quantity=tokenSpec.durability=Items::DefaultValue;tokenSpec.stateFlags=Items::ItemStateIdentified;
    tokenSpec.destination.structSize=sizeof tokenSpec.destination;
    tokenSpec.destination.container=Items::ItemContainer::Inventory;
    tokenSpec.destination.placement=Items::Placement::Automatic;
    ItemHandle token=InvalidItemHandle;
    if(!Current(claim))return;
    const auto created=items->createItem(context,player,&tokenSpec,&token);
    if(created!=Items::Result::Success||!token) {
        char line[128];std::snprintf(line,sizeof line,"Frame development: gear exchange input creation failed, SDK result=%u",unsigned(created));
        framevr::logging::Write(line);Finish(r,Result::KitRefused,claim);return;
    }
    if(!Current(claim)) {
        items->destroyItem(context,player,token,Items::SocketedItemPolicy::RejectIfNotEmpty);
        return;
    }
    const Items::TransactionInput input{token,1,Items::SocketedItemPolicy::RejectIfNotEmpty};
    Items::Transaction transaction{};transaction.structSize=sizeof transaction;
    transaction.player=player;transaction.inputCount=1;transaction.inputs=&input;
    transaction.outputCount=uint32_t(count);transaction.outputs=specs.data();
    transaction.outputItems=handles.data();transaction.outputCapacity=uint32_t(handles.size());
    Items::TransactionResult result{};result.structSize=sizeof result;
    const auto status=items->executeTransaction(context,&transaction,&result);
    // The SDK rolls back the entire kit when validation or placement fails.
    const bool ok=status==Items::Result::Success&&result.outputCount==count;
    if(!ok) {
        // Native rollback preserved the temporary input. Remove only the
        // exact handle we created above; never enumerate/delete player items.
        const auto cleanup=items->destroyItem(context,player,token,Items::SocketedItemPolicy::RejectIfNotEmpty);
        char line[160];std::snprintf(line,sizeof line,"Frame development: gear transaction refused, SDK result=%u; temporary input cleanup=%u",unsigned(status),unsigned(cleanup));
        framevr::logging::Write(line);
    }
    Finish(r,ok?Result::Applied:Result::KitRefused,claim);
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
    framevr::logging::Write("Frame development: isolated pcvr-dev environment enabled; class presets require disposable offline expansion characters");
}
void Capture(framevr::interaction::Snapshot& s){s.developer=enabled;}
void Request(const framevr::interaction::Request& r) {
    const bool valid=Valid(r);
    const auto run=threads?(framevr::dev::IsKit(r.index)?threads->runOnGameThread:threads->runOnUiThread):nullptr;
    uintptr_t claim;
    {
        std::lock_guard lock(mutex);if(scheduled||queued.action!=Action::None)return;
        claim=++ticket;
        if(valid&&run) {
            queued=r;scheduled=true;reply={r.sequence,r.session,Result::Pending};
        }
    }
    if(!valid||!run){Finish(r,Result::Refused,claim);return;}
    // Scheduling is an external call too. Keep status/cancellation readable
    // even if a loader callback runs or pumps the UI before this returns.
    if(run(context,framevr::dev::IsKit(r.index)?Gear:Commands,reinterpret_cast<void*>(claim))!=D2RL::Threads::Result::Success) {
        {std::lock_guard lock(mutex);if(claim==ticket)queued={};}
        Finish(r,Result::Refused,claim);
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
