#include "../cleanroom/camera/monster_bars.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

struct Fixture {
    static constexpr uintptr_t base=0x100000,unit=base,data=base+0x100,row=base+0x200;
    std::array<unsigned char,0x300> bytes{};
    uintptr_t fail=0;
    unsigned reads=0;
    template<class T> void Put(uintptr_t address,T value){std::memcpy(bytes.data()+address-base,&value,sizeof value);}
    Fixture(){
        Put(unit,uint32_t(1));Put(unit+4,uint32_t(19));Put(unit+0x10,data);
        Put(data,row);Put(row,uint16_t(19));Put(row+0x3c,uint32_t((1u<<25)|(1u<<15)));
    }
    bool Hide(bool stereo=true,bool show=false,bool above=true){
        return d2rcam::monsterbars::Hide(stereo,show,above,unit,[&](uintptr_t at,auto& value){
            ++reads;
            if((fail>=at&&fail<at+sizeof value)||at<base||at-base+sizeof value>bytes.size())return false;   // a read fails as a whole
            std::memcpy(&value,bytes.data()+at-base,sizeof value);return true;
        });
    }
};
int main(){
    Fixture normal;
    assert(normal.Hide()); // Ordinary monster, both name and health use this anchor.
    assert(normal.Hide()); // The second eye re-evaluates the same unit.
    normal.reads=0;
    assert(!normal.Hide(false)&&!normal.Hide(true,true)&&!normal.Hide(true,false,false));
    assert(normal.reads==0); // No unit reads outside VR, when enabled or for ground anchors.
    assert(normal.Hide()&&normal.reads==3); // One guarded read per record: the unit, its monster data, its row.
    {Fixture f;f.Put(Fixture::unit,uint32_t(2));assert(!f.Hide()&&f.reads==1);} // No monster: the unit's record only.
    {Fixture f;f.Put(Fixture::data+0x1a,uint8_t(1));assert(!f.Hide()&&f.reads==2);} // A champion: its row is not read.
    for(unsigned bit=0;bit<8;++bit){
        Fixture elite;elite.Put(Fixture::data+0x1a,uint8_t(1u<<bit));assert(!elite.Hide());
    }
    for(unsigned bit:{6,7,8,9,10}){
        Fixture keep;keep.Put(Fixture::row+0x3c,uint32_t((1u<<25)|(1u<<15)|(1u<<bit)));assert(!keep.Hide());
    }
    // Act bosses retain bars even with no unique/champion flags.
    for(uint32_t boss:{156,211,242,243,544}){
        Fixture f;f.Put(Fixture::unit+4,boss);f.Put(Fixture::row,uint16_t(boss));
        f.Put(Fixture::row+0x3c,uint32_t((1u<<25)|(1u<<15)|(1u<<6)|(1u<<7)));assert(!f.Hide());
    }
    for(uint32_t type:{0,2,3,4,5}){Fixture f;f.Put(Fixture::unit,type);assert(!f.Hide());}
    for(uintptr_t at:{Fixture::unit,Fixture::unit+4,Fixture::unit+0x10,Fixture::data,
                      Fixture::data+0x1a,Fixture::row,Fixture::row+0x3c}){
        Fixture f;f.fail=at;assert(!f.Hide());
    }
    {Fixture f;f.Put(Fixture::unit+0x10,uintptr_t(0));assert(!f.Hide());}
    {Fixture f;f.Put(Fixture::data,uintptr_t(0));assert(!f.Hide());}
    {Fixture f;f.Put(Fixture::row,uint16_t(20));assert(!f.Hide());}
    for(uint32_t flags:{0u,1u<<15,1u<<25}){
        Fixture f;f.Put(Fixture::row+0x3c,flags);assert(!f.Hide());
    }
    // No previous ordinary target can suppress a subsequent boss or vice versa.
    normal.Put(Fixture::data+0x1a,uint8_t(8));assert(!normal.Hide());
    normal.Put(Fixture::data+0x1a,uint8_t(0));assert(normal.Hide());
    std::puts("Monster bar filtering, protected labels, eye passes and failed reads passed");
}
