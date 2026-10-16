#include <windows.h>
#include <cstdio>
#include <cstring>
#include "vr_settings_ui.h"
namespace framevr::settings {
std::vector<uint8_t> Paint(const Model& m,Hit hover,const char* runtimeStatus,const calibration::Flow* body,const controllers::Labels& detected){
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
    const auto labels=controllers::Resolve(controllers::Choice(int(m.Get()[ControllerLabels])),detected);
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
    const char* tabs[]={"Controls","VR","Video","HUD","Combat","Haptics"};
    for(int i=0;i<TabCount;++i)button(Tabs[i],tabs[i],{Kind::Tab,i},int(m.page)==i);
    if(m.page==Page::Controls){
        text("Each stick opens its hand's wheel. Use the OTHER hand's laser to assign skills.",{40,180,1200,32},false,false,RGB(116,220,219));
        text("LEFT HAND",{58,215,520,40},true);text("RIGHT HAND",{676,215,520,40},true);
        struct Help{const char* key;const char* action;};
        const auto leftFaces=labels.Button(1,1)+" / "+labels.Button(1,2);
        const auto leftAlternates=labels.Button(1,3)+" / "+labels.Button(1,4);
        const auto rightFaces=labels.Button(0,1)+" / "+labels.Button(0,2)+" / grip + face";
        const Help left[]={{"Stick","Walk"},{leftFaces.c_str(),"Assigned left-hand skills"},{leftAlternates.c_str(),"Alternate left-hand skills"},{"Trigger","Assigned left-hand skill"},{"Stick click","Toggle left wheel; right laser edits"},{"Grip + stick click","Inventory / character / skills menu"},{"Grip near belt","Grab a potion"}};
        const Help right[]={{"Stick / click","Turn / toggle right wheel"},{"Trigger","Assigned right skill / grab bow string"},{"Hold Right Grip + Right Trigger","Show all nearby loot labels (Alt)"},{rightFaces.c_str(),"Interact by default / assigned skills"},{"Dominant grip at shoulder","Either shoulder: swap weapon sets I / II"},{"Trigger with bow / javelin","Draw & release / throw in WEAPON mode"},{"Left Grip + Right Trigger","Laser context click in game panels"}};
        for(int i=0;i<7;++i)for(int side=0;side<2;++side){
            const auto& h=(side?right:left)[i];const int x=side?658:40,y=252+i*58;
            fill({x,y,582,56},i%2?RGB(19,31,39):RGB(22,36,44));
            text(h.key,{x+18,y+2,550,29},false,false,RGB(116,220,219));
            text(h.action,{x+18,y+30,550,30});
        }
        text(Definitions[WeaponSkillIndicators].label,{40,660,875,32});
        text(Definitions[WeaponSkillIndicators].detail,{40,692,875,29},false,false,quiet);
        button(WeaponIndicatorsBox,m.Get()[WeaponSkillIndicators]>.5f?"On":"Off",{Kind::Change,WeaponSkillIndicators,1},false,m.ready&&!m.Busy());
        text(Definitions[PlanarSpellAim].label,{40,722,875,32});
        text(Definitions[PlanarSpellAim].detail,{40,754,875,29},false,false,quiet);
        button(PlanarSpellAimBox,m.Get()[PlanarSpellAim]>.5f?"On":"Off",{Kind::Change,PlanarSpellAim,1},false,m.ready&&!m.Busy());
        text(Definitions[ProjectileAimAssist].label,{40,784,875,32});
        text(Definitions[ProjectileAimAssist].detail,{40,816,875,29},false,false,quiet);
        button(AimAssistBox,AimAssistNames[int(m.Get()[ProjectileAimAssist])],{Kind::Change,ProjectileAimAssist,1},false,m.ready&&!m.Busy());
        const auto panelHelp="Panels: Left Trigger = Ctrl; Right Grip = Shift; Right "+labels.Button(0,2)+" = Back.";
        text(panelHelp.c_str(),{40,845,1200,29},false,false,quiet);
    }else{
        if(m.page==Page::VR){
            text("Controller labels - select your controllers",{40,181,810,29},true);
            char status[128];std::snprintf(status,sizeof status,"Detected: left %s / right %s. Automatic or manual override:",
                controllers::Name(detected.hands[1]),controllers::Name(detected.hands[0]));
            text(status,{40,212,815,27},false,false,quiet);
            fill(ControllerBox,hover.kind==Kind::ControllerMenu?RGB(63,103,112):RGB(31,48,56));
            const std::string selected=std::string(controllers::Choices[int(m.Get()[ControllerLabels])])+"  \xe2\x96\xbc";
            text(selected.c_str(),{ControllerBox.x,ControllerBox.y+10,ControllerBox.w,37},true,true,RGB(116,220,219));
        }else text(m.page==Page::Haptics?"Tune vibration for your headset and controllers.":m.page==Page::Combat?"Tune physical weapon combat.":m.page==Page::HUD?"Adjust your HUD and dropped-item labels.":runtimeStatus,{40,197,1200,42},false,false,quiet);
        for(int row=0;row<RowCount(m.page);++row){
            const Key key=RowKey(m.page,row);const auto& d=Definitions[key];const float value=m.Get()[key];const int y=RowY+row*RowStep;
            fill({40,y-4,1200,RowStep},row%2?RGB(19,31,39):RGB(22,36,44));
            const int textWidth=m.page==Page::Haptics?520:780;
            text(d.label,{58,y+1,textWidth,29},true);text(d.detail,{58,y+30,textWidth,28},false,false,quiet);
            if(m.page==Page::Haptics&&HapticCategory(key)>=0){
                const auto category=haptics::Category(HapticCategory(key));
                for(int choice=0;choice<haptics::PreviewCount(category);++choice)
                    button(TestHapticsBox(row,choice,category),haptics::PreviewLabel(category,choice),{Kind::TestHaptics,HapticCategory(key),choice+1},false,m.CanTestHaptics(HapticCategory(key)));
            }
            char label[96];
            if(key==Foveation){const char* modes[]={"Off","Fixed","Eye tracked"};std::snprintf(label,sizeof label,"%s",modes[int(value)]);}
            else if(key==VitalsLocation||key==NavigationLocation)std::snprintf(label,sizeof label,"%s",vitals::LocationNames[int(value)]);
            else if(key==RenderResolution)std::snprintf(label,sizeof label,"%s",resolution::Choices[int(value)]);
            else if(key==MenuDistance)std::snprintf(label,sizeof label,"%s",MenuDistanceNames[int(value)]);
            else if(key==TooltipTiming)std::snprintf(label,sizeof label,"%s",TooltipTimingNames[int(value)]);
            else if(key==Dominant)std::snprintf(label,sizeof label,"%s",value>.5f?"Left":"Right");
            else if(key==MeleeRules)std::snprintf(label,sizeof label,"%s",MeleeRulesNames[value>.5f?1:0]);
            else if(d.max==1)std::snprintf(label,sizeof label,"%s",value>.5f?"On":"Off");
            else if(key==Snap)std::snprintf(label,sizeof label,"%.0f deg",value);
            else if(key==VitalsTilt)std::snprintf(label,sizeof label,"%+.6g deg",value);
            else if(key==TurnSpeed)std::snprintf(label,sizeof label,"%.0f deg/s",value);
            else if(key==MeleeHitSpeed||key==MeleeThrustSpeed)std::snprintf(label,sizeof label,"%.2f m/s",value);
            else if(HapticKey(key)){if(value==0)std::snprintf(label,sizeof label,"Off (0%%)");else std::snprintf(label,sizeof label,"%.0f%%",value);}
            else if(key==LootLabelSize)std::snprintf(label,sizeof label,"%g%%",value);
            else if(key==Waist||key==BeltForward)std::snprintf(label,sizeof label,"%.1f cm",value*100);
            else std::snprintf(label,sizeof label,"%.0f",value);
            const bool enabled=m.ready&&!m.Busy();
            button(Decrease(row),"-",{Kind::Change,key,-1},false,enabled);
            button(Increase(row),"+",{Kind::Change,key,1},false,enabled);
            button(ValueBox(row),label,{d.max==1?Kind::Change:Kind::None,key,1},false,enabled);
        }
        if(m.page==Page::Video){
            text("Applying resolution changes rebuilds the VR view. Desktop restores the game's window size.",{40,716,1200,65},false,false,quiet);
            text("Headset uses the runtime recommendation, capped at 4096 per side. DLSS: game Options.",{40,795,1200,65},false,false,quiet);
        }
        if(m.page==Page::HUD){
            text("Palm: raise your left hand, palm up; release trigger and grip. Minimap & Compass move together.",{58,804,1160,30},false,false,quiet);
            text("Loot size: 20-150%, in steps of 5. Apply saves; Close discards edits.",{58,840,1160,30},false,false,quiet);
        }
        if(m.page==Page::Combat){
            text("Unleashed: each deliberate stroke can hit multiple creatures. Classic: attack rating, defense and block decide each hit; attack speed paces them and misses spend the attack.",{58,450,1160,76},false,false,quiet);
            text("Swings: 12.00 m/s by default and 18 cm of travel. Thrusts: 2.00 m/s by default and 12 cm toward the weapon tip. Pull back before stabbing again.",{58,550,1160,76},false,false,quiet);
            text("Both speed settings apply in either mode. Apply updates them immediately. Shield bash, arrows and spells are unchanged.",{58,650,1160,76},false,false,quiet);
        }
        if(m.page==Page::Haptics){
            text("0% is Off; 100% is normal. Overall strength scales each category, up to the controller's maximum. Adjust in steps of 10%.",{58,750,1160,56},false,false,quiet);
            text("Tests match the action's hands and use displayed settings. Apply saves; Close discards edits. Alerts also need the HUD option.",{58,810,1160,56},false,false,quiet);
        }
    }
    const char* message=!m.ready?"Configuration unavailable; controls help still works.":m.result==Result::Saving?"Saving...":m.result==Result::Saved?"Saved. Changes take effect live.":m.result==Result::Failed?"Save failed. Your edits are still unsaved.":m.result==Result::Conflict?"Settings changed elsewhere. Current values reloaded.":m.Dirty()?"Unsaved changes. Apply saves; Close discards them.":"World remains live. Right B closes this panel.";
    text(message,{40,876,1200,38},false,false,m.Dirty()?RGB(241,207,139):quiet);
    button(Recenter,"Reset eye height",{Kind::Recenter});
    button(Calibrate,"Calibrate body",{Kind::Calibrate},false,m.ready&&!m.Busy()&&!m.Dirty());
    button(Close,"Close",{Kind::Close});
    button(Apply,"Apply",{Kind::Apply},true,m.ready&&m.Dirty()&&!m.Busy());
    if(m.page==Page::VR&&m.controllerMenu){
        for(int i=0;i<int(controllers::Choice::Count);++i)
            button(ControllerOption(i),controllers::Choices[i],{Kind::ControllerChoice,i},int(m.Get()[ControllerLabels])==i);
    }
    }
    GdiFlush();std::vector<uint8_t> rgba(Width*Height*4);const auto* p=static_cast<uint8_t*>(bits);
    for(int i=0;i<Width*Height;++i){rgba[4*i]=p[4*i+2];rgba[4*i+1]=p[4*i+1];rgba[4*i+2]=p[4*i];rgba[4*i+3]=255;}
    SelectObject(dc,oldFont);DeleteObject(normal);DeleteObject(bold);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return rgba;
}
}
