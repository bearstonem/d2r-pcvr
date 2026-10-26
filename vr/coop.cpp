// Experimental connection diagnostics. No remote damage, inventory or stat
// operations are implemented here; guest physical action routing is separate.
#include <windows.h>
#include <D2RLPlugin/api.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <cstdio>
#include "coop.h"
#include "pcvr_identity.h"
#include "offline_difficulty_signatures.h"
#include "sigscan.h"
#include "logging.h"
#include "../steamvr/coop_protocol.h"
#include "../steamvr/callback_ticket.h"
namespace gamestate {uint64_t LocalPlayer();}
namespace coop {
namespace {
using namespace D2RL;
using namespace framevr::coop;
const PluginContext* context=nullptr;
const NetworkService* network=nullptr;
const ThreadService* threads=nullptr;
Network::ChannelHandle channel=0;
std::wstring ini;
std::atomic<bool> enabled{false};
std::atomic<uint64_t> rules{0};
constexpr uint64_t build=BuildHash(D2RPCVR_VERSION "-coop-diagnostics-1");
framevr::CallbackTicket uiTicket;
std::mutex uiMutex;
std::mutex mutex;
Receipt receipt;
uint64_t session=0,nextHello=0,nextNonce=0;
struct Pending {uint64_t token=0,at=0;Network::PeerHandle peer=0;Message message;};
std::array<Pending,16> pending{};
std::array<Network::PeerHandle,16> reportedPeers{};
uint64_t nextToken=0;
int lastStatus=-1;
void Status(int status,const char* text) {
    std::lock_guard lock(mutex);
    if(lastStatus==status)return;
    lastStatus=status;framevr::logging::Write(text);
}
void __cdecl Resolve(const PluginContext* ctx,void* token) noexcept {
    Pending request;
    {
        std::lock_guard lock(mutex);
        for(auto& p:pending)if(p.token==reinterpret_cast<uintptr_t>(token)){request=p;p={};break;}
    }
    if(!request.token||!enabled.load()||GetTickCount64()-request.at>3000)return;
    PlayerHandle player=InvalidPlayerHandle;
    if(network->getPeerPlayer(ctx,channel,request.peer,&player)!=Network::Result::Success||!player)return;
    const auto currentRules=rules.load();
    Message reply{Reply(request.message,currentRules,build),request.message.nonce,currentRules,build};
    const auto bytes=Encode(reply);
    network->sendToClient(ctx,channel,request.peer,1,bytes.data(),uint32_t(bytes.size()));
    bool first=false;
    {
        std::lock_guard lock(mutex);
        if(std::find(reportedPeers.begin(),reportedPeers.end(),request.peer)==reportedPeers.end())
            for(auto& p:reportedPeers)if(!p){p=request.peer;first=true;break;}
    }
    if(first)framevr::logging::Write("Frame co-op: authenticated peer handshake received on host game thread; remote physical-action bridge NOT implemented");
}
void __cdecl HostMessage(const PluginContext* ctx,Network::ChannelHandle handle,Network::PeerHandle peer,
                        uint16_t id,const void* data,uint32_t size,void*) noexcept {
    if(!enabled.load()||handle!=channel||!peer||id!=1||!data||size!=PacketSize)return;
    Message message;
    if(!Decode({static_cast<const uint8_t*>(data),size},message)||message.kind!=Kind::Hello)return;
    const auto now=GetTickCount64();uint64_t token=0;
    {
        std::lock_guard lock(mutex);
        // Coalesce each peer and bound callbacks; unload/session changes may
        // discard a callback, so expired reservations are reusable.
        for(auto& p:pending)if(p.token&&(now-p.at>3000))p={};
        for(const auto& p:pending)if(p.token&&p.peer==peer)return;
        for(auto& p:pending)if(!p.token){token=++nextToken;if(!token)token=++nextToken;p={token,now,peer,message};break;}
    }
    if(!token)return;
    if(threads->runOnGameThread(ctx,Resolve,reinterpret_cast<void*>(uintptr_t(token)))!=Threads::Result::Success){
        std::lock_guard lock(mutex);for(auto& p:pending)if(p.token==token)p={};
    }
}
void __cdecl ClientMessage(const PluginContext*,Network::ChannelHandle handle,uint16_t id,
                          const void* data,uint32_t size,void*) noexcept {
    if(!enabled.load()||handle!=channel||id!=1||!data||size!=PacketSize)return;
    Message message;if(!Decode({static_cast<const uint8_t*>(data),size},message))return;
    std::lock_guard lock(mutex);
    receipt.Accept(message,GetTickCount64());
}
void __cdecl Connection(const PluginContext*,const Network::ConnectionEvent* event,void*) noexcept {
    if(!enabled.load()||!Network::HasConnectionEventField(event,Network::ConnectionEventRequiredSize)||event->channel!=channel)return;
    if(event->state==Network::ConnectionState::Disconnected||event->state==Network::ConnectionState::Rejected){
        std::lock_guard lock(mutex);receipt.at=0;receipt.result=Kind::Hello;
        // Other connected host peers keep their own pending requests.
        for(auto& p:pending)if(p.peer==event->peer)p={};
        for(auto& p:reportedPeers)if(p==event->peer)p=0;
    }
}
uint64_t Rules() {
    // Only shared combat policy is compared; hand bindings, calibration,
    // rendering, aim assistance and gesture thresholds remain personal.
    uint64_t value=GetPrivateProfileIntW(L"gameplay",L"melee_rules",0,ini.c_str())==1?1:0;
    value|=uint64_t(GetPrivateProfileIntW(L"gameplay",L"contact_hits",1,ini.c_str())!=0)<<1;
    value|=uint64_t(GetPrivateProfileIntW(L"gameplay",L"enemies_always_hit",1,ini.c_str())!=0)<<2;
    return value;
}
void __cdecl Refresh(const PluginContext* ctx,void* token) noexcept {
    {std::lock_guard lock(uiMutex);if(!uiTicket.Claim(reinterpret_cast<uintptr_t>(token))||!enabled.load())return;}
    rules=Rules();
    using namespace offlinedifficulty::abi;
    if(!d2rsig::Check(ModeRva,ModeSig,sizeof ModeSig))return;
    const int mode=reinterpret_cast<int(*)()>(d2rsig::Addr(ModeRva))();
    const auto player=gamestate::LocalPlayer(),now=GetTickCount64();
    if(!player||(mode!=5&&mode!=7)){
        {std::lock_guard lock(mutex);session=0;receipt.Reset(0);nextHello=0;}
        Status(0,"Frame co-op: no TCP/IP session; single-player behavior unchanged");return;
    }
    uint64_t nonce=0;
    {
        std::lock_guard lock(mutex);
        if(session!=player){session=player;if(!++nextNonce)++nextNonce;receipt.Reset(nextNonce);nextHello=0;}
        nonce=receipt.nonce;
    }
    Network::ChannelInfo info{};info.structSize=Network::ChannelInfoSize;
    if(network->getChannelInfo(ctx,channel,&info)!=Network::Result::Success)return;
    if(info.connectionState!=Network::ConnectionState::Connected){
        const auto result=network->connectToHost(ctx,channel);
        if(result==Network::Result::WrongRole)Status(1,"Frame co-op: TCP/IP host transport active; remote physical-action bridge NOT implemented");
        else if(result==Network::Result::Incompatible)Status(2,"Frame co-op: incompatible loader transport; both PCs need the reviewed D2RLoader build");
        else Status(3,"Frame co-op: waiting for host transport; both PCs must use the same Unleashed candidate and mod name");
        return;
    }
    Receipt latest;
    {std::lock_guard lock(mutex);latest=receipt;}
    if(latest.result==Kind::RulesMismatch)Status(4,"Frame co-op: combat rules differ; match Melee rules, contact_hits and enemies_always_hit on both PCs");
    else if(latest.result==Kind::BuildMismatch)Status(5,"Frame co-op: Unleashed candidate versions differ; install the same test build on both PCs");
    else if(latest.Ready(now))Status(6,"Frame co-op: host handshake and combat policy match; remote physical-action bridge NOT implemented");
    else Status(7,"Frame co-op: connected to host; waiting for authenticated handshake reply");
    if(now>=nextHello){
        const auto bytes=Encode({Kind::Hello,nonce,rules.load(),build});
        network->sendToHost(ctx,channel,1,bytes.data(),uint32_t(bytes.size()));nextHello=now+2000;
    }
}
}
void Init(const D2RL::PluginContext* ctx,const wchar_t* path) {
    context=ctx;ini=path?path:L"";
    if(ctx->QueryService(&network)!=ServiceQueryResult::Success||
       !HasNetworkServiceField(network,NetworkServiceRequiredSize)||
       !network->registerChannel||!network->getChannelInfo||!network->connectToHost||
       !network->sendToHost||!network->sendToClient||!network->getPeerPlayer||
       ctx->QueryService(&threads)!=ServiceQueryResult::Success||
       !HasThreadServiceField(threads,ThreadServiceRequiredSize)||!threads->runOnGameThread||!threads->runOnUiThread){
        framevr::logging::Write("Frame co-op: loader network service unavailable; single-player remains available");return;
    }
    Network::ChannelRegistration registration{};registration.structSize=Network::ChannelRegistrationSize;
    registration.localChannelId=1;registration.compatibilityToken=ChannelToken;
    registration.hostMessage=HostMessage;registration.clientMessage=ClientMessage;registration.connectionState=Connection;
    if(network->registerChannel(ctx,&registration,&channel)!=Network::Result::Success){
        framevr::logging::Write("Frame co-op: diagnostic channel registration unavailable; single-player remains available");return;
    }
    rules=Rules();enabled=true;
    framevr::logging::Write("Frame co-op: experimental transport diagnostics ready; no remote action dispatch enabled");
}
void Tick() {
    if(!enabled.load())return;
    const auto now=GetTickCount64();
    uintptr_t token=0;
    {std::lock_guard lock(uiMutex);if(uiTicket.Expired(now,1500))uiTicket.Cancel();token=uiTicket.Start(now);}
    if(token&&threads->runOnUiThread(context,Refresh,reinterpret_cast<void*>(token))!=Threads::Result::Success){
        std::lock_guard lock(uiMutex);uiTicket.Claim(token);
    }
}
void Shutdown(){enabled=false;std::lock_guard lock(uiMutex);uiTicket.Cancel();}
}
