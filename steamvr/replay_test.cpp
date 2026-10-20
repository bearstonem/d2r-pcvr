#include "replay_camera.h"
#include "replay_groups.h"
#include "replay_pause.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void Check(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
static bool Near(double a,double b,double e=1e-4){return std::fabs(a-b)<=e*(1+std::fabs(b));}

static void Groups() {
    struct Sub {const void* q;const void* cl;uint32_t call=0;};
    int qa=0,qb=0,l[8]{};
    std::vector<Sub> subs{{&qa,&l[0]},{&qa,&l[1]},{&qb,&l[2]},{&qa,&l[3]},{&qa,&l[4]},{&qa,&l[5]}};
    const void* first[]={&l[0],&l[1]};
    Check(replaygroups::Tag(subs,&qa,first,2,7)==2&&subs[0].call==7&&subs[1].call==7,"a call numbers its lists");
    const void* other[]={&l[2]};
    Check(replaygroups::Tag(subs,&qa,other,1,8)==0&&subs[2].call==0,"another queue's list is not this call's");
    Check(replaygroups::Tag(subs,&qb,other,1,8)==1&&subs[2].call==8,"its own queue's call numbers it");
    const void* again[]={&l[0]};
    Check(replaygroups::Tag(subs,&qa,again,1,9)==0&&subs[0].call==7,"a numbered list keeps its number");
    const void* late[]={&l[3],&l[4]};replaygroups::Tag(subs,&qa,late,2,10);
    // run 3..6 on qa: lists 3, 4 in call 10, list 5 unseen
    auto game=replaygroups::PlanRun(subs,3,6,replaygroups::kGame);
    Check(game.size()==2&&game[0].first==3&&game[0].count==2&&game[1].first==5&&game[1].count==1,"the game's calls, an unseen list alone");
    auto one=replaygroups::PlanRun(subs,3,6,replaygroups::kPerListSignal);
    Check(one.size()==3&&one[2].first==5&&one[2].count==1,"one call a list");
    auto all=replaygroups::PlanRun(subs,3,6,replaygroups::kBatched);
    Check(all.size()==1&&all[0].count==3,"batched: the run in one call");
    Check(replaygroups::PlanRun(subs,4,4,replaygroups::kGame).empty()&&replaygroups::PlanRun(subs,5,99,replaygroups::kPerList).size()==1,"empty and clamped runs");
    Check(replaygroups::SignalAfterEach(1)&&replaygroups::SignalAfterEach(4)&&!replaygroups::SignalAfterEach(3)&&!replaygroups::SignalAfterEach(0),"signals after each in 1 and 4");
}

static void Pause() {
    replaypause::Settle s;replaypause::Why why;double t=0;
    for(int i=0;i<replaypause::Settle::kPairs-1;++i){Check(!s.Step(0,t+=11000,true,&why),"not before 240 settled pairs");}
    Check(s.Step(0,t+=11000,true,&why)&&why==replaypause::Why::kNone,"the 240th settled pair replays");
    Check(!s.Step(1,t+=11000,true,&why)&&why==replaypause::Why::kArea,"a biome set stops it");
    for(int i=0;i<replaypause::Settle::kPairs;++i)s.Step(1,t+=11000,true,&why);
    Check(s.Settled(),"settled again");
    Check(!s.Step(1,t+=200000,true,&why)&&why==replaypause::Why::kLongFrame,"a frame over 150 ms stops it");
    for(int i=0;i<replaypause::Settle::kPairs;++i)s.Step(1,t+=11000,true,&why);
    Check(!s.Step(1,t+=11000,false,&why)&&why==replaypause::Why::kNoWorld,"no world stops it");
    Check(!s.Step(1,t+=11000,false,&why)&&why==replaypause::Why::kNone,"stopped is told once");
    Check(s.held==replaypause::Why::kNoWorld,"each restart of the settle has its reason, settled or not");
    Check(!s.Step(2,t+=11000,true,&why)&&s.held==replaypause::Why::kArea&&!s.Step(2,t+=11000,true,&why)&&s.held==replaypause::Why::kNone,"a pair that keeps settling has none");
}

