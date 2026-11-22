// A held weapon's melee contact by its kind, and a flail's reaching its head, each hand by the item in it.
#include "weapon_reach.h"
#include "dual_wield.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b){return std::fabs(a-b)<1e-5f;}
int main() {
    using namespace framevr::reach;
    {   // The five flails, by their padded codes; no other mace, and nothing unpadded.
        for(const char* code:{"fla ","9fl ","7fl ","qf1 ","qf2 "})Check(FlailCode(code),"Flail, Knout, Scourge, Khalim's Flail and Khalim's Will are flails");
        for(const char* code:{"mac ","mst ","9mt ","7mt ","clb ","whm ","fla","9fl","FLA ","","qf3 ","qey "})Check(!FlailCode(code),"other maces, an unpadded code and other quest items are not");
    }
    {   // The capsule by kind, as melee contact always sized it.
        Check(Near(Contact(D2RVR_TYPE_UNARMED,false,false).length,.04f)&&Near(Contact(D2RVR_TYPE_UNARMED,false,false).radius,.08f),"a fist");
        for(uint32_t short_:{D2RVR_TYPE_DAGGER,D2RVR_TYPE_CLAW,D2RVR_TYPE_ORB})Check(Near(Contact(short_,false,false).length,.28f),"daggers, claws and orbs");
        Check(Near(Contact(D2RVR_TYPE_WAND,false,false).length,.38f),"a wand");
        Check(Near(Contact(D2RVR_TYPE_STAFF,true,false).length,1.2f),"a staff");
        Check(Near(Contact(D2RVR_TYPE_SPEAR,true,false).length,1.5f)&&Near(Contact(D2RVR_TYPE_POLEARM,true,false).length,1.5f),"spears and polearms");
        for(uint32_t one:{D2RVR_TYPE_SWORD,D2RVR_TYPE_AXE,D2RVR_TYPE_MACE,D2RVR_TYPE_SCEPTER,D2RVR_TYPE_OTHER})
            Check(Near(Contact(one,false,false).length,.65f)&&Near(Contact(one,false,false).radius,.065f),"one-handed swords, axes, maces and scepters");
        Check(Near(Contact(D2RVR_TYPE_SWORD,true,false).length,1.1f),"a two-handed weapon in both hands");
        // The main hand's kind carries the grip's key bits above the kind (vrcam): it takes the default, as before.
        Check(Near(Contact(D2RVR_TYPE_DAGGER+(0x1234u<<16),false,false).length,.65f),"a kind with key bits sizes as before");
    }
    {   // A flail reaches its head, whatever kind and grip it is read as; its half width is a mace's.
        Check(FlailLength>=.85f&&FlailLength<1.f,"the ball's centre and a little past, 0.76-0.91 m from the grip");
        for(uint32_t kind:{uint32_t(D2RVR_TYPE_MACE),uint32_t(D2RVR_TYPE_MACE+(0x77u<<16)),uint32_t(D2RVR_TYPE_OTHER),0u})
            for(bool both:{false,true})Check(Near(Contact(kind,both,true).length,FlailLength)&&Near(Contact(kind,both,true).radius,.065f),"a flail");
        Check(Contact(D2RVR_TYPE_MACE,false,true).length>Contact(D2RVR_TYPE_MACE,false,false).length+.2f,"past a mace's reach, to the ball");
    }
    {   // Each hand by its own item: the drawn order maps hands to body slots, as the kinds.
        dualwield::Equipment e;e.id[0]=11;e.id[1]=22;e.type[0]=e.type[1]=D2RVR_TYPE_MACE;e.flail[0]=false;e.flail[1]=true;
        Check(!e.Flail(0)&&!e.Flail(1),"no drawn order known yet: neither hand");
        e.displayed[0]=22;e.displayed[1]=11;   // the flail (body slot 1) drawn in the right hand
        Check(e.Flail(0)&&!e.Flail(1),"the flail's hand reaches its head, the mace's hand a mace's");
        e.displayed[0]=11;e.displayed[1]=22;
        Check(!e.Flail(0)&&e.Flail(1),"swapped, the other hand");
        e.flail[0]=true;
        Check(e.Flail(0)&&e.Flail(1),"two flails: both hands");
        Check(!e.Flail(-1)&&!e.Flail(2),"no such hand");
        Check(Near(Contact(e.Type(0),false,e.Flail(0)).length,FlailLength)&&Near(Contact(e.Type(1),false,e.Flail(1)).length,FlailLength),"two flails: each reaches its head");
        e.flail[0]=false;
        Check(Near(Contact(e.Type(0),false,e.Flail(0)).length,.65f)&&Near(Contact(e.Type(1),false,e.Flail(1)).length,FlailLength),
              "a flail and a mace: each hand its own reach");
    }
    std::puts("weapon reach: ok");
    return 0;
}
