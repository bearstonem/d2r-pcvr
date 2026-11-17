#include "../vr/upper_body_pose.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main(){
 using namespace upperbody;
 Pose p{};p.tracked=3;p.hand[0][0]=.4f;p.worldPosition[0]=123;p.classId=3;
 uint8_t b[224]{};size_t n=0;assert(Encode(p,b,sizeof b,&n));
 Pose q{};assert(Decode(b,n,&q));assert(q.worldPosition[0]==123&&q.classId==3&&q.hand[0][0]==.4f);
 assert(!Decode(b,n-1,&q));assert(!Decode(b,n+1,&q));
 p.hand[0][0]=std::numeric_limits<float>::quiet_NaN();assert(!Encode(p,b,sizeof b,&n));p.hand[0][0]=0;
 p.headPitch=100;assert(!Encode(p,b,sizeof b,&n));p.headPitch=0;
 Playback pb;assert(pb.Push(7,1,p,1000));assert(!pb.Push(7,1,p,1010));assert(!pb.Push(7,0,p,1010));
 p.hand[0][0]=1;assert(pb.Push(7,2,p,1040));Remote r{};assert(pb.Sample(7,1060,&r));
 assert(!pb.Sample(7,1600,&r));assert(pb.Expire(1600,500)==1);pb.Reset();assert(!pb.Sample(7,1060,&r));
 p.resolved.mask=15;
 for(int f=0;f<4;++f){p.resolved.frame[f][0]=f==3?-1.f:1.f;p.resolved.frame[f][4]=p.resolved.frame[f][8]=1;
  p.resolved.frame[f][9]=.12345f+f*.1f;p.resolved.frame[f][10]=-.5f;}
 assert(Encode(p,b,sizeof b,&n)&&n+8<=224);assert(Decode(b,n,&q));
 assert(q.resolved.mask==15);
 for(int f=0;f<4;++f)for(int i=0;i<12;++i)assert(std::fabs(q.resolved.frame[f][i]-p.resolved.frame[f][i])<.00025f);
 p.resolved.frame[0][0]=99;assert(!Encode(p,b,sizeof b,&n));p.resolved.frame[0][0]=1;
 p.resolved.mask=16;assert(!Encode(p,b,sizeof b,&n));p.resolved.mask=0;
 puts("upper body pose and resolved grip codec tests passed");
}
