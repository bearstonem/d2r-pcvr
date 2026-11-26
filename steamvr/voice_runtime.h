#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <filesystem>
#include <cstring>
#include <map>
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
    std::string profile;
    std::map<std::string,Editor> drafts;
    HANDLE map=nullptr,mutex=nullptr,process=nullptr;Control* control=nullptr;
    std::filesystem::path directory,file,storage;ULONGLONG pollAt=0,retryAt=0;unsigned generation=0,completed=0;bool pageOpen=false,actionPending=false,launch=true;std::string published;
    template<class F> bool With(F f){if(!control)return false;DWORD result=WaitForSingleObject(mutex,0);if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED)return false;f(*control);ReleaseMutex(mutex);return true;}
public:
    explicit Runtime(bool startProcess=true):launch(startProcess){}
    bool SelectCharacter(Editor& e,const std::string& name,int characterClass,unsigned bank){
        std::string key;
        if(!name.empty()&&name.size()<16&&characterClass>=0){
            key=std::to_string(bank)+"-"+std::to_string(characterClass)+"-";
            constexpr char hex[]="0123456789abcdef";
            for(unsigned char c:name){key+=hex[c>>4];key+=hex[c&15];}
        }
        if(key==profile)return false;
        if(!profile.empty())drafts[profile]=e;
        profile=key;e=Editor{};
        if(auto it=drafts.find(key);it!=drafts.end())e=it->second;
        e.character=name;e.loaded=false;
        published.clear();actionPending=false;pollAt=0;
        With([&](Control& c){c.mode=0;c.generation=++generation;c.telemetry={};completed=0;});
        Load(e);return true;
    }
    ~Runtime(){Close();}
    void Close(){With([](Control& c){c.mode=0;c.reserved=1;++c.generation;});if(control)UnmapViewOfFile(control);if(map)CloseHandle(map);if(mutex)CloseHandle(mutex);if(process)CloseHandle(process);map=mutex=process=nullptr;control=nullptr;}
    void Load(Editor& e){
        if(e.loaded)return;e.loaded=true;
        HMODULE module=nullptr;wchar_t path[32768]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Utf8),&module);
        GetModuleFileNameW(module,path,32768);directory=std::filesystem::path(path).parent_path()/L"voice";
        auto root=directory.parent_path()/L"voice-profiles"/profile;
        file=root/L"assignments.ini";storage=root/L"samples";
        if(profile.empty()){e.status="Enter a character to configure voice commands";return;}
        if(drafts.contains(profile)){auto name=e.character;e=drafts[profile];e.character=name;e.loaded=true;return;}
        e.microphone=std::max(-1,int(GetPrivateProfileIntW(L"voice",L"microphone",-1,file.c_str())));   // checked against the devices in ListMicrophones
        e.tolerance=std::clamp(int(GetPrivateProfileIntW(L"voice",L"tolerance",2,file.c_str())),0,2);
        e.mode=GetPrivateProfileIntW(L"voice",L"enabled",0,file.c_str())?2:0;
        for(int i=0;i<6;++i){auto section=L"phrase"+std::to_wstring(i+1);wchar_t text[128]{};GetPrivateProfileStringW(section.c_str(),L"text",L"",text,128,file.c_str());e.bindings[size_t(i)]={Utf8(text),int(GetPrivateProfileIntW(section.c_str(),L"skill",-1,file.c_str()))};}
        e.Commit();
    }
    // Listing the microphones took 74 ms the first time (5 devices, a fresh process): on the frame path that is a
    // stall, so it waits for the Voice page or saved voice casting, never every character's first frame.
    void ListMicrophones(Editor& e){
        if(e.microphonesListed||profile.empty())return;e.microphonesListed=true;
        for(UINT i=0;i<waveInGetNumDevs();++i){WAVEINCAPSW caps{};if(waveInGetDevCapsW(i,&caps,sizeof caps)==MMSYSERR_NOERROR)e.microphones.push_back(Utf8(caps.szPname));else e.microphones.push_back("Microphone "+std::to_string(i+1));}
        const int last=int(e.microphones.size())-2;
        e.microphone=std::clamp(e.microphone,-1,last);e.savedMicrophone=std::clamp(e.savedMicrophone,-1,last);
    }
    bool Save(Editor& e,unsigned command=0){
        Load(e);
        if(profile.empty()){e.status="Enter a character to configure voice commands";return false;}
        if(command){published.clear();actionPending=Publish(e,command,true);return actionPending;}
        std::error_code error;std::filesystem::create_directories(file.parent_path(),error);
        if(error){e.status="Could not create character voice profile";return false;}
        auto temp=file;temp+=L".tmp";
        bool saved=WritePrivateProfileStringW(L"voice",L"microphone",std::to_wstring(e.microphone).c_str(),temp.c_str())!=0;
        for(int i=0;i<6;++i){auto section=L"phrase"+std::to_wstring(i+1);auto& b=e.bindings[size_t(i)];std::wstring text(b.phrase.begin(),b.phrase.end());saved=WritePrivateProfileStringW(section.c_str(),L"text",text.c_str(),temp.c_str())&&saved;saved=WritePrivateProfileStringW(section.c_str(),L"skill",std::to_wstring(b.skill).c_str(),temp.c_str())&&saved;}
        saved=WritePrivateProfileStringW(L"voice",L"tolerance",std::to_wstring(e.tolerance).c_str(),temp.c_str())&&saved;
        saved=WritePrivateProfileStringW(L"voice",L"enabled",e.mode==2?L"1":L"0",temp.c_str())&&saved;
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp.c_str());
        if(!saved||!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){e.status="Could not save voice-assignments.ini";return false;}
        e.Commit();published.clear();actionPending=false;return Sync(e,pageOpen);
    }
    bool Sync(Editor& e,bool settingsPage){Load(e);pageOpen=settingsPage;if(profile.empty())return Publish(e,0,false);if(settingsPage||e.savedMode)ListMicrophones(e);if(actionPending&&settingsPage)return true;if(!settingsPage)actionPending=false;return Publish(e,settingsPage?1u:unsigned(e.savedMode),settingsPage);}
    bool Publish(Editor& e,unsigned mode,bool draft){
        std::string bindings;for(const auto& b:draft?e.bindings:e.savedBindings){if(!bindings.empty())bindings+=',';bindings+="{\"Phrase\":"+Quote(b.phrase)+",\"Skill\":"+std::to_string(b.skill)+"}";}
        if(!control){
            auto name=L"Local\\D2RVRVoiceIntegrated"+std::to_wstring(GetCurrentProcessId());
            mutex=CreateMutexW(nullptr,FALSE,(name+L"Mutex").c_str());map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Control),name.c_str());
            if(map)control=static_cast<Control*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Control)));
            if(!control||!mutex){e.status="Cannot create recognition control channel";return false;}
        }
        auto json="{\"Storage\":"+Quote(Utf8(storage.wstring()))+",\"Slot\":"+std::to_string(e.slot)+",\"Sample\":"+std::to_string(e.sample)+",\"Tolerance\":"+std::to_string(draft?e.tolerance:e.savedTolerance)+",\"Microphone\":"+std::to_string(draft?e.microphone:e.savedMicrophone)+",\"Bindings\":["+bindings+"]}";
        const auto signature=std::to_string(mode)+json;
        if(signature==published)return true;
        if(mode&&launch&&!process&&GetTickCount64()<retryAt)return false;
        if(json.size()>=sizeof(Control::json)){e.status="Voice configuration is too long";return false;}
        if(!With([&](Control& c){std::memcpy(c.json,json.c_str(),json.size()+1);c.length=unsigned(json.size());c.mode=mode;c.reserved=0;c.status[0]=0;c.generation=++generation;})){e.status="Recognition busy; press Save again";return false;}
        e.status=mode?"Starting local recognition...":"Voice recognition off";
        if(process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0){CloseHandle(process);process=nullptr;}
        if(mode&&!process){
            if(!launch){published=signature;return true;}
            auto exe=directory/L"voice-recognition.exe";
            std::wstring command=L"\""+exe.wstring()+L"\" --embedded "+std::to_wstring(GetCurrentProcessId());
            STARTUPINFOW si{};si.cb=sizeof si;si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
            if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&si,&pi)){retryAt=GetTickCount64()+2000;e.status="Cannot start voice helper. Check installation / antivirus.";return false;}
            process=pi.hProcess;CloseHandle(pi.hThread);
        }
        published=signature;return true;
    }
    bool Poll(Editor& e){Load(e);auto now=GetTickCount64();if(now<pollAt)return false;pollAt=now+200;std::string status;bool changed=false;
        With([&](Control& c){status.assign(c.status,strnlen(c.status,sizeof c.status));
            const auto& t=c.telemetry;if(t.padding!=completed){completed=t.padding;published.clear();actionPending=false;}
            bool fresh=t.magic==0x534e4432&&now>=t.at&&now-t.at<1500;
            int level=fresh?std::min(100u,t.level):0,mode=fresh?int(t.mode):0;bool signal=fresh&&t.active;
            if(e.level!=level||e.activeMode!=mode||e.signal!=signal){e.level=level;e.activeMode=mode;e.signal=signal;changed=true;}
            if(t.magic==0x534e4432&&e.resultSerial!=t.serial){e.resultSerial=t.serial;e.matchedSlot=t.matched;e.lastResult.assign(t.result,strnlen(t.result,sizeof t.result));changed=true;}
        });
        if(process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0){CloseHandle(process);process=nullptr;status="Recognition stopped. Save / retry to restart.";}
        // Which examples are recorded matters to the Voice page only: no file checks on the frame path otherwise.
        if(pageOpen)for(int slot=0;slot<6;++slot){unsigned mask=0;for(int sample=0;sample<3;++sample){auto path=storage/(L"sound-"+std::to_wstring(slot+1)+L"-"+std::to_wstring(sample+1)+L".wav");if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)mask|=1u<<sample;}if(e.samples[size_t(slot)]!=mask){e.samples[size_t(slot)]=mask;changed=true;}}
        if(!status.empty()&&status!=e.status){e.status=status;return true;}return changed;
    }
};
}
