#pragma once
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// What of a painted picture changed, so that only that is copied to the GPU.
// Uploading a whole 1024 x 1024 atlas for a changed life orb held the draw
// thread 0.80 ms (0.61 ms of it the copy), 15-34 times in 10 s of a fight
// (PERFORMANCE.md, 2026-11-10). The painter compares its new picture with the
// one before on its own thread (Differ) and hands over a mask of changed
// 64-texel tiles; the draw thread copies those tiles' rectangles alone.
//
// A swapchain has several images and each acquire may give another one, so an
// image is behind by every change since it was last written, not by the latest
// alone (Lag). Nothing here touches D3D or OpenXR.
namespace framevr::atlasdirty {
struct Rect {
    int x=0,y=0,w=0,h=0;
    bool operator==(const Rect&) const=default;
};
inline constexpr int Tile=64;
inline constexpr int MaxWidth=Tile*64;   // a row of tiles is one 64-bit word

class Mask {
    int width=0,height=0,columns=0;
    std::vector<uint64_t> rows;
    uint64_t Whole() const {return columns>=64?~0ull:(1ull<<columns)-1;}
public:
    Mask()=default;
    Mask(int w,int h,bool full=false) {
        if(w<=0||h<=0||w>MaxWidth)return;
        width=w;height=h;columns=(w+Tile-1)/Tile;
        rows.assign(size_t((h+Tile-1)/Tile),full?Whole():0);
    }
    int Width() const {return width;}
    int Height() const {return height;}
    int Columns() const {return columns;}
    int Rows() const {return int(rows.size());}
    bool Sized(int w,int h) const {return width==w&&height==h&&!rows.empty();}
    bool Any() const {return std::any_of(rows.begin(),rows.end(),[](uint64_t r){return r!=0;});}
    bool Full() const {return !rows.empty()&&std::all_of(rows.begin(),rows.end(),[&](uint64_t r){return r==Whole();});}
    void Fill() {std::fill(rows.begin(),rows.end(),Whole());}
    void Clear() {std::fill(rows.begin(),rows.end(),0ull);}
    bool Test(int column,int row) const {return (rows[size_t(row)]>>column)&1ull;}
    void Set(int column,int row) {rows[size_t(row)]|=1ull<<column;}
    // Every tile the rectangle touches; the part outside the picture is ignored.
    void Add(Rect r) {
        const int x0=std::max(r.x,0),y0=std::max(r.y,0),x1=std::min(r.x+r.w,width),y1=std::min(r.y+r.h,height);
        if(x0>=x1||y0>=y1)return;
        const int c0=x0/Tile,c1=(x1-1)/Tile;
        const uint64_t run=(c1-c0+1>=64?~0ull:(1ull<<(c1-c0+1))-1)<<c0;
        for(int row=y0/Tile;row<=(y1-1)/Tile;++row)rows[size_t(row)]|=run;
    }
    // Another mask of the same picture; one of another size counts as everything.
    void Add(const Mask& other) {
        if(!other.Sized(width,height)){Fill();return;}
        for(size_t i=0;i<rows.size();++i)rows[i]|=other.rows[i];
    }
    int64_t Texels() const {
        int64_t n=0;
        for(int row=0;row<Rows();++row) {
            const int h=std::min(Tile,height-row*Tile);
            for(uint64_t left=rows[size_t(row)];left;left&=left-1) {
                const int column=std::countr_zero(left);
                n+=int64_t(std::min(Tile,width-column*Tile))*h;
            }
        }
        return n;
    }
    Rect Bounds() const {
        int c0=columns,c1=-1,r0=Rows(),r1=-1;
        for(int row=0;row<Rows();++row) {
            const uint64_t bits=rows[size_t(row)];
            if(!bits)continue;
            r0=std::min(r0,row);r1=row;
            c0=std::min(c0,std::countr_zero(bits));c1=std::max(c1,63-std::countl_zero(bits));
        }
        if(c1<0)return {};
        const int x=c0*Tile,y=r0*Tile;
        return {x,y,std::min((c1+1)*Tile,width)-x,std::min((r1+1)*Tile,height)-y};
    }
    // The set tiles as rectangles: each run of tiles along a row, grown down
    // through the following rows while they hold the same run. Texels, clipped
    // to the picture. At most `most` are written; the count needed is returned
    // (more than `most`: the caller takes Bounds instead).
    size_t Rects(Rect* out,size_t most) const {
        struct Run {int c0,c1,r0,r1;};
        std::vector<Run> runs;
        for(int row=0;row<Rows();++row) {
            for(uint64_t left=rows[size_t(row)];left;) {
                const int c0=std::countr_zero(left),n=std::countr_one(left>>c0),c1=c0+n;
                left&=~((n>=64?~0ull:(1ull<<n)-1)<<c0);
                auto above=std::find_if(runs.begin(),runs.end(),[&](const Run& r){return r.r1==row&&r.c0==c0&&r.c1==c1;});
                if(above!=runs.end())above->r1=row+1;else runs.push_back({c0,c1,row,row+1});
            }
        }
        for(size_t i=0;i<runs.size()&&i<most;++i) {
            const auto& r=runs[i];const int x=r.c0*Tile,y=r.r0*Tile;
            out[i]={x,y,std::min(r.c1*Tile,width)-x,std::min(r.r1*Tile,height)-y};
        }
        return runs.size();
    }
};
// The rectangles to copy for a mask: its own while they are few, else the one round all of them.
inline size_t Uploads(const Mask& mask,Rect* out,size_t most) {
    if(!most||!mask.Any())return 0;
    const size_t n=mask.Rects(out,most);
    if(n<=most)return n;
    out[0]=mask.Bounds();return 1;
}

// A painted picture's change. serial counts the painter's pictures; the mask
// is against the picture with the serial before. bgra: the bytes are B, G, R, A.
struct Change {
    uint64_t serial=0;
    Mask mask;
    bool bgra=false;
};
// R and B exchanged in place: a picture painted RGBA for a BGRA atlas. One
// pass over whole pixels, on the painter's thread.
inline void SwapRedBlue(uint8_t* pixels,size_t count) {
    for(size_t i=0;i<count;++i) {
        uint32_t v;std::memcpy(&v,pixels+i*4,4);
        v=(v&0xff00ff00u)|((v&0xffu)<<16)|((v>>16)&0xffu);
        std::memcpy(pixels+i*4,&v,4);
    }
}
inline void CopySwapped(uint8_t* to,const uint8_t* from,size_t count) {
    for(size_t i=0;i<count;++i) {
        uint32_t v;std::memcpy(&v,from+i*4,4);
        v=(v&0xff00ff00u)|((v&0xffu)<<16)|((v>>16)&0xffu);
        std::memcpy(to+i*4,&v,4);
    }
}
// On the painter's thread: keeps the picture painted before, and says which
// tiles of the new one differ from it. A picture of another size, or the
// first, is all changed.
class Differ {
    std::vector<uint8_t> previous;
    int width=0,height=0;
    uint64_t serial=0;
public:
    Change Step(const std::vector<uint8_t>& pixels,int w,int h) {
        Change change;change.serial=++serial;change.mask=Mask(w,h);
        if(w<=0||h<=0||w>MaxWidth||pixels.size()!=size_t(w)*size_t(h)*4){previous.clear();width=height=0;return change;}
        if(previous.size()!=pixels.size()||w!=width||h!=height) {
            previous=pixels;width=w;height=h;change.mask.Fill();return change;
        }
        auto& mask=change.mask;
        const size_t stride=size_t(w)*4;
        for(int y=0;y<h;++y) {
            const int row=y/Tile;
            const uint8_t* a=pixels.data()+size_t(y)*stride;const uint8_t* b=previous.data()+size_t(y)*stride;
            for(int column=0;column<mask.Columns();++column) {
                if(mask.Test(column,row))continue;
                const size_t x=size_t(column)*Tile*4,n=std::min(size_t(Tile)*4,stride-x);
                if(std::memcmp(a+x,b+x,n))mask.Set(column,row);
            }
        }
        // Keep the new picture: its changed tiles only.
        for(int row=0;row<mask.Rows();++row)for(int column=0;column<mask.Columns();++column) {
            if(!mask.Test(column,row))continue;
            const size_t x=size_t(column)*Tile*4,n=std::min(size_t(Tile)*4,stride-x);
            for(int y=row*Tile;y<std::min((row+1)*Tile,h);++y)
                std::memcpy(previous.data()+size_t(y)*stride+x,pixels.data()+size_t(y)*stride+x,n);
        }
        return change;
    }
};
// A painter's picture made ready for its atlas, on the painter's thread: the
// channel order the atlas stores, then what changed since the one before.
inline Change Finish(std::vector<uint8_t>& pixels,int w,int h,bool bgra,Differ& differ) {
    const bool sized=w>0&&h>0&&pixels.size()==size_t(w)*size_t(h)*4;
    if(bgra&&sized)SwapRedBlue(pixels.data(),pixels.size()/4);
    auto change=differ.Step(pixels,w,h);
    change.bgra=bgra&&sized;
    return change;
}
// On the uploading thread: the change is against the picture uploaded last
// (`seen`, its serial; 0 none). A painter's result can be dropped on the way,
// and then the next one's mask misses what the dropped one changed: all of
// that picture is uploaded instead.
inline bool Follows(const Change& change,uint64_t seen,int w,int h) {
    return seen&&change.serial==seen+1&&change.mask.Sized(w,h);
}

// What each image of a swapchain is behind by. A change is marked on all of
// them; the image acquired is brought up to date by copying its own mask, and
// is then behind by nothing. New images are behind by everything.
class Lag {
    std::vector<Mask> behind;
public:
    Lag()=default;
    Lag(size_t images,int w,int h):behind(images,Mask(w,h,true)) {}
    size_t Images() const {return behind.size();}
    void Mark(Rect r) {for(auto& m:behind)m.Add(r);}
    void Mark(const Mask& mask) {for(auto& m:behind)m.Add(mask);}
    void MarkAll() {for(auto& m:behind)m.Fill();}
    const Mask& Of(size_t image) const {return behind[image];}
    void Done(size_t image) {behind[image].Clear();}
};
}
