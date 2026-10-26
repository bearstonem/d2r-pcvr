#include "props.h"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace framevr;
using namespace framevr::props;
using math::Vec;
using math::Quat;
namespace {
// props.bin as prepare_props.py writes it.
std::vector<uint8_t> File(const std::vector<std::pair<const char*,std::array<uint8_t,2>>>& kinds,const std::vector<Entry>& entries) {
    std::vector<uint8_t> out(16+kinds.size()*32+entries.size()*20);
    auto put=[&](size_t at,const void* v,size_t n){std::memcpy(out.data()+at,v,n);};
    const uint32_t header[4]{CatalogMagic,CatalogFormat,uint32_t(kinds.size()),uint32_t(entries.size())};
    put(0,header,16);
    for(size_t i=0;i<kinds.size();++i){put(16+i*32,kinds[i].first,std::strlen(kinds[i].first));put(16+i*32+28,kinds[i].second.data(),2);}
    for(size_t i=0;i<entries.size();++i) {
        const size_t at=16+kinds.size()*32+i*20;const auto& e=entries[i];
        put(at,&e.id,4);put(at+4,&e.x,4);put(at+8,&e.y,4);put(at+12,&e.z,4);put(at+16,&e.kind,2);
    }
    return out;
}
bool Close(Vec a,Vec b,float e=1e-4f) {return std::fabs(a.x-b.x)<=e&&std::fabs(a.y-b.y)<=e&&std::fabs(a.z-b.z)<=e;}
float Angle(Quat a,Quat b) {return 2*std::acos(std::min(1.f,std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)));}
}
int main() {
    // The catalog: an id matches only at its preset position.
    {
        const auto data=File({{"cup",{0,0}},{"skull",{1,5}}},{{5,1,2,3,1},{9,275.2813f,-.09458591f,104.5846f,0},{9,-4,0,8,1}});
        Catalog c;assert(c.Parse(data));
        assert(c.Size()==3&&c.Kinds()==2);
        const float plank[3]{275.2813f,-.09458591f,104.5846f},other[3]{-4,0,8},moved[3]{275.29f,-.09458591f,104.5846f};
        assert(c.Find(9,plank)==0&&c.Find(9,other)==1&&c.Find(9,moved)<0);
        const float a[3]{1,2,3};assert(c.Find(5,a)==1&&c.Find(6,a)<0&&c.Find(4,a)<0);
        assert(!std::strcmp(c.KindAt(1).name,"skull")&&c.KindAt(1).shape==Shape::Box&&c.KindAt(1).material==Material::Bone);
        assert(c.KindAt(7).name[0]==0);
        // Malformed files keep nothing.
        auto bad=data;bad[0]^=1;assert(!c.Parse(bad)&&c.Size()==0);
        bad=data;bad.pop_back();assert(!c.Parse(bad));
        bad=File({{"cup",{3,0}}},{});assert(!c.Parse(bad));                       // unknown shape
        bad=File({{"cup",{0,0}}},{{5,0,0,0,1}});assert(!c.Parse(bad));            // unknown kind
        bad=File({{"cup",{0,0}}},{{9,0,0,0,0},{5,0,0,0,0}});assert(!c.Parse(bad)); // unsorted
        bad=File({{"cup",{0,0}}},{{9,std::numeric_limits<float>::quiet_NaN(),0,0,0}});assert(!c.Parse(bad));
        assert(!c.Parse({}));
    }
    // The plank's matrices from the probe: decomposed and composed back.
    {
        const float world[16]{.7450f,-.0368f,.0046f,0, .0368f,.7450f,.0036f,0, -.0048f,-.0034f,.7459f,0, 10273.8672f,-.0946f,10103.1709f,1};
        Placement p;assert(Decompose(world,p));
        assert(std::fabs(p.scale.x-.7459f)<.002f&&std::fabs(p.scale.y-.7459f)<.002f&&std::fabs(p.scale.z-.7459f)<.002f);
        assert(Close(p.p,{10273.8672f,-.0946f,10103.1709f}));
        // The preset's orientation, (0.0023578, -0.0031717, -0.0246630, 0.999688).
        assert(Angle(p.q,{.0023578f,-.0031717f,-.024663f,.999688f})<.01f);
        float back[16];Compose(p,back);
        for(int i=0;i<16;++i)assert(std::fabs(back[i]-world[i])<.002f*(i<12?1:10));
        // The renderer's own world bounds for its local bounds under that matrix.
        const Box bounds=WorldBounds({{-6.0558f,-.1687f,-.8255f},{1.3895f,.2619f,.6976f}},world);
        assert(Near(bounds,{{10269.3467f,-.2738f,10102.5254f},{10274.916f,.3262f,10103.6992f}},.01f));
        assert(!Near(bounds,{{10269.3467f,-.2738f,10102.5254f},{10274.916f,.3262f,10103.8f}},.01f));
        // Turned and scaled per axis.
        const Placement t{{1,2,3},{0,std::sin(.6f),0,std::cos(.6f)},{2,.5f,1.25f}};
        float m[16];Compose(t,m);Placement u;assert(Decompose(m,u));
        assert(Close(u.p,t.p)&&Close(u.scale,t.scale)&&Angle(u.q,t.q)<1e-3f);
        // Mirrored, sheared, projective or broken matrices are refused.
        float mirrored[16];std::memcpy(mirrored,m,sizeof m);for(int i=8;i<11;++i)mirrored[i]=-mirrored[i];assert(!Decompose(mirrored,u));
        float sheared[16];std::memcpy(sheared,m,sizeof m);sheared[4]+=.5f;assert(!Decompose(sheared,u));
        float projective[16];std::memcpy(projective,m,sizeof m);projective[3]=.1f;assert(!Decompose(projective,u));
        float flat[16];std::memcpy(flat,m,sizeof m);flat[4]=flat[5]=flat[6]=0;assert(!Decompose(flat,u));
        float broken[16];std::memcpy(broken,m,sizeof m);broken[13]=std::numeric_limits<float>::infinity();assert(!Decompose(broken,u));
    }
    // What a prop rests on (metres here, so unitsPerMetre is 1).
    {
        const float floor=0;
        const Box cup{{-.04f,0,-.04f},{.04f,.1f,.04f}};
        assert(FindSupport(cup,{},floor,1).floor);   // on the floor
        auto at=[](Box b,Vec d){return Box{b.lo+d,b.hi+d};};
        const Box onTable=at(cup,{.3f,.75f,.2f});
        const Box table{{0,0,0},{1.2f,.75f,.8f}},room{{-5,0,-5},{5,3,5}},tiny{{.29f,.7f,.19f},{.31f,.76f,.21f}};
        const std::array<Box,3> around{room,table,tiny};
        auto s=FindSupport(onTable,around,floor,1);
        assert(!s.floor&&std::fabs(s.y-.75f)<1e-5f);            // the table's top, not the room or the tiny thing
        assert(s.x0==0&&s.z0==0&&s.x1==1.2f&&s.z1==.8f);
        // Just above the top (on a plate): still the table's top.
        s=FindSupport(at(cup,{.3f,.765f,.2f}),around,floor,1);assert(std::fabs(s.y-.75f)<1e-5f);
        // Sunk a little into it: the surface stays under the prop.
        s=FindSupport(at(cup,{.3f,.74f,.2f}),around,floor,1);assert(std::fabs(s.y-.74f)<1e-5f);
        // On a shelf inside a cabinet: the prop's own height.
        const Box cabinet{{2,0,0},{3,2,.5f}};
        const std::array<Box,2> shelves{room,cabinet};
        s=FindSupport(at(cup,{2.5f,1.1f,.25f}),shelves,floor,1);
        assert(!s.floor&&std::fabs(s.y-1.1f)<1e-5f&&s.x0==2&&s.x1==3);
        // A long counter is cut to the reach either side of the prop.
        const Box counter{{-50,0,0},{50,1,.6f}};
        s=FindSupport(at(cup,{4,1,.3f}),std::span(&counter,1),floor,1);
        assert(!s.guessed&&std::fabs(s.x0-(4-SupportReach))<1e-4f&&std::fabs(s.x1-(4+SupportReach))<1e-4f&&s.z0==0&&s.z1==.6f);
        // Inside a building (a mantle that is part of it): guessed, just past the prop.
        const Box building{{-6,0,-5},{6,6,5}};
        s=FindSupport(at(cup,{4,1.3f,4}),std::span(&building,1),floor,1);
        assert(!s.floor&&s.guessed&&std::fabs(s.y-1.3f)<1e-5f&&std::fabs(s.x0-(4-.04f-GuessMargin))<1e-4f&&std::fabs(s.z1-(4+.04f+GuessMargin))<1e-4f);
        // Nothing under it: the same small guessed plane at its own height.
        s=FindSupport(at(cup,{8,1.5f,8}),around,floor,1);
        assert(!s.floor&&s.guessed&&std::fabs(s.y-1.5f)<1e-5f&&s.x1-s.x0<.2f&&s.x0<7.96f&&s.x1>8.04f);
        // Books along a mantle join into one plane; one on another shelf, or
        // farther along, does not.
        std::array<Support,5> row{};
        for(int i=0;i<3;++i)row[size_t(i)]=FindSupport(at(cup,{4+i*.15f,1.3f,4}),std::span(&building,1),floor,1);
        row[3]=FindSupport(at(cup,{4.15f,1.6f,4}),std::span(&building,1),floor,1);
        row[4]=FindSupport(at(cup,{6,1.3f,4}),std::span(&building,1),floor,1);
        const auto tableTop=FindSupport(onTable,around,floor,1);
        JoinLedges(row,1);
        assert(row[0]==row[1]&&row[1]==row[2]);
        assert(std::fabs(row[0].x0-(4-.04f-GuessMargin))<1e-4f&&std::fabs(row[0].x1-(4.3f+.04f+GuessMargin))<1e-4f);
        assert(!(row[3]==row[0])&&row[3].x1-row[3].x0<.2f);
        assert(!(row[4]==row[0])&&row[4].x1-row[4].x0<.2f);
        std::array<Support,2> mixed{tableTop,row[4]};JoinLedges(mixed,1);
        assert(mixed[0]==tableTop);                               // only guessed planes join
        // A cup on a plate rests on the plate, not one beside or below it.
        const Box plate{{3.9f,1.29f,3.9f},{4.1f,1.31f,4.1f}};
        assert(RestsOn(at(cup,{4,1.31f,4}),plate,1));
        assert(!RestsOn(at(cup,{4.3f,1.31f,4}),plate,1));
        assert(!RestsOn(at(cup,{4,1.41f,4}),plate,1));
        assert(!RestsOn(plate,at(cup,{4,1.31f,4}),1));
        // Units scale with the renderer's.
        const Box scaled{{-.13f,3.3f*.75f,-.13f},{.13f,3.3f*.85f,.13f}};
        const Box scaledTable{{-1,0,-1},{1,3.3f*.75f,1}};
        s=FindSupport(scaled,std::span(&scaledTable,1),0,3.3f);assert(!s.floor&&std::fabs(s.y-3.3f*.75f)<1e-4f);
    }
    // A weapon reaching a box.
    {
        const Vec p{1,0,0},c{0,.05f,0},h{.04f,.05f,.04f};const Quat q{0,0,0,1};
        assert(CapsuleTouchesBox({.5f,.05f,0},{1.5f,.05f,0},.02f,p,q,c,h));   // straight through
        assert(CapsuleTouchesBox({1.06f,.05f,-1},{1.06f,.05f,1},.03f,p,q,c,h)); // grazing past within its radius
        assert(!CapsuleTouchesBox({1.1f,.05f,-1},{1.1f,.05f,1},.03f,p,q,c,h));  // passing beside
        assert(!CapsuleTouchesBox({1,.2f,-1},{1,.2f,1},.03f,p,q,c,h));          // passing over
        assert(CapsuleTouchesBox({1,.05f,0},{1,.05f,0},.01f,p,q,c,h));          // a point inside
        // Turned a quarter about y: the long side now lies along x.
        const Quat turn{0,std::sin(.7853982f),0,std::cos(.7853982f)};const Vec longH{.04f,.05f,.3f};
        assert(CapsuleTouchesBox({1.25f,.05f,-1},{1.25f,.05f,1},.01f,p,turn,c,longH));
        assert(!CapsuleTouchesBox({1,.05f,.25f},{1.2f,.05f,.25f},.01f,p,turn,c,longH));
        assert(Overlap({{0,0,0},{1,1,1}},{{1,1,1},{2,2,2}})&&!Overlap({{0,0,0},{1,1,1}},{{1.01f,0,0},{2,1,1}}));
    }
    // Every material is physical.
    for(const auto& m:Properties)assert(m.density>0&&m.friction>0&&m.bounce>=0&&m.bounce<1&&m.rolling>=0);
    std::puts("props: ok");
}
