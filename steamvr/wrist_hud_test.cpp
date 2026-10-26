#include "wrist_hud.h"
#include "wrist_compass.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace framevr::wrist;
int main() {
    MapLayoutLease layout;
    assert(layout.Step(false,0)==-1);
    assert(layout.Step(true,0)==1&&layout.Step(true,1)==-1);
    assert(layout.Step(false,1)==0&&layout.Step(false,0)==-1);
    assert(layout.Step(true,2)==1&&layout.Step(false,1)==2);
    assert(layout.Step(true,0)==1&&layout.Step(true,2)==1&&layout.Step(false,1)==2);
    assert(layout.Step(true,0)==1&&layout.Step(false,2)==-1);
    assert(layout.Step(true,1)==-1&&layout.Step(false,1)==-1);
    {   // Map view = Full map: borrows 0, gives the player's corner back
        MapLayoutLease full;
        assert(full.Step(true,1,0)==0&&full.Step(true,0,0)==-1&&full.Step(false,0)==1);
        assert(full.Step(true,1,1)==-1&&full.Step(true,1,0)==0&&full.Step(true,0,0)==-1);   // switched while borrowed
        assert(full.Step(false,0)==1);   // the player's left corner, not lost to the borrowed 1
        assert(full.Step(true,2,0)==0&&full.Step(true,0,1)==1&&full.Step(true,1,1)==-1&&full.Step(false,1)==2);
    }
    using namespace framevr;
    compass::Heading heading;
    const float north=.71f;
    for(int i=0;i<8;++i) {
        const float turn=i*compass::Tau/8;
        assert(heading.Update(1,true,math::Yaw(-turn),north,north));
        assert(compass::Crop(heading.bearing)==i*compass::Period/8); // physical right turn
        assert(heading.Update(1,true,math::Yaw(0),north-turn,north));
        assert(compass::Crop(heading.bearing)==i*compass::Period/8); // stick right turn
        assert(heading.Update(1,true,math::Yaw(.3f-turn),north-.3f,north));
        assert(compass::Crop(heading.bearing)==i*compass::Period/8); // recentered stage
    }
    assert(heading.Update(1,true,math::Yaw(-compass::Tau/4),north,north));
    const math::Quat down{std::sin(compass::Tau/8),0,0,std::cos(compass::Tau/8)};
    assert(heading.Update(1,true,down,north,north)&&compass::Crop(heading.bearing)==256);
    assert(heading.Update(1,true,down,north-compass::Tau/4,north)&&compass::Crop(heading.bearing)==512);
    assert(!heading.Update(2,true,down,north,north)); // a new player cannot inherit the old bearing
    assert(heading.Update(2,true,math::Yaw(0),north,north));
    assert(!heading.Update(2,false,math::Yaw(0),north,north));
    assert(!heading.Update(2,true,down,north,north)); // tracking loss requires a reliable heading
    assert(!heading.Update(2,true,math::Yaw(0),NAN,north));
    assert(!heading.Update(2,true,{0,0,0,0},north,north));
    assert(!heading.Update(0,true,math::Yaw(0),north,north));
    for(int degrees=-720;degrees<=720;++degrees) {
        const auto x=compass::Crop(degrees*compass::Tau/360);
        assert(x>=0&&x+compass::Window<=compass::Width);
    }
    assert(compass::Crop(-compass::Tau/4)==768&&compass::Crop(compass::Tau)==0&&compass::Crop(NAN)==0);
    Experience xp{1,99,0,0,500,true};
    assert(xp.Valid()&&xp.Progress()==0);
    xp.current=499;assert(std::abs(xp.Progress()-.998)<1e-12);
    // Mixed level/stat reads during level-up must not show a false full bar.
    xp.current=500;assert(!xp.Valid());
    xp={2,99,500,500,1500,true};assert(xp.Valid()&&xp.Progress()==0);
    xp.current=499;assert(!xp.Valid()); // death penalty/transition stale bounds
    xp={98,99,3400000000u,3229426756u,3520485254u,true};
    assert(xp.Valid()&&xp.Progress()>.58&&xp.Progress()<.59);
    xp={99,99,3520485254u,3520485254u,3837739017u,true};
    assert(xp.Maximum()&&xp.Progress()==1);
    xp.level=100;assert(!xp.Valid());
    xp={};assert(!xp.Valid()&&xp.Progress()==0);
    xp={1,99,10,0,0,true};assert(!xp.Valid());
    xp={1,99,10,0,500,false};assert(!xp.Valid());

    MapLease map;
    assert(map.Step(1,1000,true,false,false)==1); // temporarily open
    assert(map.Active(1000,false));
    assert(map.Step(1,1200,true,false,true)==-1); // no repeat toggles
    assert(map.Step(1,1700,false,false,true)==-1); // brief pose gap
    assert(map.Step(1,1950,false,false,true)==0); // restore closed
    assert(!map.Active(1950,false));
    assert(map.Step(1,2200,true,false,true)==-1); // user's already-open map
    assert(map.Step(1,3000,false,false,true)==-1); // must remain open
    assert(map.Step(1,3200,true,false,false)==1);
    assert(map.Step(1,3300,false,true,true)==0); // menus release ownership
    assert(!map.Active(3300,true));
    assert(map.Step(1,3400,false,false,false)==-1); // no stale menu reopen
    assert(map.Step(1,3500,true,false,false)==1);
    assert(map.Step(2,3600,false,false,true)==0); // new character clears lease
    assert(!map.Active(3600,false));
    assert(map.Step(2,3700,true,false,false)==1);
    assert(map.Step(0,3800,false,false,true)==0); // exit session
    assert(!map.Active(3800,false));
    std::puts("Wrist HUD: unsigned XP, level-up/max/unknown states, map restoration, menus and session changes passed");
}
