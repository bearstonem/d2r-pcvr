// Supply a VR-only player count to the native offline scaling calculation.
// No enemy stat edits, simulated clients, packet injection or Settings.json writes.
#include <windows.h>
#include <atomic>
#include <cstdio>
#include "offline_difficulty.h"
#include "offline_difficulty_signatures.h"
#include "settings_menu.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"

namespace offlinedifficulty {
namespace {
using PlayersFn=int(*)(uintptr_t);
using ModeFn=int(*)();
PlayersFn original=nullptr;
ModeFn mode=nullptr;
std::atomic<int> wanted{0};
std::atomic<unsigned> observed{0};
bool installed=false,toldMissing=false;
int Players(uintptr_t game) {
    // The loader enables its detour just before publishing the trampoline.
    auto& slot=reinterpret_cast<PlayersFn const volatile&>(original);
    auto call=slot;
    for(int i=0;!call&&i<(1<<20);++i){YieldProcessor();call=slot;}
    if(!call)return 1;
    const int native=call(game),choice=wanted.load();
    if(choice==0||!mode||mode()!=0||game<=0x10000)return native;
    uint8_t kind=0;
    if(!framevr::ReadMemory(&kind,reinterpret_cast<void*>(game+0x101),sizeof kind)||kind<1||kind>3)
        return native;
    // The native game owns HP, damage, experience and loot formulas. Changing
    // this input does not retrofit already-spawned monsters or change levels.
    observed.fetch_or(1u<<choice);
    return choice;
}
}
void Configure(int players,bool vr) {
    const int next=vr&&players>=1&&players<=8?players:0;
    if(wanted.exchange(next)!=next) {
        char line[160];std::snprintf(line,sizeof line,
            "Frame offline difficulty: requested=%d (0=native default); new monsters use native scaling",next);
        framevr::logging::Write(line);
    }
}
void Install() {
    if(installed)return;
    using namespace abi;
    if(!d2rsig::Check(PlayersRva,PlayersSig,sizeof PlayersSig)||!d2rsig::Check(ModeRva,ModeSig,sizeof ModeSig)) {
        if(!toldMissing){toldMissing=true;framevr::logging::Write("Frame offline difficulty: native signatures unavailable; leaving scaling unchanged (will retry)");}
        return;
    }
    mode=reinterpret_cast<ModeFn>(d2rsig::Addr(ModeRva));
    if(!mode)return;
    installed=d2rsig::Hook(PlayersRva,PlayersSig,sizeof PlayersSig,reinterpret_cast<void*>(&Players),reinterpret_cast<void**>(&original));
    if(installed){
        vrsettings::SetOfflineDifficultyAvailable(true);
        framevr::logging::Write("Frame offline difficulty: native Players 1-8 scaling adapter ready; offline VR only");
    }
}
bool Offline(){return mode&&mode()==0;}
void Report() {
    static unsigned reported=0;
    const auto seen=observed.load();
    for(int i=1;i<=8;++i)if((seen&~reported)&(1u<<i)) {
        char line[120];std::snprintf(line,sizeof line,"Frame offline difficulty: native scaling requested effective players=%d",i);
        framevr::logging::Write(line);
    }
    reported=seen;
}
}
