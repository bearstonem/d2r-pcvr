#pragma once
#include <cstdint>
namespace framevr::voice {
struct Command {uint64_t sequence=0,at=0,session=0;int skill=-1;};
struct Gate {
    uint64_t seen=0,last=0;
    bool Take(const Command& c,uint64_t session,uint64_t now,bool allowed){
        if(!c.sequence||c.sequence==seen)return false;
        seen=c.sequence; // Busy, stale and menu commands are discarded, never deferred.
        if(!allowed||!session||c.session!=session||now<c.at||now-c.at>400||c.skill<0||
           (last&&(now<last||now-last<1500)))return false;
        last=now;return true;
    }
};
}
