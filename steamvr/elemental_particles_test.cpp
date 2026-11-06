// The elemental hands' particles (elemental_particles.h): each look's particles, bounded, kept near the
// hand, trailing a moving hand, fading with it, a burst on a cast, gaps and bad input. No Direct3D.
#include "elemental_particles.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
using namespace framevr;
using namespace elements;
static void Check(bool value,const std::string& message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message.c_str());std::exit(1);}}
struct Sim {
    Scene scene;Effects effects;uint64_t at=100000;
    Sim(Element element,int skill,int hand=0) {
        scene.hands[size_t(hand)]={{{.2f,1.2f,-.4f},{0,0,0,1}},element,1,0,skill};
    }
    Hand& H(int h=0){return scene.hands[size_t(h)];}
    void Run(float seconds,float frame=1/90.f) {
        for(int i=0;i<int(seconds/frame+.5f);++i){at+=uint64_t(frame*1000+.5f);scene.at=at;scene.time=float(at%60000)/1000.f;effects.Step(scene);}
    }
    unsigned Of(Kind kind)const{unsigned n=0;for(unsigned i=0;i<scene.count;++i)n+=scene.drawn[i].kind==kind;return n;}
    float Farthest(math::Vec from)const {
        float most=0;
        for(unsigned i=0;i<scene.count;++i){const auto d=scene.drawn[i].p-from;most=std::max(most,std::sqrt(math::Dot(d,d)));}
        return most;
    }
    bool Finite()const {
        for(unsigned i=0;i<scene.count;++i){const auto& p=scene.drawn[i];if(!std::isfinite(p.p.x+p.p.y+p.p.z+p.size+p.intensity+p.life))return false;}
        return true;
    }
};
int main() {
    const struct {Element element;int skill;Kind must[3];const char* name;} looks[]={
        {Element::Fire,36,{Flame,Ember,Glow},"Fire Bolt"},{Element::Fire,47,{Flame,Core,Glow},"Fireball"},
        {Element::Lightning,38,{Arc,Spark,Plasma},"Charged Bolt"},{Element::Cold,39,{Mist,Flake,Glow},"Ice Bolt"},
        {Element::Cold,45,{Orb,Chunk,Mist},"Ice Blast"},{Element::Fire,999,{Flame,Ember,Glow},"the torch"}};
    for(const auto& l:looks) {
        const std::string name=l.name;
        Sim s(l.element,l.skill);s.Run(1.5f);
        for(const auto kind:l.must)Check(s.Of(kind)>0,name+": its kinds are drawn");
        Check(s.scene.count>5&&s.scene.count<=MaxDrawn&&s.effects.Count(0)<=Effects::MaxLive,name+": some, never more than the pass holds");
        const auto centre=s.H().pose.p;
        Check(s.Farthest(centre)<(l.skill==999?.75f:.35f),name+": a held hand's effect stays near it");
        Check(s.Finite(),name+": every particle finite");
        Check(s.effects.Count(1)==0,name+": the other hand, with no spell, has none");
    }
    {   // the hand's size: a Fire Bolt's flames and glow within a fist or two of it; the torch about twice that
        Sim fire(Element::Fire,36);fire.Run(1);Sim torch(Element::Fire,999);torch.Run(1);
        float a=0,b=0;
        for(unsigned i=0;i<fire.scene.count;++i)if(fire.scene.drawn[i].kind==Glow)a=fire.scene.drawn[i].size;
        for(unsigned i=0;i<torch.scene.count;++i)if(torch.scene.drawn[i].kind==Glow)b=torch.scene.drawn[i].size;
        Check(a>.03f&&a<.08f&&b>a*1.8f,"sizes: a hand's glow 3-8 cm, the torch's twice and more");
    }
    {   // a moving hand leaves its particles behind: they stay in the room
        Sim s(Element::Fire,36);s.Run(.8f);
        const auto before=s.H().pose.p;
        for(int i=0;i<30;++i){s.H().pose.p=s.H().pose.p+math::Vec{.01f,0,0};s.Run(1/90.f);}
        float behind=0;for(unsigned i=0;i<s.scene.count;++i)behind=std::max(behind,s.H().pose.p.x-s.scene.drawn[i].p.x);
        Check(behind>.15f,"moving 30 cm, its earlier flames are left along the way");
        unsigned between=0;for(unsigned i=0;i<s.scene.count;++i)between+=s.scene.drawn[i].p.x>before.x+.05f&&s.scene.drawn[i].p.x<s.H().pose.p.x-.05f;
        Check(between>3,"a trail between where it was and where it is, not gaps");
    }
    {   // orbiting flakes stay with the hand
        Sim s(Element::Cold,39);s.Run(1);
        s.H().pose.p=s.H().pose.p+math::Vec{0,0,-.5f};s.Run(.05f);
        unsigned near=0,flakes=0;
        for(unsigned i=0;i<s.scene.count;++i)if(s.scene.drawn[i].kind==Flake){++flakes;const auto d=s.scene.drawn[i].p-s.H().pose.p;near+=math::Dot(d,d)<.1f*.1f;}
        Check(flakes>0&&near==flakes,"the snowflakes circle the hand wherever it goes");
    }
    {   // the hand fading near the face: fewer spawned, dimmer; gone, the rest burn out
        Sim s(Element::Fire,36);s.Run(1);const float full=s.scene.drawn[0].intensity;const unsigned many=s.effects.Count(0);
        s.H().opacity=.3f;s.Run(1);
        Check(s.scene.drawn[0].intensity<full&&s.effects.Count(0)<many,"a hand fading: dimmer and fewer");
        s.H().element=Element::None;s.Run(.3f);
        Check(s.scene.count>0,"its spell gone, what it threw still burns out");
        s.Run(2);Check(s.scene.count==0,"then nothing");
    }
    {   // a confirmed cast throws a burst
        Sim a(Element::Lightning,38),b(Element::Lightning,38);a.Run(1);b.Run(1);
        b.H().pulse=1;a.Run(1/90.f);b.Run(1/90.f);
        Check(b.Of(Spark)>a.Of(Spark)+8&&b.scene.count>a.scene.count,"a cast: a burst of sparks");
        b.Run(1/90.f);const unsigned once=b.Of(Spark);b.Run(1/90.f);
        Check(b.Of(Spark)<=once+2,"the pulse held: one burst, not one a frame");
    }
    {   // a gap (a load, a menu) starts over; the scene hidden draws nothing
        Sim s(Element::Fire,36);s.Run(1);
        s.at+=Effects::GapMs+100;s.scene.at=s.at;s.effects.Step(s.scene);
        Check(s.effects.Count(0)==0,"after a gap: started over");
        s.Run(.5f);s.H().opacity=0;s.Run(1/90.f);Check(s.scene.count==0&&s.effects.Count(0)==0,"hidden: nothing, and nothing kept");
    }
    {   // a long frame steps at most MaxStep: nothing flies off
        Sim s(Element::Fire,36);s.Run(.5f);
        s.Run(.2f,.2f);Check(s.Finite()&&s.Farthest(s.H().pose.p)<.35f,"a hitch: bounded steps");
    }
    {   // bad input
        Sim s(Element::Fire,36);s.Run(.3f);
        s.H().pose.p.x=std::numeric_limits<float>::quiet_NaN();s.Run(.2f);
        Check(s.Finite(),"a NaN hand: no NaN particle");
        Sim t(Element::Fire,36);t.scene.time=std::numeric_limits<float>::infinity();t.scene.at=5;t.effects.Step(t.scene);
        Check(t.scene.count==0,"no time: nothing");
    }
    {   // both hands, different spells, within one pass
        Sim s(Element::Fire,999);s.scene.hands[1]={{{-.2f,1.2f,-.4f},{0,0,0,1}},Element::Fire,1,0,999};s.Run(3);
        Check(s.scene.count<=MaxDrawn&&s.effects.Count(0)>0&&s.effects.Count(1)>0,"two torches fit one pass");
        Sim m(Element::Lightning,38);m.scene.hands[1]={{{-.2f,1.2f,-.4f},{0,0,0,1}},Element::Cold,1,0,45};m.Run(1);
        Check(m.Of(Plasma)==1&&m.Of(Orb)==1,"a lightning hand and an ice hand, each its own");
    }
    std::puts("Elemental hand particles: six looks, bounded and near the hand, trails, orbits, fades, cast bursts, gaps and bad input passed");
    return 0;
}
