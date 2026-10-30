#include "melee_weapon.h"
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
#include "aim_rules.h"
#include "spell_cast.h"
#include "weapon_skills.h"
#include "strike_skills.h"
#include "panel_input.h"
#include "spells.h"
#include "bow.h"
#include "../steamvr/attack_speed.h"
namespace gamestate {framevr::dualwield::Equipment MeleeEquipment();void SetMeleeOrder(uint32_t,uint32_t);uint32_t DataBank();bool MenuOpen();int AttackWeapon(int hand=0);int AttackWsm(int hand=0);uint32_t HandsKey();uint32_t WeaponSet();}
namespace melee {void SetAttackCycle(uint64_t session,uint32_t milliseconds,int hand=0);}
namespace interactionstate {int ShapeForm(uint64_t session,uint64_t now);}
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
bool Lookup(const char* key,char* text,uint32_t size) {
    return key&&strings&&strings->getStringByKey&&
        strings->getStringByKey(context,key,text,size,nullptr)==D2RL::Localization::Result::Success&&text[0];
}
// A name lookup that found nothing: retried after 10 s, three times at most.
std::map<int,ULONGLONG> nameMissAt;
std::map<int,int> nameMisses;
// The first of these keys the game knows (spellings differ across the
// expansion); shift: try id+shift before id. Which key answered is logged once.
std::string FirstKnown(int id,std::initializer_list<const char*> patterns,int shift) {
    const ULONGLONG now=GetTickCount64();
    if(auto m=nameMissAt.find(id);m!=nameMissAt.end()&&(now-m->second<10000||nameMisses[id]>=3))return {};
    char key[80]{},text[256]{};
    for(const char* pattern:patterns)for(int s:{shift,0}) {
        std::snprintf(key,sizeof key,pattern,id+s);
        if(Lookup(key,text,sizeof text)) {
            char line[160];std::snprintf(line,sizeof line,"Frame skill names: skill %d from key %s",id,key);
            framevr::logging::Write(line);
            return text;
        }
        if(!shift)break;
    }
    nameMissAt[id]=now;++nameMisses[id];
    char line[120];std::snprintf(line,sizeof line,"Frame skill names: no name found for skill %d",id);
    framevr::logging::Write(line);
    return {};
}
// The skill tables' own text: skills.txt row -> its skilldesc row -> the name's
// string ID. The binary row layouts are not documented in the loader SDK, so
// where those two fields sit is found once, by matching against the names the
// string keys already give for the original classes. The Warlock's skills
// (373-402, new in D2R) have no regular keys and are named this way.
struct SkillTextLayout { bool ok=false; ULONGLONG retryAt=0; uint32_t descIndex=0,name=0; } layout;
std::string ById(uint32_t stringId) {
    char text[256]{};
    if(!strings||!strings->getStringById||!stringId||stringId>=0xFFFF)return {};
    if(strings->getStringById(context,stringId,text,sizeof text,nullptr)!=D2RL::Localization::Result::Success||!text[0])return {};
    return text;
}
// The original classes' names from their keys, quietly.
std::string KnownName(int id) {
    char key[80]{},text[256]{};
    if(Lookup(SkillKey(id,key),text,sizeof text))return text;
    const int shift=id>=221&&id<=280?1:0;
    for(const char* p:{"d2r:skillname%d","d2r:Skillname%d"})for(int s:{shift,0}){std::snprintf(key,sizeof key,p,id+s);if(Lookup(key,text,sizeof text))return text;}
    return {};
}
bool Tables(D2RL::DataTables::TableId id,D2RL::DataTables::TableView& view) {
    view={};view.structSize=sizeof view;
    return tables&&tables->getTable&&tables->getTable(context,D2RL::DataTables::Bank(gamestate::DataBank()),id,&view)==D2RL::DataTables::Result::Success&&
        view.rows&&view.rowCount&&view.rowSize>=4;
}
uint16_t U16(const void* rows,uint32_t rowSize,uint32_t row,uint32_t offset) {
    uint16_t v=0;Read(reinterpret_cast<uintptr_t>(rows)+uintptr_t(row)*rowSize+offset,v);return v;
}
bool DiscoverLayout() {
    if(layout.ok)return true;
    const auto now=GetTickCount64();
    if(now<layout.retryAt)return false;
    // Tables can load before localization. Retry failed discovery without
    // repeating the scan on every UI capture; successful layouts stay cached.
    layout.retryAt=now+10000;
    using D2RL::DataTables::TableId;
    D2RL::DataTables::TableView skills,desc;
    if(!Tables(TableId::Skills,skills)||!Tables(TableId::SkillDesc,desc))return false;   // tried again later
    std::map<int,std::string> known;   // id -> name, the original classes
    for(int id=0;id<=280&&id<int(skills.rowCount);++id)if(auto n=KnownName(id);!n.empty())known[id]=n;
    if(known.size()<30){framevr::logging::Write("Frame skill names: too few known names to read the skill tables");return false;}
    std::map<std::string,int> byName;for(auto& [id,n]:known)byName[n]=id;
    // skilldesc: the field whose string is a known skill name in the most rows
    uint32_t bestName=0;int bestNameHits=0;
    for(uint32_t o=0;o+2<=desc.rowSize;o+=2) {
        int hits=0;
        for(uint32_t r=0;r<desc.rowCount;++r)if(byName.count(ById(U16(desc.rows,desc.rowSize,r,o))))++hits;
        if(hits>bestNameHits){bestNameHits=hits;bestName=o;}
    }
    // skills: the field indexing a skilldesc row that carries the skill's own name
    uint32_t bestIndex=0;int bestIndexHits=0;
    for(uint32_t p=0;p+2<=skills.rowSize;p+=2) {
        int hits=0;
        for(auto& [id,n]:known) {
            const uint16_t d=U16(skills.rows,skills.rowSize,uint32_t(id),p);
            if(d&&d<desc.rowCount&&ById(U16(desc.rows,desc.rowSize,d,bestName))==n)++hits;
        }
        if(hits>bestIndexHits){bestIndexHits=hits;bestIndex=p;}
    }
    layout.ok=bestNameHits>=20&&bestIndexHits>=int(known.size())*3/4;
    layout.descIndex=bestIndex;layout.name=bestName;
    char line[260];std::snprintf(line,sizeof line,
        "Frame skill names: skill tables %s - skills row %u bytes x %u, desc index at +0x%X (%d/%zu); skilldesc row %u bytes x %u, name at +0x%X (%d)",
        layout.ok?"read":"NOT read",skills.rowSize,skills.rowCount,bestIndex,bestIndexHits,known.size(),desc.rowSize,desc.rowCount,bestName,bestNameHits);
    framevr::logging::Write(line);
    return layout.ok;
}
// A skill's name from the tables, or empty.
std::string FromTables(int id) {
    if(!DiscoverLayout())return {};
    using D2RL::DataTables::TableId;
    D2RL::DataTables::TableView skills,desc;
    if(!Tables(TableId::Skills,skills)||!Tables(TableId::SkillDesc,desc)||id<0||uint32_t(id)>=skills.rowCount)return {};
    const uint16_t d=U16(skills.rows,skills.rowSize,uint32_t(id),layout.descIndex);
    if(!d||d>=desc.rowCount)return {};
    return ById(U16(desc.rows,desc.rowSize,d,layout.name));
}
std::string Name(int id) {
    static SRWLOCK s_nameLock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&s_nameLock);
    auto resolve = [&]() -> std::string {
        if(auto i=names.find(id);i!=names.end())return i->second;
        char key[80]{},text[256]{};
        if(Lookup(SkillKey(id,key),text,sizeof text))return names.emplace(id,text).first->second;
        if(auto t=FromTables(id);!t.empty())return names.emplace(id,t).first->second;   // the Warlock's, and any key miss
        // Item-granted Druid skills such as 221 and 225 showed as "Skill 221": the
        // shifted key is not every skill's. Try the other spellings.
        if(id>=0&&id<=511) {   // to the end of the class trees, the Warlock's included
            auto found=FirstKnown(id,{"d2r:skillname%d","d2r:Skillname%d","d2r:skillsname%d","d2r:SkillName%d"},id>=221&&id<=280?1:0);
            if(!found.empty())return names.emplace(id,found).first->second;
        }
        std::snprintf(text,sizeof text,"Skill %d",id);return text;
    };
    std::string res = resolve();
    ReleaseSRWLockExclusive(&s_nameLock);
    return res;
}
}
std::string SkillName(int id) {
    return Name(id);
}
const void* GetSkillRow(int id) {
    using D2RL::DataTables::TableId;
    D2RL::DataTables::TableView skills;
    if (!Tables(TableId::Skills, skills) || id < 0 || uint32_t(id) >= skills.rowCount || !skills.rows) {
        return nullptr;
    }
    return static_cast<const uint8_t*>(skills.rows) + uint32_t(id) * skills.rowSize;
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
    s.skillClass=h[1]<framevr::skillcategories::Classes.size()?int(h[1]):-1;
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
    // Bone Armor's pool (132) and maximum (133): the forearm grafts (steamvr/bone_graft.h).
    s.boneArmor=stat(player,132,0);s.boneArmorMax=stat(player,133,0);
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
    // hand, plus skill speed and slows (Fanaticism, Frenzy, Burst of Speed, chill,
    // Decrepify) from the unit's attackrate (attack_speed.h SkillSpeed). An unknown
    // class or weapon gives cycle 0, which never gates.
    {
        const auto equipment=gamestate::MeleeEquipment();
        if(framevr::dualwield::Eligible(int(h[1]),equipment)&&meleeweapon::Ready()) {
            const auto right=meleeweapon::Select(player,0,equipment),left=meleeweapon::Select(player,1,equipment);
            uint32_t rightId=0,leftId=0;
            if(right&&left&&Read(right+8,rightId)&&Read(left+8,leftId))gamestate::SetMeleeOrder(rightId,leftId);
        }
        const int weapon=gamestate::AttackWeapon(),wsm=gamestate::AttackWsm(),ias=stat(player,93,0);
        // A druid in Werewolf / Werebear form attacks with the form's normal attack (D2R 2.4,
        // attack_speed.h): its speed sum takes the unit's attackrate (ItemStatCost 68: 100 plus
        // Werewolf / Maul / aura skill speed, less WSM and slows). Both claws share the cycle.
        const int form=interactionstate::ShapeForm(s.session,GetTickCount64());
        const int attackRate=stat(player,68,0);
        const bool rateKnown=attackRate>=10&&attackRate<=400;
        const int sias=rateKnown?framevr::attackspeed::SkillSpeed(attackRate,wsm):0;   // human form (an unreadable rate: none)
        const int frames=weapon<0?0:form?(rateKnown?framevr::attackspeed::WereformFrames(int(h[1]),form,framevr::attackspeed::Weapon(weapon),
                                                                                         framevr::attackspeed::FormEias(ias,attackRate)):0)
                                         :framevr::attackspeed::Frames(int(h[1]),framevr::attackspeed::Weapon(weapon),ias,wsm,sias);
        melee::SetAttackCycle(s.session,framevr::attackspeed::Milliseconds(frames));
        const int offWeapon=gamestate::AttackWeapon(1),offWsm=gamestate::AttackWsm(1);
        const int offFrames=offWeapon>=0?framevr::attackspeed::Frames(int(h[1]),framevr::attackspeed::Weapon(offWeapon),ias,offWsm,sias):0;
        melee::SetAttackCycle(s.session,framevr::attackspeed::Milliseconds(offFrames),1);
        bowvr::SetCrossbowCycle(s.session,gamestate::HandsKey()^(gamestate::WeaponSet()<<30),
            weapon==int(framevr::attackspeed::Weapon::Crossbow)?framevr::attackspeed::Milliseconds(frames):0);
        static int told[7]={-2,-2,-2,-2,-2,-2,-2};
        const int now[7]={int(h[1]),weapon,ias,wsm,frames,form,form?attackRate:0};   // attackrate moves with every aura and slow: logged only in form
        if(!std::equal(now,now+7,told)) {
            std::copy(now,now+7,told);
            char line[260];std::snprintf(line,sizeof line,"Frame melee attack speed: class=%d weapon_class=%d ias=%d eias=%d wsm=%d attackrate=%d form=%s -> %d frames (%u ms per landed hit)",
                now[0],weapon,ias,form?framevr::attackspeed::FormEias(ias,attackRate):framevr::attackspeed::Eias(ias),wsm,attackRate,
                form==1?"werewolf":form==2?"werebear":"human",frames,framevr::attackspeed::Milliseconds(frames));
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
            // Telemetry: "Frame skill row:" once per skill the aim rules do not know (the Warlock's
            // 373-402 first): its game flags, to give it the right rule (aim_rules.h)
            if(static bool seen[512]{};skillId>=0&&skillId<512&&!seen[skillId]&&framevr::aimrules::Of(skillId,skill.directed)==framevr::aimrules::Use::Native&&(skillId<357||skillId>372)) {
                seen[skillId]=true;char line[200];
                std::snprintf(line,sizeof line,"Frame skill row: skill %d flags 0x%08x server function %d missile %d directed %d",skillId,flags,int(serverFunction),int(missile),int(skill.directed));
                framevr::logging::Write(line);
            }
            skill.missile=serverFunction==0?missile:-1;
            skill.motion=framevr::weaponskills::Bow(skillId)||(skillId==framevr::weaponskills::Smite&&serverFunction==150);
            int16_t start=-1,secondary=-1;
            uint8_t los=255;
            if(Read(row+0x4c,start))skill.strike=framevr::strikeskills::Matches(skillId,start,serverFunction);
            if(Read(row+0x4c,start)&&Read(row+0x68,secondary)&&Read(row+0x231,los))skill.physical=skill.owner==-1&&los==0&&
                framevr::spellcast::Candidate(h[1],skillId,start,serverFunction,missile,flags,secondary)&&
                (skillId!=101||bowvr::HolyReady());
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
