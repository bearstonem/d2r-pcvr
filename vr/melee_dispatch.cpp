#include "melee_dispatch.h"
#include <D2RLPlugin/context.h>
#include <D2RLPlugin/network.h>
#include <D2RLPlugin/threads.h>
#include <windows.h>
#include <array>
#include <mutex>

namespace meleedispatch { namespace {
namespace nw=D2RL::Network;
namespace wire=framevr::melee::dispatch;
using wire::Contact;
constexpr uint16_t Hello=1,Ack=2,Data=3,ContactFelt=4; // additive feedback; attack wire remains unchanged
constexpr uint64_t Fresh=250,Lease=1500;
struct Peer {nw::PeerHandle id=0;uint64_t sender=0,sequence=0;uint32_t hero=0;};
enum class Kind {HelloSend,HelloReceive,AckSend,ContactSend,ContactReceive};
struct Job {
    uint64_t ticket=0,session=0,at=0,nonce=0,sender=0;
    nw::PeerHandle peer=0;Kind kind=Kind::HelloSend;Contact contact{};
};
std::mutex mutex;
const D2RL::PluginContext* ctx=nullptr;
const D2RL::NetworkService* net=nullptr;
const D2RL::ThreadService* threads=nullptr;
ApplyFn apply=nullptr;nw::ChannelHandle channel=0;
bool dead=true;uint64_t session=0,serial=0,helloNonce=0,helloAt=0,nextHello=0,readyUntil=0;
std::array<Peer,8> peers{};std::array<Job,64> jobs{};
// Separate bounded feedback storage cannot consume attack-dispatch job slots.
constexpr uint64_t FeedbackFresh=500;
struct FeedbackJob {uint64_t ticket=0,hostSession=0,at=0;nw::PeerHandle peer=0;Feedback value{};};
struct TimedFeedback {Feedback value{};uint64_t at=0;};
std::array<FeedbackJob,64> feedbackJobs{};
std::array<TimedFeedback,256> sentSweeps{};
std::array<TimedFeedback,64> receivedFeedback{};
framevr::melee::HitSet felt[3];size_t sentNext=0;
void ClearFeedback(){feedbackJobs={};sentSweeps={};receivedFeedback={};for(auto& f:felt)f={};sentNext=0;}
Feedback From(const Contact& c,uint32_t target){
    Feedback f;f.session=c.session;f.sequence=c.sequence;f.stroke=c.sweep.stroke;f.epoch=c.sample.epoch;
    f.hero=c.hero;f.target=target;f.equipment=c.sample.equipment;f.hand=c.sample.handIndex;f.shield=c.sample.shield;return f;
}
TimedFeedback* Sent(const Feedback& f,uint64_t now){
    for(auto& p:sentSweeps)if(p.at&&now>=p.at&&now-p.at<=FeedbackFresh&&
       p.value.session==f.session&&p.value.sequence==f.sequence&&p.value.stroke==f.stroke&&
       p.value.hero==f.hero&&p.value.hand==f.hand&&p.value.shield==f.shield)return &p;
    return nullptr;
}
bool MarkFelt(const Feedback& f){return felt[f.shield?2:f.hand].Insert(f.session,f.stroke,f.target);}
uint64_t Next(){if(!++serial)++serial;return serial;}
Peer* FindPeer(nw::PeerHandle id){for(auto& p:peers)if(id&&p.id==id)return &p;return nullptr;}
Job* FindJob(uint64_t id){for(auto& j:jobs)if(id&&j.ticket==id)return &j;return nullptr;}
uint64_t Queue(Job j){
    const auto now=GetTickCount64();
    for(auto& slot:jobs)if(!slot.ticket||now-slot.at>Fresh){j.ticket=Next();j.session=session;j.at=now;slot=j;return j.ticket;}
    return 0;
}
void Forget(uint64_t ticket){std::lock_guard lock(mutex);if(auto* j=FindJob(ticket))j->ticket=0;}
bool Take(uint64_t ticket,Job& out){
    std::lock_guard lock(mutex);auto* j=FindJob(ticket);
    if(dead||!j)return false;out=*j;j->ticket=0;
    return out.session==session&&session&&GetTickCount64()-out.at<=Fresh;
}
void Put64(uint8_t* b,uint64_t x){for(int k=0;k<8;++k)b[k]=uint8_t(x>>(8*k));}
uint64_t Get64(const uint8_t* b){uint64_t x=0;for(int k=0;k<8;++k)x|=uint64_t(b[k])<<(8*k);return x;}
void __cdecl SendFeedback(const D2RL::PluginContext*,void* data)noexcept {
    FeedbackJob copy;const auto ticket=uint64_t(uintptr_t(data));
    {std::lock_guard lock(mutex);if(dead||!session)return;
     auto* found=static_cast<FeedbackJob*>(nullptr);
     for(auto& j:feedbackJobs)if(j.ticket==ticket&&ticket){found=&j;break;}
     if(!found)return;copy=*found;*found={};
     const auto* peer=FindPeer(copy.peer);const auto now=GetTickCount64();
     if(copy.hostSession!=session||now<copy.at||now-copy.at>Fresh||!peer||
        peer->sender!=copy.value.session||peer->hero!=copy.value.hero)return;}
    const auto& f=copy.value;uint8_t bytes[48]{};
    Put64(bytes,f.session);Put64(bytes+8,f.sequence);Put64(bytes+16,f.stroke);
    Put64(bytes+24,f.hero);Put64(bytes+32,f.target);Put64(bytes+40,uint64_t(f.hand)|(uint64_t(f.shield)<<1));
    net->sendToClient(ctx,channel,copy.peer,ContactFelt,bytes,sizeof bytes);
}
void __cdecl SendJob(const D2RL::PluginContext*,void* data)noexcept {
    Job j;if(!Take(uint64_t(uintptr_t(data)),j))return;
    if(j.kind==Kind::ContactSend){
        {std::lock_guard lock(mutex);if(dead||session!=j.session||GetTickCount64()>readyUntil)return;}
        wire::Bytes bytes;if(wire::Encode(j.contact,bytes))net->sendToHost(ctx,channel,Data,bytes.data(),uint32_t(bytes.size()));
        return;
    }
    uint8_t bytes[16];Put64(bytes,j.sender);Put64(bytes+8,j.nonce);
    if(j.kind==Kind::HelloSend){net->connectToHost(ctx,channel);net->sendToHost(ctx,channel,Hello,bytes,sizeof bytes);}
    else if(j.kind==Kind::AckSend)net->sendToClient(ctx,channel,j.peer,Ack,bytes,sizeof bytes);
}
bool Post(uint64_t ticket,bool game);
void __cdecl ApplyJob(const D2RL::PluginContext*,void* data)noexcept {
    Job j;if(!Take(uint64_t(uintptr_t(data)),j))return;
    D2RL::PlayerHandle handle=0;
    if(net->getPeerPlayer(ctx,channel,j.peer,&handle)!=nw::Result::Success||!handle)return;
    uint64_t ack=0;bool execute=false;
    {
        std::lock_guard lock(mutex);if(dead||session!=j.session)return;
        auto* p=FindPeer(j.peer);
        if(j.kind==Kind::HelloReceive){
            if(!p)for(auto& slot:peers)if(!slot.id){p=&slot;*p={j.peer,j.sender,0,0};break;}
            if(!p||p->sender!=j.sender)return;
            j.kind=Kind::AckSend;ack=Queue(j);
        }else if(j.kind==Kind::ContactReceive){
            // Cooperative association as in the reference mod: the claimed hero
            // is pinned to this live peer. This is NOT authenticated ownership.
            if(!p||p->sender!=j.contact.session||j.contact.sequence<=p->sequence||
               (p->hero&&p->hero!=j.contact.hero))return;
            for(const auto& other:peers)if(other.id!=p->id&&other.hero==j.contact.hero)return;
            p->hero=j.contact.hero;p->sequence=j.contact.sequence;execute=true;
        }
    }
    if(ack)Post(ack,false);
    if(execute)apply(j.peer,j.contact);
}
bool Post(uint64_t ticket,bool game){
    if(!ticket)return false;const auto fn=game?threads->runOnGameThread:threads->runOnUiThread;
    if(fn(ctx,game?ApplyJob:SendJob,reinterpret_cast<void*>(uintptr_t(ticket)))==D2RL::Threads::Result::Success)return true;
    Forget(ticket);return false;
}
void __cdecl Host(const D2RL::PluginContext*,nw::ChannelHandle ch,nw::PeerHandle peer,uint16_t id,const void* data,uint32_t size,void*)noexcept {
    if(!peer||!data)return;Job j;j.peer=peer;
    if(id==Hello){
        if(size!=16)return;const auto* b=static_cast<const uint8_t*>(data);
        j.sender=Get64(b);j.nonce=Get64(b+8);if(!j.sender||!j.nonce)return;j.kind=Kind::HelloReceive;
    }else if(id==Data){if(!wire::Decode(data,size,j.contact))return;j.kind=Kind::ContactReceive;}
    else return;
    uint64_t ticket=0;
    {std::lock_guard lock(mutex);if(dead||ch!=channel||!session)return;ticket=Queue(j);}
    Post(ticket,true);
}
void __cdecl Client(const D2RL::PluginContext*,nw::ChannelHandle ch,uint16_t id,const void* data,uint32_t size,void*)noexcept {
    if(id==ContactFelt){
        if(!data||size!=48)return;const auto* b=static_cast<const uint8_t*>(data);
        const auto hero=Get64(b+24),target=Get64(b+32),flags=Get64(b+40);
        if(!hero||hero>UINT32_MAX||!target||target>UINT32_MAX||flags>3)return;
        Feedback f;f.session=Get64(b);f.sequence=Get64(b+8);f.stroke=Get64(b+16);
        f.hero=uint32_t(hero);f.target=uint32_t(target);f.hand=int(flags&1);f.shield=bool(flags&2);
        std::lock_guard lock(mutex);const auto now=GetTickCount64();
        if(dead||ch!=channel||!session||f.session!=session)return;
        const auto* pending=Sent(f,now);if(!pending)return;
        f.epoch=pending->value.epoch;f.equipment=pending->value.equipment;
        for(auto& slot:receivedFeedback)if(!slot.at||now-slot.at>150){
            if(MarkFelt(f))slot={f,now};return;
        }
        return;
    }
    if(id!=Ack||!data||size!=16)return;const auto* b=static_cast<const uint8_t*>(data);
    std::lock_guard lock(mutex);const auto now=GetTickCount64();
    if(dead||ch!=channel||!session||!helloNonce||Get64(b)!=session||Get64(b+8)!=helloNonce||now-helloAt>Lease)return;
    readyUntil=now+Lease;helloNonce=0; // A duplicate acknowledgement cannot renew the lease.
}
void __cdecl Connection(const D2RL::PluginContext*,const nw::ConnectionEvent* e,void*)noexcept {
    if(!e||e->structSize<nw::ConnectionEventRequiredSize)return;
    if(e->state!=nw::ConnectionState::Disconnected&&e->state!=nw::ConnectionState::Rejected)return;
    std::lock_guard lock(mutex);if(dead||e->channel!=channel)return;
    for(auto& p:peers)if(p.id==e->peer)p={};
    for(auto& j:jobs)if(j.peer==e->peer||j.kind==Kind::ContactSend||j.kind==Kind::HelloSend)j={};
    helloNonce=readyUntil=nextHello=0;ClearFeedback();
}
}
bool Init(const D2RL::PluginContext* c,ApplyFn fn){
    if(!c||!fn)return false;const D2RL::NetworkService* n=nullptr;const D2RL::ThreadService* t=nullptr;
    if(c->QueryService(&n)!=D2RL::ServiceQueryResult::Success||!D2RL::HasNetworkServiceField(n,D2RL::NetworkServiceRequiredSize)||
       !n->registerChannel||!n->unregisterChannel||!n->connectToHost||!n->sendToHost||!n->sendToClient||!n->getPeerPlayer||
       c->QueryService(&t)!=D2RL::ServiceQueryResult::Success||!D2RL::HasThreadServiceField(t,D2RL::ThreadServiceRequiredSize)||
       !t->runOnGameThread||!t->runOnUiThread)return false;
    {std::lock_guard lock(mutex);if(!dead)return false;ctx=c;net=n;threads=t;apply=fn;session=helloNonce=helloAt=nextHello=readyUntil=0;peers={};jobs={};ClearFeedback();}
    nw::ChannelRegistration registration{};registration.structSize=sizeof registration;
    registration.localChannelId=0x4d44;registration.compatibilityToken=0x504356524d440001ull;
    registration.hostMessage=Host;registration.clientMessage=Client;registration.connectionState=Connection;
    nw::ChannelHandle h=0;const auto result=n->registerChannel(c,&registration,&h);
    std::lock_guard lock(mutex);channel=h;dead=result!=nw::Result::Success||!h;return !dead;
}
void Tick(uint64_t s){
    uint64_t ticket=0;
    {std::lock_guard lock(mutex);if(dead)return;
        if(s!=session){session=s;peers={};jobs={};ClearFeedback();helloNonce=helloAt=nextHello=readyUntil=0;}
        const auto now=GetTickCount64();if(!s||now<nextHello)return;
        nextHello=now+500;helloNonce=Next();helloAt=now;
        Job j;j.kind=Kind::HelloSend;j.sender=s;j.nonce=helloNonce;ticket=Queue(j);
    }
    Post(ticket,false);
}
bool Ready(uint64_t s){std::lock_guard lock(mutex);return !dead&&s&&s==session&&readyUntil&&GetTickCount64()<=readyUntil;}
bool Send(const Contact& c){
    if(!wire::Valid(c))return false;uint64_t ticket=0;
    {std::lock_guard lock(mutex);if(dead||!session||c.session!=session||!readyUntil||GetTickCount64()>readyUntil)return false;
        Job j;j.kind=Kind::ContactSend;j.contact=c;ticket=Queue(j);
        if(ticket)sentSweeps[sentNext++%sentSweeps.size()]={From(c,0),GetTickCount64()};
    }
    if(Post(ticket,false))return true;
    {std::lock_guard lock(mutex);for(auto& p:sentSweeps)if(p.value.session==c.session&&p.value.sequence==c.sequence)p={};}
    return false;
}
void NotifyContact(const Contact& c,uint32_t target){
    if(!target||!wire::Valid(c))return;uint64_t ticket=0;
    {std::lock_guard lock(mutex);if(dead||!session)return;
     const Peer* peer=nullptr;for(const auto& p:peers)if(p.id&&p.sender==c.session&&p.hero==c.hero){peer=&p;break;}
     if(!peer)return;const auto now=GetTickCount64();
     for(auto& slot:feedbackJobs)if(!slot.ticket||now-slot.at>Fresh){
         ticket=Next();slot={ticket,session,now,peer->id,From(c,target)};break;
     }}
    if(ticket&&threads->runOnUiThread(ctx,SendFeedback,reinterpret_cast<void*>(uintptr_t(ticket)))!=D2RL::Threads::Result::Success){
        std::lock_guard lock(mutex);for(auto& j:feedbackJobs)if(j.ticket==ticket)j={};
    }
}
bool Predicted(const Contact& c,uint32_t target){
    if(!target||!wire::Valid(c))return false;const auto f=From(c,target);
    std::lock_guard lock(mutex);
    return !dead&&session==c.session&&Sent(f,GetTickCount64())&&MarkFelt(f);
}
bool TakeFeedback(uint64_t s,Feedback& out){
    std::lock_guard lock(mutex);if(dead||!s||s!=session)return false;const auto now=GetTickCount64();
    for(auto& slot:receivedFeedback)if(slot.at){const auto copy=slot;slot={};
        if(now>=copy.at&&now-copy.at<=150){out=copy.value;return true;}}
    return false;
}
bool Alive(uint64_t peer,uint64_t sender){
    const D2RL::NetworkService* n=nullptr;const D2RL::PluginContext* c=nullptr;nw::ChannelHandle ch=0;
    {std::lock_guard lock(mutex);
     if(dead||!net||!net->getPeerPlayer)return false;
     bool bound=false;for(const auto& p:peers)if(p.id==peer&&p.sender==sender)bound=true;
     if(!bound)return false;n=net;c=ctx;ch=channel;}
    D2RL::PlayerHandle player{};
    return n->getPeerPlayer(c,ch,peer,&player)==nw::Result::Success&&player;
}
void Stop(){
    const D2RL::NetworkService* n;const D2RL::PluginContext* c;nw::ChannelHandle h;
    {std::lock_guard lock(mutex);dead=true;session=helloNonce=readyUntil=0;peers={};jobs={};ClearFeedback();n=net;c=ctx;h=channel;channel=0;}
    if(n&&c&&h)n->unregisterChannel(c,h);
}
}
