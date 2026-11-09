#include "monster_immunity.h"
#include "monster_position.h"
#include <cassert>
#include <climits>
#include <cstring>
#include <limits>
#include <vector>
using namespace framevr::immunity;
static void NativePosition() {
    std::vector<uint8_t> memory(0x20000);
    const auto put=[&](uintptr_t address,const auto& value){std::memcpy(memory.data()+address,&value,sizeof value);};
    const auto ptr=[&](uintptr_t address,uint64_t value){put(address,value);};
    const auto word=[&](uintptr_t address,uint32_t value){put(address,value);};
    uintptr_t unreadable=0;unsigned reads=0;
    const auto read=[&](uintptr_t address,auto& value) {
        ++reads;
        if(!address||address==unreadable||address>memory.size()-sizeof value)return false;
        std::memcpy(&value,memory.data()+address,sizeof value);return true;
    };
    constexpr uintptr_t manager=0x4000,registry=0x2000,unit=0x3000;
    constexpr uint32_t entity=0x100007;
    ptr(unit+0xd8,123);ptr(manager+0x1738,1);ptr(manager+0x1740,0x6000);
    ptr(0x6000,0xd000);ptr(0xd008,123);ptr(0xd018,0xf000);word(0xf0a8,entity);
    ptr(registry+0x30,0x10000);ptr(registry+0x38,0x10020);word(0x1001c,entity);
    ptr(registry,0x7000);ptr(registry+8,0x7028);ptr(0x7020,0x8000);
    ptr(0x8008,0x9000);ptr(0x8010,0x9008);ptr(0x9000,0xa000);word(0xa01c,3);
    ptr(0x8020,0xb000);ptr(0x8028,0xb010);word(0xb00c,entity);ptr(0x8038,0xc000);
    const auto position=framevr::coordinates::RenderGround({6,0,8},-17);
    put(0xc000+3*0xd0+0x70,position);
    float out[3]{};
    const auto get=[&]{return RenderedPosition(manager,registry,0,unit,read,out);};
    assert(get()&&out[0]==position.x&&out[1]==-17&&out[2]==position.z);
    ptr(manager+0x1738,512);ptr(0x6000,0);ptr(0x6000+168*8,0xd000);assert(get()); // native key hash
    ptr(0x6000+168*8,0);assert(!get());ptr(0x6000,0xd000);ptr(manager+0x1738,1);
    ptr(0xd020,0x11000);word(0x11008,0);assert(!get()); // expired weak model reference
    word(0x11008,1);assert(get());ptr(0xd020,0);
    word(0x1001c,entity+0x100000);assert(!get());word(0x1001c,entity); // recycled entity
    word(0xb00c,entity+0x100000);assert(!get());word(0xb00c,entity); // stale component
    word(0xa01c,0xfffff);assert(!get());word(0xa01c,3); // component removed
    unreadable=0xf0a8;assert(!get());unreadable=0;
    ptr(manager+0x1738,3);assert(!get());ptr(manager+0x1738,1);
    ptr(0xd008,124);ptr(0xd000,0xd000);reads=0;assert(!get()&&reads<300); // cyclic chain bounded
    ptr(0xd008,123);ptr(0xd000,0);
    auto invalid=position;invalid.y=std::numeric_limits<float>::quiet_NaN();
    put(0xc000+3*0xd0+0x70,invalid);assert(!get());
}
int main() {
    NativePosition();
    static_assert(Stats==std::array<int,6>{36,37,39,41,43,45});
    for(int type=0;type<6;++type)for(int resistance:{INT_MIN,-1,0,99,100,101,INT_MAX}) {
        const auto r=Detect([&](int stat,int& value){value=stat==Stats[type]?resistance:0;return true;});
        assert(r.known&&r.mask==(resistance>=100?1u<<type:0));
    }
    assert(Detect([](int,int& value){value=100;return true;}).mask==63);
    assert(!Detect([](int stat,int& value){value=100;return stat!=43;}).known);
    Snapshot s;s.session=7;s.at=100;
    assert(Fresh(s,7,100)&&Fresh(s,7,600));
    assert(!Fresh(s,8,100)&&!Fresh(s,0,100)&&!Fresh(s,7,99)&&!Fresh(s,7,601));
    for(int i=60;i>=1;--i)Insert(s,{uint32_t(i),float(i)/2,0,0,0,1},0,0);
    assert(s.count==MaxMonsters&&s.monsters.front().id==1&&s.monsters.back().id==24);
    Insert(s,{99,0,0,0,0,0},0,0);
    Insert(s,{99,0,0,0,0,64},0,0);
    Insert(s,{99,std::numeric_limits<float>::quiet_NaN(),0,0,0,1},0,0);
    Insert(s,{99,33,0,0,0,1},0,0);
    assert(s.count==MaxMonsters&&s.monsters.front().id==1);
    Snapshot previous;previous.session=7;previous.at=100;previous.count=1;previous.monsters[0]={1,1,2,1,2,1};
    Snapshot current;current.session=7;current.at=200;current.count=1;current.monsters[0]={1,3,4,0,0,5};
    Connect(current,previous);
    assert(Position(current.monsters[0],current,200)==(std::array<float,2>{1,2}));
    assert(Position(current.monsters[0],current,250)==(std::array<float,2>{2,3}));
    assert(Position(current.monsters[0],current,300)==(std::array<float,2>{3,4}));
    assert(current.monsters[0].mask==5); // new immunity applies immediately, only position eases
    previous.session=8;Connect(current,previous);
    assert(Position(current.monsters[0],current,200)==(std::array<float,2>{3,4}));
    previous.session=7;previous.monsters[0].x=20;Connect(current,previous);
    assert(current.monsters[0].previousX==3); // teleport
    previous.at=1;current.at=1000;Connect(current,previous);
    assert(current.monsters[0].previousX==3); // stale world
    previous.at=100;current.at=200;previous.monsters[0]={1,1,2,0,0,1,-3};
    current.monsters[0]={1,3,4,0,0,1,3};Connect(current,previous);
    const auto at=FootPosition(current.monsters[0],current,250,{0,9,0});
    assert(std::abs(at.x-4)<.0001f&&at.y==-9&&std::abs(at.z-6)<.0001f);
    const auto shifted=FootPosition(current.monsters[0],current,250,{2,-9,4});
    assert(shifted.x==at.x-2&&shifted.y==9&&shifted.z==at.z-4);
    // Vertical teleports and new sessions snap to the monster, never the hero's floor.
    current.monsters[0].height=17;Connect(current,previous);
    assert(current.monsters[0].previousHeight==17);
    const auto elevated=FootPosition(current.monsters[0],current,200,{0,0,0});
    assert(elevated.y==17&&std::abs(elevated.x-(6+17*framevr::coordinates::GroundShear))<.0001f);
    Snapshot invalidHeight;Insert(invalidHeight,{1,0,0,0,0,1,std::numeric_limits<float>::infinity()},0,0);
    assert(invalidHeight.count==0);
}
