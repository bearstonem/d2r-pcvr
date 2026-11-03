#include "monster_immunity.h"
#include <cassert>
#include <climits>
#include <limits>
using namespace framevr::immunity;
int main() {
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
}
