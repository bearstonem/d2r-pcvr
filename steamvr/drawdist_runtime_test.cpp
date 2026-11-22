// Exercise the production adapter against owned fixtures and fake signature
// callbacks. Never reads the game, its binaries, or its logs.
#include "../cleanroom/drawdist/drawdist.cpp"
#include <array>
#include <cassert>
namespace fixture {
bool tilesReady=false,radiusReady=false,recalcReady=false,versionReady=true,tileSucceeds=false;
int hooks=0,checks=0;float radius=150;uint8_t version=1,recalc=0;
std::array<std::array<uint8_t,0xa0>,5> drlg{};
std::array<std::array<uint8_t,0x20>,5> tiles{};
std::array<void*,5> neighbours{};int level=0;
void Setter(float value){radius=value;}float Getter(){return radius;}
void Find(void*,void*,void*){}void Tick(void*){}
void* Ensure(uint32_t,void* node){
    if(!tileSucceeds)return nullptr;
    for(size_t i=0;i<drlg.size();++i)if(node==drlg[i].data()){
        d2rcam::drawdist::At<void*>(node,d2rcam::layout::drlgroom::Room)=tiles[i].data();return tiles[i].data();
    }
    assert(false);return nullptr;
}
void Append(void* list,void* const* element){
    using namespace d2rcam::drawdist;
    auto& count=At<size_t>(list,d2rcam::layout::roomlist::Size);
    At<void**>(list,d2rcam::layout::roomlist::Data)[count++]=*element;
}
}
namespace framevr::logging {void Write(const char*){}}
namespace d2rsig {
void Resolve(const D2RL::PluginContext*,bool){}
bool Check(uint64_t rva,const void*,uint32_t){
    using namespace d2rcam::layout;++fixture::checks;
    if(rva==EnsureRoomTiled.rva||rva==RoomListPushBack.rva)return fixture::tilesReady;
    if(rva==SetModelRadius.rva||rva==GetModelRadius.rva)return fixture::radiusReady;
    if(rva==ModelRecalcCheck.rva)return fixture::recalcReady;
    return rva==FindRooms.rva||rva==LevelSimTick.rva;
}
uintptr_t Addr(uint64_t rva){
    using namespace d2rcam::layout;
    if(rva==EnsureRoomTiled.rva)return reinterpret_cast<uintptr_t>(&fixture::Ensure);
    if(rva==RoomListPushBack.rva)return reinterpret_cast<uintptr_t>(&fixture::Append);
    if(rva==SetModelRadius.rva)return reinterpret_cast<uintptr_t>(&fixture::Setter);
    if(rva==GetModelRadius.rva)return reinterpret_cast<uintptr_t>(&fixture::Getter);
    if(rva==ModelRecalcEveryFrame)return reinterpret_cast<uintptr_t>(&fixture::recalc);
    if(rva==GameVersionByte)return fixture::versionReady?reinterpret_cast<uintptr_t>(&fixture::version):0;
    return 0;
}
bool Hook(uint64_t rva,const void*,uint32_t,void*,void** original){
    ++fixture::hooks;
    *original=rva==d2rcam::layout::FindRooms.rva?reinterpret_cast<void*>(&fixture::Find):reinterpret_cast<void*>(&fixture::Tick);
    return true;
}
}
int main(){
    using namespace d2rcam::drawdist;using namespace d2rcam::layout;
    Refresh();assert(fixture::checks==0);
    D2RL::PluginContext ctx{};ctx.exeBase=1;
    assert(Install(&ctx)&&Installed()&&fixture::hooks==2);
    auto s=ReadStatus();assert(!s.tiler&&!s.append&&!s.modelRadius&&s.activeModelRadius==-1);
    assert(!d2rcam::drawdist::SetModelRadius(3260));
    const int initialChecks=fixture::checks;Refresh();assert(fixture::checks==initialChecks);
    fixture::tilesReady=true;g_nextBindingsTry.store(0);
    assert(Install(&ctx));s=ReadStatus();assert(s.tiler&&s.append&&!s.modelRadius&&fixture::hooks==2);
    fixture::radiusReady=true;fixture::recalcReady=true;g_nextBindingsTry.store(0);
    Refresh();assert(d2rcam::drawdist::SetModelRadius(3260));s=ReadStatus();
    assert(s.modelRadius&&s.recalculate&&s.activeModelRadius==3260&&fixture::recalc==1);
    assert(d2rcam::drawdist::SetModelRadius(0)&&ModelRadius()==10);assert(d2rcam::drawdist::SetModelRadius(20000)&&ModelRadius()==10000);
    g_nextBindingsTry.store(0);const int resolvedChecks=fixture::checks;Refresh();assert(fixture::checks==resolvedChecks);
    SetRadius(8);assert(ReadStatus().rings==8);
    // Construct a five-room chain. Rooms3 and4 need tiling. Each graph pass
    // may attempt only one tile, and failed attempts are not successful loads.
    void* entries[16]{fixture::tiles[0].data()};struct {void** data;size_t count;} list{entries,1};
    for(size_t i=0;i<fixture::drlg.size();++i){
        auto* node=fixture::drlg[i].data();auto* tile=fixture::tiles[i].data();
        At<void*>(tile,room::Drlg)=node;At<void*>(node,drlgroom::Level)=&fixture::level;
        At<void*>(node,drlgroom::Room)=i<3?tile:nullptr;
        fixture::neighbours[i]=i+1<fixture::drlg.size()?fixture::drlg[i+1].data():nullptr;
        At<void**>(node,drlgroom::NearData)=&fixture::neighbours[i];
        At<size_t>(node,drlgroom::NearCount)=fixture::neighbours[i]?1:0;
    }
    LoadAndAdd(fixture::tiles[0].data(),&list,8);s=ReadStatus();
    assert(s.expansion==5&&s.furthestRoom==2&&s.tileAttempts==1&&s.roomsLoaded==0);
    fixture::tileSucceeds=true;LoadAndAdd(fixture::tiles[0].data(),&list,8);s=ReadStatus();
    assert(s.furthestRoom==3&&s.tileAttempts==2&&s.roomsLoaded==1);
    LoadAndAdd(fixture::tiles[0].data(),&list,8);s=ReadStatus();
    assert(s.furthestRoom==4&&s.tileAttempts==3&&s.roomsLoaded==2);
    fixture::versionReady=false;LoadAndAdd(fixture::tiles[0].data(),&list,8);assert(ReadStatus().expansion==2);
    fixture::versionReady=true;fixture::version=9;LoadAndAdd(fixture::tiles[0].data(),&list,8);assert(ReadStatus().expansion==3);
    std::puts("Draw-distance late-binding, readback, tiling budget and success counters passed (owned fixtures)");
}
