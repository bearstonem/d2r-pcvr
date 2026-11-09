// Heads turn to you (look_at.h): the head found by name and bind matrix, how far it turns
// for where the player's eyes are, the ease, and the neck and head turned in a pose.
// The skeleton hook that feeds it (vr/skeletons.cpp) needs the game; this does not.
#include "look_at.h"
#include <array>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>
using namespace framevr;
using namespace framevr::lookat;
namespace {
int failures=0;
void Check(bool ok,const char* what) {
    if(!ok){++failures;std::printf("FAILED: %s\n",what);}
}
using Mat=std::array<float,16>;
// A rigid matrix: the axes turned about `axis` by `angle`, then moved to `at` (rows, v' = v . M).
Mat Rigid(Vec axis,float angle,Vec at,float scale=1,bool mirror=false) {
    Mat m{};
    const Vec basis[3]{{1,0,0},{0,1,0},{0,0,1}};
    for(int i=0;i<3;++i) {
        Vec v=About(math::Normalized(axis),angle,basis[i])*scale;
        if(mirror)v.x=-v.x;
        m[size_t(i)*4]=v.x;m[size_t(i)*4+1]=v.y;m[size_t(i)*4+2]=v.z;
    }
    m[12]=at.x;m[13]=at.y;m[14]=at.z;m[15]=1;
    return m;
}
Mat Mul(const Mat& a,const Mat& b) {
    Mat r{};
    for(int i=0;i<4;++i)for(int k=0;k<4;++k)for(int j=0;j<4;++j)r[size_t(i*4+j)]+=a[size_t(i*4+k)]*b[size_t(k*4+j)];
    return r;
}
Mat Inverse(const Mat& m) {
    Mat r{};float i3[9];
    if(!Invert3(m.data(),i3))return r;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[size_t(i*4+j)]=i3[i*3+j];
    for(int j=0;j<3;++j)r[size_t(12+j)]=-(m[12]*i3[j]+m[13]*i3[3+j]+m[14]*i3[6+j]);
    r[15]=1;
    return r;
}
Vec Point(const Mat& m,Vec v) {return Direction(m.data(),v)+Vec{m[12],m[13],m[14]};}
float Degrees(float radians) {return radians/Rad;}
bool Near(float a,float b,float within) {return std::fabs(a-b)<=within;}

// A body standing at its bind pose, facing the model's +z: pelvis, spine, neck, head (its own
// axes turned every which way, as a rig's are), hair under the head, an arm off the spine.
struct Body {
    static constexpr int Count=6;
    enum {Pelvis,Spine,Neck,Head,Hair,Arm};
    std::array<const char*,Count> names{"pelvis_bind_jnt","spine_04_bind_jnt","neck_bind_jnt","head_bind_jnt","hair_3_bind_jnt","L_upr_arm_bind_jnt"};
    std::array<int16_t,Count> parents{-1,0,1,2,3,1};
    std::array<Mat,Count> bind,inverse;
    std::vector<float> pose;
    Rig rig;
    Body() {
        bind[Pelvis]=Rigid({0,1,0},0,{0,1.0f,0});
        bind[Spine]=Rigid({1,0,0},.4f,{0,1.3f,0});
        bind[Neck]=Rigid({0,0,1},1.2f,{0,1.5f,.02f});
        bind[Head]=Rigid({1,2,3},2.1f,{0,1.62f,.04f});
        bind[Hair]=Rigid({3,1,-2},.7f,{0,1.75f,-.08f});
        bind[Arm]=Rigid({0,1,0},1.5f,{.2f,1.45f,0});
        for(int i=0;i<Count;++i)inverse[size_t(i)]=Inverse(bind[size_t(i)]);
        Reset();
        Check(Relearn()==Refusal::None,"the test body's head is found");
    }
    Refusal Relearn() {
        std::array<lookat::Bone,Count> bones;
        for(int i=0;i<Count;++i)bones[size_t(i)]={names[size_t(i)],parents[size_t(i)],inverse[size_t(i)].data()};
        return Learn(bones.data(),Count,rig);
    }
    void Reset() {pose.clear();for(const auto& m:bind)pose.insert(pose.end(),m.begin(),m.end());}
    Mat Matrix(int i)const{Mat m;std::copy(pose.begin()+i*16,pose.begin()+i*16+16,m.begin());return m;}
    void Set(int i,const Mat& m){std::copy(m.begin(),m.end(),pose.begin()+i*16);}
    Vec HeadAt(const Mat& placement)const{return Point(placement,Point(Matrix(Head),{0,0,0}));}
    Vec HeadAhead(const Mat& placement)const{return math::Normalized(Direction(placement.data(),Direction(pose.data()+Head*16,rig.headAhead)));}
};
// An eye `metres` from the head, `round` radians round the world's up from the heading `ahead`, `rise` above level.
Vec EyeFrom(Vec head,Vec ahead,float round,float rise,float metres,float upm) {
    const float yaw=YawOf(ahead)+round;
    return head+Vec{std::sin(yaw)*std::cos(rise),std::sin(rise),std::cos(yaw)*std::cos(rise)}*(metres*upm);
}
}  // namespace

