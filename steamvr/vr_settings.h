#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include "controller_labels.h"
#include "hand_vitals.h"
#include "vr_resolution.h"
#include "haptic_strength.h"

namespace framevr::settings {
enum class Page { Controls, VR, Video, HUD, Combat, Haptics };
enum Key { Motion, Seated, Dominant, Snap, TurnSpeed, Waist, BeltForward,
           Foveation, Sky, Fog, Ceiling, DrawDistance, ObjectDistance,
           EyeHeight, ArmSpan, BodyCalibration, SkillGestures, WristMap, WristCompass, ControllerLabels, WeaponSkillIndicators, PlanarSpellAim, VitalsLocation, CriticalAlerts, RenderResolution, NavigationLocation, SmoothTurn, OrdinaryMobBars, MenuDistance, TooltipTiming, ProjectileAimAssist, MeleeHitSpeed, VitalsTilt, HapticStrength, HapticMelee, HapticBowDraw, HapticBowRelease, HapticSpell, HapticInteraction, HapticAlert, HapticShortMelee, BloodVisor, BloodWipe, IndoorFog, Count };
using ChangeMask=uint64_t;
static_assert(Count<=64,"settings change mask capacity");
constexpr ChangeMask SupportedChanges=~ChangeMask{0}>>(64-Count);
struct Definition {
    const char* label;
    const char* detail;
    const wchar_t* section;
    const wchar_t* key;
    float defaultValue,min,max,step;
};
inline constexpr std::array<Definition,Count> Definitions{{
    {"Motion combat", "Physical melee, bow and javelin; spell wheel and belt interactions", L"gameplay",L"motion_controls",0,0,1,1},
    {"Seated belt position", "Raises the belt for seated reach", L"interactions",L"seated",0,0,1,1},
    {"Dominant hand", "Grip over either shoulder switches weapon sets", L"interactions",L"dominant_hand",0,0,1,1},
    {"Snap turn angle", "Degrees per flick; used when smooth turning is off", L"turn",L"snap",30,15,180,15},
    {"Smooth turn speed", "Degrees per second; used when smooth turning is on", L"turn",L"speed",120,10,720,30},
    {"Belt drop below eyes", "Standing mode only; smaller values raise the belt", L"interactions",L"waist_below_eyes",.65f,.30f,.95f,.05f},
    {"Belt forward reach", "Distance forward from the estimated torso", L"interactions",L"belt_forward",.26f,.03f,.5f,.025f},
    {"Foveated rendering", "Reduce peripheral shading; missing gaze uses full quality", L"render",L"fixed_foveation",1,0,2,1},
    {"Added sky", "The mod's outdoor sky effect", L"sky",L"enabled",0,0,1,1},
    {"Added fog", "Distance fog; indoor areas also require Indoor fog", L"fog",L"enabled",0,0,1,1},
    {"Dungeon ceiling", "The mod's overhead dungeon surfaces", L"ceiling",L"enabled",0,0,1,1},
    {"Terrain draw distance", "Higher values show more map tiles and cost more to render", L"render",L"rings",2,1,8,1},
    {"Object draw distance", "Higher values retain more distant scenery", L"render",L"model_radius",300,10,10000,50},
    {"Standing eye height", "Measured from the SteamVR floor", L"calibration",L"eye_height_m",1.64f,.8f,2.3f,.01f},
    {"Controller span", "Measured between grips in a T-pose", L"calibration",L"arm_span_m",1.6f,.85f,2.6f,.01f},
    {"Body calibration", "Saved body calibration version", L"calibration",L"version",0,0,1,1},
    {"Weapon skill gestures", "Compatible trigger skills power shield bash and bow release", L"gameplay",L"skill_gestures",1,0,1,1},
    {"Wrist minimap", "Show above wrist health and mana; the wheel MAP tab stays available", L"interactions",L"wrist_map",1,0,1,1},
    {"Wrist compass", "Left wrist bearings; N follows the top of the minimap", L"interactions",L"wrist_compass",1,0,1,1},
    {"Headset / controller labels", "Automatic uses the active controllers; override if labels do not match", L"interactions",L"controller_labels",0,0,4,1},
    {"Weapon skill indicators", "Show skill icons and names beside both weapons", L"interactions",L"weapon_skill_indicators",0,0,1,1},
    {"Planar spell aiming", "Keep straight projectiles level; Off allows aiming up/down", L"gameplay",L"planar_spell_aim",0,0,1,1},
    {"Health / mana location", "Wrist, palm-up, either gesture, or hidden", L"interactions",L"vitals_location",0,0,3,1},
    {"Low health / mana alerts", "Brief popup and vibration when resources are low", L"interactions",L"critical_alerts",0,0,1,1},
    {"VR render resolution", "Independent of monitor; higher values cost GPU time", L"render",L"vr_resolution",0,0,6,1},
    {"Minimap & Compass", "Wrist / Palm / Either / Off; independent of health and mana", L"interactions",L"navigation_location",0,0,3,1},
    {"Smooth turning", "Continuous right-stick turning; Off uses snap turning", L"turn",L"smooth",0,0,1,1},
    {"Ordinary mob bars", "Weak mobs' health and names; bosses and elites stay visible", L"hud",L"ordinary_mob_bars",0,0,1,1},
    {"Menu distance", "Eye-level inventory, merchant and main menus", L"hud",L"menu_distance",1,0,2,1},
    {"Item tooltip timing", "Laser hover: Instant, Short (300 ms), or Original timing", L"hud",L"tooltip_timing",0,0,2,1},
    {"Projectile aim assist", "Hand spells: Light assists launch; Strong adds gentle homing", L"gameplay",L"projectile_aim_assist",2,0,2,1},
    {"Melee hit speed", "Higher requires faster weapon swings; shield bash is unchanged", L"gameplay",L"melee_hit_speed",12,1.5f,std::numeric_limits<float>::max(),.25f},
    {"Health / mana tilt", "Wrist / palm angle; + tilts the top toward you", L"interactions",L"vitals_tilt",0,vitals::MinTilt,vitals::MaxTilt,vitals::TiltStep},
    {"Overall strength", "Both controllers; scales every category below", L"haptics",L"strength",100,0,200,10},
    {"Melee impacts", "Weapon and shield contact", L"haptics",L"melee",100,0,200,10},
    {"Bow draw", "Nocking, pulling and full-draw cues", L"haptics",L"bow_draw",100,0,200,10},
    {"Bow / throw release", "Arrow releases and javelin throws", L"haptics",L"bow_release",100,0,200,10},
    {"Spell casts", "Supported hand spells; one pulse per cast", L"haptics",L"spells",100,0,200,10},
    {"Interactions", "Loot, potions, holsters and menu feedback", L"haptics",L"interactions",100,0,200,10},
    {"Resource alerts", "Optional low health / mana warnings", L"haptics",L"alerts",100,0,200,10},
    {"Short melee pulses", "Off: longer taps; On: very short, sharp taps", L"haptics",L"short_melee",0,0,1,1},
    {"Blood on visor", "Low life splashes the view; healing back above a band fades it", L"hud",L"blood_visor",1,0,1,1},
    {"Face wipe gesture", "Scrub side to side in front of your face to wipe blood away", L"hud",L"blood_wipe",1,0,1,1},
    {"Indoor fog", "Caves and dungeons; requires Added fog to be On", L"fog",L"caves",0,0,1,1},
}};
using Values=std::array<float,Count>;
inline bool HapticKey(Key key){return key>=HapticStrength&&key<=HapticAlert;}
inline int HapticCategory(Key key){return key>=HapticMelee&&key<=HapticAlert?key-HapticMelee:-1;}
inline haptics::Sample HapticSample(const Values& values,haptics::Category category,int hand=0){return haptics::SampleFor(category,values[HapticShortMelee]>.5f,hand);}
inline haptics::Strengths HapticLevels(const Values& values){
    haptics::Strengths levels;levels.master=values[HapticStrength];
    static_assert(HapticAlert-HapticMelee+1==haptics::CategoryCount);
    for(int i=0;i<haptics::CategoryCount;++i)levels.category[i]=values[HapticMelee+i];
    return levels;
}
inline Values Defaults(){Values v{};for(int i=0;i<Count;++i)v[i]=Definitions[i].defaultValue;return v;}
inline float Normalize(Key key,float value){
    const auto& d=Definitions[key];
    if(!std::isfinite(value))return d.defaultValue;
    if(key==VitalsTilt)return vitals::NormalizeTilt(value);
    if(HapticKey(key))return haptics::NormalizeStrength(value);
    if((key==ControllerLabels||key==VitalsLocation||key==NavigationLocation||key==RenderResolution||key==MenuDistance||key==TooltipTiming||key==ProjectileAimAssist)&&(value<d.min||value>d.max||value!=std::round(value)))return d.defaultValue;
    value=std::clamp(value,d.min,d.max);
    return d.step==1?std::round(value):value;
}
// Existing profiles used snap=0 for smooth turning. An explicit toggle takes
// precedence; keep a usable angle so turning smooth off restores snap turning.
inline bool SmoothTurning(float snap,float smooth){return smooth<0?snap==0:Normalize(SmoothTurn,smooth)>.5f;}
inline float SnapDegrees(float snap){return snap==0?Definitions[Snap].defaultValue:Normalize(Snap,snap);}
inline float EffectiveSnapAngle(float snap,float smooth){return SmoothTurning(snap,smooth)?0.f:SnapDegrees(snap);}
inline ChangeMask Changed(const Values& a,const Values& b){ChangeMask mask=0;for(int i=0;i<Count;++i)if(std::fabs(a[i]-b[i])>.0001f)mask|=ChangeMask{1}<<i;return mask;}
inline bool Conflict(const Values& current,const Values& base,ChangeMask mask){return (Changed(current,base)&mask)!=0;}
enum class Result { None, Saving, Saved, Failed, Conflict };
struct Snapshot {Values values=Defaults();bool ready=false;uint64_t sequence=0,revision=0;Result result=Result::None;};
struct Request {Values base{},values{};ChangeMask changed=0;uint64_t sequence=0;};
struct Rect {
    int x,y,w,h;
    bool Contains(float px,float py)const{return px>=x&&px<x+w&&py>=y&&py<y+h;}
};
constexpr int Width=1280,Height=1024;
constexpr int TabCount=6;
constexpr Rect Tabs[TabCount]={{40,114,185,60},{243,114,185,60},{446,114,185,60},{649,114,185,60},{852,114,185,60},{1055,114,185,60}};
constexpr Rect Apply{965,918,275,68},Close{650,918,275,68},Recenter{40,918,270,68},Calibrate{330,918,300,68};
constexpr int RowY=242,RowStep=62;
constexpr Rect ControllerBox{865,181,375,52};
inline constexpr const char* MenuDistanceNames[]={"Close","Medium","Far"};
inline constexpr const char* TooltipTimingNames[]={"Instant","Short","Original"};
constexpr Rect WeaponIndicatorsBox{942,666,221,52},PlanarSpellAimBox{942,728,221,52},AimAssistBox{942,790,221,52};
inline Rect TestHapticsBox(int row,int choice,haptics::Category category){return {haptics::PreviewCount(category)==1?700:590+choice*128,RowY+row*RowStep,haptics::PreviewCount(category)==1?140:122,52};}
inline constexpr const char* AimAssistNames[]={"Off","Light","Strong"};
inline Rect ControllerOption(int option){return {865,233+option*58,375,58};}
inline Rect Decrease(int row){return {865,RowY+row*RowStep,64,52};}
inline Rect Increase(int row){return {1176,RowY+row*RowStep,64,52};}
inline Rect ValueBox(int row){return {942,RowY+row*RowStep,221,52};}
inline int RowCount(Page page){return page==Page::Haptics?2+haptics::CategoryCount:page==Page::Combat?1:page==Page::VR?10:page==Page::Video?8:page==Page::HUD?8:0;}
inline Key RowKey(Page page,int row){
    constexpr Key hapticRows[]={HapticStrength,HapticMelee,HapticShortMelee,HapticBowDraw,HapticBowRelease,HapticSpell,HapticInteraction,HapticAlert};
    if(page==Page::Haptics)return hapticRows[row];
    if(page==Page::Combat)return MeleeHitSpeed;
    constexpr Key hud[]={VitalsLocation,VitalsTilt,CriticalAlerts,NavigationLocation,MenuDistance,TooltipTiming,BloodVisor,BloodWipe};
    if(page==Page::HUD)return hud[row];
    constexpr Key video[]={Foveation,Sky,Fog,IndoorFog,Ceiling,DrawDistance,ObjectDistance,RenderResolution};
    if(page==Page::Video)return video[row];
    constexpr Key vrRows[]={Motion,Seated,Dominant,SmoothTurn,Snap,TurnSpeed,Waist,BeltForward,SkillGestures,OrdinaryMobBars};
    return page==Page::VR?vrRows[row]:Key(Foveation+row);
}
enum class Kind { None, Tab, Change, Apply, Close, Recenter, Calibrate, ControllerMenu, ControllerChoice, DismissMenu, TestHaptics };
struct Hit {Kind kind=Kind::None;int index=-1,delta=0;};
inline Hit HitTest(Page page,float u,float v,bool controllerMenu=false){
    if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>=1||v<0||v>=1)return {};
    const float x=u*Width,y=v*Height;
    if(page==Page::VR){
        if(ControllerBox.Contains(x,y))return {Kind::ControllerMenu};
        if(controllerMenu){
            for(int i=0;i<int(controllers::Choice::Count);++i)
                if(ControllerOption(i).Contains(x,y))return {Kind::ControllerChoice,i};
            return {Kind::DismissMenu}; // Never click through the dropdown.
        }
    }
    for(int i=0;i<TabCount;++i)if(Tabs[i].Contains(x,y))return {Kind::Tab,i};
    if(Apply.Contains(x,y))return {Kind::Apply};
    if(Close.Contains(x,y))return {Kind::Close};
    if(Recenter.Contains(x,y))return {Kind::Recenter};
    if(Calibrate.Contains(x,y))return {Kind::Calibrate};
    if(page==Page::Controls&&WeaponIndicatorsBox.Contains(x,y))return {Kind::Change,WeaponSkillIndicators,1};
    if(page==Page::Controls&&PlanarSpellAimBox.Contains(x,y))return {Kind::Change,PlanarSpellAim,1};
    if(page==Page::Controls&&AimAssistBox.Contains(x,y))return {Kind::Change,ProjectileAimAssist,1};
    for(int r=0;r<RowCount(page);++r){
        const auto key=RowKey(page,r);
        if(page==Page::Haptics&&HapticCategory(key)>=0){
            const auto category=haptics::Category(HapticCategory(key));
            for(int choice=0;choice<haptics::PreviewCount(category);++choice)
                if(TestHapticsBox(r,choice,category).Contains(x,y))return {Kind::TestHaptics,HapticCategory(key),choice+1};
        }
        if(Decrease(r).Contains(x,y))return {Kind::Change,key,-1};
        if(Increase(r).Contains(x,y))return {Kind::Change,key,1};
        if(Definitions[key].max==1&&ValueBox(r).Contains(x,y))return {Kind::Change,key,1};
    }
    return {};
}
class Model {
    Values base=Defaults(),draft=base;
    uint64_t serial=0,pending=0;
public:
    bool open=false,ready=false,controllerMenu=false;
    Page page=Page::Controls;
    Result result=Result::None;
    uint64_t revision=0;
    const Values& Get()const{return draft;}
    bool Dirty()const{return Changed(base,draft)!=0;}
    bool Busy()const{return pending!=0;}
    bool CanTestHaptics(int category)const{return open&&ready&&!Busy()&&page==Page::Haptics&&category>=0&&category<haptics::CategoryCount;}
    void Open(const Snapshot& s){open=true;controllerMenu=false;page=Page::Controls;base=draft=s.values;ready=s.ready;result=s.result;serial=std::max(serial,s.sequence);pending=s.result==Result::Saving?s.sequence:0;++revision;}
    void Close(){open=false;controllerMenu=false;++revision;}
    bool DismissControllerMenu(){if(!controllerMenu)return false;controllerMenu=false;++revision;return true;}
    void Observe(const Snapshot& s){
        if(!open)return;
        if(pending){
            if(s.sequence==pending&&s.result!=Result::Saving){
                pending=0;result=s.result;ready=s.ready;
                if(s.result==Result::Saved||s.result==Result::Conflict)base=draft=s.values;
                ++revision;
            }
        }else if(!Dirty()&&(Changed(base,s.values)||ready!=s.ready)){
            base=draft=s.values;ready=s.ready;++revision;
        }
    }
    Request Click(Hit hit){
        if(!open)return {};
        if(hit.kind==Kind::Close){Close();return {};}
        if(hit.kind==Kind::DismissMenu){DismissControllerMenu();return {};}
        if(hit.kind==Kind::Tab&&hit.index>=0&&hit.index<TabCount){controllerMenu=false;page=Page(hit.index);++revision;return {};}
        if(!ready||Busy())return {};
        if(hit.kind==Kind::ControllerMenu&&page==Page::VR){controllerMenu=!controllerMenu;++revision;return {};}
        if(hit.kind==Kind::ControllerChoice&&page==Page::VR&&controllerMenu&&hit.index>=0&&hit.index<int(controllers::Choice::Count)){
            draft[ControllerLabels]=float(hit.index);controllerMenu=false;result=Result::None;++revision;return {};
        }
        if(hit.kind==Kind::Change&&hit.index>=0&&hit.index<Count){
            const auto key=Key(hit.index);const auto& d=Definitions[key];
            if(d.max==1)draft[key]=draft[key]>.5f?0.f:1.f;
            else if(key==ProjectileAimAssist)draft[key]=float((int(draft[key])+(hit.delta<0?2:1))%3);
            else draft[key]=Normalize(key,draft[key]+hit.delta*d.step);
            result=Result::None;++revision;
        }
        if(hit.kind==Kind::Apply&&Dirty()){
            pending=++serial;result=Result::Saving;++revision;
            return {base,draft,Changed(base,draft),pending};
        }
        return {};
    }
};
// Tracking loss and opening/closing consume existing holds. The menu gets a
// single click per physical trigger press, even when the laser crosses rows.
class InputGate {
    bool primed=false,chordWas=false,triggerWas=false,cancelWas=false;
public:
    struct Edges {bool chord=false,click=false,cancel=false;};
    void Suspend(){primed=false;}
    void Consume(){triggerWas=true;cancelWas=true;}
    Edges Step(bool tracked,bool chord,float trigger,bool cancel){
        const bool down=trigger>(triggerWas?.20f:.55f);
        if(!tracked){Suspend();return {};}
        if(!primed){chordWas=chord;triggerWas=down;cancelWas=cancel;primed=true;return {};}
        Edges e{chord&&!chordWas,down&&!triggerWas,cancel&&!cancelWas};
        chordWas=chord;triggerWas=down;cancelWas=cancel;return e;
    }
};
}
