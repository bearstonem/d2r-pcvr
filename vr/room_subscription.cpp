// Experimental server coverage, exact D2R 3.4.95032, offline local client only.
#include "room_subscription.h"
#include "room_subscription_signatures.h"
#include "../steamvr/room_subscription.h"
#include "../steamvr/memory.h"
#include "../steamvr/logging.h"
#include "../steamvr/callback_ticket.h"
#include "melee.h"
#include "offline_difficulty.h"
#include "sigscan.h"
#include <D2RLPlugin/api.h>
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace roomsubscription {
namespace {
using namespace framevr::roomsubscription;
using TransitionFn=void(*)(Address,Address);
using NearFn=void(*)(Address,Address**,uint32_t*);
using StatusFn=void(*)(uint32_t,Address,Address);
using GenerateFn=void(*)(uint32_t,Address);
using MembershipFn=void(*)(Address,Address,Address);
MembershipFn originalUpdate=nullptr;
TransitionFn originalRemove=nullptr;
TransitionFn originalTransition=nullptr,originalCleanup=nullptr;
NearFn originalNear=nullptr;
std::atomic<bool> ready{false};
std::atomic<int> requested{0},state{0};
std::atomic<unsigned> held{0},subscribed{0},furthest{0};
std::atomic<bool> truncated{false};
std::atomic<uint64_t> transitions{0};
std::atomic<uint64_t> updatePasses{0},removalPasses{0};
std::atomic<unsigned> updateRooms{0};
Address image=0,owner=0,ownerGame=0;
uint32_t ownerVersion=0;
unsigned appliedRings=0;
Coverage coverage; // authoritative game thread only, including disconnect
bool nearHook=false,cleanupHook=false,transitionHook=false,updateHook=false,removeHook=false;
const D2RL::ThreadService* threads=nullptr;
std::mutex ticketMutex;
framevr::CallbackTicket ticket;

template<class T> bool Read(Address a,T& value){
    return a>=0x10000&&framevr::ReadMemory(&value,reinterpret_cast<void*>(a),sizeof value);
}
template<class Fn> Fn Original(Fn const volatile& slot){
    Fn call=slot;for(int i=0;!call&&i<(1<<20);++i){YieldProcessor();call=slot;}return call;
}
struct Adapter {
    uint32_t version;
    Address game=0;
    Address Drlg(Address room){Address p=0;Read(room+0x18,p);return p;}
    Address Level(Address drlg){Address p=0;if(drlg)Read(drlg+0x90,p);return p;}
    bool NearRooms(Address room,std::vector<Address>& out){
        if(!room)return true;
        Address data=0;uint32_t size=0;
        if(!Read(room,data)||!Read(room+0x40,size)||size>MaxNeighbors||(!data&&size))return false;
        out.resize(size);
        return !size||framevr::ReadMemory(out.data(),reinterpret_cast<void*>(data),size*sizeof(Address));
    }
    bool Neighbors(Address drlg,std::vector<Address>& out){
        uint64_t size=0;Address data=0;
        if(!Read(drlg+0x18,size))return false;
        if(!size)reinterpret_cast<GenerateFn>(image+abi::GenerateNear)(version,drlg);
        if(!Read(drlg+0x18,size)||size>MaxNeighbors||!Read(drlg+0x10,data)||(!data&&size))return false;
        out.resize(size);
        return !size||framevr::ReadMemory(out.data(),reinterpret_cast<void*>(data),size*sizeof(Address));
    }
    Address Acquire(Address drlg){
        // Retain the level before room activation; no native center is moved.
        reinterpret_cast<StatusFn>(image+abi::LevelStatus)(version,0,drlg);
        reinterpret_cast<StatusFn>(image+abi::RoomStatus)(version,0,drlg);
        Address room=0;
        if(Read(drlg+0x58,room)&&room)return room;
        Release(drlg);return 0;
    }
    void Release(Address drlg){
        // Native symmetric release includes the recursive room-status refs.
        reinterpret_cast<StatusFn>(image+abi::RoomStatus)(version,drlg,0);
        reinterpret_cast<StatusFn>(image+abi::LevelStatus)(version,drlg,0);
    }
    bool HasClient(Address room,Address client){
        Address data=0;uint64_t size=0;
        if(!Read(room+0x68,data)||!Read(room+0x70,size)||size>MaxNeighbors||(!data&&size))return false;
        for(uint64_t i=0;i<size;++i){Address entry=0;if(!Read(data+i*8,entry))return false;if(entry==client)return true;}
        return false;
    }
    void Subscribe(Address room,Address client){
        reinterpret_cast<MembershipFn>(image+abi::Subscribe)(game,room,client);
    }
    void Unsubscribe(Address room,Address client){
        reinterpret_cast<MembershipFn>(image+abi::Unsubscribe)(game,room,client);
    }
};
thread_local Lists* activeLists=nullptr;
struct Scope {Lists* saved;Scope(Lists& next):saved(activeLists){activeLists=&next;}~Scope(){activeLists=saved;}};

void Near(Address room,Address** data,uint32_t* size){
    const auto call=Original(originalNear);if(!call)return;
    const Address caller=reinterpret_cast<Address>(_ReturnAddress());
    if(auto* scope=activeLists;scope&&data&&size){
        RoomPass pass=RoomPass::Other;
        if(caller==image+abi::NewReturn)pass=RoomPass::Enter;
        if(caller==image+abi::OldReturn)pass=RoomPass::Leave;
        if(caller==image+abi::CleanupReturn)pass=RoomPass::Disconnect;
        if(caller==image+abi::UpdateReturn)pass=RoomPass::Update;
        if(caller==image+abi::RemoveReturn)pass=RoomPass::Remove;
        auto* selected=scope->Select(pass,room);
        if(selected){*data=selected->data();*size=static_cast<uint32_t>(selected->size());return;}
    }
    call(room,data,size);
}

bool Local(Address client,Address& game,uint32_t& version){
    Address player=0,nativeGame=0,nativePlayer=0;
    uint8_t kind=0,v=255;
    if(!offlinedifficulty::Offline()||!Read(client+0x2b0,game)||!game||
       !Read(client+0x278,player)||!player||!melee::LocalServer(nativeGame,nativePlayer)||
       game!=nativeGame||player!=nativePlayer||!Read(game+0x101,kind)||kind<1||kind>3||
       !Read(game+0x106,v)||v>3)return false;
    version=v;return true;
}

bool OwnedUpdate(Address room,Address client,Address game){
    if(!ready.load(std::memory_order_acquire)||activeLists||!owner||client!=owner||
       coverage.pins.empty()||state.load()!=3)return false;
    Address matchedGame=0,centre=0;uint32_t version=0;
    return Local(client,matchedGame,version)&&matchedGame==ownerGame&&(!game||game==matchedGame)&&
        Read(client+0x2c0,centre)&&centre==room;
}

void Update(Address game,Address room,Address client){
    const auto call=Original(originalUpdate);if(!call)return;
    if(!OwnedUpdate(room,client,game)){call(game,room,client);return;}
    Lists lists;lists.updateRoom=room;lists.updates=&coverage.rooms;
    // Let the native dirty-unit pass deliver creates, movement, animations,
    // missiles and object changes across exactly the subscribed room set.
    {Scope scope(lists);call(game,room,client);}
    updateRooms=static_cast<unsigned>(coverage.rooms.size());
    if(updatePasses.fetch_add(1)==0)
        framevr::logging::Write("Frame subscriptions: native unit update pass now uses expanded coverage (create/movement/animation delivery)");
}

void Remove(Address room,Address client){
    const auto call=Original(originalRemove);if(!call)return;
    if(!OwnedUpdate(room,client,0)){call(room,client);return;}
    Lists lists;lists.updateRoom=room;lists.updates=&coverage.rooms;
    // The native deletion queues must cover the same rooms as unit updates.
    {Scope scope(lists);call(room,client);}
    if(removalPasses.fetch_add(1)==0)
        framevr::logging::Write("Frame subscriptions: native unit removal pass now uses expanded coverage");
}

void Transition(Address client,Address nextRoom){
    const auto call=Original(originalTransition);if(!call)return;
    if(!ready.load(std::memory_order_acquire)||activeLists){call(client,nextRoom);return;}
    Address game=0,oldRoom=0;uint32_t version=0;
    if(!Read(client+0x2c0,oldRoom)||oldRoom==nextRoom){call(client,nextRoom);return;}
    const bool local=Local(client,game,version);
    // A disconnect can invalidate local-player lookup before teardown. Only
    // the already-owned client/game may contract without a fresh local match.
    const bool owned=owner==client&&Read(client+0x2b0,game)&&game==ownerGame;
    if(!local&&!owned){call(client,nextRoom);return;}
    if(owner&&(!owned)){call(client,nextRoom);return;}
    const unsigned rings=local&&nextRoom&&requested.load()==8?8:0;
    if(!owner&&!rings){call(client,nextRoom);return;}
    if(owned)version=ownerVersion;
    Adapter api{version};Lists lists;lists.oldRoom=oldRoom;lists.newRoom=nextRoom;
    if(owned)lists.before=coverage.rooms;
    else if(!api.NearRooms(oldRoom,lists.before)){state=4;call(client,nextRoom);return;}
    state=2;
    Plan plan=coverage.Prepare(api,nextRoom,rings);
    const bool failed=!plan.valid;
    if(failed){
        // Acquired refs were rolled back. Use a full expanded->native diff so
        // an unavailable graph cannot leave stale extra subscriptions behind.
        plan=coverage.Prepare(api,nextRoom,0);
        if(!plan.valid){state=4;call(client,nextRoom);return;}
    }
    lists.after=plan.rooms;
    {Scope scope(lists);call(client,nextRoom);}
    // Native has sent removals while both old/new room sets were still alive.
    const unsigned depth=plan.depth;const bool capped=plan.truncated;
    coverage.Commit(api,std::move(plan));
    appliedRings=failed?0:rings;
    owner=client;ownerGame=game;ownerVersion=version;
    held=static_cast<unsigned>(coverage.pins.size());furthest=depth;truncated=capped;
    unsigned count=0;for(Address room:coverage.rooms)if(api.HasClient(room,client))++count;
    subscribed=count;++transitions;
    state=failed?4:count!=coverage.rooms.size()?5:rings?3:0;
    char line[240];std::snprintf(line,sizeof line,
        "Frame subscriptions: target=%u depth=%u held=%u subscribed=%u/%zu capped=%d state=%d (native room transition)",
        rings,depth,held.load(),count,coverage.rooms.size(),int(capped),state.load());
    framevr::logging::Write(line);
    if(coverage.pins.empty()){coverage.Forget();owner=ownerGame=0;}
}

void __cdecl Refresh(const D2RL::PluginContext*,void* data) noexcept {
    {std::lock_guard lock(ticketMutex);if(!ticket.Claim(reinterpret_cast<uintptr_t>(data)))return;}
    if(!ready.load(std::memory_order_acquire)||activeLists||!offlinedifficulty::Offline())return;
    // Resolve fresh server/player/client identities on the authoritative game
    // thread. No game pointers are captured by the settings worker.
    Address game=0,player=0,path=0,room=0,clients=0;uint64_t count=0;
    if(!melee::LocalServer(game,player)||!Read(player+0x38,path)||!path||
       !Read(path+0x20,room)||!room||!Read(room+0x68,clients)||!clients||
       !Read(room+0x70,count)||count>MaxNeighbors)return;
    Address client=0;uint32_t version=0;
    for(uint64_t i=0;i<count;++i){
        Address candidate=0,matchedGame=0,centre=0;uint32_t matchedVersion=0;
        if(Read(clients+i*8,candidate)&&candidate&&Local(candidate,matchedGame,matchedVersion)&&
           matchedGame==game&&Read(candidate+0x2c0,centre)&&centre==room){
            client=candidate;version=matchedVersion;break;
        }
    }
    if(!client||(owner&&(owner!=client||ownerGame!=game)))return;
    const unsigned rings=requested.load()==8?8:0;
    if((!owner&&!rings)||(owner&&appliedRings==rings))return;
    Adapter api{version,game};
    std::vector<Address> before;
    if(owner)before=coverage.rooms;
    else if(!api.NearRooms(room,before)){state=4;return;}
    state=2;
    auto plan=coverage.Prepare(api,room,rings);
    if(!plan.valid){state=4;return;}
    // Do not call Transition(client, room): native rejects an unchanged centre.
    // Centre/level bookkeeping stays untouched; only membership is refreshed.
    SynchronizeRooms(api,client,before,plan.rooms);
    const unsigned depth=plan.depth;const bool capped=plan.truncated;
    coverage.Commit(api,std::move(plan));
    owner=client;ownerGame=game;ownerVersion=version;appliedRings=rings;
    held=static_cast<unsigned>(coverage.pins.size());furthest=depth;truncated=capped;
    unsigned actual=0;for(Address candidate:coverage.rooms)if(api.HasClient(candidate,client))++actual;
    subscribed=actual;state=actual!=coverage.rooms.size()?5:rings?3:0;
    char line[240];std::snprintf(line,sizeof line,
        "Frame subscriptions: target=%u depth=%u held=%u subscribed=%u/%zu capped=%d state=%d (automatic stationary refresh)",
        rings,depth,held.load(),actual,coverage.rooms.size(),int(capped),state.load());
    framevr::logging::Write(line);
    if(coverage.pins.empty()){coverage.Forget();owner=ownerGame=0;}
}

void QueueRefresh(const D2RL::PluginContext* ctx){
    if(!ctx)return;
    if(!threads)(void)ctx->QueryService(&threads);
    if(!threads||!threads->runOnGameThread)return;
    uintptr_t token=0;
    {std::lock_guard lock(ticketMutex);
        if(ticket.Expired(GetTickCount64(),2000))ticket.Cancel();
        token=ticket.Start(GetTickCount64());
    }
    if(token&&threads->runOnGameThread(ctx,&Refresh,reinterpret_cast<void*>(token))!=D2RL::Threads::Result::Success){
        std::lock_guard lock(ticketMutex);ticket.Claim(token);
    }
}

void Cleanup(Address room,Address client){
    const auto call=Original(originalCleanup);if(!call)return;
    if(!ready.load(std::memory_order_acquire)||activeLists||client!=owner){call(room,client);return;}
    Address game=0;
    if(!Read(client+0x2b0,game)||game!=ownerGame){
        // Do not dereference saved graph pointers after a game is destroyed.
        coverage.Forget();owner=ownerGame=0;held=0;subscribed=0;furthest=0;state=6;call(room,client);return;
    }
    Adapter api{ownerVersion};Lists lists;lists.cleanupRoom=room;
    api.NearRooms(room,lists.cleanup);lists.cleanup.insert(lists.cleanup.end(),coverage.rooms.begin(),coverage.rooms.end());Unique(lists.cleanup);
    // Native vector removal assumes membership: never feed it a duplicate or
    // an already removed client (its primitive decrements size unconditionally).
    std::erase_if(lists.cleanup,[&](Address candidate){return !api.HasClient(candidate,client);});
    {Scope scope(lists);call(room,client);}
    coverage.Clear(api);owner=ownerGame=0;held=0;subscribed=0;furthest=0;truncated=false;state=0;
    framevr::logging::Write("Frame subscriptions: disconnect removed extra clients and released all owned room/level references");
}

bool Guards(){
    using namespace abi;
    return d2rsig::Check(abi::Transition,TransitionSig,sizeof TransitionSig)&&
        d2rsig::Check(abi::Near,NearSig,sizeof NearSig)&&d2rsig::Check(abi::Cleanup,CleanupSig,sizeof CleanupSig)&&
        d2rsig::Check(RoomStatus,RoomStatusSig,sizeof RoomStatusSig)&&d2rsig::Check(LevelStatus,LevelStatusSig,sizeof LevelStatusSig)&&
        d2rsig::Check(GenerateNear,GenerateNearSig,sizeof GenerateNearSig)&&
        d2rsig::Check(Subscribe,SubscribeSig,sizeof SubscribeSig)&&
        d2rsig::Check(Unsubscribe,UnsubscribeSig,sizeof UnsubscribeSig)&&
        d2rsig::Check(abi::Update,UpdateSig,sizeof UpdateSig)&&
        d2rsig::Check(abi::Remove,RemoveSig,sizeof RemoveSig)&&
        d2rsig::Check(0x48a2f4,UpdateCallSig,sizeof UpdateCallSig)&&
        d2rsig::Check(0x48a149,RemoveCallSig,sizeof RemoveCallSig)&&
        d2rsig::Check(0x486a45,UpdateParentSig,sizeof UpdateParentSig)&&
        d2rsig::Check(0x486a37,RemoveParentSig,sizeof RemoveParentSig)&&
        d2rsig::Check(0x487267,NewCallSig,sizeof NewCallSig)&&d2rsig::Check(0x48728d,OldCallSig,sizeof OldCallSig)&&
        d2rsig::Check(0x48a228,CleanupCallSig,sizeof CleanupCallSig);
}
}

void Poll(const D2RL::PluginContext* ctx,const wchar_t* ini,bool vr){
    // This experiment defaults to eight. An explicit zero restores the native
    // subscription set automatically on the game thread; renderer rings are separate.
    const int choice=vr&&GetPrivateProfileIntW(L"render",L"subscription_rings",8,ini)==8?8:0;
    requested=choice;
    if(ready.load(std::memory_order_acquire)){QueueRefresh(ctx);return;}
    if(!choice)return;
    if(!ctx||!ctx->exeBase||!ctx->buildName||std::strcmp(ctx->buildName,"95032")){state=7;return;}
    state=1;
    if(!Guards())return; // late-published protected code is retried next poll
    image=ctx->exeBase;
    if(!nearHook)nearHook=d2rsig::Hook(abi::Near,abi::NearSig,sizeof abi::NearSig,reinterpret_cast<void*>(&Near),reinterpret_cast<void**>(&originalNear));
    if(!cleanupHook)cleanupHook=d2rsig::Hook(abi::Cleanup,abi::CleanupSig,sizeof abi::CleanupSig,reinterpret_cast<void*>(&Cleanup),reinterpret_cast<void**>(&originalCleanup));
    if(!transitionHook)transitionHook=d2rsig::Hook(abi::Transition,abi::TransitionSig,sizeof abi::TransitionSig,reinterpret_cast<void*>(&Transition),reinterpret_cast<void**>(&originalTransition));
    if(!updateHook)updateHook=d2rsig::Hook(abi::Update,abi::UpdateSig,sizeof abi::UpdateSig,reinterpret_cast<void*>(&Update),reinterpret_cast<void**>(&originalUpdate));
    if(!removeHook)removeHook=d2rsig::Hook(abi::Remove,abi::RemoveSig,sizeof abi::RemoveSig,reinterpret_cast<void*>(&Remove),reinterpret_cast<void**>(&originalRemove));
    if(nearHook&&cleanupHook&&transitionHook&&updateHook&&removeHook){
        ready.store(true,std::memory_order_release);
        framevr::logging::Write("Frame subscriptions: exact 95032 guards ready; eight-ring offline server activation/client subscription experiment; automatic initialization and expanded native unit update/removal passes enabled");
        QueueRefresh(ctx);
    }
}
Status ReadStatus(){return {ready.load(),requested.load(),state.load(),held.load(),subscribed.load(),furthest.load(),truncated.load(),transitions.load(),updatePasses.load(),removalPasses.load(),updateRooms.load()};}
}
