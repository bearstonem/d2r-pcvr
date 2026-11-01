#include <windows.h>
#include <D2RLPlugin/api.h>
#include "quick_items.h"
#include "quick_items_signatures.h"
#include "combat.h"
#include "memory.h"
#include "sigscan.h"
#include "logging.h"
#include <string>
#include <cstdio>
#include <cstdlib>
namespace gamestate {uint64_t LocalPlayer();bool MenuOpen();}
namespace gamecmd {bool ExecuteUiCommand(uint32_t);}
namespace quickitems {
namespace {
using namespace D2RL;
using namespace framevr::interaction;
const PluginContext* context=nullptr;
const InventoryService* inventory=nullptr;
const ItemService* items=nullptr;
const LocalizationService* strings=nullptr;
std::wstring ini;
uint32_t assigned=0;
template<class T>bool Read(uintptr_t p,T& out){return p>=0x10000&&p<=UINTPTR_MAX-sizeof out&&framevr::ReadMemory(&out,reinterpret_cast<void*>(p),sizeof out);}
bool Ready() {
    static bool ready=false;static uint64_t retry=0;
    if(ready)return true;
    if(GetTickCount64()<retry)return false;
    retry=GetTickCount64()+1000;
    using namespace abi;
#define CHECK(x) if(!d2rsig::Check(x##Rva,x##Sig,sizeof x##Sig))return false
    CHECK(Use);CHECK(UseArgs);CHECK(Inventory);CHECK(InventoryHead);CHECK(ItemData);CHECK(Next);CHECK(Usable);CHECK(UsableLayout);CHECK(Code);
#undef CHECK
    ready=true;framevr::logging::Write("Frame quick use: native inventory activation verified");return true;
}
Item Copy(const Items::ItemInfo& i){const uint32_t sockets=(i.socketedItemCount>0)?i.socketedItemCount:i.socketCount;return {i.handle,i.runtimeId,i.code,uint32_t(i.quality),i.qualityRecordId,sockets,0,0};}
bool Carried(const Items::ItemInfo& i){return i.handle&&i.container==Items::ItemContainer::Inventory;}
int Quantity(const Items::ItemInfo& i) {
    // Empty tomes remain assignable but cannot be used. Non-stackable misc
    // items (including single scrolls and quest consumables) count as one.
    if(i.code==Code('t','b','k')||i.code==Code('i','b','k'))return std::max(0,i.quantity);
    return std::max(1,i.quantity);
}
struct ClientItems {
    struct Entry {uintptr_t unit;uint32_t id,classId;};
    std::array<Entry,1024> entries{};unsigned count=0;
    bool Load(uint64_t session) {
        count=0;uintptr_t player=0,head=0;uint32_t header[4]{},magic=0;
        if(!session||session!=gamestate::LocalPlayer()||!combat::LocalPlayerUnit(player)||
           !Read(player,header)||header[0]!=0||header[3]==0||header[3]==17)return false;
        const auto inv=reinterpret_cast<uintptr_t(*)(uintptr_t)>(d2rsig::Addr(abi::InventoryRva))(player);
        if(!Read(inv,magic)||magic!=0x01020304||!Read(inv+0x10,head))return false;
        for(;head&&count<entries.size();++count) {
            uintptr_t data=0,next=0;
            if(!Read(head,header)||header[0]!=4||!Read(head+0x10,data)||!data||!Read(data+0xb0,next))return false;
            for(unsigned j=0;j<count;++j)if(entries[j].unit==head)return false;
            entries[count]={head,header[2],header[1]};head=next;
        }
        return !head; // malformed/cyclic or excessive list: no partial result
    }
    uintptr_t Find(const Items::ItemInfo& i) const {
        for(unsigned n=0;n<count;++n)if(entries[n].id==i.runtimeId&&entries[n].classId==i.classId) {
            const auto unit=entries[n].unit;
            if(reinterpret_cast<uint32_t(*)(uintptr_t)>(d2rsig::Addr(abi::CodeRva))(unit)!=i.code)return 0;
            return reinterpret_cast<int(*)(uintptr_t)>(d2rsig::Addr(abi::UsableRva))(unit)?unit:0;
        }
        return 0;
    }
};
void Name(uint32_t code,char* out,size_t size) {
    char shortCode[5]{};std::memcpy(shortCode,&code,4);
    for(int i=3;i>=0&&shortCode[i]==' ';--i)shortCode[i]=0;
    char key[20];std::snprintf(key,sizeof key,"d2r:%s",shortCode);
    if(strings&&strings->getStringByKey(context,key,out,uint32_t(size),nullptr)==Localization::Result::Success&&out[0])return;
    const char* fallback=code==Code('t','b','k')?"Tome of Town Portal":code==Code('i','b','k')?"Tome of Identify":
        code==Code('t','s','c')?"Scroll of Town Portal":code==Code('i','s','c')?"Scroll of Identify":code==Code('b','o','x')?"Horadric Cube":shortCode;
    std::snprintf(out,size,"%s",fallback);
}
struct Collect {QuickInventory& result;const ClientItems& client;};
Inventory::IterationAction __cdecl CollectItem(const PluginContext*,const Items::ItemInfo* i,void* user) noexcept {
    auto& c=*static_cast<Collect*>(user);auto& q=c.result;
    if(!i||!Carried(*i)||!c.client.Find(*i))return Inventory::IterationAction::Continue;
    auto* row=const_cast<QuickItem*>(q.Find(i->code));
    if(!row){if(q.count==q.items.size())return Inventory::IterationAction::Continue;row=&q.items[q.count++];row->item=Copy(*i);Name(i->code,row->name,sizeof row->name);}
    const int count=Quantity(*i);
    if(!row->quantity&&count>0)row->item=Copy(*i); // prefer a nonempty tome
    row->quantity=int(std::min<int64_t>(INT_MAX,int64_t(row->quantity)+count));
    return Inventory::IterationAction::Continue;
}
bool Validate(const Request& r,const Snapshot& s,Items::ItemInfo& info) {
    if(!items||!Ready()||!Fresh(s,r.session,GetTickCount64())||r.session!=gamestate::LocalPlayer()||gamestate::MenuOpen()||!s.quick.known)return false;
    const auto* row=s.quick.Find(uint32_t(r.index));
    if(!row||row->item!=r.item)return false;
    info.structSize=sizeof info;
    return items->getItemInfo(context,r.item.handle,&info)==Items::Result::Success&&Carried(info)&&Copy(info)==r.item;
}
}
void Init(const PluginContext* c,const wchar_t* settingsPath) {
    context=c;c->QueryService(&inventory);c->QueryService(&items);c->QueryService(&strings);
    HMODULE module=nullptr;wchar_t path[32768]{};
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Init),&module)&&
       GetModuleFileNameW(module,path,DWORD(std::size(path)))) {
        ini=path;const auto slash=ini.find_last_of(L"\\/");
        if(slash!=std::wstring::npos){ini.resize(slash+1);ini+=L"d2r_pcvr.ini";}else ini.clear();
    }
    if(settingsPath)ini=settingsPath;
    wchar_t value[32]{};
    if(!ini.empty()){GetPrivateProfileStringW(L"wheel",L"quick_use_item",L"0",value,32,ini.c_str());assigned=uint32_t(wcstoul(value,nullptr,10));}
}
void Capture(Snapshot& s) {
    s.quick={};s.quick.assigned=assigned;
    if(!inventory||!items||!Ready())return;
    ClientItems client;if(!client.Load(s.session))return;
    Inventory::ItemFilter filter{sizeof filter,0,Items::ContainerBit(Items::ItemContainer::Inventory),0};
    Collect collect{s.quick,client};
    s.quick.known=inventory->forEachInventoryItem(context,s.session,&filter,&CollectItem,&collect)==Inventory::Result::Success;
    std::sort(s.quick.items.begin(),s.quick.items.begin()+s.quick.count,[](const auto& a,const auto& b){return a.item.code<b.item.code;});
}
bool Assign(const Request& r,const Snapshot& s) {
    if(r.action!=Action::BindItem||ini.empty()||r.revision!=assigned||!s.quick.known||
       !Fresh(s,r.session,GetTickCount64())||r.session!=gamestate::LocalPlayer()||gamestate::MenuOpen())return false;
    Items::ItemInfo info{};
    if(r.index&&!Validate(r,s,info))return false;
    if(!r.index&&r.item.handle)return false;
    wchar_t value[32];swprintf_s(value,L"%u",uint32_t(r.index));
    if(!WritePrivateProfileStringW(L"wheel",L"quick_use_item",value,ini.c_str()))return false;
    assigned=uint32_t(r.index);return true;
}
bool Use(const Request& r,const Snapshot& s) {
    Items::ItemInfo info{};ClientItems client;
    if(r.action!=Action::UseItem||!assigned||uint32_t(r.index)!=assigned||!Validate(r,s,info)||
       Quantity(info)<=0||!client.Load(r.session))return false;
    auto unit=client.Find(info);if(!unit)return false;
    if(info.code==Code('i','s','c')||info.code==Code('i','b','k')) {
        // Native identify enters target selection; make the target inventory
        // visible, then resolve again because opening UI may pump callbacks.
        if(!gamecmd::ExecuteUiCommand(0x01)||r.session!=gamestate::LocalPlayer())return false;
        Items::ItemInfo after{};after.structSize=sizeof after;
        if(items->getItemInfo(context,info.handle,&after)!=Items::Result::Success||!Carried(after)||Copy(after)!=r.item||
           Quantity(after)<=0||!client.Load(r.session)||(unit=client.Find(after))==0)return false;
    }
    // InventoryGrid passes mask 3 for ordinary inventory use. This native
    // handler owns eligibility, charges, targeting and packet generation.
    // void return means dispatched, NOT proven consumption. Never retry.
    reinterpret_cast<void(*)(uintptr_t,int)>(d2rsig::Addr(abi::UseRva))(unit,3);
    framevr::logging::Write("Frame quick use: native inventory action dispatched once");return true;
}
const char* UseResultName(UseResult c) {
    switch(c) {
        case UseResult::Used:return "used";
        case UseResult::NotReady:return "native inventory use unverified";
        case UseResult::NoPlayer:return "no local player";
        case UseResult::ListUnreadable:return "client inventory list unreadable";
        case UseResult::Missing:return "not in the client's inventory list";
        case UseResult::WrongCode:return "a different item under that id";
        case UseResult::Unusable:return "the game says it cannot be used now";
    }
    return "?";
}
UseResult UseClientItem(uint64_t session,const ClientItem& item,uint32_t* seen,unsigned capacity,unsigned* count) {
    if(count)*count=0;
    if(!Ready())return UseResult::NotReady;
    if(!session||session!=gamestate::LocalPlayer())return UseResult::NoPlayer;
    ClientItems client;if(!client.Load(session))return UseResult::ListUnreadable;
    const auto code=reinterpret_cast<uint32_t(*)(uintptr_t)>(d2rsig::Addr(abi::CodeRva));
    for(unsigned n=0;n<client.count;++n) {
        const auto& e=client.entries[n];
        if(e.id!=item.runtimeId||e.classId!=item.classId)continue;
        if(code(e.unit)!=item.code)return UseResult::WrongCode;
        if(!reinterpret_cast<int(*)(uintptr_t)>(d2rsig::Addr(abi::UsableRva))(e.unit))return UseResult::Unusable;
        reinterpret_cast<void(*)(uintptr_t,int)>(d2rsig::Addr(abi::UseRva))(e.unit,3);   // InventoryGrid's mask, as Use
        framevr::logging::Write("Frame respec: native inventory use of the token dispatched once");
        return UseResult::Used;
    }
    for(unsigned n=0;n<client.count&&seen&&count&&*count<capacity;++n)
        if(code(client.entries[n].unit)==item.code)seen[(*count)++]=client.entries[n].id;
    return UseResult::Missing;
}
bool HoldsClientItem(uint64_t session,const ClientItem& item,bool& holds) {
    holds=false;
    if(!Ready()||!session||session!=gamestate::LocalPlayer())return false;
    ClientItems client;if(!client.Load(session))return false;
    for(unsigned n=0;n<client.count;++n)
        if(client.entries[n].id==item.runtimeId&&client.entries[n].classId==item.classId){holds=true;break;}
    return true;
}
}
