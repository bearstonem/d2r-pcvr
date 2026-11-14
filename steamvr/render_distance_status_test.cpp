#include "render_distance_status.h"
#include <cassert>
#include <string>
int main(int argc,char** argv){
    using namespace framevr::renderstatus;
    if(argc==2){
        wchar_t ini[MAX_PATH]{};assert(MultiByteToWideChar(CP_UTF8,0,argv[1],-1,ini,MAX_PATH));
        Writer fixture;char json[1000];const auto now=static_cast<unsigned long long>(GetTickCount64());
        std::snprintf(json,sizeof json,
            "{\"schema\":1,\"pid\":%lu,\"tick_ms\":%llu,\"in_world\":true,\"requested_rings\":8,"
            "\"requested_model_radius\":3260,\"active_model_radius\":3260,\"room_hooks\":true,"
            "\"tiler_ready\":true,\"append_ready\":true,\"radius_ready\":true,\"recalculate_ready\":false,"
            "\"expansion_state\":5,\"furthest_room\":4,\"rooms_added\":3,\"rooms_loaded\":2,"
            "\"tile_attempts\":3,\"last_expansion_ms\":%llu}",GetCurrentProcessId(),now,now);
        assert(fixture.Publish(ini,json));std::puts("READY");std::fflush(stdout);
        std::getchar();return 0;
    }
    wchar_t path[MAX_PATH];std::swprintf(path,MAX_PATH,L".\\runtime-status-test-%lu.ini",GetCurrentProcessId());
    const auto name=Name(path);assert(!name.empty());
    assert(Name(L".\\CASE-test.ini")==Name(L".\\case-TEST.ini"));
    Writer writer;assert(writer.Publish(path,"{\"value\":1}"));
    HANDLE mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());assert(mapping);
    auto* shared=static_cast<const Shared*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,MappingBytes));assert(shared);
    assert(shared->sequence==2&&std::strcmp(shared->json,"{\"value\":1}")==0);
    assert(writer.Publish(path,"{\"value\":2}"));assert(shared->sequence==4&&std::strcmp(shared->json,"{\"value\":2}")==0);
    assert(!writer.Publish(path,std::string(MappingBytes,'x').c_str()));
    assert(shared->sequence==4&&std::strcmp(shared->json,"{\"value\":2}")==0);
    UnmapViewOfFile(shared);CloseHandle(mapping);writer.Close();
    assert(!OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str()));
    std::puts("Runtime status mapping publication, read-only access, bounds and cleanup passed");
}
