#pragma once
#include <cstdint>
namespace framevr::nativehud {
// The native HUD update assumes ExpFlashLeft/ExpFlashRight exist. A hidden
// controller HUD may lack them (observed null +0x170 during combat).
// Defer that presentation update until the layout is ready.
inline bool DeferUpdate(bool hidden,bool readable,uintptr_t first,uintptr_t second) {
    return hidden&&(!readable||!first||!second);
}
// Hide the native panel itself, so nonstandard UI layouts cannot escape a
// bottom-strip crop. Restore only a panel this override actually hid. Calls
// run on the UI thread; repeated hides cover native mode/layout refreshes.
struct VisibilityOverride {
    bool hidden=false;
    template<class Set>void Update(bool hide,bool found,Set set){
        if(!found){hidden=false;return;}
        if(hide){if(set(false))hidden=true;}
        else if(hidden&&set(true))hidden=false;
    }
};
// The aura icon at the screen's top left is the game's own root panel
// PaladinAuraPanel, apart from HUDPanel. Active auras already show on the
// wrist (Companions & effects), so in a VR session, menus included, while
// that HUD row is On (Want), it is
// hidden by visibility, the panel's own switch (no screen box is cleared:
// portraits share that corner), on every UI step, as HUDPanel and
// NotificationPanel are (VisibilityOverride), since the game may show it again
// unasked. Leaving the VR session, or turning that row Off (auras then show
// nowhere on the wrist), releases it: shown again only while an aura
// runs (an aura that starts later shows it by itself); with no fresh aura
// sample it waits UnknownWaitMs, then leaves it alone. An unloaded panel is
// forgotten. The flat screen never touches it.
inline constexpr const char* AuraPanel="PaladinAuraPanel";
struct AuraIconHider {
    static constexpr uint64_t UnknownWaitMs=2000;
    static constexpr int MaxLines=60;   // "Frame aura icon:" detail lines per VR session
    enum class Act { None, Hide, Show, Leave, LeaveUnknown, Wait, Reentry };
    VisibilityOverride visibility;   // hidden: this override hid it
    bool busy=false;                 // inside our own visibility call
    uint64_t releaseSince=0;
    bool Hidden()const{return visibility.hidden;}
    // Hidden only in a VR session with HUD > Companions & effects On.
    static constexpr bool Want(bool session,bool wristEffects){return session&&wristEffects;}
    // want: Want(). aura: an aura state on the local
    // player now (1), none (0) or no fresh sample (-1). set(visible): the
    // panel's visibility call, true on success. A PanelManager message raised
    // by that call re-enters here and is refused (Reentry), never looping.
    template<class Set>Act Step(bool want,bool found,int aura,uint64_t now,Set set){
        if(busy)return Act::Reentry;
        busy=true;
        Act act=Act::None;
        if(want){releaseSince=0;visibility.Update(true,found,set);act=visibility.hidden?Act::Hide:Act::None;}
        else if(visibility.hidden) {
            if(!found)visibility.hidden=false;   // unloaded: a new one starts shown
            else if(aura>0){visibility.Update(false,true,set);act=visibility.hidden?Act::Wait:Act::Show;}   // a refused show retries
            else if(aura==0){visibility.hidden=false;act=Act::Leave;}
            else {
                if(!releaseSince)releaseSince=now;
                if(now-releaseSince>=UnknownWaitMs){visibility.hidden=false;act=Act::LeaveUnknown;}
                else act=Act::Wait;
            }
            if(!visibility.hidden)releaseSince=0;
        }
        busy=false;
        return act;
    }
};
// Detail lines: at most MaxLines from each VR session's start. The release
// lines are written outside this cap.
struct AuraIconLog {
    int lines=0;
    void Restart(){lines=0;}
    bool Allow(){return lines<AuraIconHider::MaxLines&&++lines>0;}
};
struct Policy {
    int hide=0;
    bool copyToolbar=false,redrawToolbar=false;
    // Classic views only remove a bar when its replacement copy succeeded.
    bool ClearToolbar(bool copied)const{return hide==1&&(!redrawToolbar||copied);}
};
inline bool Gameplay(bool stereo,bool inWorld,bool nativeMenu,bool spatialMenu) {
    return stereo&&inWorld&&!nativeMenu&&!spatialMenu;
}
inline Policy Resolve(bool vrGameplay,bool menu,bool classic,bool pictureReady,bool barChanged,int configuredHide) {
    // The tracked vitals/belt replace the desktop toolbar in native OpenXR.
    // Ignore legacy INI hide=0 and classic-camera redraw preferences here.
    if(vrGameplay)return {1,false,false};
    if(menu)return {};
    if(classic)return pictureReady&&barChanged?Policy{1,true,true}:Policy{};
    return {configuredHide,configuredHide>=1,false};
}
}
