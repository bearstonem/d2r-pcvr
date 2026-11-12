// Weapon heft (#443): how heavy each weapon swings, the hand travel it needs for a full hit, the
// share a shorter stroke lands, and the melee engine's measure of a stroke's committed hand travel.
#include "weapon_heft.h"
#include "melee_contact.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;using namespace framevr::melee;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b,float e=.0001f){return std::fabs(a-b)<e;}
// A blade held in the hand, its tip .7 m out along the hand's yaw.
struct Swing {
    Engine engine;Sample sample;Sweep sweep;float yaw=0;
    Swing(){sample.minWeaponSpeed=4;sample.active=true;sample.epoch=1;sample.equipment=1;sample.unitsPerMetre=4;
        sample.hand.p={0,1,-.4f};Pose();for(int i=0;i<8;++i)Step({},0);}
    void Pose(){sample.hand.q=math::Yaw(yaw);sample.blade={sample.hand.p,sample.hand.p+math::Rotate(sample.hand.q,{0,0,-.7f}),.04f};
        sample.thrustAxis=math::Rotate(sample.hand.q,{0,0,-1});}
    bool Step(Vec move,float turn){sample.time+=.02;sample.hand.p=sample.hand.p+move;yaw+=turn;Pose();return engine.Step(sample,sweep);}
    void Rest(){for(int i=0;i<8;++i)Step({},0);}
};
int main(){
    using namespace framevr::heft;
    {   // Weight: the kind, the two-handed grip and the speed (WSM).
        for(uint32_t light:{D2RVR_TYPE_UNARMED,D2RVR_TYPE_DAGGER,D2RVR_TYPE_CLAW,D2RVR_TYPE_WAND,D2RVR_TYPE_ORB,D2RVR_TYPE_THROWING,
                            D2RVR_TYPE_JAVELIN,D2RVR_TYPE_BOW,D2RVR_TYPE_CROSSBOW,D2RVR_TYPE_UNKNOWN})
            for(int wsm:{-20,0,20})Check(Weight(light,false,wsm)==0&&Weight(light,true,wsm)==0,"daggers, claws, wands, orbs, thrown weapons and fists weigh nothing");
        Check(Near(Weight(D2RVR_TYPE_SWORD,false,0),.40f),"a one-handed sword");
        Check(Near(Weight(D2RVR_TYPE_SWORD,false,-10),.30f),"a long sword (WSM -10) is lighter");
        Check(Near(Weight(D2RVR_TYPE_MACE,true,20),1.f),"a great maul (two-handed, WSM 20) is the heaviest");
        Check(Near(Weight(D2RVR_TYPE_AXE,true,0),.80f)&&Near(Weight(D2RVR_TYPE_SWORD,true,0),.80f),"two-handed swords and axes");
        Check(Near(Weight(D2RVR_TYPE_SPEAR,false,-10),.60f)&&Near(Weight(D2RVR_TYPE_POLEARM,true,0),.70f)&&Near(Weight(D2RVR_TYPE_STAFF,true,20),.90f),
              "spears, polearms and staves are two-handed whatever the flag says");
        Check(Near(Weight(D2RVR_TYPE_SWORD,false,50),.60f)&&Near(Weight(D2RVR_TYPE_SWORD,false,-90),.20f),"WSM counts at most 20 either way");
        Check(Weight(D2RVR_TYPE_SCEPTER,false,0)>0&&Weight(D2RVR_TYPE_OTHER,false,0)>0,"scepters and other melee kinds have weight");
    }
    {   // Need and Factor.
        Check(Near(Need(1),FullStrokeM)&&Near(Need(.3f),.12f)&&Need(0)==0&&Need(NAN)==0&&Near(Need(4),FullStrokeM),"the stroke a weight needs");
        Check(Factor(.5f,.4f)==1&&Factor(.4f,.4f)==1,"a stroke as long as needed, or longer, is whole");
        Check(Near(Factor(.2f,.4f),.5f),"half the stroke, half the hit");
        Check(Near(Factor(0,.4f),Floor)&&Near(Factor(.05f,.4f),Floor),"a flick still lands its floor share");
        Check(Factor(0,0)==1&&Factor(.01f,0)==1,"a weapon with no weight is always whole");
        Check(Factor(NAN,.4f)==1&&Factor(-1,.4f)==1&&Factor(.1f,NAN)==1,"unreadable values never penalise");
    }
    {   // The engine's committed travel: brisk hand travel since the path began.
        Swing s;bool hit=false;
        for(int i=0;i<10&&!hit;++i)hit=s.Step({.1f,0,0},0);   // 5 m/s at the grip
        Check(hit&&s.sweep.travel>.10f,"a committed stroke carries its hand travel");
        float travel=s.sweep.travel;
        for(int i=0;i<4;++i)if(s.Step({.1f,0,0},0)){Check(s.sweep.travel>travel,"the travel grows along the stroke");travel=s.sweep.travel;}
        Check(Near(travel,s.sweep.travel)&&travel>.30f,"a long stroke passes a heavy weapon's need");
        s.Rest();
        // A flick: the wrist turns the blade fast while the hand barely moves.
        hit=false;for(int i=0;i<10&&!hit;++i)hit=s.Step({.004f,0,0},.15f);
        Check(hit,"a wrist flick is a stroke at this swing speed");
        Check(s.sweep.travel<.01f,"a flick carries almost no hand travel");
        Check(Near(Factor(s.sweep.travel,Need(Weight(D2RVR_TYPE_MACE,true,20))),Floor)&&
              Factor(s.sweep.travel,Need(Weight(D2RVR_TYPE_DAGGER,false,0)))==1,"a maul's flick lands its floor share; a dagger's lands whole");
        s.Rest();
        // A slow drift (under 1 m/s, above the engine's stillness) before the flick adds nothing.
        for(int i=0;i<20;++i)s.Step({.012f,0,0},0);   // 0.6 m/s for 0.4 s: 24 cm of drift
        hit=false;for(int i=0;i<10&&!hit;++i)hit=s.Step({.004f,0,0},.15f);
        Check(hit&&s.sweep.travel<.01f,"drifting the hand slowly first does not count as a committed stroke");
        s.Rest();
        // A reversal starts a new path: the backswing is not the strike's travel.
        for(int i=0;i<8;++i)s.Step({.1f,0,0},0);
        uint64_t first=s.sweep.stroke;hit=false;
        for(int i=0;i<12&&!hit;++i)if(s.Step({-.1f,0,0},0)&&s.sweep.stroke!=first)hit=true;
        Check(hit&&s.sweep.travel<.30f,"after a reversal only the new stroke's travel counts");
    }
    std::puts("weapon-heft-test: ok");
    return 0;
}
