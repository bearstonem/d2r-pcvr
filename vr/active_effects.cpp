#include <windows.h>
#include <D2RLPlugin/api.h>
#include "active_effects.h"
#include "melee.h"
#include "memory.h"
#include "sigscan.h"
#include "logging.h"
#include <atomic>
#include <cstdio>
namespace gamestate {uint64_t LocalPlayer();}
namespace effectstate {
namespace {
const D2RL::PluginContext* context=nullptr;
const D2RL::DataTableService* tables=nullptr;
// Same read-only state accessor already used for the hireling's native tint.
// Audited 3.3.93787: validates unit type/state bounds, then reads the state bits.
constexpr uint64_t CheckStateRva=0x3351b0;
constexpr uint8_t CheckStateSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,
    0x57,0x48,0x83,0xec,0x20,0x8b,0xda,0x48,0x8b,0xf1,0xe8,0x07,0x68,0x01,0x00};
constexpr uint8_t TailSig[]={0x5f,0xe9,0x9a,0x0f,0xfc,0xff};
template<class T>bool Read(uintptr_t p,T& out){return p>=0x10000&&p<=UINTPTR_MAX-sizeof out&&framevr::ReadMemory(&out,reinterpret_cast<void*>(p),sizeof out);}
// The last sample's Holy Shield, for the shield guard on another thread.
std::atomic<uint64_t> holySession{0},holyAt{0};
// Whether the last known sample had an aura, for the aura icon's release.
std::atomic<uint64_t> auraSession{0},auraSampledAt{0};
std::atomic<bool> auraOn{false};
}
void Init(const D2RL::PluginContext* c){context=c;c->QueryService(&tables);}
framevr::effects::Status Sample(uint64_t session) {
    framevr::effects::Status result;uintptr_t game=0,player=0;uint32_t header[4]{};uint8_t bank=0;
    if(session&&session==gamestate::LocalPlayer()&&tables&&tables->getTable&&
       d2rsig::Check(CheckStateRva,CheckStateSig,sizeof CheckStateSig)&&d2rsig::Check(0x335280,TailSig,sizeof TailSig)&&
       melee::LocalServer(game,player)&&Read(player,header)&&header[0]==0&&header[1]<=7&&header[3]!=0&&header[3]!=17&&
       Read(game+0x106,bank)&&bank>=1&&bank<=3) {
        D2RL::DataTables::TableView view{};view.structSize=sizeof view;
        if(tables->getTable(context,D2RL::DataTables::Bank(bank),D2RL::DataTables::TableId::States,&view)==D2RL::DataTables::Result::Success&&view.rows&&view.rowSize==0x44) {
            auto check=reinterpret_cast<int(*)(uintptr_t,int)>(d2rsig::Addr(CheckStateRva));
            result=framevr::effects::Capture(view.rowCount,[&](int state){return check(player,state)?1:0;});
        }
    }
    holyAt=result.Has(framevr::effects::HolyShieldState)?GetTickCount64():0;holySession=session;
    auraOn=framevr::effects::AnyAura(result);auraSampledAt=result.known?GetTickCount64():0;auraSession=session;
    static framevr::effects::Status before;static uint64_t lastSession=0;
    if(result!=before||lastSession!=session) {
        char line[768];int n=std::snprintf(line,sizeof line,"Frame active effects: %s",result.known?"states":"unavailable");
        for(const auto& d:framevr::effects::Definitions)if(result.Has(d.state)&&n<int(sizeof line)-12)
            n+=std::snprintf(line+n,sizeof line-size_t(n)," %d",d.state);
        framevr::logging::Write(line);before=result;lastSession=session;
    }
    return result;
}
}
namespace effectstate {
bool HolyShield(uint64_t session) {
    const uint64_t at=holyAt.load(),now=GetTickCount64();
    return session&&holySession.load()==session&&at&&now>=at&&now-at<=1000;   // sampled every 200 ms
}
int AuraState(uint64_t session) {
    const uint64_t at=auraSampledAt.load(),now=GetTickCount64();
    if(!session||auraSession.load()!=session||!at||now<at||now-at>1000)return -1;
    return auraOn.load()?1:0;
}
}
