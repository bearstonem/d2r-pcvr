#include "vr_settings_help.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace framevr::settings;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static Hit At(Page p,Rect r){return HitTest(p,(r.x+r.w*.5f)/Width,(r.y+r.h*.5f)/Height);}
int main(){
    {
        Snapshot state;state.ready=true;Model model;model.Open(state);
        for(int page=0;page<TabCount;++page){
            const auto box=TabBox(page);
            const auto hit=HitTest(model.page,(box.x+box.w/2.f)/Width,(box.y+box.h/2.f)/Height);
            Check(hit.kind==Kind::Tab&&hit.index==page,"ordinary settings tabs match visible bounds");model.Click(hit);
        }
        Check(model.page==Page::ReleaseNotes&&!model.Dirty(),"release notes open in regular play without editing settings");
        Check(HitTest(Page::ReleaseNotes,.5f,.5f).kind==Kind::None,"release lines are read-only");
        model.Click({Kind::Tab,TabCount});Check(model.page==Page::ReleaseNotes,"no hidden developer settings tab");
        state.ready=false;model.Open(state);model.Click(At(Page::Controls,TabBox(int(Page::ReleaseNotes))));
        Check(model.page==Page::ReleaseNotes&&!model.Dirty(),"release notes remain readable without configuration");
    }
    {
        HoverHelp help;
        Check(help.Step(MeleeRules,100)==NoHelp&&help.Step(MeleeRules,549)==NoHelp,"passing over a setting does not flash help");
        Check(help.Step(MeleeRules,550)==MeleeRules,"steady hover reveals help after 450 ms");
        Check(help.Step(MeleeHitSpeed,600)==NoHelp&&help.Step(MeleeHitSpeed,1050)==MeleeHitSpeed,"changing settings restarts dwell and clears old help");
        Check(help.Step(NoHelp,1100)==NoHelp&&help.Step(MeleeHitSpeed,1600)==NoHelp,"leaving panel or losing tracking clears dwell");
        help.Clear();Check(help.Step(MeleeHitSpeed,3000)==NoHelp,"close or focus loss cannot retain old dwell");
        Check(HelpAt(Page::Combat,NAN,.5f)==NoHelp&&HelpAt(Page::Combat,1,.5f)==NoHelp,"invalid help coordinates ignored");
        for(int p=0;p<TabCount;++p)for(int row=0;row<RowCount(Page(p));++row){
            const float v=float(RowY+row*RowStep+20)/Height;
            const auto key=RowKey(Page(p),row);
            Check(HelpAt(Page(p),100.f/Width,v)==key&&HelpAt(Page(p),1100.f/Width,v)==key,"labels and values explain the same setting");
            Check(HelpText(key)[0]&&HelpTitle(key)[0],"every visible row has help");
            Check(HitTest(Page(p),100.f/Width,v).kind==Kind::None,"hovering or clicking a label does not edit settings");
        }
        Check(HelpAt(Page::VR,900.f/Width,250.f/Height,true)==NoHelp,"dropdown blocks help for covered settings");
        Check(HelpAt(Page::Controls,100.f/Width,750.f/Height)==PlanarSpellAim,"controls page label has help");
        Check(HelpAt(Page::HUD,1000.f/Width,950.f/Height)==HelpApply,"footer buttons have help");
        Check(HelpCard.x>=Width&&HelpCard.y+HelpCard.h<Apply.y,"help never covers controls or footer");
    }
    Snapshot s;s.ready=true;
    {
        Check(s.values[LootLabelSize]==45,"loot labels retain original shipped size by default");
        Check(Normalize(LootLabelSize,0)==20&&Normalize(LootLabelSize,200)==150&&
              Normalize(LootLabelSize,NAN)==45&&Normalize(LootLabelSize,47.5f)==47.5f,"loot scale bounds, invalid default and custom values");
        Model loot;loot.Open(s);loot.Click({Kind::Tab,int(Page::HUD)});
        Check(RowKey(Page::HUD,8)==LootLabelSize,"HUD exposes loot sizing");
        loot.Click(At(Page::HUD,Increase(8)));Check(loot.Get()[LootLabelSize]==50,"loot size increases by five percentage points");
        auto request=loot.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<LootLabelSize)&&request.values[LootLabelSize]==50,"loot Apply uses its own high mask bit");
        loot.Open(s);loot.Click(At(Page::HUD,Decrease(8)));Check(loot.Get()[LootLabelSize]==40,"loot size decreases by five percentage points");
        loot.Click({Kind::Close});loot.Open(s);Check(loot.Get()[LootLabelSize]==45&&!loot.Dirty(),"Close discards unsaved loot size");
    }
    for(int i=HapticStrength;i<=HapticAlert;++i){
        const auto key=Key(i);const int row=i-HapticStrength+(i>=HapticBowDraw?1:0);
        Check(s.values[key]==100,"haptics default to normal strength");
        Check(Normalize(key,-1)==0&&Normalize(key,250)==200&&Normalize(key,54)==50&&Normalize(key,NAN)==100,"haptic settings clamp, step and reject nonfinite values");
        Model h;h.Open(s);h.Click({Kind::Tab,int(Page::Haptics)});
        Check(RowKey(Page::Haptics,row)==key&&At(Page::Haptics,Increase(row)).index==key,"each haptic row targets its own setting");
        h.Click(At(Page::Haptics,Decrease(row)));Check(h.Get()[key]==90,"strength moves in ten-percent steps");
        if(row){
            const auto category=framevr::haptics::Category(i-HapticMelee);
            for(int choice=0;choice<framevr::haptics::PreviewCount(category);++choice){
                const auto box=TestHapticsBox(row,choice,category);
                const auto test=At(Page::Haptics,box);
                Check(test.kind==Kind::TestHaptics&&test.index==i-HapticMelee&&test.delta==choice+1&&h.CanTestHaptics(test.index),"category Test selects its action preview with unsaved settings");
                if(framevr::haptics::PreviewCount(category)==2){
                    const auto other=TestHapticsBox(row,choice^1,category);
                    Check(!other.Contains(box.x+box.w*.5f,box.y+box.h*.5f),"action preview buttons do not overlap");
                }else Check(HitTest(Page::Haptics,620.f/Width,(box.y+box.h*.5f)/Height).kind==Kind::None,"single-preview row has no invisible second test");
                Check(box.x+box.w<Decrease(row).x,"tests do not overlap strength controls");
                Check(!h.Click(test).sequence&&h.Dirty()&&h.Get()[key]==90,"Test does not persist draft changes");
            }
        }
        const auto request=h.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<key)&&request.values[key]==90&&!h.CanTestHaptics(0),"Apply saves only selected haptic strength and blocks preview while saving");
        h.Close();h.Open(s);Check(h.Get()[key]==100&&!h.CanTestHaptics(0),"close discards edits and Controls page has no preview");
    }
    Check(!Model{}.CanTestHaptics(0),"closed menu cannot preview haptics");
    {Model h;h.Open(s);h.Click({Kind::Tab,int(Page::Haptics)});
        const auto toggle=At(Page::Haptics,ValueBox(2));
        Check(s.values[HapticShortMelee]==0&&toggle.kind==Kind::Change&&toggle.index==HapticShortMelee,"default longer melee pulse has its own toggle");
        const auto normal=HapticSample(h.Get(),framevr::haptics::Category::Melee);
        Check(normal.milliseconds==90&&normal.frequency==0&&HapticSample(h.Get(),framevr::haptics::Category::Melee,1).milliseconds==45,"weapon impact lasts longer through a swing while shield keeps its existing tap");
        for(int choice=0;choice<2;++choice)Check(At(Page::Haptics,TestHapticsBox(2,choice,framevr::haptics::Category::Melee)).kind==Kind::None,"toggle row does not steal a category Test button");
        h.Click(toggle);const auto sharp=HapticSample(h.Get(),framevr::haptics::Category::Melee);
        Check(sharp.milliseconds==10&&sharp.frequency==180&&sharp.amplitude==normal.amplitude,"draft test immediately uses short pulse at the same strength");
        const auto request=h.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<HapticShortMelee)&&request.values[HapticMelee]==100,"pulse shape saves without changing strength");
        h.Close();h.Open(s);Check(HapticSample(h.Get(),framevr::haptics::Category::Melee).milliseconds==90,"closing discards unsaved short-pulse choice");
    }
    {auto missing=s;missing.ready=false;Model h;h.Open(missing);h.Click({Kind::Tab,int(Page::Haptics)});
        Check(!h.CanTestHaptics(0)&&!h.CanTestHaptics(-1)&&!h.CanTestHaptics(framevr::haptics::CategoryCount),"unavailable settings cannot preview");}
    {auto values=s.values;values[HapticStrength]=50;
        for(int i=0;i<framevr::haptics::CategoryCount;++i)values[HapticMelee+i]=float(i*10);
        const auto levels=HapticLevels(values);Check(levels.master==50,"master level reaches runtime configuration");
        for(int i=0;i<framevr::haptics::CategoryCount;++i)Check(levels.category[i]==i*10,"each saved category maps to its runtime channel");}
    Check(s.values[MeleeHitSpeed]==12,"melee defaults to twelve m/s");
    {Model melee;melee.Open(s);melee.Click({Kind::Tab,int(Page::Combat)});
        const auto hit=At(Page::Combat,Increase(1));
        Check(hit.index==MeleeHitSpeed&&hit.kind==Kind::Change,"combat speed control hit target");
        melee.Click(hit);Check(melee.Get()[MeleeHitSpeed]==12.25f,"melee speed quarter step");
        const auto request=melee.Click({Kind::Apply});
        Check(request.changed==(1u<<MeleeHitSpeed),"melee mask bit saves only melee speed");
        Check(Normalize(MeleeHitSpeed,NAN)==12&&Normalize(MeleeHitSpeed,0)==1.5f&&Normalize(MeleeHitSpeed,99)==99,"melee speed UI has no upper cap");
        s.values[MeleeHitSpeed]=6;melee.Open(s);melee.Click(hit);
        Check(melee.Get()[MeleeHitSpeed]==6.25f,"plus button goes beyond old cap");
        s.values[MeleeHitSpeed]=12;
    }
    {Model rules;rules.Open(s);rules.Click({Kind::Tab,int(Page::Combat)});
        Check(s.values[MeleeRules]==0&&RowKey(Page::Combat,0)==MeleeRules,"melee rules default to Unleashed, first on the Combat page");
        rules.Click(At(Page::Combat,ValueBox(0)));
        Check(rules.Get()[MeleeRules]==1,"clicking the value switches to Classic");
        const auto request=rules.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<MeleeRules),"melee rules save only their own setting");
    }
    {Model thrust;thrust.Open(s);thrust.Click({Kind::Tab,int(Page::Combat)});
        const auto hit=At(Page::Combat,Increase(2));
        Check(s.values[MeleeThrustSpeed]==2&&hit.index==MeleeThrustSpeed&&hit.kind==Kind::Change,"thrust has its own Combat row and two m/s default");
        thrust.Click(hit);Check(thrust.Get()[MeleeThrustSpeed]==2.25f,"thrust speed adjusts in quarter steps");
        const auto request=thrust.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<MeleeThrustSpeed),"thrust edit does not change swing speed or combat rules");
        Check(RowCount(Page::Combat)==7&&RowKey(Page::Combat,3)==ShieldBashSpeed,"seven Combat rows retain independent bash speed; parry window is fixed");
        Check(Normalize(MeleeThrustSpeed,NAN)==2&&Normalize(MeleeThrustSpeed,0)==.75f&&Normalize(MeleeThrustSpeed,99)==10,"thrust speed has finite usable bounds");
    }
    {Model strain;strain.Open(s);strain.Click({Kind::Tab,int(Page::Combat)});
        Check(RowKey(Page::Combat,4)==StrainDark&&s.values[StrainDark]==0,"guard strain colour is the fifth Combat row, White by default");
        strain.Click(At(Page::Combat,ValueBox(4)));
        Check(strain.Get()[StrainDark]==1,"clicking the value switches the strain to Black");
        const auto request=strain.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<StrainDark),"strain colour saves only its own setting");
    }
    {Model speed;speed.Open(s);speed.Click({Kind::Tab,int(Page::Combat)});
        Check(s.values[StrainSpeed]==1&&RowKey(Page::Combat,5)==StrainSpeed,"guard strain speed defaults to Medium in its own row");
        for(float bad:{-1.f,3.f,1.5f,NAN})Check(Normalize(StrainSpeed,bad)==1,"invalid buildup preference uses Medium");
        speed.Click(At(Page::Combat,Decrease(5)));Check(speed.Get()[StrainSpeed]==0,"minus selects Slow");
        speed.Click(At(Page::Combat,Decrease(5)));Check(speed.Get()[StrainSpeed]==0,"Slow is the lower bound");
        speed.Click(At(Page::Combat,Increase(5)));speed.Click(At(Page::Combat,Increase(5)));
        Check(speed.Get()[StrainSpeed]==2,"plus selects Fast");
        speed.Click(At(Page::Combat,Increase(5)));Check(speed.Get()[StrainSpeed]==2,"Fast is the upper bound");
        Check(speed.Click({Kind::Apply}).changed==(ChangeMask{1}<<StrainSpeed),"buildup edits preserve combat rules, difficulty and colour");
    }
    {Model difficulty;difficulty.Open(s);difficulty.Click({Kind::Tab,int(Page::Combat)});
        Check(s.values[OfflinePlayers]==0&&RowKey(Page::Combat,6)==OfflinePlayers,"difficulty preserves native settings by default");
        Check(ValueBox(6).y+ValueBox(6).h<698,"difficulty avoids Combat notes");
        Check(Normalize(OfflinePlayers,NAN)==0&&Normalize(OfflinePlayers,9)==0&&Normalize(OfflinePlayers,2.5f)==0,"invalid offline counts fail back to native");
        for(int i=1;i<=8;++i){difficulty.Click(At(Page::Combat,Increase(6)));Check(difficulty.Get()[OfflinePlayers]==i,"increase difficulty through every native count");}
        difficulty.Click(At(Page::Combat,Increase(6)));Check(difficulty.Get()[OfflinePlayers]==8,"upper bound does not wrap");
        const auto request=difficulty.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<OfflinePlayers),"difficulty does not alter melee rules or speeds");
        auto saved=s;saved.sequence=request.sequence;saved.result=Result::Saved;saved.values=request.values;
        difficulty.Observe(saved);
        for(int i=7;i>=0;--i){difficulty.Click(At(Page::Combat,Decrease(6)));Check(difficulty.Get()[OfflinePlayers]==i,"decrease through native default");}
        difficulty.Click(At(Page::Combat,Decrease(6)));Check(difficulty.Get()[OfflinePlayers]==0,"native default is lower bound");
        const auto draft=difficulty.Get();saved.offlineDifficultyAvailable=true;difficulty.Observe(saved);
        Check(difficulty.offlineDifficultyAvailable&&difficulty.Get()==draft,"capability updates preserve unsaved edits");
    }
    {Model bash;bash.Open(s);bash.Click({Kind::Tab,int(Page::Combat)});
        Check(s.values[ShieldBashSpeed]==2&&At(Page::Combat,Increase(3)).index==ShieldBashSpeed,"shield bash has its own speed row");
        bash.Click(At(Page::Combat,Increase(3)));
        Check(bash.Get()[ShieldBashSpeed]==2.25f&&bash.Click({Kind::Apply}).changed==(ChangeMask{1}<<ShieldBashSpeed),"bash speed edits only its own setting");
        Check(Normalize(ShieldBashSpeed,NAN)==2&&Normalize(ShieldBashSpeed,0)==.75f&&Normalize(ShieldBashSpeed,99)==10,"bash speed UI matches contact bounds");
    }
    Check(s.values[MenuDistance]==1&&s.values[TooltipTiming]==0,"comfortable Medium distance and Instant hover defaults");
    for(auto key:{MenuDistance,TooltipTiming}) {
        Check(Normalize(key,1.5f)==Definitions[key].defaultValue&&Normalize(key,9)==Definitions[key].defaultValue,"invalid comfort enums fall back safely");
        const int row=key==MenuDistance?4:5;
        Check(RowKey(Page::HUD,row)==key&&At(Page::HUD,Increase(row)).index==key,"HUD comfort rows hit their own settings");
        Model comfort;comfort.Open(s);comfort.Click(At(Page::HUD,Increase(row)));
        Check(comfort.Click({Kind::Apply}).changed==(1u<<key),"comfort Apply affects only its selected setting");
    }
    Check(s.values[ProjectileAimAssist]==2,"aim assist defaults to Strong");
    for(float bad:{-1.f,3.f,1.5f,NAN})Check(Normalize(ProjectileAimAssist,bad)==2,"invalid aim-assist preference falls back to Strong");
    {Model assist;assist.Open(s);const auto hit=At(Page::Controls,AimAssistBox);
        Check(hit.kind==Kind::Change&&hit.index==ProjectileAimAssist,"controls tab exposes aim assist");
        for(float choice:{0.f,1.f,2.f,0.f,1.f}){assist.Click(hit);Check(assist.Get()[ProjectileAimAssist]==choice,"aim assist cycles Off Light Strong");}
        auto request=assist.Click({Kind::Apply});
        Check(request.changed==(1u<<ProjectileAimAssist)&&request.values[ProjectileAimAssist]==1,"assist saves independently");
        assist.Click(hit);Check(assist.Get()[ProjectileAimAssist]==1,"assist cannot change while saving");
        assist.Close();assist.Open(s);Check(assist.Get()[ProjectileAimAssist]==2,"discarding edits restores saved assistance");
        Check(AimAssistBox.y>=PlanarSpellAimBox.y+PlanarSpellAimBox.h&&AimAssistBox.y+AimAssistBox.h<845,"assist avoids planar control and footer");}
    Check(s.values[OrdinaryMobBars]==0,"ordinary mob bars are off by default");
    {Model bars;bars.Open(s);bars.Click({Kind::Tab,int(Page::VR)});
        const auto hit=At(Page::VR,ValueBox(9));
        Check(hit.kind==Kind::Change&&hit.index==OrdinaryMobBars,"VR tab exposes ordinary mob toggle");
        Check(RowY+9*RowStep+72<876,"last row leaves room for save status");
        bars.Click(hit);auto request=bars.Click({Kind::Apply});
        Check(request.changed==(1u<<OrdinaryMobBars)&&request.values[OrdinaryMobBars]==1,"toggle saves only mob visibility");}
    Check(s.values[SmoothTurn]==0&&s.values[Snap]==30,"snap turning remains the default");
    Check(EffectiveSnapAngle(30,-1)==30&&EffectiveSnapAngle(0,-1)==0,"legacy snap and smooth profiles retain their mode");
    Check(EffectiveSnapAngle(45,1)==0&&EffectiveSnapAngle(45,0)==45,"explicit toggle selects camera mode without losing angle");
    Check(EffectiveSnapAngle(0,0)==30,"turning off legacy smooth mode restores default snap angle");
    Check(SnapDegrees(NAN)==30&&SnapDegrees(-20)==15&&SnapDegrees(999)==180,"camera and menu share snap bounds");
    {Model turning;turning.Open(s);
        turning.Click(At(Page::VR,ValueBox(3)));
        Check(turning.Get()[SmoothTurn]==1&&turning.Get()[Snap]==30,"value-box click enables smooth turning independently");
        turning.Click({Kind::Close});turning.Open(s);
        Check(turning.Get()[SmoothTurn]==0&&!turning.Dirty(),"closing without Apply discards smooth toggle");
        turning.Click(At(Page::VR,ValueBox(3)));
        auto request=turning.Click({Kind::Apply});
        Check(request.changed==(1u<<SmoothTurn),"Apply saves only the turning mode");
        turning.Click(At(Page::VR,ValueBox(3)));
        Check(turning.Get()[SmoothTurn]==1,"toggle cannot change while saving");
        auto saved=s;saved.values=request.values;saved.sequence=request.sequence;saved.result=Result::Saved;
        turning.Observe(saved);turning.Close();turning.Open(saved);
        turning.Click(At(Page::VR,ValueBox(3)));
        Check(turning.Get()[SmoothTurn]==0&&turning.Get()[Snap]==30,"reopened toggle restores snap turning");
    }
    Check(s.values[NavigationLocation]==0,"combined navigation defaults to Wrist");
    Check(Normalize(NavigationLocation,99)==0&&Normalize(NavigationLocation,1.5f)==0,"invalid navigation location falls back to Wrist");
    {Model navigation;navigation.Open(s);navigation.Click(At(Page::Controls,Tabs[int(Page::HUD)]));
        const int row=3;
        Check(RowCount(Page::HUD)==10&&RowKey(Page::HUD,row)==NavigationLocation,"one navigation row replaces separate map/compass switches");
        for(int location=1;location<=3;++location){
            navigation.Click(At(Page::HUD,Increase(row)));
            Check(navigation.Get()[NavigationLocation]==location,"navigation offers Palm, Either and Off");
        }
        auto request=navigation.Click({Kind::Apply});
        Check(request.changed==(1u<<NavigationLocation)&&request.values[NavigationLocation]==3&&request.values[VitalsLocation]==0,
            "combined navigation can turn off without changing vitals");}
    Check(s.values[VitalsLocation]==0&&s.values[CriticalAlerts]==0,"existing wrist placement remains default; automatic alerts require opt-in");
    Check(Normalize(VitalsLocation,99)==0&&Normalize(VitalsLocation,1.5f)==0,"unknown vitals locations fall back to wrist");
    {Model hud;hud.Open(s);hud.Click(At(hud.page,Tabs[int(Page::HUD)]));
        hud.Click(At(hud.page,Increase(0)));
        Check(hud.Get()[VitalsLocation]==1&&!hud.Get()[CriticalAlerts],"Palm selection does not enable automatic alerts");
        auto request=hud.Click({Kind::Apply});
        Check(request.changed==(1u<<VitalsLocation),"palm placement saves independently");
        hud.Open(s);hud.Click(At(Page::HUD,ValueBox(2)));request=hud.Click({Kind::Apply});
        Check(request.changed==(1u<<CriticalAlerts)&&request.values[CriticalAlerts]==1,"alert opt-in saves independently");}
    Check(s.values[BloodVisor]==1,"blood on visor defaults on");
    {Model blood;blood.Open(s);blood.Click({Kind::Tab,int(Page::HUD)});
        const auto hit=At(Page::HUD,ValueBox(6));
        Check(RowKey(Page::HUD,6)==BloodVisor&&hit.kind==Kind::Change&&hit.index==BloodVisor,"HUD tab exposes the blood toggle");
        blood.Click(hit);auto request=blood.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<BloodVisor)&&request.values[BloodVisor]==0,"blood toggle saves independently");}
    Check(s.values[BloodWipe]==1,"face wipe gesture defaults on");
    {Model wipe;wipe.Open(s);wipe.Click({Kind::Tab,int(Page::HUD)});
        const auto hit=At(Page::HUD,ValueBox(7));
        Check(RowKey(Page::HUD,7)==BloodWipe&&hit.kind==Kind::Change&&hit.index==BloodWipe,"face wipe toggle sits under the blood toggle");
        Check(RowY+7*RowStep+RowStep<=742,"eighth HUD row clears the help text");
        wipe.Click(hit);auto request=wipe.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<BloodWipe)&&request.values[BloodWipe]==0&&request.values[BloodVisor]==1,"wipe toggle saves without touching the blood toggle");}
    Check(s.values[VitalsTilt]==0,"vitals tilt keeps the original angle by default");
    Check(Normalize(VitalsTilt,NAN)==0&&Normalize(VitalsTilt,INFINITY)==0&&
        Normalize(VitalsTilt,-999)==-60&&Normalize(VitalsTilt,999)==60,"tilt rejects nonfinite input and clamps at sixty degrees");
    {Model tilt;tilt.Open(s);tilt.Click({Kind::Tab,int(Page::HUD)});
        Check(RowKey(Page::HUD,1)==VitalsTilt&&At(Page::HUD,Increase(1)).index==VitalsTilt&&
            At(Page::HUD,Decrease(1)).index==VitalsTilt,"tilt controls sit next to vitals placement");
        tilt.Click(At(Page::HUD,Increase(1)));Check(tilt.Get()[VitalsTilt]==5,"plus tilts five degrees");
        const auto request=tilt.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<VitalsTilt)&&request.changed>UINT32_MAX,
            "Apply preserves the new setting bit above the previous 32-bit mask");
        Check(request.values[VitalsLocation]==0&&request.values[NavigationLocation]==0&&request.values[CriticalAlerts]==0,
            "tilt saves independently of placement, navigation and alerts");
        auto external=s.values;external[VitalsTilt]=-5;Check(Conflict(external,request.base,request.changed),"high-bit tilt conflicts are detected");
        external=s.values;external[NavigationLocation]=1;Check(!Conflict(external,request.base,request.changed),"navigation edits do not conflict with tilt");
        tilt.Open(s);tilt.Click(At(Page::HUD,Decrease(1)));Check(tilt.Get()[VitalsTilt]==-5,"minus tilts in the opposite direction");
        tilt.Close();tilt.Open(s);Check(tilt.Get()[VitalsTilt]==0&&!tilt.Dirty(),"Close discards unapplied tilt");
        for(int i=0;i<30;++i)tilt.Click(At(Page::HUD,Increase(1)));
        Check(tilt.Get()[VitalsTilt]==60,"tilt upper limit clamps");
        for(int i=0;i<60;++i)tilt.Click(At(Page::HUD,Decrease(1)));
        Check(tilt.Get()[VitalsTilt]==-60,"tilt lower limit clamps");}
    Check(s.values[PlanarSpellAim]==0,"spell aiming defaults to free vertical aim");
    {Model aim;aim.Open(s);const auto hit=At(Page::Controls,PlanarSpellAimBox);
        Check(hit.kind==Kind::Change&&hit.index==PlanarSpellAim,"controls tab exposes planar aiming toggle");
        aim.Click(hit);Check(s.values[PlanarSpellAim]==0&&aim.Get()[PlanarSpellAim]==1,"aim change is a draft");
        const auto request=aim.Click({Kind::Apply});
        Check(request.changed==(1u<<PlanarSpellAim)&&request.values[PlanarSpellAim]==1,"planar aim saves only its preference");
        Check(!WeaponIndicatorsBox.Contains(PlanarSpellAimBox.x,PlanarSpellAimBox.y)&&
              PlanarSpellAimBox.y+PlanarSpellAimBox.h<876,"aim toggle avoids other controls and footer");}
    Check(s.values[WeaponSkillIndicators]==0,"weapon skill indicators default off");
    {Model indicators;indicators.Open(s);const auto hit=At(Page::Controls,WeaponIndicatorsBox);
        Check(hit.kind==Kind::Change&&hit.index==WeaponSkillIndicators,"controls tab exposes weapon indicator toggle");
        indicators.Click(hit);auto request=indicators.Click({Kind::Apply});
        Check(request.changed==(1u<<WeaponSkillIndicators)&&request.values[WeaponSkillIndicators]==1,"indicator opt-in saves only its preference");}
    Check(s.values[ControllerLabels]==0&&Normalize(ControllerLabels,99)==0&&Normalize(ControllerLabels,1.5f)==0,"unknown controller preferences use automatic");
    {Model chooser;chooser.Open(s);chooser.Click({Kind::Tab,int(Page::VR)});
        auto hit=[&](Rect r){return HitTest(chooser.page,(r.x+r.w*.5f)/Width,(r.y+r.h*.5f)/Height,chooser.controllerMenu);};
        chooser.Click(hit(ControllerBox));Check(chooser.controllerMenu&&!chooser.Dirty(),"dropdown opens without changing selection");
        chooser.Click(hit(ControllerOption(2)));
        Check(!chooser.controllerMenu&&chooser.Get()[ControllerLabels]==2&&chooser.Dirty(),"Index option changes draft only");
        chooser.Click(hit(ControllerBox));
        const auto covered=hit(ValueBox(0));
        Check(covered.kind==Kind::ControllerChoice,"dropdown options own covered setting rows");
        const auto outside=hit({58,RowY,100,40});
        Check(outside.kind==Kind::DismissMenu,"open dropdown consumes outside clicks instead of changing rows");
        chooser.Click(outside);Check(!chooser.controllerMenu&&chooser.Get()[Motion]==0,"outside click only dismisses");
        chooser.Click(hit(ControllerBox));Check(chooser.DismissControllerMenu()&&chooser.open,"Back dismisses dropdown first");
        auto pending=chooser.Click({Kind::Apply});
        Check(pending.changed==(1u<<ControllerLabels)&&pending.values[ControllerLabels]==2,"Apply queues preference alone");
        chooser.Click(hit(ControllerBox));Check(!chooser.controllerMenu,"cannot edit while saving");
        chooser.Close();chooser.Open(s);Check(chooser.Get()[ControllerLabels]==0&&!chooser.controllerMenu,"reopen discards unsaved choice");
    }
    Check(s.values[DrawDistance]==2&&s.values[ObjectDistance]==300,"distance fallbacks match release configuration");
    {Model distance;distance.Open(s);
        for(int i=0;i<3;++i)distance.Click({Kind::Change,ObjectDistance,-1});
        Check(distance.Get()[ObjectDistance]==150,"object distance can reach native 150 without jumping from 300 to 50");}
    Model m;m.Open(s);
    Check(m.open&&m.ready&&!m.Dirty()&&m.page==Page::Controls,"open actual configuration");
    Check(HitTest(Page::VR,-.01f,.5f).kind==Kind::None&&HitTest(Page::VR,NAN,.5f).kind==Kind::None,"invalid laser coordinates ignored");
    Check(At(Page::VR,Calibrate).kind==Kind::Calibrate&&At(Page::VR,Recenter).kind==Kind::Recenter,"separate calibration and recenter buttons");
    Check(Normalize(BeltForward,.06f)==.06f,"settings preserve the closer belt placement");
    for(int p=0;p<TabCount;++p){
        auto hit=At(Page(p),Tabs[p]);Check(hit.kind==Kind::Tab&&hit.index==p,"tab rectangle matches input");
        for(int row=0;row<RowCount(Page(p));++row){
            auto plus=At(Page(p),Increase(row)),minus=At(Page(p),Decrease(row));
            Check(plus.kind==Kind::Change&&plus.index==RowKey(Page(p),row)&&plus.delta==1&&minus.delta==-1,"row controls match drawing");
        }
    }
    m.Click(At(Page::Controls,Tabs[1]));
    m.Click(At(m.page,ValueBox(0)));
    Check(m.Get()[Motion]==1&&m.Dirty()&&!m.Busy(),"edit remains a draft until Apply");
    m.Click({Kind::Close});m.Open(s);
    Check(!m.Dirty()&&m.Get()[Motion]==0,"Close discards unsubmitted edits");
    m.Click({Kind::Change,Foveation,1});m.Click({Kind::Change,Snap,1});
    auto r=m.Click({Kind::Apply});
    Check(r.sequence&&r.changed==((1u<<Foveation)|(1u<<Snap))&&r.values[Foveation]==2,"Apply submits exactly changed values");
    Check(m.Busy()&&!m.Click({Kind::Apply}).sequence,"Apply cannot double submit");
    m.Click({Kind::Change,Motion,1});Check(m.Get()[Motion]==0,"editing blocked during save");
    Snapshot done=s;done.values=r.values;done.sequence=r.sequence;done.result=Result::Saved;m.Observe(done);
    Check(!m.Busy()&&!m.Dirty()&&m.result==Result::Saved,"save acknowledgement resets draft");
    auto changed=done;changed.values[Sky]=1;m.Observe(changed);Check(m.Get()[Sky]==1,"idle panel follows external edits");
    m.Click({Kind::Change,Motion,1});changed.values[Fog]=1;m.Observe(changed);
    Check(m.Get()[Fog]==0&&m.Get()[Motion]==1,"external polling preserves unsaved draft");
    r=m.Click({Kind::Apply});
    Check(!Conflict(changed.values,r.base,r.changed),"unrelated external edits do not conflict");
    changed.values[Motion]=1;Check(Conflict(changed.values,r.base,r.changed),"same-key external edits conflict");
    changed.sequence=r.sequence;changed.result=Result::Conflict;m.Observe(changed);
    Check(!m.Busy()&&!m.Dirty()&&m.Get()[Fog]==1,"conflict reloads current configuration");
    m.Click({Kind::Change,Foveation,-1});r=m.Click({Kind::Apply});
    changed.sequence=r.sequence;changed.result=Result::Failed;m.Observe(changed);
    Check(!m.Busy()&&m.Dirty(),"failed save retains draft for retry");
    Snapshot pending=s;pending.sequence=90;pending.result=Result::Saving;m.Open(pending);
    Check(m.Busy(),"reopening while saving waits for acknowledgement");
    pending.result=Result::Saved;m.Observe(pending);m.Click({Kind::Change,Motion,1});
    Check(m.Click({Kind::Apply}).sequence>90,"new request sequence exceeds observed save sequence");
    m.Open(s);for(int i=0;i<200;++i)m.Click({Kind::Change,Snap,1});
    Check(m.Get()[Snap]==180,"upper limit clamps");
    for(int i=0;i<200;++i)m.Click({Kind::Change,Snap,-1});
    Check(m.Get()[Snap]==15&&Normalize(Waist,INFINITY)==Definitions[Waist].defaultValue,"lower and nonfinite limits");
    s.ready=false;m.Open(s);m.Click({Kind::Change,Motion,1});
    Check(!m.Dirty()&&!m.Click({Kind::Apply}).sequence,"missing config remains read-only");
    InputGate gate;
    auto e=gate.Step(true,true,1,true);Check(!e.chord&&!e.click&&!e.cancel,"initial held controls swallowed");
    gate.Step(true,false,0,false);e=gate.Step(true,true,0,false);Check(e.chord&&!e.click,"fresh chord opens");
    gate.Consume();Check(!gate.Step(true,true,1,false).click,"opening consumes trigger already held");
    gate.Step(true,true,0,false);Check(gate.Step(true,true,.8f,false).click,"fresh press clicks");
    Check(!gate.Step(true,true,.4f,false).click&&!gate.Step(true,true,.8f,false).click,"trigger noise cannot repeat click");
    gate.Step(false,true,1,true);e=gate.Step(true,true,1,true);
    Check(!e.chord&&!e.click&&!e.cancel,"tracking return cannot replay held inputs");
    gate.Step(true,false,0,false);Check(gate.Step(true,false,0,true).cancel,"fresh B cancels");
    Check(Defaults()[CompanionCounts]==1&&RowKey(Page::HUD,9)==CompanionCounts,"companion counts default on in HUD");
    s.ready=true;s.values=Defaults();m.Open(s);m.Click({Kind::Tab,int(Page::HUD)});
    m.Click(At(Page::HUD,ValueBox(9)));
    Check(m.Get()[CompanionCounts]==0&&m.Get()[NavigationLocation]==0,"companion toggle is independent of map placement");
    Check(m.Click({Kind::Apply}).changed==(ChangeMask{1}<<CompanionCounts),"companion toggle uses its own high change-mask bit");
    std::puts("VR settings draft, persistence requests, conflicts, hit regions and input-gating tests passed");
}
