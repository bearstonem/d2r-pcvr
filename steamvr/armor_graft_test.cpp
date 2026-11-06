#include "armor_graft.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
    interaction::Snapshot s;s.session=7;s.at=5000;s.effectsAt=5000;
    s.skillClass=1;s.actionKnown=true;s.actionMode=1;
    auto select=[&](bool bone=true,bool frozen=true,bool models=true,uint64_t session=7,uint64_t now=5000){
        return armorgraft::Select(s,session,now,models,bone,frozen);};
    Check(select().source==armorgraft::Source::None,"a learned/selected skill without native state shows no armor");
    s.effects.known=true;s.effects.Set(10);
    Check(select().source==armorgraft::Source::Frozen&&select().layers==3,"active Sorceress Frozen Armor has three pieces per arm");
    Check(select().layers==3&&select().layers==3,"repeated active samples keep all pieces; no invented timer");
    Check(select(false).layers==3,"Frozen preview independent of Necromancer graft toggle");
    Check(select(true,false).layers==0,"Frozen toggle disables preview");
    Check(select(true,true,false).layers==0,"missing models hides preview");
    Check(select(true,true,true,8).layers==0,"another session cannot use old armor");
    Check(select(true,true,true,7,5651).layers==0,"stale UI snapshot hides preview");
    s.effectsAt=3999;Check(select().layers==0,"new UI snapshot does not renew stale effect state");
    s.effectsAt=5001;Check(select().layers==0,"future effect sample rejected");
    s.effectsAt=0;Check(select().layers==0,"missing effect timestamp rejected");s.effectsAt=5000;
    s.skillClass=2;Check(select().layers==0,"other classes do not enter Frozen preview");s.skillClass=1;
    s.actionMode=0;Check(select().layers==0,"death removes Frozen preview");
    s.actionMode=17;Check(select().layers==0,"dead mode removes Frozen preview");
    s.actionMode=1;s.actionKnown=false;Check(select().layers==0,"unknown actor state hides Frozen preview");s.actionKnown=true;
    for(int state:{10,88,20}){
        s.effects={};s.effects.known=true;s.effects.Set(state);
        Check(select().source==armorgraft::Source::Frozen&&select().layers==3,"each native ice armor independently enables three crystals");
        Check(select(true,false).layers==0,"existing ice armor toggle disables every variant");
        s.effectsAt=3999;Check(select().layers==0,"each armor rejects a stale game-thread sample");s.effectsAt=5000;
        s.actionMode=17;Check(select().layers==0,"each armor disappears on death");s.actionMode=1;
        s.skillClass=2;Check(select().layers==0,"each armor keeps the Sorceress-only gate");s.skillClass=1;
    }
    s.effects={};s.effects.known=true;s.effects.Set(1);s.effects.Set(11);
    Check(select().layers==0,"frozen and chilled debuffs do not masquerade as armor");
    s.effects={};s.effects.known=true;
    Check(select().layers==0,"all armor states removed clears crystals");
    s.effects.Set(10);s.effects.known=false;Check(select().layers==0,"unavailable native state hides preview");
    s.effects.known=true;s.boneArmor=100;s.boneArmorMax=300;
    Check(select().source==armorgraft::Source::Bone&&select().layers==2,"native Bone Armor retains priority and original pool thresholds");
    s.skillClass=2;s.effects={};s.effectsAt=0;
    Check(select(true,false).source==armorgraft::Source::Bone&&select(true,false).layers==2,"Necromancer path does not depend on Frozen state/timestamp/toggle");
    s.boneArmor=1;Check(select().layers==1,"last native Bone Armor layer preserved");
    s.boneArmor=0;Check(select().layers==0,"depleted Bone Armor has no pieces");
    std::puts("Armor graft preview: all checks passed.");
}
