// A registry storage read as one block (ecs_storage.h): the header's fields, an entity's dense
// index, every refusal, and how many guarded reads a look-up costs. The memory is a fake one.
#include "ecs_storage.h"
#include <cstdio>
#include <map>
#include <vector>
using namespace framevr::ecsstorage;
namespace {
int failures=0;
void Check(bool ok,const char* what) {
    if(!ok){++failures;std::printf("FAILED: %s\n",what);}
}
// Regions of readable memory by address; a read must lie inside one.
struct Memory {
    std::map<uint64_t,std::vector<unsigned char>> regions;
    int reads=0;
    void Put(uint64_t at,const void* data,size_t size) {
        auto& r=regions[at];r.resize(size);std::memcpy(r.data(),data,size);
    }
    template<class T> void Set(uint64_t at,const T& value){Put(at,&value,sizeof value);}
    bool Read(void* out,uint64_t at,size_t size) {
        ++reads;
        if(at<=0x10000)return false;
        auto it=regions.upper_bound(at);
        if(it==regions.begin())return false;
        --it;
        if(at<it->first||at+size>it->first+it->second.size())return false;
        std::memcpy(out,it->second.data()+(at-it->first),size);
        return true;
    }
};
constexpr uint64_t Storage=0x100000,Pages=0x200000,Page3=0x300000,Packed=0x400000,Components=0x500000,Vtable=0x7FF600001000;
// A storage with four sparse pages; entity (3 << 8) | 5 is at dense index 2 of three.
Memory Make(uint32_t entity,uint32_t sparseValue=2) {
    Memory m;
    std::vector<unsigned char> header(0x98,0);
    const uint64_t fields[][2]{{0,Vtable},{0x08,Pages},{0x10,Pages+4*8},{0x20,Packed},{0x28,Packed+3*4},{0x38,Components}};
    for(const auto& f:fields)std::memcpy(header.data()+f[0],&f[1],8);
    m.Put(Storage,header.data(),header.size());
    const uint64_t pages[4]{0,0,0,Page3};
    m.Put(Pages,pages,sizeof pages);
    std::vector<uint32_t> slots(256,0xFFFFFFFFu);
    slots[5]=0xABC00000u|sparseValue;            // the high bits are the entity's version
    m.Put(Page3,slots.data(),slots.size()*4);
    const uint32_t packed[3]{0x111,0x222,entity};
    m.Put(Packed,packed,sizeof packed);
    return m;
}
auto Reader(Memory& m){return [&m](void* out,uint64_t at,size_t size){return m.Read(out,at,size);};}
}
int main() {
    const uint32_t entity=(3u<<8)|5u;
    {
        Memory m=Make(entity);
        Header h;
        Check(ReadHeader(Storage,Reader(m),&h)&&m.reads==1,"the header is one read");
        Check(h.vtable==Vtable&&h.sparseBegin==Pages&&h.sparseEnd==Pages+32&&h.packedBegin==Packed&&h.packedEnd==Packed+12&&h.components==Components,
              "every field comes from its offset");
        uint32_t index=99;
        Check(DenseIndex(h,entity,Reader(m),&index)&&index==2,"an entity's dense index is found");
        Check(m.reads==4,"a look-up costs four reads with its header");
    }
    {   // refusals: each leaves the index alone
        Memory m=Make(entity);Header h;ReadHeader(Storage,Reader(m),&h);uint32_t index=99;
        Check(!DenseIndex(h,(4u<<8)|5u,Reader(m),&index),"a page past the sparse list is refused");
        Check(!DenseIndex(h,(2u<<8)|5u,Reader(m),&index),"a page never made is refused");
        Check(!DenseIndex(h,(3u<<8)|6u,Reader(m),&index),"an empty slot is refused");
        Check(index==99,"a refusal does not touch the index");
        Header bad=h;bad.sparseEnd=bad.sparseBegin-8;
        Check(!DenseIndex(bad,entity,Reader(m),&index),"a sparse list that ends before it begins is refused");
        bad=h;bad.packedEnd=bad.packedBegin-4;
        Check(!DenseIndex(bad,entity,Reader(m),&index),"a packed list that ends before it begins is refused");
    }
    {
        Memory m=Make(entity,3);Header h;ReadHeader(Storage,Reader(m),&h);uint32_t index=99;
        Check(!DenseIndex(h,entity,Reader(m),&index),"an index past the packed list is refused");
    }
    {
        Memory m=Make(entity+0x100000);Header h;ReadHeader(Storage,Reader(m),&h);uint32_t index=99;   // another version of the entity
        Check(!DenseIndex(h,entity,Reader(m),&index),"an entity the packed list does not hold there is refused");
        Check(DenseIndex(h,entity+0x100000,Reader(m),&index)&&index==2,"the versioned entity it does hold is found");
    }
    {   // memory that does not read
        Memory m=Make(entity);Header h;
        Check(!ReadHeader(0,Reader(m),&h),"no storage, no header");
        Check(!ReadHeader(0x900000,Reader(m),&h),"an unreadable storage is refused");
        Check(!ReadHeader(Storage+0x70,Reader(m),&h),"a header cut short is refused");
        ReadHeader(Storage,Reader(m),&h);
        uint32_t index=99;
        m.regions.erase(Page3);
        Check(!DenseIndex(h,entity,Reader(m),&index),"an unreadable sparse page is refused");
        m=Make(entity);m.regions.erase(Packed);
        Check(!DenseIndex(h,entity,Reader(m),&index),"an unreadable packed list is refused");
    }
    if(failures){std::printf("%d failure(s)\n",failures);return 1;}
    std::printf("ecs-storage-test: ok\n");
    return 0;
}
