// Hit reactions: a stroke's force, what each rank of monster may do, each monster's waits, and the
// damage record's result flags with the reaction asked for.
#include "hit_reactions.h"
#include "weapon_heft.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;using namespace framevr::hitreact;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b,float e=.0001f){return std::fabs(a-b)<e;}
int main(){
    {   // Force: commitment carried by weight, scaled by the damage share.
        Check(Near(Force(FullStrokeM,0,1),.5f),"a committed dagger or fist stroke just staggers");
        Check(Force(FullStrokeM*.9f,0,1)<StaggerForce,"a shorter light stroke does not");
        Check(Near(Force(1.f,.4f,1),.7f),"a one-handed sword's full stroke: .7, a stagger and no knockback");
        Check(Force(1.f,.4f,1)<KnockbackForce&&Force(1.f,.6f,1)>=KnockbackForce,"knockback needs a weight of .6");
        Check(Force(FullStrokeM,heft::Weight(D2RVR_TYPE_MACE,true,20),1)>=KnockbackForce,"a great maul's full stroke knocks back");
        Check(Force(FullStrokeM,heft::Weight(D2RVR_TYPE_SPEAR,false,0),1)>=KnockbackForce,"a spear's too");
        Check(Force(FullStrokeM*.5f,1,1)<KnockbackForce&&Force(FullStrokeM*.5f,1,1)>=StaggerForce,"half a maul stroke only staggers");
        Check(Force(1,1,.5f)<KnockbackForce,"a half-recovered stroke loses its knockback");
        Check(Force(1,1,.4f)<StaggerForce,"and a weak share its stagger");
        Check(Force(NAN,1,1)==0&&Force(1,NAN,1)==0&&Force(1,1,NAN)==0&&Force(-1,1,1)==0,"unreadable strokes have no force");
        Check(Near(Force(9,9,9),1),"never above 1");
    }
    {   // Rank.
        Check(Allowed(Rank::PrimeEvil)==None,"act bosses never react");
        Check(Allowed(Rank::Unique)==Stagger&&Allowed(Rank::Champion)==Stagger,"uniques and champions stand their ground");
        Check(Allowed(Rank::Normal)==(Stagger|Knockback)&&Allowed(Rank::Minion)==(Stagger|Knockback),"normal monsters and minions do both");
    }
    {   // The board: what a stroke asks, and each monster's waits.
        Board b;const uint8_t both=Stagger|Knockback;
        Check(b.Judge(1,7,1000,1,both,Rank::Normal)==Knockback,"a heavy stroke asks for the knockback");
        Check(b.Judge(1,7,1000,.7f,both,Rank::Normal)==Stagger,"a lighter one for the stagger");
        Check(b.Judge(1,7,1000,.49f,both,Rank::Normal)==None,"a flick for nothing");
        Check(b.Judge(1,7,1000,1,Stagger,Rank::Normal)==Stagger,"knockback switched off: the stagger");
        Check(b.Judge(1,7,1000,1,Knockback,Rank::Normal)==Knockback&&b.Judge(1,7,1000,.7f,Knockback,Rank::Normal)==None,"stagger switched off: only knockbacks");
        Check(b.Judge(1,7,1000,1,None,Rank::Normal)==None,"both off");
        Check(b.Judge(1,7,1000,1,both,Rank::Unique)==Stagger&&b.Judge(1,7,1000,1,Knockback,Rank::Unique)==None,"a unique only staggers");
        Check(b.Judge(1,7,1000,1,both,Rank::PrimeEvil)==None,"an act boss does nothing");
        b.Landed(1,7,1000,Knockback);
        Check(b.Judge(1,7,1000+StaggerMs-1,1,both,Rank::Normal)==None,"knocked back: nothing until the stagger's wait is over");
        Check(b.Judge(1,7,1000+StaggerMs,1,both,Rank::Normal)==Stagger,"then a stagger, the knockback still waiting");
        Check(b.Judge(1,7,1000+KnockbackMs,1,both,Rank::Normal)==Knockback,"then the knockback again");
        Check(b.Judge(1,8,1001,1,both,Rank::Normal)==Knockback,"another monster has its own waits");
        b.Landed(1,8,2000,Stagger);
        Check(b.Judge(1,8,2100,1,both,Rank::Normal)==Knockback,"a stagger does not delay a knockback");
        Check(b.Judge(1,8,2100,.7f,both,Rank::Normal)==None,"but delays the next stagger");
        b.Landed(1,8,2100,None);
        Check(b.Judge(1,8,2000+StaggerMs,.7f,both,Rank::Normal)==Stagger,"no reaction starts no wait");
        Check(b.Judge(2,7,1001,1,both,Rank::Normal)==Knockback,"a new game forgets");
        b.Landed(2,9,5000,Stagger);
        Check(b.Judge(1,7,5001,1,both,Rank::Normal)==Knockback,"and the old game's waits are gone");
        for(uint32_t i=0;i<40;++i)b.Landed(2,100+i,6000,Knockback);
        Check(b.Judge(2,139,6001,1,both,Rank::Normal)==None&&b.Judge(2,100,6001,1,both,Rank::Normal)==Knockback,"32 monsters kept, the oldest replaced");
    }
    {   // The record's flags.
        Check(Flags(0x1,None)==0x1,"nothing asked, nothing changed");
        Check(Flags(0x1|FlagSoftHit,Stagger)==(0x1|FlagGetHit),"a stagger replaces the soft hit");
        Check(Flags(0x1|FlagGetHit,Stagger)==(0x1|FlagGetHit),"the game's own stagger stays");
        Check(Flags(0x1|FlagKnockback,Stagger)==(0x1|FlagKnockback),"the game's own knockback stays");
        Check(Flags(0x1|FlagSoftHit,Knockback)==(0x1|FlagKnockback),"a knockback replaces the soft hit");
        Check(Flags(0x1|FlagGetHit,Knockback)==(0x1|FlagGetHit|FlagKnockback),"and joins the game's stagger");
        Check(Flags(0x1|FlagDies,Knockback)==(0x1|FlagDies)&&Flags(0x1|FlagDies|FlagSoftHit,Stagger)==(0x1|FlagDies|FlagSoftHit),"a dying monster's record is left alone");
    }
    std::puts("hit reactions: ok");
    return 0;
}
