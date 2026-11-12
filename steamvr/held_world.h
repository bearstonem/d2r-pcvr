#pragma once
#include "held_collision.h"
#include "dropped_bottles.h"
#include "world_coordinates.h"
#include <array>
#include <climits>

// Held items meet the world (owner, 2026-11-12: "Also make it so the weapons and shields can collide with other
// geometry like world geometry the same way they can collide with each other"). A weapon or a shield in either hand
// stops at the level's walls, closed doors and the floor, slides along them, and the drawn hand gives way as it does
// at your own shield (held_collision.h): up to Cap, past which the item goes into the wall, and it eases back to the
// controller once clear.
//
// The world, as the pose thread can have it cheaply:
// - The floor: the ground the hero stands on (his model's origin), flat around him - the drawn floor the arms hang
//   over, which body calibration puts at the real one. Stairs and slopes within reach are not followed.
// - Walls: the game's own collision cells around the hero (vr/wall_cells.cpp, the missile-barrier bits 0x804 physical
//   arrows and dropped bottles stop at), each 2 world units (about 46 cm) square, standing straight up from that
//   floor - they have no height of their own. Every wall runs along the cell grid, so a wall's face is one plane.
//   Left out: an object's footprint (bit 0x400: a barrel, an urn, a chest - no 6 m pillar over a barrel, and a
//   swing still meets the barrel itself and breaks it) and cells the game has no room for (a doorway into the next
//   area). A closed door (0x800) stays: the sword stops at it.
// Nothing else is world here: monsters are still struck, small props knocked, bodies pushed, by the drawn items.
//
// Pure logic: no native types, no threads. vr/skeletons.cpp (HeldCollide) carries each hand's item (the push of the
// other item already in it) into world metres by its Frame, gathers the floor and the walls under the items
// (AddFloor, AddWalls), steps a WorldSolver once per pose stamp and adds its pushes, turned back into the hands'
// frame, to the item-against-item ones. World metres: the rendered world's units over its units per metre, y up.
namespace framevr::held {
using math::Vec;

constexpr float WallBelow=.5f;         // m: a wall cell's box reaches this far under the floor
constexpr float FloorHalf=4.f;         // m: the floor's box around the hands, each way
constexpr int MaxBlocks=128;           // wall faces and the floor looked at in one step
constexpr int WindowCells=16;          // the most cells across looked at around the items
constexpr float JumpMetres=.75f;       // an item moved this far in one step (a teleport, a waypoint): started afresh
constexpr float NeedMargin=.1f;        // m round the items' reach when the walls are gathered
constexpr int MaxContacts=8;           // faces one item is held off at once (a corner, the floor)

// ---- what each hand holds, and why it may not meet the world now (held_collision.h's Reason bits) ----
struct Reach {
    bool item[2]{};         // per arm (0 right, 1 left): it holds something that meets the world
    bool shield[2]{};       // that item is the shield
    uint32_t type[2]{};     // a weapon's kind (D2RVR_TYPE_*); 0 for the shield
    uint32_t why=0;         // why nothing meets the world now (Reason bits)
    uint32_t armWhy[2]{};   // why that arm's item does not (Untracked, PutAway, Unmeasured), the skeleton hook's
    bool Acts(int arm) const {return item[arm&1]&&!why&&!armWhy[arm&1];}
    bool Any() const {return item[0]||item[1];}
};
// The items that meet the world: a one-handed weapon in either hand and the shield, as Decide pairs them for each
// other - a lone sword (the other hand empty) too. Left out as there: bows, staves and two-handed grips (the hands
// meet on them), throws, Druid forms, menus, wheels and the leather menu; and the view.
inline Reach DecideWorld(const Holding& h) {
    Reach r;
    const int mainArm=h.leftHanded?1:0,offArm=mainArm^1;
    if(OneHandWeapon(h.main)){r.item[mainArm]=true;r.type[mainArm]=h.main;}
    if(h.shield){r.item[offArm]=true;r.shield[offArm]=true;}
    else if(OneHandWeapon(h.off)){r.item[offArm]=true;r.type[offArm]=h.off;}
    if(!h.world)r.why|=WorldOff;
    if(!h.firstPerson)r.why|=View;
    if(h.menu)r.why|=Menu;
    if(h.wheel)r.why|=Wheel;
    if(h.leather)r.why|=Leather;
    if(h.form)r.why|=Form;
    if(BowType(h.main)||BowType(h.off))r.why|=Bow;
    if(h.twoHands||ShaftType(h.main)||ShaftType(h.off))r.why|=TwoHands;
    if(h.throwing)r.why|=Throwing;
    if(!r.Any())r.why|=EmptyHand;
    return r;
}

// ---- the hands' frame in the world ----
// The head's turn-only frame (the hands' frame: metres from the eyes, x right, y up, z back) placed in the world:
// world metres = rotation * v + origin. Built from the skeleton hook's own mapping of a hand (ControllerPoint): the
// eye in the hero's model space, the camera's yaw there (atan2(x, z), 0 = the model's forward), the model's units per
// metre, and the hero's model matrix (row vectors, the game's; uniform scale, upright).
struct Frame {
    bool ok=false;
    float r[9]{};            // rotation, row major: world = r * v
    Vec origin{};            // the eye, world metres
    float unitsPerMetre=0;   // the rendered world's units per metre as the arms are drawn
    float floorUnits=0;      // the hero's ground, the rendered world's height (units)
    Vec heroUnits{};         // the hero's place (units)
    Vec Axis(Vec v) const {return {r[0]*v.x+r[1]*v.y+r[2]*v.z,r[3]*v.x+r[4]*v.y+r[5]*v.z,r[6]*v.x+r[7]*v.y+r[8]*v.z};}
    Vec Point(Vec v) const {return Axis(v)+origin;}
    Vec Back(Vec w) const {return {r[0]*w.x+r[3]*w.y+r[6]*w.z,r[1]*w.x+r[4]*w.y+r[7]*w.z,r[2]*w.x+r[5]*w.y+r[8]*w.z};}   // an axis back
    float FloorY() const {return floorUnits/unitsPerMetre;}
};
inline Frame FrameOf(const float model[16],Vec eye,float yaw,float modelUnitsPerMetre) {
    Frame f;
    for(int i=0;i<16;++i)if(!std::isfinite(model[i]))return f;
    if(!Finite(eye)||!std::isfinite(yaw)||!(modelUnitsPerMetre>0))return f;
    const Vec rows[3]={{model[0],model[1],model[2]},{model[4],model[5],model[6]},{model[8],model[9],model[10]}};
    const float scale=Length(rows[0]);
    if(!(scale>.01f))return f;
    for(const Vec& row:rows)if(std::fabs(Length(row)-scale)>.02f*scale)return f;   // uniform scale only
    if(rows[1].y<.99f*scale)return f;                                                // standing upright
    const float c=std::cos(yaw),s=std::sin(yaw);
    // The hands' frame -> model space (skeletons.cpp ToModel): x and z flipped, turned by the camera's yaw.
    const float t[9]={-c,0,-s, 0,1,0, s,0,-c};
    // Model space -> world: world_k = sum_j model_j * row_j[k] / scale.
    auto component=[](Vec v,int k){return k==0?v.x:k==1?v.y:v.z;};
    for(int k=0;k<3;++k)for(int i=0;i<3;++i) {
        float sum=0;
        for(int j=0;j<3;++j)sum+=component(rows[j],k)/scale*t[j*3+i];
        f.r[k*3+i]=sum;
    }
    f.unitsPerMetre=scale*modelUnitsPerMetre;
    if(!(f.unitsPerMetre>=1&&f.unitsPerMetre<=12))return f;
    const Vec translation{model[12],model[13],model[14]};
    const Vec eyeWorld=Vec{component(rows[0],0)*eye.x+component(rows[1],0)*eye.y+component(rows[2],0)*eye.z,
                           component(rows[0],1)*eye.x+component(rows[1],1)*eye.y+component(rows[2],1)*eye.z,
                           component(rows[0],2)*eye.x+component(rows[1],2)*eye.y+component(rows[2],2)*eye.z}+translation;
    f.origin=eyeWorld*(1/f.unitsPerMetre);
    f.floorUnits=translation.y;f.heroUnits=translation;
    f.ok=Finite(f.origin);
    return f;
}

// ---- the world near the hands ----
enum class Meets : uint8_t {Floor,Wall,Door};
constexpr int MeetsCount=3;
inline const char* MeetsName(Meets m) {return m==Meets::Floor?"floor":m==Meets::Door?"door":"wall";}
// Faces of an axis box: k 0 x, 1 y, 2 z; side +1 or -1.
inline uint8_t FaceBit(int k,float side) {return uint8_t(1u<<(k*2+(side<0?1:0)));}
inline Vec Normal(int k,float side) {return k==0?Vec{side,0,0}:k==1?Vec{0,side,0}:Vec{0,0,side};}
// One thing to stay out of: an axis box (world metres) and the faces an item is pushed out through - a wall cell's
// sides whose neighbour is open, the floor's top. A cell is known by its grid place; the floor by INT_MIN.
struct Block {Plate box{};uint8_t open=0;Meets kind=Meets::Wall;int x=0,z=0;};
// The window's cells, for the log: walls kept (of them closed doors, and those wholly inside a wall), and those left
// out: objects' footprints, no room loaded, outside the sample.
struct CellCounts {
    bool floor=false,patch=false;   // the floor known; a wall sample there
    unsigned walls=0,doors=0,inside=0,objects=0,unknown=0,uncovered=0;
};
struct Surroundings {
    unsigned count=0;std::array<Block,MaxBlocks> blocks{};
    CellCounts cells{};
};
inline Plate AxisBox(Vec lo,Vec hi) {return {(lo+hi)*.5f,{(hi.x-lo.x)*.5f,0,0},{0,(hi.y-lo.y)*.5f,0},{0,0,(hi.z-lo.z)*.5f}};}
// The floor (world metres) under the items: its top only.
inline void AddFloor(Surroundings& s,float floorY,Vec lo,Vec hi) {
    if(s.count>=MaxBlocks||!std::isfinite(floorY)||!Finite(lo)||!Finite(hi))return;
    const Vec mid=(lo+hi)*.5f;
    const float half=std::max({FloorHalf,(hi.x-lo.x)*.5f+1,(hi.z-lo.z)*.5f+1});
    Block b;b.kind=Meets::Floor;b.open=FaceBit(1,1);b.x=b.z=INT_MIN;
    b.box=AxisBox({mid.x-half,floorY-1,mid.z-half},{mid.x+half,floorY,mid.z+half});
    s.blocks[s.count++]=b;s.cells.floor=true;
}
// The sample's walls under the world box lo..hi (metres): each cell a box standing on the floor (floorUnits, the
// rendered world's height of the hero's ground: the grid's shear there places it) from WallBelow under it to
// bottles::WallHeight above, pushing out only through its sides whose neighbour is not a wall.
inline void AddWalls(Surroundings& s,const bottles::Patch& p,float floorUnits,float unitsPerMetre,Vec lo,Vec hi) {
    if(!(unitsPerMetre>=1&&unitsPerMetre<=12)||!std::isfinite(floorUnits)||!Finite(lo)||!Finite(hi))return;
    s.cells.patch=true;
    const float shear=floorUnits*coordinates::GroundShear,u=1/unitsPerMetre,floorY=floorUnits*u;
    auto cell=[&](float metres){return int(std::floor((metres*unitsPerMetre-shear)*.5f));};
    int x0=cell(lo.x),x1=cell(hi.x),z0=cell(lo.z),z1=cell(hi.z);
    if(x1-x0+1>WindowCells){const int mid=(x0+x1)/2;x0=mid-WindowCells/2;x1=x0+WindowCells-1;}
    if(z1-z0+1>WindowCells){const int mid=(z0+z1)/2;z0=mid-WindowCells/2;z1=z0+WindowCells-1;}
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x) {
        if(!p.Covers(x,z)){++s.cells.uncovered;continue;}
        if(!p.Blocked(x,z))continue;
        if(p.Bit(p.unknown,x,z)){++s.cells.unknown;continue;}
        if(p.Bit(p.objects,x,z)){++s.cells.objects;continue;}
        ++s.cells.walls;
        const bool door=p.Bit(p.doors,x,z);
        if(door)++s.cells.doors;
        const uint8_t open=uint8_t((p.Wall(x+1,z)?0:FaceBit(0,1))|(p.Wall(x-1,z)?0:FaceBit(0,-1))|
                                   (p.Wall(x,z+1)?0:FaceBit(2,1))|(p.Wall(x,z-1)?0:FaceBit(2,-1)));
        if(!open){++s.cells.inside;continue;}   // its neighbours hold an item out of it
        if(s.count>=MaxBlocks)continue;
        Block b;b.kind=door?Meets::Door:Meets::Wall;b.open=open;b.x=x;b.z=z;
        b.box=AxisBox({(2.f*x+shear)*u,floorY-WallBelow,(2.f*z+shear)*u},{(2.f*x+2+shear)*u,floorY+bottles::WallHeight,(2.f*z+2+shear)*u});
        s.blocks[s.count++]=b;
    }
}

