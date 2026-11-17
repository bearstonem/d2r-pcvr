#include "party_roster.h"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace framevr::partyroster;
struct Memory {
    std::array<std::array<unsigned char,0x150>,17> entries{};
    unsigned count=0;bool failed=false;
    uintptr_t Add(uint32_t id,uint16_t party,const char* name) {
        auto& b=entries[count];const uintptr_t address=0x100000+count*0x200;
        std::memcpy(b.data(),name,std::strlen(name));
        std::memcpy(b.data()+0x48,&id,4);std::memcpy(b.data()+0x5a,&party,2);
        if(count)std::memcpy(entries[count-1].data()+0x148,&address,8);
        ++count;return address;
    }
    bool operator()(uintptr_t p,void* out,size_t n)const {
        if(failed||p<0x100000)return false;
        const auto i=(p-0x100000)/0x200,off=(p-0x100000)%0x200;
        if(i>=count||off+n>0x150)return false;
        std::memcpy(out,entries[i].data()+off,n);return true;
    }
};
int main(){
    Memory m;m.Add(1,7,"Self");m.Add(2,7,"Friend");m.Add(3,8,"Other");
    auto s=ReadRoster(0x100000,1,100,500,m);
    assert(s.known&&s.count==3&&s.members[1].id==2);
    assert(s.SameParty(2)&&!s.SameParty(3)&&!s.SameParty(1));
    assert(std::strcmp(s.members[1].name.data(),"Friend")==0);
    assert(s.Current(100,500)&&s.Current(100,1500)&&!s.Current(100,1501));
    assert(!s.Current(101,500)&&!s.Current(100,499));
    assert(!ReadRoster(0x100000,99,100,500,m).known);
    assert(!ReadRoster(0x100000,1,0,500,m).known);
    assert(!ReadRoster(0,1,100,500,m).known);
    m.failed=true;assert(!ReadRoster(0x100000,1,100,500,m).known);m.failed=false;
    uint32_t duplicate=1;std::memcpy(m.entries[1].data()+0x48,&duplicate,4);
    assert(!ReadRoster(0x100000,1,100,500,m).known);
    Memory none;none.Add(1,0xffff,"Self");none.Add(2,0xffff,"Other");
    assert(!ReadRoster(0x100000,1,100,500,none).SameParty(2));
    uintptr_t cycle=0x100000;std::memcpy(none.entries[1].data()+0x148,&cycle,8);
    assert(!ReadRoster(0x100000,1,100,500,none).known);
    Memory large;for(unsigned i=0;i<17;++i)large.Add(i+1,7,"Player");
    assert(!ReadRoster(0x100000,1,100,500,large).known);
    uintptr_t zero=0;std::memcpy(large.entries[15].data()+0x148,&zero,8);
    assert(ReadRoster(0x100000,1,100,500,large).count==16);
    std::memset(large.entries[1].data(),'a',61);
    s=ReadRoster(0x100000,1,100,500,large);assert(s.members[1].name[61]==0);
    assert(RosterPointer(0x200000,0x100)==0x200107);
    assert(RosterPointer(0x200000,-0x100)==0x1fff07);
    assert(!RosterPointer(1,-100));
    std::puts("party roster: membership, names, session/expiry, duplicate, cycle, failure and bounds checks passed");
}
