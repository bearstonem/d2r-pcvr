#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

// Frame generation (experimental, issue #308): OFXR Bridge by tig3rmast3r
// (LGPL-3.0-or-later, https://github.com/tig3rmast3r/OFXR-Bridge), an OpenXR
// API layer that puts an optical-flow frame between two rendered ones. Setup
// fetches its pinned release (steamvr/install.py) and puts the layer, its
// licenses, an explicit layer manifest and its ofxr_bridge.ini in the mod's
// d2rloader\plugins\ofxr. No tray app and no registry entry: with [render]
// frame_generation on, the transport names the layer for this process only
// (XR_API_LAYER_PATH and xrCreateInstance's layers), so it loads with an OpenXR
// instance: turned on while the game runs, the transport restarts the OpenXR
// session for it, as a window resize does (LoadsWithNewSession); turned off, the
// loaded layer is bypassed until the game restarts. The backend and NVIDIA's
// options reach the loaded layer live through its control API (provider_api.hpp
// V1, mirrored below).
// While it runs it doubles the frame time the game is given (xrWaitFrame's
// predicted period): the game draws every other headset frame and the bridge
// makes the rest. Both eyes at half the rate then shows each eye half its
// frames for real; By turns only a quarter (22 a second at 90 Hz, where By
// turns alone ran 83-89 frames: owner's PC, 2026-10-23). So it runs with Both
// eyes only: with By turns the loaded layer is asked to step aside, live, and
// VR Settings turns it on only with Both eyes.
// Value-only: nothing here loads a module or reads XR state.
namespace framevr::framegen {
enum class Mode {Off,FidelityFx,Nvidia};   // [render] frame_generation 0 / 1 / 2
inline constexpr const char* ModeNames[]={"Off","FidelityFX","NVIDIA"};
// NVIDIA optical flow: [render] frame_generation_preset, frame_generation_scale.
inline constexpr const char* PresetNames[]={"Slow","Medium","Fast"};
inline constexpr const char* ScaleNames[]={"100% scale","75% scale","50% scale"};
inline constexpr char LayerName[]="XR_APILAYER_XRFrameBridge_diagnostic";
inline constexpr wchar_t Folder[]=L"ofxr";   // beside the plugin
inline constexpr wchar_t ManifestFile[]=L"XR_APILAYER_XRFrameBridge_diagnostic.json";
inline constexpr wchar_t LibraryFile[]=L"XR_APILAYER_XRFrameBridge_diagnostic.dll";

// OFXR's provider_api.hpp, V1 (release 0.2.1, build V116): the layer exports
// these; a larger struct_size is accepted, a smaller one refused.
constexpr uint32_t ApiVersion=1;
struct ControlSettingsV1 {
    uint32_t structSize=sizeof(ControlSettingsV1);
    int32_t enabled=1,backend=0,preset=1,scale=2,backward=0,motionVectors=0;   // backend 0 FidelityFX, 1 NVIDIA
};
struct ControlSnapshotV1 {
    uint32_t structSize=sizeof(ControlSnapshotV1),apiVersion=ApiVersion;
    ControlSettingsV1 desired{};
    uint64_t revision=0;
    uint32_t sessions=0,pending=0,bypass=0,ready=0,errors=0;   // sessions by what they did with the last request
    int32_t lastError=0;
};
static_assert(sizeof(ControlSettingsV1)==28&&sizeof(ControlSnapshotV1)==72,"OFXR control ABI V1");
using VersionFn=uint32_t(*)();
using SnapshotFn=int(*)(ControlSnapshotV1*);
using RequestFn=int(*)(const ControlSettingsV1*);
inline constexpr char VersionExport[]="OFXR_GetProviderApiVersionV1",SnapshotExport[]="OFXR_GetControlSnapshotV1",RequestExport[]="OFXR_RequestControlV1";

struct Options {
    Mode mode=Mode::Off;int preset=1,scale=2;   // NVIDIA Medium at 50%: OFXR's suggested start
    bool On()const{return mode!=Mode::Off;}
    bool operator==(const Options&)const=default;
};
inline int Choice(float value,int count,int fallback){
    return std::isfinite(value)&&value>=0&&value<count&&value==std::round(value)?int(value):fallback;
}
inline Options Read(float mode,float preset,float scale){
    return {Mode(Choice(mode,3,0)),Choice(preset,3,1),Choice(scale,3,2)};
}
// What the layer is asked for. Off with the layer loaded asks it to step
// aside (enabled 0) until a restart leaves it out; bothEyes false (By turns)
// asks the same until Both eyes is back.
inline ControlSettingsV1 Control(const Options& o,bool bothEyes=true){
    ControlSettingsV1 c;c.enabled=o.On()&&bothEyes;c.backend=o.mode==Mode::Nvidia;c.preset=o.preset;c.scale=o.scale;
    return c;
}
// The backend and options as the status names them: "FidelityFX", "NVIDIA Medium, 50% scale".
inline std::string Describe(int backend,int preset,int scale){
    if(backend!=1)return ModeNames[1];
    return std::string(ModeNames[2])+" "+PresetNames[std::clamp(preset,0,2)]+", "+ScaleNames[std::clamp(scale,0,2)];
}

// How this OpenXR instance started (the transport decides once, in LoadFunctions).
enum class Start {
    NotAsked,     // frame generation was off
    Unavailable,  // WineOpenXR (Proton): no Khronos loader, so no API layers
    Missing,      // on, but plugins\ofxr has no layer manifest
    Failed,       // on, but the loader did not list the layer or the instance failed with it: started without
    Loaded,       // the instance runs through the layer, its control API found
    NoControl,    // the instance runs through the layer, without a control API this mod knows
};
enum class State {Off,Unavailable,NotInstalled,RestartToStart,Failed,Starting,Applying,Active,Bypassed,Paused,Error,NoControl,NeedsBothEyes};
inline const char* StateName(State s){
    constexpr const char* names[]={"off","unavailable","not installed","loads with a new session","failed","starting","applying",
                                   "active","bypassed","paused","error","no control","needs both eyes"};
    return names[int(s)];
}
struct Readout {
    State state=State::Off;
    int backend=0,preset=1,scale=2;   // what the layer was asked for (Loaded)
    uint32_t sessions=0,ready=0,bypass=0,errors=0;int32_t lastError=0;
    bool runtimeHalves=false;   // Active, but Virtual Desktop halves the frame rate as well (HalvingWatch)
    bool operator==(const Readout&)const=default;
};
// started: as above; installed: plugins\ofxr has the manifest now; wanted: the
// settings now; snapshot: the layer's, when Loaded and it answered; bothEyes:
// Stereo rendering is Both eyes (else it waits for it).
inline Readout Classify(Start started,bool installed,const Options& wanted,const ControlSnapshotV1* snapshot,bool bothEyes=true){
    Readout r;
    if(started==Start::NoControl){r.state=State::NoControl;return r;}
    if(started==Start::Loaded){
        if(!wanted.On()){r.state=State::Paused;return r;}
        if(!bothEyes){r.state=State::NeedsBothEyes;return r;}
        if(!snapshot){r.state=State::Error;return r;}
        r.backend=snapshot->desired.backend;r.preset=snapshot->desired.preset;r.scale=snapshot->desired.scale;
        r.sessions=snapshot->sessions;r.ready=snapshot->ready;r.bypass=snapshot->bypass;r.errors=snapshot->errors;r.lastError=snapshot->lastError;
        r.state=!snapshot->sessions?State::Starting:snapshot->pending?State::Applying:snapshot->errors?State::Error:
                snapshot->ready?State::Active:State::Bypassed;
        return r;
    }
    if(!wanted.On())r.state=State::Off;
    else if(started==Start::Unavailable)r.state=State::Unavailable;
    else if(started==Start::Failed)r.state=State::Failed;
    else if(!installed)r.state=State::NotInstalled;
    else if(!bothEyes)r.state=State::NeedsBothEyes;
    else r.state=State::RestartToStart;   // off at start, or installed since
    return r;
}
// It would run (on, installed, Both eyes) but this instance has no OFXR Bridge: the
// transport restarts the OpenXR session so a new instance loads it. Not by turns (it
// would only wait for Both eyes), not when it failed or cannot load.
inline bool LoadsWithNewSession(State s){return s==State::RestartToStart;}
// The loaded layer failed a request made while it ran: under Virtual Desktop (VDXR),
// resuming after By turns once it had made frames failed with 0x80070057 every time
// (a tester's RTX 4090 and the owner's PC, 2026-10-24) and stayed failed, while a new
// session set it up afresh. So after a live request the transport restarts the session once.
inline bool RetriesWithNewSession(const Readout& r){return r.state==State::Error&&r.errors>0;}
// Under VDXR the bridge keeps the headset's frame period while it makes frames (90 Hz
// predicted, the game drawing 45 pairs a second). A predicted rate well under the
// highest of the session means Virtual Desktop halved it too (its Synchronous
// Spacewarp), and the game draws a quarter: the tester had 22 pairs a second for 28%
// of the time with both on, none with SSW off. Note() takes a second's predicted rate;
// HoldSeconds in a row set or clear it.
struct HalvingWatch {
    static constexpr int HoldSeconds=3;
    float top=0;int below=0,above=0;bool halved=false;
    bool Note(float hz) {
        if(!std::isfinite(hz)||hz<=0)return halved;
        top=std::max(top,hz);
        if(hz<top*.6f){above=0;if(++below>=HoldSeconds)halved=true;}
        else if(hz>top*.8f){below=0;if(++above>=HoldSeconds)halved=false;}
        else below=above=0;
        return halved;
    }
};
// The Perf page's line (and the log's): one line, under 90 characters.
inline std::string Line(const Readout& r){
    char line[160];
    const std::string running=Describe(r.backend,r.preset,r.scale);
    switch(r.state){
        case State::Off:return "Frame generation: Off (experimental, OFXR Bridge)";
        case State::Unavailable:return "Frame generation: needs native Windows; Proton has no OpenXR layers";
        case State::NotInstalled:return "Frame generation: OFXR Bridge is not installed; run Setup's Update / repair";
        case State::RestartToStart:return "Frame generation: loading OFXR Bridge; the VR session restarts";
        case State::Failed:return "Frame generation: OFXR Bridge did not start; playing without it (see the log)";
        case State::Starting:return "Frame generation: OFXR Bridge loaded, waiting for the headset session";
        case State::Applying:std::snprintf(line,sizeof line,"Frame generation: OFXR Bridge switching to %s",running.c_str());return line;
        case State::Active:
            if(r.runtimeHalves)return "Frame generation: active, but Virtual Desktop's SSW halves it again; set SSW to Disabled";
            std::snprintf(line,sizeof line,"Frame generation: active, OFXR Bridge %s",running.c_str());return line;
        case State::Bypassed:return "Frame generation: OFXR Bridge loaded but bypassed; no generated frames";
        case State::Paused:return "Frame generation: Off for now; restart the game to unload OFXR Bridge";
        case State::Error:
            if(!r.errors)return "Frame generation: OFXR Bridge does not answer; no status";
            std::snprintf(line,sizeof line,"Frame generation: OFXR Bridge failed (0x%08X); no generated frames",unsigned(r.lastError));return line;
        case State::NoControl:return "Frame generation: OFXR Bridge loaded; this version gives no status";
        case State::NeedsBothEyes:return "Frame generation: paused; it needs Stereo rendering: Both eyes";
    }
    return {};
}
// Whether the line is a warning (amber on the Perf page).
inline bool Warning(const Readout& r){
    return r.state==State::Failed||r.state==State::Bypassed||r.state==State::Error||r.state==State::NotInstalled||r.state==State::Unavailable||
           r.state==State::NeedsBothEyes||r.runtimeHalves;
}
}
