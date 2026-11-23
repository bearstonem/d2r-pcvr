#include "grip_aim.h"
#include "telegraph.h"
#include "vr_settings.h"
#include <cassert>
#include <cstdio>
int main(){
 using namespace framevr;
 assert(settings::Defaults()[settings::GripAimIndicator]==0);
 assert(settings::Normalize(settings::GripAimIndicator,1.5f)==0);
 for(int m=0;m<4;++m){assert(gripaim::Selected(m,0)==bool(m&2));assert(gripaim::Selected(m,1)==bool(m&1));}
 assert(gripaim::Active(3,100,200)==3);assert(!gripaim::Active(3,100,350));assert(!gripaim::Active(3,200,100));
 float dx=0,dy=0;
 assert(gripaim::Delta({0,2,0},{0,-1,-1},0,0,0,dx,dy));assert(dx==0&&dy<0);
 assert(gripaim::Delta({0,2,0},{1,0,1},0,0,0,dx,dy));assert(std::abs(dx)<=aimreach::GameCells&&std::abs(dy)<=aimreach::GameCells);
 assert(!gripaim::Delta({0,2,0},{0,0,0},0,0,0,dx,dy));
 telegraph::Board b;b.Place(1,2,100,250,5,.65f,0,0,0,telegraph::GripRightMark);b.Place(3,4,100,250,5,.65f,0,0,0,telegraph::GripLeftMark);
 b.ClearSkill(telegraph::GripRightMark);assert(!telegraph::SkillOn(b.Get(),telegraph::GripRightMark,110));assert(telegraph::SkillOn(b.Get(),telegraph::GripLeftMark,110));
 std::puts("Grip selection, stale input, landing range, defaults and independent markers passed");
}
