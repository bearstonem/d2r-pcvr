#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <filesystem>
#include <cstring>
#include "voice_settings.h"
namespace framevr::voice {
// A private control channel owned by this game process. The recognizer is isolated
// from the rendering DLL; stopping it or a decoder failure cannot stall VR frames.
struct Control {unsigned generation=0,mode=0,length=0,reserved=0;char status[240]{};char json[7936]{};};
static_assert(sizeof(Control)==8192);
inline std::string Utf8(const std::wstring& w){int n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);std::string s(size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),s.data(),n,nullptr,nullptr);return s;}
inline std::string Quote(const std::string& s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\')out+='\\';if(c>=32)out+=char(c);}return out+'"';}
class Runtime {
    HANDLE map=nullptr,mutex=nullptr,process=nullptr;Control* control=nullptr;
    std::filesystem::path directory,file;ULONGLONG pollAt=0;unsigned generation=0;
    template<class F> bool With(F f){if(!control)return false;DWORD result=WaitForSingleObject(mutex,0);if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED)return false;f(*control);ReleaseMutex(mutex);return true;}
public:
    ~Runtime(){Close();}
    void Close(){With([](Control& c){c.mode=0;c.reserved=1;++c.generation;});if(control)UnmapViewOfFile(control);if(map)CloseHandle(map);if(mutex)CloseHandle(mutex);if(process)CloseHandle(process);map=mutex=process=nullptr;control=nullptr;}
    void Load(Editor& e){
        if(e.loaded)return;e.loaded=true;
        HMODULE module=nullptr;wchar_t path[32768]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Utf8),&module);
        GetModuleFileNameW(module,path,32768);directory=std::filesystem::path(path).parent_path()/L"voice";file=directory.parent_path()/L"voice-assignments.ini";
        for(UINT i=0;i<waveInGetNumDevs();++i){WAVEINCAPSW caps{};if(waveInGetDevCapsW(i,&caps,sizeof caps)==MMSYSERR_NOERROR)e.microphones.push_back(Utf8(caps.szPname));else e.microphones.push_back("Microphone "+std::to_string(i+1));}
        e.microphone=std::clamp(int(GetPrivateProfileIntW(L"voice",L"microphone",-1,file.c_str())),-1,int(e.microphones.size())-2);
        // Never open the microphone automatically on game startup.
        for(int i=0;i<6;++i){auto section=L"phrase"+std::to_wstring(i+1);wchar_t text[128]{};GetPrivateProfileStringW(section.c_str(),L"text",L"",text,128,file.c_str());e.bindings[size_t(i)]={Utf8(text),int(GetPrivateProfileIntW(section.c_str(),L"skill",-1,file.c_str()))};}
    }
    bool Save(Editor& e){
        Load(e);
        if(e.mode==0)With([&](Control& c){c.mode=0;c.generation=++generation;});
        std::string bindings;std::vector<std::string> used;
        for(const auto& b:e.bindings){if(b.phrase.empty()||b.skill<0)continue;
            std::string phrase=b.phrase;while(!phrase.empty()&&phrase.back()==' ')phrase.pop_back();
            if(std::find(used.begin(),used.end(),phrase)!=used.end()){e.status="Duplicate phrase: "+phrase;return false;}used.push_back(phrase);
            if(!bindings.empty())bindings+=',';bindings+="{\"Phrase\":"+Quote(phrase)+",\"Skill\":"+std::to_string(b.skill)+"}";
        }
        if(e.mode&&bindings.empty()){e.status="Assign a phrase and an action before enabling recognition.";return false;}
        auto temp=file;temp+=L".tmp";
        bool saved=WritePrivateProfileStringW(L"voice",L"microphone",std::to_wstring(e.microphone).c_str(),temp.c_str())!=0;
        for(int i=0;i<6;++i){auto section=L"phrase"+std::to_wstring(i+1);auto& b=e.bindings[size_t(i)];std::wstring text(b.phrase.begin(),b.phrase.end());saved=WritePrivateProfileStringW(section.c_str(),L"text",text.c_str(),temp.c_str())&&saved;saved=WritePrivateProfileStringW(section.c_str(),L"skill",std::to_wstring(b.skill).c_str(),temp.c_str())&&saved;}
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp.c_str());
        if(!saved||!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){e.status="Could not save voice-assignments.ini";return false;}
        if(!control){
            auto name=L"Local\\D2RVRVoiceIntegrated"+std::to_wstring(GetCurrentProcessId());
            mutex=CreateMutexW(nullptr,FALSE,(name+L"Mutex").c_str());map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Control),name.c_str());
            if(map)control=static_cast<Control*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Control)));
            if(!control||!mutex){e.status="Cannot create recognition control channel";return false;}
        }
        auto json="{\"Model\":"+Quote(Utf8((directory/L"vosk-model-small-en-us-0.15").wstring()))+",\"Microphone\":"+std::to_string(e.microphone)+",\"Confidence\":0.75,\"Bindings\":["+bindings+"]}";
        if(json.size()>=sizeof(Control::json)){e.status="Voice configuration is too long";return false;}
        if(!With([&](Control& c){std::memcpy(c.json,json.c_str(),json.size()+1);c.length=unsigned(json.size());c.mode=unsigned(e.mode);c.reserved=0;c.status[0]=0;c.generation=++generation;})){e.status="Recognition busy; press Save again";return false;}
        e.dirty=false;e.status=e.mode?"Starting local recognition...":"Voice recognition off";
        if(process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0){CloseHandle(process);process=nullptr;}
        if(e.mode&&!process){
            auto exe=directory/L"voice-recognition.exe";
            std::wstring command=L"\""+exe.wstring()+L"\" --embedded "+std::to_wstring(GetCurrentProcessId());
            STARTUPINFOW si{};si.cb=sizeof si;si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
            if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&si,&pi)){e.status="Speech files missing: rebuild and install this branch";return false;}
            process=pi.hProcess;CloseHandle(pi.hThread);
        }
        return true;
    }
    bool Poll(Editor& e){Load(e);auto now=GetTickCount64();if(now<pollAt)return false;pollAt=now+500;std::string status;
        With([&](Control& c){status.assign(c.status,strnlen(c.status,sizeof c.status));});
        if(process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0){CloseHandle(process);process=nullptr;status="Recognition stopped. Save / retry to restart.";}
        if(!status.empty()&&status!=e.status){e.status=status;return true;}return false;
    }
};
}
