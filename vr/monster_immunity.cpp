#include <windows.h>
#include <D2RLPlugin/api.h>
#include "monster_immunity.h"
#include "melee.h"
#include "combat.h"
#include "offline_difficulty_signatures.h"
#include "sigscan.h"
#include "logging.h"
#include "../steamvr/callback_ticket.h"
#include <mutex>

namespace gamestate {uint64_t LocalPlayer();}
namespace monsterimmunity {
namespace {
const D2RL::PluginContext* context=nullptr;
const D2RL::ThreadService* threads=nullptr;
std::mutex mutex;
framevr::CallbackTicket ticket;
framevr::immunity::Snapshot shown;
uint64_t nextScan=0,generation=0;
bool active=false;

void __cdecl Scan(const D2RL::PluginContext*,void* user) noexcept {
    uint64_t epoch=0;
    {
        std::lock_guard lock(mutex);
        if(!ticket.Claim(reinterpret_cast<uintptr_t>(user))||!active)return;
        epoch=generation;
    }
    using namespace framevr::immunity;
    Snapshot latest;latest.session=gamestate::LocalPlayer();latest.at=GetTickCount64();
    using namespace offlinedifficulty::abi;
    // Only offline mode is accepted, even if another mode has local server units.
    if(latest.session&&d2rsig::Check(ModeRva,ModeSig,sizeof ModeSig)&&
       reinterpret_cast<int(*)()>(d2rsig::Addr(ModeRva))()==0) {
        melee::Hostile found[128]{};float heroX=0,heroY=0;
        const int n=melee::Hostiles(found,128,RangeCells,heroX,heroY);
        uintptr_t game=0,self=0;
        if(n>=0&&melee::LocalServer(game,self))for(int i=0;i<n;++i) {
            if(!found[i].clear)continue;
            const auto unit=melee::ServerUnit(game,1,found[i].id);
            if(!unit)continue;
            const auto result=Detect([&](int stat,int& value){return combat::UnitStat(unit,stat,value);});
            if(result.known&&result.mask)Insert(latest,{found[i].id,found[i].x,found[i].y,0,0,result.mask},heroX,heroY);
        }
    }
    std::lock_guard lock(mutex);
    if(!active||generation!=epoch||latest.session!=gamestate::LocalPlayer())return;
    Connect(latest,shown);shown=latest;
}
}
void Init(const D2RL::PluginContext* ctx) {
    context=ctx;
    if(!ctx||ctx->QueryService(&threads)!=D2RL::ServiceQueryResult::Success)threads=nullptr;
    framevr::logging::Write("Frame monster immunity: read-only offline scan; physical white, magic purple, fire red, lightning yellow, cold blue, poison green");
}
void Tick(uint64_t now,bool wanted) {
    uintptr_t token=0;
    {
        std::lock_guard lock(mutex);
        if(wanted!=active) {active=wanted;++generation;ticket.Cancel();shown={};nextScan=0;}
        if(!active||!threads||!threads->runOnGameThread||now<nextScan)return;
        if(ticket.Expired(now,1000))ticket.Cancel();
        token=ticket.Start(now);
        if(!token)return;
        nextScan=now+framevr::immunity::ScanMs;
    }
    if(threads->runOnGameThread(context,&Scan,reinterpret_cast<void*>(token))!=D2RL::Threads::Result::Success) {
        std::lock_guard lock(mutex);ticket.Claim(token);
    }
}
framevr::immunity::Snapshot Latest(uint64_t now) {
    std::lock_guard lock(mutex);
    return active&&framevr::immunity::Fresh(shown,gamestate::LocalPlayer(),now)?shown:framevr::immunity::Snapshot{};
}
}
