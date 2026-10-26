#include "target_lock.h"
#include <windows.h>
#include <D2RLPlugin/api.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include "melee.h"
#include "projectiles.h"
#include "../steamvr/logging.h"

namespace targetlock {
namespace {
using namespace D2RL;
const PluginContext* context=nullptr;
const ThreadService* threads=nullptr;
std::mutex mutex;
Snapshot latest;
std::atomic<bool> scheduled{false};
std::atomic<uint64_t> askedAt{0};

void __cdecl Refresh(const PluginContext*,void*) noexcept {
    melee::Hostile found[framevr::targetcone::MaxTargets];
    float heroX=0,heroY=0;
    const int n=melee::Hostiles(found,framevr::targetcone::MaxTargets,framevr::aimreach::CornerCells+4,heroX,heroY);   // the game's reach (aimreach), its corners included
    Snapshot s;
    if(n>=0) {
        for(int i=0;i<n;++i)s.targets[i]={found[i].id,found[i].x,found[i].y,found[i].clear};
        s.count=n;s.heroX=heroX;s.heroY=heroY;s.at=GetTickCount64();
    }
    {std::lock_guard lock(mutex);latest=s;}
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
    if(ctx&&ctx->QueryService(&threads)!=ServiceQueryResult::Success)threads=nullptr;
}
void Pump(bool wanted) {
    if(!wanted){std::lock_guard lock(mutex);latest={};return;}
    const uint64_t now=GetTickCount64();
    // A request still pending after a second will not come back: leaving a game
    // drops queued game-thread work, and waiting for it stopped the highlight
    // for the rest of the session.
    if(scheduled.load()&&now-askedAt.load()>1000){
        scheduled=false;framevr::logging::Write("Frame target lock: a pending scan never ran (game left?); asking again");
    }
    if(!threads||!threads->runOnGameThread||now-askedAt.load()<100||scheduled.exchange(true))return;
    askedAt=now;
    if(threads->runOnGameThread(context,&Refresh,nullptr)!=Threads::Result::Success)scheduled=false;
}
Snapshot Get(){std::lock_guard lock(mutex);return latest;}
}
