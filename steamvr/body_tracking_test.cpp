#include "body_tracking.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace framevr;
void Check(bool b,const char* what){if(!b){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
bool Near(math::Vec a,math::Vec b){const auto d=a-b;return math::Dot(d,d)<1e-7f;}
float Length(math::Vec v){return std::sqrt(math::Dot(v,v));}
int main(){
    // Independent cardinal orientations: the offset is anatomical, while the
    // observed eye remains authoritative at every tracked translation.
    const math::Vec offset{0,.4f,.3f},eye{2,6,4};
    Check(Near(body::NeckTarget(eye,offset,0,0,0),{2,5.6f,3.7f}),"neutral neck");
    Check(Near(body::NeckTarget(eye,offset,1.57079633f,0,0),{1.7f,5.6f,4}),"yaw neck");
    Check(Near(body::NeckTarget(eye,offset,0,1.57079633f,0),{2,5.7f,4.4f}),"pitch neck");
    Check(Near(body::NeckTarget(eye,offset,0,0,1.57079633f),{2.4f,6,3.7f}),"roll neck");
    for(float yaw:{-2.f,0.f,2.f})for(float pitch:{-.8f,0.f,.8f})for(float roll:{-.7f,0.f,.7f}){
        const auto neutral=body::NeckTarget(eye,offset,yaw,pitch,roll);
        for(math::Vec motion: {math::Vec{1,0,0},math::Vec{0,-2,0},math::Vec{0,0,1}})
            Check(Near(body::NeckTarget(eye+motion,offset,yaw,pitch,roll),neutral+motion),"room movement follows 1:1");
        Check(std::fabs(Length(eye-neutral)-.5f)<1e-5f,"neck offset length preserved");
    }
    const float scale=body::UnitsPerMetre(6.8f,1.7f,1.f);
    Check(std::fabs(scale-4.f)<1e-5f,"standing scale");
    // Known proportions: 0.4m shoulders, two 0.55m arms and 0.05m palms
    // produce a 1.6m grip span at 4 model units/metre.
    Check(std::fabs(body::ArmScale(6.8f,1.7f,1.6f,1.6f,2.2f,.2f)-1)<1e-5f,"authored reach retained when it matches");
    Check(std::fabs(body::ArmScale(6.8f,1.7f,1.82f,1.6f,2.2f,.2f)-1.2f)<1e-5f,"arm bones grow to measured span");
    Check(body::UnitsPerMetre(6.8f,1.7f,1)==scale,"arm fitting does not change controller scale");
    Check(body::ArmScale(6.8f,1.7f,NAN,1.6f,2.2f,.2f)==1,"invalid measurements preserve authored bones");
    // A shapeshift form's arm twice the reach: the default stops at 0.65, a form passes 0.5.
    Check(std::fabs(body::ArmScale(6.8f,1.7f,1.6f,1.6f,4.4f,.2f)-.65f)<1e-5f,"long arms stop at the default fit");
    Check(std::fabs(body::ArmScale(6.8f,1.7f,1.6f,1.6f,4.4f,.2f,.5f)-.5f)<1e-5f,"a form's long arms fit the span");
    Check(std::fabs(body::ArmScale(6.8f,1.7f,1.6f,1.6f,22.f,.2f,0)-.3f)<1e-5f,"the shortest fit is bounded");
    Check(std::fabs(body::ArmScale(6.8f,1.7f,1.6f,1.6f,2.2f,.2f,.65f,1.25f)-3.f/2.2f)<1e-5f,"a world at another scale than the rig fits the arms in its units");
    Check(body::ArmScale(6.8f,1.7f,1.6f,1.6f,2.2f,.2f,.65f,0)==1,"no unit scale keeps authored bones");
    float bone[16]={1,0,0,0,0,1,0,0,0,0,1,0,3,6,2,1};
    body::ScaleArmBone(bone,{1,6,2},1.2f);
    Check(std::fabs(bone[12]-3.4f)<1e-5f&&bone[13]==6&&bone[14]==2&&std::fabs(bone[0]-1.2f)<1e-5f,"skin and bone length scale about the shoulder");
    // A crouch bends the knee at the original floor, preserving both lengths.
    for(float hipY:{1.f,2.f,3.f,3.9f}) {
        math::Vec hip{1,hipY,2},foot{1,0,2},knee;
        Check(body::Knee(hip,2,2,foot,{0,0,1},knee),"leg solution");
        Check(Near(foot,{1,0,2}),"foot remains grounded");
        Check(std::fabs(Length(knee-hip)-2)<1e-4f&&std::fabs(Length(foot-knee)-2)<1e-4f,"leg lengths");
        Check(knee.z>hip.z,"knee bends forward");
    }
    math::Vec foot{0,0,0},knee;
    Check(body::Knee({0,8,0},2,2,foot,{0,-1,0},knee)&&body::Finite(knee),"unreachable/parallel pole finite");
    Check(foot.y>4&&foot.y<4.01f,"unreachable target clamped");
    Check(body::Knee({0,0,0},2,2,foot={0,0,0},{0,-1,0},knee),"coincident target finite");
    Check(!body::Knee({0,2,0},0,2,foot,{0,0,1},knee),"invalid leg rejected");
    Check(!body::Knee({0,2,0},2,2,foot={0,std::numeric_limits<float>::quiet_NaN(),0},{0,0,1},knee),"NaN rejected");
    std::puts("Body tracking: cardinal head poses, 81 room movements, anatomical offset, scale and grounded leg IK passed");
}
