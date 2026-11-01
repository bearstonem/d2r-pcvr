#include "leather_menu.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static bool Near(float a,float b,float e=1e-4f){return std::fabs(a-b)<=e;}
static bool Near(math::Vec a,math::Vec b,float e=1e-4f){return Near(a.x,b.x,e)&&Near(a.y,b.y,e)&&Near(a.z,b.z,e);}
struct Sim {
    leather::Reach g;leather::ReachInput in;leather::ReachOutput out;
    Sim(){in.time=1;in.session=7;in.epoch=1;in.active=in.tracked=in.allowed=true;in.head={0,1.7f,0};in.hand={-.23f,1.5f,.14f};}
    leather::ReachOutput Step(int n=1){while(n--){in.time+=.02;out=g.Step(in);}return out;}
};
int main(){
    using leather::OnSheet;
    using Names=std::vector<std::string>;
    // Which menus ride the leather (the panel names Cal's 2026-10 logs show).
    const Names hud{"EmoteWheelPanel","HUDPanel","HireablesPanel","MiniMenuPanel","TooltipsPanel"};
    auto with=[&](std::initializer_list<const char*> extra){Names n=hud;for(auto e:extra)n.push_back(e);return n;};
    Check(!OnSheet(hud),"the HUD alone is not a menu");
    Check(OnSheet(with({"UISwitcher"})),"the tabbed menu goes on the leather");
    for(auto page:{"PlayerInventoryExpansionLayout","SkillsTreePanel","CharacterStatsPanel","QuestLogPanelExpansion","DropGoldModal","HirelingInventory"})
        Check(OnSheet(with({"UISwitcher",page})),"every page and dialog of the tabbed menu stays on it");
    Check(OnSheet(with({"UISwitcher","HoradricCubeLayout"}))&&OnSheet(with({"HoradricCubeLayout","PlayerInventoryExpansionLayout"})),
          "a cube opened from the inventory stays on the leather");
    for(auto room:{"BankExpansionLayout","VendorPanelLayout","TradeLayout","GambleLayout","WaypointsPanelExpansion","NpcDialogPanel",
                   "ImbueItemsPanel","HireMenuPanel","ScrollSpeechPanel","ScrollOfInifuss"}) {
        Check(!OnSheet(with({room})),"world-opened panels keep the room panel");
        Check(!OnSheet(with({room,"HoradricCubeLayout","PlayerInventoryExpansionLayout"})),"a cube beside the stash stays in the room");
        Check(!OnSheet(with({"UISwitcher",room})),"a world panel over the tabbed menu takes the room panel");
    }
    Check(!OnSheet(with({"PlayerInventoryExpansionLayout"})),"the stash's inventory alone is not the tabbed menu");
    Check(OnSheet(with({"UISwitcher","AdvancedStatsPanel"}))&&OnSheet(with({"AdvancedStatsPanel","CharacterStatsPanel"})),
          "Advanced Stats (the stats button's Start) stays on the leather, even should the game close UISwitcher for it");
    Check(!OnSheet(with({"QuestLogPanelExpansion"})),"the quest log alone keeps its own handheld book");
    Check(!OnSheet(Names{"PauseLayout"})&&!OnSheet(Names{"CharacterSelectPanel","MainMenuPanel"}),"pause and title menus are not on it");

    // Right B: the mod closes the menu only when the game left the press unanswered,
    // and only with nothing but the tabbed menu and its pages open.
    {
        using leather::BackCloses;
        Check(BackCloses(with({"UISwitcher","ControllerOverlay","PlayerInventoryExpansionLayout"})),"the inventory page: B may close the menu");
        for(auto page:{"CharacterStatsPanel","AdvancedStatsPanel","SkillsTreePanel","QuestLogPanelExpansion"})
            Check(BackCloses(with({"UISwitcher","ControllerOverlay",page})),"every page of the tabbed menu");
        Check(!BackCloses(hud)&&!BackCloses(with({"PlayerInventoryExpansionLayout"})),"no tabbed menu, nothing to close");
        for(auto own:{"HoradricCubeLayout","DropGoldModal","GoldTransferPanel","QuantityPanel","ConfirmationPanel","PauseLayout","SettingsPanel","SkillSelect","SomeNewPopup"})
            Check(!BackCloses(with({"UISwitcher","ControllerOverlay","PlayerInventoryExpansionLayout",own})),"a cube, dialog or options page keeps its own Back");
        leather::Back b;uint32_t changes=4;uint64_t t=1000;
        auto step=[&](bool pressed,bool dragging=false){return b.Step(true,pressed,dragging,changes,t);};
        Check(!step(true)&&b.Waiting(),"a press: the game goes first");
        t+=leather::BackWaitMs-1;Check(!step(true),"held, still waiting for the game");
        t+=1;Check(step(false)&&!b.Waiting(),"unanswered: the mod closes the menu, once");
        t+=1000;Check(!step(false),"nothing more");
        Check(!step(true),"a new press");++changes;t+=10;Check(!step(true)&&!b.Waiting(),"the game closed a panel: it answered B");
        t+=1000;Check(!step(true),"a held press never repeats");
        Check(!step(false)&&!step(true,true)&&!b.Waiting(),"no press during a laser drag");
        Check(!step(false)&&!step(true)&&b.Waiting(),"pressed again");
        t+=10;Check(!step(false,true)&&!b.Waiting(),"a drag after the press cancels it");
        Check(!step(false)&&!step(true)&&b.Waiting(),"and again");
        Check(!b.Step(false,false,false,changes,t)&&!b.Waiting(),"the menu closing resets it");
        t+=1000;Check(!step(false)&&!b.Waiting(),"nothing left over when it opens again");
    }
    {   // Premultiplied for the headset: the transparent surround adds nothing.
        std::vector<uint8_t> px{88,66,46,0, 200,100,50,255, 200,100,50,128, 255,255,255,1};
        leather::Premultiply(px);
        Check(px[0]==0&&px[1]==0&&px[2]==0&&px[3]==0,"alpha 0: black, whatever colour the cache kept");
        Check(px[4]==200&&px[5]==100&&px[6]==50&&px[7]==255,"solid hide unchanged");
        Check(px[8]==100&&px[9]==50&&px[10]==25&&px[11]==128,"half alpha: half colour, rounded");
        Check(px[12]==1&&px[13]==1&&px[14]==1&&px[15]==1,"colour never exceeds its alpha");
    }

    // The art: the stand-in's menu inside a Border; the game's hide from the cache.
    const auto stand=leather::PlaceholderArt();
    Check(stand.Valid()&&stand.width==leather::PlaceholderWidth&&stand.height==leather::PlaceholderHeight&&
          Near(stand.menuX,leather::PlaceholderWidth*leather::Border)&&Near(stand.menuY*2+stand.menuW*9/16,leather::PlaceholderHeight,.01f),
          "stand-in: the menu centred inside its border");
    const leather::Art hide{914,700,183.3f,219.f,624.7f};   // what prepare_leather.py makes of the Rogue hide
    Check(hide.Valid(),"the cached hide's layout is valid");
    for(const auto& bad:{leather::Art{0,700,1,1,100},leather::Art{1025,700,1,1,100},leather::Art{914,801,1,1,100},leather::Art{914,700,-1,1,100},
                         leather::Art{914,700,400,1,600},leather::Art{914,700,1,500,600},leather::Art{914,700,1,1,NAN},leather::Art{914,700,1,1,4}})
        Check(!bad.Valid(),"art that does not fit its picture or the atlas is refused");
    for(float mw:{.30f,.57f,.90f}) {
        const auto s=leather::Measure(hide,mw);
        Check(Near(s.menuWidth,mw)&&Near(s.menuHeight,mw*9/16),"the menu keeps its size");
        Check(Near(s.width,mw*914/624.7f,1e-3f)&&Near(s.width/s.height,914.f/700,1e-3f),"the hide grows round it, its own proportions");
        Check(Near(s.menuCentre.x,(183.3f+624.7f/2-457)*mw/624.7f,1e-4f)&&Near(s.menuCentre.y,(350-219-624.7f*9/32)*mw/624.7f,1e-4f),
              "menu centre from the hide's, y up");
    }
    {
        const auto s=leather::Measure(stand,.57f);
        Check(Near(s.menuCentre.x,0)&&Near(s.menuCentre.y,0,1e-4f)&&Near(s.width,.57f/(1-2*leather::Border),1e-4f),"stand-in centred, border round it");
        Check(Near(leather::Measure(leather::Art{},.57f).width,s.width),"no valid art: the stand-in's layout");
    }
    Check(Near(leather::Measure(hide,5).menuWidth,leather::MaxMenuWidth)&&Near(leather::Measure(hide,.1f).menuWidth,leather::MinMenuWidth)&&
          Near(leather::Measure(hide,NAN).menuWidth,leather::DefaultMenuWidth),"menu width clamped, nonfinite = default");
    Check(Near(leather::NormalizeTilt(200),leather::MaxTilt)&&Near(leather::NormalizeTilt(INFINITY),leather::DefaultTilt),"tilt clamped");

    // The cache file.
    {
        auto file=[](uint32_t format,uint32_t w,uint32_t h,float x,float y,float mw,size_t pixels) {
            std::vector<uint8_t> f{'P','V','L','H'};uint32_t head[4]{format,w,h,0};float menu[3]{x,y,mw};
            f.insert(f.end(),reinterpret_cast<uint8_t*>(head),reinterpret_cast<uint8_t*>(head)+16);
            f.insert(f.end(),reinterpret_cast<uint8_t*>(menu),reinterpret_cast<uint8_t*>(menu)+12);
            for(size_t i=0;i<pixels;++i)f.push_back(uint8_t(i));
            return f;
        };
        leather::Art art;std::vector<uint8_t> rgba;
        auto good=file(1,32,20,2,2,28,32*20*4);
        Check(leather::ParseCache(good.data(),good.size(),art,rgba)&&art.width==32&&art.height==20&&Near(art.menuW,28)&&
              rgba.size()==32*20*4&&rgba[5]==5,"a good cache reads its size, menu and pixels");
        auto shortFile=file(1,32,20,2,2,28,32*20*4-1);
        auto badMagic=good;badMagic[0]='X';
        auto newFormat=file(2,32,20,2,2,28,32*20*4);
        auto tooBig=file(1,2000,20,2,2,28,0);
        auto offPicture=file(1,32,20,10,2,28,32*20*4);
        for(auto* f:{&shortFile,&badMagic,&newFormat,&tooBig,&offPicture})
            Check(!leather::ParseCache(f->data(),f->size(),art,rgba),"a short, foreign, newer, oversized or misplaced cache is refused");
        Check(!leather::ParseCache(nullptr,0,art,rgba)&&art.width==32,"nothing read keeps the last art");
    }

    // The band of the canvas: the top of a tall one (Cal's 3072x3264), the middle of a wide one.
    {
        auto b=leather::Band(3072,3264);Check(b.x==0&&b.y==0&&b.w==3072&&b.h==1728,"tall canvas: its upper 16:9 band");
        b=leather::Band(1920,1080);Check(b.x==0&&b.y==0&&b.w==1920&&b.h==1080,"16:9 canvas: all of it");
        b=leather::Band(2560,1080);Check(b.w==1920&&b.h==1080&&b.x==320&&b.y==0,"wide canvas: centred 16:9");
        b=leather::Band(3072,3072);Check(b.x==0&&b.y==0&&b.w==3072&&b.h==1728,"the cube from the inventory: the same upper band (2026-10-29 capture)");
        b=leather::Band(0,100);Check(b.w==0&&b.h==0,"no canvas, no band");
    }

    // The strap: the fist at the middle of the menu's left edge, the face as the quest log's.
    for(const auto& art:{hide,stand})for(float yaw:{-2.f,0.f,1.f})for(float tilt:{0.f,30.f}) {
        const auto grip=math::Multiply(math::Yaw(yaw),leather::AboutX(.6f));
        const math::Vec hand{.3f,1.1f,-.4f};
        const auto s=leather::Measure(art,.57f);
        const auto strapped=leather::Strap(hand,grip,art,.57f,tilt);
        const auto& sheet=strapped.sheet;const auto& menu=strapped.menu;
        const auto right=math::Rotate(sheet.q,{1,0,0}),face=math::Rotate(sheet.q,{0,0,1});
        const auto edge=menu.p-right*(s.menuWidth*.5f)-face*(leather::Raise+leather::Lift);
        Check(Near(edge,hand+right*leather::Gap,1e-5f),"the fist holds the leather at the menu's left edge, Gap short of it");
        Check(Near(menu.p,sheet.p+math::Rotate(sheet.q,s.menuCentre)+face*leather::Lift,1e-5f),"the menu sits on its spot of the leather, Lift in front");
        Check(Near(right,math::Rotate(grip,{1,0,0})),"the menu runs out to the right of the left fist");
        const auto quest=math::Rotate(grip,{0,1,0});
        const float lean=std::acos(std::clamp(math::Dot(face,quest),-1.f,1.f))*180/3.14159265f;
        Check(Near(lean,tilt,.05f),"tilt leans the face back from the quest log's");
        Check(Near(menu.q.w,sheet.q.w)&&Near(menu.q.x,sheet.q.x),"menu and leather face the same way");
    }
    // The HUD held with the menu (owner 2026-10-29): life / mana above the menu, the map
    // on the band's left edge above the fist.
    {
        const float w=.57f,h=w*9/16;const auto hud=leather::HudPlaces(w);
        Check(Near(hud.vitals.x,0)&&Near(hud.vitals.y-hud.vitalsHeight*.5f,h*.5f+leather::HudGap),"life / mana centred just above the menu");
        Check(Near(hud.vitalsWidth,.14f)&&Near(hud.vitalsWidth,2*hud.vitalsHeight),"slightly larger (14 cm, was 10), 2:1 as the wrist's picture");
        Check(Near(hud.map.x,-w*.5f)&&hud.map.x+hud.mapSize*.5f<-w*.5f+leather::MenuArtInset*w,"the map on the band's left edge, clear of the art");
        Check(Near(hud.map.y+hud.mapSize*.5f,h*.5f+leather::HudAbove)&&hud.map.y-hud.mapSize*.5f>.04f,"its top a little above the band, above the fist");
        Check(hud.vitals.x-hud.vitalsWidth*.5f>hud.map.x+hud.mapSize*.5f,"the two never overlap");
        Check(hud.map.z>0&&Near(hud.map.z,hud.vitals.z),"in front of the menu");
        Check(Near(leather::HudPlaces(NAN).map.x,leather::HudPlaces(leather::DefaultMenuWidth).map.x),"a bad width is the default's");
    }
    // Menu only (owner 2026-10-29): no hide, and the fist holds the menu art's own left edge.
    {
        using leather::Style;using leather::NormalizeStyle;
        Check(NormalizeStyle(0)==Style::Off&&NormalizeStyle(1)==Style::Hide&&NormalizeStyle(2)==Style::MenuOnly,"Off, On, Menu only");
        Check(NormalizeStyle(.4f)==Style::Hide&&NormalizeStyle(3)==Style::Hide&&NormalizeStyle(-1)==Style::Hide&&NormalizeStyle(NAN)==Style::Hide,
              "an odd value reads On");
        const auto grip=math::Multiply(math::Yaw(.7f),leather::AboutX(.6f));const math::Vec hand{.3f,1.1f,-.4f};
        const float width=.57f,inset=leather::MenuArtInset*width;
        const auto only=leather::Strap(hand,grip,hide,width,0,inset),with=leather::Strap(hand,grip,hide,width,0);
        const auto right=math::Rotate(only.menu.q,{1,0,0}),face=math::Rotate(only.menu.q,{0,0,1});
        const auto art=only.menu.p-right*(width*.5f-inset)-face*(leather::Raise+leather::Lift);
        Check(Near(art,hand+right*leather::Gap,1e-5f),"menu only: the art's left edge (12.5% in) in the fist, Gap short of it");
        Check(Near(only.menu.p,with.menu.p-right*inset,1e-5f),"menu only: the menu slid toward the hand by the see-through margin");
        Check(Near(leather::MenuArtInset,.125f),"the art starts 384 of 3072 in");
        Check(Near(leather::Strap(hand,grip,hide,width,0,-1).menu.p,with.menu.p,1e-5f)&&
              Near(leather::Strap(hand,grip,hide,width,0,NAN).menu.p,with.menu.p,1e-5f),"a bad inset is none");
        leather::Holder h;h.Step(hand,grip,true,hide,width,0,inset);Check(Near(h.Get().menu.p,only.menu.p,1e-5f),"the holder passes the inset");
    }
    {
        leather::Holder h;Check(!h.Placed(),"nothing to show before a tracked hand");
        h.Step({0,1,0},{0,0,0,1},false,hide,.57f,0);Check(!h.Placed(),"untracked first frame is not latched");
        h.Step({0,1,0},{0,0,0,2},true,hide,.57f,0);
        Check(h.Placed()&&Near(h.Get().sheet.p,leather::Strap({0,1,0},{0,0,0,1},hide,.57f,0).sheet.p),"follows the hand, grip normalized");
        const auto held=h.Get();h.Step({5,5,5},{0,0,0,1},false,hide,.57f,0);
        Check(Near(h.Get().sheet.p,held.sheet.p)&&Near(h.Get().menu.p,held.menu.p),"a lost hand leaves the leather where it was");
        h.Step({NAN,1,0},{0,0,0,1},true,hide,.57f,0);Check(Near(h.Get().menu.p,held.menu.p),"a nonfinite pose is ignored");
        h.Open();Check(!h.Placed(),"a new menu waits for the hand again");
    }

    // Opening it: left hand over the left shoulder, a fresh grip.
    for(float yaw:{-2.f,0.f,2.f}) {
        Sim s;s.in.heading=math::Yaw(yaw);s.in.head={2,1.2f,-4};
        s.in.hand=s.in.head+math::Rotate(s.in.heading,{-.23f,-.2f,.14f});
        s.Step(10);Check(!s.out.block&&!s.out.open,"over the shoulder without grip leaves combat free");
        s.in.grip=1;Check(s.Step().open&&s.out.block,"grip over the left shoulder opens the menu at any yaw");
        for(int i=0;i<15;++i)Check(!s.Step().open&&s.out.block,"held grip never opens twice");
        s.in.grip=0;Check(!s.Step().block,"release frees the hand");
    }
    for(math::Vec p:{math::Vec{.23f,1.5f,.14f},math::Vec{-.23f,1.5f,-.14f},math::Vec{-.23f,.9f,.14f},math::Vec{0,1.5f,.14f},math::Vec{-.9f,1.5f,.14f}}) {
        Sim s;s.in.hand=p;s.Step(10);s.in.grip=1;
        Check(!s.Step().open&&!s.out.block,"right shoulder, front, hip, behind the head and far out are not the leather");
    }
    {   // A dwell is needed: the first frame in reach after arriving is not enough.
        Sim t;t.in.hand.z=-.2f;t.Step(10);t.in.hand.z=.14f;t.in.grip=1;Check(!t.Step().open,"no open on the frame the hand arrives");}
    {Sim s;s.in.grip=1;s.Step(10);Check(!s.out.open,"grip held when it becomes possible cannot open");
     s.in.grip=0;s.Step();s.in.grip=1;Check(s.Step().open,"a fresh press after release opens");}
    {Sim s;s.in.hand.z=-.2f;s.Step(10);s.in.grip=1;s.Step();s.in.hand.z=.14f;
     for(int i=0;i<15;++i)Check(!s.Step().open,"arriving with grip held cannot open");}
    {Sim s;s.Step(10);s.in.allowed=false;s.in.grip=1;Check(!s.Step().open,"wheel, bottle, holster or pending action prevent it");
     s.in.allowed=true;Check(!s.Step().open,"and cannot replay the held press after");}
    {Sim s;s.Step(10);s.in.tracked=false;s.in.grip=1;s.Step();s.in.tracked=true;
     for(int i=0;i<12;++i)Check(!s.Step().open,"tracking return with grip held cannot open");}
    for(int change=0;change<2;++change){Sim s;s.Step(10);s.in.grip=1;if(change)++s.in.session;else ++s.in.epoch;
        Check(!s.Step().open,"recenter or session change consumes the press");}
    {Sim s;s.Step(10);s.in.time+=1;s.in.grip=1;Check(!s.Step().open,"a frame hitch cannot replay it");}

    // The stand-in hide: transparent beyond its edge, solid under the menu band.
    {
        const int w=leather::PlaceholderWidth,h=leather::PlaceholderHeight;const auto px=leather::PaintPlaceholder(w,h);
        Check(px.size()==size_t(w)*h*4,"RGBA of the requested size");
        auto at=[&](int x,int y){return &px[(size_t(y)*w+x)*4];};
        Check(at(0,0)[3]==0&&at(w-1,h-1)[3]==0&&at(w-1,0)[3]==0&&at(0,h-1)[3]==0,"corners are outside the hide");
        bool solid=true;
        for(int y=int(stand.menuY);y<int(stand.menuY+stand.menuW*9/16);y+=7)for(int x=int(stand.menuX);x<int(stand.menuX+stand.menuW);x+=7)
            solid=solid&&at(x,y)[3]==255;
        Check(solid,"the whole menu band is on leather");
        const auto* c=at(w/2,h/2);Check(c[0]>c[1]&&c[1]>c[2]&&c[0]>40,"brown");
        Check(leather::PaintPlaceholder(0,5).empty(),"no size, no pixels");
    }
    std::puts("Leather menu: tabbed menu and cube on the sheet, world panels in the room, Right B fallback (unanswered presses, pages only, drags), premultiplied hide, 16:9 band, stand-in and cached hide layouts, cache file, strap at the menu edge and tilt, held/lost hand, left-shoulder grip with fresh-press/dwell/tracking guards, stand-in hide passed");
}
