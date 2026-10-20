// Exercise the real incoming-hit adapter with owned unit/path memory.
#include "../vr/combat.cpp"
#undef NDEBUG
#include <cassert>

namespace fixture {
uintptr_t imageBase=0x140000000;
uint64_t relocation=0;
uint64_t session=1;
unsigned nativeCalls=0,impacts=0,parries=0,rolled=0,swingScans=0;int parryDurability=0;
bool serve=true;
std::string log;
struct Unit {
    std::array<uint8_t,128> bytes{};
    uint16_t path[4]{};
    uintptr_t Address(){return reinterpret_cast<uintptr_t>(bytes.data());}
    template<class T> void Put(size_t offset,T value){memcpy(bytes.data()+offset,&value,sizeof value);}
    Unit(uint32_t type,uint32_t id,uint16_t z) {
        Put(0,type);Put(8,id);Put(0x38,reinterpret_cast<uintptr_t>(path));
        path[1]=100;path[3]=z;
    }
};
int NativeBlock(uintptr_t game,uintptr_t,uintptr_t,int avoid,int block) {
    assert(game==123&&avoid==7);++nativeCalls;
    if(!serve||!block||combat::forced==combat::Forced::None)return 9;
    combat::forcedServed=true;
    return combat::forced==combat::Forced::Block?1:0;
}
}
namespace gamestate {uint64_t LocalPlayer(){return fixture::session;}int32_t LeftDurability(){return -1;}}
namespace d2rsig {
uint64_t Rva(uint64_t rva){return rva?rva+fixture::relocation:0;}
uintptr_t Addr(uint64_t rva){return rva?fixture::imageBase+Rva(rva):0;}
bool Check(uint64_t,const void*,uint32_t){return false;}
bool Hook(uint64_t,const void*,uint32_t,void*,void**){return false;}
}
namespace framevr::logging {void Write(const char* line){fixture::log+=line;fixture::log+='\n';}}
namespace framevr {void ShieldParryMiss(){}bool GuardBroken(){return false;}void ShieldGuardImpact(bool,float){++fixture::impacts;}void ShieldGuardState(bool,bool){}}
namespace melee {bool QueueParry(uint32_t,int durability){++fixture::parries;fixture::parryDurability=durability;return true;}uint64_t AttackStart(uint32_t){return 0;}uint64_t ParryOpens(uint32_t){return 0;}void QueueBlockSound(){}void QueueClientDurability(uint32_t,int){}
void NoteAttackRolled(uint32_t){++fixture::rolled;}void QueueParrySwing(){++fixture::swingScans;}}

