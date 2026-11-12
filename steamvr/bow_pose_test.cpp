#include "../vr/bow_pose.h"
#include "../vr/hand_swap_pose.h"
#include "gameplay.h"
#include "hand_role.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace skel::bow;
static void Check(bool ok,const char* label) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static bool Near(Vec a,Vec b) {return Length(a-b)<.0001f;}
static Pose SyntheticBow() {
    Pose p;for(auto& m:p)m=Identity();
    Position(p[4],{0,.3f,0});Position(p[5],{0,-.3f,0});
    Position(p[6],{0,-2,-.5f});Position(p[7],{0,2,-.5f});
    Position(p[8],{0,0,-.5f});Position(p[9],{0,-1,-.5f});Position(p[10],{0,1,-.5f});
    Position(p[11],{0,0,-.48f});Position(p[12],{0,0,-.48f});return p;
}
int main() {
    {
        // Reduced LOD rig: named arrow and all its skin helpers hide without
        // touching the grip, limbs or string, regardless of the 13-bone solver.
        const std::array<std::string_view,10> names={"cog","topBowBend","botBowBend","top_stringHelper","bot_stringHelper",
            "arrow","arrow_helper","bowString","bowString_upper_helper","bowString_lower_helper"};
        const std::array<int,10> parents={-1,0,0,1,2,0,5,0,7,7};
        auto mask=ArrowBones(names,parents);
        for(size_t i=0;i<names.size();++i)Check(mask[i]==(i==5||i==6),"reduced bow hides only its original arrow skin");
        const std::array<std::string_view,5> aliases={"cog","arrow2","arrowHelper","featherChild","bowString"};
        const std::array<int,5> ancestry={-1,0,1,2,0};
        mask=ArrowBones(aliases,ancestry);
        Check(!mask[0]&&mask[1]&&mask[2]&&mask[3]&&!mask[4],"alternate arrow bone and descendants recognized");
        auto invalid=parents;invalid[5]=6;mask=ArrowBones(names,invalid);
        for(bool hidden:mask)Check(!hidden,"invalid hierarchy cannot hide unrelated geometry");
        Mat bone=Rotation({0,0,1},{1,0,0});Position(bone,{2,3,4});const auto before=bone;
        HideArrow(bone);Check(Affine(bone)&&Near(Position(bone),Position(before)),"hiding preserves finite bone position");
        for(int row=0;row<3;++row)for(int col=0;col<3;++col)
            Check(std::fabs(bone.m[row*4+col]-before.m[row*4+col]*.001f)<.000001f,"original arrow collapses without moving bow bones");
    }
    const Pose bind=SyntheticBow();Rig rig;Check(rig.Learn(bind),"synthetic bow accepted");
    Pose posed;
    for(int step=0;step<=100;++step) {
        const float draw=step/100.f;Check(rig.Draw(Identity(),draw,true,posed),"full draw range solves");
        for(int side=0;side<2;++side) {
            const int hinge=side?5:4,tip=side?6:7;
            Check(Near(Position(posed[hinge]),Position(bind[hinge])),"grip and limb hinges stay fixed");
            Check(std::fabs(Length(Position(posed[tip])-Position(posed[hinge]))-Length(Position(bind[tip])-Position(bind[hinge])))<.0001f,"limb length preserved");
            Check(std::fabs(Length(Position(posed[8])-Position(posed[tip]))-Length(Position(bind[8])-Position(bind[tip])))<.0001f,"string length preserved");
        }
        Check(Near(Position(posed[11])-Position(posed[8]),Position(bind[11])-Position(bind[8])),"arrow remains nocked during pull");
    }
    Mat placement=Rotation({0,0,1},{1,0,0});Position(placement,{3,7,1});
    Check(rig.Draw(placement,.6f,true,posed),"rotated translated bow solves");Pose local;rig.Draw(Identity(),.6f,true,local);
    for(int i=0;i<Count;++i)Check(Near(Position(posed[i]),Vector(placement,Position(local[i]))+Position(placement)),"pose follows bow placement");
    Check(rig.Draw(Identity(),0,false,posed),"released bow solves");
    Check(Near(Position(posed[8]),Position(bind[8])),"release immediately returns string, no delayed windup");
    Check(posed[11].m[0]<.002f&&posed[12].m[0]<.002f,"released arrow and helper hidden");
    {
        // A translated, scaled, mirrored attachment like a left wrist. Solve
        // a 3D two-hand target in item space, then check it back in head space.
        Mat attach=Rotation({0,0,1},{1,0,0});
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)attach.m[4*i+j]*=(i==0?-.25f:.25f);
        Position(attach,{-.2f,-.3f,-.5f});Mat undo;Check(Inverse(attach,undo),"left-hand attachment invertible");
        const Vec localNock={.3f,.2f,-1.7f},localLine={-.3f,-.2f,1.7f};
        const Vec rightHand=Vector(attach,localNock)+Position(attach),leftHand=rightHand+Vector(attach,localLine);
        const Vec nock=Vector(undo,rightHand)+Position(undo),line=Vector(undo,leftHand-rightHand);
        Check(rig.DrawAt(Identity(),nock,line,true,posed),"lateral and pitched draw solves");
        Check(Near(Vector(attach,Position(posed[String]))+Position(attach),rightHand),"string exactly follows drawing hand in 3D");
        const Vec arrowDirection=Normalized(Vector(attach,Vector(posed[Arrow],{0,0,1})));
        Check(Dot(arrowDirection,Normalized(leftHand-rightHand))>.9999f,"visible arrow and two-hand shot vector agree after mirror/rotation/scale");
        Check(rig.DrawAt(Identity(),{30,20,-50},{0,0,1},true,posed),"unreachable draw constrained safely");
    }
    Pose invalid=bind;invalid[4].m[0]=0;Check(!Rig{}.Learn(invalid),"singular bind rejected");
    invalid=bind;invalid[8].m[14]=0;Check(!Rig{}.Learn(invalid),"missing pull axis rejected");
    const Pose saved=posed;
    Check(!rig.Draw(Identity(),std::numeric_limits<float>::quiet_NaN(),true,posed),"NaN draw rejected");
    for(int i=0;i<Count;++i)for(int j=0;j<16;++j)Check(saved[i].m[j]==posed[i].m[j],"failed pose leaves output unchanged");
    // Integrate the actual gesture recognizer: visible bend before any native
    // attack, held pose sustained, one release pulse, cancellation never fires.
    using framevr::gameplay::Input;using framevr::gameplay::Engine;using framevr::gameplay::Weapon;
    Engine engine;Input in;in.active=in.tracked[0]=in.tracked[1]=in.aimTracked=true;in.weapon=Weapon::Bow;
    in.head.p={0,1.7f,0};in.hand[0].p={0,1.3f,-.4f};in.hand[1].p={0,1.3f,-.5f};
    auto step=[&](){in.time+=.02;return engine.Step(in);};step();step();in.trigger=1;
    Check(step().bowDrawing,"nock starts live pose");
    for(int i=0;i<20;++i){in.hand[0].p.z+=.02f;auto o=step();Check(o.bowDrawing&&!o.attack,"pull does not request native attack");Check(rig.Draw(Identity(),o.draw,o.bowDrawing,posed),"gesture drives native pose");}
    Check(Position(posed[8]).z<Position(bind[8]).z-1,"string bends while trigger held");
    auto held=step();Check(held.bowDrawing&&!held.attack,"holding draw stays held");
    in.trigger=0;auto released=step();Check(released.bowRelease&&!released.attack&&!released.bowDrawing,"only release requests one direct native shot");
    rig.Draw(Identity(),released.bowDrawing?released.draw:0,released.bowDrawing,posed);
    Check(Near(Position(posed[8]),Position(bind[8])),"release resets live pose on same sample");
    for(int i=0;i<8;++i) {in.aim.q=framevr::math::Yaw(float(i));auto o=step();Check(Near(o.rayDirection,released.rayDirection),"release aim stays frozen after hands turn");}
    in.cancel=true;Check(!step().bowRelease,"cancel never emits another release");
    {   // Left-handed play (hand_role.h, phase 3): the bow in the RIGHT hand, the string drawn by the LEFT.
        // The bow's attach bone is the right-handed one's mirror image with the axis across the bow's
        // plane turned over (hand_swap_pose.h), which for a bow is the bow itself: the string still
        // behind the grip, toward the player. The same solver then follows the left hand exactly.
        using namespace skel;
        Mat attach=Rotation({0,0,1},{.3f,.2f,1});   // the bow as a right-handed player holds it (on the left hand)
        for(int i=0;i<3;++i)attach.m[i]*=1.3f;for(int i=4;i<7;++i)attach.m[i]*=1.3f;for(int i=8;i<11;++i)attach.m[i]*=1.3f;
        Position(attach,{-.25f,1.35f,-.55f});
        const Vec localNock={.05f,.1f,-1.6f};   // where the string hand is, in the bow's own frame: a little off its plane
        const Vec stringHand=Vector(attach,localNock)+Position(attach),bowHand=Position(attach);
        const auto mirror=[](Vec v){return Vec{-v.x,v.y,v.z};};
        // SyntheticBow: limbs along y, the string behind at -z, so its plane's normal is the bone's x
        Mat palm=Identity();Check(handswap::SymmetryAxis(palm,{1,.1f,.1f})==0,"a bow's fist: the palm's normal is across the bow's plane");
        {   // better, the bow's own rig says which of its attach bone's axes that is, however the item hangs on the bone
            Check(Near(rig.AcrossPlane(),{1,0,0})||Near(rig.AcrossPlane(),{-1,0,0}),"the synthetic bow's plane is its y z: limbs and string");
            float alignment=0;
            Check(handswap::NearestAxis(Identity(),rig.AcrossPlane(),&alignment)==0&&alignment>.999f,"hung straight on the bone: the bone's x");
            Mat hung=Rotation({1,0,0},{0,0,1});for(int i=0;i<12;++i)hung.m[i]*=.8f;Position(hung,{.1f,.2f,.3f});   // the item turned and scaled on its bone
            Check(handswap::NearestAxis(hung,rig.AcrossPlane(),&alignment)==2&&alignment>.999f,"turned on the bone: the bone's z, whatever the item's scale and place");
            Mat turned=hung;handswap::TurnOver(turned,2);Check(handswap::NearestAxis(turned,rig.AcrossPlane())==2,"the same axis once the bone is turned over it");
            Check(handswap::NearestAxis(Rotation({1,0,0},{1,1,1}),rig.AcrossPlane(),&alignment)>=0&&alignment<.9f,"askew: an answer too poor to use");
            Check(handswap::NearestAxis(Identity(),Rig{}.AcrossPlane(),&alignment)==-1&&alignment==0,"no rig: no answer, the palm decides");
        }
        Mat left=handswap::MirrorX(attach);
        Check(handswap::MatchHandedness(left,attach,0)&&handswap::Det(left)*handswap::Det(attach)>0,"the mirrored bow is turned over its plane, never drawn mirrored");
        Check(Near(Position(left),mirror(Position(attach)))&&Near(Vector(left,{0,0,-1}),mirror(Vector(attach,{0,0,-1})))&&Near(Vector(left,{0,1,0}),mirror(Vector(attach,{0,1,0}))),
            "it is where the mirror has it: in the right hand's place, string toward the player, upper limb up");
        Mat undo,undoLeft;Check(Inverse(attach,undo)&&Inverse(left,undoLeft),"both placements invert");
        const Vec leftString=mirror(stringHand),rightBow=mirror(bowHand);   // the LEFT hand at the string, the RIGHT hand on the bow
        Pose a,b;
        Check(rig.DrawAt(Identity(),Vector(undo,stringHand)+Position(undo),Vector(undo,bowHand-stringHand),true,a),"right-handed draw solves");
        Check(rig.DrawAt(Identity(),Vector(undoLeft,leftString)+Position(undoLeft),Vector(undoLeft,rightBow-leftString),true,b),"left-handed draw solves");
        Check(Near(Vector(left,Position(b[String]))+Position(left),leftString),"the string follows the LEFT hand exactly");
        const Vec arrowRight=Normalized(Vector(attach,Vector(a[Arrow],{0,0,1}))),arrowLeft=Normalized(Vector(left,Vector(b[Arrow],{0,0,1})));
        Check(Dot(arrowLeft,Normalized(rightBow-leftString))>.9999f&&Near(arrowLeft,mirror(arrowRight)),"the nocked arrow lies from the left hand to the bow in the right: the right-handed arrow's mirror image");
        // limb for limb the mirror image: the bow bends the same, its off-plane lean the other way
        for(int i=Cog+1;i<Count;++i) {
            const Vec was=Position(a[i]),is=Position(b[i]);
            Check(Near(is,{-was.x,was.y,was.z}),"every bone of the drawn bow is the right-handed one's, mirrored across the bow's plane");
        }
        // and the recognizer by role: the left controller's trigger draws, the right controller holds the bow
        framevr::gameplay::Pose controller[2];controller[0].p={.05f,1.3f,-.5f};controller[1].p={.05f,1.3f,-.4f};   // physical: right, left
        const framevr::handrole::Map roles=framevr::handrole::Resolve(true,D2RVR_TYPE_BOW,0).map;
        Check(roles.left&&roles.Physical(1)==0&&roles.Physical(0)==1,"with a bow the off hand (the bow's) is the right controller, the main hand (the string's) the left");
        Engine lefty;Input li;li.active=li.tracked[0]=li.tracked[1]=li.aimTracked=true;li.weapon=Weapon::Bow;li.head.p={0,1.7f,0};
        float trigger[2]{};
        const auto pull=[&](){li.time+=.02;for(int role=0;role<2;++role)li.hand[role]=controller[roles.Physical(role)];li.trigger=trigger[roles.Physical(0)];li.aim=li.hand[0];return lefty.Step(li);};
        pull();pull();trigger[1]=1;Check(pull().bowDrawing,"the left trigger nocks");
        framevr::gameplay::Output o;for(int i=0;i<20;++i){controller[1].p.z+=.02f;o=pull();Check(o.bowDrawing&&rig.Draw(Identity(),o.draw,o.bowDrawing,b),"the left hand's pull bends the bow");}
        trigger[1]=0;o=pull();Check(o.bowRelease&&!o.bowDrawing&&o.releaseDirection.z<-.99f,"and its release looses the arrow from the right hand's bow");
    }
    std::puts("Bow pose: geometry, draw/release timeline, placement, visibility, invalid-input and left-handed (bow right, string left) tests passed");
}
