#include "render_resolution.h"
#include "render_window.h"
#include <intrin.h>
#include <MinHook.h>
#include <reshade.hpp>
#include <cstdio>
#include <mutex>
namespace framevr::resolution {
namespace {
RenderWindow window;
void (*logger)(const char*)=nullptr;
using Convert=BOOL(WINAPI*)(HWND,LPPOINT);
Convert screenToClient=nullptr,clientToScreen=nullptr;
uintptr_t gameBegin=0,gameEnd=0;
bool hooksReady=false;
std::once_flag setup;
std::atomic<uint64_t> requested{0};
std::atomic<HWND> attached{nullptr};
std::atomic<int> selection{0};
std::atomic<uint64_t> reapplyAt{0};
bool GameCaller(void* address){const auto p=reinterpret_cast<uintptr_t>(address);return p>=gameBegin&&p<gameEnd;}
void Scale(HWND h,LPPOINT p,bool toRender) {
    const auto logical=window.Logical(h);RECT client{};
    if(!p||!logical||!GetClientRect(h,&client)||client.right<2||client.bottom<2)return;
    p->x=toRender?Coordinate(p->x,client.right,logical.w):Coordinate(p->x,logical.w,client.right);
    p->y=toRender?Coordinate(p->y,client.bottom,logical.h):Coordinate(p->y,logical.h,client.bottom);
}
BOOL WINAPI ScreenToClientHook(HWND h,LPPOINT p) {
    const bool game=GameCaller(_ReturnAddress());
    const auto ok=screenToClient(h,p);if(ok&&game)Scale(h,p,true);return ok;
}
BOOL WINAPI ClientToScreenHook(HWND h,LPPOINT p) {
    if(GameCaller(_ReturnAddress()))Scale(h,p,false);
    return clientToScreen(h,p);
}
void Setup() {
    gameBegin=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(gameBegin);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(gameBegin+dos->e_lfanew);
    gameEnd=gameBegin+nt->OptionalHeader.SizeOfImage;
    auto user=GetModuleHandleW(L"user32.dll");
    auto a=reinterpret_cast<void*>(GetProcAddress(user,"ScreenToClient"));
    auto b=reinterpret_cast<void*>(GetProcAddress(user,"ClientToScreen"));
    if(!a||!b)return;
    if(MH_CreateHook(a,reinterpret_cast<void*>(&ScreenToClientHook),reinterpret_cast<void**>(&screenToClient))!=MH_OK)return;
    if(MH_CreateHook(b,reinterpret_cast<void*>(&ClientToScreenHook),reinterpret_cast<void**>(&clientToScreen))!=MH_OK){MH_RemoveHook(a);return;}
    hooksReady=MH_EnableHook(a)==MH_OK&&MH_EnableHook(b)==MH_OK;
    if(!hooksReady){MH_DisableHook(a);MH_DisableHook(b);MH_RemoveHook(a);MH_RemoveHook(b);}
    if(logger)logger(hooksReady?"Frame resolution: native pointer coordinate conversion ready":"Frame resolution: pointer hooks unavailable; retaining desktop resolution");
}
void Resource(reshade::api::device*,const reshade::api::resource_desc& desc,const reshade::api::subresource_data*,reshade::api::resource_usage,reshade::api::resource) {
    using namespace reshade::api;
    if(!requested.load()||desc.type!=resource_type::texture_2d||(desc.usage&resource_usage::depth_stencil)==resource_usage::undefined)return;
    const auto size=Unpack(requested.load());
    if(desc.texture.width!=size.w||desc.texture.height!=size.h)return;
    // Diagnostic evidence of native depth allocation alongside the requested
    // source. This is not a claim that every scene/postprocess uses this size.
    char line[160];std::snprintf(line,sizeof line,"Frame resolution: depth target allocated %ux%u format=%u",size.w,size.h,unsigned(desc.texture.format));
    if(logger)logger(line);
}
}
void Register(void (*log)(const char*)) {
    logger=log;
    reshade::register_event<reshade::addon_event::init_resource>(&Resource);
}
void Attach(HWND h){attached=h;}
uint64_t UiSize(uint64_t nativeSize) {
    const auto logical=UiCanvas(window.Logical(attached.load()));
    return logical?uint64_t(logical.w)|(uint64_t(logical.h)<<32):nativeSize;
}
uint64_t CursorSize(uint64_t nativeSize) {
    const auto logical=window.Logical(attached.load());
    return logical?uint64_t(logical.w)|(uint64_t(logical.h)<<32):nativeSize;
}
void UiSizeReady() {
    if(const auto size=Unpack(requested.load());size)window.Request(attached.load(),size,true);
}
bool Handle(HWND h,UINT message,WPARAM w,LPARAM& l,WNDPROC original,LRESULT& result){return window.Handle(h,message,w,l,original,result);}
void Update(HWND h,bool running,Size recommended,Size maximum,Size source,int choice) {
    selection=choice;
    if(!h||h!=attached.load())return;
    const auto wanted=running?Select(choice,recommended,maximum):Size{};
    if(wanted)std::call_once(setup,Setup);
    const auto next=hooksReady?wanted:Size{};
    const auto prior=requested.exchange(Pack(next));
    const auto now=GetTickCount64();
    auto deadline=reapplyAt.load();
    const bool reapply=next&&deadline&&now>=deadline&&reapplyAt.compare_exchange_strong(deadline,0);
    if(!next)reapplyAt=0;
    // The game's options handler can reset render state without a WM_SIZE.
    // Keep pointer mapping active, but bypass the cached-size equality once.
    // RenderWindow posts to the UI thread; never resize under renderer locks.
    window.Request(h,next,reapply);
    if(reapply&&logger)logger("Frame resolution: reapplying VR source after native graphics options Apply");
    if(prior!=Pack(next)&&logger){
        RECT client{};GetClientRect(h,&client);
        char line[224];std::snprintf(line,sizeof line,"Frame resolution: requested=%ux%u source=%ux%u desktop=%ldx%ld headset=%ux%u choice=%d",
            next.w,next.h,source.w,source.h,client.right,client.bottom,recommended.w,recommended.h,choice);logger(line);
    }
}
void OptionsApplied(){if(requested.load())reapplyAt=GetTickCount64()+500;}
Size WindowClient() {
    RECT client{};const auto h=attached.load();
    if(!h||!GetClientRect(h,&client)||client.right<1||client.bottom<1)return {};
    return {uint32_t(client.right),uint32_t(client.bottom)};
}
void Restore(){reapplyAt=0;requested=0;window.Restore();}
void Describe(char* text,size_t capacity,Size source) {
    const auto wanted=Unpack(requested.load());
    std::snprintf(text,capacity,"VR source: %u x %u per eye%s",source.w,source.h,
        wanted&&wanted!=source?" (resize pending)":selection.load()&&!wanted?" (desktop fallback)":"");
}
}
