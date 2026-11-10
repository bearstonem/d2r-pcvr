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
            // a Barbarian's two-handed sword in one hand is one-handed; with both hands on it, not yet
            Check(Resolve(true,type,D2RVR_TWO_HANDED|D2RVR_TWO_HANDED_KNOWN).map.left,"two-handed by its table, held in one hand: mirrored");
            const auto two=Resolve(true,type,D2RVR_TWO_HANDED|D2RVR_TWO_HANDS_ON);
            Check(!two.map.left&&two.Fallback()&&two.kind==Kind::TwoHands,"both hands on it: right-handed for now");
        }
        const std::pair<uint32_t,Kind> later[]={{D2RVR_TYPE_BOW,Kind::Bow},{D2RVR_TYPE_CROSSBOW,Kind::Crossbow},{D2RVR_TYPE_STAFF,Kind::Staff},
            {D2RVR_TYPE_POLEARM,Kind::Polearm},{D2RVR_TYPE_SPEAR,Kind::Spear}};
        for(const auto& [type,kind]:later)for(const uint32_t two:{0u,uint32_t(D2RVR_TWO_HANDS_ON)}) {
            const auto s=Resolve(true,type,two);
            Check(s.wanted&&s.kind==kind&&s.map==Map{}&&s.Fallback()&&!Mirrors(kind)&&Name(kind)[0],"bow, crossbow, staff, polearm, spear: right-handed whole, and named for the log");
            Check(!Resolve(false,type,two).Fallback(),"no fallback to report with the setting off");
        }
        for(int t=0;t<D2RVR_TYPE_COUNT;++t)for(uint32_t two=0;two<16;++two)
            Check(Resolve(false,uint32_t(t),two).map==Map{},"OFF IS RIGHT-HANDED for every weapon and grip: the identity map");
    }
    {   // phase 2's systems keep their physical hands whatever the roles are
        const auto on=Resolve(true,D2RVR_TYPE_SWORD,0);
        for(const System s:{System::Combat,System::Buttons,System::Haptics,System::SkillSlots})Check(For(s,on).left,"built: follows the roles");
        for(const System s:{System::Laser,System::WristHud,System::LeatherMenu,System::ClassMotions})
            Check(For(s,on)==Map{}&&For(s,on).Physical(0)==0&&For(s,on).Physical(1)==1,"not built: the laser stays right, the wrist HUD and leather menu left");
        // the wrist HUD's hand in the engines' (role) arrays: the left hand is the main hand there
        Check(on.map.Role(For(System::WristHud,on).Physical(1))==0&&Map{}.Role(1)==1,"the left wrist is role 0 left-handed, 1 right-handed");
    }
    {   // Dominant hand's behaviours: Left-handed turns them left with the one switch, whatever the weapon
        Check(Dominant(false,0)==0&&Dominant(false,1)==1,"off: Dominant hand as saved");
        Check(Dominant(true,0)==1&&Dominant(true,1)==1,"on: the left hand, whatever Dominant hand says");
        const auto sword=Resolve(true,D2RVR_TYPE_SWORD,0),bow=Resolve(true,D2RVR_TYPE_BOW,0);
        Check(sword.map.Role(Dominant(true,0))==0,"with a sword the swap hand is the main hand (role 0), the left");
        Check(bow.map.Role(Dominant(true,0))==1&&bow.map.Physical(1)==1,"with a bow it is still the LEFT hand, now the off hand: the swap hand never moves");
        // haptics: a pulse for a role goes to the controller playing it
        Check(sword.map.Physical(0)==1&&sword.map.Physical(1)==0&&bow.map.Physical(0)==0,"the main hand's pulse buzzes the left controller, the shield's the right; with a bow, as before");
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
    std::puts("Hand roles: map, weapon kinds and fallback, interface pins, dominant hand, mirrored throws / melee / toss, role slots, lanes, mirrored grip and shield placement passed");
}
