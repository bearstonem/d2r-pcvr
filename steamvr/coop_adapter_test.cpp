// The real loader adapter with synthetic services; no running game or sockets.
#include "../vr/coop.cpp"
#include <cassert>
#include <deque>
#include <vector>
using namespace D2RL;
using namespace framevr::coop;
namespace {
struct Work {Threads::Callback call;void* token;};
std::deque<Work> uiWork,gameWork;
Network::ChannelRegistration registration{};
Network::ConnectionState connection=Network::ConnectionState::Disconnected;
Network::Result connectResult=Network::Result::Busy;
Threads::Result gameResult=Threads::Result::Success;
bool serviceAvailable=true,onGame=false,peerValid=true;
int mode=0,connects=0,resolved=0;
uint64_t localPlayer=1;
std::vector<Message> sentHost,sentGuest;
Network::Result Register(const PluginContext*,const Network::ChannelRegistration* r,Network::ChannelHandle* h)noexcept {
    registration=*r;*h=9;return Network::Result::Success;
}
Network::Result Info(const PluginContext*,Network::ChannelHandle,Network::ChannelInfo* i)noexcept {
    i->connectionState=connection;return Network::Result::Success;
}
Network::Result Connect(const PluginContext*,Network::ChannelHandle)noexcept {++connects;return connectResult;}
Network::Result SendHost(const PluginContext*,Network::ChannelHandle,uint16_t id,const void* data,uint32_t size)noexcept {
    Message m;assert(id==1&&Decode({static_cast<const uint8_t*>(data),size},m));sentHost.push_back(m);return Network::Result::Success;
}
Network::Result SendGuest(const PluginContext*,Network::ChannelHandle,Network::PeerHandle,uint16_t id,const void* data,uint32_t size)noexcept {
    assert(onGame);Message m;assert(id==1&&Decode({static_cast<const uint8_t*>(data),size},m));sentGuest.push_back(m);return Network::Result::Success;
}
Network::Result Peer(const PluginContext*,Network::ChannelHandle,Network::PeerHandle,PlayerHandle* p)noexcept {
    assert(onGame);++resolved;*p=peerValid?900:0;return peerValid?Network::Result::Success:Network::Result::StaleHandle;
}
Threads::Result Ui(const PluginContext*,Threads::Callback cb,void* p)noexcept {uiWork.push_back({cb,p});return Threads::Result::Success;}
Threads::Result Game(const PluginContext*,Threads::Callback cb,void* p)noexcept {
    if(gameResult==Threads::Result::Success)gameWork.push_back({cb,p});return gameResult;
}
NetworkService networkService{sizeof(NetworkService),1,Register,nullptr,Info,Connect,SendHost,SendGuest,Peer};
ThreadService threadService{sizeof(ThreadService),1,Ui,Game};
ServiceQueryResult Query(const PluginContext*,ServiceId id,uint32_t,const void** out)noexcept {
    if(!serviceAvailable)return ServiceQueryResult::Unavailable;
    *out=id==ServiceId::Network?static_cast<const void*>(&networkService):static_cast<const void*>(&threadService);
    return ServiceQueryResult::Success;
}
PluginApi api{};PluginContext ctx{};
int Mode(){return mode;}
void Step(){coop::Tick();assert(uiWork.size()==1);const auto w=uiWork.front();uiWork.pop_front();w.call(&ctx,w.token);}
void DrainGame(){onGame=true;while(!gameWork.empty()){auto w=gameWork.front();gameWork.pop_front();w.call(&ctx,w.token);}onGame=false;}
void FromHost(const Message& m){auto b=Encode(m);registration.clientMessage(&ctx,9,1,b.data(),uint32_t(b.size()),nullptr);}
void FromGuest(Network::PeerHandle p,const Message& m){auto b=Encode(m);registration.hostMessage(&ctx,9,p,1,b.data(),uint32_t(b.size()),nullptr);}
}
namespace d2rsig {
bool Check(uint64_t rva,const void*,uint32_t){return rva==offlinedifficulty::abi::ModeRva;}
uintptr_t Addr(uint64_t rva){assert(rva==offlinedifficulty::abi::ModeRva);return reinterpret_cast<uintptr_t>(&Mode);}
}
namespace gamestate {uint64_t LocalPlayer(){return localPlayer;}}
namespace framevr::logging {void Write(const char*){}}
int main() {
    api.apiSize=sizeof api;api.queryService=Query;ctx.contextSize=sizeof ctx;ctx.api=&api;
    serviceAvailable=false;coop::Init(&ctx,L"");coop::Tick();assert(uiWork.empty());
    serviceAvailable=true;networkService.serviceSize=8;coop::Init(&ctx,L"");coop::Tick();assert(uiWork.empty());
    networkService.serviceSize=sizeof networkService;coop::Init(&ctx,L"");
    assert(registration.compatibilityToken==ChannelToken&&registration.localChannelId==1);
    Step();assert(connects==0&&sentHost.empty()); // solo must not contact a host
    for(int excluded:{1,2,3,4,6,8}){mode=excluded;Step();assert(connects==0);}
    mode=5;connectResult=Network::Result::WrongRole;Step();assert(connects==1);
    mode=7;connectResult=Network::Result::Busy;Step();assert(connects==2);
    connection=Network::ConnectionState::Connected;Step();assert(sentHost.size()==1);
    auto hello=sentHost.back();assert(hello.kind==Kind::Hello&&hello.nonce&&hello.rules==6);
    FromHost({Kind::Ready,hello.nonce+1,hello.rules,hello.build});assert(!coop::receipt.at);
    FromHost({Kind::Ready,hello.nonce,hello.rules,hello.build});assert(coop::receipt.Ready(GetTickCount64()));
    mode=0;Step();assert(!coop::receipt.nonce);
    FromHost({Kind::Ready,hello.nonce,hello.rules,hello.build});assert(!coop::receipt.at);
    mode=7;Step();assert(sentHost.size()==2&&sentHost.back().nonce!=hello.nonce);
    FromHost({Kind::Ready,hello.nonce,hello.rules,hello.build});assert(!coop::receipt.at);
    const auto current=sentHost.back();
    FromGuest(2,current);FromGuest(2,current);assert(gameWork.size()==1&&resolved==0);
    DrainGame();assert(resolved==1&&sentGuest.back().kind==Kind::Ready);
    auto mismatch=current;mismatch.rules^=1;FromGuest(2,mismatch);DrainGame();assert(sentGuest.back().kind==Kind::RulesMismatch);
    mismatch=current;mismatch.build^=1;FromGuest(2,mismatch);DrainGame();assert(sentGuest.back().kind==Kind::BuildMismatch);
    peerValid=false;const auto before=sentGuest.size();FromGuest(2,current);DrainGame();assert(sentGuest.size()==before);
    peerValid=true;gameResult=Threads::Result::Unavailable;FromGuest(2,current);assert(gameWork.empty());
    gameResult=Threads::Result::Success;FromGuest(2,current);assert(gameWork.size()==1);DrainGame();
    for(int i=1;i<=40;++i)FromGuest(Network::PeerHandle(i),current);
    assert(gameWork.size()==16);DrainGame();
    auto invalid=Encode(current);invalid[0]^=1;
    registration.hostMessage(&ctx,9,2,1,invalid.data(),uint32_t(invalid.size()),nullptr);assert(gameWork.empty());
    FromGuest(2,current);assert(gameWork.size()==1);coop::Shutdown();DrainGame();
    coop::Tick();assert(uiWork.empty());
}
