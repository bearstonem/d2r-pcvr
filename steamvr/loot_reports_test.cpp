#include "../vr/loot_reports.h"
#include <D2RLPlugin/api.h>
#include <windows.h>
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>
using namespace D2RL;using namespace D2RL::Network;
namespace {
PluginApi api{};PluginContext ctx{};NetworkService nw{};ThreadService th{};ChannelRegistration reg{};
struct Job{Threads::Callback cb;void*data;bool game;};std::vector<Job>jobs;
struct Packet{PeerHandle peer;uint16_t id;std::vector<unsigned char>b;};std::vector<Packet>host,clients;
bool onUi=false,onGame=false,peerValid=true,sampleOk=true,uiOk=true;unsigned samples=0,connects=0,unregistered=0;size_t count=2;
ServiceQueryResult __cdecl Query(const PluginContext*,ServiceId id,uint32_t,const void**out)noexcept{if(id==ServiceId::Network)*out=&nw;else if(id==ServiceId::Thread)*out=&th;else return ServiceQueryResult::Unavailable;return ServiceQueryResult::Success;}
Result __cdecl Register(const PluginContext*,const ChannelRegistration*r,ChannelHandle*h)noexcept{reg=*r;*h=77;return Result::Success;}
Result __cdecl Unregister(const PluginContext*,ChannelHandle)noexcept{++unregistered;return Result::Success;}
Result __cdecl Connect(const PluginContext*,ChannelHandle)noexcept{assert(onUi);++connects;return Result::Busy;}
Result __cdecl SH(const PluginContext*,ChannelHandle,uint16_t id,const void*d,uint32_t n)noexcept{assert(onUi&&n<=MaxPayloadSize);auto*b=(const unsigned char*)d;host.push_back({0,id,{b,b+n}});return Result::Success;}
Result __cdecl SC(const PluginContext*,ChannelHandle,PeerHandle p,uint16_t id,const void*d,uint32_t n)noexcept{assert(onUi&&n<=MaxPayloadSize);auto*b=(const unsigned char*)d;clients.push_back({p,id,{b,b+n}});return Result::Success;}
Result __cdecl Peer(const PluginContext*,ChannelHandle,PeerHandle,PlayerHandle*p)noexcept{assert(onGame);*p=peerValid?123:0;return Result::Success;}
Threads::Result __cdecl Game(const PluginContext*,Threads::Callback f,void*d)noexcept{jobs.push_back({f,d,true});return Threads::Result::Success;}
Threads::Result __cdecl Ui(const PluginContext*,Threads::Callback f,void*d)noexcept{if(!uiOk)return Threads::Result::Unavailable;jobs.push_back({f,d,false});return Threads::Result::Success;}
bool Sample(uint32_t id,const framevr::lootbeams::Wanted&w,lootscan::Shown&s){assert(onGame&&id==100&&w.highRune==26&&w.rare);++samples;lootreports::Get(7,100,GetTickCount64());s={};s.count=count;for(size_t i=0;i<count&&i<s.items.size();++i)s.items[i]={uint32_t(200+i),int(10+i),20,framevr::lootbeams::Beam::Rare};return sampleOk;}
void run(){assert(!jobs.empty());auto j=jobs.front();jobs.erase(jobs.begin());onGame=j.game;onUi=!j.game;j.cb(&ctx,j.data);onGame=onUi=false;}
void receive(const Packet&p){reg.clientMessage(&ctx,77,p.id,p.b.data(),uint32_t(p.b.size()),nullptr);}
void request(){lootreports::Tick(7,100,true,{});assert(jobs.size()==1);run();assert(!host.empty());}
void serve(const Packet&p,PeerHandle peer=9){reg.hostMessage(&ctx,77,peer,p.id,p.b.data(),uint32_t(p.b.size()),nullptr);assert(jobs.size()==1);run();if(peerValid&&sampleOk&&count<=8){assert(jobs.size()==1);run();}}
}
int main(){
api.apiSize=sizeof api;api.queryService=Query;ctx.contextSize=sizeof ctx;ctx.api=&api;
nw.serviceVersion=1;nw.serviceSize=sizeof nw;nw.registerChannel=Register;nw.unregisterChannel=Unregister;nw.connectToHost=Connect;nw.sendToHost=SH;nw.sendToClient=SC;nw.getPeerPlayer=Peer;
th.serviceVersion=1;th.serviceSize=sizeof th;th.runOnGameThread=Game;th.runOnUiThread=Ui;
assert(lootreports::Init(&ctx,Sample));request();assert(samples==0&&connects==1);serve(host.back());assert(samples==1&&clients.size()==1);auto valid=clients.back();receive(valid);
auto s=lootreports::Get(7,100,GetTickCount64());assert(s.count==2&&s.items[0].id==200);auto stamp=s.at;receive(valid);assert(lootreports::Get(7,100,GetTickCount64()).at==stamp);
assert(!lootreports::Get(8,100,GetTickCount64()).at&&!lootreports::Get(7,101,GetTickCount64()).at&&!lootreports::Get(7,100,stamp+1001).at);
Sleep(270);request();serve(host.back());valid=clients.back();auto bad=valid;
bad.b[0]=99;receive(bad);bad=valid;bad.b[8]^=128;receive(bad);bad=valid;bad.b[4]^=1;receive(bad);
bad=valid;bad.b[16]=9;receive(bad);bad=valid;bad.b[32]=9;receive(bad);bad=valid;memcpy(bad.b.data()+33,bad.b.data()+20,4);receive(bad);
bad=valid;int32_t negative=-1;memcpy(bad.b.data()+24,&negative,4);receive(bad);assert(lootreports::Get(7,100,GetTickCount64()).at==stamp);
receive(valid);assert(lootreports::Get(7,100,GetTickCount64()).at!=stamp);
Sleep(270);request();count=0;serve(host.back());receive(clients.back());assert(lootreports::Get(7,100,GetTickCount64()).count==0);
// Host serves peers with its local requesting/rendering off (desktop or beams disabled).
auto req=host.back();lootreports::Tick(7,100,false,{});Sleep(270);count=8;serve(req);assert(clients.back().b.size()==124);
// Flood limited; invalid filters and wrong channels never sample.
auto before=jobs.size();reg.hostMessage(&ctx,77,9,1,req.b.data(),uint32_t(req.b.size()),nullptr);assert(jobs.size()==before);
auto invalid=req;invalid.b[16]=16;reg.hostMessage(&ctx,77,10,1,invalid.b.data(),uint32_t(invalid.b.size()),nullptr);assert(jobs.empty());
// Queued game work cannot survive a local session transition.
reg.hostMessage(&ctx,77,10,1,req.b.data(),uint32_t(req.b.size()),nullptr);assert(jobs.size()==1);before=samples;lootreports::Tick(8,100,false,{});run();assert(samples==before);
lootreports::Tick(7,100,false,{});request();peerValid=false;serve(host.back());assert(jobs.empty());peerValid=true;
lootreports::Tick(7,100,false,{});request();count=2;serve(host.back(),11);valid=clients.back();const auto connectionsBeforeTimeout=connects;Sleep(1050);receive(valid);assert(!lootreports::Get(7,100,GetTickCount64()).at);
// A Busy connect is retried, rather than leaving the channel permanently unconnected.
lootreports::Tick(7,100,true,{});assert(jobs.size()==1);run();assert(connects==connectionsBeforeTimeout+1);
ConnectionEvent event{};event.structSize=sizeof event;event.channel=77;event.peer=11;event.state=ConnectionState::Disconnected;reg.connectionState(&ctx,&event,nullptr);assert(!lootreports::Get(7,100,GetTickCount64()).at);
uiOk=false;lootreports::Tick(7,100,true,{});assert(jobs.empty());uiOk=true;Sleep(270);request();auto sent=host.size();lootreports::Tick(8,100,false,{});receive(valid);assert(!lootreports::Get(8,100,GetTickCount64()).at);
// Only eight peers may schedule work. Re-enter Get from the sampler through
// the existing mock path in production without holding the module mutex.
lootreports::Tick(10,100,false,{});auto samplesBefore=samples;auto repliesBefore=clients.size();
for(PeerHandle p=20;p<28;++p)reg.hostMessage(&ctx,77,p,1,req.b.data(),uint32_t(req.b.size()),nullptr);
assert(jobs.size()==8);reg.hostMessage(&ctx,77,28,1,req.b.data(),uint32_t(req.b.size()),nullptr);assert(jobs.size()==8);
while(!jobs.empty())run();assert(samples==samplesBefore+8&&clients.size()==repliesBefore+8);
lootreports::Tick(7,100,true,{});assert(jobs.size()==1);lootreports::Stop();run();assert(host.size()==sent&&unregistered==1);
puts("loot reports: real SDK callbacks, filters, bounds, malformed/duplicate/stale packets, desktop host, retries and shutdown passed");
}
