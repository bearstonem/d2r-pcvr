// Wall cells for dropped bottles and held items, D2R 3.3.93787: the same room
// lookup and missile-barrier mask (0x804) physical arrows test against walls.
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
// One sample stream per user, so neither recentres the other's: 0 the dropped bottles and props (around a rolling
// bottle), 1 the held items (around the hero; held_world.h).
struct Slot {
    framevr::bottles::WallRequest wanted{};
    framevr::bottles::Patch latest{};
    bool scheduled=false,have=false;
    uint64_t sampledAt=0,askedAt=0;
};
Slot slots[2];
uint64_t serial=0;
std::atomic<int> verified{0}; // 0 unchecked, 1 ready, -1 changed game code
std::atomic<unsigned> samples[2]{},refused{0},blockedCells{0};
std::atomic<uint64_t> sampleTicks{0};   // QPC ticks spent sampling on the game thread, since the last report
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
void __cdecl Sample(const D2RL::PluginContext*,void* token) noexcept {
    const int which=int(reinterpret_cast<uintptr_t>(token)&1);
    LARGE_INTEGER began;QueryPerformanceCounter(&began);
    framevr::bottles::WallRequest r;
    {std::lock_guard lock(mutex);r=slots[which].wanted;}
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
            bool wall=cx<1||cz<1||cx>65534||cz>65534,unknown=wall;
            uint16_t bits=0;
            if(!wall) {
                const auto next=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(RoomPointRva))(room,cx,cz);
                // Closed or unloaded rooms are conservative walls, as for arrows. Every bit is read (the same one
                // call): the barrier for the bottles, and which barrier it is for the held items (held_world.h).
                if(next)bits=reinterpret_cast<uint16_t(*)(uintptr_t,int,int,int,int)>(d2rsig::Addr(MaskRva))(next,cx,cz,1,0xffff);
                unknown=!next;
                wall=unknown||(bits&framevr::bottles::BarrierBits)!=0;
                if(next)room=next;
            }
            if(wall) {
                p.rows[z]|=1u<<x;++blocked;
                if(unknown)p.unknown[z]|=1u<<x;
                else if(bits&framevr::bottles::DoorBit)p.doors[z]|=1u<<x;
                else if(bits&framevr::bottles::ObjectBit)p.objects[z]|=1u<<x;
            }
        }
        blockedCells=blocked;
    } else ok=false;
    LARGE_INTEGER ended;QueryPerformanceCounter(&ended);
    sampleTicks+=uint64_t(ended.QuadPart-began.QuadPart);
    std::lock_guard lock(mutex);
    Slot& s=slots[which];
    s.scheduled=false;s.sampledAt=GetTickCount64();
    if(ok){p.serial=++serial;p.at=s.sampledAt;s.latest=p;s.have=true;++samples[which];}
    else ++refused;
}
void Ask(int which,const framevr::bottles::WallRequest& r,uint64_t now) {
    if(!context||!threads||!threads->runOnGameThread||!r.session||!framevr::bottles::Finite(r.center)||
       !(r.unitsPerMetre>=1&&r.unitsPerMetre<=12)||!std::isfinite(r.floor))return;
    {
        std::lock_guard lock(mutex);
        Slot& s=slots[which];
        // Queued and not run yet. The loader drops queued game work at a session change: one asked long ago is lost.
        if(s.scheduled&&now>=s.askedAt&&now-s.askedAt<2000)return;
        const auto c=framevr::bottles::CellAt(r.center,r.floor,r.unitsPerMetre);
        const bool nearby=s.have&&s.latest.session==r.session&&
            std::abs(c.x-(s.latest.x0+PatchCells/2))<=Recentre&&std::abs(c.z-(s.latest.z0+PatchCells/2))<=Recentre;
        if(nearby&&now>=s.sampledAt&&now-s.sampledAt<Interval)return;
        if(!nearby&&s.have&&now>=s.sampledAt&&now-s.sampledAt<50)return; // at most one game tick apart
        s.wanted=r;s.scheduled=true;s.askedAt=now;
    }
    if(!Verified()||threads->runOnGameThread(context,&Sample,reinterpret_cast<void*>(uintptr_t(which)))!=D2RL::Threads::Result::Success) {
        std::lock_guard lock(mutex);slots[which].scheduled=false;slots[which].sampledAt=now;
    }
}
bool Take(int which,framevr::bottles::Patch& out) {
    std::lock_guard lock(mutex);
    if(!slots[which].have)return false;
    out=slots[which].latest;return true;
}
}
void Init(const D2RL::PluginContext* ctx) {
    context=ctx;
    if(ctx->QueryService(&threads)!=D2RL::ServiceQueryResult::Success||
       !D2RL::HasThreadServiceField(threads,D2RL::ThreadServiceRequiredSize))threads=nullptr;
}
void Want(const framevr::bottles::WallRequest& r,uint64_t now) {Ask(0,r,now);}
bool Latest(framevr::bottles::Patch& out) {return Take(0,out);}
void WantHeld(const framevr::bottles::WallRequest& r,uint64_t now) {Ask(1,r,now);}
bool LatestHeld(framevr::bottles::Patch& out) {return Take(1,out);}
void Report() {
    const auto s=samples[0].exchange(0),h=samples[1].exchange(0),f=refused.exchange(0);
    const auto ticks=sampleTicks.exchange(0);
    if(!s&&!h&&!f)return;
    static LARGE_INTEGER frequency{};if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);
    const double us=frequency.QuadPart?double(ticks)*1e6/double(frequency.QuadPart):0;
    char line[240];std::snprintf(line,sizeof line,"Frame dropped bottles: wall samples=%u (held items %u) refused=%u last blocked cells=%u of %d; "
        "%.0f us on the game thread (%.0f us a sample)",s,h,f,blockedCells.load(),PatchCells*PatchCells,us,s+h?us/double(s+h):0.0);
    framevr::logging::Write(line);
}
}
