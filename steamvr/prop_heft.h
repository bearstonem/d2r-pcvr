#pragma once
#include "haptic_impact.h"
#include "props.h"
#include <algorithm>
#include <cmath>
#include <string_view>

// Props with weight (docs/KNOCKABLE_PROPS.md, "Weight"; owner 2026-11-12: "add weight and heft to the
// 3D objects that physically can be moved"). The knockable props (props.h) get a mass from what they
// are and how big they are, and the weapon that strikes them has one too (dropped_bottles.cpp): a
// body of the weapon's striking mass held to the hand by a joint of limited strength, as the monster
// bodies' weapons are (limb_physics.cpp). A cup flies off a sword; a barrel takes a full swing to
// shift and stops the blade; the hand feels which. [interactions] prop_heft (VR Settings > Gameplay >
// Physics > Props have weight), on by default; off, weapons push everything aside as they did.
// Plain data and arithmetic, shared by the physics, the transport and the tests.
namespace framevr::propheft {
using math::Vec;

// ---- What a prop weighs ----
// How a kind is built. A vessel is open at the top with thin walls (cups, vases, urns, pails, baskets);
// a shell is closed with walls and some load inside (barrels, crates); anything else is solid through
// a share of its shape (books, stools - mostly air -, sacks, stone).
enum class Build:uint8_t {Vessel,Shell,Solid};
struct Recipe {
    Build build=Build::Solid;
    float density=600;              // kg/m3 of its stuff
    float fill=1;                   // Solid: the share of its shape that is stuff
    float wallShare=0,wallMin=0,wallMax=0; // Vessel/Shell: wall thickness, a share of its narrower half width, within these (m)
    float load=0;                   // Shell: kg/m3 of what is inside
    float least=.02f,most=120;      // kg
};
// Each material's usual build (props.h Material order).
inline constexpr Recipe MaterialRecipes[props::Materials]{
    {Build::Vessel,2000,1,.05f,.003f,.015f,0,.05f,80},   // ceramic: stoneware walls, thicker on a big urn
    {Build::Vessel,2500,1,.06f,.0025f,.006f,0,.05f,10},  // glass
    {Build::Vessel,7800,1,0,.0015f,.0015f,0,.1f,15},     // metal: sheet iron
    {Build::Solid,600,.6f,0,0,0,0,.05f,60},              // wood
    {Build::Solid,750,.85f,0,0,0,0,.02f,10},             // paper: books
    {Build::Solid,1900,.09f,0,0,0,0,.02f,4},             // bone: a skull is mostly hollow
    {Build::Vessel,400,1,0,.008f,.008f,0,.05f,8},        // wicker: loose weave
    {Build::Solid,900,.9f,0,0,0,0,.02f,5},               // food
    {Build::Solid,150,.6f,0,0,0,0,.02f,5},               // cloth
    {Build::Solid,650,.06f,0,0,0,0,.8f,20},              // furniture: legs, seat and back, mostly air
    {Build::Solid,2500,.85f,0,0,0,0,.1f,80},             // stone
    {Build::Solid,700,.7f,0,0,0,0,.5f,60},               // a filled sack of grain
};
// Kinds whose name says more than their material (prepare_props.py KINDS). A name ending in '*' is a
// family: any kind it begins (pillow_blue, package_brown).
struct Named {std::string_view name;Recipe recipe;};
inline constexpr Named NamedRecipes[]{
    {"barrel",{Build::Shell,700,1,.08f,.015f,.03f,40,8,90}},         // oak staves; an empty one is 40 kg, this some load
    {"crate",{Build::Shell,600,1,0,.02f,.02f,120,3,80}},
    {"container",{Build::Shell,600,1,0,.02f,.02f,120,3,80}},
    {"figurine_box",{Build::Shell,600,1,0,.008f,.008f,200,.2f,10}},
    {"firewood",{Build::Solid,600,.55f,0,0,0,0,.5f,30}},
    {"firewood_log",{Build::Solid,600,.7f,0,0,0,0,.5f,25}},
    {"plate_stack",{Build::Solid,2000,.3f,0,0,0,0,.3f,15}},
    {"bowl_set",{Build::Solid,2000,.3f,0,0,0,0,.3f,15}},
    {"bowl_stack",{Build::Solid,2000,.3f,0,0,0,0,.3f,15}},
    {"bowl_stack_small",{Build::Solid,2000,.3f,0,0,0,0,.2f,8}},
    {"stack_o_bowls",{Build::Solid,2000,.3f,0,0,0,0,.3f,15}},
    {"fruit_bowl",{Build::Solid,900,.3f,0,0,0,0,.3f,8}},
    {"pan",{Build::Vessel,7800,1,0,.003f,.003f,0,.3f,5}},             // cast iron
    {"pail",{Build::Vessel,7800,1,0,.0012f,.0012f,0,.3f,6}},
    {"bucket",{Build::Vessel,7800,1,0,.0012f,.0012f,0,.3f,6}},
    {"knife",{Build::Solid,7800,.2f,0,0,0,0,.03f,.5f}},
    {"tool_knife",{Build::Solid,7800,.2f,0,0,0,0,.03f,.5f}},
    {"tool_spoon",{Build::Solid,7800,.2f,0,0,0,0,.02f,.3f}},
    {"hammer",{Build::Solid,7800,.15f,0,0,0,0,.3f,2}},
    {"shovel",{Build::Solid,7800,.04f,0,0,0,0,1,3}},
    {"book_open",{Build::Solid,750,.5f,0,0,0,0,.1f,6}},
    {"book_stack",{Build::Solid,750,.8f,0,0,0,0,.5f,20}},
    {"scrolls",{Build::Solid,500,.35f,0,0,0,0,.05f,4}},
    {"package*",{Build::Solid,300,.9f,0,0,0,0,.1f,20}},
    {"bone",{Build::Solid,1900,.3f,0,0,0,0,.03f,1.5f}},
    {"shinbone",{Build::Solid,1900,.3f,0,0,0,0,.03f,1.5f}},
    {"basket_stack",{Build::Solid,400,.15f,0,0,0,0,.3f,10}},
    {"fish",{Build::Solid,1000,.5f,0,0,0,0,.1f,5}},
    {"pillow*",{Build::Solid,60,.7f,0,0,0,0,.2f,3}},
    {"cloth_folded",{Build::Solid,350,.8f,0,0,0,0,.1f,5}},
    {"crumpled_cloth",{Build::Solid,120,.5f,0,0,0,0,.05f,3}},
    {"rag",{Build::Solid,200,.5f,0,0,0,0,.02f,1}},
    {"chair",{Build::Solid,650,.06f,0,0,0,0,3,15}},
    {"stool",{Build::Solid,650,.06f,0,0,0,0,1.5f,8}},
    {"act1_stool",{Build::Solid,650,.06f,0,0,0,0,1.5f,8}},
    {"barstool",{Build::Solid,650,.06f,0,0,0,0,1.5f,8}},
    {"hay_bale",{Build::Solid,160,.9f,0,0,0,0,5,40}},
};
inline Recipe RecipeFor(std::string_view kind,props::Material material) {
    for(const auto& n:NamedRecipes) {
        const bool family=!n.name.empty()&&n.name.back()=='*';
        const auto stem=family?n.name.substr(0,n.name.size()-1):n.name;
        if(family?kind.substr(0,stem.size())==stem:kind==stem)return n.recipe;
    }
    const unsigned m=unsigned(material);
    return m<props::Materials?MaterialRecipes[m]:Recipe{};
}
// The volume of the body dropped_bottles.cpp builds for a prop of these half extents (m): a box, a
// ball of their mean, or an upright twelve-sided prism inside the ellipse of its x and z.
inline float ShapeVolume(props::Shape shape,Vec half) {
    const float x=std::max(half.x,0.f),y=std::max(half.y,0.f),z=std::max(half.z,0.f);
    switch(shape) {
        case props::Shape::Box:return 8*x*y*z;
        case props::Shape::Ball:{const float r=(x+y+z)/3;return 4.18879020f*r*r*r;}
        default:return 3*x*z*2*y;   // a regular 12-gon fills 3/pi of its circle
    }
}
// Its outer surface: sides, bottom and (closed) top.
inline float SurfaceArea(props::Shape shape,Vec half,bool closed) {
    const float x=std::max(half.x,0.f),y=std::max(half.y,0.f),z=std::max(half.z,0.f);
    if(shape==props::Shape::Box)return 2*(2*x+2*z)*2*y+(closed?2:1)*4*x*z;
    if(shape==props::Shape::Ball){const float r=(x+y+z)/3;return 12.5663706f*r*r;}
    // The ellipse's perimeter (Ramanujan) round its height, and its end(s).
    const float h=(x-z)*(x-z)/std::max((x+z)*(x+z),1e-12f);
    const float perimeter=3.14159265f*(x+z)*(1+3*h/(10+std::sqrt(std::max(4-3*h,0.f))));
    return perimeter*2*y+(closed?2:1)*3.14159265f*x*z;
}
inline constexpr float LeastMass=.02f,MostMass=120;   // kg, whatever it is
// What a prop weighs (kg) from its kind's name (the catalog's, "cup"; empty: its material's usual
// build), its material and shape, and its size: the full extents of its bounds in metres.
inline float Mass(std::string_view kind,props::Material material,props::Shape shape,Vec size) {
    if(!props::Finite(size)||!(size.x>0&&size.y>0&&size.z>0))return LeastMass;
    const Vec half{std::max(size.x,0.f)*.5f,std::max(size.y,0.f)*.5f,std::max(size.z,0.f)*.5f};
    const auto r=RecipeFor(kind,material);
    const float volume=ShapeVolume(shape,half),solid=r.density*volume;
    float mass=solid*r.fill;
    if(r.build!=Build::Solid) {
        const float narrow=std::min(half.x,half.z);
        const float wall=std::clamp(narrow*r.wallShare,r.wallMin,std::max(r.wallMin,r.wallMax));
        const bool closed=r.build==Build::Shell;
        // Never more than the solid it is the skin of (a plate is thinner than two walls).
        mass=std::min(solid,r.density*wall*SurfaceArea(shape,half,closed))+(closed?r.load*volume:0.f);
    }
    if(!std::isfinite(mass))return LeastMass;
    return std::clamp(std::clamp(mass,r.least,std::max(r.least,r.most)),LeastMass,MostMass);
}

// ---- What strikes it ----
// A weapon's striking mass (kg): what the swing carries into what it meets, the weapon and the hand
// and forearm behind it. From its heft (weapon_heft.h Weight: 0 fists, daggers, claws, wands .. 1 a
// great maul): a fist or dagger 0.9, a one-handed sword 1.6, a spear or staff 2.9, a two-handed
// sword 3.5, a great maul 5. A shield, braced on the forearm, 3.
inline constexpr float LightestStrike=.9f,HeaviestStrike=5,ShieldMass=3;
inline float StrikingMass(float weight) {
    const float w=std::isfinite(weight)?std::clamp(weight,0.f,1.f):0.f;
    return LightestStrike+(HeaviestStrike-LightestStrike)*w*w;
}
// How hard the arm pushes it on (N), the joint's strength: one hand about the monster bodies' push
// strength (limb_tuning.h, 300 N), two-handed weapons with both arms more. A braced shield 400.
inline constexpr float WeakestPush=250,StrongestPush=500,ShieldPush=400;
inline float PushStrength(float weight) {
    const float w=std::isfinite(weight)?std::clamp(weight,0.f,1.f):0.f;
    return WeakestPush+(StrongestPush-WeakestPush)*w;
}
// A committed swing has the arm and the body behind the weapon: its striking mass grows with the hand's
// speed, from the weapon and hand alone in a slow poke to Commitment times that in a full swing (FullSwing
// m/s and faster). A punch thrown hard then strikes with 2.7 kg, as measured punches do (2-3 kg); a full
// one-handed sword swing with 4.7 kg, a great maul with 15. A tap at 1 m/s carries one and a half times.
inline constexpr float FullSwing=4;      // m/s: the weapon's speed of a full swing
inline constexpr float Commitment=3;
inline float Committed(float speed) {
    const float s=std::isfinite(speed)?std::clamp(speed/FullSwing,0.f,1.f):0.f;
    return 1+(Commitment-1)*s;
}
// The share of a strike's speed a prop of mass m takes from a weapon of mass w in a plain collision:
// a cup off a sword nearly all of it (the blade goes on), a barrel almost none (the blade stops).
// Unknown masses (0): all, as an immovable weapon gave.
inline float Share(float prop,float weapon) {
    if(!(prop>0)||!(weapon>0)||!std::isfinite(prop)||!std::isfinite(weapon))return 1;
    return weapon/(prop+weapon);
}
// The share of the weapon's own speed it loses to it: what the hand should feel.
inline float Resistance(float prop,float weapon) {
    if(!(prop>0)||!(weapon>0)||!std::isfinite(prop)||!std::isfinite(weapon))return 0;
    return prop/(prop+weapon);
}
// Moving things (a rolling bottle, a knocked cup) set a frozen prop going only with momentum enough to
// move it at least this fast: a cup bumping a barrel leaves it frozen.
inline constexpr float BumpSpeed=.1f;   // m/s
inline bool Bumps(float moverMass,float moverSpeed,float propMass) {
    if(!(propMass>0)||!(moverMass>0))return true;   // unknown: as before
    return moverMass*moverSpeed>=propMass*BumpSpeed;
}
// A spell's kick sets a frozen prop going only if it moves it at least this fast.
inline constexpr float KickWake=.05f;   // m/s

// ---- What the hand feels ----
// The tick or thud of a weapon striking a prop at this speed (m/s): a light prop a short tick that
// grows a little with the speed (as every prop's tick was), a heavy one a hard knock and a low thud
// after it, longer the heavier it is. Strength follows how much of the swing the prop took from the
// weapon. mass or weapon 0 (heft off, or unknown): the old light tick. Melee hits scales it.
inline constexpr float HeavyProp=2;      // kg: from here a thud follows the knock
// The strike's first, strongest pulse (amplitude 0..1); 0 for no strike.
inline float Felt(float speed,float mass,float weapon) {
    if(!std::isfinite(speed)||speed<=0)return 0;
    const float resist=Resistance(mass,weapon);
    if(!(resist>0))return std::clamp(.12f+speed*.02f,.12f,.35f);
    return std::clamp(.12f+.78f*resist*std::clamp(speed/FullSwing,0.f,1.f),.12f,.9f);
}
inline haptics::Sequence Thud(float speed,float mass,float weapon,bool shortMelee=false) {
    haptics::Sequence s;s.category=haptics::Category::Melee;
    const float strength=Felt(speed,mass,weapon);
    if(!(strength>0))return s;
    const float resist=Resistance(mass,weapon);
    if(!(resist>0)) {
        s.count=1;s.steps[0]={0,{strength,15,0}};
    } else {
        const float heavy=std::clamp(std::log2(std::max(mass,1e-3f)/HeavyProp)/5,0.f,1.f);   // 2 kg 0 .. 64 kg 1
        if(mass<HeavyProp){s.count=1;s.steps[0]={0,{strength,int(15+10*resist),0}};}
        else {
            s.count=2;
            s.steps[0]={0,{strength,25,160}};
            s.steps[1]={30,{.6f*strength,int(40+70*heavy),70}};
        }
    }
    // Short melee taps: each step the 10 ms / 180 Hz tap (weighted strikes; the old tick stays as it was).
    if(shortMelee&&resist>0)for(int i=0;i<s.count;++i){s.steps[i].sample.milliseconds=haptics::ShortTapMs;s.steps[i].sample.frequency=haptics::ShortTapHz;}
    return s;
}
}
