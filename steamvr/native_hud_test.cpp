#include "native_hud.h"
#include "callback_ticket.h"
#include <cassert>
#include <cstdio>
#include <functional>
#include <initializer_list>
using namespace framevr::nativehud;
int main(){
    // Crash regression: the hidden HUD update dereferenced a null first
    // notification widget at +0x145. Cover either missing child and a layout
    // rebuild; normal desktop updates and fully bound layouts pass through.
    for(bool hidden:{false,true}) {
        assert(DeferUpdate(hidden,true,0,0x10000)==hidden);
        assert(DeferUpdate(hidden,true,0x10000,0)==hidden);
        assert(DeferUpdate(hidden,false,0x10000,0x20000)==hidden);
        assert(!DeferUpdate(hidden,true,0x10000,0x20000));
    }
    VisibilityOverride visibility;bool shown=true;int calls=0;
    auto set=[&](bool visible){shown=visible;++calls;return true;};
    visibility.Update(false,true,set);assert(shown&&calls==0); // desktop untouched
    visibility.Update(true,true,set);assert(!shown&&visibility.hidden);
    shown=true;visibility.Update(true,true,set);assert(!shown); // game rebuilt controller HUD
    visibility.Update(false,true,[](bool){return false;});assert(visibility.hidden); // retry restoration
    visibility.Update(false,true,set);assert(shown&&!visibility.hidden); // VR stop only
    visibility.Update(true,false,set);assert(!visibility.hidden); // unloaded panel
    visibility.Update(true,true,set);assert(!shown&&visibility.hidden); // new game
    visibility.Update(false,false,set);assert(!visibility.hidden); // no stale handle on exit
    // A native panel handler can re-show the HUD after the initial hide. A
    // queued UI refresh must reapply the session override, including when
    // opening/closing menus repeatedly and after recreating the HUD panel.
    framevr::CallbackTicket refresh;
    for(int transition=0;transition<8;++transition) {
        visibility.Update(true,true,set);assert(!shown);
        const auto token=refresh.Start(100+transition);
        assert(token&&!refresh.Start(100+transition)); // coalesce message burst
        shown=true; // native handler follows our message listener
        assert(refresh.Claim(token));visibility.Update(true,true,set);
        assert(!shown&&visibility.hidden);
    }
    visibility.Update(true,false,set); // old HUD unloaded
    shown=true;visibility.Update(true,true,set);assert(!shown); // replacement hidden
    const auto pending=refresh.Start(200);
    visibility.Update(false,true,set);assert(shown); // session ends before queued refresh
    assert(refresh.Claim(pending));visibility.Update(false,true,set);assert(shown); // cannot re-hide desktop
    // Reproduce both reported routes: old hide=0 INIs and a classic camera
    // with its toolbar zoom/position effect enabled. Neither can leak a bar.
    for(int configured:{0,1,2})for(bool classic:{false,true})for(bool changed:{false,true})for(bool staleMenu:{false,true}){
        const auto p=Resolve(true,staleMenu,classic,true,changed,configured);
        assert(p.hide==1&&!p.copyToolbar&&!p.redrawToolbar&&p.ClearToolbar(false));
    }
    assert(Gameplay(true,true,false,false));
    assert(!Gameplay(true,true,true,false)); // inventory/waypoint fallback
    assert(!Gameplay(true,true,true,true));  // spatial menu
    assert(!Gameplay(true,false,false,false)); // title/character selection
    assert(!Gameplay(false,true,false,false)); // stopped/lost XR or desktop
    // Releasing the per-eye override restores the exact legacy behavior.
    auto p=Resolve(false,false,false,false,false,0);
    assert(p.hide==0&&!p.copyToolbar&&!p.ClearToolbar(true));
    p=Resolve(false,false,false,false,false,1);
    assert(p.hide==1&&p.copyToolbar&&!p.redrawToolbar&&p.ClearToolbar(false));
    p=Resolve(false,false,false,false,false,2);assert(p.hide==2&&p.copyToolbar);
    p=Resolve(false,true,false,true,true,2);assert(p.hide==0&&!p.copyToolbar);
    p=Resolve(false,false,true,false,true,1);assert(p.hide==0);
    p=Resolve(false,false,true,true,false,1);assert(p.hide==0);
    p=Resolve(false,false,true,true,true,0);
    assert(p.hide==1&&p.copyToolbar&&p.redrawToolbar&&!p.ClearToolbar(false)&&p.ClearToolbar(true));
    {   // the aura icon panel, hidden on every step in VR; leaving VR shows it only with an aura running
        using Act=AuraIconHider::Act;
        AuraIconHider aura;bool shownIcon=true,accept=true;int sets=0;
        auto set=[&](bool visible){++sets;if(accept)shownIcon=visible;return accept;};
        assert(aura.Step(false,true,1,0,set)==Act::None&&shownIcon&&sets==0);      // flat screen: untouched
        assert(aura.Step(true,false,1,10,set)==Act::None&&sets==0);                // not loaded yet
        assert(aura.Step(true,true,1,20,set)==Act::Hide&&!shownIcon&&aura.Hidden());
        shownIcon=true;assert(aura.Step(true,true,1,40,set)==Act::Hide&&!shownIcon); // the game showed it again: hidden on the next step
        assert(sets==2);                                                            // every step, as HUDPanel
        // leaving VR with an aura running: shown once, then never touched
        assert(aura.Step(false,true,1,100,set)==Act::Show&&shownIcon&&!aura.Hidden());
        const int afterShow=sets;
        for(uint64_t t=200;t<3000;t+=200)assert(aura.Step(false,true,t%400?1:0,t,set)==Act::None);
        assert(sets==afterShow);
        // leaving VR with no aura: not shown; the game shows it when an aura starts
        aura.Step(true,true,0,3000,set);assert(!shownIcon);
        assert(aura.Step(false,true,0,3100,set)==Act::Leave&&!shownIcon&&!aura.Hidden());
        assert(aura.Step(false,true,1,3200,set)==Act::None&&!shownIcon);             // released: an aura later is the game's own
        // leaving VR with no fresh sample: waits, then shows it if a sample says an aura runs
        aura.Step(true,true,0,4000,set);const int beforeWait=sets;
        assert(aura.Step(false,true,-1,4100,set)==Act::Wait&&aura.Step(false,true,-1,5000,set)==Act::Wait&&sets==beforeWait);
        assert(aura.Step(false,true,1,5200,set)==Act::Show&&shownIcon);
        // ... or leaves it alone once the wait runs out
        aura.Step(true,true,0,6000,set);
        assert(aura.Step(false,true,-1,6100,set)==Act::Wait&&aura.Step(false,true,-1,6100+AuraIconHider::UnknownWaitMs,set)==Act::LeaveUnknown);
        assert(!shownIcon&&!aura.Hidden());
        // refusals retry; an unloaded panel is forgotten
        accept=false;assert(aura.Step(true,true,0,7000,set)==Act::None&&!aura.Hidden());
        accept=true;assert(aura.Step(true,true,0,7200,set)==Act::Hide);
        accept=false;assert(aura.Step(false,true,1,7300,set)==Act::Wait&&aura.Hidden());
        accept=true;assert(aura.Step(false,true,1,7400,set)==Act::Show&&shownIcon);
        aura.Step(true,true,0,7500,set);
        assert(aura.Step(false,false,1,7600,set)==Act::None&&!aura.Hidden());        // unloaded: nothing of ours to show
        // Re-entry: a PanelManager message raised inside our own call is refused, never looping
        int nested=0,outerSets=0;Act inner=Act::None;
        std::function<bool(bool)> reenter=[&](bool){++outerSets;if(++nested<5)inner=aura.Step(true,true,0,8000,reenter);return true;};
        assert(aura.Step(true,true,0,8000,reenter)==Act::Hide&&inner==Act::Reentry&&outerSets==1&&!aura.busy);
        nested=0;assert(aura.Step(false,true,1,8100,reenter)==Act::Show&&inner==Act::Reentry&&outerSets==2);
        AuraIconLog log;int allowed=0;
        for(int i=0;i<100;++i)allowed+=log.Allow();
        assert(allowed==AuraIconHider::MaxLines);log.Restart();assert(log.Allow());
    }
    {   // the HUD > Companions & effects gate: hidden only while that row is On in VR
        using Act=AuraIconHider::Act;
        static_assert(AuraIconHider::Want(true,true)&&!AuraIconHider::Want(true,false)&&
                      !AuraIconHider::Want(false,true)&&!AuraIconHider::Want(false,false));
        AuraIconHider aura;bool shownIcon=true;int sets=0;
        auto set=[&](bool visible){++sets;shownIcon=visible;return true;};
        auto step=[&](bool effects,int state,uint64_t t){return aura.Step(AuraIconHider::Want(true,effects),true,state,t,set);};
        assert(step(false,1,0)==Act::None&&shownIcon&&sets==0);              // Off from the start: never touched
        assert(step(true,0,100)==Act::Hide&&!shownIcon);                      // Apply On: hidden
        assert(step(false,1,200)==Act::Show&&shownIcon&&!aura.Hidden());      // Apply Off with an aura: shown again
        const int afterShow=sets;
        assert(step(false,0,300)==Act::None&&step(false,1,400)==Act::None&&sets==afterShow);   // then the game's own
        assert(step(true,1,500)==Act::Hide&&!shownIcon);                      // On again: hidden again
        assert(step(false,0,600)==Act::Leave&&!shownIcon&&!aura.Hidden());    // Off with no aura: left for the game to show
        assert(step(false,-1,700)==Act::None);
    }
    std::puts("Native VR toolbar suppression, menu refresh recovery, desktop restoration and legacy redraw policy passed");
}
