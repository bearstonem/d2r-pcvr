#include "client_swing_feedback.h"
#include <cassert>
#include <cstdio>
using namespace framevr;
int main(){
 melee::Sample sample{};sample.active=true;sample.unitsPerMetre=4;sample.player={0,0,0};sample.handIndex=0;
 melee::Sweep sweep{};sweep.stroke=1;sweep.before={{-.3f,.2f,0},{-.3f,.9f,0},.08f};sweep.after={{.3f,.2f,0},{.3f,.9f,0},.08f};
 targetlock::Snapshot targets{};targets.at=1000;targets.count=1;auto& target=targets.targets[0];
 target.id=42;target.clear=true;target.x=target.y=0;target.bodyWidth=2;target.bodyHeight=40;
 assert(clientcontact::Touched(sample,sweep,targets,1050)==42);
 assert(clientcontact::Touched(sample,sweep,targets,1251)==0);
 assert(clientcontact::Touched(sample,sweep,targets,999)==0);
 target.clear=false;assert(clientcontact::Touched(sample,sweep,targets,1050)==0);target.clear=true;
 sample.active=false;assert(clientcontact::Touched(sample,sweep,targets,1050)==0);sample.active=true;
 target.x=100;assert(clientcontact::Touched(sample,sweep,targets,1050)==0);target.x=0;
 targets.heroX=100;assert(clientcontact::Touched(sample,sweep,targets,1050)==0);targets.heroX=0;
 sweep.stroke=0;assert(clientcontact::Touched(sample,sweep,targets,1050)==0);
 puts("client swing feedback: same dispatched sweep, immediate contact, empty swing, stale/obstructed/travel guards passed");
}
