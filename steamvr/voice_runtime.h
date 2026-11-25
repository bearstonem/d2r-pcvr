#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <filesystem>
#include <cstring>
#include "voice_settings.h"
namespace framevr::voice {
// A private control channel owned by this game process. The recognizer is isolated
// from the rendering DLL; stopping it or a decoder failure cannot stall VR frames.
struct Telemetry {unsigned magic=0,mode=0,level=0,active=0;int matched=-1;unsigned serial=0;float distance=0;char result[240]{};unsigned padding=0;uint64_t at=0;char spare[232]{};};
static_assert(sizeof(Telemetry)==512);
struct Control {unsigned generation=0,mode=0,length=0,reserved=0;char status[240]{};char json[7424]{};Telemetry telemetry;};
static_assert(offsetof(Control,telemetry)==7680 && offsetof(Telemetry,at)==272);
static_assert(sizeof(Control)==8192);
inline std::string Utf8(const std::wstring& w){int n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);std::string s(size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),s.data(),n,nullptr,nullptr);return s;}
inline std::string Quote(const std::string& s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\')out+='\\';if(c>=32)out+=char(c);}return out+'"';}
class Runtime {
    HANDLE map=nullptr,mutex=nullptr,process=nullptr;Control* control=nullptr;
    std::filesystem::path directory,file,storage;ULONGLONG pollAt=0;unsigned generation=0;
    template<class F> bool With(F f){if(!control)return false;DWORD result=WaitForSingleObject(mutex,0);if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED)return false;f(*control);ReleaseMutex(mutex);return true;}
public:
    ~Runtime(){Close();}
    void Close(){With([](Control& c){c.mode=0;c.reserved=1;++c.generation;});if(control)UnmapViewOfFile(control);if(map)CloseHandle(map);if(mutex)CloseHandle(mutex);if(process)CloseHandle(process);map=mutex=process=nullptr;control=nullptr;}
    void Load(Editor& e){
        if(e.loaded)return;e.loaded=true;
        HMODULE module=nullptr;wchar_t path[32768]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Utf8),&module);
        GetModuleFileNameW(module,path,32768);directory=std::filesystem::path(path).parent_path()/L"voice";file=directory.parent_path()/L"voice-assignments.ini";storage=directory.parent_path()/L"voice-samples";
        for(UINT i=0;i<waveInGetNumDevs();++i){WAVEINCAPSW caps{};if(waveInGetDevCapsW(i,&caps,sizeof caps)==MMSYSERR_NOERROR)e.microphones.push_back(Utf8(caps.szPname));else e.microphones.push_back("Microphone "+std::to_string(i+1));}
        e.microphone=std::clamp(int(GetPrivateProfileIntW(L"voice",L"microphone",-1,file.c_str())),-1,int(e.microphones.size())-2);
        e.tolerance=std::clamp(int(GetPrivateProfileIntW(L"voice",L"tolerance",1,file.c_str())),0,2);
        // Never open the microphone automatically on game startup.
        for(int i=0;i<6;++i){auto section=L"phrase"+std::to_wstring(i+1);wchar_t text[128]{};GetPrivateProfileStringW(section.c_str(),L"text",L"",text,128,file.c_str());e.bindings[size_t(i)]={Utf8(text),int(GetPrivateProfileIntW(section.c_str(),L"skill",-1,file.c_str()))};}
    }
    bool Save(Editor& e,unsigned command=0){
        Load(e);
        if(e.mode==0)With([&](Control& c){c.mode=0;c.generation=++generation;});
        if(command)e.mode=0;
        std::string bindings;
        for(const auto& b:e.bindings){
            if(!bindings.empty())bindings+=',';bindings+="{\"Phrase\":"+Quote(b.phrase)+",\"Skill\":"+std::to_string(b.skill)+"}";
        }
        auto temp=file;temp+=L".tmp";
        bool saved=WritePrivateProfileStringW(L"voice",L"microphone",std::to_wstring(e.microphone).c_str(),temp.c_str())!=0;
        for(int i=0;i<6;++i){auto section=L"phrase"+std::to_wstring(i+1);auto& b=e.bindings[size_t(i)];std::wstring text(b.phrase.begin(),b.phrase.end());saved=WritePrivateProfileStringW(section.c_str(),L"text",text.c_str(),temp.c_str())&&saved;saved=WritePrivateProfileStringW(section.c_str(),L"skill",std::to_wstring(b.skill).c_str(),temp.c_str())&&saved;}
        saved=WritePrivateProfileStringW(L"voice",L"tolerance",std::to_wstring(e.tolerance).c_str(),temp.c_str())&&saved;
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp.c_str());
        if(!saved||!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){e.status="Could not save voice-assignments.ini";return false;}
        if(!control){
            auto name=L"Local\\D2RVRVoiceIntegrated"+std::to_wstring(GetCurrentProcessId());
            mutex=CreateMutexW(nullptr,FALSE,(name+L"Mutex").c_str());map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Control),name.c_str());
            if(map)control=static_cast<Control*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Control)));
            if(!control||!mutex){e.status="Cannot create recognition control channel";return false;}
        }
        const unsigned mode=command?command:unsigned(e.mode);
        auto json="{\"Storage\":"+Quote(Utf8(storage.wstring()))+",\"Slot\":"+std::to_string(e.slot)+",\"Sample\":"+std::to_string(e.sample)+",\"Tolerance\":"+std::to_string(e.tolerance)+",\"Microphone\":"+std::to_string(e.microphone)+",\"Bindings\":["+bindings+"]}";
        if(json.size()>=sizeof(Control::json)){e.status="Voice configuration is too long";return false;}
        if(!With([&](Control& c){std::memcpy(c.json,json.c_str(),json.size()+1);c.length=unsigned(json.size());c.mode=mode;c.reserved=0;c.status[0]=0;c.generation=++generation;})){e.status="Recognition busy; press Save again";return false;}
        e.dirty=false;e.status=mode?"Starting local recognition...":"Voice recognition off";
        if(process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0){CloseHandle(process);process=nullptr;}
        if(mode&&!process){
            auto exe=directory/L"voice-recognition.exe";
            std::wstring command=L"\""+exe.wstring()+L"\" --embedded "+std::to_wstring(GetCurrentProcessId());
            STARTUPINFOW si{};si.cb=sizeof si;si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
            if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&si,&pi)){e.status="Speech files missing: rebuild and install this branch";return false;}
            process=pi.hProcess;CloseHandle(pi.hThread);
        }
        return true;
    }
    bool Poll(Editor& e){Load(e);auto now=GetTickCount64();if(now<pollAt)return false;pollAt=now+200;std::string status;bool changed=false;
        With([&](Control& c){status.assign(c.status,strnlen(c.status,sizeof c.status));
            const auto& t=c.telemetry;bool fresh=t.magic==0x534e4432&&now>=t.at&&now-t.at<1500;
            int level=fresh?std::min(100u,t.level):0,mode=fresh?int(t.mode):0;bool signal=fresh&&t.active;
            if(e.level!=level||e.activeMode!=mode||e.signal!=signal){e.level=level;e.activeMode=mode;e.signal=signal;changed=true;}
            if(t.magic==0x534e4432&&e.resultSerial!=t.serial){e.resultSerial=t.serial;e.matchedSlot=t.matched;e.lastResult.assign(t.result,strnlen(t.result,sizeof t.result));changed=true;}
        });
        if(process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0){CloseHandle(process);process=nullptr;status="Recognition stopped. Save / retry to restart.";}
        for(int slot=0;slot<6;++slot){unsigned mask=0;for(int sample=0;sample<3;++sample){auto path=storage/(L"sound-"+std::to_wstring(slot+1)+L"-"+std::to_wstring(sample+1)+L".wav");if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)mask|=1u<<sample;}if(e.samples[size_t(slot)]!=mask){e.samples[size_t(slot)]=mask;changed=true;}}
        if(!status.empty()&&status!=e.status){e.status=status;return true;}return changed;
    }
};
}
