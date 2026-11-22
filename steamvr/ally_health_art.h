#pragma once
#include <windows.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include "ally_health.h"
namespace framevr::allyhealth {
// One atlas: the wrist samples the whole list; overhead quads sample one row.
inline bool Paint(const List& list,std::vector<uint8_t>& rgba) {
    rgba.assign(size_t(Width)*Height*4,0);
    if(list.count>Capacity)return false;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-Height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);void* bits=nullptr;
    HBITMAP image=dc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
    if(!dc||!image||!bits){if(image)DeleteObject(image);if(dc)DeleteDC(dc);return false;}
    std::memset(bits,0,rgba.size());const auto old=SelectObject(dc,image);
    HFONT font=CreateFontW(27,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if(!font){SelectObject(dc,old);DeleteObject(image);DeleteDC(dc);return false;}
    const auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(240,233,215));
    auto fill=[&](int l,int t,int rr,int b,COLORREF color){RECT box{l,t,rr,b};HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&box,brush);DeleteObject(brush);};
    for(unsigned i=0;i<list.count;++i) {
        const auto& e=list.entries[i];if(!e.Valid())continue;
        const int y=int(i)*RowHeight;
        fill(0,y,Width,y+RowHeight-4,RGB(104,82,45));fill(2,y+2,Width-2,y+RowHeight-6,RGB(19,18,16));
        wchar_t text[64]{};
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,e.name.data(),-1,text,64))
            std::wcscpy(text,L"Name unavailable");
        RECT name{12,y+4,Width-112,y+38};DrawTextW(dc,text,-1,&name,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX|DT_VCENTER);
        wchar_t percent[16]{};std::swprintf(percent,16,L"%.0f%%",e.Fraction()*100);
        RECT value{Width-110,y+4,Width-12,y+38};DrawTextW(dc,percent,-1,&value,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        fill(12,y+44,Width-12,y+63,RGB(50,25,24));
        const int end=12+int((Width-24)*e.Fraction());
        if(end>12)fill(12,y+44,end,y+63,e.Fraction()<=.25f?RGB(210,55,42):RGB(65,175,82));
    }
    GdiFlush();const auto* bgra=static_cast<const uint8_t*>(bits);
    for(unsigned row=0;row<list.count;++row)if(list.entries[row].Valid())
        for(int y=int(row)*RowHeight;y<int(row+1)*RowHeight-4;++y)for(int x=0;x<Width;++x){
            const size_t i=(size_t(y)*Width+x)*4;
            rgba[i]=bgra[i+2];rgba[i+1]=bgra[i+1];rgba[i+2]=bgra[i];rgba[i+3]=240;
        }
    SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteObject(image);DeleteDC(dc);return true;
}
}
