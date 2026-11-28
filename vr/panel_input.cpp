#include <windows.h>
#include <imm.h>
#include <intrin.h>
#include <cstdio>
#include <atomic>
#include <cstring>
#include <utility>
#include <string>
#include "panel_input.h"
#include "panel_input_mode.h"
#include "render_resolution.h"
#include "sigscan.h"
#include "logging.h"
#include "settings_menu.h"
#include "journal_ui.h"
#include "MinHook.h"

namespace panelinput {
namespace {
// 3.3.93787: native client-size query used by UI root sizing, safe bounds,
// layout globals and cursor bounds. It reads GetClientRect through 0x1207b20,
// independently of the renderer's WM_SIZE. A 3840-wide desktop thus clips
// menus rendered into a 3072-wide VR source. Override this query only, leaving
// OS window management, swapchain presentation and other windows physical.
constexpr uint64_t CanvasSizeRva=0x6545d0;
constexpr uint8_t CanvasSizeSig[]={
    0x48,0x83,0xec,0x38,0xe8,0x97,0xd1,0x2a,0x00,0x48,0x8d,0x54,0x24,0x20,0x48,0x8b,
    0xc8,0xe8,0x3a,0x35,0xbb,0x00,0x8b,0x44,0x24,0x28,0x89,0x44,0x24,0x40,0x8b,0x44,
    0x24,0x2c,0x89,0x44,0x24,0x44,0x48,0x8b,0x44,0x24,0x40,0x48,0x83,0xc4,0x38,0xc3};
using CanvasSize=uint64_t(*)();
CanvasSize originalCanvasSize=nullptr;
bool canvasSizeIn=false;
uintptr_t canvasImageBase=0;
uint64_t OnCanvasSize(){
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const auto native=originalCanvasSize();
    if(framevr::panels::CursorBoundsCaller(caller-canvasImageBase)||
       framevr::panels::MonsterClipCaller(caller-canvasImageBase))return framevr::resolution::CursorSize(native);
    return framevr::panels::UiCanvasCaller(caller-canvasImageBase)?framevr::resolution::UiSize(native):native;
}
void InstallCanvasSize() {
    if(canvasSizeIn)return;
    if(!d2rsig::Check(CanvasSizeRva,CanvasSizeSig,sizeof CanvasSizeSig))return;
    // Validate the relative CALL at every allowed return site before hooking.
    for(auto address:framevr::panels::UiCanvasReturns) {
        uint8_t call[5]{0xe8};const int32_t delta=int32_t(CanvasSizeRva-address);
        std::memcpy(call+1,&delta,sizeof delta);
        if(!d2rsig::Check(address-5,call,sizeof call))return;
    }
    for(auto address:framevr::panels::CursorConversionReturns) {   // the cursor's conversions, direct calls
        uint8_t call[5]{0xe8};const int32_t delta=int32_t(CanvasSizeRva-address);
        std::memcpy(call+1,&delta,sizeof delta);
        if(!d2rsig::Check(address-5,call,sizeof call))return;
    }
    // The width helper uses a tail JMP, preserving its caller's return address.
    // Guard both that CALL and the tail JMP, plus the height helper's CALL.
    // The controller cursor's clamp likewise: its CALLs to 0x7f4d0 and that tail JMP.
    for(const auto site:{std::pair<uint64_t,uint64_t>{0x7f4ad,CanvasSizeRva},
                        {0x14fb25,0x7f510},{0x7f521,CanvasSizeRva},
                        {0x14e83c,0x7f4d0},{0x14e85b,0x7f4d0},{0x7f4e1,CanvasSizeRva}}) {
        uint8_t branch[5]{uint8_t(site.first==0x7f521||site.first==0x7f4e1?0xe9:0xe8)};
        const int32_t delta=int32_t(site.second-(site.first+5));
        std::memcpy(branch+1,&delta,sizeof delta);
        if(!d2rsig::Check(site.first,branch,sizeof branch))return;
    }
    canvasImageBase=d2rsig::Addr(CanvasSizeRva)-CanvasSizeRva;
    canvasSizeIn=d2rsig::Hook(CanvasSizeRva,CanvasSizeSig,sizeof CanvasSizeSig,
        reinterpret_cast<void*>(&OnCanvasSize),reinterpret_cast<void**>(&originalCanvasSize));
    if(canvasSizeIn) {
        framevr::resolution::UiSizeReady();
        framevr::logging::Write("Frame panels: native UI canvas follows applied VR resolution; physical window unchanged");
        framevr::logging::Write("Frame panels: native cursor width/height clamp follows full VR source");
        framevr::logging::Write("Frame panels: the cursor's window/game conversions and controller clamp follow full VR source");
    }
}
framevr::panels::InputModeLease lease;
framevr::panels::TypedKeys typed;   // the player's own keys let the keyboard layout in (Preserve)
framevr::panels::ItemTooltipHover itemHover;
framevr::panels::WorldInputMode worldInput;
using Activity=bool(*)(void*);
using Request=bool(*)(int);
using Controller=bool(*)();
Activity originalActivity=nullptr;
Request originalRequest=nullptr;
Controller isController=nullptr;
Controller originalCursorSnap=nullptr;
using Dispatch=void(*)(const void*);
using Focus=void(*)(void*,bool);
Dispatch originalDispatch=nullptr;
Focus originalFocus=nullptr;
Activity originalPointer[6]{};
bool pointerIn[6]{},activityIn=false,requestIn=false,controllerIn=false,dispatchIn=false,focusIn=false,cursorSnapIn=false;
std::atomic<bool> ready{false},reported{false};
std::atomic<bool> pointerFocus{false};
thread_local bool pointerContext=false;
thread_local bool bindingContext=false;

// 3.3.93787 inventory item tooltip producer, shared by player/vendor grids.
// Its controller query supplies the delayed flag in the queued tooltip style
// (rbp+0x114). TooltipsPanel waits 1000 ms only when that flag is set. Override
// this one query, leaving the rest of the native layout/controller state alone.
constexpr uint64_t TooltipDelayRva=0x2c3dca;
constexpr uint8_t TooltipDelaySig[]={0xe8,0x41,0x40,0xdb,0xff,0x4c,0x8d,0x44,0x24,0x20,0x88,0x85,0x14,0x01,0x00,0x00};
// Native comparison already checks widget eligibility, equipped slots, empty
// text and suppression before querying Shift. Keep those decisions intact.
constexpr uint64_t TooltipCompareRva=0x2c3f07;
constexpr uint8_t TooltipCompareSig[]={0xb9,0x10,0x00,0x00,0x00,0xe8,0xef,0x61,0xf4,0x00,0x85,0xc0,0x75,0x08,
    0x38,0x87,0x32,0x06,0x00,0x00,0x74,0x72};
constexpr uint64_t KeyRva=0x120a100;
constexpr uint8_t KeySig[]={0x48,0x83,0xec,0x28,0xff,0x15,0x86,0x6e,0xaa,0x00,0xc1,0xe8,0x0f,0x83,0xe0,0x01,0x48,0x83,0xc4,0x28,0xc3};
using Key=int(*)(int);
Key originalKey=nullptr;
std::atomic<bool> tooltipsReady{false};
uintptr_t tooltipDelayReturn=0,tooltipCompareReturn=0;
// Item grids resolve their hovered item during drawing, after pointer dispatch
// has ended. Their separate cursor-mode query must also use exact laser XY.
constexpr uint64_t ItemCursorRva=0x2a8a06;
constexpr uint8_t ItemCursorSig[]={0xe8,0xc5,0x67,0xea,0xff,0x84,0xc0,0x75,0x2c};
constexpr uint64_t ItemTooltipRva=0x2c3c30;
constexpr uint8_t ItemTooltipSig[]={0x40,0x55,0x53,0x57,0x48,0x8d,0xac,0x24,0xc0,0xfd,0xff,0xff,0x48,0x81,0xec,0x40,0x03,0x00,0x00};
using ItemTooltip=void(*)(uintptr_t,uintptr_t);
// FocusManager's hovered widget is this one (its +0x178): the item grids' own test for reading the
// cell under the cursor (ItemTooltipItem). Optional: without it the grid is taken as hovered.
constexpr uint64_t WidgetHoveredRva=0x876180;
constexpr uint8_t WidgetHoveredSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0x92,0xfe,0xfc,0xff,
    0x48,0x39,0x98,0x78,0x01,0x00,0x00,0x0f,0x94,0xc0};
using WidgetHovered=bool(*)(uintptr_t);
WidgetHovered widgetHovered=nullptr;
ItemTooltip originalItemTooltip=nullptr;
uintptr_t itemCursorReturn=0;
framevr::panels::TooltipDwell tooltipDwell;
std::atomic<uint64_t> hoverGeneration{0};
std::atomic<int> tooltipTiming{0};
// [dev] collection_log: what the game did with its pointer since the last line (HoverStats).
std::atomic<uint32_t> statMoves{0},statItemQueries{0},statItemNative{0},statTips{0},statTipItems{0},statTipShown{0};
std::atomic<uintptr_t> statLastWidget{0},statLastItem{0};

bool ItemHoverActive() {
    return tooltipsReady.load(std::memory_order_acquire)&&!bindingContext&&itemHover.Active(GetTickCount64());
}
bool Owns();   // below: the laser holds the game's pointer
std::atomic<uint64_t> itemTooltipAt{0};   // the game asked for an item's tooltip: its cursor is on one
void OnItemTooltip(uintptr_t widget,uintptr_t item) {
    const uint64_t now=GetTickCount64();
    if(item)itemTooltipAt.store(now);
    ++statTips;if(item){++statTipItems;statLastWidget=widget;statLastItem=item;}
    const bool hover=ItemHoverActive();
    const bool under=!item||!hover||!widgetHovered||widgetHovered(widget);
    const bool ready=item&&hover&&under&&tooltipDwell.Ready(widget,item,now,tooltipTiming.load(),hoverGeneration.load());
    item=framevr::panels::ItemTooltipItem(item,hover,under,Owns(),ready);   // "nothing here" always reaches the game
    if(item)++statTipShown;
    originalItemTooltip(widget,item);
}
int OnKey(int key) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    return framevr::panels::ItemCompareKey(ItemHoverActive(),caller==tooltipCompareReturn,key,
        [](int k){return originalKey(k);});
}
// [dev] With a folder %TEMP%\pcvr_code present, the game's decrypted code (its first 0x1400000 bytes)
// is written there once, as d2r_code.bin, for reading the item grids' hover offline (2026-11-28).
void DumpCodeOnce() {
    static bool done=false;if(done)return;done=true;
    char dir[MAX_PATH]{};GetTempPathA(MAX_PATH,dir);std::string folder=std::string(dir)+"pcvr_code";
    const DWORD attributes=GetFileAttributesA(folder.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY))return;
    FILE* file=nullptr;if(fopen_s(&file,(folder+"\\d2r_code.bin").c_str(),"wb")||!file)return;
    const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));static uint8_t page[4096];
    for(uintptr_t offset=0;offset<0x1400000;offset+=sizeof page) {
        SIZE_T got=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(base+offset),page,sizeof page,&got)||got!=sizeof page)
            std::memset(page,0,sizeof page);
        fwrite(page,1,sizeof page,file);
    }
    fclose(file);
    framevr::logging::Write("Frame panels: [dev] the game's code written to %TEMP%\\pcvr_code\\d2r_code.bin");
}
void InstallTooltips() {
    if(tooltipsReady.load())return;
    // Optional feature: unsupported tooltip code must not disable laser input.
    if(!d2rsig::Check(TooltipDelayRva,TooltipDelaySig,sizeof TooltipDelaySig)||
       !d2rsig::Check(TooltipCompareRva,TooltipCompareSig,sizeof TooltipCompareSig)||
       !d2rsig::Check(ItemCursorRva,ItemCursorSig,sizeof ItemCursorSig)||
       (!originalItemTooltip&&!d2rsig::Check(ItemTooltipRva,ItemTooltipSig,sizeof ItemTooltipSig))||
       !d2rsig::Check(KeyRva,KeySig,sizeof KeySig))return;
    tooltipDelayReturn=d2rsig::Addr(TooltipDelayRva)+5;
    tooltipCompareReturn=d2rsig::Addr(TooltipCompareRva)+10;
    itemCursorReturn=d2rsig::Addr(ItemCursorRva)+5;
    if(!widgetHovered&&d2rsig::Check(WidgetHoveredRva,WidgetHoveredSig,sizeof WidgetHoveredSig))
        widgetHovered=reinterpret_cast<WidgetHovered>(d2rsig::Addr(WidgetHoveredRva));
    if(!widgetHovered)framevr::logging::Write("Frame panels: the grids' hovered-widget test not found; a grid the laser left may keep its item's tooltip");
    if(!originalItemTooltip&&!d2rsig::Hook(ItemTooltipRva,ItemTooltipSig,sizeof ItemTooltipSig,reinterpret_cast<void*>(&OnItemTooltip),reinterpret_cast<void**>(&originalItemTooltip)))return;
    if(d2rsig::Hook(KeyRva,KeySig,sizeof KeySig,reinterpret_cast<void*>(&OnKey),reinterpret_cast<void**>(&originalKey))) {
        tooltipsReady.store(true,std::memory_order_release);
        DumpCodeOnce();
        framevr::logging::Write("Frame panels: exact item hover, selectable tooltip timing and native equipped-item comparison installed");
    }
}

