#include "../vr/multiplayer_fields.h"
#include <cassert>
int main(){using namespace mpfields; Box b[7]{}; b[0]={true,true,10,20,100,25}; b[1]={true,false,130,20,40,25};
 assert(Pick(b,10,20)==0 && Pick(b,110,20)==-1 && Pick(b,129,20)==-1 && Pick(b,135,24)==1);
 b[4]={true,false,30,30,40,10}; assert(Pick(b,10,20)==-1 && Pick(b,31,31)==4 && Active(b)==-1);
 b[4].focused=true;assert(Active(b)==4); b[4].available=false;assert(Active(b)==0);
 D2RL::WidgetService old{};old.serviceSize=sizeof(old);old.serviceVersion=1;assert(!Api(&old).ready());assert(!Api(nullptr).ready());}
