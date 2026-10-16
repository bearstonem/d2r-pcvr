// Adopt native basic-crossbow launches into the existing physical arrow path.
#include <windows.h>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdio>
#include "crossbow.h"
#include "skeletons.h"
#include "combat.h"
#include "memory.h"
#include "logging.h"
#include "../shared/d2r_vr_state.h"
namespace gamestate {uint64_t LocalPlayer();uint32_t HandsKey();uint32_t WeaponSet();uint32_t WeaponType();bool MenuOpen();}
namespace crossbowvr {
namespace {
using namespace framevr::crossbow;
std::mutex mutex;
struct GateState {bool active=false;uint64_t session=0,at=0,epoch=0;uint32_t equipment=0;} gate;
struct Pending {uint32_t owner=0;int tx=0,ty=0;Launch shot{};bool adopted=false;};
std::array<Pending,16> pending{};size_t nextPending=0;
std::atomic<uint64_t> sequence{0};
std::atomic<unsigned> prepared[2]{},adopted{0},refused{0};
thread_local std::array<const Launch*,8> creating{};
thread_local unsigned depth=0;
template<class T> bool Read(uintptr_t p,T& v){return p>0x10000&&framevr::ReadMemory(&v,reinterpret_cast<void*>(p),sizeof v);}
void Put(std::array<uint8_t,0x98>& p,size_t at,int value){std::memcpy(p.data()+at,&value,sizeof value);}
}
void Gate(bool active,uint64_t session,uint32_t equipment,uint64_t epoch) {
    std::lock_guard lock(mutex);
    if(gate.session!=session)pending={};
    gate={active,session,GetTickCount64(),epoch,equipment};
}
bool MakeShot(const Pose& pose,uint64_t epoch,Launch& shot) {
    return Aim(pose,shot)&&Physical(shot,epoch,++sequence);
}
bool Prepare(uintptr_t params,std::array<uint8_t,0x98>& adjusted,Launch& shot,bool server) {
    uint32_t missile=0,ownerHeader[3]{},id=0;uintptr_t owner=0;int skill=-1;
    if((server&&(gamestate::WeaponType()!=D2RVR_TYPE_CROSSBOW||gamestate::MenuOpen()))||
       !Read(params+0x20,missile)||missile!=31||!Read(params+0x3c,skill)||skill!=0||
       !Read(params+8,owner)||!Read(owner,ownerHeader)||ownerHeader[0]!=0||
       !combat::LocalPlayerId(id)||ownerHeader[2]!=id)return false;
    const auto now=GetTickCount64();GateState g;Pose pose;bool paired=false;
    int target[2]{};if(!Read(params+0x2c,target)||!Read(params,adjusted))return false;
    {
        std::lock_guard lock(mutex);g=gate;
        if(!server)for(auto& p:pending)if(p.adopted&&p.owner==id&&
           ((p.tx==target[0]&&p.ty==target[1])||(p.shot.tx==target[0]&&p.shot.ty==target[1]))&&
           p.shot.pose.session==g.session&&now>=p.shot.pose.at&&now-p.shot.pose.at<=500) {
            shot=p.shot;p={};paired=true;break;
        }
    }
    if(g.session!=gamestate::LocalPlayer())return false;
    if(server) {
        if(!g.active||now<g.at||now-g.at>100||g.equipment!=(gamestate::HandsKey()^(gamestate::WeaponSet()<<30)))return false;
        if(!skel::CrossbowPose(pose)||!Fresh(pose,now,g.session,g.equipment,uint32_t(g.epoch))||
           !MakeShot(pose,g.epoch,shot)){++refused;return false;}
        std::lock_guard lock(mutex);pending[nextPending++%pending.size()]={id,target[0],target[1],shot,false};
    } else if(!paired)return false;
    uint32_t flags=0;std::memcpy(&flags,adjusted.data(),sizeof flags);flags|=1;
    std::memcpy(adjusted.data(),&flags,sizeof flags);
    Put(adjusted,0x24,shot.x);Put(adjusted,0x28,shot.y);Put(adjusted,0x2c,shot.tx);Put(adjusted,0x30,shot.ty);
    ++prepared[server?1:0];return true;
}
void ServerDone(const Launch& shot,bool accepted) {
    std::lock_guard lock(mutex);
    for(auto& p:pending)if(p.shot.flight.session==shot.flight.session&&p.shot.flight.sequence==shot.flight.sequence) {
        if(accepted){p.adopted=true;++adopted;}else {p={};++refused;}break;
    }
}
void BeginClient(const Launch* shot){if(depth<creating.size())creating[depth]=shot;++depth;}
void EndClient(){if(depth)--depth;}
const framevr::bowflight::Shot* Creating() {
    return depth&&depth<=creating.size()&&creating[depth-1]?&creating[depth-1]->flight:nullptr;
}
void Report() {
    const auto c=prepared[0].exchange(0),s=prepared[1].exchange(0),a=adopted.exchange(0),r=refused.exchange(0);
    if(!c&&!s&&!a&&!r)return;
    char message[180];std::snprintf(message,sizeof message,"Frame crossbow: physical launches client/server %u/%u, adopted %u, missing pose/refused %u",c,s,a,r);
    framevr::logging::Write(message);
}
}
