#include "dev_scenarios.h"

#ifdef D2RVR_DEV_TOOLS
#include <windows.h>
#include <D2RLPlugin/api.h>
#include <D2RLPlugin/core_exports.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstdio>
#include <atomic>
#include "interactionstate.h"
#include "../steamvr/dev_scenarios.h"
#include "../steamvr/logging.h"
namespace gamestate {uint64_t LocalPlayer();bool MenuOpen();}
namespace framevr {void SetTurnsKeepOwn(int);int TurnsKeepOwn();}   // transport: [stereo] turns_keep_own
namespace framevr {void SetTurnsSharp(bool);bool TurnsSharp();}     // and turns_sharp
namespace framevr {void SetTurnsFill(bool);bool TurnsFill();}       // and turns_fill
namespace framevr {void SetTurnsRims(bool);bool TurnsRims();}       // and turns_rims
namespace d2rcam {void SetSkyOverRims(bool);bool SkyOverRims();}    // vrcam: [sky] cover_rims
namespace d2rcam {bool SaveBodyTuning(int knob,float value);}          // vrcam: [bodies] (limb_tuning.h)

namespace devscenarios {
namespace {
using namespace D2RL;
using namespace framevr::interaction;
const PluginContext* context=nullptr;
const ThreadService* threads=nullptr;
const ItemService* items=nullptr;
CoreExports::ExecuteConsoleCommandFn execute=nullptr;
bool enabled=false;
std::atomic<bool> debugDraws{false};
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
    if(!Valid(r)){Finish(r,Result::Refused,claim);return;}
    if(r.index==framevr::dev::DebugDrawsChoice) {
        {
            std::lock_guard lock(mutex);if(claim!=ticket)return;
            debugDraws=!debugDraws.load();
        }
        framevr::logging::Write(debugDraws?"Frame development: debug draws ON (cyan measured shield, amber fallback; green blocked, red rejected, magenta native skip)":"Frame development: debug draws OFF");
        Finish(r,Result::DebugUpdated,claim);return;
    }
    // By turns A/B for the session; the ini's value applies again only when it changes.
    if(r.index==framevr::dev::TurnsKeepOwnChoice) {
        int mode;   // off, near things only, everywhere, the game's effects, near things and effects, and round
        {
            std::lock_guard lock(mutex);if(claim!=ticket)return;
            mode=(framevr::TurnsKeepOwn()+1)%5;
        }
        framevr::SetTurnsKeepOwn(mode);
        framevr::logging::Write(mode==1?"Frame development: by turns keeps the eye's own picture round near things only":
                                mode==2?"Frame development: by turns keeps the eye's own picture wherever both show the same surface":
                                mode==3?"Frame development: by turns keeps the eye's own picture where the game's effects are":
                                mode==4?"Frame development: by turns keeps the eye's own picture round near things and where the game's effects are":
                                "Frame development: by turns takes the drawn eye's picture wherever it reaches");
        Finish(r,Result::TurnsUpdated,claim);return;
    }
    if(r.index==framevr::dev::TurnsSharpChoice) {
        bool sharp;
        {
            std::lock_guard lock(mutex);if(claim!=ticket)return;
            sharp=!framevr::TurnsSharp();
        }
        framevr::SetTurnsSharp(sharp);
        framevr::logging::Write(sharp?"Frame development: by turns reads the eye not drawn sharp (Catmull-Rom)":"Frame development: by turns reads the eye not drawn soft (bilinear)");
        Finish(r,Result::SharpUpdated,claim);return;
    }
    if(r.index==framevr::dev::TurnsFillChoice) {
        bool fill;
        {
            std::lock_guard lock(mutex);if(claim!=ticket)return;
            fill=!framevr::TurnsFill();
        }
        framevr::SetTurnsFill(fill);
        framevr::logging::Write(fill?"Frame development: by turns removes frame-old ghosts beside near things":"Frame development: by turns keeps the eye's own picture beside near things");
        Finish(r,Result::FillUpdated,claim);return;
    }
    if(r.index==framevr::dev::TurnsRimsChoice) {
        bool rims;
        {
            std::lock_guard lock(mutex);if(claim!=ticket)return;
            rims=!framevr::TurnsRims();
        }
        framevr::SetTurnsRims(rims);
        framevr::logging::Write(rims?"Frame development: by turns moves the rims round things against the sky with them":"Frame development: by turns leaves the rims round things against the sky with the sky");
        Finish(r,Result::RimsUpdated,claim);return;
    }
    if(r.index==framevr::dev::SkyRimsChoice) {
        bool rims;
        {
            std::lock_guard lock(mutex);if(claim!=ticket)return;
            rims=!d2rcam::SkyOverRims();
        }
        d2rcam::SetSkyOverRims(rims);
        framevr::logging::Write(rims?"Frame development: the sky goes over the dark rim round things against it":"Frame development: the dark rim round things against the sky stays");
        Finish(r,Result::SkyRimsUpdated,claim);return;
    }
    // Bodies: a value's next step, saved to [bodies]; or all of them back to their defaults.
    if(const int knob=framevr::dev::BodyKnob(r.index);knob>=0||r.index==framevr::dev::BodyResetChoice) {
        {std::lock_guard lock(mutex);if(claim!=ticket)return;}
        char line[200];
        if(knob>=0) {
            const auto k=framevr::limbs::Knob(knob);const auto& info=framevr::limbs::KnobTable[size_t(knob)];
            const float value=framevr::limbs::NextStep(k,framevr::limbs::Tuned().Get(k));
            const bool saved=d2rcam::SaveBodyTuning(knob,value);
            std::snprintf(line,sizeof line,"Frame development: bodies %s = %g %s%s",info.name,value,info.unit,saved?"":" (not saved to the INI)");
        } else {
            bool saved=true;
            for(int k=0;k<framevr::limbs::Knobs;++k)saved=d2rcam::SaveBodyTuning(k,framevr::limbs::KnobTable[size_t(k)].standard)&&saved;
            std::snprintf(line,sizeof line,"Frame development: bodies back to their defaults%s",saved?"":" (not saved to the INI)");
        }
        framevr::logging::Write(line);
        Finish(r,Result::BodiesUpdated,claim);return;
    }
    if(!execute){
        framevr::logging::Write("Frame development: console command export unavailable");
        Finish(r,Result::Refused,claim);return;
    }
    bool accepted=true;
    if(r.index==framevr::dev::UnlockQuestsChoice) {
        const auto send=[&](const char* command) {
            if(!Current(claim))return false;
            if(r.session!=gamestate::LocalPlayer()||!execute(command)) {
                char line[160];std::snprintf(line,sizeof line,"Frame development: quest unlock dispatch refused at %s",command);
                framevr::logging::Write(line);return false;
            }
            return true;
        };
        accepted=send("cheat quests")&&send("cheat waypoints");
        Finish(r,accepted?Result::Dispatched:Result::Refused,claim);
        return;
    }
    if(const int level=framevr::dev::LevelFor(r.index)) {
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
        char command[128];std::snprintf(command,sizeof command,"cheat level %d",level);
        accepted=send(command);
        const int stat=level==90?300:level==60?200:level==30?100:50;
        for(const char* name:{"strength","dexterity","vitality","energy"}) {
            if(!accepted)break;
            const int value=std::string_view(name)=="vitality"?stat+level+10:stat;
            std::snprintf(command,sizeof command,"cheat stat %s,%d",name,value);
            accepted=send(command);
        }
        // Also reset locked skills to zero when moving down from a higher preset.
        if(accepted)for(int i=0;i<30;++i) {
            std::snprintf(command,sizeof command,"cheat sklvl %s,%d",framevr::dev::Presets[cls].skills[i],framevr::dev::SkillRank(cls,i,level));
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
    const auto s=interactionstate::Read();
    const int cls=s.devClass;
    if(!framevr::dev::ValidClass(cls)){Finish(r,Result::Refused,claim);return;}
    const int mercClassId=s.merc.known?int(s.merc.classId):-1;
    const auto codes=framevr::dev::Kit(cls,r.index,mercClassId);
    std::array<Items::ItemCreateSpec,10> specs{};
    std::array<ItemHandle,10> handles{};
    size_t count=0;
    for(size_t i=0;i<codes.size();++i) {
        if(!codes[i])continue;
        const bool ammo=framevr::dev::Ammo(codes[i]);
        auto& spec=specs[count++];spec.structSize=sizeof spec;
        spec.code=Items::MakeItemCode(codes[i][0],codes[i][1],codes[i][2]);
        const bool normal=ammo||framevr::dev::NormalKit(r.index);
        spec.quality=normal?Items::Quality::Normal:Items::Quality::Unique;spec.qualityRecordId=Items::RandomQualityRecord;
        spec.flags=normal?0:Items::ItemCreateFlagBit(Items::ItemCreateFlag::AllowDuplicateUnique);
        spec.itemLevel=framevr::dev::NormalKit(r.index)?1:99;spec.seedMode=Items::SeedMode::Random;
        spec.quantity=spec.durability=Items::DefaultValue;spec.stateFlags=Items::ItemStateIdentified;
        if(ammo)spec.quantity=250;
        if(r.index==framevr::dev::TorchChoice) {
            if(i==0||i==1) {
                spec.quality=Items::Quality::Normal;
                spec.socketCount=1;
                spec.itemLevel=1;
            } else if(i==2||i==3) {
                spec.quality=Items::Quality::Normal;
                spec.itemLevel=1;
            } else if(i==4) {
                spec.quality=Items::Quality::Unique;
                spec.qualityRecordId=Items::RandomQualityRecord;
                spec.flags=Items::ItemCreateFlagBit(Items::ItemCreateFlag::AllowDuplicateUnique);
                spec.itemLevel=99;
            } else if(i==5) {
                spec.quality=Items::Quality::Magic;
                spec.itemLevel=30;
            }
        }
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
    context=ctx;enabled=Metadata(ctx);debugDraws=false;
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
bool DebugDraws(){return enabled&&debugDraws.load();}
void Capture(framevr::interaction::Snapshot& s){s.developer=enabled;s.devDebugDraws=DebugDraws();s.devTurnsKeepOwn=enabled?framevr::TurnsKeepOwn():0;s.devTurnsSharp=enabled&&framevr::TurnsSharp();s.devTurnsFill=enabled&&framevr::TurnsFill();s.devTurnsRims=enabled&&framevr::TurnsRims();s.devSkyRims=enabled&&d2rcam::SkyOverRims();if(enabled)s.devBodies=framevr::limbs::Tuned().Get().value;}
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
bool DebugDraws(){return false;}
}
#endif
