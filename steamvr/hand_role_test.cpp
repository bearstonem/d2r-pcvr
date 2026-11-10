// Left-handed play (hand_role.h, #307): the physical <-> role mapping, what each weapon kind falls back
// to, and the role layer in front of the pure engines - the same hands and controls run right-handed and,
// mirrored through the body's middle plane, left-handed must do the same things with the other hand.
#include "hand_role.h"
#include "gameplay.h"
#include "hand_controls.h"
#include "melee_contact.h"
#include "../vr/shield_pose.h"
#include "../vr/hand_swap_pose.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static bool Near(float a,float b,float e=1e-4f){return std::fabs(a-b)<=e;}
static bool Near(math::Vec a,math::Vec b,float e=1e-4f){return Near(a.x,b.x,e)&&Near(a.y,b.y,e)&&Near(a.z,b.z,e);}

// What the controllers report, by PHYSICAL hand (0 right, 1 left), as OpenXR gives it.
struct Controllers {
    gameplay::Pose hand[2];
    bool tracked[2]{true,true};
    float trigger[2]{},grip[2]{};
    bool face[2][2]{},click[2]{};
};
// The same moment seen in a mirror standing in the body's middle plane (x -> -x): the right
// controller's pose and controls on the left, mirrored, and the left's on the right.
static math::Vec Mirror(math::Vec v){return {-v.x,v.y,v.z};}
static math::Quat Mirror(math::Quat q){return {q.x,-q.y,-q.z,q.w};}
static Controllers Mirror(const Controllers& c) {
    Controllers m;
    for(int h=0;h<2;++h) {
        m.hand[1-h]={Mirror(c.hand[h].p),Mirror(c.hand[h].q)};m.tracked[1-h]=c.tracked[h];
        m.trigger[1-h]=c.trigger[h];m.grip[1-h]=c.grip[h];m.click[1-h]=c.click[h];
        for(int b=0;b<2;++b)m.face[1-h][b]=c.face[h][b];
    }
    return m;
}
// The role layer, as transport.cpp's Input() builds the engines' inputs: every index a role.
static void Feed(const handrole::Map& map,const Controllers& c,gameplay::Input& in) {
    for(int role=0;role<2;++role) {
        const int hand=map.Physical(role);
        in.hand[role]=c.hand[hand];in.tracked[role]=c.tracked[hand];in.grip[role]=c.grip[hand];
    }
    in.trigger=c.trigger[map.Physical(0)];in.leftTrigger=c.trigger[map.Physical(1)];
    in.aim=in.hand[0];in.aimTracked=in.tracked[0];
}
static void Feed(const handrole::Map& map,const Controllers& c,skills::HandInput& in) {
    for(int role=0;role<2;++role) {
        const int hand=map.Physical(role);
        in.trigger[role]=c.trigger[hand];in.grip[role]=c.grip[hand];in.click[role]=c.click[hand];
        for(int b=0;b<2;++b)in.face[role][b]=c.face[hand][b];
    }
}
struct Play {
    handrole::Map map;gameplay::Engine engine;gameplay::Input in;gameplay::Output out;
    explicit Play(handrole::Map m,gameplay::Weapon weapon):map(m) {
        in.active=true;in.head.p={0,1.65f,0};in.weapon=weapon;in.equipment=1;
    }
    gameplay::Output Step(const Controllers& c){in.time+=.02;Feed(map,c,in);return out=engine.Step(in);}
};

