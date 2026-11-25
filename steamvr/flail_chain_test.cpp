// A flail's chain swings: its bones found by name in the game's five flail rigs; the chain posed where the world's
// coordinates are as large as the game's (what kept the first version from ever moving a chain); hanging straight at
// rest, out of the handle, the head, the floor and Khalim's other balls; a swing of the arm that the ball lags, swings
// past and comes back from; long runs of hard play without a lost point; snap turns, pauses, teleports and new sizes;
// the twist held; and what is left to the game, and why.
#include "flail_chain.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace framevr;using namespace framevr::flail;
using skel::bow::Position;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b,float e){return std::fabs(a-b)<=e;}
static float Det(const Mat& m){return math::Dot(Row(m,0),math::Cross(Row(m,1),Row(m,2)));}
static Vec Axis(const Mat& m){return math::Normalized(Row(m,0));}
// A bone at p whose x axis runs along `along`, z kept to the item's z.
static Mat Bone(Vec p,Vec along) {
    Mat m=skel::bow::Identity();const Vec x=math::Normalized(along),z{0,0,1},y=math::Cross(z,x);
    SetRow(m,0,x);SetRow(m,1,y);SetRow(m,2,z);Position(m,p);return m;
}
// A chain as the game's model has it, in the item's own units: the Flail's 18 joints straight up from the handle's top.
static std::vector<Mat> Straight(int links=18,Vec from={0,1.4166f,0},Vec step={0,.0896f,0},Vec axis={0,1,0}) {
    std::vector<Mat> bones;for(int i=0;i<links;++i)bones.push_back(Bone(from+step*float(i),axis));return bones;
}
// Turns for row vectors: about x (the item's y toward the world's +z for 90 deg) and about y (+z toward +x).
static Mat Pitch(float a){Mat m=skel::bow::Identity();SetRow(m,1,{0,std::cos(a),std::sin(a)});SetRow(m,2,{0,-std::sin(a),std::cos(a)});return m;}
static Mat Yaw(float a){Mat m=skel::bow::Identity();SetRow(m,0,{std::cos(a),0,-std::sin(a)});SetRow(m,2,{std::sin(a),0,std::cos(a)});return m;}
static Mat Placed(Mat turn,Vec at,float scale=1){for(int k:{0,1,2,4,5,6,8,9,10})turn.m[k]*=scale;Position(turn,at);return turn;}
constexpr float Units=4;                            // the world's units a metre (the game's: about 4)
constexpr Vec Far{10305.5f,0,10403.8f};             // where the owner stood on 2026-11-24, in the world's units
constexpr float Pi=3.14159265f;
// The Flail in the item's units: its chain 17 links of .0896, the ball's centre BallReach of that past the last joint.
constexpr float ChainM=17*.0896f/Units,ReachM=BallReach*17*.0896f/Units,BallM=BallRadius*17*.0896f/Units;

