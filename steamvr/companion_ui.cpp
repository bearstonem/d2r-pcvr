#include <windows.h>
#include "interaction_ui.h"
#include "interaction_icons.h"
#include <algorithm>
#include <cstdio>

namespace framevr::interaction {
std::vector<uint8_t> PaintCompanions(const companions::Roster& roster) {
    using namespace companions;
    const int count=Count(roster);
    std::vector<uint8_t> rgba(Width*Height*4);
    if(!count)return rgba;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-Height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return {};}
    auto old=SelectObject(dc,bitmap);std::memset(bits,0,rgba.size());
    auto fill=[&](RECT r,COLORREF colour){auto brush=CreateSolidBrush(colour);FillRect(dc,&r,brush);DeleteObject(brush);};
    fill({0,0,Width,UsedHeight(roster)},RGB(17,16,14));
    fill({0,0,Width,2},RGB(122,94,51));fill({0,UsedHeight(roster)-2,Width,UsedHeight(roster)},RGB(122,94,51));
    HFONT number=CreateFontW(46,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    HFONT label=CreateFontW(count>=Columns?18:22,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    const auto oldFont=SelectObject(dc,number);SetBkMode(dc,TRANSPARENT);
    int index=0;
    for(const auto& d:Definitions) {
        if(!(roster.relevant&Bit(d.type)))continue;
        const int row=index/Columns,column=index%Columns,cols=std::min(Columns,count-row*Columns);
        const int cell=Width/cols,x=column*cell,y=row*RowHeight;
        const int groupWidth=std::min(cell-12,128),left=x+(cell-groupWidth)/2,iconSize=std::min(64,groupWidth/2);
        const bool alive=roster.alive[d.type]>0;
        if(column)fill({x,y+12,x+1,y+RowHeight-12},RGB(64,50,31));
        auto image=d.skill>=0?icons::Get("s"+std::to_string(d.skill)):nullptr;
        if(image&&image->width&&image->height) {
            GdiFlush();auto* dst=static_cast<uint8_t*>(bits);
            const float scale=std::min(float(iconSize)/image->width,float(iconSize)/image->height);
            const int w=std::max(1,int(image->width*scale)),h=std::max(1,int(image->height*scale));
            const int ix=left+(iconSize-w)/2,iy=y+18+(64-h)/2;
            for(int j=0;j<h;++j)for(int i=0;i<w;++i) {
                const auto* p=&image->rgba[(std::min(image->height-1,unsigned(j/scale))*image->width+std::min(image->width-1,unsigned(i/scale)))*4];
                auto* out=&dst[((iy+j)*Width+ix+i)*4];const float alpha=p[3]/255.f;
                for(int c=0;c<3;++c)out[c]=uint8_t(p[2-c]*(alive?1.f:.45f)*alpha+out[c]*(1-alpha));
            }
        } else {
            // Text fallback also gives mercenaries a readable identity without
            // shipping game art or requiring a new extracted-icon cache.
            SelectObject(dc,label);SetTextColor(dc,RGB(190,163,110));
            const char* tag=d.type==7?"MERC":d.type==4?"SKEL":d.type==5?"MAGE":d.type==18?"TRAP":"ALLY";
            RECT box{left,y+20,left+iconSize,y+84};DrawTextA(dc,tag,-1,&box,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        SelectObject(dc,number);SetTextColor(dc,alive?RGB(245,232,198):RGB(228,124,92));
        char text[16];std::snprintf(text,sizeof text,"%u",unsigned(roster.alive[d.type]));
        RECT value{left+iconSize+3,y+13,x+cell-6,y+88};
        DrawTextA(dc,text,-1,&value,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(dc,label);SetTextColor(dc,alive?RGB(222,206,174):RGB(175,142,124));
        RECT name{x+4,y+98,x+cell-4,y+132};DrawTextA(dc,d.name,-1,&name,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        ++index;
    }
    if(roster.commanderBadge&&(roster.relevant&ArmyTypeMask)!=0) {
        HFONT badgeFont=CreateFontW(15,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
        const auto prev=SelectObject(dc,badgeFont);
        char badgeText[48]{};
        if(roster.freeRoam) {
            std::snprintf(badgeText,sizeof badgeText,"FREE ROAM");
        } else {
            const char* stanceTag=roster.anchorActive?"HOLD":roster.stance==0?"DEF":roster.stance==2?"AGG":"BAL";
            const char* formTag=roster.formation==1?"RING 360":roster.formation==2?"WEDGE":roster.formation==3?"COLUMN":"PHALANX";
            std::snprintf(badgeText,sizeof badgeText,"%s \xB7 %s",stanceTag,formTag);
        }
        RECT pill{Width-184,2,Width-6,20};
        fill(pill,roster.freeRoam?RGB(36,34,30):roster.stance==0?RGB(24,48,62):roster.stance==2?RGB(64,28,24):RGB(46,38,24));
        SetTextColor(dc,roster.freeRoam?RGB(205,195,175):roster.stance==0?RGB(165,228,250):roster.stance==2?RGB(250,185,155):RGB(235,212,155));
        DrawTextA(dc,badgeText,-1,&pill,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(dc,prev);DeleteObject(badgeFont);
    }
    GdiFlush();const auto* src=static_cast<const uint8_t*>(bits);
    for(int i=0;i<Width*UsedHeight(roster);++i) {
        rgba[i*4]=src[i*4+2];rgba[i*4+1]=src[i*4+1];rgba[i*4+2]=src[i*4];rgba[i*4+3]=255;
    }
    SelectObject(dc,oldFont);DeleteObject(number);DeleteObject(label);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return rgba;
}
std::vector<uint8_t> PaintWristStatus(const companions::Roster& roster,const effects::Status& status) {
    constexpr int Width=wriststatus::Width,RowHeight=companions::RowHeight;
    std::vector<uint8_t> rgba(Width*wriststatus::Height*4);
    const int companionHeight=companions::UsedHeight(roster),height=effects::Rows(status)*RowHeight;
    if(companionHeight) {
        const auto companions=PaintCompanions(roster);if(companions.empty())return {};
        std::copy_n(companions.data(),Width*companionHeight*4,rgba.data());
    }
    if(!height)return rgba;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return {};}
    const auto old=SelectObject(dc,bitmap);std::memset(bits,0,Width*height*4);
    auto fill=[&](RECT r,COLORREF c){const auto b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);};
    fill({0,0,Width,height},RGB(17,16,14));
    fill({0,0,Width,2},RGB(122,94,51));fill({0,height-2,Width,height},RGB(122,94,51));
    HFONT label=CreateFontW(21,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    HFONT symbol=CreateFontW(44,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    const auto oldFont=SelectObject(dc,label);SetBkMode(dc,TRANSPARENT);
    const auto ordered=effects::Ordered(status);const int count=effects::Count(status),cells=effects::Cells(status);
    for(int i=0;i<cells;++i) {
        const auto& d=*ordered[i];
        const int row=i/companions::Columns,col=i%companions::Columns;
        const int cols=std::min(companions::Columns,cells-row*companions::Columns),cell=Width/cols,x=col*cell,y=row*RowHeight;
        const bool more=count>cells&&i==cells-1;
        const COLORREF colour=more||d.kind==effects::Kind::Other?RGB(211,189,138):
            d.kind==effects::Kind::Harm?RGB(242,137,116):RGB(171,218,176);
        if(col)fill({x,y+10,x+1,y+RowHeight-10},RGB(64,50,31));
        fill({x+cell/2-30,y+81,x+cell/2+30,y+84},colour);
        const auto image=!more&&d.skill>=0?icons::Get("s"+std::to_string(d.skill)):nullptr;
        if(image&&image->width&&image->height) {
            GdiFlush();auto* dst=static_cast<uint8_t*>(bits);
            const float scale=std::min(64.f/image->width,64.f/image->height);
            const int w=std::max(1,int(image->width*scale)),h=std::max(1,int(image->height*scale));
            const int ix=x+(cell-w)/2,iy=y+10+(64-h)/2;
            for(int j=0;j<h;++j)for(int k=0;k<w;++k) {
                const auto* p=&image->rgba[(std::min(image->height-1,unsigned(j/scale))*image->width+std::min(image->width-1,unsigned(k/scale)))*4];
                auto* out=&dst[((iy+j)*Width+ix+k)*4];const float alpha=p[3]/255.f;
                for(int c=0;c<3;++c)out[c]=uint8_t(p[2-c]*alpha+out[c]*(1-alpha));
            }
        } else {
            // Generic status symbols accompany readable names when there is no
            // skill icon (poison, potions, shrines) or the local cache is absent.
            SelectObject(dc,symbol);SetTextColor(dc,colour);char text[20];
            if(more)std::snprintf(text,sizeof text,"+%d",count-cells+1);
            else std::snprintf(text,sizeof text,"%s",d.kind==effects::Kind::Harm?"!":d.kind==effects::Kind::Benefit?"+":"*");
            RECT r{x+4,y+8,x+cell-4,y+76};DrawTextA(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        SelectObject(dc,label);SetTextColor(dc,colour);
        RECT r{x+5,y+88,x+cell-5,y+138};
        DrawTextA(dc,more?"More effects":d.name,-1,&r,DT_CENTER|DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    GdiFlush();const auto* src=static_cast<const uint8_t*>(bits);
    for(int i=0;i<Width*height;++i) {
        auto* out=rgba.data()+(Width*companionHeight+i)*4;
        out[0]=src[i*4+2];out[1]=src[i*4+1];out[2]=src[i*4];out[3]=255;
    }
    SelectObject(dc,oldFont);DeleteObject(label);DeleteObject(symbol);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return rgba;
}
}
