// Exercise the actual backend with fake SDK callbacks; never loads a game.
#define D2RVR_DEVELOPER 1
#include "../vr/dev_scenarios.cpp"
#include <cassert>
#include <vector>

using namespace D2RL;
using namespace framevr::interaction;
static Snapshot state;
static uint64_t local=7;
static bool menu=false,failItems=false,failCommand=false,failQueue=false;
static int itemCalls=0;
static std::vector<std::string> commands;
struct Task {Threads::Callback call;void* token;bool game;};
static std::vector<Task> tasks;
namespace gamestate {uint64_t LocalPlayer(){return local;}bool MenuOpen(){return menu;}}
namespace interactionstate {Snapshot Read(){return state;}}
namespace framevr::logging {void Write(const char*){}}
static Threads::Result Queue(Threads::Callback callback,void* data,bool game) {
    if(failQueue)return Threads::Result::Unavailable;
    tasks.push_back({callback,data,game});return Threads::Result::Success;
}
static Threads::Result __cdecl Ui(const PluginContext*,Threads::Callback c,void* d) noexcept {return Queue(c,d,false);}
static Threads::Result __cdecl Game(const PluginContext*,Threads::Callback c,void* d) noexcept {return Queue(c,d,true);}
static bool __fastcall Execute(const char* text) noexcept {commands.emplace_back(text);return !failCommand;}
static Items::Result __cdecl ItemsCall(const PluginContext*,const Items::Transaction* t,Items::TransactionResult* r) noexcept {
    ++itemCalls;assert(t->inputCount==0&&t->inputs==nullptr&&t->player==local&&t->outputCount==10);
    for(unsigned i=0;i<t->outputCount;++i) {
        const auto& s=t->outputs[i];assert(s.quality==Items::Quality::Unique&&s.itemLevel==99);
        assert(s.destination.container==Items::ItemContainer::Inventory);
        assert(s.quantity==Items::DefaultValue&&s.durability==Items::DefaultValue);
    }
    if(failItems)return Items::Result::PolicyRejected;
    r->outputCount=t->outputCount;return Items::Result::Success;
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
    assert(!ValidChoice(-1)&&!ValidChoice(8)&&ValidChoice(3));
    ThreadService threadService{};threadService.runOnUiThread=Ui;threadService.runOnGameThread=Game;
    ItemService itemService{};itemService.executeTransaction=ItemsCall;
    devscenarios::threads=&threadService;devscenarios::items=&itemService;
    devscenarios::execute=Execute;
    state.session=local;state.developer=state.devPaladin=state.actionKnown=true;state.actionMode=1;
    auto r=Make(3);devscenarios::Request(r);assert(tasks.empty()); // compiled dev, wrong environment
    devscenarios::enabled=true;
    devscenarios::Request(Make(3));assert(tasks.size()==1&&!tasks.back().game);
    Run(0);assert(commands.size()==1&&commands.back()=="goto 74");
    assert(devscenarios::Status().result==Result::Dispatched);
    devscenarios::Request(Make(1));assert(tasks.back().game);Run(1);
    assert(itemCalls==1&&devscenarios::Status().result==Result::Applied);
    failItems=true;devscenarios::Request(Make(2));Run(2);
    assert(itemCalls==2&&devscenarios::Status().result==Result::KitRefused);failItems=false;
    devscenarios::Request(Make(3));auto old=tasks.size()-1;devscenarios::Cancel();
    devscenarios::Request(Make(4,2));auto next=tasks.size()-1;
    Run(old);assert(commands.size()==1&&devscenarios::Status().result==Result::Pending);
    Run(next);assert(commands.back()=="goto 1");
    devscenarios::Request(Make(3));old=tasks.size()-1;++local;Run(old);
    assert(devscenarios::Status().result==Result::Refused);--local;
    for(int reason=0;reason<5;++reason) {
        r=Make(3);const auto count=tasks.size();
        if(reason==0)state.devPaladin=false;
        if(reason==1)menu=true;
        if(reason==2)r.at=GetTickCount64()-2000;
        if(reason==3)state.actionMode=0;
        if(reason==4)r.index=99;
        devscenarios::Request(r);assert(tasks.size()==count&&devscenarios::Status().result==Result::Refused);
        state.devPaladin=true;menu=false;state.actionMode=1;
    }
    // Preparation is available in town and never touches the item service.
    devscenarios::items=nullptr;
    for(uint32_t mode:{1u,2u,3u,5u,6u}) {
        state.actionMode=mode;const auto before=commands.size();
        devscenarios::Request(Make(0));assert(!tasks.back().game);Run(tasks.size()-1);
        assert(devscenarios::Status().result==Result::Dispatched);
        assert(commands.size()==before+std::size(Preparation)&&commands[before]=="cheat level 90");
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
    std::puts("Development backend: isolation, queue routing, cancellation, stale sessions, kit transactions and dispatch results passed");
}
