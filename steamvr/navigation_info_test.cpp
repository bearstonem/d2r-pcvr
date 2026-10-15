#include "navigation_info.h"
#include "interaction_paint.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace framevr;
int main() {
    constexpr uintptr_t base=0x100000;
    // Synthetic LIVE runtime row: independently verified compiler layout,
    // not the 404-byte archive format that caused all locations to be rejected.
    std::array<uint8_t,396> row{};row[4]=23;
    std::memcpy(row.data()+253,"test_area",10);
    bool readable=true,localized=true;
    int reads=0,lookups=0;
    const auto read=[&](uintptr_t p,auto& value){
        ++reads;if(!readable||p<base||p-base+sizeof value>row.size())return false;
        std::memcpy(&value,row.data()+p-base,sizeof value);return true;
    };
    const auto localize=[&](const char* key,char* output,uint32_t size){
        ++lookups;assert(!std::strcmp(key,"d2r:test_area"));
        std::snprintf(output,size,"Test area floor 3");return localized;
    };
    for(int act=0;act<5;++act) {
        row[13]=uint8_t(act);const auto info=navigation::ReadLocation(23,base,396,read,localize);
        assert(info.level==23&&info.act==act+1&&!std::strcmp(info.name,"Test area floor 3"));
        assert(std::strcmp(navigation::ActLabel(info.act),"ACT --"));
    }
    assert(!std::strcmp(navigation::ActLabel(1),"ACT I")&&!std::strcmp(navigation::ActLabel(5),"ACT V"));
    assert(!std::strcmp(navigation::ActLabel(-1),"ACT --")&&!std::strcmp(navigation::ActLabel(6),"ACT --"));
    reads=lookups=0;
    for(int size:{0,300,395,397,403,404,405})assert(navigation::ReadLocation(23,base,size,read,localize)==navigation::Location{});
    for(int level:{-1,0,65536})assert(navigation::ReadLocation(level,base,396,read,localize)==navigation::Location{});
    assert(navigation::ReadLocation(23,0,396,read,localize)==navigation::Location{});
    assert(navigation::ReadLocation(23,UINTPTR_MAX-100,396,read,localize)==navigation::Location{});
    assert(!reads&&!lookups); // unsupported rows/addresses never reach native readers
    assert(navigation::ReadLocation(24,base,396,read,localize)==navigation::Location{});
    row[13]=5;assert(navigation::ReadLocation(23,base,396,read,localize)==navigation::Location{});row[13]=0;
    readable=false;assert(navigation::ReadLocation(23,base,396,read,localize)==navigation::Location{});readable=true;
    assert(!lookups);
    row[253]=0;assert(navigation::ReadLocation(23,base,396,read,localize)==navigation::Location{});
    std::memset(row.data()+253,'x',40);assert(navigation::ReadLocation(23,base,396,read,localize)==navigation::Location{});
    assert(!lookups);std::memset(row.data()+253,0,40);std::memcpy(row.data()+253,"test_area",10);
    localized=false;const auto unavailable=navigation::ReadLocation(23,base,396,read,localize);
    assert(unavailable.level==23&&unavailable.act==1&&!unavailable.name[0]);
    assert(!std::strcmp(navigation::AreaLabel(unavailable),"Location unavailable"));
    const auto unterminated=navigation::ReadLocation(23,base,396,read,[](const char*,char* out,uint32_t size){
        std::memset(out,'x',size);return true;
    });
    assert(!unterminated.name[0]);
    // ID is a full uint16: reject a different row with the same low byte.
    row[5]=1;
    assert(navigation::ReadLocation(23,base,396,read,localize)==navigation::Location{});
    const auto extended=navigation::ReadLocation(279,base,396,read,localize);
    assert(extended.level==279&&extended.act==1);row[5]=0;
    localized=true;
    interaction::Snapshot snapshot;snapshot.session=1;snapshot.at=1000;
    snapshot.location=navigation::ReadLocation(23,base,396,read,localize);
    interaction::Output output;const auto painted=interaction::Context(snapshot,output,true);
    assert(interaction::LocationImageCurrent(painted,snapshot));
    // Old text must disappear as soon as an area/act/session changes, even
    // while the background painter still has the old atlas in flight.
    snapshot.location.level=24;assert(!interaction::LocationImageCurrent(painted,snapshot));
    snapshot.location=painted.location;snapshot.location.act=2;assert(!interaction::LocationImageCurrent(painted,snapshot));
    snapshot.location=painted.location;snapshot.location.name[0]='X';assert(!interaction::LocationImageCurrent(painted,snapshot));
    snapshot.location={};assert(!interaction::LocationImageCurrent(painted,snapshot));
    snapshot.location=painted.location;snapshot.session=2;assert(!interaction::LocationImageCurrent(painted,snapshot));
    snapshot.session=1;assert(!interaction::LocationImageCurrent(interaction::Context(snapshot,output,false),snapshot));
    assert(!interaction::Fresh(snapshot,1,1651)&&!interaction::Fresh(snapshot,1,999));
    // Transition through a town, outdoors and a dungeon, then another act.
    // Keys deliberately exceed eight characters: the old offset would truncate them.
    struct Area {uint16_t id;uint8_t act;const char* key;};
    for(const auto& area:{Area{1,0,"test_town_name"},Area{2,0,"test_outdoor_name"},
                         Area{23,0,"test_dungeon_floor"},Area{40,1,"test_next_act_town"}}) {
        row={};std::memcpy(row.data()+4,&area.id,2);row[13]=area.act;
        std::strcpy(reinterpret_cast<char*>(row.data()+253),area.key);
        const auto location=navigation::ReadLocation(area.id,base,396,read,[&](const char* key,char* out,uint32_t size){
            assert(!std::strncmp(key,"d2r:",4)&&!std::strcmp(key+4,area.key));
            std::snprintf(out,size,"%s",area.key);return true;
        });
        assert(location.level==area.id&&location.act==area.act+1&&!std::strcmp(location.name,area.key));
    }
    std::puts("Navigation info: live 396-byte layout, full level IDs, town/outdoor/dungeon transitions, localization and stale image guards passed");
}
