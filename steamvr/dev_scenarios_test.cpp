// Exercise the actual backend with fake SDK callbacks; never loads a game.
#define D2RVR_DEVELOPER 1
#include "../vr/dev_scenarios.cpp"
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
    ++itemCalls;assert(t->player==local&&t->outputCount>=9&&t->outputCount<=10);
    (void)devscenarios::Status();
    // Match the installed loader: zero-input exchanges are InvalidArgument.
    if(!t->inputCount||!t->inputs)return Items::Result::InvalidArgument;
    assert(t->inputCount==1&&t->inputs[0].item==Token&&t->inputs[0].quantity==1&&tokenLive);
    grantedCodes.clear();
    for(unsigned i=0;i<t->outputCount;++i) {
        const auto& s=t->outputs[i];const bool ammo=s.code==Items::MakeItemCode("aqv");
        grantedCodes.push_back(s.code);
        assert(s.quality==(ammo?Items::Quality::Normal:Items::Quality::Unique)&&s.itemLevel==99);
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
        assert(commands.size()==before+std::size(Preparation)+31&&commands[before]=="cheat level 90");
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
            assert(commands.size()==start+std::size(Preparation)+31&&commands.back()=="heal");
            for(size_t i=0;i<30;++i) {
                const std::string expected=std::string("cheat sklvl ")+Presets[cls].skills[i]+",20";
                assert(commands[start+std::size(Preparation)+i]==expected);
            }
        }
        for(int choice:{1,2}) {
            devscenarios::Request(Make(choice));assert(tasks.back().game);Run(tasks.size()-1);
            assert(devscenarios::Status().result==Result::Applied&&!tokenLive);
            std::vector<uint32_t> expected;
            for(const char* code:Kit(cls,choice))if(code)expected.push_back(Items::MakeItemCode(code[0],code[1],code[2]));
            assert(grantedCodes==expected);
        }
    }
    // Skill/weapon requirements across the supplied alternatives.
    assert(Presets[0].primaryWeapons[1]==std::string_view("aqv"));
    assert(Presets[2].alternateWeapons[0]==std::string_view("7dg"));
    assert(Presets[4].alternateWeapons[0]==std::string_view("7ts"));
    assert(Presets[6].primaryWeapons[0]==std::string_view("9tw")&&Presets[6].primaryWeapons[1]==std::string_view("7wb"));
    // Tyrael's platform and Duriel's native arrival point are distinct.
    // Both use the same UI dispatch and stale-session guard as other travel.
    for(int choice:{TyraelChoice,DurielChoice}) {
        const auto before=commands.size();
        devscenarios::Request(Make(choice));assert(!tasks.back().game);Run(tasks.size()-1);
        assert(commands.size()==before+1&&commands.back()==(choice==TyraelChoice?"goto 73,22578,15594":"goto 73"));
        assert(devscenarios::Status().result==Result::Dispatched);
        devscenarios::Request(Make(choice));const auto travelTask=tasks.size()-1;
        const auto beforeStale=commands.size();++local;Run(travelTask);--local;
        assert(commands.size()==beforeStale&&devscenarios::Status().result==Result::Refused);
    }
    inlineQueue=true;devscenarios::Request(Make(3,100));
    assert(devscenarios::Status().sequence==100&&devscenarios::Status().result==Result::Dispatched);
    std::puts("Development backend: isolation, queue routing, cancellation, stale sessions, kit transactions and dispatch results passed");
}
