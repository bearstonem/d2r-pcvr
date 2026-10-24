#pragma once
// VR Settings pointing: turns the laser's raw position and the trigger into
// safe presses (PointerFilter), and decides what the help card shows
// (CardFocus). Both run on the frame thread on a millisecond clock passed in,
// so the tests drive them with made-up times (vr_settings_pointer_test.cpp).
//
// transport.cpp drives both; settings::InputGate keeps only the chord and
// Right B edges.
#include "vr_settings_layout.h"
#include <array>
#include <cmath>
namespace framevr::settings::pointer {

// Laser + trigger -> hover, pressed and clicks.
// - Pre-trigger lock: a press goes to the element that was under the laser
//   LockMs before the trigger went down (the hand dips as it pulls).
// - Hysteresis: hover stays on an element until the laser is HysteresisPx
//   outside its rect.
// - Click on release: the press clicks when the trigger is released, unless
//   the laser is then more than ReleaseSlopPx outside the pressed element.
// - Hold-repeat (Flag::Repeat, steppers): held RepeatDelayMs, then one click
//   every 1000/RepeatPerSecond ms; a release after repeats adds no click.
// - Drag (Flag::Drag, a slider's value): a press on it does nothing; moving
//   DragStartPx along x starts a scrub of one unit per Element::dragPx (else
//   DragStepPx) from the press point, sent as Change with delta = steps since
//   the last output. A unit is Element::dragCoarse steps; coarse units land
//   on multiples of it. Units change with hysteresis (DragUnit): a full unit
//   from the press point to start, then half a unit past the last boundary to
//   turn back, so tremor at a boundary never flickers the value. A row of
//   known length (dragSteps) stops at its ends and the drag restarts there,
//   so travel past an end need not be undone; the model clamps too.
// - Only elements with a hit are targets: a label or row slab never is.
// - A trigger already held when the filter starts (or after Reset) must be
//   released first: opening the panel or changing views never presses.
class PointerFilter {
public:
    static constexpr uint64_t LockMs=90,RepeatDelayMs=400;
    static constexpr int RepeatPerSecond=8;
    static constexpr uint64_t RepeatMs=1000/RepeatPerSecond;
    static constexpr float HysteresisPx=10,ReleaseSlopPx=24,DragStartPx=16,DragStepPx=24,DragTurnUnits=.5f;
    // The drag unit for travel f (in units, signed) after unit u: away from
    // the press point it moves at each whole unit; back toward it only
    // DragTurnUnits past the boundary it crossed. Symmetric on both sides.
    static int DragUnit(float f,int u){
        if(u>=0&&f>=float(u+1))return int(std::floor(f));
        if(u<=0&&f<=float(u-1))return int(std::ceil(f));
        if(u>0&&f<float(u)-DragTurnUnits){const int n=int(std::ceil(f+DragTurnUnits))-1;return n>=0?n:f<=-1?int(std::ceil(f)):0;}
        if(u<0&&f>float(u)+DragTurnUnits){const int n=int(std::floor(f-DragTurnUnits))+1;return n<=0?n:f>=1?int(std::floor(f)):0;}
        return u;
    }
    // The steps from the press-time value for drag unit u: coarse units land
    // on multiples of `coarse` from the minimum; clamped to the row's ends.
    static int DragOffset(int u,int coarse,int index,int steps){
        coarse=std::max(1,coarse);
        if(index<0)return u*coarse;   // value unknown: plain units, the model clamps
        if(u==0)return 0;
        int target=u>0?index/coarse*coarse+u*coarse:(index+coarse-1)/coarse*coarse+u*coarse;
        if(steps>0)target=std::clamp(target,0,steps);
        return target-index;
    }
    // The units that reach the row's ends from `index` (lo <= 0 <= hi).
    static void DragUnitRange(int coarse,int index,int steps,int& lo,int& hi){
        coarse=std::max(1,coarse);
        const int up=index/coarse*coarse,down=(index+coarse-1)/coarse*coarse;
        hi=index>=steps?0:(steps-up+coarse-1)/coarse;
        lo=index<=0?0:-(down/coarse);
    }
    struct Output {
        Hit hover{};     // LayoutInput::hover
        Hit pressed{};   // LayoutInput::pressed: held, for the pressed look
        Hit click{};     // this frame's press for layout::Press (Kind::None: none)
        bool blocked=false;   // a press landed on a locked element (play the blocked sound)
        bool held=false;      // click came from a hold-repeat or a drag (the element was already pressed)
    };
    // One frame: onPanel false when the laser misses the panel or tracking is
    // lost (that cancels a press); x, y canvas pixels; down the trigger state
    // (transport: pressed above .55, released below .20).
    Output Step(const layout::Layout& layout,uint64_t ms,bool onPanel,float x,float y,bool down){
        Output out;
        hover=onPanel?Hover(layout,x,y):Hit{};
        Remember(ms,hover);
        out.hover=hover;
        if(!down){
            if(wasDown&&pressed.kind!=Kind::None&&!dragging&&!repeated&&onPanel&&Near(pressedRect,x,y,ReleaseSlopPx))
                if(const auto* e=layout::Find(layout,pressed);e&&e->Has(layout::Enabled)&&!e->Has(layout::Drag))out.click=pressed;
            Release();wasDown=false;
            return out;
        }
        if(!wasDown){   // trigger-down: lock the target
            wasDown=true;
            if(onPanel){
                Hit target=Locked(ms);
                const layout::Element* e=layout::Find(layout,target);
                if(!e){target=hover;e=layout::Find(layout,target);}
                if(e){
                    if(!e->Has(layout::Enabled))out.blocked=true;
                    else{pressed=target;pressedRect=e->HitBox();pressedAt=lastRepeat=ms;pressX=x;pressY=y;dragSent=dragUnit=0;dragging=repeated=false;
                        dragCoarse=std::max(1,e->dragCoarse);dragIndex=e->dragSteps>0?e->dragIndex:-1;dragSteps=e->dragSteps;}
                }
            }
            out.pressed=pressed;
            return out;
        }
        // Held.
        if(pressed.kind==Kind::None)return out;
        if(!onPanel){Release();return out;}   // off the panel or tracking lost: the press is gone
        const layout::Element* e=layout::Find(layout,pressed);
        if(!e){Release();return out;}
        pressedRect=e->HitBox();
        if(e->Has(layout::Drag)){
            if(!dragging&&std::fabs(x-pressX)>=DragStartPx)dragging=true;
            if(dragging){
                const float per=e->dragPx>0?e->dragPx:DragStepPx;   // the layout scales long ranges to one arm sweep
                const float f=(x-pressX)/per;
                int unit=DragUnit(f,dragUnit);bool atEnd=false;
                if(dragIndex>=0){   // a known row stops at its ends
                    int lo=0,hi=0;DragUnitRange(dragCoarse,dragIndex,dragSteps,lo,hi);
                    if(f>float(hi)){unit=hi;atEnd=true;}else if(f<float(lo)){unit=lo;atEnd=true;}
                }
                const int offset=DragOffset(unit,dragCoarse,dragIndex,dragSteps);
                if(offset!=dragSent&&e->Has(layout::Enabled)){out.click=pressed;out.click.delta=offset-dragSent;out.held=true;dragSent=offset;}
                dragUnit=unit;
                // At an end the drag starts again from there: travel past the
                // end need not be undone, and a unit back lands on a multiple.
                if(atEnd&&dragSent==offset){dragIndex+=offset;dragSent=dragUnit=0;pressX=x;}
            }
            out.pressed=pressed;
            return out;
        }
        const bool inside=Near(pressedRect,x,y,ReleaseSlopPx);
        if(e->Has(layout::Repeat)&&inside&&e->Has(layout::Enabled)){
            if(!repeated&&ms>=pressedAt+RepeatDelayMs){out.click=pressed;out.held=true;repeated=true;lastRepeat=ms;}
            else if(repeated&&ms>=lastRepeat+RepeatMs){out.click=pressed;out.held=true;lastRepeat+=RepeatMs;if(ms>=lastRepeat+RepeatMs)lastRepeat=ms;}
        }
        if(inside)out.pressed=pressed;
        return out;
    }
    // Opening, closing or a view change: forget the hover, press and history.
    // A trigger still held must be released before the next press.
    void Reset(){*this=PointerFilter{};}
    const Hit& Pressed()const{return pressed;}
private:
    struct Past {uint64_t ms=0;Hit hit{};bool used=false;};
    std::array<Past,16> history{};   // recent hovers, for the pre-trigger lock
    int head=0;
    Hit hover{},pressed{};
    Rect pressedRect{0,0,0,0};
    uint64_t pressedAt=0,lastRepeat=0;
    float pressX=0,pressY=0;
    int dragSent=0,dragUnit=0;              // steps sent since the drag's base; the drag unit now
    int dragCoarse=1,dragIndex=-1,dragSteps=0;   // the pressed value's Element::drag* (dragIndex: the base, moved to an end it reaches)
    bool wasDown=true,dragging=false,repeated=false;

