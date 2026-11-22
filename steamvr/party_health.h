#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace framevr::partyhealth {
constexpr uint32_t kFreshMs=1000;
constexpr size_t kCapacity=16,kPacketBytes=12;
struct Report {
 uint64_t session=0,at=0;uint32_t unitId=0;float health=0,maxHealth=0;
 float x=0,y=0,z=0;bool positionKnown=false,known=false;
 bool Current(uint64_t s,uint64_t now)const {
  return known&&s&&session==s&&unitId&&now>=at&&now-at<=kFreshMs&&std::isfinite(health)&&std::isfinite(maxHealth)&&health>=0&&maxHealth>0&&maxHealth<1000000&&health<=maxHealth*2;
 }
};
// Only values cross the wire. Session handles and process clocks never do.
struct Sample {float health=0,maxHealth=0;};
inline bool Valid(const Sample& p){return std::isfinite(p.health)&&std::isfinite(p.maxHealth)&&p.health>=0&&p.maxHealth>0&&p.maxHealth<1000000&&p.health<=p.maxHealth*2;}
inline bool Encode(const Sample& p,uint8_t* out,size_t cap,size_t* n){
 if(!out||!n||cap<kPacketBytes||!Valid(p))return false;
 out[0]='H';out[1]='P';out[2]=1;out[3]=0;std::memcpy(out+4,&p.health,4);std::memcpy(out+8,&p.maxHealth,4);*n=kPacketBytes;return true;
}
inline bool Decode(const uint8_t* in,size_t n,Sample* out){
 if(!in||!out||n!=kPacketBytes||in[0]!='H'||in[1]!='P'||in[2]!=1||in[3])return false;
 Sample p;std::memcpy(&p.health,in+4,4);std::memcpy(&p.maxHealth,in+8,4);if(!Valid(p))return false;*out=p;return true;
}
struct Remote {uint32_t unit=0;Sample pose{};uint32_t at=0;};
class Playback {
 struct Slot {uint32_t unit=0,seq=0,at=0;Sample value{};};std::array<Slot,kCapacity> slots{};
public:
 bool Push(uint32_t unit,uint32_t seq,const Sample& p,uint32_t now){
  if(!unit||!Valid(p))return false;
  for(auto& s:slots)if(s.unit&&uint32_t(now-s.at)>kFreshMs)s={};
  Slot* slot=nullptr;for(auto& s:slots)if(s.unit==unit){slot=&s;break;}
  if(slot&&int32_t(seq-slot->seq)<=0)return false;
  if(!slot)for(auto& s:slots)if(!s.unit){slot=&s;break;}
  if(!slot)return false;*slot={unit,seq,now,p};return true;
 }
 size_t List(Remote* out,size_t cap,uint32_t now)const {
  size_t n=0;for(const auto&s:slots)if(s.unit&&n<cap&&uint32_t(now-s.at)<=kFreshMs)out[n++]={s.unit,s.value,s.at};return n;
 }
 void Reset(){slots={};}
};
}
