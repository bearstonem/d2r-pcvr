// Exercise the real adapter against synthetic native functions and game memory.
#include "../vr/offline_difficulty.cpp"
#undef NDEBUG
#include <cassert>
#include <array>
using namespace offlinedifficulty;
namespace {
std::array<uint8_t,0x200> game{};
bool signatures=false,hookWorks=false,available=false;
uint64_t badGuard=0;
int nativePlayers=1,clientMode=0,calls=0,modeCalls=0,hooks=0;
uintptr_t G(){return reinterpret_cast<uintptr_t>(game.data());}
int Native(uintptr_t){++calls;return nativePlayers;}
int Mode(){++modeCalls;return clientMode;}
int Count(){const int before=calls;const int result=Players(G());assert(calls==before+1);return result;}
}
namespace d2rsig {
bool Check(uint64_t rva,const void*,uint32_t){return signatures&&rva!=badGuard;}
bool Hook(uint64_t rva,const void*,uint32_t,void* detour,void** out) {
    assert(rva==abi::PlayersRva&&detour==reinterpret_cast<void*>(&Players));++hooks;
    if(!hookWorks)return false;
    *out=reinterpret_cast<void*>(&Native);return true;
}
uintptr_t Addr(uint64_t rva){assert(rva==abi::ModeRva);return reinterpret_cast<uintptr_t>(&Mode);}
}
namespace vrsettings {void SetOfflineDifficultyAvailable(bool ready){available=ready;}}
namespace framevr::logging {void Write(const char*){}}
int main() {
    Install();assert(!installed&&!hooks&&!available&&ClientMode()==-1);   // the chat's client mode: unverified
    signatures=true;
    for(const auto rva:{abi::PlayersRva,abi::ModeRva}){badGuard=rva;Install();assert(!installed&&!hooks&&!available);}
    badGuard=0;Install();assert(!installed&&hooks==1&&!available);
    for(int state:{0,5,7}){clientMode=state;assert(ClientMode()==state);}   // verified apart from the player-count hook
    clientMode=0;
    hookWorks=true;Install();assert(installed&&hooks==2&&available);
    Install();assert(hooks==2);
    game[0x101]=1;
    for(int base=1;base<=8;++base) {
        nativePlayers=base;Configure(0,true);assert(Count()==base);
        for(int choice=1;choice<=8;++choice) {
            Configure(choice,true);assert(Count()==choice);
            Configure(choice,false);assert(Count()==base); // desktop remains native
        }
    }
    nativePlayers=5;Configure(8,true);
    for(int kind=0;kind<256;++kind){game[0x101]=uint8_t(kind);assert(Count()==(kind>=1&&kind<=3?8:5));}
    game[0x101]=1;
    for(int state=-1;state<=10;++state){clientMode=state;assert(Count()==(state==0?8:5));}
    clientMode=0;
    assert(Players(0)==5&&Players(0x10000)==5&&Players(0x10001)==5); // unreadable game
    for(int bad:{-1,9,999}){Configure(bad,true);assert(Count()==5);}
    Configure(7,true);assert(Count()==7);
    Configure(0,true);assert(Count()==5); // release override, including native /players changes
    nativePlayers=2;assert(Count()==2);
    assert(modeCalls>0&&observed.load()==0x1fe);
    Report();Report();
    std::puts("Offline difficulty: native defaults, all counts, both guards, retry, offline/desktop exclusions and restoration passed");
}
