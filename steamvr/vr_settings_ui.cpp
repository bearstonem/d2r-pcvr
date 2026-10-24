#include <windows.h>
#include <cstdio>
#include <cstring>
#include "vr_settings_ui.h"
#include "release_notes.h"
#include "wide_text.h"
// Retain an exact summary fingerprint in the DLL for the release packager.
extern "C" __declspec(dllexport) const char* D2RPCVR_ReleaseNotesIdentity(){return framevr::releasenotes::ContentMarker;}
static_assert(framevr::releasenotes::ReleaseLines.size()<=size_t(framevr::settings::NotesMaxLines),"release_notes.py MAX_LINES and NotesMaxLines agree");
namespace framevr::settings {
static int HelpTextHeight(HDC dc,HFONT font,const char* value,int width) {
    const auto wide=framevr::WideText(value);
    const auto old=SelectObject(dc,font);RECT bounds{0,0,width,0};
    DrawTextW(dc,wide.c_str(),-1,&bounds,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
    SelectObject(dc,old);return int(bounds.bottom);
}
// The value shown for a setting (c: the character, for Skills go to the aimed monster).
static void ValueLabel(Key key,float value,const Context& c,char* label,size_t size) {
    if(key==SpellLock)std::snprintf(label,size,"%s",BowSkillClass(c)?SpellLockNames[int(value)&3]:LockSpells(value)?"On":"Off");
    else if(key==Foveation){const char* modes[]={"Off","Fixed","Eye tracked"};std::snprintf(label,size,"%s",modes[int(value)]);}
    else if(key==FoveationStrength)std::snprintf(label,size,"%s",FoveationStrengthNames[int(value)]);
    else if(key==StereoRendering)std::snprintf(label,size,"%s",StereoRenderingNames[std::clamp(int(value),0,int(std::size(StereoRenderingNames))-1)]);
    else if(key==FrameGeneration)std::snprintf(label,size,"%s",framegen::ModeNames[std::clamp(int(value),0,3)]);
    else if(key==FrameGenPreset)std::snprintf(label,size,"%s",framegen::PresetNames[std::clamp(int(value),0,2)]);
    else if(key==FrameGenScale)std::snprintf(label,size,"%s",framegen::ScaleNames[std::clamp(int(value),0,2)]);
    else if(key==VitalsLocation||key==NavigationLocation)std::snprintf(label,size,"%s",vitals::LocationNames[int(value)]);
    else if(key==RenderResolution)std::snprintf(label,size,"%s",resolution::Choices[int(value)]);
    else if(key==MenuDistance)std::snprintf(label,size,"%s",MenuDistanceNames[int(value)]);
    else if(key==TooltipTiming)std::snprintf(label,size,"%s",TooltipTimingNames[int(value)]);
    else if(key==TargetHighlight){if(value==0)std::snprintf(label,size,"Off");else std::snprintf(label,size,"%.0f%%",value);}
    else if(key==AimHighlights)std::snprintf(label,size,"%s",HighlightsNames[value>.5f?1:0]);
    else if(key==GroundAim)std::snprintf(label,size,"%s",GroundAimNames[std::clamp(int(value),0,2)]);
    else if(HighlightKey(key)){   // its hand, on a shared row
        const char* hand=key==HighlightLeft?"L":"R";
        if(value==0)std::snprintf(label,size,"%s By range",hand);else std::snprintf(label,size,"%s %s %.0f\xc2\xb0",hand,HueName(value),value);
    }
    else if(key==BowLaser){if(value<0)std::snprintf(label,size,"Off");else std::snprintf(label,size,"%s %.0f\xc2\xb0",HueName(value),value);}
    else if(key==ThrowLift){if(value==0)std::snprintf(label,size,"Off");else std::snprintf(label,size,"%+.0f\xc2\xb0",value);}
    else if(key==Dominant)std::snprintf(label,size,"%s",value>.5f?"Left":"Right");
    else if(key==OfflinePlayers){if(value==0)std::snprintf(label,size,"Game default");else std::snprintf(label,size,"Players %.0f",value);}
    else if(key==HeartbeatStart){if(value==0)std::snprintf(label,size,"Off");else std::snprintf(label,size,"%.0f%% life",value);}
    else if(key==EnemySight){if(value==100)std::snprintf(label,size,"100%% (Default)");else std::snprintf(label,size,"%.0f%%",value);}
    else if(key==CompanionAi)std::snprintf(label,size,"%s",value>.5f?"Enabled":"Legacy");
    else if(Definitions[key].max==1)std::snprintf(label,size,"%s",value>.5f?"On":"Off");
    else if(key==Snap)std::snprintf(label,size,"%.0f deg",value);
    else if(key==VitalsTilt)std::snprintf(label,size,"%+.6g deg",value);
    else if(key==TurnSpeed)std::snprintf(label,size,"%.0f deg/s",value);
    else if(key==MeleeHitSpeed||key==MeleeThrustSpeed||key==ShieldBashSpeed)std::snprintf(label,size,"%.2f m/s",value);
    else if(HapticKey(key)){if(value==0)std::snprintf(label,size,"Off (0%%)");else std::snprintf(label,size,"%.0f%%",value);}
    else if(key==LootLabelSize)std::snprintf(label,size,"%g%%",value);
    else if(key==ShieldGrip||key==ShieldOut||key==ShieldUp)std::snprintf(label,size,"%+.0f cm",value);
    else if(key==ShieldFollow){if(value==0)std::snprintf(label,size,"0%% Forearm");else if(value==100)std::snprintf(label,size,"100%% Wrist");else std::snprintf(label,size,"%.0f%%",value);}
    else if(key==ShieldRoll)std::snprintf(label,size,"%+.0f\xc2\xb0",value);
    else if(key==Waist||key==BeltForward)std::snprintf(label,size,"%.1f cm",value*100);
    else std::snprintf(label,size,"%.0f",value);
}
int ReleaseNotesPages(){return NotesPages(int(releasenotes::ReleaseLines.size()));}
std::vector<uint8_t> Paint(const Model& m,Hit hover,const char* runtimeStatus,const calibration::Flow* body,const controllers::Labels& detected,int help,GameVideo game,bool monoAudio,
                           const PerformanceReadout& performance,const framegen::Readout& frameGen){
    const bool showHelp=!body&&help!=NoHelp&&HelpText(help)[0];
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=AtlasWidth;
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
        const auto wide=framevr::WideText(value);
        SelectObject(dc,strong?bold:normal);SetTextColor(dc,color);RECT rect{r.x,r.y,r.x+r.w,r.y+r.h};
        DrawTextW(dc,wide.c_str(),-1,&rect,DT_WORDBREAK|DT_NOPREFIX|(center?DT_CENTER:DT_LEFT));
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
    if(monoAudio)text(MonoAudioWarning,{820,44,420,42},false,true,RGB(236,186,96));   // pointing at it explains
    else text("Point for help; trigger to change",{820,44,420,42},false,true,quiet);
    for(int i=0;i<TabCount;++i)button(TabBox(i),TabNames[i],{Kind::Tab,i},int(m.page)==i);
    if(m.page==Page::ReleaseNotes){
        text(releasenotes::ReleaseTitle,{40,195,1200,45},true);
        text(releasenotes::Author,{40,241,1200,35});
        text(releasenotes::ProjectUrl,{40,281,1200,35},false,false,RGB(116,220,219));
        const int lines=int(releasenotes::ReleaseLines.size()),pages=NotesPages(lines),shown=std::clamp(m.notesPage,0,pages-1);
        int y=330;
        for(int i=shown*NotesPerPage;i<std::min(lines,(shown+1)*NotesPerPage);++i) {
            fill({40,y-3,1200,35},(y/36)%2?RGB(19,31,39):RGB(22,36,44));
            text(releasenotes::ReleaseLines[size_t(i)],{54,y,1170,35});y+=36;
        }
        if(pages>1) {   // a long release: Previous / Next under the lines
            button(NotesPrevBox,"Previous",{Kind::NotesPage,0,-1},false,shown>0);
            button(NotesNextBox,"Next",{Kind::NotesPage,0,1},false,shown<pages-1);
            char label[48];std::snprintf(label,sizeof label,"Page %d of %d",shown+1,pages);
            text(label,{NotesPageBox.x,NotesPageBox.y+6,NotesPageBox.w,34},false,true,quiet);
        }
        char credits[160];std::snprintf(credits,sizeof credits,"Credits: %s",releasenotes::Credits);
        text(credits,{40,848,1200,28},false,false,quiet);
    }else if(m.page==Page::Controls){
        text("Each stick opens its hand's wheel. Use the OTHER hand's laser to assign skills.",{40,180,1200,32},false,false,RGB(116,220,219));
        text("LEFT HAND",{58,215,520,40},true);text("RIGHT HAND",{676,215,520,40},true);
        struct Help{const char* key;const char* action;};
        const auto leftFaces=labels.Button(1,1)+" / "+labels.Button(1,2);
        const auto leftAlternates=labels.Button(1,3)+" / "+labels.Button(1,4);
        const auto rightFaces=labels.Button(0,1)+" / "+labels.Button(0,2)+" / grip + face";
        const Help left[]={{"Stick","Walk"},{leftFaces.c_str(),"Assigned left-hand skills"},{leftAlternates.c_str(),"Alternate left-hand skills"},{"Trigger","Assigned left-hand skill"},{"Stick click","Toggle left wheel; right laser edits"},{"Grip + stick click","Inventory / character / skills menu"},{"Grip near belt","Grab a potion"}};
        const Help right[]={{"Stick sideways / click","Turn / toggle right wheel"},{"Trigger","Assigned right skill / grab bow string"},{"Stick up / hold down","Run or walk / loot labels; zooms a looked-at map"},{rightFaces.c_str(),"Interact by default / assigned skills"},{"Dominant grip at shoulder","Either shoulder: swap weapon sets I / II"},{"Trigger with bow / javelin","Draw & release / throw in WEAPON mode"},{"Left Grip + Right Trigger","Laser context click in game panels"}};
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
        const auto panelHelp="Panels: Left Trigger = Ctrl; Right Grip = Shift / push-pull; Right "+labels.Button(0,2)+" = Back.";
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
        }else if(m.page==Page::HUD){
            text("Map view",{58,193,210,36},true);
            text("Rotate map",{675,193,250,36},true);
            const bool enabled=m.ready&&!m.Busy();
            button(HudMapViewBox,MapViewNames[std::clamp(int(m.Get()[MapTopDown]),0,3)],{Kind::Change,MapTopDown,1},false,enabled);
            button(HudMapRotateBox,m.Get()[MapRotate]>.5f?"On":"Off",{Kind::Change,MapRotate,1},false,enabled);
        }else if(m.page==Page::Performance&&(DeferOffered(m.Get())||GameVideoWarning(game))){
            // Warnings in amber under the status, two lines at most: the GPU
            // wait not deferred (and the button that turns it on), then the
            // game's own video options; both push the status out.
            const bool defer=DeferOffered(m.Get());const char* gameWarning=GameVideoWarning(game);
            const COLORREF warn=RGB(236,186,96);const int width=defer?DeferBox.x-50:1200;
            text(defer&&gameWarning?DeferWarning:runtimeStatus,{40,181,width,29},false,false,defer&&gameWarning?warn:quiet);
            text(gameWarning?gameWarning:DeferWarning,{40,209,width,29},false,false,warn);
            if(defer)button(DeferBox,"Defer GPU wait",{Kind::Change,DeferGpuWait,1},false,m.ready&&!m.Busy());
        }else text(m.page==Page::Haptics?"Tune vibration for your headset and controllers.":m.page==Page::Combat?"Tune physical combat and offline difficulty.":m.page==Page::Aim?"Choose how your skills find their target and what you see of it. Rows for other classes stay hidden.":
             m.page==Page::Shield?"Seat a held shield on your forearm.":m.page==Page::Video?"The mod's added effects. What changes the frame rate is on Perf.":runtimeStatus,{40,197,1200,42},false,false,quiet);
        const RowList rows=m.Rows();
        for(int row=0;row<rows.count;++row){
            const Key key=rows[row];const int y=RowY+row*RowStep;
            fill({40,y-4,1200,RowStep},row%2?RGB(19,31,39):RGB(22,36,44));
            const bool paired=Paired(key),applies=RowEnabled(m.Get(),key);
            const int textWidth=m.page==Page::Haptics?520:paired?PairLeft-70:780;
            text(RowLabel(key,m.context),{58,y+1,textWidth,29},true,false,applies?RGB(229,234,235):quiet);text(RowDetail(key),{58,y+30,textWidth,28},false,false,quiet);
            if(m.page==Page::Haptics&&HapticCategory(key)>=0){
                const auto category=haptics::Category(HapticCategory(key));
                for(int choice=0;choice<haptics::PreviewCount(category);++choice)
                    button(TestHapticsBox(row,choice,category),haptics::PreviewLabel(category,choice),{Kind::TestHaptics,HapticCategory(key),choice+1},false,m.CanTestHaptics(HapticCategory(key)));
            }
            const bool enabled=m.ready&&!m.Busy()&&applies;
            char label[96];
            for(int side=0;side<(paired?2:1);++side){   // a paired row: a stepper each, left and right
                const Key k=side?PairOf(key):key;
                ValueLabel(k,m.Get()[k],m.context,label,sizeof label);
                button(paired?PairDecrease(row,side):Decrease(row),"-",{Kind::Change,k,-1},false,enabled);
                button(paired?PairIncrease(row,side):Increase(row),"+",{Kind::Change,k,1},false,enabled);
                button(paired?PairValue(row,side):ValueBox(row),label,{ValueSteps(k)?Kind::Change:Kind::None,k,1},false,enabled);
            }
        }
        if(m.page==Page::HUD){
            text("Palm: raise left hand palm up; release grip/trigger. Status follows the minimap.",{58,854,1160,22},false,false,quiet);
        }
        if(m.page==Page::Performance){
            // The last second's frames, whether the CPU's part fits the
            // headset's frame and the game's own options.
            const COLORREF teal=RGB(116,220,219),warn=RGB(236,186,96);
            const auto line=PerformanceLine(performance),verdict=PerformanceVerdict(performance);
            text(line.c_str(),{58,PerformanceBox.y+2,1170,30},false,false,performance.valid?teal:quiet);
            if(!verdict.empty())text(verdict.c_str(),{58,PerformanceBox.y+33,1170,29},false,false,performance.CpuMs()>performance.budgetMs?warn:quiet);
            const auto gameLine=GameVideoLine(game);
            text(gameLine.empty()?GameVideoUnsaved:gameLine.c_str(),{58,GameVideoBox.y+2,1170,29},false,false,quiet);
            const auto frameGenLine=framegen::Line(frameGen);
            text(frameGenLine.c_str(),{58,FrameGenBox.y+2,1170,29},false,false,
                 framegen::Warning(frameGen)?warn:frameGen.state==framegen::State::Active?teal:quiet);
        }
        if(m.page==Page::Combat){
            // The ninth row ends at 800; without the offline adapter its notice replaces the difficulty note.
            if(m.offlineDifficultyAvailable)
                text("Difficulty affects new monsters only; existing monsters keep their stats. To apply it throughout the game, Save and Exit, then start a new game with the same character.",{58,804,1160,56},false,false,quiet);
            else text("Offline scaling adapter unavailable. Game difficulty remains unchanged.",{58,804,1160,30},false,false,quiet);
        }
        if(m.page==Page::Haptics){
            text("0% is Off; 100% is normal. Overall strength scales each category, up to the controller's maximum. Adjust in steps of 10%.",{58,750,1160,56},false,false,quiet);
            text("Tests match the action's hands and use displayed settings. Apply saves; Close discards edits. Alerts also need the HUD option.",{58,810,1160,56},false,false,quiet);
        }
    }
    const char* message=!m.ready?"Configuration unavailable; controls help still works.":m.result==Result::Saving?"Saving...":m.result==Result::Saved?"Saved.":m.result==Result::Failed?"Save failed. Your edits are still unsaved.":m.result==Result::Conflict?"Settings changed elsewhere. Current values reloaded.":m.Dirty()?"Unsaved changes. Apply saves; Close discards them.":"World remains live. Right B closes this panel.";
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
    if(showHelp){
        fill(HelpCard,RGB(22,36,44));
        fill({HelpCard.x,HelpCard.y,4,HelpCard.h},RGB(116,220,219));
        text(HelpTitle(help),{HelpCard.x+24,HelpCard.y+24,HelpCard.w-48,92},true);
        const int left=HelpCard.x+24,width=HelpCard.w-48,bottom=HelpCard.y+HelpCard.h-24;
        int y=HelpCard.y+128;
        text(HelpText(help),{left,y,width,bottom-y});
        if(const auto choice=HelpChoice(help,help<Count?m.Get()[help]:NAN);choice.name) {
            y+=HelpTextHeight(dc,normal,HelpText(help),width)+HelpChoiceGap;
            text(choice.name,{left,y,width,HelpChoiceNameHeight},true,false,RGB(116,220,219));
            y+=HelpChoiceNameHeight;
            text(choice.text,{left,y,width,bottom-y});
        }
    }
    GdiFlush();std::vector<uint8_t> rgba(AtlasWidth*Height*4);const auto* p=static_cast<uint8_t*>(bits);
    for(int y=0;y<Height;++y)for(int x=0;x<AtlasWidth;++x){
        const int i=y*AtlasWidth+x;
        if(x>=Width&&!(showHelp&&HelpCard.Contains(float(x),float(y))))continue;
        rgba[4*i]=p[4*i+2];rgba[4*i+1]=p[4*i+1];rgba[4*i+2]=p[4*i];rgba[4*i+3]=255;
    }
    SelectObject(dc,oldFont);DeleteObject(normal);DeleteObject(bold);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return rgba;
}
}