// 3.3.93787: the input-event preflight requests KB/M then clears every pad's
// state. Returning false means "no layout transition pending", so dispatch
// continues to the existing widgets. Do not return true (consumes the event),
// or call the preflight and merely suppress its mode setter (still clears pads).
constexpr uint64_t ActivityRva=0x13b480;
constexpr uint8_t ActivitySig[]={0x48,0x83,0xec,0x48,0xe8,0xf7,0xc8,0xf3,0xff,0x84,0xc0,0x0f,0x85,0xc2,0x01,0x00,0x00,
    0xe8,0x7a,0xc9,0xf3,0xff,0x84,0xc0,0x0f,0x84,0xb5,0x01,0x00,0x00};
constexpr uint64_t ActivityRequestRva=0x13b5ea;
constexpr uint8_t ActivityRequestSig[]={0x33,0xc9,0xe8,0x3f,0xc0,0x70,0x00,0x48,0x8d,0xbb,0x40,0x0e,0x00,0x00,0x0f,0xb6,0xf0};
// The shared mode request forwards (manager,method) to the native transition;
// method=0 is KB/M, method=1 controller. Keep real controller requests intact.
constexpr uint64_t RequestRva=0x847630;
constexpr uint8_t RequestSig[]={0x8b,0xd1,0x48,0x8b,0x0d,0x37,0x8b,0xbf,0x02,0x48,0x85,0xc9,0x0f,0x85,0xfe,0xdd,0x05,0x00,0x32,0xc0,0xc3};
constexpr uint64_t ControllerRva=0x77e10;
constexpr uint8_t ControllerSig[]={0x48,0x83,0xec,0x28,0xe8,0xf7,0xe1,0xff,0xff,0x80,0xb8,0xc8,0x0d,0x00,0x00,0x00,
    0x75,0x10,0x80,0x3d,0x4b,0xb4,0x9a,0x02,0x00};
