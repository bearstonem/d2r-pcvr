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
// Javelin: any thrown weapon - javelins, throwing knives and axes (#221).
enum class Weapon { Unknown, Melee, Bow, Crossbow, Other, Javelin };
// Input gating and posed contact recognition must agree: real wrist-led
// slashes exceed 35 rad/s. A stricter upstream limit continually cancels
// contact recovery before those otherwise valid swings can reach it.
// 60 still discarded the hardest swings - the riposte straight after a parry
// (2026-10-17); a real glitch is a blade jump the turn cannot explain, caught apart.
// 100, not more: at a 36 Hz frame rate it is still under half a turn per frame.
inline constexpr float MeleeAngularTrackingLimit=100.f;
inline constexpr float MeleeHandTrackingLimit=20.f; // m/s at the grip
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
    // Double Throw (#224): the left trigger arms the left hand's own throw.
    bool leftThrow=false; float leftTrigger=0;
    float throwLift=0; // degrees above the hand's path (VR Settings > Aim > Throw lift)
    // The right trigger's skill is a trap or bomb thrown to place (toss::Skill, #224),
    // or Attack or Throw with a throwing potion in the right hand (potion): tossed
    // the same way, and never swung.
    bool toss=false,potion=false;
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
    bool bowPhysical=false; // a release now is a physical arrow, not a native bow skill
    bool meleeContact=false,bowRelease=false,javelinRelease=false,javelinReady=false;
    bool nativeBowRelease=false,shieldContact=false;
    float meleeSpeed=0;
    int skill=-1, potion=-1, bottle=-1; // potion is a one-shot request, not proof of consumption
    bool swap=false;
    unsigned feedback=0; // 1 select/nock, 2 attack/release, 3 drink request, 4 string nearby, 5 draw ready, 6 melee ready
    Vec rayOrigin{}, rayDirection{}, nockPoint{}, bowForward{};
    float draw=0;
    // The left hand's throw (Double Throw): released this step, from where, which way.
    bool leftRelease=false;
    unsigned leftFeedback=0; // 1 armed, 2 released
    Vec leftOrigin{},leftDirection{};
    float leftDraw=0;
    // A trap or bomb thrown to place: in the hand while armed; released this
    // step from tossOrigin at tossVelocity (room metres, m/s).
    bool tossHeld=false,tossRelease=false;
    Vec tossOrigin{},tossVelocity{};
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
// Raises a direction by degrees (negative lowers it), keeping its heading;
// never past straight up or down. A throw's hand path dips as it is let go.
inline Vec Lift(Vec d,float degrees) {
    const float flat=std::sqrt(d.x*d.x+d.z*d.z);
    if(!(flat>1e-4f)||!std::isfinite(degrees)||degrees==0)return d;
    const float pitch=std::clamp(std::atan2(d.y,flat)+degrees*(3.14159265f/180),-1.55f,1.55f);
    const float scale=std::cos(pitch)/flat;
    return {d.x*scale,std::sin(pitch),d.z*scale};
}
// One hand's physical throw: armed by its held trigger, it leaves the hand on
// release after deliberate travel, with the hand's recent velocity.
struct HandThrow {
    bool armed=false;
    Vec start{},velocity{};
    double at=0,velocityAt=0;
    float travel=0;
    // The fastest recent sample (the hand is already slowing as it lets go),
    // for a throw whose distance is its speed: a tossed trap's arc.
    Vec peak{};double peakAt=0;
    void Arm(Vec hand,double time){*this={};armed=true;at=time;start=hand;}
    // One step; true when the release throws (direction and draw are set).
    bool Step(Vec hand,Vec delta,double dt,double time,bool released,Vec& direction,float& draw) {
        if(!armed)return false;
        travel=std::max(travel,Length(hand-start));
        const auto v=delta*(1/float(dt));
        // Store recent deliberate room-space motion, including the
        // release frame. A short grace period tolerates finger timing
        // without turning a stationary drop into a delayed throw.
        if(Length(v)>=1.4f&&Length(v)<=12.f){velocity=v;velocityAt=time;}
        if(Length(v)>=1.4f&&Length(v)<=30.f&&(time-peakAt>.12||Length(v)>=Length(peak))){peak=v;peakAt=time;}
        if(!released)return false;
        armed=false;
        if(time-at<.08||time-at>3||travel<.18f||!velocityAt||time-velocityAt>.08)return false;
        direction=math::Normalized(velocity);draw=std::clamp(Length(velocity)/5.f,0.f,1.f);return true;
    }
};
// A tossed trap: a press this short and still casts where you point; held this
// long and still, it casts natively as before (repeating).
inline constexpr double TossHold=.35;
inline constexpr float TossStill=.06f,TossMaxSpeed=20.f;

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
    HandThrow throwing,leftThrowing,tossing;
    bool tossNative=false; // a still hold: the native cast holds as before
    bool leftTriggerWas=false;
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
            leftTriggerWas=in.leftThrow&&in.leftTrigger>.55f;
            out.block=true;return out;
        }
        const Vec handDelta=in.hand[0].p-previousHand,leftDelta=in.hand[1].p-previousLeft;
        previousTime=in.time;previousHand=in.hand[0].p;previousRotation=in.hand[0].q;previousLeft=in.hand[1].p;
        const bool triggerEdge=trigger&&!triggerWas, gripEdge=grip&&!gripWas;
        const bool released=!trigger&&triggerWas;
        triggerWas=trigger;gripWas=grip;
        const bool leftTrigger=in.leftThrow&&in.leftTrigger>(leftTriggerWas?.20f:.55f);
        const bool leftEdge=leftTrigger&&!leftTriggerWas,leftReleased=!leftTrigger&&leftTriggerWas;
        leftTriggerWas=leftTrigger;
        if(!trigger)blockedTrigger=false;
        if(!grip)blockedGrip=false;
        out.palette=in.palette;
        out.rayOrigin=in.aim.p;out.rayDirection=math::Rotate(in.aim.q,{0,0,-1});out.rayValid=true;

        if(in.palette) {
            drawing=nativeBowTrigger=tossNative=false;throwing=leftThrowing=tossing={};skillPulse.Cancel();pulseUntil=0;blockedTrigger=true;
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
            blockedTrigger=true;drawing=nativeBowTrigger=tossNative=false;throwing=leftThrowing=tossing={};skillPulse.Cancel();pulseUntil=0;
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
            drawing=nativeBowTrigger=tossNative=false;throwing=leftThrowing=tossing={};skillPulse.Cancel();bowWasCharged=false;pulseUntil=shotAimUntil=0;blockedTrigger=true;
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

        // 2026-10-19: under hand bindings the right trigger always holds a slot.
        // Nothing, Attack or Throw there leaves it to the hand's own throw, and
        // so does a javelin skill or Double Throw, which the throw then
        // carries (#224); any other skill keeps the trigger (it was
        // unreachable with javelins).
        const bool handThrow=selection<0||(in.handBindings&&(in.selectedSkill<0||
            in.selectedSkill==weaponskills::Attack||in.selectedSkill==weaponskills::Throw||weaponskills::JavelinSkill(in.selectedSkill)||
            in.selectedSkill==weaponskills::DoubleThrow));
        if(in.weapon==Weapon::Javelin&&handThrow) {
            out.javelinReady=true;
            if(triggerEdge){throwing.Arm(in.hand[0].p,in.time);out.feedback=1;}
            if(throwing.Step(in.hand[0].p,handDelta,dt,in.time,released,out.rayDirection,out.draw)) {
                out.javelinRelease=true;out.feedback=2;out.rayOrigin=in.hand[0].p;out.rayDirection=Lift(out.rayDirection,in.throwLift);
            }
            // Double Throw: the left hand throws its own weapon the same way.
            if(in.leftThrow) {
                if(leftEdge){leftThrowing.Arm(in.hand[1].p,in.time);out.leftFeedback=1;}
                if(leftThrowing.Step(in.hand[1].p,leftDelta,dt,in.time,leftReleased,out.leftDirection,out.leftDraw)) {
                    out.leftRelease=true;out.leftFeedback=2;out.leftOrigin=in.hand[1].p;out.leftDirection=Lift(out.leftDirection,in.throwLift);
                }
            } else leftThrowing={};
            out.meleeContact=!trigger&&!out.javelinRelease;
            return out; // only the release spends ammunition; never hold A
        }

        // A trap or bomb thrown to place (#224), or a throwing potion. A quick
        // press casts where you point (a potion throws as it did before); held
        // still it casts natively as before, repeating; thrown, it goes where
        // its arc lands. The release makes one native press.
        if(in.toss&&selection>=0&&in.weapon!=Weapon::Bow&&in.weapon!=Weapon::Crossbow) {
            if(triggerEdge){tossing.Arm(in.hand[0].p,in.time);tossNative=false;skillPulse.Cancel();out.feedback=1;}
            const bool armed=tossing.armed;
            Vec direction{};float draw=0;
            if(tossing.Step(in.hand[0].p,handDelta,dt,in.time,released,direction,draw)) {
                out.tossRelease=true;out.feedback=2;out.tossOrigin=in.hand[0].p;
                out.tossVelocity=Lift(math::Normalized(tossing.peak),in.throwLift)*std::min(Length(tossing.peak),TossMaxSpeed);
                skillPulse.Start(selection,in.selectedSkill,in.selectedOwner,in.skillContext,in.time);
            } else if(armed&&released&&tossing.travel<TossStill) {
                skillPulse.Start(selection,in.selectedSkill,in.selectedOwner,in.skillContext,in.time);
            }
            if(trigger&&tossing.armed&&in.time-tossing.at>=TossHold&&tossing.travel<TossStill){tossNative=true;tossing={};}
            if(!trigger)tossNative=false;
            out.tossHeld=tossing.armed;
            if(tossNative)out.skill=selection;
            else if(const int pulsed=skillPulse.Get(selection,in.selectedSkill,in.selectedOwner,in.skillContext,in.time);pulsed>=0)out.skill=pulsed;
            out.meleeContact=!trigger&&!out.tossHeld&&!in.potion;   // the game never attacks with a throwing potion
            return out;
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
            out.bowDrawing=drawing;out.bowPhysical=drawing&&!skillBow;out.draw=std::clamp((gap-nockGap)/.45f,0.f,1.f);
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
