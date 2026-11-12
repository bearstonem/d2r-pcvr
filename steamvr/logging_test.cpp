#include "logging.h"
#include "log_rotation.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

DWORD WINAPI Produce(void* id){
    for(int i=0;i<200;++i){char line[60];std::snprintf(line,sizeof line,"producer-%llu-record-%03d",(unsigned long long)id,i);framevr::logging::Write(line);}
    return 0;
}
static bool Read(const std::wstring& path,std::string& text){
    text.clear();FILE* file=_wfopen(path.c_str(),L"rb");if(!file)return false;
    char buf[4096];size_t n;while((n=fread(buf,1,sizeof buf,file)))text.append(buf,n);fclose(file);return true;
}
static bool Has(const std::string& text,const char* what){return text.find(what)!=text.npos;}
// One session: start, one marker line, stop.
static bool Session(const char* marker,uint64_t cap=0){
    if(!framevr::logging::Start(nullptr,cap))return false;
    framevr::logging::Write(marker);framevr::logging::Stop();return true;
}
int main(){
    // CMake places this executable in its own logging-fixture/plugins folder.
    // Reset only that fixture's output so repeated runs check a fresh session.
    wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);std::wstring p(path);
    p.resize(p.find_last_of(L"\\/"));p.resize(p.find_last_of(L"\\/")+1);p+=L"logs\\d2r-vr-frame.log";
    const std::wstring previous=framevr::logrotation::PreviousName(p);
    for(const auto& file:{p,previous}){
        SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL);
        if(!DeleteFileW(file.c_str())&&GetLastError()!=ERROR_FILE_NOT_FOUND&&GetLastError()!=ERROR_PATH_NOT_FOUND)return 6;
    }
    const auto folder=p.substr(0,p.find_last_of(L"\\/"));
    if(!RemoveDirectoryW(folder.c_str())&&GetLastError()!=ERROR_PATH_NOT_FOUND&&GetLastError()!=ERROR_FILE_NOT_FOUND)return 7;
    if(!framevr::logging::Start(nullptr))return 1;
    HANDLE threads[]={CreateThread(nullptr,0,Produce,(void*)1,0,nullptr),CreateThread(nullptr,0,Produce,(void*)2,0,nullptr)};
    if(!threads[0]||!threads[1])return 2;
    WaitForMultipleObjects(2,threads,TRUE,INFINITE);for(auto t:threads)CloseHandle(t);
    framevr::logging::Stop();framevr::logging::Write("after-stop-must-not-appear");
    std::string text,kept;
    if(!Read(p,text))return 3;
    if(Has(text,"after-stop-must-not-appear"))return 4;
    for(int producer=1;producer<=2;++producer){
        size_t previous=0;
        for(int i=0;i<200;++i){char line[60];std::snprintf(line,sizeof line,"producer-%d-record-%03d",producer,i);auto at=text.find(line);if(at==text.npos||(i&&at<=previous)||text.find(line,at+1)!=text.npos)return 5;previous=at;}
    }
    // A first session in an empty folder has nothing to put aside.
    if(GetFileAttributesW(previous.c_str())!=INVALID_FILE_ATTRIBUTES||!Has(text,"a new log"))return 10;

    // The next session: the one before is kept whole beside it, the log holds the new session only.
    const std::string first=text;
    if(!Session("second-session-marker"))return 11;
    if(!Read(p,text)||!Read(previous,kept))return 12;
    if(kept!=first)return 13;
    if(!Has(text,"second-session-marker")||Has(text,"producer-1-record-000")||!Has(text,"is kept as d2r-vr-frame.previous.log"))return 14;
    // The one after: the kept log is now the second session's; the first is gone (one previous, not a pile).
    if(!Session("third-session-marker"))return 15;
    if(!Read(p,text)||!Read(previous,kept))return 16;
    if(!Has(kept,"second-session-marker")||Has(kept,"producer-1-record-000")||!Has(text,"third-session-marker")||Has(text,"second-session-marker"))return 17;

    // The kept file cannot be replaced (open in a program that shares nothing): nothing is lost and the launch
    // does not fail - this session is appended to the log, as every session was before rotation.
    {
        HANDLE held=CreateFileW(previous.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(held==INVALID_HANDLE_VALUE)return 18;
        const bool ok=Session("fourth-session-marker");
        CloseHandle(held);
        if(!ok)return 19;
        std::string after;
        if(!Read(p,text)||!Read(previous,after))return 20;
        if(after!=kept)return 21;                                                                     // the kept log untouched
        if(!Has(text,"third-session-marker")||!Has(text,"fourth-session-marker")||!Has(text,"could not be put aside"))return 22;   // both sessions in one file
    }
    // The log itself is open elsewhere without sharing: the session cannot open it at all. Start fails, as it did
    // before (the plugin goes on without routine diagnostics), and both files are as they were.
    {
        std::string before;
        if(!Read(p,before))return 23;
        HANDLE held=CreateFileW(p.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(held==INVALID_HANDLE_VALUE)return 24;
        const bool started=framevr::logging::Start(nullptr);
        if(started)framevr::logging::Stop();
        CloseHandle(held);
        if(started)return 25;
        if(!Read(p,text)||!Read(previous,kept)||text!=before||!Has(kept,"second-session-marker"))return 26;
    }
    // Another running game (not supported) has the log open the way this logger opens it: the new session takes
    // the name, the other keeps writing its own file under the kept name. Neither file gets the other's lines.
    {
        HANDLE other=CreateFileW(p.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(other==INVALID_HANDLE_VALUE)return 27;
        const bool ok=Session("fifth-session-marker");
        DWORD done=0;const char late[]="other-instance-late-line\n";
        WriteFile(other,late,sizeof late-1,&done,nullptr);CloseHandle(other);
        if(!ok)return 28;
        if(!Read(p,text)||!Read(previous,kept))return 29;
        if(!Has(text,"fifth-session-marker")||Has(text,"other-instance-late-line")||Has(text,"fourth-session-marker"))return 30;
        if(!Has(kept,"fourth-session-marker")||!Has(kept,"other-instance-late-line")||Has(kept,"fifth-session-marker"))return 31;
    }
    // Within a session the file stops at its cap, on a whole line that says so, and later lines cost nothing.
    {
        const uint64_t cap=16*1024;
        if(!framevr::logging::Start(nullptr,cap))return 32;
        for(int round=0;round<12;++round){   // 2 KB a batch, 24 KB in all
            for(int i=0;i<20;++i){char line[80];std::snprintf(line,sizeof line,"cap-filler-round-%d-line-%03d-................................",round,i);framevr::logging::Write(line);}
            Sleep(300);   // past the writer's 250 ms batch
        }
        framevr::logging::Write("after-the-cap-must-not-appear");
        framevr::logging::Stop();
        if(!Read(p,text))return 33;
        const std::string limit="this session's log reached its 0 MB limit";   // (16 KB, in whole megabytes)
        const auto at=text.find(limit);
        if(at==text.npos||text.find(limit,at+1)!=text.npos)return 34;           // said once
        if(text.empty()||text.back()!='\n'||text.find('\n',at)!=text.size()-1)return 35;   // and it is the last line
        if(text.size()>cap+400||Has(text,"after-the-cap-must-not-appear"))return 36;
        if(!Has(text,"cap-filler-round-0-line-000"))return 37;   // the session's start is what is kept
        if(!Read(previous,kept)||!Has(kept,"fifth-session-marker"))return 38;   // the session before was put aside first
    }
    puts("Buffered logger: both producers preserved in order; shutdown drains pending records; "
         "the session before is kept as .previous.log, a failed rotation appends, a session stops at its cap");
    return 0;
}
