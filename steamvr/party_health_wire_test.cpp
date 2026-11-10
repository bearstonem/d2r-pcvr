#include "../vr/party_health_reports.h"
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

using framevr::partyhealth::Sample;
void tick(uint32_t unit,uint64_t session,float hp=60){
 framevr::partyhealth::Report p{};p.unitId=unit;p.session=session;p.at=GetTickCount64();p.health=hp;p.maxHealth=100;p.known=true;
 partyhealthreports::PublishLocal(p);partyhealthreports::Tick(true,unit,session);runQueued();
}
std::vector<uint8_t> wire(uint32_t unit,uint32_t seq,float hp){
 std::vector<uint8_t>b(20);size_t n=0;std::memcpy(b.data(),&unit,4);std::memcpy(b.data()+4,&seq,4);
 assert(framevr::partyhealth::Encode({hp,100},b.data()+8,12,&n));return b;
}
size_t remotes(uint64_t session){framevr::partyhealth::Report reports[16]{};return partyhealthreports::Remote(session,GetTickCount64(),reports,16);}
}
int main(){
 init();missingNetwork=true;assert(!partyhealthreports::Init(&context));missingNetwork=false;assert(partyhealthreports::Init(&context));
 assert(registration.localChannelId!=0x5542);tick(101,7001);assert(toHost.size()==1&&toHost[0].bytes.size()==20);
 auto w=wire(202,1,45);registration.clientMessage(&context,kChannel,1,w.data(),uint32_t(w.size()),nullptr);
 assert(remotes(7001)==1&&remotes(8001)==0);
 framevr::partyhealth::Report reports[16]{};partyhealthreports::Remote(7001,GetTickCount64(),reports,16);
 assert(reports[0].health==45&&reports[0].maxHealth==100&&reports[0].session==7001);
 // Same sequence cannot refresh old data or rewrite its values.
 auto bad=wire(202,1,99);registration.clientMessage(&context,kChannel,1,bad.data(),20,nullptr);
 partyhealthreports::Remote(7001,GetTickCount64(),reports,16);assert(reports[0].health==45);
 ConnectionEvent event{};event.structSize=sizeof event;event.channel=kChannel;event.peer=11;event.state=ConnectionState::Connected;
 registration.connectionState(&context,&event,nullptr);Sleep(250);tick(101,7001);assert(toClient.empty()&&toHost.size()==2);
 // A different local session with the same character ID discards old reports.
 tick(101,8001);assert(remotes(8001)==0);
 registration.hostMessage(&context,kChannel,11,1,w.data(),20,nullptr);
 auto w2=wire(303,1,30);registration.hostMessage(&context,kChannel,12,1,w2.data(),20,nullptr);
 Sleep(250);tick(101,8001);assert(toClient.size()==4); // own health + other peer to each recipient
 auto conflict=wire(404,2,50);registration.hostMessage(&context,kChannel,11,1,conflict.data(),20,nullptr);
 assert(remotes(8001)==2);
 Sleep(framevr::partyhealth::kFreshMs+100);assert(remotes(8001)==0);
 registration.hostMessage(&context,kChannel,11,1,conflict.data(),20,nullptr);assert(remotes(8001)==1);
 // Local freshness is the acquisition time, not the most recent network tick.
 assert(!partyhealthreports::Local(8001,GetTickCount64()).known);
 partyhealthreports::Tick(false,0,0);assert(remotes(8001)==0);
 tick(999,9001);Sleep(250);partyhealthreports::Tick(true,999,9001);assert(!queued.empty());auto stale=queued.back();queued.pop_back();
 partyhealthreports::Stop();const auto before=toHost.size()+toClient.size();stale.cb(&context,stale.data);
 assert(toHost.size()+toClient.size()==before&&unregistered==1&&remotes(9001)==0);
 std::puts("party health wire: real transport mocked SDK send/receive, relay, local sessions, freshness, duplicates, identity and unload passed");
}
