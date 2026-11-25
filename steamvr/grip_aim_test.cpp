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
 for(int skill:{54,132,143,151,390})for(float slope:{-1.f,-.2f}){
    float x=0,y=0;
    assert(gripaim::Delta({200,4,200},{0,slope,-1},0,100,100,x,y));
    spellaim::Cast cast;
    assert(gripaim::CastAt(skill,2,true,100+x,100+y,cast));
    float tx=0,ty=0;assert(spellaim::Resolve(cast,100,100,tx,ty));
    assert(tx==100+x&&ty==100+y);
    int ix=0,iy=0;assert(spellaim::ControllerPoint(cast,100,100,ix,iy));
    spellaim::NativeCommand command{};command.actor=123;command.target=456;
    assert(spellaim::ApplyCommand({123,ix,iy},command));assert(command.target==0&&command.x==ix&&command.y==iy);
    assert(slope== -1.f?ty>95:ty<95);
 }
 for(int skill:{54,132,143,151,390}){
 spellaim::Cast unchanged{true,1,0,false};
 assert(!gripaim::CastAt(skill,0,true,100,101,unchanged));
 assert(!gripaim::CastAt(skill,1,false,100,101,unchanged));
 assert(!gripaim::CastAt(53,1,true,100,101,unchanged));
 assert(!unchanged.contactPoint&&unchanged.x==1&&unchanged.y==0);
 }
 // Enemy-dependent movement and other Warlock skills retain existing targeting.
 for(int skill:{107,275,389,391,402}){
    spellaim::Cast cast{};
    assert(!gripaim::Supported(skill)&&!gripaim::CastAt(skill,1,true,120,120,cast));
 }
 auto edge=gripaim::Backtrack(100,100,145,100,[](int x,int){return x<=120;});
 assert(edge.known&&edge.found&&edge.x==120&&edge.y==100);
 auto exact=gripaim::Backtrack(100,100,120,100,[](int,int){return true;});
 assert(exact.found&&exact.x==120);
 auto wall=gripaim::Backtrack(100,100,140,100,[](int x,int){return x<110||(x>120&&x<=135);});
 assert(wall.found&&wall.x==135); // can cross walls, never land inside them
 auto none=gripaim::Backtrack(100,100,145,100,[](int,int){return false;});
 assert(none.known&&!none.found&&none.x==100&&none.y==100);
 auto diagonal=gripaim::Backtrack(100,100,145,145,[](int x,int y){return x<=125&&y<=125;});
 assert(diagonal.found&&diagonal.x==125&&diagonal.y==125);
 auto reverse=gripaim::Backtrack(100,100,55,100,[](int x,int){return x>=80;});
 assert(reverse.found&&reverse.x==80);
 assert(!gripaim::Backtrack(100,100,NAN,100,[](int,int){return true;}).found);
 float cx=123.99f,cy=456.75f;assert(gripaim::Cell(cx,cy)&&cx==123&&cy==456);
 cx=NAN;assert(!gripaim::Cell(cx,cy));
 telegraph::Board b;b.Place(1,2,100,250,5,.65f,0,0,0,telegraph::GripRightMark);b.Place(3,4,100,250,5,.65f,0,0,0,telegraph::GripLeftMark);
 b.ClearSkill(telegraph::GripRightMark);assert(!telegraph::SkillOn(b.Get(),telegraph::GripRightMark,110));assert(telegraph::SkillOn(b.Get(),telegraph::GripLeftMark,110));
 std::puts("Grip selection, stale input, landing range, defaults and independent markers passed");
}
