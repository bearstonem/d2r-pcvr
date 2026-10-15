// Native spell adapter. Cast calls run on the game thread; visual cleanup runs on UI.
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include "spells.h"
#include "bow.h"
#include "spell_signatures.h"
#include "bow_signatures.h"
#include "native_ui_signatures.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include "../steamvr/transport.h"
namespace spellvr {
namespace {
using namespace framevr::bowflight;
using namespace native;
std::atomic<bool> ready{false};
bool castSoundReady=false;
std::atomic<bool> feedbackReady{false};
std::atomic<unsigned> casts[2]{},denied{};
std::atomic<unsigned> warnings{};
framevr::spellcast::Cadence cadence;
std::mutex feedbackMutex;
framevr::spellcast::CastWarning castWarning;
using TargetFn=int(*)(uintptr_t,uintptr_t,int*,int*);
TargetFn originalTarget=nullptr;
int Target(uintptr_t game,uintptr_t player,int* x,int* y) {
    if(bowvr::SpellTarget(game,player,x,y))return 1;
    return originalTarget(game,player,x,y);
}
template<class T> bool Read(uintptr_t p,T& value) {
    return p>0x10000&&framevr::ReadMemory(&value,reinterpret_cast<const void*>(p),sizeof value);
}
struct Selection {
    uintptr_t address=0,previous=0;
    bool changed=false;
    Selection(uintptr_t player,uintptr_t skill) {
        uintptr_t list=0;
        if(Read(player+0x100,list)&&Read(list+0x18,previous)) {
            address=list+0x18;
            changed=framevr::WriteMemory(reinterpret_cast<void*>(address),&skill,sizeof skill);
        }
    }
    ~Selection(){if(changed)framevr::WriteMemory(reinterpret_cast<void*>(address),&previous,sizeof previous);}
};
uintptr_t Skill(uintptr_t player,const Shot& shot) {
    return reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(nativeui::abi::SkillRva))(player,shot.skill,shot.owner);
}
bool CheckFeedback() {
#define CHECK(name) if(!d2rsig::Check(name##Rva,name##Sig,sizeof name##Sig))return false
    CHECK(ManaCheck);CHECK(ManaCheckCall);CHECK(ManaCost);
    CHECK(TownCheck);CHECK(TownRestriction);CHECK(TownFlag);CHECK(VoiceTownEvent);CHECK(VoiceTownCase);
    if(!d2rsig::Check(bowvr::native::RoomRva,bowvr::native::RoomSig,sizeof bowvr::native::RoomSig))return false;
    CHECK(Voice);CHECK(VoiceClass);CHECK(VoiceManaEvent);CHECK(VoiceManaCase);CHECK(VoiceCooldown);
#undef CHECK
    return true;
}
}
bool Ready(){return ready.load();}
void Install() {
    // Optional feedback must not disable independent casting if a new game
    // build changes its speech code. Retry while startup pages decrypt.
    if(!feedbackReady.load()&&CheckFeedback()) {
        feedbackReady=true;
        framevr::logging::Write("Frame hand spells: native mana and town-restriction voice feedback ready");
    }
    if(ready.load())return;
#define CHECK(name) if(!d2rsig::Check(name##Rva,name##Sig,sizeof name##Sig))return
    CHECK(Start);CHECK(Dispatch);CHECK(UsedLayout);CHECK(SkillLevel);CHECK(Relocate);CHECK(RelocateCall);
    CHECK(ClientHit);CHECK(ClientEnd);CHECK(PathMove);
    CHECK(ChargedCast);CHECK(ChargedLoop);CHECK(Target);
#undef CHECK
    if(!d2rsig::Check(nativeui::abi::SkillRva,nativeui::abi::SkillSig,sizeof nativeui::abi::SkillSig)||
       !d2rsig::Check(nativeui::abi::SkillListRva,nativeui::abi::SkillListSig,sizeof nativeui::abi::SkillListSig)||
       !d2rsig::Check(nativeui::abi::SkillLayoutRva,nativeui::abi::SkillLayoutSig,sizeof nativeui::abi::SkillLayoutSig)||
       !d2rsig::Check(nativeui::abi::StatRva,nativeui::abi::StatSig,sizeof nativeui::abi::StatSig))return;
    if(!originalTarget&&!d2rsig::Hook(TargetRva,TargetSig,sizeof TargetSig,reinterpret_cast<void*>(&Target),reinterpret_cast<void**>(&originalTarget)))return;
    castSoundReady=d2rsig::Check(CastSoundRva,CastSoundSig,sizeof CastSoundSig);
    ready=true;
    framevr::logging::Write("Frame hand spells: independent planar Sorceress bolts/balls and native Charged Bolt volleys ready");
}
bool Prepare(Shot& shot,uintptr_t game,uintptr_t player) {
    if(!ready.load()||!framevr::SpellHeld(shot))return false;
    uint32_t character=~0u,tick=0,mode=0;uintptr_t list=0;
    if(!Read(player+0xc,mode)||(mode!=1&&mode!=2&&mode!=3))return false;
    if(!Read(player+4,character)||!Read(player+0x100,list)||!list||!Read(game+0x170,tick)||
       !cadence.Ready(shot.session,shot.hand,tick)||shot.owner!=-1)return false;
    const auto node=Skill(player,shot);uintptr_t row=0;
    int16_t id=-1,start=-1,action=-1,missile=-1,secondary=-1;uint32_t flags=0;uint8_t los=255;
    if(!node||!Read(node,row)||!Read(row,id)||id!=shot.skill||!Read(row+0x4c,start)||
       !Read(row+0x4e,action)||!Read(row+0x66,missile)||!Read(row+0x68,secondary)||!Read(row+0x24,flags)||
       !Read(row+0x231,los)||los!=0||
       !framevr::spellcast::Candidate(character,id,start,action,missile,flags,secondary))return false;
    if(framevr::spellcast::NativePath(id)) {
        uintptr_t function=0;
        if(!Read(d2rsig::Addr(0x238ea00+17*8),function)||function!=d2rsig::Addr(ChargedCastRva))return false;
        missile=secondary;
    }
    shot.level=reinterpret_cast<int(*)(uintptr_t,uintptr_t,int,int)>(d2rsig::Addr(SkillLevelRva))(player,node,1,0);
    shot.missile=missile;
    int16_t sound=0;
    shot.castSound=castSoundReady&&Read(row+0x152,sound)&&sound>0?sound:0;
    return shot.level>0;
}
bool Cast(const Shot& shot,uintptr_t game,uintptr_t player) {
    const auto node=Skill(player,shot);if(!node)return false;
    // Temporarily supply THIS hand's skill to the game's own mana checks and
    // cost dispatcher. Restore before another action on this game thread;
    // never change the player's left/right assignments, animation or path.
    Selection selected(player,node);if(!selected.changed)return false;
    using StartFn=int(*)(uintptr_t,uintptr_t,int,int,int);
    const auto result=framevr::spellcast::StartWithFeedback([&]{
        using framevr::spellcast::Failure;
        if(!feedbackReady.load())return Failure::None;
        uintptr_t row=0;uint8_t flags=0;
        if(!Read(node,row)||!Read(row+0x25,flags))return Failure::None;
        const auto room=reinterpret_cast<uintptr_t(*)(uintptr_t)>(d2rsig::Addr(bowvr::native::RoomRva))(player);
        if(!room)return Failure::None;
        const bool restricted=!(flags&1)&&reinterpret_cast<int(*)(uintptr_t)>(d2rsig::Addr(TownCheckRva))(room)!=0;
        const bool enoughMana=reinterpret_cast<int(*)(uintptr_t)>(d2rsig::Addr(ManaCheckRva))(player)!=0;
        return framevr::spellcast::Refusal(restricted,enoughMana);
    },[&]{return reinterpret_cast<StartFn>(d2rsig::Addr(StartRva))(game,player,shot.skill,shot.level,0)!=0;});
    if(!result.accepted){
        ++denied;
        if(result.failure!=framevr::spellcast::Failure::None) {
            uint32_t id=0;
            if(Read(player+8,id)) {
                std::lock_guard lock(feedbackMutex);
                castWarning.Request(shot.session,id,GetTickCount64(),result.failure);
            }
        }
        return false;
    }
    // Correct the scoped launch direction once, after native acceptance.
    // Empty-mana/town refusals do not perform repeated target scans.
    bowvr::AssistSpell(game,player);
    using DispatchFn=int(*)(uintptr_t,uintptr_t,int,int,int,int,int);
    return reinterpret_cast<DispatchFn>(d2rsig::Addr(DispatchRva))(game,player,shot.skill,shot.level,1,0,0)!=0;
}
void Feedback(uint64_t session,uintptr_t player,uint32_t id,bool active) {
    auto reason=framevr::spellcast::Failure::None;
    {
        std::lock_guard lock(feedbackMutex);
        reason=castWarning.Take(session,id,GetTickCount64(),active&&feedbackReady.load());
    }
    if(reason==framevr::spellcast::Failure::None)return;
    // Events 22/25 are native mana/town responses. The game chooses
    // the class/localized voice and enforces its existing speech cooldown.
    // Only a client player is used here; no server pointer crosses threads.
    reinterpret_cast<void(*)(uintptr_t,uint16_t)>(d2rsig::Addr(VoiceRva))(player,uint16_t(reason));
    ++warnings;
}
void Fired(const Shot& shot,uintptr_t game,uintptr_t player) {
    ++casts[shot.hand];
    framevr::SpellVisualCast(shot);
    uint32_t tick=0;if(!Read(game+0x170,tick))return;
    const int fcr=reinterpret_cast<int(*)(uintptr_t,int,int)>(d2rsig::Addr(nativeui::abi::StatRva))(player,105,0);
    cadence.Fired(shot.session,shot.hand,tick,framevr::spellcast::CastFrames(fcr));
}
void Report() {
    const auto right=casts[0].exchange(0),left=casts[1].exchange(0),refused=denied.exchange(0);
    const auto spoken=warnings.exchange(0);
    if(!right&&!left&&!refused&&!spoken)return;
    char line[192];snprintf(line,sizeof line,"Frame hand spells: right=%u left=%u native start refused=%u (mana/town/native restrictions) cast refusal voice requests=%u",right,left,refused,spoken);
    framevr::logging::Write(line);
}
bool Position(uintptr_t game,uintptr_t missile,uintptr_t room,int x,int y) {
    if(!ready.load()||!room||x<1||y<1||x>65534||y>65534)return false;
    // Native relocation updates path, room membership and unit refresh as a
    // transaction. Never write only XY or temporarily swap the room pointer:
    // Fireball's splash and child effects query both at impact.
    using RelocateFn=int(*)(uintptr_t,uintptr_t,uintptr_t,int,int,int,int);
    return reinterpret_cast<RelocateFn>(d2rsig::Addr(RelocateRva))(game,missile,room,x,y,1,0)!=0;
}
bool FinishVisual(uintptr_t client,const Flight& flight) {
    if(!ready.load())return false;
    uintptr_t path=0;
    if(!Read(client+0x38,path)||!path)return false;
    const auto point=GridPoint(flight.shot,flight.Point())*flight.shot.unitsPerMetre;
    const int x=int(point.x*.5f),y=int(point.z*.5f);
    auto room=reinterpret_cast<uintptr_t(*)(uintptr_t)>(d2rsig::Addr(bowvr::native::RoomRva))(client);
    room=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(bowvr::native::RoomPointRva))(room,x,y);
    if(!room||x<1||y<1||x>65534||y>65534)return false;
    using MoveFn=int(*)(uintptr_t,uintptr_t,uintptr_t,int,int);
    if(!reinterpret_cast<MoveFn>(d2rsig::Addr(PathMoveRva))(path,client,room,x,y))return false;
    // UI-only hit effects and cleanup, after the server decided the impact.
    // This does not invoke damage or consume mana a second time.
    reinterpret_cast<bool(*)(uintptr_t,uintptr_t,int)>(d2rsig::Addr(ClientHitRva))(client,0,0);
    reinterpret_cast<void(*)(uintptr_t)>(d2rsig::Addr(ClientEndRva))(client);
    return true;
}
}
