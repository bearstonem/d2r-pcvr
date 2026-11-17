#pragma once
// Read-only adaptation of vr/party_cmd_core.h in diablo2-vr-port-main.
// Preserve upstream MPL-2.0 attribution; no invite/leave functions are used.
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
namespace framevr::partyroster {
inline constexpr uint64_t EntryRva=0x136200;
inline constexpr uint8_t EntrySig[]={0x48,0x8b,0x15};
inline constexpr uint8_t EntryTail[]={0x83,0xf9,0xff,0x74,0x2e,0x48};
inline constexpr unsigned Capacity=16;
struct Member {
    uint32_t id=0;
    uint16_t party=0xffff;
    std::array<char,62> name{};
};
struct Snapshot {
    bool known=false;
    uint64_t session=0,at=0;
    uint32_t localId=0;
    unsigned count=0;
    std::array<Member,Capacity> members{};
    bool Current(uint64_t s,uint64_t now)const {
        return known&&s&&s==session&&now>=at&&now-at<=1000;
    }
    bool SameParty(uint32_t id)const {
        if(!known||!id||id==localId)return false;
        uint16_t ours=0xffff;
        for(unsigned i=0;i<count;++i)if(members[i].id==localId)ours=members[i].party;
        if(ours==0xffff)return false;
        for(unsigned i=0;i<count;++i)if(members[i].id==id)return members[i].party==ours;
        return false;
    }
};
inline uintptr_t RosterPointer(uintptr_t instruction,int32_t displacement) {
    if(instruction>uintptr_t(std::numeric_limits<int64_t>::max()-7))return 0;
    const int64_t p=int64_t(instruction)+7+int64_t(displacement);
    return p>0x10000?uintptr_t(p):0;
}
template<class Read>Snapshot ReadRoster(uintptr_t entry,uint32_t local,uint64_t session,uint64_t now,Read read) {
    Snapshot s;
    if(!entry||!local||!session)return s;
    std::array<uintptr_t,Capacity> visited{};
    bool foundLocal=false;
    while(entry) {
        if(entry<=0x10000||entry>std::numeric_limits<uintptr_t>::max()-0x150||s.count>=Capacity)return {};
        for(unsigned i=0;i<s.count;++i)if(visited[i]==entry)return {};
        std::array<unsigned char,0x6c> raw{};uintptr_t next=0;
        if(!read(entry,raw.data(),raw.size())||!read(entry+0x148,&next,sizeof next))return {};
        Member m;std::memcpy(&m.id,raw.data()+0x48,4);std::memcpy(&m.party,raw.data()+0x5a,2);
        if(!m.id)return {};
        for(unsigned i=0;i<s.count;++i)if(s.members[i].id==m.id)return {};
        // Bound and terminate the native byte string; don't turn control bytes into UI markup.
        for(unsigned i=0;i<61&&raw[i];++i){if(raw[i]<32||raw[i]==127)break;m.name[i]=char(raw[i]);}
        visited[s.count]=entry;s.members[s.count++]=m;foundLocal|=m.id==local;entry=next;
    }
    if(!foundLocal)return {};
    s.known=true;s.session=session;s.at=now;s.localId=local;return s;
}
}
