#include "atlas_dirty.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace framevr::atlasdirty;

namespace {
std::vector<uint8_t> Picture(int w,int h,uint8_t value=0) {return std::vector<uint8_t>(size_t(w)*h*4,value);}
void Dot(std::vector<uint8_t>& p,int w,int x,int y,uint8_t value) {p[(size_t(y)*w+x)*4+1]=value;}
// A swapchain image as the GPU holds it: `Apply` copies the rectangles from the newest picture.
struct Image {
    std::vector<uint8_t> texels;int w;
    void Apply(const std::vector<uint8_t>& from,const Rect* rects,size_t n) {
        for(size_t i=0;i<n;++i)for(int y=rects[i].y;y<rects[i].y+rects[i].h;++y)
            std::memcpy(&texels[(size_t(y)*w+rects[i].x)*4],&from[(size_t(y)*w+rects[i].x)*4],size_t(rects[i].w)*4);
    }
};
}
int main() {
    // A mask's tiles, clipped to a picture that is not a whole number of tiles.
    {
        Mask m(1000,130);
        assert(m.Columns()==16&&m.Rows()==3&&!m.Any()&&!m.Full()&&m.Sized(1000,130)&&!m.Sized(1000,131));
        m.Add({990,120,50,50});   // the last tile of each of the last two rows, partly outside
        assert(m.Test(15,1)&&m.Test(15,2)&&!m.Test(14,1)&&!m.Test(15,0));
        std::array<Rect,8> r{};
        assert(m.Rects(r.data(),r.size())==1&&(r[0]==Rect{960,64,40,66}));
        assert(m.Texels()==40*66&&(m.Bounds()==Rect{960,64,40,66}));
        m.Add({-10,-10,5,5});m.Add({0,0,0,0});m.Add({2000,0,10,10});   // nothing of these is inside
        assert(m.Texels()==40*66);
        m.Fill();assert(m.Full()&&m.Texels()==1000*130&&m.Rects(r.data(),r.size())==1&&(r[0]==Rect{0,0,1000,130}));
        m.Clear();assert(!m.Any()&&m.Rects(r.data(),r.size())==0&&(m.Bounds()==Rect{}));
        assert(!Mask(0,10).Sized(0,10)&&!Mask(MaxWidth+1,10).Any());
        Mask wide(MaxWidth,64,true);assert(wide.Full()&&wide.Columns()==64&&wide.Texels()==int64_t(MaxWidth)*64);
    }
    // Runs along a row grow down while the next row holds the same run; too many
    // rectangles become the one round them all.
    {
        Mask m(1024,1024);
        m.Add({512,768,512,256});           // the wrist's vitals: 8 x 4 tiles
        m.Add({768,0,96,96});               // a skill cue: 2 x 2 tiles
        std::array<Rect,8> r{};
        const size_t n=Uploads(m,r.data(),r.size());
        assert(n==2&&(r[0]==Rect{768,0,128,128})&&(r[1]==Rect{512,768,512,256}));
        assert(m.Texels()==128*128+512*256);
        assert(Uploads(m,r.data(),1)==1&&(r[0]==Rect{512,0,512,1024}));   // one allowed: the bounds
        Mask other(1024,1024);other.Add({0,0,1,1});
        m.Add(other);assert(m.Test(0,0));
        m.Add(Mask(512,512));assert(m.Full());   // another picture's mask: everything
        assert(Uploads(Mask(64,64),r.data(),8)==0);
    }
    // The painter's side: the first picture is all changed, the same again
    // nothing, one texel its tile; the kept copy follows.
    {
        Differ differ;
        auto p=Picture(200,100);
        auto c=differ.Step(p,200,100);
        assert(c.serial==1&&c.mask.Full());
        c=differ.Step(p,200,100);
        assert(c.serial==2&&!c.mask.Any());
        Dot(p,200,199,99,7);Dot(p,200,0,0,9);
        c=differ.Step(p,200,100);
        std::array<Rect,8> r{};
        assert(c.serial==3&&c.mask.Rects(r.data(),8)==2&&(r[0]==Rect{0,0,64,64})&&(r[1]==Rect{192,64,8,36}));
        c=differ.Step(p,200,100);
        assert(c.serial==4&&!c.mask.Any());   // the kept copy took both changes
        Dot(p,200,0,0,0);
        c=differ.Step(p,200,100);
        assert(c.mask.Texels()==64*64);
        // Another size, or a picture that is not w x h: all of it, and no stale copy after.
        auto q=Picture(64,64);
        assert(differ.Step(q,64,64).mask.Full());
        assert(!differ.Step(q,65,64).mask.Any()&&differ.Step(q,64,64).mask.Full());
        assert(differ.Step(q,64,64).serial==9);
    }
    // Follows: only the very next picture after the one uploaded, of the atlas's size.
    {
        Change c;c.serial=5;c.mask=Mask(64,64);
        assert(Follows(c,4,64,64)&&!Follows(c,3,64,64)&&!Follows(c,5,64,64)&&!Follows(c,0,64,64)&&!Follows(c,4,128,64));
        c.serial=1;assert(!Follows(c,0,64,64));
    }
    // Channel order: R and B exchanged, G and A kept; twice is the picture again.
    {
        std::vector<uint8_t> p{1,2,3,4,5,6,7,8,9,10,11,12};const auto rgba=p;
        SwapRedBlue(p.data(),3);
        assert((p==std::vector<uint8_t>{3,2,1,4,7,6,5,8,11,10,9,12}));
        std::vector<uint8_t> copy(12);CopySwapped(copy.data(),p.data(),3);
        assert(copy==rgba);
        Differ differ;auto picture=Picture(64,64);picture[0]=200;picture[2]=10;
        const auto change=Finish(picture,64,64,true,differ);
        assert(change.bgra&&change.mask.Full()&&picture[0]==10&&picture[2]==200);
        auto plain=Picture(64,64);plain[0]=200;
        assert(!Finish(plain,64,64,false,differ).bgra&&plain[0]==200);
        auto wrong=Picture(32,32);wrong[0]=200;
        assert(!Finish(wrong,64,64,true,differ).bgra&&wrong[0]==200);   // not the atlas's size: left alone (and never uploaded)
    }
    // A swapchain of three images handed out in any order: with each acquired
    // image brought up to date by its own lag, every image shown is the newest
    // picture exactly - also when it was last written several pictures ago.
    {
        constexpr int W=256,H=192;
        Differ differ;
        Lag lag(3,W,H);
        std::array<Image,3> images;for(auto& i:images)i={Picture(W,H,0xee),W};   // undefined contents at first
        auto picture=Picture(W,H);
        const int order[]={0,1,2,0,0,2,1,1,0,2,2,2,1,0};
        uint32_t seed=12345;int64_t copied=0,whole=0;
        for(const int index:order) {
            for(int k=0;k<3;++k) {   // a few texels change, in one tile or several
                seed=seed*1664525u+1013904223u;
                Dot(picture,W,int((seed>>8)%W),int((seed>>20)%H),uint8_t(seed>>4|1));
            }
            const auto change=differ.Step(picture,W,H);
            lag.Mark(change.mask);
            std::array<Rect,8> r{};
            const size_t n=Uploads(lag.Of(size_t(index)),r.data(),r.size());
            images[size_t(index)].Apply(picture,r.data(),n);
            for(size_t i=0;i<n;++i)copied+=int64_t(r[i].w)*r[i].h;
            whole+=W*H;
            lag.Done(size_t(index));
            assert(images[size_t(index)].texels==picture);
        }
        assert(copied<whole);   // and less than every image whole each time
        // A change marked as a rectangle (a shared panel's region) reaches every image too.
        std::fill(picture.begin(),picture.end(),uint8_t(3));
        lag.Mark(Rect{0,0,W,H});
        for(size_t i=0;i<3;++i) {
            std::array<Rect,8> r{};const size_t n=Uploads(lag.Of(i),r.data(),r.size());
            images[i].Apply(picture,r.data(),n);lag.Done(i);
            assert(images[i].texels==picture&&!lag.Of(i).Any());
        }
        lag.MarkAll();assert(lag.Of(1).Full()&&lag.Images()==3);
        std::printf("atlas dirty: %lld of %lld texels copied over %zu uploads to 3 images\n",
            static_cast<long long>(copied),static_cast<long long>(whole),sizeof order/sizeof order[0]);
    }
    return 0;
}
