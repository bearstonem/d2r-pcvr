#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace framevr::settings {
enum class Page { Controls, VR, Video };
enum Key { Motion, Seated, Dominant, Snap, TurnSpeed, Waist, BeltForward,
           Foveation, Sky, Fog, Ceiling, DrawDistance, ObjectDistance,
           EyeHeight, ArmSpan, BodyCalibration, StereoRender, Count };
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
    {"Turning", "Zero selects smooth turning; other values are snap angles", L"turn",L"snap",0,0,180,15},
    {"Smooth turn speed", "Degrees per second; used when turning is set to Smooth", L"turn",L"speed",120,10,720,30},
    {"Belt drop below eyes", "Standing mode only; smaller values raise the belt", L"interactions",L"waist_below_eyes",.65f,.30f,.95f,.05f},
    {"Belt forward reach", "Distance forward from the estimated torso", L"interactions",L"belt_forward",.26f,.03f,.5f,.025f},
    {"Foveated rendering", "Reduce peripheral shading; missing gaze uses full quality", L"render",L"fixed_foveation",1,0,2,1},
    {"Added sky", "The mod's outdoor sky effect", L"sky",L"enabled",0,0,1,1},
    {"Added fog", "The mod's distance fog effect", L"fog",L"enabled",0,0,1,1},
    {"Dungeon ceiling", "The mod's overhead dungeon surfaces", L"ceiling",L"enabled",0,0,1,1},
    {"Terrain draw distance", "Higher values show more map tiles and cost more to render", L"render",L"rings",2,1,8,1},
    {"Object draw distance", "Higher values retain more distant scenery", L"render",L"model_radius",300,10,10000,50},
    {"Standing eye height", "Measured from the SteamVR floor", L"calibration",L"eye_height_m",1.64f,.8f,2.3f,.01f},
    {"Controller span", "Measured between grips in a T-pose", L"calibration",L"arm_span_m",1.6f,.85f,2.6f,.01f},
    {"Body calibration", "Saved body calibration version", L"calibration",L"version",0,0,1,1},
    {"Stereo rendering", "Experimental single pass: use FXAA, fixed foveation and no extra effects", L"stereo",L"replay_right",0,0,1,1},
}};
using Values=std::array<float,Count>;
inline Values Defaults(){Values v{};for(int i=0;i<Count;++i)v[i]=Definitions[i].defaultValue;return v;}
inline float Normalize(Key key,float value){
    const auto& d=Definitions[key];
    if(!std::isfinite(value))return d.defaultValue;
    value=std::clamp(value,d.min,d.max);
    return d.step==1?std::round(value):value;
}
inline uint32_t Changed(const Values& a,const Values& b){uint32_t mask=0;for(int i=0;i<Count;++i)if(std::fabs(a[i]-b[i])>.0001f)mask|=1u<<i;return mask;}
inline bool Conflict(const Values& current,const Values& base,uint32_t mask){return (Changed(current,base)&mask)!=0;}
enum class Result { None, Saving, Saved, Failed, Conflict };
struct Snapshot {Values values=Defaults();bool ready=false;uint64_t sequence=0,revision=0;Result result=Result::None;};
struct Request {Values base{},values{};uint32_t changed=0;uint64_t sequence=0;};
struct Rect {
    int x,y,w,h;
    bool Contains(float px,float py)const{return px>=x&&px<x+w&&py>=y&&py<y+h;}
};
constexpr int Width=1280,Height=1024;
constexpr Rect Tabs[3]={{40,114,386,60},{447,114,386,60},{854,114,386,60}};
constexpr Rect Apply{965,918,275,68},Close{650,918,275,68},Recenter{40,918,270,68},Calibrate{330,918,300,68};
constexpr int RowY=246,RowStep=82;
inline Rect Decrease(int row){return {865,RowY+row*RowStep,64,52};}
inline Rect Increase(int row){return {1176,RowY+row*RowStep,64,52};}
inline Rect ValueBox(int row){return {942,RowY+row*RowStep,221,52};}
inline int RowCount(Page page){return page==Page::VR?7:page==Page::Video?7:0;}
inline Key RowKey(Page page,int row){return page==Page::Video&&row==6?StereoRender:Key((page==Page::Video?7:0)+row);}
enum class Kind { None, Tab, Change, Apply, Close, Recenter, Calibrate };
struct Hit {Kind kind=Kind::None;int index=-1,delta=0;};
inline Hit HitTest(Page page,float u,float v){
    if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>=1||v<0||v>=1)return {};
    const float x=u*Width,y=v*Height;
    for(int i=0;i<3;++i)if(Tabs[i].Contains(x,y))return {Kind::Tab,i};
    if(Apply.Contains(x,y))return {Kind::Apply};
    if(Close.Contains(x,y))return {Kind::Close};
    if(Recenter.Contains(x,y))return {Kind::Recenter};
    if(Calibrate.Contains(x,y))return {Kind::Calibrate};
    for(int r=0;r<RowCount(page);++r){
        const auto key=RowKey(page,r);
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
    bool open=false,ready=false;
    Page page=Page::Controls;
    Result result=Result::None;
    uint64_t revision=0;
    const Values& Get()const{return draft;}
    bool Dirty()const{return Changed(base,draft)!=0;}
    bool Busy()const{return pending!=0;}
    void Open(const Snapshot& s){open=true;page=Page::Controls;base=draft=s.values;ready=s.ready;result=s.result;serial=std::max(serial,s.sequence);pending=s.result==Result::Saving?s.sequence:0;++revision;}
    void Close(){open=false;++revision;}
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
        if(hit.kind==Kind::Tab&&hit.index>=0&&hit.index<3){page=Page(hit.index);++revision;return {};}
        if(!ready||Busy())return {};
        if(hit.kind==Kind::Change&&hit.index>=0&&hit.index<Count){
            const auto key=Key(hit.index);const auto& d=Definitions[key];
            if(d.max==1)draft[key]=draft[key]>.5f?0.f:1.f;
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
