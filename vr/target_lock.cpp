#include "target_lock.h"
#include <windows.h>
#include <D2RLPlugin/api.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include "melee.h"
#include "target_reports.h"
#include "client_combat.h"
#include "combat.h"
namespace gamestate {uint64_t LocalPlayer();}
#include "projectiles.h"
#include "../steamvr/logging.h"

namespace targetlock {
namespace {
using namespace D2RL;
const PluginContext* context=nullptr;
const ThreadService* threads=nullptr;
std::mutex mutex;
Snapshot latest;
std::atomic<bool> networkWanted{false};
bool SamplePlayer(uint32_t player,targetlock::Snapshot& out){return melee::PlayerTargets(player,out);}
std::atomic<bool> scheduled{false};
std::atomic<uint64_t> askedAt{0};
// How often the monsters are read (owner, 2026-11-09: "speed up the game's target acquisition"): every game
// update (25 a second) while any hostile monster is near, so a pick follows a monster that moves or comes
// into view within 40 ms, not 100; every 100 ms while there is none. The scan's cost is logged every 10 s.
constexpr uint64_t NearMs=40,IdleMs=100;
std::atomic<bool> near_{false};
struct Cost {unsigned scans=0;double sum=0,most=0;uint64_t toldAt=0;} cost;   // game thread

void __cdecl Refresh(const PluginContext*,void*) noexcept {
    LARGE_INTEGER began{},ended{},frequency{};QueryPerformanceCounter(&began);
    melee::Hostile found[framevr::targetcone::MaxTargets];
    float heroX=0,heroY=0;
    const int n=melee::Hostiles(found,framevr::targetcone::MaxTargets,framevr::aimreach::CornerCells+4,heroX,heroY);   // the game's reach (aimreach), its corners included
    Snapshot s;
    if(n>=0) {
        for(int i=0;i<n;++i)s.targets[i]={found[i].id,found[i].x,found[i].y,found[i].clear};
        s.count=n;s.heroX=heroX;s.heroY=heroY;s.at=GetTickCount64();
    }
    // Monsters the aim skips for want of a clear line from the hero's spot (owner, 2026-11-09: spells do not
    // reach through a doorway): how many, and the nearest, every two seconds while there are any.
    if(static uint64_t toldAt=0;n>0&&GetTickCount64()>=toldAt) {
        int hidden=0;float nearest=1e9f;uint32_t id=0;
        for(int i=0;i<n;++i)if(!found[i].clear){++hidden;const float d=std::hypot(found[i].x-heroX,found[i].y-heroY);if(d<nearest){nearest=d;id=found[i].id;}}
        if(hidden) {
            toldAt=GetTickCount64()+2000;
            char line[200];std::snprintf(line,sizeof line,"Frame target lock: %d of %d hostile monsters have no clear line from the hero's spot and are not aimed at; the nearest is monster %u, %.1f cells off",hidden,n,id,nearest);
            framevr::logging::Write(line);
        }
    }
    {std::lock_guard lock(mutex);latest=s;}
    near_=n>0;
    QueryPerformanceCounter(&ended);QueryPerformanceFrequency(&frequency);
    const double ms=frequency.QuadPart?double(ended.QuadPart-began.QuadPart)*1000./double(frequency.QuadPart):0;
    ++cost.scans;cost.sum+=ms;if(ms>cost.most)cost.most=ms;
    if(const auto now=GetTickCount64();now>=cost.toldAt) {
        if(cost.toldAt&&n>0) {
            char line[200];std::snprintf(line,sizeof line,"Frame target lock: %u scans in 10 s (every %llu ms with monsters near), %.2f ms each on average, %.2f ms at most, %.1f ms a second",
                cost.scans,(unsigned long long)NearMs,cost.sum/cost.scans,cost.most,cost.sum/10.);
            framevr::logging::Write(line);
        }
        cost={};cost.toldAt=now+10000;
    }
    projectiles::FollowFlights();   // telemetry: where the hero's missiles end (game thread)
    static int told=-2;
    if((n<0)!=(told<0)||(n>0)!=(told>0)) {
        told=n;
        char line[128];std::snprintf(line,sizeof line,n<0?"Frame target lock: local server not available":"Frame target lock: %d hostile monsters near",n);
        framevr::logging::Write(line);
    }
    scheduled=false;
}
}
void Init(const PluginContext* ctx) {
    context=ctx;
    targetreports::Init(ctx, &SamplePlayer);
    combat::SetHitObserver(&targetreports::NoteMeleeHit);
    if(ctx&&ctx->QueryService(&threads)!=ServiceQueryResult::Success)threads=nullptr;
}
void Pump(bool wanted) {
    networkWanted=wanted;
    if(clientcombat::NativeOnly(gamestate::LocalPlayer()))return;
    if(!wanted){near_=false;std::lock_guard lock(mutex);latest={};return;}
    const uint64_t now=GetTickCount64();
    // A request still pending after a second will not come back: leaving a game
    // drops queued game-thread work, and waiting for it stopped the highlight
    // for the rest of the session.
    if(scheduled.load()&&now-askedAt.load()>1000){
        scheduled=false;framevr::logging::Write("Frame target lock: a pending scan never ran (game left?); asking again");
    }
    if(!threads||!threads->runOnGameThread||now-askedAt.load()<(near_.load()?NearMs:IdleMs)||scheduled.exchange(true))return;
    askedAt=now;
    if(threads->runOnGameThread(context,&Refresh,nullptr)!=Threads::Result::Success)scheduled=false;
}
void TickNetwork(uint64_t session,uint32_t unit){
    targetreports::Tick(session,unit,clientcombat::NativeOnly(session));
}
void Stop(){combat::SetHitObserver(nullptr);targetreports::Stop();networkWanted=false;std::lock_guard lock(mutex);latest={};}
Snapshot Get(){
    const auto session=gamestate::LocalPlayer();
    if(clientcombat::NativeOnly(session)){
        uint32_t unit=0;combat::LocalPlayerId(unit);
        return networkWanted.load()?targetreports::Get(session,unit,GetTickCount64()):Snapshot{};
    }
    std::lock_guard lock(mutex);return latest;
}
}
