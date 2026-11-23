#include "grip_aim.h"
#include "telegraph.h"
#include "vr_settings.h"
#include <cassert>
#include <cstdio>
int main(){
 using namespace framevr;
 assert(settings::Defaults()[settings::TeleportSkillAiming]==0);
 assert(settings::Normalize(settings::TeleportSkillAiming,1.5f)==0);
 for(int m=0;m<4;++m){assert(gripaim::Selected(m,0)==(m==2));assert(gripaim::Selected(m,1)==(m==1));}
 assert(settings::Normalize(settings::TeleportSkillAiming,3)==0);
 assert(gripaim::Active(3,100,200)==3);assert(!gripaim::Active(3,100,350));assert(!gripaim::Active(3,200,100));
 float dx=0,dy=0;
 assert(gripaim::Delta({0,2,0},{0,-1,-1},0,0,0,dx,dy));assert(dx==0&&dy<0);
 assert(gripaim::Delta({0,2,0},{1,0,1},0,0,0,dx,dy));assert(std::abs(dx)<=aimreach::GameCells&&std::abs(dy)<=aimreach::GameCells);
 assert(!gripaim::Delta({0,2,0},{0,0,0},0,0,0,dx,dy));
 // Exact near/far destinations survive both native resolution and controller commands.
 for(float slope:{-1.f,-.2f}){
    float x=0,y=0;
    assert(gripaim::Delta({200,4,200},{0,slope,-1},0,100,100,x,y));
    spellaim::Cast cast;
    assert(gripaim::CastAt(54,2,true,100+x,100+y,cast));
    float tx=0,ty=0;assert(spellaim::Resolve(cast,100,100,tx,ty));
    assert(tx==100+x&&ty==100+y);
    int ix=0,iy=0;assert(spellaim::ControllerPoint(cast,100,100,ix,iy));
    spellaim::NativeCommand command{};command.actor=123;command.target=456;
    assert(spellaim::ApplyCommand({123,ix,iy},command));assert(command.target==0&&command.x==ix&&command.y==iy);
    assert(slope== -1.f?ty>95:ty<95);
 }
 spellaim::Cast unchanged{true,1,0,false};
 assert(!gripaim::CastAt(54,0,true,100,101,unchanged));
 assert(!gripaim::CastAt(54,1,false,100,101,unchanged));
 assert(!gripaim::CastAt(53,1,true,100,101,unchanged));
 assert(!unchanged.contactPoint&&unchanged.x==1&&unchanged.y==0);
 using gripaim::Landing;
 assert(gripaim::Terrain(false,0)==Landing::Blocked);
 assert(gripaim::Terrain(true,1)==Landing::Blocked);
 assert(gripaim::Terrain(true,0)==Landing::Clear);
 gripaim::LandingSample sample{7,100,123,456,Landing::Blocked};
 assert(sample.For(7,123,456,200)==Landing::Blocked);
 assert(sample.For(8,123,456,200)==Landing::Unknown);
 assert(sample.For(7,124,456,200)==Landing::Unknown);
 assert(sample.For(7,123,456,350)==Landing::Unknown);
 assert(sample.For(7,123,456,99)==Landing::Unknown);
 assert(gripaim::Element(Landing::Blocked)==4&&gripaim::Element(Landing::Unknown)==2&&gripaim::Element(Landing::Clear)==5);
 float cx=123.99f,cy=456.75f;assert(gripaim::Cell(cx,cy)&&cx==123&&cy==456);
 cx=NAN;assert(!gripaim::Cell(cx,cy));
 telegraph::Board b;b.Place(1,2,100,250,5,.65f,0,0,0,telegraph::GripRightMark);b.Place(3,4,100,250,5,.65f,0,0,0,telegraph::GripLeftMark);
 b.ClearSkill(telegraph::GripRightMark);assert(!telegraph::SkillOn(b.Get(),telegraph::GripRightMark,110));assert(telegraph::SkillOn(b.Get(),telegraph::GripLeftMark,110));
 std::puts("Grip selection, stale input, landing range, defaults and independent markers passed");
}
