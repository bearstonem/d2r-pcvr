// VR Settings redesign: PointerFilter and CardFocus (vr_settings_pointer.h)
// driven by synthetic laser traces: hand jitter, the trigger-pull dip, holds,
// drags, fast sweeps and trips to the help card, on a made-up clock.
#include "vr_settings_pointer.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace framevr::settings;
namespace L=framevr::settings::layout;
namespace P=framevr::settings::pointer;

static int checks=0;
static void Check(bool ok,const std::string& what){
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",what.c_str());std::exit(1);}
}
// A small deterministic jitter (+-amp px), the same on every run.
static unsigned seed=12345;
static float Jitter(float amp){seed=seed*1103515245u+12345u;return (float((seed>>8)&0xFFFF)/65535.f*2-1)*amp;}

// Two buttons 12 px apart (MinGap), a stepper's - and +, a slider value, a
// locked toggle, a label and the help card with two buttons inside it.
static const Hit A{Kind::Apply},B{Kind::Close},Minus{Kind::Change,TurnSpeed,-1},Plus{Kind::Change,TurnSpeed,1},
    Value{Kind::Change,TurnSpeed,0},LockedToggle{Kind::Set,SmoothTurn,0,1},CardField{Kind::Card},Undo{Kind::Undo,TurnSpeed},Default{Kind::Default,TurnSpeed};
static L::Element Make(Rect r,L::Role role,Hit hit,uint16_t flags,int help=NoHelp){
    L::Element e;e.rect=r;e.role=role;e.hit=hit;e.flags=flags;e.help=help;return e;
}
static L::Layout Panel(){
    L::Layout l;
    l.elements.push_back(Make({400,100,300,64},L::Role::Button,A,L::Enabled,1));          // A: 400-700 x 100-164
    l.elements.push_back(Make({400,176,300,64},L::Role::Button,B,L::Enabled,2));          // B: 176-240 (12 px below A)
    l.elements.push_back(Make({400,300,200,64},L::Role::Label,{},0,3));                   // a label: no hit
    l.elements.push_back(Make({700,300,64,64},L::Role::StepMinus,Minus,L::Enabled|L::Repeat,3));
    l.elements.push_back(Make({782,300,200,64},L::Role::Value,Value,L::Enabled|L::Drag,3));
    l.elements.push_back(Make({1000,300,64,64},L::Role::StepPlus,Plus,L::Enabled|L::Repeat,3));
    l.elements.push_back(Make({700,400,150,64},L::Role::Toggle,LockedToggle,L::Locked,4)); // locked: never Enabled
    l.card={328,600,920,300};
    l.elements.push_back(Make(l.card,L::Role::HelpCard,CardField,L::Enabled));
    l.elements.push_back(Make({400,800,300,64},L::Role::CardButton,Undo,L::Enabled,3));
    l.elements.push_back(Make({712,800,300,64},L::Role::CardButton,Default,L::Enabled,3));
    return l;
}

// Drives one filter at 90 Hz (11 ms frames) and counts what comes out.
struct Driver {
    const L::Layout& layout;P::PointerFilter f;uint64_t ms=1000;
    int clicks=0,blocked=0,deltaSum=0;Hit lastClick{};P::PointerFilter::Output last{};
    explicit Driver(const L::Layout& l):layout(l){f.Step(layout,ms,true,0,0,false);}   // a released trigger first
    void Frame(float x,float y,bool down,bool onPanel=true){
        ms+=11;last=f.Step(layout,ms,onPanel,x,y,down);
        if(last.click.kind!=Kind::None){++clicks;lastClick=last.click;deltaSum+=last.click.delta;}
        if(last.blocked)++blocked;
    }
    void Hold(float x,float y,bool down,int frames,float jitter=0){for(int i=0;i<frames;++i)Frame(x+Jitter(jitter),y+Jitter(jitter),down);}
};

