#include "sheet_walk.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}

// A hero who walks to the last cell clicked at `speed` cells/s; `obeysStops` false:
// the game takes a stop as nothing to do and he carries on to the last walk target.
struct Sim {
    sheetwalk::Walker w;sheetwalk::Input in;
    float tx=0,ty=0,speed=9;bool obeysStops=true;
    std::vector<sheetwalk::Click> clicks;std::vector<sheetwalk::Settled> settled;int repeats=0;
    Sim(){in.now=1000;in.world=in.held=true;in.hero=true;in.px=tx=5000.4f;in.py=ty=4800.2f;}
    void Push(float sx,float sy){in.pushed=true;in.sx=sx;in.sy=sy;}
    void Release(){in.pushed=false;in.sx=in.sy=0;}
    void Step(int ms=1) {
        while(ms--) {
            ++in.now;
            const auto out=w.Step(in);
            if(out.click.kind!=sheetwalk::Kind::None) {
                clicks.push_back(out.click);
                if(out.click.kind==sheetwalk::Kind::Walk||obeysStops){tx=float(out.click.x);ty=float(out.click.y);}
            }
            if(out.repeated)++repeats;
            if(out.settled.due)settled.push_back(out.settled);
            const float dx=tx-in.px,dy=ty-in.py,d=std::sqrt(dx*dx+dy*dy),step=speed/1000;
            if(d<=step){in.px=tx;in.py=ty;}else{in.px+=dx/d*step;in.py+=dy/d*step;}
        }
    }
};

