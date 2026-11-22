// A flail's chain swings: its bones found by name, the rope hanging and swinging from the handle, the fresh starts
// (first, a pause, a jump, a new size), a snap turn carried, the floor, and what is left to the game's own pose.
#include "flail_chain.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace framevr;using namespace framevr::flail;
using skel::bow::Position;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(float a,float b,float e){return std::fabs(a-b)<=e;}
static Vec Row(const Mat& m,int r){return {m.m[r*4],m.m[r*4+1],m.m[r*4+2]};}
static float Det(const Mat& m){return math::Dot(Row(m,0),math::Cross(Row(m,1),Row(m,2)));}
// A bone at p whose x axis runs along `along` (a link's own axis), z kept to the item's z.
static Mat Bone(Vec p,Vec along) {
    Mat m=skel::bow::Identity();const Vec x=math::Normalized(along),z{0,0,1},y=math::Cross(z,x);
    m.m[0]=x.x;m.m[1]=x.y;m.m[2]=x.z;m.m[4]=y.x;m.m[5]=y.y;m.m[6]=y.z;m.m[8]=z.x;m.m[9]=z.y;m.m[10]=z.z;
    Position(m,p);return m;
}
// A flail's chain as the game's model has it, in its feet: 18 joints straight up from the handle's top.
static std::vector<Mat> Straight(int links=18,Vec from={0,1.4166f,0},Vec step={0,.0896f,0},Vec axis={0,1,0}) {
    std::vector<Mat> bones;for(int i=0;i<links;++i)bones.push_back(Bone(from+step*float(i),axis));return bones;
}
static Mat Item(float scale=1,Vec at={}) {Mat m=skel::bow::Identity();m.m[0]=m.m[5]=m.m[10]=scale;Position(m,at);return m;}
constexpr float Feet=3.2808f;   // the world's units a metre here: the item's feet are the world's
struct Run {
    Swing swing;Stats stats;std::vector<Mat> native,out;Mat item=Item();double time=0;uint32_t turn=0;float floor=NAN;
    explicit Run(std::vector<Mat> n):native(std::move(n)),out(native.size()){}
    bool Pose(double dt=1/90.){time+=dt;return swing.Pose(native,item,Feet,floor,time,turn,out,stats);}
    void For(double seconds){for(double t=0;t<seconds;t+=1/90.)Check(Pose(),"posed");}
    Vec Anchor()const{return swing.Joint(0);}
};
int main() {
    {   // The rig: link names.
        for(const char* name:{"chain_bnd_jnt_01","chain_bnd_jnt_18","L_chain_01","C_chain_05","R_bindJoint_13","C_bindJoint_02"})Check(Link(name),"chain links");
        for(const char* name:{"chain_bnd_jnt_","chain_bnd_jnt_x1","L_chain_root","C_chain_root","mainChain","cog","alt_cog","X_chain_01","L_chain_","l_chain_01",
                              "R_bindJoint_","arrow","bowString"})Check(!Link(name),"not links: the chains' roots, other bones");
    }
    {   // A flail's skeleton: one chain of 18, from the handle out.
        std::vector<std::string> owned={"root","translate","anim","cog","alt_cog"};std::vector<int> parents={-1,0,1,2,3};
        for(int i=1;i<=18;++i){char b[32];std::snprintf(b,sizeof b,"chain_bnd_jnt_%02d",i);owned.push_back(b);parents.push_back(i==1?3:int(owned.size())-2);}
        std::vector<std::string_view> names(owned.begin(),owned.end());
        const Rig rig=Find(names,parents);
        Check(rig.Valid()&&rig.chains==1&&rig.links==18&&rig.chain[0].count==18,"one chain of 18");
        for(int i=0;i<18;++i)Check(rig.chain[0].bone[i]==5+i,"its links in order");
        // A link holding something else is not a chain the swing may move.
        owned.push_back("ball_helper");parents.push_back(10);names.assign(owned.begin(),owned.end());
        Check(!Find(names,parents).Valid(),"a link with another child: left alone");
        owned.pop_back();parents.pop_back();names.assign(owned.begin(),owned.end());
        std::vector<int> bad=parents;bad[7]=7;Check(!Find(names,bad).Valid(),"a bone its own parent");
        bad=parents;bad[7]=99;Check(!Find(names,bad).Valid(),"a parent past the bones");
        Check(!Find(std::span<const std::string_view>(names.data(),8),std::span<const int>(parents.data(),8)).Valid(),"three links: too short");
        std::vector<std::string_view> bow={"root","translate","anim","cog","upperBend","lowerBend","lowerString","upperString","bowString"};
        std::vector<int> bowParents={-1,0,1,2,3,3,5,4,3};
        Check(!Find(bow,bowParents).Valid(),"a bow has no chain");
    }
    {   // Khalim's flails: three chains of 14 from one handle, their bones interleaved as the game's file has them.
        std::vector<std::string> owned={"root","translate","anim","cog","cog_helper","mainChain"};std::vector<int> parents={-1,0,1,2,3,3};
        const char* side[3]={"L","C","R"};int last[3];
        auto add=[&](const std::string& name,int parent){owned.push_back(name);parents.push_back(parent);return int(owned.size())-1;};
        // As the game's: chain_01, bindJoint_02 to _04, chain_02, _06, _07, chain_03, _09, _10, chain_04, _12, _13, chain_05.
        auto link=[&](int s,int k){
            const int chainAt[5]={1,5,8,11,14};int number=0;
            for(int c=0;c<5;++c)if(chainAt[c]==k)number=c+1;
            char b[32];
            if(number)std::snprintf(b,sizeof b,"%s_chain_%02d",side[s],number);else std::snprintf(b,sizeof b,"%s_bindJoint_%02d",side[s],k);
            return std::string(b);
        };
        for(int s=0;s<3;++s){const int root=add(std::string(side[s])+"_chain_root",5);last[s]=add(link(s,1),root);last[s]=add(link(s,2),last[s]);}
        for(int k=3;k<=14;++k)for(int s=0;s<3;++s)last[s]=add(link(s,k),last[s]);
        std::vector<std::string_view> names(owned.begin(),owned.end());
        const Rig rig=Find(names,parents);
        Check(rig.Valid()&&rig.chains==3&&rig.links==42,"three chains of 14");
        for(int c=0;c<3;++c)Check(rig.chain[c].count==14&&names[size_t(rig.chain[c].bone[0])].ends_with("_chain_01")&&names[size_t(rig.chain[c].bone[13])].ends_with("_chain_05"),
                                  "each from its first link to its last");
        owned.push_back("X_root");parents.push_back(5);
        for(int k=1;k<=6;++k){char b[32];std::snprintf(b,sizeof b,"L_chain_%02d",20+k);owned.push_back(b);parents.push_back(int(owned.size())-2);}
        names.assign(owned.begin(),owned.end());
        Check(!Find(names,parents).Valid(),"a fourth chain: no flail of the game's");
    }
    const float chainM=17*.0896f/Feet,ballM=HeadLinks*.0896f/Feet;
    {   // The first pose is the game's own; then it hangs straight down from the handle, its links their own lengths.
        Run r(Straight());
        Check(r.Pose()&&r.stats.starts==1,"a fresh start");
        for(size_t i=0;i<r.out.size();++i)for(int k=0;k<16;++k)Check(Near(r.out[i].m[k],r.native[i].m[k],1e-4f),"first: the game's own pose");
        r.For(4);
        const Vec anchor=r.Anchor(),head=r.swing.Head();
        Check(Near(anchor.y,1.4166f/Feet,1e-4f)&&Near(anchor.x,0,1e-4f),"its top stays at the handle");
        Check(head.y<anchor.y-.97f*(chainM+ballM)&&std::fabs(head.x)<.01f&&std::fabs(head.z)<.01f,"hanging straight down");
        for(size_t i=0;i+1<r.out.size();++i) {
            Check(Near(math::Dot(math::Normalized(Row(r.out[i],0)),{0,-1,0}),1,.01f),"each link points down the chain");
            Check(Near(Length(Position(r.out[i+1])-Position(r.out[i])),.0896f,.0009f),"links keep their length");
            Check(Near(Det(r.out[i]),Det(r.native[i]),1e-3f),"turned, not stretched or mirrored");
        }
        Check(Near(Position(r.out[0]).y,1.4166f,1e-4f),"the first link stays on the handle");
        const Vec before=r.swing.Head();r.Pose();
        Check(Length(r.swing.Head()-before)<.001f&&r.stats.fastest<40,"settled");
        // Poses sooner than a step (several a frame) lay the same bones again.
        const unsigned steps=r.stats.steps;const auto laid=r.out;
        Check(r.Pose(.0005)&&r.Pose(.0005)&&r.stats.steps==steps,"no step between two poses of a frame");
        for(size_t i=0;i<laid.size();++i)for(int k=0;k<16;++k)Check(laid[i].m[k]==r.out[i].m[k],"the same bones");
    }
    {   // The hand moves: the ball lags, swings past, and comes back under the handle.
        Run r(Straight());r.For(4);
        bool lagged=false,past=false;
        for(int f=0;f<9;++f){r.item=Item(1,{.3f*Feet*float(f+1)/9,0,0});r.Pose();lagged|=r.swing.Head().x<r.Anchor().x-.02f;}
        for(int f=0;f<90;++f){r.Pose();past|=r.swing.Head().x>r.Anchor().x+.02f;}
        Check(lagged,"the ball lags behind a quick move");
        Check(past,"and swings on past the handle");
        Check(r.stats.fastest>.3f,"it moved");
        r.For(5);
        Check(std::fabs(r.swing.Head().x-r.Anchor().x)<.01f,"it settles under the handle again");
        Check(r.stats.starts==1&&!r.stats.jumps&&!r.stats.pauses,"one start only");
    }
    {   // Carried along at a steady run, it hangs as it does standing: the rub is about the hand, not the air.
        Run r(Straight());r.For(3);
        for(int f=0;f<450;++f){r.item=Item(1,{3*Feet*float(f+1)/90,0,0});Check(r.Pose(),"posed while running");}
        Check(std::fabs(r.swing.Head().x-r.Anchor().x)<.02f&&r.swing.Head().y<r.Anchor().y-.97f*(chainM+ballM),"hanging straight under the hand at 3 m/s");
        Check(!r.stats.jumps,"a run is no jump");
    }
    {   // Fresh starts: a teleport, a pause, another size.
        Run r(Straight());r.For(2);
        r.item=Item(1,{5*Feet,0,0});
        Check(r.Pose()&&r.stats.jumps==1,"a jump starts afresh");
        for(size_t i=0;i<r.out.size();++i)Check(Length(Position(r.out[i])-Position(r.native[i]))<1e-4f,"from the game's own pose");
        r.For(1);
        Check(r.Pose(.5)&&r.stats.pauses==1,"a pause starts afresh");
        r.item=Item(1.5f,{5*Feet,0,0});
        Check(r.Pose()&&r.stats.resized==1,"another size starts afresh");
    }
    {   // A snap turn: the world turns about the player in one frame; the chain keeps its shape on the item.
        Run r(Straight());r.For(1);   // still swinging down
        const auto laid=r.out;
        const float c=std::cos(.5236f),s=std::sin(.5236f);
        Mat turned=skel::bow::Identity();turned.m[0]=c;turned.m[2]=-s;turned.m[8]=s;turned.m[10]=c;Position(turned,{.4f*Feet,0,-.2f*Feet});
        r.item=turned;++r.turn;
        Check(r.Pose()&&r.stats.turns==1&&r.stats.starts==1,"carried, not started afresh");
        for(size_t i=0;i<laid.size();++i)Check(Length(Position(r.out[i])-Position(laid[i]))<1e-3f,"the same shape on the item");
    }
    {   // The floor: the ball rests on it, the links do not sink through it.
        Run r(Straight());r.floor=(1.4166f/Feet-.3f)*Feet;r.For(4);
        const float floor=1.4166f/Feet-.3f;
        Check(r.swing.Head().y>=floor+ballM-.002f,"the ball rests on the floor");
        for(int i=0;i<18;++i)Check(r.swing.Joint(i).y>=floor-.002f,"no link through it");
        for(size_t i=0;i+1<r.out.size();++i)Check(Near(Length(Position(r.out[i+1])-Position(r.out[i])),.0896f,.0009f),"links keep their length on the floor");
    }
    {   // Khalim's side chains run along their bones' -x: each still hangs, its bones turned the same way round.
        Run r(Straight(14,{-.028f,1.28f,0},{-.13f,0,0},{1,0,0}));
        r.For(4);
        Check(r.swing.Head().y<r.Anchor().y-.9f*(13*.13f/Feet),"a side chain hangs too");
        for(size_t i=0;i+1<r.out.size();++i)Check(Near(math::Dot(math::Normalized(Row(r.out[i],0)),{0,1,0}),1,.02f),"its links' -x points down the chain");
    }
    {   // A mirrored item (left-handed play draws the mirror image): it hangs down in the world just the same.
        Run r(Straight());Mat mirror=Item();mirror.m[0]=-1;r.item=mirror;
        r.For(4);
        Check(r.swing.Head().y<r.Anchor().y-.97f*(chainM+ballM),"hanging down");
        for(size_t i=0;i<r.out.size();++i)Check(Near(Det(r.out[i]),Det(r.native[i]),1e-3f),"each bone as handed as the game's");
    }
    {   // Left to the game: an item hidden by shrinking, a broken matrix, too few bones, a world of no size.
        Run r(Straight());r.For(1);
        const auto laid=r.out;
        r.item=Item(.001f);
        Check(!r.Pose()&&r.stats.refused==1,"a shrunk item: the game's pose");
        for(size_t i=0;i<laid.size();++i)for(int k=0;k<16;++k)Check(laid[i].m[k]==r.out[i].m[k],"out untouched");
        r.item=Item();r.item.m[5]=NAN;
        Check(!r.Pose(),"a broken matrix");
        r.item=Item();
        Check(r.Pose()&&r.stats.starts==2,"afterwards, a fresh start");
        std::vector<Mat> one(1,Bone({},{0,1,0})),out(1);Stats s;Swing w;
        Check(!w.Pose(one,Item(),Feet,NAN,1,0,out,s),"one bone is no chain");
        std::vector<Mat> two=Straight(4),outTwo(3);
        Check(!w.Pose(two,Item(),Feet,NAN,1,0,outTwo,s),"out the wrong size");
        outTwo.resize(4);
        Check(!w.Pose(two,Item(),0,NAN,1,0,outTwo,s)&&!w.Pose(two,Item(),NAN,NAN,1,0,outTwo,s),"no world size");
        Check(w.Pose(two,Item(),Feet,NAN,1,0,outTwo,s),"otherwise posed");
    }
    {   // Whirled hard (the hand round a 30 cm circle twice a second, turning as it goes): it stays whole and in reach.
        Run r(Straight());r.For(1);
        float farthest=0;
        for(int f=0;f<270;++f) {
            const float a=float(f)/90*2*3.14159f*2,c=std::cos(a),s=std::sin(a);
            Mat spun=skel::bow::Identity();spun.m[0]=c;spun.m[1]=s;spun.m[4]=-s;spun.m[5]=c;Position(spun,{.3f*Feet*c,.3f*Feet*s,0});
            r.item=spun;Check(r.Pose(),"posed while whirled");
            for(int i=0;i<18;++i)Check(Finite(r.swing.Joint(i))&&Finite(r.swing.Head()),"no point lost");
            farthest=std::max(farthest,Length(r.swing.Head()-r.Anchor()));
            for(size_t i=0;i+1<r.out.size();++i)Check(Near(Length(Position(r.out[i+1])-Position(r.out[i])),.0896f,.0009f),"links keep their length");
        }
        Check(farthest<=chainM+ballM+.002f,"never longer than the chain and the ball");
        Check(r.stats.fastest<=MaxSpeed+.001f&&r.stats.fastest>2,"flung, but no faster than the cap");
        Check(r.stats.starts==1&&!r.stats.jumps,"no fresh start");
    }
    {   // Slow and fast frame rates hang it the same.
        for(double rate:{45.,72.,144.}) {
            Run r(Straight());
            for(double t=0;t<4;t+=1/rate)r.Pose(1/rate);
            Check(r.swing.Head().y<r.Anchor().y-.97f*(chainM+ballM)&&std::fabs(r.swing.Head().x)<.01f,"hangs at any frame rate");
        }
    }
    std::puts("flail chain: ok");
    return 0;
}
