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
    // Three plaques: the two tab buttons and the Collection button.
    assert(stashbuttons::ButtonCount==3);
    for(float aspect:{16.f/9,4.f/3,1.f}) {
        const float w=1.2f,h=w/aspect;
        const float band=std::min(h,w*9.f/16.f),bandWidth=band*16.f/9.f;
        for(int i=0;i<stashbuttons::ButtonCount;++i) {
            const auto s=stashbuttons::Place(w,h,i);
            const float u=.5f+s.x/w,v=.5f-s.y/h;
            assert(respecbutton::Hit(s,w,h,u,v));
            for(int other=0;other<stashbuttons::ButtonCount;++other)
                if(other!=i)assert(!respecbutton::Hit(stashbuttons::Place(w,h,other),w,h,u,v));
            assert(u>0&&u<1&&v>0&&v<1);
            assert(s.x-s.width/2>-w/2&&s.y-s.height/2>-h/2);
            // Recess bounds estimated from the in-game screenshot. Every button
            // stays below the grid and clear of the central gold field.
            const float x=.5f+s.x/bandWidth,y=(h*.5f-s.y)/band;
            assert(y-s.height/(2*band)>.670f&&y-s.height/(2*band)<.682f);
            assert(y+s.height/(2*band)>.750f&&y+s.height/(2*band)<.760f);
            const float left=x-s.width/(2*bandWidth),right=x+s.width/(2*bandWidth);
            const float bounds[3][2]={{.176f,.260f},{.362f,.446f},{.548f,.632f}};
            assert(left>bounds[i][0]&&right<bounds[i][1]);
        }
    }
    assert(stashbuttons::Place(0,1,0).width==0);
    // One pull owns one action until release, even if the ray crosses to another
    // plaque; the two tab buttons and the Collection button each answer for
    // themselves.
    const stashbuttons::Action wanted[stashbuttons::ButtonCount]={stashbuttons::Action::Previous,stashbuttons::Action::Next,stashbuttons::Action::Collection};
    for(int index=0;index<stashbuttons::ButtonCount;++index) {
        stashbuttons::Input in;
        const auto action=wanted[index];
        assert(in.Step(true,index,true,0)==stashbuttons::Action::None); // pre-held on entry
        assert(in.Step(true,index,false,1)==stashbuttons::Action::None);
        assert(in.Step(true,index,true,2)==action);
        assert(in.Step(true,(index+1)%stashbuttons::ButtonCount,true,30)==action); // crossing never switches
        assert(in.Step(true,-1,false,31)==action); // minimum poll duration
        assert(in.Step(true,-1,false,82)==stashbuttons::Action::None);
        assert(in.Step(true,-1,true,90)==stashbuttons::Action::None); // started off a plaque
        assert(in.Step(true,index,true,100)==stashbuttons::Action::None);
        in.Step(true,-1,false,110);
        assert(in.Step(true,index,true,120)==action);
        assert(in.Step(true,index,true,200)==stashbuttons::Action::None); // release before repeat
        assert(in.Step(true,(index+1)%stashbuttons::ButtonCount,true,220)==stashbuttons::Action::None);
        assert(in.Step(true,index,true,520)==stashbuttons::Action::None); // bounded held pulse
        assert(in.Step(true,(index+1)%stashbuttons::ButtonCount,true,530)==stashbuttons::Action::None);
        in.Step(false,-1,false,540);
        assert(in.Step(true,index,true,550)==stashbuttons::Action::None); // requires release after close/focus loss
        in.Step(true,index,false,560);
        assert(in.Step(true,index,true,570)==action);
        assert(in.Step(false,index,true,571)==stashbuttons::Action::None); // cancels immediately
    }
    std::puts("Stash navigation: visibility, placement, three plaques, one action per pull, fresh presses, direction lock, poll duration and cancellation passed");
}
