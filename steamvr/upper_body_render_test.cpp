#include "../vr/upper_body_render.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
 using namespace upperbodyrender;
 Candidate c[2]={{7,3,{20,0,20}},{8,3,{30,0,30}}};float rig[3]={20,0,20},local[3]={0,0,0};
 assert(Match(c,2,3,rig,local)==0);assert(Match(c,2,2,rig,local)==-1);
 assert(Match(c,2,3,rig,rig)==-1);c[1].position[0]=20;c[1].position[2]=20;
 assert(Match(c,2,3,rig,local)==-1);
 const float pivot[3]={2,3,4};
 for(float nativeYaw:{0.f,1.5707963f,3.1415927f,-1.5707963f})for(float facing:{0.f,.7f,-2.f}){
  float m[16];FacingCorrection(nativeYaw,facing,pivot,m);
  float fx=std::sin(nativeYaw),fz=std::cos(nativeYaw);
  assert(std::fabs(fx*m[0]+fz*m[8]-std::sin(facing))<1e-5f);
  assert(std::fabs(fx*m[2]+fz*m[10]-std::cos(facing))<1e-5f);
  assert(std::fabs(pivot[0]*m[0]+pivot[2]*m[8]+m[12]-pivot[0])<1e-5f);
  assert(std::fabs(pivot[0]*m[2]+pivot[2]*m[10]+m[14]-pivot[2])<1e-5f);
  assert(m[5]==1&&m[13]==0); // gait height is unchanged
  assert(std::fabs(m[0]*m[0]+m[2]*m[2]-1)<1e-5f); // no stretch
 }
 const float eye[3]={1,6,-2};float bone[16]={-1,0,0,0,0,2,0,0,0,0,1,0,2,5,1,1};
 float packed[12],shown[16];
 assert(PackFrame(bone,eye,4,0,packed));assert(std::fabs(packed[9]-.25f)<1e-6f);
 for(float yaw:{0.f,.7f,3.1415927f}){
  assert(PackFrame(bone,eye,4,yaw,packed));assert(UnpackFrame(packed,eye,4,yaw,shown));
  for(int i=0;i<16;++i)assert(std::fabs(shown[i]-bone[i])<1e-5f);
 }
 assert(PackFrame(bone,eye,4,0,packed));
 const float remoteEye[3]={0,3,0};assert(UnpackFrame(packed,remoteEye,2,0,shown));
 assert(shown[0]==-1&&shown[5]==2); // mirror and weapon size survive
 assert(std::fabs(shown[12]-.5f)<1e-6f&&std::fabs(shown[13]-2.5f)<1e-6f);
 assert(!PackFrame(bone,eye,0,0,packed));
 puts("upper body rig matching, facing and resolved grip frame tests passed");
}
