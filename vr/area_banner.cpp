// Keep D2R's LevelNamePanel, TextBoxWidget, localization and fade animation.
// Place it within the headset view and append the native terror level.
#include <windows.h>
#include <intrin.h>
#include "area_banner.h"
#include "../steamvr/area_banner.h"
#include "journal_ui.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"

namespace areabanner {
namespace {
using framevr::areabanner::TerrorInfo;
struct Text {const char* data;size_t size;};
static_assert(sizeof(Text)==16);
using SetTextFn=void(*)(void*,const Text*);
SetTextFn original=nullptr;
using UpdateFn=void(*)(void*);
UpdateFn originalUpdate=nullptr;
void Update(void* panel) {
    auto call=reinterpret_cast<UpdateFn const volatile&>(originalUpdate);
    if(!call)return;
    uintptr_t children[2]{};
    const bool readable=panel&&framevr::ReadMemory(children,
        reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(panel)+0x198),sizeof children);
    if(!framevr::areabanner::CanUpdate(readable,children[0],children[1])) {
        static uint64_t told=0;const auto now=GetTickCount64();
        if(!told||now-told>=5000){told=now;framevr::logging::Write(
            "Frame area banner: skipped incomplete native panel (NameText/DesecratedText missing)");}
        return;
    }
    // The native HD canvas occupies the top 16:9 band of the eye texture.
    // A desktop y anchor therefore puts this announcement near the headset's
    // upper edge. Express its baseline in full-eye height instead.
    static void* lastPanel=nullptr;static float nativeY=.4f;
    float anchor[2]{};
    auto* anchorAddress=static_cast<uint8_t*>(panel)+0x48;
    if(framevr::ReadMemory(anchor,anchorAddress,sizeof anchor)) {
        if(lastPanel!=panel){lastPanel=panel;nativeY=anchor[1];}
        const bool vr=hud::NativeToolbarHidden();
        const float y=vr?framevr::areabanner::ViewAnchor(
            framevr::areabanner::canvasWidth.load(std::memory_order_relaxed),
            framevr::areabanner::canvasHeight.load(std::memory_order_relaxed),nativeY):nativeY;
        if(anchor[1]!=y&&framevr::WriteMemory(anchorAddress+sizeof(float),&y,sizeof y)) {
            char line[160];std::snprintf(line,sizeof line,"Frame area banner: %s anchor %.3f -> %.3f",
                vr?"headset":"desktop",anchor[1],y);framevr::logging::Write(line);
        }
    }
    call(panel);
}
using RoomFn=uintptr_t(*)();
using LevelFn=int(*)(uintptr_t);
using TerrorFn=TerrorInfo*(*)(TerrorInfo*,int);
RoomFn roomNow=nullptr;LevelFn levelOf=nullptr;TerrorFn terror=nullptr;
constexpr uint64_t SetTextRva=0x86dd50,SubtitleReturn=0x14e5445;
uintptr_t subtitleReturnAddress=0;

void SetText(void* widget,const Text* text) {
    auto& slot=reinterpret_cast<SetTextFn const volatile&>(original);
    auto call=slot;
    for(int i=0;!call&&i<(1<<20);++i){YieldProcessor();call=slot;}
    if(!call)return;
    if(uintptr_t(_ReturnAddress())!=subtitleReturnAddress||!hud::NativeToolbarHidden()){
        call(widget,text);return;
    }
    // This is the game's own UI update, after it decided this area is terrorized.
    // Resolve the same current room as LevelNamePanel, not a stale UI snapshot.
    const auto room=roomNow();const int level=room?levelOf(room):0;
    TerrorInfo info{};Text source{};char native[512]{},output[544]{};
    if(level>0&&terror(&info,level)&&info.active&&
       framevr::ReadMemory(&source,text,sizeof source)&&source.data&&source.size<=sizeof native&&
       framevr::ReadMemory(native,source.data,source.size)&&
       framevr::areabanner::WithLevel({native,source.size},info,output,sizeof output)){
        const Text replacement{output,std::strlen(output)};
        call(widget,&replacement); // Native setter copies the view into its own string.
        return;
    }
    call(widget,text);
}
}

