#include "ally_health_state.h"
#include "native_ui.h"
#include "native_ui_signatures.h"
#include "party_health_reports.h"
#include "../steamvr/ally_native.h"
#include "memory.h"
#include "sigscan.h"
#include <windows.h>
#include <mutex>
#include <cstring>
namespace allyhealthstate {
using namespace framevr;
namespace {
std::mutex mutex;allyhealth::List snapshot;uint32_t snapshotLocalId=0;
partyhealth::Report serverMerc{};
bool Read(uintptr_t p,void* out,size_t n){return p>=0x10000&&p<=UINTPTR_MAX-n&&framevr::ReadMemory(out,reinterpret_cast<void*>(p),n);}
uintptr_t ClientTable(){
 // FindClientUnit's own RIP-relative table address (3.3.93787 reference).
 // Resolve the displacement rather than assuming an absolute global address.
 const auto fn=d2rsig::Addr(0x9a5d0);uint8_t code[18]{};
 constexpr uint8_t prefix[]={0x4c,0x63,0xca,0x48,0x8d,0x05};
 constexpr uint8_t tail[]={0x8b,0xd1,0x44,0x8b,0xc1,0x49,0x8b,0xc9};
 if(!Read(fn,code,sizeof code)||std::memcmp(code,prefix,6)||std::memcmp(code+10,tail,8))return 0;
 int32_t displacement=0;std::memcpy(&displacement,code+6,4);
 return fn+10+intptr_t(displacement);
}
bool StatsReady(){using namespace nativeui::abi;
 return d2rsig::Check(StatRva,StatSig,sizeof StatSig)&&d2rsig::Check(StatBodyRva,StatBodySig,sizeof StatBodySig)&&d2rsig::Check(StatLookupRva,StatLookupSig,sizeof StatLookupSig);
}
bool Life(uintptr_t unit,uint32_t type,uint32_t id,float* health,float* maximum) noexcept {
 uint32_t h[3]{};if(!Read(unit,h,sizeof h)||h[0]!=type||h[2]!=id)return false;
 const auto fn=d2rsig::Addr(nativeui::abi::StatRva);if(!fn)return false;
 __try {
  const auto stat=reinterpret_cast<int(*)(uintptr_t,int,int)>(fn);
  const float hp=stat(unit,6,0)/256.f,max=stat(unit,7,0)/256.f;
  if(!partyhealth::Valid({hp,max}))return false;*health=hp;*maximum=max;return true;
 } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Position(allyhealth::Entry& e,const allynative::Unit& u){e.positionKnown=u.position;e.gridPosition=true;e.world={u.x,0,u.z};}
}
void PublishMerc(const partyhealth::Report& report){std::lock_guard lock(mutex);serverMerc=report;}
void Clear(){std::lock_guard lock(mutex);snapshot={};snapshotLocalId=0;serverMerc={};}
void Capture(uintptr_t player,uint64_t session,uint32_t localId){
 allyhealth::List result;result.session=session;result.at=GetTickCount64();
 uint32_t header[3]{};
 if(!session||!localId||!Read(player,header,sizeof header)||header[0]!=0||header[2]!=localId){Clear();return;}
 {std::lock_guard lock(mutex);if(snapshotLocalId==localId&&snapshot.session==session&&result.at>=snapshot.at&&result.at-snapshot.at<100)return;}
 const auto roster=nativeui::PartyRoster(session,result.at);
 // Reserve native roster names even without readable remote stats. Networking
 // can fill their health later, but it can never add a non-party identity.
 for(unsigned i=0;i<roster.count&&result.count<allyhealth::Capacity-1;++i){
  const auto& m=roster.members[i];if(!roster.SameParty(m.id)||!m.name[0])continue;
  auto& e=result.entries[result.count++];e.id=m.id;
  std::memcpy(e.name.data(),m.name.data(),m.name.size());
 }
 const auto table=ClientTable();
 if(table&&StatsReady()){
  const bool playersOk=allynative::Scan(table,0,Read,[&](const allynative::Unit& u){
   for(unsigned i=0;i<result.count;++i)if(result.entries[i].id==u.id){
    auto& e=result.entries[i];Position(e,u);Life(u.pointer,0,u.id,&e.health,&e.maxHealth);
   }
  });
  if(!playersOk)for(unsigned i=0;i<result.count;++i){auto& e=result.entries[i];e.positionKnown=false;e.health=e.maxHealth=0;}
  allyhealth::Entry merc;unsigned matches=0;
  const bool mercOk=allynative::Scan(table,1,Read,[&](const allynative::Unit& u){
   if(!allynative::OwnMercenary(u,localId))return;++matches;
   merc.id=u.id;merc.mercenary=true;std::strcpy(merc.name.data(),"Mercenary");Position(merc,u);
   Life(u.pointer,1,u.id,&merc.health,&merc.maxHealth);
  });
  // Owner + hireling class, never proximity. Ambiguous ownership hides the bar.
  // The native personal name is not yet resolved; the label says Mercenary.
  if(mercOk&&matches==1&&merc.id&&result.count<allyhealth::Capacity)result.entries[result.count++]=merc;
 }
 std::lock_guard lock(mutex);snapshot=result;snapshotLocalId=localId;
}
allyhealth::List Current(uint64_t session,uint64_t now){
 allyhealth::List source;partyhealth::Report authoritative;
 {std::lock_guard lock(mutex);source=snapshot;authoritative=serverMerc;}
 if(!source.Current(session,now))return {};
 partyhealth::Report remotes[partyhealth::kCapacity]{};
 const auto n=partyhealthreports::Remote(session,now,remotes,partyhealth::kCapacity);
 allyhealth::List result;result.session=session;result.at=source.at;
 for(unsigned i=0;i<source.count;++i){auto e=source.entries[i];
  if(e.mercenary&&authoritative.session==session){
   // Exact client-owned identity must also match the authoritative game-thread sample.
   // A stale/missing server sample hides it; never fall back to replica percentages.
   if(authoritative.unitId!=e.id||!authoritative.Current(session,now))continue;
   e.health=authoritative.health;e.maxHealth=authoritative.maxHealth;result.at=std::min(result.at,authoritative.at);
  }
  // Prefer the member's own validated life over replicated client stats.
  if(!e.mercenary)for(size_t j=0;j<n;++j)if(remotes[j].unitId==e.id&&remotes[j].Current(session,now)){
   e.health=remotes[j].health;e.maxHealth=remotes[j].maxHealth;result.at=std::min(result.at,remotes[j].at);break;
  }
  if(e.Valid())result.entries[result.count++]=e;
 }
 return result;
}
}
