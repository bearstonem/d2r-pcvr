#include "portal_placement.h"
#include "portal_placement_signatures.h"
#include "../steamvr/portal_placement.h"
#include "bow_signatures.h"
#include "offline_difficulty.h"
#include "combat.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include <windows.h>
#include <mutex>
#include <cstdio>
namespace gamestate {uint64_t LocalPlayer();}
namespace portalplacement {
namespace {
using Point=framevr::portalplacement::Point;
using Create=int(*)(uintptr_t,uintptr_t,uintptr_t,int,int,int,uintptr_t*,int,int,int);
using Free=uintptr_t(*)(uintptr_t,Point*,Point*,int,int,int,int);
using RoomPoint=uintptr_t(*)(uintptr_t,int,int);
using Mask=uint16_t(*)(uintptr_t,int,int,int,int);
using Level=int(*)(uintptr_t);
Create original=nullptr;
std::mutex mutex;
struct Facing {uint64_t session=0,at=0;float x=0,y=0;} facing;
template<class T>bool Read(uintptr_t p,T& v){return p>=0x10000&&framevr::ReadMemory(&v,reinterpret_cast<void*>(p),sizeof v);}
int OnCreate(uintptr_t game,uintptr_t player,uintptr_t room,int x,int y,int destination,uintptr_t* output,int kind,int skipSearch,int bypassTown) {
    Facing f;{std::lock_guard lock(mutex);f=facing;}
    uint32_t local=0,header[3]{};uintptr_t path=0;uint32_t xy[2]{};
    const auto now=GetTickCount64();
    const bool town=destination==1||destination==40||destination==75||destination==103||destination==109;
    if(kind==59&&output&&!skipSearch&&!bypassTown&&town&&offlinedifficulty::Offline()&&
       f.session&&f.session==gamestate::LocalPlayer()&&now>=f.at&&now-f.at<=250&&
       combat::LocalPlayerId(local)&&Read(player,header)&&header[0]==0&&header[2]==local&&
       Read(player+0x38,path)&&Read(path,xy)){
        const float px=xy[0]/65536.f,py=xy[1]/65536.f;
        const auto free=reinterpret_cast<Free>(d2rsig::Addr(abi::FreeRva));
        const auto roomAt=reinterpret_cast<RoomPoint>(d2rsig::Addr(bowvr::native::RoomPointRva));
        const auto mask=reinterpret_cast<Mask>(d2rsig::Addr(bowvr::native::MaskRva));
        const auto level=reinterpret_cast<Level>(d2rsig::Addr(abi::LevelRva));
        const int area=level(room);
        Point chosen;uintptr_t chosenRoom=0;
        const bool placed=framevr::portalplacement::Choose(px,py,f.x,f.y,
            [&](Point seed,Point& result)->uintptr_t {
                const auto found=free(room,&seed,&result,3,0x3e01,0xc01,0);
                return found&&level(found)==area?found:0;
            },[&](Point end){
                // Avoid choosing open ground beyond a wall. Keep the native
                // endpoint footprint check and test the walking corridor too.
                const float distance=std::hypot(end.x-px,end.y-py);
                const int steps=int(std::ceil(distance*2));
                for(int i=1;i<=steps;++i){
                    const int cx=int(std::lround(px+(end.x-px)*i/steps));
                    const int cy=int(std::lround(py+(end.y-py)*i/steps));
                    const auto r=roomAt(room,cx,cy);
                    if(!r||level(r)!=area||mask(r,cx,cy,1,0x1))return false;
                }
                return true;
            },chosen,chosenRoom);
        if(placed){x=chosen.x;y=chosen.y;room=chosenRoom;skipSearch=1;}
        char text[180];std::snprintf(text,sizeof text,"Frame town portal: %s at %d,%d; player %.2f,%.2f view %.2f,%.2f",
            placed?"forward position":"no reachable forward space; native placement",x,y,px,py,f.x,f.y);
        framevr::logging::Write(text);
    }
    // Exactly one creation call: ownership, charge use, link and lifetime stay native.
    return original(game,player,room,x,y,destination,output,kind,skipSearch,bypassTown);
}
}
void Publish(uint64_t session,float x,float y,bool active){
    std::lock_guard lock(mutex);facing=active?Facing{session,GetTickCount64(),x,y}:Facing{};
}
void Install(){
    if(original)return;
    static uint64_t retry=0;const auto now=GetTickCount64();if(now<retry)return;retry=now+5000;
    if(!d2rsig::Check(abi::FreeRva,abi::FreeSig,sizeof abi::FreeSig)||
       !d2rsig::Check(abi::FreeCallRva,abi::FreeCallSig,sizeof abi::FreeCallSig)||
       !d2rsig::Check(abi::LevelRva,abi::LevelSig,sizeof abi::LevelSig)||
       !d2rsig::Check(bowvr::native::RoomPointRva,bowvr::native::RoomPointSig,sizeof bowvr::native::RoomPointSig)||
       !d2rsig::Check(bowvr::native::MaskRva,bowvr::native::MaskSig,sizeof bowvr::native::MaskSig))return;
    if(d2rsig::Hook(abi::CreateRva,abi::CreateSig,sizeof abi::CreateSig,reinterpret_cast<void*>(&OnCreate),reinterpret_cast<void**>(&original)))
        framevr::logging::Write("Frame town portal: forward native placement ready (offline VR)");
}
}
