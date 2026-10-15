#include <windows.h>
#include <D2RLPlugin/api.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include "native_ui.h"
#include "native_ui_signatures.h"
#include "sigscan.h"
#include "memory.h"
#include "combat.h"
#include "logging.h"
#include "controller_bindings.h"
#include "skill_slots.h"
#include "spell_aim.h"
#include "spell_cast.h"
#include "weapon_skills.h"
#include "panel_input.h"
#include "spells.h"
#include "../steamvr/attack_speed.h"
namespace gamestate {uint32_t DataBank();bool MenuOpen();int AttackWeapon();int AttackWsm();}
namespace melee {void SetAttackCycle(uint64_t session,uint32_t milliseconds);}
namespace nativeui {
namespace {
const D2RL::PluginContext* context=nullptr;
const D2RL::LocalizationService* strings=nullptr;
const D2RL::DataTableService* tables=nullptr;
D2RL::Reimplementations::GetLevelIdFromUnitFn getLevelId=nullptr;
std::map<int,std::string> names;
bool ready=false;
bool bindingReady=false;
uint64_t retryAt=0;
template<class T>bool Read(uintptr_t p,T& t){return p>0x10000&&framevr::ReadMemory(&t,reinterpret_cast<void*>(p),sizeof t);}
bool Ready() {
    if(ready)return true;
    if(GetTickCount64()<retryAt)return false;
    retryAt=GetTickCount64()+1000;
    using namespace abi;
#define CHECK(x) if(!d2rsig::Check(x##Rva,x##Sig,sizeof x##Sig))return false
    CHECK(LocalIndex);CHECK(ClientPlayer);CHECK(ButtonSlot);CHECK(SlotLayout);CHECK(Binding);CHECK(BindingLayout);
    CHECK(Owner);CHECK(Skill);CHECK(SkillLayout);CHECK(SkillList);CHECK(Stat);CHECK(StatBody);CHECK(StatLookup);
    CHECK(ControllerBank);CHECK(ControllerOwner);CHECK(SkillLevelLayout);
#undef CHECK
    ready=true;framevr::logging::Write("Frame interactions: controller-only assignments and resource readers verified (independent of mouse/keyboard mode)");return true;
}
bool BindingReady() {
    if(bindingReady)return true;
    using namespace abi;
#define CHECK(x) if(!d2rsig::Check(x##Rva,x##Sig,sizeof x##Sig))return false
    CHECK(AssignSkill);CHECK(AssignSkillArgs);CHECK(AssignSkillBank);CHECK(ClearSkill);CHECK(ClearSkillWrite);
    CHECK(SkillPositive);CHECK(SkillPositiveLevel);CHECK(SkillLeftFlag);
#undef CHECK
    bindingReady=true;framevr::logging::Write("Frame skill editor: native assignment, clear and catalog readers verified");return true;
}
int Slot(int wheel) {
    if(wheel<0||wheel>=framevr::skills::Count)return -1;
    return reinterpret_cast<int(*)(int,bool)>(d2rsig::Addr(abi::ButtonSlotRva))(
        framevr::skills::Buttons(wheel),framevr::skills::Alternate(wheel));
}
const char* SkillKey(int id,char key[80]) {
    // Original localization keys are not fully regular across the expansion.
    // These are identifiers, not copied text/images from the game.
    if(id==61)return "d2r:skillsname61";
    if(id==197)return "d2r:Skillname226";
    if(id==350)return "d2r:Runeword22";
    if(id>=357&&id<=364){const char* special[]={"SkillNameInteract","SkillNameLoot","skillname219","SkillNameEmoteWheel","SkillNameSwapWeapons","SkillNameMap","SkillNameShowItems","SkillNameRunToggle"};std::snprintf(key,80,"d2r:%s",special[id-357]);return key;}
    if(id==370)return "d2r:SkillNameCubeLoot";
    if(id==371)return "d2r:SkillnameMetamorphosisBear";
    if(id==372)return "d2r:SkillnameMetamorphosisWolf";
    if(id>372)return nullptr; // table/localization adapter needed; no fabricated name
    std::snprintf(key,80,"d2r:skillname%d",id+(id>=221&&id<=280?1:0));return key;
}
std::string Name(int id) {
    if(auto i=names.find(id);i!=names.end())return i->second;
    char key[80]{},text[256]{};const char* k=SkillKey(id,key);
    if(k&&strings&&strings->getStringByKey(context,k,text,sizeof text,nullptr)==D2RL::Localization::Result::Success)
        return names.emplace(id,text).first->second;
    std::snprintf(text,sizeof text,"Skill %d",id);return text;
}
}
void Init(const D2RL::PluginContext* c){
    context=c;c->QueryService(&strings);c->QueryService(&tables);
    const auto core=GetModuleHandleW(L"D2RCore.dll");
    getLevelId=core?reinterpret_cast<D2RL::Reimplementations::GetLevelIdFromUnitFn>(
        GetProcAddress(core,MAKEINTRESOURCEA(D2RL::Reimplementations::GetLevelIdFromUnitInfo.ordinal))):nullptr;
    if(!getLevelId)framevr::logging::Write("Frame navigation: loader level accessor unavailable");
}
void Capture(framevr::interaction::Snapshot& s) {
    int reason=1;uintptr_t player=0;uint32_t id=0,clientId=0;
    struct Trace {
        framevr::interaction::Snapshot& s;int& reason;uintptr_t& player;uint32_t& id;uint32_t& clientId;
        ~Trace() {
            if(!s.session)return;
            static uint64_t next=0;static int last=-1;const auto now=GetTickCount64();
            if(reason==last&&now<next)return;last=reason;next=now+5000;
            char line[240];std::snprintf(line,sizeof line,"Frame vitals read: reason=%d session=%llu client=%p client_id=%u observed_id=%u hp=%.1f/%.1f mana=%.1f/%.1f",reason,
                (unsigned long long)s.session,reinterpret_cast<void*>(player),clientId,id,s.health,s.maxHealth,s.mana,s.maxMana);
            framevr::logging::Write(line);
        }
    } trace{s,reason,player,id,clientId};
    if(!s.session||!Ready())return;
    reason=2;
    const int index=reinterpret_cast<int(*)()>(d2rsig::Addr(abi::LocalIndexRva))();
    if(index<0||index>=8)return;
    player=reinterpret_cast<uintptr_t(*)(int)>(d2rsig::Addr(abi::ClientPlayerRva))(index);
    uint32_t h[4]{};reason=3;
    if(!Read(player,h)||h[0]!=0)return;
    s.actionKnown=true;s.actionMode=h[3];
    clientId=h[2];reason=4;
    // This signature-checked accessor resolves the local UI player, after the
    // SDK has confirmed the current session. Aim/camera proximity is not an
    // identity source: it can stop matching during a level warp.
    if(!combat::ObserveUiPlayer(player,s.session))return;
    if(!combat::LocalPlayerId(id))return;
    reason=5;if(id!=h[2])return;
    s.devClass=framevr::dev::ExpansionClass(h[1],gamestate::DataBank());
    // The supported loader accessor reads this live, identity-checked UI player.
    // Copy its current table row/name on the UI thread every capture so warps,
    // loading gaps and language changes cannot reuse a previous area's label.
    {
        const int level=getLevelId?getLevelId(reinterpret_cast<const void*>(player)):0;
        D2RL::DataTables::RowView row{};row.structSize=sizeof row;
        auto result=D2RL::DataTables::Result::Unavailable;
        if(level>0&&tables&&tables->findRowById)
            result=tables->findRowById(context,D2RL::DataTables::Bank(gamestate::DataBank()),
                D2RL::DataTables::TableId::Levels,uint32_t(level),&row);
        if(result==D2RL::DataTables::Result::Success)
            s.location=framevr::navigation::ReadLocation(level,reinterpret_cast<uintptr_t>(row.row),row.rowSize,
                [](uintptr_t p,auto& value){return Read(p,value);},
                [](const char* key,char* text,uint32_t size){return strings&&strings->getStringByKey&&
                    strings->getStringByKey(context,key,text,size,nullptr)==D2RL::Localization::Result::Success;});
        // Trace changes, including unavailable data, so a layout/lookup failure
        // can be distinguished from a loading gap without a memory dump.
        static int previousLevel=-1;static uint32_t previousSize=0;static auto previousResult=result;
        static framevr::navigation::Location previous;
        if(previousLevel!=level||previousSize!=row.rowSize||previousResult!=result||previous!=s.location) {
            previousLevel=level;previousSize=row.rowSize;previousResult=result;previous=s.location;
            char line[420];std::snprintf(line,sizeof line,
                "Frame navigation: level=%d bank=%u table_result=%u row_size=%u act=%d area=%s",
                level,gamestate::DataBank(),uint32_t(result),row.rowSize,s.location.act,
                framevr::navigation::AreaLabel(s.location));
            framevr::logging::Write(line);
        }
    }
    spellvr::Feedback(s.session,player,id,!gamestate::MenuOpen()&&(h[3]==1||h[3]==2||h[3]==3));
    auto stat=reinterpret_cast<int(*)(uintptr_t,int,int)>(d2rsig::Addr(abi::StatRva));
    s.health=stat(player,6,0)/256.f;s.maxHealth=stat(player,7,0)/256.f;
    s.mana=stat(player,8,0)/256.f;s.maxMana=stat(player,9,0)/256.f;
    s.vitalsKnown=s.health>=0&&s.maxHealth>0&&s.health<=s.maxHealth*2&&s.maxHealth<1000000&&
        s.mana>=0&&s.maxMana>=0&&s.mana<=std::max(1.f,s.maxMana)*2&&s.maxMana<1000000;
    reason=s.vitalsKnown?0:6;
    // ItemStatCost IDs verified against this game's own table: level=12,
    // experience=13. Experience rows contain one unsigned threshold per class
    // followed by ExpRatio; row 0 is MaxLvl, row 1 starts character level 1.
    // Query fresh game-owned rows on the UI thread; never retain native rows.
    D2RL::DataTables::TableView xp{};xp.structSize=sizeof xp;
    if(tables&&tables->getTable(context,D2RL::DataTables::Bank(gamestate::DataBank()),
        D2RL::DataTables::TableId::Experience,&xp)==D2RL::DataTables::Result::Success&&xp.rows&&
        (xp.rowSize==32||xp.rowSize==36)&&h[1]<xp.rowSize/4-1&&xp.rowCount>=3&&xp.rowCount<=512) {
        auto& e=s.experience;e.level=uint32_t(stat(player,12,0));e.current=uint32_t(stat(player,13,0));
        const auto base=reinterpret_cast<uintptr_t>(xp.rows)+h[1]*sizeof(uint32_t);
        uint32_t zero=1,first=0;
        if(Read(base,e.maximumLevel)&&e.maximumLevel>=1&&e.maximumLevel<256&&xp.rowCount>e.maximumLevel+1&&
           e.level>=1&&e.level<=e.maximumLevel&&Read(base+xp.rowSize,zero)&&zero==0&&
           Read(base+2*xp.rowSize,first)&&first>0&&Read(base+e.level*xp.rowSize,e.start)&&
           Read(base+(e.level+1)*xp.rowSize,e.next))e.known=true;
        e.known=e.Valid();
    }
    // Attack speed for free-swing melee (attack_speed.h): class, IAS from the
    // player's items (ItemStatCost 93, item_fasterattackrate) and the weapon in
    // hand. Skill speed (Fanaticism, Frenzy, Burst of Speed) and slows are not
    // read yet. An unknown class or weapon gives cycle 0, which never gates.
    {
        const int weapon=gamestate::AttackWeapon(),wsm=gamestate::AttackWsm(),ias=stat(player,93,0);
        const int frames=weapon>=0?framevr::attackspeed::Frames(int(h[1]),framevr::attackspeed::Weapon(weapon),ias,wsm):0;
        melee::SetAttackCycle(s.session,framevr::attackspeed::Milliseconds(frames));
        static int told[5]={-2,-2,-2,-2,-2};
        const int now[5]={int(h[1]),weapon,ias,wsm,frames};
        if(!std::equal(now,now+5,told)) {
            std::copy(now,now+5,told);
            char line[220];std::snprintf(line,sizeof line,"Frame melee attack speed: class=%d weapon_class=%d ias=%d eias=%d wsm=%d -> %d frames (%u ms per landed hit)",
                now[0],weapon,ias,framevr::attackspeed::Eias(ias),wsm,frames,framevr::attackspeed::Milliseconds(frames));
            framevr::logging::Write(line);
        }
    }
    // Lookup asserts on a missing skill list during load/teardown. The
    // signature-checked accessor's +0x100 layout is guarded separately.
    uintptr_t skillList=0;
    if(!Read(player+0x100,skillList)||skillList<0x10000)return;
    const int bank=reinterpret_cast<int(*)()>(d2rsig::Addr(abi::ControllerBankRva))();
    if(bank!=0&&bank!=16)return;
    s.bindingBank=bank;
    unsigned usedSlots=0;
    for(int i=0;i<framevr::skills::Count;++i){
        const int slot=Slot(i);
        // A customized/unsupported controller layout must not alias two hand
        // bindings. Leave the skill surface unavailable rather than edit/cast
        // a different slot. Equipment, vitals and native menus remain usable.
        if(slot<0||slot>=16||(usedSlots&(1u<<slot)))return;
        usedSlots|=1u<<slot;
    }
    for(int i=0;i<framevr::skills::Count;++i) {
        const int slot=Slot(i);
        if(slot<0||slot>=16)continue;
        auto& skill=s.skills[i];
        framevr::interaction::ControllerBinding binding;
        if(!framevr::interaction::ReadControllerBinding(d2rsig::Addr(abi::ControllerTableRva),bank,slot,
            [](uintptr_t p,auto& value){return Read(p,value);},binding))continue;
        int skillId=binding.skill;
        // Keep the physical right-primary interaction safe for existing layouts.
        // Explicit skill assignments replace it; clearing restores Interact.
        if(i==8&&skillId<=0)skillId=357;
        // Preserve the native controller getter's combined Town Portal shortcut:
        // use a scroll when its base level is positive, otherwise the tome.
        if(skillId==359) {
            const auto scroll=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(abi::SkillRva))(player,219,-1);
            int level=0;if(scroll&&!Read(scroll+0x48,level))continue;
            skillId=scroll&&level>0?219:220;
        }
        skill.id=skillId;skill.owner=binding.owner;skill.known=true;
        if(skillId==-1){std::snprintf(skill.name,sizeof skill.name,"Unassigned");continue;}
        const int owner=binding.owner;
        const auto node=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(abi::SkillRva))(player,skillId,owner);
        // Presence is availability of the assignment, not a mana/cooldown
        // guarantee. Those gates remain in the native casting path.
        skill.available=node!=0||skillId>=357;
        // Same checked skill-node -> compiled Skills row used by the catalog.
        // Offsets independently audited across all 429 local skill records.
        uintptr_t row=0;int16_t rowId=-1,missile=-1,serverFunction=0;uint32_t flags=0;
        if(node&&Read(node,row)&&Read(row,rowId)&&rowId==skillId&&
           Read(row+0x24,flags)&&Read(row+0x4e,serverFunction)&&Read(row+0x66,missile)) {
            skill.directed=framevr::spellaim::Directed(flags,missile,serverFunction);
            skill.motion=framevr::weaponskills::Bow(skillId)||(skillId==framevr::weaponskills::Smite&&serverFunction==150);
            int16_t start=-1,secondary=-1;
            uint8_t los=255;
            if(Read(row+0x4c,start)&&Read(row+0x68,secondary)&&Read(row+0x231,los))skill.physical=skill.owner==-1&&los==0&&
                framevr::spellcast::Candidate(h[1],skillId,start,serverFunction,missile,flags,secondary);
        }
        const auto name=Name(skillId);std::snprintf(skill.name,sizeof skill.name,"%s",name.c_str());
    }
    if(BindingReady()) {
        auto& interact=s.catalog[s.skillCount++];interact.id=357;interact.known=interact.available=true;
        std::snprintf(interact.name,sizeof interact.name,"Interact / pick up");
        uintptr_t node=0;bool valid=Read(skillList,node);
        unsigned visited=0;
        for(;valid&&node&&visited<256;++visited) {
            struct Node {uintptr_t row,next;uint8_t pad[0x30];int level,bonus,base,owner;} n{};
            static_assert(offsetof(Node,owner)==0x4c);
            struct Row {int16_t id;uint8_t pad[0x22],flags[4];} row{};
            if(!Read(node,n)||!Read(n.row,row)||row.id<0||row.id>4095||n.next==node){valid=false;break;}
            // Native compiled Skills row flags; checked against the local
            // skills.bin and source table: passive +0x24/0x10, leftskill +0x27/0x40.
            const bool positive=reinterpret_cast<bool(*)(uintptr_t,uintptr_t)>(d2rsig::Addr(abi::SkillPositiveRva))(player,node);
            const int choice=framevr::interaction::CatalogSkillId(row.id);
            const int owner=choice==359?-1:n.owner;
            const bool duplicate=std::any_of(s.catalog.begin(),s.catalog.begin()+s.skillCount,[&](const auto& c){return c.id==choice&&c.owner==owner;});
            if(choice>=0&&!(row.flags[0]&0x10)&&positive&&!duplicate) {
                if(s.skillCount==s.catalog.size()){valid=false;break;}
                auto& option=s.catalog[s.skillCount++];option.id=choice;option.owner=owner;
                option.level=n.level+n.bonus;option.known=option.available=true;
                const auto name=Name(choice);std::snprintf(option.name,sizeof option.name,"%s",name.c_str());
            }
            node=n.next;
        }
        if(node)valid=false;
        s.bindingsEditable=valid&&std::all_of(s.skills.begin(),s.skills.end(),[](const auto& skill){return skill.known;});
        if(!valid){s.catalog={};s.skillCount=0;}
        else std::sort(s.catalog.begin(),s.catalog.begin()+s.skillCount,[](const auto& a,const auto& b){
            const int order=std::strcmp(a.name,b.name);return order?order<0:a.owner<b.owner;
        });
    }
    static uint64_t lastSession=0;static int lastBank=-1;static std::array<int,framevr::skills::Count> lastIds{};static unsigned lastDirected=0;
    unsigned directed=0;std::array<int,framevr::skills::Count> ids{};
    for(int i=0;i<framevr::skills::Count;++i){ids[i]=s.skills[i].known?s.skills[i].id:-2;if(s.skills[i].directed)directed|=1u<<i;}
    if(lastSession!=s.session||lastBank!=bank||lastIds!=ids||lastDirected!=directed) {
        char line[240];std::snprintf(line,sizeof line,"Frame hand assignments: controller bank=%d skills=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d directed=0x%03X",
            bank,ids[0],ids[1],ids[2],ids[3],ids[4],ids[5],ids[6],ids[7],ids[8],ids[9],directed);
        framevr::logging::Write(line);lastSession=s.session;lastBank=bank;lastIds=ids;lastDirected=directed;
    }
}
bool Assign(const framevr::interaction::Request& r,const framevr::interaction::Snapshot& current) {
    if(!Ready()||!BindingReady()||!framevr::interaction::CanBind(current,r,GetTickCount64()))return false;
    const int slot=Slot(r.index);
    const int bank=reinterpret_cast<int(*)()>(d2rsig::Addr(abi::ControllerBankRva))();
    if(slot<0||slot>=16||bank!=r.bank)return false;
    bool left=r.skill==357;
    if(r.skill>=0&&r.skill!=357) {
        const int index=reinterpret_cast<int(*)()>(d2rsig::Addr(abi::LocalIndexRva))();
        if(index<0||index>=8)return false;
        const auto player=reinterpret_cast<uintptr_t(*)(int)>(d2rsig::Addr(abi::ClientPlayerRva))(index);
        const auto lookup=reinterpret_cast<uintptr_t(*)(uintptr_t,int,int)>(d2rsig::Addr(abi::SkillRva));
        auto node=lookup(player,r.skill==359?219:r.skill,r.skillOwner);
        if(r.skill==359) {
            int level=0;
            if(!node||!Read(node+0x48,level)||level<=0)node=lookup(player,220,-1);
        }
        uintptr_t row=0;uint8_t flag=0;
        if(!node||!Read(node,row)||!Read(row+0x27,flag))return false;
        left=(flag&0x40)!=0||r.skill==357||r.skill==358||r.skill==370;
    }
    struct Args {int slot,skill,owner;bool left;} args{slot,r.skill,r.skillOwner,left};
    // The native setter chooses its table through the controller getter. Scope
    // that choice to this synchronous UI-thread operation; do not alter the
    // user's desktop input mode or write to their save/skill records ourselves.
    if(!panelinput::ControllerAction(+[](void* data){
        const auto& a=*static_cast<Args*>(data);
        if(a.skill<0)reinterpret_cast<void(*)(int)>(d2rsig::Addr(abi::ClearSkillRva))(a.slot);
        else reinterpret_cast<void(*)(int,bool,int,int)>(d2rsig::Addr(abi::AssignSkillRva))(a.slot,a.left,a.skill,a.owner);
    },&args))return false;
    framevr::interaction::ControllerBinding value;
    const bool applied=framevr::interaction::ReadControllerBinding(d2rsig::Addr(abi::ControllerTableRva),bank,slot,
        [](uintptr_t p,auto& v){return Read(p,v);},value)&&value.skill==r.skill&&value.owner==r.skillOwner;
    char line[160];std::snprintf(line,sizeof line,"Frame skill editor: slot=%d bank=%d skill=%d native readback=%s",r.index,bank,r.skill,applied?"confirmed":"refused");
    framevr::logging::Write(line);return applied;
}
}
