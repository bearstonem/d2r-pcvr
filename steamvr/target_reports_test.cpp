#include "../vr/target_reports.h"
#include <D2RLPlugin/network.h>
#include <D2RLPlugin/threads.h>
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace D2RL;using namespace D2RL::Network;
namespace {
struct Job{Threads::Callback cb;void* data;bool game;};
struct Packet{PeerHandle peer;uint16_t id;std::vector<unsigned char>b;};
NetworkService nw{};ThreadService th{};ChannelRegistration reg{};PluginContext ctx{};PluginApi api{};
std::vector<Job>jobs;std::vector<Packet>host,clients;
bool onGame=false,onUi=false,peerValid=true,sampleOk=true;int targetCount=64;unsigned samples=0,unregistered=0;
ServiceQueryResult __cdecl Query(const PluginContext*,ServiceId id,uint32_t,const void**o)noexcept{if(id==ServiceId::Network)*o=&nw;else if(id==ServiceId::Thread)*o=&th;else return ServiceQueryResult::Unavailable;return ServiceQueryResult::Success;}
Result __cdecl Register(const PluginContext*,const ChannelRegistration*r,ChannelHandle*h)noexcept{reg=*r;*h=55;return Result::Success;}
Result __cdecl Unregister(const PluginContext*,ChannelHandle)noexcept{++unregistered;return Result::Success;}
Result __cdecl Connect(const PluginContext*,ChannelHandle)noexcept{assert(onUi);return Result::Success;}
Result __cdecl SH(const PluginContext*,ChannelHandle,uint16_t id,const void*d,uint32_t n)noexcept{assert(onUi&&n<=MaxPayloadSize);auto*b=(const unsigned char*)d;host.push_back({0,id,{b,b+n}});return Result::Success;}
Result __cdecl SC(const PluginContext*,ChannelHandle,PeerHandle p,uint16_t id,const void*d,uint32_t n)noexcept{assert(onUi&&n<=MaxPayloadSize);auto*b=(const unsigned char*)d;clients.push_back({p,id,{b,b+n}});return Result::Success;}
Result __cdecl Peer(const PluginContext*,ChannelHandle,PeerHandle,PlayerHandle*p)noexcept{assert(onGame);*p=peerValid?999:0;return Result::Success;}
Threads::Result __cdecl QueueGame(const PluginContext*,Threads::Callback cb,void*d)noexcept{jobs.push_back({cb,d,true});return Threads::Result::Success;}
Threads::Result __cdecl QueueUi(const PluginContext*,Threads::Callback cb,void*d)noexcept{jobs.push_back({cb,d,false});return Threads::Result::Success;}
bool Sample(uint32_t u,targetlock::Snapshot&s){assert(onGame&&u==100);++samples;s={};s.count=targetCount;s.heroX=10;s.heroY=20;for(int i=0;i<targetCount;++i)s.targets[i]={uint32_t(i+1),float(11+i),20,i%2==0};return sampleOk;}
void run(){assert(!jobs.empty());auto j=jobs.front();jobs.erase(jobs.begin());onGame=j.game;onUi=!j.game;j.cb(&ctx,j.data);onGame=onUi=false;}
void receive(const Packet&p){reg.clientMessage(&ctx,55,p.id,p.b.data(),uint32_t(p.b.size()),nullptr);}
void request(){targetreports::Tick(7,100,true);assert(jobs.size()==1);run();assert(!host.empty());}
void serve(){auto r=host.back();reg.hostMessage(&ctx,55,9,r.id,r.b.data(),uint32_t(r.b.size()),nullptr);assert(jobs.size()==1);run();if(sampleOk&&peerValid){assert(jobs.size()==1);run();}}
}
int main(){
 api.apiSize=sizeof api;api.queryService=Query;ctx.contextSize=sizeof ctx;ctx.api=&api;
 nw.serviceVersion=1;nw.serviceSize=sizeof nw;nw.registerChannel=Register;nw.unregisterChannel=Unregister;nw.connectToHost=Connect;nw.sendToHost=SH;nw.sendToClient=SC;nw.getPeerPlayer=Peer;
 th.serviceVersion=1;th.serviceSize=sizeof th;th.runOnGameThread=QueueGame;th.runOnUiThread=QueueUi;
 assert(targetreports::Init(&ctx,Sample));request();assert(samples==0);serve();assert(samples==1&&clients.size()==5);
 receive(clients[4]);receive(clients[4]);assert(!targetreports::Get(7,100,GetTickCount64()).at);
 for(int i=0;i<4;++i)receive(clients[i]);
 auto s=targetreports::Get(7,100,GetTickCount64());assert(s.count==64&&s.targets[63].id==64&&s.heroX==10);
 const auto stamp=s.at;receive(clients[0]);assert(targetreports::Get(7,100,GetTickCount64()).at==stamp);
 assert(!targetreports::Get(8,100,GetTickCount64()).at&&!targetreports::Get(7,101,GetTickCount64()).at);
 assert(!targetreports::Get(7,100,stamp+501).at);
 Sleep(120);request();auto old=clients;clients.clear();targetCount=0;serve();assert(clients.size()==1);
 for(auto&r:old)receive(r);assert(targetreports::Get(7,100,GetTickCount64()).count==64);
 receive(clients[0]);assert(targetreports::Get(7,100,GetTickCount64()).count==0&&targetreports::Get(7,100,GetTickCount64()).at);
 Sleep(120);request();clients.clear();targetCount=2;serve();auto good=clients[0],bad=good;
 uint32_t nan=0x7fc00000;memcpy(bad.b.data()+24,&nan,4);receive(bad);
 bad=good;bad.b[32+12]=2;receive(bad);bad=good;memcpy(bad.b.data()+45,bad.b.data()+32,4);receive(bad);
 assert(targetreports::Get(7,100,GetTickCount64()).count==0);receive(good);assert(targetreports::Get(7,100,GetTickCount64()).count==2);
 Sleep(120);request();peerValid=false;auto before=samples;serve();assert(samples==before);peerValid=true;
 targetreports::Tick(7,100,false);assert(!targetreports::Get(7,100,GetTickCount64()).at);
 Sleep(120);request();auto r=host.back();reg.hostMessage(&ctx,55,9,r.id,r.b.data(),uint32_t(r.b.size()),nullptr);
 targetreports::Tick(8,100,false);before=samples;run();assert(samples==before);
 // A desktop host with local targeting off must still serve its peer.
 targetreports::Tick(7,100,false);Sleep(120);
 reg.hostMessage(&ctx,55,9,r.id,r.b.data(),uint32_t(r.b.size()),nullptr);assert(jobs.size()==1);
 targetreports::Tick(7,100,false);run();assert(jobs.size()==1);run();assert(samples==before+1);
 // Flooding the same peer cannot enqueue another scan within the rate limit.
 auto calls=jobs.size();reg.hostMessage(&ctx,55,9,r.id,r.b.data(),uint32_t(r.b.size()),nullptr);assert(jobs.size()==calls);
 // Lost response times out; its late packets cannot fill the next request.
 request();auto stale=clients.back();Sleep(550);request();receive(stale);assert(!targetreports::Get(7,100,GetTickCount64()).at);
 ConnectionEvent event{};event.structSize=sizeof event;event.channel=55;event.peer=9;event.state=ConnectionState::Disconnected;
 reg.connectionState(&ctx,&event,nullptr);assert(!targetreports::Get(7,100,GetTickCount64()).at);
 targetreports::Tick(7,100,true);assert(jobs.size()==1);auto sent=host.size();targetreports::Stop();run();assert(host.size()==sent&&unregistered==1);
 puts("target reports: real SDK callbacks, 64-target chunking, ordering, malformed packets, freshness, sessions, peer checks and shutdown passed");
}
