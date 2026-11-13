#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace framevr::roomsubscription {
inline constexpr unsigned MaxRooms=512,MaxNeighbors=64,ExperimentRings=8;
using Address=uintptr_t;
struct Node {Address drlg=0;unsigned depth=0;};
struct Pin {Address drlg=0,room=0;unsigned depth=0;};
enum class RoomPass {Other,Enter,Leave,Disconnect,Update,Remove};
// Only proven call sites within a scoped native pass may see these lists.
// Updates borrow the retained coverage vector, avoiding a copy every tick.
struct Lists {
    Address oldRoom=0,newRoom=0,cleanupRoom=0,updateRoom=0;
    std::vector<Address> before,after,cleanup;
    std::vector<Address>* updates=nullptr;
    std::vector<Address>* Select(RoomPass pass,Address room){
        if(pass==RoomPass::Enter&&room==newRoom)return &after;
        if(pass==RoomPass::Leave&&room==oldRoom)return &before;
        if(pass==RoomPass::Disconnect&&room==cleanupRoom)return &cleanup;
        if((pass==RoomPass::Update||pass==RoomPass::Remove)&&room==updateRoom)return updates;
        return nullptr;
    }
};
template<class T> bool Contains(const std::vector<T>& values,T value){return std::find(values.begin(),values.end(),value)!=values.end();}
inline void Unique(std::vector<Address>& rooms){
    std::erase(rooms,Address{0});
    std::sort(rooms.begin(),rooms.end());rooms.erase(std::unique(rooms.begin(),rooms.end()),rooms.end());
}
// Apply coverage while stationary using the same native per-room operations
// as a ROOM transition. The caller retains both room sets through this diff.
// Membership checks are required: native removal assumes an existing client.
template<class Adapter> void SynchronizeRooms(Adapter& api,Address client,
    const std::vector<Address>& before,const std::vector<Address>& after){
    for(Address room:after)if(!api.HasClient(room,client))api.Subscribe(room,client);
    for(Address room:before)
        if(!Contains(after,room)&&api.HasClient(room,client))api.Unsubscribe(room,client);
}
struct Plan {
    std::vector<Pin> pins;
    std::vector<Address> rooms,acquired;
    bool valid=true,truncated=false;
    unsigned depth=0;
};
// The caller owns the authoritative game thread. Prepare retains BOTH sets
// until native subscription removal finishes; Commit then releases old pins.
// No ROOM/DRLG neighbor arrays are modified.
class Coverage {
public:
    std::vector<Pin> pins;
    std::vector<Address> rooms;
    template<class Adapter> Plan Prepare(Adapter& api,Address centre,unsigned rings){
        Plan out;
        if(!api.NearRooms(centre,out.rooms)){out.valid=false;return out;}
        if(!rings){Unique(out.rooms);return out;}
        const Address seed=api.Drlg(centre),level=api.Level(seed);
        if(!seed||!level){out.valid=false;return out;}
        std::vector<Node> queue{{seed,0}};queue.reserve(MaxRooms);
        for(size_t i=0;i<queue.size();++i){
            const Node node=queue[i];
            if(node.depth>=std::min(rings,ExperimentRings))continue;
            std::vector<Address> near;
            if(!api.Neighbors(node.drlg,near)||near.size()>MaxNeighbors){out.valid=false;return out;}
            for(Address next:near){
                if(!next||api.Level(next)!=level||std::any_of(queue.begin(),queue.end(),[&](Node n){return n.drlg==next;}))continue;
                if(queue.size()==MaxRooms){out.truncated=true;continue;}
                queue.push_back({next,node.depth+1});
            }
        }
        for(Node node:queue){
            auto existing=std::find_if(pins.begin(),pins.end(),[&](Pin p){return p.drlg==node.drlg;});
            Address room=0;
            if(existing!=pins.end())room=existing->room;
            else {
                room=api.Acquire(node.drlg);
                if(!room){out.valid=false;Rollback(api,out);return out;}
                out.acquired.push_back(node.drlg);
            }
            out.pins.push_back({node.drlg,room,node.depth});out.rooms.push_back(room);
            out.depth=std::max(out.depth,node.depth);
        }
        Unique(out.rooms);return out;
    }
    template<class Adapter> void Rollback(Adapter& api,Plan& plan){
        for(auto it=plan.acquired.rbegin();it!=plan.acquired.rend();++it)api.Release(*it);
        plan.acquired.clear();plan.pins.clear();plan.rooms.clear();
    }
    template<class Adapter> void Commit(Adapter& api,Plan&& next){
        for(const Pin old:pins)
            if(std::none_of(next.pins.begin(),next.pins.end(),[&](Pin p){return p.drlg==old.drlg;}))api.Release(old.drlg);
        pins=std::move(next.pins);rooms=std::move(next.rooms);
    }
    template<class Adapter> void Clear(Adapter& api){for(auto it=pins.rbegin();it!=pins.rend();++it)api.Release(it->drlg);Forget();}
    void Forget(){pins.clear();rooms.clear();}
};
}
