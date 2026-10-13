#include <windows.h>
#include <cstdio>
#include <cstring>
#include "vr_settings_ui.h"
namespace framevr::settings {
std::vector<uint8_t> Paint(const Model& m,Hit hover,const char* runtimeStatus,const calibration::Flow* body,bool rendererUnavailable){
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=Width;
    info.bmiHeader.biHeight=-Height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return {};}
    auto old=SelectObject(dc,bitmap);SetBkMode(dc,TRANSPARENT);
    HFONT normal=CreateFontW(27,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
    HFONT bold=CreateFontW(32,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
    auto oldFont=SelectObject(dc,normal);
    auto fill=[&](Rect r,COLORREF c){RECT rect{r.x,r.y,r.x+r.w,r.y+r.h};auto brush=CreateSolidBrush(c);FillRect(dc,&rect,brush);DeleteObject(brush);};
    auto text=[&](const char* value,Rect r,bool strong=false,bool center=false,COLORREF color=RGB(229,234,235)){
        wchar_t wide[512]{};MultiByteToWideChar(CP_UTF8,0,value,-1,wide,512);
        SelectObject(dc,strong?bold:normal);SetTextColor(dc,color);RECT rect{r.x,r.y,r.x+r.w,r.y+r.h};
        DrawTextW(dc,wide,-1,&rect,DT_WORDBREAK|DT_NOPREFIX|(center?DT_CENTER:DT_LEFT));
    };
    const COLORREF quiet=RGB(154,174,182);
    auto button=[&](Rect rect,const char* label,Hit hit,bool selected=false,bool enabled=true){
        const bool hot=hover.kind==hit.kind&&hover.index==hit.index&&hover.delta==hit.delta;
        fill(rect,!enabled?RGB(33,40,44):hot?RGB(63,103,112):selected?RGB(39,80,89):RGB(31,48,56));
        rect.y+=13;text(label,rect,true,true,enabled?RGB(231,238,239):RGB(114,131,140));
    };
    fill({0,0,Width,Height},RGB(13,22,29));
    if(body){
        using calibration::Hint;
        text("CALIBRATE YOUR BODY",{60,48,1160,65},true,true);
        text("1. Stand upright and look straight ahead.",{80,153,1120,60},true);
        text("2. Stretch both arms out sideways at shoulder height (T-pose).",{80,238,1120,100},true);
        text("3. Release both triggers, then squeeze and hold both for a moment.",{80,348,1120,100},true);
        const char* hint="Ready. Hold both triggers and keep still.";
        switch(body->hint){
            case Hint::Release:hint="Release both triggers before starting.";break;
            case Hint::Floor:hint="Waiting for floor-relative tracking. If this persists, check SteamVR room setup, or press B to skip.";break;
            case Hint::Height:hint="Stand upright. Waiting for a plausible standing eye height above the floor.";break;
            case Hint::Tracking:hint="Waiting for tracked headset and both controllers.";break;
            case Hint::Upright:hint="Keep your head upright and look straight ahead.";break;
            case Hint::Arms:hint="Arms straight out to your sides, level with your shoulders. Then release and hold both triggers.";break;
            case Hint::Still:hint="Movement detected. Release both triggers and try again, keeping still.";break;
            case Hint::Saving:hint="Saving your measurements...";break;
            case Hint::Saved:hint="Saved. Release both triggers to return to the game.";break;
            case Hint::Failed:hint="Save failed. Release and hold both triggers to retry, or press B to cancel.";break;
            case Hint::Conflict:hint="Settings changed elsewhere. Release and hold both triggers to try again.";break;
            default:break;
        }
        fill({80,493,1120,210},RGB(22,36,44));
        text(hint,{110,520,1060,135},true,true,RGB(116,220,219));
        fill({110,663,1060,13},RGB(39,56,64));
        fill({110,663,int(1060*body->progress),13},RGB(116,220,219));
        if(body->phase==calibration::Phase::Saving||body->phase==calibration::Phase::Saved){
            const auto measured=body->Get();char label[160];
            std::snprintf(label,sizeof label,"Standing eye height: %.1f cm     Controller span: %.1f cm",measured.eyeHeight*100,measured.armSpan*100);
            text(label,{80,744,1120,55},false,true);
        }else text("Height sets world scale. Controller span fits your character's arms.",{80,744,1120,65},false,true,quiet);
        text("Right B: skip / cancel. Reopen any time with Calibrate body in VR settings.",{80,846,1120,80},false,true,quiet);
        text("Controls are blocked. The game world remains live.",{80,939,1120,50},false,true,quiet);
    }else{
    text("DIABLO II / VR SETTINGS",{40,33,960,55},true);
    text("Right-hand laser + trigger",{862,44,378,42},false,true,quiet);
    const char* tabs[]={"Controls","VR","Video"};
    for(int i=0;i<3;++i)button(Tabs[i],tabs[i],{Kind::Tab,i},int(m.page)==i);
    if(m.page==Page::Controls){
        text("LEFT HAND",{58,209,520,46},true);text("RIGHT HAND",{676,209,520,46},true);
        struct Help{const char* key;const char* action;};
        const Help left[]={{"Stick","Walk"},{"X","Inventory / close game panels"},{"Y (hold)","Spell and equipment wheel"},{"Grip + Y","Open VR settings / Calibrate body"},{"Stick click","Map"},{"Grip + stick click","Reset eye height"},{"Grip near belt","Grab a potion"}};
        const Help right[]={{"Stick / click + hold","Turn / sort inventory in native panels"},{"Trigger","Attack / panel click or drag"},{"A / B","Interact / cancel or close menu"},{"Dominant grip at shoulder","Either shoulder: swap weapon sets I / II"},{"Trigger at bow string","Nock, pull back, release to shoot"},{"Trigger with javelin","Hold, throw, release in WEAPON mode"},{"Left Grip + Right Trigger","Laser context click in game panels"}};
        for(int i=0;i<7;++i)for(int side=0;side<2;++side){
            const auto& h=(side?right:left)[i];const int x=side?658:40,y=264+i*72;
            fill({x,y,582,66},i%2?RGB(19,31,39):RGB(22,36,44));
            text(h.key,{x+18,y+5,550,31},false,false,RGB(116,220,219));
            text(h.action,{x+18,y+33,550,34});
        }
        text(m.Get()[Motion]>.5f?"Arrow skills: draw/release. Smite: left shield swing/thrust. Quick cast: Left Trigger + face; Left Grip for alternates.":"Motion combat is off. Enable it on the VR tab for swings, bow draw, javelin throws and the wheel.",{40,788,1200,42});
        text("Panels: Left Trigger = Ctrl; Right Grip = Shift; Right B = Back. Keep D2R focused.",{40,832,1200,42},false,false,quiet);
    }else{
        text(m.page==Page::VR?"Change values, then Apply. Controls are blocked while this panel is open.":runtimeStatus,{40,197,1200,42},false,false,quiet);
        for(int row=0;row<RowCount(m.page);++row){
            const Key key=RowKey(m.page,row);const auto& d=Definitions[key];const float value=m.Get()[key];const int y=RowY+row*RowStep;
            fill({40,y-4,1200,76},row%2?RGB(19,31,39):RGB(22,36,44));
            const bool unavailable=key==StereoRender&&value>.5f&&!m.Dirty()&&rendererUnavailable;
            text(d.label,{58,y+1,780,39},true);text(unavailable?"Select Standard and Apply before retrying Single pass":d.detail,{58,y+42,795,30},false,false,quiet);
            char label[96];
            if(key==Foveation){const char* modes[]={"Off","Fixed","Eye tracked"};std::snprintf(label,sizeof label,"%s",modes[int(value)]);}
            else if(key==Dominant)std::snprintf(label,sizeof label,"%s",value>.5f?"Left":"Right");
            else if(key==StereoRender)std::snprintf(label,sizeof label,"%s",unavailable?"Unavailable":value>.5f?"Single pass":"Standard");
            else if(d.max==1)std::snprintf(label,sizeof label,"%s",value>.5f?"On":"Off");
            else if(key==Snap&&value==0)std::snprintf(label,sizeof label,"Smooth");
            else if(key==Snap)std::snprintf(label,sizeof label,"Snap %.0f deg",value);
            else if(key==TurnSpeed)std::snprintf(label,sizeof label,"%.0f deg/s",value);
            else if(key==Waist||key==BeltForward)std::snprintf(label,sizeof label,"%.1f cm",value*100);
            else std::snprintf(label,sizeof label,"%.0f",value);
            const bool enabled=m.ready&&!m.Busy();
            button(Decrease(row),"-",{Kind::Change,key,-1},false,enabled);
            button(Increase(row),"+",{Kind::Change,key,1},false,enabled);
            button(ValueBox(row),label,{d.max==1?Kind::Change:Kind::None,key,1},false,enabled);
        }
        if(m.page==Page::Video){
            text("Game graphics: Options > Video. Stream resolution: SteamVR. Eye tracking requires runtime permission.",{40,816,1200,48},false,false,quiet);
        }
    }
    const char* message=!m.ready?"Configuration unavailable; controls help still works.":m.result==Result::Saving?"Saving...":m.result==Result::Saved?"Saved. Changes take effect live.":m.result==Result::Failed?"Save failed. Your edits are still unsaved.":m.result==Result::Conflict?"Settings changed elsewhere. Current values reloaded.":m.Dirty()?"Unsaved changes. Apply saves; Close discards them.":"World remains live. Right B closes this panel.";
    text(message,{40,876,1200,38},false,false,m.Dirty()?RGB(241,207,139):quiet);
    button(Recenter,"Reset eye height",{Kind::Recenter});
    button(Calibrate,"Calibrate body",{Kind::Calibrate},false,m.ready&&!m.Busy()&&!m.Dirty());
    button(Close,"Close",{Kind::Close});
    button(Apply,"Apply",{Kind::Apply},true,m.ready&&m.Dirty()&&!m.Busy());
    }
    GdiFlush();std::vector<uint8_t> rgba(Width*Height*4);const auto* p=static_cast<uint8_t*>(bits);
    for(int i=0;i<Width*Height;++i){rgba[4*i]=p[4*i+2];rgba[4*i+1]=p[4*i+1];rgba[4*i+2]=p[4*i];rgba[4*i+3]=255;}
    SelectObject(dc,oldFont);DeleteObject(normal);DeleteObject(bold);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return rgba;
}
}
