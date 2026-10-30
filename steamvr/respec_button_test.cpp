#include "respec_button.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
int main(){
    using respecbutton::Shown;using respecbutton::Face;using respecbutton::Engine;
    using Names=std::vector<std::string>;
    const Names hud{"EmoteWheelPanel","HUDPanel","MiniMenuPanel","TooltipsPanel"};
    auto with=[&](std::initializer_list<const char*> extra){Names n=hud;for(auto e:extra)n.push_back(e);return n;};

    // Only the tabbed menu's Character page (panel names from Cal's 2026-10 logs).
    Check(!Shown(hud),"no menu, no button");
    Check(Shown(with({"UISwitcher","ControllerOverlay","CharacterStatsPanel"})),"the Character page");
    for(auto page:{"PlayerInventoryExpansionLayout","SkillsTreePanel","QuestLogPanelExpansion","DropGoldModal"})
        Check(!Shown(with({"UISwitcher","ControllerOverlay",page})),"no other page of the tabbed menu");
    Check(!Shown(with({"CharacterStatsPanel"})),"the page needs the tabbed menu");
    Check(!Shown(with({"UISwitcher","CharacterStatsPanel","AdvancedStatsPanel"})),"not over Advanced Stats");
    Check(!Shown(with({"UISwitcher","CharacterStatsPanel","HoradricCubeLayout"})),"not with the cube");
    for(auto room:{"BankExpansionLayout","VendorPanelLayout","TradeLayout","WaypointsPanelExpansion","NpcDialogPanel"})
        Check(!Shown(with({"UISwitcher","CharacterStatsPanel",room})),"not with a world panel");

    // Two clicks, no timer; a page change takes the confirm back.
    {
        respecbutton::Button b;uint64_t t=1000;
        b.Step(true,Engine::Idle,t);Check(b.Showing(t)==Face::Reset,"starts as Reset Skills & Stats");
        Check(!b.Click(t,Engine::Idle)&&b.Showing(t)==Face::Confirm,"first click asks Confirm?");
        t+=600000;b.Step(true,Engine::Idle,t);Check(b.Showing(t)==Face::Confirm,"Confirm? stays with no timer");
        b.Step(false,Engine::Idle,t);b.Step(true,Engine::Idle,t);
        Check(b.Showing(t)==Face::Reset,"leaving the page takes Confirm? back");
        Check(!b.Click(t,Engine::Idle),"first click again only asks");
        Check(b.Click(t,Engine::Idle),"second click resets");
        Check(b.Showing(t)==Face::Busy,"busy while the engine works");
        Check(!b.Click(t,Engine::Busy)&&!b.Click(t,Engine::Busy),"no clicks while busy");
        b.Step(true,Engine::Busy,t+50);Check(b.Showing(t+50)==Face::Busy,"still busy");
        b.Step(true,Engine::Done,t+400);Check(b.Showing(t+400)==Face::Done,"says it worked");
        Check(!b.Click(t+500,Engine::Done),"no click during the notice");
        const uint64_t after=t+400+respecbutton::NoticeMs;
        Check(b.Showing(after)==Face::Reset,"back to Reset after the notice");
        Check(!b.Click(after,Engine::Done)&&b.Click(after,Engine::Done),"a later reset takes two clicks again");
        b.Refused();Check(b.Showing(after)==Face::Reset,"a refused request ends busy");
        b.Click(after,Engine::Done);b.Hide();Check(b.Showing(after)==Face::Reset,"VR Settings over the menu takes Confirm? back");
    }
    {
        respecbutton::Button b;uint64_t t=5;b.Click(t,Engine::Idle);b.Click(t,Engine::Idle);
        b.Step(true,Engine::NoRoom,t+10);Check(b.Showing(t+10)==Face::NoRoom,"no free square says so");
        b.Click(t,Engine::Idle);b=respecbutton::Button{};b.Click(t,Engine::Idle);b.Click(t,Engine::Idle);
        b.Step(false,Engine::Failed,t+10);Check(b.Showing(t+10)==Face::Failed,"the answer shows even after a page change");
    }
    for(int f=0;f<respecbutton::Faces;++f)Check(respecbutton::Labels[f]&&respecbutton::Labels[f][0],"every face has a label");
    Check(std::wstring(respecbutton::Labels[0])==L"Reset Skills & Stats"&&std::wstring(respecbutton::Labels[1])==L"Confirm?","the owner's wording");
    Check(respecbutton::Clickable(Face::Reset)&&respecbutton::Clickable(Face::Confirm)&&!respecbutton::Clickable(Face::Busy),"only Reset and Confirm click");

    // Where: lower right of the band, on the leather and the room panel alike.
    {
        const float w=.57f,h=w*9/16;
        const auto s=respecbutton::Place(w,h);
        Check(s.x>0&&s.y<0,"lower right of the menu");
        Check(std::fabs(s.height-statsbutton::Size*h)<1e-5f&&std::fabs(s.width-5*s.height)<1e-5f,"stats-button height, five wide");
        Check(s.x+s.width*.5f<=w*.5f&&s.y-s.height*.5f>=-h*.5f,"inside the band");
        // Centred in the page's open stone (band x 1932-2642, y 876-1290 of 3072 x 1728).
        const float bw=h*16/9,left=(s.x-s.width*.5f)/bw+.5f,right=(s.x+s.width*.5f)/bw+.5f;
        const float top=.5f-(s.y+s.height*.5f)/h,bottom=.5f-(s.y-s.height*.5f)/h;
        Check(left>1932.f/3072&&right<2642.f/3072&&top>876.f/1728&&bottom<1290.f/1728,"inside the open stone");
        Check(std::fabs((left-1932.f/3072)-(2642.f/3072-right))<.005f&&std::fabs((top-876.f/1728)-(1290.f/1728-bottom))<.005f,"centred in it");
        const auto u=.5f+(s.x)/w,v=.5f-(s.y)/h;
        Check(respecbutton::Hit(s,w,h,u,v),"its centre hits");
        Check(!respecbutton::Hit(s,w,h,.5f,.5f)&&!respecbutton::Hit({},w,h,u,v),"the menu's centre and no spot miss");
        const auto tall=respecbutton::Place(w,w);   // the room panel: the band at the top of a square canvas
        Check(std::fabs(tall.x-s.x)<1e-5f&&std::fabs(tall.width-s.width)<1e-5f,"the same size on a taller surface");
        Check(respecbutton::Place(0,h).width==0,"no surface, no spot");
    }
    // The face: a stone plaque, premultiplied, the rim gold when lit; the label stamps gold.
    {
        constexpr int W=respecbutton::TexelWidth,H=respecbutton::TexelHeight;
        auto plain=respecbutton::PaintFace(W,H,false),lit=respecbutton::PaintFace(W,H,true);
        Check(plain.size()==size_t(W)*H*4,"the picture's size");
        const auto at=[&](const std::vector<uint8_t>& p,int x,int y){return &p[(size_t(y)*W+x)*4];};
        Check(at(plain,0,0)[3]==0&&at(plain,W/2,H/2)[3]==255,"transparent corners, an opaque face");
        bool premultiplied=true;for(size_t i=0;i<plain.size();i+=4)for(int k=0;k<3;++k)premultiplied=premultiplied&&plain[i+k]<=plain[i+3];
        Check(premultiplied,"premultiplied colour");
        const auto* rim=at(plain,W/2,3);const auto* rimLit=at(lit,W/2,3);
        Check(rim[0]>rim[2]+40&&rimLit[0]>rimLit[2]+40,"a gold rim, lit or not");
        Check(rimLit[0]>rim[0],"brighter under the laser");
        const auto* stone=at(plain,W/2,H/2);
        Check(std::abs(int(stone[0])-int(stone[2]))<20,"a grey stone face");
        std::vector<uint8_t> coverage(size_t(W)*H,0);coverage[size_t(H/2)*W+W/2]=255;coverage[0]=255;
        auto stamped=plain;respecbutton::Stamp(stamped,coverage,W,H,false);
        const auto* text=at(stamped,W/2,H/2);
        Check(text[0]>200&&text[1]>200&&text[2]>200&&std::abs(int(text[0])-int(text[2]))<20,"the label is white");
        Check(at(stamped,0,0)[0]==0&&at(stamped,0,0)[3]==0,"no label off the plaque");
        Check(at(stamped,W/2+1,H/2)[0]==at(plain,W/2+1,H/2)[0],"uncovered stone unchanged");
    }
    std::puts("Respec button tests passed");
}
