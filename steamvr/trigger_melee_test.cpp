// Exercise the real hit check (vr/combat.cpp Hit) for Unleashed trigger/button melee:
// the local player's melee hit check outside a physical swing is a stroke like a swing.
#include "../vr/combat.cpp"
#undef NDEBUG
#include <cassert>

namespace fixture {
uintptr_t imageBase=0x140000000;
uint64_t session=1;
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
// What melee::ButtonHit answers (the swing clocks live in melee.cpp), and what it was asked.
melee::ButtonStroke stroke;unsigned strokes=0;uint32_t strokeMonster=0;
// The native hit check as the hooks see it: its seed (GetSeed at the hit roll gives the
// game's own roll) and then 427EA0 with the clamped chance. Forced: it always hits.
int roll=10,chance=50;
uint64_t NativeHitChance(uintptr_t,uintptr_t,uintptr_t,int,int){return 0;}
int NativeHit(uintptr_t attacker,uintptr_t defender,int,int) {
    if(combat::forcedCheck)combat::nativeHitRoll=roll;
    combat::HitChance(123,attacker,defender,chance,roll);
    return 1;
}
// Two of the gate's roll sites as the hooks reach them: the item events and the damage application.
unsigned nativeEvents=0;
uint32_t NativeTrigger(uintptr_t,int,uintptr_t,uintptr_t,uintptr_t){++nativeEvents;return 1;}
uint64_t NativeApplyDamage(uintptr_t,uintptr_t,uintptr_t,int,uintptr_t){return 0;}
}
namespace gamestate {uint64_t LocalPlayer(){return fixture::session;}int32_t LeftDurability(){return -1;}}
namespace d2rsig {
uint64_t Rva(uint64_t rva){return rva;}
uintptr_t Addr(uint64_t rva){return rva?fixture::imageBase+rva:0;}
bool Check(uint64_t,const void*,uint32_t){return false;}
bool Hook(uint64_t,const void*,uint32_t,void*,void**){return false;}
}
namespace framevr::logging {void Write(const char*){}}
namespace framevr {void ShieldParryMiss(){}bool GuardBroken(){return false;}void ShieldGuardImpact(bool,float){}void ShieldGuardState(bool,bool){}}
namespace melee {bool QueueParry(uint32_t,int){return false;}uint64_t AttackStart(uint32_t){return 0;}uint64_t ParryOpens(uint32_t){return 0;}void QueueBlockSound(){}
void QueueClientDurability(uint32_t,int){}void NoteAttackRolled(uint32_t){}void QueueParrySwing(){}
ButtonStroke ButtonHit(uint32_t monster){++fixture::strokes;fixture::strokeMonster=monster;return fixture::stroke;}}

