#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace upperbodyrender {
// Row-vector correction for native lower-body facing, about its own pelvis.
inline void FacingCorrection(float nativeYaw,float facing,const float* pivot,float* out){
 for(int i=0;i<16;++i)out[i]=0;
 const float c=std::cos(facing-nativeYaw),s=std::sin(facing-nativeYaw);
 out[0]=out[10]=c;out[2]=-s;out[8]=s;out[5]=out[15]=1;
 out[12]=pivot[0]-(pivot[0]*c+pivot[2]*s);
 out[14]=pivot[2]-(-pivot[0]*s+pivot[2]*c);
}
// Affine frames retain authored scale and mirror handedness (no quaternion
// decomposition); only the translation is normalized by player height.
inline bool PackFrame(const float*m,const float*eye,float upm,float yaw,float*out){
 if(!std::isfinite(upm)||upm<=.01f||!std::isfinite(yaw))return false;
 const float c=std::cos(yaw),s=std::sin(yaw);
 for(int row=0;row<4;++row){
  float x=m[row*4],y=m[row*4+1],z=m[row*4+2];
  if(row==3){x=(x-eye[0])/upm;y=(y-eye[1])/upm;z=(z-eye[2])/upm;}
  out[row*3]=x*c-z*s;out[row*3+1]=y;out[row*3+2]=x*s+z*c;
 }
 for(int i=0;i<12;++i)if(!std::isfinite(out[i])||std::fabs(out[i])>7.5f)return false;
 return true;
}
inline bool UnpackFrame(const float*f,const float*eye,float upm,float yaw,float*out){
 if(!std::isfinite(upm)||upm<=.01f||!std::isfinite(yaw))return false;
 for(int i=0;i<12;++i)if(!std::isfinite(f[i])||std::fabs(f[i])>7.5f)return false;
 for(int i=0;i<16;++i)out[i]=0;out[15]=1;
 const float c=std::cos(yaw),s=std::sin(yaw);
 for(int row=0;row<4;++row){
  out[row*4]=f[row*3]*c+f[row*3+2]*s;
  out[row*4+1]=f[row*3+1];out[row*4+2]=-f[row*3]*s+f[row*3+2]*c;
  if(row==3)for(int col=0;col<3;++col)out[12+col]=out[12+col]*upm+eye[col];
 }
 return true;
}
struct Candidate {uint64_t unit;int cls;float position[3];};
inline float Distance(const float*a,const float*b){float x=a[0]-b[0],z=a[2]-b[2];return x*x+z*z;}
// Game unit IDs are not renderer ECS IDs. Match only a unique nearby hero of
// the correct class, never the local hero; overlapping players use vanilla pose.
inline int Match(const Candidate*c,size_t n,int cls,const float*rig,const float*local){
 if(Distance(rig,local)<4)return -1;
 int best=-1;float d1=16,d2=16;
 for(size_t i=0;i<n;++i)if(c[i].unit&&c[i].cls==cls){
  float d=Distance(rig,c[i].position);if(!std::isfinite(d))continue;
  if(d<d1){d2=d1;d1=d;best=int(i);}else if(d<d2)d2=d;
 }
 return best>=0&&d2-d1>1?best:-1;
}
}
