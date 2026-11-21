#include "entry_facing.h"
#include <cstdlib>
#include <initializer_list>
#include <cstdio>
using namespace framevr::entryfacing;
void Check(bool ok){if(!ok){std::puts("entry facing check failed");std::exit(1);}}
int main(){
    State s;float h=0;
    Check(!s.Observe(1,2,0));Check(!s.Observe(1,0,10));
    Check(s.Observe(1,8,100));
    Check(!s.Choose(100,42,100,100,98,100,h));
    Check(s.Choose(180,42,100,100,98,100,h));Check(std::abs(h+90)<.01f);
    Check(!s.Choose(200,42,100,100,98,100,h));Check(!s.Observe(1,8,200));
    for(float base:{-170.f,0.f,135.f})for(float yaw:{-180.f,30.f,179.f}){
        const float turn=TurnDelta(base,yaw,h);
        Check(std::abs(std::remainder(base-yaw-turn-h,360.f))<.001f);
    }
    Check(s.Observe(1,9,300));Check(!s.Choose(300,43,10,10,10,10,h));
    Check(!s.Choose(310,43,10,10,10,8,h));Check(!s.Choose(400,43,11,10,10,8,h));
    Check(!s.Observe(1,9,410));
    Check(s.Observe(1,10,500));Check(!s.Choose(2100,44,10,10,8,10,h));
    Check(!s.Observe(2,10,2200));Check(!s.Observe(0,0,2300));
    std::puts("Entry facing transition, one-shot, movement, timeout and yaw checks passed");
}
