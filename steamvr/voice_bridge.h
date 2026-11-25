#pragma once
#include <windows.h>
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
// Local-session mapping, guarded by an OS mutex. The game never waits for the helper.
class Bridge {
    HANDLE mapping=nullptr,mutex=nullptr;Shared* data=nullptr;
public:
    ~Bridge(){Close();}
    void Close(){if(data)UnmapViewOfFile(data);if(mapping)CloseHandle(mapping);if(mutex)CloseHandle(mutex);data=nullptr;mapping=mutex=nullptr;}
    bool Open(bool create){
        if(data)return true;
        mutex=CreateMutexW(nullptr,FALSE,L"Local\\D2RVRVoicePrototypeMutex1");
        mapping=create?CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),L"Local\\D2RVRVoicePrototype1"):
            OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,L"Local\\D2RVRVoicePrototype1");
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