int main(){
    using sheetwalk::Kind;
    // The direction: the game's isometric screen, right = grid +x -y, up = grid -x -y.
    {
        float gx=0,gy=0;
        Check(sheetwalk::GridDirection(1,0,gx,gy)&&gx>.7f&&gy<-.7f,"right on the screen: +x -y");
        Check(sheetwalk::GridDirection(0,1,gx,gy)&&gx<-.7f&&gy<-.7f,"up on the screen: -x -y");
        Check(!sheetwalk::GridDirection(0,0,gx,gy)&&!sheetwalk::GridDirection(NAN,1,gx,gy),"no direction from nothing");
        int x=0,y=0;
        Check(sheetwalk::Cell(10.6f,10.4f,x,y)&&x==11&&y==10,"cells round to the nearest");
        Check(!sheetwalk::Cell(.2f,5,x,y)&&!sheetwalk::Cell(5,70000,x,y)&&!sheetwalk::Cell(NAN,5,x,y),"off the map: no cell");
    }
    // The stop's cell: on his way, never behind him, never far.
    for(float px=5000;px<5001;px+=.05f)for(float py=4800;py<4801;py+=.1f)for(int a=0;a<16;++a) {
        const float dx=std::cos(a*.3927f),dy=std::sin(a*.3927f);int x=0,y=0;
        Check(sheetwalk::StopCell(px,py,dx,dy,x,y),"a stop cell on the map");
        const float ox=x-px,oy=y-py,along=ox*dx+oy*dy;
        Check(along>=0&&std::sqrt(ox*ox+oy*oy)<=1.5f,"the stop: ahead of him (or level), within a cell and a half");
    }
    // Walking: a press, then the button held every ClickMs, a few cells ahead.
    {
        Sim s;s.Push(0,1);s.Step();
        Check(s.clicks.size()==1&&s.clicks[0].kind==Kind::Walk&&!s.clicks[0].held,"the first walk click is a press");
        Check(s.clicks[0].x<5000&&s.clicks[0].y<4800,"stick up walks -x -y");
        const float ox=s.clicks[0].x-5000.4f,oy=s.clicks[0].y-4800.2f,lead=std::sqrt(ox*ox+oy*oy);
        Check(lead>=sheetwalk::LeadMax-.8f&&lead<=sheetwalk::LeadMax+.8f,"a full push leads LeadMax cells");
        s.Step(sheetwalk::ClickMs-2);Check(s.clicks.size()==1,"no second click inside ClickMs");
        s.Step(2);Check(s.clicks.size()==2&&s.clicks[1].kind==Kind::Walk&&s.clicks[1].held,"then the held button");
        s.Step(1000);Check(s.clicks.size()>=20&&s.clicks.size()<=23,"about one click per ClickMs");
        const float walked=std::sqrt((s.in.px-5000.4f)*(s.in.px-5000.4f)+(s.in.py-4800.2f)*(s.in.py-4800.2f));
        Check(walked>9.f,"he walks the whole second at his pace, the lead always ahead of him");
        Sim small;small.Push(0,.4f);small.Step();
        const float sx=small.clicks[0].x-5000.4f,sy=small.clicks[0].y-4800.2f,near=std::sqrt(sx*sx+sy*sy);
        Check(near<lead&&near>=sheetwalk::LeadMin-.8f,"a slight push leads less");
    }
    // Let go: a stop at once, toward the next cell on his way; he stands within a cell or two.
    {
        Sim s;s.Push(1,0);s.Step(400);
        const float atX=s.in.px,atY=s.in.py;const size_t before=s.clicks.size();
        s.Release();s.Step();
        Check(s.clicks.size()==before+1&&s.clicks.back().kind==Kind::Stop&&!s.clicks.back().held,"the let-go stops him on the next pass");
        Check(s.w.Stopping(),"and watches the stop");
        s.Step(1000);
        const float went=std::sqrt((s.in.px-atX)*(s.in.px-atX)+(s.in.py-atY)*(s.in.py-atY));
        Check(went<=1.6f,"he goes on at most a cell and a half");
        Check(s.repeats==0&&s.clicks.size()==before+1,"an obeyed stop is sent once");
        Check(s.settled.size()==1&&!s.settled[0].moving&&s.settled[0].repeats==0&&s.settled[0].cells<=1.6f&&
              s.settled[0].ms<=sheetwalk::SampleMs+250,"the watch reports where and when he stood");
        Check(!s.w.Busy(),"then idle");
    }
    // A slowed hero (chilled: 3 cells/s) still reaching the stop's cell is not stopped again.
    {
        Sim s;s.speed=3;s.Push(1,0);s.Step(500);s.Release();s.Step(1500);
        Check(s.repeats==0&&s.settled.size()==1&&!s.settled[0].moving,"a slow arrival is no reason to stop again");
    }
    // A stop the game ignores: sent again while he walks on, at most MaxRepeats times.
    {
        Sim s;s.obeysStops=false;s.Push(1,0);s.Step(400);s.Release();s.Step(1500);
        Check(s.repeats>=1&&s.repeats<=sheetwalk::MaxRepeats,"still walking after CheckMs: the stop again");
        Check(s.settled.size()==1,"the watch ends and reports");
        int stops=0;for(const auto& c:s.clicks)stops+=c.kind==Kind::Stop;
        Check(stops==1+s.repeats,"one stop and its repeats");
        Check(s.settled[0].cells<=sheetwalk::LeadMax+1.f,"ignored, he still goes no further than the last lead");
    }
    // The menu closed as the stick was let go: the stop still goes.
    {
        Sim s;s.Push(1,0);s.Step(300);s.Release();s.in.held=false;const size_t before=s.clicks.size();s.Step();
        Check(s.clicks.size()==before+1&&s.clicks.back().kind==Kind::Stop&&s.w.Stopping(),"menu gone and stick let go: stopped");
    }
    // The menu closed under a pushed stick: one stop, then the game's own walk, unwatched.
    {
        Sim s;s.Push(1,0);s.Step(300);s.in.held=false;const size_t before=s.clicks.size();s.Step();
        Check(s.clicks.size()==before+1&&s.clicks.back().kind==Kind::Stop&&!s.w.Busy(),"menu gone, stick held: one stop, no watch");
        s.Step(1000);Check(s.clicks.size()==before+1,"nothing more while the game walks him");
    }
    // Pushed again while a stop is watched: a fresh walk (a press), the watch dropped.
    {
        Sim s;s.Push(1,0);s.Step(300);s.Release();s.Step(50);s.Push(0,-1);s.Step();
        Check(s.clicks.back().kind==Kind::Walk&&!s.clicks.back().held&&s.w.Walking(),"walking again: a press");
        Check(s.settled.empty(),"no report for an interrupted stop");
    }
    // The stick pushed some other way while the stop is watched (menu closed): the watch ends.
    {
        Sim s;s.Push(1,0);s.Step(300);s.Release();s.Step(20);s.in.held=false;s.Push(0,1);s.Step(500);
        int stops=0;for(const auto& c:s.clicks)stops+=c.kind==Kind::Stop;
        Check(stops==1&&!s.w.Busy()&&s.settled.empty(),"no repeat stop against the game's own walk");
    }
    // Out of the game, or the hero unknown, at the let-go: nothing sent, idle.
    {
        Sim s;s.Push(1,0);s.Step(300);s.Release();s.in.world=false;const size_t before=s.clicks.size();s.Step();
        Check(s.clicks.size()==before&&!s.w.Busy(),"out of the game: no stop");
        Sim t;t.Push(1,0);t.Step(300);t.Release();t.in.hero=false;const size_t n=t.clicks.size();t.Step();
        Check(t.clicks.size()==n&&!t.w.Busy(),"no hero: no stop");
        Sim u;u.in.hero=false;u.Push(1,0);u.Step(200);Check(u.clicks.empty(),"no hero: no walk");
        Sim v;v.in.held=false;v.Push(1,0);v.Step(200);Check(v.clicks.empty(),"no held menu: no walk");
        Sim z;z.Push(NAN,1);z.Step(200);Check(z.clicks.empty(),"a bad stick: no walk");
    }
    // Idle: nothing at all.
    {
        Sim s;s.Step(2000);Check(s.clicks.empty()&&s.settled.empty()&&!s.w.Busy(),"idle stays quiet");
    }
    std::puts("sheet walk: all checks passed");
    return 0;
}
