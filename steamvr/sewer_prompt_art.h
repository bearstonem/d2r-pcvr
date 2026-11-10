#pragma once
#include <windows.h>
#include "sewer_prompt.h"
namespace framevr::sewerprompt {
// Draw once into two unused legacy skill cells, shared with the preview.
inline bool Paint(HDC dc,int x=0,int y=0,const wchar_t* button=Shortcut) {
    const int saved=SaveDC(dc);
    auto fill=[&](int l,int t,int r,int b,COLORREF color) {
        RECT rect{x+l,y+t,x+r,y+b};auto brush=CreateSolidBrush(color);
        FillRect(dc,&rect,brush);DeleteObject(brush);
    };
    fill(0,0,Width,Height,RGB(116,88,44));fill(2,2,Width-2,Height-2,RGB(20,17,13));
    fill(7,7,Width-7,8,RGB(64,49,29));fill(7,Height-8,Width-7,Height-7,RGB(64,49,29));
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(237,213,164));
    HFONT title=CreateFontW(36,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    HFONT shortcut=CreateFontW(27,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    bool fits=true;
    auto text=[&](HFONT font,const wchar_t* value,int top,int bottom) {
        SelectObject(dc,font);RECT measured{0,0,0,0};
        DrawTextW(dc,value,-1,&measured,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
        fits=fits&&measured.right<=Width-32&&measured.bottom<=bottom-top;
        RECT bounds{x+16,y+top,x+Width-16,y+bottom};
        DrawTextW(dc,value,-1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    };
    text(title,L"ENTER SEWERS",16,65);
    SetTextColor(dc,RGB(215,205,183));text(shortcut,button,70,112);
    RestoreDC(dc,saved);DeleteObject(title);DeleteObject(shortcut);return fits;
}
}
