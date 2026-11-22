#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "wrist_chat_ui.h"
#include <algorithm>
namespace framevr::wristchat {
std::vector<uint8_t> Paint(bool detached,bool typing,bool supported,bool captured,const std::string& draft){
 constexpr int W=768,H=512;BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=W;bi.bmiHeader.biHeight=-H;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
 HDC dc=CreateCompatibleDC(nullptr);if(!dc)return {};void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);if(!bitmap){DeleteDC(dc);return {};}
 auto old=SelectObject(dc,bitmap);const auto fill=[&](RECT rect,COLORREF c){auto b=CreateSolidBrush(c);FillRect(dc,&rect,b);DeleteObject(b);};
 fill({0,0,W,H},RGB(18,18,21));fill({0,0,W,56},RGB(43,34,26));fill({14,414,754,476},RGB(38,38,43));
 HFONT font=CreateFontW(-22,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(236,227,208));
 const auto text=[&](std::wstring s,RECT rect){DrawTextW(dc,s.c_str(),int(s.size()),&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);};
 text(L"CHAT",{18,0,750,56});
 if(!captured)text(supported?L"Waiting for native chat history...":L"Native chat unavailable in this game build",{26,155,738,255});
 std::wstring line;
 if(detached)line=L"Open Chat on your wrist to type";
 else if(!supported)line=L"Typing unavailable";
 else if(!typing)line=L"Click here to type with the PCVR keyboard";
 else if(draft.empty())line=L"Type a message - Enter sends to game chat";
 else {int n=MultiByteToWideChar(CP_UTF8,0,draft.data(),int(draft.size()),nullptr,0);line.resize(n);if(n)MultiByteToWideChar(CP_UTF8,0,draft.data(),int(draft.size()),line.data(),n);}
 text(line,{26,414,738,476});text(detached?L"With the menu open, grip to move or dock":L"Point + grip, move, release to pin on HUD",{20,478,750,510});
 GdiFlush();std::vector<uint8_t> out(W*H*4);auto* p=static_cast<uint8_t*>(bits);for(size_t i=0;i<out.size();i+=4){out[i]=p[i+2];out[i+1]=p[i+1];out[i+2]=p[i];out[i+3]=uint8_t(80+(175*std::max(p[i],std::max(p[i+1],p[i+2])))/255); for(int ch=0;ch<3;++ch)out[i+ch]=uint8_t(unsigned(out[i+ch])*out[i+3]/255);}
 SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return out;
}
}
