#include "entry_facing.h"
#include <cstdlib>
#include <initializer_list>
#include <cstdio>
using namespace framevr::entryfacing;
void Check(bool ok){if(!ok){std::puts("entry facing check failed");std::exit(1);}}
int main(){
    State s;float h=0;
    Check(!s.Observe(1,2,0));Check(!s.Observe(1,0,10));
    Check(s.Observe(1,8,100));Check(!s.Observe(1,8,100));Check(!s.Observe(1,8,180));
    Check(s.Observe(1,2,300));Check(!s.Observe(1,2,1800));
    Check(!s.Observe(2,8,2000));Check(!s.Observe(0,0,2100));
    Check(Direction(100,100,98,100,[](int,int){return true;},h));Check(std::abs(h+90)<.01f);
    // A farther obstacle makes a diagonal ray longer, but the doorway's
    // forward direction is already clear. Neither side should pull the view.
    for(int side:{-1,1}) {
        Check(Direction(100,100,98,100,[&](int x,int y){return x<104||(y-100)*side>0;},h));
        Check(std::abs(h+90)<.01f);
    }
    // Cave spawn exactly on the warp: west is a wall, east leads inside.
    Check(Direction(100,100,100,100,[](int x,int y){return x>=100&&y>=99&&y<=101;},h));Check(std::abs(h+90)<.01f);
    // Leaving a cave: the entrance to the east must be behind the view.
    Check(Direction(100,100,102,100,[](int x,int){return x<=100;},h));Check(std::abs(h-90)<.01f);
    Check(!Direction(100,100,100,100,[](int,int){return false;},h));
    // Jail landings vary sideways and along the stairs. The native doorway
    // direction must stay +Y even when the spawn-to-tile vector is diagonal.
    for(uint32_t warp:{13u,14u})for(float x:{99.5f,100.f,100.5f})for(float y:{100.8f,102.f,104.f}) {
        float direction=0;
        Check(NativeExitHeading(warp,direction)||Direction(x,y,100,100,[](int,int){return true;},direction));
        const float radians=direction*.0174532925f;
        Check(std::abs(std::sin(radians))<.001f&&-std::cos(radians)>.999f);
    }
    Check(!NativeExitHeading(57,h)); // Other entrances retain their existing resolver.
    for(float base:{-170.f,0.f,135.f})for(float yaw:{-180.f,30.f,179.f}){
        const float turn=TurnDelta(base,yaw,h);
        Check(std::abs(std::remainder(base-yaw-turn-h,360.f))<.001f);
    }
    std::puts("First-frame entry/exit, overlapping cave warp, blocked geometry, no delayed turn and yaw checks passed");
}
