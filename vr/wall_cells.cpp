// Wall cells for dropped bottles, D2R 3.3.93787: the same room lookup and
// missile-barrier mask (0x804) physical arrows test against walls.
#include <windows.h>
#include <D2RLPlugin/api.h>
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include "wall_cells.h"
#include "bow_signatures.h"
#include "melee.h"
#include "sigscan.h"
#include "logging.h"
namespace gamestate {uint64_t LocalPlayer();}
namespace wallcells {
namespace {
using namespace bowvr::native;
using framevr::bottles::PatchCells;
constexpr uint64_t Interval=500; // doors open and close: resample a still point this often
constexpr int Recentre=6;        // cells the wanted point may move before a new sample
const D2RL::PluginContext* context=nullptr;
const D2RL::ThreadService* threads=nullptr;
std::mutex mutex;
framevr::bottles::WallRequest wanted{};
framevr::bottles::Patch latest{};
bool scheduled=false,have=false;
uint64_t sampledAt=0,serial=0;
std::atomic<int> verified{0}; // 0 unchecked, 1 ready, -1 changed game code
std::atomic<unsigned> samples{0},refused{0},blockedCells{0};
bool Verified() {
    int v=verified.load();
    if(!v) {
        v=d2rsig::Check(RoomRva,RoomSig,sizeof RoomSig)&&d2rsig::Check(RoomPointRva,RoomPointSig,sizeof RoomPointSig)&&
          d2rsig::Check(MaskRva,MaskSig,sizeof MaskSig)?1:-1;
        verified=v;
        framevr::logging::Write(v>0?"Frame dropped bottles: native wall cells ready":
            "Frame dropped bottles: native wall cells unavailable (changed game code); bottles keep the floor only");
    }
    return v>0;
}
void __cdecl Sample(const D2RL::PluginContext*,void*) noexcept {
    framevr::bottles::WallRequest r;
    {std::lock_guard lock(mutex);r=wanted;}
    framevr::bottles::Patch p;
    uintptr_t game=0,player=0,room=0;
    bool ok=r.session&&r.session==gamestate::LocalPlayer()&&melee::LocalServer(game,player);
    if(ok)room=reinterpret_cast<uintptr_t(*)(uintptr_t)>(d2rsig::Addr(RoomRva))(player);
    if(ok&&room) {
        const auto centre=framevr::bottles::CellAt(r.center,r.floor,r.unitsPerMetre);
        p.session=r.session;p.x0=centre.x-PatchCells/2;p.z0=centre.z-PatchCells/2;
        unsigned blocked=0;
        // Serpentine order: each lookup starts from a neighbouring cell's room.
        for(int z=0;z<PatchCells;++z)for(int i=0;i<PatchCells;++i) {
            const int x=z&1?PatchCells-1-i:i,cx=p.x0+x,cz=p.z0+z;
            bool wall=cx<1||cz<1||cx>65534||cz>65534;
            if(!wall) {
                const auto next=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(RoomPointRva))(room,cx,cz);
                // Closed or unloaded rooms are conservative walls, as for arrows.
                wall=!next||reinterpret_cast<uint16_t(*)(uintptr_t,int,int,int,int)>(d2rsig::Addr(MaskRva))(next,cx,cz,1,0x804)!=0;
                if(next)room=next;
            }
            if(wall){p.rows[z]|=1u<<x;++blocked;}
        }
        blockedCells=blocked;
    } else ok=false;
    std::lock_guard lock(mutex);
    scheduled=false;sampledAt=GetTickCount64();
    if(ok){p.serial=++serial;latest=p;have=true;++samples;}
    else ++refused;
}
}
void Init(const D2RL::PluginContext* ctx) {
    context=ctx;
    if(ctx->QueryService(&threads)!=D2RL::ServiceQueryResult::Success||
       !D2RL::HasThreadServiceField(threads,D2RL::ThreadServiceRequiredSize))threads=nullptr;
}
void Want(const framevr::bottles::WallRequest& r,uint64_t now) {
    if(!context||!threads||!threads->runOnGameThread||!r.session||!framevr::bottles::Finite(r.center)||
       !(r.unitsPerMetre>=1&&r.unitsPerMetre<=12)||!std::isfinite(r.floor))return;
    {
        std::lock_guard lock(mutex);
        if(scheduled)return;
        const auto c=framevr::bottles::CellAt(r.center,r.floor,r.unitsPerMetre);
        const bool nearby=have&&latest.session==r.session&&
            std::abs(c.x-(latest.x0+PatchCells/2))<=Recentre&&std::abs(c.z-(latest.z0+PatchCells/2))<=Recentre;
        if(nearby&&now>=sampledAt&&now-sampledAt<Interval)return;
        if(!nearby&&have&&now>=sampledAt&&now-sampledAt<50)return; // at most one game tick apart
        wanted=r;scheduled=true;
    }
    if(!Verified()||threads->runOnGameThread(context,&Sample,nullptr)!=D2RL::Threads::Result::Success) {
        std::lock_guard lock(mutex);scheduled=false;sampledAt=now;
    }
}
bool Latest(framevr::bottles::Patch& out) {
    std::lock_guard lock(mutex);
    if(!have)return false;
    out=latest;return true;
}
void Report() {
    const auto s=samples.exchange(0),f=refused.exchange(0);
    if(!s&&!f)return;
    char line[160];std::snprintf(line,sizeof line,"Frame dropped bottles: wall samples=%u refused=%u last blocked cells=%u of %d",
        s,f,blockedCells.load(),PatchCells*PatchCells);
    framevr::logging::Write(line);
}
}
