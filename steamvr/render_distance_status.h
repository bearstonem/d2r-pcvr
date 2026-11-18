#pragma once
// Current status only, shared read-only with the desktop editor. No disk writes,
// process-memory inspection, historical logs or game assets are involved.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
namespace framevr::renderstatus {
inline constexpr size_t MappingBytes=4096;
struct Shared {volatile LONG sequence;char json[MappingBytes-sizeof(LONG)];};
static_assert(sizeof(Shared)==MappingBytes);
inline std::wstring Name(const wchar_t* ini){
    wchar_t absolute[MAX_PATH]{};
    const DWORD size=GetFullPathNameW(ini,MAX_PATH,absolute,nullptr);
    if(!size||size>=MAX_PATH)return {};
    uint64_t hash=1469598103934665603ull;
    for(const wchar_t* p=absolute;*p;++p){
        uint16_t c=uint16_t(*p);if(c>='A'&&c<='Z')c+=32;
        hash=(hash^uint8_t(c))*1099511628211ull;
        hash=(hash^uint8_t(c>>8))*1099511628211ull;
    }
    wchar_t name[100];std::swprintf(name,100,L"Local\\D2RPCVR-RenderDistanceStatus-%016llx",static_cast<unsigned long long>(hash));
    return name;
}
class Writer {
    HANDLE mapping=nullptr;Shared* shared=nullptr;std::wstring name;
public:
    Writer()=default;Writer(const Writer&)=delete;Writer& operator=(const Writer&)=delete;
    ~Writer(){Close();}
    void Close(){if(shared)UnmapViewOfFile(shared);if(mapping)CloseHandle(mapping);shared=nullptr;mapping=nullptr;name.clear();}
    bool Publish(const wchar_t* ini,const char* json){
        const size_t size=std::strlen(json);if(size>=sizeof(Shared::json))return false;
        const auto requested=Name(ini);if(requested.empty())return false;
        if(requested!=name){
            Close();mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,MappingBytes,requested.c_str());
            if(!mapping)return false;
            shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,MappingBytes));
            if(!shared){Close();return false;}name=requested;
            // The editor never creates a mapping. A stale reader may briefly
            // keep one alive between game sessions; restore an even sequence.
            InterlockedExchange(&shared->sequence,0);
        }
        InterlockedIncrement(&shared->sequence);MemoryBarrier();
        std::memcpy(shared->json,json,size+1);
        MemoryBarrier();InterlockedIncrement(&shared->sequence);return true;
    }
};
}