// ---- an item: a weapon's capsule or the shield's box ----
struct Shape {bool plate=false;Capsule blade{};Plate box{};};
inline bool Valid(const Shape& s) {return s.plate?Valid(s.box):Valid(s.blade);}
inline Span Project(const Shape& s,Vec axis) {return s.plate?Project(s.box,axis):Project(s.blade,axis);}
inline Shape Moved(Shape s,Vec d) {if(s.plate)s.box.center=s.box.center+d;else{s.blade.a=s.blade.a+d;s.blade.b=s.blade.b+d;}return s;}
inline Shape Lerp(const Shape& a,const Shape& b,float t) {Shape s=a;if(a.plate)s.box=Lerp(a.box,b.box,t);else s.blade=Lerp(a.blade,b.blade,t);return s;}
inline void Bounds(const Shape& s,Vec& lo,Vec& hi) {
    if(s.plate) {
        const Plate& p=s.box;
        const Vec e{std::fabs(p.right.x)+std::fabs(p.up.x)+std::fabs(p.out.x),std::fabs(p.right.y)+std::fabs(p.up.y)+std::fabs(p.out.y),
                    std::fabs(p.right.z)+std::fabs(p.up.z)+std::fabs(p.out.z)};
        lo=p.center-e;hi=p.center+e;
    } else {
        const Capsule& c=s.blade;const Vec r{c.radius,c.radius,c.radius};
        lo=Vec{std::min(c.a.x,c.b.x),std::min(c.a.y,c.b.y),std::min(c.a.z,c.b.z)}-r;
        hi=Vec{std::max(c.a.x,c.b.x),std::max(c.a.y,c.b.y),std::max(c.a.z,c.b.z)}+r;
    }
}
inline void Grow(Vec& lo,Vec& hi,Vec plo,Vec phi) {
    lo={std::min(lo.x,plo.x),std::min(lo.y,plo.y),std::min(lo.z,plo.z)};hi={std::max(hi.x,phi.x),std::max(hi.y,phi.y),std::max(hi.z,phi.z)};
}
// The separating axes of an item and a box: the box's faces, a shield's own three, and their crossings (a blade's
// own axis crossed with the faces). Overlap is false when any parts them; Separation is the widest gap (<= 0
// touching), for the log.
template<class F> inline void Axes(const Shape& s,const Plate& box,F f) {
    for(int k=0;k<3;++k)if(!f(Face(box,k)))return;
    Vec own[3];int n=1;
    if(s.plate){own[0]=Face(s.box,0);own[1]=Face(s.box,1);own[2]=Face(s.box,2);n=3;for(int i=0;i<3;++i)if(!f(own[i]))return;}
    else own[0]=math::Normalized(s.blade.b-s.blade.a);
    for(int i=0;i<n;++i)for(int k=0;k<3;++k) {
        Vec axis=math::Cross(own[i],Face(box,k));const float l=Length(axis);
        if(l<1e-3f)continue;
        if(!f(axis*(1/l)))return;
    }
}
inline bool Overlap(const Shape& s,const Plate& box) {
    bool parted=false;
    Axes(s,box,[&](Vec axis){const Span a=Project(s,axis),b=Project(box,axis);parted=a.high<=b.low||b.high<=a.low;return !parted;});
    return !parted;
}
inline float Separation(const Shape& s,const Plate& box) {
    float best=-1e9f;
    Axes(s,box,[&](Vec axis){const Span a=Project(s,axis),b=Project(box,axis);best=std::max(best,std::max(b.low-a.high,a.low-b.high));return true;});
    return best;
}
// How far the item must move out along face k toward side to lie wholly beyond that face.
inline float Depth(const Shape& s,const Plate& box,int k,float side) {
    const Vec axis=Face(box,k);const Span a=Project(s,axis),b=Project(box,axis);
    return side>0?b.high-a.low:a.high-b.low;
}
// Lying across the box on its two other face axes: an item beyond face k has passed through the box there.
inline bool Across(const Shape& s,const Plate& box,int k) {
    for(int j=0;j<3;++j)if(j!=k) {
        const Vec axis=Face(box,j);const Span a=Project(s,axis),b=Project(box,axis);
        if(a.high<=b.low||b.high<=a.low)return false;
    }
    return true;
}
inline bool BoxesMeet(Vec alo,Vec ahi,Vec blo,Vec bhi) {
    return alo.x<bhi.x&&blo.x<ahi.x&&alo.y<bhi.y&&blo.y<ahi.y&&alo.z<bhi.z&&blo.z<ahi.z;
}
// The face of block b an item is kept out of, met at `at` after `before` (the step before it first touched). One it
// is already held off (the next cell of the same wall, so a slide runs on across the cells' seams); else the face it
// lay off just before, none when that face is shared with another wall cell (the item came at this cell through
// that one, which holds it); else the open face it is least deep behind.
inline bool FaceFor(const Shape& before,bool haveBefore,const Shape& at,const Block& b,uint8_t held,int& k,float& side) {
    float best=1e9f;bool found=false;
    for(int j=0;j<3;++j)for(float sd:{1.f,-1.f})if(b.open&held&FaceBit(j,sd)) {
        const float d=Depth(at,b.box,j,sd);
        if(d<best){best=d;k=j;side=sd;found=true;}
    }
    if(found)return true;
    if(haveBefore) {
        float gap=0;int bk=-1;float bs=1;
        for(int j=0;j<3;++j)for(float sd:{1.f,-1.f}) {
            const float g=-Depth(before,b.box,j,sd);
            if(g>gap){gap=g;bk=j;bs=sd;}
        }
        if(bk>=0) {
            if(!(b.open&FaceBit(bk,bs)))return false;
            k=bk;side=bs;return true;
        }
    }
    for(int j=0;j<3;++j)for(float sd:{1.f,-1.f})if(b.open&FaceBit(j,sd)) {
        const float d=Depth(at,b.box,j,sd);
        if(d<best){best=d;k=j;side=sd;found=true;}
    }
    return found;
}

