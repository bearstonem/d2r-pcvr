#include "room_subscription.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <set>
using namespace framevr::roomsubscription;
struct World {
    std::map<Address,int> refs;
    std::set<Address> subscribed;
    Address fail=0;
    bool removing=false;
    unsigned releases=0;
    unsigned additions=0,removals=0;
    bool HasClient(Address room,Address){return subscribed.contains(room);}
    void Subscribe(Address room,Address){assert(subscribed.insert(room).second);++additions;}
    void Unsubscribe(Address room,Address){assert(subscribed.erase(room)==1);++removals;}
    static Address Id(int x,int y){return 1+Address(y+20)*41+Address(x+20);}
    Address Drlg(Address room){return room?room-10000:0;}
    Address Level(Address node){return node?1:0;}
    bool Neighbors(Address node,std::vector<Address>& result){
        int x=int((node-1)%41)-20,y=int((node-1)/41)-20;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            if(x+dx>=-20&&x+dx<=20&&y+dy>=-20&&y+dy<=20)result.push_back(Id(x+dx,y+dy));
        return true;
    }
    bool NearRooms(Address centre,std::vector<Address>& out){
        if(!centre)return true;
        std::vector<Address> near;Neighbors(Drlg(centre),near);
        for(Address node:near)out.push_back(node+10000);
        return true;
    }
    Address Acquire(Address node){if(node==fail)return 0;++refs[node];return node+10000;}
    void Release(Address node){
        assert(refs[node]==1);
        // A live subscription must be removed before the last retained ref.
        assert(!subscribed.contains(node+10000));
        --refs[node];++releases;
    }
    void NativeDiff(const std::vector<Address>& old,const std::vector<Address>& next){
        for(Address room:old)if(!Contains(next,room))subscribed.erase(room);
        for(Address room:next)if(!Contains(old,room))assert(subscribed.insert(room).second);
    }
};
int main(){
    World api;Coverage owned;const auto centre=World::Id(0,0)+10000;
    std::vector<Address> baseline;api.NearRooms(centre,baseline);
    api.subscribed.insert(baseline.begin(),baseline.end());
    auto first=owned.Prepare(api,centre,8);
    assert(first.valid&&!first.truncated&&first.depth==8&&first.pins.size()==289&&first.rooms.size()==289);
    assert(api.releases==0);
    // Stationary initialization preserves the centre's nine native rooms.
    SynchronizeRooms(api,1,baseline,first.rooms);owned.Commit(api,std::move(first));
    assert(api.additions==280&&api.removals==0&&api.subscribed.size()==289);
    // Repeated preparation of an unchanged area acquires no duplicate refs.
    auto same=owned.Prepare(api,centre,8);assert(same.acquired.empty());
    SynchronizeRooms(api,1,owned.rooms,same.rooms);owned.Commit(api,std::move(same));
    assert(api.additions==280&&api.removals==0);
    // Repair a missing membership without adding any duplicate clients.
    api.subscribed.erase(owned.rooms.back());
    SynchronizeRooms(api,1,owned.rooms,owned.rooms);
    assert(api.additions==281&&api.subscribed.size()==289);
    // Regression: room subscription alone cannot deliver dirty-unit events
    // after the initial room snapshot. Both native per-client passes must
    // borrow the expanded list, while unrelated getter calls remain native.
    Lists tick;tick.updateRoom=centre;tick.updates=&owned.rooms;
    assert(tick.Select(RoomPass::Other,centre)==nullptr);
    assert(tick.Select(RoomPass::Update,centre+1)==nullptr);
    assert(tick.Select(RoomPass::Remove,centre+1)==nullptr);
    const Address farRoom=World::Id(7,7)+10000;
    assert(!Contains(baseline,farRoom)&&Contains(owned.rooms,farRoom));
    const auto dispatch=[&](RoomPass pass,Address room){
        auto* expanded=tick.Select(pass,room);
        const auto& used=expanded?*expanded:baseline;
        // A create/animation/removal queued in the distant room must reach
        // the same client, without another ROOM crossing or subscription.
        return Contains(used,farRoom);
    };
    assert(!dispatch(RoomPass::Other,centre));
    assert(dispatch(RoomPass::Update,centre));
    assert(dispatch(RoomPass::Remove,centre));
    assert(tick.Select(RoomPass::Update,centre)==tick.Select(RoomPass::Remove,centre));
    assert(api.additions==281&&api.removals==0); // updates do not resubscribe
    // Cross a ROOM edge repeatedly, as in the owner capture. Both sets must
    // remain alive until native has removed the old client membership.
    for(int crossing=0;crossing<18;++crossing){
        auto next=owned.Prepare(api,World::Id(0,crossing%2?0:1)+10000,8);
        assert(next.valid&&next.pins.size()==289&&next.acquired.size()==17);
        for(auto pin:owned.pins)assert(api.refs[pin.drlg]==1);
        api.NativeDiff(owned.rooms,next.rooms);owned.Commit(api,std::move(next));
        assert(api.subscribed.size()==289);
    }
    // The borrowed view follows committed coverage rather than retaining a
    // stale copy of rooms from the first frame.
    assert(tick.Select(RoomPass::Update,centre)==&owned.rooms);
    // A failed activation rolls back only new pins, retaining the old area.
    api.fail=World::Id(-8,9);const auto saved=owned.rooms;
    auto failed=owned.Prepare(api,World::Id(0,1)+10000,8);
    assert(!failed.valid&&failed.acquired.empty()&&owned.rooms==saved);
    for(auto pin:owned.pins)assert(api.refs[pin.drlg]==1);
    api.fail=0;
    // Disable: removal happens while old refs still exist, then all refs drop.
    auto off=owned.Prepare(api,centre,0);assert(off.valid&&off.pins.empty()&&off.rooms.size()==9);
    SynchronizeRooms(api,1,owned.rooms,off.rooms);
    assert(api.removals==280);
    // Native center keeps these baseline rooms alive after our pins release.
    // The fixture models only owned refs, so exempt the native baseline here.
    auto native=api.subscribed;api.subscribed.clear();owned.Commit(api,std::move(off));api.subscribed=native;
    assert(owned.pins.empty()&&api.subscribed.size()==9);
    assert(tick.Select(RoomPass::Update,centre)->size()==9);
    for(auto [node,count]:api.refs)assert(count==0);
    // Disconnect and level travel cannot leave subscriptions or retained refs.
    auto again=owned.Prepare(api,World::Id(15,15)+10000,8);assert(again.valid);
    api.NativeDiff(owned.rooms,again.rooms);owned.Commit(api,std::move(again));
    api.subscribed.clear();owned.Clear(api);
    assert(tick.Select(RoomPass::Update,centre)->empty());
    for(auto [node,count]:api.refs)assert(count==0);
    // Invalid ROOM input is rejected without touching native state.
    auto invalid=owned.Prepare(api,0,8);assert(!invalid.valid&&owned.pins.empty());
    // Dense/irregular room graphs stop at the fixed traversal bound instead
    // of allocating indefinitely. Other levels are excluded from expansion.
    struct Fan:World {
        Address Level(Address node){return node==999999?2:1;}
        bool Neighbors(Address node,std::vector<Address>& out){
            for(unsigned i=1;i<64;++i)out.push_back(node*64+i);
            out.push_back(999999);return true;
        }
    } fan;
    auto capped=owned.Prepare(fan,10001,8);
    assert(capped.valid&&capped.truncated&&capped.pins.size()==MaxRooms);
    assert(std::none_of(capped.pins.begin(),capped.pins.end(),[](Pin p){return p.drlg==999999;}));
    owned.Rollback(fan,capped);for(auto [node,count]:fan.refs)assert(count==0);
    struct Broken:World {bool Neighbors(Address,std::vector<Address>&){return false;}} broken;
    auto unread=owned.Prepare(broken,centre,8);assert(!unread.valid&&broken.refs.empty());
    puts("room subscription: eight rings, repeated edges, rollback, disable and teardown passed");
}