static void PointerTests(){
    const auto layout=Panel();
    {   // A jittery press on A clicks once, on release, never on the press.
        Driver d(layout);d.Hold(550,132,false,20,4);
        Check(d.last.hover==A,"jittery laser hovers A");
        d.Frame(550,132,true);Check(d.clicks==0&&d.last.pressed==A,"trigger-down presses A without clicking");
        d.Hold(550,132,true,10,4);Check(d.clicks==0,"holding a button never clicks");
        d.Frame(551,131,false);Check(d.clicks==1&&d.lastClick==A,"release clicks A once");
        d.Hold(550,132,false,10,4);Check(d.clicks==1,"no extra click after release");
    }
    {   // Hysteresis: hover holds 10 px outside A, lets go beyond, and moves at once onto B.
        Driver d(layout);d.Hold(550,150,false,5);
        d.Frame(550,172,false);Check(d.last.hover==A,"8 px below A still hovers A (hysteresis)");
        d.Frame(550,175,false);Check(d.last.hover==Hit{},"11 px below A, in the gap: no hover");
        d.Hold(550,150,false,3);d.Frame(550,180,false);Check(d.last.hover==B,"the laser on B hovers B at once");
        d.Frame(550,170,false);Check(d.last.hover==B,"6 px above B still hovers B");
    }
    {   // The trigger-pull dip: on A, the hand dips onto B's top edge 33 ms before
        // trigger-down and releases there: the press and click go to A.
        Driver d(layout);d.Hold(550,140,false,20,3);
        d.Hold(550,182,false,3);   // dipped onto B
        Check(d.last.hover==B,"the dip reaches B");
        d.Frame(550,183,true);Check(d.last.pressed==A,"pre-trigger lock: the press goes to A, under the laser 90 ms before");
        d.Hold(550,184,true,5);d.Frame(550,184,false);
        Check(d.clicks==1&&d.lastClick==A,"release on B's edge (within 24 px of A) clicks A");
    }
    {   // A deliberate move: on B for 150 ms before pressing presses B.
        Driver d(layout);d.Hold(550,140,false,10);d.Hold(550,208,false,14,3);
        d.Frame(550,208,true);Check(d.last.pressed==B,"a laser settled on B for 150 ms presses B");
        d.Frame(550,208,false);Check(d.lastClick==B,"and clicks B");
    }
    {   // Release slop: 20 px off still clicks, 30 px off does not.
        Driver d(layout);d.Hold(550,132,false,12);d.Frame(550,132,true);d.Hold(720,132,true,3);d.Frame(719,132,false);
        Check(d.clicks==1,"release 19 px right of A clicks");
        Driver e(layout);e.Hold(550,132,false,12);e.Frame(550,132,true);e.Hold(731,132,true,3);
        Check(e.last.pressed==Hit{},"31 px off A drops the pressed look");
        e.Frame(731,132,false);Check(e.clicks==0,"release 31 px right of A does not click");
        Driver g(layout);g.Hold(550,132,false,12);g.Frame(550,132,true);g.Hold(731,132,true,3);g.Hold(550,132,true,3);g.Frame(550,132,false);
        Check(g.clicks==1,"coming back onto A before release still clicks");
    }
    {   // Off the panel (or tracking lost) cancels the press.
        Driver d(layout);d.Hold(550,132,false,12);d.Frame(550,132,true);d.Frame(550,132,true,false);d.Hold(550,132,true,3);d.Frame(550,132,false);
        Check(d.clicks==0,"leaving the panel cancels the press");
    }
    {   // A trigger held at the start (or after Reset) never presses until released.
        Driver d(layout);d.f.Reset();d.Hold(550,132,true,10);d.Frame(550,132,false);
        Check(d.clicks==0&&d.blocked==0,"a trigger held through Reset does not click");
        d.Hold(550,132,false,12);d.Frame(550,132,true);d.Frame(550,132,false);Check(d.clicks==1,"the next real press clicks");
    }
    {   // Labels and slabs are never targets; a locked control blocks.
        Driver d(layout);d.Hold(450,332,false,12);Check(d.last.hover==Hit{},"a label is not a target");
        d.Frame(450,332,true);d.Frame(450,332,false);Check(d.clicks==0&&d.blocked==0,"a press on a label does nothing");
        d.Hold(770,432,false,12);Check(d.last.hover==LockedToggle,"a locked control still hovers (for its card)");
        d.Frame(770,432,true);Check(d.blocked==1&&d.last.pressed==Hit{},"a press on a locked control is blocked");
        d.Frame(770,432,false);Check(d.clicks==0,"and never clicks");
    }
    {   // Hold-repeat: a short press steps once on release; a 1 s hold steps 5 times, then nothing on release.
        Driver d(layout);d.Hold(1032,332,false,12,3);d.Frame(1032,332,true);d.Hold(1032,332,true,20,3);
        Check(d.clicks==0,"a stepper held 220 ms has not repeated yet");
        d.Frame(1032,332,false);Check(d.clicks==1&&d.lastClick==Plus,"a short stepper press steps once on release");
        Driver h(layout);h.Hold(1032,332,false,12,3);h.Frame(1032,332,true);
        const uint64_t start=h.ms;while(h.ms<start+1000)h.Frame(1032+Jitter(3),332+Jitter(3),true);
        Check(h.clicks==5,"a 1 s hold: 400 ms, then 8 a second (5 steps), got "+std::to_string(h.clicks));
        h.Frame(1032,332,false);Check(h.clicks==5,"release after repeats adds no step");
        Driver o(layout);o.Hold(1032,332,false,12);o.Frame(1032,332,true);o.Hold(1032,332,true,30);
        const int before=o.clicks;o.Hold(1032,250,true,30);Check(o.clicks==before,"repeat pauses while the laser is off the button");
    }
    {   // Drag: a press on the value does nothing; 16 px starts; 1 step per 24 px from the press point.
        Driver d(layout);d.Hold(880,332,false,12,2);d.Frame(880,332,true);d.Hold(880,332,true,5,2);d.Frame(880,332,false);
        Check(d.clicks==0,"a press on a slider's value does nothing");
        Driver g(layout);g.Hold(880,332,false,12);g.Frame(880,332,true);
        g.Hold(890,332,true,3);Check(g.clicks==0,"a 10 px drag does nothing");
        for(int i=1;i<=6;++i)g.Frame(890+float(i)*5,333,true);   // to 920 (40 px)
        for(int i=1;i<=4;++i)g.Frame(920+float(i)*5,333,true);   // to 940 (60 px)
        Check(g.deltaSum==2&&g.lastClick.kind==Kind::Change&&g.lastClick.index==TurnSpeed,"a 60 px drag moves 2 steps");
        g.Hold(1100,333,true,3);Check(g.deltaSum==9,"the drag keeps scrubbing off the value (220 px: 9 steps)");
        g.Hold(856,333,true,3);Check(g.deltaSum==-1,"dragging back left of the press steps down");
        const int n=g.clicks;g.Frame(856,333,false);Check(g.clicks==n,"releasing a drag adds no step");
    }
    {   // Drag tremor: +-4 px around a unit boundary never flickers; turning back takes half a unit past it.
        Driver g(layout);g.Hold(880,332,false,12);g.Frame(880,332,true);
        for(int i=1;i<=12;++i)g.Frame(880+float(i)*4,333,true);   // to 928: 2 units (24 px each)
        Check(g.deltaSum==2,"48 px: 2 steps");
        const int n=g.clicks;
        for(int i=0;i<40;++i)g.Frame(928+(i%2?4.f:-4.f),333,true);   // shaking across the 928 boundary
        Check(g.clicks==n&&g.deltaSum==2,"+-4 px of tremor at a boundary sends nothing");
        g.Hold(918,333,true,2);Check(g.deltaSum==2,"10 px back (under half a unit past the boundary) holds");
        g.Hold(915,333,true,2);Check(g.deltaSum==1,"13 px back (past half a unit) steps down once");
    }
    {   // A long range drags 5 steps a unit, landing on multiples of 5; - / + keep 1. Ends clamp at once.
        auto l=layout;
        for(auto& e:l.elements)if(e.hit==Value){e.dragPx=10;e.dragCoarse=5;e.dragIndex=3;e.dragSteps=100;}
        Driver g(l);g.Hold(880,332,false,12);g.Frame(880,332,true);
        g.Hold(889,333,true,2);Check(g.deltaSum==0,"9 px (under the 16 px start): nothing");
        g.Hold(896,333,true,2);Check(g.deltaSum==2,"one unit up from step 3 lands on 5");
        g.Hold(911,333,true,2);Check(g.deltaSum==12,"three units: 15");
        g.Hold(860,333,true,2);Check(g.deltaSum==-3,"two units down from the press: the minimum, 0");
        g.Hold(700,333,true,2);Check(g.deltaSum==-3,"far past the minimum: clamped, nothing more");
        g.Hold(706,333,true,2);Check(g.deltaSum==-3,"turning back 6 px from there: under a unit, nothing");
        g.Hold(711,333,true,2);Check(g.deltaSum==2,"a unit back from the end moves at once: 0 -> 5");
        auto s=layout;for(auto& e:s.elements)if(e.hit==Value){e.dragPx=8;e.dragCoarse=1;e.dragIndex=48;e.dragSteps=50;}
        Driver f(s);f.Hold(880,332,false,12);f.Frame(880,332,true);f.Hold(1200,333,true,2);
        Check(f.deltaSum==2,"a short range stops at its end (48 -> 50)");
        f.Hold(1192,333,true,2);Check(f.deltaSum==1,"and comes back one step a unit from there");
    }
    {   // The card's field is a container: hover stays on a card button near its edge.
        Driver d(layout);d.Hold(550,832,false,12);Check(d.last.hover==Undo,"a card button hovers");
        d.Frame(550,870,false);Check(d.last.hover==Undo,"6 px below it, on the card's field: still the button");
        d.Frame(550,880,false);Check(d.last.hover==CardField,"16 px below: the card's field");
        d.Hold(550,832,false,3);d.Frame(712,832,false);Check(d.last.hover==Default,"the next card button takes over at once");
    }
}