constexpr uint64_t DispatchRva=0x843d90;
constexpr uint8_t DispatchSig[]={0x40,0x53,0x56,0x57,0x48,0x83,0xec,0x20,0x4c,0x89,0x7c,0x24,0x58,0x4c,0x8b,0xf9};
constexpr uint64_t MessageLayoutRva=0x866fbd;
constexpr uint8_t MessageLayoutSig[]={0x48,0x8b,0x86,0x88,0x00,0x00,0x00,0x48,0xb9,0x8c,0x93,0x7c,0xac,0x2c,0x17,0xd5,0x02};
constexpr uint64_t FocusRva=0x8676e0;
constexpr uint8_t FocusSig[]={0x40,0x55,0x41,0x57,0x48,0x8d,0xac,0x24,0x38,0xfd,0xff,0xff,0x48,0x81,0xec,0xc8,0x03,0x00,0x00};
// FocusManager calls this after dispatching MouseMove. In controller layout
// it replaces the exact hovered widget with the closest controller cursor
// region (867bd0..867ce3), stealing Yes/No focus from NpcDialogPanel. This is
// NOT the controller-layout query above. Never change its native global flag.
constexpr uint64_t CursorSnapRva=0x14f1d0;
constexpr uint8_t CursorSnapSig[]={0x0f,0xb6,0x05,0x25,0x95,0x93,0x02,0xc3};
constexpr uint64_t FocusSnapRva=0x867bd0;
constexpr uint8_t FocusSnapSig[]={0xe8,0xfb,0x75,0x8e,0xff,0x84,0xc0,0x0f,0x84,0xac,0x01,0x00,0x00};
// Native UISystem event callbacks: move, left down/up, right down/up, scroll.
// All accept the native event pointer and return whether it was consumed.
constexpr uint64_t PointerRvas[]={0x83f340,0x83f400,0x83f870,0x83fcc0,0x840120,0x840dc0};
constexpr uint8_t MoveSig[]={0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0xe8,0x32,0x8a,0x83,0xff};
constexpr uint8_t ButtonSig[]={0x48,0x89,0x74,0x24,0x20,0x55,0x48,0x8d,0xac,0x24,0xe0,0xfd,0xff,0xff,0x48,0x81,0xec,0x20,0x03,0x00,0x00};

