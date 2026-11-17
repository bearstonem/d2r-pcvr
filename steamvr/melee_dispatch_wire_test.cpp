#include "melee_dispatch_wire.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace framevr::melee;
int main(){
    dispatch::Contact a{};a.hero=17;a.sequence=2;a.session=9;
    a.sweep.stroke=3;a.sweep.before={{1,2,3},{4,5,6},.04f};a.sweep.after={{2,3,4},{5,6,7},.05f};
    a.sweep.speed=8;a.sweep.travel=.4f;a.sweep.thrust=true;
    a.sample.active=true;a.sample.epoch=4;a.sample.equipment=5;a.sample.player={10,2,20};a.sample.unitsPerMetre=4;
    a.sample.handIndex=1;a.sample.claws=true;a.sample.strikeSkill=248;a.sample.skillOwner=-1;a.sample.skillSlot=2;
    a.sample.skillContext=37;a.sample.reach=.2f;a.sample.heft=.3f;a.sample.weight=.8f;a.sample.reactions=3;
    a.cycle[0]=600;a.cycle[1]=400;a.shape=1;
    dispatch::Bytes bytes{};assert(dispatch::Encode(a,bytes));assert(bytes.size()<=224);
    dispatch::Contact b{};assert(dispatch::Decode(bytes.data(),bytes.size(),b));
    assert(b.hero==17&&b.sequence==2&&b.session==9&&b.sweep.stroke==3&&b.sweep.before.tip.z==6&&b.sweep.after.tip.z==7);
    assert(b.sample.handIndex==1&&b.sample.claws&&b.sample.strikeSkill==248&&b.sample.skillOwner==-1&&b.sample.skillContext==37);
    assert(b.sweep.thrust&&b.sweep.travel==.4f&&b.sample.heft==.3f&&b.cycle[1]==400&&b.shape==1);
    assert(!dispatch::Decode(bytes.data(),bytes.size()-1,b));assert(!dispatch::Decode(nullptr,bytes.size(),b));
    auto bad=bytes;bad[0]^=1;assert(!dispatch::Decode(bad.data(),bad.size(),b));
    // A rejected decode must not replace the caller's last valid contact.
    b.hero=1234;bad=bytes;bad[dispatch::Size-12]|=0x80;
    assert(!dispatch::Decode(bad.data(),bad.size(),b)&&b.hero==1234);
    for(int hand=0;hand<2;++hand)for(int shape=0;shape<3;++shape){
        a.sample.handIndex=hand;a.shape=shape;a.sample.claws=shape!=0;
        a.sample.weapons={{41,42},{D2RVR_TYPE_SWORD,D2RVR_TYPE_AXE},{42,41}};
        a.dualSpeed=0x123456789abcdef0ull;
        assert(dispatch::Encode(a,bytes)&&dispatch::Decode(bytes.data(),bytes.size(),b));
        assert(b.sample.weapons.BodyHand(hand)==1-hand&&b.shape==shape&&b.sample.claws==(shape!=0)&&b.dualSpeed==a.dualSpeed);
    }
    a.sample.shield=true;a.sample.claws=false;a.sample.bowBash=false;
    assert(dispatch::Encode(a,bytes)&&dispatch::Decode(bytes.data(),bytes.size(),b)&&b.sample.shield);
    a.sample.shield=false;a.sample.bowBash=true;
    assert(dispatch::Encode(a,bytes)&&dispatch::Decode(bytes.data(),bytes.size(),b)&&b.sample.bowBash);
    a.sample.unitsPerMetre=std::numeric_limits<float>::quiet_NaN();assert(!dispatch::Encode(a,bytes));
    a.sample.unitsPerMetre=4;a.sample.handIndex=2;assert(!dispatch::Encode(a,bytes));
    a.sample.handIndex=0;a.sequence=0;assert(!dispatch::Encode(a,bytes));
    puts("melee dispatch wire: original sweep metadata roundtrip and malformed rejection passed");
}
