#pragma once
#include "shoulder_swap.h"
#include "skill_slots.h"
#include "skill_categories.h"
#include "frame_math.h"
#include "potion_pose.h"
#include "wrist_hud.h"
#include "hand_vitals.h"
#include "dev_scenarios.h"
#include "navigation_info.h"
#include "companions.h"
#include "active_effects.h"
#include "torch_light.h"
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
inline Pose NavigationMapPanel(Pose hand,Pose head,bool palm,bool vitalsOnFace,float footer=0) {
    auto panel=palm?PalmPanel(hand,head):WristPanel(hand,head);
    if(vitalsOnFace)panel.p=panel.p+math::Rotate(panel.q,{0,((palm?PalmHeight:WristHeight)+WristMapSize)*.5f+.01f+footer,0});
    return panel;
}
constexpr float WristCompassWidth=.24f,WristCompassHeight=.045f;
inline Pose NavigationCompassPanel(Pose hand,Pose head,bool palm,bool vitalsOnFace,float footer=0) {
    auto panel=NavigationMapPanel(hand,head,palm,vitalsOnFace,footer);
    panel.p=panel.p+math::Rotate(panel.q,{0,(WristMapSize+WristCompassHeight)*.5f+.01f,0});
    return panel;
}
constexpr float NavigationInfoWidth=WristCompassWidth;
constexpr float NavigationInfoHeight=NavigationInfoWidth*navigation::Height/navigation::Width;
inline Pose NavigationInfoPanel(Pose hand,Pose head,bool palm,bool vitalsOnFace,float footer=0) {
    auto panel=NavigationCompassPanel(hand,head,palm,vitalsOnFace,footer);
    panel.p=panel.p+math::Rotate(panel.q,{0,(WristCompassHeight+NavigationInfoHeight)*.5f+.006f,0});
    return panel;
}
inline float CompanionHeight(int rows){return WristMapSize*companions::RowHeight/companions::Width*rows;}
inline float CompanionSpace(int rows){return rows>0?CompanionHeight(rows)+.006f:0;}
// A footer below the complete map, with extra spacing above wrist vitals.
inline Pose NavigationCompanionsPanel(Pose hand,Pose head,bool palm,bool vitalsOnFace,int rows) {
    auto panel=NavigationMapPanel(hand,head,palm,vitalsOnFace,CompanionSpace(rows));
    panel.p=panel.p+math::Rotate(panel.q,{0,-(WristMapSize+CompanionHeight(rows))*.5f-.006f,0});
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
    uint32_t socketed=0;
    uint32_t fireAffix=0;
    uint32_t fireDamage=0;
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
// A released Health/Rejuvenation/Antidote/Thawing bottle flies in room metres and lands on the
// world floor. With the physics world (Input::physicsThrows) it flies as a
// body there instead and lands where it first touches anything: a wall, a
// table, a prop or the floor. Only its landing point leaves the engine; the game thread
// decides whether it fell at the hireling's feet, and the native belt key's
// mercenary use (not an item edit) gives the potion to the mercenary.
// Launch speed is the PEAK hand speed in the last ThrowWindow before the grip
// opens (the hand is already slowing by then), 1:1 with the real hand. Fast
// samples are capped at ThrowMaxSpeed, never discarded; only tracking jumps
// above ThrowGlitchSpeed are ignored.
constexpr float ThrowGravity=9.81f,ThrowMinSpeed=1.4f,ThrowMaxSpeed=20.f,ThrowGlitchSpeed=30.f,ThrowClearOfBelt=.15f;
constexpr float ThrowMaxSpin=30.f; // rad/s; a wrist flick, not a tracking jump
constexpr double ThrowWindow=.12,ThrowMaxFlight=3.;
constexpr double TossPickup=.2; // the physics world takes a throw within this, else it flies the arc
// The player's own motion through the world (running, walking): the stage
// frame's velocity in world metres per second. A throw or a let-go bottle
// carries it, as anything thrown carries its thrower's speed. Whether a
// release is a throw still depends on the hand alone. A jump faster than any
// run (a waypoint, a portal, a recentre) or a gap in the frames resets it.
constexpr float CarryMaxSpeed=10.f;
constexpr double CarryTime=.1,CarryGap=.25;
struct Carry {
    bool known=false;
    Vec velocity{};
    double at=0;
    Vec was{};
    bool have=false;
    void Reset() {*this={};}
    void Update(double time,Vec anchor) {
        if(!std::isfinite(time)||!std::isfinite(anchor.x)||!std::isfinite(anchor.y)||!std::isfinite(anchor.z)){Reset();return;}
        if(!have||!(time>at)||time-at>CarryGap){*this={};have=true;at=time;was=anchor;return;}
        const float dt=float(time-at);const Vec v=(anchor-was)*(1/dt);
        at=time;was=anchor;
        if(math::Dot(v,v)>CarryMaxSpeed*CarryMaxSpeed){known=false;velocity={};return;}
        velocity=known?velocity+(v-velocity)*std::min(1.f,dt/float(CarryTime)):v;known=true;
    }
};
// Hand turn between two poses as an angular velocity (rad/s, the poses' frame).
inline Vec Spin(Quat from,Quat to,float dt) {
    Quat d=math::Multiply(to,math::Conjugate(from));
    if(d.w<0)d={-d.x,-d.y,-d.z,-d.w};
    const float s=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
    if(!(dt>0)||!std::isfinite(s)||s<1e-7f)return {};
    const float rate=2*std::atan2(s,d.w)/dt;
    return Vec{d.x,d.y,d.z}*(rate/s);
}
// Potions the game itself lets a mercenary take from the belt with Shift.
inline bool Throwable(Potion type) {
    return type==Potion::Health||type==Potion::Rejuvenation||type==Potion::Antidote||type==Potion::Thawing;
}
inline double LandingTime(Vec p,Vec v,float floor) {
    const double drop=double(p.y)-floor;
    if(!std::isfinite(drop)||drop<=0||!std::isfinite(v.y))return -1;
    const double t=(v.y+std::sqrt(double(v.y)*v.y+2*ThrowGravity*drop))/ThrowGravity;
    return t>0&&t<=ThrowMaxFlight?t:-1;
}
inline Vec Ballistic(Vec p,Vec v,float t) {return p+v*t+Vec{0,-.5f*ThrowGravity*t*t,0};}
// Stage-to-world frame of the current view (yaw only): world metres =
// anchor + base * (room - origin). Walking, turning and recentering change it.
// A thrown bottle lives in the world once released, so it is integrated in
// world metres and only mapped back to room space for drawing.
struct Stage {
    bool known=false;
    Vec origin{},anchor{};Quat base{0,0,0,1};
    Vec World(Vec room) const {return anchor+math::Rotate(base,room-origin);}
    Vec Room(Vec world) const {return origin+math::Rotate(math::Conjugate(base),world-anchor);}
    bool operator==(const Stage&) const=default;
};
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
    int32_t missile=-1; // native srvmissile of the assigned skill, -1 if none
    bool known=false,available=false,directed=false,motion=false,physical=false;
    bool strike=false; // a melee skill physical strikes cast, its compiled row confirmed (strike_skills.h)
    char name[64]{};
    bool operator==(const Skill&) const = default;
};
// The local hireling, sampled on the game thread during VR gameplay.
struct Mercenary {
    bool known=false;
    float health=0,maxHealth=0;
    uint32_t classId=0,unitId=0;
    bool operator==(const Mercenary&) const=default;
};
constexpr float MercLowHealth=.5f; // cue threshold: below 50% life
inline bool MercLow(const Mercenary& m) {return m.known&&m.maxHealth>0&&m.health>0&&m.health<m.maxHealth*MercLowHealth;}
struct QuickItem {
    Item item{};
    int quantity=0;
    char name[80]{};
    bool operator==(const QuickItem&) const=default;
};
struct QuickInventory {
    bool known=false;
    uint32_t assigned=0;
    std::array<QuickItem,128> items{};
    unsigned count=0;
    bool operator==(const QuickInventory&) const=default;
    const QuickItem* Find(uint32_t code) const {
        for(unsigned i=0;i<count&&i<items.size();++i)if(items[i].item.code==code)return &items[i];
        return nullptr;
    }
};
struct Snapshot {
    QuickInventory quick;
    bool developer=false;
    bool devDebugDraws=false;
    int devTurnsKeepOwn=0;   // [stereo] turns_keep_own as it stands (DEV switch): 0 off, 1 near things, 2 everywhere
    bool devTurnsSharp=false;   // [stereo] turns_sharp as it stands (DEV switch)
    bool devTurnsFill=false;    // and turns_fill
    bool devTurnsRims=false;    // and turns_rims
    bool devSkyRims=false;      // and [sky] cover_rims
    int devClass=-1,skillClass=-1;
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
    Mercenary merc;
    companions::Roster companions;
    effects::Status effects;
};
inline bool HasTorchInHand(const Snapshot& s, int hand, uint32_t& outFlags) {
    outFlags = 0;
    if (!s.equipmentKnown || (s.activeSet != 1 && s.activeSet != 2) || hand < 0 || hand > 1) return false;
    const auto& item = s.sets[s.activeSet - 1][hand];
    if (!item.code) return false;
    char codeText[5]{};
    codeText[0] = static_cast<char>(item.code & 0xFF);
    codeText[1] = static_cast<char>((item.code >> 8) & 0xFF);
    codeText[2] = static_cast<char>((item.code >> 16) & 0xFF);
    codeText[3] = static_cast<char>((item.code >> 24) & 0xFF);
    const bool enchant = s.effects.Has(16);
    const bool devClub = false;
    const auto qual = framevr::torch::EvaluateQualification(codeText, item.fireDamage, enchant, devClub);
    outFlags = static_cast<uint32_t>(qual);
    return framevr::torch::IsTorchQualified(outFlags);
}
struct TorchHandTag {
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
enum class Action { None, Potion, Equip, Quest, Map, Character, Skills, BindSkill, Developer, FeedMerc, BindItem, UseItem, RunWalk };
enum class Result { None, Pending, Applied, Refused, Cancelled, Dispatched, KitRefused, DebugUpdated, TurnsUpdated, SharpUpdated, FillUpdated, RimsUpdated, SkyRimsUpdated };
struct Request {
    Action action=Action::None;
    uint64_t sequence=0,session=0,at=0;
    int index=-1;
    Item item{};
    uint32_t revision=0;
    int skill=-1,skillOwner=-1,bank=-1,previousSkill=-1,previousOwner=-1;
    // FeedMerc: the landing point in world metres (and room metres for
    // diagnostics). The transport adds the rendered player and scale.
    Vec point{},world{},player{};
    float unitsPerMetre=0;
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
enum class MenuPage { Spells, Equipment, Map, EditSlots, EditSkills, Developer, EditItems };
struct MenuBounds {
    float left,top,right,bottom;
    bool Contains(float u,float v) const {return u>left&&u<right&&v>top&&v<bottom;}
};
constexpr int QuickUseCell=210,LeftStrikesCell=212;   // 211 was RUN / WALK, 213 the movement slider (now VR Settings > VR > Movement speed)
constexpr double QuickEditHold=.65;
constexpr MenuBounds QuickUseBounds{.025f,.025f,.255f,.18f};
// Druid: the left claw's strike skill, [gameplay] left_hand_strikes. Lower left,
// above the ACTIVE line (y 637) and clear of the hand wheel's 216-degree label.
constexpr MenuBounds LeftStrikesBounds{.025f,.72f,.235f,.82f};
inline bool QuickControls(MenuPage page){return page==MenuPage::Spells||page==MenuPage::Equipment;}
constexpr int SkillCategoryCell(int category){return 220+category;}
inline MenuBounds SkillCategoryBounds(int category){
    const float x=.05f+category*.184f;return {x,.205f,x+.174f,.29f};
}
inline MenuBounds SkillOptionBounds(int index){
    const float x=.05f+(index%2)*.46f,y=.305f+(index/2)*.13f;
    return {x,y,x+.44f,y+.118f};
}
// The skill picker's card for the option pointed at: over the other column,
// from the category row's top to the last row's bottom; its text, in a
// SkillCardFont px font, starts SkillCardTop pixels down (of 768),
// SkillCardPad in from the sides.
inline MenuBounds SkillCardBounds(int index){
    const auto first=SkillOptionBounds(index%2?0:1),last=SkillOptionBounds(index%2?6:7);
    return {first.left,SkillCategoryBounds(0).top,first.right,last.bottom};
}
constexpr int SkillCardTop=62,SkillCardPad=12,SkillCardFont=16;
constexpr MenuBounds MapContent{.14f,.12f,.86f,.84f};
// Footer cells occupy 9-13. Keep developer actions separate as the list grows.
constexpr int DeveloperCell(int index) {return 14+index;}
constexpr int DeveloperChoice(int cell) {return cell-14;}
constexpr int DeveloperPageCell(int page) {return 200+page;}
inline MenuBounds DeveloperPageBounds(int page) {
    const float step=.92f/dev::PageCount,x=.05f+page*step;
    return {x,.135f,x+step-.02f,.185f};
}
inline MenuBounds DeveloperBounds(int index) {
    index=dev::SlotFor(index);
    const float x=.05f+(index%2)*.46f,y=.20f+(index/2)*.087f;
    return {x,y,x+.44f,y+.082f};
}
inline bool Editing(MenuPage page) {return page==MenuPage::EditSlots||page==MenuPage::EditSkills||page==MenuPage::EditItems;}
inline int FooterCount(MenuPage page,bool developer=false) {return Editing(page)?3:developer?5:4;}
inline MenuBounds FooterBounds(int index,MenuPage page,bool developer=false) {
    const float step=.96f/FooterCount(page,developer),left=.03f+index*step;
    return {left,.895f,left+step-.02f,.965f};
}
inline int FooterCell(int index,MenuPage page) {
    constexpr int tabs[]={9,12,10,11,13};return Editing(page)?9+index:tabs[index];
}
inline int WheelCell(float u,float v,int previous=-1,MenuPage page=MenuPage::Spells,int hand=-1,bool developer=false,int devPage=0,bool leftStrikes=false) {
    if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1)return -1;
    for(int i=0;i<FooterCount(page,developer);++i)if(FooterBounds(i,page,developer).Contains(u,v))return FooterCell(i,page);
    if(QuickControls(page)) {
        if(QuickUseBounds.Contains(u,v))return QuickUseCell;
        if(leftStrikes&&LeftStrikesBounds.Contains(u,v))return LeftStrikesCell;
    }
    if(page==MenuPage::Map)return -1; // no invisible spells behind the map
    if(page==MenuPage::Developer) {
        if(developer&&devPage>=0&&devPage<dev::PageCount) {
            for(int i=0;i<dev::PageCount;++i)
                if(DeveloperPageBounds(i).Contains(u,v))return DeveloperPageCell(i);
            for(int i:dev::Pages[devPage])
                if(i>=0&&DeveloperBounds(i).Contains(u,v))return DeveloperCell(i);
        }
        return -1;
    }
    if(page==MenuPage::EditItems) {
        if(u>.74f&&u<.95f&&v>.12f&&v<.19f)return 8;
        for(int i=0;i<8;++i) {
            const float x=.05f+(i%2)*.46f,y=.23f+(i/2)*.145f;
            if(u>x&&u<x+.44f&&v>y&&v<y+.13f)return i;
        }
        return -1;
    }
    if(page==MenuPage::EditSkills) {
        if(u>.74f&&u<.95f&&v>.12f&&v<.19f)return 8; // clear selected slot
        for(int category=0;category<skillcategories::Count;++category)
            if(SkillCategoryBounds(category).Contains(u,v))return SkillCategoryCell(category);
        for(int i=0;i<8;++i)if(SkillOptionBounds(i).Contains(u,v))return i;
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
    bool gaitToggle=false;   // the game's run / walk toggle is the other way than the hero should go (Movement speed's WALK or a run stop, or the stick's switch): send it
    int leftStrikes=-1;   // Druid: [gameplay] left_hand_strikes for the wheel's box (0 own trigger, 1 same as right); -1 hides it
    vitals::Location navigationLocation=vitals::Location::Wrist;
    vitals::Location vitalsLocation=vitals::Location::Wrist;
    float vitalsTilt=0;
    bool criticalAlerts=false,leftCasting=false;
    bool floorKnown=false; float floor=0; // room-space height of the world ground under the player
    Stage stage{};
    Pose head{},hand[2]{};
    Vec handVelocity[2]{}; bool velocityKnown[2]{}; // runtime-reported room-space m/s at the pose time
    float trigger=0,leftTrigger=0,grip[2]{};
    int hover=-1,wheelHand=-1;
    Snapshot snapshot{};
    Reply reply{};
    // Throws fly in the physics world (bottles::World), which reports the
    // body of the last one: id, pose in world metres, and whether it landed.
    bool physicsThrows=false;
    struct Tossed {uint64_t id=0;bool landed=false;Vec p{};Quat q{0,0,0,1};} tossed{};
    Vec carry{}; // the player's own world velocity (Carry), zero when unknown
};
struct Output {
    bool developer=false;
    int devPage=0;
    SkillCue cue{},leftCue{};
    Owner owner[2]{Owner::Combat,Owner::Combat};
    bool block=false,wheel=false,utility=false,map=false,stowed=false,wrist=false,cancelPending=false,critical=false,settings=false;
    bool vitals=false,palm=false,navigation=false,navigationPalm=false;
    int wheelHand=-1;
    bool inventory=false;
    int leftStrikes=-1;             // the wheel's left-claw box: shown value, -1 hidden
    bool toggleLeftStrikes=false;   // clicked this step: the transport saves the other choice
    int select=-2,bottle=-1,bottleHand=-1,nearBelt=-1,nearHolster=-1,hover=-1;
    int feedback=0,feedbackHand=0;
    Result result=Result::None;
    Item bottleItem{};
    bool bottleEmpty=false,beltReady=false;
    bool flying=false; Pose flight{}; Item flightItem{}; int flightColumn=-1; // thrown bottle, model pose
    // An emptied bottle let go of, this step only: its model pose and the
    // hand's motion in world metres (Stage::World), and the floor under the
    // player. It falls into the world as a physics body, drawn only.
    struct Discard {bool active=false;Vec p{},v{},w{};Quat q{0,0,0,1};float floor=0;uint32_t code=0;} discard{};
    // A bottle thrown this step, for the physics world (Input::physicsThrows):
    // the same release as a discard, named by id. flightId is the physics
    // throw the flying bottle follows, 0 on the arc.
    struct Toss {bool active=false;uint64_t id=0;Vec p{},v{},w{};Quat q{0,0,0,1};float floor=0;uint32_t code=0;} toss{};
    uint64_t flightId=0;
    Pose body{},belt[4]{},holster[2]{};
    Request request{};
    int editor=0,editSlot=-1,editPage=0,editPages=1,editCount=0,editCategory=0,editClass=-1;
    std::array<int,skillcategories::Count> editCategoryCounts{};
    std::array<Skill,8> editOptions{};
    std::array<QuickItem,8> itemOptions{};
};
inline MenuPage Page(const Output& out) {
    return out.developer?MenuPage::Developer:out.editor==3?MenuPage::EditItems:out.editor==2?MenuPage::EditSkills:out.editor?MenuPage::EditSlots:out.map?MenuPage::Map:out.utility?MenuPage::Equipment:MenuPage::Spells;
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
    int devPage=0;
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
    double quickPressedAt=0;
    bool quickPressed=false;
    Item quickPressedItem{};
    uint32_t quickPressedCode=0;
    std::array<QuickItem,128> itemChoices{};
    uint32_t itemAssignment=0;
    void EditItems(const Snapshot& s) {
        editor=3;editPage=0;itemChoices=s.quick.items;choiceCount=std::min<unsigned>(s.quick.count,itemChoices.size());
        itemAssignment=s.quick.assigned;quickPressed=false;rearm=true;
    }
    shoulder::Gesture shoulderSwap;
    vitals::PalmReveal palmReveal;
    std::array<Skill,128> choices{};
    std::array<int,128> choiceIndices{};
    std::array<int,skillcategories::Count> categoryCounts{};
    int allCount=0,editCategory=0,editClass=-1;
    const Skill& Choice(int index)const{return choices[choiceIndices[index]];}
    void FilterSkills(int category) {
        editCategory=category;editPage=choiceCount=0;
        for(int i=0;i<allCount;++i)
            if(category==skillcategories::All||skillcategories::Category(editClass,choices[i].id,choices[i].owner)==category)
                choiceIndices[choiceCount++]=i;
    }
    void OpenSkills(const Snapshot& s) {
        // Freeze identities, ordering and class for the whole edit transaction.
        choices=s.catalog;allCount=std::clamp<int>(s.skillCount,0,choices.size());editClass=s.skillClass;
        categoryCounts={};categoryCounts[skillcategories::All]=allCount;
        for(int i=0;i<allCount;++i)++categoryCounts[skillcategories::Category(editClass,choices[i].id,choices[i].owner)];
        FilterSkills(skillcategories::All);editor=2;
    }
    // p, v and q are in WORLD metres/orientation; stage is the latest frame
    // used to draw it, kept through a brief loss of the view frame.
    struct Flight {bool active=false;double start=0,land=0;Vec p{},v{};Quat q{0,0,0,1};int column=-1,hand=-1;Item item{};uint32_t revision=0;Stage stage{};
                   uint64_t id=0;double arc=0;} flight; // id: the physics throw it follows (0 = the arc); arc: the arc's landing time
    uint64_t tossSerial=0;
    struct Motion {double at=0;Vec v{},w{};};
    std::array<Motion,32> motion{};unsigned motionNext=0;
    Vec bottleWas{};Quat bottleTurnWas{0,0,0,1};bool bottleTracked=false;
    void Drop() {bottle=bottleHand=holster=holsterHand=-1;drinkStart=0;spent=consumed=false;held={};owner[0]=owner[1]=Owner::Combat;
        bottleTracked=false;motion={};motionNext=0;}
    // Fastest recent sample, capped in magnitude, keeping its direction.
    bool PeakVelocity(double now,Vec& out,float minimum=ThrowMinSpeed) const {
        float best=0;
        for(const auto& m:motion) {
            if(!m.at||now-m.at>ThrowWindow||m.at>now)continue;
            const float s=std::sqrt(math::Dot(m.v,m.v));
            if(s>best){best=s;out=m.v;}
        }
        if(best<minimum)return false;
        if(best>ThrowMaxSpeed)out=out*(ThrowMaxSpeed/best);
        return true;
    }
    // Mean hand turn over the release window, capped.
    Vec ReleaseSpin(double now) const {
        Vec sum{};int n=0;
        for(const auto& m:motion)if(m.at&&now-m.at<=ThrowWindow&&m.at<=now){sum=sum+m.w;++n;}
        if(!n)return {};
        sum=sum*(1.f/n);
        const float s=std::sqrt(math::Dot(sum,sum));
        return s>ThrowMaxSpin?sum*(ThrowMaxSpin/s):sum;
    }
    // Let go of an emptied bottle: it leaves the hand into the world with the
    // hand's motion, slow or fast. Nothing native follows; it is drawn only.
    void Discard(const Input& in,Output& out) const {
        if(!consumed||!in.floorKnown||!in.stage.known)return;
        const auto& hand=in.hand[bottleHand];const auto& stage=in.stage;
        Vec v{};PeakVelocity(in.time,v,0);
        out.discard={true,stage.World(potions::Center(hand.p,hand.q)),math::Rotate(stage.base,v)+in.carry,
            math::Rotate(stage.base,ReleaseSpin(in.time)),math::Multiply(stage.base,potions::HeldRotation(hand.q)),
            stage.anchor.y+(in.floor-stage.origin.y),held.item.code};
    }
    // Grip released with recent deliberate motion, away from the belt: launch.
    // A slow or still release keeps the original behaviour (back to the belt).
    bool Throw(const Input& in,bool fresh,Vec slot,Output& out) {
        Vec throwVelocity{};
        if(spent||!in.floorKnown||!in.stage.known||!fresh||!SameBelt(in.snapshot,held)||!Throwable(PotionType(held.item.code))||
           !PeakVelocity(in.time,throwVelocity))return false;
        const auto& hand=in.hand[bottleHand];
        const Vec p=potions::Center(hand.p,hand.q);
        if(math::Dot(p-slot,p-slot)<ThrowClearOfBelt*ThrowClearOfBelt)return false;
        // Release: freeze the bottle into the world. The floor under the
        // player at release is the landing plane (yaw-only frame: heights map 1:1).
        const auto& stage=in.stage;
        const Vec worldP=stage.World(p),worldV=math::Rotate(stage.base,throwVelocity)+in.carry;
        const double land=LandingTime(worldP,worldV,stage.anchor.y+(in.floor-stage.origin.y));
        if(land<=0)return false;
        flight={true,in.time,in.time+land,worldP,worldV,math::Multiply(stage.base,potions::HeldRotation(hand.q)),
            bottle,bottleHand,held.item,held.revision,stage};
        if(in.physicsThrows) {
            flight.id=++tossSerial;flight.arc=flight.land;flight.land=in.time+ThrowMaxFlight;
            out.toss={true,flight.id,worldP,worldV,math::Rotate(stage.base,ReleaseSpin(in.time)),flight.q,
                stage.anchor.y+(in.floor-stage.origin.y),held.item.code};
        }
        return true;
    }
public:
    void Configure(Config c) {config=c;Reset();}
    void Reset() {quickPressed=false;primed=false;rearm=true;wheelLatch=true;Drop();flight={};pending={};result=Result::None;criticalUntil=criticalAgain=0;editor=devPage=0;map=developer=false;editSlot=-1;shoulderSwap.Reset();}
    MenuPage Page() const {return developer?MenuPage::Developer:editor==3?MenuPage::EditItems:editor==2?MenuPage::EditSkills:editor?MenuPage::EditSlots:map?MenuPage::Map:utility?MenuPage::Equipment:MenuPage::Spells;}
    int DeveloperPage() const {return devPage;}
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
        if(handChanged){quickPressed=false;editor=0;editSlot=-1;map=utility=developer=false;rearm=true;wheelLatch=false;lastWheelHand=in.wheelHand;}
        const bool triggerEdge=trigger&&!triggerWas&&!handChanged;
        const bool edges[2]={grips[0]&&!gripWas[0],grips[1]&&!gripWas[1]};
        triggerWas=trigger;gripWas[0]=grips[0];gripWas[1]=grips[1];
        if(!in.wheel){quickPressed=false;wheelLatch=false;editor=0;map=developer=false;editSlot=-1;}
        if(!in.snapshot.developer)developer=false;
        if(!trigger&&!grips[0]&&!grips[1])rearm=false;
        if(in.cancel) {quickPressed=false;Drop();flight={};rearm=true;wheelLatch=true;pending={};out.cancelPending=true;editor=0;map=developer=false;editSlot=-1;}
        if(pending.action!=Action::None) {
            if(in.reply.sequence==pending.sequence&&in.reply.session==session&&in.reply.result!=Result::Pending) {
                result=in.reply.result;resultAt=in.time;out.feedback=(result==Result::Applied||result==Result::Dispatched||result==Result::DebugUpdated||result==Result::TurnsUpdated||result==Result::SharpUpdated||result==Result::FillUpdated||result==Result::RimsUpdated||result==Result::SkyRimsUpdated)?2:3;
                out.feedbackHand=pendingHand;
                if(result==Result::Applied&&pending.action==Action::Equip)stowed=false;
                if(result==Result::Applied&&pending.action==Action::Potion&&pending.item==held.item)consumed=true;
                if(result==Result::Applied&&pending.action==Action::BindItem&&editor==3){editor=0;rearm=true;}
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
        // VR Settings > Movement speed: WALK walks, every other stop runs. When the game's
        // toggle is the other way, its own run / walk command (no tick).
        if(in.gaitToggle&&fresh&&request(Action::RunWalk,0))pendingHand=0;
        if(wheel) {
            out.wheel=true;out.utility=utility;out.map=map;out.hover=in.hover;out.block=true;out.leftStrikes=in.leftStrikes;
            owner[0]=owner[1]=Owner::Wheel;
            if((editor==1||editor==2)&&fresh&&in.snapshot.bindingBank!=editBank){editor=in.wheelHand>=0?0:1;editSlot=-1;editBank=in.snapshot.bindingBank;rearm=true;}
            // A hold edits; a short click uses on release. A lost ray/session
            // or changed item cancels without spending anything.
            if(quickPressed) {
                if(!fresh||!in.snapshot.quick.known||in.hover!=QuickUseCell||!QuickControls(Page())||in.snapshot.quick.assigned!=quickPressedCode)quickPressed=false;
                else if(trigger&&in.time-quickPressedAt>=QuickEditHold)EditItems(in.snapshot);
                else if(!trigger) {
                    quickPressed=false;
                    if(!quickPressedCode)EditItems(in.snapshot);
                    else {
                        const auto* current=in.snapshot.quick.Find(quickPressedCode);
                        if(current&&current->quantity>0&&current->item==quickPressedItem&&request(Action::UseItem,int(quickPressedCode),quickPressedItem)) {
                            pendingHand=in.wheelHand>=0?1-in.wheelHand:0;
                            wheelLatch=true;out.wheel=false;
                        } else out.feedback=3;
                        rearm=true;
                    }
                }
            }
            if(!rearm&&triggerEdge&&pending.action==Action::None) {
                if(QuickControls(Page())&&in.hover==QuickUseCell) {
                    if(fresh&&in.snapshot.quick.known) {
                        quickPressed=true;quickPressedAt=in.time;quickPressedCode=in.snapshot.quick.assigned;
                        const auto* assigned=in.snapshot.quick.Find(quickPressedCode);
                        quickPressedItem=assigned?assigned->item:Item{};
                    } else out.feedback=3;
                } else if(QuickControls(Page())&&in.hover==LeftStrikesCell) {
                    // A local setting, not a game command: the wheel stays open to show it.
                    if(in.leftStrikes>=0){out.toggleLeftStrikes=true;out.feedback=1;}else out.feedback=3;
                    out.feedbackHand=in.wheelHand>=0?1-in.wheelHand:0;rearm=true;
                } else if(editor==3) {
                    if(in.hover==11){editor=0;editSlot=-1;}
                    else if(in.hover==9)editPage=std::max(0,editPage-1);
                    else if(in.hover==10)editPage=std::min(std::max(0,(choiceCount-1)/8),editPage+1);
                    else if(in.hover==8||(in.hover>=0&&in.hover<8&&editPage*8+in.hover<choiceCount)) {
                        const auto item=in.hover==8?QuickItem{}:itemChoices[editPage*8+in.hover];
                        if(fresh&&in.snapshot.quick.known&&in.snapshot.quick.assigned==itemAssignment&&
                           request(Action::BindItem,int(item.item.code),item.item,itemAssignment))pendingHand=in.wheelHand>=0?1-in.wheelHand:0;
                        else out.feedback=3;
                    }
                    rearm=true;
                } else if(!editor&&in.hover==13&&fresh&&in.snapshot.developer) {
                    developer=!developer;devPage=0;utility=map=false;out.utility=out.map=false;rearm=true;
                } else if(developer&&in.hover>=DeveloperPageCell(0)&&in.hover<DeveloperPageCell(dev::PageCount)) {
                    if(fresh)devPage=in.hover-DeveloperPageCell(0);
                    out.hover=-1;rearm=true;
                } else if(developer&&dev::ValidChoice(DeveloperChoice(in.hover))) {
                    if(fresh&&dev::ValidClass(in.snapshot.devClass)&&dev::PageFor(DeveloperChoice(in.hover))==devPage)
                        request(Action::Developer,DeveloperChoice(in.hover));
                    else out.feedback=3;
                    rearm=true;
                } else if(editor==2) {
                    if(in.hover>=SkillCategoryCell(0)&&in.hover<SkillCategoryCell(skillcategories::Count)) {
                        const int category=in.hover-SkillCategoryCell(0);
                        if(fresh&&categoryCounts[category])FilterSkills(category);
                        else out.feedback=3;
                    }
                    else if(in.hover==11){editor=in.wheelHand>=0?0:1;editSlot=-1;}
                    else if(in.hover==9)editPage=std::max(0,editPage-1);
                    else if(in.hover==10)editPage=std::min(std::max(0,(choiceCount-1)/8),editPage+1);
                    else if(in.hover==8||(in.hover>=0&&in.hover<8&&editPage*8+in.hover<choiceCount)) {
                        const auto skill=in.hover==8?Skill{}:Choice(editPage*8+in.hover);
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
                        OpenSkills(in.snapshot);
                    } else out.feedback=3;
                    rearm=true;
                } else if(in.wheelHand>=0&&in.hover==11){developer=false;out.inventory=true;out.wheel=false;wheelLatch=true;rearm=true;}
                else if(in.wheelHand>=0&&!utility&&!map&&in.hover>=0&&in.hover<5&&fresh&&in.snapshot.bindingsEditable){
                    editSlot=skills::HandSlot(in.wheelHand,in.hover);editPage=0;editBank=in.snapshot.bindingBank;previousBinding=in.snapshot.skills[editSlot];
                    OpenSkills(in.snapshot);rearm=true;
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
            if(!editor&&!map&&utility&&!rearm&&!flight.active&&edges[pointerHand]&&in.hover>=4&&in.hover<8) {
                const auto& b=in.snapshot.belt[in.hover-4];
                if(fresh&&b.known&&b.count&&b.Next().handle) {
                    bottle=in.hover-4;bottleHand=pointerHand;held={Action::Potion,0,session,now,bottle,b.Next(),b.revision};
                    owner[pointerHand]=Owner::Bottle;wheelLatch=true;out.wheel=false;out.feedback=1;out.feedbackHand=pointerHand;
                }
            }
        } else {quickPressed=false;if(bottle<0&&holster<0)owner[0]=owner[1]=Owner::Combat;}
        if(!wheel&&!in.quickCast&&!rearm&&bottle<0&&holster<0&&!flight.active&&pending.action==Action::None&&fresh) {
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
            // Prefer the runtime's own controller velocity; fall back to the
            // pose difference when the runtime does not report one.
            const Vec handNow=in.hand[bottleHand].p;const Quat turnNow=in.hand[bottleHand].q;
            if(in.velocityKnown[bottleHand]||bottleTracked) {
                const Vec v=in.velocityKnown[bottleHand]?in.handVelocity[bottleHand]:(handNow-bottleWas)*(1/float(dt));
                const Vec w=bottleTracked?Spin(bottleTurnWas,turnNow,float(dt)):Vec{};
                if(math::Dot(v,v)<=ThrowGlitchSpeed*ThrowGlitchSpeed){motion[motionNext]={in.time,v,w};motionNext=(motionNext+1)%motion.size();}
            }
            bottleWas=handNow;bottleTurnWas=turnNow;bottleTracked=true;
            if(!grips[bottleHand]&&Throw(in,fresh,out.belt[bottle].p,out)) {Drop();rearm=true;out.feedback=1;out.feedbackHand=flight.hand;}
            else if(!grips[bottleHand]||(!spent&&(!fresh||!SameBelt(in.snapshot,held)))) {if(!grips[bottleHand])Discard(in,out);Drop();rearm=true;}
            else {
                out.bottle=bottle;out.bottleHand=bottleHand;out.bottleItem=held.item;out.bottleEmpty=consumed;owner[bottleHand]=Owner::Bottle;
                if(!spent&&potions::AtMouth(in.hand[bottleHand].p,in.hand[bottleHand].q,in.head.p,in.head.q,drinkStart!=0)) {
                    if(!drinkStart)drinkStart=in.time;
                    if(in.time-drinkStart>=.25&&request(Action::Potion,bottle,held.item,held.revision)){spent=true;pendingHand=bottleHand;}
                } else drinkStart=0;
            }
        }
        if(flight.active) {
            if(in.stage.known)flight.stage=in.stage; // follow walking/turning for drawing only
            // The physics world never took it (unavailable now): the arc after all.
            const bool carried=flight.id&&in.tossed.id==flight.id;
            if(flight.id&&!carried&&in.time-flight.start>=TossPickup){flight.id=0;flight.land=flight.arc;}
            if((carried&&in.tossed.landed)||in.time>=flight.land) {
                // Landed: one exact-item request. A miss or a dead/absent
                // hireling is refused natively-side and the bottle stays belted.
                const float t=float(flight.land-flight.start);
                const Vec where=carried?in.tossed.p:Ballistic(flight.p,flight.v,t);
                const Request landed{Action::FeedMerc,0,session,now,flight.column,flight.item,flight.revision};
                if(fresh&&SameBelt(in.snapshot,landed)&&request(Action::FeedMerc,flight.column,flight.item,flight.revision)) {
                    pending.world=where;pending.point=flight.stage.Room(pending.world);
                    out.request=pending;pendingHand=flight.hand;
                } else out.feedback=3;
                out.feedbackHand=flight.hand;flight={};
            } else {
                const float t=float(in.time-flight.start),spin=t*9.f; // tumble end over end
                out.flying=true;out.flightColumn=flight.column;out.flightItem=flight.item;out.flightId=flight.id;
                if(carried)out.flight={flight.stage.Room(in.tossed.p),math::Multiply(math::Conjugate(flight.stage.base),in.tossed.q)};
                else {
                    const Quat world=math::Multiply(flight.q,{std::sin(spin*.5f),0,0,std::cos(spin*.5f)});
                    out.flight={flight.stage.Room(Ballistic(flight.p,flight.v,t)),math::Multiply(math::Conjugate(flight.stage.base),world)};
                }
            }
        }
        out.developer=developer;out.devPage=devPage;out.stowed=stowed;out.result=result;
        out.editor=editor;out.editSlot=editSlot;out.editPage=editPage;out.editPages=std::max(1,(choiceCount+7)/8);
        if(editor==3)for(int i=0;i<8&&editPage*8+i<choiceCount;++i){out.itemOptions[i]=itemChoices[editPage*8+i];++out.editCount;}
        out.editCategory=editCategory;out.editClass=editClass;out.editCategoryCounts=categoryCounts;
        if(editor==2)for(int i=0;i<8&&editPage*8+i<choiceCount;++i){out.editOptions[i]=Choice(editPage*8+i);++out.editCount;}
        out.block=out.block||rearm||stowed||pending.action!=Action::None;
        for(int h=0;h<2;++h)out.owner[h]=rearm?Owner::Rearm:owner[h];
        return out;
    }
};
}
