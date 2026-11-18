// Upper-body visual channel; native combat remains separate.
#include "upper_body_wire.h"
#include <D2RLPlugin/network.h>
#include <D2RLPlugin/threads.h>
#include <windows.h>
#include <mutex>
#include <array>
namespace upperbody {
namespace {
namespace nw=D2RL::Network;
const D2RL::PluginContext* context=nullptr;
const D2RL::NetworkService* net=nullptr;
const D2RL::ThreadService* threads=nullptr;
nw::ChannelHandle channel=0;
std::mutex mutex;
bool dead=true,inWorld=false,host=false,ownValid=false;
uint32_t ownUnit=0,sequence=0,generation=1,pending=0;
uint64_t nextSend=0,nextConnect=0,queuedAt=0,ownAt=0;
Pose ownPose{};Playback playback;
struct Peer {nw::PeerHandle id=0;uint32_t unit=0,seq=0;Pose pose{};uint64_t at=0;};
std::array<Peer,kCapacity> peers{};
constexpr size_t wireBytes=8+kPacketBytes;
static_assert(wireBytes<=nw::MaxPayloadSize);
bool Packet(const void*data,uint32_t size,uint32_t&unit,uint32_t&seq,Pose&p){
 if(!data||size!=wireBytes)return false;
 const auto*b=static_cast<const uint8_t*>(data);std::memcpy(&unit,b,4);std::memcpy(&seq,b+4,4);
 return unit&&Decode(b+8,kPacketBytes,&p);
}
void Send(bool toHost,nw::PeerHandle peer,uint32_t unit,uint32_t seq,const Pose&p){
 uint8_t b[wireBytes];size_t n=0;if(!unit||!Encode(p,b+8,kPacketBytes,&n))return;
 std::memcpy(b,&unit,4);std::memcpy(b+4,&seq,4);
 if(toHost)net->sendToHost(context,channel,1,b,sizeof b);
 else net->sendToClient(context,channel,peer,1,b,sizeof b);
}
void __cdecl OnHost(const D2RL::PluginContext*,nw::ChannelHandle ch,nw::PeerHandle peer,uint16_t id,const void*data,uint32_t size,void*) noexcept {
 // Presence-only clients have no tracked pose, but still receive VR players.
 // Message 2 never creates playback or changes an existing pose binding.
 if(id==2){
  uint32_t unit=0;if(!data||size!=sizeof unit)return;std::memcpy(&unit,data,sizeof unit);
  std::lock_guard lock(mutex);if(dead||!inWorld||ch!=channel||!peer||!unit||unit==ownUnit)return;
  host=true;for(const auto&x:peers)if(x.id==peer)return;
  for(auto&x:peers)if(!x.id){x.id=peer;break;}
  return;
 }
 uint32_t unit=0,seq=0;Pose p;if(id!=1||!Packet(data,size,unit,seq,p))return;
 std::lock_guard lock(mutex);if(dead||!inWorld||ch!=channel||unit==ownUnit)return;
 host=true;Peer*slot=nullptr;
 for(auto&x:peers)if(x.id==peer){slot=&x;break;}
 if(!slot)for(auto&x:peers)if(!x.id){x.id=peer;slot=&x;break;}
 if(!slot)return;
 const auto now=GetTickCount64();
 // A connection can outlive its character. Protect live bindings, but do not
 // permanently pin an expired identity after a character/session transition.
 if(slot->unit&&slot->unit!=unit&&now-slot->at<=kFreshMs)return;
 for(const auto&x:peers)if(x.id!=peer&&x.unit==unit&&now-x.at<=kFreshMs)return;
 // Push expires old playback before considering a restarted sequence. Only
 // release conflicting stale ownership after the new sample is accepted.
 if(!playback.Push(unit,seq,p,uint32_t(now)))return;
 for(auto&x:peers)if(x.id!=peer&&x.unit==unit){x.unit=0;x.seq=0;x.at=0;}
 slot->unit=unit;slot->seq=seq;slot->pose=p;slot->at=now;
}
void __cdecl OnClient(const D2RL::PluginContext*,nw::ChannelHandle ch,uint16_t id,const void*data,uint32_t size,void*) noexcept {
 uint32_t unit=0,seq=0;Pose p;if(id!=1||!Packet(data,size,unit,seq,p))return;
 std::lock_guard lock(mutex);if(dead||!inWorld||ch!=channel||unit==ownUnit)return;
 playback.Push(unit,seq,p,uint32_t(GetTickCount64()));
}
void __cdecl OnConnection(const D2RL::PluginContext*,const nw::ConnectionEvent*e,void*) noexcept {
 if(!e||e->structSize<nw::ConnectionEventRequiredSize)return;
 std::lock_guard lock(mutex);if(dead||e->channel!=channel)return;
 // Connection events do not distinguish host from client; only OnHost does.
 if(e->state==nw::ConnectionState::Disconnected||e->state==nw::ConnectionState::Rejected){
  for(auto&x:peers)if(x.id==e->peer)x={};
  playback.Reset();nextConnect=0;
 }
}
void __cdecl UiJob(const D2RL::PluginContext*,void*user) noexcept {
 const uint32_t ticket=uint32_t(reinterpret_cast<uintptr_t>(user));
 std::array<Peer,kCapacity> copy{};Pose mine{};uint32_t unit=0,seq=0;bool isHost=false,have=false,connect=false;
 const auto now=GetTickCount64();
 {
  std::lock_guard lock(mutex);
  if(dead||!inWorld||ticket!=pending)return;
  pending=0;isHost=host;copy=peers;mine=ownPose;unit=ownUnit;
  have=ownValid&&unit&&now-ownAt<=kFreshMs;seq=++sequence;
  if(!isHost&&now>=nextConnect){nextConnect=now+3000;connect=true;}
 }
 // SDK callbacks can reenter: do not hold our state lock over SDK calls.
 if(!isHost){
  if(connect){
   net->connectToHost(context,channel);
   // Repeat presence at the connection retry cadence, not the pose frame rate.
   // New desktop clients send no pose; older peers safely ignore message 2.
   if(!have&&unit)net->sendToHost(context,channel,2,&unit,sizeof unit);
  }
  if(have)Send(true,0,unit,seq,mine);
 }else{
  for(const auto&dest:copy)if(dest.id){
   if(have)Send(false,dest.id,unit,seq,mine);
   for(const auto&src:copy)if(src.id&&src.id!=dest.id&&src.unit&&now-src.at<=kFreshMs)
    Send(false,dest.id,src.unit,src.seq,src.pose);
  }
 }
}
}
bool Init(const D2RL::PluginContext*ctx,LogFn log){
 if(!ctx)return false;
 const D2RL::NetworkService*n=nullptr;const D2RL::ThreadService*t=nullptr;
 if(ctx->QueryService(&n)!=D2RL::ServiceQueryResult::Success||!D2RL::HasNetworkServiceField(n,D2RL::NetworkServiceRequiredSize)||
    !n->registerChannel||!n->unregisterChannel||!n->connectToHost||!n->sendToHost||!n->sendToClient||
    ctx->QueryService(&t)!=D2RL::ServiceQueryResult::Success||!D2RL::HasThreadServiceField(t,D2RL::ThreadServiceRequiredSize)||!t->runOnUiThread){
  if(log)log("PCVR upper body: loader network/thread service unavailable; disabled");return false;
 }
 {std::lock_guard lock(mutex);context=ctx;net=n;threads=t;dead=false;inWorld=false;host=false;peers={};playback.Reset();pending=0;ownUnit=0;ownValid=false;++generation;}
 nw::ChannelRegistration reg{};reg.structSize=sizeof reg;reg.localChannelId=0x5542;reg.compatibilityToken=0x5043565255420002ull;
 reg.hostMessage=&OnHost;reg.clientMessage=&OnClient;reg.connectionState=&OnConnection;
 nw::ChannelHandle handle=0;auto result=n->registerChannel(ctx,&reg,&handle);
 {std::lock_guard lock(mutex);channel=handle;if(result!=nw::Result::Success){dead=true;channel=0;}}
 if(log)log(result==nw::Result::Success?"PCVR upper body: visual pose channel registered (25Hz)":"PCVR upper body: channel registration failed; disabled");
 return result==nw::Result::Success;
}
void Tick(bool inGame,const Pose*own,uint32_t unit){
 uint32_t ticket=0;const D2RL::PluginContext*ctx;const D2RL::ThreadService*t;
 const auto now=GetTickCount64();
 {
  std::lock_guard lock(mutex);if(dead||!channel)return;
  if(inGame!=inWorld||(unit&&ownUnit&&unit!=ownUnit)){
   inWorld=inGame;host=false;peers={};playback.Reset();pending=0;++generation;nextConnect=nextSend=0;sequence=0;
  }
  ownUnit=unit;ownValid=own&&Valid(*own);if(ownValid){ownPose=*own;ownAt=now;}
  if(!inWorld||now<nextSend)return;
  if(pending&&now-queuedAt<1000)return;
  nextSend=now+40;pending=++generation;if(!pending)pending=++generation;
  ticket=pending;queuedAt=now;ctx=context;t=threads;
 }
 if(t->runOnUiThread(ctx,&UiJob,reinterpret_cast<void*>(uintptr_t(ticket)))!=D2RL::Threads::Result::Success){
  std::lock_guard lock(mutex);if(pending==ticket)pending=0;
 }
}
size_t Remotes(Remote*out,size_t cap){
 if(!out||!cap)return 0;std::lock_guard lock(mutex);
 return dead||!inWorld?0:playback.List(out,cap,uint32_t(GetTickCount64()));
}
void Shutdown(){
 const D2RL::NetworkService*n;const D2RL::PluginContext*ctx;nw::ChannelHandle ch;
 {std::lock_guard lock(mutex);dead=true;inWorld=false;ownValid=false;pending=0;++generation;peers={};playback.Reset();n=net;ctx=context;ch=channel;}
 if(n&&ctx&&ch)n->unregisterChannel(ctx,ch);
 // Loader owns callback quiescence and discards queued UI work on unload.
}
}