int main() {
    const Limits limits;
    const float upm=2.2f;
    {   // names
        for(const char* n:{"head_bind_jnt","head","head_main_00_M_JNT","head_01_bind_jnt","M_head_bind_jnt","C_NeckHead_head","head_M_JNT"})
            Check(HeadName(n),n);
        for(const char* n:{"neck_bind_jnt","head_jaw_bind_jnt","R_eye_head","head_M_JNT_END","head_nub","hair_head_01","helmet_head_attach","spearhead","pelvis","L_ForeArm","headdress_01","head_fx"})
            Check(!HeadName(n),n);
        Check(NeckName("neck_bind_jnt")&&NeckName("C_Neck_neck2")&&NeckName("neck_main_00_M_JNT"),"necks");
        Check(!NeckName("necklace_01")&&!NeckName("C_NeckHead_head")&&!NeckName("spine_04"),"not necks");
    }
    Body body;
    {   // the head, its neck and what it turns against
        Check(body.rig.head==Body::Head&&body.rig.root==Body::Neck&&body.rig.body==Body::Spine,"head, the neck under it, the spine as the body");
        const Mat upright=Rigid({0,1,0},0,{0,0,0});
        const Vec ahead=body.HeadAhead(upright);
        Check(Near(ahead.z,1,1e-4f)&&Near(ahead.x,0,1e-4f)&&Near(ahead.y,0,1e-4f),"at its bind pose the head looks along the model's +z, whatever its own axes");
        Body noNeck;noNeck.names[Body::Neck]="spine_05_bind_jnt";
        Check(noNeck.Relearn()==Refusal::None&&noNeck.rig.root==Body::Head&&noNeck.rig.body==Body::Neck,"no neck: the head alone turns, against its parent");
        Body twoNecks;twoNecks.names[Body::Spine]="C_Neck_neck1";
        Check(twoNecks.Relearn()==Refusal::None&&twoNecks.rig.root==Body::Spine&&twoNecks.rig.body==Body::Pelvis,"a run of necks: the lowest is the root");
        Body leaf;leaf.names[Body::Hair]="head";
        Check(leaf.Relearn()==Refusal::None&&leaf.rig.head==Body::Head,"a second head under the first is not taken");
        Body headless;headless.names[Body::Head]="Spine_TopSHJnt";
        Check(headless.Relearn()==Refusal::NoHead&&!headless.rig.Usable(),"no bone named as a head: left alone");
        Body broken;broken.inverse[Body::Head][5]=std::numeric_limits<float>::quiet_NaN();
        Check(broken.Relearn()==Refusal::Bind&&!broken.rig.Usable(),"a head whose bind does not read: left alone");
        Body order;order.parents[Body::Neck]=4;
        Check(order.Relearn()==Refusal::Bones,"a parent after its bone: refused");
        Rig r;Check(Learn(nullptr,4,r)==Refusal::Bones&&Learn(nullptr,MaxBones+1,r)==Refusal::Bones,"no bones, too many bones");
    }
    // Every placement: plain, turned and moved, scaled, mirrored (a left-handed world).
    const Mat placements[]{Rigid({0,1,0},0,{0,0,0}),Rigid({0,1,0},2.3f,{40,-3,17}),Rigid({0,1,0},-1.1f,{-8,2,5},1.7f),Rigid({0,1,0},.6f,{3,1,-9},1,true)};
    for(const auto& placement:placements) {
        body.Reset();
        const Vec head=body.HeadAt(placement),ahead=body.HeadAhead(placement);
        auto turned=[&](float round,float rise,float metres,Aim& aim,float neckShare=Limits{}.neckShare) {
            body.Reset();
            Limits l=limits;l.neckShare=neckShare;
            const Vec eye=EyeFrom(head,ahead,round,rise,metres,upm);
            if(!Want(body.rig,body.pose.data(),Body::Count,placement.data(),eye,upm,l,aim))return false;
            return Apply(body.rig,body.parents.data(),body.pose.data(),Body::Count,placement.data(),{aim.yaw,aim.tilt},l);
        };
        Aim aim;
        {   // straight ahead
            Check(turned(0,0,3,aim)&&Near(aim.yaw,0,1e-3f)&&Near(aim.tilt,0,1e-3f)&&Near(aim.weight,1,1e-4f)&&Near(aim.metres,3,1e-2f),"eyes straight ahead: nothing to turn");
            const Vec after=body.HeadAhead(placement);
            Check(Near(Degrees(Wrap(YawOf(after)-YawOf(ahead))),0,.1f),"and the head stays");
        }
        for(const float round:{40*Rad,-55*Rad,10*Rad}) for(const float rise:{0.f,20*Rad,-15*Rad}) {
            Check(turned(round,rise,3,aim,0)&&Near(aim.weight,1,1e-4f),"eyes to the side, in reach");
            const Vec after=body.HeadAhead(placement);
            Check(Near(Degrees(Wrap(YawOf(after)-YawOf(ahead))),Degrees(round),.2f),"the head alone turns exactly to the eyes");
            Check(Near(Degrees(TiltOf(after)),Degrees(rise),.2f),"and tilts exactly to them");
            Check(turned(round,rise,3,aim)&&Near(Degrees(Wrap(YawOf(body.HeadAhead(placement))-YawOf(ahead))),Degrees(round),.2f)&&
                  Near(Degrees(TiltOf(body.HeadAhead(placement))),Degrees(rise),.2f),"shared with the neck, the head ends on the same heading");
        }
        {   // the limits
            Check(turned(95*Rad,0,3,aim)&&Near(Degrees(Wrap(YawOf(body.HeadAhead(placement))-YawOf(ahead))),70,.3f),"past 70 degrees the head stops at 70");
            Check(turned(-100*Rad,0,3,aim)&&Near(Degrees(Wrap(YawOf(body.HeadAhead(placement))-YawOf(ahead))),-70,.3f),"either side");
            Check(turned(170*Rad,0,3,aim)&&aim.weight==0&&aim.yaw==0&&aim.tilt==0,"eyes behind: the animation's head");
            Check(turned(120*Rad,0,3,aim)&&aim.weight>0&&aim.weight<1,"going behind: let go of by degrees");
            Check(turned(0,70*Rad,3,aim)&&Near(Degrees(TiltOf(body.HeadAhead(placement))),40,.3f),"up: 40 degrees at most");
            Check(turned(0,-70*Rad,3,aim)&&Near(Degrees(TiltOf(body.HeadAhead(placement))),-30,.3f),"down: 30");
            Check(turned(30*Rad,0,6.5f,aim)&&aim.weight>0&&aim.weight<1&&std::fabs(aim.yaw)<30*Rad,"near the edge of the range: part of the turn");
            Check(turned(30*Rad,0,7.5f,aim)&&aim.weight==0,"beyond it: none");
            Check(turned(30*Rad,0,.1f,aim)&&aim.weight==0,"eyes inside the head: none");
        }
        {   // what moves and what does not
            body.Reset();
            const std::vector<float> before=body.pose;
            const Mat hairInHead=Mul(body.Matrix(Body::Hair),Inverse(body.Matrix(Body::Head)));
            Check(turned(50*Rad,15*Rad,2,aim),"a turn");
            for(const int still:{Body::Pelvis,Body::Spine,Body::Arm})
                Check(std::equal(before.begin()+still*16,before.begin()+still*16+16,body.pose.begin()+still*16),"the body and arms are the game's");
            const Mat hairAfter=Mul(body.Matrix(Body::Hair),Inverse(body.Matrix(Body::Head)));
            bool rigid=true;for(int i=0;i<16;++i)rigid=rigid&&Near(hairAfter[size_t(i)],hairInHead[size_t(i)],1e-4f);
            Check(rigid,"the hair rides the head");
            bool neckPlace=true;for(int i=12;i<15;++i)neckPlace=neckPlace&&Near(body.pose[size_t(Body::Neck*16+i)],before[size_t(Body::Neck*16+i)],1e-5f);
            bool neckTurned=false;for(int i=0;i<11;++i)neckTurned=neckTurned||!Near(body.pose[size_t(Body::Neck*16+i)],before[size_t(Body::Neck*16+i)],1e-4f);
            Check(neckPlace&&neckTurned,"the neck turns about its own joint");
            const Vec gap=Point(body.Matrix(Body::Head),{0,0,0})-Point(body.Matrix(Body::Neck),{0,0,0});
            const Vec was{before[Body::Head*16+12]-before[Body::Neck*16+12],before[Body::Head*16+13]-before[Body::Neck*16+13],before[Body::Head*16+14]-before[Body::Neck*16+14]};
            Check(Near(math::Dot(gap,gap),math::Dot(was,was),1e-4f),"the head stays its length from the neck");
            for(const float f:body.pose)Check(std::isfinite(f),"finite");
        }
        {   // an animation that already turns the head: the turn added makes up the difference
            body.Reset();
            const Vec at=Point(body.Matrix(Body::Head),{0,0,0});
            Mat animated=body.Matrix(Body::Head);animated[12]=animated[13]=animated[14]=0;
            animated=Mul(animated,Rigid({0,1,0},-25*Rad,at));
            const Vec eye=EyeFrom(head,ahead,35*Rad,10*Rad,3,upm);
            body.Set(Body::Head,animated);
            const float animatedYaw=Wrap(YawOf(body.HeadAhead(placement))-YawOf(ahead));
            Check(std::fabs(Degrees(animatedYaw))>20,"the animation turned the head");
            Limits l=limits;l.neckShare=0;
            Check(Want(body.rig,body.pose.data(),Body::Count,placement.data(),eye,upm,l,aim)&&
                  Apply(body.rig,body.parents.data(),body.pose.data(),Body::Count,placement.data(),{aim.yaw,aim.tilt},l),"turned from there");
            Check(Near(Degrees(Wrap(YawOf(body.HeadAhead(placement))-YawOf(ahead))),35,.3f)&&Near(Degrees(TiltOf(body.HeadAhead(placement))),10,.3f),"it ends on the eyes all the same");
        }
        {   // a body lying down (dying, a corpse posed again): left as the game has it
            body.Reset();
            const Mat fallen=Rigid({1,0,0},Pi/2,{0,0,0});
            for(int i=0;i<Body::Count;++i)body.Set(i,Mul(body.Matrix(i),fallen));
            const Vec eye=EyeFrom(body.HeadAt(placement),ahead,30*Rad,0,2,upm);
            Check(Want(body.rig,body.pose.data(),Body::Count,placement.data(),eye,upm,limits,aim)&&aim.weight==0&&aim.yaw==0,"a fallen body's head is not turned");
            body.Reset();
            const Mat leaning=Rigid({1,0,0},25*Rad,{0,0,0});
            for(int i=0;i<Body::Count;++i)body.Set(i,Mul(body.Matrix(i),leaning));
            const Vec eye2=EyeFrom(body.HeadAt(placement),ahead,30*Rad,0,2,upm);
            Check(Want(body.rig,body.pose.data(),Body::Count,placement.data(),eye2,upm,limits,aim)&&aim.weight>.9f,"a body leaning into its stride still looks");
        }
    }
    {   // what does not read
        body.Reset();Aim aim;
        const Mat placement=Rigid({0,1,0},0,{0,0,0});
        const Vec eye{1,1.6f,4};
        Check(!Want(body.rig,body.pose.data(),Body::Head,placement.data(),eye,upm,limits,aim),"a pose shorter than the head's index");
        Check(!Want(body.rig,body.pose.data(),Body::Count,placement.data(),eye,0,limits,aim)&&!Want(body.rig,body.pose.data(),Body::Count,placement.data(),eye,40,limits,aim),"no scale");
        Check(!Want(body.rig,body.pose.data(),Body::Count,placement.data(),{std::numeric_limits<float>::quiet_NaN(),0,0},upm,limits,aim),"no eyes");
        Check(!Want(Rig{},body.pose.data(),Body::Count,placement.data(),eye,upm,limits,aim),"no head");
        body.pose[Body::Head*16+3+2]=std::numeric_limits<float>::infinity();
        Check(!Want(body.rig,body.pose.data(),Body::Count,placement.data(),eye,upm,limits,aim),"a head that is not finite");
        body.Reset();
        Mat flat=placement;flat[5]=0;
        Check(!Apply(body.rig,body.parents.data(),body.pose.data(),Body::Count,flat.data(),{.3f,.1f},limits),"a placement that does not invert");
        Check(!Apply(body.rig,body.parents.data(),body.pose.data(),Body::Count,placement.data(),{std::numeric_limits<float>::quiet_NaN(),0},limits),"a turn that is not a number");
        Check(!Apply(Rig{},body.parents.data(),body.pose.data(),Body::Count,placement.data(),{.3f,.1f},limits),"no head to turn");
    }
    {   // the ease
        Eased e;Aim want;want.yaw=60*Rad;want.tilt=-20*Rad;
        Step(e,want,0,limits);Check(e.yaw==0&&e.tilt==0,"no time, no turn");
        Step(e,want,1/90.f,limits);
        Check(e.yaw>0&&e.yaw<=limits.fastest/90+1e-5f&&e.tilt<0,"one frame: a part of the way, no faster than the fastest");
        float last=e.yaw;bool rising=true;
        for(int i=0;i<90;++i){Step(e,want,1/90.f,limits);rising=rising&&e.yaw>=last&&e.yaw<=want.yaw+1e-4f;last=e.yaw;}
        Check(rising&&Near(e.yaw,want.yaw,1*Rad)&&Near(e.tilt,want.tilt,1*Rad),"within a second it is there, never past");
        Check(!Still(e),"a turn to see");
        for(int i=0;i<180;++i)Step(e,Aim{},1/90.f,limits);
        Check(Still(e),"let go of, it eases back to the animation");
        Eased stall;Step(stall,want,5,limits);
        Check(stall.yaw<=limits.fastest*.1f+1e-4f,"a stalled frame counts as a tenth of a second");
        Eased bad;bad.yaw=std::numeric_limits<float>::quiet_NaN();Aim nan;nan.tilt=std::numeric_limits<float>::quiet_NaN();
        Step(bad,nan,.01f,limits);Check(std::isfinite(bad.yaw)&&std::isfinite(bad.tilt),"never not a number");
        Eased wide;Aim far;far.yaw=10;far.tilt=-10;
        for(int i=0;i<400;++i)Step(wide,far,.05f,limits);
        Check(wide.yaw<=limits.turnMost+1e-5f&&wide.tilt>=-limits.tiltMost-1e-5f,"the most a head is ever turned");
    }
    if(failures){std::printf("look-at-test: %d failed\n",failures);return 1;}
    std::puts("Look at you: head names, the neck's root, bind axes, turn and tilt limits, range, fallen bodies, mirrored worlds and the ease passed");
    return 0;
}