// Drives CardFocus with HelpAt over the same panel.
struct Card {
    const L::Layout& layout;P::CardFocus c;uint64_t ms=1000;int shown=NoHelp;
    explicit Card(const L::Layout& l):layout(l){}
    void Frame(float x,float y,bool onPanel=true){ms+=11;shown=c.Step(ms,onPanel?L::HelpAt(layout,x,y):NoHelp,x,y,onPanel,layout.card);}
    void Hold(float x,float y,int frames,float jitter=0){for(int i=0;i<frames;++i)Frame(x+Jitter(jitter),y+Jitter(jitter));}
    // Moves at speed px/s from (x0, y0) to (x1, y1).
    void Sweep(float x0,float y0,float x1,float y1,float speed){
        const float d=std::hypot(x1-x0,y1-y0);const int frames=std::max(1,int(d/speed*1000/11));
        for(int i=1;i<=frames;++i)Frame(x0+(x1-x0)*float(i)/float(frames),y0+(y1-y0)*float(i)/float(frames));
    }
};
static void CardTests(){
    // Rows down the content: a tall column of help subjects 84 px apart.
    L::Layout l;
    for(int i=0;i<6;++i)l.elements.push_back(Make({328,90+i*84,920,84},L::Role::Row,{},0,10+i));
    l.card={328,600,920,300};
    l.elements.push_back(Make(l.card,L::Role::HelpCard,{Kind::Card},L::Enabled));
    l.elements.push_back(Make({400,800,300,64},L::Role::CardButton,{Kind::Undo,10},L::Enabled,99));
    {   // Dwell with hand jitter: 250 ms switches, 220 ms does not.
        Card c(l);c.Hold(600,132,20,3);Check(c.shown==NoHelp,"220 ms on a row: the card has not switched");
        c.Hold(600,132,4,3);Check(c.shown==10,"260 ms on a row with +-3 px jitter: the card shows it");
        Check(!c.c.ButtonsArmed(c.ms),"card buttons wait after the switch");
        c.Hold(600,132,30,3);Check(c.c.ButtonsArmed(c.ms),"and take presses 300 ms later");
    }
    {   // A fast sweep (1000 px/s) across five rows never switches the card.
        Card c(l);c.Hold(600,132,30);Check(c.shown==10,"settled on the first row");
        c.Sweep(600,132,600,560,1000);Check(c.shown==10,"a 1000 px/s sweep over the rows keeps the card");
        c.Sweep(600,560,600,132,1000);Check(c.shown==10,"and back");
        c.Sweep(600,132,600,470,300);c.Hold(600,470,25,2);Check(c.shown==14,"a slow move then a 275 ms stop switches it");
    }
    {   // Travel to the card: across rows to the card, inside it, and out.
        Card c(l);c.Hold(600,132,30);
        c.Sweep(600,132,600,700,1500);Check(c.shown==10,"the trip to the card keeps the subject");
        c.Hold(550,832,40,3);Check(c.shown==10,"inside the card it never switches (pointing at its button)");
        c.Hold(600,700,40,3);Check(c.shown==10,"nor while resting on its field");
        c.Frame(600,560);c.Hold(600,560,25,2);Check(c.shown==10,"300 ms frozen after leaving the card");
        c.Hold(600,560,30,2);Check(c.shown==15,"then the row below switches it after its dwell");
    }
    {   // Nothing pointed, off the panel, Focus and Reset.
        Card c(l);c.Hold(600,132,30);c.Hold(1270,20,40);Check(c.shown==10,"nothing pointed: the card keeps its subject");
        for(int i=0;i<40;++i)c.Frame(0,0,false);Check(c.shown==10,"off the panel: sticky too");
        c.c.Focus(13,c.ms);Check(c.c.Shown()==13&&!c.c.ButtonsArmed(c.ms+299)&&c.c.ButtonsArmed(c.ms+300),"Focus points the card with the 300 ms button guard");
        c.c.Reset();Check(c.c.Shown()==NoHelp,"Reset clears the subject");
    }
}