int main() {
    using namespace combat;
    const auto approx=[](double a,double b){return a>b-1e-6&&a<b+1e-6;};
    fixture::Unit player(0,42,100),monster(1,9,99),other(1,10,98);
    const uintptr_t p=player.Address(),m=monster.Address();
    enabled=ready=true;originalHit=&fixture::NativeHit;originalHitChance=&fixture::NativeHitChance;
    ConfigureButtonRules(true);ConfigureArDamage(true,0);
    const auto trigger=[&](melee::ButtonStroke s,int rollAt,uintptr_t target,int missile=0) {
        assert(ObserveUiPlayer(p,fixture::session));
        fixture::stroke=s;fixture::roll=rollAt;
        return Hit(p,target,0,missile);
    };
    // A recovered trigger hit the native roll would have landed: attack rating scales it, effects native.
    assert(trigger({1,true},10,m)==1&&fixture::strokes==1&&fixture::strokeMonster==9);
    assert(buttonHit.attacker==p&&buttonHit.defender==m&&buttonHit.chance==50&&buttonHit.ar&&buttonHit.recovery==1.f);
    assert(approx(ContactFactor(p,m),.5)&&!missScope.attacker&&!buttonOnHit.attacker&&!OnHitOff(p,m));
    assert(verdictButtonHit.exchange(0)==1&&verdictButtonMiss.load()==0);
    // A roll the game would have missed: it lands, but its effects go as the miss they would have been.
    assert(trigger({1,true},70,m)==1&&MissGated(p,m)&&verdictButtonMiss.exchange(0)==1);
    // Inside a swing's cycle: the recovery factor joins attack rating, and on-hit effects are off
    // for this attacker on this target only (leech, procs, crit, prevent heal, defense per hit).
    assert(trigger({.25f,false},10,m)==1&&approx(ContactFactor(p,m),.125));
    assert(OnHitOff(p,m)&&OnHitOffAttacker(p)&&OnHitOffTarget(m)&&!OnHitOff(p,other.Address())&&!OnHitOffTarget(other.Address()));
    {   // At the real sites: the attacker's melee procs (5/7) are skipped, its leech zeroed; other targets untouched.
        originalTrigger=&fixture::NativeTrigger;originalApplyDamage=&fixture::NativeApplyDamage;leechGate=true;
        const auto events=fixture::nativeEvents;
        assert(Trigger(123,DoMeleeDamage,p,m,0)==0&&Trigger(123,DoMeleeAttack,p,m,0)==0&&fixture::nativeEvents==events);
        assert(Trigger(123,DoMeleeDamage,p,other.Address(),0)==1&&fixture::nativeEvents==events+1);
        std::array<int32_t,0x140/4> record{};record[0x120/4]=7;record[0x124/4]=3;
        ApplyDamage(123,p,m,0,reinterpret_cast<uintptr_t>(record.data()));
        assert(record[0x120/4]==0&&record[0x124/4]==0);
        record[0x120/4]=7;ApplyDamage(123,p,other.Address(),0,reinterpret_cast<uintptr_t>(record.data()));
        assert(record[0x120/4]==7);
        // The gate is bounded to the trigger hit's own call chain (the would-hit gate's window).
        const auto at=buttonOnHit.at;buttonOnHit.at=at-IncomingWindowMs-20;
        assert(!OnHitOff(p,m)&&!OnHitOffAttacker(p)&&!OnHitOffTarget(m)&&Trigger(123,DoMeleeDamage,p,m,0)==1);
        buttonOnHit.at=at;
    }
    // The next attack of the local player replaces that gate.
    assert(trigger({1,true},10,m)==1&&!buttonOnHit.attacker&&!OnHitOff(p,m));
    // Smite always hits natively: no attack rating and no would-hit gate, only the shared recovery.
    verdictButtonHit=0;verdictButtonMiss=0;
    assert(trigger({.5f,true,true},70,m)==1&&!missScope.attacker&&!buttonHit.ar&&buttonHit.chance==-1);
    assert(approx(ContactFactor(p,m),.5)&&verdictButtonHit.load()==0&&verdictButtonMiss.load()==0);
    // A physical swing's own native attack is not a trigger hit, and it clears a trigger's leftovers.
    {
        const auto strokes=fixture::strokes;
        const auto was=PhysicalSwing(9);const auto scope=GateOnHit({});
        assert(!buttonHit.attacker&&!buttonOnHit.attacker);
        assert(trigger({.25f,false},10,m)==1&&fixture::strokes==strokes&&!buttonHit.attacker);
        GateOnHit(scope);PhysicalSwing(was);
    }
    // Arrows and throws are not trigger melee, and a trigger hit's unwritten damage record
    // (no life write, e.g. zero damage) never reaches the arrow's.
    {
        assert(trigger({.25f,false},10,m)==1&&buttonHit.attacker==p);
        const auto strokes=fixture::strokes;
        assert(trigger({.25f,false},10,m,1)==1&&fixture::strokes==strokes&&!buttonHit.attacker&&!buttonOnHit.attacker);
    }
    // Off: the old forced, unscaled hit.
    {
        ConfigureButtonRules(false);const auto strokes=fixture::strokes;
        assert(trigger({.25f,false},70,m)==1&&fixture::strokes==strokes&&!buttonHit.attacker&&ContactFactor(p,m)==1&&!missScope.attacker);
        ConfigureButtonRules(true);
    }
    std::puts("Trigger melee: attack rating, would-hit gate, shared recovery, on-hit gate, Smite and swing/missile/off exclusions passed");
}
