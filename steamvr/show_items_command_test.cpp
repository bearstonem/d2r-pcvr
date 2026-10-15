// Exercise the production command dispatcher with a fake loader/UI queue and
// native executor. No game, keyboard injection or rendering hooks are needed.
#include <windows.h>
#include <D2RLPlugin/api.h>
#include "dropped_item_labels.h"
#include "../cleanroom/sigscan/sigscan.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace gamecmd {void Init(const D2RL::PluginContext*);void Tick();}
namespace {
bool menu=false,verified=false,held=false;
int table=0;
std::vector<bool> edges;
D2RL::Threads::Callback queued=nullptr;
void* queuedData=nullptr;
bool Execute(const void* binding,void* nativeTable,bool force,bool down) {
    uint32_t command=0;std::memcpy(&command,binding,sizeof command);
    assert(command==0x25&&nativeTable==&table&&force); // actual Show Items command, not an arbitrary key
    assert(held!=down);held=down;edges.push_back(down);return true;
}
bool Down(const void* b,void* t,bool f){return Execute(b,t,f,true);}
bool Up(const void* b,void* t,bool f){return Execute(b,t,f,false);}
D2RL::Threads::Result __cdecl Queue(const D2RL::PluginContext*,D2RL::Threads::Callback cb,void* data) noexcept {
    assert(!queued);queued=cb;queuedData=data;return D2RL::Threads::Result::Success;
}
const D2RL::ThreadService threads{sizeof(D2RL::ThreadService),D2RL::ThreadService::AbiVersion,&Queue,nullptr};
D2RL::ServiceQueryResult __cdecl Query(const D2RL::PluginContext*,D2RL::ServiceId id,uint32_t version,const void** service) noexcept {
    assert(id==D2RL::ServiceId::Thread&&version==D2RL::ThreadService::AbiVersion);
    *service=&threads;return D2RL::ServiceQueryResult::Success;
}
void Drain(const D2RL::PluginContext& context){if(auto cb=std::exchange(queued,nullptr))cb(&context,queuedData);}
}
namespace gamestate {bool MenuOpen(){return menu;}}
namespace d2rsig {
bool Check(uint64_t,const void*,uint32_t){return verified;}
uint64_t Rva(uint64_t rva){return rva+16;} // use synthetic command bindings
uintptr_t Addr(uint64_t rva){
    switch(rva){case 0x11ff30:return uintptr_t(&Down);case 0x1203a0:return uintptr_t(&Up);case 0x22a7930:return uintptr_t(&table);}
    assert(false);return 0;
}
}
int main() {
    using namespace framevr::lootlabels;
    D2RL::PluginApi api{};api.apiSize=sizeof api;api.queryService=&Query;
    D2RL::PluginContext context{};context.contextSize=sizeof context;context.api=&api;
    gamecmd::Init(&context);
    uint64_t player=1,epoch=1,sampleTime=1000;
    Shortcut chord;
    auto sample=[&](float trigger,float grip,bool allowed=true){
        sampleTime+=20;
        const bool show=chord.Step(player,epoch,sampleTime,allowed,trigger,grip);
        display.Refresh(player,GetTickCount64(),allowed,show);
    };
    auto tick=[&]{gamecmd::Tick();Drain(context);};
    sample(0,0);tick();assert(edges.empty()); // normal drop/hover labels
    sample(1,1);tick();assert(edges.empty()); // key code not decrypted yet
    verified=true;sample(1,1);tick();assert(held&&edges.size()==1); // retry while still physically held
    for(int i=0;i<10;++i){sample(.7f,.8f);tick();}assert(edges.size()==1); // no repeated presses
    sample(0,1);tick();assert(!held&&edges.size()==2); // releasing trigger restores native behavior
    assert(display.Active(GetTickCount64())); // readability remains on with show-all off
    sample(1,1);tick();assert(held);
    sample(1,0);tick();assert(!held&&chord.Consumed()); // grip release hides labels but cannot cast
    sample(1,1);tick();assert(held&&chord.GripConsumed()); // regrip resumes labels without a grab
    menu=true;tick();assert(!held); // menu before next XR refresh releases the command
    sample(1,1,false);menu=false;sample(1,1);tick();assert(!held); // no held replay after a menu
    sample(0,0);sample(1,1);tick();assert(held);
    display.Clear();chord.Suspend();tick();assert(!held); // lost focus, tracking or VR session
    sample(1,1);tick();assert(!held); // fresh release is needed on recovery
    sample(0,0);sample(1,1);tick();assert(held);
    display.Refresh(player,GetTickCount64()-LeaseMs,true,true);tick();assert(!held); // stopped producer
    sampleTime+=1000;sample(1,1);tick();assert(!held); // stalled input cannot silently resume
    sample(0,0);sample(1,1);tick();assert(held);
    ++player;sample(1,1);tick();assert(!held); // new character cannot inherit a held chord
    // A queued request must re-read the lease; never press Alt after entering a menu.
    sample(0,0);sample(1,1);gamecmd::Tick();assert(queued);menu=true;Drain(context);assert(!held);
    sample(1,1,false);menu=false;sample(0,0);sample(1,1);tick();assert(held);
    display.Refresh(0,GetTickCount64(),false);tick();assert(!held); // leaving the world
    // Releasing while a press is queued must cancel that press, too.
    sample(0,0);sample(1,1);gamecmd::Tick();assert(queued);sample(0,1);Drain(context);assert(!held);
    // An ongoing normal attack cannot become a loot chord by adding grip.
    sample(0,0);sample(1,0);sample(1,1);tick();assert(!held&&!chord.Consumed());
    std::puts("Native Show Items: physical chord hold, either release, no combat replay, queued cancellation and lifecycle guards passed");
}
