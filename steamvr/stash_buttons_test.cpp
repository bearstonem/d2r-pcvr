#include "stash_buttons.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace framevr;
int main() {
    using Names=std::vector<std::string>;
    assert(stashbuttons::Shown(Names{"BankExpansionLayout","PlayerInventoryExpansionLayout"}));
    assert(stashbuttons::Shown(Names{"BankOriginalLayout"}));
    assert(stashbuttons::Shown(Names{"StashPanel"}));
    for(const auto* name:{"PlayerInventoryExpansionLayout","UISwitcher","CharacterStats","Vendor","Trade","Gamble","HoradricCubeLayout"}) {
        assert(!stashbuttons::Shown(Names{name}));
        if(std::string_view(name)!="PlayerInventoryExpansionLayout"&&std::string_view(name)!="UISwitcher"&&std::string_view(name)!="CharacterStats")
            assert(!stashbuttons::Shown(Names{"BankExpansionLayout",name}));
    }
    for(float aspect:{16.f/9,4.f/3,1.f}) {
        const float w=1.2f,h=w/aspect;
        const float band=std::min(h,w*9.f/16.f),bandWidth=band*16.f/9.f;
        for(int i=0;i<2;++i) {
            const auto s=stashbuttons::Place(w,h,i);
            const float u=.5f+s.x/w,v=.5f-s.y/h;
            assert(respecbutton::Hit(s,w,h,u,v));
            assert(!respecbutton::Hit(stashbuttons::Place(w,h,1-i),w,h,u,v));
            assert(u>0&&u<.5f&&v>0&&v<1);
            assert(s.x-s.width/2>-w/2&&s.y-s.height/2>-h/2);
            // Mockup: both plaques stay inside the stash frame, below the
            // grid/page hint and beside (never on) the central gold field.
            const float x=.5f+s.x/bandWidth,y=(h*.5f-s.y)/band;
            assert(y-s.height/(2*band)>.68f&&y+s.height/(2*band)<.74f);
            if(i==0)assert(x-s.width/(2*bandWidth)>.16f&&x+s.width/(2*bandWidth)<.26f);
            else assert(x-s.width/(2*bandWidth)>.36f&&x+s.width/(2*bandWidth)<.46f);
        }
    }
    assert(stashbuttons::Place(0,1,0).width==0);
    for(int direction=0;direction<2;++direction) {
        stashbuttons::Input in;
        const auto button=direction?menus::RightBumper:menus::LeftBumper;
        assert(!in.Step(true,direction,true,0)); // pre-held on entry
        assert(!in.Step(true,direction,false,1));
        assert(in.Step(true,direction,true,2)==button);
        assert(in.Step(true,1-direction,true,30)==button); // crossing never reverses
        assert(in.Step(true,-1,false,31)==button); // minimum poll duration
        assert(!in.Step(true,-1,false,82));
        assert(!in.Step(true,-1,true,90)); // started off button
        assert(!in.Step(true,direction,true,100));
        in.Step(true,-1,false,110);
        assert(in.Step(true,direction,true,120)==button);
        assert(!in.Step(true,direction,true,200)); // release native bumper before repeat
        assert(!in.Step(true,1-direction,true,220));
        assert(!in.Step(true,direction,true,520)); // bounded held pulse
        assert(!in.Step(true,1-direction,true,530));
        in.Step(false,-1,false,540);
        assert(!in.Step(true,direction,true,550)); // requires release after close/focus loss
        in.Step(true,direction,false,560);
        assert(in.Step(true,direction,true,570)==button);
        assert(!in.Step(false,direction,true,571)); // cancels immediately
    }
    std::puts("Stash navigation: visibility, placement, either bumper, fresh presses, direction lock, poll duration and cancellation passed");
}
