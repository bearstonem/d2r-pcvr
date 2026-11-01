#include "menu_drop.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static bool Near(float a,float b,float e=1e-4f){return std::fabs(a-b)<=e;}
int main(){
    using namespace menudrop;
    // The art: on the leather the surface is the band; on the room panel the band is
    // the top 9/16 of a square canvas's width.
    {
        const float w=.57f,h=w*9/16;
        Check(Near(BandShare(w,h),1)&&Near(BandShare(1.2f,1.2f),9.f/16),"the band's share of the surface");
        Check(InArt(.5f,.5f,w,h)&&InArt(.13f,.1f,w,h)&&InArt(.87f,.9f,w,h),"the menu's art");
        Check(!InArt(.05f,.5f,w,h)&&!InArt(.95f,.5f,w,h),"the band's see-through margins are outside it");
        Check(!InArt(.5f,.97f,w,h),"below the hint row and the stats button: outside");
        Check(InArt(.5f,.93f,w,h),"the stats button's row counts as the menu (no drop there)");
        Check(InArt(.5f,.5f*9/16,1.2f,1.2f)&&!InArt(.5f,.8f,1.2f,1.2f),"room panel: the art in the top band; the canvas below is outside");
        Check(!InArt(NAN,.5f,w,h),"no hit, not the art");
    }
    // X: one per pull begun off the menu with an item on the cursor.
    {
        Drop d;uint64_t t=1000;
        Check(!d.Step(true,true,true,t).press,"a trigger already down does nothing");
        d.Step(true,true,false,t+=16);
        Check(!d.Step(false,true,true,t+=16).press,"a pull on the menu: the game's own click, not a drop");
        d.Step(false,true,false,t+=16);
        Check(!d.Step(true,false,true,t+=16).press,"nothing on the cursor: no X (it would drop the selected item)");
        d.Step(true,false,false,t+=16);
        auto o=d.Step(true,true,true,t+=16);Check(o.press&&d.Down(),"off the menu with an item: X down");
        o=d.Step(false,false,false,t+=16);Check(!o.release&&d.Down(),"a quick tap still holds X MinMs");
        o=d.Step(false,false,false,t+=MinMs);Check(o.release&&!d.Down(),"then lets go");
        o=d.Step(false,false,false,t+=CheckMs);Check(o.check,"then a check whether the item left the cursor");
        o=d.Step(true,true,true,t+=16);Check(o.press,"another pull, another X");
        for(int i=0;i<200&&d.Down();++i)d.Step(true,true,true,t+=16);
        Check(!d.Down(),"never held past HoldMs");
        d.Reset();Check(!d.Step(true,true,true,t+=16).press,"reset: the trigger must be let go first");
    }
    std::puts("Menu drop: off the art with an item on the cursor, one X per pull, checked after passed");
    return 0;
}
