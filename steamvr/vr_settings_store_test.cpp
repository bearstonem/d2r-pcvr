// Isolated Win32 profile-file tests; never opens the installed game configuration.
#include <windows.h>
#include "../vr/settings_menu.h"
#include "body_calibration.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace framevr::settings;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s (Win32 %lu)\n",label,GetLastError());std::exit(1);}}
static int Int(const std::wstring& path,const wchar_t* section,const wchar_t* key){return GetPrivateProfileIntW(section,key,-999,path.c_str());}
int main(){
    wchar_t dir[MAX_PATH],name[MAX_PATH];Check(GetTempPathW(MAX_PATH,dir)&&GetTempFileNameW(dir,L"vrs",0,name),"create isolated INI");
    const std::wstring path=name,backup=path+L".before-vr-menu";
    Check(WritePrivateProfileStringW(L"custom",L"keep",L"12345",name),"write unrelated key");
    Check(WritePrivateProfileStringW(L"render",L"rings",L"6",name),"write existing setting");
    vrsettings::Poll(name);auto before=vrsettings::Read();
    Check(before.values[IndoorFog]==0,"profiles without an indoor fog preference default off");
    Check(WritePrivateProfileStringW(L"fog",L"caves",L"1",name),"write existing indoor fog preference");
    Check(WritePrivateProfileStringW(L"fog",L"enabled",L"1",name),"write outdoor fog preference");
    vrsettings::Poll(name);
    Check(vrsettings::Read().values[IndoorFog]==1,"existing explicit indoor fog choice is preserved");
    for(int choice:{0,1,0}) {
        const auto current=vrsettings::Read();Model fog;fog.Open(current);
        fog.Click({Kind::Change,IndoorFog,1});const auto request=fog.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<IndoorFog)&&vrsettings::Submit(request)&&vrsettings::Poll(name),"persist independent indoor fog toggle");
        const auto saved=vrsettings::Read();
        Check(saved.values[IndoorFog]==choice&&Int(path,L"fog",L"caves")==choice,"indoor fog survives reload in the existing caves key");
        Check(Changed(saved.values,current.values)==(ChangeMask{1}<<IndoorFog)&&Int(path,L"fog",L"enabled")==1&&Int(path,L"custom",L"keep")==12345,
            "indoor fog leaves outdoor fog and unrelated settings untouched");
    }
    Check(before.values[HapticShortMelee]==0,"missing short-melee preference selects the compatible longer pulse");
    for(int choice:{1,0}){
        const auto current=vrsettings::Read();Model h;h.Open(current);h.Click({Kind::Change,HapticShortMelee,1});
        const auto request=h.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<HapticShortMelee)&&vrsettings::Submit(request)&&vrsettings::Poll(name),"persist short-melee toggle");
        const auto saved=vrsettings::Read();
        Check(saved.values[HapticShortMelee]==choice&&Int(path,L"haptics",L"short_melee")==choice,"short-melee toggle survives profile reload");
        Check(Changed(saved.values,current.values)==(ChangeMask{1}<<HapticShortMelee)&&Int(path,L"custom",L"keep")==12345,"pulse toggle preserves all strengths and unrelated settings");
    }
    for(int i=HapticStrength;i<=HapticAlert;++i){
        const auto key=Key(i);const auto& definition=Definitions[key];
        Check(before.values[key]==100,"existing profiles keep full haptic strength");
        for(int choice:{50,0,200,100}){
            const auto current=vrsettings::Read();Model h;h.Open(current);
            const int delta=(choice-int(current.values[key]))/10;
            for(int step=0;step<std::abs(delta);++step)h.Click({Kind::Change,key,delta>0?1:-1});
            const auto request=h.Click({Kind::Apply});
            Check(request.changed==(ChangeMask{1}<<key)&&vrsettings::Submit(request)&&vrsettings::Poll(name),"save haptic strength through its high mask bit");
            const auto saved=vrsettings::Read();
            Check(saved.values[key]==choice&&Int(path,definition.section,definition.key)==choice,"muted, reduced and boosted strengths survive INI reload");
            Check(Changed(saved.values,current.values)==(ChangeMask{1}<<key)&&Int(path,L"custom",L"keep")==12345,"haptic edits preserve other categories and unrelated preferences");
        }
    }
    Check(before.values[VitalsTilt]==0,"existing profiles keep their original vitals angle");
    for(int choice:{5,0,-5,0}) {
        const auto current=vrsettings::Read();Model tilt;tilt.Open(current);
        tilt.Click({Kind::Change,VitalsTilt,choice>current.values[VitalsTilt]?1:-1});
        const auto request=tilt.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<VitalsTilt)&&vrsettings::Submit(request)&&vrsettings::Poll(name),"save tilt through a mask above bit 31");
        const auto saved=vrsettings::Read();
        Check(saved.values[VitalsTilt]==choice&&Int(path,L"interactions",L"vitals_tilt")==choice,"positive, negative and zero tilt survive INI reload");
        Check(Changed(saved.values,current.values)==(ChangeMask{1}<<VitalsTilt)&&Int(path,L"custom",L"keep")==12345,
            "tilt saves preserve every unrelated preference and INI key");
    }
    {const auto current=vrsettings::Read();Request request;
        request.base=request.values=current.values;request.values[VitalsTilt]=15;request.values[Motion]=1;
        request.changed=Changed(request.base,request.values);request.sequence=current.sequence+1;
        Check(request.changed==((ChangeMask{1}<<VitalsTilt)|(ChangeMask{1}<<Motion))&&vrsettings::Submit(request)&&vrsettings::Poll(name),
            "low and high change bits save together");
        Check(vrsettings::Read().values[VitalsTilt]==15&&vrsettings::Read().values[Motion]==1,"mixed-mask values both reload");
        const auto saved=vrsettings::Read();request.base=saved.values;request.values=current.values;
        request.changed=Changed(request.base,request.values);request.sequence=saved.sequence+1;
        Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"restore mixed-mask fixture preferences");
        request.base=request.values=vrsettings::Read().values;request.values[VitalsTilt]=5;
        request.changed=(ChangeMask{1}<<VitalsTilt)|(ChangeMask{1}<<63);request.sequence+=1;
        Check(!vrsettings::Submit(request),"unsupported high change bits are rejected");
    }
    Check(before.values[MeleeHitSpeed]==12,"missing melee speed uses twelve m/s default");
    for(float choice:{12.25f,12.f}) {
        Model melee;melee.Open(vrsettings::Read());melee.Click({Kind::Change,MeleeHitSpeed,choice>12?1:-1});
        const auto request=melee.Click({Kind::Apply});
        Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"existing melee mask bit persists speed");
        Check(vrsettings::Read().values[MeleeHitSpeed]==choice&&Int(path,L"custom",L"keep")==12345,"fractional melee speed reloads without altering unrelated data");
    }
    for(float choice:{12.25f,24.f,3.f}) {
        const auto current=vrsettings::Read();Request request;
        request.base=request.values=current.values;request.values[MeleeHitSpeed]=choice;
        request.changed=Changed(request.base,request.values);request.sequence=current.sequence+1;
        Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"save uncapped melee speed");
        Check(vrsettings::Read().values[MeleeHitSpeed]==choice,"values above six survive INI reload");
    }
    Check(vrsettings::Read().values[MeleeHitSpeed]==3,"explicit older preference remains unchanged by new default");
    Check(before.values[MenuDistance]==1&&before.values[TooltipTiming]==0,"legacy profile uses Medium distance and Instant tooltips");
    Check(before.values[ProjectileAimAssist]==2,"missing aim assist defaults to Strong");
    for(int choice:{0,1,2,0}) {
        Model assist;assist.Open(vrsettings::Read());assist.Click({Kind::Change,ProjectileAimAssist,1});
        const auto request=assist.Click({Kind::Apply});
        Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"save aim assist preference");
        const auto saved=vrsettings::Read();
        Check(saved.values[ProjectileAimAssist]==choice&&Int(path,L"gameplay",L"projectile_aim_assist")==choice,"aim assist survives profile reload");
        Check(saved.values[PlanarSpellAim]==0&&Int(path,L"custom",L"keep")==12345,"assistance preserves free aim and unrelated configuration");
    }
    Check(before.values[OrdinaryMobBars]==0,"legacy profile defaults to hidden ordinary bars");
    Check(before.values[SmoothTurn]==0&&before.values[Snap]==30,"missing turn settings default to snap turning");
    Check(before.values[WristMap]==1,"legacy INI without wrist_map defaults on");
    Check(before.values[WristCompass]==1,"legacy INI without wrist_compass defaults on");
    Check(before.values[VitalsLocation]==0&&before.values[CriticalAlerts]==0,"legacy INI preserves wrist but disables unsolicited vitals alerts");
    Check(before.ready&&before.values[DrawDistance]==6&&before.values[Foveation]==1&&before.values[ObjectDistance]==300,"explicit distance retained; missing object distance uses shipped default");
    Check(WritePrivateProfileStringW(L"render",L"rings",nullptr,name),"remove terrain distance in isolated legacy profile");
    vrsettings::Poll(name);before=vrsettings::Read();
    Check(before.values[DrawDistance]==2&&before.values[ObjectDistance]==300,"missing draw distances do not silently select 4 rings and radius 1000");
    Check(WritePrivateProfileStringW(L"render",L"rings",L"6",name),"restore explicit terrain distance");
    vrsettings::Poll(name);before=vrsettings::Read();
    // Old separate switches migrate only until an explicit combined choice exists.
    for(int map:{0,1})for(int compass:{0,1}) {
        Check(WritePrivateProfileStringW(L"interactions",L"wrist_map",map?L"1":L"0",name),"write legacy map preference");
        Check(WritePrivateProfileStringW(L"interactions",L"wrist_compass",compass?L"1":L"0",name),"write legacy compass preference");
        vrsettings::Poll(name);
        Check(vrsettings::Read().values[NavigationLocation]==(map||compass?0:3),"legacy both-off stays off; either enabled migrates to combined wrist");
    }
    Check(WritePrivateProfileStringW(L"interactions",L"navigation_location",L"1",name),"set explicit palm navigation");
    Check(WritePrivateProfileStringW(L"interactions",L"wrist_map",L"0",name)&&WritePrivateProfileStringW(L"interactions",L"wrist_compass",L"0",name),"legacy switches now off");
    vrsettings::Poll(name);Check(vrsettings::Read().values[NavigationLocation]==1,"explicit combined preference overrides old switches");
    Check(WritePrivateProfileStringW(L"interactions",L"navigation_location",L"1.5",name),"malformed enum");
    vrsettings::Poll(name);Check(vrsettings::Read().values[NavigationLocation]==0,"fractional location falls back to Wrist");
    Check(WritePrivateProfileStringW(L"interactions",L"navigation_location",nullptr,name)&&
        WritePrivateProfileStringW(L"interactions",L"wrist_map",L"1",name)&&WritePrivateProfileStringW(L"interactions",L"wrist_compass",L"1",name),"restore legacy defaults");
    vrsettings::Poll(name);before=vrsettings::Read();
    Model model;model.Open(before);model.Click({Kind::Change,Foveation,1});model.Click({Kind::Change,Motion,1});
    model.Click({Kind::Change,OrdinaryMobBars,1});
    auto request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&!vrsettings::Submit(request),"one queued transaction at a time");
    Check(vrsettings::Read().result==Result::Saving&&vrsettings::Poll(name),"write transaction succeeds");
    auto saved=vrsettings::Read();Check(saved.result==Result::Saved&&saved.values[Foveation]==2&&saved.values[Motion]==1,"saved values round trip");
    Check(saved.values[OrdinaryMobBars]==1&&Int(path,L"hud",L"ordinary_mob_bars")==1,"show ordinary bars round trips");
    Check(saved.values[RenderResolution]==0,"legacy profile keeps desktop resolution until opted in");
    for(int choice=1;choice<framevr::resolution::ChoiceCount;++choice){
        model.Open(vrsettings::Read());model.Click({Kind::Change,RenderResolution,1});request=model.Click({Kind::Apply});
        Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"save render resolution");
        saved=vrsettings::Read();
        Check(saved.values[RenderResolution]==choice&&Int(path,L"render",L"vr_resolution")==choice&&Int(path,L"custom",L"keep")==12345,
            "headset and all fixed presets survive reload without changing unrelated settings");
    }
    model.Open(saved);for(int i=1;i<framevr::resolution::ChoiceCount;++i)model.Click({Kind::Change,RenderResolution,-1});
    request=model.Click({Kind::Apply});Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"restore desktop resolution");
    saved=vrsettings::Read();Check(saved.values[RenderResolution]==0&&Int(path,L"render",L"vr_resolution")==0,"desktop preference survives reload");
    for(int location=1;location<=3;++location){
        model.Open(vrsettings::Read());model.Click({Kind::Change,VitalsLocation,1});request=model.Click({Kind::Apply});
        Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"save vitals location");
        saved=vrsettings::Read();
        Check(saved.values[VitalsLocation]==location&&Int(path,L"interactions",L"vitals_location")==location&&
            saved.values[CriticalAlerts]==0&&saved.values[WristMap]==1&&saved.values[WristCompass]==1,"all locations survive reload without changing alerts or navigation");
    }
    for(int enabled:{1,0}){
        model.Open(vrsettings::Read());model.Click({Kind::Change,CriticalAlerts,1});request=model.Click({Kind::Apply});
        Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"save alert preference");
        saved=vrsettings::Read();
        Check(saved.values[CriticalAlerts]==enabled&&Int(path,L"interactions",L"critical_alerts")==enabled&&Int(path,L"custom",L"keep")==12345,
            "alert opt-in and opt-out survive reload and preserve unrelated settings");
    }
    Check(saved.values[SkillGestures]==1,"skill gestures default on for existing profiles");
    Check(saved.values[WeaponSkillIndicators]==0,"existing profiles hide weapon skill indicators by default");
    model.Open(saved);model.Click({Kind::Change,WeaponSkillIndicators,1});request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"weapon indicator opt-in saves");
    saved=vrsettings::Read();
    Check(saved.values[WeaponSkillIndicators]==1&&Int(path,L"interactions",L"weapon_skill_indicators")==1,"weapon indicator opt-in survives reload");
    model.Open(saved);model.Click({Kind::Change,WeaponSkillIndicators,1});request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"weapon indicators can be hidden again");
    saved=vrsettings::Read();Check(saved.values[WeaponSkillIndicators]==0&&saved.values[SkillGestures]==1,"hiding indicators preserves weapon skill gestures");
    Check(saved.values[PlanarSpellAim]==0,"missing planar key defaults to free aim");
    model.Open(saved);model.Click({Kind::Change,PlanarSpellAim,1});request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"free spell aim opt-in saves");
    saved=vrsettings::Read();
    Check(saved.values[PlanarSpellAim]==1&&Int(path,L"gameplay",L"planar_spell_aim")==1,"explicit planar aim survives INI reload");
    model.Open(saved);model.Click({Kind::Change,PlanarSpellAim,1});request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"free aiming can be restored");
    saved=vrsettings::Read();
    Check(saved.values[PlanarSpellAim]==0&&Int(path,L"gameplay",L"planar_spell_aim")==0,"free aim restore survives INI reload");
    Check(saved.values[ControllerLabels]==0,"existing INI defaults to automatic labels");
    model.Open(saved);model.Click({Kind::Tab,int(Page::VR)});model.Click({Kind::ControllerMenu});
    model.Click({Kind::ControllerChoice,int(framevr::controllers::Choice::Index)});request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"controller override saves through INI transaction");
    saved=vrsettings::Read();
    Check(saved.values[ControllerLabels]==2&&Int(path,L"interactions",L"controller_labels")==2,"controller selection survives reload");
    Check(Int(path,L"render",L"fixed_foveation")==1&&Int(path,L"render",L"eye_tracked_foveation")==1,"eye-tracked mode writes both keys");
    Check(Int(path,L"custom",L"keep")==12345&&Int(path,L"render",L"rings")==6,"Apply preserves unrelated keys");
    Check(Int(backup,L"custom",L"keep")==12345&&Int(backup,L"gameplay",L"motion_controls")==-999,"first Apply preserves original config backup");
    model.Open(saved);model.Click({Kind::Change,SkillGestures,-1});request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"gesture preference saves through the normal settings transaction");
    saved=vrsettings::Read();
    Check(saved.values[SkillGestures]==0&&Int(path,L"gameplay",L"skill_gestures")==0,"gesture opt-out survives profile reload");
    for(int location=1;location<=3;++location){
        model.Open(vrsettings::Read());model.Click({Kind::Change,NavigationLocation,1});request=model.Click({Kind::Apply});
        Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"save combined navigation location");
        saved=vrsettings::Read();
        Check(saved.values[NavigationLocation]==location&&Int(path,L"interactions",L"navigation_location")==location&&
            saved.values[VitalsLocation]==3&&saved.values[CriticalAlerts]==0,"navigation location survives reload independently of vitals");
    }
    model.Open(saved);model.Click({Kind::Change,Foveation,-1});model.Click({Kind::Change,Foveation,-1});
    model.Click({Kind::Change,OrdinaryMobBars,-1});
    request=model.Click({Kind::Apply});
    Check(WritePrivateProfileStringW(L"custom",L"later",L"42",name),"simulate unrelated external edit");
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"merge unrelated external edit");
    Check(Int(path,L"render",L"fixed_foveation")==0&&Int(path,L"render",L"eye_tracked_foveation")==0,"Off writes both keys");
    Check(vrsettings::Read().values[OrdinaryMobBars]==0&&Int(path,L"hud",L"ordinary_mob_bars")==0,"hide ordinary bars round trips");
    Check(Int(path,L"custom",L"later")==42&&Int(backup,L"custom",L"later")==-999,"backup is not overwritten on subsequent saves");
    model.Open(vrsettings::Read());model.Click({Kind::Change,DrawDistance,-1});request=model.Click({Kind::Apply});
    Check(WritePrivateProfileStringW(L"render",L"rings",L"2",name),"simulate same-key external edit");
    Check(vrsettings::Submit(request)&&!vrsettings::Poll(name),"concurrent same-key write rejected");
    Check(vrsettings::Read().result==Result::Conflict&&Int(path,L"render",L"rings")==2,"conflict preserves external value");
    request.base=vrsettings::Read().values;request.values=request.base;request.values[DrawDistance]=99;request.changed=1u<<DrawDistance;request.sequence=900;
    Check(!vrsettings::Submit(request),"out of range request rejected before disk write");
    request.values[DrawDistance]=3;request.changed=1u<<Motion;Check(!vrsettings::Submit(request),"mismatched key mask rejected");
    request.changed=1u<<DrawDistance;request.base[Sky]=NAN;Check(!vrsettings::Submit(request),"nonfinite request baseline rejected");
    Check(WritePrivateProfileStringW(L"interactions",L"belt_forward",L"0.06",name),"save closer belt preference");
    vrsettings::Poll(name);before=vrsettings::Read();
    Check(before.values[BeltForward]==.06f&&!framevr::calibration::Stored(before.values),"uncalibrated profile keeps belt preference");
    request=framevr::calibration::SaveRequest(before,{1.72f,1.63f});
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"body measurement transaction succeeds");
    saved=vrsettings::Read();
    Check(framevr::calibration::Stored(saved.values)&&std::fabs(saved.values[EyeHeight]-1.72f)<1e-5f&&std::fabs(saved.values[ArmSpan]-1.63f)<1e-5f,"eye height, arm span and version round trip together");
    Check(Int(path,L"calibration",L"version")==1&&Int(path,L"custom",L"keep")==12345&&saved.values[BeltForward]==.06f,"calibration preserves unrelated settings");
    Check(WritePrivateProfileStringW(L"calibration",L"version",L"99",name),"simulate future calibration version");
    vrsettings::Poll(name);Check(!framevr::calibration::Stored(vrsettings::Read().values),"unknown record version requests calibration");
    Check(WritePrivateProfileStringW(L"calibration",L"version",L"1",name)&&WritePrivateProfileStringW(L"calibration",L"eye_height_m",L"3.4",name),"simulate corrupt height");
    vrsettings::Poll(name);Check(!framevr::calibration::Stored(vrsettings::Read().values),"invalid raw height cannot become calibrated through clamps");
    Check(WritePrivateProfileStringW(L"turn",L"snap",L"45",name),"set a custom snap angle");
    vrsettings::Poll(name);
    for(int mode=1;mode>=0;--mode){
        model.Open(vrsettings::Read());model.Click({Kind::Change,SmoothTurn,1});request=model.Click({Kind::Apply});
        Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"save smooth turning toggle");
        saved=vrsettings::Read();
        Check(saved.values[SmoothTurn]==mode&&saved.values[Snap]==45&&Int(path,L"turn",L"smooth")==mode&&Int(path,L"turn",L"snap")==45,"toggle round trip preserves custom snap angle");
        Check(EffectiveSnapAngle(saved.values[Snap],saved.values[SmoothTurn])==(mode?0:45),"saved mode selects expected camera turning");
    }
    Check(WritePrivateProfileStringW(L"turn",L"smooth",nullptr,name)&&WritePrivateProfileStringW(L"turn",L"snap",L"0",name),"simulate legacy smooth profile");
    vrsettings::Poll(name);saved=vrsettings::Read();
    Check(saved.values[SmoothTurn]==1&&saved.values[Snap]==30,"legacy smooth profile loads with a usable snap angle");
    model.Open(saved);model.Click({Kind::Change,SmoothTurn,1});request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"legacy smooth profile can opt into snap turning");
    saved=vrsettings::Read();
    Check(saved.values[SmoothTurn]==0&&saved.values[Snap]==30&&EffectiveSnapAngle(float(Int(path,L"turn",L"snap")),float(Int(path,L"turn",L"smooth")))==30,"explicit off overrides legacy snap zero in menu and camera");
    Check(WritePrivateProfileStringW(L"turn",L"smooth",nullptr,name),"restore legacy smooth mode");
    vrsettings::Poll(name);model.Open(vrsettings::Read());model.Click({Kind::Change,Snap,1});request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"edit angle in legacy smooth profile");
    saved=vrsettings::Read();
    Check(saved.values[SmoothTurn]==1&&saved.values[Snap]==45&&Int(path,L"turn",L"smooth")==1,"angle edit preserves legacy smooth mode explicitly");
    Check(Int(path,L"custom",L"keep")==12345,"turning changes preserve unrelated settings");
    for(auto key:{MenuDistance,TooltipTiming})for(int choice:{2,1,0}) {
        auto current=vrsettings::Read();model.Open(current);
        const int delta=choice-int(current.values[key]);
        for(int i=0;i<std::abs(delta);++i)model.Click({Kind::Change,key,delta>0?1:-1});
        request=model.Click({Kind::Apply});
        Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"save comfort enum");
        const auto& definition=Definitions[key];
        Check(vrsettings::Read().values[key]==choice&&Int(path,definition.section,definition.key)==choice&&Int(path,L"custom",L"keep")==12345,"all comfort presets survive INI round trip and preserve unrelated settings");
    }
    model.Open(vrsettings::Read());model.Click({Kind::Change,Motion,-1});request=model.Click({Kind::Apply});
    Check(DeleteFileW(name)&&vrsettings::Submit(request)&&!vrsettings::Poll(name),"missing file fails safely");
    Check(!vrsettings::Read().ready&&vrsettings::Read().result==Result::Failed,"failure status published");
    Check(GetFileAttributesW(name)==INVALID_FILE_ATTRIBUTES&&Int(backup,L"custom",L"keep")==12345,"missing original not recreated; backup survives");
    Check(DeleteFileW(backup.c_str()),"clean up isolated backup");
    std::puts("VR settings Windows INI transaction, backup, conflict and failure tests passed");
}
