#include "../vr/combat.h"
#include <windows.h>
#include <atomic>
#include <cassert>
#include <cstdint>

namespace {std::atomic<uint64_t> session{1};}
namespace gamestate {uint64_t LocalPlayer(){return session.load();}}
// The fixture exercises the real combat identity readers without installing
// hooks or opening a game. Native accessors are represented by owned unit data.
namespace d2rsig {
uint64_t Rva(uint64_t rva){return rva;}
uintptr_t Addr(uint64_t){return 0;}
bool Check(uint64_t,const void*,uint32_t){return false;}
bool Hook(uint64_t,const void*,uint32_t,void*,void**){return false;}
}
namespace framevr::logging {void Write(const char*){}}
// Physical guard outputs (transport haptics, melee parry queue): not reached here.
namespace framevr {void ShieldParryMiss(){}bool GuardBroken(){return false;}void ShieldGuardImpact(bool,float){}void ShieldGuardState(bool,bool){}}
namespace melee {bool QueueParry(uint32_t,int){return false;}uint64_t AttackStart(uint32_t){return 0;}uint64_t ParryOpens(uint32_t){return 0;}void QueueBlockSound(){}void QueueClientDurability(uint32_t,int){}void NoteAttackRolled(uint32_t){}void QueueParrySwing(){}}
namespace gamestate {int32_t LeftDurability(){return -1;}}
int main() {
    uint32_t outdoors[]={0,3,0},cave[]={0,3,0},monster[]={1,3,0};
    uintptr_t unit=0;uint32_t id=99;
    assert(!combat::LocalPlayerUnit(unit));
    assert(combat::ObserveUiPlayer(reinterpret_cast<uintptr_t>(outdoors),1));
    assert(combat::LocalPlayerId(id)&&id==0); // native id zero is valid
    assert(combat::LocalPlayerUnit(unit)&&unit==reinterpret_cast<uintptr_t>(outdoors));
    // Area transition: aim hook stops; a later UI update resolves the new unit
    // with the same id. No camera or old native pointer may gate the update.
    assert(combat::ObserveUiPlayer(reinterpret_cast<uintptr_t>(cave),1));
    assert(combat::LocalPlayerUnit(unit)&&unit==reinterpret_cast<uintptr_t>(cave));
    assert(!combat::ObserveUiPlayer(reinterpret_cast<uintptr_t>(monster),1));
    cave[2]=42;assert(!combat::LocalPlayerUnit(unit)); // reused native address
    cave[2]=0;session=2;
    assert(!combat::LocalPlayerUnit(unit)&&!combat::LocalPlayerId(id));
    assert(!combat::ObserveUiPlayer(reinterpret_cast<uintptr_t>(cave),1));
    assert(combat::ObserveUiPlayer(reinterpret_cast<uintptr_t>(cave),2));
    Sleep(700);assert(!combat::LocalPlayerUnit(unit)); // stopped UI reader
    assert(!combat::ObserveUiPlayer(0,2));
}
