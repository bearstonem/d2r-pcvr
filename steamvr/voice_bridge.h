#pragma once
#include <windows.h>
#include <cstddef>
#include <string>
#include "voice_commands.h"
namespace framevr::voice {
inline constexpr uint32_t Magic=0x564f4931;
struct Skill {int id=-1;char name[96]{};};
struct Shared {
    uint32_t magic=0;
    uint64_t gameAt=0,helperAt=0,session=0;
    bool enabled=false,ready=false;unsigned count=0;
    Skill skills[140]{};
    Command command;
    uint64_t ack=0;char status[128]{};
};
// Shared with the C# speech GUI; fail compilation if its wire offsets drift.
static_assert(sizeof(Shared)==14208 && offsetof(Shared,skills)==40 && offsetof(Shared,command)==14040 &&
    offsetof(Shared,ack)==14072 && offsetof(Shared,status)==14080 && offsetof(Shared,enabled)==32 && offsetof(Shared,count)==36);
// Local-session mapping, guarded by an OS mutex. The game never waits for the helper.
class Bridge {
    HANDLE mapping=nullptr,mutex=nullptr;Shared* data=nullptr;
public:
    ~Bridge(){Close();}
    void Close(){if(data)UnmapViewOfFile(data);if(mapping)CloseHandle(mapping);if(mutex)CloseHandle(mutex);data=nullptr;mapping=mutex=nullptr;}
    bool Open(bool create,DWORD game=0){
        if(data)return true;
        const auto suffix=game?L"-game-"+std::to_wstring(game):std::wstring{};
        const auto mappingName=L"Local\\D2RVRVoicePrototype1"+suffix,mutexName=L"Local\\D2RVRVoicePrototypeMutex1"+suffix;
        mutex=CreateMutexW(nullptr,FALSE,mutexName.c_str());
        mapping=create?CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),mappingName.c_str()):
            OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,mappingName.c_str());
        if(mapping)data=(Shared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared));
        if(!data||!mutex){Close();return false;}
        return true;
    }
    template<class F> bool With(F f){
        if(!data)return false;
        const DWORD result=WaitForSingleObject(mutex,0);
        if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED)return false;
        if(data->magic!=Magic){*data=Shared{};data->magic=Magic;}
        f(*data);ReleaseMutex(mutex);return true;
    }
};
}