int main() {
    using namespace handrole;
    {   // the map: right-handed it changes nothing; left it is one swap, the same both ways
        constexpr Map right{},left{true};
        static_assert(right.Physical(0)==0&&right.Physical(1)==1&&right.Role(0)==0&&right.Role(1)==1);
        static_assert(left.Physical(0)==1&&left.Physical(1)==0&&left.Role(1)==0&&left.Role(0)==1);
        static_assert(right.Main()==0&&right.Off()==1&&left.Main()==1&&left.Off()==0);
        static_assert(left.Physical(2)==2&&left.Physical(-1)==-1,"the shield's lane and no hand pass through");
        static_assert(right.Bits(1)==1&&right.Bits(2)==2&&left.Bits(1)==2&&left.Bits(2)==1&&left.Bits(3)==3&&left.Bits(5)==6);
        static_assert(left.Pick(0,10,20)==20&&left.Pick(1,10,20)==10&&right.Pick(0,10,20)==10);
        for(const Map m:{right,left})for(int h=0;h<2;++h)Check(m.Role(m.Physical(h))==h&&m.Physical(m.Role(h))==h,"role and back");
        int pair[2]={7,9};right.Order(pair);Check(pair[0]==7,"right-handed order is the physical one");
        left.Order(pair);Check(pair[0]==9&&pair[1]==7,"left-handed order swaps");left.Order(pair);Check(pair[0]==7,"and swaps back");
    }
    {   // what is mirrored now, per weapon kind; everything else plays right-handed whole
        const uint32_t oneHanded[]={D2RVR_TYPE_UNKNOWN,D2RVR_TYPE_UNARMED,D2RVR_TYPE_SWORD,D2RVR_TYPE_AXE,D2RVR_TYPE_MACE,D2RVR_TYPE_SCEPTER,
            D2RVR_TYPE_WAND,D2RVR_TYPE_DAGGER,D2RVR_TYPE_THROWING,D2RVR_TYPE_JAVELIN,D2RVR_TYPE_CLAW,D2RVR_TYPE_ORB,D2RVR_TYPE_OTHER};
        for(const uint32_t type:oneHanded) {
            const auto on=Resolve(true,type,D2RVR_TWO_HANDED_KNOWN);
            Check(on.wanted&&on.map.left&&!on.Fallback()&&on.kind==Kind::OneHand,"a one-handed kind is mirrored with the setting on");
            Check(Resolve(false,type,0).map==Map{}&&!Resolve(false,type,0).Fallback(),"and untouched with it off");
            // a Barbarian's two-handed sword in one hand is one-handed; with both hands on it, its own kind
            Check(Resolve(true,type,D2RVR_TWO_HANDED|D2RVR_TWO_HANDED_KNOWN).map.left,"two-handed by its table, held in one hand: mirrored");
            const auto two=Resolve(true,type,D2RVR_TWO_HANDED|D2RVR_TWO_HANDS_ON);
            constexpr bool twoHandsMirrored=true;
            Check(two.kind==Kind::TwoHands&&Mirrors(Kind::TwoHands)==twoHandsMirrored&&two.map.left==twoHandsMirrored&&two.Fallback()==!twoHandsMirrored,
                "both hands on it: mirrored too (phase 3)");
        }
        // phase 3: the kinds that fell back whole, each with its own switch (Mirrors): mirrored, or the whole
        // mode right-handed while it is equipped - never anything between
        const struct {uint32_t type;Kind kind;bool mirrored;} later[]={{D2RVR_TYPE_BOW,Kind::Bow,true},{D2RVR_TYPE_CROSSBOW,Kind::Crossbow,true},
            {D2RVR_TYPE_STAFF,Kind::Staff,true},{D2RVR_TYPE_POLEARM,Kind::Polearm,true},{D2RVR_TYPE_SPEAR,Kind::Spear,true}};
        for(const auto& k:later)for(const uint32_t two:{0u,uint32_t(D2RVR_TWO_HANDS_ON)}) {
            const auto s=Resolve(true,k.type,two);
            Check(s.wanted&&s.kind==k.kind&&Mirrors(k.kind)==k.mirrored&&s.map==Map{k.mirrored}&&s.Fallback()==!k.mirrored&&Name(k.kind)[0]&&HeldLeftHanded(k.kind)[0],
                "bow, crossbow, staff, polearm, spear: mirrored with no fallback (or falling back whole), named and described for the log");
            Check(!Resolve(false,k.type,two).Fallback()&&Resolve(false,k.type,two).map==Map{},"and right-handed with the setting off");
        }
        Check(std::strstr(HeldLeftHanded(Kind::Bow),"RIGHT hand")&&std::strstr(HeldLeftHanded(Kind::Bow),"LEFT hand draws"),"the bow goes to the right hand, the string to the left");
        Check(std::strstr(HeldLeftHanded(Kind::Crossbow),"stock in the LEFT")&&std::strstr(HeldLeftHanded(Kind::Staff),"LEFT hand drives"),"stock and shaft in the left hand");
        for(const char* kind:{"bow","crossbow","staff","spear","polearm","two-handed"})Check(std::strstr(MirroredKinds,kind)!=nullptr,"the log's list of mirrored kinds names it");
        // ROLE STABILITY: with no kind falling back, the roles depend on the setting alone. Equipping or
        // putting away any weapon, with any grip bits, never moves a stick, a button or a trigger.
        for(const bool wanted:{false,true}) {
            const Map first=Resolve(wanted,D2RVR_TYPE_UNARMED,0).map;
            for(int t=0;t<D2RVR_TYPE_COUNT;++t)for(uint32_t two=0;two<16;++two) {
                const auto s=Resolve(wanted,uint32_t(t),two);
                Check(s.map==first&&s.map.left==wanted&&!s.Fallback(),"the roles are the setting's, whatever is equipped: no flip on a weapon change");
            }
        }
        for(int t=0;t<D2RVR_TYPE_COUNT;++t)for(uint32_t two=0;two<16;++two) {
            const auto off=Resolve(false,uint32_t(t),two);
            Check(off.map==Map{}&&!off.wanted&&!off.Fallback()&&off.map.Physical(0)==0&&off.map.Physical(1)==1&&off.map.Bits(1)==1&&off.map.Bits(2)==2,
                "OFF IS RIGHT-HANDED for every weapon and grip: the identity map");
            for(const System system:{System::Combat,System::Buttons,System::Haptics,System::SkillSlots,System::Laser,System::WristHud,System::LeatherMenu,System::ClassMotions})
                Check(For(system,off)==Map{},"and every system right-handed with it");
        }
    }
    {   // phase 2: the interface and the class motions follow the roles too - every system is mirrored
        const auto on=Resolve(true,D2RVR_TYPE_SWORD,0);
        for(const System s:{System::Combat,System::Buttons,System::Haptics,System::SkillSlots,System::Laser,System::WristHud,System::LeatherMenu,System::ClassMotions}) {
            Check(Mirrored(s)&&For(s,on).left,"built: follows the roles");
            Check(For(s,on).Physical(0)==1&&For(s,on).Physical(1)==0,"left-handed: side 0 (the laser, the motions' main hand) is the left hand, side 1 (the menu, the wrist HUD) the right");
            Check(For(s,Resolve(false,D2RVR_TYPE_SWORD,0))==Map{},"OFF: every system right-handed");
            // a weapon that plays right-handed whole takes every system with it: never half swapped
            State whole;whole.wanted=true;whole.map=Map{};
            Check(whole.Fallback()&&For(s,whole)==Map{},"the fallback is right-handed in every system");
        }
        // the wrist HUD's hand in the engines' (role) arrays: the off hand, index 1, either way
        Check(on.map.Role(For(System::WristHud,on).Physical(1))==1&&Map{}.Role(1)==1,"the wrist HUD is on the off hand (role 1): the right wrist left-handed, the left right-handed");
        Check(on.map.Role(For(System::Laser,on).Physical(0))==0,"the laser is the main hand's (role 0)");
        Check(MirroredSystems[0],"the systems are named for the log");
    }
    {   // Dominant hand's behaviours: Left-handed turns them left with the one switch, whatever the weapon
        Check(Dominant(false)==0,"off: the right hand");
        Check(Dominant(true)==1,"on: the left hand");
        const auto sword=Resolve(true,D2RVR_TYPE_SWORD,0),bow=Resolve(true,D2RVR_TYPE_BOW,0);
        Check(sword.map.Role(Dominant(true))==0,"with a sword the swap hand is the main hand (role 0), the left");
        Check(bow.map.Role(Dominant(true))==0&&bow.map.Physical(0)==1&&bow.map==sword.map,"with a bow it is the same LEFT hand in the same role (it draws the string): the swap hand never moves");
        // haptics: a pulse for a role goes to the controller playing it
        Check(sword.map.Physical(0)==1&&sword.map.Physical(1)==0&&bow.map.Physical(0)==1&&bow.map.Physical(1)==0,
            "the main hand's pulse buzzes the left controller, the shield's the right; with a bow the string hand's (nock, draw, release) the left, the bow hand's the right");
    }
    {   // a javelin throw and Double Throw: right-handed, and the mirrored moment left-handed
        const Map maps[2]={Map{},Map{true}};
        gameplay::Output thrown[2],second[2];
        for(int lh=0;lh<2;++lh) {
            Play p(maps[lh],gameplay::Weapon::Javelin);p.in.leftThrow=true;
            Controllers c;c.hand[0].p={.25f,1.25f,-.2f};c.hand[1].p={-.25f,1.25f,-.4f};
            auto step=[&](const Controllers& now){return p.Step(lh?Mirror(now):now);};
            step(c);step(c);step(c);
            c.trigger[0]=1;Check(step(c).feedback==1&&p.out.javelinReady,"the main trigger arms the main hand's throw");
            for(int i=0;i<6;++i){c.hand[0].p.z-=.06f;c.hand[0].p.x+=.01f;step(c);}
            c.trigger[0]=0;c.hand[0].p.z-=.06f;c.hand[0].p.x+=.01f;thrown[lh]=step(c);
            Check(thrown[lh].javelinRelease&&thrown[lh].feedback==2&&!thrown[lh].leftRelease,"released: the main hand throws");
            // the off hand's own throw
            c.trigger[1]=1;Check(step(c).leftFeedback==1,"the off trigger arms the off hand's throw");
            for(int i=0;i<6;++i){c.hand[1].p.z-=.06f;step(c);}
            c.trigger[1]=0;c.hand[1].p.z-=.06f;second[lh]=step(c);
            Check(second[lh].leftRelease&&second[lh].leftFeedback==2&&!second[lh].javelinRelease,"released: the off hand throws");
        }
        Check(Near(thrown[1].rayOrigin,Mirror(thrown[0].rayOrigin))&&Near(thrown[1].rayDirection,Mirror(thrown[0].rayDirection))&&Near(thrown[1].draw,thrown[0].draw),
            "left-handed, the throw leaves the LEFT hand along the mirrored line with the same strength");
        Check(thrown[0].rayOrigin.x>0&&thrown[1].rayOrigin.x<0,"the right hand's side right-handed, the left hand's side left-handed");
        Check(Near(second[1].leftOrigin,Mirror(second[0].leftOrigin))&&Near(second[1].leftDirection,Mirror(second[0].leftDirection))&&second[1].leftOrigin.x>0,
            "Double Throw's second weapon leaves the RIGHT hand left-handed");
    }
    {   // melee, the trigger's attack and the shield's bash gate: the same outputs both ways, step by step
        const Map maps[2]={Map{},Map{true}};
        for(const bool withTrigger:{false,true}) {
            gameplay::Output out[2];
            for(int lh=0;lh<2;++lh) {
                Play p(maps[lh],gameplay::Weapon::Melee);p.in.shieldEquipped=true;
                Controllers c;c.hand[0].p={.25f,1.25f,-.4f};c.hand[1].p={-.25f,1.25f,-.4f};
                auto step=[&](const Controllers& now){return p.Step(lh?Mirror(now):now);};
                step(c);step(c);step(c);
                c.trigger[0]=withTrigger?1.f:0.f;c.trigger[1]=withTrigger?0.f:1.f;   // the off trigger never attacks
                for(int i=0;i<5;++i){c.hand[0].p.z-=.05f;out[lh]=step(c);}
            }
            Check(out[0].attack==withTrigger&&out[0].meleeContact==!withTrigger,"right-handed: the right trigger attacks, a free swing is contact melee");
            Check(out[1].attack==out[0].attack&&out[1].meleeContact==out[0].meleeContact&&out[1].block==out[0].block&&out[1].skill==out[0].skill,
                "left-handed: the LEFT trigger attacks, the right one does not");
            Check(Near(out[1].rayOrigin,Mirror(out[0].rayOrigin))&&Near(out[1].rayDirection,Mirror(out[0].rayDirection)),"and the aim comes from the left hand");
        }
    }
    {   // a tossed trap or potion: armed by the main trigger, thrown by the main hand
        const Map maps[2]={Map{},Map{true}};gameplay::Output out[2];
        for(int lh=0;lh<2;++lh) {
            Play p(maps[lh],gameplay::Weapon::Melee);p.in.toss=true;p.in.handBindings=true;p.in.selectedSkill=271;p.in.selectedAvailable=true;
            p.engine.Select(skills::TriggerSlot(0));
            Controllers c;c.hand[0].p={.25f,1.25f,-.2f};c.hand[1].p={-.25f,1.25f,-.4f};
            auto step=[&](const Controllers& now){return p.Step(lh?Mirror(now):now);};
            step(c);step(c);step(c);
            c.trigger[0]=1;Check(step(c).tossHeld,"the main trigger holds the trap in the main hand");
            for(int i=0;i<6;++i){c.hand[0].p.z-=.07f;c.hand[0].p.y+=.02f;step(c);}
            c.trigger[0]=0;c.hand[0].p.z-=.07f;out[lh]=step(c);
            Check(out[lh].tossRelease,"let go: tossed");
        }
        Check(Near(out[1].tossOrigin,Mirror(out[0].tossOrigin))&&Near(out[1].tossVelocity,Mirror(out[0].tossVelocity),1e-3f)&&out[1].tossOrigin.x<0,
            "left-handed the toss leaves the left hand with the mirrored velocity");
    }
    {   // skill slots follow the ROLE: the main hand's five slots are the left controller's when left-handed
        for(int lh=0;lh<2;++lh)for(int hand=0;hand<2;++hand)for(int key=0;key<5;++key) {
            const Map map{lh==1};
            skills::HandControls e;skills::HandInput in;in.active=true;in.context=1;in.layout=1;
            Controllers c;
            auto step=[&]{in.time+=.02;Feed(map,c,in);return e.Step(in);};
            step();step();step();
            if(key==0)c.trigger[hand]=1;else{c.grip[hand]=key>2?1.f:0.f;c.face[hand][(key-1)%2]=true;}
            const auto out=step();
            const int role=map.Role(hand);
            Check(out.slot==skills::HandSlot(role,key)&&out.hand==role,"a controller casts its ROLE's slot");
            if(lh==0)Check(out.slot==skills::HandSlot(hand,key),"right-handed: exactly the slot it casts today");
            else Check(out.slot==skills::HandSlot(1-hand,key),"left-handed: the left controller has the main hand's saved slots, the right the off hand's");
        }
        // the wheel: each controller's stick click opens its ROLE's wheel
        skills::HandControls e;skills::HandInput in;in.active=true;in.context=1;in.layout=1;Controllers c;const Map left{true};
        auto step=[&]{in.time+=.02;Feed(left,c,in);return e.Step(in);};
        step();step();c.click[1]=true;Check(step().wheel==0,"left-handed the left stick's click opens the main hand's wheel");
    }
    {   // the melee lanes are roles: lane 0 the main hand's weapon, 2 the off hand's, 1 the shield
        melee::Sample main,off,shield;off.handIndex=1;shield.shield=true;shield.handIndex=1;
        Check(melee::Lane(main)==0&&melee::Lane(off)==2&&melee::Lane(shield)==1,"lanes by role, whichever controller feeds them");
        // the transport fills lane 0 from the main hand's pose: the physical left one when left-handed
        Controllers c;c.hand[0].p={.3f,1,0};c.hand[1].p={-.3f,1,0};
        gameplay::Input in;Feed(Map{true},c,in);main.hand=in.hand[0];off.hand=in.hand[1];
        Check(main.hand.p.x<0&&off.hand.p.x>0,"left-handed: the weapon's lane reads the left hand, the shield's and the second weapon's the right");
        Feed(Map{},c,in);Check(in.hand[0].p.x>0&&in.hand[1].p.x<0,"right-handed: as it always was");
    }
    {   // the weapon in the other hand: the mirror image of the known grip, then turned, never mirrored
        using namespace skel;
        // a symmetric rig: the left wrist's bind matrix is the right one's mirror image (x -> -x), as D2R's is
        bow::Mat bindRight=bow::Identity();
        const bow::Vec ax{0,0,-1},ay{.6f,.8f,0},az=bow::Cross(ax,ay);
        const bow::Vec rows[3]={ax,ay,az};
        for(int r=0;r<3;++r){bindRight.m[r*4]=rows[r].x;bindRight.m[r*4+1]=rows[r].y;bindRight.m[r*4+2]=rows[r].z;}
        bow::Position(bindRight,{-.7f,1.3f,.1f});   // the model's right hand is at -x ("its left is +x")
        bow::Mat bindLeft=bindRight;for(int r=0;r<4;++r)bindLeft.m[r*4]=-bindLeft.m[r*4];
        Check(handswap::Det(bindRight)>0&&handswap::Det(bindLeft)<0,"the left bone is the right one's mirror image");
        bow::Mat grip=bow::Identity();grip.m[0]=0;grip.m[1]=1;grip.m[4]=-1;grip.m[5]=0;bow::Position(grip,{.05f,.02f,-.01f});   // the weapon in the right wrist's frame
        bow::Mat mirrored;Check(handswap::MirrorGrip(grip,bindRight,bindLeft,mirrored),"the mirror grip exists");
        // with the left hand in its bind pose, the bone is exactly the mirror of the right-handed bind picture
        const bow::Mat rightHeld=bow::Multiply(grip,bindRight);bow::Mat leftHeld=bow::Multiply(mirrored,bindLeft);
        for(int r=0;r<4;++r)for(int col=0;col<3;++col)Check(Near(leftHeld.m[r*4+col],(col==0?-1.f:1.f)*rightHeld.m[r*4+col]),"every axis and the origin are the mirror image's");
        Check(handswap::Det(leftHeld)<0&&handswap::Det(rightHeld)>0,"which is of the wrong handedness to draw the right-hand item from");
        const bow::Vec origin=bow::Position(leftHeld),blade{leftHeld.m[4],leftHeld.m[5],leftHeld.m[6]};
        Check(handswap::MatchHandedness(leftHeld,rightHeld,0),"so one axis is turned over");
        Check(handswap::Det(leftHeld)>0&&Near(bow::Position(leftHeld).x,origin.x)&&Near(leftHeld.m[4],blade.x)&&Near(leftHeld.m[5],blade.y)&&Near(leftHeld.m[6],blade.z),
            "the bone has its own handedness again; its origin (the grip) and its y (the blade) are where the mirror put them");
        Check(!handswap::MatchHandedness(leftHeld,rightHeld,0),"a bone already right is left alone");
        // the same works whatever frame convention the other wrist's bone has (not a mirror: turned)
        bow::Mat odd=bindLeft;handswap::TurnOver(odd,2);
        bow::Mat viaOdd;Check(handswap::MirrorGrip(grip,bindRight,odd,viaOdd),"another bone convention");
        const bow::Mat heldOdd=bow::Multiply(viaOdd,odd);
        for(int i=0;i<16;++i)Check(Near(heldOdd.m[i],bow::Multiply(mirrored,bindLeft).m[i]),"the weapon lands in the same place: only the joint positions' symmetry is relied on");
        // the axis turned over: the blade's flat (nearest the palm's normal), so the edge still leads
        bow::Mat attach=bow::Identity();   // x = the flat, y = the blade, z = the edge
        Check(handswap::FlatAxis(attach,{1,0,0})==0&&handswap::FlatAxis(attach,{.2f,0,.9f})==2&&handswap::FlatAxis(attach,{0,0,0})==0,"x or z, whichever faces the palm; x when the palm is unknown");
        Check(handswap::TurnAxis(0,2)==2&&handswap::TurnAxis(0,0)==0&&handswap::TurnAxis(1,2)==0&&handswap::TurnAxis(3,0)==2&&handswap::TurnAxis(2,0)==1&&handswap::TurnAxis(9,2)==2,
            "[hands] left_handed_turn_axis overrides the choice for comparing");
    }
    {   // the shield on the right forearm: where the mirror puts it, facing out, top to the thumb, not mirrored
        using namespace skel;using bow::Vec;
        // the left arm in the bind pose (the model's left is +x): forearm out to the side, thumb up
        const Vec elbowL{.35f,1.3f,0},wristL{.60f,1.3f,0},indexL{.68f,1.33f,0},pinkyL{.68f,1.27f,0},middleL{.69f,1.30f,0};
        auto mirror=[](Vec v){return Vec{-v.x,v.y,v.z};};
        const float handed=shield::Handed(elbowL,wristL,indexL,pinkyL,middleL,1);
        Check(handed!=0,"the arm's handedness is read from the bind pose");
        const auto bindFrame=shield::ArmFrame(elbowL,wristL,indexL,pinkyL,handed);
        // the right arm now: the mirror image of the left arm's bind pose
        const auto other=shield::ArmFrame(mirror(elbowL),mirror(wristL),mirror(indexL),mirror(pinkyL),-handed);
        Check(bindFrame.ok&&other.ok&&Near(math::Vec{other.along.x,other.along.y,other.along.z},math::Vec{-bindFrame.along.x,bindFrame.along.y,bindFrame.along.z})&&
              Near(other.out.x,-bindFrame.out.x)&&Near(other.out.z,bindFrame.out.z)&&Near(other.up.y,bindFrame.up.y),"the other arm's frame, built the same anatomical way, is the mirror image");
        // the shield's bone: 6 cm toward the elbow, 4 cm out from the forearm, its axes turned 30 degrees
        bow::Mat attach=bow::Identity();const float c30=std::cos(.5236f),s30=std::sin(.5236f);
        attach.m[0]=c30;attach.m[1]=s30;attach.m[4]=-s30;attach.m[5]=c30;
        bow::Position(attach,wristL+bindFrame.along*-.06f+bindFrame.out*.04f+bindFrame.up*.01f);
        const float det=handswap::Det(attach);
        for(int turn=0;turn<3;++turn) {
            bow::Mat placed;
            Check(shield::PlaceAcross(attach,bindFrame,wristL,1,other,mirror(wristL),1,turn,placed),"placed across");
            Check(Near(bow::Position(placed).x,-bow::Position(attach).x)&&Near(bow::Position(placed).y,bow::Position(attach).y)&&Near(bow::Position(placed).z,bow::Position(attach).z),
                "the attach point is where the mirror image has it: as far up the right forearm and out from it");
            Check((handswap::Det(placed)<0)==(det<0)&&Near(std::fabs(handswap::Det(placed)),std::fabs(det)),"the bone keeps its own handedness and size: turned, never mirrored");
            // the shield's face (the arm's out direction, in the bone's own frame) and its top (up)
            bow::Mat inverse;bow::Inverse(attach,inverse);bow::Mat nowInverse;bow::Inverse(placed,nowInverse);
            const Vec faceLocal=bow::Vector(inverse,bindFrame.out),topLocal=bow::Vector(inverse,bindFrame.up),sideLocal=bow::Vector(inverse,bindFrame.along);
            const Vec face=bow::Vector(placed,faceLocal),top=bow::Vector(placed,topLocal),along=bow::Vector(placed,sideLocal);
            const float outward=bow::Dot(face,other.out),upward=bow::Dot(top,other.up),toward=bow::Dot(along,other.along);
            if(turn==0)Check(Near(outward,1)&&Near(upward,1)&&Near(toward,-1),"default: the face stays outward and the top toward the thumb; turned end for end along the arm");
            if(turn==2)Check(Near(outward,1)&&Near(upward,-1)&&Near(toward,1),"turn 2: face outward, upside down");
            if(turn==1)Check(Near(outward,-1)&&Near(upward,1)&&Near(toward,1),"turn 1: the face inward, for comparing");
        }
        bow::Mat plain;Check(shield::Place(attach,bindFrame,wristL,1,bindFrame,wristL,1,plain),"right-handed placement is untouched");
        for(int i=0;i<16;++i)Check(Near(plain.m[i],attach.m[i]),"Place with the bind frame is the bind pose, as before");
    }
    {   // the right arm's shield: the wrist's bend stop is the left arm's, mirrored (shield::WristStop)
        using namespace skel;using bow::Vec;
        const Vec forearm{0,0,-1};   // elbow -> wrist
        const auto bent=[&](float degrees,float side){   // the back of the hand, `degrees` off the forearm, leaning to one side
            const float a=degrees/57.2958f;return bow::Normalized(Vec{side*.2f*std::sin(a),std::sin(a),-std::cos(a)});
        };
        const Vec rigid=bent(90,0);   // the hand straight on the forearm: its back square to it
        Vec given;
        Check(shield::WristStop(bent(40,0),rigid,forearm,given)==0&&Near(given,bent(40,0)),"bending toward the palm is free");
        Check(shield::WristStop(bent(74,0),rigid,forearm,given)==0,"and so is everything short of the easing (15 degrees before the stop)");
        float last=0;
        for(float asked=76;asked<=170;asked+=2) {
            const float held=shield::WristStop(bent(asked,0),rigid,forearm,given);
            const float got=std::acos(std::clamp(bow::Dot(given,forearm),-1.f,1.f))*57.2958f;
            Check(held>0&&Near(got+held,asked,.02f)&&got<90&&got>=last-1e-3f,"bending back eases toward the stop, never reaches it and never goes back");
            last=got;
        }
        Check(last>89,"a hand bent far back sits just short of straight on the forearm");
        // the left arm's stop seen in the mirror is the right arm's: the same bend held, the mirror image let go
        for(const float asked:{60.f,80.f,95.f,140.f})for(const float side:{-1.f,0.f,1.f}) {
            Vec left,right;const Vec back=bent(asked,side);
            const float heldLeft=shield::WristStop(back,rigid,forearm,left);
            const float heldRight=shield::WristStop(Mirror(back),Mirror(rigid),Mirror(forearm),right);
            Check(Near(heldLeft,heldRight)&&Near(right,Mirror(left)),"the right wrist's stop is the left one's mirror image");
        }
        Check(shield::WristStop({0,0,-1},rigid,forearm,given)==0&&shield::WristStop({NAN,0,0},rigid,forearm,given)==0,"along the forearm or not a direction: nothing held");
    }
    {   // OFF, the body's pose code (skeletons.cpp) is what it was: its left-handed terms are factors of
        // exactly 1 and indices of exactly the old hands. A product by 1.0f is the same bits for every float.
        using namespace skel;
        const float samples[]={0.f,-0.f,1.f,-1.f,.1f,-3.4028235e38f,1.17549435e-38f,1e-45f,INFINITY,-INFINITY,NAN,123456.789f};
        for(const float x:samples) {
            const float mirrorX=1.f,turned=1.f;   // right-handed: skeletons.cpp's mirrorX, and the carry line's sign for an axis not turned
            const float a=-x*mirrorX,b=-x,c=(x<0?-1.f:1.f)*turned,d=x<0?-1.f:1.f;
            Check(!std::memcmp(&a,&b,sizeof a)&&!std::memcmp(&c,&d,sizeof c),"times one: bit for bit the value it was");
        }
        bow::Mat bone=bow::Identity();bone.m[1]=.25f;bone.m[6]=-.5f;bone.m[12]=3;
        const bow::Mat same=handswap::AsMirrored(bone,-1);Check(!std::memcmp(&same,&bone,sizeof bone),"no axis turned: the bone read as it is");
        const handrole::Map off=Resolve(false,D2RVR_TYPE_STAFF,D2RVR_TWO_HANDS_ON).map;
        Check(off.Main()==0&&off.Off()==1&&!off.left,"the holding arm is the right one and the supporting arm the left, as hard-coded before");
    }
    {   // PHASE 3, the mirror world (hand_swap_pose.h): the right-handed two-handed code, run on the
        // reflection of a left-handed scene, poses the mirror image of what it poses right-handed.
        using namespace skel;using bow::Vec;using handswap::Cross;
        Check(handswap::TwinName("R_hand_bind_jnt")=="L_hand_bind_jnt"&&handswap::TwinName("L_ForeArm")=="R_ForeArm"&&
              handswap::TwinName("wrist_R_JNT")=="wrist_L_JNT"&&handswap::TwinName("hand_index_01_L_JNT")=="hand_index_01_R_JNT"&&
              handswap::TwinName("R_finger_A_A")=="L_finger_A_A"&&handswap::TwinName("R_Arm_UpperArm")=="L_Arm_UpperArm"&&
              handswap::TwinName("elbow_R")=="elbow_L","the three hero rigs' and the forms' side names");
        Check(handswap::TwinName("spine_01_bind_jnt")=="spine_01_bind_jnt"&&handswap::TwinName("head")=="head"&&handswap::TwinName("shield_attach")=="shield_attach"&&
              handswap::TwinName("R")=="R"&&handswap::TwinName("")=="","a middle bone is its own twin");
        // A rig: spine, head, each arm (upper, forearm, wrist), a finger and the weapon bone under each
        // wrist, shield_attach under the left forearm only, a pauldron on the right shoulder only.
        enum {Spine,Head,RU,RL,RH,RF,RW,LU,LL,LH,LF,LW,Shield,Pauldron,N};
        const std::vector<std::string> names={"spine_01","head","R_UpperArm","R_ForeArm","R_Wrist","R_finger_A_A","R_weapon_attach",
            "L_UpperArm","L_ForeArm","L_Wrist","L_finger_A_A","L_weapon_attach","shield_attach","R_pauldron"};
        const std::vector<int> parents={-1,Spine,Spine,RU,RL,RH,RH,Spine,LU,LL,LH,LH,LL,RU};
        const int upper[2]={RU,LU},lower[2]={RL,LL},hand[2]={RH,LH},weapon[2]={RW,LW};
        std::vector<char> inHand(N,0);for(const int b:{RF,RW,LF,LW})inHand[b]=1;
        const auto frame=[](Vec x,Vec y,Vec at,float handedness){   // a bone: two of its axes, its origin, its handedness
            x=bow::Normalized(x);y=bow::Normalized(y-x*bow::Dot(y,x));const Vec z=bow::Cross(x,y)*handedness;
            bow::Mat m=bow::Identity();const Vec rows[3]={x,y,z};
            for(int r=0;r<3;++r){m.m[r*4]=rows[r].x;m.m[r*4+1]=rows[r].y;m.m[r*4+2]=rows[r].z;}
            bow::Position(m,at);return m;
        };
        // The bind pose: joints mirror images of each other, each bone's axes its own (the left arm's bones
        // are mirrored ones on some rigs, turned ones on others: both here).
        std::vector<bow::Mat> bind(N);
        bind[Spine]=frame({1,0,0},{0,1,0},{0,1.2f,0},1);bind[Head]=frame({0,0,1},{0,1,0},{0,1.7f,.02f},1);
        bind[RU]=frame({-1,0,0},{0,1,0},{-.20f,1.5f,0},1);bind[RL]=frame({-1,-.1f,0},{0,1,0},{-.50f,1.5f,0},1);
        bind[RH]=frame({-1,0,.1f},{0,1,0},{-.76f,1.5f,0},1);bind[RF]=frame({-1,0,0},{0,0,1},{-.86f,1.5f,.02f},1);
        bind[RW]=frame({0,0,1},{0,1,0},{-.82f,1.47f,.01f},1);
        bind[LU]=frame({1,0,0},{0,1,0},{.20f,1.5f,0},-1);bind[LL]=frame({0,1,0},{1,-.1f,0},{.50f,1.5f,0},1);
        bind[LH]=frame({1,0,.1f},{0,1,0},{.76f,1.5f,0},-1);bind[LF]=frame({0,0,1},{1,0,0},{.86f,1.5f,.02f},1);
        bind[LW]=frame({0,0,1},{0,1,0},{.82f,1.47f,.01f},-1);
        bind[Shield]=frame({0,1,0},{0,0,1},{.60f,1.52f,.03f},1);bind[Pauldron]=frame({0,1,0},{1,0,0},{-.24f,1.6f,0},1);
        const auto rig=handswap::LearnMirror(names,parents,bind,inHand,upper,lower,hand,weapon);
        Check(rig.ok&&rig.asymmetry<1e-5f,"the rig is learned; its wrists' bind joints are mirror images");
        Check(rig.twin[RU]==LU&&rig.twin[LL]==RL&&rig.twin[RH]==LH&&rig.twin[RF]==LF&&rig.twin[Spine]==Spine&&rig.twin[Head]==Head,"twins across the body, the middle to itself");
        Check(rig.cross[RU]==Cross::Across&&rig.cross[Spine]==Cross::Across&&rig.cross[RF]==Cross::Finger&&rig.cross[LF]==Cross::Finger&&
              rig.cross[RW]==Cross::Item&&rig.cross[LW]==Cross::Item&&rig.twin[RW]==RW&&rig.twin[LW]==LW,"fingers come back across; the weapon bones are items, never carried");
        Check(rig.cross[Shield]==Cross::Kept&&rig.cross[Pauldron]==Cross::Kept&&rig.twin[Shield]==Shield,"a one-sided bone under an arm rides its parent");
        {   // refusals: nothing half learned
            auto bad=parents;bad[RL]=RH;Check(!handswap::LearnMirror(names,bad,bind,inHand,upper,lower,hand,weapon).ok,"a child before its parent");
            auto flat=bind;for(int i=0;i<3;++i)flat[LH].m[i]=0;Check(!handswap::LearnMirror(names,parents,flat,inHand,upper,lower,hand,weapon).ok,"a singular bind bone");
            const int none[2]={-1,LU};Check(!handswap::LearnMirror(names,parents,bind,inHand,none,lower,hand,weapon).ok,"a missing arm");
            auto lopsided=bind;bow::Position(lopsided[LH],{.80f,1.5f,0});
            Check(Near(handswap::LearnMirror(names,parents,lopsided,inHand,upper,lower,hand,weapon).asymmetry,.04f),"an asymmetric bind pose is measured for the log");
        }
        // A pose: each bone moved from its bind place by its own rigid motion, children following their parents.
        const auto pose=[&](Vec rightHand,Vec leftHand,float yaw) {
            std::vector<bow::Mat> w=bind;
            const auto place=[&](int b,Vec at,Vec toward,float roll){   // turned to point its x at `toward`, rolled, put at `at`
                bow::Mat m=bind[b];bow::Position(m,{});
                m=bow::Multiply(m,bow::Rotation({m.m[0],m.m[1],m.m[2]},toward));
                m=bow::Multiply(m,bow::Rotation({0,1,0},bow::Normalized(Vec{std::sin(roll),std::cos(roll),0})));
                bow::Position(m,at);w[b]=m;
            };
            const auto follow=[&](int b){bow::Mat inverse;bow::Inverse(bind[parents[b]],inverse);w[b]=bow::Multiply(bow::Multiply(bind[b],inverse),w[parents[b]]);};
            const bow::Mat turn=bow::Rotation({0,0,1},{std::sin(yaw),0,std::cos(yaw)});
            w[Spine]=bow::Multiply(bind[Spine],turn);follow(Head);follow(RU);follow(LU);follow(Pauldron);
            place(RL,(bow::Position(w[RU])+rightHand)*.5f+Vec{0,-.1f,0},rightHand-bow::Position(w[RU]),.3f);
            place(LL,(bow::Position(w[LU])+leftHand)*.5f+Vec{0,-.1f,0},leftHand-bow::Position(w[LU]),-.2f);
            place(RH,rightHand,{.2f,.3f,1},.4f);place(LH,leftHand,{-.1f,.5f,1},-.7f);
            for(const int b:{RF,RW,LF,LW,Shield})follow(b);
            return w;
        };
        const auto mirrorPose=[&](const std::vector<bow::Mat>& w) {   // the same pose seen in the mirror, as the twin bones have it
            std::vector<bow::Mat> m(N);
            for(int i=0;i<N;++i)m[i]=handswap::Across(rig.carrier[i],w[rig.twin[i]]);
            return m;
        };
        const std::vector<bow::Mat> real=pose({-.35f,1.25f,.45f},{.25f,1.35f,.30f},.3f);
        std::vector<bow::Mat> mirrored;handswap::EnterMirror(rig,parents,real,mirrored);
        for(const int b:{Spine,Head,RU,RL,RH,LU,LL,LH}) {
            Check(Near(bow::Position(mirrored[b]),Mirror(bow::Position(real[rig.twin[b]])),1e-4f),"in the mirror world each joint is where its twin's reflection is");
            Check((handswap::Det(mirrored[b])<0)==(handswap::Det(bind[b])<0)&&Near(std::fabs(handswap::Det(mirrored[b])),1,1e-3f),"and every bone keeps its own handedness: a drawable pose");
        }
        {   // directions too: what points along the left forearm really, points along the right one in the mirror
            const Vec realAlong=bow::Normalized(bow::Position(real[LH])-bow::Position(real[LL])),there=bow::Normalized(bow::Position(mirrored[RH])-bow::Position(mirrored[RL]));
            Check(Near(there,Mirror(realAlong)),"the mirror world's right forearm is the left one's reflection");
            bow::Mat inverse;bow::Inverse(bind[LL],inverse);const Vec local=bow::Vector(inverse,{0,1,0});   // a direction fixed in the left forearm's bind frame (model up)
            bow::Mat inverseR;bow::Inverse(bind[RL],inverseR);const Vec localR=bow::Vector(inverseR,{0,1,0});
            Check(Near(bow::Vector(mirrored[RL],localR),Mirror(bow::Vector(real[LL],local)),1e-4f),"and its twist about its length is carried with it");
        }
        // under a hand: the right-handed animation's grip, on the mirror world's own hand
        {bow::Mat a,b;bow::Inverse(real[RH],a);bow::Inverse(mirrored[RH],b);
         const bow::Mat was=bow::Multiply(real[RW],a),is=bow::Multiply(mirrored[RW],b);
         for(int i=0;i<16;++i)Check(Near(was.m[i],is.m[i],1e-4f),"the weapon bone sits in the mirror world's right hand as the animation has it in the right hand");}
        // Nothing done in the mirror world: nothing comes back changed (arms bit for bit; fingers the same
        // within rounding here, the bind pose's fingers being mirror images).
        {
            std::vector<bow::Mat> back=real;const auto entered=mirrored;
            handswap::LeaveMirror(rig,parents,entered,mirrored,back);
            for(const int b:{Spine,Head,RU,RL,RH,LU,LL,LH,Shield,Pauldron})Check(!std::memcmp(&back[b],&real[b],sizeof back[b]),"an untouched bone is not written at all");
            for(const int b:{RF,LF})for(int i=0;i<16;++i)Check(Near(back[b].m[i],real[b].m[i],1e-4f),"a symmetric grip comes back as it was");
            Check(handswap::Det(back[RW])*handswap::Det(real[RW])<0,"an item bone comes back a mirror image: to be turned, not drawn");
        }
        // A right-handed poser, as skeletons.cpp's staff code is: the weapon fast in the RIGHT hand (its
        // grip), its shaft the bone's y; the LEFT hand goes onto the shaft 30 cm up from the right fist,
        // turned with the shaft. It reads the hands and the controllers, hard-coded right and left.
        struct Scene {std::vector<bow::Mat> w;Vec controller[2];float grip[2];};
        bow::Mat gripInHand;{bow::Mat inverse;bow::Inverse(bind[RH],inverse);gripInHand=bow::Multiply(bind[RW],inverse);}
        const auto rightHandedPoser=[&](Scene& s) {
            s.w[RW]=bow::Multiply(gripInHand,s.w[RH]);
            const Vec shaft=bow::Normalized({s.w[RW].m[4],s.w[RW].m[5],s.w[RW].m[6]});
            if(s.grip[1]>.6f) {   // the support hand's own grip
                const Vec onShaft=bow::Position(s.w[RW])+shaft*.30f;
                bow::Mat m=s.w[LH];bow::Position(m,{});
                m=bow::Multiply(m,bow::Rotation(bow::Normalized(s.controller[1]-s.controller[0]),shaft));
                bow::Position(m,onShaft);
                bow::Mat inverse;bow::Inverse(s.w[LH],inverse);const bow::Mat finger=bow::Multiply(s.w[LF],inverse);
                s.w[LH]=m;s.w[LF]=bow::Multiply(finger,m);
            }
        };
        const auto leftHanded=[&](Scene real,int turnAxis,int* turned=nullptr) {   // the mirror world round the same poser
            Scene there;std::vector<bow::Mat> entered;
            handswap::EnterMirror(rig,parents,real.w,there.w);entered=there.w;
            for(int h=0;h<2;++h){there.controller[h]=Mirror(real.controller[1-h]);there.grip[h]=real.grip[1-h];}
            rightHandedPoser(there);
            handswap::LeaveMirror(rig,parents,entered,there.w,real.w);
            for(const int b:{RW,LW}) {
                const bool did=handswap::MatchHandedness(real.w[b],bind[b],turnAxis);
                if(turned&&b==RW)*turned=did?turnAxis:-1;
            }
            return real;
        };
        for(const float supportGrip:{0.f,1.f}) {
            // a right-handed player, and the same player seen in the mirror: left-handed
            Scene right;right.w=real;right.controller[0]=bow::Position(real[RH]);right.controller[1]=bow::Position(real[LH]);
            right.grip[0]=0;right.grip[1]=supportGrip;
            Scene left;left.w=mirrorPose(real);
            for(int h=0;h<2;++h){left.controller[h]=Mirror(right.controller[1-h]);left.grip[h]=right.grip[1-h];}
            Scene expected=right;rightHandedPoser(expected);
            int turned=-1;const Scene got=leftHanded(left,0,&turned);
            // the arms: the left-handed pose is the right-handed pose's mirror image, bone for bone
            for(const int b:{RU,RL,RH,LU,LL,LH})
                Check(Near(bow::Position(got.w[b]),Mirror(bow::Position(expected.w[rig.twin[b]])),2e-4f),"left-handed, every arm joint is the right-handed pose's mirror image");
            Check(Near(bow::Position(got.w[RH]),Mirror(bow::Position(expected.w[LH])),2e-4f)&&
                  (supportGrip<.5f||bow::Length(bow::Position(got.w[RH])-bow::Position(left.w[RH]))>.05f),"the RIGHT hand is the support hand: it goes onto the shaft with its own grip");
            Check(!std::memcmp(&got.w[LU],&left.w[LU],sizeof(bow::Mat))&&(supportGrip>.5f||!std::memcmp(&got.w[RH],&left.w[RH],sizeof(bow::Mat))),"an arm the code did not move is not rewritten");
            // the weapon: where the mirror has it, in the LEFT hand, with the bone's own handedness
            const bow::Mat image=handswap::MirrorX(expected.w[RW]);
            Check(Near(bow::Position(got.w[RW]),bow::Position(image),2e-4f)&&bow::Length(bow::Position(got.w[RW])-bow::Position(got.w[LH]))<.12f,"the staff is in the left hand, where the mirror image has it");
            Check(turned==0&&(handswap::Det(got.w[RW])<0)==(handswap::Det(bind[RW])<0),"its bone is turned, never mirrored: the handedness the game draws it with");
            for(int axis=1;axis<3;++axis)for(int k=0;k<3;++k)Check(Near(got.w[RW].m[axis*4+k],image.m[axis*4+k],2e-4f),"the shaft and the third axis are the mirror image's");
            for(int k=0;k<3;++k)Check(Near(got.w[RW].m[k],-image.m[k],2e-4f),"only the turned axis differs");
            const bow::Mat held=handswap::AsMirrored(got.w[RW],turned);
            for(int i=0;i<16;++i)Check(Near(held.m[i],image.m[i],2e-4f),"AsMirrored gives directions the mirror image's axes back (a blade's line, a casting end)");
            // the support hand's fingers ride it; the weapon hand's fingers are the right hand's grip, mirrored
            Check(Near(bow::Position(got.w[RF]),Mirror(bow::Position(expected.w[LF])),2e-4f)&&Near(bow::Position(got.w[LF]),Mirror(bow::Position(expected.w[RF])),2e-4f),"the fingers too");
            // one-sided bones stay on their own arm, riding it
            {bow::Mat a,b;bow::Inverse(left.w[LL],a);bow::Inverse(got.w[LL],b);
             const bow::Mat was=bow::Multiply(left.w[Shield],a),is=bow::Multiply(got.w[Shield],b);
             for(int i=0;i<16;++i)Check(Near(was.m[i],is.m[i],2e-4f),"shield_attach stays on the left forearm as it was");}
            // another turn axis (the INI knobs): the same place, another axis turned
            int other=-1;const Scene z=leftHanded(left,2,&other);
            Check(other==2&&Near(bow::Position(z.w[RW]),bow::Position(image),2e-4f)&&Near(z.w[RW].m[4],image.m[4],2e-4f)&&Near(z.w[RW].m[8],-image.m[8],2e-4f),"[hands] left_handed_*_turn_axis turns another axis over, in the same place");
        }
        // the controllers and the eye in the mirror world
        {
            const math::Quat q{.3f,-.5f,.2f,.7874008f};
            float raw[4]={q.x,q.y,q.z,q.w};handswap::MirrorQuat(raw);const math::Quat there{raw[0],raw[1],raw[2],raw[3]};
            // the gun's frame (skeletons.cpp GunFrame): ahead the knuckles' -Y, top -Z, right +X
            const Vec ahead=math::Rotate(q,{0,-1,0}),top=math::Rotate(q,{0,0,-1}),side=math::Rotate(q,{1,0,0});
            Check(Near(math::Rotate(there,{0,-1,0}),Mirror(ahead))&&Near(math::Rotate(there,{0,0,-1}),Mirror(top)),"the reflected controller points and tops where the reflection does");
            Check(Near(math::Rotate(there,{1,0,0}),Mirror(side)*-1),"and its right is a right controller's: x keeps OpenXR's convention, a proper turn");
            // head frame -> model space (skeletons.cpp ToModel), the body's turn the other way in the mirror world
            const auto toModel=[](Vec v,float c,float s){const Vec m{-v.x,v.y,-v.z};return Vec{m.x*c+m.z*s,m.y,-m.x*s+m.z*c};};
            const float yaw=.7f,c=std::cos(yaw),s=std::sin(yaw);
            float hand[3]={.31f,-.42f,-.55f};const Vec realAt=toModel({hand[0],hand[1],hand[2]},c,s);
            handswap::MirrorPoint(hand);
            Check(Near(toModel({hand[0],hand[1],hand[2]},c,-s),Mirror(realAt)),"a controller, x negated and the body's turn reversed, lands at its reflection in model space");
        }
        // which axis is turned over: the one the item is its own mirror image across
        {
            bow::Mat attach=bow::Identity();   // x across the bow's plane, y its limbs, z the arrow
            Check(handswap::SymmetryAxis(attach,{.9f,.1f,.2f})==0&&handswap::SymmetryAxis(attach,{0,1,.1f})==1&&handswap::SymmetryAxis(attach,{.1f,.2f,-1})==2,"of all three, the one nearest the palm's normal");
            Check(handswap::SymmetryAxis(attach,{.1f,1,.3f},1)==2&&handswap::SymmetryAxis(attach,{1,0,0},0)!=0&&handswap::SymmetryAxis(attach,{0,0,0},0)==1&&handswap::SymmetryAxis(attach,{0,0,0})==0,
                "never the shaft's own axis: a staff or a spear does not point backwards");
            bow::Mat scaled=attach;for(int k=0;k<3;++k)scaled.m[4+k]*=.001f;   // an axis shrunk by the game is still a direction
            Check(handswap::SymmetryAxis(scaled,{0,1,.2f})==1,"read by direction, not by length");
            Check(handswap::ItemTurnAxis(0,2)==2&&handswap::ItemTurnAxis(1,2)==0&&handswap::ItemTurnAxis(2,0)==1&&handswap::ItemTurnAxis(3,0)==2&&handswap::ItemTurnAxis(7,1)==1&&handswap::ItemTurnAxis(0,-1)==0,
                "the per-kind INI knob overrides it for comparing");
        }
    }
    std::puts("Hand roles: map, every weapon kind mirrored and no fallback, role stability, interface pins, dominant hand, mirrored throws / melee / toss, role slots, lanes, "
              "mirrored grip and shield placement, the right wrist's stop, the two-handed mirror world passed");
}