// A camera record as D2R lays it out: V, V^-1, P, P^-1, V*P, (V*P)^-1, last V*P.
static void Record(float* f,int o,const double V[16],const double P[16],const double prev[16]) {
    double iV[16],Pi[16],vp[16],ivp[16];
    replaycam::InvRigid(V,iV);replaycam::Inv4(P,Pi);replaycam::Mul(V,P,vp);replaycam::Mul(Pi,iV,ivp);
    replaycam::Put(f+o,V,16);replaycam::Put(f+o+16,iV,16);replaycam::Put(f+o+32,P,16);replaycam::Put(f+o+48,Pi,16);
    replaycam::Put(f+o+64,vp,16);replaycam::Put(f+o+80,ivp,16);replaycam::Put(f+o+96,prev,16);
}
// row-major v*M: the view of an eye at `at` turned `yaw` about +y
static void View(double yaw,const double at[3],double V[16]) {
    const double c=std::cos(yaw),s=std::sin(yaw);
    const double r[3]={c,0,-s},u[3]={0,1,0},b[3]={s,0,c};   // the eye's right, up, back in the world
    for(int i=0;i<3;++i){V[i*4]=r[i];V[i*4+1]=u[i];V[i*4+2]=b[i];V[i*4+3]=0;}
    V[12]=-(at[0]*r[0]+at[1]*r[1]+at[2]*r[2]);V[13]=-(at[0]*u[0]+at[1]*u[1]+at[2]*u[2]);V[14]=-(at[0]*b[0]+at[1]*b[1]+at[2]*b[2]);V[15]=1;
}
// infinite reverse-Z, as framevr::math::Projection
static void Projection(double l,double r,double d,double u,double nearZ,double m[16]) {
    for(int i=0;i<16;++i)m[i]=0;
    m[0]=2/(r-l);m[5]=2/(u-d);m[8]=(r+l)/(r-l);m[9]=(u+d)/(u-d);m[11]=-1;m[14]=nearZ;
}
static void Clip(const double p[3],const double M[16],double out[4]) {
    for(int c=0;c<4;++c)out[c]=p[0]*M[c]+p[1]*M[4+c]+p[2]*M[8+c]+M[12+c];
}