bool Owns(){return ready.load(std::memory_order_acquire)&&lease.Active(GetTickCount64());}
bool ControllerMode(){
    if(bindingContext)return true;
    if(pointerContext)return false;
    if(ItemHoverActive()&&tooltipTiming.load()!=2&&reinterpret_cast<uintptr_t>(_ReturnAddress())==tooltipDelayReturn)return false;
    return isController();
}
bool CursorSnap(){
    const bool itemQuery=reinterpret_cast<uintptr_t>(_ReturnAddress())==itemCursorReturn;
    const bool snap=framevr::panels::ItemCursorSnap(ItemHoverActive(),itemQuery,
        framevr::panels::CursorSnapAllowed(pointerContext,bindingContext)&&originalCursorSnap());
    if(itemQuery){++statItemQueries;if(snap)++statItemNative;}
    return snap;
}
template<int I> bool OnPointer(void* event) {
    if constexpr(I==0)++statMoves;
    return framevr::panels::RoutePointer(Owns(),pointerContext,[&]{return originalPointer[I](event);});
}
constexpr Activity pointerHooks[]={&OnPointer<0>,&OnPointer<1>,&OnPointer<2>,&OnPointer<3>,&OnPointer<4>,&OnPointer<5>};
void OnDispatch(const void* message) {
    uint64_t target=0,command=0;
    if(message){std::memcpy(&target,message,8);std::memcpy(&command,static_cast<const char*>(message)+0x88,8);}
    framevr::panels::PointerScope scope(pointerContext,Owns()&&framevr::panels::PointerMessage(target,command));
    originalDispatch(message);
}
// FocusManager's own clear of a focus no control is near (0x867cf9..0x867d4e) and its mouse path
// (0x867d89: hovered +0x178, vtable +0xb0 takes the focus). Checked before they are mirrored.
constexpr uint64_t FocusClearRva=0x867cf9;
constexpr uint8_t FocusClearSig[]={0x49,0x83,0xbf,0x90,0x01,0x00,0x00,0x00,0x74,0x4b,0x41,0x80,0xbf,0x80,0x01,0x00,0x00,0x00,
    0x49,0x8b,0xbf,0x88,0x01,0x00,0x00,0x49,0x8b,0x8f,0x90,0x01,0x00,0x00,0x74,0x21,0x49,0xc7,0x87,0x88,0x01,0x00,0x00,
    0x00,0x00,0x00,0x00,0x49,0xc7,0x87,0x90,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x48,0x85,0xc9,0x74,0x06,0x48,0x8b,0x01,
    0xff,0x50,0x78};
