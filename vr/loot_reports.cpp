#include "loot_reports.h"
#include <D2RLPlugin/network.h>
#include <D2RLPlugin/threads.h>
#include <windows.h>
#include <array>
#include <cstring>
#include <mutex>
namespace lootreports { namespace {
namespace nw=D2RL::Network;
constexpr uint32_t Version=1,RequestBytes=18;
constexpr uint64_t Fresh=1000,Rate=250;
constexpr size_t Max=framevr::lootbeams::MaxBeams;
static_assert(20+Max*13<=nw::MaxPayloadSize);
struct Request {uint32_t unit=0;uint64_t token=0;uint8_t flags=0,high=26;};
struct Peer {nw::PeerHandle id=0;uint64_t at=0,ticket=0;Request request{};lootscan::Shown result{};};
std::mutex mutex;
const D2RL::PluginContext*ctx=nullptr;
const D2RL::NetworkService*net=nullptr;
const D2RL::ThreadService*threads=nullptr;
SampleFn sample=nullptr;nw::ChannelHandle channel=0;
bool dead=true;
uint64_t session=0,serial=0,pending=0,pendingAt=0,nextSend=0,uiTicket=0,nextConnect=0;
uint32_t unit=0;Request request{};lootscan::Shown shown{};std::array<Peer,8>peers{};
uint64_t Ticket(){if(!++serial)++serial;return serial;}
template<class T>void Put(uint8_t*b,size_t at,T value){std::memcpy(b+at,&value,sizeof value);}
template<class T>T Read(const uint8_t*b,size_t at){T value;std::memcpy(&value,b+at,sizeof value);return value;}
bool Valid(const lootscan::Shown&s){
 if(s.count>Max)return false;
 for(size_t i=0;i<s.count;++i){const auto&a=s.items[i];
  if(!a.id||a.x<0||a.x>65535||a.y<0||a.y>65535||a.beam<framevr::lootbeams::Beam::Rare||a.beam>framevr::lootbeams::Beam::Rune)return false;
  for(size_t j=0;j<i;++j)if(s.items[j].id==a.id)return false;
 }return true;
}
void ClearClient(){pending=uiTicket=0;pendingAt=nextSend=nextConnect=0;shown={};}
Peer*Find(uint64_t t){for(auto&p:peers)if(t&&p.ticket==t)return &p;return nullptr;}
void __cdecl Reply(const D2RL::PluginContext*,void*data)noexcept{
 const auto t=uint64_t(uintptr_t(data));Peer copy;
 {std::lock_guard lock(mutex);auto*p=Find(t);const auto now=GetTickCount64();if(dead||!p||now<p->at||now-p->at>Fresh)return;copy=*p;p->ticket=0;}
 uint8_t bytes[nw::MaxPayloadSize]{};Put(bytes,0,Version);Put(bytes,4,copy.request.unit);Put(bytes,8,copy.request.token);Put(bytes,16,uint32_t(copy.result.count));
 for(size_t i=0;i<copy.result.count;++i){const auto&a=copy.result.items[i];size_t at=20+i*13;Put(bytes,at,a.id);Put(bytes,at+4,int32_t(a.x));Put(bytes,at+8,int32_t(a.y));bytes[at+12]=uint8_t(a.beam);}
 net->sendToClient(ctx,channel,copy.id,2,bytes,uint32_t(20+13*copy.result.count));
}
void __cdecl Sample(const D2RL::PluginContext*,void*data)noexcept{
 const auto t=uint64_t(uintptr_t(data));Peer copy;
 {std::lock_guard lock(mutex);auto*p=Find(t);const auto now=GetTickCount64();if(dead||!p||now<p->at||now-p->at>Fresh)return;copy=*p;}
 // Opaque handle proves only a connected peer. The claimed player ID is
 // validated by the read-only sampler; this is never an authorization path.
 D2RL::PlayerHandle handle=0;lootscan::Shown result{};const auto&r=copy.request;
 const framevr::lootbeams::Wanted wanted{bool(r.flags&1),bool(r.flags&2),bool(r.flags&4),bool(r.flags&8),r.high};
 const bool ok=net->getPeerPlayer(ctx,channel,copy.id,&handle)==nw::Result::Success&&handle&&sample(r.unit,wanted,result)&&Valid(result);
 {std::lock_guard lock(mutex);auto*p=Find(t);if(dead||!p)return;if(!ok){p->ticket=0;return;}p->result=result;}
 if(threads->runOnUiThread(ctx,Reply,data)!=D2RL::Threads::Result::Success){std::lock_guard lock(mutex);if(auto*p=Find(t))p->ticket=0;}
}
void __cdecl Host(const D2RL::PluginContext*,nw::ChannelHandle ch,nw::PeerHandle peer,uint16_t id,const void*data,uint32_t size,void*)noexcept{
 if(id!=1||!peer||!data||size!=RequestBytes)return;const auto*b=static_cast<const uint8_t*>(data);
 Request r{Read<uint32_t>(b,4),Read<uint64_t>(b,8),b[16],b[17]};
 if(Read<uint32_t>(b,0)!=Version||!r.unit||!r.token||(r.flags&~15)||r.high<1||r.high>33)return;
 uint64_t ticket=0;const auto now=GetTickCount64();
 {std::lock_guard lock(mutex);if(dead||ch!=channel||!session)return;
  Peer*slot=nullptr;for(auto&p:peers)if(p.id==peer){slot=&p;break;}
  if(slot&&now>=slot->at&&(now-slot->at<Rate||(slot->ticket&&now-slot->at<=Fresh)))return;
  if(!slot)for(auto&p:peers)if(!p.id||now<p.at||now-p.at>2000){slot=&p;break;}
  if(!slot)return;*slot={};slot->id=peer;slot->at=now;slot->request=r;slot->ticket=ticket=Ticket();
 }
 if(threads->runOnGameThread(ctx,Sample,reinterpret_cast<void*>(uintptr_t(ticket)))!=D2RL::Threads::Result::Success){std::lock_guard lock(mutex);if(auto*p=Find(ticket))p->ticket=0;}
}
void __cdecl Client(const D2RL::PluginContext*,nw::ChannelHandle ch,uint16_t id,const void*data,uint32_t size,void*)noexcept{
 if(id!=2||!data||size<20||size>nw::MaxPayloadSize)return;const auto*b=static_cast<const uint8_t*>(data);const auto count=Read<uint32_t>(b,16);
 if(Read<uint32_t>(b,0)!=Version||count>Max||size!=20+13*count)return;
 lootscan::Shown result{};result.count=count;
 for(size_t i=0;i<count;++i){size_t at=20+13*i;result.items[i]={Read<uint32_t>(b,at),Read<int32_t>(b,at+4),Read<int32_t>(b,at+8),framevr::lootbeams::Beam(b[at+12])};}
 if(!Valid(result))return;
 const auto now=GetTickCount64();std::lock_guard lock(mutex);
 if(dead||ch!=channel||!session||Read<uint32_t>(b,4)!=unit||!pending||Read<uint64_t>(b,8)!=pending||now<pendingAt||now-pendingAt>Fresh)return;
 result.at=now;shown=result;pending=uiTicket=0;
}
void __cdecl Connection(const D2RL::PluginContext*,const nw::ConnectionEvent*e,void*)noexcept{
 if(!e||e->structSize<nw::ConnectionEventRequiredSize)return;
 std::lock_guard lock(mutex);if(dead||e->channel!=channel)return;
 if(e->state==nw::ConnectionState::Disconnected||e->state==nw::ConnectionState::Rejected){for(auto&p:peers)if(p.id==e->peer)p={};ClearClient();}
}
void __cdecl Send(const D2RL::PluginContext*,void*data)noexcept{
 const auto ticket=uint64_t(uintptr_t(data));Request copy;bool connect=false;
 {std::lock_guard lock(mutex);const auto now=GetTickCount64();
  if(dead||ticket!=uiTicket||!session||!unit||!pending||now<pendingAt||now-pendingAt>Fresh)return;
  copy=request;uiTicket=0;connect=now>=nextConnect;if(connect)nextConnect=now+1000;
 }
 // Encode explicitly: C++ struct padding is not part of the protocol.
 uint8_t bytes[RequestBytes]{};Put(bytes,0,Version);Put(bytes,4,copy.unit);Put(bytes,8,copy.token);bytes[16]=copy.flags;bytes[17]=copy.high;
 if(connect)net->connectToHost(ctx,channel);
 net->sendToHost(ctx,channel,1,bytes,sizeof bytes);
}
}
bool Init(const D2RL::PluginContext*c,SampleFn fn){
 if(!c||!fn)return false;const D2RL::NetworkService*n=nullptr;const D2RL::ThreadService*t=nullptr;
 if(c->QueryService(&n)!=D2RL::ServiceQueryResult::Success||!D2RL::HasNetworkServiceField(n,D2RL::NetworkServiceRequiredSize)||!n->registerChannel||!n->unregisterChannel||!n->connectToHost||!n->sendToHost||!n->sendToClient||!n->getPeerPlayer||
    c->QueryService(&t)!=D2RL::ServiceQueryResult::Success||!D2RL::HasThreadServiceField(t,D2RL::ThreadServiceRequiredSize)||!t->runOnGameThread||!t->runOnUiThread)return false;
 {std::lock_guard lock(mutex);ctx=c;net=n;threads=t;sample=fn;dead=false;session=0;unit=0;peers={};ClearClient();}
 nw::ChannelRegistration r{};r.structSize=sizeof r;r.localChannelId=0x4c42;r.compatibilityToken=0x4c4f4f5452500001ull;r.hostMessage=Host;r.clientMessage=Client;r.connectionState=Connection;
 nw::ChannelHandle h=0;const auto result=n->registerChannel(c,&r,&h);
 {std::lock_guard lock(mutex);channel=h;dead=result!=nw::Result::Success;}return result==nw::Result::Success;
}
void Tick(uint64_t s,uint32_t u,bool enabled,const framevr::lootbeams::Wanted&w){
 uint64_t ticket=0;const auto now=GetTickCount64();
 {std::lock_guard lock(mutex);if(dead)return;
  if(s!=session||u!=unit){session=s;unit=u;peers={};ClearClient();}
  if(!s||!u||!enabled||w.highRune<1||w.highRune>33){ClearClient();return;}
  if(pending&&(now<pendingAt||now-pendingAt>Fresh))pending=uiTicket=0;
  if(pending||now<nextSend)return;
  nextSend=now+Rate;pending=Ticket();pendingAt=now;uiTicket=ticket=Ticket();
  request={u,pending,uint8_t((w.rare?1:0)|(w.set?2:0)|(w.unique?4:0)|(w.runes?8:0)),uint8_t(w.highRune)};
 }
 if(threads->runOnUiThread(ctx,Send,reinterpret_cast<void*>(uintptr_t(ticket)))!=D2RL::Threads::Result::Success){std::lock_guard lock(mutex);if(uiTicket==ticket)pending=uiTicket=0;}
}
lootscan::Shown Get(uint64_t s,uint32_t u,uint64_t now){std::lock_guard lock(mutex);
 return !dead&&s&&s==session&&u==unit&&shown.at&&now>=shown.at&&now-shown.at<=Fresh?shown:lootscan::Shown{};
}
void Stop(){const D2RL::NetworkService*n;const D2RL::PluginContext*c;nw::ChannelHandle h;
 {std::lock_guard lock(mutex);dead=true;ClearClient();peers={};session=0;unit=0;n=net;c=ctx;h=channel;channel=0;}
 if(n&&c&&h)n->unregisterChannel(c,h);
}
}