struct Run {
    Swing swing;Stats stats;World world;double time=0;
    std::vector<std::vector<Mat>> native,out;
    Mat item=Placed(Pitch(Pi/2),Far);   // the handle held out level, pointing ahead (+z), far from the world's origin
    explicit Run(std::vector<std::vector<Mat>> n):native(std::move(n)){for(auto& c:native)out.emplace_back(c.size());world.unitsPerMetre=Units;}
    explicit Run(std::vector<Mat> one=Straight()):Run(std::vector<std::vector<Mat>>{std::move(one)}){}
    bool Pose(double dt=1/90.) {
        time+=dt;Bones b;b.chains=int(native.size());
        for(size_t c=0;c<native.size();++c){b.native[c]=native[c];b.out[c]=out[c];}
        return swing.Pose(b,item,world,time,stats);
    }
    void For(double seconds,double rate=90){for(double t=0;t<seconds-1e-9;t+=1/rate)Check(Pose(1/rate),"posed");}
    Vec Out(int c=0)const{return swing.Head(c)-swing.Top(c);}   // the top to the ball's centre, m
    float Reach(int c=0)const{return swing.ChainLength(c)+BallReach*swing.ChainLength(c);}
    // Every link at its length on the item, every bone finite and as handed as the game's.
    void Whole(const char* why)const {
        auto fail=[&](const char* what,size_t c,size_t i,float a,float b){
            std::fprintf(stderr,"%s: %s (chain %zu, %zu: %g against %g)\n",why,what,c,i,a,b);std::exit(1);
        };
        for(size_t c=0;c<out.size();++c) {
            for(size_t i=0;i<out[c].size();++i) {
                for(float x:out[c][i].m)if(!std::isfinite(x))fail("a bone not finite",c,i,x,0);
                const float a=Det(out[c][i]),b=Det(native[c][i]);
                if((a>0)!=(b>0)||!Near(std::fabs(a),std::fabs(b),2e-3f))fail("a bone stretched or mirrored",c,i,a,b);
            }
            for(size_t i=0;i+1<out[c].size();++i) {
                const float a=Length(Position(out[c][i+1])-Position(out[c][i])),b=Length(Position(native[c][i+1])-Position(native[c][i]));
                if(!Near(a,b,.002f))fail("a link off its length",c,i,a,b);
            }
            for(int i=0;i<=swing.Links(int(c));++i)if(!Finite(swing.Point(int(c),i))||!Finite(swing.Speed(int(c),i)))fail("a point lost",c,size_t(i),0,0);
            const float reach=Length(swing.Head(int(c))-swing.Top(int(c)));
            if(reach>Reach(int(c))+.002f)fail("the ball past the chain's reach",c,0,reach,Reach(int(c)));
        }
    }
};
// How far a bone rolled about its own link between two poses (deg): its turn with the swing of the link taken out. A
// bone that flips over (the link pointing where it did, the bone turned half round about it) rolls 180.
static float Rolled(const Mat& a,const Mat& b) {
    const Mat x=Orthonormal(a),y=Orthonormal(b);
    const Mat swung=Times(x,skel::bow::Rotation(Row(x,0),Row(y,0)));
    float trace=0;for(int i=0;i<3;++i)trace+=math::Dot(Row(swung,i),Row(y,i));
    return std::acos(std::clamp((trace-1)/2,-1.f,1.f))*57.29578f;
}
// Khalim's flails: three chains from one handle, their bones interleaved as the game's files have them.
// links 14: Khalim's Flail (chain_NN at 1,5,8,11,14); 12: Khalim's Will (at 1,4,7,10,12).
static Rig Khalim(int links,std::vector<std::string>& owned,std::vector<int>& parents) {
    owned={"root","translate","anim","cog","cog_helper","mainChain"};parents={-1,0,1,2,3,3};
    const char* side[3]={"L","C","R"};int last[3];
    auto add=[&](const std::string& name,int parent){owned.push_back(name);parents.push_back(parent);return int(owned.size())-1;};
    auto link=[&](int s,int k){
        const int at14[5]={1,5,8,11,14},at12[5]={1,4,7,10,12};const int* at=links==14?at14:at12;int number=0;
        for(int c=0;c<5;++c)if(at[c]==k)number=c+1;
        char b[32];
        if(number)std::snprintf(b,sizeof b,"%s_chain_%02d",side[s],number);else std::snprintf(b,sizeof b,"%s_bindJoint_%02d",side[s],k);
        return std::string(b);
    };
    for(int s=0;s<3;++s){const int root=add(std::string(side[s])+"_chain_root",links==14?5:3);last[s]=add(link(s,1),root);last[s]=add(link(s,2),last[s]);}
    for(int k=3;k<=links;++k)for(int s=0;s<3;++s)last[s]=add(link(s,k),last[s]);
    std::vector<std::string_view> names(owned.begin(),owned.end());
    return Find(names,parents);
}
// Khalim's Flail as the game's model has it: L and R chains out along +-x from the handle's top, C straight up.
static std::vector<std::vector<Mat>> KhalimChains() {
    return {Straight(14,{.028f,1.281f,0},{.13f,0,0},{1,0,0}),Straight(14,{0,1.308f,0},{0,.13f,0},{0,1,0}),
            Straight(14,{-.028f,1.281f,0},{-.13f,0,0},{1,0,0})};
}

