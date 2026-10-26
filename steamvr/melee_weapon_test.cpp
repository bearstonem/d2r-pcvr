// Actual native selector/scope code with synthetic game functions and units.
#include "../vr/melee_weapon.cpp"
#undef NDEBUG
#include <cassert>
#include <vector>
#include <thread>
#include <array>
using namespace meleeweapon;
namespace {
bool signatures=false,hookOk=false,usable=true;
int installs=0;
uint32_t player[3]={0,4,17},other[3]={0,4,18};
uint32_t right[3]={4,100,41},left[3]={4,101,42},inventory=0x01020304;
uintptr_t P(){return reinterpret_cast<uintptr_t>(player);}
uintptr_t R(){return reinterpret_cast<uintptr_t>(right);}
uintptr_t L(){return reinterpret_cast<uintptr_t>(left);}
thread_local std::vector<uintptr_t> merges;
uintptr_t FakeWeapon(uintptr_t p){return p==P()?R():0;}
uintptr_t FakeInventory(uintptr_t p){assert(p==P());return reinterpret_cast<uintptr_t>(&inventory);}
uintptr_t FakeComposite(uintptr_t p,uint8_t side){assert(p==reinterpret_cast<uintptr_t>(&inventory));assert(side==5||side==6);return side==5?R():L();}
int FakeType(uintptr_t p,int type){assert(p==R()||p==L());assert(type==45);return 1;}
int FakeUsable(uintptr_t p){assert(p==R()||p==L());return usable;}
void FakeMerge(uintptr_t p,int mode){assert(p==P()&&mode==0);merges.push_back(CurrentWeapon(p));}
}
namespace d2rsig {
bool Check(uint64_t,const void*,uint32_t){return signatures;}
bool Hook(uint64_t rva,const void*,uint32_t,void* detour,void** original) {
    assert(rva==abi::WeaponRva&&detour==reinterpret_cast<void*>(&CurrentWeapon));++installs;
    if(!hookOk)return false;*original=reinterpret_cast<void*>(&FakeWeapon);return true;
}
uintptr_t Addr(uint64_t rva) {
    if(rva==abi::InventoryRva)return reinterpret_cast<uintptr_t>(&FakeInventory);
    if(rva==abi::CompositeRva)return reinterpret_cast<uintptr_t>(&FakeComposite);
    if(rva==abi::TypeRva)return reinterpret_cast<uintptr_t>(&FakeType);
    if(rva==abi::UsableRva)return reinterpret_cast<uintptr_t>(&FakeUsable);
    if(rva==abi::MergeRva)return reinterpret_cast<uintptr_t>(&FakeMerge);
    assert(false);return 0;
}
}
namespace framevr::logging {void Write(const char*){}}
int main() {
    framevr::dualwield::Equipment e{{41,42},{D2RVR_TYPE_SWORD,D2RVR_TYPE_AXE},{41,42}};
    Install();assert(!Ready()&&installs==0&&!Select(P(),1,e));
    signatures=true;Install();assert(!Ready()&&installs==1);
    hookOk=true;Install();assert(Ready()&&installs==2);
    Install();assert(installs==2);
    assert(Select(P(),0,e)==R()&&Select(P(),1,e)==L());
    std::swap(e.id[0],e.id[1]);std::swap(e.type[0],e.type[1]);
    assert(Select(P(),0,e)==R()&&Select(P(),1,e)==L());
    assert(e.BodyHand(0)==1&&e.BodyHand(1)==0&&e.Type(0)==D2RVR_TYPE_SWORD);
    e.id[0]=99;assert(!Select(P(),1,e));e.id[0]=42;
    player[1]=3;assert(!Select(P(),1,e));player[1]=4;
    left[0]=1;assert(!Select(P(),1,e));left[0]=4;
    usable=false;assert(!Select(P(),1,e));usable=true;
    inventory=0;assert(!Select(P(),1,e));inventory=0x01020304;
    assert(!Select(P(),-1,e)&&!Select(P(),2,e));
    e.type[0]=D2RVR_TYPE_BOW;assert(!Select(P(),1,e));e.type[0]=D2RVR_TYPE_AXE;
    e.id[0]=e.id[1];assert(!Select(P(),1,e));e.id[0]=42;
    assert(CurrentWeapon(P())==R());
    {
        Scope scope(P(),L());assert(CurrentWeapon(P())==L());
        assert(CurrentWeapon(reinterpret_cast<uintptr_t>(other))==0);
        {Scope nested(P(),R());assert(CurrentWeapon(P())==R());}
        assert(CurrentWeapon(P())==L());
        std::thread separate([]{assert(CurrentWeapon(P())==R());});separate.join();
    }
    assert(CurrentWeapon(P())==R());
    assert((merges==std::vector<uintptr_t>{L(),R(),L(),R()}));
    {Scope unselected(P(),0);assert(CurrentWeapon(P())==R());}
    assert(merges.size()==4);
    // No unit identity or inventory data was rewritten by scoping attacks.
    assert(player[1]==4&&right[2]==41&&left[2]==42&&inventory==0x01020304);

    // Assassin dual claws use the same rendered-hand selection, independently
    // of inventory/equip order. Each hit refreshes that claw's native stats
    // and restores normal selection on exit.
    player[1]=6;
    e={{41,42},{D2RVR_TYPE_CLAW,D2RVR_TYPE_CLAW},{41,42}};
    assert(framevr::dualwield::Eligible(6,e));
    for(bool reversed:{false,true}) {
        if(reversed)std::swap(right[2],left[2]);
        e.displayed[0]=right[2];e.displayed[1]=left[2];
        assert(Select(P(),0,e)==R()&&Select(P(),1,e)==L());
        assert(e.BodyHand(0)==int(reversed)&&e.BodyHand(1)==int(!reversed));
        merges.clear();
        for(int hand=0;hand<2;++hand) {
            Scope selected(P(),Select(P(),hand,e));
            assert(CurrentWeapon(P())==(hand?L():R()));
            std::thread separate([]{assert(CurrentWeapon(P())==R());});separate.join();
        }
        assert(CurrentWeapon(P())==R());
        assert((merges==std::vector<uintptr_t>{R(),R(),L(),R()}));
    }
    std::swap(right[2],left[2]);e.displayed[0]=41;e.displayed[1]=42;
    usable=false;assert(!Select(P(),0,e)&&!Select(P(),1,e));usable=true;
    e.id[1]=99;assert(!Select(P(),0,e)&&!Select(P(),1,e));e.id[1]=42;
    e.id[1]=41;assert(!framevr::dualwield::Eligible(6,e));e.id[1]=42;
    e.id[1]=0;assert(!framevr::dualwield::Eligible(6,e));e.id[1]=42;
    for(int hero:{-1,0,1,2,3,4,5,7}) {
        player[1]=uint32_t(hero);
        assert(!Select(P(),0,e)&&!Select(P(),1,e));
    }
    player[1]=6;
    // Only a pair of claws qualifies: no claw/shield, mixed melee pair,
    // empty off-hand, ranged weapon, unknown type or Barbarian weapon pair.
    for(uint32_t type:{D2RVR_TYPE_UNKNOWN,D2RVR_TYPE_UNARMED,D2RVR_TYPE_BOW,
                       D2RVR_TYPE_CROSSBOW,D2RVR_TYPE_SWORD,D2RVR_TYPE_AXE,D2RVR_TYPE_OTHER}) {
        e.type[0]=type;assert(!Select(P(),0,e)&&!Select(P(),1,e));e.type[0]=D2RVR_TYPE_CLAW;
        e.type[1]=type;assert(!Select(P(),0,e)&&!Select(P(),1,e));e.type[1]=D2RVR_TYPE_CLAW;
    }
    e.type[0]=D2RVR_TYPE_SWORD;e.type[1]=D2RVR_TYPE_AXE;
    assert(!Select(P(),0,e)&&!Select(P(),1,e));
    player[1]=4;
    assert(Select(P(),0,e)==R()&&Select(P(),1,e)==L()); // Barbarian regression
    assert(right[2]==41&&left[2]==42&&inventory==0x01020304);
    // Double Throw (#224): only a Barbarian's throwing weapon in each hand.
    e.type[0]=D2RVR_TYPE_THROWING;e.type[1]=D2RVR_TYPE_JAVELIN;
    assert(framevr::dualwield::ThrownPair(4,e)&&!framevr::dualwield::ThrownPair(6,e));
    e.type[1]=D2RVR_TYPE_SWORD;assert(framevr::dualwield::Eligible(4,e)&&!framevr::dualwield::ThrownPair(4,e));
    e.type[1]=D2RVR_TYPE_THROWING;e.id[1]=0;assert(!framevr::dualwield::ThrownPair(4,e));e.id[1]=42;
}
