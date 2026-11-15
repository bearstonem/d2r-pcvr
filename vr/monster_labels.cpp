#include "sigscan.h"
#include "memory.h"
#include "logging.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <string>

namespace monsterlabels {
namespace {
constexpr uint64_t UpdateRva=0x1f84e0,FindRva=0x855c90;
constexpr uint8_t UpdateSig[]={0x48,0x89,0x5c,0x24,0x18,0x44,0x88,0x4c,0x24,0x20,
    0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x57,0x41,0x54};
constexpr uint8_t FindSig[]={0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x48,0x83,0xec,0x20,
    0x4c,0x89,0x7c,0x24,0x68,0x48,0x8b,0xf2,0x48,0x8b,0xf9};
// Audited SetInfo payload: native UTF-8 descriptions at +20/+28, owner event
// at +110. SetNoInfo and unrelated layouts must keep their original behavior.
constexpr uint8_t InfoSig[]={0x4c,0x8b,0xb1,0x10,0x01,0x00,0x00};
constexpr uint8_t TextSig[]={0x49,0x8b,0x4e,0x20,0x80,0x39,0x00,0x75,0x04,0x49,0x8b,0x4e,0x28};
using Update=void(*)(uintptr_t,uintptr_t,uintptr_t,bool);
Update original=nullptr;
template<class T> bool Read(uintptr_t at,T& value) {
    return at&&framevr::ReadMemory(&value,reinterpret_cast<void*>(at),sizeof value);
}
bool Text(uintptr_t at,std::string& out) {
    if(!at)return false;
    // Bounded, page-local reads avoid one system call per character at VR Hz.
    for(size_t i=0;i<4096;){
        std::array<char,64> block{};
        const size_t n=std::min({block.size(),size_t(4096-i),size_t(4096-((at+i)&4095))});
        if(!framevr::ReadMemory(block.data(),reinterpret_cast<void*>(at+i),n))return false;
        const auto end=std::find(block.begin(),block.begin()+n,'\0');
        out.append(block.begin(),end);
        if(end!=block.begin()+n)return true;
        i+=n;
    }
    return false;
}
void OnUpdate(uintptr_t panel,uintptr_t event,uintptr_t group,bool attached) {
    // Never modify the game's event or its strings. The synchronous native
    // setter copies the temporary string view into the original Unique widget.
    alignas(uintptr_t) std::array<uint8_t,0x118> message{};
    alignas(uintptr_t) std::array<uint8_t,0x40> info{};
    uint64_t kind=0;uintptr_t data=0,first=0,second=0;
    std::string abilities,additional;
    if(Read(event+0x88,kind)&&kind==0x40ffba2d98a3ff97ULL&&
       reinterpret_cast<uintptr_t(*)(uintptr_t,const char*)>(d2rsig::Addr(FindRva))(group,"PCVRCompactDescriptions")&&
       Read(event,message)&&Read(event+0x110,data)&&Read(data,info)) {
        std::memcpy(&first,info.data()+0x20,sizeof first);
        std::memcpy(&second,info.data()+0x28,sizeof second);
        if(Text(first,abilities)&&Text(second,additional)&&!abilities.empty()&&!additional.empty()) {
            // Preserve native localization and embedded color/immune markers.
            // One newline flows directly after the last wrapped ability line.
            const std::string combined=abilities+'\n'+additional;
            const uintptr_t joined=reinterpret_cast<uintptr_t>(combined.c_str());
            const uintptr_t empty=reinterpret_cast<uintptr_t>("");
            const uintptr_t copy=reinterpret_cast<uintptr_t>(info.data());
            std::memcpy(info.data()+0x20,&joined,sizeof joined);
            std::memcpy(info.data()+0x28,&empty,sizeof empty);
            std::memcpy(message.data()+0x110,&copy,sizeof copy);
            original(panel,reinterpret_cast<uintptr_t>(message.data()),group,attached);
            static bool told=false;
            if(!told){told=true;framevr::logging::Write("Frame monster labels: combined native abilities and immunity text");}
            return;
        }
    }
    original(panel,event,group,attached);
}
}
void Install() {
    if(original)return;
    static uint64_t retry=0;
    const auto now=GetTickCount64();if(now<retry)return;retry=now+5000;
    if(!d2rsig::Check(FindRva,FindSig,sizeof FindSig)||
       !d2rsig::Check(0x1f8705,InfoSig,sizeof InfoSig)||
       !d2rsig::Check(0x1f888c,TextSig,sizeof TextSig)||
       !d2rsig::Hook(UpdateRva,UpdateSig,sizeof UpdateSig,reinterpret_cast<void*>(&OnUpdate),reinterpret_cast<void**>(&original))) {
        framevr::logging::Write("Frame monster labels: compact text unavailable; native separate labels retained");return;
    }
    framevr::logging::Write("Frame monster labels: native descriptions flow in one wrapped text block");
}
}
