#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace upperbody {
// Hands: metres from headset, x right / y up / z back; quaternions xyzw.
// World position uses the game's renderer coordinates; yaw angles are radians.
// Final locally solved wrists + R/L weapon attachments, in yaw-neutral model
// axes. Each affine frame stores three 3D rows and an eye-relative metre offset.
struct Resolved {uint8_t mask=0;float frame[4][12]{};};
struct Pose {
 Resolved resolved{};
 uint8_t tracked=0;
 float hand[2][3]{};
 float rot[2][4]{{0,0,0,1},{0,0,0,1}};
 float userHeadM=1.6f,headPitch=0,headRoll=0,yawInModel=0,worldYaw=0;
 float worldPosition[3]{};
 uint32_t classId=0,sampleMs=0;
};
struct Remote {uint32_t unit=0;Pose pose{};};
constexpr size_t kPacketBytes=199,kCapacity=16;
constexpr uint32_t kFreshMs=500;
inline bool Valid(const Resolved&r){
 if(r.mask&~15u)return false;
 for(int f=0;f<4;++f)if(r.mask&(1u<<f))for(float v:r.frame[f])
  if(!std::isfinite(v)||std::fabs(v)>7.5f)return false;
 return true;
}
inline bool Valid(const Pose&p){
 if(!Valid(p.resolved))return false;
 if(p.tracked&~3u||p.classId>7||!std::isfinite(p.userHeadM)||p.userHeadM<.5f||p.userHeadM>2.5f)return false;
 for(int h=0;h<2;++h){
  for(float v:p.hand[h])if(!std::isfinite(v)||std::fabs(v)>4)return false;
  float norm=0;for(float v:p.rot[h]){if(!std::isfinite(v))return false;norm+=v*v;}
  if(norm<.25f||norm>2.25f)return false;
 }
 for(float v:{p.headPitch,p.headRoll})if(!std::isfinite(v)||std::fabs(v)>3.2f)return false;
 for(float v:{p.yawInModel,p.worldYaw})if(!std::isfinite(v)||std::fabs(v)>7)return false;
 for(float v:p.worldPosition)if(!std::isfinite(v)||std::fabs(v)>1000000)return false;
 return true;
}
inline bool Encode(const Pose&p,uint8_t*out,size_t cap,size_t*n){
 if(!out||!n||cap<kPacketBytes||!Valid(p))return false;
 size_t z=0;auto put=[&](const auto&v){std::memcpy(out+z,&v,sizeof v);z+=sizeof v;};
 const uint8_t prefix[6]={'U','B','P',2,p.tracked,0};std::memcpy(out,prefix,6);z=6;
 for(auto&h:p.hand)for(float v:h)put(v);
 for(auto&q:p.rot)for(float v:q)put(v);
 for(float v:{p.userHeadM,p.headPitch,p.headRoll,p.yawInModel,p.worldYaw})put(v);
 for(float v:p.worldPosition)put(v);put(p.classId);put(p.sampleMs);
 put(p.resolved.mask);
 for(int f=0;f<4;++f)for(float v:p.resolved.frame[f]){
  const int16_t packed=int16_t(std::lround((p.resolved.mask&(1u<<f)?v:0.f)*4096.f));put(packed);
 }
 *n=z;return z==kPacketBytes;
}
inline bool Decode(const uint8_t*b,size_t n,Pose*out){
 if(!b||!out||n!=kPacketBytes||b[0]!='U'||b[1]!='B'||b[2]!='P'||b[3]!=2||b[5])return false;
 Pose p{};p.tracked=b[4];size_t z=6;
 auto get=[&](auto&v){std::memcpy(&v,b+z,sizeof v);z+=sizeof v;};
 for(auto&h:p.hand)for(float&v:h)get(v);
 for(auto&q:p.rot)for(float&v:q)get(v);
 get(p.userHeadM);get(p.headPitch);get(p.headRoll);get(p.yawInModel);get(p.worldYaw);
 for(float&v:p.worldPosition)get(v);get(p.classId);get(p.sampleMs);
 get(p.resolved.mask);
 for(auto& frame:p.resolved.frame)for(float&v:frame){int16_t packed=0;get(packed);v=float(packed)/4096.f;}
 if(!Valid(p))return false;
 for(auto&q:p.rot){float norm=0;for(float v:q)norm+=v*v;for(float&v:q)v/=std::sqrt(norm);}
 *out=p;return true;
}
class Playback {
 struct Slot {bool used=false;uint32_t unit=0,seq=0,atA=0,atB=0;Pose a{},b{};};
 std::array<Slot,kCapacity> slots_{};
public:
 bool Push(uint32_t unit,uint32_t seq,const Pose&p,uint32_t now){
  if(!unit||!Valid(p))return false;Expire(now,kFreshMs);
  Slot*s=nullptr;for(auto&x:slots_)if(x.used&&x.unit==unit){s=&x;break;}
  if(s){if(int32_t(seq-s->seq)<=0)return false;s->a=s->b;s->atA=s->atB;}
  else {for(auto&x:slots_)if(!x.used){s=&x;break;}if(!s)return false;s->a=p;s->atA=now;}
  s->used=true;s->unit=unit;s->seq=seq;s->b=p;s->atB=now;return true;
 }
 bool Sample(uint32_t unit,uint32_t now,Remote*out)const {
  if(!out)return false;
  for(const auto&s:slots_)if(s.used&&s.unit==unit&&uint32_t(now-s.atB)<=kFreshMs){
   *out={unit,s.b};const auto span=uint32_t(s.atB-s.atA);
   // A 50ms playback delay, bounded between the latest two received samples.
   float t=span?std::clamp(float(int32_t(now-50-s.atA))/float(span),0.f,1.f):1.f;
   auto lerp=[&](float a,float b){return a+(b-a)*t;};
   for(int h=0;h<2;++h){
    for(int j=0;j<3;++j)out->pose.hand[h][j]=lerp(s.a.hand[h][j],s.b.hand[h][j]);
    float dot=0,norm=0;for(int j=0;j<4;++j)dot+=s.a.rot[h][j]*s.b.rot[h][j];
    for(int j=0;j<4;++j){float v=lerp(s.a.rot[h][j],s.b.rot[h][j]*(dot<0?-1.f:1.f));out->pose.rot[h][j]=v;norm+=v*v;}
    if(norm>1e-6f)for(float&v:out->pose.rot[h])v/=std::sqrt(norm);
   }
   auto angle=[&](float a,float b){return a+std::remainder(b-a,6.283185307f)*t;};
   out->pose.headPitch=angle(s.a.headPitch,s.b.headPitch);out->pose.headRoll=angle(s.a.headRoll,s.b.headRoll);
   out->pose.yawInModel=angle(s.a.yawInModel,s.b.yawInModel);out->pose.worldYaw=angle(s.a.worldYaw,s.b.worldYaw);
   return true;
  }return false;
 }
 size_t List(Remote*out,size_t cap,uint32_t now)const{size_t n=0;for(const auto&s:slots_)if(s.used&&n<cap&&Sample(s.unit,now,out+n))++n;return n;}
 size_t Expire(uint32_t now,uint32_t age){size_t n=0;for(auto&s:slots_)if(s.used&&uint32_t(now-s.atB)>age){s={};++n;}return n;}
 void Reset(){slots_={};}
};
}