int main() {
    {   // The rig: link names.
        for(const char* name:{"chain_bnd_jnt_01","chain_bnd_jnt_18","L_chain_01","C_chain_05","R_bindJoint_13","C_bindJoint_02"})Check(Link(name),"chain links");
        for(const char* name:{"chain_bnd_jnt_","chain_bnd_jnt_x1","L_chain_root","C_chain_root","mainChain","cog","alt_cog","X_chain_01","L_chain_",
                              "l_chain_01","R_bindJoint_","arrow","bowString"})Check(!Link(name),"not links: the chains' roots, other bones");
    }
    {   // Flail, Knout and Scourge (flail.skeleton, 23 bones): one chain of 18, from the handle out.
        std::vector<std::string> owned={"root","translate","anim","cog","alt_cog"};std::vector<int> parents={-1,0,1,2,3};
        for(int i=1;i<=18;++i){char b[32];std::snprintf(b,sizeof b,"chain_bnd_jnt_%02d",i);owned.push_back(b);parents.push_back(i==1?3:int(owned.size())-2);}
        std::vector<std::string_view> names(owned.begin(),owned.end());
        const Rig rig=Find(names,parents);
        Check(owned.size()==23&&rig.Valid()&&rig.why==RigWhy::Found&&rig.chains==1&&rig.links==18&&rig.chain[0].count==18,"one chain of 18");
        for(int i=0;i<18;++i)Check(rig.chain[0].bone[i]==5+i,"its links in order");
        owned.push_back("ball_helper");parents.push_back(10);names.assign(owned.begin(),owned.end());
        Check(Find(names,parents).why==RigWhy::Branched,"a link with another child: left alone, and why");
        owned.pop_back();parents.pop_back();names.assign(owned.begin(),owned.end());
        std::vector<int> bad=parents;bad[7]=7;Check(Find(names,bad).why==RigWhy::Unreadable,"a bone its own parent");
        bad=parents;bad[7]=99;Check(Find(names,bad).why==RigWhy::Unreadable,"a parent past the bones");
        Check(Find(std::span<const std::string_view>(names.data(),8),std::span<const int>(parents.data(),8)).why==RigWhy::Short,"three links: too short");
        std::vector<std::string_view> bow={"root","translate","anim","cog","upperBend","lowerBend","lowerString","upperString","bowString"};
        std::vector<int> bowParents={-1,0,1,2,3,3,5,4,3};
        Check(Find(bow,bowParents).why==RigWhy::NoLinks,"a bow has no chain");
        Check(std::string(Text(RigWhy::Branched)).size()>10&&std::string(Text(Why::Size)).size()>10,"reasons for the log");
    }
    {   // Khalim's Flail (51 bones: three chains of 14) and Khalim's Will (45: three of 12).
        std::vector<std::string> owned;std::vector<int> parents;
        Rig rig=Khalim(14,owned,parents);
        Check(owned.size()==51&&rig.Valid()&&rig.chains==3&&rig.links==42,"Khalim's Flail: three chains of 14");
        std::vector<std::string_view> names(owned.begin(),owned.end());
        for(int c=0;c<3;++c)Check(rig.chain[c].count==14&&names[size_t(rig.chain[c].bone[0])].ends_with("_chain_01")&&names[size_t(rig.chain[c].bone[13])].ends_with("_chain_05"),
                                  "each from its first link to its last");
        rig=Khalim(12,owned,parents);
        Check(owned.size()==45&&rig.Valid()&&rig.chains==3&&rig.links==36,"Khalim's Will: three chains of 12");
        owned.push_back("X_root");parents.push_back(5);
        for(int k=1;k<=6;++k){char b[32];std::snprintf(b,sizeof b,"L_chain_%02d",20+k);owned.push_back(b);parents.push_back(int(owned.size())-2);}
        names.assign(owned.begin(),owned.end());
        Check(Find(names,parents).why==RigWhy::Many,"a fourth chain: no flail of the game's");
    }
    {   // Why the first version never moved a chain: the game's world is past 10000 units nearly everywhere, and the bow's
        // matrix check refuses that. A flail held there, or at the edge of the largest map, is posed and swings.
        Mat check;
        Check(!skel::bow::Inverse(Placed(Pitch(Pi/2),Far),check),"the bow's inverse refuses the owner's place");
        for(Vec at:{Far,Vec{65000,-30,64000},Vec{0,0,0}}) {
            Run r;r.item=Placed(Pitch(Pi/2),at);
            Check(r.Pose()&&r.stats.poses==1&&!r.stats.refused,"posed far from the world's origin");
            r.For(1);
            const Vec before=r.Out();
            for(int f=0;f<20;++f){r.item=Placed(Pitch(Pi/2),at+Vec{.5f*Units*float(f+1)/20,0,0});Check(r.Pose(),"posed moving");}
            Check(Length(r.Out()-before)>.05f,"and the chain swings there");
            r.Whole("whole far out");
        }
    }
    {   // Hanging at rest from the first pose: straight down from the handle's top, links at their lengths.
        Run r;
        Check(r.Pose()&&r.stats.starts==1,"hung afresh");
        r.Whole("whole at once");
        Check(Near(r.Out().y,-(ChainM+ReachM),.004f)&&std::fabs(r.Out().x)<.002f&&std::fabs(r.Out().z)<.002f,"straight down at once");
        r.For(4);
        const Vec top=r.swing.Top();
        Check(Near(top.z,1.4166f/Units,1e-4f)&&Near(top.x,0,1e-4f)&&Near(top.y,0,1e-4f),"its top at the handle's");
        Check(r.Out().y<-.99f*(ChainM+ReachM)&&std::fabs(r.Out().x)<.005f&&std::fabs(r.Out().z)<.005f,"still hanging straight down");
        for(size_t i=0;i<r.out[0].size();++i) {
            // the handle held out level: the item's y is the world's +z and its z the world's down
            Check(Near(math::Dot(Axis(r.out[0][i]),{0,0,1}),1,.01f),"each link points down the chain");
        }
        Check(Length(Position(r.out[0][0])-Position(r.native[0][0]))<1e-4f,"the first link stays on the handle");
        r.Whole("whole at rest");
        const Vec before=r.swing.Head();r.Pose();
        Check(Length(r.swing.Head()-before)<.0005f,"settled");
        const unsigned steps=r.stats.steps;const auto laid=r.out;
        Check(r.Pose(.0005)&&r.Pose(.0005)&&r.stats.steps==steps,"no step between two poses of a frame");
        for(size_t i=0;i<laid[0].size();++i)for(int k=0;k<16;++k)Check(Near(laid[0][i].m[k],r.out[0][i].m[k],1e-5f),"the same bones");
    }
    {   // A swing of the arm: the handle sweeps 90 deg round the shoulder in 0.3 s, then stops. The ball lags behind it,
        // swings on past the handle's top after the stop, and comes back with a swing still well to be seen.
        Run r;
        auto hold=[&](float a){
            const Vec radial{std::sin(a),0,std::cos(a)};
            r.item=Placed(Times(Pitch(Pi/2),Yaw(a)),Far+radial*(.6f*Units));
        };
        const float from=-Pi/3,to=Pi/6;
        hold(from);r.For(3);
        float lag=0;
        for(int f=1;f<=27;++f) {
            const float t=float(f)/27,s=t*t*(3-2*t),a=from+(to-from)*s;
            hold(a);Check(r.Pose(),"posed swinging");
            lag=std::min(lag,math::Dot(r.Out(),{std::cos(a),0,-std::sin(a)}));
        }
        const Vec onward{std::cos(to),0,-std::sin(to)};
        float past=0,back=0;int crossings=0;float side=math::Dot(r.Out(),onward);
        for(int f=0;f<270;++f) {
            Check(r.Pose(),"posed after the swing");
            const float along=math::Dot(r.Out(),onward);
            if(f<90)past=std::max(past,along);
            else back=std::min(back,along);
            if((along>0)!=(side>0))++crossings;
            side=along;
            r.Whole("whole through the swing");
        }
        std::printf("swing: lag %.3f m, past %.3f m, back %.3f m, %d crossings, the ball up to %.1f m/s\n",lag,past,back,crossings,r.stats.fastest);
        Check(lag<-.05f,"the ball lags behind the moving handle");
        Check(past>.1f,"and swings on past it after the stop");
        Check(back<-.04f,"then swings back, still to be seen");
        Check(crossings>=2,"to and fro");
        Check(r.stats.fastest>2&&r.stats.widest>30,"flung out");
        r.For(8);
        Check(std::fabs(math::Dot(r.Out(),onward))<.01f,"and settles hanging again");
        Check(r.stats.starts==1&&!r.stats.jumps&&!r.stats.pauses&&!r.stats.lost,"one start only");
    }
    {   // The ball is heavy: whirled round, the chain runs straight from the hand to it.
        Run r;r.For(1);
        float bow=0;
        for(int f=0;f<360;++f) {
            const float a=float(f)/90*2*Pi*1.5f;
            r.item=Placed(Pitch(Pi/2),Far+Vec{std::cos(a),0,std::sin(a)}*(.25f*Units));Check(r.Pose(),"whirled");
            if(f>180) {
                const Vec top=r.swing.Top(),head=r.swing.Head(),mid=r.swing.Point(0,9);
                const Vec line=math::Normalized(head-top);
                bow=std::max(bow,Length((mid-top)-line*math::Dot(mid-top,line)));
            }
        }
        std::printf("whirl: the chain's middle off the straight line by %.3f m\n",bow);
        Check(bow<.06f,"taut behind the heavy ball");
        Check(r.stats.widest>60,"flung out wide");
        r.Whole("whole whirled");
    }
    {   // Carried along at a steady run it hangs as it does standing: the rub is about the hand, not the air.
        Run r;r.For(2);
        float x=0;   // m: up to 3 m/s over two seconds, then six seconds at it
        for(int f=0;f<720;++f) {
            const float t=std::min(1.f,float(f)/180),speed=3*t*t*(3-2*t);
            x+=speed/90;r.item=Placed(Pitch(Pi/2),Far+Vec{x*Units,0,0});Check(r.Pose(),"posed while running");
        }
        Check(std::fabs(r.Out().x)<.02f&&r.Out().y<-.97f*(ChainM+ReachM),"hanging straight under the hand at 3 m/s");
        Check(!r.stats.jumps,"a run is no jump");
    }
    {   // A teleport and a pause carry the chain on the item as it was; another size hangs it afresh.
        Run r;r.For(.3);
        r.item=Placed(Pitch(Pi/2),Far);
        for(int f=0;f<9;++f){r.item=Placed(Pitch(Pi/2),Far+Vec{.3f*Units*float(f+1)/9,0,0});r.Pose();}
        const auto laid=r.out;
        r.item=Placed(Pitch(Pi/2),Far+Vec{40*Units,0,0});
        Check(r.Pose()&&r.stats.jumps==1&&r.stats.starts==1,"a jump: carried, not hung afresh");
        for(size_t i=0;i<laid[0].size();++i)Check(Length(Position(r.out[0][i])-Position(laid[0][i]))<1e-3f,"the same shape on the item");
        r.For(1);
        Check(r.Pose(.5)&&r.stats.pauses==1&&r.stats.starts==1,"a pause: carried");
        r.item=Placed(Pitch(Pi/2),Far,1.5f);
        Check(r.Pose()&&r.stats.resized==1,"another size: hung afresh");
        Check(r.Out().y<-.99f*1.5f*(ChainM+ReachM),"at its new length");
    }
    {   // A snap turn: the world turns about the player in one frame; the chain keeps its shape on the item, and its swing.
        Run r;r.For(1);
        for(int f=0;f<9;++f){r.item=Placed(Pitch(Pi/2),Far+Vec{.3f*Units*float(f+1)/9,0,0});r.Pose();}
        const auto laid=r.out;
        r.item=Placed(Times(Pitch(Pi/2),Yaw(Pi/6)),Far+Vec{.1f*Units,0,-.2f*Units});++r.world.turn;
        Check(r.Pose()&&r.stats.turns==1&&r.stats.starts==1,"carried, not hung afresh");
        for(size_t i=0;i<laid[0].size();++i)Check(Length(Position(r.out[0][i])-Position(laid[0][i]))<1e-3f,"the same shape on the item");
        const Vec at=r.Out();r.For(.1);
        Check(Length(r.Out()-at)>.02f,"still swinging after the turn");
    }
    {   // The floor: the ball rests on it, the links do not sink through it.
        Run r;r.world.floor=Far.y-.3f*Units;r.For(4);
        Check(r.swing.Head().y>=-.3f+BallM-.002f,"the ball rests on the floor");
        for(int i=0;i<=18;++i)Check(r.swing.Point(0,i).y>=-.3f-.002f,"no link through it");
        r.Whole("whole on the floor");
    }
    {   // The player's head where the ball would hang, and a swing through it: the chain goes round it.
        Run r;const Vec eyes=Vec{0,-(ChainM+ReachM),1.4166f/Units};
        r.world.head=Far+eyes*Units;r.For(2);
        auto clear=[&](const char* why){
            for(int i=1;i<=18;++i){const float need=HeadRadius+(i==18?BallM:LinkRadius);Check(Length(r.swing.Point(0,i)-eyes)>=need-.002f,why);}
        };
        clear("hung beside the head, not in it");
        for(int f=0;f<60;++f){r.item=Placed(Pitch(Pi/2),Far+Vec{.4f*Units*std::sin(float(f)/60*2*Pi),0,0});Check(r.Pose(),"swung");clear("swung round the head");}
        r.Whole("whole round the head");
    }
    {   // Held upright, the chain hangs down beside the handle, not through it.
        Run r;r.item=Placed(skel::bow::Identity(),Far);r.For(3);
        const Vec top{0,1.4166f/Units,0};const float handle=HandleRadius*ChainM;
        for(int i=1;i<=18;++i) {
            const Vec p=r.swing.Point(0,i);const float radius=i==18?BallM:LinkRadius;
            const float t=std::clamp(p.y/(top.y-handle-LinkRadius),0.f,1.f);
            Check(Length(p-Vec{0,t*(top.y-handle-LinkRadius),0})>=handle+radius-.002f,"outside the handle");
        }
        Check(r.swing.Head().y<top.y-.8f*ChainM,"hanging down beside it");
        r.Whole("whole upright");
    }
    {   // Twisting the handle twists the chain with it; whirled round and round, the twist stays within the limit, and no
        // bone flips over as the chain swings through where the game's pose had it pointing the other way.
        Run r;r.item=Placed(skel::bow::Identity(),Far);r.For(1);
        auto previous=r.out;float worst=0;
        for(int f=0;f<540;++f) {
            const float a=float(f)/90*2*Pi*.75f,b=std::min(1.f,float(f)/90);
            r.item=Placed(Times(Pitch(.9f*b*std::sin(a)),Yaw(.9f*b*std::cos(a))),Far+Vec{.15f*b*std::cos(a),0,.15f*b*std::sin(a)}*Units);
            Check(r.Pose(),"circled");
            for(size_t i=0;i<r.out[0].size();++i)worst=std::max(worst,Rolled(previous[0][i],r.out[0][i]));
            previous=r.out;
        }
        std::printf("twist: the most a bone rolled about its link in a frame %.1f deg\n",worst);
        // A flip rolls a bone half round in a frame. A chain draped over the handle's top rolls its links some as the
        // bend swings round (a 60 deg bend turned 30 deg rolls what follows it 15 deg), as a real one does.
        Check(worst<45,"no bone flips over");
        r.item=Placed(Pitch(Pi/2),Far);r.For(4);
        const float twist=r.swing.Twist(0);
        Check(std::isfinite(twist)&&std::fabs(twist)<=TwistLimit+.02f,"hanging, the twist within the limit");
        r.Whole("whole after circling");
        // The wrist rolled half round about the handle in half a second and held: the chain's twist against the handle
        // is brought back within the limit, smoothly.
        previous=r.out;worst=0;
        for(int f=1;f<=135;++f) {
            Mat roll=skel::bow::Identity();const float a=std::min(1.f,float(f)/45)*Pi;
            SetRow(roll,0,{std::cos(a),0,-std::sin(a)});SetRow(roll,2,{std::sin(a),0,std::cos(a)});   // about the item's y: the handle
            r.item=Placed(Times(roll,Pitch(Pi/2)),Far);Check(r.Pose(),"rolled");
            for(size_t i=0;i<r.out[0].size();++i)worst=std::max(worst,Rolled(previous[0][i],r.out[0][i]));
            previous=r.out;
        }
        Check(worst<45,"rolled with the wrist: no bone flips over");
        Check(std::fabs(r.swing.Twist(0))<=TwistLimit+.02f,"rolled and held: the twist back within the limit");
    }
    {   // A mirrored item (left-handed play draws the mirror image): it hangs down in the world just the same.
        Run r;Mat mirror=Placed(Pitch(Pi/2),Far);for(int k:{0,1,2})mirror.m[k]=-mirror.m[k];r.item=mirror;
        r.For(4);
        Check(r.Out().y<-.99f*(ChainM+ReachM),"hanging down");
        r.Whole("each bone as handed as the game's");
    }
    {   // Khalim's Flail: three chains, three balls that hang apart; whirled, all stay whole.
        Run r(KhalimChains());r.item=Placed(Pitch(Pi/2),Far);
        Check(r.Pose()&&r.swing.Chains()==3,"three chains");
        r.For(4);
        for(int a=0;a<3;++a) {
            Check(r.Out(a).y<-.7f*r.Reach(a),"each hangs");
            for(int b=a+1;b<3;++b)Check(Length(r.swing.Head(a)-r.swing.Head(b))>=r.swing.BallSize(a)+r.swing.BallSize(b)-.003f,"the balls apart");
        }
        r.Whole("Khalim's whole at rest");
        for(int f=0;f<360;++f){const float a=float(f)/90*2*Pi*1.3f;r.item=Placed(Times(Pitch(Pi/2),Yaw(.5f*std::sin(a))),Far+Vec{std::cos(a),.3f*std::sin(2*a),std::sin(a)}*(.2f*Units));
            Check(r.Pose(),"whirled");r.Whole("Khalim's whole whirled");}
        Check(r.stats.fastest>1,"swung");
        // Khalim's Will: three chains of 12.
        Run w(std::vector<std::vector<Mat>>{Straight(12,{.02f,1.461f,0},{.132f,0,0},{1,0,0}),Straight(12,{0,1.482f,0},{0,.132f,0},{0,1,0}),
                                              Straight(12,{-.02f,1.461f,0},{-.132f,0,0},{1,0,0})});
        w.For(3);w.Whole("Khalim's Will whole");
        for(int c=0;c<3;++c)Check(w.Out(c).y<-.7f*w.Reach(c),"Khalim's Will hangs");
    }
    {   // Long hard play at uneven frame rates, hitches, teleports and snap turns: nothing lost, nothing stretched,
        // nothing faster than the cap, for both kinds of rig.
        for(int kind=0;kind<2;++kind) {
            Run r=kind?Run(KhalimChains()):Run();
            uint32_t seed=12345;auto random=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
            double clock=0;Vec offset{};float yaw=0;
            const int frames=kind?30000:60000;
            for(int f=0;f<frames;++f) {
                double dt=1/(60+84*double(random()));
                if(random()<.002f)dt=.15+.3*random();            // a hitch
                if(random()<.001f)offset=offset+Vec{random()*40-20,0,random()*40-20}*Units;   // a teleport
                if(random()<.002f){yaw+=random()<.5f?Pi/6:-Pi/6;++r.world.turn;}            // a snap turn
                clock+=dt;const float t=float(clock);
                const Vec hand=Vec{.5f*std::sin(2.1f*t)+.2f*std::sin(7.3f*t),.4f*std::sin(3.7f*t+1),.5f*std::cos(1.7f*t)+.15f*std::sin(11.f*t)}*Units;
                const Mat turn=Times(Times(Pitch(1.6f*std::sin(2.9f*t)+Pi/2),Yaw(yaw+1.2f*std::sin(1.3f*t))),skel::bow::Identity());
                r.item=Placed(turn,Far+offset+hand);
                r.world.head=Far+offset+Vec{0,.35f,-.3f}*Units;r.world.floor=Far.y-1.f*Units;
                Check(r.Pose(dt),"posed through the long run");
                if(f%7==0)r.Whole("whole through the long run");
            }
            std::printf("long run %d: %u steps, the ball up to %.1f m/s, links within %.4f of length, lost %u, starts %u\n",kind,
                        r.stats.steps,r.stats.fastest,r.stats.stretch,r.stats.lost,r.stats.starts);
            Check(!r.stats.lost&&r.stats.starts==1,"no point lost, never hung afresh");
            Check(r.stats.fastest<=MaxSpeed+.01f,"no faster than the cap");
            Check(r.stats.stretch<.01f,"links within 1% of their length after every step");
        }
    }
    {   // The same swing at any headset's frame rate: the same hang, and nearly the same swing.
        std::vector<Vec> after;
        for(double rate:{45.,72.,90.,120.,144.}) {
            Run r;r.For(2,rate);
            Check(r.Out().y<-.99f*(ChainM+ReachM)&&std::fabs(r.Out().x)<.005f,"hangs at any frame rate");
            const int frames=int(std::lround(.2*rate));
            for(int f=1;f<=frames;++f){r.item=Placed(Pitch(Pi/2),Far+Vec{.4f*Units*float(f)/float(frames),0,0});r.Pose(1/rate);}
            r.For(.4,rate);
            after.push_back(r.Out());
        }
        for(const Vec& v:after)Check(Length(v-after[2])<.04f,"the same swing at 45 to 144 Hz");
    }
    {   // Left to the game, with the reason: an item hidden by shrinking, a broken or far-off matrix, bad bones, no world.
        Run r;r.For(1);
        const auto laid=r.out;
        r.item=Placed(Pitch(Pi/2),Far,.001f);
        Check(!r.Pose()&&r.stats.refused==1&&r.stats.why==Why::Size,"a shrunk item: the game's pose");
        for(size_t i=0;i<laid[0].size();++i)for(int k=0;k<16;++k)Check(laid[0][i].m[k]==r.out[0][i].m[k],"out untouched");
        r.item=Placed(Pitch(Pi/2),Far);r.item.m[5]=NAN;
        Check(!r.Pose()&&r.stats.why==Why::Item,"a broken matrix");
        r.item=Placed(Pitch(Pi/2),{2e7f,0,0});
        Check(!r.Pose()&&r.stats.why==Why::Item,"placed past any world");
        r.item=Placed(Pitch(Pi/2),Far);r.world.unitsPerMetre=0;
        Check(!r.Pose()&&r.stats.why==Why::World,"no world size");
        r.world.unitsPerMetre=Units;
        Check(r.Pose()&&r.stats.starts==2,"afterwards, hung afresh");
        std::vector<Mat> one(1,Bone({},{0,1,0})),out(1);Stats s;Swing w;World world;world.unitsPerMetre=Units;
        Bones b;b.chains=1;b.native[0]=one;b.out[0]=out;
        Check(!w.Pose(b,Placed(Pitch(Pi/2),Far),world,1,s)&&s.why==Why::Bones,"one bone is no chain");
        std::vector<Mat> four=Straight(4),three(3);b.native[0]=four;b.out[0]=three;
        Check(!w.Pose(b,Placed(Pitch(Pi/2),Far),world,1,s)&&s.why==Why::Bones,"out the wrong size");
        std::vector<Mat> outFour(4);b.out[0]=outFour;
        Check(w.Pose(b,Placed(Pitch(Pi/2),Far),world,1,s),"otherwise posed");
        b.chains=0;Check(!w.Pose(b,Placed(Pitch(Pi/2),Far),world,2,s)&&s.why==Why::Bones,"no chain");
    }
    std::puts("flail chain: ok");
    return 0;
}
