// Exercise the production command dispatcher with a fake loader/UI queue and
// native executor. No game, keyboard injection or rendering hooks are needed.
#include <windows.h>
#include <D2RLPlugin/api.h>
#include "dropped_item_labels.h"
#include "stick_gestures.h"
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
    framevr::stickgestures::Gestures stick;   // the right stick: x, y (down -1)
    auto sample=[&](float x,float y,bool allowed=true){
        sampleTime+=20;
        const bool show=stick.Step(allowed,x,y).showLoot;
        display.Refresh(player,GetTickCount64(),allowed,show);
    };
    auto tick=[&]{gamecmd::Tick();Drain(context);};
    sample(0,0);tick();assert(edges.empty()); // normal drop/hover labels
    sample(0,-1);tick();assert(edges.empty()); // key code not decrypted yet
    verified=true;sample(0,-1);tick();assert(held&&edges.size()==1); // retry while still held down
    for(int i=0;i<10;++i){sample(.2f,-.8f);tick();}assert(edges.size()==1); // no repeated presses
    sample(0,0);tick();assert(!held&&edges.size()==2); // letting go restores native behavior
    assert(display.Active(GetTickCount64())); // readability remains on with show-all off
    sample(0,-1);tick();assert(held);
    menu=true;tick();assert(!held); // menu before next XR refresh releases the command
    sample(0,-1,false);menu=false;sample(0,-1);tick();assert(!held); // no held replay after a menu
    sample(0,0);sample(0,-1);tick();assert(held);
    display.Clear();stick.Reset();tick();assert(!held); // lost focus, tracking or VR session
    sample(0,-1);tick();assert(!held); // the stick must come back to the centre on recovery
    sample(0,0);sample(0,-1);tick();assert(held);
    display.Refresh(player,GetTickCount64()-LeaseMs,true,true);tick();assert(!held); // stopped producer
    sample(0,0);sample(0,-1);tick();assert(held);
    ++player;sample(0,-1,false);sample(0,-1);tick();assert(!held); // a new character (loading between) needs a fresh push
    // A queued request must re-read the lease; never press Alt after entering a menu.
    sample(0,0);sample(0,-1);gamecmd::Tick();assert(queued);menu=true;Drain(context);assert(!held);
    sample(0,-1,false);menu=false;sample(0,0);sample(0,-1);tick();assert(held);
    display.Refresh(0,GetTickCount64(),false);tick();assert(!held); // leaving the world
    // Letting go while a press is queued must cancel that press, too.
    sample(0,0);sample(0,-1);gamecmd::Tick();assert(queued);sample(0,0);Drain(context);assert(!held);
    // A turn that slides down is still a turn, not the labels.
    sample(0,0);sample(1,0);sample(.3f,-.9f);tick();assert(!held);
    std::puts("Native Show Items: right stick down hold, release, queued cancellation and lifecycle guards passed");
}
