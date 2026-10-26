#include <windows.h>
#include "dev_scenarios.h"
#include <wincodec.h>
#include "interaction_ui.h"
#include "interaction_icons.h"
#include "skill_slots.h"
#include "skill_usage.h"
#include "wrist_compass.h"
#include <cstdio>
#include <string>
namespace framevr::compass {
std::vector<uint8_t> Paint() {
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-Height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return {};}
    auto old=SelectObject(dc,bitmap);std::memset(bits,0,Width*Height*4);
    auto fill=[&](RECT r,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);};
    fill({0,0,Width,StripHeight},RGB(17,16,14));
    for(int y:{1,StripHeight-3})fill({0,y,Width,y+2},RGB(122,94,51));
    fill({0,4,Width,5},RGB(51,43,30));
    const char* labels[]={"N","NE","E","SE","S","SW","W","NW"};
    HFONT major=CreateFontW(34,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    HFONT minor=CreateFontW(25,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    auto oldFont=SelectObject(dc,major);SetBkMode(dc,TRANSPARENT);
    for(int x=0;x<=Width;x+=Period/32) {
        const int step=(x-Window/2+Period)/ (Period/32);
        const bool cardinal=step%8==0,diagonal=step%4==0;
        fill({x-1,diagonal?63:73,x+1,85},cardinal?RGB(231,202,141):RGB(133,112,75));
        if(!diagonal)continue;
        const int index=(step/4)%8;
        SelectObject(dc,cardinal?major:minor);
        SetTextColor(dc,index==0?RGB(240,209,139):cardinal?RGB(229,218,191):RGB(170,157,128));
        RECT r{x-44,20,x+44,61};DrawTextA(dc,labels[index],-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    // A separate fixed marker overlays the scrolling bearing strip. Its
    // transparent tile shares this one immutable atlas and swapchain.
    for(int y=0;y<MarkerHeight;++y) {
        const int inset=y;
        fill({inset,StripHeight+y,MarkerWidth-inset,StripHeight+y+1},RGB(242,204,120));
    }
    GdiFlush();const auto* b=static_cast<uint8_t*>(bits);std::vector<uint8_t> rgba(Width*Height*4);
    for(int i=0;i<Width*Height;++i){rgba[4*i]=b[4*i+2];rgba[4*i+1]=b[4*i+1];rgba[4*i+2]=b[4*i];rgba[4*i+3]=(b[4*i]||b[4*i+1]||b[4*i+2])?255:0;}
    SelectObject(dc,oldFont);DeleteObject(major);DeleteObject(minor);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return rgba;
}
}
namespace framevr::interaction {
namespace {
// Decode and downsample once. The small cached RGBA image is shared by every
// repaint; PNG decoding and COM setup never run at the atlas refresh rate.
std::vector<uint8_t> DecodeArt(const wchar_t* name,UINT width,UINT height) {
        std::vector<uint8_t> rgba;
        HMODULE module=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&DecodeArt),&module);
        const auto resource=FindResourceW(module,name,MAKEINTRESOURCEW(10));
        const DWORD bytes=resource?SizeofResource(module,resource):0;
        auto* data=resource?static_cast<BYTE*>(LockResource(LoadResource(module,resource))):nullptr;
        if(!bytes||!data)return rgba;
        const HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        IWICImagingFactory* wic=nullptr;IWICStream* stream=nullptr;
        IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;
        IWICBitmapScaler* scaler=nullptr;IWICFormatConverter* converter=nullptr;
        if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)))&&
            SUCCEEDED(wic->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromMemory(data,bytes))&&
            SUCCEEDED(wic->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder))&&
            SUCCEEDED(decoder->GetFrame(0,&frame))&&SUCCEEDED(wic->CreateBitmapScaler(&scaler))&&
            SUCCEEDED(scaler->Initialize(frame,width,height,WICBitmapInterpolationModeFant))&&
            SUCCEEDED(wic->CreateFormatConverter(&converter))&&
            SUCCEEDED(converter->Initialize(scaler,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,
                nullptr,0,WICBitmapPaletteTypeCustom))) {
            rgba.resize(width*height*4);
            if(FAILED(converter->CopyPixels(nullptr,width*4,DWORD(rgba.size()),rgba.data())))rgba.clear();
        }
        for(IUnknown* p:{static_cast<IUnknown*>(converter),static_cast<IUnknown*>(scaler),
            static_cast<IUnknown*>(frame),static_cast<IUnknown*>(decoder),static_cast<IUnknown*>(stream),static_cast<IUnknown*>(wic)})if(p)p->Release();
        if(SUCCEEDED(co))CoUninitialize();
        if(rgba.empty())OutputDebugStringA("Frame VR UI: embedded artwork unavailable; using fallback shapes\n");
        return rgba;
}
const std::vector<uint8_t>& WristArt() {
    static const auto pixels=DecodeArt(L"D2RVR_WRIST_VITALS",512,256);return pixels;
}
const std::vector<uint8_t>& WheelArt() {
    static const auto pixels=DecodeArt(L"D2RVR_WHEEL_FRAME",768,768);return pixels;
}
const std::vector<uint8_t>& HandWheelArt() {
    static const auto pixels=DecodeArt(L"D2RVR_HAND_WHEEL_FRAME",768,768);return pixels;
}
bool KnownVital(bool known,float value,float maximum) {
    return known&&std::isfinite(value)&&std::isfinite(maximum)&&maximum>0&&value>=0;
}
void DrainOrb(std::vector<uint8_t>& rgba,bool mana,bool known,float value,float maximum) {
    // Measured glass interiors in the generated sprite, after 512x256 scaling.
    // Only glass is changed: carved frames/figures retain their original colors.
    const float cx=mana?364.0f:147.8f,cy=125.0f,rx=57.5f,ry=56.5f;
    const float fraction=KnownVital(known,value,maximum)?std::clamp(value/maximum,0.f,1.f):0;
    if(fraction>=1)return;
    const float level=cy+ry-2*ry*fraction;
    for(int y=66;y<184;++y)for(int x=int(cx-rx-2);x<int(cx+rx+2);++x) {
        const float dx=(x+.5f-cx)/rx,dy=(y+.5f-cy)/ry;
        const float radius=std::sqrt(dx*dx+dy*dy);
        auto* p=&rgba[(y*512+x)*4];
        const int dominant=p[mana?2:0],other=std::max(p[1],p[mana?0:2]);
        // Colored glass near the rim may be slightly irregular. Include that
        // fringe, while leaving the neutral metal/stone (and any inset hands).
        const bool glass=radius<.92f||(radius<1.045f&&dominant>other+10);
        if(!glass||!p[3])continue;
        if(fraction==0||y+.5f<level) {
            const float shade=p[0]*.2126f+p[1]*.7152f+p[2]*.0722f;
            p[0]=uint8_t(7+shade*.21f);p[1]=uint8_t(9+shade*.23f);p[2]=uint8_t(12+shade*.26f);
        } else if(y+.5f<level+1.0f) {
            // A one-pixel meniscus makes the live level legible at wrist size.
            for(int c=0;c<3;++c)p[c]=uint8_t(std::min(255,int(p[c])+((c==(mana?2:0))?45:12)));
        }
    }
}
}
std::vector<uint8_t> Paint(const Snapshot& s,const Output& o,int selection,const controllers::Labels& labels) {
    constexpr int size=1024;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=size;info.bmiHeader.biHeight=-size;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return {};}
    auto old=SelectObject(dc,bitmap);std::memset(bits,0,size*size*4);
    HFONT font=CreateFontW(20,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    HFONT shortcutFont=CreateFontW(14,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    HFONT cardFont=CreateFontW(SkillCardFont,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    HFONT skillFont=CreateFontW(18,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(238,235,220));
    auto fill=[&](RECT r,COLORREF c){auto b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);};
    auto text=[&](const char* t,RECT r){wchar_t w[256]{};MultiByteToWideChar(CP_UTF8,0,t,-1,w,256);DrawTextW(dc,w,-1,&r,DT_CENTER|DT_VCENTER|DT_WORDBREAK|DT_NOPREFIX);};
    auto paragraph=[&](const std::string& t,RECT r){wchar_t w[1024]{};MultiByteToWideChar(CP_UTF8,0,t.c_str(),-1,w,1024);DrawTextW(dc,w,-1,&r,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);};
    auto polygon=[&](POINT* p,int n,COLORREF c){auto b=CreateSolidBrush(c);auto was=SelectObject(dc,b);auto pen=SelectObject(dc,GetStockObject(NULL_PEN));Polygon(dc,p,n);SelectObject(dc,pen);SelectObject(dc,was);DeleteObject(b);};
    const bool handWheel=o.wheelHand>=0;
    const bool fiveSectors=handWheel&&!o.utility;
    auto wheelArt=fiveSectors?HandWheelArt():WheelArt();
    if(!wheelArt.empty()) {
        static const auto cells=[] {
            std::array<std::vector<int16_t>,2> result;
            for(int layout=0;layout<2;++layout){
                result[layout].resize(768*768);
                for(int y=0;y<768;++y)for(int x=0;x<768;++x)
                    result[layout][y*768+x]=int16_t(WheelCell((x+.5f)/768,(y+.5f)/768,-1,MenuPage::Spells,layout?0:-1));
            }
            return result;
        }();
        for(int y=0;y<768;++y)for(int x=0;x<768;++x) {
            auto* p=&wheelArt[(y*768+x)*4];
            const int cell=cells[fiveSectors?1:0][y*768+x];
            const bool active=!o.editor&&!o.utility&&cell==(fiveSectors?0:selection);
            if(!o.developer&&!o.map&&o.editor<2&&p[3]&&cell>=0&&(!o.utility||cell!=2)&&(cell==o.hover||active)) {
                const float amount=cell==o.hover?.26f:.13f;
                const int gold[]={185,133,55};
                for(int c=0;c<3;++c)p[c]=uint8_t(p[c]*(1-amount)+gold[c]*amount);
            }
        }
        GdiFlush();auto* dst=static_cast<uint8_t*>(bits);
        for(int y=0;y<768;++y)for(int x=0;x<768;++x) {
            const auto* p=&wheelArt[(y*768+x)*4];auto* d=&dst[(y*size+x)*4];
            d[0]=p[2];d[1]=p[1];d[2]=p[0];d[3]=p[3];
        }
    }
    auto icon=[&](std::shared_ptr<const icons::Image> image,RECT box,bool available=true) {
        if(!image)return false;
        GdiFlush();auto* dst=static_cast<uint8_t*>(bits);
        const float scale=std::min(float(box.right-box.left)/image->width,float(box.bottom-box.top)/image->height);
        const int w=std::max(1,int(image->width*scale)),h=std::max(1,int(image->height*scale));
        const int left=(box.left+box.right-w)/2,top=(box.top+box.bottom-h)/2;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            if(left+x<0||left+x>=size||top+y<0||top+y>=size)continue;
            const auto* p=&image->rgba[(std::min(image->height-1,unsigned(y/scale))*image->width+std::min(image->width-1,unsigned(x/scale)))*4];
            auto* d=&dst[((top+y)*size+left+x)*4];const float a=p[3]/255.f;
            for(int c=0;c<3;++c) {
                const float value=available?p[2-c]:((p[0]+p[1]+p[2])/3.f)*.45f;
                d[c]=uint8_t(value*a+d[c]*(1-a));
            }
        }
        return true;
    };
    auto pair=[&](int set,RECT box) {
        const auto& a=s.sets[set][0];const auto& b=s.sets[set][1];
        if(a.handle&&b.handle) {
            const LONG mid=(box.left+box.right)/2;
            icon(icons::Get(a),{box.left,box.top,mid,box.bottom});
            icon(icons::Get(b),{mid,box.top,box.right,box.bottom});
        } else icon(icons::Get(a.handle?a:b),box);
    };
    const int sectors=handWheel&&!o.utility?5:8;
    for(int i=0;!o.developer&&!o.map&&o.editor<2&&i<sectors;++i) {
        const int slot=handWheel&&!o.utility?skills::HandSlot(o.wheelHand,i):i;
        if(o.utility&&i==2)continue; // Native tabbed menu retains journal access.
        const float sector=2*Pi/sectors;
        const float angle=i*sector+(sectors==5?0:WheelPhase);
        const int x=int(384+std::sin(angle)*218),y=int(345.6f-std::cos(angle)*218)-(std::cos(angle)<-.5f?14:0);
        if(wheelArt.empty()) {
            POINT points[34];int n=0;
            for(int k=0;k<=16;++k){float a=angle-sector/2+.012f+(sector-.024f)*k/16;points[n++]={LONG(384+std::sin(a)*308),LONG(345.6f-std::cos(a)*308)};}
            for(int k=16;k>=0;--k){float a=angle-sector/2+.012f+(sector-.024f)*k/16;points[n++]={LONG(384+std::sin(a)*110),LONG(345.6f-std::cos(a)*110)};}
            polygon(points,n,o.hover==i?RGB(100,76,40):RGB(32,29,26));
        }
        char label[120]{};RECT box{x-51,y-64,x+51,y+31};
        if(!o.utility) {
            box={x-37,y-43,x+37,y+28};
            const auto& skill=s.skills[slot];
            if(skill.known&&skill.id>=0)icon(icons::Get("s"+std::to_string(skill.id)),box,skill.available);
            if(o.editor)std::snprintf(label,sizeof label,"%d. %s",i+1,skill.known?skill.name:"Unknown");
            else std::snprintf(label,sizeof label,"%s",skill.known?skill.name:"Unknown");
            if(!o.editor&&(handWheel?i==0:i==selection)) {
                const auto pen=CreatePen(PS_SOLID,3,RGB(230,190,96));
                const auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
                RoundRect(dc,box.left-5,box.top-5,box.right+5,box.bottom+5,10,10);
                SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
                fill({x-34,y+12,x+34,y+28},RGB(100,74,29));
                const auto prior=SelectObject(dc,shortcutFont);text(handWheel?((o.wheelHand==1?o.leftCue.gesture:o.cue.gesture)?"GESTURE":"PRIMARY"):"ACTIVE",{x-34,y+12,x+34,y+28});SelectObject(dc,prior);
            }
        } else if(i<4) {
            const char* utility[]={"Weapon I","Weapon II","",o.stowed?"Draw weapons":"Stow weapons"};
            std::snprintf(label,sizeof label,"%s",utility[i]);
            if(i<2&&s.equipmentKnown)pair(i,box);
            else if(i==3&&s.equipmentKnown&&s.activeSet>=1&&s.activeSet<=2)pair(s.activeSet-1,box);
        } else {
            const auto& b=s.belt[i-4];
            if(b.known&&b.count)icon(icons::Get(b.Next()),box);
            std::snprintf(label,sizeof label,"%s%s%d",b.known?(b.count?PotionName(PotionType(b.Next().code)):"Empty"):"Unknown",b.known&&b.count?" x":"\nSlot ",b.known&&b.count?b.count:i-3);
        }
        SetTextColor(dc,(!o.utility&&s.skills[slot].known&&!s.skills[slot].available)?RGB(148,137,117):RGB(235,218,181));
        if(o.utility)text(label,{x-76,y+35,x+76,y+86});
        if(!o.utility) {
            auto previous=SelectObject(dc,skillFont);
            text(label,{x-63,y+29,x+63,y+67});
            fill({x-77,y-79,x+77,y-46},RGB(100,78,44));
            fill({x-75,y-77,x+75,y-48},RGB(24,20,16));
            SetTextColor(dc,RGB(235,205,151));
            SelectObject(dc,shortcutFont);
            text(handWheel?labels.Button(o.wheelHand,i).c_str():skills::NativeLabel(i),{x-73,y-77,x+73,y-48});
            SetTextColor(dc,RGB(206,191,159));
            text(labels.Slot(slot).c_str(),{x-85,y+68,x+85,y+105});
            SelectObject(dc,previous);
        }
    }
    SetTextColor(dc,RGB(235,218,181));
    if(wheelArt.empty()) {
        fill({315,278,453,414},RGB(39,35,30));fill({23,688,434,741},RGB(39,35,30));fill({472,688,745,741},RGB(39,35,30));
    }
    auto frame=[&](RECT box,bool hot=false) {
        fill(box,hot?RGB(183,143,73):RGB(104,83,53));
        RECT inside{box.left+2,box.top+2,box.right-2,box.bottom-2};fill(inside,RGB(18,16,14));
        inside={box.left+5,box.top+5,box.right-5,box.bottom-5};fill(inside,hot?RGB(65,49,29):RGB(35,29,23));
    };
    if(o.developer) {
        frame({20,28,748,681});
        const bool available=dev::ValidClass(s.devClass);
        char title[120];std::snprintf(title,sizeof title,"DEVELOPMENT / %s",available?dev::Presets[s.devClass].name:"OFFLINE EXPANSION");
        text(title,{38,42,730,85});
        SelectObject(dc,shortcutFont);
        text(available?dev::PageHelp[std::clamp(o.devPage,0,dev::PageCount-1)]:"Load an offline expansion character in pcvr-dev.",{38,83,730,101});
        SelectObject(dc,font);
        for(int page=0;page<dev::PageCount;++page) {
            const auto b=DeveloperPageBounds(page);
            RECT r{LONG(768*b.left),LONG(768*b.top),LONG(768*b.right),LONG(768*b.bottom)};
            frame(r,o.devPage==page||o.hover==DeveloperPageCell(page));
            text(dev::PageNames[page],r);
        }
        for(int i:dev::Pages[std::clamp(o.devPage,0,dev::PageCount-1)]) {
            if(i<0)continue;
            const auto bounds=DeveloperBounds(i);
            const int x=int(768*bounds.left),y=int(768*bounds.top),right=int(768*bounds.right),bottom=int(768*bounds.bottom);
            const auto choice=available?dev::ChoiceFor(s.devClass,i):dev::Choices[i];
            frame({x,y,right,bottom},available&&o.hover==DeveloperCell(i));
            SetTextColor(dc,available?RGB(235,218,181):RGB(148,137,117));
            const char* title=i==dev::DebugDrawsChoice?(s.devDebugDraws?"Render debug draws: ON":"Render debug draws: OFF"):
                              i==dev::TurnsKeepOwnChoice?(s.devTurnsKeepOwn==1?"By turns own picture: NEAR":s.devTurnsKeepOwn==2?"By turns own picture: ALL":
                                                          s.devTurnsKeepOwn==3?"By turns own picture: FX":s.devTurnsKeepOwn==4?"By turns own picture: NEAR+FX":"By turns own picture: OFF"):
                              i==dev::TurnsSharpChoice?(s.devTurnsSharp?"By turns filter: SHARP":"By turns filter: SOFT"):
                              i==dev::TurnsFillChoice?(s.devTurnsFill?"By turns ghosts: FIXED":"By turns ghosts: OLD"):
                              i==dev::TurnsRimsChoice?(s.devTurnsRims?"By turns rims: MOVED":"By turns rims: OLD"):
                              i==dev::SkyRimsChoice?(s.devSkyRims?"Sky over rims: ON":"Sky over rims: OFF"):choice.name;
            text(title,{x+8,y+4,right-8,y+29});
            SelectObject(dc,shortcutFont);text(choice.detail,{x+8,y+29,right-8,bottom-4});SelectObject(dc,font);
        }
        SetTextColor(dc,RGB(235,218,181));SelectObject(dc,shortcutFont);
        text(o.result==Result::Pending?"Working...":o.result==Result::DebugUpdated?(s.devDebugDraws?"Cyan: measured / Amber: fallback. Hit flash: green block, red rejected, pink native skip.":"Debug draws OFF. Blocking behavior is unchanged."):
             o.result==Result::TurnsUpdated?(s.devTurnsKeepOwn==1?"By turns: the eye not drawn keeps its own picture round near things only.":
                                         s.devTurnsKeepOwn==2?"By turns: the eye not drawn keeps its own picture wherever both show the same surface.":
                                         s.devTurnsKeepOwn==3?"By turns: the eye not drawn keeps its own picture where the game's effects are (spells, flames, wings).":
                                         s.devTurnsKeepOwn==4?"By turns: the eye not drawn keeps its own picture round near things and where the game's effects are.":
                                         "By turns: the eye not drawn takes the drawn eye's picture wherever it reaches."):
             o.result==Result::SharpUpdated?(s.devTurnsSharp?"By turns: the eye not drawn is read sharp (Catmull-Rom).":"By turns: the eye not drawn is read soft (bilinear), as before."):
             o.result==Result::FillUpdated?(s.devTurnsFill?"By turns: no frame-old ghosts beside moving things.":"By turns: beside near things the eye keeps its own older picture, as before."):
             o.result==Result::RimsUpdated?(s.devTurnsRims?"By turns: the dark rim round roofs and trees against the sky moves with them.":
                                        "By turns: the rim round things against the sky stays with the sky, as before. Watch roof edges."):
             o.result==Result::SkyRimsUpdated?(s.devSkyRims?"The sky is painted over the dark rim round roofs and trees.":
                                           "The dark rim round roofs and trees against the sky stays, as before."):
             o.result==Result::Applied?"Kit added to inventory. Equip it using the native inventory screen.":
             o.result==Result::Dispatched?"Commands sent. Check character stats / destination in game.":
             o.result==Result::KitRefused?"Kit could not be added. Check inventory space and the development log.":
             o.result==Result::Refused?"Action unavailable. Close native menus and retry; see the development log.":
             "Release reference: VR SETTINGS > RELEASE NOTES. Gear grants keep existing items.",{38,628,730,677});
        SelectObject(dc,font);
    } else if(o.map) {
        frame({20,18,748,680});
        text("MAP",{38,27,730,78});
        const RECT content{LONG(MapContent.left*768),LONG(MapContent.top*768),LONG(MapContent.right*768),LONG(MapContent.bottom*768)};
        frame({content.left-4,content.top-4,content.right+4,content.bottom+4});
        text("Waiting for map...",content);
        SelectObject(dc,shortcutFont);text("Hand map + compass: VR Settings > HUD",{38,650,730,677});SelectObject(dc,font);
    } else if(o.editor==3) {
        frame({20,28,748,681});
        text("QUICK USE / INVENTORY",{38,47,730,88});
        SelectObject(dc,shortcutFont);text("Choose an item type. Restocking keeps the assignment.",{38,96,550,143});SelectObject(dc,font);
        frame({568,92,730,146},o.hover==8);text("CLEAR SLOT",{574,98,724,140});
        for(int i=0;i<8;++i) {
            const int x=int(768*(.05f+(i%2)*.46f)),y=int(768*(.23f+(i/2)*.145f));
            frame({x,y,x+337,y+99},o.hover==i&&i<o.editCount);
            if(i>=o.editCount)continue;
            const auto& item=o.itemOptions[i];
            icon(icons::Get(item.item),{x+9,y+10,x+88,y+89},item.quantity>0);
            text(item.name,{x+94,y+9,x+329,y+66});
            char detail[64];std::snprintf(detail,sizeof detail,"Available: %d",item.quantity);
            SetTextColor(dc,RGB(169,151,118));text(detail,{x+94,y+68,x+329,y+95});SetTextColor(dc,RGB(235,218,181));
        }
        if(!o.editCount)text("No usable items in carried inventory",{56,249,712,430});
    } else if(o.editor==2) {
        frame({20,28,748,681});
        char title[120];std::snprintf(title,sizeof title,"CHOOSE SKILL  /  %s",handWheel?labels.Slot(o.editSlot).c_str():skills::NativeLabel(o.editSlot));
        text(title,{38,47,730,88});
        char pageLabel[100];std::snprintf(pageLabel,sizeof pageLabel,"%s  /  Page %d of %d",
            skillcategories::Name(o.editClass,o.editCategory),o.editPage+1,o.editPages);
        SelectObject(dc,skillFont);text(pageLabel,{38,96,552,143});SelectObject(dc,font);
        frame({568,92,730,146},o.hover==8);text("CLEAR SLOT",{574,98,724,140});
        for(int category=0;category<skillcategories::Count;++category) {
            const auto b=SkillCategoryBounds(category);
            RECT r{LONG(768*b.left),LONG(768*b.top),LONG(768*b.right),LONG(768*b.bottom)};
            const bool available=o.editCategoryCounts[category]>0;
            frame(r,o.editCategory==category||(available&&o.hover==SkillCategoryCell(category)));
            SetTextColor(dc,available?RGB(235,218,181):RGB(148,137,117));
            SelectObject(dc,skillFont);text(skillcategories::Name(o.editClass,category),{r.left+6,r.top+3,r.right-6,r.bottom-20});
            char count[16];std::snprintf(count,sizeof count,"%d",o.editCategoryCounts[category]);
            SelectObject(dc,shortcutFont);text(count,{r.left+6,r.bottom-20,r.right-6,r.bottom-3});
        }
        SetTextColor(dc,RGB(235,218,181));SelectObject(dc,font);
        for(int i=0;i<8;++i) {
            const auto b=SkillOptionBounds(i);
            const int x=int(768*b.left),y=int(768*b.top),right=int(768*b.right),bottom=int(768*b.bottom);
            frame({x,y,right,bottom},o.hover==i&&i<o.editCount);
            if(i>=o.editCount)continue;
            const auto& skill=o.editOptions[i];
            icon(icons::Get("s"+std::to_string(skill.id)),{x+9,y+10,x+79,y+80});
            text(skill.name,{x+85,y+7,right-8,y+60});
            char detail[64];if(skill.owner==-1)std::snprintf(detail,sizeof detail,"Level %d",skill.level);
            else std::snprintf(detail,sizeof detail,"Item-granted skill");
            SelectObject(dc,shortcutFont);SetTextColor(dc,RGB(169,151,118));
            // Gesture skills (skill_usage.h): GESTURE from this slot, ON TRIGGER where a trigger makes one.
            const bool here=skillusage::Gesture(skill.id,o.editSlot);
            const bool trigger=here||skillusage::Gesture(skill.id,skills::TriggerSlot(0))||skillusage::Gesture(skill.id,skills::TriggerSlot(1));
            text(detail,{x+85,y+63,trigger?right-106:right-8,bottom-4});
            if(trigger) {
                const RECT chip{right-102,bottom-27,right-8,bottom-7};
                fill(chip,here?RGB(100,74,29):RGB(48,40,28));SetTextColor(dc,here?RGB(235,218,181):RGB(169,151,118));
                text(here?"GESTURE":"ON TRIGGER",chip);
            }
            SetTextColor(dc,RGB(235,218,181));SelectObject(dc,font);
        }
        // The skill pointed at: how it works from the slot being assigned
        // (skill_usage.h), on a card over the other column.
        if(o.hover>=0&&o.hover<o.editCount) {
            const auto& skill=o.editOptions[o.hover];
            const auto card=SkillCardBounds(o.hover);
            const RECT box{LONG(768*card.left),LONG(768*card.top),LONG(768*card.right),LONG(768*card.bottom)};
            fill(box,RGB(22,18,14));frame(box,true);
            SelectObject(dc,font);text(skill.name,{box.left+10,box.top+8,box.right-10,box.top+36});
            const bool trigger=o.editSlot==skills::TriggerSlot(skills::Hand(o.editSlot));
            SelectObject(dc,shortcutFont);SetTextColor(dc,RGB(232,196,119));
            text(skillusage::Gesture(skill.id,o.editSlot)?"GESTURE ON THIS TRIGGER":trigger?"ON THIS TRIGGER":"ON THIS BUTTON",{box.left+10,box.top+38,box.right-10,box.top+56});
            SelectObject(dc,cardFont);SetTextColor(dc,RGB(235,218,181));
            paragraph(skillusage::Details(skill.id,o.editSlot),{box.left+SkillCardPad,box.top+SkillCardTop,box.right-SkillCardPad,box.bottom-SkillCardPad});
            SelectObject(dc,font);
        }
        if(!o.editCount)text("No active skills available",{56,249,712,430});
    } else if(handWheel&&!o.utility) {
        text(o.wheelHand?"LEFT HAND":"RIGHT HAND",{290,305,478,345});
        text("CLOSE",{301,356,467,398});
    } else if(o.editor) {
        text(s.bindingsEditable?"CHOOSE\nA SLOT":"SKILLS\nUNAVAILABLE",{301,301,467,397});
    } else {
        if(s.equipmentKnown&&s.activeSet>=1&&s.activeSet<=2)pair(s.activeSet-1,{346,295,422,363});
        text("BASIC ATTACK",{301,368,467,398});
        if(selection<0){const auto prior=SelectObject(dc,shortcutFont);text("ACTIVE",{327,399,441,417});SelectObject(dc,prior);}
    }
    if(QuickControls(Page(o))) {
        const auto q=QuickUseBounds;
        RECT box{LONG(q.left*768),LONG(q.top*768),LONG(q.right*768),LONG(q.bottom*768)};
        frame(box,o.hover==QuickUseCell);
        SelectObject(dc,shortcutFont);text("QUICK USE",{box.left+6,box.top+6,box.right-6,box.top+24});
        const auto* item=s.quick.Find(s.quick.assigned);
        const bool haveIcon=item&&icon(icons::Get(item->item),{box.left+8,box.top+28,box.left+56,box.bottom-24},item->quantity>0);
        SelectObject(dc,skillFont);
        const char* name=!s.quick.known?"Unavailable":item?item->name:s.quick.assigned?"Out of stock":"Assign item";
        text(name,{box.left+(haveIcon?60:8),box.top+26,box.right-8,box.bottom-40});
        SelectObject(dc,shortcutFont);
        if(item){char count[32];std::snprintf(count,sizeof count,"x %d",item->quantity);text(count,{box.left+60,box.bottom-44,box.right-8,box.bottom-25});}
        text("Click: use | Hold: edit",{box.left+6,box.bottom-23,box.right-6,box.bottom-5});SelectObject(dc,font);
        if(o.leftStrikes>=0) {   // Druid: which trigger skill the left claw's strikes cast
            const auto l=LeftStrikesBounds;
            RECT claw{LONG(l.left*768),LONG(l.top*768),LONG(l.right*768),LONG(l.bottom*768)};
            frame(claw,o.hover==LeftStrikesCell);
            SelectObject(dc,shortcutFont);text("LEFT CLAW STRIKES",{claw.left+6,claw.top+5,claw.right-6,claw.top+22});
            SelectObject(dc,skillFont);text(o.leftStrikes==1?"Right trigger skill":"Own trigger skill",{claw.left+6,claw.top+23,claw.right-6,claw.bottom-22});
            SelectObject(dc,shortcutFont);text("Click to toggle",{claw.left+6,claw.bottom-22,claw.right-6,claw.bottom-5});SelectObject(dc,font);
        }
    }
    // The skill pointed at and the slot it is (or would be) on: how it works there
    // (skill_usage.h), under the picker, the slot chooser and the hand wheel.
    const char* usage=[&]()->const char* {
        if(o.developer||o.map||o.utility||o.editor>=2)return "";   // the picker shows a card instead
        if(o.hover<0||o.hover>=(handWheel?5:8))return "";
        const int slot=handWheel?skills::HandSlot(o.wheelHand,o.hover):o.hover;
        return s.skills[slot].known?skillusage::Line(s.skills[slot].id,slot):"";
    }();
    if(o.editor) {
        char hint[120];std::snprintf(hint,sizeof hint,"Set %s  /  %s",s.activeSet==2?"II":"I",
            o.result==Result::Pending?"Saving...":o.result==Result::Applied?"Assigned":o.result==Result::Refused?"Changed or unavailable; choose the slot again":handWheel?"Click stick to close":"Hold Left Y while editing");
        if(o.editor==3)std::snprintf(hint,sizeof hint,"Page %d  /  %s",o.editPage+1,
            o.result==Result::Pending?"Saving...":o.result==Result::Refused?"Item changed or unavailable. Reopen the picker.":"Select to assign; BACK leaves your slot unchanged.");
        fill({29,637,739,680},RGB(22,18,14));
        if(usage[0]&&o.result!=Result::Pending&&o.result!=Result::Refused){SelectObject(dc,shortcutFont);text(usage,{35,639,733,678});SelectObject(dc,font);}
        else text(hint,{35,639,733,678});
    }
    const char* footer[5]={(o.utility||o.map||o.developer)?"SPELLS":"EQUIPMENT","MAP","VR SETTINGS",handWheel?"INVENTORY":"EDIT SKILLS","DEV"};
    if(o.editor==1){footer[0]="BACK";footer[1]="";footer[2]="DONE";}
    if(o.editor>=2){footer[0]="PREVIOUS";footer[1]="NEXT";footer[2]="BACK";}
    fill({20,686,749,745},RGB(18,16,14));
    const auto page=Page(o);
    for(int i=0;i<FooterCount(page,s.developer);++i) {
        const auto bounds=FooterBounds(i,page,s.developer);const int cell=FooterCell(i,page);
        const RECT box{LONG(bounds.left*768),LONG(bounds.top*768),LONG(bounds.right*768),LONG(bounds.bottom*768)};
        frame(box,o.hover==cell||(o.map&&cell==12)||(o.developer&&cell==13));
        text(footer[i],{box.left+5,box.top+5,box.right-5,box.bottom-5});
    }
    if(!o.developer&&!o.editor&&!o.utility&&!o.map) {
        char active[120];if(handWheel)std::snprintf(active,sizeof active,"Other hand: point + trigger to assign. Click this stick to close.");
        else std::snprintf(active,sizeof active,"ACTIVE: %s",o.cue.known?o.cue.name:"Unknown skill");
        const auto prior=SelectObject(dc,skillFont);SetTextColor(dc,RGB(232,196,119));
        if(handWheel&&usage[0]){SelectObject(dc,shortcutFont);text(usage,{38,639,730,685});}   // the slot pointed at
        else {
            if(handWheel){SelectObject(dc,shortcutFont);text("Top trigger slot: compatible skills also power weapon gestures.",{38,639,730,662});}
            text(active,handWheel?RECT{38,662,730,685}:RECT{150,648,618,681});
        }
        SelectObject(dc,prior);
    }
    // Reserved atlas space: a compact weapon icon and an optional two-line
    // explanation. The native icon comes from the same cache as its wheel slot.
    for(int hand=0;hand<2;++hand){
    const auto& cue=hand?o.leftCue:o.cue;
    const int offset=hand*200;
    auto cueRect=[&](int l,int t,int r,int b){return RECT{l,t+offset,r,b+offset};};
    SetTextColor(dc,RGB(238,235,220));
    fill(cueRect(768,0,864,96),RGB(115,89,43));fill(cueRect(772,4,860,92),RGB(25,21,17));
    const bool haveIcon=cue.id>=0&&icon(icons::Get("s"+std::to_string(cue.id)),cueRect(778,10,854,86),cue.available);
    if(!haveIcon)text(cue.id<0&&cue.known?"BASIC":"?",cueRect(776,8,856,88));
    fill(cueRect(768,100,1024,192),RGB(25,21,17));
    SetTextColor(dc,cue.available?RGB(235,218,181):RGB(166,153,128));
    const auto priorCue=SelectObject(dc,skillFont);
    text(cue.name,cueRect(774,104,1018,144));
    SelectObject(dc,shortcutFont);text(cue.hint,cueRect(774,146,1018,188));SelectObject(dc,priorCue);
    if(cue.temporary){SetTextColor(dc,RGB(232,196,119));text("QUICK CAST",cueRect(866,8,1018,55));}
    if(cue.motionUnavailable){SetTextColor(dc,RGB(232,196,119));text("TRIGGER ONLY",cueRect(866,8,1018,55));}
    }
    SetTextColor(dc,RGB(238,235,220));
    const COLORREF colours[]={RGB(90,90,90),RGB(169,37,45),RGB(43,82,193),RGB(131,55,165),RGB(67,151,66),RGB(183,191,205),RGB(218,165,70)};
    for(int i=0;i<4;++i) {
        const auto& b=s.belt[i];const int x=i*128;
        fill({x+42,774,x+86,814},RGB(160,135,83));
        fill({x+18,814,x+110,940},b.known&&b.count?colours[unsigned(PotionType(b.Next().code))]:RGB(45,47,47));
        char label[60];std::snprintf(label,sizeof label,"%s\nx%d",b.known?(b.count?PotionName(PotionType(b.Next().code)):"Empty"):"Unknown",b.count);
        if(b.known&&b.count&&icons::Get(b.Next())) {fill({x+18,774,x+110,940},RGB(28,25,22));icon(icons::Get(b.Next()),{x+20,780,x+108,935});}
        text(label,{x,943,x+128,1024});
    }
    // A grabbed bottle has its own tile. Native refill must not recolor it to
    // the next belt item; only confirmed consumption makes this preview empty.
    if(o.bottleItem.handle) {
        const auto kind=PotionType(o.bottleItem.code);
        fill({810,520,866,554},RGB(160,135,83));
        fill({790,554,886,693},o.bottleEmpty?RGB(45,47,47):colours[unsigned(kind)]);
        text(o.bottleEmpty?"Used":PotionName(kind),{768,703,896,768});
    }
    auto wristArt=WristArt();
    if(!wristArt.empty()) {
        DrainOrb(wristArt,false,s.vitalsKnown,s.health,s.maxHealth);
        DrainOrb(wristArt,true,s.vitalsKnown,s.mana,s.maxMana);
        GdiFlush();auto* dst=static_cast<uint8_t*>(bits);
        for(int y=0;y<256;++y)for(int x=0;x<512;++x) {
            const auto* p=&wristArt[(y*512+x)*4];auto* d=&dst[((y+768)*size+x+512)*4];
            d[0]=p[2];d[1]=p[1];d[2]=p[0];d[3]=p[3];
        }
        HFONT gothic=CreateFontW(19,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
        auto previous=SelectObject(dc,gothic);SetTextColor(dc,RGB(235,219,182));
        auto label=[&](const char* name,float value,float maximum,RECT rect) {
            char line[80];
            if(KnownVital(s.vitalsKnown,value,maximum))std::snprintf(line,sizeof line,"%s %.0f / %.0f",name,value,maximum);
            else std::snprintf(line,sizeof line,"%s -- / --",name);
            wchar_t w[80]{};MultiByteToWideChar(CP_UTF8,0,line,-1,w,80);
            DrawTextW(dc,w,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        };
        label("LIFE",s.health,s.maxHealth,{570,954,748,978});
        label("MANA",s.mana,s.maxMana,{789,954,964,978});
        SelectObject(dc,previous);DeleteObject(gothic);SetTextColor(dc,RGB(238,235,220));
    } else {
      fill({512,776,1024,1024},RGB(20,28,33));
      if(KnownVital(s.vitalsKnown,s.health,s.maxHealth)&&KnownVital(s.vitalsKnown,s.mana,s.maxMana)){
        auto gauge=[&](float value,float max,int y,COLORREF c,const char* name){
            fill({536,y,1000,y+32},RGB(50,50,50));
            if(max>0)fill({536,y,536+LONG(464*std::clamp(value/max,0.f,1.f)),y+32},c);
            char label[64];std::snprintf(label,sizeof label,"%s %.0f / %.0f",name,value,max);text(label,{536,y-31,1000,y});};
        gauge(s.health,s.maxHealth,842,RGB(180,35,42),"Health");gauge(s.mana,s.maxMana,908,RGB(48,83,185),"Mana");
      } else text("Status unavailable",{530,800,1006,885});
    }
    const char* status=o.result==Result::Pending?"Waiting for game...":o.result==Result::Applied?"Confirmed":o.result==Result::Refused?"Action not available":o.stowed?"Weapons stowed":"";
    if(*status)text(status,{530,768,1006,798});
    // Keep XP beneath the orb labels, sharing their wrist atlas and repaint
    // cadence. It never needs a separate XR layer or a per-frame CPU upload.
    fill({554,982,982,1022},RGB(99,77,45));
    fill({556,984,980,1020},RGB(19,17,14));
    fill({564,1007,972,1015},RGB(56,48,35));
    const auto& xp=s.experience;
    if(xp.Valid())fill({564,1007,564+LONG(408*xp.Progress()),1015},RGB(212,175,93));
    for(int i=1;i<10;++i)fill({564+i*408/10,1007,565+i*408/10,1015},RGB(99,77,45));
    HFONT xpFont=CreateFontW(17,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    const auto prior=SelectObject(dc,xpFont);SetTextColor(dc,RGB(235,219,182));
    char xpLabel[80];
    if(xp.Maximum())std::snprintf(xpLabel,sizeof xpLabel,"LEVEL %u  /  MAX LEVEL",xp.level);
    else if(xp.Valid())std::snprintf(xpLabel,sizeof xpLabel,"LEVEL %u  /  XP %.1f%%",xp.level,xp.Progress()*100);
    else std::snprintf(xpLabel,sizeof xpLabel,"XP --");
    RECT xpRect{564,985,972,1006};DrawTextA(dc,xpLabel,-1,&xpRect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    SelectObject(dc,prior);DeleteObject(xpFont);
    // A dedicated tile follows the hand navigation display, independent of
    // the scrolling compass atlas and the player's health/XP panel.
    const int nx=navigation::AtlasX,ny=navigation::AtlasY;
    fill({nx,ny,nx+navigation::Width,ny+navigation::Height},RGB(17,16,14));
    fill({nx,ny,nx+navigation::Width,ny+2},RGB(122,94,51));
    fill({nx,ny+navigation::Height-2,nx+navigation::Width,ny+navigation::Height},RGB(122,94,51));
    HFONT actFont=CreateFontW(17,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    HFONT areaFont=CreateFontW(19,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Georgia");
    const auto beforeLocation=SelectObject(dc,actFont);SetTextColor(dc,RGB(231,202,141));
    RECT actRect{nx+8,ny+7,nx+navigation::Width-8,ny+29};
    DrawTextA(dc,navigation::ActLabel(s.location.act),-1,&actRect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    SelectObject(dc,areaFont);SetTextColor(dc,RGB(238,231,212));
    wchar_t area[256]{};MultiByteToWideChar(CP_UTF8,0,navigation::AreaLabel(s.location),-1,area,256);
    RECT areaRect{nx+8,ny+31,nx+navigation::Width-8,ny+navigation::Height-3};
    DrawTextW(dc,area,-1,&areaRect,DT_CENTER|DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);
    SelectObject(dc,beforeLocation);DeleteObject(actFont);DeleteObject(areaFont);
    GdiFlush();std::vector<uint8_t> rgba(size*size*4);const auto* b=static_cast<const uint8_t*>(bits);
    for(int i=0;i<size*size;++i){
        rgba[4*i]=b[4*i+2];rgba[4*i+1]=b[4*i+1];rgba[4*i+2]=b[4*i];
        rgba[4*i+3]=(b[4*i]||b[4*i+1]||b[4*i+2])?255:0;
        if(!wheelArt.empty()&&i%size<768&&i/size<768) {
            const auto* p=&wheelArt[((i/size)*768+i%size)*4];
            const bool changed=rgba[4*i]!=p[0]||rgba[4*i+1]!=p[1]||rgba[4*i+2]!=p[2];
            rgba[4*i+3]=changed?255:p[3];
        }
        const int x=i%size-512,y=i/size-768;
        if(!wristArt.empty()&&x>=0&&y>=0) {
            const auto* p=&wristArt[(y*512+x)*4];
            // GDI supplies runtime text; the generated silhouette retains real
            // alpha, rather than turning translucent edge pixels into a box.
            const bool textPixel=rgba[4*i]!=p[0]||rgba[4*i+1]!=p[1]||rgba[4*i+2]!=p[2];
            rgba[4*i+3]=textPixel?255:p[3];
        }
    }
    SelectObject(dc,oldFont);DeleteObject(font);DeleteObject(shortcutFont);DeleteObject(skillFont);DeleteObject(cardFont);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return rgba;
}
std::vector<uint8_t> BloodArt(int width,int height) {
    return width>0&&height>0?DecodeArt(L"D2RVR_BLOOD_VISOR",UINT(width),UINT(height)):std::vector<uint8_t>{};
}
}
