// What the skeleton hook remembers of the instances it meets (aged_cache.h): forgotten by age
// or, when full, the older half - never everything at once.
#include "aged_cache.h"
#include <cstdio>
#include <string>
using namespace framevr;
namespace {
int failures=0;
void Check(bool ok,const char* what) {
    if(!ok){++failures;std::printf("FAILED: %s\n",what);}
}
}
int main() {
    {   // found, replaced, and forgotten only once not looked up for the keep time
        AgedCache<uintptr_t,int> cache(8,1000,100);
        cache.Put(0x1000,1,10);cache.Put(0x2000,2,10);
        Check(cache.Size()==2&&cache.Find(0x1000,20)&&*cache.Find(0x1000,20)==1,"an entry is found by its key");
        Check(!cache.Find(0x3000,20),"an unknown key is not found");
        cache.Put(0x1000,7,30);
        Check(cache.Size()==2&&*cache.Find(0x1000,30)==7,"the same key replaces its entry");
        // 0x1000 keeps being looked up, 0x2000 is not (its instance was freed)
        for(uint64_t t=100;t<=2000;t+=100)cache.Find(0x1000,t);
        CacheSweep swept;
        cache.Put(0x4000,4,2000,&swept);
        Check(swept.held==2&&swept.aged==1&&swept.crowded==0&&swept.Dropped()==1,"only the entry not looked up for the keep time is forgotten");
        Check(cache.Find(0x1000,2000)&&!cache.Find(0x2000,2000)&&cache.Find(0x4000,2000),"the looked-up entry and the new one stay");
    }
    {   // no sweep before one is due
        AgedCache<uintptr_t,int> cache(8,1000,5000);
        cache.Put(1,1,6000);                       // the first Put sweeps (nothing held)
        CacheSweep swept;swept.held=99;
        cache.Put(2,2,9000,&swept);
        Check(swept.held==99&&cache.Size()==2,"a sweep waits for its interval");
        cache.Put(3,3,11000,&swept);
        Check(swept.held==2&&swept.aged==2&&cache.Size()==1,"and runs when it is due");
    }
    {   // full, nothing old: the half looked up longest ago goes, the rest and the new entry stay
        AgedCache<uintptr_t,int> cache(8,100000,100000);
        for(int i=0;i<8;++i)cache.Put(uintptr_t(i),i,uint64_t(1000+i*10));
        CacheSweep swept;
        cache.Put(100,100,1100,&swept);
        Check(swept.held==8&&swept.aged==0&&swept.crowded==5,"a full cache drops its older half (and the middle)");
        Check(cache.Size()==4&&cache.Find(100,1100)&&cache.Find(7,1100)&&cache.Find(5,1100)&&!cache.Find(4,1100)&&!cache.Find(0,1100),
              "the newer entries are the ones kept");
        // never more than the limit, whatever is put
        for(int i=0;i<1000;++i)cache.Put(uintptr_t(1000+i),i,uint64_t(2000+i));
        Check(cache.Size()<=8,"the cache never passes its limit");
    }
    {   // every entry the same age: all go (the limit holds), the new one stays
        AgedCache<uintptr_t,int> cache(4,100000,100000);
        for(int i=0;i<4;++i)cache.Put(uintptr_t(i),i,500);
        CacheSweep swept;cache.Put(9,9,500,&swept);
        Check(swept.crowded==4&&cache.Size()==1&&cache.Find(9,500),"entries of one age all make room");
    }
    {   // a look-up stamps at most every StampEvery, and a clock that went back forgets nothing
        AgedCache<uintptr_t,int> cache(8,1000,0);
        cache.Put(1,1,5000);
        cache.Find(1,5100);                        // within StampEvery: the stamp stays 5000
        CacheSweep swept=cache.Sweep(6050);
        Check(swept.aged==1,"a look-up inside the stamp interval does not renew the entry");
        cache.Put(2,2,5000);cache.Find(2,5300);    // stamped 5300
        swept=cache.Sweep(6200);
        Check(swept.aged==0&&cache.Size()==1,"a later look-up renews it");
        swept=cache.Sweep(100);
        Check(swept.aged==0&&cache.Size()==1,"an earlier clock forgets nothing");
    }
    {   // pair keys, a value that owns memory, Each and Clear
        AgedCache<std::pair<uintptr_t,uintptr_t>,std::string,PairHash> cache(16,1000,100);
        cache.Put({1,2},"a",10);cache.Put({2,1},"b",10);cache.Put({1,3},"c",10);
        Check(*cache.Find({1,2},10)=="a"&&*cache.Find({2,1},10)=="b"&&!cache.Find({3,1},10),"a pair key keeps its order");
        std::string all;cache.Each([&](const std::pair<uintptr_t,uintptr_t>&,const std::string& v){all+=v;});
        Check(all.size()==3,"Each visits every entry");
        std::string& kept=cache.Put({5,5},"kept",20);
        for(int i=0;i<12;++i)cache.Put({uintptr_t(10+i),0},"x",uint64_t(21+i));
        Check(kept=="kept","an entry's reference survives later entries");
        cache.Clear();Check(cache.Size()==0,"Clear empties it");
    }
    if(failures){std::printf("%d failure(s)\n",failures);return 1;}
    std::printf("aged-cache-test: ok\n");
    return 0;
}
