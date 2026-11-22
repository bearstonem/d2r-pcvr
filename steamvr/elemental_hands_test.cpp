#include "elemental_hands.h"
#include "spell_sprites.h"
#include "armor_mist.h"
#include "assassin_charges.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace framevr;
using namespace elements;
static void Check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
// A synthetic sprite cache (no game data); corrupt picks one defect.
static std::vector<uint8_t> Cache(int corrupt=0) {
    std::vector<uint8_t> d;
    auto put=[&](uint32_t v){d.insert(d.end(),reinterpret_cast<uint8_t*>(&v),reinterpret_cast<uint8_t*>(&v)+4);};
    put(0x31505353);put(corrupt==8?1:sprites::Version);put(sprites::Count);
    for(unsigned i=0;i<sprites::Count;++i) {
        const uint32_t format=sprites::Ramp(i)?29:i==sprites::Distortion?83:i==sprites::FrozenOrb||i==sprites::Flakes?77:80,
            width=corrupt==3&&i==sprites::SoftFlame?6:8;
        put(corrupt==1&&i==sprites::Flash?5:i);put(width);put(4);put(corrupt==2&&i==sprites::FlameRamp?80:format);put(2);
        for(uint32_t m=0;m<2;++m) {
            const auto bytes=uint32_t(sprites::MipBytes(format,std::max(1u,width>>m),std::max(1u,4u>>m)))+(corrupt==4&&m==1?1:0);
            put(bytes);d.insert(d.end(),bytes,uint8_t(i));
        }
    }
    if(corrupt==5)d.push_back(0);
    if(corrupt==6)d.pop_back();
    if(corrupt==7)d[0]^=1;
    return d;
}
struct Sim {
    Input in;
    interaction::Snapshot snapshot;
    State state;
    Scene scene;
    Sim() {
        in.active=in.tracked[0]=in.tracked[1]=true;
        in.context={7,9,11,13};in.at=1000;
        in.hands[0].p={.2f,0,-.5f};in.hands[1].p={-.2f,0,-.5f};
        snapshot.session=7;snapshot.skillClass=1;snapshot.vitalsKnown=true;snapshot.health=50;
        snapshot.actionKnown=true;snapshot.actionMode=1;
        Bind(0,47);Bind(4,45);Step(0);Step(120);
    }
    void Bind(int slot,int skill){auto& s=snapshot.skills[slot];s.id=skill;s.known=s.available=true;}
    Scene Step(uint64_t ms=10){in.at+=ms;snapshot.at=in.at;return scene=state.Step(in,snapshot);}
    Cast Event(int h)const {
        const int slot=in.casting[h];const auto& s=snapshot.skills[slot];
        return {in.context,in.at,in.activation[h],slot,s.id,s.owner};
    }
};
int main() {
    {
        Input in;in.head.p={0,0,0};in.tracked[0]=true;
        in.hands[0].p={0,-.1f,-.5f};in.hands[0].q={0,0,0,1};
        const std::array<math::Vec,2> arms{math::Vec{0,.3f,0},math::Vec{}};
        Scene mist;mist.count=4;
        mist.drawn[0].kind=PsychicMist;mist.drawn[0].p={0,0,-1};
        mist.drawn[1].kind=WhiteMist;mist.drawn[1].p={0,0,-1};
        mist.drawn[2].kind=PsychicMist;mist.drawn[2].p={0,0,-.4f};
        mist.drawn[3].kind=Glow;mist.drawn[3].p={0,0,-1};
        armormist::Occlude(mist,in,arms,{});
        Check(mist.drawn[0].intensity==0&&mist.drawn[1].intensity==0,"both mist colours hide behind a forearm");
        Check(mist.drawn[2].intensity==1&&mist.drawn[3].intensity==1,"near mist and unrelated effects stay visible");
        in.tracked[0]=false;mist.drawn[0].intensity=1;
        armormist::Occlude(mist,in,arms,{});
        Check(mist.drawn[0].intensity==1,"lost tracking never creates an invisible arm occluder");
    }
    Check(armocclusion::Hidden({0,0,0},{0,0,-1},{0,-.1f,-.5f},{0,.2f,-.5f}),"forearm hides a charge behind it");
    Check(!armocclusion::Hidden({0,0,0},{0,0,-.4f},{0,-.1f,-.5f},{0,.2f,-.5f}),"charge in front of arm remains visible");
    Check(!armocclusion::Hidden({0,0,0},{.2f,0,-1},{0,-.1f,-.5f},{0,.2f,-.5f}),"charge beside arm remains visible");
    {
        Sim s;s.snapshot.effects.known=true;s.snapshot.effectsAt=s.in.at;
        for(unsigned group=0;group<6;++group)for(int n=1;n<=3;++n) {
            s.snapshot.effects={};s.snapshot.effects.known=true;s.snapshot.effects.Set(effects::ChargeStates[group]);
            effects::CaptureCharges(s.snapshot.effects,[&](int stat,int& value){value=stat==effects::ChargeStats[group]?n:0;return true;});
            Check(s.snapshot.effects.chargesKnown,"native charge counter validated");
            const std::array<math::Vec,2> axes{math::Vec{0,0,1},math::Vec{0,0,1}};
            auto scene=assassincharges::Scene(s.in,s.snapshot,2,axes);
            Check(scene.count==unsigned(n*2)&&scene.chargeOnly,"every skill draws exact 1/2/3 charges on both arms");
            Check(!assassincharges::Scene(s.in,s.snapshot,0,axes).Visible()&&!assassincharges::Scene(s.in,s.snapshot,1,axes).Visible(),"Off and Original draw no custom charges");
            s.in.hands[0].p=s.in.head.p;
            Check(assassincharges::Scene(s.in,s.snapshot,2,axes).count==unsigned(n),"near-face charges fade one arm only");
            s.in.hands[0].p={.2f,0,-.5f};
        }
        for(int bad:{-1,4,999}) {
            effects::CaptureCharges(s.snapshot.effects,[&](int,int& value){value=bad;return true;});
            Check(!s.snapshot.effects.chargesKnown,"invalid charge counts fail open");
        }
        s.snapshot.effects={};s.snapshot.effects.known=true;
        for(int state:effects::ChargeStates)s.snapshot.effects.Set(state);
        effects::CaptureCharges(s.snapshot.effects,[](int,int& value){value=3;return true;});
        auto stacked=assassincharges::Scene(s.in,s.snapshot,2,{math::Vec{0,0,1},math::Vec{0,0,1}});
        Check(stacked.count==36,"all six charged skills coexist without losing orbs");
        s.snapshot.effectsAt=s.in.at-1001;
        Check(!assassincharges::Scene(s.in,s.snapshot,2,{}).Visible(),"stale counts disappear");
    }

    {
        Sim s;s.snapshot.effects.known=true;s.snapshot.effects.Set(206);s.snapshot.effectsAt=s.in.at;
        s.snapshot.skillClass=6; // native state, never a class or selected spell check
        auto ward=armormist::Scene(s.in,s.snapshot,true);
        const std::array<math::Vec,2> axis{math::Vec{0,0,1},math::Vec{0,0,1}};
        const std::array<math::Vec,2> wrist{math::Vec{.035f,0,-.02f},math::Vec{-.035f,0,-.02f}};
        auto placed=armormist::Scene(s.in,s.snapshot,true,axis,206,Element::Psychic,387,wrist,.10f);
        for(int h=0;h<2;++h) {
            const auto expected=s.in.hands[h].p+math::Rotate(s.in.hands[h].q,wrist[h]+axis[h]*.10f);
            const auto delta=placed.hands[h].pose.p-expected;
            Check(math::Dot(delta,delta)<1e-10f,"mist follows the drawn wrist toward elbow, including controller offset");
        }
        Check(ward.hands[0].element==Element::Psychic&&ward.hands[1].opacity==1,"ward surrounds both tracked hands");
        Check(!armormist::Scene(s.in,s.snapshot,false).Visible(),"Off has no replacement");
        s.in.hands[0].p=s.in.head.p;
        ward=armormist::Scene(s.in,s.snapshot,true);
        Check(ward.hands[0].opacity==0&&ward.hands[1].opacity==1,"near-face hand fades independently");
        s.in.tracked[1]=false;Check(!armormist::Scene(s.in,s.snapshot,true).Visible(),"tracking loss hides mist");
        s.in.tracked[1]=true;s.snapshot.effectsAt=s.in.at-1001;
        Check(!armormist::Scene(s.in,s.snapshot,true).Visible(),"stale ward state never renews protection visuals");
        s.snapshot.effectsAt=s.in.at;s.snapshot.effects={};
        Check(!armormist::Scene(s.in,s.snapshot,true).Visible(),"ward removal hides mist immediately");
        s.snapshot.effects.known=true;s.snapshot.effects.Set(206);s.snapshot.health=0;
        Check(!armormist::Scene(s.in,s.snapshot,true).Visible(),"death clears mist");
    }

    {
        Sim s;s.snapshot.effects.known=true;s.snapshot.effects.Set(151);s.snapshot.effectsAt=s.in.at;
        auto wind=armormist::Scene(s.in,s.snapshot,true,{},151,Element::Wind,235);
        Check(wind.hands[0].element==Element::Wind&&wind.hands[1].opacity==1,"Cyclone state shows white mist on both arms");
        Check(!armormist::Scene(s.in,s.snapshot,true).Visible(),"Cyclone never selects Psychic Ward mist");
        s.snapshot.effects.Set(206);
        Check(armormist::Scene(s.in,s.snapshot,true).Visible()&&armormist::Scene(s.in,s.snapshot,true,{},151,Element::Wind,235).Visible(),"both armors can coexist");
        s.snapshot.effects={};s.snapshot.effects.known=true;s.snapshot.effects.Set(14);
        Check(!armormist::Scene(s.in,s.snapshot,true,{},151,Element::Wind,235).Visible(),"Bone Armor never selects Cyclone mist");
    }
    for(int id:{36,41,46,47,51,52,56,62})Check(Classify(id)==Element::Fire,"active fire mapping");
    for(int id:{39,40,44,45,50,55,59,60,64})Check(Classify(id)==Element::Cold,"active cold mapping");
    for(int id:{38,42,48,49,53,57})Check(Classify(id)==Element::Lightning,"active lightning mapping");
    for(int id:{-1,0,37,43,54,58,61,63,65,219,357,4095})Check(Classify(id)==Element::None,"utility/passive/unknown neutral");
    Check(SpellLook(36,Element::Fire)==Look::FireBolt&&SpellLook(47,Element::Fire)==Look::Fireball&&
          SpellLook(38,Element::Lightning)==Look::ChargedBolt&&SpellLook(39,Element::Cold)==Look::IceBolt&&
          SpellLook(45,Element::Cold)==Look::IceBlast&&SpellLook(999,Element::Fire)==Look::Torch,"each hand spell shows its own effect");
    Check(SpellLook(51,Element::Fire)==Look::FireBolt&&SpellLook(49,Element::Lightning)==Look::ChargedBolt&&
          SpellLook(44,Element::Cold)==Look::IceBolt&&SpellLook(64,Element::Cold)==Look::IceBlast,"other skills take their element's look");
    {   // a spell hand's light: its element's colour, fire flickering and lightning crackling, frost even; none without
        const auto fire=HandLight(Element::Fire,1,1000,0),cold=HandLight(Element::Cold,1,1000,0);
        Check(fire.rgb[0]>fire.rgb[2]&&cold.rgb[2]>cold.rgb[0]&&fire.radius>cold.radius&&cold.strength>.3f,"fire orange and farther, frost cyan");
        float lo=1,hi=0,clo=1,chi=0;
        for(uint64_t ms=0;ms<2000;ms+=7){const float s=HandLight(Element::Lightning,1,ms,1).strength;lo=std::min(lo,s);hi=std::max(hi,s);
            const float c=HandLight(Element::Cold,1,ms,1).strength;clo=std::min(clo,c);chi=std::max(chi,c);}
        Check(hi-lo>.4f&&chi-clo<.15f,"lightning crackles, frost stays even");
        Check(HandLight(Element::Fire,.5f,1000,0).strength<fire.strength&&HandLight(Element::None,1,1000,0).strength==0&&
              HandLight(Element::Fire,std::numeric_limits<float>::quiet_NaN(),1000,0).strength==0,"dimmed with the hand, none without a spell or with a bad opacity");
    }
    {
        const auto textures=sprites::Parse(Cache());
        Check(textures.size()==sprites::Count&&textures[sprites::FlameRamp].format==29&&textures[sprites::FrozenOrb].format==77&&
              textures[sprites::Distortion].format==83&&textures[sprites::Distortion].mips[0].size()==32&&
              textures[sprites::Flash].format==80&&textures[sprites::Flash].mips.size()==2&&textures[sprites::Flash].mips[1].size()==8,
              "spell sprite cache parses");
        for(int corrupt=1;corrupt<9;++corrupt) {   // 8: a version 1 cache (an older install)
            bool rejected=false;
            try{sprites::Parse(Cache(corrupt));}catch(const std::exception&){rejected=true;}
            Check(rejected,"malformed spell sprite cache rejected");
        }
    }
    {
        Sim s;Check(s.scene.hands[0].element==Element::Fire&&s.scene.hands[1].element==Element::Cold&&
            s.scene.hands[0].opacity==1&&s.scene.hands[1].opacity==1,"independent trigger elements reach idle visibility");
        Check(s.scene.hands[0].skill==47&&s.scene.hands[1].skill==45,"each hand carries its spell");
        Check(s.scene.hands[0].pose.p.x==.2f&&s.scene.hands[1].pose.p.x==-.2f,"room hand positions preserved");
        s.Bind(1,49);s.in.casting[0]=1;s.Step();s.Step(120);
        Check(s.scene.hands[0].element==Element::Lightning&&s.scene.hands[1].element==Element::Cold,"shortcut affects only its own hand");
        s.in.casting[0]=-1;s.Step();Check(s.scene.hands[0].element==Element::Fire,"release restores trigger element");
        s.Bind(1,54);s.in.casting[0]=1;s.Step();Check(s.scene.hands[0].opacity==0,"utility shortcut suppresses idle element");
        s.in.casting[0]=4;s.Step();Check(s.scene.hands[0].opacity==0,"foreign hand slot cannot borrow an element");
    }
    for(int reason=1;reason<12;++reason) {
        Sim s;
        if(reason==1)s.in.active=false;
        if(reason==2)s.snapshot.skillClass=0;
        if(reason==3)s.snapshot.skillClass=-1;
        if(reason==4)s.snapshot.health=0;
        if(reason==5)s.snapshot.vitalsKnown=false;
        if(reason==6)s.snapshot.session=8;
        if(reason==7)s.in.head.q.w=0;
        if(reason==8)s.snapshot.health=std::numeric_limits<float>::quiet_NaN();
        if(reason==9)s.snapshot.actionKnown=false;
        if(reason==10)s.snapshot.actionMode=0;
        if(reason==11)s.snapshot.actionMode=17;
        Check(!s.Step().Visible(),"inactive/other class/dead/invalid state clears both hands");
    }
    {
        Sim s;s.in.at+=651;Check(!s.state.Step(s.in,s.snapshot).Visible(),"stale snapshot clears effects");
        Check(!Current(s.scene,7,s.scene.at+151)&&!Current(s.scene,8,s.scene.at)&&
            !Current(s.scene,7,s.scene.at-1)&&Current(s.scene,7,s.scene.at+150),"render refuses stale, foreign and future scenes");
    }
    for(int reason=0;reason<5;++reason) {
        Sim s;
        if(reason==0)s.in.tracked[0]=false;
        if(reason==1)s.in.occupied[0]=true;
        if(reason==2)s.snapshot.skills[0].available=false;
        if(reason==3)s.snapshot.skills[0].known=false;
        if(reason==4)s.in.hands[0].p.x=std::numeric_limits<float>::infinity();
        s.Step();Check(s.scene.hands[0].opacity==0&&s.scene.hands[1].opacity==1,"hand loss/occupancy/unknown binding leaves the other hand alone");
    }
    {
        Sim s;s.in.hands[0].p={0,0,-.15f};s.Step();Check(s.scene.hands[0].opacity==0,"face proximity hides aura");
        s.in.hands[0].p={0,0,-.225f};s.Step();Check(s.scene.hands[0].opacity>.49f&&s.scene.hands[0].opacity<.51f,"face proximity fades continuously");
    }
    {
        Sim s;s.in.casting[0]=0;s.in.activation[0]=3;s.Step();
        Check(s.scene.hands[0].pulse==0,"holding a trigger alone never claims cast success");
        s.state.Accepted(0,s.Event(0));s.Step();
        Check(s.scene.hands[0].pulse>.9f&&s.scene.hands[1].pulse==0,"confirmed cast pulses its own hand");
        s.Step(100);const float decayed=s.scene.hands[0].pulse;
        Check(decayed>0&&decayed<.6f,"success pulse decays");s.Step(110);
        Check(s.scene.hands[0].pulse==0,"success pulse expires without another cast");
    }
    for(int reason=0;reason<10;++reason) {
        Sim s;s.in.casting[0]=0;s.in.activation[0]=3;s.Step();auto event=s.Event(0);
        if(reason==0)++event.context.session;
        if(reason==1)++event.context.epoch;
        if(reason==2)++event.context.binding;
        if(reason==3)++event.context.equipment;
        if(reason==4)++event.activation;
        if(reason==5)event.skill=36;
        if(reason==6)event.owner=42;
        if(reason==7)event.at-=221;
        if(reason==8)event.at+=100;
        if(reason==9)event.slot=1;
        s.state.Accepted(0,event);s.Step();Check(s.scene.hands[0].pulse==0,"stale or mismatched cast feedback discarded");
    }
    {
        Sim s;s.in.casting[0]=0;s.in.activation[0]=1;s.Step();s.state.Accepted(0,s.Event(0));
        s.in.occupied[0]=true;s.Step();s.in.occupied[0]=false;s.Step();
        Check(s.scene.hands[0].pulse==0,"suppression cannot replay an accepted cast on resume");
        s.state.Accepted(0,s.Event(0));s.Step(151);Check(s.scene.hands[0].pulse==0,"input stall clears pending feedback");
        s.state.Accepted(0,s.Event(0));--s.in.at;s.Step(0);Check(s.scene.hands[0].pulse==0,"clock rollback clears pending feedback");
    }
    std::puts("Elemental hand state: skill selection, spell looks, sprite cache, independent hands, lifecycle, proximity and confirmed-cast feedback passed");
}