void Install() {
    if(original&&originalUpdate)return;
    static uint64_t next=0;const auto now=GetTickCount64();if(now<next)return;next=now+1000;
    // All three reported crashes were the NameText null dereference at 14e54d3.
    // Protect the update independently of subtitle-hook availability.
    if(!originalUpdate) {
        constexpr uint8_t entry[]={0x48,0x8b,0xc4,0x55,0x57,0x48,0x8b,0xec,0x48,0x83,0xec,0x68};
        constexpr uint8_t nameRead[]={0x48,0x8b,0x8f,0x98,0x01,0,0,0x33,0xd2,0x48,0x8b,0x01,0xff,0x50,0x48};
        constexpr uint8_t terrorRead[]={0x48,0x8b,0x8f,0xa0,0x01,0,0,0x33,0xd2,0x48,0x8b,0x01,0xff,0x50,0x48};
        if(d2rsig::Check(0x14e54ca,nameRead,sizeof nameRead)&&
           d2rsig::Check(0x14e54e8,terrorRead,sizeof terrorRead)&&
           d2rsig::Hook(0x14e51a0,entry,sizeof entry,reinterpret_cast<void*>(&Update),reinterpret_cast<void**>(&originalUpdate)))
            framevr::logging::Write("Frame area banner: missing-child crash guard installed");
    }
    if(original)return;
    // Verified in the loaded Battle.net game: both the caller and the callee,
    // plus the exact automap query/ABI. Unknown builds retain the stock text.
    constexpr uint8_t setSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x4c,0x8b,0x42,0x08,0x48,0x8b,0xd9,0x48,0x8b,0x12};
    constexpr uint8_t callSig[]={0x48,0x8d,0x55,0xc8,0x49,0x8b,0xcf,0xe8,0x0b,0x89,0x38,0xff};
    constexpr uint8_t roomSig[]={0x48,0x83,0xec,0x28,0xe8,0x47,0x11,0xff,0xff,0x8b,0xc8,0xe8,0xf0,0x02,0,0};
    constexpr uint8_t levelSig[]={0x48,0x85,0xc9,0x75,0x03,0x33,0xc0,0xc3,0x48,0x8b,0x49,0x18,0xe9,0x9f,0x13,0x07,0};
    constexpr uint8_t terrorSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x33,0xdb,0x8b,0xf2};
    // Automap consumes result +0 as level and +4 as active, then formats it.
    constexpr uint8_t resultSig[]={0xe8,0x9d,0xb8,0xc1,0xfe,0x40,0x38,0x74,0x24,0x4c,0x74,0x51,0x8b,0x5c,0x24,0x48};
    if(!d2rsig::Check(0x14e5439,callSig,sizeof callSig)||
       !d2rsig::Check(0x9a180,roomSig,sizeof roomSig)||!d2rsig::Check(0x2efc10,levelSig,sizeof levelSig)||
       !d2rsig::Check(0x90c70,terrorSig,sizeof terrorSig)||!d2rsig::Check(0x14753ce,resultSig,sizeof resultSig))return;
    roomNow=reinterpret_cast<RoomFn>(d2rsig::Addr(0x9a180));
    levelOf=reinterpret_cast<LevelFn>(d2rsig::Addr(0x2efc10));
    terror=reinterpret_cast<TerrorFn>(d2rsig::Addr(0x90c70));
    subtitleReturnAddress=d2rsig::Addr(SubtitleReturn);
    if(d2rsig::Hook(SetTextRva,setSig,sizeof setSig,reinterpret_cast<void*>(&SetText),reinterpret_cast<void**>(&original)))
        framevr::logging::Write("Frame area banner: native terror level added to native announcement text");
}
}
