#include "client_attack_timing.h"
#include <cassert>
#include <cstdio>
using namespace framevr::clienttiming;
namespace skills=framevr::skills;
int main(){
 Probe p;
 assert(p.Pulse(7,true,0,0,100));
 assert(!p.Pulse(7,true,0,0,120));
 assert(!p.Submit(0,false,130));
 assert(p.Submit(skills::Buttons(0),false,140));
 assert(!p.Submit(skills::Buttons(0),false,160));
 auto hit=p.Confirm(7,200);assert(hit.valid&&hit.pulses==1&&hit.toPad==40&&hit.toFeedback==100);
 assert(!p.Confirm(7,201).valid);
 p.Pulse(7,false,-1,-1,210);assert(p.Pulse(7,true,1,4,250));
 assert(!p.Submit(skills::Buttons(4),false,251));
 assert(p.Submit(skills::Buttons(4),true,310));
 p.Pulse(7,false,-1,-1,450);assert(p.Pulse(7,true,1,4,500));
 hit=p.Confirm(7,600);assert(hit.valid&&hit.pulses==2&&!hit.padKnown); // ambiguous attribution labelled
 assert(!p.Pulse(0,true,0,0,700));assert(!p.Confirm(7,701).valid);
 assert(p.Pulse(8,true,0,0,800));assert(!p.Confirm(7,810).valid);
 assert(!p.Confirm(8,1801).valid); // never retain indefinitely
 assert(p.Pulse(9,true,0,0,1900));assert(!p.Confirm(9,1800).valid);
 puts("client timing: first edge, native pad bank, confirmation, ambiguity, expiry and reset passed");
}
