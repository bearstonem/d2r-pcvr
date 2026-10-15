#include "vr_settings.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace framevr::settings;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static Hit At(Page p,Rect r){return HitTest(p,(r.x+r.w*.5f)/Width,(r.y+r.h*.5f)/Height);}
int main(){
    Snapshot s;s.ready=true;
    {Model effects;effects.Open(s);effects.Click({Kind::Tab,int(Page::Combat)});
        Check(s.values[ElementalHands]==0,"experimental hand effects default off");
        const auto hit=At(Page::Combat,ValueBox(1));
        Check(hit.index==ElementalHands&&hit.kind==Kind::Change,"elemental hand toggle hit target");
        effects.Click(hit);const auto request=effects.Click({Kind::Apply});
        Check(request.changed==(ChangeMask{1}<<ElementalHands)&&request.values[ElementalHands]==1,"setting beyond bit 31 saves independently");
        auto external=request.base;external[Motion]=1;
        Check(!Conflict(external,request.base,request.changed),"high-bit edit tolerates unrelated setting changes");
        external[ElementalHands]=1;Check(Conflict(external,request.base,request.changed),"high-bit conflicts detected");
    }
    Check(s.values[MeleeHitSpeed]==12,"melee defaults to twelve m/s");
    {Model melee;melee.Open(s);melee.Click({Kind::Tab,int(Page::Combat)});
        const auto hit=At(Page::Combat,Increase(0));
        Check(hit.index==MeleeHitSpeed&&hit.kind==Kind::Change,"combat speed control hit target");
        melee.Click(hit);Check(melee.Get()[MeleeHitSpeed]==12.25f,"melee speed quarter step");
        const auto request=melee.Click({Kind::Apply});
        Check(request.changed==(1u<<MeleeHitSpeed),"highest mask bit saves only melee speed");
        Check(Normalize(MeleeHitSpeed,NAN)==12&&Normalize(MeleeHitSpeed,0)==1.5f&&Normalize(MeleeHitSpeed,99)==99,"melee speed UI has no upper cap");
        s.values[MeleeHitSpeed]=6;melee.Open(s);melee.Click(hit);
        Check(melee.Get()[MeleeHitSpeed]==6.25f,"plus button goes beyond old cap");
        s.values[MeleeHitSpeed]=12;
    }
    Check(s.values[MenuDistance]==1&&s.values[TooltipTiming]==0,"comfortable Medium distance and Instant hover defaults");
    for(auto key:{MenuDistance,TooltipTiming}) {
        Check(Normalize(key,1.5f)==Definitions[key].defaultValue&&Normalize(key,9)==Definitions[key].defaultValue,"invalid comfort enums fall back safely");
        const int row=key==MenuDistance?3:4;
        Check(RowKey(Page::HUD,row)==key&&At(Page::HUD,Increase(row)).index==key,"HUD comfort rows hit their own settings");
        Model comfort;comfort.Open(s);comfort.Click(At(Page::HUD,Increase(row)));
        Check(comfort.Click({Kind::Apply}).changed==(1u<<key),"comfort Apply affects only its selected setting");
    }
    Check(s.values[ProjectileAimAssist]==2,"aim assist defaults to Strong");
    for(float bad:{-1.f,3.f,1.5f,NAN})Check(Normalize(ProjectileAimAssist,bad)==2,"invalid aim-assist preference falls back to Strong");
    {Model assist;assist.Open(s);const auto hit=At(Page::Controls,AimAssistBox);
        Check(hit.kind==Kind::Change&&hit.index==ProjectileAimAssist,"controls tab exposes aim assist");
        for(float choice:{0.f,1.f,2.f,0.f,1.f}){assist.Click(hit);Check(assist.Get()[ProjectileAimAssist]==choice,"aim assist cycles Off Light Strong");}
        auto request=assist.Click({Kind::Apply});
        Check(request.changed==(1u<<ProjectileAimAssist)&&request.values[ProjectileAimAssist]==1,"assist saves independently");
        assist.Click(hit);Check(assist.Get()[ProjectileAimAssist]==1,"assist cannot change while saving");
        assist.Close();assist.Open(s);Check(assist.Get()[ProjectileAimAssist]==2,"discarding edits restores saved assistance");
        Check(AimAssistBox.y>=PlanarSpellAimBox.y+PlanarSpellAimBox.h&&AimAssistBox.y+AimAssistBox.h<845,"assist avoids planar control and footer");}
    Check(s.values[OrdinaryMobBars]==0,"ordinary mob bars are off by default");
    {Model bars;bars.Open(s);bars.Click({Kind::Tab,int(Page::VR)});
        const auto hit=At(Page::VR,ValueBox(9));
        Check(hit.kind==Kind::Change&&hit.index==OrdinaryMobBars,"VR tab exposes ordinary mob toggle");
        Check(RowY+9*RowStep+72<876,"last row leaves room for save status");
        bars.Click(hit);auto request=bars.Click({Kind::Apply});
        Check(request.changed==(1u<<OrdinaryMobBars)&&request.values[OrdinaryMobBars]==1,"toggle saves only mob visibility");}
    Check(s.values[SmoothTurn]==0&&s.values[Snap]==30,"snap turning remains the default");
    Check(EffectiveSnapAngle(30,-1)==30&&EffectiveSnapAngle(0,-1)==0,"legacy snap and smooth profiles retain their mode");
    Check(EffectiveSnapAngle(45,1)==0&&EffectiveSnapAngle(45,0)==45,"explicit toggle selects camera mode without losing angle");
    Check(EffectiveSnapAngle(0,0)==30,"turning off legacy smooth mode restores default snap angle");
    Check(SnapDegrees(NAN)==30&&SnapDegrees(-20)==15&&SnapDegrees(999)==180,"camera and menu share snap bounds");
    {Model turning;turning.Open(s);
        turning.Click(At(Page::VR,ValueBox(3)));
        Check(turning.Get()[SmoothTurn]==1&&turning.Get()[Snap]==30,"value-box click enables smooth turning independently");
        turning.Click({Kind::Close});turning.Open(s);
        Check(turning.Get()[SmoothTurn]==0&&!turning.Dirty(),"closing without Apply discards smooth toggle");
        turning.Click(At(Page::VR,ValueBox(3)));
        auto request=turning.Click({Kind::Apply});
        Check(request.changed==(1u<<SmoothTurn),"Apply saves only the turning mode");
        turning.Click(At(Page::VR,ValueBox(3)));
        Check(turning.Get()[SmoothTurn]==1,"toggle cannot change while saving");
        auto saved=s;saved.values=request.values;saved.sequence=request.sequence;saved.result=Result::Saved;
        turning.Observe(saved);turning.Close();turning.Open(saved);
        turning.Click(At(Page::VR,ValueBox(3)));
        Check(turning.Get()[SmoothTurn]==0&&turning.Get()[Snap]==30,"reopened toggle restores snap turning");
    }
    Check(s.values[NavigationLocation]==0,"combined navigation defaults to Wrist");
    Check(Normalize(NavigationLocation,99)==0&&Normalize(NavigationLocation,1.5f)==0,"invalid navigation location falls back to Wrist");
    {Model navigation;navigation.Open(s);navigation.Click(At(Page::Controls,Tabs[int(Page::HUD)]));
        const int row=2;
        Check(RowCount(Page::HUD)==5&&RowKey(Page::HUD,row)==NavigationLocation,"one navigation row replaces separate map/compass switches");
        for(int location=1;location<=3;++location){
            navigation.Click(At(Page::HUD,Increase(row)));
            Check(navigation.Get()[NavigationLocation]==location,"navigation offers Palm, Either and Off");
        }
        auto request=navigation.Click({Kind::Apply});
        Check(request.changed==(1u<<NavigationLocation)&&request.values[NavigationLocation]==3&&request.values[VitalsLocation]==0,
            "combined navigation can turn off without changing vitals");}
    Check(s.values[VitalsLocation]==0&&s.values[CriticalAlerts]==0,"existing wrist placement remains default; automatic alerts require opt-in");
    Check(Normalize(VitalsLocation,99)==0&&Normalize(VitalsLocation,1.5f)==0,"unknown vitals locations fall back to wrist");
    {Model hud;hud.Open(s);hud.Click(At(hud.page,Tabs[int(Page::HUD)]));
        hud.Click(At(hud.page,Increase(0)));
        Check(hud.Get()[VitalsLocation]==1&&!hud.Get()[CriticalAlerts],"Palm selection does not enable automatic alerts");
        auto request=hud.Click({Kind::Apply});
        Check(request.changed==(1u<<VitalsLocation),"palm placement saves independently");
        hud.Open(s);hud.Click(At(Page::HUD,ValueBox(1)));request=hud.Click({Kind::Apply});
        Check(request.changed==(1u<<CriticalAlerts)&&request.values[CriticalAlerts]==1,"alert opt-in saves independently");}
    Check(s.values[PlanarSpellAim]==0,"spell aiming defaults to free vertical aim");
    {Model aim;aim.Open(s);const auto hit=At(Page::Controls,PlanarSpellAimBox);
        Check(hit.kind==Kind::Change&&hit.index==PlanarSpellAim,"controls tab exposes planar aiming toggle");
        aim.Click(hit);Check(s.values[PlanarSpellAim]==0&&aim.Get()[PlanarSpellAim]==1,"aim change is a draft");
        const auto request=aim.Click({Kind::Apply});
        Check(request.changed==(1u<<PlanarSpellAim)&&request.values[PlanarSpellAim]==1,"planar aim saves only its preference");
        Check(!WeaponIndicatorsBox.Contains(PlanarSpellAimBox.x,PlanarSpellAimBox.y)&&
              PlanarSpellAimBox.y+PlanarSpellAimBox.h<876,"aim toggle avoids other controls and footer");}
    Check(s.values[WeaponSkillIndicators]==0,"weapon skill indicators default off");
    {Model indicators;indicators.Open(s);const auto hit=At(Page::Controls,WeaponIndicatorsBox);
        Check(hit.kind==Kind::Change&&hit.index==WeaponSkillIndicators,"controls tab exposes weapon indicator toggle");
        indicators.Click(hit);auto request=indicators.Click({Kind::Apply});
        Check(request.changed==(1u<<WeaponSkillIndicators)&&request.values[WeaponSkillIndicators]==1,"indicator opt-in saves only its preference");}
    Check(s.values[ControllerLabels]==0&&Normalize(ControllerLabels,99)==0&&Normalize(ControllerLabels,1.5f)==0,"unknown controller preferences use automatic");
    {Model chooser;chooser.Open(s);chooser.Click({Kind::Tab,int(Page::VR)});
        auto hit=[&](Rect r){return HitTest(chooser.page,(r.x+r.w*.5f)/Width,(r.y+r.h*.5f)/Height,chooser.controllerMenu);};
        chooser.Click(hit(ControllerBox));Check(chooser.controllerMenu&&!chooser.Dirty(),"dropdown opens without changing selection");
        chooser.Click(hit(ControllerOption(2)));
        Check(!chooser.controllerMenu&&chooser.Get()[ControllerLabels]==2&&chooser.Dirty(),"Index option changes draft only");
        chooser.Click(hit(ControllerBox));
        const auto covered=hit(ValueBox(0));
        Check(covered.kind==Kind::ControllerChoice,"dropdown options own covered setting rows");
        const auto outside=hit({58,RowY,100,40});
        Check(outside.kind==Kind::DismissMenu,"open dropdown consumes outside clicks instead of changing rows");
        chooser.Click(outside);Check(!chooser.controllerMenu&&chooser.Get()[Motion]==0,"outside click only dismisses");
        chooser.Click(hit(ControllerBox));Check(chooser.DismissControllerMenu()&&chooser.open,"Back dismisses dropdown first");
        auto pending=chooser.Click({Kind::Apply});
        Check(pending.changed==(1u<<ControllerLabels)&&pending.values[ControllerLabels]==2,"Apply queues preference alone");
        chooser.Click(hit(ControllerBox));Check(!chooser.controllerMenu,"cannot edit while saving");
        chooser.Close();chooser.Open(s);Check(chooser.Get()[ControllerLabels]==0&&!chooser.controllerMenu,"reopen discards unsaved choice");
    }
    Check(s.values[DrawDistance]==2&&s.values[ObjectDistance]==300,"distance fallbacks match release configuration");
    {Model distance;distance.Open(s);
        for(int i=0;i<3;++i)distance.Click({Kind::Change,ObjectDistance,-1});
        Check(distance.Get()[ObjectDistance]==150,"object distance can reach native 150 without jumping from 300 to 50");}
    Model m;m.Open(s);
    Check(m.open&&m.ready&&!m.Dirty()&&m.page==Page::Controls,"open actual configuration");
    Check(HitTest(Page::VR,-.01f,.5f).kind==Kind::None&&HitTest(Page::VR,NAN,.5f).kind==Kind::None,"invalid laser coordinates ignored");
    Check(At(Page::VR,Calibrate).kind==Kind::Calibrate&&At(Page::VR,Recenter).kind==Kind::Recenter,"separate calibration and recenter buttons");
    Check(Normalize(BeltForward,.06f)==.06f,"settings preserve the closer belt placement");
    for(int p=0;p<TabCount;++p){
        auto hit=At(Page(p),Tabs[p]);Check(hit.kind==Kind::Tab&&hit.index==p,"tab rectangle matches input");
        for(int row=0;row<RowCount(Page(p));++row){
            auto plus=At(Page(p),Increase(row)),minus=At(Page(p),Decrease(row));
            Check(plus.kind==Kind::Change&&plus.index==RowKey(Page(p),row)&&plus.delta==1&&minus.delta==-1,"row controls match drawing");
        }
    }
    m.Click(At(Page::Controls,Tabs[1]));
    m.Click(At(m.page,ValueBox(0)));
    Check(m.Get()[Motion]==1&&m.Dirty()&&!m.Busy(),"edit remains a draft until Apply");
    m.Click({Kind::Close});m.Open(s);
    Check(!m.Dirty()&&m.Get()[Motion]==0,"Close discards unsubmitted edits");
    m.Click({Kind::Change,Foveation,1});m.Click({Kind::Change,Snap,1});
    auto r=m.Click({Kind::Apply});
    Check(r.sequence&&r.changed==((1u<<Foveation)|(1u<<Snap))&&r.values[Foveation]==2,"Apply submits exactly changed values");
    Check(m.Busy()&&!m.Click({Kind::Apply}).sequence,"Apply cannot double submit");
    m.Click({Kind::Change,Motion,1});Check(m.Get()[Motion]==0,"editing blocked during save");
    Snapshot done=s;done.values=r.values;done.sequence=r.sequence;done.result=Result::Saved;m.Observe(done);
    Check(!m.Busy()&&!m.Dirty()&&m.result==Result::Saved,"save acknowledgement resets draft");
    auto changed=done;changed.values[Sky]=1;m.Observe(changed);Check(m.Get()[Sky]==1,"idle panel follows external edits");
    m.Click({Kind::Change,Motion,1});changed.values[Fog]=1;m.Observe(changed);
    Check(m.Get()[Fog]==0&&m.Get()[Motion]==1,"external polling preserves unsaved draft");
    r=m.Click({Kind::Apply});
    Check(!Conflict(changed.values,r.base,r.changed),"unrelated external edits do not conflict");
    changed.values[Motion]=1;Check(Conflict(changed.values,r.base,r.changed),"same-key external edits conflict");
    changed.sequence=r.sequence;changed.result=Result::Conflict;m.Observe(changed);
    Check(!m.Busy()&&!m.Dirty()&&m.Get()[Fog]==1,"conflict reloads current configuration");
    m.Click({Kind::Change,Foveation,-1});r=m.Click({Kind::Apply});
    changed.sequence=r.sequence;changed.result=Result::Failed;m.Observe(changed);
    Check(!m.Busy()&&m.Dirty(),"failed save retains draft for retry");
    Snapshot pending=s;pending.sequence=90;pending.result=Result::Saving;m.Open(pending);
    Check(m.Busy(),"reopening while saving waits for acknowledgement");
    pending.result=Result::Saved;m.Observe(pending);m.Click({Kind::Change,Motion,1});
    Check(m.Click({Kind::Apply}).sequence>90,"new request sequence exceeds observed save sequence");
    m.Open(s);for(int i=0;i<200;++i)m.Click({Kind::Change,Snap,1});
    Check(m.Get()[Snap]==180,"upper limit clamps");
    for(int i=0;i<200;++i)m.Click({Kind::Change,Snap,-1});
    Check(m.Get()[Snap]==15&&Normalize(Waist,INFINITY)==Definitions[Waist].defaultValue,"lower and nonfinite limits");
    s.ready=false;m.Open(s);m.Click({Kind::Change,Motion,1});
    Check(!m.Dirty()&&!m.Click({Kind::Apply}).sequence,"missing config remains read-only");
    InputGate gate;
    auto e=gate.Step(true,true,1,true);Check(!e.chord&&!e.click&&!e.cancel,"initial held controls swallowed");
    gate.Step(true,false,0,false);e=gate.Step(true,true,0,false);Check(e.chord&&!e.click,"fresh chord opens");
    gate.Consume();Check(!gate.Step(true,true,1,false).click,"opening consumes trigger already held");
    gate.Step(true,true,0,false);Check(gate.Step(true,true,.8f,false).click,"fresh press clicks");
    Check(!gate.Step(true,true,.4f,false).click&&!gate.Step(true,true,.8f,false).click,"trigger noise cannot repeat click");
    gate.Step(false,true,1,true);e=gate.Step(true,true,1,true);
    Check(!e.chord&&!e.click&&!e.cancel,"tracking return cannot replay held inputs");
    gate.Step(true,false,0,false);Check(gate.Step(true,false,0,true).cancel,"fresh B cancels");
    std::puts("VR settings draft, persistence requests, conflicts, hit regions and input-gating tests passed");
}
