#include <windows.h>
#include <D2RLPlugin/api.h>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include "interactionstate.h"
#include "logging.h"
#include "native_ui.h"
#include "native_belt.h"
#include "dev_scenarios.h"
#include "melee.h"
#include "bow.h"
#include "companions.h"
#include "active_effects.h"
namespace gamestate {uint32_t WeaponSet();uint64_t LocalPlayer();bool MenuOpen();}
namespace gamecmd {bool ExecuteUiCommand(uint32_t);}
namespace framevr {bool VrGameplay(uint64_t now);}
namespace interactionstate {
namespace {
using namespace D2RL;
using namespace framevr::interaction;
const PluginContext* context=nullptr;
const InventoryService* inventory=nullptr;
const ItemService* items=nullptr;
const ThreadService* threads=nullptr;
std::mutex mutex;
Snapshot snapshot;
Reply reply;
framevr::interaction::Request queued,waiting;
bool tossHit=false; // FeedMerc: the game thread found the hireling at the landing point
// Hireling vitals: sampled on the game thread every MercPeriod ms, copied into
// each snapshot while younger than MercMaxAge.
constexpr uint64_t MercPeriod=200,MercMaxAge=1000;
Mercenary merc;uint64_t mercAt=0,mercAskedAt=0,mercSession=0;bool mercAsking=false,mercWasLow=false;
framevr::companions::Roster companions;
framevr::effects::Status effects;
// Native cosmetic cues (states.txt / overlay.txt rows): the hurt merc is tinted
// with state "red"; a confirmed thrown potion plays overlay "healing" on it.
constexpr int HurtTintState=91,HealOverlay=26;
std::atomic<uint32_t> tintedUnit{0}; // written on the game thread: hireling currently tinted by us
// sounds.txt object_glass_break_1: a 3-sound group that redirects to its HD
// group, so the game picks the variant and the HD/legacy version itself.
constexpr int GlassBreak=2587;
uint64_t issuedAt=0,cancelEpoch=0;
bool scheduled=false;
template<class T>const T* Service(const PluginContext* c) {const T* p=nullptr;return c->QueryService(&p)==ServiceQueryResult::Success?p:nullptr;}
Item Copy(const Items::ItemInfo& i){return {i.handle,i.runtimeId,i.code,uint32_t(i.quality),i.qualityRecordId};}
void __cdecl PlayHeal(const PluginContext*,void* unit) noexcept {
    const bool played=melee::HirelingOverlay(uint32_t(reinterpret_cast<uintptr_t>(unit)),HealOverlay);
    framevr::logging::Write(played?"Frame potion toss: native healing overlay played on the mercenary":
        "Frame potion toss: healing overlay unavailable (mercenary gone or code mismatch)");
}
struct BeltRead {Snapshot* result;bool invalid=false;};
Inventory::IterationAction __cdecl BeltItem(const PluginContext*,const Items::ItemInfo* i,void* user) noexcept {
    auto& read=*static_cast<BeltRead*>(user);
    if(!i||i->container!=Items::ItemContainer::Belt)return Inventory::IterationAction::Continue;
    int column=-1,row=-1;
    // Both SDK coordinate representations agree for the bottom-row item.
    if(i->x>=0&&i->x<16&&i->y==0){column=i->x%4;row=i->x/4;}
    else if(i->x>=0&&i->x<4&&i->y>=0&&i->y<4){column=i->x;row=i->y;}
    if(column<0||row<0||!i->handle){read.invalid=true;return Inventory::IterationAction::Continue;}
    auto& b=read.result->belt[column];
    if(b.items[row].handle){read.invalid=true;return Inventory::IterationAction::Continue;}
    b.items[row]=Copy(*i);++b.count;
    return Inventory::IterationAction::Continue;
}
Snapshot Collect() {
    Snapshot s;s.at=GetTickCount64();
    PlayerHandle player=InvalidPlayerHandle;
    if(!inventory||!items||inventory->getLocalPlayer(context,&player)!=Inventory::Result::Success||
       !player||player!=gamestate::LocalPlayer())return s;
    s.session=player;s.activeSet=gamestate::WeaponSet();
    s.equipmentKnown=s.activeSet==1||s.activeSet==2;
    for(int i=0;i<4;++i) {
        ItemHandle handle=InvalidItemHandle;
        const auto result=inventory->getEquippedItem(context,player,i<2?4+i:9+i,&handle);
        if(result!=Inventory::Result::Success&&result!=Inventory::Result::NotFound){s.equipmentKnown=false;continue;}
        if(!handle)continue;
        Items::ItemInfo item{};item.structSize=sizeof item;
        if(items->getItemInfo(context,handle,&item)!=Items::Result::Success){s.equipmentKnown=false;continue;}
        if(s.activeSet==1||s.activeSet==2)s.sets[(i/2)^(s.activeSet-1)][i%2]=Copy(item);
    }
    Inventory::ItemFilter filter{sizeof filter,0,Items::ContainerBit(Items::ItemContainer::Belt),0};
    BeltRead belt{&s};
    s.beltKnown=inventory->forEachInventoryItem(context,player,&filter,&BeltItem,&belt)==Inventory::Result::Success&&!belt.invalid;
    for(auto& b:s.belt)b.known=s.beltKnown;
    nativeui::Capture(s);
    devscenarios::Capture(s);
    {std::lock_guard lock(mutex);if(mercSession==s.session&&s.at>=mercAt&&s.at-mercAt<=MercMaxAge){s.merc=merc;s.companions=companions;s.effects=effects;}}
    if(s.companions.known)for(unsigned i=0;i<s.skillCount&&i<s.catalog.size();++i)
        if(s.catalog[i].known&&s.catalog[i].available)
            s.companions.relevant|=framevr::companions::Bit(framevr::companions::SkillType(s.catalog[i].id));
    return s;
}
void __cdecl SampleMerc(const PluginContext*,void* token) noexcept {
    const uint64_t session=reinterpret_cast<uint64_t>(token);
    static uint32_t hint=0;
    Mercenary m;
    const auto roster=companionstate::Sample(session);
    const auto active=effectstate::Sample(session);
    m.known=session==gamestate::LocalPlayer()&&melee::HirelingVitals(hint,m.health,m.maxHealth,m.classId,m.unitId);
    hint=m.known?m.unitId:0;
    const bool low=MercLow(m);
    // Tint only while low; clear it on recovery, death (the corpse too) or
    // when another unit is the hireling (resurrected/rehired).
    if(const auto tinted=tintedUnit.load();tinted&&(!low||tinted!=m.unitId)){melee::HirelingState(tinted,HurtTintState,false);tintedUnit=0;}
    if(low&&!tintedUnit.load()&&melee::HirelingState(m.unitId,HurtTintState,true))tintedUnit=m.unitId;
    {std::lock_guard lock(mutex);merc=m;companions=roster;effects=active;mercAt=GetTickCount64();mercSession=session;mercAsking=false;
     if(low==mercWasLow)return;mercWasLow=low;}
    char line[180];std::snprintf(line,sizeof line,"Frame merc: %s (class=%u life %.0f/%.0f, red tint %s)",
        low?"low health":"health recovered",m.classId,m.health,m.maxHealth,tintedUnit.load()?"on":"off");
    framevr::logging::Write(line);
}
// Leaving VR (desktop/flat play, motion controls off, headset removed):
// remove our tint so non-VR play never shows it.
void __cdecl ClearTint(const PluginContext*,void*) noexcept {
    if(const auto tinted=tintedUnit.exchange(0)) {
        melee::HirelingState(tinted,HurtTintState,false);
        framevr::logging::Write("Frame merc: VR gameplay inactive - red tint cleared");
    }
    std::lock_guard lock(mutex);mercAsking=false;mercWasLow=false;companions={};effects={};
}
void Publish(Snapshot s) {
    std::lock_guard lock(mutex);
    s.revision=snapshot.revision;
    bool changed=s.developer!=snapshot.developer||s.devClass!=snapshot.devClass||s.session!=snapshot.session||s.activeSet!=snapshot.activeSet||s.equipmentKnown!=snapshot.equipmentKnown||
                 s.sets!=snapshot.sets||s.beltKnown!=snapshot.beltKnown||s.skills!=snapshot.skills||
                 s.catalog!=snapshot.catalog||s.skillCount!=snapshot.skillCount||s.bindingBank!=snapshot.bindingBank||s.bindingsEditable!=snapshot.bindingsEditable||
                 s.vitalsKnown!=snapshot.vitalsKnown||int(s.health)!=int(snapshot.health)||int(s.mana)!=int(snapshot.mana)||
                 s.maxHealth!=snapshot.maxHealth||s.maxMana!=snapshot.maxMana||s.experience!=snapshot.experience||s.location!=snapshot.location||
                 s.actionKnown!=snapshot.actionKnown||s.actionMode!=snapshot.actionMode||s.companions!=snapshot.companions||s.effects!=snapshot.effects;
    for(int i=0;i<4;++i) {
        auto& b=s.belt[i];const auto& before=snapshot.belt[i];
        const bool change=s.session!=snapshot.session||b.items!=before.items||b.known!=before.known||b.count!=before.count;
        b.revision=before.revision+(change?1:0);changed|=change;
    }
    if(changed)++s.revision;
    snapshot=s;
    if(waiting.action!=Action::None) {
        Result result=Result::Pending;
        if(waiting.session!=s.session)result=Result::Cancelled;
        else if((waiting.action==Action::Potion||waiting.action==Action::FeedMerc)&&PotionGone(s,waiting))result=Result::Applied;
        else if(waiting.action==Action::Equip&&s.equipmentKnown&&s.activeSet==uint32_t(waiting.index))result=Result::Applied;
        else if(s.at-issuedAt>1000)result=Result::Refused;
        reply={waiting.sequence,waiting.session,result};
        if(waiting.action==Action::Potion&&result==Result::Applied)
            framevr::logging::Write("Frame potion sip: native belt item consumption confirmed");
        else if(waiting.action==Action::Potion&&result==Result::Refused)
            framevr::logging::Write("Frame potion sip: native command did not consume the held belt item");
        else if(waiting.action==Action::FeedMerc&&result==Result::Applied) {
            framevr::logging::Write("Frame potion toss: native belt item given to the mercenary (consumption confirmed)");
            // The game's own healing overlay on the merc, with its "thanks".
            if(threads&&threads->runOnGameThread&&merc.unitId)
                threads->runOnGameThread(context,&PlayHeal,reinterpret_cast<void*>(uintptr_t(merc.unitId)));
        }
        else if(waiting.action==Action::FeedMerc&&result==Result::Refused)
            framevr::logging::Write("Frame potion toss: native mercenary belt use did not consume the thrown item");
        if(result!=Result::Pending)waiting={};
    }
}
void __cdecl Apply(const PluginContext*,void*) noexcept {
    framevr::interaction::Request r;uint64_t epoch;
    bool hit=false;
    {std::lock_guard lock(mutex);scheduled=false;r=queued;queued={};epoch=cancelEpoch;hit=tossHit;tossHit=false;}
    if(r.action==Action::None)return;
    if(r.action==Action::FeedMerc) {
        // Every landing shatters; only a landing at the hireling's feet feeds.
        if(!bowvr::PlayHeroSound(GlassBreak))
            framevr::logging::Write("Frame potion toss: native glass sound unavailable");
        if(!hit){std::lock_guard lock(mutex);reply={r.sequence,r.session,Result::Refused};return;}
    }
    Publish(Collect());
    const auto s=Read();const auto now=GetTickCount64();
    bool valid=Fresh(s,r.session,now)&&now>=r.at&&now-r.at<250&&!gamestate::MenuOpen();
    if(r.action==Action::BindSkill) {
        {
            std::lock_guard lock(mutex);
            const bool accepted=valid&&epoch==cancelEpoch&&nativeui::Assign(r,s);
            reply={r.sequence,r.session,accepted?Result::Applied:Result::Refused};
        }
        Publish(Collect()); // refresh wheel and any duplicates cleared by D2R
        return;
    }
    uint32_t command=0;
    switch(r.action) {
        case Action::Potion:{auto current=r;current.at=now;valid=valid&&SameBelt(s,current);command=0x17+r.index;break;}
        case Action::FeedMerc:{auto current=r;current.at=now;valid=valid&&SameBelt(s,current);command=0x17+r.index;break;}
        case Action::Equip:valid=valid&&s.equipmentKnown&&(r.index==1||r.index==2);command=0x2c;break;
        case Action::Quest:command=0x04;break;
        case Action::Map:command=0x0b;break;
        case Action::Character:command=0x03;break;
        case Action::Skills:command=0x0c;break;
        default:valid=false;break;
    }
    std::lock_guard lock(mutex);
    if(epoch!=cancelEpoch)valid=false;
    const bool already=r.action==Action::Equip&&s.equipmentKnown&&s.activeSet==uint32_t(r.index);
    const bool accepted=valid&&(already||(r.action==Action::Potion?nativebelt::Drink(r.index,r.session):
        r.action==Action::FeedMerc?nativebelt::FeedMerc(r.index,r.session):gamecmd::ExecuteUiCommand(command)));
    if(accepted&&(r.action==Action::Potion||r.action==Action::FeedMerc||r.action==Action::Equip)&&!already) {
        waiting=r;issuedAt=now;reply={r.sequence,r.session,Result::Pending};
    } else reply={r.sequence,r.session,accepted?Result::Applied:Result::Refused};
    framevr::logging::Write(accepted?"Frame interactions: native action accepted; item/set changes require confirmation":"Frame interactions: stale or refused native action discarded");
}
// Game thread: did the thrown bottle land at the local hireling's feet? Every
// landing continues to the UI thread, which plays the shatter; only a hit then
// uses the belt item on the mercenary. A miss leaves the belt untouched.
void __cdecl AimAtHireling(const PluginContext*,void*) noexcept {
    framevr::interaction::Request r;
    {std::lock_guard lock(mutex);r=queued;if(r.action!=Action::FeedMerc){scheduled=false;return;}} // cancelled
    float distance=-1;uint32_t classId=0;
    const bool hit=r.session==gamestate::LocalPlayer()&&
        melee::NearHireling(r.world,r.player,r.unitsPerMetre,distance,classId);
    char line[220];
    if(distance<0)std::snprintf(line,sizeof line,"Frame potion toss: no live hireling nearby (nearest friendly class=%u; column=%d type=%s)",
        classId,r.index+1,PotionName(PotionType(r.item.code)));
    else std::snprintf(line,sizeof line,"Frame potion toss: landed %.2f m from the mercenary (class=%u) - %s (column=%d type=%s)",
        distance,classId,hit?"at its feet":"miss",r.index+1,PotionName(PotionType(r.item.code)));
    framevr::logging::Write(line);
    {std::lock_guard lock(mutex);tossHit=hit;}
    if(threads->runOnUiThread(context,&Apply,nullptr)==D2RL::Threads::Result::Success)return;
    // No UI callback follows, so release the slot even if Cancel() ran meanwhile;
    // Request() admits nothing new while it is scheduled.
    std::lock_guard lock(mutex);scheduled=false;tossHit=false;
    if(queued.sequence==r.sequence){queued={};reply={r.sequence,r.session,Result::Refused};}
}
}
void Init(const D2RL::PluginContext* ctx) {
    devscenarios::Init(ctx);
    companionstate::Init(ctx);
    effectstate::Init(ctx);
    nativeui::Init(ctx);
    context=ctx;inventory=Service<D2RL::InventoryService>(ctx);items=Service<D2RL::ItemService>(ctx);threads=Service<D2RL::ThreadService>(ctx);
}
void Capture(){
    Publish(Collect());
    const auto now=GetTickCount64();const auto session=gamestate::LocalPlayer();
    // Merc cues are VR-only: no sampling or tint in desktop/flat play.
    const bool vr=framevr::VrGameplay(now);
    {std::lock_guard lock(mutex);
     // A dropped game-thread callback (loading, unload) must not stall sampling.
     if(!threads||!threads->runOnGameThread||!session||now-mercAskedAt<(mercAsking?2000:MercPeriod))return;
     if(!vr&&!tintedUnit.load())return;
     mercAsking=true;mercAskedAt=now;}
    if(threads->runOnGameThread(context,vr?&SampleMerc:&ClearTint,reinterpret_cast<void*>(session))!=D2RL::Threads::Result::Success)
        {std::lock_guard lock(mutex);mercAsking=false;}
}
framevr::interaction::Snapshot Read(){std::lock_guard lock(mutex);return snapshot;}
framevr::interaction::Reply Status(){const auto dev=devscenarios::Status();if(dev.sequence)return dev;std::lock_guard lock(mutex);return reply;}
void Request(const framevr::interaction::Request& r) {
    if(r.action==Action::Developer){devscenarios::Request(r);return;}
    devscenarios::Cancel();
    if(!threads||!threads->runOnUiThread||r.action==Action::None)return;
    {std::lock_guard lock(mutex);
     if(queued.action!=Action::None||waiting.action!=Action::None||scheduled)return;
     queued=r;scheduled=true;reply={r.sequence,r.session,Result::Pending};}
    if(r.action==Action::Potion) {
        char line[176];std::snprintf(line,sizeof line,"Frame potion sip: gesture completed; native belt command queued (column=%d type=%s)",
            r.index+1, PotionName(PotionType(r.item.code)));
        framevr::logging::Write(line);
    }
    // A thrown bottle is first checked against the hireling on the game thread.
    const bool toss=r.action==Action::FeedMerc;
    const auto run=toss?threads->runOnGameThread:threads->runOnUiThread;
    if(!run||run(context,toss?&AimAtHireling:&Apply,nullptr)!=D2RL::Threads::Result::Success) {
        std::lock_guard lock(mutex);queued={};scheduled=false;reply={r.sequence,r.session,Result::Refused};
    }
}
void Cancel(){devscenarios::Cancel();std::lock_guard lock(mutex);++cancelEpoch;queued={};waiting={};reply={};}
}
