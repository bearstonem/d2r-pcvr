#include "../vr/melee_dispatch.h"
#include <D2RLPlugin/context.h>
#include <D2RLPlugin/network.h>
#include <D2RLPlugin/threads.h>
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace D2RL;using namespace D2RL::Network;
namespace {
struct Job{Threads::Callback cb;void* data;bool game;};
struct Packet{PeerHandle peer;uint16_t id;std::vector<uint8_t> b;};
NetworkService nw{};ThreadService th{};ChannelRegistration reg{};PluginContext ctx{};PluginApi api{};
std::vector<Job> jobs;std::vector<Packet> sent,reply;bool onGame=false,onUi=false,peerValid=true;unsigned applied=0,unregistered=0;
ServiceQueryResult __cdecl Query(const PluginContext*,ServiceId id,uint32_t,const void** out)noexcept{
 if(id==ServiceId::Network)*out=&nw;else if(id==ServiceId::Thread)*out=&th;else return ServiceQueryResult::Unavailable;return ServiceQueryResult::Success;
}
Result __cdecl Register(const PluginContext*,const ChannelRegistration* r,ChannelHandle* h)noexcept{reg=*r;*h=55;return Result::Success;}
Result __cdecl Unregister(const PluginContext*,ChannelHandle)noexcept{++unregistered;return Result::Success;}
Result __cdecl Connect(const PluginContext*,ChannelHandle)noexcept{assert(onUi);return Result::Success;}
Result __cdecl SH(const PluginContext*,ChannelHandle,uint16_t id,const void* d,uint32_t n)noexcept{assert(onUi&&n<=MaxPayloadSize);auto b=static_cast<const uint8_t*>(d);sent.push_back({0,id,{b,b+n}});return Result::Success;}
Result __cdecl SC(const PluginContext*,ChannelHandle,PeerHandle peer,uint16_t id,const void* d,uint32_t n)noexcept{assert(onUi&&n<=MaxPayloadSize);auto b=static_cast<const uint8_t*>(d);reply.push_back({peer,id,{b,b+n}});return Result::Success;}
Result __cdecl Peer(const PluginContext*,ChannelHandle,PeerHandle,PlayerHandle* h)noexcept{assert(onGame);*h=peerValid?999:0;return Result::Success;}
Threads::Result __cdecl Game(const PluginContext*,Threads::Callback cb,void* d)noexcept{jobs.push_back({cb,d,true});return Threads::Result::Success;}
Threads::Result __cdecl Ui(const PluginContext*,Threads::Callback cb,void* d)noexcept{jobs.push_back({cb,d,false});return Threads::Result::Success;}
bool Apply(uint64_t peer,const framevr::melee::dispatch::Contact& c){assert(onGame&&peer==9&&c.hero==100);++applied;return true;}
void Run(){assert(!jobs.empty());auto j=jobs.front();jobs.erase(jobs.begin());onGame=j.game;onUi=!j.game;j.cb(&ctx,j.data);onGame=onUi=false;}
void Drain(){unsigned n=0;while(!jobs.empty()){assert(++n<200);Run();}}
void Host(const Packet& p){reg.hostMessage(&ctx,55,9,p.id,p.b.data(),uint32_t(p.b.size()),nullptr);}
void Client(const Packet& p){reg.clientMessage(&ctx,55,p.id,p.b.data(),uint32_t(p.b.size()),nullptr);}
}
int main(){
 api.apiSize=sizeof api;api.queryService=Query;ctx.contextSize=sizeof ctx;ctx.api=&api;
 nw.serviceVersion=1;nw.serviceSize=sizeof nw;nw.registerChannel=Register;nw.unregisterChannel=Unregister;nw.connectToHost=Connect;nw.sendToHost=SH;nw.sendToClient=SC;nw.getPeerPlayer=Peer;
 th.serviceVersion=1;th.serviceSize=sizeof th;th.runOnGameThread=Game;th.runOnUiThread=Ui;
 assert(meleedispatch::Init(&ctx,Apply));meleedispatch::Tick(7);assert(!meleedispatch::Ready(7));
 assert(!jobs.empty());Drain();assert(sent.size()==1);auto hello=sent.back();Host(hello);Drain();assert(reply.size()==1&&reply.back().peer==9);
 Client(reply.back());assert(meleedispatch::Ready(7)&&!meleedispatch::Ready(8));
 onGame=true;assert(meleedispatch::Alive(9,7)&&!meleedispatch::Alive(9,8)&&!meleedispatch::Alive(10,7));onGame=false;
 framevr::melee::dispatch::Contact c;c.hero=100;c.session=7;c.sequence=1;c.sweep.stroke=1;c.sample.active=true;
 assert(meleedispatch::Send(c));assert(applied==0);Drain();auto packet=sent.back();Host(packet);assert(applied==0);Drain();assert(applied==1);
 Host(packet);Drain();assert(applied==1);
 c.sequence=2;c.hero=101;assert(meleedispatch::Send(c));Drain();Host(sent.back());Drain();assert(applied==1);
 c.hero=100;c.sequence=3;assert(meleedispatch::Send(c));Drain();peerValid=false;Host(sent.back());Drain();assert(applied==1);peerValid=true;
 c.sequence=4;assert(meleedispatch::Send(c));Drain();Host(sent.back());Sleep(300);Drain();assert(applied==1);
 c.sequence=5;assert(meleedispatch::Send(c));Drain();Host(sent.back());
 ConnectionEvent e{};e.structSize=sizeof e;e.channel=55;e.peer=9;e.state=ConnectionState::Disconnected;reg.connectionState(&ctx,&e,nullptr);Drain();assert(applied==1&&!meleedispatch::Ready(7));
 onGame=true;assert(!meleedispatch::Alive(9,7));onGame=false;
 Client(reply.back());assert(!meleedispatch::Ready(7));meleedispatch::Tick(8);Client(reply.back());assert(!meleedispatch::Ready(8));
 auto n=sent.size();meleedispatch::Stop();Drain();assert(sent.size()==n&&unregistered==1&&!meleedispatch::Ready(8));
 puts("melee dispatch: real callbacks, UI sends, host-thread apply, handshake, replay, identity binding, expiry and cancellation passed");
}
