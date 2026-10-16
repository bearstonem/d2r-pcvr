#pragma once
#include "frame_math.h"
#include <cstdint>
#include "skill_slots.h"
#include "weapon_skills.h"

// Room-space gesture recognizer. No game memory, damage, inventory edits or
// synthetic velocity from head/camera turning. Right hand = 0, left = 1.
namespace framevr::gameplay {
using math::Vec; using math::Quat;
struct Pose { Vec p{}; Quat q{0,0,0,1}; };
inline float Length(Vec v) { return std::sqrt(math::Dot(v,v)); }
inline bool Finite(Vec v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
inline bool Valid(Pose p) {
    const float n=p.q.x*p.q.x+p.q.y*p.q.y+p.q.z*p.q.z+p.q.w*p.q.w;
    return Finite(p.p)&&std::isfinite(n)&&n>.9f&&n<1.1f;
}
enum class Weapon { Unknown, Melee, Bow, Crossbow, Other, Javelin };
// Input gating and posed contact recognition must agree: real wrist-led
// slashes exceed 35 rad/s. A stricter upstream limit continually cancels
// contact recovery before those otherwise valid swings can reach it.
inline constexpr float MeleeAngularTrackingLimit=60.f;
// Gesture sensitivity, not hitbox dimensions or native weapon reach. Selected
// by equipped weapon category, never by character class or animation rig.
enum class MeleeStyle { Unarmed, Short, OneHand, Long };
inline float MeleeLeverage(MeleeStyle style) {
    switch(style) {
        case MeleeStyle::Unarmed:return 0;
        case MeleeStyle::Short:return .18f;
        case MeleeStyle::Long:return .55f;
        default:return .35f;
    }
}
inline float RotationDistance(Quat a,Quat b) {
    const float n=std::sqrt((a.x*a.x+a.y*a.y+a.z*a.z+a.w*a.w)*(b.x*b.x+b.y*b.y+b.z*b.z+b.w*b.w));
    return 2*std::acos(std::clamp(std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)/n,0.f,1.f));
}
struct Input {
    double time=0;
    bool active=false, tracked[2]{}, aimTracked=false, palette=false, cancel=false;
    Pose head{}, hand[2]{}, aim{};
    float grip[2]{}, trigger=0;
    Weapon weapon=Weapon::Unknown;
    MeleeStyle meleeStyle=MeleeStyle::OneHand;
    uint32_t equipment=0;
    // Pointer cell: 0..7 native controller skills, 8 attack, 9 swap,
    // 10/11 unused, 12..15 belt columns. -1 = no hit.
    int hover=-1;
    bool handBindings=false;
    bool quickCast=false;
    int quickSkill=-1;
    int selectedSkill=-1,selectedOwner=-1;
    int bowSlot=-1,bowSkill=-1,bowOwner=-1;
    uint64_t skillContext=0;
    bool selectedAvailable=false,shieldEquipped=false;
};
struct Output {
    bool attack=false, block=false, palette=false, bowDrawing=false, rayValid=false;
    bool bowReady=false, bowInReach=false, bowCharged=false;
    bool meleeContact=false,bowRelease=false,javelinRelease=false,javelinReady=false;
    bool nativeBowRelease=false,shieldContact=false;
    float meleeSpeed=0;
    int skill=-1, potion=-1, bottle=-1; // potion is a one-shot request, not proof of consumption
    bool swap=false;
    unsigned feedback=0; // 1 select/nock, 2 attack/release, 3 drink request, 4 string nearby, 5 draw ready, 6 melee ready
    Vec rayOrigin{}, rayDirection{}, nockPoint{}, bowForward{};
    float draw=0;
};
inline Vec BowForward(Pose hand) {
    // Match the existing bow grip convention: knuckles point through the bow,
    // with controller forward as a fallback when the knuckles point vertically.
    const Vec knuckles=math::Rotate(hand.q,{0,-1,0});
    return math::Normalized(std::fabs(knuckles.y)<.7f?knuckles:math::Rotate(hand.q,{0,0,-1}));
}
inline int PaletteCell(float u,float v) {
    if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>=1||v<.125f||v>=.875f)return -1;
    return int((v-.125f)/.1875f)*4+int(u*4);
}
// Values from XInput's public button constants; no Windows dependency in tests.
// Shared by native aim and locomotion routing: a selected skill alone does
// not own input, and a stale/blocked cast cannot change the movement route.
inline bool CastingSkill(const Input& in,const Output& out,uint64_t ageMs) {
    return in.active&&!out.block&&out.rayValid&&ageMs<150&&out.skill>=0&&out.skill<skills::Count;
}
inline uint16_t SkillButtons(int slot) {
    return skills::Buttons(slot);
}
inline bool SkillModifier(int slot) { return skills::Alternate(slot); }
// Only a ready physical basic crossbow shot consumes its native attack button.
// Leaving that button down enters the native attack animation and stops walking.
inline bool RouteCrossbow(const Input& in,Output& out,bool rightHand,bool ready,bool skillAvailable,int skillId) {
    if(!ready||!rightHand||!in.active||in.weapon!=Weapon::Crossbow||out.block||!out.rayValid||
       !(out.attack||(out.skill>=0&&skillAvailable&&skillId==0)))return false;
    out.attack=false;out.skill=-1;return true;
}

