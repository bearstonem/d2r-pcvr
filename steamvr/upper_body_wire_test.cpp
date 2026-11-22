#include "../vr/upper_body_wire.h"
#include <D2RLPlugin/network.h>
#include <D2RLPlugin/threads.h>
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using namespace D2RL;
using namespace D2RL::Network;
constexpr ChannelHandle kChannel=77;
struct Queued { Threads::Callback cb; void* data; };
struct Packet { PeerHandle peer; std::vector<uint8_t> bytes; };
NetworkService network{}; ThreadService thread{};
ChannelRegistration registration{};
std::vector<Queued> queued;
std::vector<Packet> toHost, toClient, presence;
unsigned unregistered=0, connects=0;
bool missingNetwork=false;
ServiceQueryResult __cdecl Query(const PluginContext*,ServiceId id,uint32_t,const void**out) noexcept {
 if(id==ServiceId::Network){if(missingNetwork)return ServiceQueryResult::Unavailable;*out=&network;return ServiceQueryResult::Success;}
 if(id==ServiceId::Thread){*out=&thread;return ServiceQueryResult::Success;}
 return ServiceQueryResult::Unavailable;
}
Result __cdecl Register(const PluginContext*,const ChannelRegistration*r,ChannelHandle*h) noexcept {registration=*r;*h=kChannel;return Result::Success;}
Result __cdecl Unregister(const PluginContext*,ChannelHandle h) noexcept {assert(h==kChannel);++unregistered;return Result::Success;}
Result __cdecl Connect(const PluginContext*,ChannelHandle h) noexcept {assert(h==kChannel);++connects;return Result::Success;}
Result __cdecl SendHost(const PluginContext*,ChannelHandle h,uint16_t id,const void*d,uint32_t n) noexcept {assert(h==kChannel&&(id==1||id==2));auto*b=(const uint8_t*)d;if(id==2)presence.push_back({0,{b,b+n}});else toHost.push_back({0,{b,b+n}});return Result::Success;}
Result __cdecl SendClient(const PluginContext*,ChannelHandle h,PeerHandle p,uint16_t id,const void*d,uint32_t n) noexcept {assert(h==kChannel&&id==1);auto*b=(const uint8_t*)d;toClient.push_back({p,{b,b+n}});return Result::Success;}
Threads::Result __cdecl QueueUi(const PluginContext*,Threads::Callback cb,void*d) noexcept {queued.push_back({cb,d});return Threads::Result::Success;}
PluginContext context{};PluginApi api{};
void init(){api.apiSize=sizeof api;api.queryService=Query;context.contextSize=sizeof context;context.api=&api;
 network={};network.serviceSize=sizeof network;network.serviceVersion=1;network.registerChannel=Register;network.unregisterChannel=Unregister;network.connectToHost=Connect;network.sendToHost=SendHost;network.sendToClient=SendClient;
 thread={};thread.serviceSize=sizeof thread;thread.serviceVersion=1;thread.runOnUiThread=QueueUi;}
