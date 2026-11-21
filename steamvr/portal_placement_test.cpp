#include "portal_placement.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace framevr::portalplacement;
void Check(bool v,const char* msg){if(!v){std::puts(msg);std::exit(1);}}
int main(){
    Point out;uintptr_t room=0;
    auto free=[](Point seed,Point& result){result=seed;return uintptr_t(1);};
    auto clear=[](Point){return true;};
    for(Point forward:{Point{1,0},Point{-1,0},Point{0,1},Point{0,-1},Point{1,1}}){
        Check(Choose(100,100,float(forward.x),float(forward.y),free,clear,out,room),"open ground needs a position");
        const float n=std::hypot(float(forward.x),float(forward.y));
        Check(InFront(out,100,100,forward.x/n,forward.y/n),"portal must be ahead for every heading");
    }
    int attempts=0;
    Check(Choose(100,100,1,0,[&](Point p,Point& r){++attempts;if(attempts==1)return uintptr_t(0);r=p;return uintptr_t(1);},clear,out,room)&&attempts==2,"try nearby forward space if center blocked");
    Check(!Choose(100,100,1,0,[](Point,Point& r){r={97,100};return uintptr_t(1);},clear,out,room),"reject native search relocating behind player");
    Check(!Choose(100,100,1,0,free,[](Point){return false;},out,room),"reject space across a blocked corridor");
    Check(!Choose(100,100,1,0,[](Point,Point&){return uintptr_t(0);},clear,out,room),"blocked candidates leave native fallback");
    Check(!Choose(100,100,0,0,free,clear,out,room),"invalid heading must not guess");
    Check(!Choose(100,100,std::numeric_limits<float>::quiet_NaN(),0,free,clear,out,room),"nonfinite heading refused");
    Check(!Choose(0,100,1,0,free,clear,out,room),"invalid location refused");
    std::puts("Portal placement headings, collision fallback, native displacement and invalid input checks passed");
}
