#pragma once
#include "menu_controls.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace framevr::lootlabels {
constexpr float BackgroundAlpha=.85f;
constexpr uint64_t LeaseMs=300;
constexpr uint32_t ShowItemsCommand=0x25; // native "Show items (Alt)"; honors the player's loot filter
constexpr uint64_t ShowItemsMask=uint64_t{1}<<ShowItemsCommand;
// XR publishes a short gameplay lease. Native label callbacks never take the
// XR lock. The command dispatcher holds Show Items only while that lease is live;
// releasing the chord leaves native drop/hover labels alone. Show Items uses
// the game's nearby loot list, without a VR aim/pickup-distance restriction.
class Display {
    std::atomic<uint64_t> until{0},showUntil{0};
public:
    void Refresh(uint64_t player,uint64_t now,bool active,bool held=false) {
        const auto end=active&&player?now+LeaseMs:0;
        showUntil.store(held?end:0,std::memory_order_release);
        until.store(end,std::memory_order_release);
    }
    void Clear(){showUntil.store(0,std::memory_order_release);until.store(0,std::memory_order_release);}
    bool Active(uint64_t now)const {const auto end=until.load(std::memory_order_acquire);return end&&now<end;}
    bool ShowAll(uint64_t now)const {const auto end=showUntil.load(std::memory_order_acquire);return end&&now<end;}
};
inline Display display;
// Only raise the opacity of an existing native box. Negative/zero alpha tells
// the game not to fill it; rarity colors and intentional no-fill styles survive.
inline float BackingAlpha(float native){return native>0?std::max(native,BackgroundAlpha):native;}

class Shortcut {
    uint64_t session=0,epoch=0,last=0;
    bool armed=false,triggerHeld=false,gripHeld=false,triggerConsumed=false,gripConsumed=false;
public:
    void Suspend(){armed=false;last=0;}
    bool Step(uint64_t player,uint64_t version,uint64_t now,bool allowed,float trigger,float grip) {
        const bool finite=std::isfinite(trigger)&&std::isfinite(grip);
        const bool valid=allowed&&player&&finite;
        if(!valid||player!=session||version!=epoch||now<=last||now-last>150)armed=false;
        session=player;epoch=version;last=now;
        const bool wasTrigger=triggerHeld;
        // Use the analog actions directly. Hysteresis tolerates squeeze noise;
        // adding grip to a trigger already used for combat is not a new chord.
        triggerHeld=finite&&trigger>(triggerHeld?.2f:.55f);
        gripHeld=finite&&grip>(gripHeld?.35f:.55f);
        const bool edge=triggerHeld&&!wasTrigger;
        if(!triggerHeld)triggerConsumed=false;
        if(!gripHeld)gripConsumed=false;
        if(valid&&triggerHeld&&gripHeld&&(edge||!armed||triggerConsumed))triggerConsumed=gripConsumed=true;
        if(!triggerHeld&&valid)armed=true;
        // Hold, never latch. Either release hides the extra labels immediately,
        // while the remaining consumed button cannot replay into combat/grabs.
        // Grip may be re-squeezed during our consumed trigger hold; adding it
        // to an unrelated attack still cannot steal that attack's trigger.
        return valid&&armed&&triggerHeld&&gripHeld&&triggerConsumed;
    }
    bool Consumed(bool nativeUi=false)const{return !nativeUi&&triggerConsumed;}
    bool GripConsumed(bool nativeUi=false)const{return !nativeUi&&gripConsumed;}
    void Filter(menus::Pad& controls,float& rightGrip,bool nativeUi=false)const {
        if(Consumed(nativeUi))controls.rt=0;
        if(GripConsumed(nativeUi)){controls.buttons&=~menus::RightBumper;rightGrip=0;}
    }
};
struct TextScaleScope {
    float& slot;float previous;
    TextScaleScope(float& value,float scale):slot(value),previous(value){slot=scale;}
    ~TextScaleScope(){slot=previous;}
    TextScaleScope(const TextScaleScope&)=delete;
};

// Rebuild the native bounds using actual text measurements at the new scale.
// This keeps overlap placement, the backing box and click bounds in agreement.
inline bool Resize(int32_t* box,const int32_t* oldText,const int32_t* newText,float scale) {
    if(!box||!oldText||!newText||!std::isfinite(scale)||scale<.2f||scale>2.f)return false;
    const int64_t padW=int64_t(box[2])-oldText[0],padH=int64_t(box[3])-oldText[1];
    if(oldText[0]<=0||oldText[1]<=0||newText[0]<=0||newText[1]<=0||padW<0||padH<0||padW>400||padH>200)return false;
    const int64_t width=int64_t(newText[0])+int64_t(std::ceil(float(padW)*scale));
    const int64_t height=int64_t(newText[1])+int64_t(std::ceil(float(padH)*scale));
    const int64_t x=int64_t(box[0])+box[2]/2-width/2,y=int64_t(box[1])+box[3]-height;
    if(width>INT32_MAX||height>INT32_MAX||x<INT32_MIN||x>INT32_MAX||y<INT32_MIN||y>INT32_MAX)return false;
    box[0]=int32_t(x);box[1]=int32_t(y);box[2]=int32_t(width);box[3]=int32_t(height);return true;
}
}