    static bool Near(const Rect& r,float x,float y,float slop){
        return x>=float(r.x)-slop&&x<float(r.x+r.w)+slop&&y>=float(r.y)-slop&&y<float(r.y+r.h)+slop;
    }
    static bool Encloses(const Rect& outer,const Rect& inner){
        return inner.x>=outer.x&&inner.y>=outer.y&&inner.x+inner.w<=outer.x+outer.w&&inner.y+inner.h<=outer.y+outer.h;
    }
    // The pointed element with hysteresis: the old one holds within
    // HysteresisPx of its rect unless the laser is on another element (a
    // container around it, such as the help card's field, does not count).
    Hit Hover(const layout::Layout& layout,float x,float y)const{
        const layout::Element* raw=layout::ElementAt(layout,x,y);
        if(hover.kind!=Kind::None&&!(raw&&raw->hit==hover))
            if(const auto* old=layout::Find(layout,hover);old&&Near(old->HitBox(),x,y,HysteresisPx)&&(!raw||Encloses(raw->HitBox(),old->HitBox())))return hover;
        return raw?raw->hit:Hit{};
    }
    void Remember(uint64_t ms,const Hit& hit){
        head=(head+1)%int(history.size());history[size_t(head)]={ms,hit,true};
    }
    // The hover LockMs ago: the newest sample at or before then, else the
    // oldest kept; a miss then (nothing pointed) falls back to now.
    Hit Locked(uint64_t ms)const{
        const Past* oldest=nullptr;
        for(int i=0;i<int(history.size());++i){
            const auto& p=history[size_t((head-i+int(history.size()))%int(history.size()))];
            if(!p.used)break;
            if(p.ms+LockMs<=ms)return p.hit.kind!=Kind::None?p.hit:hover;
            oldest=&p;
        }
        return oldest&&oldest->hit.kind!=Kind::None?oldest->hit:hover;
    }
    void Release(){pressed={};dragging=repeated=false;dragSent=dragUnit=0;}
};

// An open popover closes when the laser points away: off the popover's card
// and off the row it edits (each with SlopPx around it) for AwayMs, or off
// the panel for as long. Crossing the gap from the row to the card, or a
// tremor at its edge, does not close it. Nothing open: nothing to watch.
class PopoverWatch {
public:
    static constexpr uint64_t AwayMs=450;
    static constexpr float SlopPx=24;
    // True on the frame the popover should close (Model::popoverKey = -1).
    bool Step(uint64_t ms,bool open,bool onPanel,float x,float y,const Rect& card,const Rect& row){
        if(!open||card.w<=0){awaySince=0;return false;}
        const bool pointing=onPanel&&(Near(card,x,y)||Near(row,x,y));
        if(pointing){awaySince=0;return false;}   // (not "near": a windows.h macro)
        if(!awaySince)awaySince=ms?ms:1;
        if(ms>=awaySince+AwayMs){awaySince=0;return true;}
        return false;
    }
    void Reset(){awaySince=0;}
private:
    uint64_t awaySince=0;
    static bool Near(const Rect& r,float x,float y){
        return r.w>0&&x>=float(r.x)-SlopPx&&x<float(r.x+r.w)+SlopPx&&y>=float(r.y)-SlopPx&&y<float(r.y+r.h)+SlopPx;
    }
};

// What the help card shows.
// - Dwell: the card switches to a new subject only after DwellMs of pointing
//   at it while the laser moves slower than MaxSpeedPxPerS; a fast sweep
//   never switches it. Speed is measured over SpeedWindowMs, so hand jitter
//   does not read as a sweep.
// - Freeze: inside the card's rect the card never switches, and it stays
//   frozen FreezeAfterMs after the laser leaves it.
// - Button guard: card buttons ignore presses for ButtonGuardMs after the card
//   changes subject (also covers Open and pending-list jumps: call Focus).
// - Nothing pointed: the card keeps its subject (sticky) until Reset.
class CardFocus {
public:
    static constexpr uint64_t DwellMs=250,FreezeAfterMs=300,ButtonGuardMs=300,SpeedWindowMs=60;
    static constexpr float MaxSpeedPxPerS=400;
    // One frame: pointed is layout::HelpAt under the laser (NoHelp: none);
    // card the card's rect from the last layout. Returns the subject shown.
    int Step(uint64_t ms,int pointed,float x,float y,bool onPanel,Rect card){
        const float speed=onPanel?Speed(ms,x,y):0.f;
        if(onPanel)Remember(ms,x,y);else samples=0;
        if(onPanel&&card.w>0&&card.h>0&&card.Contains(x,y)){inCard=true;leftCardAt=ms;candidate=NoHelp;return shown;}
        if(inCard){inCard=false;leftCardAt=ms;}
        if(leftCardAt&&ms<leftCardAt+FreezeAfterMs){candidate=NoHelp;return shown;}
        if(!onPanel||pointed==NoHelp||pointed==shown){candidate=NoHelp;return shown;}
        if(pointed!=candidate||speed>=MaxSpeedPxPerS){candidate=pointed;candidateSince=ms;}
        else if(ms>=candidateSince+DwellMs){shown=candidate;candidate=NoHelp;changedAt=ms;}
        return shown;
    }
    int Shown()const{return shown;}
    // Point the card at a subject now (Open's focus key; NoHelp: the idle
    // card), with the button guard.
    void Focus(int subject,uint64_t ms){shown=subject;candidate=NoHelp;changedAt=ms;}
    // False for ButtonGuardMs after the subject changed: LayoutInput::cardArmed.
    bool ButtonsArmed(uint64_t ms)const{return ms>=changedAt+ButtonGuardMs;}
    void Reset(){*this=CardFocus{};}
private:
    struct Sample {uint64_t ms=0;float x=0,y=0;};
    std::array<Sample,16> recent{};
    int head=0,samples=0;
    int shown=NoHelp,candidate=NoHelp;
    uint64_t candidateSince=0,changedAt=0,leftCardAt=0;
    bool inCard=false;
    void Remember(uint64_t ms,float x,float y){head=(head+1)%int(recent.size());recent[size_t(head)]={ms,x,y};samples=std::min(samples+1,int(recent.size()));}
    // px/s from the newest kept sample at least SpeedWindowMs old (else the
    // oldest kept, over at least SpeedWindowMs, so the first frames' jitter
    // does not read as a sweep) to now.
    float Speed(uint64_t ms,float x,float y)const{
        const Sample* from=nullptr;
        for(int i=0;i<samples;++i){
            const auto& s=recent[size_t((head-i+int(recent.size()))%int(recent.size()))];
            from=&s;
            if(s.ms+SpeedWindowMs<=ms)break;
        }
        if(!from||ms<=from->ms)return 0;
        return std::hypot(x-from->x,y-from->y)*1000.f/float(std::max<uint64_t>(ms-from->ms,SpeedWindowMs));
    }
};
}
