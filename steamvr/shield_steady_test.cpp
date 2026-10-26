#include "../vr/shield_steady.h"
#include <cstdio>
#include <cstdlib>
using namespace skel::steady;
namespace bow=skel::bow;
void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
bool Near(float a,float b,float e=1e-3f){return std::fabs(a-b)<=e;}
// A shield matrix turned deg about up (y), at x, with a row scale.
bow::Mat Held(float deg,float x,float scale=1.2f) {
    const float r=deg/57.2958f,c=std::cos(r),s=std::sin(r);bow::Mat m;
    const float rows[3][3]={{c,0,-s},{0,1,0},{s,0,c}};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)m.m[i*4+j]=rows[i][j]*scale;
    m.m[12]=x;m.m[13]=1;m.m[14]=2;m.m[15]=1;return m;
}
float Yaw(const bow::Mat& m){return std::atan2(-m.m[2],m.m[0])*57.2958f;}
int main() {
    const float dt=1/72.f,cm=.02f;   // 72 fps; 0.02 model units per cm
    // Quaternion round trip keeps the rotation.
    {float r[3][3]={{0,0,-1},{0,1,0},{1,0,0}},b[3][3];ToRows(FromRows(r),b);
     for(int i=0;i<3;++i)for(int j=0;j<3;++j)Check(Near(r[i][j],b[i][j]),"quaternion round trip");}
    // No dead zones: the shield passes through exactly.
    {Filter f;const Settings off;bow::Mat m=Held(0,0);f.Apply(m,dt,cm,off);
     for(int i=1;i<60;++i){m=Held(i%2?1.f:0.f,i%2?.01f:0.f);f.Apply(m,dt,cm,off);
         Check(Near(Yaw(m),i%2?1.f:0.f,.01f)&&Near(m.m[12],i%2?.01f:0.f,1e-5f),"no dead zone = no change");}}
    // The shipped values: a held-still shiver (0.6 degrees, 0.25 mm a frame) is gone; scale kept.
    Check(Near(Shield.angleDeadDeg,1)&&Near(Shield.moveDeadCm,.15f),"shipped dead zones: 1 degree, 1.5 mm");
    {Filter f;bow::Mat m=Held(0,0);f.Apply(m,dt,cm,Shield);
     for(int i=1;i<144;++i){m=Held(i%2?.6f:0.f,i%2?.025f*cm:0.f);f.Apply(m,dt,cm,Shield);
         Check(Near(Yaw(m),0,.01f)&&Near(m.m[12],0,1e-6f),"shiver inside the dead zones dropped");}
     Check(Near(std::sqrt(m.m[0]*m.m[0]+m.m[1]*m.m[1]+m.m[2]*m.m[2]),1.2f),"row scale kept");}
    // Past a dead zone only the excess passes (no jump at its edge) ...
    {Filter f;bow::Mat m=Held(0,0);f.Apply(m,dt,cm,Shield);
     m=Held(3,0);f.Apply(m,dt,cm,Shield);Check(Near(Yaw(m),2,.02f),"3 degrees past a 1-degree dead zone moves 2");
     m=Held(3,1.f*cm);f.Apply(m,dt,cm,Shield);Check(Near(Yaw(m),2,.02f)&&Near(m.m[12],.85f*cm,1e-5f),"1 cm past a 1.5 mm dead zone moves 0.85 cm");}
    // ... in the same frame: no trailing. A fast block and a sidestep follow at once, within the dead zones.
    {Filter f;bow::Mat m=Held(0,0);f.Apply(m,dt,cm,Shield);
     for(int i=1;i<=7;++i){m=Held(90.f*i/7,30.f*i/7*cm);f.Apply(m,dt,cm,Shield);
         Check(std::fabs(Yaw(m)-90.f*i/7)<=1.01f&&std::fabs(m.m[12]-30.f*i/7*cm)<=.151f*cm,"every frame within the dead zones of the arm");}}
    // A gap restarts: after 0.5 s the shield is taken as it is.
    {Filter f;bow::Mat m=Held(0,0);f.Apply(m,dt,cm,Shield);
     m=Held(.5f,.05f*cm);f.Apply(m,.5f,cm,Shield);Check(Near(Yaw(m),.5f,.01f)&&Near(m.m[12],.05f*cm),"a gap restarts the filter");}
    // Round trips through every quaternion branch (trace <= 0): half turns about x, y and z.
    {const float rs[3][3][3]={{{1,0,0},{0,-1,0},{0,0,-1}},{{-1,0,0},{0,1,0},{0,0,-1}},{{-1,0,0},{0,-1,0},{0,0,1}}};
     for(const auto& r:rs){float b[3][3];ToRows(FromRows(r),b);
         for(int i=0;i<3;++i)for(int j=0;j<3;++j)Check(Near(r[i][j],b[i][j]),"half-turn round trip");}}
    // A mirrored bone keeps its mirror and is still steadied.
    {Filter f;bow::Mat m=Held(0,0);for(int c=0;c<3;++c)m.m[8+c]=-m.m[8+c];f.Apply(m,dt,cm,Shield);
     for(int i=1;i<20;++i){m=Held(i%2?.6f:0.f,0);for(int c=0;c<3;++c)m.m[8+c]=-m.m[8+c];f.Apply(m,dt,cm,Shield);
         const float det=m.m[0]*(m.m[5]*m.m[10]-m.m[6]*m.m[9])-m.m[1]*(m.m[4]*m.m[10]-m.m[6]*m.m[8])+m.m[2]*(m.m[4]*m.m[9]-m.m[5]*m.m[8]);
         Check(det<0&&Near(Yaw(m),0,.01f)&&Near(m.m[10],-1.2f),"mirrored bone: mirror kept, shiver dropped");}}
    // A degenerate matrix resets and passes through.
    {Filter f;bow::Mat z;f.Apply(z,dt,cm,Shield);Check(!f.ok,"degenerate matrix rejected");}
    std::puts("Shield steady: dead zones drop the shiver, real moves pass in the same frame, gaps and Off passed");
}