void runQueued(){assert(!queued.empty());auto q=queued.front();queued.erase(queued.begin());q.cb(&context,q.data);}
upperbody::Pose pose(float x){upperbody::Pose p{};p.tracked=3;p.hand[0][0]=x;p.hand[1][1]=x+.25f;p.sampleMs=uint32_t(x*100);p.resolved.mask=15;for(auto&f:p.resolved.frame){f[0]=-1;f[4]=f[8]=1;f[9]=x;}return p;}
void checkPacket(const Packet&packet,uint32_t unit,float x){assert(packet.bytes.size()==8+upperbody::kPacketBytes);uint32_t actual=0;std::memcpy(&actual,packet.bytes.data(),4);assert(actual==unit);upperbody::Pose decoded{};assert(upperbody::Decode(packet.bytes.data()+8,upperbody::kPacketBytes,&decoded));assert(decoded.hand[0][0]==x);assert(decoded.resolved.mask==15&&decoded.resolved.frame[3][0]==-1&&decoded.resolved.frame[3][9]==x);}
void tickAfter(uint32_t unit,const upperbody::Pose&p){Sleep(80);upperbody::Tick(true,&p,unit);runQueued();}
}
int main(){
 init();missingNetwork=true;assert(!upperbody::Init(&context,nullptr));missingNetwork=false;
 assert(upperbody::Init(&context,nullptr));assert(registration.hostMessage&&registration.clientMessage&&registration.connectionState);
 const auto own=pose(1.0f);upperbody::Tick(true,&own,101);assert(queued.size()==1);runQueued();assert(connects==1&&toHost.size()==1);checkPacket(toHost.back(),101,1.0f);
 // Client receives host/peer pose; own-unit echoes are ignored.
 auto remote=pose(2.0f);uint8_t wire[8+upperbody::kPacketBytes]{};size_t bytes=0;assert(upperbody::Encode(remote,wire+8,upperbody::kPacketBytes,&bytes));uint32_t unit=202,seq=1;std::memcpy(wire,&unit,4);std::memcpy(wire+4,&seq,4);
 registration.clientMessage(&context,kChannel,1,wire,sizeof wire,nullptr);upperbody::Remote remotes[4]{};assert(upperbody::Remotes(remotes,4)==1&&remotes[0].unit==202);assert(remotes[0].pose.resolved.mask==15&&remotes[0].pose.resolved.frame[2][9]==2);
 // Nonzero peer connection events also occur on clients; never infer role.
 ConnectionEvent event{};event.structSize=sizeof event;event.channel=kChannel;event.peer=11;event.state=ConnectionState::Connected;
 registration.connectionState(&context,&event,nullptr);auto hostBefore=toHost.size();tickAfter(101,own);assert(toHost.size()==hostBefore+1&&toClient.empty());
 // New session: actual host callback proves host role, two peers permit relay.
 upperbody::Tick(false,nullptr,0);const auto hostPose=pose(3.0f);upperbody::Tick(true,&hostPose,303);runQueued();
 registration.hostMessage(&context,kChannel,11,1,wire,sizeof wire,nullptr);
 uint32_t second=404;std::memcpy(wire,&second,4);registration.hostMessage(&context,kChannel,12,1,wire,sizeof wire,nullptr);
 const size_t before=toClient.size();tickAfter(303,hostPose);assert(toClient.size()==before+4);
 bool host11=false,host12=false,relay11=false,relay12=false;
 for(size_t i=before;i<toClient.size();++i){uint32_t u=0;std::memcpy(&u,toClient[i].bytes.data(),4);
  if(u==303){checkPacket(toClient[i],303,3);if(toClient[i].peer==11)host11=true;if(toClient[i].peer==12)host12=true;}
  if(u==202){checkPacket(toClient[i],202,2);assert(toClient[i].peer==12);relay12=true;}
  if(u==404){checkPacket(toClient[i],404,2);assert(toClient[i].peer==11);relay11=true;}
 }
 assert(host11&&host12&&relay11&&relay12);
 // Live bindings must reject conflicting identities, but expired bindings recover.
 auto deliver=[&](PeerHandle peer,uint32_t u,uint32_t s){
  std::memcpy(wire,&u,4);std::memcpy(wire+4,&s,4);
  registration.hostMessage(&context,kChannel,peer,1,wire,sizeof wire,nullptr);
 };
 auto contains=[&](uint32_t u){auto n=upperbody::Remotes(remotes,4);for(size_t i=0;i<n;++i)if(remotes[i].unit==u)return true;return false;};
 deliver(11,505,2);assert(!contains(505)&&contains(202)); // cannot replace a live identity
 deliver(12,202,2);assert(contains(404)); // cannot steal another live peer's identity
 Sleep(upperbody::kFreshMs+150);
 deliver(11,505,1);assert(contains(505)&&!contains(202)); // new identity after stale binding
 // Former peer 12's unit can move to another connection once its binding expires.
 deliver(13,404,1);assert(contains(404)&&contains(505));
 deliver(13,606,2);assert(!contains(606)); // renewed binding is protected again
 deliver(14,505,2);assert(contains(505));
 // Rejected duplicate must not poison the legitimate stream.
 deliver(11,505,3);assert(contains(505));
 // Desktop receiver: a presence message registers a recipient without a VR pose.
 uint32_t desktopUnit=707;
 registration.hostMessage(&context,kChannel,15,2,&desktopUnit,sizeof desktopUnit,nullptr);
 assert(!contains(desktopUnit));
 const auto desktopBefore=toClient.size();tickAfter(303,hostPose);
 bool desktopGotHost=false;
 for(size_t i=desktopBefore;i<toClient.size();++i)if(toClient[i].peer==15){
  uint32_t u=0;std::memcpy(&u,toClient[i].bytes.data(),4);if(u==303)desktopGotHost=true;
 }
 assert(desktopGotHost);
 // Queue while active, THEN exit: the stale callback must not send.
 Sleep(80);upperbody::Tick(true,&hostPose,303);assert(!queued.empty());auto stale=queued.back();queued.pop_back();
 upperbody::Tick(false,nullptr,0);const auto beforeStale=toHost.size()+toClient.size();stale.cb(&context,stale.data);assert(toHost.size()+toClient.size()==beforeStale);
 // A desktop client announces itself without fabricating tracked hands.
 const auto poseBeforeDesktop=toHost.size();
 upperbody::Tick(true,nullptr,909);runQueued();
 assert(presence.size()==1&&presence.back().bytes.size()==sizeof(uint32_t));
 uint32_t announced=0;std::memcpy(&announced,presence.back().bytes.data(),4);assert(announced==909);
 assert(toHost.size()==poseBeforeDesktop);
 upperbody::Tick(false,nullptr,0);
 upperbody::Tick(true,&own,101);assert(!queued.empty());auto unload=queued.back();queued.pop_back();upperbody::Shutdown();
 const auto sent=toHost.size()+toClient.size();unload.cb(&context,unload.data);assert(toHost.size()+toClient.size()==sent);assert(unregistered==1);assert(upperbody::Remotes(remotes,4)==0);
 std::puts("upper body wire: mocked SDK bidirectional send/receive, host relay, session reset, unload and missing service passed");
}