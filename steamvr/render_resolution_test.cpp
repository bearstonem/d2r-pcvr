#include "render_window.h"
#include "vr_settings.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace framevr::resolution;
static RenderWindow render;
static std::vector<Size> sizes;
static LRESULT CALLBACK Native(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_SIZE){sizes.push_back({LOWORD(l),HIWORD(l)});return 0;}
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK Hook(HWND h,UINT m,WPARAM w,LPARAM l){
    LRESULT result=0;if(render.Handle(h,m,w,l,Native,result))return result;
    return Native(h,m,w,l);
}
static void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE))DispatchMessageW(&m);}
static Size Physical(HWND h){RECT r{};assert(GetClientRect(h,&r));return {uint32_t(r.right),uint32_t(r.bottom)};}
int main(){
    assert((UiCanvas({3072,3072})==Size{3072,1728}));
    assert((UiCanvas({4096,4096})==Size{4096,2304}));
    assert((UiCanvas({2160,2400})==Size{2160,1215}));
    assert((UiCanvas({3440,1440})==Size{3440,1440}));
    assert(!UiCanvas({}));
    assert(!Select(0,{3000,3000},{8192,8192}));
    assert(!Select(-1,{3000,3000},{8192,8192}));
    assert(!Select(7,{3000,3000},{8192,8192}));
    assert(!Select(1,{}, {8192,8192}));
    assert(!Select(4,{3000,3000},{}));
    assert((Select(4,{3000,3000},{8192,8192})==Size{2560,2560}));
    assert((Select(1,{6000,4800},{8192,8192})==Size{4096,3276}));
    assert((Select(1,{3000,2400},{2000,1600})==Size{2000,1600}));
    assert(!Select(4,{3000,3000},{500,500}));
    assert((Unpack(Pack({4096,2160}))==Size{4096,2160}));
    // Native UI pixels must match the stretched desktop image, including edges
    // and the inverse conversion used by gamepad cursor warps.
    assert(Coordinate(0,1920,4096)==0&&Coordinate(1919,1920,4096)==4095);
    assert(Coordinate(1079,1080,2560)==2559);
    for(int p=0;p<1920;++p)assert(Coordinate(Coordinate(p,1920,4096),4096,1920)==p);
    assert(Coordinate(-10,100,200)<0&&Coordinate(100,100,200)>199);
    assert(Coordinate(100000,100,4096)==32767);
    assert(Coordinate(25,1,4096)==25);
    using namespace framevr::settings;
    Snapshot s;s.ready=true;assert(s.values[RenderResolution]==0);
    assert(Normalize(RenderResolution,9)==0&&Normalize(RenderResolution,1.5f)==0);
    Model model;model.Open(s);model.Click({Kind::Tab,int(Page::Video)});
    const int row=RowCount(Page::Video)-1;assert(RowKey(Page::Video,row)==RenderResolution);
    const auto box=Increase(row);assert(box.y+box.h<716);
    auto hit=HitTest(Page::Video,(box.x+box.w*.5f)/Width,(box.y+box.h*.5f)/Height);
    assert(hit.index==RenderResolution);model.Click(hit);
    const auto save=model.Click({Kind::Apply});
    assert(save.changed==(1u<<RenderResolution)&&save.values[RenderResolution]==1);
    WNDCLASSW cls{};cls.lpfnWndProc=Hook;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"FrameResolutionFixture";
    assert(RegisterClassW(&cls));
    auto h=CreateWindowW(cls.lpszClassName,L"Resolution fixture",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,cls.hInstance,nullptr);
    assert(h);const auto physical=Physical(h);sizes.clear();
    assert(render.Request(h,{2560,2560}));assert(sizes.empty()&&!render.Logical(h));
    Pump();assert(sizes.size()==1&&(sizes.back()==Size{2560,2560})&&Physical(h)==physical);
    assert(render.Request(h,{2560,2560}));Pump();assert(sizes.size()==1);
    assert(render.Request(h,{3072,3072}));assert(render.Request(h,{4096,4096}));
    Pump();assert(sizes.size()==2&&(sizes.back()==Size{4096,4096}));
    // Native Apply can discard its render configuration without changing the
    // remembered window size. Re-send that size once without losing pointer
    // coordinates or resizing the physical monitor/window.
    const auto beforeApply=sizes.size();
    assert(render.Request(h,{4096,4096},true));
    assert(render.Request(h,{4096,4096},true));
    assert((render.Logical(h)==Size{4096,4096})&&sizes.size()==beforeApply);
    Pump();assert(sizes.size()==beforeApply+1&&(sizes.back()==Size{4096,4096})&&Physical(h)==physical);
    render.Request(h,{4096,4096});Pump();assert(sizes.size()==beforeApply+1);
    render.Request(h,{4096,4096},true);render.Request(h,{3072,3072});
    Pump();assert(sizes.size()==beforeApply+2&&(sizes.back()==Size{3072,3072}));
    render.Request(h,{4096,4096});Pump();
    assert(SetWindowPos(h,nullptr,0,0,900,700,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    assert((sizes.back()==Size{4096,4096})&&Physical(h)!=physical);
    const auto resized=Physical(h);
    render.Restore();Pump();assert(sizes.back()==resized&&!render.Logical(h));
    // A superseded queued request must not re-enable the override.
    auto count=sizes.size();render.Request(h,{4096,4096});render.Restore();Pump();assert(sizes.size()==count);
    render.Request(h,{4096,4096},true);render.Restore();Pump();assert(sizes.size()==count);
    render.Request(h,{2560,2560});Pump();
    SendMessageW(h,WM_SIZE,SIZE_MINIMIZED,0);assert((render.Logical(h)==Size{2560,2560}));
    SendMessageW(h,WM_SIZE,SIZE_RESTORED,MAKELPARAM(resized.w,resized.h));assert((sizes.back()==Size{2560,2560}));
    assert(!render.Logical(GetDesktopWindow()));
    SendMessageW(h,WM_CLOSE,0,0);assert(!IsWindow(h)&&!render.Logical(h)&&sizes.back()==resized);
    assert(!render.Request(h,{2048,2048}));
    UnregisterClassW(cls.lpszClassName,cls.hInstance);
    std::puts("Resolution limits, UI selection, pointer mapping, asynchronous native resize, coalescing and desktop restoration passed");
}
