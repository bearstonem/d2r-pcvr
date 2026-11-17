#include "target_reports.h"
#include <D2RLPlugin/network.h>
#include <D2RLPlugin/threads.h>
#include <windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#ifdef D2RVR_DEVELOPER
#include "../steamvr/logging.h"
#include <atomic>
#include <cstdio>
#endif
namespace targetreports { namespace {
namespace nw=D2RL::Network;
constexpr uint32_t Version=3,PerChunk=12,MaxTargets=framevr::targetcone::MaxTargets;
constexpr uint64_t Fresh=500;
static_assert(32+PerChunk*15<=nw::MaxPayloadSize);
struct Request{uint32_t version=Version,unit=0;uint64_t token=0;};
static_assert(sizeof(Request)==16);
struct Peer{nw::PeerHandle id=0;uint64_t ticket=0,at=0;Request request{};targetlock::Snapshot result{};uint64_t hit=0,hitAt=0;uint32_t target=0;};
std::mutex mutex;
const D2RL::PluginContext*ctx=nullptr;
const D2RL::NetworkService*net=nullptr;
const D2RL::ThreadService*threads=nullptr;
SampleFn sample=nullptr;nw::ChannelHandle channel=0;
bool dead=true;uint64_t session=0,serial=0,pending=0,pendingAt=0,uiTicket=0,nextSend=0,nextConnect=0;
uint64_t lastHit=0,impactAt=0;
uint32_t unit=0,received=0;targetlock::Snapshot published{},assembling{};
std::array<Peer,8>peers{};
struct Recent{uint64_t token=0,at=0,session=0;uint32_t unit=0;};std::array<Recent,8>recent{};size_t recentNext=0;
struct HitJob{uint64_t id=0,seq=0,at=0,session=0; nw::PeerHandle peer=0;Request request{};uint32_t target=0;bool sent=false;};std::array<HitJob,8>hitJobs{};size_t hitNext=0;
uint64_t Ticket(){if(!++serial)++serial;return serial;}
bool Valid(const targetlock::Snapshot&s){
 if(s.count<0||s.count>int(MaxTargets)||!std::isfinite(s.heroX)||!std::isfinite(s.heroY))return false;
 for(int i=0;i<s.count;++i){const auto&t=s.targets[i];if(!t.id||!std::isfinite(t.x)||!std::isfinite(t.y))return false;
  for(int j=0;j<i;++j)if(s.targets[j].id==t.id)return false;}
 return true;
}
void ClearClient(){recent={};recentNext=0;impactAt=0;pending=uiTicket=0;published={};assembling={};received=0;nextSend=nextConnect=0;}
Peer* Find(uint64_t ticket){for(auto&p:peers)if(p.ticket==ticket&&ticket)return &p;return nullptr;}
template<class T>void Put(uint8_t*b,size_t off,const T&v){std::memcpy(b+off,&v,sizeof v);}
template<class T>T Read(const uint8_t*b,size_t off){T v;std::memcpy(&v,b+off,sizeof v);return v;}
void __cdecl SendResult(const D2RL::PluginContext*,void*data)noexcept{
 const auto ticket=uint64_t(uintptr_t(data));Peer copy;
 {std::lock_guard lock(mutex);auto*p=Find(ticket);if(dead||!p||GetTickCount64()-p->at>Fresh)return;copy=*p;p->ticket=0;}
 if(copy.hit&&GetTickCount64()-copy.hitAt<=Fresh){
  uint8_t event[32]{};Put(event,0,Version);Put(event,4,copy.request.unit);Put(event,8,copy.request.token);
  Put(event,16,copy.hit);Put(event,24,copy.target);
  net->sendToClient(ctx,channel,copy.id,3,event,sizeof event);
 }
 const uint32_t count=uint32_t(copy.result.count),chunks=std::max(1u,(count+PerChunk-1)/PerChunk);
 for(uint32_t c=0;c<chunks;++c){
  uint8_t bytes[nw::MaxPayloadSize]{};const uint32_t offset=c*PerChunk,n=std::min(PerChunk,count-offset);
  Put(bytes,0,Version);Put(bytes,4,copy.request.unit);Put(bytes,8,copy.request.token);
  Put(bytes,16,count);Put(bytes,20,offset);Put(bytes,24,copy.result.heroX);Put(bytes,28,copy.result.heroY);
  for(uint32_t i=0;i<n;++i){auto&t=copy.result.targets[offset+i];const size_t at=32+i*15;
   Put(bytes,at,t.id);Put(bytes,at+4,t.x);Put(bytes,at+8,t.y);bytes[at+12]=t.clear?1:0;bytes[at+13]=t.bodyWidth;bytes[at+14]=t.bodyHeight;}
  net->sendToClient(ctx,channel,copy.id,2,bytes,32+n*15);
 }
}
void __cdecl SamplePeer(const D2RL::PluginContext*,void*data)noexcept{
 const auto ticket=uint64_t(uintptr_t(data));Peer copy;
 {std::lock_guard lock(mutex);auto*p=Find(ticket);if(dead||!p||GetTickCount64()-p->at>Fresh)return;copy=*p;}
 // A PlayerHandle is opaque. This establishes a connected peer, NOT that
 // its claimed unit ID is authenticated. The sampler is read-only and
 // validates the requested ID against live server units. Never use this
 // protocol to authorize attacks, inventory operations or other mutations.
 D2RL::PlayerHandle handle=0;targetlock::Snapshot s{};
 const bool ok=net->getPeerPlayer(ctx,channel,copy.id,&handle)==nw::Result::Success&&handle&&sample(copy.request.unit,s)&&Valid(s);
 {std::lock_guard lock(mutex);auto*p=Find(ticket);if(dead||!p)return;if(!ok){p->ticket=0;return;}p->result=s;}
 if(threads->runOnUiThread(ctx,SendResult,data)!=D2RL::Threads::Result::Success){std::lock_guard lock(mutex);if(auto*p=Find(ticket))p->ticket=0;}
}
void __cdecl Host(const D2RL::PluginContext*,nw::ChannelHandle ch,nw::PeerHandle peer,uint16_t id,const void*data,uint32_t size,void*)noexcept{
 if(id!=1||!data||size!=sizeof(Request)||!peer)return;Request request;std::memcpy(&request,data,sizeof request);
 if(request.version!=Version||!request.unit||!request.token)return;
 uint64_t ticket=0;const auto now=GetTickCount64();
 {std::lock_guard lock(mutex);if(dead||ch!=channel||!session)return;
  Peer*slot=nullptr;for(auto&p:peers)if(p.id==peer){slot=&p;break;}
  if(slot&&now-slot->at<100)return;
  if(slot&&slot->ticket&&now-slot->at<=Fresh)return;
  if(!slot)for(auto&p:peers)if(!p.id||now-p.at>2000){slot=&p;break;}
  if(!slot)return;ticket=Ticket();if(slot->id!=peer||slot->request.unit!=request.unit)*slot={};slot->id=peer;slot->ticket=ticket;slot->at=now;slot->request=request;
 }
 if(threads->runOnGameThread(ctx,SamplePeer,reinterpret_cast<void*>(uintptr_t(ticket)))!=D2RL::Threads::Result::Success){std::lock_guard lock(mutex);if(auto*p=Find(ticket))p->ticket=0;}
}
void __cdecl Client(const D2RL::PluginContext*,nw::ChannelHandle ch,uint16_t id,const void*data,uint32_t size,void*)noexcept{
 if(id==3){
  if(!data||size!=32)return;const auto*b=static_cast<const uint8_t*>(data);
  const auto now=GetTickCount64();std::lock_guard lock(mutex);
  const auto seq=Read<uint64_t>(b,16);
  const auto token=Read<uint64_t>(b,8);
  bool expected=pending&&token==pending&&now>=pendingAt&&now-pendingAt<=Fresh;
  for(const auto&r:recent)if(r.token==token&&r.unit==unit&&r.session==session&&now>=r.at&&now-r.at<=Fresh)expected=true;
  if(dead||ch!=channel||Read<uint32_t>(b,0)!=Version||Read<uint32_t>(b,4)!=unit||!session||
     !expected||!seq||seq<=lastHit||!Read<uint32_t>(b,24))return;
  lastHit=seq;impactAt=now;
#ifdef D2RVR_DEVELOPER
  static unsigned noted=0;if(noted++<80){char line[160];std::snprintf(line,sizeof line,"Frame client timing: confirmed hit packet received seq=%llu tick=%llu",(unsigned long long)seq,(unsigned long long)now);framevr::logging::Write(line);}
#endif
  return;
 }
 if(id!=2||!data||size<32||size>nw::MaxPayloadSize)return;
 auto*b=static_cast<const uint8_t*>(data);
 const auto v=Read<uint32_t>(b,0),u=Read<uint32_t>(b,4),count=Read<uint32_t>(b,16),offset=Read<uint32_t>(b,20);
 const auto token=Read<uint64_t>(b,8);
 if(v!=Version||!u||!token||count>MaxTargets||offset%PerChunk||offset>count||(count&&offset==count))return;
 const uint32_t n=std::min(PerChunk,count-offset),chunks=std::max(1u,(count+PerChunk-1)/PerChunk),bit=1u<<(offset/PerChunk);
 if(size!=32+n*15)return;
 targetlock::Snapshot part{};part.count=int(n);part.heroX=Read<float>(b,24);part.heroY=Read<float>(b,28);
 for(uint32_t i=0;i<n;++i){size_t at=32+i*15;if(b[at+12]>1||b[at+13]>8||(bool(b[at+13])!=bool(b[at+14])))return;part.targets[i]={Read<uint32_t>(b,at),Read<float>(b,at+4),Read<float>(b,at+8),b[at+12]!=0,b[at+13],b[at+14]};}
 if(!Valid(part))return;
 const auto now=GetTickCount64();std::lock_guard lock(mutex);
 if(dead||ch!=channel||!session||u!=unit||!pending||token!=pending||now-pendingAt>Fresh||(received&bit))return;
 if(received&&(assembling.count!=int(count)||assembling.heroX!=part.heroX||assembling.heroY!=part.heroY))return;
 auto next=assembling;next.count=int(count);next.heroX=part.heroX;next.heroY=part.heroY;
 for(uint32_t i=0;i<n;++i)next.targets[offset+i]=part.targets[i];
 // Reject duplicate IDs even when they arrive across separate chunks.
 for(uint32_t i=0;i<count;++i)if(next.targets[i].id)for(uint32_t j=0;j<i;++j)if(next.targets[j].id==next.targets[i].id)return;
 assembling=next;received|=bit;
 if(received==((1u<<chunks)-1)){if(!Valid(assembling))return;assembling.at=now;published=assembling;recent[recentNext++%recent.size()]={pending,pendingAt,session,unit};pending=0;received=0;assembling={};}
}
void __cdecl Connection(const D2RL::PluginContext*,const nw::ConnectionEvent*e,void*)noexcept{
 if(!e||e->structSize<nw::ConnectionEventRequiredSize)return;
 std::lock_guard lock(mutex);if(dead||e->channel!=channel)return;
 if(e->state==nw::ConnectionState::Disconnected||e->state==nw::ConnectionState::Rejected){
  for(auto&p:peers)if(p.id==e->peer)p={};for(auto&h:hitJobs)if(h.peer==e->peer)h={};ClearClient();}
}
void __cdecl SendRequest(const D2RL::PluginContext*,void*data)noexcept{
 const auto ticket=uint64_t(uintptr_t(data));Request r;bool connect=false;
 {std::lock_guard lock(mutex);const auto now=GetTickCount64();if(dead||!session||!unit||ticket!=uiTicket||!pending||now-pendingAt>Fresh)return;
  uiTicket=0;r.unit=unit;r.token=pending;connect=now>=nextConnect;if(connect)nextConnect=now+3000;}
 if(connect)net->connectToHost(ctx,channel);
 net->sendToHost(ctx,channel,1,&r,sizeof r);
}
}
bool Init(const D2RL::PluginContext*c,SampleFn fn){
 if(!c||!fn)return false;const D2RL::NetworkService*n=nullptr;const D2RL::ThreadService*t=nullptr;
 if(c->QueryService(&n)!=D2RL::ServiceQueryResult::Success||!D2RL::HasNetworkServiceField(n,D2RL::NetworkServiceRequiredSize)||
    !n->registerChannel||!n->unregisterChannel||!n->connectToHost||!n->sendToHost||!n->sendToClient||!n->getPeerPlayer||
    c->QueryService(&t)!=D2RL::ServiceQueryResult::Success||!D2RL::HasThreadServiceField(t,D2RL::ThreadServiceRequiredSize)||!t->runOnGameThread||!t->runOnUiThread)return false;
 {std::lock_guard lock(mutex);ctx=c;net=n;threads=t;sample=fn;dead=false;session=0;unit=0;peers={};hitJobs={};hitNext=0;lastHit=0;ClearClient();}
 nw::ChannelRegistration r{};r.structSize=sizeof r;r.localChannelId=0x5452;r.compatibilityToken=0x5043565254470003ull;
 r.hostMessage=Host;r.clientMessage=Client;r.connectionState=Connection;nw::ChannelHandle h=0;
 const auto result=n->registerChannel(c,&r,&h);{std::lock_guard lock(mutex);channel=h;dead=result!=nw::Result::Success;}
 c->LogInfo(result==nw::Result::Success?"PCVR targets: host-assisted target channel ready":"PCVR targets: channel unavailable");
 return result==nw::Result::Success;
}
void Tick(uint64_t s,uint32_t u,bool wanted){
 uint64_t ticket=0;const auto now=GetTickCount64();
 {std::lock_guard lock(mutex);if(dead)return;
  if(s!=session||u!=unit){session=s;unit=u;peers={};hitJobs={};hitNext=0;lastHit=0;ClearClient();}
  if(!s||!u||!wanted){ClearClient();return;}
  if(pending&&now-pendingAt>Fresh){pending=uiTicket=0;received=0;assembling={};}
  if(pending||now<nextSend)return;
  nextSend=now+100;pending=Ticket();pendingAt=now;uiTicket=ticket=Ticket();received=0;assembling={};
 }
 if(threads->runOnUiThread(ctx,SendRequest,reinterpret_cast<void*>(uintptr_t(ticket)))!=D2RL::Threads::Result::Success){std::lock_guard lock(mutex);if(uiTicket==ticket)pending=uiTicket=0;}
}
targetlock::Snapshot Get(uint64_t s,uint32_t u,uint64_t now){std::lock_guard lock(mutex);
 return !dead&&s&&s==session&&u==unit&&published.at&&now>=published.at&&now-published.at<=Fresh?published:targetlock::Snapshot{};
}
void __cdecl SendHit(const D2RL::PluginContext*,void*data)noexcept{
 const auto id=uint64_t(uintptr_t(data));HitJob copy;{std::lock_guard lock(mutex);auto it=std::find_if(hitJobs.begin(),hitJobs.end(),[&](const HitJob&h){return h.id==id&&id;});if(dead||it==hitJobs.end()||it->sent||it->session!=session||GetTickCount64()-it->at>Fresh)return;copy=*it;it->sent=true;}
 uint8_t event[32]{};Put(event,0,Version);Put(event,4,copy.request.unit);Put(event,8,copy.request.token);Put(event,16,copy.seq);Put(event,24,copy.target);const auto sent=net->sendToClient(ctx,channel,copy.peer,3,event,sizeof event);
#ifdef D2RVR_DEVELOPER
 static std::atomic<unsigned> noted{0};if(noted.fetch_add(1)<80){const auto now=GetTickCount64();char line[192];std::snprintf(line,sizeof line,
  "Frame client timing: host confirmed hit relay seq=%llu tick=%llu life-loss-to-send=%llu ms result=%u",
  (unsigned long long)copy.seq,(unsigned long long)now,(unsigned long long)(now>=copy.at?now-copy.at:0),unsigned(sent));framevr::logging::Write(line);}
#else
 (void)sent;
#endif
}
void NoteMeleeHit(uint32_t attacker,uint32_t target){
 if(!attacker||!target)return;uint64_t jobs[8]{};size_t count=0;{std::lock_guard lock(mutex);if(dead||!session)return;const auto now=GetTickCount64();for(auto&p:peers)if(p.id&&p.request.unit==attacker&&now-p.at<=2000){auto&h=hitJobs[hitNext++%hitJobs.size()];h={};h.id=Ticket();h.seq=h.id;h.at=now;h.session=session;h.peer=p.id;h.request=p.request;h.target=target;jobs[count++]=h.id;p.hit=h.seq;p.hitAt=now;p.target=target;}}
 for(size_t i=0;i<count;++i)if(threads->runOnUiThread(ctx,SendHit,reinterpret_cast<void*>(uintptr_t(jobs[i])) )!=D2RL::Threads::Result::Success){std::lock_guard lock(mutex);for(auto&h:hitJobs)if(h.id==jobs[i])h.sent=false;}
}

bool TakeMeleeHit(uint64_t s,uint32_t u,uint64_t now){
 std::lock_guard lock(mutex);const auto at=impactAt;impactAt=0;
 return !dead&&s&&s==session&&u==unit&&at&&now>=at&&now-at<=Fresh;
}
void Stop(){const D2RL::NetworkService*n;const D2RL::PluginContext*c;nw::ChannelHandle h;
 {std::lock_guard lock(mutex);dead=true;ClearClient();peers={};hitJobs={};session=0;unit=0;n=net;c=ctx;h=channel;channel=0;}
 if(n&&c&&h)n->unregisterChannel(c,h);
}
}
