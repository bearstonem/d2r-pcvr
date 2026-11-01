#include "panel_atlas.h"
#include "leather_menu.h"
#include <cassert>
#include <cstdio>
using namespace framevr::panelatlas;

int main() {
    // Every region inside its image, each pair a gap apart.
    const Region statics[]={Gameplay,Pointer,Compass,Leather,Stats,StatsLit,
        Respec(0),Respec(1),Respec(2),Respec(3),Respec(4),Respec(5),Respec(6),Respec(7)};
    constexpr int Statics=int(sizeof statics/sizeof statics[0]);
    static_assert(Statics==6+RespecPictures,"every static picture checked");
    const Region shared[]={Settings,Blood,Strain,Notice,BowLaser};
    for(const auto& r:statics)assert(Inside(r,StaticWidth,StaticHeight));
    for(const auto& r:shared)assert(Inside(r,SharedWidth,SharedHeight));
    for(int i=0;i<Statics;++i)for(int j=i+1;j<Statics;++j)assert(Apart(statics[i],statics[j]));
    for(int i=0;i<5;++i)for(int j=i+1;j<5;++j)assert(Apart(shared[i],shared[j]));
    // The sizes the panels paint at, and the regions the transport's quads read.
    assert(Settings.w==1280&&Settings.h==1024&&Blood.w==1024&&Strain.w==256&&Notice.w==512&&Notice.h==112&&BowLaser.w==64&&BowLaser.h==160);
    assert(Pointer.w==64&&Pointer.h==96&&Compass.w==1536&&Compass.h==112);
    // The leather region holds the largest art the cache may carry, and the stand-in.
    assert(Leather.w==framevr::leather::MaxArtWidth&&Leather.h==framevr::leather::MaxArtHeight);
    assert(framevr::leather::PlaceholderWidth<=Leather.w&&framevr::leather::PlaceholderHeight<=Leather.h);
    // The padding quad's transparent texel: the pointer's (0,32).
    assert(Pointer.x==1032&&Pointer.y==0);
    assert(Stats.w==StatsSize&&Stats.h==StatsSize&&StatsLit.w==StatsSize&&StatsLit.h==StatsSize);
    assert(Respec(0).w==RespecWidth&&Respec(0).h==RespecHeight&&RespecWidth==5*RespecHeight);
    assert(RespecLitReset<RespecPictures&&RespecLitConfirm<RespecPictures);
    // Apart is strict: touching or closer than Gap is not apart; a gap of exactly Gap is.
    assert(!Apart({0,0,10,10},{10,0,10,10})&&!Apart({0,0,10,10},{17,0,10,10})&&Apart({0,0,10,10},{18,0,10,10}));
    assert(!Inside({0,0,10,10},9,10)&&!Inside({-1,0,1,1},10,10)&&Inside({0,0,10,10},10,10));
    std::printf("Panel atlases: static %dx%d (%d pictures), shared %dx%d (5 panels), all inside and %d texels apart\n",
        StaticWidth,StaticHeight,Statics,SharedWidth,SharedHeight,Gap);
    return 0;
}
