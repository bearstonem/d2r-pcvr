#include "../vr/ally_health_state.h"
#include "../vr/native_ui.h"
#include "../vr/party_health_reports.h"
#include <windows.h>
#include <cassert>
#include <cstring>
#include <array>
#include <vector>
#include <cstdio>
namespace {
std::vector<uint8_t> bytes(0x10000);bool stats=true;framevr::partyroster::Snapshot roster;
framevr::partyhealth::Report remote;
uintptr_t at(size_t offset){return reinterpret_cast<uintptr_t>(bytes.data()+offset);}
template<class T>void put(size_t offset,const T&v){std::memcpy(bytes.data()+offset,&v,sizeof v);}
int NativeStat(uintptr_t unit,int stat,int){uint32_t id=0;std::memcpy(&id,reinterpret_cast<void*>(unit+8),4);return (stat==7?100:id==202?40:70)*256;}
void unit(size_t offset,uint32_t type,uint32_t id,uint32_t cls,uint32_t owner){
 put(offset,type);put(offset+4,cls);put(offset+8,id);put(offset+12,uint32_t(1));
 put(offset+0x10,at(offset+0x200));put(offset+0x254,owner);put(offset+0x38,at(offset+0x300));uint16_t xy[4]={0,100,0,200};put(offset+0x300,xy);
 put(0x1000+type*0x400+(id&127)*8,at(offset));
}
}
namespace d2rsig {
uintptr_t Addr(uint64_t rva){return rva==0x9a5d0?at(0x100):reinterpret_cast<uintptr_t>(&NativeStat);}
bool Check(uint64_t,const void*,uint32_t){return stats;}
}
namespace nativeui {framevr::partyroster::Snapshot PartyRoster(uint64_t s,uint64_t now){return roster.Current(s,now)?roster:framevr::partyroster::Snapshot{};}}
namespace partyhealthreports {size_t Remote(uint64_t s,uint64_t now,framevr::partyhealth::Report*out,size_t n){if(n&&remote.Current(s,now)){*out=remote;return 1;}return 0;}}
int main(){
 const uint8_t code[]={0x4c,0x63,0xca,0x48,0x8d,0x05,0,0,0,0,0x8b,0xd1,0x44,0x8b,0xc1,0x49,0x8b,0xc9};put(0x100,code);put(0x106,int32_t(0x1000-0x10a));
 unit(0x2000,0,101,1,0);unit(0x3000,0,202,2,0);unit(0x4000,0,303,3,0);
 unit(0x5000,1,404,271,101);unit(0x6000,1,505,271,303);
 auto now=GetTickCount64();roster.known=true;roster.session=7001;roster.at=now;roster.localId=101;roster.count=3;
 roster.members[0].id=101;roster.members[0].party=4;
 roster.members[1].id=202;roster.members[1].party=4;std::strcpy(roster.members[1].name.data(),"Peer Two");
 roster.members[2].id=303;roster.members[2].party=5;std::strcpy(roster.members[2].name.data(),"Nonparty");
 allyhealthstate::Clear();allyhealthstate::Capture(at(0x2000),7001,101);now=GetTickCount64();auto list=allyhealthstate::Current(7001,now);
 assert(list.count==2&&list.entries[0].id==202&&list.entries[0].health==40&&list.entries[0].positionKnown);
 assert(list.entries[1].id==404&&list.entries[1].mercenary&&list.entries[1].health==70);
 assert(allyhealthstate::Current(9999,now).count==0&&allyhealthstate::Current(7001,now+1100).count==0);
 remote={7001,now,202,20,100,0,0,0,false,true};assert(allyhealthstate::Current(7001,now).entries[0].health==20);
 // Network presence cannot introduce a player outside the actual party.
 remote.unitId=303;assert(allyhealthstate::Current(7001,now).count==2);
 // The client replica is not authoritative: server full life must win.
 allyhealthstate::PublishMerc({7001,now,404,100,100,0,0,0,false,true});
 assert(allyhealthstate::Current(7001,now).entries[1].health==100);
 allyhealthstate::PublishMerc({7001,now,404,25,100,0,0,0,false,true});
 assert(allyhealthstate::Current(7001,now).entries[1].health==25);
 allyhealthstate::PublishMerc({7001,now,505,100,100,0,0,0,false,true});
 assert(allyhealthstate::Current(7001,now).count==1); // another merc is never substituted
 allyhealthstate::PublishMerc({7001,now-1100,404,100,100,0,0,0,false,true});
 assert(allyhealthstate::Current(7001,now).count==1); // no fallback to wrong client percentage
 // Session reset must not keep a previous authoritative report.
 allyhealthstate::Clear();allyhealthstate::Capture(at(0x2000),7001,101);
 assert(allyhealthstate::Current(7001,GetTickCount64()).entries[1].health==70);
 // Two owner matches are ambiguous: neither mercenary is shown.
 unit(0x7000,1,606,338,101);allyhealthstate::Clear();allyhealthstate::Capture(at(0x2000),7001,101);assert(allyhealthstate::Current(7001,GetTickCount64()).count==1);
 // Corrupt/cyclic monster hash cannot leak a partial mercenary result.
 put(0x5000+0x158,at(0x5000));allyhealthstate::Clear();allyhealthstate::Capture(at(0x2000),7001,101);assert(allyhealthstate::Current(7001,GetTickCount64()).count==1);
 stats=false;allyhealthstate::Clear();allyhealthstate::Capture(at(0x2000),7001,101);assert(allyhealthstate::Current(7001,GetTickCount64()).count==0);
 allyhealthstate::Capture(at(0x2000),7001,999);assert(allyhealthstate::Current(7001,GetTickCount64()).count==0);
 std::puts("ally state: production acquisition with mock native memory, party filter, exact merc owner, self-health merge, corruption and freshness passed");
}
