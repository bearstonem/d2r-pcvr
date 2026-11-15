// Floating map grab hint (grab_hint.h): it shows only inside the pull band with the wrist map shown and no copy out; the
// exit gap; the fades and the tick; after three pulls only the glow; a new game starts over; the loop and the glow in step;
// a gap in the steps clears it; where the quads go (the border's strips never overlap); the pictures; the layer budget
// (the strips go first); the band is the floating map's own.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "floating_map.h"
#include "grab_hint.h"
#include "hand_role.h"
#include "panel_atlas.h"

namespace gh=framevr::grabhint;
namespace fm=framevr::floatmap;
namespace math=framevr::math;
static int g_fail=0;
static void Check(bool ok,const char* what){if(!ok){std::printf("FAIL: %s\n",what);++g_fail;}}
static bool Near(float a,float b,float tol=1e-3f){return std::fabs(a-b)<=tol;}

static gh::Input In(double t,float miss,bool ready=true,bool copyOut=false){
    gh::Input x;x.time=t;x.session=7;x.ready=ready;x.copyOut=copyOut;x.miss=miss;x.band=15;return x;
}
// Steps at 90 Hz from t for `s` seconds at a fixed miss; returns the last output.
static gh::Output Run(gh::Engine& e,double& t,double s,float miss,const gh::Tuning& tu={},bool ready=true,bool copyOut=false){
    gh::Output o;
    for(double end=t+s;t<end;t+=1.0/90)o=e.Step(In(t,miss,ready,copyOut),tu);
    return o;
}

