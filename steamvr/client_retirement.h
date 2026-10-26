#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace framevr::bowflight {
// UI callbacks can request retirement, but must not delete HD entities while
// the renderer's registry is immutable. Consume requests from the native
// missile update/kill hooks. Keep the request until destruction returns so a
// reentrant UI callback cannot create its replacement halfway through cleanup.
// The caller serializes access. No game pointer is dereferenced here.
//
// The game updates a live client missile every frame. A request it has not
// reached within Unreached ms is for a missile it already removed on a path
// these hooks do not see (2026-10-21: a recovered bow preview waited for the
// rest of the session, leaving the placeholder arrow and no skill arrow). It
// stops holding back the replacement but still retires that missile should
// the game update it after all (a paused game, say) until the session ends:
// a recycled address fails Begin's exact match and is dropped there.
class ClientRetirements {
    struct Entry {
        uintptr_t unit;
        uint32_t id,missile;
        uint64_t ticket,requested;
        bool destroying=false;
    };
    std::vector<Entry> entries;
    uint64_t serial=0;
    static bool Unreachable(const Entry& e,uint64_t now) {return !e.destroying&&now>=e.requested&&now-e.requested>=Unreached;}
public:
    static constexpr uint64_t Unreached=1000;
    bool Pending() const {return !entries.empty();} // the native hooks keep watching
    // A replacement waits only for retirements the game can still reach.
    bool Blocking(uint64_t now) const {
        return std::any_of(entries.begin(),entries.end(),[&](const auto& e){return !Unreachable(e,now);});
    }
    bool Contains(uintptr_t unit,uint32_t id,uint32_t missile) const {
        return std::any_of(entries.begin(),entries.end(),[&](const auto& e){return e.unit==unit&&e.id==id&&e.missile==missile;});
    }
    bool Abandoned(uintptr_t unit,uint32_t id,uint32_t missile,uint64_t now) const {
        return std::any_of(entries.begin(),entries.end(),[&](const auto& e){
            return e.unit==unit&&e.id==id&&e.missile==missile&&Unreachable(e,now);});
    }
    void Request(uintptr_t unit,uint32_t id,uint32_t missile,uint64_t now) {
        if(unit&&!Contains(unit,id,missile))entries.push_back({unit,id,missile,++serial,now});
    }
    uint64_t Begin(uintptr_t unit,const uint32_t header[3]) {
        for(auto it=entries.begin();it!=entries.end();) {
            if(it->unit!=unit){++it;continue;}
            // A recycled address must never delete somebody else's missile.
            if(header[0]!=3||header[1]!=it->missile||header[2]!=it->id){it=entries.erase(it);continue;}
            if(it->destroying)return 0;
            it->destroying=true;return it->ticket;
        }
        return 0;
    }
    void Finish(uint64_t ticket) {
        if(!ticket)return;
        std::erase_if(entries,[&](const auto& e){return e.ticket==ticket&&e.destroying;});
    }
    void Reset(){entries.clear();} // serial survives: old callbacks cannot clear a new request
};
}
