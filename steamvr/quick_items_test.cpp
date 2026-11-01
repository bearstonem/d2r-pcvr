// Real adapter, synthetic client inventory + loader services. Never opens a game.
#include <windows.h>
#include <D2RLPlugin/api.h>
#include "../vr/quick_items.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace D2RL;
using namespace framevr::interaction;
namespace {
bool verified=true,menu=false,local=true,usable=true;
uint64_t session=10;
uint32_t player[4]={0,3,7,1};
std::array<uint8_t,32> inv{},unit{};
std::array<uint8_t,192> itemData{};
Items::ItemInfo item{};
int uses=0,opens=0;
template<class T>void Put(uint8_t* dst,size_t offset,T value){std::memcpy(dst+offset,&value,sizeof value);}
uintptr_t ClientInventory(uintptr_t p){assert(p==uintptr_t(player));return uintptr_t(inv.data());}
uint32_t CodeOf(uintptr_t p){assert(p==uintptr_t(unit.data()));return item.code;}
int Usable(uintptr_t p){assert(p==uintptr_t(unit.data()));return usable;}
void Use(uintptr_t p,int mask){assert(p==uintptr_t(unit.data())&&mask==3);++uses;}
Inventory::Result __cdecl Enumerate(const PluginContext* c,PlayerHandle p,const Inventory::ItemFilter*,Inventory::ItemCallback callback,void* user) noexcept {
    assert(p==session);callback(c,&item,user);return Inventory::Result::Success;
}
Items::Result __cdecl Info(const PluginContext*,ItemHandle handle,Items::ItemInfo* out) noexcept {
    if(handle!=item.handle)return Items::Result::NotFound;*out=item;return Items::Result::Success;
}
InventoryService inventory{};ItemService items{};
ServiceQueryResult __cdecl Query(const PluginContext*,ServiceId id,uint32_t,const void** out) noexcept {
    if(id==ServiceId::Inventory){*out=&inventory;return ServiceQueryResult::Success;}
    if(id==ServiceId::Item){*out=&items;return ServiceQueryResult::Success;}
    *out=nullptr;return ServiceQueryResult::Unavailable;
}
Snapshot Capture(){Snapshot s;s.session=session;s.at=GetTickCount64();quickitems::Capture(s);return s;}
Request RequestFor(Action action,const Snapshot& s){return {action,1,session,GetTickCount64(),int(item.code),s.quick.items[0].item,s.quick.assigned};}
}
namespace gamestate {uint64_t LocalPlayer(){return session;}bool MenuOpen(){return menu;}}
namespace combat {bool LocalPlayerUnit(uintptr_t& out){out=uintptr_t(player);return local;}}
namespace d2rsig {
bool Check(uint64_t,const void*,uint32_t){return verified;}
uintptr_t Addr(uint64_t rva){switch(rva){case 0x34a360:return uintptr_t(&ClientInventory);case 0x36ef50:return uintptr_t(&CodeOf);case 0x374920:return uintptr_t(&Usable);case 0x161f20:return uintptr_t(&Use);}assert(false);return 0;}
}
namespace framevr::logging {void Write(const char*){}}
namespace gamecmd {bool ExecuteUiCommand(uint32_t command){assert(command==1);++opens;menu=true;return true;}}
int main(int argc,char**) {
    verified=argc==1;
    wchar_t temp[MAX_PATH]{},path[MAX_PATH]{};assert(GetTempPathW(MAX_PATH,temp)&&GetTempFileNameW(temp,L"qit",0,path));
    struct Cleanup {const wchar_t* path;~Cleanup(){DeleteFileW(path);}} cleanup{path};
    inventory.serviceSize=sizeof inventory;inventory.serviceVersion=InventoryService::AbiVersion;inventory.forEachInventoryItem=&Enumerate;
    items.serviceSize=sizeof items;items.serviceVersion=ItemService::AbiVersion;items.getItemInfo=&Info;
    PluginApi api{};api.apiSize=sizeof api;api.queryService=&Query;
    PluginContext context{};context.contextSize=sizeof context;context.api=&api;quickitems::Init(&context,path);
    Put(inv.data(),0,uint32_t(0x01020304));Put(inv.data(),16,uintptr_t(unit.data()));
    Put(unit.data(),0,uint32_t(4));Put(unit.data(),4,uint32_t(22));Put(unit.data(),8,uint32_t(17));Put(unit.data(),16,uintptr_t(itemData.data()));
    item.structSize=sizeof item;item.handle=42;item.classId=22;item.runtimeId=17;item.code=Code('t','b','k');item.quantity=3;item.container=Items::ItemContainer::Inventory;
    auto s=Capture();
    if(!verified){assert(!s.quick.known&&!s.quick.count&&!uses);std::puts("Unknown native signatures disable quick use");return 0;}
    assert(s.quick.known&&s.quick.count==1&&s.quick.items[0].quantity==3);
    auto bind=RequestFor(Action::BindItem,s);assert(quickitems::Assign(bind,s));
    quickitems::Init(&context,path);s=Capture();assert(s.quick.assigned==item.code); // persistent setting
    auto r=RequestFor(Action::UseItem,s);assert(quickitems::Use(r,s)&&uses==1&&!opens);
    menu=true;assert(!quickitems::Use(r,s));menu=false;
    ++session;assert(!quickitems::Use(r,s));--session;
    item.container=Items::ItemContainer::PersonalStash;assert(!quickitems::Use(r,s));assert(!Capture().quick.count);item.container=Items::ItemContainer::Inventory;
    item.quantity=0;assert(!quickitems::Use(r,s));assert(Capture().quick.items[0].quantity==0);item.quantity=3;
    item.handle=43;assert(!quickitems::Use(r,s));item.handle=42;
    usable=false;assert(!quickitems::Use(r,s));assert(!Capture().quick.count);usable=true;
    local=false;assert(!quickitems::Use(r,s));local=true;
    Put(itemData.data(),0xb0,uintptr_t(unit.data()));assert(!Capture().quick.known&&!quickitems::Use(r,s));Put(itemData.data(),0xb0,uintptr_t(0));
    assert(uses==1&&!opens); // failed checks never dispatch or open inventory
    item.code=Code('i','b','k');s=Capture();bind=RequestFor(Action::BindItem,s);assert(quickitems::Assign(bind,s));
    s=Capture();r=RequestFor(Action::UseItem,s);assert(quickitems::Use(r,s)&&uses==2&&opens==1&&menu);
    menu=false;item.quantity=0;s=Capture();r=RequestFor(Action::UseItem,s);assert(!quickitems::Use(r,s)&&opens==1);
    Request clear{Action::BindItem,2,session,GetTickCount64(),0,{},s.quick.assigned};assert(quickitems::Assign(clear,s));
    s=Capture();assert(!s.quick.assigned&&!quickitems::Use(r,s));
    assert(!quickitems::Assign(bind,s)); // old picker cannot overwrite new assignment
    // Reset Skills & Stats (vr/respec.cpp): the minted token found in the client's own
    // list by the identity the game gave it (runtime id 17, class 22), menu open.
    using quickitems::UseResult;
    item.code=Code('t','o','a');item.quantity=1;menu=true;const int before=uses;
    const quickitems::ClientItem token{17,22,Code('t','o','a')};
    assert(quickitems::UseClientItem(session,token)==UseResult::Used&&uses==before+1&&opens==1);
    uint32_t seen[4]{};unsigned count=9;
    assert(quickitems::UseClientItem(session,{18,22,Code('t','o','a')},seen,4,&count)==UseResult::Missing&&count==1&&seen[0]==17);
    assert(quickitems::UseClientItem(session,{17,23,Code('t','o','a')})==UseResult::Missing);
    assert(quickitems::UseClientItem(session,{17,22,Code('h','p','1')})==UseResult::WrongCode);
    assert(quickitems::UseClientItem(session+1,token)==UseResult::NoPlayer&&quickitems::UseClientItem(0,token)==UseResult::NoPlayer);
    usable=false;assert(quickitems::UseClientItem(session,token)==UseResult::Unusable);usable=true;
    local=false;assert(quickitems::UseClientItem(session,token)==UseResult::ListUnreadable);local=true;
    bool holds=false;assert(quickitems::HoldsClientItem(session,token,holds)&&holds);
    assert(quickitems::HoldsClientItem(session,{18,22,Code('t','o','a')},holds)&&!holds);
    local=false;assert(!quickitems::HoldsClientItem(session,token,holds));local=true;
    assert(uses==before+1&&opens==1);menu=false; // refusals never dispatch
    std::puts("Quick-use ownership, native dispatch, empty tomes, targeting, cancellation, persistence and corrupt-list guards passed");
}