// Every numeric row (Stepper, Slider) on its real page: its value is grabbed
// with the trigger and dragged; from the minimum (the handle at the bar's left
// end) a drag to the panel's right edge reaches the maximum, and back from the
// maximum to the left reaches the minimum, clamped at both ends. An unbounded
// row (no maximum, no bar) still drags in plain steps.
static void DragTests(){
    const auto& t=L::ContentTables();
    int rows=0;float finest=1e9f,widest=0;std::string finestRow;
    std::string coarseRows;
    for(size_t i=0;i<t.rowCount;++i){
        const auto& r=t.rows[i];using content::RowKind;
        if(r.kind!=RowKind::Stepper&&r.kind!=RowKind::Slider)continue;
        const Key k=r.key;const auto& d=Definitions[k];const std::string name=r.label;
        Values v=Defaults();
        v[TargetHighlight]=60;v[GroundAim]=1;v[SpellLock]=1;v[Foveation]=1;v[BloodVisor]=1;v[SmoothTurn]=k==TurnSpeed?1.f:0.f;v[Seated]=0;
        Snapshot s;s.values=v;s.ready=true;s.offlineDifficultyAvailable=true;
        Model m;m.Open(s);L::Press(m,{Kind::Open,int(r.page),-1},0);
        const bool bounded=std::isfinite(d.max)&&d.max<1e30f;
        m.Set(k,d.min);
        auto drag=[&](float fromX,float toX){
            P::PointerFilter f;uint64_t ms=0;L::Layout l=L::Build(m,m.context,{});
            const L::Element* e=nullptr;for(const auto& el:l.elements)if(el.role==L::Role::Value&&el.key==k)e=&el;
            Check(e&&e->Has(L::Drag)&&e->Has(L::Enabled)&&e->dragPx>0,name+": its value is draggable");
            if(e->dragPx<finest){finest=e->dragPx;finestRow=name;}
            Check(e->dragPx>=L::DragMinStepPx,name+": a drag unit is at least DragMinStepPx (tremor)");
            if(e->dragSteps)widest=std::max(widest,e->dragPx*float((e->dragSteps+e->dragCoarse-1)/e->dragCoarse));
            if(e->dragCoarse>1&&coarseRows.find(name)==std::string::npos)coarseRows+=(coarseRows.empty()?"":", ")+name+" x"+std::to_string(e->dragCoarse);
            const float y=float(e->rect.y+e->rect.h/2),x0=float(e->rect.x)+fromX*float(e->rect.w);
            f.Step(l,ms+=11,true,x0,y,false);   // pointed, trigger up (primes the filter)
            f.Step(l,ms+=11,true,x0,y,false);
            f.Step(l,ms+=11,true,x0,y,true);    // grab
            const int frames=60;
            for(int n=1;n<=frames;++n){
                const float x=x0+(toX-x0)*float(n)/frames;
                const auto out=f.Step(l,ms+=11,true,x,y,true);
                if(out.click.kind!=Kind::None)L::Press(m,out.click,ms);
                l=L::Build(m,m.context,{});   // the layout follows the value, as on the worker
            }
            f.Step(l,ms+=11,true,toX,y,false);
        };
        const float right=float(L::PanelInner.x+L::PanelInner.w-1);
        if(bounded){
            const float leftEnd=L::BarInset/float(L::Find(L::Build(m,m.context,{}),{Kind::Change,k,0})->rect.w);
            drag(leftEnd,right);
            Check(std::fabs(m.Get()[k]-Normalize(k,d.max))<.001f,name+": one sweep from the minimum to the panel's edge reaches the maximum");
            drag(1-leftEnd,float(L::ContentX));
            Check(std::fabs(m.Get()[k]-Normalize(k,d.min))<.001f,name+": and one sweep left reaches the minimum");
            const float before=m.Get()[k];drag(.5f,float(L::ContentX));
            Check(m.Get()[k]==before,name+": clamped at the end");
        }else{
            drag(.5f,right);Check(m.Get()[k]>d.min,name+": an unbounded row drags up in steps");
        }
        ++rows;
    }
    Check(rows>=20,"the drag test covered every numeric row");
    // At 0.95 m for the 1280 px panel: 0.742 mm a canvas px.
    std::printf("Dragged %d numeric rows end to end; finest %.2f px (%.1f mm) per drag unit (%s); widest sweep %.0f px (%.1f cm)\n",
        rows,double(finest),double(finest)*950.0/1280.0,finestRow.c_str(),double(widest),double(widest)*95.0/1280.0);
    std::printf("Coarse drag rows: %s\n",coarseRows.empty()?"none":coarseRows.c_str());
}

