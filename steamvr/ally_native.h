#pragma once
// Read-only adaptation of diablo2-vr-port-main vr/unit_scan.h and
// vr/client_verdict.h (MPL-2.0). Never uses nearest-friendly identity.
#include <array>
#include <cstdint>
#include <cstring>
namespace framevr::allynative {
struct Unit {uintptr_t pointer=0;uint32_t type=0,id=0,cls=0,mode=0,owner=UINT32_MAX;float x=0,z=0;bool position=false;};
inline bool Hireling(uint32_t c){return c==271||c==338||c==359||c==560||c==561;}
inline bool OwnMercenary(const Unit& u,uint32_t local){return local&&u.type==1&&Hireling(u.cls)&&u.owner==local&&u.id&&u.mode!=0&&u.mode!=12;}
// All-or-nothing bounded row. Corruption/cycles/duplicate IDs discard its output.
template<class Read,class Fn>bool Scan(uintptr_t table,uint32_t type,Read read,Fn emit) {
 if(!table||type>1)return false;
 std::array<uintptr_t,128> heads{};if(!read(table+type*0x400,heads.data(),sizeof heads))return false;
 std::array<uintptr_t,2048> visited{};std::array<uint32_t,2048> ids{};unsigned n=0;
 for(auto p:heads)for(unsigned chain=0;p;++chain){
  if(chain>=64||n>=visited.size()||p<0x10000||p>UINTPTR_MAX-0x160)return false;
  for(unsigned i=0;i<n;++i)if(visited[i]==p)return false;
  std::array<uint8_t,0x160> b{};if(!read(p,b.data(),b.size()))return false;
  auto u32=[&](size_t at){uint32_t v;std::memcpy(&v,b.data()+at,4);return v;};
  auto ptr=[&](size_t at){uintptr_t v;std::memcpy(&v,b.data()+at,8);return v;};
  Unit u;u.pointer=p;u.type=u32(0);u.cls=u32(4);u.id=u32(8);u.mode=u32(12);
  if(u.type!=type||!u.id)return false;
  for(unsigned i=0;i<n;++i)if(ids[i]==u.id)return false;
  visited[n]=p;ids[n++]=u.id;
  uint16_t xy[4]{};const auto path=ptr(0x38);
  if(path>=0x10000&&read(path,xy,sizeof xy)){
   u.x=float(xy[1])+float(xy[0])/65536.f;u.z=float(xy[3])+float(xy[2])/65536.f;
   u.position=u.x>0&&u.z>0;
  }
  if(type==1&&Hireling(u.cls)){
   const auto data=ptr(0x10);uint32_t owner=UINT32_MAX;
   if(data>=0x10000&&data<=UINTPTR_MAX-0x58&&read(data+0x54,&owner,4))u.owner=owner;
  }
  emit(u);p=ptr(0x158);
 }
 return true;
}
}
