// Exercise the actual queued scanner with synthetic native/loader boundaries.
#include "../vr/monster_immunity.cpp"
#include <cassert>
#include <vector>

namespace {
uint64_t session=7;
bool signatures=true,localServer=true,statAvailable=true,cancelDuringRead=false;
int mode=0,statReads=0,hostileScans=0;
std::array<int,6> resistance{0,0,100,0,100,0};
std::vector<melee::Hostile> hostiles{{1,0,3,4,true}};
struct Pending {D2RL::Threads::Callback callback;void* user;};
std::vector<Pending> pending;
D2RL::Threads::Result Queue(const D2RL::PluginContext*,D2RL::Threads::Callback callback,void* user) noexcept {
    pending.push_back({callback,user});return D2RL::Threads::Result::Success;
}
void Run(size_t index=0) {auto p=pending[index];pending.erase(pending.begin()+index);p.callback(nullptr,p.user);}
int Mode(){return mode;}
}
namespace gamestate {uint64_t LocalPlayer(){return session;}}
namespace framevr::logging {void Write(const char*){}}
namespace d2rsig {
bool Check(uint64_t,const void*,uint32_t){return signatures;}
uintptr_t Addr(uint64_t){return reinterpret_cast<uintptr_t>(&Mode);}
}
namespace melee {
bool LocalServer(uintptr_t& game,uintptr_t& player){game=0x10000;player=0x20000;return localServer;}
uintptr_t ServerUnit(uintptr_t,int,uint32_t id){return id?0x30000:0;}
int Hostiles(Hostile* out,int max,float,float& x,float& y){
    ++hostileScans;x=y=0;
    const int n=std::min(max,int(hostiles.size()));
    std::copy_n(hostiles.begin(),n,out);return n;
}
}
namespace combat {
bool UnitStat(uintptr_t,int stat,int& value){
    ++statReads;
    if(cancelDuringRead){cancelDuringRead=false;monsterimmunity::Tick(GetTickCount64(),false);}
    const auto it=std::find(framevr::immunity::Stats.begin(),framevr::immunity::Stats.end(),stat);
    assert(it!=framevr::immunity::Stats.end());
    value=resistance[size_t(it-framevr::immunity::Stats.begin())];return statAvailable;
}
}
int main(){
    using namespace monsterimmunity;
    D2RL::ThreadService service{};service.runOnGameThread=&Queue;threads=&service;
    uint64_t now=GetTickCount64();
    Tick(now,false);assert(pending.empty());
    Tick(now,true);assert(pending.size()==1&&statReads==0); // no native access on the update thread
    Tick(now+20,true);assert(pending.size()==1);
    Run();auto shown=Latest(GetTickCount64());
    assert(shown.count==1&&shown.monsters[0].mask==20&&statReads==6);
    assert(Latest(GetTickCount64()+501).count==0);
    ++session;assert(Latest(GetTickCount64()).count==0);--session;

    // A callback left queued while VR/menu/world conditions become inactive cannot republish.
    now+=100;Tick(now,true);assert(pending.size()==1);Tick(now,false);Run();
    assert(Latest(GetTickCount64()).count==0&&statReads==6);
    now+=100;Tick(now,true);Run();assert(Latest(GetTickCount64()).count==1);

    // A discarded callback's late arrival cannot steal a newer request's ticket.
    now+=100;Tick(now,true);now+=1100;Tick(now,true);assert(pending.size()==2);
    const int before=statReads;Run();assert(statReads==before&&pending.size()==1);
    Run();assert(statReads==before+6);

    // A broken immunity, removed enemy and lost line of sight clear on the next scan.
    resistance.fill(99);now+=100;Tick(now,true);Run();assert(Latest(GetTickCount64()).count==0);
    resistance.fill(100);hostiles[0].clear=false;now+=100;Tick(now,true);Run();assert(Latest(GetTickCount64()).count==0);
    hostiles[0].clear=true;statAvailable=false;now+=100;Tick(now,true);Run();assert(Latest(GetTickCount64()).count==0);
    statAvailable=true;now+=100;Tick(now,true);Run();assert(Latest(GetTickCount64()).monsters[0].mask==63);
    hostiles.clear();now+=100;Tick(now,true);Run();assert(Latest(GetTickCount64()).count==0);
    hostiles={{1,0,3,4,true}};

    for(int online:{1,5,7}){mode=online;const int scans=hostileScans;now+=100;Tick(now,true);Run();
        assert(hostileScans==scans&&Latest(GetTickCount64()).count==0);}
    mode=0;signatures=false;now+=100;Tick(now,true);Run();assert(Latest(GetTickCount64()).count==0);
    signatures=true;localServer=false;now+=100;Tick(now,true);Run();assert(Latest(GetTickCount64()).count==0);
    localServer=true;cancelDuringRead=true;now+=100;Tick(now,true);Run();assert(Latest(GetTickCount64()).count==0);
}
