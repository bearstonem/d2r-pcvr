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
    Check(before.ready&&before.values[DrawDistance]==6&&before.values[Foveation]==1&&before.values[ObjectDistance]==300,"explicit distance retained; missing object distance uses shipped default");
    Check(WritePrivateProfileStringW(L"render",L"rings",nullptr,name),"remove terrain distance in isolated legacy profile");
    vrsettings::Poll(name);before=vrsettings::Read();
    Check(before.values[DrawDistance]==2&&before.values[ObjectDistance]==300,"missing draw distances do not silently select 4 rings and radius 1000");
    Check(WritePrivateProfileStringW(L"render",L"rings",L"6",name),"restore explicit terrain distance");
    vrsettings::Poll(name);before=vrsettings::Read();
    Model model;model.Open(before);model.Click({Kind::Change,Foveation,1});model.Click({Kind::Change,Motion,1});
    auto request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&!vrsettings::Submit(request),"one queued transaction at a time");
    Check(vrsettings::Read().result==Result::Saving&&vrsettings::Poll(name),"write transaction succeeds");
    auto saved=vrsettings::Read();Check(saved.result==Result::Saved&&saved.values[Foveation]==2&&saved.values[Motion]==1,"saved values round trip");
    Check(Int(path,L"render",L"fixed_foveation")==1&&Int(path,L"render",L"eye_tracked_foveation")==1,"eye-tracked mode writes both keys");
    Check(Int(path,L"custom",L"keep")==12345&&Int(path,L"render",L"rings")==6,"Apply preserves unrelated keys");
    Check(Int(backup,L"custom",L"keep")==12345&&Int(backup,L"gameplay",L"motion_controls")==-999,"first Apply preserves original config backup");
    model.Open(saved);model.Click({Kind::Change,Foveation,-1});model.Click({Kind::Change,Foveation,-1});
    request=model.Click({Kind::Apply});
    Check(WritePrivateProfileStringW(L"custom",L"later",L"42",name),"simulate unrelated external edit");
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"merge unrelated external edit");
    Check(Int(path,L"render",L"fixed_foveation")==0&&Int(path,L"render",L"eye_tracked_foveation")==0,"Off writes both keys");
    Check(Int(path,L"custom",L"later")==42&&Int(backup,L"custom",L"later")==-999,"backup is not overwritten on subsequent saves");
    model.Open(vrsettings::Read());model.Click({Kind::Change,StereoRender,1});request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name),"enable single pass in existing running menu transaction");
    Check(Int(path,L"stereo",L"replay_right")==1&&vrsettings::Read().values[StereoRender]==1,"manual renderer selection persists");
    model.Open(vrsettings::Read());model.Click({Kind::Change,StereoRender,-1});request=model.Click({Kind::Apply});
    Check(vrsettings::Submit(request)&&vrsettings::Poll(name)&&Int(path,L"stereo",L"replay_right")==0,"return to Standard without restart");
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
    model.Open(vrsettings::Read());model.Click({Kind::Change,Motion,-1});request=model.Click({Kind::Apply});
    Check(DeleteFileW(name)&&vrsettings::Submit(request)&&!vrsettings::Poll(name),"missing file fails safely");
    Check(!vrsettings::Read().ready&&vrsettings::Read().result==Result::Failed,"failure status published");
    Check(GetFileAttributesW(name)==INVALID_FILE_ATTRIBUTES&&Int(backup,L"custom",L"keep")==12345,"missing original not recreated; backup survives");
    Check(DeleteFileW(backup.c_str()),"clean up isolated backup");
    std::puts("VR settings Windows INI transaction, backup, conflict and failure tests passed");
}
