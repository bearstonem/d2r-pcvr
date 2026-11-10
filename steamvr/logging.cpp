#include "logging.h"
#include "log_rotation.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace framevr::logging {
namespace {
// Bounded queue: game/render callbacks never perform file IO, wait for a
// writer, or force durable filesystem commits. Diagnostics may be dropped
// under extreme pressure; gameplay must keep moving.
SRWLOCK lock=SRWLOCK_INIT;
struct Row {char text[640];};
std::array<Row,1024> rows;
uint32_t count=0,dropped=0;
bool accepting=false;
HANDLE file=INVALID_HANDLE_VALUE,stop=nullptr,worker=nullptr;
// This session's bytes in the file and where it stops (log_rotation.h); the writer thread's alone once started.
uint64_t written=0,cap=logrotation::SessionCapBytes;
bool WriteAll(const char* data,size_t size){
    size_t at=0;
    while(at<size){
        DWORD done=0;
        if(!WriteFile(file,data+at,DWORD(size-at),&done,nullptr)||!done)break;
        at+=done;
    }
    written+=at;return at==size;
}
DWORD WINAPI Writer(void*) {
    std::string batch;batch.reserve(640*1024);
    for(;;){
        const bool ending=WaitForSingleObject(stop,250)==WAIT_OBJECT_0;
        batch.clear();
        AcquireSRWLockExclusive(&lock);
        for(uint32_t i=0;i<count;++i)batch+=rows[i].text;
        if(dropped){char line[100];std::snprintf(line,sizeof line,"Frame logger: dropped %u diagnostic records (queue full)\n",dropped);batch+=line;}
        count=dropped=0;
        ReleaseSRWLockExclusive(&lock);
        if(logrotation::Fits(written,batch.size(),cap))WriteAll(batch.data(),batch.size());
        else{
            // The session's log is full: one last line, and nothing more is queued or written. The thread only
            // waits for Stop from here.
            AcquireSRWLockExclusive(&lock);accepting=false;count=dropped=0;ReleaseSRWLockExclusive(&lock);
            char text[200],line[280];logrotation::LimitLine(cap,text,sizeof text);
            SYSTEMTIME t{};GetLocalTime(&t);
            const int n=std::snprintf(line,sizeof line,"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [INFO] [Frame] %s\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,text);
            if(n>0)WriteAll(line,size_t(n)<sizeof line?size_t(n):sizeof line-1);
            if(!ending)WaitForSingleObject(stop,INFINITE);
            break;
        }
        // Deliberately no FlushFileBuffers: the SDK's per-message fsync was
        // observed blocking the game's main thread for hundreds of ms.
        if(ending)break;
    }
    return 0;
}
}
bool Start(HMODULE plugin,uint64_t sessionCap){
    wchar_t path[MAX_PATH];const DWORD len=GetModuleFileNameW(plugin,path,MAX_PATH);
    if(!len||len>=MAX_PATH)return false;
    std::wstring p(path);const auto last=p.find_last_of(L"\\/");if(last==p.npos)return false;
    p.resize(last);const auto parent=p.find_last_of(L"\\/");if(parent==p.npos)return false;
    p.resize(parent+1);p+=L"logs";
    // A fresh mod-folder install has no log directory until its first launch.
    if(!CreateDirectoryW(p.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    p+=L"\\d2r-vr-frame.log";
    // The session before is put aside as d2r-vr-frame.previous.log, in place of the one kept there, and this
    // session starts an empty file. Whatever stops the rename - the file open in a program that does not share
    // it, a read-only folder - leaves the log where it is and this session appends to it, as every session used to.
    // A second game running (not supported) keeps writing its own file under its new name; nothing is mixed.
    const std::wstring previous=logrotation::PreviousName(p);
    WIN32_FILE_ATTRIBUTE_DATA info{};
    const bool exists=GetFileAttributesExW(p.c_str(),GetFileExInfoStandard,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY);
    const uint64_t oldSize=exists?(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow:0;
    const bool rotate=logrotation::RotateAtStart(exists,oldSize);
    const bool rotated=rotate&&MoveFileExW(p.c_str(),previous.c_str(),MOVEFILE_REPLACE_EXISTING);
    const DWORD rotateError=rotate&&!rotated?GetLastError():0;
    written=0;cap=sessionCap?sessionCap:logrotation::SessionCapBytes;
    file=CreateFileW(p.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(stop)worker=CreateThread(nullptr,0,&Writer,nullptr,0,nullptr);
    if(!worker){if(stop)CloseHandle(stop);CloseHandle(file);stop=nullptr;file=INVALID_HANDLE_VALUE;return false;}
    AcquireSRWLockExclusive(&lock);accepting=true;ReleaseSRWLockExclusive(&lock);
    Write("Frame logger: buffered diagnostics started; routine logging does not force disk commits");
    char line[240];
    if(rotated)std::snprintf(line,sizeof line,"Frame logger: the session before (%.1f MB) is kept as d2r-vr-frame.previous.log; this file holds this session only, up to %llu MB",oldSize/1048576.0,(unsigned long long)(cap>>20));
    else if(rotate)std::snprintf(line,sizeof line,"Frame logger: the earlier log (%.1f MB) could not be put aside (Windows error %lu - is it open in another program?); this session is appended to it",oldSize/1048576.0,(unsigned long)rotateError);
    else std::snprintf(line,sizeof line,"Frame logger: a new log; this file holds this session only, up to %llu MB",(unsigned long long)(cap>>20));
    Write(line);return true;
}
void Write(const char* text){
    if(!text)return;
    SYSTEMTIME t{};GetLocalTime(&t);
    // The short queue lock cannot be held across disk IO by the writer.
    AcquireSRWLockExclusive(&lock);
    if(accepting){
        if(count<rows.size())std::snprintf(rows[count++].text,sizeof rows[0].text,"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [INFO] [Frame] %s\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,text);
        else ++dropped;
    }
    ReleaseSRWLockExclusive(&lock);
}
void Stop(){
    AcquireSRWLockExclusive(&lock);accepting=false;ReleaseSRWLockExclusive(&lock);
    if(worker){SetEvent(stop);WaitForSingleObject(worker,INFINITE);CloseHandle(worker);worker=nullptr;}
    if(stop){CloseHandle(stop);stop=nullptr;}
    if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);file=INVALID_HANDLE_VALUE;}
}
}
