#include "teleport_arc.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace framevr;
using framevr::math::Vec;
static void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static Vec Tilted(float deg,float yawDeg=0){
    const float t=deg*.0174532925f,y=yawDeg*.0174532925f;
    return {-std::sin(y)*std::cos(t),std::sin(t),-std::cos(y)*std::cos(t)};   // yaw 0: -z ahead
}
int main() {
    namespace ta=teleportarc;
    const Vec hand{0,1.2f,0};const float floor=0,nearM=1,farM=17.6f;
    // level at 1.2 m: sqrt(2 h far) ahead
    const auto level=ta::Compute(hand,Tilted(0),floor,nearM,farM);
    Check(level.ok&&std::fabs(level.distance-std::sqrt(2*1.2f*farM))<.01f,"level: sqrt(2 * height * far) ahead");
    Check(std::fabs(level.land.z+level.distance)<1e-3f&&std::fabs(level.land.x)<1e-4f&&level.land.y==floor,"it lands ahead (-z) on the floor");
    // farther with every degree up to 45, which reaches far (clamped), and no farther past it
    float last=0;
    for(int deg=-70;deg<=45;deg+=5){
        const auto a=ta::Compute(hand,Tilted(float(deg)),floor,nearM,farM);
        Check(a.ok&&a.distance>=last-1e-4f,"never nearer as the controller tilts up");
        last=a.distance;
    }
    Check(std::fabs(last-farM)<1e-3f,"45 degrees up reaches the farthest");
    Check(std::fabs(ta::Compute(hand,Tilted(80),floor,nearM,farM).distance-farM)<1e-3f,"pointing higher still lands at the farthest");
    Check(std::fabs(ta::Compute(hand,Tilted(-89),floor,nearM,farM).distance-nearM)<1e-3f&&
          std::fabs(ta::Compute(hand,Tilted(-70),floor,nearM,farM).distance-nearM)<1e-3f,"pointed at the floor: the nearest");
    const auto mid=ta::Compute(hand,Tilted(10),floor,nearM,farM),down=ta::Compute(hand,Tilted(-20),floor,nearM,farM);
    Check(mid.distance>level.distance&&mid.distance<farM&&down.distance<level.distance&&down.distance>nearM,"10 up between level and far, 20 down between near and level");
    // the arc: from the controller to the landing, never under the floor, rising first when tilted up
    for(const auto* a:{&level,&mid,&down}) {
        Check(a->points[0]==hand,"the arc leaves the controller");
        Check(a->points[ta::Points-1]==a->land,"and ends at the landing");
        for(const auto& p:a->points)Check(p.y>=floor-1e-4f,"never under the floor");
    }
    Check(mid.points[2].y>hand.y,"tilted up, the arc rises first");
    Check(down.points[1].y<hand.y,"tilted down, it falls from the start");
    // the heading is the controller's, level
    const auto right=ta::Compute(hand,Tilted(0,-90),floor,nearM,farM);   // yaw -90: +x
    Check(right.ok&&std::fabs(right.way.x-1)<1e-4f&&std::fabs(right.way.z)<1e-4f&&right.land.x>0,"pointed right, it lands to the right");
    // steep down past the straight line's floor point: a straight line, not an upward bow
    const auto steep=ta::Compute(hand,Tilted(-60),floor,3.f,farM);
    for(int i=1;i<ta::Points;++i)Check(steep.points[size_t(i)].y<=steep.points[size_t(i-1)].y+1e-5f,"a near clamp never bows the arc upward");
    // nonsense: under the floor, straight up or down, bad ranges
    Check(!ta::Compute({0,-.1f,0},Tilted(0),floor,nearM,farM).ok,"a controller under the floor throws nothing");
    Check(!ta::Compute(hand,{0,1,0},floor,nearM,farM).ok&&!ta::Compute(hand,{0,-1,0},floor,nearM,farM).ok,"straight up or down has no heading");
    Check(!ta::Compute(hand,Tilted(0),floor,5,5).ok&&!ta::Compute(hand,{NAN,0,-1},floor,nearM,farM).ok,"bad ranges and directions throw nothing");
    // the board: shown while fresh
    auto& board=ta::Current();board.Place(level.points,1000);
    Check(ta::Shown(board.Get(),1000)&&ta::Shown(board.Get(),1000+ta::ShowMs-1)&&!ta::Shown(board.Get(),1000+ta::ShowMs),"the drawn arc lasts ShowMs");
    board.Clear();Check(!ta::Shown(board.Get(),1000),"cleared, nothing shows");
    std::puts("Teleport arc tests passed");
}
