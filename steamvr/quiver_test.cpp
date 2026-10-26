#include "bow_flight.h"
#include "gameplay.h"
#include "hand_controls.h"
#include "client_retirement.h"
#include "quiver_impact.h"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace framevr;
using namespace framevr::bowflight;
static void Check(bool ok,const char* what) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
static bool Near(float a,float b){return std::fabs(a-b)<.0005f;}
static Shot Release() {
    Shot s;s.origin={100,1.4f,100};s.direction={0,0,-1};s.player={100,0,100};
    s.unitsPerMetre=4;s.draw=1;s.session=11;s.epoch=7;s.sequence=1;s.at=1000;s.equipment=42;return s;
}
static Vec Point(const float* m,Vec p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
// Child world = local * parent (row vectors): identity orientation, scale 1.
static void Child(const float* parent,Vec offset,float out[16]) {
    for(int i=0;i<12;++i)out[i]=parent[i];
    const auto p=Point(parent,offset);out[12]=p.x;out[13]=p.y;out[14]=p.z;out[15]=1;
}
int main() {
    using weaponskills::Quiver;
    {
        // Low shots hit with the tip. On the first sweep the tail may not
        // have moved at all; reconstructing from tail distance loses contact.
        for(float floor:{0.f,6.f,-3.f}) {
            Flight f;f.shot=Release();f.shot.skill=6;f.shot.missile=27;
            f.shot.player.y=floor;f.shot.origin.y=floor+.3f;f.shot.direction={0,-1,0};
            f.Contact({100,floor+.015f,100});
            Check(Near(f.ImpactPoint().y,floor+.015f)&&f.distance==0,"first-sweep contact survives zero tail travel");
            Check(!Near(f.TipAt(0).y,f.ImpactPoint().y),"negative control: reconstructed first-tick tip is below ground");
            f.shot.direction={.6f,-.8f,0};f.distance=2;
            const auto ground=f.TipAt(f.distance);f.Contact(ground);
            Check(Near(f.Point().y-f.ImpactPoint().y,.6f),"negative control: arrow tail floats 60 cm above its tip");
            for(const auto pair:{std::pair{27,204},std::pair{12,29},std::pair{28,30},std::pair{40,30},std::pair{41,42},std::pair{41,265},std::pair{87,88}}) {
                f.shot.missile=pair.first;
                Check(QuiverImpactMissile(pair.first,pair.second),"native arrow explosion is eligible");
                const auto grid=GridPoint(f.shot,f.ImpactPoint())*f.shot.unitsPerMetre;
                const int x=int(grid.x*.5f),y=int(grid.z*.5f);
                const auto impact=ImpactOrigin(f,x,y);
                Check(Length(impact-f.ImpactPoint()*f.shot.unitsPerMetre)<.0005f,"impact retains floor height and sub-cell contact");
                Check(Length(ImpactOrigin(f,x+1,y-2)-impact-Vec{2,0,-4})<.0005f,"native splash spreads around contact instead of collapsing to center");
                // Nonidentity native orientation and scale, with stale translation.
                float m[16]{0,0,-2,0, 0,3,0,0, 4,0,0,0, 10,20,30,1};
                ImpactMatrix(impact,pair.second,m);
                Check(Length(Point(m,QuiverImpactOffset(pair.second))-impact)<.0005f,"authored emitter height/forward offset cancel at contact");
                Check(m[2]==-2&&m[5]==3&&m[8]==4,"impact keeps native effect size and orientation");
            }
        }
        Check(!QuiverImpactMissile(0,204)&&!QuiverImpactMissile(27,30)&&!QuiverImpactMissile(85,67),"basic arrows, unrelated effects and persistent fire stay native");
        Flight spell;spell.shot=Release();spell.shot.kind=Kind::Spell;spell.distance=3;
        Check(Length(spell.ImpactPoint()-spell.Point())<.0005f,"zero-length straight spell fallback is unchanged");
        spell.shot.skill=38;spell.nativePoint={12,3,14};
        Check(Length(spell.ImpactPoint()-spell.nativePoint)<.0005f,"native ground spell fallback is unchanged");
    }
    {
        // Native child creation can bind HD entities before it returns, or
        // after asynchronous asset loading. Full unit/entity generations and
        // monotonic tickets prevent reuse from moving a different effect.
        ImpactVisuals effects;Flight f;f.shot=Release();f.shot.skill=6;f.shot.missile=27;
        f.Contact({100.1f,.015f,100.2f});
        const auto grid=GridPoint(f.shot,f.ImpactPoint())*f.shot.unitsPerMetre;
        const uint32_t magic[3]{3,204,700},reused[3]{3,204,701},other[3]{3,30,700};
        auto ticket=effects.Reserve(f,204,1000);
        Check(ticket&&effects.Owns(ticket,0x1000,magic),"synchronous creation owns only its expected native effect");
        Check(!effects.Owns(0,0x1000,magic)&&!effects.Owns(ticket,0x1000,other),"nested unrelated allocation cannot inherit effect binding");
        Check(!effects.Bind(ticket,0x1000,magic,42,false,&grid),"child emitter does not become effect root");
        Check(effects.Bind(ticket,0x1000,magic,42,true,&grid),"early native root binds");
        effects.Complete(ticket,0x1000,magic,&grid);
        Check(effects.Owns(0,0x1000,magic)&&!effects.Owns(0,0x1000,reused),"returned unit keeps exact identity");
        float m[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        Check(effects.Matrix(42,11,m)&&Length(Point(m,{0,4.75f,1})-f.ImpactPoint()*4)<.0005f,"native emitter appears at low impact");
        Check(!effects.Matrix(42+(1u<<20),11,m)&&!effects.Matrix(42,12,m),"recycled renderer generation and different session are untouched");
        effects.Forget(0x1000);Check(!effects.Matrix(42,11,m),"native destruction clears placement");
        effects.Expire(11,1001);
        ticket=effects.Reserve(f,204,1002);effects.Complete(ticket,0x2000,magic,&grid);
        Check(effects.Bind(0,0x2000,magic,43,true,&grid),"delayed native HD initialization binds completed client");
        effects.Reset();const auto fresh=effects.Reserve(f,204,1003);
        effects.Complete(ticket,0,magic);Check(effects.Owns(fresh,0x3000,magic),"old completion cannot clear new session reservation");
        effects.Complete(fresh,0,magic);effects.Expire(11,1004);
        Check(!effects.Pending(),"failed native allocation releases reservation");
        for(int i=0;i<128;++i)Check(effects.Reserve(f,204,2000)!=0,"bounded tracker accepts available effects");
        Check(!effects.Reserve(f,204,2000),"tracking saturation falls back without affecting native creation");
        effects.Expire(11,7000);Check(!effects.Pending(),"missed native deletion cannot retain placement forever");
        Check(effects.Reserve(f,204,7001)!=0,"expired slots are reusable");
        effects.Expire(12,7002);Check(!effects.Pending(),"session change drops old visual records");
    }
    {
        // Skills.txt srvdofunc 0 arrow skills and their Missiles.txt ids only.
        for(int id:{6,7,11,16,21,27,31})Check(weaponskills::QuiverSkill(id)&&weaponskills::Bow(id),"single-missile arrow skill loads");
        for(int id:{0,12,22,26,36,97})Check(!weaponskills::QuiverSkill(id),"volleys, guided, basic and spells stay native");
        for(int id:{12,27,28,40,41,85,87})Check(weaponskills::QuiverMissile(id),"arrow skill missile");
        for(int id:{-1,0,1,31,59,213})Check(!weaponskills::QuiverMissile(id),"basic/bolt/spell/volley missile rejected");
    }
    {
        Quiver q;
        auto r=q.Step(5,3,7,-1,12,true);
        Check(r.consumed&&r.change==Quiver::Change::Loaded&&q.Current()==weaponskills::Loaded{7,-1,12},"press loads Fire Arrow");
        for(int i=0;i<20;++i){r=q.Step(5,3,7,-1,12,true);Check(r.consumed&&r.change==Quiver::Change::None,"held press toggles once and never casts");}
        r=q.Step(5,-1,-1,-1,-1,true);Check(!r.consumed&&q.Current().skill==7,"release keeps the arrow loaded");
        r=q.Step(5,3,7,-1,12,true);Check(r.consumed&&r.change==Quiver::Change::Unloaded&&!q.Current().Active(),"same button again: basic arrows");
        q.Step(5,-1,-1,-1,-1,true);
        q.Step(5,3,7,-1,12,true);q.Step(5,-1,-1,-1,-1,true);
        r=q.Step(5,4,11,-1,40,true);Check(r.change==Quiver::Change::Loaded&&q.Current().skill==11,"other skill swaps directly");
        q.Step(5,-1,-1,-1,-1,true);
        r=q.Step(5,3,7,2,12,true);Check(r.change==Quiver::Change::Loaded&&q.Current().owner==2,"item-granted copy is a different arrow");
        q.Step(5,-1,-1,-1,-1,true);
        auto fresh=[&](int slot,int skill,int missile,bool eligible){q.Step(5,-1,-1,-1,-1,true);return q.Step(5,slot,skill,-1,missile,eligible);};
        r=fresh(2,36,58,true);Check(!r.consumed&&q.Current().skill==7,"non-arrow skill casts natively, quiver kept");
        r=fresh(2,12,-1,true);Check(!r.consumed,"Multiple Shot keeps its native button");
        r=fresh(3,7,59,true);Check(!r.consumed,"unexpected missile id is not loaded");
        r=fresh(3,11,40,false);Check(!r.consumed&&q.Current().skill==7,"without a ready bow the button casts natively");
        r=q.Step(5,3,11,-1,40,true);Check(!r.consumed&&q.Current().skill==7,"a native press stays native when the bow becomes ready");
        r=fresh(3,11,40,true);Check(r.consumed&&q.Current().skill==11,"next press loads");
        for(int i=0;i<3;++i){r=q.Step(5,3,11,-1,40,false);Check(r.consumed&&r.change==Quiver::Change::None&&q.Current().skill==11,"stale frame mid-press: no native cast, no second toggle");}
        r=q.Step(6,-1,-1,-1,-1,true);Check(!q.Current().Active(),"character/session change empties the quiver");
        // #224: a thrown weapon loads the javelin skills the same way, and only those.
        for(int id:{15,20,25,35})Check(weaponskills::JavelinSkill(id)&&!weaponskills::QuiverSkill(id),"the four javelin skills ride a throw, not a bow");
        Check(weaponskills::Loadable(15,38,true)&&weaponskills::Loadable(20,205,true)&&weaponskills::Loadable(25,43,true)&&weaponskills::Loadable(35,206,true),
              "each with its own missile");
        Check(!weaponskills::Loadable(15,38,false)&&!weaponskills::Loadable(7,12,true)&&!weaponskills::Loadable(15,1,true)&&!weaponskills::Loadable(10,-1,true),
              "no javelin skill on a bow, no arrow skill on a javelin, no stranger missile, no melee Jab");
        Check(weaponskills::Loadable(weaponskills::DoubleThrow,-1,true)&&!weaponskills::Loadable(weaponskills::DoubleThrow,-1,false),
              "Double Throw (no missile of its own) rides a throw, never a bow");
        q.Step(7,-1,-1,-1,-1,true);
        r=q.Step(7,2,25,-1,43,true,true);Check(r.consumed&&r.change==Quiver::Change::Loaded&&q.Current().skill==25,"a thrown weapon loads Plague Javelin");
        q.Step(7,-1,-1,-1,-1,true,true);
        r=q.Step(7,3,7,-1,12,true,true);Check(!r.consumed&&q.Current().skill==25,"Fire Arrow stays native with a javelin in hand");
        q.Step(7,-1,-1,-1,-1,true,true);
        r=q.Step(7,2,25,-1,43,true,true);Check(r.change==Quiver::Change::Unloaded&&!q.Current().Active(),"the same button again: basic throws");
        r=q.Step(6,3,7,-1,12,true);Check(r.consumed&&q.Current().skill==7,"new character starts empty and loads normally");
    }
    {
        const Gate gate{true,11,7,1000,42};
        auto s=Release();s.skill=7;s.missile=12;
        Check(SkillArrow(s)&&MissileId(s)==12&&Valid(s)&&Allowed(s,gate,1100),"quiver release is a valid arrow");
        Check(ProjectileLength(s)==ArrowLength,"keeps the arrow's length");
        Check(Near(FlightSpeed(s,4608),FlightSpeed(Release(),4608)),"keeps the basic arrow's draw speed");
        auto low=s;low.draw=.2f;Check(FlightSpeed(low,4608)<FlightSpeed(s,4608),"weak draw is slower for skill arrows too");
        auto bad=s;bad.missile=59;Check(!Valid(bad),"skill/missile mismatch rejected");
        bad=s;bad.skill=12;bad.missile=213;Check(!Valid(bad),"Multiple Shot cannot use the single-arrow path");
        bad=s;bad.missile=-1;Check(!Valid(bad),"missing missile rejected");
        Check(!SkillArrow(Release())&&MissileId(Release())==0,"basic arrow unchanged");
        auto spell=Release();spell.kind=Kind::Spell;spell.skill=36;spell.missile=58;
        Check(!SkillArrow(spell),"hand spells are not quiver arrows");
    }
    {
        // The root-bound model must land exactly where a basic arrow mesh is.
        for(int missile:{12,27,40,41,85,87})for(Vec d:{Vec{0,0,-1},Vec{.6f,.3f,-.74f},Vec{-.2f,-.5f,.84f},Vec{0,.97f,.24f}}) {
            auto s=Release();s.skill=7;s.missile=missile;s.direction=math::Normalized(d);
            Flight f;f.shot=s;f.distance=3;f.alive=true;f.updatedAt=10;
            float arrow[16],root[16],model[16];
            ArrowMatrix(f,10,arrow);QuiverMatrix(f,10,root);
            Child(root,{0,QuiverModelHeight,QuiverModelAhead(missile)},model);
            for(int i=0;i<16;++i)Check(Near(model[i],arrow[i]),"quiver model matches basic arrow placement");
            for(int i=0;i<12;++i)Check(Near(root[i],arrow[i]),"root keeps arrow orientation and length scale");
        }
        Check(QuiverModelAhead(12)<0&&QuiverModelAhead(40)==0&&QuiverModelAhead(27)==0,"fire model sits behind its particles");
    }
    {
        auto pose=Release();float basic[16],held[16],quiver[16],hidden[16];
        PreviewMatrix(pose,true,basic);
        Flight f;f.shot=pose;ArrowMatrix(f,0,held);
        for(int i=0;i<16;++i)Check(Near(basic[i],held[i]),"basic nocked arrow unchanged");
        pose.skill=7;pose.missile=12;
        PreviewMatrix(pose,true,quiver,0);
        for(int i=0;i<16;++i)Check(Near(quiver[i],held[i]),"a basic held client keeps the basic matrix during a swap");
        PreviewMatrix(pose,true,quiver,12);f.shot=pose;QuiverMatrix(f,0,held);
        for(int i=0;i<16;++i)Check(Near(quiver[i],held[i]),"nocked Fire Arrow uses the root placement");
        PreviewMatrix(pose,false,hidden,12);
        Check(std::fabs(hidden[0])<std::fabs(quiver[0])*.01f+1e-6f&&Near(hidden[12],quiver[12]),"hidden quiver preview collapses in place");
    }
    {
        // Headset regression 2: Fire Arrow on left X (slot 5) never loaded.
        // Presses come from the raw buttons, so a trigger held on the string
        // does not block them, and a gripped bow hand falls back from an
        // empty grip layer to the plain button.
        std::array<int,skills::Count> ids{};ids.fill(-1);ids[0]=0;ids[5]=7;ids[8]=357;
        auto assigned=[&](int slot){return ids[slot];};
        skills::HandInput in;
        Check(skills::QuiverFace(in,assigned).slot<0,"no press, no slot");
        in.face[1][0]=true;
        auto p=skills::QuiverFace(in,assigned);Check(p.slot==5&&p.hand==1,"left X names slot 5");
        in.trigger[0]=1;p=skills::QuiverFace(in,assigned);Check(p.slot==5,"right trigger drawing the string does not block it");
        in.grip[1]=1;p=skills::QuiverFace(in,assigned);Check(p.slot==5,"gripped bow hand with an empty grip layer keeps left X");
        ids[7]=36;p=skills::QuiverFace(in,assigned);Check(p.slot==7,"an assigned grip layer still wins");
        in={};in.face[0][1]=true;p=skills::QuiverFace(in,assigned);Check(p.slot==1&&p.hand==0,"right B names slot 1");
        in={};in.face[0][0]=true;p=skills::QuiverFace(in,assigned);Check(p.slot==8,"right A stays Interact's slot");
        Quiver q;Check(!q.Step(1,8,357,-1,-1,true).consumed,"Interact is never loaded");
    }
    {
        // Headset regression: a nocked Magic Arrow (27) was checked against
        // the basic id 0, refused and dropped alive, so each retry flew off
        // the bow as a native missile. Every unkept live client is destroyed.
        const uint32_t magic[3]{3,27,900},basic[3]{3,0,901},monster[3]{1,27,902};
        Check(CreatedClient(true,magic,27,true)==ClientUse::Keep,"requested quiver preview kept");
        Check(CreatedClient(true,magic,0,true)==ClientUse::Destroy,"unexpected missile destroyed, not leaked");
        Check(CreatedClient(true,basic,27,true)==ClientUse::Destroy,"basic client for a quiver request destroyed");
        Check(CreatedClient(true,magic,27,false)==ClientUse::Destroy,"stale/unwanted preview destroyed");
        Check(CreatedClient(true,basic,0,true)==ClientUse::Keep,"basic nocked arrow unchanged");
        Check(CreatedClient(false,magic,27,true)==ClientUse::Unreadable,"unreadable result is never killed");
        Check(CreatedClient(true,monster,27,true)==ClientUse::Unreadable,"non-missile unit is never killed");
    }
    {
        // With a quiver arrow the engine's bow slot is cleared: a full draw
        // is a physical release, never the native skill button pulse.
        gameplay::Engine engine;gameplay::Input in;gameplay::Output out;
        in.active=true;in.tracked[0]=in.tracked[1]=in.aimTracked=true;in.weapon=gameplay::Weapon::Bow;in.equipment=1;
        in.handBindings=true;in.bowSlot=-1;in.bowSkill=-1;in.skillContext=7;
        in.head.p={0,1.65f,0};in.hand[0].p={0,1.3f,-.4f};in.hand[1].p={0,1.3f,-.5f};in.aim=in.hand[0];
        auto step=[&](int n=1){while(n--){in.time+=.02;out=engine.Step(in);}};
        step(2);
        in.trigger=1;step();Check(out.bowDrawing,"nocks from the string");
        for(int i=0;i<10;++i){in.hand[0].p.z+=.04f;step();}
        Check(out.bowCharged,"draw charges");
        in.trigger=0;step();
        Check(out.bowRelease&&!out.nativeBowRelease&&out.skill<0,"quiver release is physical");
    }
    {
        // Regression: selecting/unloading a skill deleted the old preview
        // immediately from the UI callback, while HD entity mutation could be
        // forbidden. Requests must keep ownership until the native client
        // tick destroys the exact object and returns, including skill arrows.
        ClientRetirements retire;
        unsigned destroys=0;
        for(uint32_t missile:{0u,12u,27u,28u,40u,41u,85u,87u}) {
            const uintptr_t unit=0x1000+missile;
            const uint32_t header[3]{3,missile,500+missile};
            retire.Request(unit,header[2],missile,0); // UI: schedule only
            retire.Request(unit,header[2],missile,0); // repeated UI callback
            Check(retire.Pending()&&retire.Contains(unit,header[2],missile),"swap keeps exact old preview owned until native cleanup");
            Check(!retire.Begin(unit+1000,header),"unrelated missile is not retired");
            const auto ticket=retire.Begin(unit,header); // native client update
            Check(ticket!=0,"native tick retires basic AND skill previews");
            Check(retire.Pending()&&!retire.Begin(unit,header),"reentrant update neither destroys twice nor allows replacement");
            ++destroys;
            retire.Finish(ticket+1000);
            Check(retire.Pending(),"unrelated completion cannot release the creation barrier");
            retire.Finish(ticket); // after native destruction returns
            Check(!retire.Pending(),"replacement can now be created for latest selection");
        }
        Check(destroys==8,"one retirement per old model, including unloading to basic");
        for(const std::array<uint32_t,3> recycled: {std::array<uint32_t,3>{3,12,902},{3,40,901},{1,12,901}}) {
            retire.Request(0x2000,901,12,0);
            Check(!retire.Begin(0x2000,recycled.data())&&!retire.Pending(),"reused address/id/type is discarded without deleting another unit");
        }
        const uint32_t fire[3]{3,12,901},ice[3]{3,40,902};
        retire.Request(0x2000,901,12,0);const auto stale=retire.Begin(0x2000,fire);
        retire.Reset(); // area/character teardown owns the old objects
        retire.Request(0x3000,902,40,0);const auto current=retire.Begin(0x3000,ice);
        retire.Finish(stale);
        Check(retire.Pending()&&current!=stale,"old-session completion cannot clear new preview retirement");
        retire.Finish(current);
        retire.Request(0x2000,901,12,0);retire.Request(0x3000,902,40,0);
        const auto first=retire.Begin(0x2000,fire);retire.Finish(first);
        Check(retire.Pending(),"unwanted creation and old preview can both wait for native cleanup");
        retire.Finish(retire.Begin(0x3000,ice));
        Check(!retire.Pending(),"all queued native objects are retired");
        // Regression (2026-10-21): a recovery retired a preview the game had
        // already removed. ClientTick never reached it, and the barrier held
        // back every new preview for the rest of the session.
        retire.Request(0x2000,901,12,5000);
        Check(retire.Blocking(5000)&&retire.Blocking(5000+ClientRetirements::Unreached-1)&&
              !retire.Abandoned(0x2000,901,12,5000+ClientRetirements::Unreached-1),"a live missile has a second to reach its retirement");
        Check(!retire.Blocking(5000+ClientRetirements::Unreached)&&retire.Abandoned(0x2000,901,12,5000+ClientRetirements::Unreached),
              "an unreached retirement stops holding back the replacement");
        Check(retire.Pending()&&retire.Contains(0x2000,901,12),"the native hooks still watch for it");
        const auto late=retire.Begin(0x2000,fire); // the game updates it after all (a paused game)
        Check(late!=0&&retire.Blocking(99999)&&!retire.Abandoned(0x2000,901,12,99999),"a late update still retires it, and its destruction blocks however old");
        retire.Finish(late);
        Check(!retire.Pending(),"and then it is gone");
        retire.Request(0x2000,901,12,5000);
        Check(retire.Blocking(4000),"a clock behind the request keeps waiting");
        Check(!retire.Begin(0x2000,ice)&&!retire.Pending(),"an unreached address reused by another missile is dropped, never destroyed");
    }
    std::puts("quiver tests passed");
}
