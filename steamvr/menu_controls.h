#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace framevr::menus {
// XInput bit values; portable so menu transitions can be exercised without XR.
enum Button : uint16_t {
    Start=0x10,LeftStick=0x40,RightStick=0x80,LeftBumper=0x100,RightBumper=0x200,
    A=0x1000,B=0x2000,X=0x4000,Y=0x8000
};
struct Pad {
    uint16_t buttons=0;
    uint8_t lt=0,rt=0;
    int16_t lx=0,ly=0,rx=0,ry=0;
};
enum class Mode : uint32_t {World=0x56524d30,Native=0x56524d31,Blocked=0x56524d32};
// Opt-in tags in BodyWalk's reserved fields. Existing mirror readers/writers
// retain their wire format. A game poll rejects reports from another UI epoch.
inline bool Tagged(uint32_t tag){return tag==uint32_t(Mode::World)||tag==uint32_t(Mode::Native)||tag==uint32_t(Mode::Blocked);}
inline bool Allowed(uint32_t tag,uint32_t epoch,uint32_t current,bool native,bool settings) {
    return epoch==current&&!settings&&tag==uint32_t(native?Mode::Native:Mode::World);
}
inline bool Tilted(int16_t x,int16_t y){return std::max(std::abs(int(x)),std::abs(int(y)))>8000;}
inline bool Busy(const Pad& p){return p.buttons||p.lt>25||p.rt>25||Tilted(p.lx,p.ly)||Tilted(p.rx,p.ry);}
inline bool SettingsChord(const Pad& p,bool quickCastConsumed) {
    return !quickCastConsumed&&(p.buttons&(Y|LeftBumper))==(Y|LeftBumper);
}
inline uint16_t WorldButtons(const Pad& p){return p.buttons&(B|Y);}

// Each held control must be released before it can act in a new context. A
// held grip does not prevent a newly pressed face button or the other stick.
class ReleaseGate {
    uint32_t phase=0,epoch=0;
    uint16_t buttons=0xffff;
    bool lt=true,rt=true,left=true,right=true;
public:
    void Suspend(){phase=0;buttons=0xffff;lt=rt=left=right=true;}
    Pad Step(Mode mode,uint32_t version,Pad p) {
        if(phase!=uint32_t(mode)||epoch!=version){Suspend();phase=uint32_t(mode);epoch=version;}
        buttons&=p.buttons;p.buttons&=~buttons;
        if(p.lt<=25)lt=false;if(lt)p.lt=0;
        if(p.rt<=25)rt=false;if(rt)p.rt=0;
        if(std::max(std::abs(int(p.lx)),std::abs(int(p.ly)))<=4000)left=false;if(left)p.lx=p.ly=0;
        if(std::max(std::abs(int(p.rx)),std::abs(int(p.ry)))<=4000)right=false;if(right)p.rx=p.ry=0;
        return p;
    }
};

// Left grip + laser trigger provides the old pointer context-click without
// stealing native A. A modifier change cannot convert a held item drag.
struct LaserClick {
    bool held=false,context=false;
    void Step(bool trigger,bool modifier) {
        if(trigger&&!held)context=modifier;
        held=trigger;
    }
    bool Left() const{return held&&!context;}
    bool Right() const{return held&&context;}
};

// VR Settings > HUD > Game menus, the pointer half ([hud] menu_pointer).
enum class Pointer { Auto, Laser, Controller };

// Auto: sticks/face buttons select native navigation. A deliberate change in
// aim returns to the laser after native controls are released. Tracking jitter
// cannot steal focus; a drag keeps its pointer even outside the canvas.
// Laser: controls still reach the game while held; the laser returns on release.
// Controller: native navigation only.
class Navigation {
    bool native=false,anchor=false;
    float u=0,v=0;
    ReleaseGate nativeGate;
public:
    Pointer pointer=Pointer::Auto;
    void Reset(){native=anchor=false;nativeGate.Suspend();}
    bool Native() const{return native;}
    Pad Step(bool active,Pad p,bool hit,float x,float y,bool dragging) {
        if(!active){Reset();return {};}
        const bool intent=(p.buttons&~(LeftBumper|RightBumper))||Tilted(p.lx,p.ly)||Tilted(p.rx,p.ry);
        if((!dragging&&intent)||pointer==Pointer::Controller){
            if(!native){nativeGate.Suspend();native=true;}
            anchor=hit;u=x;v=y;
        }else if(native&&!Busy(p)&&(pointer==Pointer::Laser||(hit&&(!anchor||std::hypot(x-u,y-v)>.045f)))) {
            native=false;anchor=false;
        }
        if(!native)return {};
        // Modifiers already held for the laser must not become native triggers
        // or bumpers when a face button switches navigation. Navigation intent
        // itself is fresh (the outer gate consumed menu-entry holds).
        const Pad modifiers=nativeGate.Step(Mode::Native,0,p);
        p.buttons=(p.buttons&~(LeftBumper|RightBumper))|(modifiers.buttons&(LeftBumper|RightBumper));
        p.lt=modifiers.lt;p.rt=modifiers.rt;
        return p;
    }
};
}
