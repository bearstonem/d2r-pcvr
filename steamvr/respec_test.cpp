// The real Reset Skills & Stats engine (vr/respec.cpp) against synthetic
// loader services: queued game/UI callbacks, a scripted createItem and use.
// Never opens a game.
#include <windows.h>
#include <D2RLPlugin/api.h>
#include "../vr/respec.h"
#include "../vr/quick_items.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace D2RL;
namespace {
uint64_t session=10;
bool offline=true,clientSees=true,consumes=true,identityReadable=true;
Items::Result createResult=Items::Result::Success;
ItemHandle nextHandle=100;
std::vector<ItemHandle> alive;           // tokens in the inventory
int creates=0,uses=0,destroys=0;
std::vector<std::pair<Threads::Callback,void*>> game,ui;
bool Alive(ItemHandle h){for(auto a:alive)if(a==h)return true;return false;}
void Drop(ItemHandle h){for(size_t i=0;i<alive.size();++i)if(alive[i]==h){alive.erase(alive.begin()+long(i));return;}}
Items::Result __cdecl Create(const PluginContext*,PlayerHandle p,const Items::ItemCreateSpec* s,ItemHandle* out) noexcept {
    assert(p==session&&s->code==Items::MakeItemCode("toa")&&s->quality==Items::Quality::Normal);
    assert(s->destination.container==Items::ItemContainer::Inventory&&s->destination.placement==Items::Placement::Automatic);
    ++creates;if(createResult!=Items::Result::Success)return createResult;
    *out=nextHandle++;alive.push_back(*out);return Items::Result::Success;
}
// The game thread reads the new token's identity: runtime id 1000 + handle (the client's id).
Items::Result __cdecl Info(const PluginContext*,ItemHandle h,Items::ItemInfo* out) noexcept {
    if(!Alive(h)||!identityReadable)return Items::Result::NotFound;
    out->handle=h;out->container=Items::ItemContainer::Inventory;out->runtimeId=uint32_t(1000+h);out->classId=2136;
    out->code=Items::MakeItemCode("toa");return Items::Result::Success;
}
Items::Result __cdecl Destroy(const PluginContext*,PlayerHandle p,ItemHandle h,Items::SocketedItemPolicy) noexcept {
    assert(p==session);++destroys;if(!Alive(h))return Items::Result::NotFound;Drop(h);return Items::Result::Success;
}
Threads::Result __cdecl OnGame(const PluginContext*,Threads::Callback c,void* u) noexcept {game.push_back({c,u});return Threads::Result::Success;}
Threads::Result __cdecl OnUi(const PluginContext*,Threads::Callback c,void* u) noexcept {ui.push_back({c,u});return Threads::Result::Success;}
ItemService items{};ThreadService threads{};
ServiceQueryResult __cdecl Query(const PluginContext*,ServiceId id,uint32_t,const void** out) noexcept {
    if(id==ServiceId::Item){*out=&items;return ServiceQueryResult::Success;}
    if(id==ServiceId::Thread){*out=&threads;return ServiceQueryResult::Success;}
    *out=nullptr;return ServiceQueryResult::Unavailable;
}
PluginContext context{};
void RunGame(){auto q=game;game.clear();for(auto& [c,u]:q)c(&context,u);}
}
namespace gamestate {uint64_t LocalPlayer(){return session;}}
namespace offlinedifficulty {bool Offline(){return offline;}}
namespace quickitems {
const char* UseResultName(UseResult){return "test";}
UseResult UseClientItem(uint64_t s,const ClientItem& i,uint32_t*,unsigned,unsigned* count){
    if(count)*count=0;
    assert(i.classId==2136&&i.code==Items::MakeItemCode("toa"));
    const ItemHandle h=ItemHandle(i.runtimeId-1000);   // the client knows it by its id, not a handle
    if(s!=session)return UseResult::NoPlayer;
    if(!clientSees||!Alive(h))return UseResult::Missing;
    ++uses;if(consumes)Drop(h);return UseResult::Used;
}
bool HoldsClientItem(uint64_t s,const ClientItem& i,bool& holds){
    if(s!=session)return false;holds=Alive(ItemHandle(i.runtimeId-1000));return true;
}
}
namespace framevr::logging {void Write(const char* line){std::printf("  %s\n",line);}}
int main() {
    using respec::State;
    items.serviceSize=sizeof items;items.serviceVersion=ItemService::AbiVersion;
    items.createItem=&Create;items.getItemInfo=&Info;items.destroyItem=&Destroy;
    threads.runOnGameThread=&OnGame;threads.runOnUiThread=&OnUi;
    PluginApi api{};api.apiSize=sizeof api;api.queryService=&Query;
    context.contextSize=sizeof context;context.api=&api;
    respec::Init(&context);
    const uint64_t t=GetTickCount64();

    // The reset: mint, use once when the client holds it, consumed: Done.
    assert(respec::Available()&&respec::Status()==State::Idle);
    assert(respec::Request()&&respec::Status()==State::Busy);
    assert(!respec::Request());                                   // one at a time
    respec::PollAt(t);assert(!uses);                              // nothing until it is made
    RunGame();assert(creates==1&&alive.size()==1);
    respec::PollAt(t+200);assert(uses==1&&alive.empty());
    respec::PollAt(t+400);assert(respec::Status()==State::Done&&uses==1&&!destroys);
    respec::PollAt(t+600);assert(uses==1);                        // never a second use

    // The client is slow to show it: wait, then use it once.
    clientSees=false;assert(respec::Request());RunGame();
    respec::PollAt(t+200);respec::PollAt(t+1000);assert(uses==1&&respec::Status()==State::Busy);
    clientSees=true;respec::PollAt(t+1200);respec::PollAt(t+1400);
    assert(uses==2&&respec::Status()==State::Done&&alive.empty());

    // A full inventory: no token, nothing changed, says so.
    createResult=Items::Result::Conflict;assert(respec::Request());RunGame();
    assert(respec::Status()==State::NoRoom&&alive.empty()&&uses==2);
    createResult=Items::Result::Success;

    // A token the game will not consume (a Classic character): removed by its handle.
    consumes=false;assert(respec::Request());RunGame();
    respec::PollAt(t+200);assert(uses==3&&alive.size()==1);
    respec::PollAt(t+1000);assert(respec::Status()==State::Busy);
    const uint64_t later=GetTickCount64()+4000;
    respec::PollAt(later);assert(game.size()==1);RunGame();
    assert(respec::Status()==State::Failed&&alive.empty()&&destroys==1);
    consumes=true;

    // The client never shows it: removed, never used.
    clientSees=false;assert(respec::Request());RunGame();
    respec::PollAt(GetTickCount64()+4000);RunGame();
    assert(respec::Status()==State::Failed&&alive.empty()&&destroys==2&&uses==3);
    clientSees=true;

    // The game thread never ran the mint: given up; a late mint undoes itself.
    assert(respec::Request());respec::PollAt(GetTickCount64()+4000);
    assert(respec::Status()==State::Failed);
    RunGame();assert(alive.empty()&&creates==5);
    respec::PollAt(GetTickCount64()+100);assert(uses==3);

    // The character changed after the token was made: removed, never used.
    assert(respec::Request());RunGame();assert(alive.size()==1);
    ++session;respec::PollAt(t+200);--session;assert(game.size()==1);
    RunGame();assert(respec::Status()==State::Failed&&alive.empty()&&uses==3);

    // The new token's identity can't be read on the game thread: removed at once.
    identityReadable=false;assert(respec::Request());RunGame();
    assert(respec::Status()==State::Failed&&alive.empty()&&uses==3);
    identityReadable=true;

    // TCP/IP: not offline, no button, no request.
    offline=false;assert(!respec::Available()&&!respec::Request()&&game.empty());offline=true;
    std::puts("Respec engine: one token, one native use, consumed or removed by its own handle; full inventory, Classic, slow client, lost mint and TCP/IP guarded");
}
