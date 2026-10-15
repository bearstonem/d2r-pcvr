#include <windows.h>
#include <D2RLPlugin/api.h>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include "interactionstate.h"
#include "logging.h"
#include "native_ui.h"
#include "native_belt.h"
namespace gamestate {uint32_t WeaponSet();uint64_t LocalPlayer();bool MenuOpen();}
namespace gamecmd {bool ExecuteUiCommand(uint32_t);}
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
uint64_t issuedAt=0,cancelEpoch=0;
bool scheduled=false;
template<class T>const T* Service(const PluginContext* c) {const T* p=nullptr;return c->QueryService(&p)==ServiceQueryResult::Success?p:nullptr;}
Item Copy(const Items::ItemInfo& i){return {i.handle,i.runtimeId,i.code,uint32_t(i.quality),i.qualityRecordId};}
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
    return s;
}
void Publish(Snapshot s) {
    std::lock_guard lock(mutex);
    s.revision=snapshot.revision;
    bool changed=s.session!=snapshot.session||s.heroClass!=snapshot.heroClass||s.activeSet!=snapshot.activeSet||s.equipmentKnown!=snapshot.equipmentKnown||
                 s.sets!=snapshot.sets||s.beltKnown!=snapshot.beltKnown||s.skills!=snapshot.skills||
                 s.catalog!=snapshot.catalog||s.skillCount!=snapshot.skillCount||s.bindingBank!=snapshot.bindingBank||s.bindingsEditable!=snapshot.bindingsEditable||
                 s.vitalsKnown!=snapshot.vitalsKnown||int(s.health)!=int(snapshot.health)||int(s.mana)!=int(snapshot.mana)||
                 s.maxHealth!=snapshot.maxHealth||s.maxMana!=snapshot.maxMana||s.experience!=snapshot.experience||
                 s.actionKnown!=snapshot.actionKnown||s.actionMode!=snapshot.actionMode;
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
        else if(waiting.action==Action::Potion&&PotionGone(s,waiting))result=Result::Applied;
        else if(waiting.action==Action::Equip&&s.equipmentKnown&&s.activeSet==uint32_t(waiting.index))result=Result::Applied;
        else if(s.at-issuedAt>1000)result=Result::Refused;
        reply={waiting.sequence,waiting.session,result};
        if(waiting.action==Action::Potion&&result==Result::Applied)
            framevr::logging::Write("Frame potion sip: native belt item consumption confirmed");
        else if(waiting.action==Action::Potion&&result==Result::Refused)
            framevr::logging::Write("Frame potion sip: native command did not consume the held belt item");
        if(result!=Result::Pending)waiting={};
    }
}
void __cdecl Apply(const PluginContext*,void*) noexcept {
    framevr::interaction::Request r;uint64_t epoch;
    {std::lock_guard lock(mutex);scheduled=false;r=queued;queued={};epoch=cancelEpoch;}
    if(r.action==Action::None)return;
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
    const bool accepted=valid&&(already||(r.action==Action::Potion?
        nativebelt::Drink(r.index,r.session):gamecmd::ExecuteUiCommand(command)));
    if(accepted&&(r.action==Action::Potion||r.action==Action::Equip)&&!already) {
        waiting=r;issuedAt=now;reply={r.sequence,r.session,Result::Pending};
    } else reply={r.sequence,r.session,accepted?Result::Applied:Result::Refused};
    framevr::logging::Write(accepted?"Frame interactions: native action accepted; item/set changes require confirmation":"Frame interactions: stale or refused native action discarded");
}
}
void Init(const D2RL::PluginContext* ctx) {
    nativeui::Init(ctx);
    context=ctx;inventory=Service<D2RL::InventoryService>(ctx);items=Service<D2RL::ItemService>(ctx);threads=Service<D2RL::ThreadService>(ctx);
}
void Capture(){Publish(Collect());}
framevr::interaction::Snapshot Read(){std::lock_guard lock(mutex);return snapshot;}
framevr::interaction::Reply Status(){std::lock_guard lock(mutex);return reply;}
void Request(const framevr::interaction::Request& r) {
    if(!threads||!threads->runOnUiThread||r.action==Action::None)return;
    {std::lock_guard lock(mutex);
     if(queued.action!=Action::None||waiting.action!=Action::None||scheduled)return;
     queued=r;scheduled=true;reply={r.sequence,r.session,Result::Pending};}
    if(r.action==Action::Potion) {
        char line[176];std::snprintf(line,sizeof line,"Frame potion sip: gesture completed; native belt command queued (column=%d type=%s)",
            r.index+1, PotionName(PotionType(r.item.code)));
        framevr::logging::Write(line);
    }
    if(threads->runOnUiThread(context,&Apply,nullptr)!=D2RL::Threads::Result::Success) {
        std::lock_guard lock(mutex);queued={};scheduled=false;reply={r.sequence,r.session,Result::Refused};
    }
}
void Cancel(){std::lock_guard lock(mutex);++cancelEpoch;queued={};waiting={};reply={};}
}
