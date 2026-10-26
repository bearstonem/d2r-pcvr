#include "blood_screen.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static void CheckVisorExtent() {
    // Projection FOVs from the reported 2026-10-15 sessions: Quest 3 had the
    // same values through VDXR and SteamVR; Frame's splats sat too far out.
    const math::Fov quest{-.94248f,.94248f,-.95993f,.95993f};
    const math::Fov frame{-1.03239f,1.03239f,-1.04476f,1.04476f};
    auto oldExtent=[](math::Fov f){return blood::QuadExtent{8*std::tan(f.right)*1.05f,8*std::tan(f.up)*1.05f};};
    const auto oldQuest=oldExtent(quest),oldFrame=oldExtent(frame);
    const auto q=blood::VisorExtent(quest,quest),f=blood::VisorExtent(frame,frame);
    Check(std::fabs(q.width-oldQuest.width)<.0001f&&std::fabs(q.height-oldQuest.height)<.0001f,
          "preserve the Quest 3 placement verified through both runtimes");
    Check(std::fabs(f.width-q.width)<.0001f&&std::fabs(f.height-q.height)<.0001f,
          "Frame rim splats use the same angular footprint as the verified Quest visor");
    Check(f.width<oldFrame.width*.85f&&f.height<oldFrame.height*.85f,
          "bring the reported Frame splats inward on both axes");
    const math::Fov narrow{-.7f,.7f,-.75f,.75f};
    const auto n=blood::VisorExtent(narrow,narrow),oldNarrow=oldExtent(narrow);
    Check(n.width==oldNarrow.width&&n.height==oldNarrow.height,"smaller fields of view keep their original fit");
    const math::Fov asymmetricLeft{-.9f,.7f,-.8f,.85f},asymmetricRight{-.7f,.9f,-.8f,.85f};
    const auto a=blood::VisorExtent(asymmetricLeft,asymmetricRight);
    const auto swapped=blood::VisorExtent(asymmetricRight,asymmetricLeft);
    Check(a.width==swapped.width&&a.height==swapped.height&&a.width>n.width&&a.width<q.width,
          "asymmetric eye bounds fit consistently regardless of eye order");
    for(const math::Fov invalid:{math::Fov{},math::Fov{NAN,.8f,-.8f,.8f},math::Fov{-.8f,INFINITY,-.8f,.8f},math::Fov{-.8f,.8f,-.8f,1.6f}}) {
        Check(blood::VisorExtent(invalid,quest).width==0&&blood::VisorExtent(quest,invalid).height==0,
              "invalid eye FOVs suppress the optional visor quad");
        Check(blood::ViewExtent(invalid,quest).width==0&&blood::ViewExtent(quest,invalid).height==0,
              "invalid eye FOVs suppress the whole-view quad");
    }
    // Whole view (the guard strain light): the visor wherever its bound does not apply, the real edge where it does.
    for(const auto& fit:{std::array<math::Fov,2>{quest,quest},{narrow,narrow},{asymmetricLeft,asymmetricRight}}) {
        const auto v=blood::ViewExtent(fit[0],fit[1]),b=blood::VisorExtent(fit[0],fit[1]);
        Check(std::fabs(v.width/b.width-1)<1e-5f&&std::fabs(v.height/b.height-1)<1e-5f,"Quest 3 and narrower views: the whole view is the visor");
    }
    const auto wide=blood::ViewExtent(frame,frame);
    Check(std::fabs(wide.width-oldFrame.width)<.0001f&&std::fabs(wide.height-oldFrame.height)<.0001f&&wide.width>f.width*1.15f,
          "Frame: the whole view reaches its own rim, past the Quest-bound visor");
    const math::Quat outLeft{0,std::sin(.15f),0,std::cos(.15f)},outRight{0,-std::sin(.15f),0,std::cos(.15f)}; // eyes canted 0.3 rad outward
    const auto canted=blood::ViewExtent(quest,quest,outLeft,outRight);
    Check(canted.width>8*std::tan(.94248f+.29f)*1.05f&&canted.height>q.height,"canted eyes: the quad covers each eye's outer corners");
    Check(blood::ViewExtent(quest,quest,{0,0,0,0},{NAN,0,0,1}).width==blood::ViewExtent(quest,quest).width,"a missing eye turn reads as straight ahead");
    const math::Fov huge{-1.55f,1.55f,-1.55f,1.55f};
    const auto bounded=blood::ViewExtent(huge,huge,outLeft,outRight);
    Check(std::isfinite(bounded.width)&&bounded.width<=8*5.6713f*1.05f+.001f&&bounded.height<=8*5.6713f*1.05f+.001f,"bounded at about 80 degrees per side");
}
struct Sim {
    blood::Engine e;blood::Input in;blood::Output out;
    float lowest=1;
    Sim(float yaw=0){
        in.time=1;in.session=7;in.epoch=1;in.enabled=in.active=in.fresh=in.vitalsKnown=true;
        in.health=in.maxHealth=100;in.head={2,1.6f,-3};in.headRotation=math::Yaw(yaw);
        in.tracked[0]=in.tracked[1]=true;Rest();
    }
    void Rest(){for(int h=0;h<2;++h)in.hand[h]=Local({h?-.25f:.25f,-.55f,-.25f});}
    math::Vec Local(math::Vec p) const {return in.head+math::Rotate(in.headRotation,p);}
    blood::Output Step(int n=1){while(n--){in.time+=1/90.;out=e.Step(in);}return out;}
    // Side-to-side in front of the visor: amplitude metres either side, hz full cycles.
    int Scrub(int hand,int strokes,float amplitude=.07f,float hz=2,math::Vec centre={0,-.05f,-.15f},math::Vec axis={1,0,0}){
        int counted=0;const double start=in.time;
        const int frames=int(strokes/(2*hz)*90)+12;
        for(int i=0;i<frames;++i){
            const float phase=float((in.time-start)*hz*6.28318530718);
            in.hand[hand]=Local(centre+axis*(amplitude*std::sin(phase)));
            Step();counted+=out.stroke[hand];lowest=std::min(lowest,out.coverage);
        }
        Rest();Step(5);return counted;
    }
};
int main(){
    CheckVisorExtent();
    {Sim s;s.Step(30);Check(s.out.coverage==0&&s.out.visible==0,"full life draws nothing");}
    Check(blood::Target(70,100)==0&&blood::Target(55,100)==.35f&&blood::Target(30,100)==.65f&&blood::Target(10,100)==1,"three life bands");
    Check(blood::Target(0,100)<0&&blood::Target(NAN,100)<0&&blood::Target(50,0)<0,"dead or unknown life holds");
    {Sim s;s.Step(5);s.in.health=55;s.Step();Check(s.out.coverage>0&&s.out.coverage<.35f,"a new wound spreads rather than snapping");
     s.Step(60);Check(s.out.coverage==.35f&&s.out.visible==0x1fu&&!s.out.fading,"first threshold lands the five rim splats");
     s.in.health=10;s.Step(60);Check(s.out.coverage==1&&s.out.visible==0xffffu,"lowest band shows every splat");
     s.in.health=15;s.Step(30);Check(s.out.coverage==1&&!s.out.healed,"healing within a band changes nothing");
     s.in.health=100;s.Step();Check(s.out.healed&&s.out.coverage==0&&s.out.visible==0xffffu&&s.out.fading,"healing above every band starts a fade, not a snap");
     s.Step(44);Check(s.out.visible==0xffffu&&s.out.opacity[15]>.4f&&s.out.opacity[15]<.6f,"half a second in, the blood is half faded");
     s.Step(50);Check(s.out.visible==0&&!s.out.fading,"the heal fade finishes in about a second");}
    {Sim s;s.in.health=10;s.Step(60);s.in.health=45;s.Step(100);
     Check(s.out.coverage==.35f&&s.out.visible==0x1fu,"a partial heal fades only the bands it crossed");
     Check(s.out.opacity[0]==1&&s.out.opacity[4]==1,"blood for the current band stays");}
    for(float yaw:{-2.f,0.f,1.f}){Sim s(yaw);s.in.health=10;s.Step(60);
        Check(s.Scrub(0,8)>=5&&s.out.coverage==0,"the right hand wipes at any head yaw, even while hurt");}
    {Sim s;s.in.health=10;s.Step(60);
     const int strokes=s.Scrub(1,8);s.Step(25);
     Check(strokes>=5&&s.out.coverage==0&&s.out.visible==0,"left-hand scrubbing clears a hurt visor");
     Check(!s.out.block[0]&&!s.out.block[1],"combat returns once the hand leaves the face");
     s.Step(300);Check(s.out.visible==0,"without a new hit the visor stays clean");
     s.in.health=9.8f;s.Step();Check(s.out.visible==0,"life jitter below a hit is not a hit");
     s.in.health=8;s.Step(60);Check(s.out.coverage==1&&s.out.visible==0xffffu,"the next hit splashes the band straight back");}
    {Sim s;s.in.health=10;s.Step(60);s.Scrub(1,1);const auto after=s.out;
     Check(after.coverage<1&&after.coverage>.7f,"one stroke wipes some, not all");
     s.Step(25);int gone=0;for(int i=0;i<blood::Placements;++i)gone+=s.out.opacity[i]==0;
     Check(gone>=2,"a wiped splat smears away within a quarter second");}
    {Sim s;s.in.health=55;s.Step(60);s.Scrub(1,8);s.Step(30);s.in.health=30;s.Step(60);
     Check(s.out.coverage==.65f,"a worse band after wiping splashes again");}
    {Sim s;s.in.health=10;s.Step(60);s.Scrub(1,8);s.Step(30);s.in.health=100;s.Step();
     Check(!s.out.healed&&s.out.coverage==0,"healing a wiped visor has nothing to fade");}
    {Sim s;s.in.health=10;s.Step(60);
     bool blocked=false;const double start=s.in.time;
     for(int i=0;i<90;++i){s.in.hand[1]=s.Local({.07f*std::sin(float((s.in.time-start)*12.566f)),-.05f,-.15f});s.Step();blocked|=s.out.block[1];
         Check(!s.out.block[0],"the other hand keeps fighting");}
     Check(blocked,"an active scrub claims that hand from combat");}
    {Sim s;bool blocked=false;const double start=s.in.time;
     for(int i=0;i<90;++i){s.in.hand[1]=s.Local({.07f*std::sin(float((s.in.time-start)*12.566f)),-.05f,-.15f});s.Step();blocked|=s.out.block[1];}
     Check(!blocked,"a clean visor never claims a hand");}
    {Sim s;s.in.health=10;s.Step(60);
     s.in.busy[1]=true;Check(s.Scrub(1,8)==0,"a held bottle, squeezed trigger or bow draw never wipes");
     s.in.busy[1]=false;s.in.active=false;Check(s.Scrub(1,8)==0,"wheel, settings and menus never wipe");
     s.in.active=true;s.in.tracked[1]=false;Check(s.Scrub(1,8)==0,"untracked hand never wipes");
     s.in.tracked[1]=true;Check(s.Scrub(1,8,.07f,2,{0,-.55f,-.25f})==0,"side-to-side at the chest is not the face");
     Check(s.Scrub(1,8,.07f,2,{0,-.05f,-.15f},{0,1,0})==0,"vertical rubbing is not a side-to-side wipe");
     Check(s.Scrub(1,8,.07f,2,{0,-.05f,-.15f},{0,0,1})==0,"pushing toward the face is not a wipe");
     Check(s.Scrub(1,8,.07f,2,{0,-.05f,-.15f},{.6f,.8f,0})==0,"steep diagonal rubbing is not side to side");
     Check(s.Scrub(1,8,.02f)==0,"tracking jitter is not a stroke");
     Check(s.Scrub(1,3,.12f,.25f)==0,"slow drifting across the face is not a scrub");
     Check(s.Scrub(1,8,.15f,9)==0,"weapon-speed passes are swings, not wipes");
     Check(s.out.coverage==1&&s.out.visible==0xffffu,"rejected motions leave all blood");}
    {Sim s;s.in.health=10;s.Step(60);
     s.in.fresh=false;s.in.health=100;s.Step(30);Check(s.out.coverage==1,"stale snapshots cannot heal");
     s.Scrub(1,8);s.Step(30);s.in.health=5;s.Step(30);Check(s.out.coverage==0,"stale snapshots cannot splash");}
    {Sim s;s.in.health=10;s.Step(60);s.in.session=0;s.Step(30);Check(s.out.coverage==1,"loading screens keep the stain");
     s.in.session=7;s.Step();Check(s.out.coverage==1,"same hero returning keeps the stain");
     s.in.session=8;s.in.health=100;s.Step(30);Check(s.out.coverage==0&&s.out.visible==0,"a new game starts clean");}
    {Sim s;s.in.health=10;s.Step(60);s.in.health=0;s.Step(30);Check(s.out.coverage==1,"death holds the blood");
     s.in.health=100;s.Step(120);Check(s.out.visible==0,"a full-life resurrection fades it");}
    {Sim s;s.in.health=50;s.Step(60);Check(s.out.coverage==.35f,"setup: 50% life");
     s.in.maxHealth=160;s.Step(60);Check(s.out.band==.65f&&s.out.coverage==.35f&&s.out.visible==0x1fu,"a max-life buff alone never splashes");
     s.in.health=60;s.in.maxHealth=200;s.Step(30);Check(s.out.coverage==.35f,"a buff that adds less life than max never splashes");
     s.in.health=55;s.Step(60);Check(s.out.coverage==.65f,"the next hit splashes the buffed band");}
    {Sim s;s.in.health=30;s.Step(60);Check(s.out.coverage==.65f,"setup: 30% life");
     s.in.maxHealth=60;s.Step(60);Check(s.out.band==.35f&&s.out.coverage==.65f&&!s.out.healed&&s.out.visible==0x3ffu,"a buff expiring alone never fades");
     s.in.health=50;s.Step();Check(s.out.healed,"the next heal fades everything above its band");s.Step(100);Check(s.out.visible==0,"healed clean");}
    {Sim s;s.in.health=70;s.in.maxHealth=200;s.Step(60);Check(s.out.coverage==.65f,"setup: buffed 35%");
     s.in.health=60;s.in.maxHealth=60;s.Step(100);Check(s.out.band==0&&s.out.coverage==.65f&&!s.out.healed,"losing max life and capped life never fades");}
    {Sim s;s.in.health=10;s.Step(60);s.in.wipe=false;
     bool blocked=false;const double start=s.in.time;
     for(int i=0;i<180;++i){s.in.hand[1]=s.Local({.07f*std::sin(float((s.in.time-start)*12.566f)),-.05f,-.15f});s.Step();blocked|=s.out.block[1]||s.out.stroke[1];}
     Check(!blocked&&s.out.coverage==1,"with the gesture off, scrubbing neither wipes nor takes the hand from combat");
     s.Rest();s.in.health=100;s.Step(120);Check(s.out.visible==0,"with the gesture off, healing still clears the visor");
     s.in.health=10;s.Step(60);s.in.wipe=true;Check(s.Scrub(1,8)>=5&&s.out.coverage==0,"turning the gesture back on wipes again");}
    {Sim s;s.in.health=10;s.Step(60);s.in.enabled=false;s.Step();Check(s.out.coverage==0&&s.out.visible==0&&!s.out.block[1],"disabled draws and blocks nothing");}
    {Sim s;s.in.health=10;s.Step(60);
     const double start=s.in.time;int strokes=0;
     for(int i=0;i<200;++i){s.in.hand[1]=s.Local({.07f*std::sin(float((s.in.time-start)*12.566f)),-.05f,-.15f});
         if(i%9==4){s.in.time+=.3;}
         s.Step();strokes+=s.out.stroke[1];}
     Check(strokes==0,"frame hitches cannot assemble strokes");
     ++s.in.epoch;s.Step();Check(s.out.coverage==1,"recentering keeps the stain");}
    // Splat ages drive the drips: they start at appearance and reset on a wipe.
    {Sim s;s.in.health=55;s.Step(90);
     Check(s.out.age[0]>.9f&&s.out.age[0]<1.1f&&s.out.age[5]==0,"visible splats age; hidden ones do not");
     s.Scrub(1,8);s.Step(30);Check(s.out.visible==0,"wiped");
     s.in.health=50;s.Step(10);Check(s.out.age[0]<.2f,"a returning splat starts with fresh drips");}
    for(int i=0;i<blood::Placements;++i){
        Check(blood::VisibleFor(blood::Layout[i].threshold)>>i&1,"each splat appears at its threshold");
        if(i)Check(blood::Layout[i].threshold>blood::Layout[i-1].threshold,"splats appear in order and wipe in reverse");
        const float r=blood::Layout[i].radius;Check(r>blood::ClearRadius+.25f&&r<1,"splats sit outside the clear centre");}
    for(int i=0;i<blood::Placements;++i)for(int d=0;d<blood::DripsPerSplat;++d){
        const auto m=blood::Motion(i,d);
        Check(blood::DripLength(i,d,m.delay*.9f)==0,"drips wait before running");
        Check(blood::DripLength(i,d,m.delay+1)>0&&blood::DripLength(i,d,m.delay+2)>blood::DripLength(i,d,m.delay+1),"drips run slowly down");
        Check(blood::DripLength(i,d,1e6f)==int(m.length)&&m.speed<=16&&m.delay+m.length/m.speed<36,"drips dry within about half a minute");}
    {std::array<float,blood::Placements> age{},full{};full.fill(1);uint64_t previous=blood::PaintKey(0xffff,age,full);int changes=0;
     for(float t=.05f;t<40;t+=.05f){age.fill(t);const auto key=blood::PaintKey(0xffff,age,full);changes+=key!=previous;previous=key;}
     Check(changes>20,"growing drips request repaints");
     age.fill(40);const auto dry=blood::PaintKey(0xffff,age,full);age.fill(80);Check(blood::PaintKey(0xffff,age,full)==dry,"dried drips stop repainting");
     Check(blood::PaintKey(0x1f,age,full)!=blood::PaintKey(0x3f,age,full),"a new splat repaints");
     auto half=full;half[3]=.5f;Check(blood::PaintKey(0xffff,age,half)!=dry,"a fading splat repaints");
     half[3]=1-.01f;Check(blood::PaintKey(0xffff,age,half)==dry,"sub-step opacity changes do not repaint");}
    // Compositor with the fallback art (the shipped strip has the same layout).
    const auto patches=blood::Prepare(blood::FallbackStrip());
    int drips=0;for(const auto& p:patches){Check(!p.rgba.empty(),"every placement renders");for(const auto& d:p.drips)drips+=d.used;}
    Check(drips>=blood::Placements,"large blobs grow drips");
    Check(blood::Prepare(std::vector<uint8_t>(16)).at(0).rgba.empty(),"a wrong-sized strip renders nothing");
    std::array<float,blood::Placements> fresh{},old{},full{},half{};old.fill(60);full.fill(1);half.fill(.5f);
    const auto none=blood::Compose(patches,0,fresh,full),some=blood::Compose(patches,0x1f,fresh,full),all=blood::Compose(patches,0xffff,fresh,full),
        dripped=blood::Compose(patches,0xffff,old,full),faded=blood::Compose(patches,0xffff,old,half),gone=blood::Compose(patches,0xffff,old,fresh);
    auto alpha=[](const std::vector<uint8_t>& p,int x,int y){return p[(size_t(y)*blood::Size+x)*4+3];};
    long sums[4]{};int grew=0;
    for(int y=0;y<blood::Size;++y)for(int x=0;x<blood::Size;++x){
        Check(alpha(none,x,y)==0&&alpha(gone,x,y)==0,"no splats or zero opacity, no blood");
        Check(alpha(faded,x,y)<=alpha(dripped,x,y)&&(alpha(dripped,x,y)<8||alpha(faded,x,y)>0),"half opacity dims every splat and drip without removing it");
        Check(alpha(all,x,y)>=alpha(some,x,y)&&alpha(dripped,x,y)>=alpha(all,x,y),"more splats and longer drips only add blood");
        const float u=(x+.5f)/blood::Size*2-1,v=(y+.5f)/blood::Size*2-1;
        if(u*u*.85f+v*v<blood::ClearRadius*blood::ClearRadius)Check(alpha(dripped,x,y)==0,"the view's centre is never covered");
        sums[1]+=alpha(some,x,y);sums[2]+=alpha(all,x,y);sums[3]+=alpha(dripped,x,y);grew+=alpha(dripped,x,y)>alpha(all,x,y);
    }
    Check(sums[2]>sums[1]&&sums[3]>sums[2]&&grew>500,"coverage grows with wounds, then with drips");
    std::puts("Blood visor: headset sizing, bands, heal fades, scrub at any time, re-splash on hits, max-life changes, wipe toggle, exclusions, sessions, hitches, drips and compositor passed");
}
