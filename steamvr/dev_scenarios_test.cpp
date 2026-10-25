// Exercise the actual backend with fake SDK callbacks; never loads a game.
#define D2RVR_DEV_TOOLS 1
#include "../vr/dev_scenarios.cpp"
#include "weapon_skills.h"
#include <cassert>
#include <vector>
#include <functional>

using namespace D2RL;
using namespace framevr::interaction;
static Snapshot state;
static uint64_t local=7;
static bool menu=false,failItems=false,failCommand=false,failQueue=false,failToken=false,tokenLive=false;
static bool inlineQueue=false,cancelAfterToken=false;
static std::function<void()> onExecute;
static int itemCalls=0,tokenCreates=0,tokenRemoves=0;
static constexpr ItemHandle Token=9001;
static std::vector<std::string> commands;
static std::vector<uint32_t> grantedCodes;
struct Task {Threads::Callback call;void* token;bool game;};
static std::vector<Task> tasks;
namespace gamestate {uint64_t LocalPlayer(){return local;}bool MenuOpen(){return menu;}}
namespace interactionstate {Snapshot Read(){return state;}}
namespace framevr::logging {void Write(const char*){}}
static int keepOwn=3,motion=1;static bool sharp=true,fill=true;
namespace framevr {void SetTurnsKeepOwn(int mode){keepOwn=mode;}int TurnsKeepOwn(){return keepOwn;}}
namespace framevr {void SetTurnsSharp(bool on){sharp=on;}bool TurnsSharp(){return sharp;}}
namespace framevr {void SetTurnsFill(bool on){fill=on;}bool TurnsFill(){return fill;}}
namespace framevr {void SetTurnsMotion(int mode){motion=mode;}int TurnsMotion(){return motion;}}