constexpr uint64_t FocusMouseRva=0x867d89;
constexpr uint8_t FocusMouseSig[]={0x4d,0x85,0xf6,0x74,0x3f,0x49,0x8b,0x06,0x49,0x8b,0xce,0xff,0x90,0xb0,0x00,0x00,0x00,0x84,0xc0};
std::atomic<int> focusClear{-1};   // -1 unchecked, 0 not this build, 1 mirrored
void ClearStaleFocus(void* self) {
    if(focusClear.load()<0) {
        const bool ok=d2rsig::Check(FocusClearRva,FocusClearSig,sizeof FocusClearSig)&&d2rsig::Check(FocusMouseRva,FocusMouseSig,sizeof FocusMouseSig);
        focusClear=ok?1:0;
        if(!ok)framevr::logging::Write("Frame panels: FocusManager's focus clear not found; a worn item's tooltip may stay over the background");
    }
    if(focusClear.load()!=1||!self)return;
    auto* manager=static_cast<uint8_t*>(self);
    uintptr_t hovered=0,focused=0;std::memcpy(&hovered,manager+0x178,8);std::memcpy(&focused,manager+0x190,8);
    bool takes=false;
    if(hovered){auto table=*reinterpret_cast<void***>(hovered);takes=reinterpret_cast<bool(*)(uintptr_t)>(table[0xb0/8])(hovered);}
    if(!framevr::panels::ClearPointerFocus(framevr::panels::FocusUsesPointer(Owns(),pointerFocus.load()),focused!=0,takes))return;
    uintptr_t previous=0;std::memcpy(&previous,manager+0x188,8);
    const uintptr_t none=0;
    if(manager[0x180]) {
        std::memcpy(manager+0x188,&none,8);std::memcpy(manager+0x190,&none,8);
        auto table=*reinterpret_cast<void***>(focused);reinterpret_cast<void(*)(uintptr_t)>(table[0x78/8])(focused);
    }
    std::memcpy(manager+0x188,&previous,8);std::memcpy(manager+0x190,&none,8);
}
void OnFocus(void* self,bool sound) {
    {
        framevr::panels::PointerScope scope(pointerContext,framevr::panels::FocusUsesPointer(Owns(),pointerFocus.load()));
        originalFocus(self,sound);
    }
    ClearStaleFocus(self);
}

