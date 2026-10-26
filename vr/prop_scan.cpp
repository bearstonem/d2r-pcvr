// Knockable level props, D2R 3.3.93787 (prop_scan.h). The HD renderer's entt
// registry (skeletons.cpp): pools at registry+0/+8 in 0x28-byte slots, the
// storage at slot+0x20; a storage keeps sparse pages at +0x08/+0x10, packed
// entities at +0x20/+0x28 and its components contiguously at +0x38/+0x40.
// Three storages matter. Pool numbers are runtime type numbers, so each is
// told apart by what it holds:
//  - transforms, 0xd0 bytes: local matrix +0x00, world +0x40, the world
//    before that +0x80 (UpdateTransform 0xe3a8c0 copies +0x40 there before it
//    writes a new one), parent entity +0xc0. Static level models are never
//    updated again, and the renderer draws them from +0x40 every frame;
//  - preset ids, 4 bytes: the level preset entity's "id" (prepare_props.py);
//  - bounds, 0x60 bytes: local min/max (vec4) twice, then world min/max.
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "prop_scan.h"
#include "dev_scenarios.h"
#include "../steamvr/latest_worker.h"
#include "../steamvr/transport.h"
#include "sigscan.h"
#include "memory.h"
#include "logging.h"

namespace propscan {
namespace {
using namespace framevr;
using props::Box;
using math::Vec;
constexpr uint64_t RegistryRva=0x2677170;
constexpr size_t TransformSize=0xd0,BoundsSize=0x60,IdSize=4,MaxPools=4096,MaxEntities=200000,Chunk=512;
constexpr uint64_t Interval=500,Rediscover=1000,Keep=2000;
constexpr float PropReach=3,SupportReach=6,Below=.5f,Above=2.5f; // metres around the player
constexpr unsigned MaxNear=4096,MaxPending=64;
constexpr uint32_t NoParent=0xfffff;

template<class T> bool Read(uintptr_t p,T& v){return p>0x10000&&framevr::ReadMemory(&v,reinterpret_cast<const void*>(p),sizeof v);}
bool ReadBytes(uintptr_t p,void* out,size_t n){return p>0x10000&&n&&framevr::ReadMemory(out,reinterpret_cast<const void*>(p),n);}
bool Same(float a,float b){return std::fabs(a-b)<=1e-3f+1e-6f*std::fabs(a);}
bool Same(const float* a,const float* b,int n){for(int i=0;i<n;++i)if(!Same(a[i],b[i]))return false;return true;}
bool Finite(const float* v,int n){for(int i=0;i<n;++i)if(!std::isfinite(v[i]))return false;return true;}

struct Storage {
    uintptr_t object=0,sparse=0,sparseEnd=0,packed=0,payload=0;
    size_t count=0,elem=0;
    bool Load(uintptr_t at) {
        uintptr_t h[9]{};
        *this={};
        if(!ReadBytes(at,h,sizeof h))return false;
        object=at;sparse=h[1];sparseEnd=h[2];packed=h[4];payload=h[7];
        count=h[5]>=h[4]&&(h[5]-h[4])%4==0?(h[5]-h[4])/4:0;
        elem=count&&h[8]>h[7]&&(h[8]-h[7])%count==0?(h[8]-h[7])/count:0;
        return count&&count<=MaxEntities&&elem&&elem<=0x1000;
    }
    bool At(uint32_t index,uint32_t entity) const {
        uint32_t back=0;
        return index<count&&Read(packed+uintptr_t(index)*4,back)&&back==entity;
    }
    // The dense index of an entity, as the renderer's views find it.
    bool Dense(uint32_t entity,uint32_t& index) const {
        const size_t page=(entity>>8)&0xfff;uintptr_t pagePtr=0;uint32_t v=0;
        if(sparseEnd<sparse||page>=(sparseEnd-sparse)/8||!Read(sparse+page*8,pagePtr)||!pagePtr||!Read(pagePtr+(entity&255)*4,v))return false;
        v&=0xfffff;
        if(v==0xfffff||!At(v,entity))return false;
        index=v;return true;
    }
    template<class T> bool Copy(std::vector<uint32_t>& ids,std::vector<T>& items) const {
        ids.resize(count);items.resize(count*elem/sizeof(T));
        return elem%sizeof(T)==0&&ReadBytes(packed,ids.data(),count*4)&&ReadBytes(payload,items.data(),count*elem);
    }
};
// An entity near the player, from its transform.
struct Near {
    uint32_t entity=0,index=0,parent=0,boundsIndex=0,preset=0;   // preset: its level preset id, catalogued or not
    float local[3]{},world[16]{};
    int kind=-1;
    bool reach=false,bounded=false;
    Box localBounds{},worldBounds{};
};
// A prop the physics may move, and what the write pass knows of it.
struct Tracked {
    uint32_t entity=0,parent=0,transformIndex=0,boundsIndex=0;
    float local[3]{},world[16]{};
    Box bounds{};
    bool hasBounds=false,settle=false;
    uint64_t writtenAt=0;
};
struct Job {Request request;int idPool=-1,boundsPool=-1;bool discover=false,unknown=false;};
// A level entity in reach the catalog does not know (pcvr-dev's log, to map more
// props): its preset id and local position (prepare_props.py --lookup), its size.
struct Unknown {uint32_t preset=0;float local[3]{},size[3]{},distance=0;};
constexpr unsigned MaxUnknown=12;
constexpr float UnknownLargest=2.5f;   // metres: walls, floors and buildings are larger
struct Found {
    bool ok=false;
    const char* why="";
    props::Scan scan{};
    std::vector<Tracked> tracked;
    uintptr_t transforms=0,bounds=0;
    int idPool=-1,boundsPool=-1;
    unsigned nearby=0,matched=0;
    unsigned unbounded=0,unrigid=0,sized=0,beyond=0; // catalogued props in reach left out, and why
    std::vector<Unknown> unknown;   // nearest first, pcvr-dev only
    double ms=0;
};
props::Catalog catalog;
std::atomic<bool> enabled{true},catalogReady{false};

std::vector<uintptr_t> Pools() {
    std::vector<uintptr_t> objects;
    const auto registry=d2rsig::Addr(RegistryRva);
    uintptr_t begin=0,end=0;
    if(!registry||!Read(registry,begin)||!Read(registry+8,end)||end<=begin||(end-begin)%40||(end-begin)/40>MaxPools)return objects;
    std::vector<uint8_t> slots(end-begin);
    if(!ReadBytes(begin,slots.data(),slots.size()))return objects;
    objects.resize(slots.size()/40);
    for(size_t i=0;i<objects.size();++i)std::memcpy(&objects[i],slots.data()+i*40+0x20,8);
    return objects;
}
// The transform storage: the largest with 0xd0-byte components whose first
// matrices look like matrices.
bool Transforms(const std::vector<uintptr_t>& pools,Storage& out) {
    out={};
    for(const auto object:pools) {
        Storage s;float m[32];
        if(!object||!s.Load(object)||s.elem!=TransformSize||s.count<=out.count||!ReadBytes(s.payload,m,sizeof m))continue;
        if(m[15]==1&&m[31]==1&&m[3]==0&&m[7]==0&&m[11]==0&&m[19]==0&&m[23]==0&&m[27]==0)out=s;
    }
    return out.count>0;
}
Found Work(const Job& job) {
    Found f;
    const auto started=std::chrono::steady_clock::now();
    const auto& r=job.request;const float u=r.unitsPerMetre;
    f.scan.session=r.session;f.scan.upm=u;f.scan.floor=r.floor;
    const auto pools=Pools();
    Storage transforms;
    if(pools.empty()){f.why="the renderer's registry does not read";return f;}
    if(!Transforms(pools,transforms)){f.why="no transform storage";return f;}
    f.transforms=transforms.object;
    // Entities near the player, by their world origin.
    std::vector<uint32_t> ids(transforms.count);
    if(!ReadBytes(transforms.packed,ids.data(),ids.size()*4)){f.why="the transforms moved while read";return f;}
    std::vector<Near> nearby;std::unordered_map<uint32_t,uint32_t> nearIndex;
    std::vector<float> block(Chunk*TransformSize/4);
    for(size_t at=0;at<transforms.count&&nearby.size()<MaxNear;at+=Chunk) {
        const size_t n=std::min(Chunk,transforms.count-at);
        if(!ReadBytes(transforms.payload+at*TransformSize,block.data(),n*TransformSize))continue;
        for(size_t i=0;i<n&&nearby.size()<MaxNear;++i) {
            const float* m=block.data()+i*(TransformSize/4);
            const float dx=m[28]-r.centre[0],dy=m[29]-r.floor,dz=m[30]-r.centre[2];
            const float h=std::hypot(dx,dz);
            if(!(h<=SupportReach*u)||!(dy>=-Below*u&&dy<=(Above+.5f)*u))continue;
            Near e;e.entity=ids[at+i];e.index=uint32_t(at+i);
            std::memcpy(&e.parent,m+48,4);std::memcpy(e.local,m+12,12);std::memcpy(e.world,m+16,64);
            e.reach=h<=PropReach*u&&dy<=Above*u;
            if(!Finite(e.world,16))continue;
            nearIndex[e.entity]=uint32_t(nearby.size());nearby.push_back(e);
        }
    }
    f.nearby=unsigned(nearby.size());
    // Preset ids: the 4-byte storage whose values the catalog knows at these
    // entities' local positions.
    std::vector<uint32_t> packed;std::vector<uint32_t> values;
    auto matchIds=[&](const Storage& s,bool apply) {
        unsigned matched=0;
        if(!s.Copy(packed,values))return 0u;
        for(size_t i=0;i<s.count;++i) {
            const auto it=nearIndex.find(packed[i]);
            if(it==nearIndex.end())continue;
            auto& e=nearby[it->second];
            if((e.parent&NoParent)==NoParent)continue;   // not part of a level preset
            if(apply)e.preset=values[i];
            const int kind=catalog.Find(values[i],e.local);
            if(kind<0)continue;
            ++matched;if(apply)e.kind=kind;
        }
        return matched;
    };
    Storage idStorage;
    if(job.idPool>=0&&size_t(job.idPool)<pools.size()&&idStorage.Load(pools[size_t(job.idPool)])&&idStorage.elem==IdSize)f.idPool=job.idPool;
    else if(job.discover) {
        unsigned best=0;
        for(size_t i=0;i<pools.size();++i) {
            Storage s;
            if(!pools[i]||!s.Load(pools[i])||s.elem!=IdSize||s.count<64)continue;
            const unsigned m=matchIds(s,false);
            if(m>best){best=m;f.idPool=int(i);idStorage=s;}
        }
    }
    if(f.idPool>=0)f.matched=matchIds(idStorage,true);
    // Bounds: the 0x60-byte storage whose world box is its local box under
    // the entity's world matrix.
    std::vector<float> boxes;
    auto matchBounds=[&](const Storage& s,bool apply,unsigned& checked) {
        unsigned agree=0;checked=0;
        if(!s.Copy(packed,boxes))return 0u;
        for(size_t i=0;i<s.count;++i) {
            const auto it=nearIndex.find(packed[i]);
            if(it==nearIndex.end())continue;
            auto& e=nearby[it->second];const float* b=boxes.data()+i*(BoundsSize/4);
            if(!Finite(b,24))continue;
            const Box local{{b[0],b[1],b[2]},{b[4],b[5],b[6]}},world{{b[16],b[17],b[18]},{b[20],b[21],b[22]}};
            const float scale=std::max({std::fabs(world.lo.x),std::fabs(world.hi.x),std::fabs(world.lo.z),std::fabs(world.hi.z),1.f});
            ++checked;
            if(!props::Near(props::WorldBounds(local,e.world),world,.02f+scale*2e-5f))continue;
            ++agree;
            if(apply){e.bounded=true;e.boundsIndex=uint32_t(i);e.localBounds=local;e.worldBounds=world;}
        }
        return agree;
    };
    Storage boundsStorage;unsigned checked=0;
    if(job.boundsPool>=0&&size_t(job.boundsPool)<pools.size()&&boundsStorage.Load(pools[size_t(job.boundsPool)])&&boundsStorage.elem==BoundsSize)
        f.boundsPool=job.boundsPool;
    else if(job.discover&&f.matched) {
        unsigned best=0;
        for(size_t i=0;i<pools.size();++i) {
            Storage s;
            if(!pools[i]||!s.Load(pools[i])||s.elem!=BoundsSize)continue;
            const unsigned agree=matchBounds(s,false,checked);
            if(agree>=3&&agree*2>=checked&&agree>best){best=agree;f.boundsPool=int(i);boundsStorage=s;}
        }
    }
    if(f.boundsPool<0){f.why=f.matched?"no bounds storage":"no catalogued props nearby";f.ok=true;return f;}
    matchBounds(boundsStorage,true,checked);
    f.bounds=boundsStorage.object;
    // What the props rest on: level entities that are not props themselves.
    std::vector<Box> around;
    for(const auto& e:nearby)
        if(e.bounded&&e.kind<0&&(e.parent&NoParent)!=NoParent&&props::Finite(e.worldBounds.lo)&&props::Finite(e.worldBounds.hi)&&
           e.worldBounds.lo.x<=e.worldBounds.hi.x&&e.worldBounds.lo.y<=e.worldBounds.hi.y&&e.worldBounds.lo.z<=e.worldBounds.hi.z)
            around.push_back(e.worldBounds);
    if(job.unknown) {
        for(const auto& e:nearby) {
            props::Placement at;
            if(e.kind>=0||!e.reach||!e.preset||!e.bounded||!props::Decompose(e.world,at))continue;
            const Vec size=e.localBounds.hi-e.localBounds.lo;
            Unknown n;n.preset=e.preset;std::memcpy(n.local,e.local,12);
            n.size[0]=size.x*at.scale.x/u;n.size[1]=size.y*at.scale.y/u;n.size[2]=size.z*at.scale.z/u;
            if(std::max({n.size[0],n.size[1],n.size[2]})>UnknownLargest)continue;
            n.distance=std::hypot(e.world[12]-r.centre[0],e.world[14]-r.centre[2])/u;
            f.unknown.push_back(n);
        }
        std::sort(f.unknown.begin(),f.unknown.end(),[](const Unknown& a,const Unknown& b){return a.distance<b.distance;});
        if(f.unknown.size()>MaxUnknown)f.unknown.resize(MaxUnknown);
    }
    std::vector<const Near*> found;
    for(const auto& e:nearby) {
        if(e.kind<0||!e.reach)continue;
        if(!e.bounded){++f.unbounded;continue;}
        props::Placement at;
        if(!props::Decompose(e.world,at)){++f.unrigid;continue;}
        const Vec size=e.localBounds.hi-e.localBounds.lo;
        const float x=size.x*at.scale.x/u,y=size.y*at.scale.y/u,z=size.z*at.scale.z/u;
        if(std::max({x,y,z})>props::MaxExtent||std::min({x,y,z})<props::MinExtent){++f.sized;continue;}
        found.push_back(&e);
    }
    auto distance=[&](const Near* e){return std::hypot(e->world[12]-r.centre[0],e->world[14]-r.centre[2]);};
    std::sort(found.begin(),found.end(),[&](const Near* a,const Near* b){return distance(a)<distance(b);});
    if(found.size()>props::MaxSeen){f.beyond=unsigned(found.size()-props::MaxSeen);found.resize(props::MaxSeen);}
    // What each rests on: another of these props (its body holds it), or a
    // surface; guessed surfaces side by side join into one.
    std::vector<Box> placed;for(const auto* e:found)placed.push_back(props::WorldBounds(e->localBounds,e->world));
    std::vector<props::Support> supports(found.size());
    for(size_t i=0;i<found.size();++i) {
        bool onProp=false;
        for(size_t j=0;j<found.size()&&!onProp;++j)onProp=j!=i&&props::RestsOn(placed[i],placed[j],u);
        if(onProp){supports[i]={false,placed[i].lo.y};supports[i].onProp=true;}
        else supports[i]=props::FindSupport(placed[i],around,r.floor,u);
    }
    props::JoinLedges(supports,u);
    for(size_t i=0;i<found.size();++i) {
        const auto* e=found[i];
        auto& s=f.scan.seen[f.scan.count++];
        const auto& kind=catalog.KindAt(unsigned(e->kind));
        s.entity=e->entity;s.kind=uint16_t(e->kind);s.shape=kind.shape;s.material=kind.material;
        std::memcpy(s.world,e->world,64);s.local=e->localBounds;
        s.support=supports[i];
        Tracked t;t.entity=e->entity;t.parent=e->parent;t.transformIndex=e->index;t.boundsIndex=e->boundsIndex;
        std::memcpy(t.local,e->local,12);std::memcpy(t.world,e->world,64);t.bounds=e->localBounds;t.hasBounds=true;
        f.tracked.push_back(t);
    }
    f.ok=true;
    f.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    return f;
}
LatestWorker<Job,Found> worker(Work);
// XR thread only.
uint64_t job=0,submittedAt=0,discoveredAt=0,serial=0;
int idPool=-1,boundsPool=-1;
const char* lastWhy="";
// The write pass and the XR thread.
std::mutex trackMutex;
std::vector<Tracked> tracked;
std::vector<uint32_t> lostPending;
std::atomic<uintptr_t> transformsObject{0},boundsObject{0};
std::atomic<bool> active{false},passing{false};
std::atomic<uint64_t> passAt{0};
std::mutex poseMutex;
props::Poses pending{};
uint64_t applied=0; // the write pass only
std::atomic<unsigned> scans{0},failed{0},seenProps{0},matchedProps{0},writes{0},settles{0},lost{0},boundsRefused{0},passes{0};
std::atomic<unsigned> struckKnocks{0},chainedKnocks{0},mostSeen{0};
std::atomic<unsigned> leftOut[4]{}; // the last scan's catalogued props in reach left out: unbounded, unrigid, size, beyond the nearest
uint64_t reportedAt=0; // Report's caller only
std::vector<Unknown> unknownNow;std::vector<uint32_t> unknownLogged;   // the XR thread (Latest, Report)
std::atomic<uint32_t> scanMicros{0};

void Lose(size_t i) {
    if(lostPending.size()<MaxPending)lostPending.push_back(tracked[i].entity);
    tracked.erase(tracked.begin()+ptrdiff_t(i));++lost;
}
void Pass() {
    std::unique_lock lock(trackMutex,std::try_to_lock);
    if(!lock||tracked.empty())return;
    props::Poses poses;bool fresh=false;
    {
        std::lock_guard l(poseMutex);
        if(pending.serial!=applied){poses=pending;applied=pending.serial;fresh=true;}
    }
    bool due=fresh;
    for(const auto& t:tracked)due|=t.settle;
    if(!due)return;
    Storage transforms,bounds;
    if(!transforms.Load(transformsObject.load())||transforms.elem!=TransformSize)return;
    const bool haveBounds=bounds.Load(boundsObject.load())&&bounds.elem==BoundsSize;
    const auto now=GetTickCount64();
    ++passes;
    for(size_t i=tracked.size();i-->0;) {
        auto& t=tracked[i];
        const props::Pose* pose=nullptr;
        if(fresh)for(unsigned k=0;k<std::min<unsigned>(poses.count,props::MaxMoving);++k)if(poses.poses[k].entity==t.entity)pose=&poses.poses[k];
        if((!pose&&!t.settle)||(pose&&!Finite(pose->world,16)))continue;
        // Still the prop that was read: same entity at that index, same
        // preset position and parent, and the matrix last put there.
        uint32_t index=t.transformIndex;
        if(!transforms.At(index,t.entity)&&!transforms.Dense(t.entity,index)){Lose(i);continue;}
        t.transformIndex=index;
        const uintptr_t component=transforms.payload+uintptr_t(index)*TransformSize;
        float m[TransformSize/4];uint32_t parent=0;
        if(!ReadBytes(component,m,sizeof m)){Lose(i);continue;}
        std::memcpy(&parent,m+48,4);
        if(parent!=t.parent||!Same(m+12,t.local,3)||!Same(m+16,t.world,16)){Lose(i);continue;}
        // The new world, and the old one where the renderer keeps the last
        // frame's (motion vectors): a settle copies the matrix over itself.
        float out[32];
        std::memcpy(out,pose?pose->world:t.world,64);std::memcpy(out+16,m+16,64);
        if(!WriteMemory(reinterpret_cast<void*>(component+0x40),out,sizeof out)){Lose(i);continue;}
        if(pose)++writes;else ++settles;
        if(pose&&haveBounds&&t.hasBounds) {
            uint32_t b=t.boundsIndex;float box[BoundsSize/4];
            const Box expected=props::WorldBounds(t.bounds,m+16);
            const float scale=std::max({std::fabs(expected.lo.x),std::fabs(expected.hi.x),std::fabs(expected.lo.z),std::fabs(expected.hi.z),1.f});
            if((bounds.At(b,t.entity)||bounds.Dense(t.entity,b))&&ReadBytes(bounds.payload+uintptr_t(b)*BoundsSize,box,sizeof box)&&Finite(box,24)&&
               props::Near(expected,{{box[16],box[17],box[18]},{box[20],box[21],box[22]}},.02f+scale*2e-5f)) {
                const Box next=props::WorldBounds(t.bounds,out);
                const float world[8]{next.lo.x,next.lo.y,next.lo.z,box[19],next.hi.x,next.hi.y,next.hi.z,box[23]};
                t.boundsIndex=b;
                if(!WriteMemory(reinterpret_cast<void*>(bounds.payload+uintptr_t(b)*BoundsSize+0x40),world,sizeof world))t.hasBounds=false;
            } else {t.hasBounds=false;++boundsRefused;}
        }
        std::memcpy(t.world,out,64);t.writtenAt=now;t.settle=pose!=nullptr;
    }
    active=!tracked.empty();
}
std::filesystem::path Directory() {
    HMODULE module=nullptr;wchar_t path[32768]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Directory),&module)||
       !GetModuleFileNameW(module,path,32768))return {};
    return std::filesystem::path(path).parent_path()/L"d2r_vr_props";
}
}
void Init() {
    const auto file=Directory()/L"props.bin";
    std::ifstream in(file,std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    char line[200];
    if(catalog.Parse(data)) {
        catalogReady=true;
        std::snprintf(line,sizeof line,"Frame props: catalog of %zu knockable level props of %u kinds",catalog.Size(),catalog.Kinds());
    } else std::snprintf(line,sizeof line,"Frame props: no prop catalog (%s); level props stay where they are until the installer catalogues them",
                         data.empty()?"missing":"unreadable");
    logging::Write(line);
}
void Configure(bool on) {enabled=on;}
const char* KindName(uint16_t kind) {return catalog.KindAt(kind).name;}
void Want(const Request& r,uint64_t now) {
    if(!enabled.load()||!catalogReady.load()||!r.session||!(r.unitsPerMetre>=1&&r.unitsPerMetre<=12)||
       !std::isfinite(r.centre[0]+r.centre[1]+r.centre[2]+r.floor))return;
    if(job||(submittedAt&&now>=submittedAt&&now-submittedAt<Interval)||!worker.Start())return;
    // Looking for the id and bounds storages reads every candidate: not every scan.
    const bool discover=(idPool<0||boundsPool<0)&&(!discoveredAt||now-discoveredAt>=Rediscover);
    if(discover)discoveredAt=now;
    job=worker.Submit({r,idPool,boundsPool,discover,devscenarios::Isolated()});
    submittedAt=now;
}
bool Latest(props::Scan& out) {
    Found f;
    if(!job||!worker.Take(job,f))return false;
    job=0;
    if(f.idPool>=0)idPool=f.idPool;
    if(f.boundsPool>=0)boundsPool=f.boundsPool;
    if(f.ok) {
        ++scans;scanMicros=uint32_t(f.ms*1000);seenProps=f.scan.count;matchedProps=f.matched;if(f.scan.count>mostSeen)mostSeen=f.scan.count;
        leftOut[0]=f.unbounded;leftOut[1]=f.unrigid;leftOut[2]=f.sized;leftOut[3]=f.beyond;
        unknownNow=std::move(f.unknown);
    }
    else ++failed;
    lastWhy=f.why;
    out=f.scan;
    const auto now=GetTickCount64();
    {
        std::lock_guard lock(trackMutex);
        // Props written lately are the physics' own: what the scan read of
        // them may be a frame old. Others take what was read.
        std::vector<Tracked> next;
        for(auto& t:f.tracked) {
            auto it=std::find_if(tracked.begin(),tracked.end(),[&](const Tracked& o){return o.entity==t.entity;});
            if(it!=tracked.end()&&now-it->writtenAt<Keep){next.push_back(*it);continue;}
            next.push_back(t);
        }
        for(const auto& t:tracked)
            if(now-t.writtenAt<Keep&&std::none_of(next.begin(),next.end(),[&](const Tracked& o){return o.entity==t.entity;}))next.push_back(t);
        tracked=std::move(next);
        if(f.transforms)transformsObject=f.transforms;
        if(f.bounds)boundsObject=f.bounds;
        for(const auto entity:lostPending)if(out.lost<props::MaxLost)out.lostEntities[out.lost++]=entity;
        lostPending.clear();
        active=!tracked.empty();
    }
    out.session=f.ok?out.session:0;
    out.serial=++serial;
    return true;
}
void Publish(const props::Poses& poses) {
    std::lock_guard lock(poseMutex);
    pending=poses;
}
void Transformed() {
    if(!active.load(std::memory_order_relaxed))return;
    const uint64_t at=framevr::BowRenderTime();
    if(!at||at==passAt.load(std::memory_order_relaxed)||passing.exchange(true,std::memory_order_acquire))return;
    if(passAt.load(std::memory_order_relaxed)!=at){passAt=at;Pass();}
    passing.store(false,std::memory_order_release);
}
void CountKnocks(unsigned byWeapons,unsigned byMovingThings) {struckKnocks+=byWeapons;chainedKnocks+=byMovingThings;}
void Report() {
    const auto now=GetTickCount64();
    if(reportedAt&&now>=reportedAt&&now-reportedAt<10000)return;
    reportedAt=now;
    const auto s=scans.exchange(0),x=failed.exchange(0),w=writes.exchange(0),t=settles.exchange(0),l=lost.exchange(0),
        b=boundsRefused.exchange(0),p=passes.exchange(0),k=struckKnocks.exchange(0),c=chainedKnocks.exchange(0),most=mostSeen.exchange(0);
    if(!s&&!x&&!w&&!l)return;
    char line[560];
    std::snprintf(line,sizeof line,"Frame props: ten seconds: scans=%u failed=%u (last %.1f ms%s%s) props in reach now=%u most=%u, catalogued within 6 m=%u, "
        "left out now: no bounds=%u not rigid=%u size=%u beyond the nearest %u=%u; storages ids=%d bounds=%d; knocks by weapons=%u by moving things=%u; "
        "write passes=%u moves=%u settles=%u lost=%u bounds refused=%u",
        s,x,scanMicros.load()/1000.,*lastWhy?", ":"",lastWhy,seenProps.load(),most,matchedProps.load(),leftOut[0].load(),leftOut[1].load(),leftOut[2].load(),
        props::MaxSeen,leftOut[3].load(),idPool,boundsPool,k,c,p,w,t,l,b);
    logging::Write(line);
    // pcvr-dev: what is in reach but stays put, when that changes - each id's model
    // from `prepare_props.py --game <game> --lookup <id>...`, to add it to KINDS.
    std::vector<uint32_t> ids;for(const auto& n:unknownNow)ids.push_back(n.preset);
    std::sort(ids.begin(),ids.end());
    if(ids.empty()||ids==unknownLogged)return;
    unknownLogged=ids;
    std::string text="Frame props: not catalogued in reach (preset id, local position, size in m, distance):";
    for(const auto& n:unknownNow) {
        char one[160];
        std::snprintf(one,sizeof one," %u (%.2f %.2f %.2f) %.2fx%.2fx%.2f %.1f m;",n.preset,n.local[0],n.local[1],n.local[2],n.size[0],n.size[1],n.size[2],n.distance);
        text+=one;
    }
    logging::Write(text.c_str());
}
}
