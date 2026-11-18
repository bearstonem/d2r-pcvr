#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
namespace monsterlabels {void Install();}
namespace {
using Update=void(*)(uintptr_t,uintptr_t,uintptr_t,bool);
Update hook=nullptr;
bool marker=true;
int calls=0;
std::string first,second;
uintptr_t received=0;
template<class T> T Get(uintptr_t at){T x{};std::memcpy(&x,reinterpret_cast<void*>(at),sizeof x);return x;}
void Native(uintptr_t panel,uintptr_t event,uintptr_t group,bool attached) {
    assert(panel==1&&group==2&&attached);++calls;received=event;
    const auto data=Get<uintptr_t>(event+0x110);
    first=reinterpret_cast<const char*>(Get<uintptr_t>(data+0x20));
    second=reinterpret_cast<const char*>(Get<uintptr_t>(data+0x28));
}
uintptr_t Find(uintptr_t group,const char* name){assert(group==2&&!std::strcmp(name,"PCVRCompactDescriptions"));return marker?3:0;}
}
namespace framevr::logging {void Write(const char*){}}
namespace d2rsig {
bool Check(uint64_t,const void*,uint32_t){return true;}
uintptr_t Addr(uint64_t rva){assert(rva==0x855c90);return reinterpret_cast<uintptr_t>(&Find);}
bool Hook(uint64_t rva,const void*,uint32_t,void* fn,void** original){assert(rva==0x1f84e0);hook=reinterpret_cast<Update>(fn);*original=reinterpret_cast<void*>(&Native);return true;}
}
int main() {
    monsterlabels::Install();assert(hook);
    alignas(uintptr_t) std::array<uint8_t,0x118> message{};
    alignas(uintptr_t) std::array<uint8_t,0x40> data{};
    auto put=[](auto& bytes,size_t at,auto value){std::memcpy(bytes.data()+at,&value,sizeof value);};
    put(message,0x88,uint64_t(0x40ffba2d98a3ff97ULL));
    put(message,0x110,reinterpret_cast<uintptr_t>(data.data()));
    auto run=[&](const char* a,const char* b) {
        put(data,0x20,reinterpret_cast<uintptr_t>(a));put(data,0x28,reinterpret_cast<uintptr_t>(b));
        const auto beforeMessage=message;const auto beforeData=data;const int before=calls;
        hook(1,reinterpret_cast<uintptr_t>(message.data()),2,true);
        assert(calls==before+1&&message==beforeMessage&&data==beforeData);
    };
    run("Extra Strong", "Immune to Cold");assert(first=="Extra Strong\n\nImmune to Cold"&&second.empty());
    run("Extra Strong\nCold Enchanted", "Demon |cFF0000Immune to Fire");
    assert(first=="Extra Strong\nCold Enchanted\n\nDemon |cFF0000Immune to Fire"&&second.empty());
    run("", "Undead");assert(first.empty()&&second=="Undead");
    run("Cursed", "");assert(first=="Cursed"&&second.empty());
    run("", "");assert(first.empty()&&second.empty());
    marker=false;run("Extra Fast", "Immune to Cold");assert(first=="Extra Fast"&&second=="Immune to Cold");
    marker=true;put(message,0x88,uint64_t(0));run("Clear", "Other");
    assert(received==reinterpret_cast<uintptr_t>(message.data())&&first=="Clear"&&second=="Other");
    put(message,0x88,uint64_t(0x40ffba2d98a3ff97ULL));
    std::string tooLong(4096,'x');run(tooLong.c_str(),"Immune");assert(first==tooLong&&second=="Immune");
}