int main() {
    using namespace combat;
    assert(NativeCallerRva(fixture::imageBase+0x462fb0)==0x462fb0);
    fixture::relocation=0x3000;
    assert(NativeCallerRva(fixture::imageBase+0x465fb0)==0x465fb0);
    assert(NativeCallerRva(fixture::imageBase-1)==0&&NativeCallerRva(fixture::imageBase+0x4000000)==0);
    fixture::relocation=0;
    fixture::Unit player(0,42,100),monster(1,9,99);
    enabled=ready=guardChecked=true;originalBlock=&fixture::NativeBlock;
    ConfigureGuard(true,0,true);
    const auto prime=[&] {
        assert(ObserveUiPlayer(player.Address(),fixture::session));
        guard={};guard.session=fixture::session;guard.eligible=true;guard.upm=2;guard.at=GetTickCount64();
        framevr::guard::Pose pose{{-.12f,1.35f,-.25f},{0,0,-1},{0,1.7f,0},{-.12f,-.35f,-.25f},2,guard.at,1};
        PublishGuard(fixture::session,true,true,pose,true,true);
    };
    const auto block=[&] {
        const auto calls=fixture::nativeCalls;
        const int result=Block(123,monster.Address(),player.Address(),7,1);
        assert(fixture::nativeCalls==calls+1&&forced==Forced::None&&!forcedServed);
        return result;
    };
    framevr::guard::Debug debug;
    // Regression: the old adapter overrode these valid guards to OPEN in
    // walking(2), running(3), and town walking(6). Native dispatch still runs.
    for(uint32_t mode:{1u,2u,3u,6u}) {
        player.Put(0xc,mode);prime();const auto impacts=fixture::impacts;
        assert(block()==1&&fixture::impacts==impacts+1);
        assert(ReadGuardDebug(debug)&&debug.raised&&debug.hit==1&&debug.hitAt);
    }
    // Movement never grants a guard by itself; hand and view rules still apply.
    player.Put(0xc,3u);prime();
    auto lowered=guard.state.surface;lowered.hand={-.2f,-.8f,0};
    framevr::guard::Update(guard.state,lowered,true,true,guard.at,true,GuardTuning());
    assert(block()==0&&guardRejections[size_t(framevr::guard::Rejection::TooLow)]==1);
    assert(ReadGuardDebug(debug)&&!debug.raised&&debug.hit==2);
    assert(fixture::log.find("reason=hand_low")!=std::string::npos);
    prime();guard.state.surface.normal={1,0,0};assert(block()==0);
    prime();guard.state.surface.center.x=-.8f;assert(block()==0);
    prime();guard.state.seenAt=GetTickCount64()-200;assert(block()==0);
    assert(!ReadGuardDebug(debug));
    // Classic, missing shield/menu eligibility, wrong session and other players
    // keep native behavior. An unavailable native roll causes no fake feedback.
    prime();ConfigureGuard(false,0);assert(block()==9);
    assert(!ReadGuardDebug(debug));
    ConfigureGuard(true,0,true);prime();guard.eligible=false;assert(block()==9);
    prime();guard.session=2;assert(block()==9);
    assert(!ReadGuardDebug(debug));
    prime();player.Put(8,43u);assert(block()==9);player.Put(8,42u);
    prime();fixture::serve=false;const auto impacts=fixture::impacts;
    assert(block()==9&&fixture::impacts==impacts&&guardUnserved==1);
    assert(ReadGuardDebug(debug)&&debug.hit==3);
    assert(fixture::log.find("block_flag=1 avoid_flag=7 result=9 roll_served=0")!=std::string::npos);
    prime();fixture::serve=true;
    assert(Block(123,monster.Address(),player.Address(),7,0)==9&&fixture::impacts==impacts&&guardUnserved==2);
    assert(fixture::log.find("block_flag=0 avoid_flag=7 result=9 roll_served=0")!=std::string::npos);
    // Elemental/otherwise unblockable damage must not steal a timed parry
    // from the next blockable hit. An actual shield roll consumes it once.
    ConfigureGuard(true,400,true);prime();
    guard.state.snapAt=guard.at;guard.state.snapUsed=false;
    assert(Block(123,monster.Address(),player.Address(),7,0)==9&&!guard.state.snapUsed);
    fixture::serve=false;assert(block()==9&&!guard.state.snapUsed);
    fixture::serve=true;assert(block()==1&&guard.state.snapUsed);
    // A melee attack that misses its native to-hit roll never reaches the block
    // check. A timed parry swing still staggers it, once, with no shield wear
    // to refund; without a live swing a miss stays silent. Hits wait for the
    // block check, and native hit/miss is never changed.
    ConfigureGuard(true,450,true);prime();
    {const auto queued=fixture::parries,taps=fixture::impacts;
     const unsigned misses=incomingMisses,hits=incomingHits,missed=missParries;
     IncomingRoll(monster.Address(),player.Address(),false);
     assert(fixture::parries==queued&&fixture::impacts==taps&&incomingMisses==misses+1);
     assert(fixture::log.find("Frame guard miss: monster=9 class=0 verdict=MISSED parry=no_swing")!=std::string::npos);
     guard.state.snapAt=guard.at;guard.state.snapUsed=false;
     IncomingRoll(monster.Address(),player.Address(),true);
     assert(fixture::parries==queued&&!guard.state.snapUsed&&incomingHits==hits+1);
     IncomingRoll(monster.Address(),player.Address(),false);
     assert(fixture::parries==queued+1&&fixture::parryDurability==-1&&guard.state.snapUsed&&missParries==missed+1&&fixture::impacts==taps+1);
     assert(fixture::log.find("Frame guard miss: monster=9 class=0 verdict=PARRY parry=ok")!=std::string::npos);
     IncomingRoll(monster.Address(),player.Address(),false);
     assert(fixture::parries==queued+1);                                     // one parry per swing
     // Attacks on another player, and Classic, are not counted or parried.
     prime();guard.state.snapAt=guard.at;guard.state.snapUsed=false;
     const unsigned before=incomingMisses;
     player.Put(8,43u);IncomingRoll(monster.Address(),player.Address(),false);player.Put(8,42u);
     ConfigureGuard(false,450);IncomingRoll(monster.Address(),player.Address(),false);ConfigureGuard(true,450,true);
     assert(incomingMisses==before&&fixture::parries==queued+1&&!guard.state.snapUsed);}
    // Every monster to-hit roll marks its attack as too late to stagger early.
    {const auto marked=fixture::rolled;IncomingRoll(monster.Address(),player.Address(),false);assert(fixture::rolled==marked+1);}
    // A parry resolves as soon as the swing and the monster's attack have both
    // begun (2026-10-18: hit-frame parries came a median 516 ms after the
    // swing). Judging spends nothing; ConfirmParry, after the stagger, spends
    // the swing once. A swing that began too long before the attack does not.
    ConfigureGuard(true,450,true);prime();
    {assert(!ParryArmed());
     const auto now=GetTickCount64();guard.state.snapAt=now;guard.state.snapUsed=false;
     assert(ParryArmed());
     assert(!EarlyParry(monster.Address(),player.Address(),now+GuardTuning().parryLeadMs+1,0,"attack_start"));
     const uint64_t swing=EarlyParry(monster.Address(),player.Address(),now,0,"attack_start");
     assert(swing==now&&!guard.state.snapUsed);
     assert(fixture::log.find("Frame guard early parry: monster=9 class=0 trigger=attack_start")!=std::string::npos);
     const auto taps=fixture::impacts;const unsigned early=earlyParries,total=guardParries;
     ConfirmParry(swing);
     assert(guard.state.snapUsed&&fixture::impacts==taps+1&&earlyParries==early+1&&guardParries==total+1&&!ParryArmed());
     assert(!EarlyParry(monster.Address(),player.Address(),now,0,"swing"));
     // A swing still in the attacker's wind-up, before its downstroke opens,
     // does not parry; one at the downstroke does (2026-10-19).
     guard.state.snapUsed=false;
     assert(!EarlyParry(monster.Address(),player.Address(),now,now+1,"swing")&&!guard.state.snapUsed);
     assert(EarlyParry(monster.Address(),player.Address(),now-200,now,"swing")==now);
     guard.state.snapUsed=true;
     // Classic, or a lowered guard, never parries early.
     guard.state.snapUsed=false;ConfigureGuard(false,450);
     assert(!ParryArmed()&&!EarlyParry(monster.Address(),player.Address(),now,0,"swing"));
     ConfigureGuard(true,450,true);guard.state.raised=false;
     assert(!EarlyParry(monster.Address(),player.Address(),now,0,"swing"));}
    // A newly armed swing asks the game thread to look for an attack under way.
    {prime();const auto scans=fixture::swingScans;
     framevr::guard::Pose from{{.18f,1.22f,-.30f},{0,0,-1},{0,1.7f,0},{.18f,-.48f,-.30f},2,GetTickCount64(),1};
     for(int i=0;i<3;++i){from.at=GetTickCount64();PublishGuard(fixture::session,true,true,from,true,true);}
     const auto start=GetTickCount64();
     while(GetTickCount64()-start<40){}                                     // a real swing: 0.4 m in ~40 ms
     framevr::guard::Pose to{{-.20f,1.64f,-.32f},{0,0,-1},{0,1.7f,0},{-.20f,-.06f,-.32f},2,GetTickCount64(),1};
     PublishGuard(fixture::session,true,true,to,true,true);
     assert(guard.state.snapAt&&fixture::swingScans==scans+1);
     PublishGuard(fixture::session,true,true,to,true,true);assert(fixture::swingScans==scans+1);}
    // A snap turn invalidates the previous world frame until a new pose lands.
    prime();auto changed=guard.state.surface;changed.epoch++;
    PublishGuard(fixture::session,true,false,changed,true,true);assert(block()==0);
    changed.at=GetTickCount64();PublishGuard(fixture::session,true,true,changed,true,true);assert(block()==1);
    PublishGuard(fixture::session,false,false,changed,false,false);assert(!ReadGuardDebug(debug));
    std::puts("Physical shield adapter: walking/running guards, rejection reasons, native dispatch, Classic and eligibility checks passed");
}
