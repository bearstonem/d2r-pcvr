// Reset Skills & Stats: one Token of Absolution minted on the game thread
// (ItemService::createItem, as the DEV kits do) and used once through the
// native inventory use (quick_items.cpp), whose token handler does the refund.
// No tables, shop, quest flag or Akara change. A token the game does not
// consume (a Classic character, a refused use) is destroyed by its own handle,
// so no free token is ever left behind; the player's own items are never touched.
#include <windows.h>
#include <D2RLPlugin/api.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include "respec.h"
#include "quick_items.h"
#include "offline_difficulty.h"
#include "logging.h"
namespace gamestate {uint64_t LocalPlayer();}
namespace respec {
namespace {
using namespace D2RL;
const PluginContext* context=nullptr;
const ItemService* items=nullptr;
const ThreadService* threads=nullptr;
std::atomic<int> state{int(State::Idle)};
// The reset in flight; all under the mutex.
std::mutex mutex;
PlayerHandle player=InvalidPlayerHandle;
ItemHandle token=InvalidItemHandle;
quickitems::ClientItem identity;   // read on the game thread when it was made
int lastMiss=-1;                   // the last reason the client lookup missed, logged once each
bool minted=false,used=false,ending=false;
uint64_t requestedAt=0,mintedAt=0,usedAt=0;
uintptr_t generation=0;   // each Request's own; a late Mint from an older one does nothing
// The client holds a new item a frame or more after the game makes it; the
// token handler consumes it well inside a second. Generous bounds either way.
constexpr uint64_t SeeMs=3000,ConsumeMs=3000;

void Finish(State s,const char* line) {
    framevr::logging::Write(line);
    // ++generation: a Mint still queued for this request now does nothing.
    {std::lock_guard lock(mutex);player=InvalidPlayerHandle;token=InvalidItemHandle;identity={};lastMiss=-1;minted=used=ending=false;++generation;}
    state.store(int(s));
}
// Game thread: make the token in the first free inventory square.
void __cdecl Mint(const PluginContext*,void* ticket) noexcept {
    PlayerHandle who;
    {std::lock_guard lock(mutex);if(reinterpret_cast<uintptr_t>(ticket)!=generation||ending||minted)return;who=player;}
    if(!who||who!=gamestate::LocalPlayer()){Finish(State::Failed,"Frame respec: the character changed before the token was made; nothing changed");return;}
    Items::ItemCreateSpec spec{};spec.structSize=sizeof spec;
    spec.code=Items::MakeItemCode("toa");spec.quality=Items::Quality::Normal;
    spec.qualityRecordId=Items::RandomQualityRecord;spec.itemLevel=1;spec.seedMode=Items::SeedMode::Random;
    spec.quantity=spec.durability=Items::DefaultValue;spec.stateFlags=Items::ItemStateIdentified;
    spec.destination.structSize=sizeof spec.destination;
    spec.destination.container=Items::ItemContainer::Inventory;
    spec.destination.placement=Items::Placement::Automatic;
    ItemHandle made=InvalidItemHandle;
    const auto result=items->createItem(context,who,&spec,&made);
    if(result!=Items::Result::Success||!made) {
        char line[160];std::snprintf(line,sizeof line,"Frame respec: the token could not be made (SDK result=%u); nothing changed",unsigned(result));
        // Not authoritative: a TCP/IP client (the button hides there). Otherwise the
        // likeliest refusal is a full inventory: the token needs one free square.
        Finish(result==Items::Result::NotAuthoritative?State::Failed:State::NoRoom,line);return;
    }
    // Its identity, read here where the handle createItem returned is certainly valid:
    // the UI thread then finds it in the client's own inventory list by id (2026-10-30:
    // rev1 read this handle on the UI thread and never matched the client's item).
    Items::ItemInfo info{};info.structSize=sizeof info;
    const auto read=items->getItemInfo(context,made,&info);
    const bool known=read==Items::Result::Success&&info.runtimeId;
    {
        std::lock_guard lock(mutex);
        if(reinterpret_cast<uintptr_t>(ticket)==generation&&!ending&&known) {
            token=made;identity={info.runtimeId,info.classId,info.code?info.code:spec.code};
            minted=true;mintedAt=GetTickCount64();made=InvalidItemHandle;
        }
    }
    if(made) {   // given up while this ran, or its identity unreadable: undo at once, on this thread
        const auto removed=items->destroyItem(context,who,made,Items::SocketedItemPolicy::RejectIfNotEmpty);
        char line[200];
        if(!known){
            std::snprintf(line,sizeof line,"Frame respec: the token's identity could not be read (SDK result=%u); removed it (SDK result=%u); nothing changed",unsigned(read),unsigned(removed));
            Finish(State::Failed,line);
        } else {std::snprintf(line,sizeof line,"Frame respec: a late token was removed (SDK result=%u)",unsigned(removed));framevr::logging::Write(line);}
        return;
    }
    char line[200];std::snprintf(line,sizeof line,"Frame respec: Token of Absolution made in the inventory (id=%u class=%u container=%u)",
        info.runtimeId,info.classId,unsigned(info.container));
    framevr::logging::Write(line);
}
// Game thread: remove the unconsumed token by its own handle.
void __cdecl Remove(const PluginContext*,void* why) noexcept {
    PlayerHandle who;ItemHandle what;
    {std::lock_guard lock(mutex);who=player;what=token;}
    const auto result=what?items->destroyItem(context,who,what,Items::SocketedItemPolicy::RejectIfNotEmpty):Items::Result::NotFound;
    char line[200];std::snprintf(line,sizeof line,"Frame respec: %s; the unused token was removed (SDK result=%u); nothing changed",
        static_cast<const char*>(why),unsigned(result));
    Finish(State::Failed,line);
}
void Abandon(const char* why) {
    {std::lock_guard lock(mutex);if(ending)return;ending=true;}
    if(threads->runOnGameThread(context,&Remove,const_cast<char*>(why))!=Threads::Result::Success)
        Finish(State::Failed,"Frame respec: could not schedule removal of the unused token");   // left in the inventory, usable by hand
}
}
void Init(const PluginContext* ctx) {
    context=ctx;
    if(ctx->QueryService(&items)!=ServiceQueryResult::Success||!items->createItem||!items->destroyItem||!items->getItemInfo)items=nullptr;
    if(ctx->QueryService(&threads)!=ServiceQueryResult::Success||!threads->runOnGameThread||!threads->runOnUiThread)threads=nullptr;
    framevr::logging::Write(items&&threads?"Frame respec: Reset Skills & Stats ready (offline games)":
        "Frame respec: item or thread service missing - Reset Skills & Stats hidden");
}
bool Available(){return items&&threads&&offlinedifficulty::Offline();}
bool Request() {
    if(!Available())return false;
    int idle=state.load();
    if(idle==int(State::Busy))return false;
    const PlayerHandle who=gamestate::LocalPlayer();
    if(!who)return false;
    if(!state.compare_exchange_strong(idle,int(State::Busy)))return false;
    uintptr_t ticket;
    {std::lock_guard lock(mutex);player=who;token=InvalidItemHandle;minted=used=ending=false;requestedAt=GetTickCount64();ticket=++generation;}
    if(threads->runOnGameThread(context,&Mint,reinterpret_cast<void*>(ticket))!=Threads::Result::Success) {
        Finish(State::Failed,"Frame respec: could not schedule the token");return true;
    }
    framevr::logging::Write("Frame respec: Reset Skills & Stats confirmed");
    return true;
}
State Status(){return State(state.load());}
void Poll(){PollAt(GetTickCount64());}
void PollAt(uint64_t now) {
    if(!items||state.load()!=int(State::Busy))return;
    PlayerHandle who;quickitems::ClientItem item;bool made,done;uint64_t askedAt,madeAt,doneAt;int missed;
    {std::lock_guard lock(mutex);if(ending)return;who=player;item=identity;made=minted;done=used;askedAt=requestedAt;madeAt=mintedAt;doneAt=usedAt;missed=lastMiss;}
    if(!made) {   // Mint still to run; a late one undoes itself
        if(now-askedAt>SeeMs){{std::lock_guard lock(mutex);if(minted)return;ending=true;}Finish(State::Failed,"Frame respec: the token was never made; nothing changed");}
        return;
    }
    if(who!=gamestate::LocalPlayer()){Abandon("the character changed");return;}
    if(!done) {
        // Native use, as a right-click on it in the inventory, once the client holds it.
        uint32_t seen[8]{};unsigned count=0;
        const auto result=quickitems::UseClientItem(who,item,seen,8,&count);
        if(result==quickitems::UseResult::Used){std::lock_guard lock(mutex);used=true;usedAt=now;return;}
        if(int(result)!=missed) {   // why it missed, once per reason (the next test reads it)
            char line[240];int at=std::snprintf(line,sizeof line,"Frame respec: token id=%u not used yet - %s",item.runtimeId,quickitems::UseResultName(result));
            if(result==quickitems::UseResult::Missing)
                at+=std::snprintf(line+at,sizeof line-size_t(at),"; tokens the client holds: %u%s",count,count?" (ids":"");
            for(unsigned i=0;i<count&&at<int(sizeof line)-16;++i)at+=std::snprintf(line+at,sizeof line-size_t(at)," %u",seen[i]);
            if(count&&at<int(sizeof line)-2)std::snprintf(line+at,sizeof line-size_t(at),")");
            framevr::logging::Write(line);
            std::lock_guard lock(mutex);lastMiss=int(result);
        }
        if(now-madeAt>SeeMs)Abandon("the client never showed the token");
        return;
    }
    bool holds=true;
    if(quickitems::HoldsClientItem(who,item,holds)&&!holds) {
        Finish(State::Done,"Frame respec: the token was consumed - the game reset skills and stats");return;
    }
    if(now-doneAt>ConsumeMs)Abandon("the game did not consume the token (a Classic character cannot use one)");
}
}