std::atomic<bool> textLayoutTold{false};
bool Preserve() {
    const auto now=GetTickCount64();
    if(!ready.load(std::memory_order_acquire))return false;
    const bool hold=framevr::panels::HoldController(lease.Active(now),typed.Active(now),gamestate::KeyboardText(),true);
    if(!hold){
        if(gamestate::KeyboardText()&&lease.Active(now)&&!textLayoutTold.exchange(true))
            framevr::logging::Write("Frame panels: multiplayer panel - the laser lets the game take its keyboard and mouse layout (its boxes take letters only there)");
        return false;
    }
    return isController();
}

// The game window's input method, on while a text box marked imeEnabled has the focus (TextFocus).
using AssociateFn=HIMC(WINAPI*)(HWND,HIMC);
using AssociateExFn=BOOL(WINAPI*)(HWND,HIMC,DWORD);
AssociateFn originalAssociate=nullptr;
AssociateExFn originalAssociateEx=nullptr;
std::atomic<int> textFocus{int(framevr::panels::TextFocus::Unknown)};
std::atomic<uint32_t> textFocusOns{0};
bool imeWatchIn=false;
void NoteIme(void* caller,HWND hwnd,bool ex,HIMC context,DWORD flags) {
    DWORD pid=0;
    if(!hwnd||!GetWindowThreadProcessId(hwnd,&pid)||pid!=GetCurrentProcessId())return;   // the game's own windows only
    HMODULE from=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCWSTR>(caller),&from);
    if(from!=GetModuleHandleW(nullptr)) {   // the loader's or ReShade's overlay, not the game's text boxes
        static std::atomic<int> toldOther{0};
        if(toldOther.fetch_add(1)<4) {
            wchar_t name[MAX_PATH]{};if(from)GetModuleFileNameW(from,name,MAX_PATH);
            const wchar_t* base=wcsrchr(name,L'\\');
            char line[260];snprintf(line,sizeof line,"Frame panels: input method call from %ls (not the game's) - not a text box's focus",base?base+1:(*name?name:L"unknown code"));
            framevr::logging::Write(line);
        }
        return;
    }
    const auto call=framevr::panels::ImeAssociation(ex,reinterpret_cast<uintptr_t>(context),flags);
    const auto focus=framevr::panels::NextTextFocus(framevr::panels::TextFocus(textFocus.load()),call);
    const int was=textFocus.exchange(int(focus));
    if(was==int(focus))return;
    if(focus==framevr::panels::TextFocus::On)textFocusOns.fetch_add(1);
    static std::atomic<int> told{0};
    if(told.fetch_add(1)<40) {
        char line[200];snprintf(line,sizeof line,"Frame panels: the game window's input method %s (%s flags 0x%lx) - %s",
            focus==framevr::panels::TextFocus::On?"on":"off",ex?"ImmAssociateContextEx":"ImmAssociateContext",(unsigned long)flags,
            focus==framevr::panels::TextFocus::On?"a text box has the focus":"no text box has the focus");
        framevr::logging::Write(line);
    }
}
HIMC WINAPI OnAssociate(HWND hwnd,HIMC context){NoteIme(_ReturnAddress(),hwnd,false,context,0);return originalAssociate(hwnd,context);}
BOOL WINAPI OnAssociateEx(HWND hwnd,HIMC context,DWORD flags){NoteIme(_ReturnAddress(),hwnd,true,context,flags);return originalAssociateEx(hwnd,context,flags);}
void InstallImeWatch() {
    if(imeWatchIn)return;
    imeWatchIn=true;
    HMODULE imm=LoadLibraryW(L"imm32.dll");
    void* a=imm?reinterpret_cast<void*>(GetProcAddress(imm,"ImmAssociateContext")):nullptr;
    void* b=imm?reinterpret_cast<void*>(GetProcAddress(imm,"ImmAssociateContextEx")):nullptr;
    const bool okA=a&&MH_CreateHook(a,reinterpret_cast<void*>(&OnAssociate),reinterpret_cast<void**>(&originalAssociate))==MH_OK&&MH_EnableHook(a)==MH_OK;
    const bool okB=b&&MH_CreateHook(b,reinterpret_cast<void*>(&OnAssociateEx),reinterpret_cast<void**>(&originalAssociateEx))==MH_OK&&MH_EnableHook(b)==MH_OK;
    framevr::logging::Write(okA&&okB?"Frame panels: watching the game window's input method (a text box's focus: the multiplayer keyboard comes up with it)":
                                     "Frame panels: the input method watch FAILED - the multiplayer keyboard stays up with its panel");
}
void Report() {
    if(!reported.exchange(true))framevr::logging::Write("Frame panels: layout switch suppressed; laser routed through UI-only pointer dispatch");
}
bool OnActivity(void* event) {
    if(Preserve()){Report();return false;}
    return originalActivity(event);
}
bool OnRequest(int method) {
    if(method==0&&Preserve()){Report();return false;}
    return originalRequest(method);
}
}
void OwnPointer(){pointerFocus=true;lease.Refresh(GetTickCount64());}
void Typed(uintptr_t extra){
    static std::atomic<bool> told{false};
    if(typed.Key(GetTickCount64(),extra)&&lease.Active(GetTickCount64())&&!told.exchange(true))
        framevr::logging::Write("Frame panels: a key typed - the game may take its keyboard layout for text boxes");
}
void UseController(){pointerFocus=false;ItemHover(false);}
void ItemHover(bool active,bool hit,bool dragging){
    if(!active||!hit||dragging)++hoverGeneration;
    tooltipTiming=int(vrsettings::Read().values[framevr::settings::TooltipTiming]);
    itemHover.Update(GetTickCount64(),active,hit,dragging);
}
void WorldInput(bool active){worldInput.Update(GetTickCount64(),active);}
int TextFocusState(){return textFocus.load();}
uint32_t TextFocusOns(){return textFocusOns.load();}
void MaintainWorldController(bool player,bool menu){
    if(!ready.load(std::memory_order_acquire))return;
    if(worldInput.NeedsController(GetTickCount64(),player,menu,isController())) {
        // Use the same verified native transition as the tabbed menu. A fake
        // A press would attack when no target exists and cannot solve the
        // circular dependency between acquiring a target and accepting A.
        static uint64_t reportAt=0;
        const auto now=GetTickCount64();
        if(now>=reportAt){framevr::logging::Write("Frame input: restoring native controller layout for VR world interactions");reportAt=now+2000;}
        originalRequest(1);
    }
}
bool BlocksWorld(){return Owns();}
bool OverItem(){return GetTickCount64()-itemTooltipAt.load()<150;}
void HoverStats(char* out,size_t size){
    if(!out||!size)return;
    std::snprintf(out,size,"moves=%u itemQ=%u itemQnative=%u tips=%u tipsItem=%u tipsShown=%u last=%llx/%llx hover=%d",
        statMoves.exchange(0),statItemQueries.exchange(0),statItemNative.exchange(0),statTips.exchange(0),
        statTipItems.exchange(0),statTipShown.exchange(0),(unsigned long long)statLastWidget.load(),
        (unsigned long long)statLastItem.load(),int(ItemHoverActive()));
}
bool ControllerAction(void (*action)(void*),void* data){
    if(!ready.load(std::memory_order_acquire)||!action)return false;
    const bool previous=bindingContext;bindingContext=true;
    action(data);bindingContext=previous;return true;
}
bool ControllerReady(){
    if(!ready.load(std::memory_order_acquire))return false;
    if(isController())return true;
    originalRequest(1); // native transition; its UI update commits/reloads layouts
    return false;
}
void Install() {
    InstallImeWatch();
    InstallCanvasSize();
    if(ready.load()){InstallTooltips();return;}
    if((!controllerIn&&!d2rsig::Check(ControllerRva,ControllerSig,sizeof ControllerSig))||
       !d2rsig::Check(ActivityRequestRva,ActivityRequestSig,sizeof ActivityRequestSig))return;
    // Validate every entry point before changing any of them. Partial installs
    // pass through until all callbacks and the layout guard are ready.
    if((!activityIn&&!d2rsig::Check(ActivityRva,ActivitySig,sizeof ActivitySig))||
       (!requestIn&&!d2rsig::Check(RequestRva,RequestSig,sizeof RequestSig))||
       (!dispatchIn&&!d2rsig::Check(DispatchRva,DispatchSig,sizeof DispatchSig))||
       (!focusIn&&!d2rsig::Check(FocusRva,FocusSig,sizeof FocusSig))||
       (!cursorSnapIn&&!d2rsig::Check(CursorSnapRva,CursorSnapSig,sizeof CursorSnapSig))||
       !d2rsig::Check(FocusSnapRva,FocusSnapSig,sizeof FocusSnapSig)||
       !d2rsig::Check(MessageLayoutRva,MessageLayoutSig,sizeof MessageLayoutSig))return;
    for(int i=0;i<6;++i)if(!pointerIn[i]&&!d2rsig::Check(PointerRvas[i],i?ButtonSig:MoveSig,i?sizeof ButtonSig:sizeof MoveSig))return;
    if(!controllerIn)controllerIn=d2rsig::Hook(ControllerRva,ControllerSig,sizeof ControllerSig,reinterpret_cast<void*>(&ControllerMode),reinterpret_cast<void**>(&isController));
    if(!activityIn)activityIn=d2rsig::Hook(ActivityRva,ActivitySig,sizeof ActivitySig,reinterpret_cast<void*>(&OnActivity),reinterpret_cast<void**>(&originalActivity));
    if(!requestIn)requestIn=d2rsig::Hook(RequestRva,RequestSig,sizeof RequestSig,reinterpret_cast<void*>(&OnRequest),reinterpret_cast<void**>(&originalRequest));
    if(!dispatchIn)dispatchIn=d2rsig::Hook(DispatchRva,DispatchSig,sizeof DispatchSig,reinterpret_cast<void*>(&OnDispatch),reinterpret_cast<void**>(&originalDispatch));
    if(!focusIn)focusIn=d2rsig::Hook(FocusRva,FocusSig,sizeof FocusSig,reinterpret_cast<void*>(&OnFocus),reinterpret_cast<void**>(&originalFocus));
    if(!cursorSnapIn)cursorSnapIn=d2rsig::Hook(CursorSnapRva,CursorSnapSig,sizeof CursorSnapSig,reinterpret_cast<void*>(&CursorSnap),reinterpret_cast<void**>(&originalCursorSnap));
    bool complete=activityIn&&requestIn&&controllerIn&&dispatchIn&&focusIn&&cursorSnapIn;
    for(int i=0;i<6;++i) {
        if(!pointerIn[i])pointerIn[i]=d2rsig::Hook(PointerRvas[i],i?ButtonSig:MoveSig,i?sizeof ButtonSig:sizeof MoveSig,
            reinterpret_cast<void*>(pointerHooks[i]),reinterpret_cast<void**>(&originalPointer[i]));
        complete&=pointerIn[i];
    }
    if(complete){ready.store(true,std::memory_order_release);framevr::logging::Write("Frame panels: controller layout retained; exact laser focus without controller cursor snapping and event consumption installed");InstallTooltips();}
}
}