class Engine {
    bool primed=false, triggerWas=false, gripWas=false, blockedTrigger=true;
    bool drawing=false, blockedGrip=true, bowWasInReach=false, bowWasCharged=false;
    int selection=-1, bottle=-1;
    bool bottleSpent=false;
    double previousTime=0, drawStart=0, pulseUntil=0, shotAimUntil=0, drinkStart=0;
    Vec previousHand{},previousLeft{},shotOrigin{},shotDirection{};
    Quat previousRotation{0,0,0,1};
    uint32_t equipment=0;
    Weapon weapon=Weapon::Unknown;
    float nockGap=0;
    bool throwing=false;
    Vec throwStart{},throwVelocity{};
    double throwAt=0,velocityAt=0;
    float throwTravel=0;
    int drawSkill=-1,drawOwner=-1;
    uint64_t drawContext=0;
    bool nativeBowTrigger=false;
    weaponskills::Pulse skillPulse;
public:
    void Cancel() {
        const int chosen=selection;const auto heldWeapon=weapon;const auto heldEquipment=equipment;
        *this=Engine{};selection=chosen;weapon=heldWeapon;equipment=heldEquipment;
    }
    int Selection() const { return selection; }
    void Select(int slot) {if(slot>=-1&&slot<skills::Count){Cancel();selection=slot;}}
    Output Step(const Input& in) {
        Output out;
        // Analog trigger hysteresis: a small squeeze fluctuation must not let
        // go of a grabbed string. Fully release to shoot or rearm after cancel.
        const bool trigger=in.trigger>(triggerWas?.20f:.55f), grip=in.grip[0]>.6f;
        const bool valid=in.active&&in.tracked[0]&&in.tracked[1]&&in.aimTracked&&
                         Valid(in.head)&&Valid(in.hand[0])&&Valid(in.hand[1])&&Valid(in.aim)&&std::isfinite(in.time);
        if(!valid||in.cancel) {Cancel();return out;}
        const double dt=in.time-previousTime;
        const bool changed=weapon!=in.weapon||equipment!=in.equipment;
        // Hitches, recenter/pose discontinuities and equipment changes cancel
        // pending actions. Held controls must be released before starting again.
        if(!primed||dt<=0||dt>.10||changed||Length(in.hand[0].p-previousHand)>.35f||Length(in.hand[1].p-previousLeft)>.35f||
           ((in.weapon==Weapon::Melee||in.weapon==Weapon::Javelin)&&bottle<0&&!in.palette&&!in.quickCast&&RotationDistance(previousRotation,in.hand[0].q)>MeleeAngularTrackingLimit*dt)) {
            Cancel();primed=true;previousTime=in.time;previousHand=in.hand[0].p;previousRotation=in.hand[0].q;previousLeft=in.hand[1].p;
            if(changed&&!in.handBindings&&(in.weapon==Weapon::Bow||in.weapon==Weapon::Melee||in.weapon==Weapon::Javelin))selection=-1;
            weapon=in.weapon;equipment=in.equipment;triggerWas=trigger;gripWas=grip;
            out.block=true;return out;
        }
        const Vec handDelta=in.hand[0].p-previousHand;
        previousTime=in.time;previousHand=in.hand[0].p;previousRotation=in.hand[0].q;previousLeft=in.hand[1].p;
        const bool triggerEdge=trigger&&!triggerWas, gripEdge=grip&&!gripWas;
        const bool released=!trigger&&triggerWas;
        triggerWas=trigger;gripWas=grip;
        if(!trigger)blockedTrigger=false;
        if(!grip)blockedGrip=false;
        out.palette=in.palette;
        out.rayOrigin=in.aim.p;out.rayDirection=math::Rotate(in.aim.q,{0,0,-1});out.rayValid=true;

        if(in.palette) {
            drawing=throwing=nativeBowTrigger=false;skillPulse.Cancel();pulseUntil=0;blockedTrigger=true;
            if(bottle<0&&triggerEdge) {
                if(in.hover>=0&&in.hover<8) {selection=in.hover;out.feedback=1;}
                else if(in.hover==8) {selection=-1;out.feedback=1;}
                else if(in.hover==9) {out.swap=true;out.feedback=1;}
            }
            if(bottle<0&&!blockedGrip&&gripEdge&&in.hover>=12&&in.hover<16) {
                bottle=in.hover-12;bottleSpent=false;drinkStart=0;out.feedback=1;
            }
        }
        if(bottle>=0) {
            blockedTrigger=true;drawing=throwing=nativeBowTrigger=false;skillPulse.Cancel();pulseUntil=0;
            if(!grip) {bottle=-1;bottleSpent=false;drinkStart=0;}
            else {
                out.bottle=bottle;
                const Vec mouth=in.head.p+math::Rotate(in.head.q,{0,-.10f,-.08f});
                const bool atMouth=Length(in.hand[0].p-mouth)<.23f;
                const bool tipped=math::Rotate(in.hand[0].q,{0,1,0}).y<.25f;
                if(!bottleSpent&&atMouth&&tipped) {
                    if(!drinkStart)drinkStart=in.time;
                    if(in.time-drinkStart>=.25) {out.potion=bottle;out.feedback=3;bottleSpent=true;}
                } else drinkStart=0;
            }
            out.block=true;return out;
        }
        if(in.palette) {out.block=true;return out;}
        if(in.quickCast) {
            drawing=throwing=nativeBowTrigger=false;skillPulse.Cancel();bowWasCharged=false;pulseUntil=shotAimUntil=0;blockedTrigger=true;
            if(in.quickSkill>=0&&in.quickSkill<skills::Count)out.skill=in.quickSkill;
            return out; // preserve selected trigger skill; no melee or bow release
        }
        if(blockedTrigger) {out.block=true;return out;}

        const int bowSlot=in.handBindings?in.bowSlot:selection;
        const int bowSkill=in.handBindings?in.bowSkill:in.selectedSkill,bowOwner=in.handBindings?in.bowOwner:in.selectedOwner;
        const bool skillBow=bowSlot>=0&&(in.handBindings||in.selectedAvailable)&&weaponskills::Bow(bowSkill);
        if((drawing||(nativeBowTrigger&&!in.handBindings))&&((!in.handBindings&&selection>=0&&!in.selectedAvailable)||drawSkill!=bowSkill||drawOwner!=bowOwner||drawContext!=in.skillContext)) {
            drawing=nativeBowTrigger=false;skillPulse.Cancel();blockedTrigger=true;out.block=true;return out;
        }

        if(in.weapon==Weapon::Javelin&&selection<0) {
            out.javelinReady=true;
            if(triggerEdge) {
                throwing=true;throwAt=in.time;throwStart=in.hand[0].p;
                throwVelocity={};velocityAt=0;throwTravel=0;out.feedback=1;
            }
            if(throwing) {
                throwTravel=std::max(throwTravel,Length(in.hand[0].p-throwStart));
                const auto velocity=handDelta*(1/float(dt));
                // Store recent deliberate room-space motion, including the
                // release frame. A short grace period tolerates finger timing
                // without turning a stationary drop into a delayed throw.
                if(Length(velocity)>=1.4f&&Length(velocity)<=12.f) {
                    throwVelocity=velocity;velocityAt=in.time;
                }
                if(released) {
                    if(in.time-throwAt>=.08&&in.time-throwAt<=3&&throwTravel>=.18f&&
                       velocityAt&&in.time-velocityAt<=.08) {
                        out.javelinRelease=true;out.feedback=2;
                        out.rayOrigin=in.hand[0].p;out.rayDirection=math::Normalized(throwVelocity);
                        out.draw=std::clamp(Length(throwVelocity)/5.f,0.f,1.f);
                    }
                    throwing=false;
                }
            }
            out.meleeContact=!trigger&&!out.javelinRelease;
            return out; // only the release spends ammunition; never hold A
        }

        if(in.weapon==Weapon::Bow&&(in.handBindings||selection<0||skillBow)) {
            const Vec line=in.hand[1].p-in.hand[0].p;
            const float gap=Length(line);
            out.bowReady=true;out.bowForward=BowForward(in.hand[1]);
            out.rayOrigin=in.hand[1].p;out.rayDirection=out.bowForward;
            out.nockPoint=in.hand[1].p-out.bowForward*.16f;
            out.bowInReach=Length(in.hand[0].p-out.nockPoint)<.28f;
            if(out.bowInReach&&!bowWasInReach&&!drawing)out.feedback=4;
            bowWasInReach=out.bowInReach;
            // The bow belongs to the left hand; the right trigger still grabs
            // its string. Away from it, the right trigger keeps its own skill.
            if(in.handBindings&&(nativeBowTrigger||(triggerEdge&&!out.bowInReach))) {
                nativeBowTrigger=trigger;
                if(trigger){out.skill=selection;out.attack=selection<0;
                    out.rayOrigin=in.aim.p;out.rayDirection=math::Rotate(in.aim.q,{0,0,-1});return out;}
                return out;
            }
            // The equipped bow already belongs to the left hand. No extra
            // grip clutch: trigger grabs its visible string target, including
            // when squeezed just before entering the generous grab region.
            // Away from the string, a fresh press retains the native comfort
            // fallback. Once that press casts, moving into the string cannot
            // also nock an arrow until the trigger has been released.
            if(!in.handBindings&&skillBow&&triggerEdge&&!out.bowInReach) {
                nativeBowTrigger=true;drawSkill=in.selectedSkill;drawOwner=in.selectedOwner;drawContext=in.skillContext;
            }
            if(nativeBowTrigger) {
                if(trigger) {out.skill=selection;out.rayOrigin=in.aim.p;out.rayDirection=math::Rotate(in.aim.q,{0,0,-1});return out;}
                nativeBowTrigger=false;
            }
            if(trigger&&!drawing&&out.bowInReach&&in.time>=pulseUntil) {
                drawing=true;drawStart=in.time;nockGap=gap;out.feedback=1;
                drawSkill=bowSkill;drawOwner=bowOwner;drawContext=in.skillContext;
            }
            if(drawing&&gap>1.10f) {drawing=false;blockedTrigger=true;out.block=true;return out;}
            out.bowCharged=drawing&&gap>=.28f&&gap-nockGap>=.12f&&in.time-drawStart>=.12;
            if(out.bowCharged&&!bowWasCharged)out.feedback=5;
            bowWasCharged=out.bowCharged;
            if(drawing&&released) {
                if(out.bowCharged&&math::Dot(math::Normalized(line),out.bowForward)>.15f) {
                    shotOrigin=in.hand[1].p;shotDirection=math::Normalized(line);
                    pulseUntil=in.time+.065;out.feedback=2;
                    if(skillBow) {
                        out.nativeBowRelease=skillPulse.Start(bowSlot,bowSkill,bowOwner,in.skillContext,in.time);
                    } else out.bowRelease=true;
                    shotAimUntil=in.time+.4;
                }
                drawing=false;
            }
            out.bowDrawing=drawing;out.draw=std::clamp((gap-nockGap)/.45f,0.f,1.f);
            if(drawing||in.time<shotAimUntil) {
                out.rayOrigin=drawing?in.hand[1].p:shotOrigin;
                out.rayDirection=drawing?(gap>.05f?math::Normalized(line):out.bowForward):shotDirection;
            }
            // Basic arrows use the direct native missile path. Selected arrow
            // skills use one brief native button press, retaining their volley,
            // effects, resource checks and native animation/cadence.
            out.attack=false;
            if(skillBow)out.skill=skillPulse.Get(bowSlot,bowSkill,bowOwner,in.skillContext,in.time);
            return out;
        }
        // Contact melee runs from the equipped weapon's posed transform. It
        // stays available with a trigger skill selected, but pauses during
        // casting. It never generates an attack pulse or needs a grip clutch.
        out.meleeContact=(in.weapon==Weapon::Melee||in.weapon==Weapon::Javelin)&&!trigger;
        out.shieldContact=in.shieldEquipped&&selection>=0&&in.selectedAvailable&&in.selectedSkill==weaponskills::Smite&&!trigger;
        out.attack=trigger&&selection<0; // explicit button fallback only
        if(trigger&&selection>=0)out.skill=selection;
        return out;
    }
};
}
