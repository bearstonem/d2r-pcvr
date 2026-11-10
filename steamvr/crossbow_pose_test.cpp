#include <windows.h>
#include "../vr/crossbow.h"
#include "../vr/hand_swap_pose.h"
#include "gameplay.h"
#include "hand_role.h"
#include "../shared/d2r_vr_state.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
using namespace framevr::crossbow;
static Pose pose;
static uint64_t session=7;
static uint32_t equipment=42,type=D2RVR_TYPE_CROSSBOW;
static bool menu=false,havePose=true;
namespace gamestate {
uint64_t LocalPlayer(){return session;}uint32_t HandsKey(){return equipment;}
uint32_t WeaponSet(){return 0;}uint32_t WeaponType(){return type;}bool MenuOpen(){return menu;}
}
namespace framevr::logging {void Write(const char*){}}
namespace combat {bool LocalPlayerId(uint32_t& id){id=0;return true;}}
namespace skel {bool CrossbowPose(framevr::crossbow::Pose& out){out=pose;return havePose;}}
template<size_t N,class T> static void Put(std::array<uint8_t,N>& b,size_t at,T value){std::memcpy(b.data()+at,&value,sizeof value);}
template<class T,size_t N> static T Get(const std::array<uint8_t,N>& b,size_t at){T v;std::memcpy(&v,b.data()+at,sizeof v);return v;}
static bool Near(Vec a,Vec b){return skel::bow::Length(a-b)<.004f;}
int main() {
    // Independent firing keeps a real cycle even though no native attack
    // animation owns locomotion. Missing/stale stats never become rapid fire.
    FireRate rate{7,1000,42,720};
    assert(rate.Cycle(7,42,1100)==720&&rate.Cycle(7,42,2501)==1000);
    assert(rate.Cycle(8,42,1100)==1000&&rate.Cycle(7,43,1100)==1000&&rate.Cycle(7,42,999)==1000);
    rate.milliseconds=0;assert(rate.Cycle(7,42,1100)==1000);
    framevr::attackspeed::Cadence cadence;
    cadence.Landed(7,1000,720);
    for(uint64_t at=1001;at<1720;++at)assert(!cadence.Ready(7,at));
    assert(cadence.Ready(7,1720)&&cadence.Ready(8,1100));
    // An unloaded helper can be far forward of the barrel. Locate the stock
    // from its limb roots rather than treating that helper as a bolt centre.
    Mat cog=skel::bow::Identity(),helper=cog,left=cog,right=cog,barrel;
    Position(helper,{0,-.12f,-4});Position(left,{1,0,-2});Position(right,{-1,0,-2}); // inverse binds
    assert(BarrelInCog(cog,helper,left,right,barrel));
    assert(Near(Position(barrel),{0,.12f,2}));
    Position(helper,{0,-.12f,-6});Mat other;
    assert(BarrelInCog(cog,helper,left,right,other)&&Near(Position(other),Position(barrel)));
    // Real attachment evaluation includes an extra transform between item
    // local and the chosen wrist. The old reconstructed pose omitted it.
    Mat item=skel::bow::Rotation({0,0,1},{1,0,1});Position(item,{.2f,0,-.1f});
    Mat override=skel::bow::Rotation({0,1,0},{0,1,1});Position(override,{.5f,-.2f,.3f});
    for(float mirror:{-1.f,1.f}) {
        Mat wrist=skel::bow::Rotation({0,0,1},{-1,.3f,1});
        for(int i=0;i<3;++i)wrist.m[i]*=mirror;
        Position(wrist,{22600,7,15600});
        const Mat rendered=Multiply(Multiply(item,override),wrist);
        Mat actual,relative;assert(RenderedGeometry(barrel,rendered,wrist,actual,relative));
        const Mat expected=Multiply(barrel,rendered),wrong=Multiply(Multiply(barrel,item),wrist);
        assert(Near(Position(actual),Position(expected))&&!Near(Position(actual),Position(wrong)));
        assert(skel::bow::Length(Vector(actual,{0,0,1})-Vector(wrong,{0,0,1}))>.1f);
        // Support follows the next completed wrist pose without retaining the
        // previous frame's world position, including locomotion/turning.
        Mat next=skel::bow::Rotation({0,0,1},{1,0,-1});Position(next,{22605,8,15607});
        const Mat predicted=Multiply(relative,next),drawn=Multiply(Multiply(Multiply(barrel,item),override),next);
        assert(Near(Position(predicted),Position(drawn)));
        assert(Near(Vector(predicted,{0,0,1}),Vector(drawn,{0,0,1})));
    }
    assert(!SupportLatched(true,false,false,false,true));
    assert(SupportLatched(true,true,false,true,true)); // enter range with Grip already held
    assert(SupportLatched(true,true,true,false,true)); // latched until release
    assert(!SupportLatched(false,true,true,true,true));
    assert(!SupportLatched(true,true,false,true,false)); // existing staff press behavior
    {   // both grips cast a spell's motion: the left grip alone takes the staff
        double pending=-1;
        assert(!SoloSupportLatched(true,false,false,true,false,1.0,pending)&&pending==1.0);   // pressed at the shaft: wait
        assert(!SoloSupportLatched(true,true,false,true,false,1.05,pending));
        assert(SoloSupportLatched(true,true,false,true,false,1.13,pending)&&pending<0);      // still alone: taken
        assert(SoloSupportLatched(true,true,true,false,false,2.0,pending));                  // held, it stays
        assert(!SoloSupportLatched(true,true,true,true,true,2.1,pending));                   // the right grip too: let go
        pending=-1;
        assert(!SoloSupportLatched(true,false,false,true,false,3.0,pending));
        assert(!SoloSupportLatched(true,true,false,true,true,3.04,pending)&&pending<0);      // the right grip follows: a spell, not the staff
        assert(!SoloSupportLatched(true,true,false,true,false,3.5,pending));                 // and no later take without a new press
        assert(!SoloSupportLatched(true,false,false,false,false,4.0,pending)&&pending<0);    // pressed away from the shaft: nothing
    }
    // Shield-set swaps can leave the native palms 1.3-4.48 model units apart
    // throughout idle. Controller placement must work without first firing
    // an attack animation to bring those palms together.
    for(float gap:{.8f,1.3f,4.48f})assert(GripCaptureAllowed(gap,true,.93f));
    assert(!GripCaptureAllowed(4.48f,false)&&GripCaptureAllowed(.8f,false));
    assert(!GripCaptureAllowed(4.48f,true,.001f)); // hidden swap attachment: wait, never force two-hand mode
    assert(!GripCaptureAllowed(NAN,true)&&!GripCaptureAllowed(1,true,INFINITY));
    SupportGrip supportGrip;
    for(int frame=0;frame<100;++frame)assert(!supportGrip.Step(42,10,true,1,true)); // shoulder grip carried into the swap
    assert(!supportGrip.Step(42,10,true,0,true)); // release arms this crossbow
    assert(!supportGrip.Step(42,10,true,1,false)); // squeeze away from stock
    assert(supportGrip.Step(42,10,true,1,true)); // approach while held still works
    assert(supportGrip.Step(42,10,true,.5f,false)); // hysteresis and latched movement
    assert(!supportGrip.Step(42,10,true,0,false)); // can always free the support hand
    assert(supportGrip.Step(42,10,true,1,true)); // grab again without swapping
    assert(!supportGrip.Step(99,11,true,1,true)); // switch equipment while held
    assert(!supportGrip.Step(42,12,true,1,true)); // return to original crossbow while held
    assert(!supportGrip.Step(42,12,true,0,true)&&supportGrip.Step(42,12,true,1,true));
    assert(!supportGrip.Step(42,13,true,1,true)); // rebuilt hero cannot retain a previous latch
    assert(!supportGrip.Step(42,13,true,0,true)&&supportGrip.Step(42,13,true,1,true));
    assert(!supportGrip.Step(42,13,false,1,true)); // lost tracking frees the hand
    assert(!supportGrip.Step(42,13,true,1,true)); // reacquisition requires release
    assert(!supportGrip.Step(42,13,true,0,true)&&supportGrip.Step(42,13,true,1,true));
    assert(!supportGrip.Step(42,13,true,-1,true)); // missing grip input is not "always held"
    assert(!supportGrip.Step(42,13,true,NAN,true));
    supportGrip.Reset();assert(!supportGrip.Step(42,13,true,1,true)); // inactive/menu interval
    // Includes vertical aim: the former ground-path adapter refused these
    // shots and flattened every other bolt's direction to horizontal.
    for(float scale:{.7f,1.f,1.4f})for(float sign:{-1.f,1.f})
    for(Vec heading:{Vec{0,0,1},Vec{1,.2f,1},Vec{-1,-.3f,0},Vec{0,1,0},Vec{0,-1,0}}) {
        Mat local=skel::bow::Identity();Position(local,{.18f,.13f,1.f});
        Mat attach=skel::bow::Rotation({0,0,1},heading);
        for(int row=0;row<3;++row)for(int col=0;col<3;++col)attach.m[row*4+col]*=scale*(row==0?sign:1.f);
        Position(attach,{22600,8,15600});
        const Mat world=Multiply(local,attach);Line line;assert(FromBolt(world,line));
        const auto muzzle=Muzzle(line);
        assert(Near(muzzle,Position(world)));
        const Vec palm=Position(attach),support=Support(line,palm,.3f,4.f);
        assert(skel::bow::Length(framevr::math::Cross(support+line.up*(SupportDrop*4)-line.center,line.direction))<.005f);
        assert(std::fabs(framevr::math::Dot(support-line.center,line.up)+.28f)<.005f);
        assert(framevr::math::Dot(support-line.center,line.direction)<=-.24f+.005f);
        assert(framevr::math::Dot(support-palm,line.direction)<=1.2f+.005f);
        Pose p{muzzle,line.direction,{22600,2,15600},line.scale,100,7,42,3,4,4.f};
        Launch launch;assert(Aim(p,launch)&&Physical(launch,3,1));
        using namespace framevr::bowflight;
        assert(MissileId(launch.flight)==31&&Allowed(launch.flight,{true,7,3,100,42,false},100));
        assert(Near(Tip(launch.flight,0)*4,muzzle));
        Flight flight;flight.shot=launch.flight;flight.updatedAt=100;flight.speed=20;flight.alive=true;
        Mat visual;ArrowMatrix(flight,100,visual.m);
        assert(Near(Position(visual)+Vector(visual,{0,0,BoltTip}),muzzle));
        flight.distance=2;ArrowMatrix(flight,100,visual.m);
        assert(Near(Position(visual)+Vector(visual,{0,0,BoltTip}),muzzle+line.direction*8));
        const Mat frozen=visual;p.muzzle.x+=30;ArrowMatrix(flight,100,visual.m);
        assert(std::memcmp(&frozen,&visual,sizeof visual)==0);
        {
            // #432: Fires Explosive Bolts (Demon Machine) makes the basic
            // attack name explodingarrow 41, not bolt 31. The physical bolt
            // flies it, drawn as the exploding arrow's model at bolt length.
            assert(BasicLaunchMissile(Kind::Crossbow,31)==-1&&BasicLaunchMissile(Kind::Crossbow,41)==41);
            assert(BasicLaunchMissile(Kind::Crossbow,0)==RefusedMissile&&BasicLaunchMissile(Kind::Crossbow,-1)==RefusedMissile);
            Flight item;item.shot=launch.flight;item.shot.missile=41;item.updatedAt=100;item.speed=20;item.alive=true;
            assert(ItemArrow(item.shot)&&OwnMissile(item.shot)&&!SkillRelease(item.shot)&&MissileId(item.shot)==41);
            assert(Valid(item.shot)&&Allowed(item.shot,{true,7,3,100,42,false},100));
            assert(ProjectileLength(item.shot)==launch.flight.boltLength);
            Flight plain=flight;plain.distance=0;
            Mat model,bolt;ArrowMatrix(item,100,model.m);ArrowMatrix(plain,100,bolt.m);
            assert(Near(Position(model)+Vector(model,{0,0,1}),muzzle)); // the arrow model's tip at the muzzle
            assert(Near(Position(model)+Vector(model,{0,0,-.98391676f}),Position(bolt)+Vector(bolt,{0,0,-BoltTip}))); // its tail at the bolt's
            assert(!Near(Position(model)+Vector(model,{0,0,BoltTip}),muzzle)); // negative control: not placed as the bolt mesh
        }
        float t=0;const Vec start=Tip(launch.flight,0),target=start+line.direction;
        assert(CapsuleEntry(start,start+line.direction*2,target,target,.1f,t)&&t<1);
        assert(Fresh(p,150,7,42,3)&&!Fresh(p,201,7,42,3)&&!Fresh(p,99,7,42,3));
        assert(!Fresh(p,150,8,42,3)&&!Fresh(p,150,7,43,3)&&!Fresh(p,150,7,42,4));
        // Holding from above/below must translate the palm without rotating
        // the tracked wrist, including mirrored character attachments.
        for(Vec up:{Vec{0,1,0},Vec{0,-1,0},Vec{1,0,0}}) {
            Mat hand=Multiply(skel::bow::Rotation({0,1,0},up),attach);
            const Vec palmLocal{.02f,.1f,.07f};const auto held=SupportHand(hand,palmLocal,support);
            assert(std::memcmp(hand.m,held.m,12*sizeof(float))==0);
            assert(Near(Position(held)+Vector(held,palmLocal),support));
        }
    }
    Line line;Mat invalid=skel::bow::Identity();invalid.m[10]=0;assert(!FromBolt(invalid,line));
    invalid=skel::bow::Identity();invalid.m[5]=0;assert(!FromBolt(invalid,line));
    invalid=skel::bow::Identity();invalid.m[12]=std::numeric_limits<float>::quiet_NaN();assert(!FromBolt(invalid,line));
    // Actual native request adapter with writable synthetic memory, including
    // authoritative player ID zero. Costs/damage and all unrelated bytes stay.
    std::array<uint8_t,0x98> original{},adjusted{};std::array<uint8_t,0x100> owner{};
    Put(original,0,uint32_t(0x10020));Put(original,8,reinterpret_cast<uintptr_t>(owner.data()));
    Put(original,0x20,uint32_t(31));Put(original,0x24,100);Put(original,0x28,100);Put(original,0x2c,130);Put(original,0x30,110);
    Put(original,0x3c,0);Put(original,0x40,9);Put(original,0x70,uint64_t(123456));
    pose={{11240.7f,7,11604.2f},{0,0,1},{11240,1,11604},1,GetTickCount64(),session,equipment,3,4,4.f};
    crossbowvr::Gate(true,session,equipment,3);Launch launch;
    const auto before=original;const auto ptr=reinterpret_cast<uintptr_t>(original.data());
    assert(!crossbowvr::Prepare(ptr,adjusted,launch)); // no independently launched client flight
    assert(crossbowvr::Prepare(ptr,adjusted,launch,true));assert(original==before);
    assert(!crossbowvr::Prepare(ptr,adjusted,launch)); // wait for successful native server adoption
    crossbowvr::ServerDone(launch,true);const auto sequence=launch.flight.sequence;
    const Pose serverPose=pose;pose.muzzle.x+=4;
    assert(crossbowvr::Prepare(ptr,adjusted,launch));assert(Near(launch.pose.muzzle,serverPose.muzzle));
    assert(launch.flight.sequence==sequence);
    pose=serverPose;
    for(size_t i=0;i<original.size();++i)if(i!=0&&(i<0x24||i>=0x34))assert(adjusted[i]==original[i]);
    assert(Get<uint32_t>(adjusted,0)==0x10021&&Get<int>(adjusted,0x24)==launch.x&&Get<int>(adjusted,0x28)==launch.y);
    assert(Get<int>(adjusted,0x2c)==launch.tx&&Get<int>(adjusted,0x30)==launch.ty);
    assert(!crossbowvr::Prepare(ptr,adjusted,launch)); // consume once
    assert(crossbowvr::Prepare(ptr,adjusted,launch,true));auto replicated=adjusted;
    crossbowvr::ServerDone(launch,true);pose.muzzle.x+=4;
    // A fired bolt survives the menu/equipment change between native callbacks.
    menu=true;type=D2RVR_TYPE_BOW;
    assert(crossbowvr::Prepare(reinterpret_cast<uintptr_t>(replicated.data()),adjusted,launch));
    assert(Near(launch.pose.muzzle,serverPose.muzzle));pose=serverPose;menu=false;type=D2RVR_TYPE_CROSSBOW;
    Put(owner,0,uint32_t(1));assert(!crossbowvr::Prepare(ptr,adjusted,launch,true));Put(owner,0,uint32_t(0));
    Put(owner,8,uint32_t(2));assert(!crossbowvr::Prepare(ptr,adjusted,launch,true));Put(owner,8,uint32_t(0));
    Put(original,0x20,uint32_t(0));assert(!crossbowvr::Prepare(ptr,adjusted,launch,true));Put(original,0x20,uint32_t(31));
    Put(original,0x3c,7);assert(!crossbowvr::Prepare(ptr,adjusted,launch,true));Put(original,0x3c,0);
    menu=true;assert(!crossbowvr::Prepare(ptr,adjusted,launch,true));menu=false;
    havePose=false;assert(!crossbowvr::Prepare(ptr,adjusted,launch,true));havePose=true;
    crossbowvr::Gate(false,session,equipment,3);assert(!crossbowvr::Prepare(ptr,adjusted,launch,true));
    crossbowvr::Gate(true,session,equipment,4);assert(!crossbowvr::Prepare(ptr,adjusted,launch,true));
    crossbowvr::Gate(true,session,equipment,3);assert(crossbowvr::Prepare(ptr,adjusted,launch,true));
    crossbowvr::ServerDone(launch,false);assert(!crossbowvr::Prepare(ptr,adjusted,launch));
    crossbowvr::BeginClient(&launch);assert(crossbowvr::Creating()==&launch.flight);
    crossbowvr::BeginClient(nullptr);assert(!crossbowvr::Creating());
    crossbowvr::EndClient();assert(crossbowvr::Creating()==&launch.flight);
    crossbowvr::EndClient();assert(!crossbowvr::Creating());
    assert(crossbowvr::Prepare(ptr,adjusted,launch,true));crossbowvr::ServerDone(launch,true);
    crossbowvr::Gate(true,++session,equipment,3);assert(!crossbowvr::Prepare(ptr,adjusted,launch));
    {   // Left-handed play (hand_role.h, phase 3): the stock in the LEFT hand, the right hand supporting it.
        // The body poses it in the mirror world (vr/hand_swap_pose.h): the attach bone comes back as the
        // right-handed one's mirror image with its side axis turned over, and the bolt's line, captured
        // against the bone AS DRAWN, must be the mirror image's line inside the mirror world.
        using namespace skel;
        const auto mirror=[](Vec v){return Vec{-v.x,v.y,v.z};};
        Mat attach=bow::Rotation({0,0,1},{-.4f,.2f,1});Position(attach,{-.3f,1.2f,-.5f});   // the bone, right-handed, model space
        const int side=0;   // the crossbow's side axis in this bone (skeletons.cpp: the third axis of its gun frame)
        // left-handed, as drawn: the reflection of that bone, its side axis turned over
        Mat drawn=handswap::MirrorX(attach);assert(handswap::MatchHandedness(drawn,attach,side));
        assert(handswap::Det(drawn)*handswap::Det(attach)>0);
        // the mirror world's bone is the right-handed one; there the captured bolt goes on it as drawn (that axis turned)
        Mat asDrawn=attach;handswap::TurnOver(asDrawn,side);
        {const Mat reflected=handswap::MirrorX(drawn);for(int i=0;i<16;++i)assert(std::fabs(asDrawn.m[i]-reflected.m[i])<1e-5f);}
        for(const bool symmetric:{true,false}) {
            // the bolt helper in the attach bone's frame: in the crossbow's middle plane, or (a rig that is not) off it
            Mat bolt=bow::Rotation({0,0,1},symmetric?Vec{0,.1f,1}:Vec{.2f,.1f,1});Position(bolt,{symmetric?0:.05f,.11f,.6f});
            Line rightLine;assert(FromBolt(Multiply(bolt,attach),rightLine));
            // the rendered crossbow hangs on the drawn bone; its bolt against that bone is what is captured
            Mat boltWorld,captured;
            assert(RenderedGeometry(bolt,drawn,drawn,boltWorld,captured));
            Line leftLine;assert(FromBolt(boltWorld,leftLine));
            // a crossbow that is its own mirror image across its side axis shoots along the mirror image's line
            if(symmetric)assert(Near(leftLine.center,mirror(rightLine.center))&&Near(leftLine.direction,mirror(rightLine.direction)));
            // and whatever the rig, the line the mirror world works with is the real line's reflection
            Line there;assert(FromBolt(Multiply(captured,asDrawn),there));
            assert(Near(there.center,mirror(leftLine.center))&&Near(there.direction,mirror(leftLine.direction))&&Near(there.up,mirror(leftLine.up)));
            // so the support hand, found right-handed in the mirror world, is the real right hand's place under the real stock
            const Vec leftPalm=Position(drawn)+Vec{0,-.02f,.01f};    // the driving (left) hand, real
            const Vec realSupport=mirror(Support(there,mirror(leftPalm),.25f,1.f));
            const float along=skel::bow::Dot(realSupport-leftLine.center,leftLine.direction);
            const Vec off=realSupport-(leftLine.center+leftLine.direction*along);
            assert(std::fabs(skel::bow::Length(off)-SupportDrop)<.002f&&along<-.05f);   // under the real bolt line, behind the muzzle
        }
        const Vec support{.1f,1.1f,-.2f};
        // the latch is the support hand's own grip, whichever controller that is
        const framevr::handrole::Map roles=framevr::handrole::Resolve(true,D2RVR_TYPE_CROSSBOW,D2RVR_TWO_HANDS_ON).map;
        assert(roles.left&&roles.Physical(0)==1&&roles.Physical(1)==0);   // main: left (the trigger that fires), off: right (the support grip)
        float grip[2]={1,0};   // physical: the right grip squeezed
        SupportGrip latch;
        assert(!latch.Step(42,10,true,0,true));                            // armed by a release
        assert(latch.Step(42,10,true,grip[roles.Physical(1)],true));       // the right hand's grip takes the stock
        assert(!latch.Step(42,10,true,grip[roles.Physical(0)],true));      // the left (driving) hand's grip is not the support's
        // the trigger: the mod's Attack on the main trigger, by role
        using framevr::gameplay::CrossbowAttack;using framevr::gameplay::TriggerSelection;using framevr::gameplay::Weapon;
        assert(CrossbowAttack(true,Weapon::Crossbow,true,36,true)&&TriggerSelection(true,36,true)==-1);   // slot 0 is the main hand's: the left trigger attacks
        // a support palm placed under the stock keeps its tracked turn (SupportHand), mirrored like the rest
        Mat palm=bow::Rotation({0,0,1},{1,.5f,.2f});Position(palm,{.2f,1,-.3f});
        const Mat placed=SupportHand(palm,{.01f,.02f,.03f},support);
        Mat mirroredPalm=handswap::MirrorX(palm);const Mat mirroredPlaced=SupportHand(mirroredPalm,{.01f,.02f,.03f},mirror(support));
        assert(Near(Position(mirroredPlaced),mirror(Position(placed))));
    }
    puts("Crossbow physical flight, free aiming, tracked support wrist, native request pairing and the left-handed stock passed");
}
