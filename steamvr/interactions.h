#pragma once
#include "shoulder_swap.h"
#include "skill_slots.h"
#include "frame_math.h"
#include "potion_pose.h"
#include "wrist_hud.h"
#include "hand_vitals.h"
#include "dev_scenarios.h"
#include "navigation_info.h"
#include <array>
#include <cstdint>
#include <cstring>

// Value-only bridge. UI/game threads publish snapshots; XR never follows native
// pointers. Requests name a session and an exact item, not a guessed belt slot.
namespace framevr::interaction {
using math::Vec; using math::Quat;
struct Pose { Vec p{}; Quat q{0,0,0,1}; };
inline bool Valid(Pose p) {
    const float n=p.q.x*p.q.x+p.q.y*p.q.y+p.q.z*p.q.z+p.q.w*p.q.w;
    return std::isfinite(p.p.x)&&std::isfinite(p.p.y)&&std::isfinite(p.p.z)&&std::isfinite(n)&&n>.9f&&n<1.1f;
}
// The avatar already follows the tracked head. Keep waist equipment under the
// same position/facing, excluding pitch and roll. At a vertical gaze there is
// no reliable horizontal heading; retain the last one while looking at feet.
inline Pose UprightBody(Pose head,Quat previousYaw={0,0,0,1}) {
    const auto forward=math::Rotate(head.q,{0,0,-1});
    return {head.p,forward.x*forward.x+forward.z*forward.z>.01f?
        math::Yaw(std::atan2(-forward.x,-forward.z)):previousYaw};
}
// OpenXR grip +X points out of the LEFT palm: the back of the wrist is -X.
// Grip +/-Y is the thumb/knuckle edge, not either face of the wrist.
// https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#semantic-path-grip-pose
inline Vec WristNormal(Pose hand) {return math::Normalized(math::Rotate(hand.q,{-1,0,0}));}
inline Vec PalmNormal(Pose hand) {return math::Normalized(math::Rotate(hand.q,{1,0,0}));}
inline Pose HandFacePanel(Pose hand,Pose head,Vec normal,Vec offset) {
    // Keep the panel on the watch face, rotating only within that plane so
    // the text reads upright instead of sideways when the forearm is crossed.
    auto right=math::Cross(math::Rotate(head.q,{0,1,0}),normal);
    if(math::Dot(right,right)<.01f)right=math::Cross(math::Rotate(hand.q,{0,0,-1}),normal);
    right=math::Normalized(right);
    return {hand.p+math::Rotate(hand.q,offset),
        math::FromAxes(right,math::Cross(normal,right),normal)};
}
inline Pose WristPanel(Pose hand,Pose head) {return HandFacePanel(hand,head,WristNormal(hand),{-.055f,0,.06f});}
inline Pose PalmPanel(Pose hand,Pose head) {return HandFacePanel(hand,head,PalmNormal(hand),{.055f,0,-.04f});}
inline Pose VitalsPanel(Pose hand,Pose head,bool palm,float tiltDegrees=0) {
    auto panel=palm?PalmPanel(hand,head):WristPanel(hand,head);
    // Pitch about the readable panel's horizontal axis, keeping its center
    // fixed. Positive tilt brings the top edge toward the viewer. Navigation
    // retains the original hand-face plane and its existing spacing.
    const float half=vitals::NormalizeTilt(tiltDegrees)*.00872664626f;
    panel.q=math::Multiply(panel.q,{std::sin(half),0,0,std::cos(half)});
    return panel;
}
constexpr float PalmWidth=.20f,PalmHeight=.10f;
constexpr float WristWidth=.24f,WristHeight=.12f,WristMapSize=.24f;
// Navigation uses one face and one layout for both the map and compass.
// Reserve space for vitals only when they are displayed on that same face.
inline Pose NavigationMapPanel(Pose hand,Pose head,bool palm,bool vitalsOnFace) {
    auto panel=palm?PalmPanel(hand,head):WristPanel(hand,head);
    if(vitalsOnFace)panel.p=panel.p+math::Rotate(panel.q,{0,((palm?PalmHeight:WristHeight)+WristMapSize)*.5f+.01f,0});
    return panel;
}
constexpr float WristCompassWidth=.24f,WristCompassHeight=.045f;
inline Pose NavigationCompassPanel(Pose hand,Pose head,bool palm,bool vitalsOnFace) {
    auto panel=NavigationMapPanel(hand,head,palm,vitalsOnFace);
    panel.p=panel.p+math::Rotate(panel.q,{0,(WristMapSize+WristCompassHeight)*.5f+.01f,0});
    return panel;
}
constexpr float NavigationInfoWidth=WristCompassWidth;
constexpr float NavigationInfoHeight=NavigationInfoWidth*navigation::Height/navigation::Width;
inline Pose NavigationInfoPanel(Pose hand,Pose head,bool palm,bool vitalsOnFace) {
    auto panel=NavigationCompassPanel(hand,head,palm,vitalsOnFace);
    panel.p=panel.p+math::Rotate(panel.q,{0,(WristCompassHeight+NavigationInfoHeight)*.5f+.006f,0});
    return panel;
}
inline bool WristPresented(Pose hand,Pose head) {
    if(!Valid(hand)||!Valid(head))return false;
    const Vec toward=head.p-hand.p;
    const float distance2=math::Dot(toward,toward);
    return distance2>.01f&&distance2<.75f*.75f&&
        math::Dot(WristNormal(hand),math::Normalized(toward))>.55f;
}
inline bool PalmPresented(Pose hand,Pose head,bool keeping=false) {
    if(!Valid(hand)||!Valid(head))return false;
    const auto toward=head.p-hand.p,normal=PalmNormal(hand);
    const float distance2=math::Dot(toward,toward);
    if(distance2<.15f*.15f||distance2>(keeping?.80f*.80f:.70f*.70f))return false;
    // Face upward, toward the eyes, and stay in front of the player. A casting
    // pose with the palm facing sideways or behind the head is not a reveal.
    return normal.y>(keeping?.50f:.70f)&&toward.y>.10f&&toward.y<(keeping?.75f:.60f)&&
        math::Dot(normal,math::Normalized(toward))>(keeping?.35f:.55f)&&
        math::Dot(math::Rotate(head.q,{0,0,-1}),math::Normalized(toward)*-1)>(keeping?.35f:.50f);
}
enum class Owner { Combat, Wheel, Bottle, Holster, Journal, Rearm };
enum class Potion { Unknown, Health, Mana, Rejuvenation, Antidote, Thawing, Stamina };
struct Item {
    uint64_t handle=0;
    uint32_t id=0,code=0;
    uint32_t quality=0;
    int32_t qualityRecord=-1;
    bool operator==(const Item&) const = default;
};
constexpr uint32_t Code(char a,char b=' ',char c=' ',char d=' ') {
    return uint32_t(a)|(uint32_t(b)<<8)|(uint32_t(c)<<16)|(uint32_t(d)<<24);
}
inline Potion PotionType(uint32_t code) {
    if((code&0xffff)==Code('h','p',0,0))return Potion::Health;
    if((code&0xffff)==Code('m','p',0,0))return Potion::Mana;
    code=(code&0xffffff)|uint32_t(' ')<<24; // tolerate zero or space padding
    if(code==Code('r','v','s')||code==Code('r','v','l'))return Potion::Rejuvenation;
    if(code==Code('y','p','s'))return Potion::Antidote;
    if(code==Code('w','m','s'))return Potion::Thawing;
    if(code==Code('v','p','s'))return Potion::Stamina;
    return Potion::Unknown;
}
inline const char* PotionName(Potion type) {
    constexpr const char* names[]={"Unknown", "Health", "Mana", "Rejuvenation", "Antidote", "Thawing", "Stamina"};
    return names[unsigned(type)<=unsigned(Potion::Stamina)?unsigned(type):0];
}
struct Belt {
    std::array<Item,4> items{}; // bottom first; native belt action uses bottom
    uint32_t revision=0;
    uint8_t count=0;
    bool known=false;
    Item Next() const {return items[0];}
    bool operator==(const Belt&) const = default;
};
struct Skill {
    int32_t id=-1,level=0,charges=-1;
    int32_t owner=-1;
    bool known=false,available=false,directed=false,motion=false,physical=false;
    char name[64]{};
    bool operator==(const Skill&) const = default;
};
struct Snapshot {
    bool developer=false;
    int devClass=-1;
    uint64_t session=0,at=0;
    uint32_t revision=0,activeSet=0;
    bool equipmentKnown=false,beltKnown=false;
    std::array<std::array<Item,2>,2> sets{};
    std::array<Belt,4> belt{};
    std::array<Skill,skills::Count> skills{};
    std::array<Skill,128> catalog{}; // currently owned, active (non-passive) skills
    uint32_t skillCount=0;
    int32_t bindingBank=-1;
    bool bindingsEditable=false;
    float health=0,maxHealth=0,mana=0,maxMana=0;
    bool vitalsKnown=false;
    wrist::Experience experience;
    navigation::Location location;
    bool actionKnown=false;
    uint32_t actionMode=0;
};
struct SkillCue {
    int slot=-1,id=-1,owner=-1;
    bool known=false,available=false,temporary=false,expanded=false,busy=false,motionUnavailable=false;
    bool gesture=false;
    char name[64]{},hint[64]{};
    bool operator==(const SkillCue&) const=default;
};
inline bool Fresh(const Snapshot& s,uint64_t session,uint64_t now) {
    return session&&s.session==session&&now>=s.at&&now-s.at<=650;
}
enum class Action { None, Potion, Equip, Quest, Map, Character, Skills, BindSkill, Developer };
enum class Result { None, Pending, Applied, Refused, Cancelled, Dispatched, KitRefused };
struct Request {
    Action action=Action::None;
    uint64_t sequence=0,session=0,at=0;
    int index=-1;
    Item item{};
    uint32_t revision=0;
    int skill=-1,skillOwner=-1,bank=-1,previousSkill=-1,previousOwner=-1;
};
inline bool CanBind(const Snapshot& s,const Request& r,uint64_t now) {
    if(!Fresh(s,r.session,now)||!s.bindingsEditable||r.index<0||r.index>=skills::Count||
       s.bindingBank!=r.bank||(r.bank!=0&&r.bank!=16)||s.skills[r.index].id!=r.previousSkill||
       s.skills[r.index].owner!=r.previousOwner)return false;
    if(r.skill==-1)return r.skillOwner==-1;
    for(unsigned i=0;i<s.skillCount&&i<s.catalog.size();++i)
        if(s.catalog[i].id==r.skill&&s.catalog[i].owner==r.skillOwner&&s.catalog[i].available)return true;
    return false;
}
struct Reply {uint64_t sequence=0,session=0;Result result=Result::None;};
inline bool SameBelt(const Snapshot& s,const Request& r) {
    if(!Fresh(s,r.session,r.at)||r.index<0||r.index>=4)return false;
    const auto& b=s.belt[r.index];
    return b.known&&b.count&&b.Next().handle&&b.Next()==r.item&&b.revision==r.revision;
}
inline bool PotionGone(const Snapshot& s,const Request& r) {
    if(s.session!=r.session||!s.beltKnown)return false;
    for(const auto& b:s.belt)for(const auto& item:b.items)if(item.handle==r.item.handle)return false;
    return r.item.handle!=0;
}

// Wheel coordinates are shared by painter and hit testing. Eight native slots
// clockwise from the upper-right segment; center is weapon mode.
constexpr float Pi=3.14159265358979323846f;
constexpr float WheelPhase=Pi/8;
enum class MenuPage { Spells, Equipment, Map, EditSlots, EditSkills, Developer };
struct MenuBounds {
    float left,top,right,bottom;
    bool Contains(float u,float v) const {return u>left&&u<right&&v>top&&v<bottom;}
};
constexpr MenuBounds MapContent{.14f,.12f,.86f,.84f};
// Footer cells occupy 9-13. Keep developer actions separate as the list grows.
constexpr int DeveloperCell(int index) {return 14+index;}
constexpr int DeveloperChoice(int cell) {return cell-14;}
inline MenuBounds DeveloperBounds(int index) {
    const float x=.05f+(index%2)*.46f,y=.20f+(index/2)*.087f;
    return {x,y,x+.44f,y+.082f};
}
inline bool Editing(MenuPage page) {return page==MenuPage::EditSlots||page==MenuPage::EditSkills;}
inline int FooterCount(MenuPage page,bool developer=false) {return Editing(page)?3:developer?5:4;}
inline MenuBounds FooterBounds(int index,MenuPage page,bool developer=false) {
    const float step=.96f/FooterCount(page,developer),left=.03f+index*step;
    return {left,.895f,left+step-.02f,.965f};
}
inline int FooterCell(int index,MenuPage page) {
    constexpr int tabs[]={9,12,10,11,13};return Editing(page)?9+index:tabs[index];
}
inline int WheelCell(float u,float v,int previous=-1,MenuPage page=MenuPage::Spells,int hand=-1,bool developer=false) {
    if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1)return -1;
    for(int i=0;i<FooterCount(page,developer);++i)if(FooterBounds(i,page,developer).Contains(u,v))return FooterCell(i,page);
    if(page==MenuPage::Map)return -1; // no invisible spells behind the map
    if(page==MenuPage::Developer) {
        if(developer)for(int i=0;i<int(dev::Choices.size());++i)
            if(DeveloperBounds(i).Contains(u,v))return DeveloperCell(i);
        return -1;
    }
    if(page==MenuPage::EditSkills) {
        if(u>.74f&&u<.95f&&v>.12f&&v<.19f)return 8; // clear selected slot
        for(int i=0;i<8;++i) {
            const float x=.05f+(i%2)*.46f,y=.23f+(i/2)*.145f;
            if(u>x&&u<x+.44f&&v>y&&v<y+.13f)return i;
        }
        return -1;
    }
    const float x=u-.5f,y=v-.45f,r=std::hypot(x,y);
    if(r<.105f)return 8;
    if(r<.14f||r>.405f)return -1;
    float a=std::atan2(x,-y);if(a<0)a+=2*Pi;
    if(hand>=0&&page==MenuPage::Spells)return int(std::floor((a+Pi/5)/(2*Pi/5)))%5;
    if(previous>=0&&previous<8) {
        const float difference=std::remainder(a-WheelPhase-previous*Pi/4,2*Pi);
        if(std::fabs(difference)<Pi/8+.035f)return page==MenuPage::Equipment&&previous==2?-1:previous;
    }
    const int cell=int(std::floor(a/(Pi/4)))%8;
    return page==MenuPage::Equipment&&cell==2?-1:cell; // Journal is accessed through the native tabbed menu.
}
struct Config {
    float waistBelowEyes=.65f,beltForward=.26f,hipWidth=.28f;
    bool seated=false,holsters=false;
    int dominant=0;
};
struct Input {
    double time=0;
    uint64_t session=0,epoch=0,at=0; // at shares the native snapshot's monotonic clock
    bool active=false,tracked[2]{},wheel=false,cancel=false,reservedLeft=false,spell=false,quickCast=false;
    vitals::Location navigationLocation=vitals::Location::Wrist;
    vitals::Location vitalsLocation=vitals::Location::Wrist;
    float vitalsTilt=0;
    bool criticalAlerts=false,leftCasting=false;
    Pose head{},hand[2]{};
    float trigger=0,leftTrigger=0,grip[2]{};
    int hover=-1,wheelHand=-1;
    Snapshot snapshot{};
    Reply reply{};
};
struct Output {
    bool developer=false;
    SkillCue cue{},leftCue{};
    Owner owner[2]{Owner::Combat,Owner::Combat};
    bool block=false,wheel=false,utility=false,map=false,stowed=false,wrist=false,cancelPending=false,critical=false,settings=false;
    bool vitals=false,palm=false,navigation=false,navigationPalm=false;
    int wheelHand=-1;
    bool inventory=false;
    int select=-2,bottle=-1,bottleHand=-1,nearBelt=-1,nearHolster=-1,hover=-1;
    int feedback=0,feedbackHand=0;
    Result result=Result::None;
    Item bottleItem{};
    bool bottleEmpty=false,beltReady=false;
    Pose body{},belt[4]{},holster[2]{};
    Request request{};
    int editor=0,editSlot=-1,editPage=0,editPages=1,editCount=0;
    std::array<Skill,8> editOptions{};
};
inline MenuPage Page(const Output& out) {
    return out.developer?MenuPage::Developer:out.editor==2?MenuPage::EditSkills:out.editor?MenuPage::EditSlots:out.map?MenuPage::Map:out.utility?MenuPage::Equipment:MenuPage::Spells;
}
inline bool MenuMapWanted(const Input& in,const Output& out,uint64_t now) {
    return in.active&&out.wheel&&out.map&&!out.editor&&Fresh(in.snapshot,in.session,now);
}
inline bool NavigationWanted(const Input& in,const Output& out,uint64_t now) {
    return in.active&&in.navigationLocation!=vitals::Location::Off&&out.navigation&&!out.wheel&&!out.settings&&
        in.tracked[1]&&Fresh(in.snapshot,in.session,now);
}
inline bool MapWanted(const Input& in,const Output& out,uint64_t now) {
    return MenuMapWanted(in,out,now)||NavigationWanted(in,out,now);
}
class Engine {
    bool developer=false;
    Config config{};
    uint64_t session=0,epoch=0,sequence=0;
    double lastTime=0,drinkStart=0,pendingAt=0,resultAt=0,criticalUntil=0,criticalAgain=0;
    bool primed=false,triggerWas=false,gripWas[2]{},wheelLatch=false,utility=false,map=false,rearm=true,stowed=false;
    bool spent=false,consumed=false;
    Owner owner[2]{Owner::Combat,Owner::Combat};
    int bottle=-1,bottleHand=-1,holster=-1,holsterHand=-1,pendingHand=0;
    Pose body{};
    Vec holsterGrab{};
    Request held{},pending{};
    Result result=Result::None;
    int editor=0,editSlot=-1,editPage=0,editBank=-1,choiceCount=0,lastWheelHand=-1;
    Skill previousBinding{};
    shoulder::Gesture shoulderSwap;
    vitals::PalmReveal palmReveal;
    std::array<Skill,128> choices{};
    void Drop() {bottle=bottleHand=holster=holsterHand=-1;drinkStart=0;spent=consumed=false;held={};owner[0]=owner[1]=Owner::Combat;}
public:
    void Configure(Config c) {config=c;Reset();}
    void Reset() {primed=false;rearm=true;wheelLatch=true;Drop();pending={};result=Result::None;criticalUntil=criticalAgain=0;editor=0;map=developer=false;editSlot=-1;shoulderSwap.Reset();}
    MenuPage Page() const {return developer?MenuPage::Developer:editor==2?MenuPage::EditSkills:editor?MenuPage::EditSlots:map?MenuPage::Map:utility?MenuPage::Equipment:MenuPage::Spells;}
    bool WheelVisible(bool heldButton) const {return heldButton&&!wheelLatch;}
    Output Step(const Input& in) {
        Output out;out.wheelHand=in.wheelHand;
        // Passive vitals need the left hand and head only. Combat/interactions
        // still require both hands; a resting/occluded right hand must not hide
        // a correctly presented watch or permit an input through the guard.
        out.wrist=in.active&&in.session&&in.tracked[1]&&std::isfinite(in.time)&&WristPresented(in.hand[1],in.head);
        const bool palmAllowed=in.active&&in.tracked[1]&&(vitals::Palm(in.vitalsLocation)||vitals::Palm(in.navigationLocation))&&
            !in.wheel&&!in.cancel&&!in.leftCasting&&in.leftTrigger<.2f&&in.grip[1]<.6f&&
            !in.reservedLeft&&bottleHand!=1&&holsterHand!=1&&Fresh(in.snapshot,in.session,in.at);
        const bool palm=palmReveal.Step(in.session,in.epoch,in.time,palmAllowed,
            PalmPresented(in.hand[1],in.head),PalmPresented(in.hand[1],in.head,true));
        out.palm=palm&&vitals::Palm(in.vitalsLocation);
        out.vitals=out.palm||(out.wrist&&vitals::Wrist(in.vitalsLocation));
        out.navigationPalm=palm&&vitals::Palm(in.navigationLocation);
        out.navigation=out.navigationPalm||(out.wrist&&vitals::Wrist(in.navigationLocation));
        const bool trigger=in.trigger>(triggerWas?.2f:.55f);
        const bool grips[2]={in.grip[0]>.6f,in.grip[1]>.6f};
        const uint64_t now=in.at;
        const bool fresh=Fresh(in.snapshot,in.session,now);
        const double dt=in.time-lastTime;
        if(!in.active||!in.tracked[0]||!in.tracked[1]||!std::isfinite(in.time)||
           !Valid(in.head)||!Valid(in.hand[0])||!Valid(in.hand[1])||!primed||session!=in.session||epoch!=in.epoch||dt<=0||dt>.15) {
            Reset();primed=in.active&&in.tracked[0]&&in.tracked[1]&&Valid(in.head)&&Valid(in.hand[0])&&Valid(in.hand[1])&&std::isfinite(in.time);
            if(session!=in.session)stowed=false;
            session=in.session;epoch=in.epoch;lastTime=in.time;
            triggerWas=trigger;gripWas[0]=grips[0];gripWas[1]=grips[1];
            body=Valid(in.head)?UprightBody(in.head,body.q):Pose{};out.block=true;out.stowed=stowed;out.cancelPending=true;return out;
        }
        lastTime=in.time;
        const bool handChanged=in.wheelHand!=lastWheelHand;
        if(handChanged){editor=0;editSlot=-1;map=utility=developer=false;rearm=true;wheelLatch=false;lastWheelHand=in.wheelHand;}
        const bool triggerEdge=trigger&&!triggerWas&&!handChanged;
        const bool edges[2]={grips[0]&&!gripWas[0],grips[1]&&!gripWas[1]};
        triggerWas=trigger;gripWas[0]=grips[0];gripWas[1]=grips[1];
        if(!in.wheel){wheelLatch=false;editor=0;map=developer=false;editSlot=-1;}
        if(!in.snapshot.developer)developer=false;
        if(!trigger&&!grips[0]&&!grips[1])rearm=false;
        if(in.cancel) {Drop();rearm=true;wheelLatch=true;pending={};out.cancelPending=true;editor=0;map=developer=false;editSlot=-1;}
        if(pending.action!=Action::None) {
            if(in.reply.sequence==pending.sequence&&in.reply.session==session&&in.reply.result!=Result::Pending) {
                result=in.reply.result;resultAt=in.time;out.feedback=(result==Result::Applied||result==Result::Dispatched)?2:3;
                out.feedbackHand=pendingHand;
                if(result==Result::Applied&&pending.action==Action::Equip)stowed=false;
                if(result==Result::Applied&&pending.action==Action::Potion&&pending.item==held.item)consumed=true;
                if(result==Result::Applied&&pending.action==Action::BindSkill&&editor){editor=in.wheelHand>=0?0:1;editSlot=-1;}
                pending={};
            } else if(in.time-pendingAt>(pending.action==Action::Developer?10.:1.5)) {result=Result::Refused;resultAt=in.time;out.feedback=3;pending={};out.cancelPending=true;}
        }
        if(result!=Result::Pending&&in.time-resultAt>2.5)result=Result::None;
        const auto& stats=in.snapshot;
        const bool low=in.criticalAlerts&&in.vitalsLocation!=vitals::Location::Off&&
            fresh&&stats.vitalsKnown&&stats.health>0&&stats.maxHealth>0&&
            (stats.health/stats.maxHealth<.25f||(in.spell&&stats.maxMana>0&&stats.mana/stats.maxMana<.10f));
        if(low&&in.time>=criticalAgain){criticalUntil=in.time+1.5;criticalAgain=in.time+10;out.feedback=4;out.feedbackHand=1;}
        if(!low)criticalUntil=criticalAgain=0;
        out.critical=low&&in.time<criticalUntil;
        auto request=[&](Action a,int index,Item item={},uint32_t revision=0) {
            if(pending.action!=Action::None)return false;
            pending={a,++sequence,session,now,index,item,revision};pendingAt=in.time;pendingHand=0;
            out.request=pending;result=Result::Pending;return true;
        };
        // Follow even with lowered hands or a held bottle: proximity must not
        // leave the belt behind as the player turns, steps or crouches.
        body=UprightBody(in.head,body.q);
        out.body=body;
        const float waist=config.seated?.43f:config.waistBelowEyes;
        for(int i=0;i<4;++i)out.belt[i]={body.p+math::Rotate(body.q,{(i-1.5f)*.095f,-waist+.08f,-config.beltForward}),body.q};
        out.beltReady=true;
        for(int i=0;i<2;++i)out.holster[i]={body.p+math::Rotate(body.q,{(i==0?1:-1)*config.hipWidth,-waist,-.06f}),body.q};
        auto distance=[](Vec a,Vec b){return std::sqrt(math::Dot(a-b,a-b));};
        const bool wheel=in.wheel&&!wheelLatch&&bottle<0&&holster<0;
        const int dominant=config.dominant==1?1:0;
        shoulder::Input reach;reach.time=in.time;reach.session=in.session;reach.epoch=in.epoch;reach.dominant=dominant;
        reach.active=in.active;reach.tracked=in.tracked[dominant];reach.head=in.head.p;reach.hand=in.hand[dominant].p;reach.heading=body.q;
        reach.grip=in.grip[dominant];
        reach.allowed=!wheel&&!in.quickCast&&!in.cancel&&!rearm&&bottle<0&&holster<0&&pending.action==Action::None&&
            fresh&&in.snapshot.equipmentKnown&&(in.snapshot.activeSet==1||in.snapshot.activeSet==2);
        const auto shoulder=shoulderSwap.Step(reach);out.block=out.block||shoulder.block;
        if(shoulder.swap&&request(Action::Equip,3-int(in.snapshot.activeSet))){
            pendingHand=dominant;out.feedback=1;out.feedbackHand=dominant;
        }
        if(wheel) {
            out.wheel=true;out.utility=utility;out.map=map;out.hover=in.hover;out.block=true;
            owner[0]=owner[1]=Owner::Wheel;
            if(editor&&fresh&&in.snapshot.bindingBank!=editBank){editor=in.wheelHand>=0?0:1;editSlot=-1;editBank=in.snapshot.bindingBank;rearm=true;}
            if(!rearm&&triggerEdge&&pending.action==Action::None) {
                if(!editor&&in.hover==13&&fresh&&in.snapshot.developer) {
                    developer=!developer;utility=map=false;out.utility=out.map=false;rearm=true;
                } else if(developer&&dev::ValidChoice(DeveloperChoice(in.hover))) {
                    if(fresh&&dev::ValidClass(in.snapshot.devClass))request(Action::Developer,DeveloperChoice(in.hover));
                    else out.feedback=3;
                    rearm=true;
                } else if(editor==2) {
                    if(in.hover==11){editor=in.wheelHand>=0?0:1;editSlot=-1;}
                    else if(in.hover==9)editPage=std::max(0,editPage-1);
                    else if(in.hover==10)editPage=std::min(std::max(0,(choiceCount-1)/8),editPage+1);
                    else if(in.hover==8||(in.hover>=0&&in.hover<8&&editPage*8+in.hover<choiceCount)) {
                        const auto skill=in.hover==8?Skill{}:choices[editPage*8+in.hover];
                        if(fresh&&in.snapshot.bindingsEditable&&request(Action::BindSkill,editSlot)) {
                            pending.skill=skill.id;pending.skillOwner=skill.owner;pending.bank=editBank;
                            pending.previousSkill=previousBinding.id;pending.previousOwner=previousBinding.owner;
                            out.request=pending;
                        } else out.feedback=3;
                    }
                    rearm=true;
                } else if(editor==1) {
                    if(in.hover==9||in.hover==11){editor=0;editSlot=-1;}
                    else if(in.hover>=0&&in.hover<8&&fresh&&in.snapshot.bindingsEditable) {
                        editSlot=in.hover;editPage=0;previousBinding=in.snapshot.skills[editSlot];
                        choices=in.snapshot.catalog;choiceCount=std::min<int>(in.snapshot.skillCount,choices.size());editor=2;
                    } else out.feedback=3;
                    rearm=true;
                } else if(in.wheelHand>=0&&in.hover==11){developer=false;out.inventory=true;out.wheel=false;wheelLatch=true;rearm=true;}
                else if(in.wheelHand>=0&&!utility&&!map&&in.hover>=0&&in.hover<5&&fresh&&in.snapshot.bindingsEditable){
                    editSlot=skills::HandSlot(in.wheelHand,in.hover);editPage=0;editBank=in.snapshot.bindingBank;previousBinding=in.snapshot.skills[editSlot];
                    choices=in.snapshot.catalog;choiceCount=std::min<int>(in.snapshot.skillCount,choices.size());editor=2;rearm=true;
                }
                else if(in.hover==11){developer=false;editor=1;editBank=in.snapshot.bindingBank;editSlot=-1;map=false;out.map=false;utility=false;out.utility=false;rearm=true;}
                else if(in.hover==10){out.settings=true;out.wheel=false;wheelLatch=true;rearm=true;}
                else if(in.hover==12){developer=false;map=true;out.map=true;utility=false;out.utility=false;rearm=true;}
                else if(in.hover==9){utility=(map||developer)?false:!utility;developer=false;out.utility=utility;map=false;out.map=false;rearm=true;}
                else if(!map&&in.hover==8) {if(in.wheelHand<0)out.select=-1;stowed=false;wheelLatch=true;out.wheel=false;rearm=true;out.feedback=1;}
                else if(!map&&in.hover>=0&&in.hover<8&&(in.wheelHand<0||utility)&&(!utility||in.hover!=2)) {
                    if(!utility) {
                        const auto& skill=in.snapshot.skills[in.hover];
                        if(!fresh||!skill.known||skill.available) {
                            out.select=in.hover;stowed=false;wheelLatch=true;out.wheel=false;out.feedback=1;
                        } else out.feedback=3;
                    } else if(in.hover<2) {if(fresh&&in.snapshot.equipmentKnown)request(Action::Equip,in.hover+1);else out.feedback=3;}
                    else if(in.hover==3) {stowed=!stowed;out.feedback=1;}
                    else {
                        const int col=in.hover-4;const auto& b=in.snapshot.belt[col];
                        if(fresh&&b.known&&b.count&&b.Next().handle)request(Action::Potion,col,b.Next(),b.revision);
                        else out.feedback=3;
                    }
                    if(utility){wheelLatch=true;out.wheel=false;}
                    rearm=true;
                }
            }
            const int pointerHand=in.wheelHand>=0?1-in.wheelHand:0;
            if(!editor&&!map&&utility&&!rearm&&edges[pointerHand]&&in.hover>=4&&in.hover<8) {
                const auto& b=in.snapshot.belt[in.hover-4];
                if(fresh&&b.known&&b.count&&b.Next().handle) {
                    bottle=in.hover-4;bottleHand=pointerHand;held={Action::Potion,0,session,now,bottle,b.Next(),b.revision};
                    owner[pointerHand]=Owner::Bottle;wheelLatch=true;out.wheel=false;out.feedback=1;out.feedbackHand=pointerHand;
                }
            }
        } else if(bottle<0&&holster<0)owner[0]=owner[1]=Owner::Combat;
        if(!wheel&&!in.quickCast&&!rearm&&bottle<0&&holster<0&&pending.action==Action::None&&fresh) {
            float nearestHand=.095f;
            for(int h=0;h<2;++h) {
                if(h==1&&in.reservedLeft)continue;
                int nearest=-1;float reach=.095f;
                for(int i=0;i<4;++i) {
                    const auto& b=in.snapshot.belt[i];
                    const float d=distance(in.hand[h].p,out.belt[i].p);
                    if(b.known&&b.count&&b.Next().handle&&d<reach){nearest=i;reach=d;}
                }
                if(nearest>=0) {
                    if(reach<nearestHand){out.nearBelt=nearest;nearestHand=reach;}
                    const auto& b=in.snapshot.belt[nearest];
                    if(edges[h]) {
                        bottle=nearest;bottleHand=h;held={Action::Potion,0,session,now,nearest,b.Next(),b.revision};
                        out.nearBelt=nearest;
                        owner[h]=Owner::Bottle;out.feedback=1;out.feedbackHand=h;break;
                    }
                }
                if(bottle>=0)break;
                for(int i=0;i<2&&config.holsters;++i)if(distance(in.hand[h].p,out.holster[i].p)<.115f) {
                    out.nearHolster=i;
                    if(edges[h]&&in.snapshot.equipmentKnown) {
                        holster=i;holsterHand=h;
                        holsterGrab=math::Rotate(math::Conjugate(body.q),in.hand[h].p-body.p);
                        owner[h]=Owner::Holster;out.feedback=1;out.feedbackHand=h;break;
                    }
                }
                if(holster>=0)break;
            }
        }
        if(holster>=0) {
            out.block=true;out.nearHolster=holster;
            if(!grips[holsterHand]){Drop();rearm=true;}
            // Moving/turning the body with the gripping hand is not a draw.
            else if(distance(holsterGrab,math::Rotate(math::Conjugate(body.q),in.hand[holsterHand].p-body.p))>.12f) {
                request(Action::Equip,holster+1);Drop();rearm=true;
            }
        }
        if(bottle>=0) {
            out.block=true;
            held.at=now;
            if(!grips[bottleHand]||(!spent&&(!fresh||!SameBelt(in.snapshot,held)))) {Drop();rearm=true;}
            else {
                out.bottle=bottle;out.bottleHand=bottleHand;out.bottleItem=held.item;out.bottleEmpty=consumed;owner[bottleHand]=Owner::Bottle;
                if(!spent&&potions::AtMouth(in.hand[bottleHand].p,in.hand[bottleHand].q,in.head.p,in.head.q,drinkStart!=0)) {
                    if(!drinkStart)drinkStart=in.time;
                    if(in.time-drinkStart>=.25&&request(Action::Potion,bottle,held.item,held.revision)){spent=true;pendingHand=bottleHand;}
                } else drinkStart=0;
            }
        }
        out.developer=developer;out.stowed=stowed;out.result=result;
        out.editor=editor;out.editSlot=editSlot;out.editPage=editPage;out.editPages=std::max(1,(choiceCount+7)/8);
        if(editor==2)for(int i=0;i<8&&editPage*8+i<choiceCount;++i){out.editOptions[i]=choices[editPage*8+i];++out.editCount;}
        out.block=out.block||rearm||stowed||pending.action!=Action::None;
        for(int h=0;h<2;++h)out.owner[h]=rearm?Owner::Rearm:owner[h];
        return out;
    }
};
}