static Threads::Result Queue(Threads::Callback callback,void* data,bool game) {
    assert(devscenarios::Status().result==Result::Pending);
    if(failQueue)return Threads::Result::Unavailable;
    tasks.push_back({callback,data,game});
    if(inlineQueue)callback(nullptr,data);
    return Threads::Result::Success;
}
static Threads::Result __cdecl Ui(const PluginContext*,Threads::Callback c,void* d) noexcept {return Queue(c,d,false);}
static Threads::Result __cdecl Game(const PluginContext*,Threads::Callback c,void* d) noexcept {return Queue(c,d,true);}
static bool __fastcall Execute(const char* text) noexcept {
    // Native travel can render another frame before dispatch returns. Its
    // interaction status read must never block on the command callback.
    assert(devscenarios::Status().result==Result::Pending);
    commands.emplace_back(text);if(onExecute)onExecute();return !failCommand;
}
static Items::Result __cdecl CreateToken(const PluginContext*,PlayerHandle player,const Items::ItemCreateSpec* s,ItemHandle* out) noexcept {
    ++tokenCreates;assert(!tokenLive&&player==local);
    (void)devscenarios::Status();
    assert(s->code==Items::MakeItemCode("hp1")&&s->quality==Items::Quality::Normal);
    assert(s->destination.container==Items::ItemContainer::Inventory&&s->itemLevel==1);
    if(failToken)return Items::Result::PolicyRejected;
    tokenLive=true;*out=Token;
    if(cancelAfterToken)devscenarios::Cancel();
    return Items::Result::Success;
}
static Items::Result __cdecl RemoveToken(const PluginContext*,PlayerHandle player,ItemHandle item,Items::SocketedItemPolicy policy) noexcept {
    assert(player==local&&item==Token&&tokenLive&&policy==Items::SocketedItemPolicy::RejectIfNotEmpty);
    (void)devscenarios::Status();
    ++tokenRemoves;tokenLive=false;return Items::Result::Success;
}
static Items::Result __cdecl ItemsCall(const PluginContext*,const Items::Transaction* t,Items::TransactionResult* r) noexcept {
    ++itemCalls;assert(t->player==local&&(t->outputCount==1||t->outputCount==2||(t->outputCount>=5&&t->outputCount<=10)));
    if(t->outputCount==1)assert(t->outputs[0].code==Items::MakeItemCode("buc")||t->outputs[0].code==Items::MakeItemCode("clb"));
    if(t->outputCount==5){constexpr const char* kit[]={"jav","pil","glv","tkf","tax"};   // the throwing kit
        for(unsigned i=0;i<5;++i)assert(t->outputs[i].code==Items::MakeItemCode(kit[i][0],kit[i][1],kit[i][2]));}
    if(t->outputCount==2) {
        const bool bow=t->outputs[0].code==Items::MakeItemCode("6lw");
        assert(bow||t->outputs[0].code==Items::MakeItemCode("lxb"));
        assert(t->outputs[1].code==Items::MakeItemCode(bow?"aqv":"cqv"));
    }
    (void)devscenarios::Status();
    // Match the installed loader: zero-input exchanges are InvalidArgument.
    if(!t->inputCount||!t->inputs)return Items::Result::InvalidArgument;
    assert(t->inputCount==1&&t->inputs[0].item==Token&&t->inputs[0].quantity==1&&tokenLive);
    grantedCodes.clear();
    for(unsigned i=0;i<t->outputCount;++i) {
        const auto& s=t->outputs[i];const bool ammo=s.code==Items::MakeItemCode("aqv")||s.code==Items::MakeItemCode("cqv");
        grantedCodes.push_back(s.code);
        const bool potions=t->outputCount==10&&t->outputs[0].code==Items::MakeItemCode("hp5"); // the potion kit
        const bool torch=t->outputCount==6&&t->outputs[0].code==Items::MakeItemCode("clb");
        const bool starter=(t->outputCount<9&&t->outputCount>2)||potions;
        if(torch) {
            if(i==0||i==1)assert(s.quality==Items::Quality::Normal&&s.socketCount==1&&s.itemLevel==1);
            else if(i==2||i==3)assert(s.quality==Items::Quality::Normal&&s.itemLevel==1);
            else if(i==4)assert(s.quality==Items::Quality::Unique&&s.itemLevel==99);
            else if(i==5)assert(s.quality==Items::Quality::Magic&&s.itemLevel==30);
        } else {
            assert(s.quality==((ammo||starter)?Items::Quality::Normal:Items::Quality::Unique)&&s.itemLevel==(starter?1:99));
        }
        assert(s.destination.container==Items::ItemContainer::Inventory);
        assert(s.quantity==(ammo?250:Items::DefaultValue)&&s.durability==Items::DefaultValue);
    }
    if(failItems)return Items::Result::PolicyRejected;
    tokenLive=false;r->outputCount=t->outputCount;return Items::Result::Success;
}
static framevr::interaction::Request Make(int index,uint64_t sequence=1) {
    state.at=GetTickCount64();return {Action::Developer,sequence,local,state.at,index};
}
static void Run(size_t i){tasks[i].call(nullptr,tasks[i].token);}
int main(){
    using namespace framevr::dev;
    assert(Isolated(Mod,ModInfo));assert(!Isolated("other",ModInfo));
    assert(!Isolated(Mod,"{\"name\":\"pcvr-dev\",\"savepath\":\"\"}\n"));
    {
        // Match the real loader context: modDirectory already ends in .mpq.
        wchar_t temporary[MAX_PATH]{},name[MAX_PATH]{};
        assert(GetTempPathW(MAX_PATH,temporary)&&GetTempFileNameW(temporary,L"dev",0,name));
        const std::filesystem::path root=std::wstring(name)+L"-mod";
        const auto data=root/L"pcvr-dev.mpq";
        std::filesystem::create_directories(data);
        const auto directory=data.wstring();
        PluginContext ctx{};ctx.activeMod=Mod;ctx.modDirectory=directory.c_str();
        assert(!devscenarios::Metadata(&ctx)); // missing metadata stays disabled
        {std::ofstream file(data/L"modinfo.json",std::ios::binary);file<<ModInfo;}
        assert(devscenarios::Metadata(&ctx));
        ctx.activeMod="other";assert(!devscenarios::Metadata(&ctx));ctx.activeMod=Mod;
        {std::ofstream file(data/L"modinfo.json",std::ios::binary);file<<"{\"name\":\"pcvr-dev\",\"savepath\":\"\"}";}
        assert(!devscenarios::Metadata(&ctx)); // wrong save path stays disabled
        std::filesystem::remove(data/L"modinfo.json");
        std::filesystem::remove(data);std::filesystem::remove(root);
        assert(DeleteFileW(name));
    }
    assert(!ValidChoice(-1)&&!ValidChoice(int(Choices.size()))&&ValidChoice(3)&&ValidChoice(TyraelChoice)&&ValidChoice(DurielChoice));
    ThreadService threadService{};threadService.runOnUiThread=Ui;threadService.runOnGameThread=Game;
    ItemService itemService{};itemService.executeTransaction=ItemsCall;
    itemService.createItem=CreateToken;itemService.destroyItem=RemoveToken;
    devscenarios::threads=&threadService;devscenarios::items=&itemService;
    devscenarios::execute=Execute;
    state.session=local;state.developer=state.actionKnown=true;state.devClass=3;state.actionMode=1;
    auto r=Make(3);devscenarios::Request(r);assert(tasks.empty()); // compiled dev, wrong environment
    devscenarios::enabled=true;
    devscenarios::Request(Make(3));assert(tasks.size()==1&&!tasks.back().game);
    Run(0);assert(commands.size()==1&&commands.back()=="goto 74");
    assert(devscenarios::Status().result==Result::Dispatched);
    devscenarios::Request(Make(1));assert(tasks.back().game);Run(1);
    assert(itemCalls==1&&devscenarios::Status().result==Result::Applied);
    assert(tokenCreates==1&&tokenRemoves==0&&!tokenLive); // exchange consumed its input
    failItems=true;devscenarios::Request(Make(2));Run(2);
    assert(itemCalls==2&&devscenarios::Status().result==Result::KitRefused);failItems=false;
    assert(tokenCreates==2&&tokenRemoves==1&&!tokenLive); // rejected kit left no temporary item
    devscenarios::Request(Make(3));auto old=tasks.size()-1;devscenarios::Cancel();
    devscenarios::Request(Make(4,2));auto next=tasks.size()-1;
    Run(old);assert(commands.size()==1&&devscenarios::Status().result==Result::Pending);
    Run(next);assert(commands.back()=="goto 1");
    devscenarios::Request(Make(3));old=tasks.size()-1;++local;Run(old);
    assert(devscenarios::Status().result==Result::Refused);--local;
    for(int reason=0;reason<5;++reason) {
        r=Make(3);const auto count=tasks.size();
        if(reason==0)state.devClass=-1;
        if(reason==1)menu=true;
        if(reason==2)r.at=GetTickCount64()-2000;
        if(reason==3)state.actionMode=0;
        if(reason==4)r.index=99;
        devscenarios::Request(r);assert(tasks.size()==count&&devscenarios::Status().result==Result::Refused);
        state.devClass=3;menu=false;state.actionMode=1;
    }
    // Preparation is available in town and never touches the item service.
    devscenarios::items=nullptr;
    for(uint32_t mode:{1u,2u,3u,5u,6u}) {
        state.actionMode=mode;const auto before=commands.size();
        devscenarios::Request(Make(0));assert(!tasks.back().game);Run(tasks.size()-1);
        assert(devscenarios::Status().result==Result::Dispatched);
        assert(commands.size()==before+PreparationCount+31&&commands[before]=="cheat level 90");
        assert(itemCalls==2);
    }
    for(uint32_t mode:{0u,4u,7u,17u}) {
        state.actionMode=mode;const auto before=tasks.size();devscenarios::Request(Make(0));
        assert(tasks.size()==before&&devscenarios::Status().result==Result::Refused);
    }
    devscenarios::items=&itemService;state.actionMode=5;
    failCommand=true;devscenarios::Request(Make(0));const auto before=commands.size();Run(tasks.size()-1);
    assert(commands.size()==before+1&&devscenarios::Status().result==Result::Refused);failCommand=false;
    failQueue=true;devscenarios::Request(Make(1));assert(devscenarios::Status().result==Result::Refused);
    failQueue=false;devscenarios::Request(Make(1));assert(devscenarios::Status().result==Result::Pending);
    devscenarios::Cancel();Run(tasks.size()-1);assert(devscenarios::Status().sequence==0);
    failToken=true;const auto transactions=itemCalls,removals=tokenRemoves;
    devscenarios::Request(Make(1));Run(tasks.size()-1);
    assert(devscenarios::Status().result==Result::KitRefused&&!tokenLive);
    assert(itemCalls==transactions&&tokenRemoves==removals); // no exchange/deletion when input creation fails
    failToken=false;
    // Cancellation while a native call pumps the UI stops preparation and
    // does not allow its old completion to replace a newer request's reply.
    onExecute=[] {devscenarios::Cancel();};
    const auto beforeCancel=commands.size();devscenarios::Request(Make(0));Run(tasks.size()-1);
    assert(commands.size()==beforeCancel+1&&devscenarios::Status().sequence==0);
    onExecute=[] {devscenarios::Cancel();devscenarios::Request(Make(4,99));};
    devscenarios::Request(Make(3));const auto travel=tasks.size()-1;Run(travel);
    assert(devscenarios::Status().sequence==99&&devscenarios::Status().result==Result::Pending);
    onExecute={};Run(tasks.size()-1);assert(devscenarios::Status().sequence==99&&devscenarios::Status().result==Result::Dispatched);
    // A cancellation after temporary input creation still removes that input.
    cancelAfterToken=true;const auto beforeExchange=itemCalls;
    devscenarios::Request(Make(1));Run(tasks.size()-1);
    assert(!tokenLive&&itemCalls==beforeExchange&&devscenarios::Status().sequence==0);cancelAfterToken=false;
    // Every class gets its own 30 exact skill assignments, also on repeated
    // preparation, and the correct weapon/ammo transaction for both kits.
    assert(ExpansionClass(0,1)==-1&&ExpansionClass(6,1)==-1&&ExpansionClass(7,2)==7);
    assert(ExpansionClass(7,3)==7&&ExpansionClass(8,3)==-1);
    for(int cls=0;cls<8;++cls) {
        assert(ExpansionClass(cls,3)==cls);state.devClass=cls;
        for(int repeat=0;repeat<2;++repeat) {
            const auto start=commands.size();devscenarios::Request(Make(0));Run(tasks.size()-1);
            assert(devscenarios::Status().result==Result::Dispatched);
            assert(commands.size()==start+PreparationCount+31&&commands.back()=="heal");
            for(size_t i=0;i<30;++i) {
                const std::string expected=std::string("cheat sklvl ")+Presets[cls].skills[i]+",20";
                assert(commands[start+PreparationCount+i]==expected);
            }
        }
        for(int choice:{1,2,CrossbowChoice,BowChoice,SmallShieldChoice,StarterPrimaryChoice,StarterAlternateChoice,ThrowingChoice,PotionsChoice,TorchChoice,ThrowingPotionsChoice}) {
            const auto commandCount=commands.size();
            devscenarios::Request(Make(choice));assert(tasks.back().game);Run(tasks.size()-1);
            assert(devscenarios::Status().result==Result::Applied&&!tokenLive);
            assert(commands.size()==commandCount); // gear does not change stats, bindings or destination
            std::vector<uint32_t> expected;
            for(const char* code:Kit(cls,choice))if(code)expected.push_back(Items::MakeItemCode(code[0],code[1],code[2]));
            assert(grantedCodes==expected);
        }
    }
    // Downshifting must explicitly clear higher-tier skills for every class.
    for(int cls=0;cls<8;++cls)for(int choice:{0,Level10Choice,Level30Choice,Level60Choice,Level10Choice}) {
        state.devClass=cls;const auto start=commands.size();
        devscenarios::Request(Make(choice));Run(tasks.size()-1);
        assert(commands[start]=="cheat level "+std::to_string(LevelFor(choice)));
        assert(commands.size()==start+PreparationCount+31);
        for(int i=0;i<30;++i)assert(commands[start+PreparationCount+i]==
            "cheat sklvl "+std::string(Presets[cls].skills[i])+","+std::to_string(SkillRank(cls,i,LevelFor(choice))));
    }
    assert(SkillRank(0,29,10)==0&&SkillRank(0,29,30)==1&&SkillRank(0,29,60)==20);
    assert(SkillRank(4,12,10)==5&&SkillRank(4,13,10)==0); // Shout vs Stun
    assert(SkillRank(7,0,10)==10&&SkillRank(7,2,10)==5&&SkillRank(7,3,10)==0);
    for(int i=0;i<int(Choices.size());++i)assert(PageFor(i)>=0&&SlotFor(i)>=0);
    // Skill/weapon requirements across the supplied alternatives.
    assert(Presets[0].primaryWeapons[1]==std::string_view("aqv"));
    assert(Presets[2].alternateWeapons[0]==std::string_view("7dg"));
    assert(Presets[4].alternateWeapons[0]==std::string_view("7ts"));
    // Distinct one-hand weapons make it possible to verify each rendered
    // blade's damage, reach and controller feedback independently.
    assert(Kit(4,1)[0]==std::string_view("7cr")&&Kit(4,1)[1]==std::string_view("7wa"));
    assert(Presets[6].primaryWeapons[0]==std::string_view("9tw")&&Presets[6].primaryWeapons[1]==std::string_view("7wb"));
    // Extra test weapons never replace the Paladin's large shield.
    for(int choice:{1,2})assert(Kit(3,choice)[1]==std::string_view("pa9"));
    // The throwing kit: one of each thrown missile, the same for every class, on the equipment page.
    assert(IsKit(ThrowingChoice)&&NormalKit(ThrowingChoice)&&!NormalKit(BowChoice)&&PageFor(ThrowingChoice)==1);
    for(int cls=0;cls<8;++cls) {
        std::vector<std::string_view> codes;for(const char* code:Kit(cls,ThrowingChoice))if(code)codes.push_back(code);
        assert((codes==std::vector<std::string_view>{"jav","pil","glv","tkf","tax"}));
    }
    // The potion kit: the four potions a hireling takes from a throw, for every class.
    assert(IsKit(PotionsChoice)&&NormalKit(PotionsChoice)&&PageFor(PotionsChoice)==1);
    for(int cls=0;cls<8;++cls) {
        std::vector<std::string_view> codes;for(const char* code:Kit(cls,PotionsChoice))if(code)codes.push_back(code);
        assert((codes==std::vector<std::string_view>{"hp5","hp5","hp5","hp5","rvl","rvl","yps","yps","wms","wms"}));
    }
    // The throwing potion kit: one stack of each gas and oil potion, normal, for every class.
    assert(IsKit(ThrowingPotionsChoice)&&NormalKit(ThrowingPotionsChoice)&&PageFor(ThrowingPotionsChoice)==0&&SlotFor(ThrowingPotionsChoice)==6);
    for(int cls=0;cls<8;++cls) {
        std::vector<std::string_view> codes;for(const char* code:Kit(cls,ThrowingPotionsChoice))if(code)codes.push_back(code);
        assert((codes==std::vector<std::string_view>{"gps","gpm","gpl","ops","opm","opl"}));
        for(const auto code:codes)assert(framevr::weaponskills::ThrowingPotion(code));
    }
    // The torch kit: a one-handed club for dynamic lighting and melee, for every class.
    assert(IsKit(TorchChoice)&&NormalKit(TorchChoice)&&PageFor(TorchChoice)==1);
    for(int cls=0;cls<8;++cls) {
        std::vector<std::string_view> codes;for(const char* code:Kit(cls,TorchChoice))if(code)codes.push_back(code);
        assert((codes==std::vector<std::string_view>{"clb","clb","gcr","r08","clb","clb"}));
    }
    // Unlock quests & waypoints button: sends cheat quests and cheat waypoints via console
    assert(ValidChoice(UnlockQuestsChoice)&&PageFor(UnlockQuestsChoice)==0&&SlotFor(UnlockQuestsChoice)==5);
    const auto beforeQuestCommands = commands.size();
    devscenarios::Request(Make(UnlockQuestsChoice));
    assert(!tasks.back().game); // Runs on UI thread like all console commands
    Run(tasks.size() - 1);
    assert(devscenarios::Status().result == Result::Dispatched);
    assert(commands.size() == beforeQuestCommands + 2);
    assert(commands[beforeQuestCommands] == "cheat quests");
    assert(commands[beforeQuestCommands + 1] == "cheat waypoints");

    // Failed standalone kits clean up only the generated exchange token.
    failItems=true;
    for(int choice:{CrossbowChoice,BowChoice,SmallShieldChoice,ThrowingChoice,PotionsChoice,TorchChoice,ThrowingPotionsChoice}) {
        const auto beforeCleanup=tokenRemoves;
        devscenarios::Request(Make(choice));Run(tasks.size()-1);
        assert(devscenarios::Status().result==Result::KitRefused&&!tokenLive&&tokenRemoves==beforeCleanup+1);
    }
    failItems=false;
    // Inspection destinations use native UI dispatch and stale-session guards.
    for(int choice=0;choice<int(Choices.size());++choice) {
        if(!Choices[choice].command)continue;
        const auto before=commands.size();
        devscenarios::Request(Make(choice));assert(!tasks.back().game);Run(tasks.size()-1);
        const char* expected=Choices[choice].command;
        assert(commands.size()==before+1&&commands.back()==expected);
        assert(devscenarios::Status().result==Result::Dispatched);
        devscenarios::Request(Make(choice));const auto travelTask=tasks.size()-1;
        const auto beforeStale=commands.size();++local;Run(travelTask);--local;
        assert(commands.size()==beforeStale&&devscenarios::Status().result==Result::Refused);
    }
    // Debug draws are a session toggle, with no console/item operations. A
    // cancelled or stale queued action cannot unexpectedly enable the overlay.
    const auto debugCommands=commands.size();const auto debugItems=itemCalls;
    assert(!devscenarios::DebugDraws());
    devscenarios::execute=nullptr;
    for(bool expected:{true,false}) {
        devscenarios::Request(Make(DebugDrawsChoice));assert(!tasks.back().game);Run(tasks.size()-1);
        assert(devscenarios::Status().result==Result::DebugUpdated&&devscenarios::DebugDraws()==expected);
        Snapshot snapshot;devscenarios::Capture(snapshot);assert(snapshot.devDebugDraws==expected);
    }
    devscenarios::Request(Make(DebugDrawsChoice));devscenarios::Cancel();Run(tasks.size()-1);
    assert(!devscenarios::DebugDraws());
    devscenarios::Request(Make(DebugDrawsChoice));++local;Run(tasks.size()-1);--local;
    assert(!devscenarios::DebugDraws()&&devscenarios::Status().result==Result::Refused);
    // The By turns switch steps the transport's [stereo] turns_keep_own for
    // the session (near things and effects, off, near things, everywhere, effects), also with no
    // console/item operations; a stale one does not.
    for(int expected:{4,0,1,2,3}) {
        devscenarios::Request(Make(TurnsKeepOwnChoice));assert(!tasks.back().game);Run(tasks.size()-1);
        assert(devscenarios::Status().result==Result::TurnsUpdated&&keepOwn==expected);
        Snapshot snapshot;devscenarios::Capture(snapshot);assert(snapshot.devTurnsKeepOwn==expected);
    }
    devscenarios::Request(Make(TurnsKeepOwnChoice));++local;Run(tasks.size()-1);--local;
    assert(keepOwn==3&&devscenarios::Status().result==Result::Refused);
    // The filter switch flips [stereo] turns_sharp the same way (soft, sharp).
    for(bool expected:{false,true}) {
        devscenarios::Request(Make(TurnsSharpChoice));assert(!tasks.back().game);Run(tasks.size()-1);
        assert(devscenarios::Status().result==Result::SharpUpdated&&sharp==expected);
        Snapshot snapshot;devscenarios::Capture(snapshot);assert(snapshot.devTurnsSharp==expected&&snapshot.devTurnsKeepOwn==3);
    }
    devscenarios::Request(Make(TurnsSharpChoice));++local;Run(tasks.size()-1);--local;
    assert(sharp&&devscenarios::Status().result==Result::Refused);
    // And the ghosts switch [stereo] turns_fill (as before, fixed).
    for(bool expected:{false,true}) {
        devscenarios::Request(Make(TurnsFillChoice));assert(!tasks.back().game);Run(tasks.size()-1);
        assert(devscenarios::Status().result==Result::FillUpdated&&fill==expected);
        Snapshot snapshot;devscenarios::Capture(snapshot);assert(snapshot.devTurnsFill==expected&&snapshot.devTurnsSharp);
    }
    devscenarios::Request(Make(TurnsFillChoice));++local;Run(tasks.size()-1);--local;
    assert(fill&&devscenarios::Status().result==Result::Refused);
    // The motion switch steps [stereo] turns_motion the same way (shown, off, on).
    for(int expected:{2,0,1}) {
        devscenarios::Request(Make(TurnsMotionChoice));assert(!tasks.back().game);Run(tasks.size()-1);
        assert(devscenarios::Status().result==Result::MotionUpdated&&motion==expected);
        Snapshot snapshot;devscenarios::Capture(snapshot);assert(snapshot.devTurnsMotion==expected&&snapshot.devTurnsKeepOwn==3);
    }
    devscenarios::Request(Make(TurnsMotionChoice));++local;Run(tasks.size()-1);--local;
    assert(motion==1&&devscenarios::Status().result==Result::Refused);
    assert(commands.size()==debugCommands&&itemCalls==debugItems);
    devscenarios::execute=Execute;
    inlineQueue=true;devscenarios::Request(Make(3,100));
    assert(devscenarios::Status().sequence==100&&devscenarios::Status().result==Result::Dispatched);
    std::puts("Development backend: isolation, queue routing, cancellation, stale sessions, kit transactions and dispatch results passed");
}
