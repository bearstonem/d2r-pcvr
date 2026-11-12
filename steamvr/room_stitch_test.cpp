#include "room_stitch.h"
#include <cassert>
#include <cstring>
#include <map>
#include <vector>

using namespace framevr::roomstitch;

// A fake game memory: byte buffers at made-up addresses.
struct Memory {
    std::map<uintptr_t,std::vector<uint8_t>> blocks;
    uintptr_t next=0x100000;
    uintptr_t Alloc(size_t n){const uintptr_t a=next;blocks[a].assign(n,0);next+=(n+0xfff)&~size_t(0xfff);next+=0x1000;return a;}
    template<class T> void Put(uintptr_t a,T v){for(auto& [b,bytes]:blocks)if(a>=b&&a+sizeof v<=b+bytes.size()){std::memcpy(&bytes[a-b],&v,sizeof v);return;}assert(false);}
    template<class T> bool Get(uintptr_t a,T& v)const{for(auto& [b,bytes]:blocks)if(a>=b&&a+sizeof v<=b+bytes.size()){std::memcpy(&v,&bytes[a-b],sizeof v);return true;}return false;}
};
struct Room {uintptr_t at=0;int x=0,y=0;uint64_t key=0;};

int main() {
    Memory m;
    const auto read=[&](uintptr_t a,auto& v){return m.Get(a,v);};
    // Five rooms in a row and one above the middle: W(0,0) H(10,0) M(20,0) E(30,0), N(20,-10) S(20,10).
    auto mk=[&](int x,int y,uint64_t key){Room r;r.at=m.Alloc(0x200);r.x=x;r.y=y;r.key=key;m.Put(r.at+PosX,int32_t(x));m.Put(r.at+PosY,int32_t(y));m.Put(r.at+Key,key);return r;};
    const Room w=mk(0,0,50),h=mk(10,0,20),mid=mk(20,0,90),e=mk(30,0,70),n=mk(20,-10,10),s=mk(20,10,40);
    // The middle room's near list: everyone (order shuffled), plus a second room to the west further away.
    const Room farW=mk(-10,0,99);
    const uintptr_t list=m.Alloc(8*8);
    const uintptr_t order[]={s.at,farW.at,e.at,n.at,h.at,w.at};
    for(int i=0;i<6;++i)m.Put(list+i*8,order[i]);
    m.Put(mid.at+NearData,list);m.Put(mid.at+NearCount,uint64_t(6));
    // The layer's table: entries sorted by key. Built: h (key 20), n (10), s (40), e (70); not w, farW, mid.
    const uintptr_t layer=m.Alloc(0x400);
    struct E {uint64_t key;float edges[4];float height;};
    std::vector<E> entries={{10,{0,0,0,2},5},{20,{0,0,1,0},0},{40,{0,3,0,0},-6.5f},{70,{4,0,0,0},12}};
    const uintptr_t table=m.Alloc(entries.size()*Stride);
    for(size_t i=0;i<entries.size();++i) {
        const uintptr_t at=table+i*Stride;m.Put(at,entries[i].key);
        for(int k=0;k<4;++k)m.Put(at+0x28+k*4,entries[i].edges[k]);
        m.Put(at+Height,entries[i].height);
    }
    m.Put(layer+TableData,table);m.Put(layer+TableCount,uint64_t(entries.size()));
    // Find: every built key, none of the others.
    for(size_t i=0;i<entries.size();++i)assert(Find(layer,entries[i].key,read)==table+i*Stride);
    for(uint64_t k:{0ull,15ull,50ull,90ull,1000ull})assert(!Find(layer,k,read));
    // The middle room's edges (W N E S) and its candidates: neighbour h + facing edge - own edge.
    const float edges[4]={1,2,3,4};
    Candidate c[4];
    // West: the first room to the west in the list is farW (not built) - the game stops there, so no west.
    assert(Candidates(layer,mid.at,edges,read,c)==3);
    assert(!c[West].have);
    assert(c[North].have&&c[North].room==n.at&&c[North].h==5+2-2);       // n's south edge 2, our north 2
    assert(c[East].have&&c[East].room==e.at&&c[East].h==12+4-3);         // e's west edge 4, our east 3
    assert(c[South].have&&c[South].room==s.at&&c[South].h==-6.5f+3-4);  // s's north edge 3, our south 4
    // The game: the hero's room outright, else the largest in size (13 east).
    assert(GameChoice(c,0)==East);
    assert(GameChoice(c,s.at)==South);
    // Ours: the built neighbour nearest the hero's room.
    assert(Choice(c,0,true,20,20)==South);        // hero south of us: the south room (distance 10)
    assert(Choice(c,0,true,20,-30)==North);
    assert(Choice(c,0,true,40,0)==East);
    assert(Choice(c,n.at,true,20,-10)==North);    // his own room first
    assert(Choice(c,0,false,0,0)==East);          // hero unknown: the game's pick
    // A tie keeps the game's pick: hero at (25,-5) is 10 from n and 10 from e.
    assert(Choice(c,0,true,25,-5)==East);
    assert(HeightOf(c,-1)==0);
    // With the hero's room to the west built, it wins (game and ours).
    entries.insert(entries.begin()+3,E{50,{0,0,6,0},1});   // w (key 50) built, west neighbour now... farW is first in the list
    // reorder the near list so w comes before farW
    const uintptr_t order2[]={s.at,w.at,e.at,n.at,h.at,farW.at};
    for(int i=0;i<6;++i)m.Put(list+i*8,order2[i]);
    const uintptr_t table2=m.Alloc(entries.size()*Stride);
    for(size_t i=0;i<entries.size();++i) {
        const uintptr_t at=table2+i*Stride;m.Put(at,entries[i].key);
        for(int k=0;k<4;++k)m.Put(at+0x28+k*4,entries[i].edges[k]);
        m.Put(at+Height,entries[i].height);
    }
    m.Put(layer+TableData,table2);m.Put(layer+TableCount,uint64_t(entries.size()));
    assert(Candidates(layer,mid.at,edges,read,c)==4&&c[West].room==w.at&&c[West].h==1+6-1);
    assert(GameChoice(c,w.at)==West&&Choice(c,w.at,true,0,0)==West);
    // Unreadable or absurd lists give nothing.
    m.Put(mid.at+NearCount,uint64_t(1000));
    assert(Candidates(layer,mid.at,edges,read,c)==0&&GameChoice(c,0)==-1&&Choice(c,0,true,0,0)==-1);
    float nan=NAN;const float bad[4]={nan,nan,nan,nan};
    m.Put(mid.at+NearCount,uint64_t(6));
    assert(Candidates(layer,mid.at,bad,read,c)==0);
    (void)h;
    return 0;
}
