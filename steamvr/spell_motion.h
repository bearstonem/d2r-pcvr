#pragma once
#include "frame_math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <iterator>
#include <vector>

// Spells cast by their motion (owner, 2026-11-03). Every Sorceress spell with a motion, unless the
// wheel's MOTIONS page turned it off, casts by it while both grips are held (owner: "we are tracking
// motion all the time so it gets triggered in unwanted situations", "what about holding both grips to
// cast?"): no button or glyph needed. The transport runs one watcher per motion (free: watched while
// active, the grips' hold) and casts the first to complete as a glyph casts (glyph_cast.h), so mana,
// cast rate and the spell stay native. Held, the motion again casts again.
//
// The first prototype readied a glyph's spell on the right trigger instead (Input::held, a gate
// pressed fresh, with a drop logged on its release: the tests still use it); the MOTIONS page retired
// that ("I don't want this to get over complicated").
//
// The Paladin's and the Druid's gestures took the same gate and their own MOTIONS page (owner,
// 2026-11-04: "Both grips, like Sorceress") on detectors of their own until the owner, 2026-11-08
// ("Sorceress' motion gestures produce haptic feedback through vibrations but paladin motions do not ...
// We should migrate those older detectors"): now they are watchers on this engine too, their tuning
// unchanged (motions_paladin.h, motions_druid.h).
//
// The Amazon's, the Barbarian's, the Necromancer's and the Assassin's (owner, 2026-11-04: "also have
// multiple agents implement the motions", the table at claude.ai/artifact/MLr2xB4qEMTf8pzaTRKQ9T, codes
// A1-A4, B1-B8, N1-N8 with N6 Lower Resist, S1-S7) take the Sorceress's engine and gate: one watcher per
// motion, each class's detectors in its own header (motions_amazon.h, motions_barbarian.h,
// motions_necromancer.h, motions_assassin.h, motions_paladin.h, motions_druid.h, motions_warlock.h; ClassDone at the end of
// this file).
//
// Head-local frame (+X right, +Y up from the eyes, forward -Z, the head's yaw only); hand 0 is the right.
//
// Left-handed play (hand_role.h, #307; owner, 2026-11-09: "mirror everything ... they're going to just do the
// things they would have done with their right hand instead with their left hand"): Input::mirror. The
// transport then gives the LEFT hand as hand 0 and the right as hand 1, and the engine watches the mirror
// image of what it is given (Mirrored: the world reflected in the upright plane through the head along its
// heading), so every detector, every class's, sees a right-handed player doing the right-handed motion and
// needs no left-handed copy: "the right fist" in a detector or a cue is the main hand's, "the left shoulder"
// the off hand's side. What comes back that has a side (Output::aim, point, spot.right, end) is mirrored
// again (MirrorSides), so a cast goes where the left-handed player sent it. The cues' words are mirrored
// where they are shown (hand_text.h).
// Value-only: steamvr/spell_motion_test.cpp.
namespace framevr::spellmotion {
enum class Motion {None,FlingOut,PushDown,RubSnap,CrossGuard,ArmUp,MatchStrike,ForearmSweep,Shake,Hug,Punch,
    FireWall,Meteor,Blizzard,Hydra,                                                               // Sorceress (2026-11-05)
    InnerSight,SlowMissiles,Decoy,Valkyrie,                                                       // Amazon A1-A4
    BattleOrders,Shout,BattleCommand,WarCry,Howl,BattleCry,Leap,FindItem,LeapAttack,Whirlwind,    // Barbarian B1-B10
    BoneArmor,RaiseSkeleton,CorpseExplosion,Golem,AmplifyDamage,LowerResist,PoisonNova,BonePrison,GolemLeft,// Necromancer N1-N8, N4b
    BoneWall,PoisonExplosion,RaiseMage,                                                           // Necromancer N9-N11
    BurstOfSpeed,Fade,CloakOfShadows,Venom,BladeShield,ShadowWarrior,MindBlast,                   // Assassin S1-S7
    Charge,BlessedHammer,HolyShield,FistOfHeavens,                                                // Paladin (2026-11-08)
    Werewolf,Werebear,                                                                            // Druid (2026-11-08)
    SummonGoatman,SummonTainted,SummonDefiler,PsychicWard,RingOfFire,EldritchBlast,               // Warlock (2026-11-10)
    HexBane,HexPurge,HexSiphon,SigilLethargy,SigilRancor,SigilDeath,
    End};
constexpr int SorceressCount=int(Motion::Hydra);   // the Sorceress's, 1..SorceressCount ([motions] off: her bits)
constexpr int MotionCount=int(Motion::End)-1;      // every class's, 1..MotionCount (a watcher each)
constexpr int MaxCards=14;                         // a MOTIONS page's cards (interaction::MotionCards), one bit each
inline Motion For(int skill) {
    switch(skill) {
    case 48:return Motion::FlingOut;      // Nova
    case 44:return Motion::PushDown;      // Frost Nova
    case 42:return Motion::RubSnap;       // Static Field
    case 58:return Motion::CrossGuard;    // Energy Shield
    case 57:return Motion::ArmUp;         // Thunder Storm
    case 46:return Motion::MatchStrike;   // Blaze
    case 40:return Motion::ForearmSweep;  // Frozen Armor
    case 50:return Motion::Shake;         // Shiver Armor
    case 60:return Motion::Hug;           // Chilling Armor
    case 54:return Motion::Punch;         // Teleport (owner: "as a motion similar to Paladin's Charge")
    case 51:return Motion::FireWall;case 56:return Motion::Meteor;case 59:return Motion::Blizzard;case 62:return Motion::Hydra;
    case 8:return Motion::InnerSight;case 17:return Motion::SlowMissiles;case 28:return Motion::Decoy;case 32:return Motion::Valkyrie;
    case 149:return Motion::BattleOrders;case 138:return Motion::Shout;case 155:return Motion::BattleCommand;case 154:return Motion::WarCry;
    case 130:return Motion::Howl;case 146:return Motion::BattleCry;case 132:return Motion::Leap;case 142:return Motion::FindItem;
    case 143:return Motion::LeapAttack;case 151:return Motion::Whirlwind;
    case 68:return Motion::BoneArmor;case 70:return Motion::RaiseSkeleton;case 74:return Motion::CorpseExplosion;
    case 75:case 94:return Motion::Golem;   // the right arm's card is Clay Golem's; it casts the strongest learned (CastSkillOf)
    case 85:return Motion::GolemLeft;       // the left arm's is Blood Golem's; it casts the next strongest
    case 66:return Motion::AmplifyDamage;case 91:return Motion::LowerResist;case 92:return Motion::PoisonNova;case 88:return Motion::BonePrison;
    case 78:return Motion::BoneWall;case 83:return Motion::PoisonExplosion;case 80:return Motion::RaiseMage;
    case 258:return Motion::BurstOfSpeed;case 267:return Motion::Fade;case 264:return Motion::CloakOfShadows;case 278:return Motion::Venom;
    case 277:return Motion::BladeShield;case 268:case 279:return Motion::ShadowWarrior;case 273:return Motion::MindBlast;
    case 107:return Motion::Charge;case 112:return Motion::BlessedHammer;case 117:return Motion::HolyShield;case 121:return Motion::FistOfHeavens;
    case 223:return Motion::Werewolf;case 228:return Motion::Werebear;
    case 373:return Motion::SummonGoatman;case 376:return Motion::SummonTainted;case 377:return Motion::SummonDefiler;
    case 387:return Motion::PsychicWard;case 394:return Motion::RingOfFire;case 384:return Motion::EldritchBlast;
    case 385:return Motion::HexBane;case 389:return Motion::HexPurge;case 386:return Motion::HexSiphon;
    case 393:return Motion::SigilLethargy;case 396:return Motion::SigilRancor;case 400:return Motion::SigilDeath;
    default:return Motion::None;
    }
}
// The MOTIONS page (the wheel; owner 2026-11-03: "a place where you can see the motion spells that are
// active"): the motion spells in the order it shows them (skill ids, the trees' order), each with its
// bit in [motions] off (the motion's number - 1), set to turn it off.
constexpr int PageSkills[SorceressCount]={40,42,44,46,48,50,51,54,56,57,58,59,60,62};   // Fire Wall, Meteor, Blizzard, Hydra since 2026-11-05
inline int Bit(int skill){const int m=int(For(skill));return m>0&&m<=SorceressCount?m-1:-1;}
inline int OffMask(float value){return std::isfinite(value)?std::clamp(int(std::lround(value)),0,(1<<MaxCards)-1):0;}
inline bool On(int skill,int off){const int b=Bit(skill);return b>=0&&!((off>>b)&1);}
// Every class's MOTIONS page (owner, 2026-11-04: "Both grips, like Sorceress" for the Paladin's and the
// Druid's gestures as well): its skills in card order, each turned off by a bit of the class's own
// setting. The Sorceress's bit is her motion's number - 1 ([motions] off, as saved since 2026-11-03);
// another class's is its card's. skillClass as the game's: 1 Sorceress, 3 Paladin, 5 Druid, 7 Warlock.
constexpr int PaladinSkills[]={107,112,117,121};   // Charge, Blessed Hammer, Holy Shield, Fist of the Heavens
constexpr int DruidSkills[]={223,228};             // Werewolf, Werebear
// The table's order (owner, 2026-11-04): A1-A4, B1-B8, N1-N8 (N4 the golems' card, N6 Lower Resist), S1-S7.
constexpr int AmazonSkills[]={8,17,28,32};
// 143: Leap Attack, after the table's (owner, 2026-11-06: Leap's swing from the arms pulled further back); 151:
// Whirlwind (owner, 2026-11-06: "get on T-pose ... Which then triggers whirlwind").
constexpr int BarbarianSkills[]={149,138,155,154,130,146,132,142,143,151};
// 85: the left arm's golem, then Bone Wall, Poison Explosion and Raise Skeletal Mage (owner, 2026-11-05), after the table's
constexpr int NecromancerSkills[]={68,70,74,75,66,91,92,88,85,78,83,80};
constexpr int AssassinSkills[]={258,267,264,278,277,268,273};
// The Warlock's (owner, 2026-11-10; class 7, D2R's own): what goes on a spot first - the demons, then the sigils -
// then what has no target: the hexes, the ward, the blast and the ring (motions_warlock.h).
constexpr int WarlockSkills[]={373,376,377,393,396,400,385,389,386,387,384,394};
struct Page {const int* skills=nullptr;int count=0;};
inline Page PageOf(int skillClass) {
    switch(skillClass) {
    case 0:return {AmazonSkills,int(std::size(AmazonSkills))};
    case 1:return {PageSkills,SorceressCount};
    case 2:return {NecromancerSkills,int(std::size(NecromancerSkills))};
    case 3:return {PaladinSkills,int(std::size(PaladinSkills))};
    case 4:return {BarbarianSkills,int(std::size(BarbarianSkills))};
    case 5:return {DruidSkills,int(std::size(DruidSkills))};
    case 6:return {AssassinSkills,int(std::size(AssassinSkills))};
    case 7:return {WarlockSkills,int(std::size(WarlockSkills))};
    default:return {};
    }
}
inline bool HasMotions(int skillClass){return PageOf(skillClass).count>0;}
inline int Bit(int skillClass,int skill) {
    if(skillClass==1)return Bit(skill);
    const auto page=PageOf(skillClass);
    for(int i=0;i<page.count;++i)if(page.skills[i]==skill)return i;
    return -1;
}
inline bool On(int skillClass,int skill,int off){const int b=Bit(skillClass,skill);return b>=0&&!((off>>b)&1);}
// Its name, for a hero without the skill (the catalog has none then).
inline const char* SpellName(int skill) {
    switch(skill) {
    case 40:return "Frozen Armor";case 42:return "Static Field";case 44:return "Frost Nova";case 46:return "Blaze";
    case 48:return "Nova";case 50:return "Shiver Armor";case 54:return "Teleport";case 57:return "Thunder Storm";
    case 58:return "Energy Shield";case 60:return "Chilling Armor";case 51:return "Fire Wall";case 56:return "Meteor";
    case 59:return "Blizzard";case 62:return "Hydra";
    case 107:return "Charge";case 112:return "Blessed Hammer";case 117:return "Holy Shield";case 121:return "Fist of the Heavens";
    case 223:return "Werewolf";case 228:return "Werebear";
    case 8:return "Inner Sight";case 17:return "Slow Missiles";case 28:return "Decoy";case 32:return "Valkyrie";
    case 149:return "Battle Orders";case 138:return "Shout";case 155:return "Battle Command";case 154:return "War Cry";
    case 130:return "Howl";case 146:return "Battle Cry";case 132:return "Leap";case 142:return "Find Item";case 143:return "Leap Attack";case 151:return "Whirlwind";
    case 68:return "Bone Armor";case 70:return "Raise Skeleton";case 74:return "Corpse Explosion";case 75:return "Clay Golem";
    case 85:return "Blood Golem";case 94:return "Fire Golem";case 66:return "Amplify Damage";case 91:return "Lower Resist";
    case 92:return "Poison Nova";case 88:return "Bone Prison";case 78:return "Bone Wall";case 83:return "Poison Explosion";
    case 80:return "Raise Skeletal Mage";
    case 258:return "Burst of Speed";case 267:return "Fade";case 264:return "Cloak of Shadows";case 278:return "Venom";
    case 277:return "Blade Shield";case 268:return "Shadow Warrior";case 279:return "Shadow Master";case 273:return "Mind Blast";
    case 373:return "Summon Goatman";case 376:return "Summon Tainted";case 377:return "Summon Defiler";case 387:return "Psychic Ward";
    case 394:return "Ring of Fire";case 384:return "Eldritch Blast";case 385:return "Hex: Bane";case 389:return "Hex: Purge";
    case 386:return "Hex: Siphon";case 393:return "Sigil: Lethargy";case 396:return "Sigil: Rancor";case 400:return "Sigil: Death";
    default:return "";
    }
}
// A card that stands for a family (the table's N4 and S6): the MOTIONS page names it, and shows its icon, by
// the skill it casts now (CastSkillOf), the golems with their arm. Both golems' cards read "Golem (... arm)"
// until the owner, 2026-11-06: "clearly label the different kinds of golems ... One is a blood golem, the
// other one is a fire golem".
inline bool Family(int skill){return skill==75||skill==85||skill==268;}
inline const char* CardArm(int skill){return skill==75?" (right arm)":skill==85?" (left arm)":"";}
// The skill a card casts: the strongest of its family learned (known(id): the hero has the skill). The golems
// Fire, then Blood, then Clay (Iron Golem takes an item, so it stays on its button); Shadow Master once learned.
template<class Known> int CastSkillOf(int skill,Known known) {
    switch(skill) {
    case 75:case 85: {   // the right arm's: the strongest golem learned; the left arm's: the next one down (else the strongest)
        int learned[3],n=0;
        for(const int id:{94,85,75})if(known(id))learned[n++]=id;
        if(!n)return skill;
        return skill==85&&n>1?learned[1]:learned[0];
    }
    case 268:return known(279)?279:268;
    default:return skill;
    }
}
// The other classes' cues, and the Sorceress's last four (their headers; defined at the end of this file).
inline const char* ClassCue(Motion m);
// How to do it: on the MOTIONS page and in the log.
inline const char* Cue(Motion m) {
    switch(m) {
    case Motion::FlingOut:return "bring the hands together, then fling both arms out wide";
    case Motion::PushDown:return "push both hands down hard in front of you, to the waist";
    case Motion::RubSnap:return "rub the palms together, then snap them apart";
    case Motion::CrossGuard:return "both hands up at the face, close together (crossed or not), and hold";
    case Motion::ArmUp:return "raise one arm straight up and hold";
    case Motion::MatchStrike:return "strike one hand down the thigh, hip to knee";
    case Motion::ForearmSweep:return "sweep the left hand from the right hand up to the right shoulder";
    case Motion::Shake:return "shake both fists hard, held well apart (not rubbed together)";
    case Motion::Hug:return "hug yourself, each hand on the other shoulder";
    case Motion::Punch:return "right arm out, aim the controller's arc where you want to land, then yank the fist back";
    default:return m>Motion::Punch&&m<Motion::End?ClassCue(m):"no motion";
    }
}
// A page skill's cue: its motion's (every class's, the Paladin's and the Druid's too since 2026-11-08).
inline const char* CueOf(int skill){return Cue(For(skill));}
// The grips' hold every class's motions take (owner, 2026-11-03: "what about holding both grips to
// cast?"): both held above .6, let go below .35, so a squeeze that dips mid-motion keeps it.
struct GripGate {
    bool down[2]{};
    void Reset(){*this=GripGate{};}
    bool Step(const float grip[2]) {
        for(int h=0;h<2;++h)down[h]=std::isfinite(grip[h])&&grip[h]>(down[h]?.35f:.6f);
        return Held();
    }
    bool Held()const{return down[0]&&down[1];}
};
// The gate every class's motions take: the grips' hold with the hands free for them (hands: the
// transport's - a motion of the class learned and on, Weapon skill gestures, both triggers free, no
// bottle, holster, bow, wheel, menu or glyph stroke). open: the motions are watched; watch: open, or
// the grips open this frame after a hold (a gesture may end as they open: Fist of the Heavens' fist).
struct Gate {
    GripGate grips;bool was=false;
    struct State{bool held=false,open=false,watch=false;};
    void Reset(){*this=Gate{};}
    State Step(const float grip[2],bool hands) {
        State s;s.held=grips.Step(grip);s.open=s.held&&hands;s.watch=hands&&(s.held||was);
        was=s.held;return s;
    }
};
// While both grips cast spells (owner, 2026-11-03: "when both grips are held you should prevent
// actions like grabbing potions or grabbing above the shoulder"): a grip counts for a grab only
// alone - held SoloSeconds with the other free - and then until it is let go; pressed with the
// other, or while the other is held, never until it is let go. Held above .6, let go below .35, so a
// squeeze that dips mid-motion is not a new press (the grabs take a grip's press: a dip and back at
// the belt or a shoulder took a potion or swapped weapons).
struct SoloGrips {
    static constexpr double SoloSeconds=.1;
    bool held[2]{},solo[2]{};
    double since[2]{-1,-1};   // when pressed alone; -2 pressed with the other (never, until let go)
    void Reset(){*this=SoloGrips{};}
    void Step(double now,const float grip[2]) {
        for(int h=0;h<2;++h)held[h]=std::isfinite(grip[h])&&grip[h]>(held[h]?.35f:.6f);
        for(int h=0;h<2;++h) {
            if(!held[h]){solo[h]=false;since[h]=-1;continue;}
            if(solo[h])continue;
            if(held[1-h]){since[h]=-2;continue;}
            if(since[h]==-1)since[h]=now;
            if(since[h]>=0&&now-since[h]>=SoloSeconds)solo[h]=true;
        }
    }
};
struct Tuning {
    // Tuned on the owner's first session (2026-11-03): Nova flings that reached 78 and 89 cm, Frost
    // Nova pushes begun at the chest and ending at the sides, and an Energy Shield held 12-22 cm apart
    // at the face that never cast.
    float together=.30f;      // m: Nova starts with the hands this close...
    float flingApart=.75f;    // ...and ends with them this far apart,
    float flingSide=.25f;     // each this far out on its own side,
    float flingWindow=.8f;    // s after they were together
    float pushDrop=.25f;      // m: Frost Nova: both hands this much lower than
    float pushWindow=.5f;     // s ago (a push, not lowering the arms),
    float pushBelow=-.40f;    // m: ending this far below the eyes, not behind the back,
    float pushSpeed=1.4f;     // m/s: each hand at least this fast on the way (pushed hard, not lowered)
    // The owner with a two-handed staff (2026-11-06: "everything feels way too sensitive for the motions. I'm
    // accidentally triggering everything"): handling the staff with both grips held lowered it as a push and
    // raised it as a guard. Frost Nova then took 20 cm at any speed and Energy Shield a 35 cm guard held
    // 0.35 s; the owner's own pushes (2026-11-03) fell 26 cm in 0.3 s, about 1.6 m/s.
    float rubClose=.18f;      // m: Static Field: controllers this close are palms together;
    float rubPath=.15f;       // m of their rubbing charges it (leaking rubLeak m a second: the owner's two hands
    float rubLeak=.12f;       // held together to point charged it by their wobble and a yank cast it, 2026-11-05),
    float snapApart=.45f;     // m: then the hands this far apart
    float snapWindow=.5f;     // s after they parted
    float guardApart=.30f;    // m: Energy Shield: both hands up at the face, this close (crossed or not),
    float guardHold=.50f;     // s held (a staff's two hands sit wider; a guard is held, not passed)
    float holdSeconds=.35f;   // a held pose (Energy Shield, Thunder Storm, Chilling Armor)
    float stillSpeed=.8f;     // m/s: a held pose moves slower than this
    float raiseAbove=.22f;    // m above the eyes: Thunder Storm's raised hand
    float strikeDrop=.22f;    // m: Blaze: one hand this much lower than
    float strikeWindow=.25f;  // s ago (a strike),
    float strikeFrom=-.40f;   // m: starting below this (the hip, not the chest)
    float sweepAt=.20f;       // m: Frozen Armor: the left hand this near the right hand,
    float sweepTo=.22f;       // m: then this near the right shoulder,
    float sweepWindow=1.2f;   // s later
    float shakeSpeed=.35f;    // m/s: Shiver Armor: a swing faster than this, then back,
    int shakeTurns=3;         // this many times with each hand
    float shakeWindow=.9f;    // s
    // m: and the fists at least this far apart throughout: Static Field's rub has them together (a tester,
    // 2026-11-05: "This was getting confused with static field ... Rubbing reads as shaking")
    float shakeApart=.25f;
    // Teleport by point and pull (owner, 2026-11-05, in the headset: "I do definitely like the targeting aspect
    // of it, but I want to be able to use that targeting better ... With our arm kind of tucked into us. It's hard
    // to aim where you want to go"). Until then a fast pull of the fist showed the circle and a punch landed there;
    // the circle also came up as the arm came back after a teleport and at the start of Frost Nova and Blaze.
    // Now the right arm held out ahead (the fist pointOut ahead of the eyes or more, pointArm from the shoulder or
    // more, not pointing up past pointUp of its length) and steady (slower than pointSteady) for pointHold shows
    // where it lands (Output::aiming, Output::point: the shoulder-to-fist line, head-local): along the arm's
    // heading, pointFar cells with the arm level, nearer as it points down (evenly by its angle), pointNear at pointSteepDeg below
    // level (PointSpot). It follows the arm while the arm stays out and steady. The fist yanked back toward the
    // shoulder, yankBack or more within yankWindow, mostly back (rising, falling or drifting at most 0.7 as far), from where it pointed, casts there
    // (Output::landing). The arm drawn in or let down without a yank for aimLost, or aimSeconds, gives it up.
    float pointOut=.30f,pointArm=.38f,pointUp=.30f,pointSteady=.60f,pointHold=.15f;
    // The right arm alone (owner, 2026-11-05, in the headset: "it should only be showing that circle if one arm
    // is raised. When we raise both arms, the circle shouldn't be drawn for teleport"): the left hand out in
    // front (pointOtherAhead or more) at the chest or higher (over pointOtherLow), or up past pointOtherHigh
    // anywhere, shows none and takes one shown away at once (the yank then casts nothing).
    float pointOtherAhead=.25f,pointOtherLow=-.45f,pointOtherHigh=-.15f;
    float pointFar=32.f,pointNear=6.f,pointSteepDeg=50.f;
    float yankBack=.18f,yankWindow=.25f,aimLost=.6f,aimSeconds=8.f;
    float cooldown=.6f;       // s after a cast
    float history=1.5f;       // s of the hands kept
};
// A spot on the floor, in cells ahead of the eyes' heading and to its right: Teleport's landing for an
// arm pointing (Output::point, PointSpot), or a summon's (Output::spot).
struct Spot {float ahead=0,right=0;};
// Where a cast goes besides its way: nowhere of its own (the way, else the right hand's ray), a Spot
// (Decoy, Golem), or where the right hand's pointer meets the floor (Valkyrie: "it didn't spawn her where
// I pointed", 2026-11-05).
enum class Land {None,Spot,Pointed};
constexpr float Begun=.25f;   // Output::progress once a motion's first part is done
// The body every motion's distances were tuned on (the owner's, 2026-11-03..05: VR Settings' controller span
// 1.447-1.469 m between the grips in a T-pose, eye height 1.66 m). Another player's hands are measured in
// this body's units (Input::bodyScale), so a reach, a push or a hand "past the eyes" asks the same of every
// body; times are unchanged. The owner, 2026-11-05: calibration to make the motions "more accurate".
constexpr float ReferenceSpan=1.46f;
constexpr float MinBodyScale=.8f,MaxBodyScale=1.2f;   // a mistaken calibration cannot make the motions absurd
// The calibrated controller span as a body scale (1: none calibrated).
inline float BodyScale(float span){return std::isfinite(span)&&span>0?std::clamp(span/ReferenceSpan,MinBodyScale,MaxBodyScale):1.f;}
struct Input {
    double time=0;
    uint64_t session=0,epoch=0;
    bool active=false;            // a spell with a motion readied, the hands free for it (no wheel, menu, bottle)
    bool held=false;              // the right trigger
    bool free=false;              // no trigger: watched whenever active, and never dropped (a spell on a slot)
    Motion motion=Motion::None;
    math::Vec head{};math::Quat headRotation{0,0,0,1};
    math::Vec hand[2]{};          // world frame; 0 right, 1 left
    bool tracked[2]{};
    // Left-handed: hand 0 is the LEFT hand and hand 1 the right (grip, tracked, handVelocity with them), and
    // the motion watched is the mirror image (Mirrored). Output's per-hand figures keep that order: 0 the
    // main (left) hand.
    bool mirror=false;
    float bodyScale=1;            // the player's size against the reference body (BodyScale): hands are divided by it
    // For the Paladin's and the Druid's detectors (motions_paladin.h, motions_druid.h), as their own engines took
    // them: the grips as pulled (a fist), each hand's velocity from the runtime (world, m/s) when it gives one,
    // and the calibrated controller span (m; 0 none).
    float grip[2]{1,1};
    math::Vec handVelocity[2]{};bool velocityKnown[2]{};
    float armSpan=0;
};
struct Output {
    bool began=false;             // the trigger pressed: watching
    bool cast=false;              // the motion completed
    const char* dropped=nullptr;  // let go, or stopped, without it (for the log)
    float seconds=0;              // held so far
    // Over the hold, for tuning: the hands least and most apart, the highest and lowest hand from the
    // eyes, the fastest hand, each hand's biggest fall within pushWindow, and where the hands were last
    // (head-local: x right, y up, z back) (m, m/s).
    float apartMin=0,apartMax=0,top=0,low=0,fastest=0,fall[2]{};
    float behind[2]{};            // the farthest each hand went behind the eyes (m; Leap's and Leap Attack's wind-up)
    math::Vec end[2]{};
    // With a cast that goes somewhere (Teleport): the way the motion went, head-local as end (Teleport's: the
    // arm it pointed with); zero for the rest.
    math::Vec aim{};
    // Teleport pointed at (Tuning::pointOut...): its landing shown, where PointSpot puts it for point (the
    // shoulder-to-fist line, head-local). With cast, landing: the yank came while it showed, so it lands there.
    bool aiming=false,landing=false;
    math::Vec point{};
    // Teleport's circle shown this hold, for the log when it is given up (Stats): the longest pull back within
    // yankWindow from where the arm pointed.
    bool pointAimed=false;float yankBest=0;
    // How near the watched motion is to casting, for the controllers' warmer/colder buzz (a tester,
    // 2026-11-05: "continuous vibration as you get closer to the 2nd part of the sequence"): 0 nothing
    // begun, Begun once its first part is done (the hands together, a fist at the mouth, a pose taken),
    // rising to 1 as the second nears its end. Zero on the cast's frame and while cooling down.
    float progress=0;
    // With cast: a summon that goes to a spot of its own (Engine::LandAt, LandPointed), not along aim.
    Land land=Land::None;Spot spot{};
    // The Paladin's and the Druid's own log lines (DropLine): a gesture given up partway this frame (why, nullptr
    // none; its hand, 0 right, 1 left, -1 the motion's; its class's figures), and with cast its figures (Fist of
    // the Heavens: from and to, degrees). Engine::Abandon, CastFigures.
    const char* abandoned=nullptr;int abandonedHand=-1;float abandonedFigures[3]{};
    float castFigures[2]{};
};
// A spot for an arm pointing along way (head-local), in cells ahead of the eyes' heading and to its right:
// farCells with the arm level (or up), easing to nearCells at steepDeg below.
inline Spot SpotAlong(math::Vec way,float farCells,float nearCells,float steepDeg) {
    const float level=std::hypot(way.x,way.z);
    if(!(level>1e-4f)||!std::isfinite(way.y))return {nearCells,0};
    const float below=std::atan2(-way.y,level)*57.2957795f;
    const float f=std::clamp(below/steepDeg,0.f,1.f),cells=farCells+(nearCells-farCells)*f;
    return {cells*(-way.z/level),cells*(way.x/level)};
}
// Teleport's landing (Output::point): pointFar level, pointNear at pointSteepDeg below. Meteor's: its own.
inline Spot PointSpot(math::Vec way,const Tuning& t){return SpotAlong(way,t.pointFar,t.pointNear,t.pointSteepDeg);}
// Left-handed play (Input::mirror): the input a right-handed player's mirror image would give. Everything
// with a place or a way is reflected in the upright plane through the head that holds its heading - the hands
// and their velocities, and the head's own turn (its heading and pitch stay, its roll reverses) - so the
// head-local frame is the same one with left and right exchanged. The hands' order is the caller's (hand 0
// the main hand).
inline Input Mirrored(Input in) {
    const auto yaw=math::Yaw(math::Heading(in.headRotation)),inverse=math::Conjugate(yaw);
    const auto reflect=[&](math::Vec v){auto l=math::Rotate(inverse,v);l.x=-l.x;return math::Rotate(yaw,l);};
    for(int h=0;h<2;++h){in.hand[h]=in.head+reflect(in.hand[h]-in.head);in.handVelocity[h]=reflect(in.handVelocity[h]);}
    // a turn by an angle about an axis, reflected: about the reflected axis, by the angle reversed
    const auto level=math::Multiply(inverse,in.headRotation);
    in.headRotation=math::Multiply(yaw,math::Quat{level.x,-level.y,-level.z,level.w});
    in.mirror=false;
    return in;
}
// And what an Output says with a side, turned back for the player (head-local: x reversed; a spot's right).
inline void MirrorSides(Output& out) {
    out.aim.x=-out.aim.x;out.point.x=-out.point.x;out.spot.right=-out.spot.right;
    for(auto& e:out.end)e.x=-e.x;
}
// The hands at one frame, head-local: l[0] the right, l[1] the left; w as given (world frame).
struct Sample {double t=0;math::Vec l[2]{},w[2]{};};
class Engine;
// The other classes' detectors, and the Sorceress's last four (defined at the end of this file).
inline bool ClassDone(Engine& e,Motion m);
// A motion's own rest after it casts (s; below zero: Tuning::cooldown), and its detector told the watch stopped
// (a hand busy, the grips let go, a frame gap) before its history goes (the Paladin's and the Druid's log what
// they had begun: Engine::Abandon). Defined at the end of this file.
inline float ClassRest(Motion m);
inline void ClassStopped(Engine& e,Motion m,const char* why);
class Engine {
    std::vector<Sample> samples;
    uint64_t session=0,epoch=0;
    bool watching=false,wasHeld=false;
    int casts=0;                  // in this hold
    double since=0,last=0,castAt=-1,poseSince=-1;
    float rub=0;double parted=-1; // Static Field
    math::Vec aimed{};            // Teleport: the arm Done found pointing
    // Teleport pointed at: since when (-1: not), the arm out and steady since, the arm in or down since (-1: not),
    // where it last pointed; landed: the yank came while it showed.
    double aimingSince=-1,steadySince=-1,outLost=-1;bool landed=false;math::Vec pointWay{};
    float progress=0;             // this frame's (Output::progress), the most any part of Done reported
    Land land=Land::None;Spot spot{};   // a summon's spot (LandAt, LandPointed)
    struct Swing {int sign=0;float peak=0;};
    Swing swing[2][3]{};          // Shiver Armor: each hand's last swing on each axis
    std::vector<double> turns[2][3];
    Output stats;
    Input current;float body=1;       // this frame's input, and the body scale its hands were divided by
    Motion watched=Motion::None;      // the motion this hold watches
    struct Abandoned {const char* why=nullptr;int hand=-1;float figures[3]{};} abandon;   // this frame's (Abandon)
    float castFigures[2]{};bool keep=false;   // this frame's (CastFigures, KeepAfterCast)
    static bool Finite(math::Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
    void Clear() {
        samples.clear();poseSince=-1;rub=0;parted=-1;aimed={};scratch={};land=Land::None;spot={};
        aimingSince=steadySince=outLost=-1;landed=false;pointWay={};
        for(auto& h:swing)for(auto& a:h)a={};
        for(auto& h:turns)for(auto& a:h)a.clear();
    }
    void Stop(){watching=false;casts=0;Clear();}
    void Report(Output& out)const {
        out.abandoned=abandon.why;out.abandonedHand=abandon.hand;
        for(int i=0;i<3;++i)out.abandonedFigures[i]=abandon.figures[i];
    }
public:
    // For a class's detector (motions_<class>.h, through ClassDone): the hands' history (head-local, the
    // newest last, history seconds of it), these helpers, a scratch store cleared with the hold and after
    // each cast, and Aim for a cast that goes somewhere (a spot: Output::aim, head-local).
    const std::vector<Sample>& History()const{return samples;}
    std::array<double,8> scratch{};
    void Aim(math::Vec way){aimed=way;}
    // A summon's spot: cells ahead of the eyes' heading and to its right, or where the right hand points.
    void LandAt(float ahead,float right){land=Land::Spot;spot={ahead,right};}
    void LandPointed(){land=Land::Pointed;spot={};}
    // For the Paladin's and the Druid's detectors: this frame's input as given (world head and hands, the grips,
    // the runtime's velocities, the arm span), and the body scale the history's hands were divided by (a hand's
    // metres: l * Body()).
    const Input& Current()const{return current;}
    float Body()const{return body;}
    // A gesture given up partway, for its log line (Output::abandoned; the first in a frame is kept), and a cast's
    // figures (Output::castFigures).
    void Abandon(const char* why,int hand,float a=0,float b=0,float c=0){if(!abandon.why)abandon={why,hand,{a,b,c}};}
    void CastFigures(float a,float b){castFigures[0]=a;castFigures[1]=b;}
    // The cast this frame leaves the history and scratch as they are (Blessed Hammer: the other fist's pump goes on).
    void KeepAfterCast(){keep=true;}
    // The motion's first part is done and its second this fraction of the way (0..1) to its end
    // (Output::progress: Begun to 1). Several reports in a frame keep the most.
    void Toward(float fraction) {
        if(!std::isfinite(fraction))return;
        progress=std::max(progress,Begun+(1-Begun)*std::clamp(fraction,0.f,1.f));
    }
    static float Len(math::Vec v){return std::sqrt(math::Dot(v,v));}
    static float Apart(const Sample& s){return Len(s.l[0]-s.l[1]);}
    static float Forward(math::Vec l){return -l.z;}
    // The hand's velocity over the last few samples (m/s), zero until there are some.
    math::Vec Velocity(int h)const {
        if(samples.size()<3)return {};
        const auto& a=samples[samples.size()-3];const auto& b=samples.back();
        const double dt=b.t-a.t;
        return dt>1e-4?(b.l[h]-a.l[h])*float(1/dt):math::Vec{};
    }
    bool Still(int h)const{return Len(Velocity(h))<tuning.stillSpeed;}
    // The fastest hand h moved (m/s) from sample i to the newest.
    float PeakSpeed(size_t i,int h)const {
        float most=0;
        for(size_t j=i+1;j<samples.size();++j) {
            const double dt=samples[j].t-samples[j-1].t;
            if(dt>1e-4)most=std::max(most,Len(samples[j].l[h]-samples[j-1].l[h])/float(dt));
        }
        return most;
    }
    // Chilling Armor's hug: each hand on the other side, at the shoulders or the chest, close to the body (not up
    // in front: Energy Shield). Up to 12 cm under the eyes (the owner's hugs, 2026-11-05, held a hand 17 cm under
    // them, where 18 was the top; a guard 8 cm under them, crossed close to the face, stays Energy Shield).
    static bool HugPose(const Sample& n) {
        const auto& r=n.l[0];const auto& l=n.l[1];
        return r.x>=-.40f&&r.x<=-.02f&&l.x<=.40f&&l.x>=.02f&&r.y>=-.60f&&r.y<=-.12f&&l.y>=-.60f&&l.y<=-.12f&&
            Forward(r)>=-.25f&&Forward(r)<=.15f&&Forward(l)>=-.25f&&Forward(l)<=.15f;
    }
    // A pose held: true once it has been kept (and kept still) for seconds (holdSeconds when not given).
    bool Held(bool pose,double now,double seconds=-1) {
        if(!pose){poseSince=-1;return false;}
        if(poseSince<0)poseSince=now;
        const double need=seconds>=0?seconds:tuning.holdSeconds;
        Toward(need>0?float((now-poseSince)/need):1.f);   // the pose taken: warmer as it is held
        return now-poseSince>=need;
    }
private:
    bool Done(Motion m) {
        const auto& n=samples.back();
        const auto& t=tuning;
        switch(m) {
        case Motion::FlingOut: {
            bool together=false;
            for(const auto& s:samples)
                if(n.t-s.t<=t.flingWindow&&Apart(s)<=t.together&&Forward(s.l[0])>-.1f&&Forward(s.l[1])>-.1f){together=true;break;}
            if(!together)return false;
            Toward((Apart(n)-t.together)/(t.flingApart-t.together));
            return Apart(n)>=t.flingApart&&n.l[0].x>=t.flingSide&&n.l[1].x<=-t.flingSide;
        }
        case Motion::PushDown: {
            float fall[2]{};   // each hand's biggest fall within the window, for the buzz
            for(const auto& s:samples)if(n.t-s.t<=t.pushWindow)for(int h=0;h<2;++h)fall[h]=std::max(fall[h],s.l[h].y-n.l[h].y);
            if(std::min(fall[0],fall[1])>=.06f)Toward(std::min(fall[0],fall[1])/t.pushDrop);
            if(n.l[0].y>t.pushBelow||n.l[1].y>t.pushBelow||Forward(n.l[0])<-.25f||Forward(n.l[1])<-.25f)return false;
            for(size_t i=0;i<samples.size();++i) {
                const auto& s=samples[i];
                if(n.t-s.t<=t.pushWindow&&s.l[0].y-n.l[0].y>=t.pushDrop&&s.l[1].y-n.l[1].y>=t.pushDrop&&
                   PeakSpeed(i,0)>=t.pushSpeed&&PeakSpeed(i,1)>=t.pushSpeed)return true;
            }
            return false;
        }
        case Motion::RubSnap: {
            const float apart=Apart(n);
            if(samples.size()>=2) {
                const auto& p=samples[samples.size()-2];
                if(apart<=t.rubClose&&Apart(p)<=t.rubClose)rub+=Len((n.l[0]-n.l[1])-(p.l[0]-p.l[1]));
                rub=std::max(0.f,rub-t.rubLeak*float(n.t-p.t));
            }
            if(apart<=t.rubClose)parted=-1;
            else if(parted<0&&rub>0)parted=n.t;
            if(parted>=0&&n.t-parted>t.snapWindow){rub=0;parted=-1;}   // apart too long: the charge is lost
            if(rub>=.03f)Toward(.7f*std::min(rub/t.rubPath,1.f)+(parted>=0?.3f*(apart-t.rubClose)/(t.snapApart-t.rubClose):0.f));
            return rub>=t.rubPath&&parted>=0&&apart>=t.snapApart;
        }
        case Motion::CrossGuard: {
            // Crossed or not: the owner's guard held the controllers 12-22 cm apart at the face. Crossed close to
            // the body is the hug (Chilling Armor's; the owner's hugs, 2026-11-05, cast this instead).
            if(HugPose(n))return Held(false,n.t);
            bool pose=Apart(n)<=t.guardApart;
            for(int h=0;h<2;++h){const auto& l=n.l[h];pose=pose&&l.y>=-.30f&&l.y<=.20f&&Forward(l)>=0&&Forward(l)<=.60f&&std::fabs(l.x)<=.30f&&Still(h);}
            return Held(pose,n.t,t.guardHold);
        }
        case Motion::ArmUp: {
            bool pose=false;
            for(int h=0;h<2;++h){const auto& l=n.l[h];pose=pose||(l.y>=t.raiseAbove&&std::fabs(l.x)<=.55f&&Forward(l)>=-.35f&&Forward(l)<=.60f&&Still(h));}
            return Held(pose,n.t);
        }
        case Motion::MatchStrike: {
            for(int h=0;h<2;++h) {
                const auto& c=n.l[h];
                if(Forward(c)>.20f||Forward(c)<-.35f||std::fabs(c.x)<.15f)continue;   // at the side, not in front
                for(const auto& s:samples) {
                    if(n.t-s.t>t.strikeWindow||s.l[h].y>t.strikeFrom||std::fabs(s.l[h].x-c.x)>.20f)continue;
                    if(s.l[h].y-c.y>=.05f)Toward((s.l[h].y-c.y)/t.strikeDrop);
                    if(s.l[h].y-c.y>=t.strikeDrop)return true;
                }
            }
            return false;
        }
        case Motion::ForearmSweep: {
            const math::Vec shoulder{.17f,-.22f,.05f};
            // The right arm held out on its own side (a hug has it across the chest).
            if(Len(n.l[0]-shoulder)<.30f||n.l[0].x<.10f)return false;
            bool met=false;
            for(const auto& s:samples)if(n.t-s.t<=t.sweepWindow&&Apart(s)<=t.sweepAt){met=true;break;}
            if(!met)return false;
            Toward(1-(Len(n.l[1]-shoulder)-t.sweepTo)/.30f);
            return Len(n.l[1]-shoulder)<=t.sweepTo;
        }
        case Motion::Shake: {
            if(samples.size()<3)return false;
            if(Apart(n)<t.shakeApart) {   // together: rubbing, never shaking
                for(auto& h:swing)for(auto& a:h)a={};
                for(auto& h:turns)for(auto& a:h)a.clear();
                return false;
            }
            int most[2]{};
            for(int h=0;h<2;++h) {
                const auto v=Velocity(h);const float axis[3]={v.x,v.y,v.z};
                for(int a=0;a<3;++a) {
                    auto& w=swing[h][a];auto& times=turns[h][a];
                    const int sign=axis[a]>0?1:axis[a]<0?-1:0;
                    if(sign&&w.sign&&sign!=w.sign){if(w.peak>=t.shakeSpeed)times.push_back(n.t);w.peak=0;}
                    if(sign)w.sign=sign;
                    w.peak=std::max(w.peak,std::fabs(axis[a]));
                    times.erase(std::remove_if(times.begin(),times.end(),[&](double at){return n.t-at>t.shakeWindow;}),times.end());
                    most[h]=std::max(most[h],int(times.size()));
                }
            }
            const bool front=Forward(n.l[0])>-.10f&&Forward(n.l[1])>-.10f&&n.l[0].y>-.90f&&n.l[1].y>-.90f;
            if(front&&std::min(most[0],most[1])>=1)Toward(float(std::min(most[0],most[1]))/float(t.shakeTurns));
            return front&&most[0]>=t.shakeTurns&&most[1]>=t.shakeTurns;
        }
        case Motion::Punch: {   // Teleport: point and pull
            const auto& c=n.l[0];
            const math::Vec shoulder{.17f,-.22f,.05f};
            const auto way=c-shoulder;const float arm=Len(way);
            const bool out=Forward(c)>=t.pointOut&&arm>=t.pointArm&&way.y<=t.pointUp*arm;
            // shown long enough: given up, not shown again until the arm has come in (steadySince -2)
            if(aimingSince>=0&&n.t-aimingSince>t.aimSeconds){aimingSince=-1;steadySince=-2;}
            const auto& l=n.l[1];   // the left arm raised too: none
            if((Forward(l)>=t.pointOtherAhead&&l.y>=t.pointOtherLow)||l.y>=t.pointOtherHigh) {
                aimingSince=outLost=-1;if(steadySince!=-2)steadySince=-1;
                return false;
            }
            if(out&&Len(Velocity(0))<t.pointSteady) {   // held out steady: aimed, and following the arm
                if(steadySince==-1)steadySince=n.t;
                if(steadySince>=0&&n.t-steadySince>=t.pointHold){if(aimingSince<0)aimingSince=n.t;pointWay=way;}
            } else if(!out||steadySince!=-2)steadySince=-1;
            if(aimingSince<0)return false;
            stats.pointAimed=true;
            for(const auto& s:samples) {   // yanked back toward the shoulder from where it pointed
                const auto& a=s.l[0];const float back=Forward(a)-Forward(c);
                if(n.t-s.t>t.yankWindow||Forward(a)<t.pointOut-.05f)continue;
                stats.yankBest=std::max(stats.yankBest,back);
                if(back>=t.yankBack&&std::fabs(c.y-a.y)<=.7f*back&&std::fabs(c.x-a.x)<=.7f*back){aimed=pointWay;landed=true;return true;}
            }
            if(out)outLost=-1;   // drawn in or let down without a yank: given up a moment later
            else if(outLost<0)outLost=n.t;
            else if(n.t-outLost>t.aimLost){aimingSince=steadySince=outLost=-1;}
            return false;
        }
        case Motion::Hug:return Held(HugPose(n),n.t);
        default:return m>Motion::Punch&&m<Motion::End&&ClassDone(*this,m);
        }
    }
public:
    Tuning tuning;
    void Reset(){const auto keep=tuning;*this=Engine{};tuning=keep;}
    bool Watching()const{return watching;}
    // The figures of the hold being watched, or of the last one (until the next begins). With Input::mirror
    // they are the mirror image's, as the detectors saw it (MirrorSides turns a copy back).
    const Output& Stats()const{return stats;}
    Output Step(const Input& in) {
        if(!in.mirror)return Watch(in);
        // Left-handed: watch the mirror image, and give back its sides the player's way round.
        auto out=Watch(std::isfinite(in.time)&&Finite(in.head)?Mirrored(in):in);
        MirrorSides(out);
        return out;
    }
private:
    Output Watch(const Input& in) {
        Output out;
        // A reset forgets the hold: a trigger still held must be pressed again.
        if(!std::isfinite(in.time)||!Finite(in.head)){Reset();wasHeld=in.held;return out;}
        if(in.session!=session||in.epoch!=epoch){Reset();session=in.session;epoch=in.epoch;wasHeld=in.held;}
        current=in;abandon={};castFigures[0]=castFigures[1]=0;keep=false;
        const double dt=in.time-last;
        const bool continuous=last>0&&dt>0&&dt<=.15;
        last=in.time;
        const bool pressed=in.held&&!wasHeld;
        wasHeld=in.held;
        const bool ready=in.active&&in.motion!=Motion::None&&in.tracked[0]&&in.tracked[1]&&Finite(in.hand[0])&&Finite(in.hand[1]);
        if(watching&&(!(in.free||in.held)||!ready||!continuous)) {
            const char* why=!(in.free||in.held)?"let go without the motion":!continuous?"frame gap":"hand busy, untracked or input inactive";
            if(!casts&&!in.free) {   // a hold that cast at least once was not dropped
                out=stats;out.seconds=float(in.time-since);
                out.dropped=!in.held?"let go without the motion":!continuous?"frame gap":"hands busy, untracked or input inactive";
            }
            ClassStopped(*this,watched,why);Report(out);
            Stop();return out;
        }
        if(!watching) {
            if(!(in.free||pressed)||!ready)return out;   // only a fresh press watches: a trigger held as the spell was readied does not
            watching=true;since=in.time;casts=0;Clear();watched=in.motion;
            stats={};stats.apartMin=1e9f;stats.top=-1e9f;stats.low=1e9f;stats.behind[0]=stats.behind[1]=-1e9f;
            out.began=!in.free;
        }
        const auto inverse=math::Conjugate(math::Yaw(math::Heading(in.headRotation)));
        Sample s;s.t=in.time;
        body=std::isfinite(in.bodyScale)?std::clamp(in.bodyScale,MinBodyScale,MaxBodyScale):1.f;
        for(int h=0;h<2;++h){s.l[h]=math::Rotate(inverse,in.hand[h]-in.head)*(1/body);s.w[h]=in.hand[h];}   // in the reference body's units
        samples.push_back(s);
        samples.erase(samples.begin(),std::find_if(samples.begin(),samples.end(),[&](const Sample& x){return in.time-x.t<=tuning.history;}));
        const float apart=Apart(s);
        stats.apartMin=std::min(stats.apartMin,apart);stats.apartMax=std::max(stats.apartMax,apart);
        for(int h=0;h<2;++h) {
            stats.top=std::max(stats.top,s.l[h].y);stats.low=std::min(stats.low,s.l[h].y);stats.fastest=std::max(stats.fastest,Len(Velocity(h)));
            stats.behind[h]=std::max(stats.behind[h],s.l[h].z);
            for(const auto& x:samples)if(in.time-x.t<=tuning.pushWindow)stats.fall[h]=std::max(stats.fall[h],x.l[h].y-s.l[h].y);
            stats.end[h]=s.l[h];
        }
        out.seconds=float(in.time-since);
        const float rest=ClassRest(in.motion);
        if(castAt>=0&&in.time-castAt<(rest>=0?rest:tuning.cooldown))return out;
        progress=0;
        if(Done(in.motion)) {
            const auto seconds=out.seconds;out=stats;out.seconds=seconds;out.cast=true;out.aim=aimed;
            out.landing=landed;out.point=landed?pointWay:math::Vec{};
            out.land=land;out.spot=spot;
            out.castFigures[0]=castFigures[0];out.castFigures[1]=castFigures[1];
            castAt=in.time;++casts;
            if(!keep)Clear();   // the motion again, from its start, casts again
        }
        else {
            out.progress=progress;
            if(aimingSince>=0){out.aiming=true;out.point=pointWay;}
        }
        Report(out);
        return out;
    }
};
}
// The classes' detectors. Each header includes this one; include spell_motion.h, never those directly.
#include "motions_amazon.h"
#include "motions_barbarian.h"
#include "motions_necromancer.h"
#include "motions_sorceress.h"
#include "motions_assassin.h"
#include "motions_paladin.h"
#include "motions_druid.h"
#include "motions_warlock.h"
namespace framevr::spellmotion {
inline bool Warlock(Motion m){return m>=Motion::SummonGoatman&&m<=Motion::SigilDeath;}
inline bool Paladin(Motion m){return m>=Motion::Charge&&m<=Motion::FistOfHeavens;}
inline bool Druid(Motion m){return m==Motion::Werewolf||m==Motion::Werebear;}
inline bool ClassDone(Engine& e,Motion m) {
    if(m>=Motion::FireWall&&m<=Motion::Hydra)return sorceress::Done(e,m);
    if(m>=Motion::InnerSight&&m<=Motion::Valkyrie)return amazon::Done(e,m);
    if(m>=Motion::BattleOrders&&m<=Motion::Whirlwind)return barbarian::Done(e,m);
    if(m>=Motion::BoneArmor&&m<=Motion::RaiseMage)return necromancer::Done(e,m);
    if(m>=Motion::BurstOfSpeed&&m<=Motion::MindBlast)return assassin::Done(e,m);
    if(Paladin(m))return paladin::Done(e,m);
    if(Druid(m))return druid::Done(e,m);
    if(Warlock(m))return warlock::Done(e,m);
    return false;
}
inline const char* ClassCue(Motion m) {
    if(m>=Motion::FireWall&&m<=Motion::Hydra)return sorceress::Cue(m);
    if(m>=Motion::InnerSight&&m<=Motion::Valkyrie)return amazon::Cue(m);
    if(m>=Motion::BattleOrders&&m<=Motion::Whirlwind)return barbarian::Cue(m);
    if(m>=Motion::BoneArmor&&m<=Motion::RaiseMage)return necromancer::Cue(m);
    if(m>=Motion::BurstOfSpeed&&m<=Motion::MindBlast)return assassin::Cue(m);
    if(Paladin(m))return paladin::Cue(m);
    if(Druid(m))return druid::Cue(m);
    if(Warlock(m))return warlock::Cue(m);
    return "no motion";
}
inline float ClassRest(Motion m){return Paladin(m)?paladin::Rest(m):Druid(m)?druid::Rest(m):-1.f;}
inline void ClassStopped(Engine& e,Motion m,const char* why) {
    if(Paladin(m))paladin::Stopped(e,m,why);
    else if(Druid(m))druid::Stopped(e,m,why);
}
// The line a watcher's Output::abandoned logs (the Paladin's Frame hammer:, charge:, holy shield: and heavens:, the
// Druid's Frame shapeshift:, as their own engines' were), false for none.
inline bool DropLine(Motion m,const Output& o,char* line,size_t size) {
    if(!o.abandoned||!line||!size)return false;
    if(Paladin(m))return paladin::DropLine(m,o,line,size);
    if(Druid(m))return druid::DropLine(m,o,line,size);
    return false;
}
// Blessed Hammer's pumps (owner, as their own engine's: one hammer a punch, as fast as the cast rate): never the
// second's wait after a cast, start none, and leave the other watchers be (the transport).
inline bool Rapid(Motion m){return m==Motion::BlessedHammer;}
// The Paladin's and the Druid's are watched the frame the grips open too (Gate::State::watch): Fist of the Heavens
// may end on the fists opening, as their own engines were.
inline bool WatchesOpening(Motion m){return Paladin(m)||Druid(m);}
// The order a class's motions are watched in: the first to complete in a frame casts. The Sorceress's
// Static Field before Nova (its snap apart is as wide as a fling), Blizzard before Frost Nova (a pull from
// overhead to the waist is Blizzard's), Hydra and Meteor before Thunder Storm (each passes an arm overhead);
// the other classes' their header's Order.
constexpr Motion SorceressOrder[]={Motion::RubSnap,Motion::Blizzard,Motion::FlingOut,Motion::PushDown,Motion::CrossGuard,
    Motion::Hydra,Motion::Meteor,Motion::ArmUp,Motion::MatchStrike,Motion::ForearmSweep,Motion::Shake,Motion::Hug,Motion::Punch,
    Motion::FireWall};
struct Watched {const Motion* motions=nullptr;int count=0;};
inline Watched OrderOf(int skillClass) {
    switch(skillClass) {
    case 0:return {amazon::Order,int(std::size(amazon::Order))};
    case 1:return {SorceressOrder,int(std::size(SorceressOrder))};
    case 2:return {necromancer::Order,int(std::size(necromancer::Order))};
    case 3:return {paladin::Order,int(std::size(paladin::Order))};
    case 4:return {barbarian::Order,int(std::size(barbarian::Order))};
    case 5:return {druid::Order,int(std::size(druid::Order))};
    case 6:return {assassin::Order,int(std::size(assassin::Order))};
    case 7:return {warlock::Order,int(std::size(warlock::Order))};
    default:return {};
    }
}
}