static void Camera() {
    const double eyeAt[3]={1200.5,30.2,-842.75},shift=.27;   // D2R's world: far from the origin
    double V[16],P[16],prev[16],Vprev[16];
    View(.6,eyeAt,V);Projection(-1,1,-1.05,1.05,.2,P);
    const double prevAt[3]={1200.3,30.2,-842.6};View(.58,prevAt,Vprev);replaycam::Mul(Vprev,P,prev);
    std::vector<float> f(1024,0.f);
    Record(f.data(),20,V,P,prev);
    f[4]=float(eyeAt[0]);f[5]=float(eyeAt[1]);f[6]=float(eyeAt[2]);   // the camera position on its own
    replaycam::Eyes e;for(int i=0;i<16;++i)e.left[i]=V[i];e.shift=shift;e.projSx=P[0];
    const auto lay=replaycam::Learn(f.data(),int(f.size()),e.left);
    Check(lay.nv==1&&lay.vo[0]==20&&lay.parts[0]==(2|4|8),"the eye's record found with all its parts");
    Check(lay.nc==1&&lay.co[0]==4,"the camera position on its own found");
    // a shadow cascade's light camera, turned as the eye is: orthographic
    std::vector<float> cascade(1024,0.f);
    double ortho[16];for(double& x:ortho)x=0;ortho[0]=.01;ortho[5]=.01;ortho[10]=.001;ortho[15]=1;
    Record(cascade.data(),212,V,ortho,ortho);
    const auto light=replaycam::Learn(cascade.data(),1024,e.left);
    std::vector<float> lit=cascade;
    Check(light.nv==1&&(light.parts[0]&4)&&light.nc==0,"a cascade's matrices hold no camera position on their own");
    Check(replaycam::PatchRight(lit.data(),1024,light,e).folded==0&&lit==cascade,"the shadow cascade is left alone");
    std::vector<float> left=f,right=f;
    Check(replaycam::PrevToRightEye(left.data(),int(left.size()),lay,e)==1,"the eye record's last frame moved");
    right=left;
    const auto done=replaycam::PatchRight(right.data(),int(right.size()),lay,e);
    Check(done.folded==1&&done.positions==1,"one record folded, the one position moved");
    // the right eye's camera: the left one moved +shift along its own right
    const double rightAt[3]={eyeAt[0]+V[0]*shift,eyeAt[1]+V[4]*shift,eyeAt[2]+V[8]*shift};
    double R[16],RP[16];View(.6,rightAt,R);replaycam::Mul(R,P,RP);
    for(int i=0;i<16;++i)Check(right[20+i]==f[20+i],"the view stays the left one");
    const double world[3]={1195,31,-850};
    double want[4],got[4],vp[16];
    for(int i=0;i<16;++i)vp[i]=right[20+64+i];
    Clip(world,RP,want);Clip(world,vp,got);
    for(int c=0;c<4;++c)Check(Near(got[c],want[c],1e-3),"V*P_R draws the world as the right eye sees it");
    double inv[16],vpD[16];for(int i=0;i<16;++i)vpD[i]=right[20+80+i];
    replaycam::Mul(vp,vpD,inv);
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)Check(Near(inv[r*4+c],r==c?1:0,2e-3),"its inverse goes with it");
    Check(Near(right[4],eyeAt[0]+V[0]*shift,1e-6)&&Near(right[6],eyeAt[2]+V[8]*shift,1e-6),"the camera position moves with the eye");
    double prevR[16],Pr[16];replaycam::RightProjection(P,e,Pr);replaycam::Mul(Vprev,Pr,prevR);
    for(int i=0;i<16;++i)Check(Near(left[20+96+i],prevR[i],1e-3),"last frame's V*P becomes the right eye's, in the left values too");
    // an off-axis eye: the right eye's own term
    e.projShift=.05;std::vector<float> off=f;replaycam::PatchRight(off.data(),int(off.size()),lay,e);
    Check(Near(off[20+32+8],P[8]-.1,1e-6),"each eye's own off-axis term");
    // a view with no projection beside it stays (it pairs with a shifted one elsewhere)
    std::vector<float> bare(1024,.37f);replaycam::Put(bare.data()+788,V,16);   // other values round it, as in the game's buffers
    const auto lone=replaycam::Learn(bare.data(),int(bare.size()),e.left);
    const std::vector<float> asWas=bare;
    Check(lone.nv==1&&!(lone.parts[0]&4)&&replaycam::PatchRight(bare.data(),1024,lone,e).folded==0&&bare==asWas,"a view alone is not folded");
    // another projection's scale (a different camera), and positions only where there is one
    replaycam::Eyes other=e;other.projSx=P[0]*1.5;std::vector<float> g=f;
    Check(replaycam::PatchRight(g.data(),int(g.size()),lay,other).folded==0,"not our projection: not an eye camera");
    replaycam::Layout four=lay;four.nc=4;four.co[1]=100;four.co[2]=110;four.co[3]=120;
    std::vector<float> h=f;Check(replaycam::PatchRight(h.data(),int(h.size()),four,e).positions==0,"four positions in one buffer are shared: left alone");
}

int main() {
    Groups();Pause();Camera();
    std::puts("Replay helpers passed: submission groups, area settle, right-eye camera fold");
    return 0;
}