// ---- the solver ----
struct WorldInput {
    double time=0;          // seconds, rising (one step per pose stamp)
    Reach gate{};
    Shape item[2]{};        // per arm, world metres: where its controller holds it now (the other item's push in it)
    const Surroundings* world=nullptr;
};
struct WorldOutput {
    Vec push[2]{};          // per arm: the drawn hand moved off its controller, world metres
    bool touching[2]{};
    unsigned clankArms=0;   // bit per arm: a first touch at ClankSpeed or more, none within ClankGap on that arm
    float strength=0,speed=0;   // the strongest one's strength 0..1 and its closing speed, m/s
    Meets clankOn=Meets::Wall;  // what it struck
};
// For the log, since the last TakeStats.
struct WorldStats {
    unsigned contacts[MeetsCount]{};   // first touches of a surface: floor, walls, closed doors
    unsigned capped=0,clanks=0,steps=0,acting=0;
    unsigned noWalls=0;                // acting steps with no wall sample to stand on
    float deepest=0;                   // m: the farthest an item was held off its hand by the world
    float nearest[MeetsCount]{1e9f,1e9f,1e9f};   // m: the closest an item came to each (<= 0 touching), 1e9 never
    uint32_t why=0;                    // every reason that kept a held item from meeting the world
    CellCounts cells{};                // the last acting step's window
};
class WorldSolver {
public:
    enum class Phase {Idle,Touching,Passing};
    // The world box (metres) the next Step will look in for these items: where they are now and as last drawn.
    bool Need(const WorldInput& in,Vec& lo,Vec& hi) const {
        bool any=false;
        for(int arm=0;arm<2;++arm) {
            if(!in.gate.item[arm]||!Valid(in.item[arm]))continue;
            Vec l,h;Bounds(in.item[arm],l,h);
            const Vec push=arms[arm].push;
            Vec pl,ph;Bounds(Moved(in.item[arm],push),pl,ph);Grow(l,h,pl,ph);
            if(arms[arm].havePrevious){Bounds(arms[arm].drawn,pl,ph);Grow(l,h,pl,ph);}
            if(!any){lo=l;hi=h;any=true;}else Grow(lo,hi,l,h);
        }
        if(any){const Vec m{NeedMargin,NeedMargin,NeedMargin};lo=lo-m;hi=hi+m;}
        return any;
    }
    WorldOutput Step(const WorldInput& in) {
        WorldOutput out;
        const double dt=primed?in.time-last:0;
        if(primed&&!(dt>0)){for(int arm=0;arm<2;++arm){out.push[arm]=arms[arm].push;out.touching[arm]=arms[arm].phase==Phase::Touching;}return out;}
        const bool fresh=!primed||dt>MaxStep;
        last=in.time;primed=true;++stats.steps;
        const float decay=fresh?0.f:float(std::exp(-dt/EaseSeconds));
        const Surroundings* w=in.world;
        bool counted=false;
        for(int arm=0;arm<2;++arm) {
            Arm& a=arms[arm];
            a.push=a.push*decay;if(Length(a.push)<.001f)a.push={};
            if(in.gate.item[arm])stats.why|=(in.gate.why|in.gate.armWhy[arm])&~uint32_t(EmptyHand);
            const bool acts=in.gate.Acts(arm)&&w&&Valid(in.item[arm]);
            if(!acts){a.phase=Phase::Idle;a.count=0;a.havePrevious=false;out.push[arm]=a.push;continue;}
            ++stats.acting;
            if(!counted){counted=true;stats.cells=w->cells;if(!w->cells.patch)++stats.noWalls;}
            const Shape& S=in.item[arm];
            for(unsigned i=0;i<w->count;++i) {   // how close it came (the log)
                const Block& b=w->blocks[i];const float gap=Separation(S,b.box);
                float& closest=stats.nearest[int(b.kind)];if(std::isfinite(gap))closest=std::min(closest,gap);
            }
            const bool jump=a.havePrevious&&Travel(a.raw,S)>JumpMetres;
            if(fresh||!a.havePrevious||jump) {   // nothing to follow from: only remembered, so the next step has a way
                a.phase=Phase::Idle;a.count=0;Remember(a,S);out.push[arm]=a.push;continue;
            }
            if(a.phase==Phase::Passing) {        // let through past the cap: nothing pushes until it is clear
                bool inside=false;
                for(unsigned i=0;i<w->count&&!inside;++i)inside=Overlap(S,w->blocks[i].box);
                if(inside){Remember(a,S);out.push[arm]=a.push;continue;}
                a.phase=Phase::Idle;
            }
            const Vec eased=a.push;
            uint8_t heldBefore=0;for(int c=0;c<a.count;++c)heldBefore|=FaceBit(a.contacts[c].k,a.contacts[c].side);
            uint8_t held=heldBefore;
            // 1. Held off the faces it lay on: pushed back to them (the push eased since the last step).
            Shape D=Moved(S,a.push);
            Resolve(a,S,D,*w);
            // 2. New touches on the way from the last drawn pose here, in steps of SubstepMetres: each face is the one
            //    it lay off just before (FaceFor), so a fast swing is stopped at the face it came from.
            bool newFace[MaxContacts]{};
            {
                Vec lo,hi,l,h;Bounds(a.drawn,lo,hi);Bounds(D,l,h);Grow(lo,hi,l,h);
                const int steps=Substeps(a.drawn,D);
                for(unsigned i=0;i<w->count;++i) {
                    const Block& b=w->blocks[i];
                    if(!b.open||Holds(a,b))continue;
                    Vec bl,bh;Bounds({true,{},b.box},bl,bh);
                    if(!BoxesMeet(lo,hi,bl,bh))continue;
                    for(int s=1;s<=steps;++s) {
                        const Shape at=Lerp(a.drawn,D,float(s)/steps);
                        if(!Overlap(at,b.box))continue;
                        int k=0;float side=1;
                        if(FaceFor(Lerp(a.drawn,D,float(s-1)/steps),true,at,b,held,k,side)&&a.count<MaxContacts) {
                            newFace[a.count]=!(heldBefore&FaceBit(k,side));
                            a.contacts[a.count++]={b.kind,b.x,b.z,k,side};held|=FaceBit(k,side);
                        }
                        break;
                    }
                }
            }
            // 3. Pushed out of all of them, a few times round (a corner: out of one face into the other's cell).
            for(int pass=0;pass<4;++pass) {
                const bool moved=Resolve(a,S,D,*w);
                bool added=false;
                for(unsigned i=0;i<w->count&&a.count<MaxContacts;++i) {
                    const Block& b=w->blocks[i];
                    if(!b.open||Holds(a,b)||!Overlap(D,b.box))continue;
                    int k=0;float side=1;
                    if(!FaceFor(a.drawn,true,D,b,held,k,side))continue;
                    newFace[a.count]=!(heldBefore&FaceBit(k,side));
                    a.contacts[a.count++]={b.kind,b.x,b.z,k,side};held|=FaceBit(k,side);added=true;
                }
                if(!moved&&!added)break;
            }
            // Too far off the hand: let through, the push eased back from where it was.
            if(Length(a.push)>Cap+1e-4f) {
                a.push=eased;a.count=0;a.phase=Phase::Passing;++stats.capped;
                Remember(a,S);out.push[arm]=a.push;continue;
            }
            // The faces it still rests on (within Slop), lying across their cells; the rest are let go.
            D=Moved(S,a.push);
            int kept=0;bool keptFresh[MaxContacts]{};
            for(int c=0;c<a.count;++c) {
                const Block* b=Find(*w,a.contacts[c]);
                if(!b)continue;
                const float depth=Depth(D,b->box,a.contacts[c].k,a.contacts[c].side);
                if(depth>-Slop&&(Overlap(D,b->box)||Across(D,b->box,a.contacts[c].k))){keptFresh[kept]=newFace[c];a.contacts[kept++]=a.contacts[c];}
            }
            a.count=kept;
            // A surface newly met: counted, and a clank when it closed fast (the real hand's speed into the face).
            for(int c=0;c<a.count;++c)if(keptFresh[c]) {
                const Contact& t=a.contacts[c];
                bool sameWay=false;for(int e=0;e<c;++e)sameWay=sameWay||(keptFresh[e]&&a.contacts[e].k==t.k&&a.contacts[e].side==t.side);
                if(sameWay)continue;   // one surface, met across several cells: one touch
                ++stats.contacts[int(t.kind)];
                const Vec n=Normal(t.k,t.side);
                const Vec point=S.plate?S.box.center:At(S.blade,Deepest(S.blade,Normal(t.k,1),t.side));
                const Vec was=S.plate?a.raw.box.center:At(a.raw.blade,Deepest(S.blade,Normal(t.k,1),t.side));
                const float closing=-math::Dot((point-was)*float(1/dt),n);
                if(std::isfinite(closing)&&closing>=ClankSpeed&&in.time-a.lastClank>=ClankGap) {
                    a.lastClank=in.time;++stats.clanks;out.clankArms|=1u<<arm;
                    if(closing>out.speed){out.speed=closing;out.strength=ClankStrength(closing);out.clankOn=t.kind;}
                }
            }
            a.phase=a.count?Phase::Touching:Phase::Idle;
            out.touching[arm]=a.count>0;
            if(a.count)stats.deepest=std::max(stats.deepest,Length(a.push));
            Remember(a,S);
            out.push[arm]=a.push;
        }
        return out;
    }
    WorldStats TakeStats(){const WorldStats s=stats;stats={};return s;}
    Phase State(int arm)const{return arms[arm&1].phase;}
    int Contacts(int arm)const{return arms[arm&1].count;}
    void Reset(){const WorldStats s=stats;*this=WorldSolver{};stats=s;}
private:
    struct Contact {Meets kind=Meets::Wall;int x=0,z=0;int k=0;float side=1;};
    struct Arm {
        Phase phase=Phase::Idle;Vec push{};
        Shape raw{},drawn{};bool havePrevious=false;   // the item as its controller held it, and as drawn, last step
        Contact contacts[MaxContacts]{};int count=0;
        double lastClank=-1e9;
    };
    Arm arms[2];bool primed=false;double last=0;
    WorldStats stats{};
    static bool Same(const Contact& c,const Block& b) {return c.kind==b.kind&&c.x==b.x&&c.z==b.z;}
    static bool Holds(const Arm& a,const Block& b) {for(int c=0;c<a.count;++c)if(Same(a.contacts[c],b))return true;return false;}
    static const Block* Find(const Surroundings& w,const Contact& c) {
        for(unsigned i=0;i<w.count;++i)if(Same(c,w.blocks[i]))return &w.blocks[i];
        return nullptr;
    }
    static void Remember(Arm& a,const Shape& S) {a.raw=S;a.drawn=Moved(S,a.push);a.havePrevious=true;}
    // Each face it is held off, in or lying across its cell, pushes it back out to that face (D: S moved by the push).
    static bool Resolve(Arm& a,const Shape& S,Shape& D,const Surroundings& w) {
        bool moved=false;
        for(int c=0;c<a.count;++c) {
            const Block* b=Find(w,a.contacts[c]);
            if(!b)continue;
            const float depth=Depth(D,b->box,a.contacts[c].k,a.contacts[c].side);
            if(!(depth>0)||!(Overlap(D,b->box)||Across(D,b->box,a.contacts[c].k)))continue;
            a.push=a.push+Normal(a.contacts[c].k,a.contacts[c].side)*depth;D=Moved(S,a.push);moved=true;
        }
        return moved;
    }
    static float Travel(const Shape& a,const Shape& b) {
        if(a.plate!=b.plate)return 1e9f;
        if(a.plate)return Length(b.box.center-a.box.center)+Length(b.box.right-a.box.right)+Length(b.box.up-a.box.up);
        return std::max(Length(b.blade.a-a.blade.a),Length(b.blade.b-a.blade.b));
    }
    static int Substeps(const Shape& a,const Shape& b) {
        return std::clamp(int(std::ceil(Travel(a,b)/SubstepMetres)),1,MaxSubsteps);
    }
};
}
