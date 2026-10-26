#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <cwchar>
#include <cstdlib>
#include "settings_menu.h"
#include "../steamvr/body_calibration.h"
namespace vrsettings {
namespace {
using namespace framevr::settings;
std::mutex mutex;
Snapshot published;
Request queued;
bool busy=false;
std::atomic<bool> depthActive{true},legacyHighlight{false};
float Value(const wchar_t* path,const wchar_t* section,const wchar_t* key,float fallback){
    wchar_t def[64],text[128];std::swprintf(def,64,L"%.6g",double(fallback));
    GetPrivateProfileStringW(section,key,def,text,128,path);
    wchar_t* end=nullptr;const float value=std::wcstof(text,&end);
    return end==text||!std::isfinite(value)?fallback:value;
}
Values Load(const wchar_t* path){
    Values v{},raw{};
    bool legacy=false;
    for(int i=0;i<Count;++i){
        const auto& d=Definitions[i];
        // Legacy separate switches are read only for migration. Both off stays
        // off; either enabled becomes the combined wrist display. An explicit
        // navigation_location takes precedence without rewriting old keys.
        // The old single outline color reaches the per-hand colors the same way
        // (LegacyHighlight): a hand's own key, once saved, wins.
        // Guard strain color Off (guard_strain_dark 2) carries to the vignette switch as Off.
        const float fallback=i==NavigationLocation?(v[WristMap]>.5f||v[WristCompass]>.5f?0.f:3.f):
                             i==StrainVignette?(std::lround(Value(path,L"gameplay",L"guard_strain_dark",0))==2?0.f:1.f):
                             HighlightKey(Key(i))?LegacyHighlight(Key(i),Value(path,L"gameplay",L"target_outline_by_range",-1),Value(path,L"gameplay",L"target_outline_hue",-1)):
                             d.defaultValue;
        raw[i]=Value(path,d.section,d.key,fallback);v[i]=Normalize(Key(i),raw[i]);
        if(HighlightKey(Key(i))&&fallback!=d.defaultValue&&Value(path,d.section,d.key,-1)<0)legacy=true;   // no own key: the old color counted
    }
    legacyHighlight.store(legacy);
    v[SmoothTurn]=SmoothTurning(raw[Snap],Value(path,L"turn",L"smooth",-1))?1.f:0.f;
    v[Snap]=SnapDegrees(raw[Snap]);
    // A malformed/future record must prompt again, not silently become valid
    // through the slider clamps. Camera and settings agree on raw validity.
    if(raw[BodyCalibration]!=1||!framevr::calibration::Valid({raw[EyeHeight],raw[ArmSpan]}))v[BodyCalibration]=0;
    if(v[Foveation]>0)v[Foveation]=Value(path,L"render",L"eye_tracked_foveation",0)!=0?2.f:1.f;
    return v;
}
bool SameFile(const WIN32_FILE_ATTRIBUTE_DATA& a,const WIN32_FILE_ATTRIBUTE_DATA& b){
    return CompareFileTime(&a.ftLastWriteTime,&b.ftLastWriteTime)==0&&a.nFileSizeLow==b.nFileSizeLow&&a.nFileSizeHigh==b.nFileSizeHigh;
}
Result Save(const wchar_t* path,const Request& r){
    WIN32_FILE_ATTRIBUTE_DATA before{},after{};
    if(!GetFileAttributesExW(path,GetFileExInfoStandard,&before))return Result::Failed;
    const auto current=Load(path);
    if(Conflict(current,r.base,r.changed))return Result::Conflict;
    const std::wstring temp=std::wstring(path)+L".vrmenu."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    if(!CopyFileW(path,temp.c_str(),FALSE))return Result::Failed;
    auto write=[&](const wchar_t* section,const wchar_t* key,float value){
        wchar_t text[64];std::swprintf(text,64,L"%.6g",double(value));
        return WritePrivateProfileStringW(section,key,text,temp.c_str())!=FALSE;
    };
    bool ok=true;
    for(int i=0;i<Count&&ok;++i)if(r.changed&(ChangeMask{1}<<i)){
        const auto& d=Definitions[i];
        if(i==Foveation)ok=write(L"render",L"fixed_foveation",r.values[i]>0?1:0)&&write(L"render",L"eye_tracked_foveation",r.values[i]==2?1:0);
        // Materialize the mode before replacing legacy snap=0. Changing the
        // stored angle must not implicitly switch a smooth profile to snap.
        else if(i==Snap)ok=write(d.section,d.key,r.values[i])&&write(L"turn",L"smooth",(r.changed&(ChangeMask{1}<<SmoothTurn))?r.values[SmoothTurn]:current[SmoothTurn]);
        else ok=write(d.section,d.key,r.values[i]);
    }
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp.c_str());
    if(!ok){DeleteFileW(temp.c_str());return Result::Failed;}
    if(!GetFileAttributesExW(path,GetFileExInfoStandard,&after)||!SameFile(before,after)){
        DeleteFileW(temp.c_str());return Result::Conflict;
    }
    // Preserve the configuration before the first in-headset edit. Never
    // overwrite that recovery copy on later Apply presses.
    const std::wstring backup=std::wstring(path)+L".before-vr-menu";
    if(!CopyFileW(path,backup.c_str(),TRUE)&&GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS){
        DeleteFileW(temp.c_str());return Result::Failed;
    }
    if(!MoveFileExW(temp.c_str(),path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temp.c_str());return Result::Failed;
    }
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,path);
    return (Changed(Load(path),r.values)&r.changed)==0?Result::Saved:Result::Failed;
}
}
framevr::settings::Snapshot Read(){std::lock_guard lock(mutex);return published;}
void SetReShadeDepth(bool active){depthActive.store(active);}
bool ReShadeDepthActive(){return depthActive.load();}
bool LegacyHighlightUsed(){return legacyHighlight.load();}
void SetOfflineDifficultyAvailable(bool available){
    std::lock_guard lock(mutex);
    if(published.offlineDifficultyAvailable!=available){published.offlineDifficultyAvailable=available;++published.revision;}
}
bool Submit(const framevr::settings::Request& r){
    using namespace framevr::settings;
    if(!r.sequence||!r.changed||(r.changed&~SupportedChanges)||r.changed!=Changed(r.base,r.values))return false;
    for(int i=0;i<Count;++i)if(!std::isfinite(r.base[i])||!std::isfinite(r.values[i])||Normalize(Key(i),r.values[i])!=r.values[i])return false;
    if(r.values[BodyCalibration]==1&&!framevr::calibration::Stored(r.values))return false;
    std::lock_guard lock(mutex);if(busy)return false;
    queued=r;busy=true;published.sequence=r.sequence;published.result=Result::Saving;++published.revision;return true;
}
bool Poll(const wchar_t* path){
    using namespace framevr::settings;
    Request request;{std::lock_guard lock(mutex);request=queued;queued={};}
    // The update thread polls twice a second. Read profile values only when
    // the file changes or Apply queues a transaction, never on the XR thread.
    static std::wstring cachedPath;
    static WIN32_FILE_ATTRIBUTE_DATA cachedAttributes{};
    static bool cached=false;
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    const bool exists=GetFileAttributesExW(path,GetFileExInfoStandard,&attributes)&&!(attributes.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
    if(!request.sequence&&cached&&exists&&cachedPath==path&&SameFile(cachedAttributes,attributes))return false;
    const auto result=request.sequence?Save(path,request):Result::None;
    const auto values=Load(path);
    cached=GetFileAttributesExW(path,GetFileExInfoStandard,&cachedAttributes)&&!(cachedAttributes.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
    cachedPath=path;const bool ready=cached;
    std::lock_guard lock(mutex);
    if(Changed(published.values,values)||published.ready!=ready||request.sequence)++published.revision;
    published.values=values;published.ready=ready;
    if(request.sequence){published.sequence=request.sequence;published.result=result;busy=false;}
    return result==Result::Saved;
}
}
