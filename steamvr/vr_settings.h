#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include "controller_labels.h"
#include "enemy_sight.h"
#include "hand_vitals.h"
#include "vr_resolution.h"
#include "haptic_strength.h"
#include "frame_generation.h"

namespace framevr::settings {
// The menu's views, in rail order (names, icons, sections and rows:
// vr_settings_content.h). Presets heads the rail; Guide and WhatsNew open from
// the page band, not the rail; More (rows no override placed yet, the menu data
// rules) too, and only while it has rows.
enum class View : uint8_t { Presets, Body, Combat, Aim, Targets, Shield, Wrist, Interface, Survival, World, Vibration, Performance, Guide, WhatsNew, More, Count };
enum Key { Motion, Seated, Dominant, Snap, TurnSpeed, Waist, BeltForward,
           Foveation, Sky, Fog, Ceiling, DrawDistance, ObjectDistance,
           EyeHeight, ArmSpan, BodyCalibration, SkillGestures, WristMap, WristCompass, ControllerLabels, WeaponSkillIndicators, PlanarSpellAim, VitalsLocation, CriticalAlerts, RenderResolution, NavigationLocation, SmoothTurn, OrdinaryMobBars, MenuDistance, TooltipTiming, ProjectileAimAssist, MeleeHitSpeed, VitalsTilt, HapticStrength, HapticMelee, HapticBowDraw, HapticBowRelease, HapticSpell, HapticInteraction, HapticAlert, HapticShortMelee, BloodVisor, BloodWipe, MeleeThrustSpeed, LootLabelSize, CompanionCounts, StrainVignette, OfflinePlayers, ShieldBashSpeed, DesktopOneEye, MapTopDown, MapRotate, FoveationStrength, StereoRendering, DeferGpuWait, TargetHighlight, HeartbeatStart, SpellLock, StaffPoint, LandingCircles, GroundAim, AimHighlights, HighlightRight, HighlightLeft, ElementalHands, EnemySight, BowLaser, ThrowLift, BottlePhysics, PropPhysics, CompanionAi, ShieldGrip, ShieldOut, ShieldUp, ShieldFollow, ShieldRoll, AimFollowsView, LeftHandStrikes, TorchFlame, FrameGeneration, FrameGenPreset, FrameGenScale, Count };
// One bit per setting, 128 wide: two words, since MSVC has no 128-bit integer.
struct ChangeMask {
    static constexpr int Bits=128;
    uint64_t low=0,high=0;
    constexpr ChangeMask(uint64_t bits=0):low(bits){}
    constexpr ChangeMask(uint64_t lowBits,uint64_t highBits):low(lowBits),high(highBits){}
    constexpr explicit operator bool()const{return (low|high)!=0;}
    constexpr bool operator==(const ChangeMask&)const=default;
    constexpr ChangeMask operator~()const{return {~low,~high};}
    constexpr ChangeMask operator&(ChangeMask o)const{return {low&o.low,high&o.high};}
    constexpr ChangeMask operator|(ChangeMask o)const{return {low|o.low,high|o.high};}
    constexpr ChangeMask& operator|=(ChangeMask o){low|=o.low;high|=o.high;return *this;}
    constexpr ChangeMask operator<<(int n)const{return n<=0?*this:n>=Bits?ChangeMask{}:n>=64?ChangeMask{0,low<<(n-64)}:ChangeMask{low<<n,high<<n|low>>(64-n)};}
    constexpr ChangeMask operator>>(int n)const{return n<=0?*this:n>=Bits?ChangeMask{}:n>=64?ChangeMask{high>>(n-64),0}:ChangeMask{low>>n|high<<(64-n),high>>n};}
};
static_assert(Count<=ChangeMask::Bits,"settings change mask capacity");
constexpr ChangeMask SupportedChanges=~ChangeMask{0}>>(ChangeMask::Bits-Count);
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
    {"Added fog", "The mod's distance fog effect", L"fog",L"enabled",0,0,1,1},
    {"Dungeon ceiling", "The mod's overhead dungeon surfaces", L"ceiling",L"enabled",0,0,1,1},
    {"Terrain draw distance", "Higher values show more map tiles and cost more to render", L"render",L"rings",2,1,8,1},
    // 210: just past the fog's end (200). In Rogue Encampment 360 held 45 fps, 260 55-80, 210 88-90 (steamvr/PERFORMANCE.md).
    {"Object draw distance", "Higher values retain more distant scenery", L"render",L"model_radius",210,10,10000,50},
    {"Standing eye height", "Measured from the SteamVR floor", L"calibration",L"eye_height_m",1.64f,.8f,2.3f,.01f},
    {"Controller span", "Measured between grips in a T-pose", L"calibration",L"arm_span_m",1.6f,.85f,2.6f,.01f},
    {"Body calibration", "Saved body calibration version", L"calibration",L"version",0,0,1,1},
    {"Weapon skill gestures", "Compatible trigger skills power strikes, shield bash and bow release", L"gameplay",L"skill_gestures",1,0,1,1},
    {"Wrist minimap", "Show above wrist health and mana; the wheel MAP tab stays available", L"interactions",L"wrist_map",1,0,1,1},
    {"Wrist compass", "Left wrist bearings; N follows the top of the minimap", L"interactions",L"wrist_compass",1,0,1,1},
    {"Headset / controller labels", "Automatic uses the active controllers; override if labels do not match", L"interactions",L"controller_labels",0,0,4,1},
    {"Weapon skill indicators", "Show skill icons and names beside both weapons", L"interactions",L"weapon_skill_indicators",0,0,1,1},
    {"Planar spell aiming", "Keep straight projectiles level; Off allows aiming up/down", L"gameplay",L"planar_spell_aim",0,0,1,1},
    {"Health / mana location", "Wrist, palm-up, either gesture, or hidden", L"interactions",L"vitals_location",0,0,3,1},
    {"Low health / mana alerts", "Brief popup and vibration when resources are low", L"interactions",L"critical_alerts",0,0,1,1},
    {"VR render resolution", "Independent of monitor; higher values cost GPU time", L"render",L"vr_resolution",1,0,6,1},
    {"Minimap & Compass", "Wrist / Palm / Either / Off; independent of health and mana", L"interactions",L"navigation_location",0,0,3,1},
    {"Smooth turning", "Continuous right-stick turning; Off uses snap turning", L"turn",L"smooth",0,0,1,1},
    {"Ordinary mob bars", "Weak mobs' health and names; bosses and elites stay visible", L"hud",L"ordinary_mob_bars",0,0,1,1},
    {"Menu distance", "Eye-level inventory, merchant and main menus", L"hud",L"menu_distance",1,0,2,1},
    {"Item tooltip timing", "Laser hover: Instant, Short (300 ms), or Original timing", L"hud",L"tooltip_timing",0,0,2,1},
    {"Projectile aim assist", "Hand spells: Light assists launch; Strong adds gentle homing", L"gameplay",L"projectile_aim_assist",2,0,2,1},
    {"Melee hit speed", "Higher requires faster weapon swings; shield bash has its own setting", L"gameplay",L"melee_hit_speed",12,1.5f,std::numeric_limits<float>::max(),.25f},
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
    {"Melee thrust speed", "Forward stabs along the blade; independent of swing speed", L"gameplay",L"melee_thrust_speed",2,.75f,10,.25f},
    {"Loot label size", "Dropped-item names; 45% is the original size", L"hud",L"labels_size",45,20,150,5},
    {"Companions & effects", "Companion counts and current buffs, auras and harmful effects", L"interactions",L"companion_counts",1,0,1,1},
    {"Guard strain vignette", "Blocks darken the rim of the view; the rarer the attacker, the faster", L"gameplay",L"guard_strain_vignette",1,0,1,1},
    {"Offline difficulty", "Players 1-8 scaling for newly spawned monsters", L"gameplay",L"offline_players",0,0,8,1},
    {"Shield bash speed", "Hand speed for bash / Smite; wrist twisting never attacks", L"gameplay",L"shield_bash_speed",2,.75f,10,.25f},
    {"Desktop shows one eye", "Monitor shows the left eye only; one extra GPU copy per frame", L"render",L"desktop_one_eye",1,0,1,1},
    // 0 Isometric, 1 Top-down (the corner map, as before); 2 Full map, 3 Full top-down: the game's full map, zoomed from its widest (wrist_full_map.h)
    {"Map view", "The minimap Isometric or Top-down, or the game's full map", L"interactions",L"map_top_down",0,0,3,1},
    {"Rotate map", "Top-down only: keep your facing direction at the top", L"interactions",L"map_rotate",0,0,1,1},
    {"Foveation strength", "Higher levels shade a larger edge area more coarsely", L"render",L"foveation_strength",1,0,3,1},
    {"Stereo rendering", "Both eyes: each frame twice; By turns: one eye a frame", L"stereo",L"rendering",1,0,1,1},
    {"Defer GPU wait", "Start the next frame while the GPU finishes the last", L"render",L"defer_gpu_wait",0,0,1,1},
    {"Monster outline", "Outline strength for the monster you aim at; Off by default", L"gameplay",L"target_outline_pct",0,0,100,10},
    {"Heartbeat starts at", "Life % where the heartbeat begins; louder and faster every 10% lower", L"gameplay",L"low_life_heart_start",60,0,90,10},
    {"Skills go to the aimed monster", "The hand's monster, else the other hand's, else where you look", L"gameplay",L"spell_lock",0,0,3,1},
    {"Staff aims where your arm points", "Upright: along your arm. Leveled out: from the staff's top", L"gameplay",L"staff_point",0,0,1,1},
    {"Landing circles", "Where and when a ground skill will hit, in its element's color", L"gameplay",L"landing_circles",1,0,1,1},
    {"Ground skills", "The game's own aim, the floor you point at, or the aimed monster", L"gameplay",L"ground_aim",0,0,2,1},
    {"Monster highlights", "Dominant hand: its monster only. Both hands: each hand's own", L"gameplay",L"aim_highlights",1,0,1,1},
    {"Right hand color", "The right hand's monster: By range, or one color", L"gameplay",L"highlight_right_hue",45,0,360,15},
    {"Left hand color", "The left hand's monster: By range, or one color", L"gameplay",L"highlight_left_hue",210,0,360,15},
    {"Elemental hand effects", "Sorceress: fire, frost and lightning around each hand (experimental)", L"interactions",L"elemental_hands",0,0,1,1},
    {"Enemy sight distance", "Offline monster detection range; 100% is game default", L"gameplay",L"enemy_sight_pct",enemysight::DefaultPercent,enemysight::MinPercent,enemysight::MaxPercent,enemysight::StepPercent},
    // Off (LaserOff, below), else the line's hue; steps wrap through Off.
    {"Bow path laser", "A faint line along a drawn arrow's flight: Off or its color", L"bow",L"path_laser_hue",-15,-15,345,15},
    // Degrees above the throwing hand's path; a release follows through low.
    {"Throw lift", "Thrown weapons leave above the hand's path; - lowers them", L"gameplay",L"throw_lift_deg",10,-10,30,5},
    {"Bottle physics", "Emptied bottles roll; thrown potions and traps stop at walls", L"interactions",L"bottle_physics",1,0,1,1},
    {"Knockable props", "Weapons and hands knock the level's cups, plates and vases about", L"interactions",L"prop_physics",1,0,1,1},
    {"Enhanced VR Mercenary", "Tactical positioning, dynamic detection range, elastic leash and anti-stuck watchdog", L"gameplay",L"companion_ai",1,0,1,1},
    {"Shield grip offset", "Slides the shield along the forearm so the hand reaches its grip", L"gameplay",L"shield_grip_cm",0,-10,40,1},
    {"Shield out from the arm", "Moves the shield away from the forearm (+) or into it (-)", L"gameplay",L"shield_out_cm",0,-30,30,1},
    {"Shield up", "Moves the shield across the forearm, square to Out", L"gameplay",L"shield_up_cm",0,-30,30,1},
    {"Shield follows", "0% strapped to the forearm; 100% turns with the wrist", L"gameplay",L"shield_follow_pct",50,0,100,10},
    {"Shield roll", "Turns the shield around the forearm; + toward the little finger", L"gameplay",L"shield_roll_deg",0,-180,180,5},
    // The eyes' 17-degree tunnel, the hands' 30-degree wedge, a raised hand alone (targetcone, aimrules::Tries).
    {"Aim follows your view", "Only monsters ahead of you; a raised hand aims alone", L"gameplay",L"aim_follows_view",0,0,1,1},
    // Melee skill strikes (strike_skills.h): which skill the left claw's strikes cast.
    // Set from the skill wheel's lower-left box (Druid), not a VR Settings row.
    {"Left-hand strikes", "Druid: the left claw casts its own trigger skill or the right trigger's", L"gameplay",L"left_hand_strikes",0,0,1,1},
    {"Torch flame", "The flame on a torch club's head; its light stays either way", L"torch",L"flame",1,0,1,1},
    // OFXR Bridge (frame_generation.h): 0 Off, 1 FidelityFX, 2 NVIDIA; on or off at the next start.
    {"Frame generation", "Experimental, with Both eyes; on or off takes a restart", L"render",L"frame_generation",0,0,2,1},
    // NVIDIA's optical flow, live: 0 Slow, 1 Medium, 2 Fast; its scale 0 100%, 1 75%, 2 50%.
    {"NVIDIA flow quality", "Slow, Medium or Fast optical flow", L"render",L"frame_generation_preset",1,0,2,1},
    {"NVIDIA flow scale", "The size the optical flow works at", L"render",L"frame_generation_scale",2,0,2,1},
}};
using Values=std::array<float,Count>;
inline bool HapticKey(Key key){return key>=HapticStrength&&key<=HapticAlert;}
// An outline's color per source (right hand, left hand; the eyes take the hand that would cast): 0 By range, else its hue.
inline bool HighlightKey(Key key){return key==HighlightRight||key==HighlightLeft;}
// Before the per-hand colors (v0.3.21-v0.3.24) one outline color served every
// monster: [gameplay] target_outline_by_range (1 By range) and target_outline_hue
// (0-345; -1 when a key is missing). Until a hand's own color is saved they keep
// the player's choice: By range for both hands, else the old hue as the right
// hand's color (the old red 0 becomes 360, since 0 now means By range).
inline float LegacyHighlight(Key key,float byRange,float hue){
    if(byRange>.5f)return 0;
    if(key==HighlightRight&&std::isfinite(hue)&&hue>=0){const float snapped=std::round(hue/15)*15;return snapped<=0?360.f:std::min(snapped,360.f);}
    return Definitions[key].defaultValue;
}
inline int HapticCategory(Key key){return key>=HapticMelee&&key<=HapticAlert?key-HapticMelee:-1;}
inline haptics::Sample HapticSample(const Values& values,haptics::Category category,int hand=0){return haptics::SampleFor(category,values[HapticShortMelee]>.5f,hand);}
inline haptics::Strengths HapticLevels(const Values& values){
    haptics::Strengths levels;levels.master=values[HapticStrength];
    static_assert(HapticAlert-HapticMelee+1==haptics::CategoryCount);
    for(int i=0;i<haptics::CategoryCount;++i)levels.category[i]=values[HapticMelee+i];
    return levels;
}
inline Values Defaults(){Values v{};for(int i=0;i<Count;++i)v[i]=Definitions[i].defaultValue;return v;}
// Bow path laser: its hue, or Off as any negative value (saved as -15).
inline constexpr float LaserOff=-15;
inline bool LaserOn(const Values& values){return values[BowLaser]>=0;}
inline float Normalize(Key key,float value){
    const auto& d=Definitions[key];
    if(!std::isfinite(value))return d.defaultValue;
    if(key==OfflinePlayers&&(value<0||value>8||std::floor(value)!=value))return 0;
    if(key==BowLaser&&value<0)return LaserOff;
    if(key==LeftHandStrikes)return std::round(value)==1?1.f:0.f;   // the retired 2 (Basic attack) reads Own trigger skill
    if(key==HeartbeatStart)return std::clamp(std::round(value/10)*10,d.min,d.max);   // 0 = Off, else a 10% step
    if(key==ShieldFollow||key==ShieldRoll)return std::clamp(std::round(value/d.step)*d.step,d.min,d.max)+0.f;   // whole steps, as skel::shield::FollowSetting / RollSetting
    if(HighlightKey(key))return std::clamp(std::round(value/15)*15,d.min,d.max);   // 0 = By range, else a hue in 15-degree steps
    if(key==EnemySight)return float(enemysight::NormalizePercent(value));
    if(key==VitalsTilt)return vitals::NormalizeTilt(value);
    if(HapticKey(key))return haptics::NormalizeStrength(value);
    if((key==ControllerLabels||key==VitalsLocation||key==NavigationLocation||key==RenderResolution||key==MenuDistance||key==TooltipTiming||key==ProjectileAimAssist||key==GroundAim||key==SpellLock||key==FoveationStrength||key==StereoRendering||key==FrameGeneration||key==FrameGenPreset||key==FrameGenScale)&&(value<d.min||value>d.max||value!=std::round(value)))return d.defaultValue;
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
struct Snapshot {Values values=Defaults();bool ready=false,offlineDifficultyAvailable=false;uint64_t sequence=0,revision=0;Result result=Result::None;};
struct Request {Values base{},values{};ChangeMask changed=0;uint64_t sequence=0;};
struct Rect {
    int x,y,w,h;
    bool Contains(float px,float py)const{return px>=x&&px<x+w&&py>=y&&py<y+h;}
    bool operator==(const Rect&)const=default;
};
constexpr int Width=1280,Height=1024;
// Map view (map_top_down): 0 Isometric, 1 Top-down, 2 Full map, 3 Full top-down
// (the names: content::MapViewOptions).
inline bool MapViewTopDown(float view){return view==1||view==3;}
inline bool MapViewFull(float view){return view>=2;}
// The outline color for a hue, at full saturation and brightness.
inline void HueRgb(float hue,float rgb[3]){
    const float h=std::fmod(std::max(0.f,hue),360.f)/60.f,x=1.f-std::fabs(std::fmod(h,2.f)-1.f);
    const float table[6][3]={{1,x,0},{x,1,0},{0,1,x},{0,x,1},{x,0,1},{1,0,x}};
    for(int i=0;i<3;++i)rgb[i]=table[std::min(5,int(h))][i];
}
// Stereo rendering: Both eyes (0) or By turns (1). Replay (one CPU pass for
// both eyes) left the choices on 2026-10-20: on the owner's PC it halved the
// CPU work a frame but not the frame time - the right eye's GPU work waits for
// the whole left pass - so it stayed at 45 Hz like Both eyes, while By turns
// ran 74-90 (steamvr/PERFORMANCE.md). A profile saved with it (2) is out of
// range and goes by turns (Normalize).
// Every stereo rendering choice ends each frame waiting for the GPU unless
// the wait is deferred ([render] defer_gpu_wait): then about half the frame
// rate. While it is not deferred, the Performance readout warns and offers a
// button that turns it on (Apply saves it).
// [gameplay] left_hand_strikes: 0 the left trigger's skill, 1 the right trigger's,
// set from the skill wheel's Left claw strikes box, not a VR Settings row.
// (2, Basic attack, left the choices on 2026-10-23: Own trigger with no strike
// skill on the left trigger is the same; a profile saved with it reads Own.)
// Windows Mono audio (Settings > Accessibility > Audio) folds the PC's sound to
// the center before any headset stream, so head-relative sound cannot pan.
// While it is on, the band shows this warning on every page.
inline constexpr const char* MonoAudioWarning="Windows Mono audio is on";
inline bool DeferOffered(const Values& values){return values[DeferGpuWait]<.5f;}
inline constexpr const char* DeferWarning="Each frame waits for the GPU: about half the frame rate";
// The game's own video options that cost the headset frames, from its
// Settings.json (Saved Games\Diablo II Resurrected, written when its options
// close): Vertical Sync holds each frame to the monitor's refresh, and
// anti-aliasing other than FXAA ("Anti Aliasing": 1) blurs (TAA builds each
// frame from the ones before it) or costs more. The Performance page warns about
// each; a key the file lacks is not warned about. It also shows the options
// that set how much the game draws (-1: not in the file), beside the frame
// times they change.
struct GameVideo {
    bool vsync=false,antiAliasing=false;
    int shadows=-1,characterDetail=-1,ambientOcclusion=-1,atmospherics=-1,lights=-1,resolutionScale=-1,dlss=-1;
    int Code()const{return int(vsync)|int(antiAliasing)<<1;}
    bool operator==(const GameVideo&)const=default;
};
// "key": integer in the game's JSON; -1 if absent or not a number.
inline int GameSetting(std::string_view json,std::string_view key) {
    for(size_t at=json.find(key);at!=std::string_view::npos;at=json.find(key,at+1)){
        size_t i=at+key.size();
        if(at==0||json[at-1]!='"'||i>=json.size()||json[i]!='"')continue;
        const auto blank=[&]{while(i<json.size()&&(json[i]==' '||json[i]=='\t'||json[i]=='\r'||json[i]=='\n'))++i;};
        ++i;blank();
        if(i>=json.size()||json[i]!=':')continue;
        ++i;blank();
        if(i>=json.size()||json[i]<'0'||json[i]>'9')return -1;
        int value=0;
        while(i<json.size()&&json[i]>='0'&&json[i]<='9'&&value<100000)value=value*10+(json[i++]-'0');
        return value;
    }
    return -1;
}
inline GameVideo ParseGameVideo(std::string_view json) {
    const int antiAliasing=GameSetting(json,"Anti Aliasing");
    GameVideo game;game.vsync=GameSetting(json,"VSync")>0;game.antiAliasing=antiAliasing>=0&&antiAliasing!=1;
    game.shadows=GameSetting(json,"Shadow Quality");game.characterDetail=GameSetting(json,"Character Detail");
    game.ambientOcclusion=GameSetting(json,"Ambient Occlusion Quality");game.atmospherics=GameSetting(json,"Atmospherics Quality");
    game.lights=GameSetting(json,"Light Quality");
    game.resolutionScale=GameSetting(json,"Resolution Scale");game.dlss=GameSetting(json,"NVIDIA DLSS");
    return game;
}
// The game's options above as one line for the Performance page; empty when the file
// has none of them. The numbers are the game's own, 0 its lowest.
inline std::string GameVideoLine(const GameVideo& g) {
    std::string line;
    const auto add=[&](const char* name,int value,const char* unit=""){
        if(value<0)return;
        char part[64];std::snprintf(part,sizeof part,"%s%s %d%s",line.empty()?"":" \xc2\xb7 ",name,value,unit);line+=part;
    };
    // The CPU's first (what is drawn), then the graphics card's.
    add("Shadows",g.shadows);add("Character detail",g.characterDetail);add("Lights",g.lights);
    add("AO",g.ambientOcclusion);add("Atmospherics",g.atmospherics);add("Scale",g.resolutionScale,"%");
    if(g.dlss>=0)line+=std::string(line.empty()?"":" \xc2\xb7 ")+(g.dlss>0?"DLSS on":"DLSS off");
    return line.empty()?line:"Game: "+line;
}
// What the Performance page reports of the last second's frames, as the render
// thread measured them: the game's update between frames, the wait for the
// headset (xrWaitFrame), the game's drawing (one eye a frame by turns, both
// with Both eyes) and the mod's own work around it (input, copies, submit).
// budgetMs is the headset's frame.
struct PerformanceReadout {
    bool valid=false,byTurns=true;
    float fps=0,budgetMs=0,gameMs=0,waitMs=0,drawMs=0,vrMs=0;
    bool mixed=false;   // some frames by turns, some pairs
    float CpuMs()const{return gameMs+drawMs+vrMs;}
    bool operator==(const PerformanceReadout&)const=default;
};
inline std::string PerformanceLine(const PerformanceReadout& r) {
    if(!r.valid)return "Now: waiting for a second of headset frames in the game";
    char line[200];
    std::snprintf(line,sizeof line,"Now: %.0f fps \xc2\xb7 drawing %.1f ms (%s) \xc2\xb7 game %.1f \xc2\xb7 VR %.1f \xc2\xb7 headset wait %.1f",
        double(r.fps),double(r.drawMs),r.mixed?"mixed":r.byTurns?"one eye":"both eyes",double(r.gameMs),double(r.vrMs),double(r.waitMs));
    return line;
}
inline constexpr const char* GameVideoUnsaved="Game: options not saved yet; the game writes them when its options menu closes";
// Whether the CPU's part fits the headset's frame: past it, the runtime
// holds every late frame back to its next refresh.
inline std::string PerformanceVerdict(const PerformanceReadout& r) {
    if(!r.valid||r.budgetMs<=0)return {};
    char line[200];
    if(r.CpuMs()>r.budgetMs)std::snprintf(line,sizeof line,"CPU %.1f ms of the headset's %.1f ms frame: draw less (draw distances, shadows); resolution won't help",double(r.CpuMs()),double(r.budgetMs));
    else std::snprintf(line,sizeof line,"CPU %.1f ms of the headset's %.1f ms frame: it fits",double(r.CpuMs()),double(r.budgetMs));
    return line;
}
// Launched with -mod <mod>, the game keeps that mod's own Settings.json in
// Saved Games\Diablo II Resurrected\mods\<mod>, not the unmodded game's one
// two levels up. <mod> is the folder the plugin loads from
// (<game>\mods\<mod>\d2rloader\plugins\); empty when the plugin is elsewhere.
inline std::wstring ModSettingsFile(std::wstring_view savedGames,std::wstring_view plugin) {
    std::wstring lower(plugin);
    for(auto& c:lower)if(c==L'/')c=L'\\';else if(c>=L'A'&&c<=L'Z')c=wchar_t(c-L'A'+L'a');
    const size_t at=lower.rfind(L"\\mods\\");
    if(at==std::wstring::npos||savedGames.empty())return {};
    const size_t start=at+6,end=lower.find(L'\\',start);
    if(end==std::wstring::npos||end==start||lower.compare(end,19,L"\\d2rloader\\plugins\\")!=0)return {};
    return std::wstring(savedGames)+L"\\Diablo II Resurrected\\mods\\"+std::wstring(plugin.substr(start,end-start))+L"\\Settings.json";
}
inline const char* GameVideoWarning(GameVideo game) {
    return game.vsync&&game.antiAliasing?"Game Options > Video: turn Vertical Sync off and set Anti-Aliasing to FXAA":
           game.vsync?"Game Options > Video: turn Vertical Sync off":
           game.antiAliasing?"Game Options > Video: set Anti-Aliasing to FXAA":nullptr;
}
// Strength only applies while foveated rendering is selected; Off greys it out.
inline bool RowEnabled(const Values& values,Key key){
    if(key==DeferGpuWait)return DeferOffered(values);   // the button only turns it on
    if(key==AimHighlights||HighlightKey(key))return values[TargetHighlight]>0;   // only while the monster outline shows
    if(key==LandingCircles)return values[GroundAim]>.5f;   // only while the mod places ground skills
    // Frame generation runs with Both eyes only (frame_generation.h): turned on
    // only then; once on it can still be changed or turned off.
    if(key==FrameGeneration)return values[StereoRendering]<.5f||values[FrameGeneration]>.5f;
    return key!=FoveationStrength||values[Foveation]>.5f;
}
// The character the panel is open for: rows for another class, or for a
// weapon not in hand, are left out. An unknown class (-1: the title and
// character screens) shows every row.
namespace heroclass {enum {Amazon=0,Sorceress=1,Necromancer=2,Paladin=3,Barbarian=4,Druid=5,Assassin=6};}
struct Context {int heroClass=-1;bool bow=false,staff=false,thrown=false;bool operator==(const Context&)const=default;};
inline bool ClassIs(const Context& c,int heroClass){return c.heroClass<0||c.heroClass==heroClass;}
// Bow skills are the Amazon's.
inline bool BowSkillClass(const Context& c){return ClassIs(c,heroclass::Amazon);}
// The classes with ground skills (aimrules: Fire Blast and traps, Blizzard,
// Meteor, Fire Wall, Hydra, Fissure, Volcano, Bone Wall and Bone Prison).
inline bool GroundSkillClass(const Context& c){
    return c.heroClass<0||c.heroClass==heroclass::Sorceress||c.heroClass==heroclass::Assassin||
           c.heroClass==heroclass::Druid||c.heroClass==heroclass::Necromancer;
}
// Something takes the aimed monster: the outline, Skills go to the aimed monster,
// or Ground skills on Aimed monster (the target lock's scan runs: mobaim::Wanted).
inline bool AimedMonsterUsed(const Values& v){return v[TargetHighlight]>0||v[SpellLock]>0||v[GroundAim]>1.5f;}
// Whether a row is for this character at all (its class, or the weapon in
// hand); RowShown adds the rows that depend on another row's choice. Presets
// skip the keys this leaves out.
inline bool RowForClass(Key key,const Context& c){
    switch(key){
        case GroundAim:case LandingCircles:return GroundSkillClass(c);
        case BowLaser:return c.heroClass<0||c.heroClass==heroclass::Amazon||c.bow;
        case ThrowLift:return c.heroClass<0||c.heroClass==heroclass::Amazon||c.heroClass==heroclass::Barbarian||c.heroClass==heroclass::Assassin||c.thrown;
        case StaffPoint:return c.heroClass<0||c.staff||c.heroClass==heroclass::Sorceress||c.heroClass==heroclass::Druid;
        case ElementalHands:return ClassIs(c,heroclass::Sorceress);
        default:return true;
    }
}
// A row that cannot apply is left out rather than grayed: one that only
// matters with another row's choice, or for another class or weapon.
// Each hand's color has its own row (HighlightLeft, HighlightRight).
inline bool RowShown(Key key,const Values& v,const Context& c){
    if(!RowForClass(key,c))return false;
    switch(key){
        case AimHighlights:case HighlightLeft:case HighlightRight:return v[TargetHighlight]>0;   // the outline's own rows
        case AimFollowsView:return AimedMonsterUsed(v);
        case LandingCircles:return v[GroundAim]>.5f;     // ground skills the mod places
        case FoveationStrength:return v[Foveation]>.5f;
        case Snap:return !SmoothTurning(v[Snap],v[SmoothTurn]);
        case TurnSpeed:return SmoothTurning(v[Snap],v[SmoothTurn]);
        case Waist:return v[Seated]<.5f;   // standing mode only
        case BloodWipe:return v[BloodVisor]>.5f;
        case FrameGenPreset:case FrameGenScale:return framegen::Read(v[FrameGeneration],0,0).mode==framegen::Mode::Nvidia;   // with its scale
        default:return true;
    }
}
// Skills go to the aimed monster, spell_lock bits: 1 spells, 2 bow skills.
inline bool LockSpells(float value){return (int(value)&1)!=0;}
inline bool LockBowSkills(float value){return (int(value)&2)!=0;}
// Off, Spells, Bow skills, Both. A class without bow skills steps its spells
// only (On / Off), keeping the bow skills' half for its Amazon.
inline float LockStep(float value,int delta,const Context& c){
    const int v=int(value)&3;
    return float(BowSkillClass(c)?(v+(delta<0?3:1))%4:v^1);
}
// What a press on a control does. layout::Press (vr_settings_layout.h)
// handles most and passes Change's steps, Apply, Close's plain case and
// NotesPage to Model::Click.
//   Change       index Key, delta steps (-1/+1 from a stepper, n from a drag)
//   TestHaptics  index haptics category, delta preview choice + 1 ("Feel it")
//   NotesPage    What's new: delta -1 Previous / +1 Next page (Model::Click; works before ready)
//   Open         index View: a rail tab, Presets, Guide, What's new, a card's
//                "Open <page>" or a pending-list entry; delta: the Key to point
//                the card at there, or -1
//   Set          index Key, value: the new ini value (chip, toggle, swatch,
//                hue grid, Defer GPU wait's one-way offer)
//   Undo         index Key: back to the applied value (Model::Applied)
//   Default      index Key: back to Definitions[key].defaultValue
//   Preset       index: entry in content::PresetSpecs (Menu().presets); sets only its listed keys
//   ResetPage    every row the current view shows, to its default (a draft)
//   UndoReset    within UndoResetMs: the values from before Reset page
//   ConfirmClose the confirm row's "Discard n changes": drop the draft and close
//   KeepEditing  the confirm row's middle button: back to the page
//   ApplyClose   the confirm row's right button: Apply, then close once saved
//   Card         inside the help card but on none of its buttons: swallows the press
//   Popover      index Key: open its list (a named list too long for chips) on the press's release
//   PopoverClose the dimmed panel around an open popover: close it without a change (the press goes no further)
enum class Kind { None, Change, Apply, Close, Recenter, Calibrate, TestHaptics, NotesPage,
                  Open, Set, Undo, Default, Preset, ResetPage, UndoReset, ConfirmClose, KeepEditing, ApplyClose, Card,
                  Popover, PopoverClose };
// What's new pages its release lines (2026-10-23: a long list since the last
// release): NotesPerPage a page, at most NotesMaxLines (release_notes.py
// MAX_LINES). Previous / Next and the page number sit under the lines, shown
// only with more than one page; paging needs no settings (works before ready).
constexpr int NotesPerPage=12,NotesMaxLines=72;
inline int NotesPages(int lines){return std::max(1,(std::max(lines,0)+NotesPerPage-1)/NotesPerPage);}
struct Hit {Kind kind=Kind::None;int index=-1,delta=0;float value=0;bool operator==(const Hit&)const=default;};
class Model {
    Values base=Defaults(),draft=base;
    uint64_t serial=0,pending=0;
public:
    bool open=false,ready=false,offlineDifficultyAvailable=false;
    int notesPage=0,notesPages=1;   // What's new's page, of SetNotesPages
    int morePage=0;                 // the More view's page (8 rows a page; layout::Press turns it)
    Context context;   // the character: SetContext each frame while open
    Result result=Result::None;
    uint64_t revision=0;
    const Values& Get()const{return draft;}
    // ---- the menu's state (layout::Press drives it) ----
    View view=View::Guide;          // the view shown; the panel opens on the Controller guide, as main opens on Controls
    bool confirmClose=false;        // Close with a draft turned the footer into Discard / Keep editing / Apply and close
    uint64_t confirmAt=0;           // when it did (ms): the row ignores presses for ConfirmGuardMs
    uint64_t undoResetUntil=0;      // Reset page's slot reads Undo reset until then (ms); 0: no undo
    uint64_t undoResetAt=0;         // when Reset page was pressed: Undo reset ignores presses for UndoResetGuardMs
    Values beforeReset{};           // the draft from before Reset page, for Undo reset
    ChangeMask resetKeys=0;         // the rows Reset page changed (Undo reset puts back only these)
    View lastPage=View::Body;       // the settings page Guide / What's new return to when pressed again
    int focusKey=-1;                // Open's delta: the Key the card should point at on arrival (-1: none); the pointer layer reads and clears it
    int pickedPreset=-1;            // the preset last pressed (content::PresetSpecs index), for "Edited" on Presets
    bool closeWhenSaved=false;      // Apply and close: close once this save lands as Saved
    int popoverKey=-1;              // the Key whose list is open over the page (-1: none); layout::Press opens and closes it
    uint64_t popoverAt=0;           // when it opened (ms): its options take presses PopoverGuardMs later
    static constexpr uint64_t ConfirmGuardMs=300,UndoResetMs=6000,UndoResetGuardMs=400,PopoverGuardMs=300;
    const Values& Applied()const{return base;}   // the saved values the draft started from
    // Drop the draft: every row back to its applied value (Discard n changes).
    void Discard(){if(Dirty()){draft=base;result=Result::None;++revision;}}
    // Calibrate body starts only with no draft (it saves its own measurements).
    bool CanCalibrate()const{return open&&ready&&!Busy()&&!Dirty();}
    // One setting of the draft, as a chip, toggle, Undo or Default sets it.
    // False when nothing changed or the panel cannot edit (closed, not ready, saving).
    bool Set(Key key,float value){
        if(!open||!ready||Busy()||key<0||key>=Count)return false;
        const float v=Normalize(key,value);
        if(draft[key]==v)return false;
        draft[key]=v;result=Result::None;++revision;return true;
    }
    void SetContext(const Context& c){if(!(c==context)){context=c;++revision;}}
    void SetNotesPages(int pages){pages=std::max(1,pages);if(pages!=notesPages||notesPage>=pages){notesPages=pages;notesPage=std::min(notesPage,pages-1);++revision;}}
    bool Dirty()const{return Changed(base,draft)!=0;}
    bool Busy()const{return pending!=0;}
    // Feel it: on the Vibration view.
    bool CanTestHaptics(int category)const{return open&&ready&&!Busy()&&view==View::Vibration&&category>=0&&category<haptics::CategoryCount;}
    void Open(const Snapshot& s){
        open=true;base=draft=s.values;ready=s.ready;offlineDifficultyAvailable=s.offlineDifficultyAvailable;result=s.result;serial=std::max(serial,s.sequence);pending=s.result==Result::Saving?s.sequence:0;
        view=View::Guide;lastPage=View::Body;notesPage=0;morePage=0;confirmClose=false;confirmAt=0;undoResetUntil=undoResetAt=0;beforeReset={};resetKeys=0;focusKey=-1;pickedPreset=-1;closeWhenSaved=false;popoverKey=-1;popoverAt=0;
        ++revision;
    }
    void Close(){open=false;confirmClose=false;undoResetUntil=0;closeWhenSaved=false;popoverKey=-1;++revision;}
    void Observe(const Snapshot& s){
        if(!open)return;
        if(offlineDifficultyAvailable!=s.offlineDifficultyAvailable){offlineDifficultyAvailable=s.offlineDifficultyAvailable;++revision;}
        if(pending){
            if(s.sequence==pending&&s.result!=Result::Saving){
                pending=0;result=s.result;ready=s.ready;
                if(s.result==Result::Saved||s.result==Result::Conflict)base=draft=s.values;
                ++revision;
                if(closeWhenSaved){closeWhenSaved=false;if(s.result==Result::Saved)Close();}   // Apply and close: a failed save stays open to show why
            }
        }else if(!Dirty()&&(Changed(base,s.values)||ready!=s.ready)){
            base=draft=s.values;ready=s.ready;++revision;
        }
    }
    Request Click(Hit hit){
        if(!open)return {};
        if(hit.kind==Kind::Close){Close();return {};}
        if(hit.kind==Kind::NotesPage&&view==View::WhatsNew){
            const int next=std::clamp(notesPage+(hit.delta<0?-1:1),0,notesPages-1);
            if(next!=notesPage){notesPage=next;++revision;}
            return {};
        }
        if(!ready||Busy())return {};
        if(hit.kind==Kind::Change&&hit.index>=0&&hit.index<Count&&RowEnabled(draft,Key(hit.index))){
            const auto key=Key(hit.index);const auto& d=Definitions[key];
            if(d.max==1)draft[key]=draft[key]>.5f?0.f:1.f;
            else if(key==OfflinePlayers)draft[key]=std::clamp(draft[key]+float(hit.delta),0.f,8.f);
            else if(key==FoveationStrength||key==StereoRendering||key==FrameGeneration||key==FrameGenPreset||key==FrameGenScale)draft[key]=std::clamp(draft[key]+float(hit.delta),d.min,d.max);
            else if(key==SpellLock)draft[key]=LockStep(draft[key],hit.delta,context);
            else if(key==ProjectileAimAssist||key==GroundAim)draft[key]=float((int(draft[key])+(hit.delta<0?2:1))%3);   // cycles
            else if(key==MapTopDown)draft[key]=float((int(draft[key])+(hit.delta<0?3:1))%4);   // Isometric, Top-down, Full map, Full top-down, round
            else if(key==BowLaser){   // round the wheel through Off: ... 345, Off, 0, 15 ... both ways
                const float next=draft[key]+hit.delta*d.step;
                draft[key]=Normalize(key,next>d.max?d.min:next<d.min?d.max:next);
            }
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
// A picture the page painter could not draw (no pixels: the painter is not
// Ready, e.g. its DirectWrite factory or embedded fonts failed). The panel
// would be a laser at nothing, so the first empty picture after Open closes
// the panel and tells the player once (the notice card); no more requests go
// to the painter until the next Open, which tries again.
class PaintGuard {
    bool failed=false;
public:
    void Open(){failed=false;}
    bool CanRequest()const{return !failed;}
    // A finished picture: true the first time one comes back empty since Open.
    bool Failed(bool empty){if(!empty||failed)return false;failed=true;return true;}
};
// The notice card's words for it ("VR Settings could not draw: ..."), short
// enough for the card: a long error is cut at MaxError characters.
inline std::string PaintFailureNotice(std::string_view what,std::string_view error){
    constexpr size_t MaxError=72;
    std::string e(error.empty()?std::string_view("unknown error"):error);
    if(e.size()>MaxError){e.resize(MaxError-3);e+="...";}
    return std::string(what)+" could not draw: "+e;
}
// The settings chord (open; while open it is Close) and Right B (Close) as press edges.
// Tracking loss and opening/closing consume existing holds: a button already
// held then must be released first. Trigger presses on the panel go through
// pointer::PointerFilter.
class InputGate {
    bool primed=false,chordWas=false,cancelWas=false;
public:
    struct Edges {bool chord=false,cancel=false;};
    void Suspend(){primed=false;}
    void Consume(){cancelWas=true;}
    Edges Step(bool tracked,bool chord,bool cancel){
        if(!tracked){Suspend();return {};}
        if(!primed){chordWas=chord;cancelWas=cancel;primed=true;return {};}
        Edges e{chord&&!chordWas,cancel&&!cancelWas};
        chordWas=chord;cancelWas=cancel;return e;
    }
};
}