int main(){
    {   // The shipped values.
        const gh::Tuning tu;
        Check(tu.pullsLimit==3&&Near(tu.glow,.7f)&&Near(tu.reachM,.01f)&&Near(tu.buzz,.15f)&&Near(gh::EdgeM,.0025f)&&Near(gh::SignM,.03f)&&Near(gh::SignGapM,.015f),
              "shipped: the sign stops after 3 pulls, glow 70%, reach 1 cm, tick 15%, border 2.5 mm, sign 3 cm, 1.5 cm from the edge");
        Check(gh::CycleS>=1.0,"one loop at least a second (under one pulse a second)");
    }
    {   // Shows only inside the band, with the pull's gates and no copy out.
        gh::Engine e;double t=0;
        auto o=Run(e,t,.5,30);
        Check(!o.in&&!o.Any(),"aimed away: nothing");
        o=Run(e,t,.5,10,{},false);
        Check(!o.in&&!o.Any(),"in the band but the gates fail (wrist hidden, hand busy, Right Grip held): nothing");
        o=Run(e,t,.5,10,{},true,true);
        Check(!o.in&&!o.Any(),"in the band with a copy out: nothing");
        o=Run(e,t,.5,10);
        Check(o.in&&o.Any()&&Near(o.sign,1)&&o.ring>0,"in the band, ready, no copy: the sign and the glow");
        o=e.Step(In(t,10,true,true));t+=1.0/90;
        Check(!o.in&&o.event==gh::Event::Hidden,"a copy comes out: it hides at once (fades out)");
    }
    {   // The exit gap: in at the band, out only OutExtraDeg past it.
        gh::Engine e;double t=0;
        auto o=Run(e,t,.1,15.5f);
        Check(!o.in,"just outside the band: not in");
        o=e.Step(In(t,15));t+=1.0/90;
        Check(o.in&&o.event==gh::Event::Shown,"at the band: in, shown");
        o=Run(e,t,.3,19.5f);
        Check(o.in&&o.event==gh::Event::None,"4.5 deg past the band: still in (no flicker)");
        o=e.Step(In(t,20.5f));t+=1.0/90;
        Check(!o.in&&o.event==gh::Event::Hidden,"past the band plus the gap: hidden");
        o=e.Step(In(t,16));t+=1.0/90;
        Check(!o.in,"back inside the gap but outside the band: stays hidden");
        gh::Engine b;auto x=In(0,.5f);x.band=0;
        Check(!b.Step(x).in,"band 0 (at a belt bottle or holster): half a degree off the map is not in");
        x=In(1.0/90,0);x.band=0;
        Check(b.Step(x).in,"band 0: exactly on the map is");
    }
    {   // Fades: in over FadeInS, out over FadeOutS; the tick once a showing.
        gh::Engine e;double t=0;
        auto o=e.Step(In(t,0));t+=1.0/90;
        Check(Near(o.buzz,gh::Buzz)&&o.event==gh::Event::Shown&&o.level<.1f,"shown: one tick, starting faint");
        int buzzes=0;for(int i=0;i<20;++i){o=e.Step(In(t,0));t+=1.0/90;buzzes+=o.buzz>0;}
        Check(buzzes==0&&Near(o.level,1),"one tick a showing; full after 150 ms");
        o=e.Step(In(t,40));t+=1.0/90;
        Check(o.level>.5f&&o.Any(),"aimed away: fading out, not gone");
        o=Run(e,t,.21,40);
        Check(Near(o.level,0)&&!o.Any(),"gone after 200 ms");
        gh::Tuning quiet;quiet.buzz=0;gh::Engine e2;
        Check(e2.Step(In(0,0),quiet).buzz==0,"tick 0: none");
        // The tick re-arms only after the hint was gone TickRearmS: aiming in and out at the band's edge ticks once.
        gh::Engine j;double tj=0;
        o=j.Step(In(tj,0));tj+=1.0/90;Check(o.buzz>0,"the first showing ticks");
        o=Run(j,tj,.2,0);
        o=j.Step(In(tj,30));tj+=1.0/90;Check(o.event==gh::Event::Hidden,"aimed away: hidden");
        o=Run(j,tj,.1,30);
        o=j.Step(In(tj,0));tj+=1.0/90;Check(o.event==gh::Event::Shown&&o.buzz==0,"back within 0.3 s: shown, no tick");
        o=j.Step(In(tj,30));tj+=1.0/90;
        o=Run(j,tj,gh::TickRearmS+.05,30);
        o=j.Step(In(tj,0));Check(o.event==gh::Event::Shown&&o.buzz>0,"back after 0.3 s or more: a tick again");
    }
    {   // After three pulls in a game only the glow; never (0) keeps the sign; a new game starts over.
        gh::Engine e;double t=0;
        for(int i=1;i<=3;++i){
            auto x=In(t,0);x.pulled=true;
            const auto o=e.Step(x);t+=1.0/90;
            Check(o.pulledNow&&o.pulls==unsigned(i)&&o.signStops==(i==3)&&!o.in,"each pull counted, the hint hides on it; the third stops the sign");
            Run(e,t,.3,0,{},true,true);   // the copy is out
            Run(e,t,.3,40);               // put away, aimed away
        }
        auto o=Run(e,t,.3,0);
        Check(o.in&&o.sign==0&&o.ring>0,"after 3 pulls: only the glow");
        gh::Engine e2;double t2=0;gh::Tuning always;always.pullsLimit=0;
        for(int i=0;i<5;++i){auto p=In(t2,0);p.pulled=true;e2.Step(p,always);t2+=.4;}
        o=Run(e2,t2,.3,0,always);
        Check(o.sign>.99f&&o.pulls==5,"never (0): the sign stays after 5 pulls");
        gh::Engine e3;double t3=0;
        for(int i=0;i<3;++i){auto p=In(t3,40);p.pulled=true;e3.Step(p);t3+=.1;}
        auto n=In(t3,0);n.session=99;e3.Step(n);t3+=1.0/90;
        o=Run(e3,t3,.3,0);
        Check(o.pulls==0&&o.sign>.99f,"a new game (Save and Exit): the count starts over, the sign is back");
        gh::Engine e4;double t4=0;
        for(int i=0;i<3;++i){auto p=In(t4,0);p.pulled=true;e4.Step(p);t4+=.4;}
        auto l=In(t4+=3,0);e4.Step(l);
        o=Run(e4,t4,.3,0);
        Check(o.pulls==3&&o.sign==0,"a load screen (a gap in the same game) keeps the count");
    }
    {   // A gap in the steps (the setting Off, a load screen): it starts hidden, nothing left over.
        gh::Engine e;double t=0;
        auto o=Run(e,t,.5,0);
        Check(o.Any(),"showing before the gap");
        t+=2;
        o=e.Step(In(t,40));
        Check(!o.Any()&&!o.in&&o.event==gh::Event::None,"after a gap, aimed away: nothing drawn, no stale hidden line");
        t+=2;
        o=e.Step(In(t,0));
        Check(o.in&&o.event==gh::Event::Shown&&o.level<.1f,"after a gap, in the band: shown again from faint");
        gh::Engine b;double tb=5;Run(b,tb,.3,0);
        o=b.Step(In(1,0));
        Check(o.level<.1f,"time going backwards: starts over");
    }
    {   // The loop: the pause on frame 0, then 8 frames at 8 a second; the glow reaches out only on the slide frames.
        gh::Engine e;double t=0;
        auto o=e.Step(In(t,0));
        Check(o.frame==0&&o.lobe==0&&!o.Any(),"shown: starts faint (nothing yet), no glow out");
        int seen[gh::Frames]{};bool lobeOffSlide=false,lobeOnSlide=false,ringPress=false,slideOff=false,slideBack=true;
        float lastSlide=0,maxSlide=0;bool slideMonotonic=true;
        for(int i=1;i<int(gh::CycleS*90);++i){
            t=i/90.0;o=e.Step(In(t,0));
            if(o.frame>=0&&o.frame<gh::Frames)++seen[o.frame];
            if(o.frame<gh::SlideFirst&&o.lobe>0)lobeOffSlide=true;
            if((o.frame<gh::SlideFirst||o.frame>gh::SlideLast)&&o.slide!=0)slideOff=true;
            if(o.frame==gh::Frames-1&&(o.slide!=0||o.lobe!=0))slideBack=false;
            if(o.frame>=gh::SlideFirst&&o.frame<=gh::SlideLast){slideMonotonic=slideMonotonic&&o.slide>=lastSlide-1e-5f;lastSlide=o.slide;maxSlide=std::fmax(maxSlide,o.slide);}
            if(o.frame>=gh::SlideFirst+1&&o.lobe>0&&o.lobeM>0)lobeOnSlide=true;
            if(o.frame>=gh::PressFirst&&o.frame<gh::SlideFirst&&o.ring>gh::Glow*o.level*.9f)ringPress=true;
            if(Near(float(t),.05f,.006f))Check(o.frame==0&&o.sign>0,"in the pause: frame 0, the sign showing");
        }
        bool all=true;for(int f=1;f<gh::Frames;++f)all=all&&seen[f]>=10&&seen[f]<=13;
        Check(all,"each frame about 1/8 s (11 or 12 headset frames at 90 Hz)");
        Check(!lobeOffSlide&&lobeOnSlide,"the glow reaches out only while the controller slides");
        Check(!slideOff&&slideMonotonic&&maxSlide>.9f,"the sign moves only on the slide frames (4-6), steadily, almost all the way");
        Check(slideBack,"the last frame: the sign back at rest, no glow out");
        Check(ringPress,"the border brightens with the squeeze");
        o=e.Step(In(gh::CycleS+.01,0));
        Check(o.frame==0,"it loops: the pause on frame 0 again");
        gh::Tuning border;border.reachM=0;
        gh::Engine r;double tr=0;bool lobe=false;
        for(int i=0;i<180;++i){o=r.Step(In(tr,0),border);tr+=1.0/90;lobe=lobe||o.lobe>0||o.lobeM>0;}
        Check(!lobe&&o.ring>0,"reach 0: the border only");
        gh::Tuning dark;dark.glow=0;
        gh::Engine d;double td=0;o=Run(d,td,.3,0,dark);
        Check(o.ring==0&&o.lobe==0&&o.sign>.99f,"glow 0: only the sign");
    }
    {   // Where the quads go: the pulling hand's side; four strips round the border that never overlap.
        const math::Quat q{0,0,0,1};
        const float size=.24f,half=size*.5f,e=gh::EdgeM,in=gh::InnerM;
        const auto r=gh::Place({0,1,-.4f},q,size,1,.01f);
        Check(Near(r.sign.p.x,half+.015f+.015f)&&r.sign.p.y<1&&Near(r.sign.p.y-1,(.03f-size)*.5f)&&Near(r.sign.w,.03f),"right-handed: the sign right of the map, its bottom on the map's");
        Check(Near(r.lobe.p.x,half+.005f)&&Near(r.lobe.w,.01f)&&Near(r.lobe.h,size*(1+2*gh::LobeOver)),"the glow out starts at the right edge");
        Check(Near(r.edge[0].p.x+r.edge[0].w*.5f,half+e,1e-5f)&&Near(r.edge[0].p.x-r.edge[0].w*.5f,half-in,1e-5f),
              "the right strip: from 1.25 mm inside the edge to 2.5 mm past it");
        // Each strip's rectangle in the map's plane (x, y as seen on its face): no two overlap, and together they cover
        // the whole band round the border, its corners included.
        struct Rect {float x0,y0,x1,y1;};
        Rect rect[gh::Edges];
        for(int i=0;i<gh::Edges;++i){
            const auto along=math::Rotate(r.edge[i].q,{0,1,0}),across=math::Rotate(r.edge[i].q,{1,0,0});
            const float hx=std::fabs(across.x)*r.edge[i].w*.5f+std::fabs(along.x)*r.edge[i].h*.5f;
            const float hy=std::fabs(across.y)*r.edge[i].w*.5f+std::fabs(along.y)*r.edge[i].h*.5f;
            rect[i]={r.edge[i].p.x-hx,r.edge[i].p.y-1-hy,r.edge[i].p.x+hx,r.edge[i].p.y-1+hy};
        }
        bool apart=true;
        for(int i=0;i<gh::Edges;++i)for(int j=i+1;j<gh::Edges;++j){
            const float ox=std::fmin(rect[i].x1,rect[j].x1)-std::fmax(rect[i].x0,rect[j].x0),oy=std::fmin(rect[i].y1,rect[j].y1)-std::fmax(rect[i].y0,rect[j].y0);
            apart=apart&&(ox<=1e-6f||oy<=1e-6f);
        }
        Check(apart,"the four strips never overlap (no corner glows twice as bright)");
        bool covered=true;int samples=0;
        for(float y=-half-e+1e-4f;y<half+e;y+=.0005f)for(float x=-half-e+1e-4f;x<half+e;x+=.0005f){
            if(std::fabs(x)<half-in-1e-4f&&std::fabs(y)<half-in-1e-4f)continue;   // the map's middle
            int n=0;for(const auto& k:rect)n+=x>=k.x0-1e-6f&&x<=k.x1+1e-6f&&y>=k.y0-1e-6f&&y<=k.y1+1e-6f;
            covered=covered&&n>=1;++samples;
        }
        Check(covered&&samples>1000,"together they cover the whole band round the map, corners included");
        Check(Near(rect[1].x0,-half-e,1e-5f)&&Near(rect[1].x1,half+e,1e-5f)&&Near(rect[0].y1,half-in,1e-5f)&&Near(rect[0].y0,-half+in,1e-5f),
              "the top and bottom strips run the full width; the left and right stop where they begin");
        const auto across=[](const gh::Quad& qd){return math::Rotate(qd.q,{1,0,0});};
        Check(Near(across(r.edge[0]).x,1)&&Near(across(r.edge[1]).y,1)&&Near(across(r.edge[2]).x,-1)&&Near(across(r.edge[3]).y,-1),
              "each strip's profile runs outward (its picture's inner side over the map)");
        bool lifted=true;for(const auto& s:r.edge)lifted=lifted&&Near(s.p.z,-.4f+gh::Lift,1e-6f);
        Check(lifted&&Near(r.lobe.p.z,-.4f+gh::Lift,1e-6f)&&Near(r.sign.p.z,-.4f+gh::Lift,1e-6f),"every quad a Lift in front of the map");
        const auto l=gh::Place({0,1,-.4f},q,size,-1,.01f);
        Check(Near(l.sign.p.x,-(half+.03f))&&Near(l.lobe.p.x,-(half+.005f))&&Near(std::fabs(l.lobe.q.z),1),"left-handed: both on the left, the glow out turned half a turn");
        const auto m=gh::Place({0,1,-.4f},q,size,1,.01f,.03f),ml=gh::Place({0,1,-.4f},q,size,-1,.01f,.03f);
        Check(Near(m.sign.p.x-r.sign.p.x,.03f)&&Near(ml.sign.p.x-l.sign.p.x,-.03f)&&Near(m.edge[0].p.x,r.edge[0].p.x)&&Near(m.lobe.p.x,r.lobe.p.x),
              "the slide moves only the sign, toward the pulling hand (right-handed right, left-handed left)");
    }
    {   // The side: from the physical hand that pulls (transport: UiHand(0, WristHud)), never its role. Left-handed the
        // wrist HUD's roles are mirrored, so the pulling hand's role is 0 either way and only its physical hand tells.
        namespace hr=framevr::handrole;
        hr::State right,left;left.map.left=true;
        const int rightHand=hr::For(hr::System::WristHud,right).Physical(0),leftHand=hr::For(hr::System::WristHud,left).Physical(0);
        Check(gh::SideOf(rightHand)==1&&gh::SideOf(leftHand)==-1,"right-handed: the hint on the map's right; left-handed: its left");
        Check(left.map.Role(leftHand)==0&&gh::SideOf(left.map.Role(leftHand))==1,"the pulling hand's role is 0 left-handed too: a role would give the wrong side");
        std::string here=__FILE__;here=here.substr(0,here.find_last_of("/\\")+1);
        std::ifstream src(here+"transport.cpp",std::ios::binary);std::stringstream ss;ss<<src.rdbuf();
        const std::string tr=ss.str();
        const size_t at=tr.find("floatmap::DrawHint(");
        const std::string call=at==std::string::npos?std::string():tr.substr(at,600);
        Check(call.find("grabhint::SideOf(UiHand(0,handrole::System::WristHud))")!=std::string::npos&&call.find("UiRole(")==std::string::npos,
              "transport passes DrawHint the side of the physical pulling hand (UiHand), not its role");
    }
    {   // The pictures: every cell inside the region, a Pad apart; the border's profile; one glow at the corners.
        std::vector<gh::Cell> cells;
        for(int i=0;i<gh::SignSlots;++i)cells.push_back(gh::SlotCell(i));
        for(int l=0;l<gh::Levels;++l){
            cells.push_back(gh::LobeCell(l));
            for(const auto s:{gh::SideCell(l),gh::AlongCell(l)})cells.push_back({s.x-1,s.y-1,s.w+2,s.h+2});   // with the gutter
        }
        bool inside=true,apart=true;
        for(size_t i=0;i<cells.size();++i){
            const auto& a=cells[i];
            inside=inside&&a.x>=0&&a.y>=0&&a.x+a.w<=gh::AtlasWidth&&a.y+a.h<=gh::AtlasHeight;
            for(size_t j=i+1;j<cells.size();++j){
                const auto& b=cells[j];
                apart=apart&&(a.x+a.w+gh::Pad<=b.x||b.x+b.w+gh::Pad<=a.x||a.y+a.h+gh::Pad<=b.y||b.y+b.h+gh::Pad<=a.y);
            }
        }
        Check(inside&&apart,"every cell inside the region, a Pad apart (no filtering bleed)");
        Check(framevr::panelatlas::GrabHint.w==gh::AtlasWidth&&framevr::panelatlas::GrabHint.h==gh::AtlasHeight,"the static image's region is the hint's size");
        Check(gh::SignCell(5,0).x==gh::SignCell(0,0).x&&gh::SignCell(5,0).y==gh::SignCell(0,0).y&&gh::SignCell(5,1).x!=gh::SignCell(0,1).x,
              "a fading frame shows frame 0 at half; full strength shows its own frame");
        bool own=true;
        for(int f=0;f<gh::Frames;++f)for(int l=0;l<gh::SignLevels;++l){
            const auto a=gh::SignCell(f,l),b=gh::SignCell(f,l,true);
            own=own&&(a.x!=b.x||a.y!=b.y);
        }
        Check(own&&gh::SignSlots==18,"the mirrored (left-handed) sign has its own nine pictures");
        Check(gh::AtlasHeight+framevr::panelatlas::Gap<=framevr::panelatlas::Compass.y&&gh::StripLong>=448,
              "the region still fits above the compass, the corner strips at least 448 texels long");
        Check(gh::LevelOf(0)==-1&&gh::LevelOf(1)==gh::Levels-1&&gh::LevelOf(1,gh::SignLevels)==gh::SignLevels-1&&gh::LevelOf(.01f)==-1,"levels: none at 0, the top at 1");
        Check(Near(gh::EdgeAlpha(0),1)&&gh::EdgeAlpha(-gh::InnerM)<.05f&&gh::EdgeAlpha(gh::EdgeM)<.05f&&gh::EdgeAlpha(gh::EdgeM*.5f)>.3f,
              "the border glow: brightest on the map's edge, almost nothing InnerM inside and EdgeM past");
        // The top strip's picture, along: alike in the middle, rounding the corners at its ends, and where the side
        // strips stop (InnerM short of the corner) the same as theirs, so the border runs on without a seam.
        float worst=0,seam=0,corner=0;
        for(int i=0;i<gh::StripAcross;++i){
            worst=std::fmax(worst,std::fabs(gh::StripAlpha(i,gh::StripLong/2,true)-gh::StripAlpha(i,0,false)));
            const float s=-gh::InnerM+(i+.5f)/gh::StripAcross*(gh::InnerM+gh::EdgeM);
            // the side strip's texel i, at its top end (y = half - InnerM), against the top strip's value at that point
            const float x=gh::MapSideM*.5f+s,atTop=gh::EdgeAlpha(gh::OutlineDistance(x,gh::MapSideM*.5f-gh::InnerM,gh::MapSideM));
            seam=std::fmax(seam,std::fabs(atTop-gh::StripAlpha(i,0,false)));
        }
        for(int j=0;j<gh::StripLong;++j)for(int i=0;i<gh::StripAcross;++i)corner=std::fmax(corner,gh::StripAlpha(i,j,true));
        Check(worst<1e-4f,"the top strip's middle is the side strips' profile");
        Check(seam<1e-4f,"where a side strip stops, the top strip's glow is the same: no seam at the corner");
        Check(corner<=1.f+1e-6f&&Near(gh::OutlineDistance(gh::MapSideM*.5f,gh::MapSideM*.5f,gh::MapSideM),0)&&
              gh::OutlineDistance(gh::MapSideM*.5f+.001f,gh::MapSideM*.5f+.001f,gh::MapSideM)>.0014f,
              "one glow at each corner, never above full strength; rounded outside it");
        const int endJ=0;
        Check(gh::StripAlpha(gh::StripAcross-1,endJ,true)<.05f,"the top strip's very end, past the corner: almost nothing");
    }
    {   // Painting: premultiplied (no color brighter than its alpha), sRGB-aware; without the sign art, the glows only.
        std::vector<uint8_t> anim(size_t(gh::SignTile)*gh::Frames*gh::SignTile*4);
        for(size_t i=0;i<anim.size();i+=4){anim[i]=255;anim[i+1]=255;anim[i+2]=255;anim[i+3]=128;}
        const auto t0=std::chrono::steady_clock::now();
        const auto img=gh::Paint(anim.data(),true);
        std::printf("paint: %.2f ms (sRGB, the whole region; once a session in SetupStatic)\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count());
        Check(img.size()==size_t(gh::AtlasWidth)*gh::AtlasHeight*4,"the region's size");
        bool premultiplied=true;
        for(size_t i=0;i<img.size();i+=4)for(int k=0;k<3;++k)premultiplied=premultiplied&&gh::ToLinear(img[i+k]/255.f)<=img[i+3]/255.f+.01f;
        Check(premultiplied,"every texel premultiplied: no channel above its alpha, in linear (the image is sRGB)");
        const auto full=gh::SignCell(3,1),half=gh::SignCell(0,0);
        const uint8_t* p=&img[(size_t(full.y+10)*gh::AtlasWidth+full.x+10)*4];
        Check(p[3]==128&&p[0]>160&&p[0]<200,"sRGB: half alpha white is premultiplied in linear (about 188), not 128");
        p=&img[(size_t(half.y+10)*gh::AtlasWidth+half.x+10)*4];
        Check(p[3]==64,"frame 0's half copy: half its alpha");
        {   // the mirrored frames: each row read right to left
            std::vector<uint8_t> marked(anim.size(),0);
            for(int f=0;f<gh::Frames;++f)for(int y=0;y<gh::SignTile;++y)for(int x=0;x<gh::SignTile;++x){
                uint8_t* q=&marked[(size_t(y)*gh::SignTile*gh::Frames+size_t(f)*gh::SignTile+x)*4];
                q[0]=uint8_t(x*2);q[1]=uint8_t(f*30);q[2]=uint8_t(y*2);q[3]=255;
            }
            const auto m=gh::Paint(marked.data(),false);
            bool flipped=true;
            for(int f=0;f<gh::Frames;++f)for(int y=0;y<gh::SignTile;y+=7)for(int x=0;x<gh::SignTile;x+=5){
                const auto a=gh::SignCell(f,1),b=gh::SignCell(f,1,true);
                const uint8_t* pa=&m[(size_t(a.y+y)*gh::AtlasWidth+a.x+x)*4];
                const uint8_t* pb=&m[(size_t(b.y+y)*gh::AtlasWidth+b.x+gh::SignTile-1-x)*4];
                flipped=flipped&&pa[0]==pb[0]&&pa[1]==pb[1]&&pa[2]==pb[2]&&pa[3]==pb[3]&&pa[0]==uint8_t(x*2)&&pa[1]==uint8_t(f*30);
            }
            Check(flipped,"each left-handed picture is its frame mirrored left to right");
        }
        const auto lin=gh::Paint(anim.data(),false);
        p=&lin[(size_t(full.y+10)*gh::AtlasWidth+full.x+10)*4];
        Check(p[3]==128&&p[0]==128,"linear format: half alpha white is 128");
        const auto none=gh::Paint(nullptr,false);
        const auto c=gh::SignCell(0,1),s=gh::SideCell(gh::Levels-1);
        Check(none[(size_t(c.y+gh::SignTile/2)*gh::AtlasWidth+c.x+gh::SignTile/2)*4+3]==0&&none[(size_t(s.y)*gh::AtlasWidth+s.x+gh::StripAcross/3)*4+3]>200,
              "no sign art: the sign cells stay empty, the glows are painted");
    }
    {   // What to draw and the layer budget: all six; short of that, the strips go first; then nothing.
        gh::Output o;
        auto d=gh::Plan(o,true);
        Check(d.need==0,"hidden: nothing to draw");
        o.ring=1;o.lobe=.5f;o.lobeM=.01f;o.sign=1;o.frame=3;
        d=gh::Plan(o,true);
        Check(d.need==gh::MaxQuads&&d.need==6&&d.sign.x==gh::SignCell(3,1).x&&d.sign.y==gh::SignCell(3,1).y&&
              d.side.x==gh::SideCell(gh::Levels-1).x&&d.side.y==gh::SideCell(gh::Levels-1).y&&d.along.h==gh::StripLong,
              "showing: four border strips, the glow out and the sign, the loop's frame at its strength");
        Check(gh::Plan(o,false).need==5&&gh::Plan(o,false).most==5,"no sign art: no sign quad");
        Check(gh::Plan(o,true,true).sign.x==gh::SignCell(3,1,true).x&&gh::Plan(o,true,true).sign.y==gh::SignCell(3,1,true).y,"left-handed: the mirrored picture");
        auto f=gh::Fit(d,6);
        Check(f.need==6&&f.side.w>0,"room for six: all of it");
        f=gh::Fit(d,5);
        Check(f.need==2&&f.side.w==0&&f.along.w==0&&f.lobe.w>0&&f.sign.w>0,"room for five: the sign and the glow out, without the strips");
        gh::Output still=o;still.lobe=0;still.lobeM=0;
        f=gh::Fit(gh::Plan(still,true),5);
        Check(f.need==1&&f.side.w==0&&f.sign.w>0,"room for five between slides: still no strips (they do not come and go with the glow out)");
        f=gh::Fit(d,2);
        Check(f.need==2&&f.sign.w>0&&f.lobe.w>0,"room for two: the sign and the glow out");
        f=gh::Fit(d,1);
        Check(f.need==1&&f.sign.w>0&&f.lobe.w==0,"room for one: the sign alone, never the glow out (no blinking sign)");
        Check(gh::Fit(gh::Plan(still,true),1).need==1&&gh::Fit(gh::Plan(still,true),1).sign.w>0,"room for one between slides: the same sign");
        Check(gh::Fit(d,0).need==0,"a full budget: nothing");
        gh::Output stopped=o;stopped.sign=0;   // after three pulls: the strips and the glow out, five at most
        Check(gh::Plan(stopped,true).most==5&&gh::Fit(gh::Plan(stopped,true),5).need==5&&gh::Fit(gh::Plan(stopped,true),4).need==1,
              "the sign stopped: room for five draws the strips and the glow out; less: the glow out alone");
    }
    {   // The log, quiet in play: a summary of the showings, the first at once, then at most one a minute; budget lines
        // at most one a 10 s, the rest counted.
        gh::ShowLog l;
        l.Shown(0);l.Ended(.5);Check(l.SummaryDue(.5)&&l.shows==1,"the first showing is summed up at once (proof it ran)");
        l.Summarized();
        for(int i=0;i<20;++i){l.Shown(1+i);l.Ended(1.5+i);Check(!l.SummaryDue(1.5+i),"showings within the minute: no line");}
        l.Shown(60.4);l.Ended(61);
        Check(l.SummaryDue(61)&&l.shows==21&&Near(float(l.shownS),10.6f,.01f)&&Near(float(l.longestS),.6f,.01f),"after a minute: one line for the 21 showings, their time and the longest");
        l.Summarized();Check(!l.SummaryDue(200),"no showing since: no line");
        Check(l.DropDue(5)&&!l.DropDue(9)&&l.dropsUntold==1&&l.DropDue(15.1),"budget lines at most one a 10 s, the rest counted");
        l.dropped=2;l.Shown(300);Check(l.dropped==0,"a new showing starts with nothing left out");
    }
    {   // The band is the floating map's own measure: on the wrist map 0; off its edge, degrees.
        const fm::Pose wrist{{0,1,-.4f},{0,0,0,1}};
        const math::Vec hand{0,1,0};
        auto a=fm::AimAt(hand,{0,0,0,1},wrist,.24f);
        Check(a.known&&a.miss<=0,"aimed at the wrist map: miss 0, in the band");
        const float off=std::atan2(.12f,.4f)/fm::Deg+10;   // 10 deg past its right edge
        a=fm::AimAt(hand,{0,std::sin(-off*fm::Deg*.5f),0,std::cos(-off*fm::Deg*.5f)},wrist,.24f);
        Check(a.known&&std::fabs(a.miss-10)<1.5f,"10 deg past its edge: miss about 10 (inside the 15 deg band)");
        Check(fm::PointDeg==15,"the band is the pull's: 15 deg past the edge");
    }
    {   // With the floating map's engine on the same input (as floating_map_frame.cpp steps them): the hint shows with
        // no copy out, hides on the pull, stays hidden while the copy floats, and shows again once it is put away.
        fm::Engine real;gh::Engine hint;
        fm::Input in;in.session=7;in.gameplay=in.handFree=in.aimTracked=in.headValid=true;in.wristShown=true;
        in.wristP={0,1,-.4f};in.hand[0]={0,1,0};in.handTracked[0]=true;in.aim={0,1,0};in.wristSize=.24f;
        const fm::Tuning t;
        int shownWhileOut=0,shown=0,pulls=0;bool hiddenOnPull=false;
        const auto step=[&](double time,float grip){
            in.time=time;in.grip[0]=grip;
            const auto o=real.Step(in,t);
            gh::Input x;x.time=time;x.session=in.session;x.copyOut=real.Placed();x.pulled=o.event==fm::Event::Pulled;
            x.ready=in.headValid&&in.gameplay&&in.handFree&&in.aimTracked&&in.wristShown&&!(in.grip[0]>fm::Press);
            x.band=t.aimDeg;
            if(x.ready&&!x.copyOut)x.miss=fm::AimAt(in.aim,fm::Unit(in.aimQ),fm::Pose{in.wristP,fm::Unit(in.wristQ)},in.wristSize).miss;
            const auto h=hint.Step(x);
            shown+=h.in;shownWhileOut+=h.in&&x.copyOut;pulls+=x.pulled;
            if(x.pulled)hiddenOnPull=!h.in&&h.pulledNow;
            return h;
        };
        double time=0;
        for(int i=0;i<30;++i)step(time+=1.0/90,0);
        Check(shown>0&&!real.Placed(),"aimed at the raised wrist map, no copy: shown");
        step(time+=1.0/90,1);
        Check(pulls==1&&hiddenOnPull&&real.Placed(),"Right Grip pulls a copy: the hint hides on that frame");
        for(int k=0;k<30;++k){in.aim={.02f*k,1,0};step(time+=1.0/90,1);}   // carried 0.6 m away
        for(int k=0;k<5;++k)step(time+=1.0/90,0);                             // let go there
        in.aim={0,1,0};
        for(int k=0;k<60;++k)step(time+=1.0/90,0);                            // aimed at the wrist again, the copy floating
        Check(real.Placed()&&shownWhileOut==0,"a copy floating: never shown at the wrist");
        step(time+=3,0);                                                      // a load screen in the same game
        for(int k=0;k<30;++k)step(time+=1.0/90,0);
        Check(real.Placed()&&shownWhileOut==0,"after a load screen the copy still floats (as the floating map keeps it): still no hint");
    }
    if(g_fail){std::printf("%d failed\n",g_fail);return 1;}
    std::printf("grab-hint-test: all passed\n");
    return 0;
}
