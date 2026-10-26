#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <vector>
#include "notice_card.h"
namespace framevr::notice {
// The card in the native tooltip's colours: a dark box, a bronze rim, the
// message wrapped and centred. Segoe UI has U+2261 and links CJK fallbacks.
// False when even the smallest size overflows (the text is still drawn).
inline bool Paint(const char* utf8,std::vector<uint8_t>& rgba) {
    rgba.assign(size_t(Width)*Height*4,0);
    wchar_t text[256]{};
    if(!utf8||!MultiByteToWideChar(CP_UTF8,0,utf8,-1,text,256))text[0]=0;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-Height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);void* bits=nullptr;
    HBITMAP bitmap=dc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
    if(!dc||!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);rgba.clear();return false;}
    const auto old=SelectObject(dc,bitmap);
    auto fill=[&](int l,int t,int r,int b,COLORREF color) {
        RECT rect{l,t,r,b};auto brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);
    };
    fill(0,0,Width,Height,RGB(116,88,44));fill(2,2,Width-2,Height-2,RGB(16,13,10));
    fill(6,6,Width-6,7,RGB(64,49,29));fill(6,Height-7,Width-6,Height-6,RGB(64,49,29));
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(238,232,214));
    constexpr int margin=22,room=Height-24;
    bool fits=false;
    for(int size:{30,27,24,21}) {
        HFONT font=CreateFontW(size,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const auto previous=SelectObject(dc,font);
        RECT measured{0,0,Width-2*margin,0};
        DrawTextW(dc,text,-1,&measured,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX|DT_CALCRECT);
        fits=measured.right<=Width-2*margin&&measured.bottom<=room;
        if(fits||size==21) {
            const int top=(Height-std::min<int>(measured.bottom,room))/2;
            RECT bounds{margin,top,Width-margin,top+room};
            DrawTextW(dc,text,-1,&bounds,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
        }
        SelectObject(dc,previous);DeleteObject(font);
        if(fits||size==21)break;
    }
    GdiFlush();
    const auto* bgra=static_cast<const uint8_t*>(bits);
    for(size_t i=0;i<rgba.size();i+=4){rgba[i]=bgra[i+2];rgba[i+1]=bgra[i+1];rgba[i+2]=bgra[i];rgba[i+3]=232;}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    return fits;
}
}