// The popover on the real layout: opening on release, the guard, a pick that
// slides off, the full-height hit boxes, and closing on a point-away.
static void PopoverTests(){
    Model m;Snapshot s;s.ready=true;m.Open(s);L::Press(m,{Kind::Open,int(View::Performance),-1},0);
    P::PointerFilter f;uint64_t ms=1000;
    L::LayoutInput in;in.nowMs=ms;auto layout=L::Build(m,{},in);
    const L::Element* face=nullptr;for(const auto& e:layout.elements)if(e.hit.kind==Kind::Popover&&e.hit.index==RenderResolution)face=&e;
    Check(face!=nullptr,"Render resolution's value opens a popover");
    // A press low in the row, below the drawn value but inside the row: its hit box is the full row height.
    const float x=float(face->rect.x+face->rect.w/2),y=float(face->HitBox().y+face->HitBox().h-3);
    Check(y>float(face->rect.y+face->rect.h),"the point is below the drawn control");
    for(int i=0;i<8;++i){f.Step(layout,ms,true,x,y,false);ms+=11;}
    f.Step(layout,ms,true,x,y,true);ms+=11;
    auto out=f.Step(layout,ms,true,x,y,false);ms+=11;
    Check(out.click.kind==Kind::Popover,"the release of a press anywhere in the row's height opens it");
    L::Press(m,out.click,ms);in.nowMs=ms;layout=L::Build(m,{},in);
    out=f.Step(layout,ms,true,x,y,false);ms+=11;
    Check(out.click.kind==Kind::None&&m.popoverKey>=0,"the opening press picks nothing");
    std::vector<const L::Element*> options;for(const auto& e:layout.elements)if(e.role==L::Role::PopoverOption)options.push_back(&e);
    Check(options.size()==7,"seven sizes");
    // Pressed on one option, released on the next: no pick.
    ms+=400;in.nowMs=ms;layout=L::Build(m,{},in);options.clear();for(const auto& e:layout.elements)if(e.role==L::Role::PopoverOption)options.push_back(&e);
    const auto at=[&](const L::Element* e){return std::pair<float,float>{float(e->rect.x+e->rect.w/2),float(e->rect.y+e->rect.h/2)};};
    auto [ax,ay]=at(options[1]);auto [bx,by]=at(options[3]);
    for(int i=0;i<10;++i){f.Step(layout,ms,true,ax,ay,false);ms+=11;}
    f.Step(layout,ms,true,ax,ay,true);ms+=11;
    for(int i=0;i<10;++i){f.Step(layout,ms,true,ax+(bx-ax)*float(i)/9,ay+(by-ay)*float(i)/9,true);ms+=11;}
    out=f.Step(layout,ms,true,bx,by,false);ms+=11;
    Check(out.click.kind==Kind::None,"a press that slides off its option picks nothing");
    // A clean pick.
    for(int i=0;i<10;++i){f.Step(layout,ms,true,bx,by,false);ms+=11;}
    f.Step(layout,ms,true,bx,by,true);ms+=11;out=f.Step(layout,ms,true,bx,by,false);ms+=11;
    Check(out.click.kind==Kind::Set&&out.click.value==options[3]->hit.value,"a press and release on one option picks it");
    L::Press(m,out.click,ms);Check(m.popoverKey<0&&m.Get()[RenderResolution]==options[3]->hit.value,"and closes it");
    // Point-away.
    P::PopoverWatch w;const Rect card{600,300,500,400},row{328,200,920,84};
    Check(!w.Step(0,true,true,700,350,card,row)&&!w.Step(1000,true,true,700,350,card,row),"on the card: stays open");
    Check(!w.Step(1100,true,true,500,240,card,row),"on its row: stays open");
    Check(!w.Step(1200,true,true,700,290,card,row)&&!w.Step(1700,true,true,1110,500,card,row),"tremor at the edge: stays open");
    Check(!w.Step(2000,true,true,100,900,card,row)&&!w.Step(2300,true,true,100,900,card,row),"away for under 450 ms: stays open");
    Check(w.Step(2450,true,true,100,900,card,row),"away 450 ms: closes");
    w.Reset();Check(!w.Step(3000,true,false,0,0,card,row)&&w.Step(3450,true,false,0,0,card,row),"off the panel 450 ms: closes");
    w.Reset();Check(!w.Step(4000,false,true,100,900,card,row)&&!w.Step(9000,false,true,100,900,card,row),"nothing open: nothing to close");
    Check(!w.Step(9000,true,true,100,900,{0,0,0,0},row),"before its picture shows (no card yet): no close");
}

int main(){
    PointerTests();
    CardTests();
    DragTests();
    PopoverTests();
    std::printf("VR settings pointer tests passed (%d checks)\n",checks);
    return 0;
}
