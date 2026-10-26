#include "dropped_bottles.h"
#include <box3d/box3d.h>
#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace framevr::bottles {
namespace {
b3Vec3 B(Vec v) {return {v.x,v.y,v.z};}
b3Quat B(Quat q) {return {{q.x,q.y,q.z},q.w};}
Vec F(b3Vec3 v) {return {v.x,v.y,v.z};}
Quat F(b3Quat q) {return {q.v.x,q.v.y,q.v.z,q.s};}
Quat Blend(Quat a,Quat b,float t) {
    if(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0)b={-b.x,-b.y,-b.z,-b.w};
    const Quat q{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    return n>1e-6f?Quat{q.x/n,q.y/n,q.z/n,q.w/n}:a;
}
Vec Lerp(Vec a,Vec b,float t) {return a+(b-a)*t;}
// Glass on stone: a short bounce, then a roll that rolling resistance ends.
constexpr float GlassFriction=.5f,GlassBounce=.3f,GlassRolling=.015f,Density=400; // ~0.3 kg
constexpr float FloorFriction=.7f,FloorBounce=.2f,WallFriction=.4f,WallBounce=.25f;
constexpr float FloorHalf=40; // the ground box around the physics origin, metres
constexpr int Sides=24,MaxSteps=8;
constexpr float WeaponFriction=.4f;
// Weapons and fists are their own collision category: a bottle leaving the
// hand ignores them for FreshTime, a thrown one always.
constexpr uint64_t PusherBit=0x2;
// Level props: twelve-sided round ones; surfaces under them 2 cm thick.
constexpr int PropSides=12,WakeSamples=6;
constexpr float MinHalf=.008f,SurfaceHalf=.01f,SurfaceFriction=.6f,SurfaceBounce=.1f,RestTurn=.3f;
// Props packed against each other (books on a shelf) touch at their bounds:
// shapes a little inside them do not start overlapping.
constexpr float Inset=.002f,InsetShare=.05f;
// Only a body moving this fast sets a frozen neighbour going, so a settling
// prop does not wake the row it leans on.
constexpr float WakeMoverSpeed=.3f;
// Overlaps resolve at this speed (m/s), gently, instead of popping apart.
constexpr float ContactSpeed=1;
}
struct World::Impl {
    b3WorldId world=b3_nullWorldId;
    b3BodyId ground=b3_nullBodyId,walls=b3_nullBodyId;
    struct Bottle {b3BodyId body=b3_nullBodyId;double born=0;uint32_t code=0;Vec p0{},p1{};Quat q0{0,0,0,1},q1{0,0,0,1};b3ShapeId shape=b3_nullShapeId;bool armed=false;};
    std::array<Bottle,MaxBottles> bottles{};
    unsigned count=0;
    // Box3D works near its origin: physics position = world - origin.
    uint64_t session=0;
    float floor=0,upm=0;
    Vec origin{};
    double last=0,accumulator=0;
    bool timed=false;
    Patch patch{};
    bool havePatch=false;
    std::array<Rect,MaxRects> rects{};
    unsigned rectCount=0;
    // Weapons are kinematic capsules along local +Y, driven to each new pose.
    struct Pusher {b3BodyId body=b3_nullBodyId;b3ShapeId shape=b3_nullShapeId;float length=0,radius=0,struck=0;Collider target{};};
    std::array<Pusher,Colliders> pushers{};
    // Level props: static while frozen, dynamic while moving. The box is the
    // shape's extent in the body's frame (the model's origin and axes).
    struct Piece {
        b3BodyId body=b3_nullBodyId;
        Prop prop{};
        Vec centre{},half{};
        props::Box reach{}; // frozen: the shape's bounds in physics space
        bool moving=false,rested=false;
        double since=0,still=0;
        Vec anchor{};Quat turned{0,0,0,1};double anchoredAt=0; // where it was at the start of the drift window
        Vec p0{},p1{};Quat q0{0,0,0,1},q1{0,0,0,1};
    };
    std::vector<Piece> pieces;
    struct Surface {b3BodyId body=b3_nullBodyId;props::Support support{};};
    std::vector<Surface> surfaces;
    std::vector<Knock> knocks;
    struct Flying {b3BodyId body=b3_nullBodyId;b3ShapeId shape=b3_nullShapeId;uint64_t id=0;bool landed=false;double born=0;Vec p0{},p1{};Quat q0{0,0,0,1},q1{0,0,0,1};};
    Flying toss{};
    Vec player{};
    Impl() {
        auto def=b3DefaultWorldDef();
        def.gravity={0,-9.81f,0};
        def.workerCount=1; // serial: Box3D starts no threads in the game process
        def.contactSpeed=ContactSpeed;
        world=b3CreateWorld(&def);
    }
    ~Impl() {if(b3World_IsValid(world))b3DestroyWorld(world);}
    static void Destroy(b3BodyId& id) {if(!B3_IS_NULL(id)&&b3Body_IsValid(id))b3DestroyBody(id);id=b3_nullBodyId;}
    bool Live() const {return !B3_IS_NULL(ground);}
    unsigned MovingCount() const {return unsigned(std::count_if(pieces.begin(),pieces.end(),[](const Piece& p){return p.moving;}));}
    void Remove(unsigned i) {
        Destroy(bottles[i].body);
        for(unsigned k=i+1;k<count;++k)bottles[k-1]=bottles[k];
        bottles[--count]={};
    }
    void Clear() {
        while(count)Remove(count-1);
        for(auto& p:pushers){Destroy(p.body);p.shape=b3_nullShapeId;}
        for(auto& p:pieces)Destroy(p.body);
        for(auto& s:surfaces)Destroy(s.body);
        Destroy(toss.body);toss={};
        pieces.clear();surfaces.clear();knocks.clear();
        Destroy(walls);Destroy(ground);rectCount=0;timed=false;accumulator=0;
    }
    b3WorldTransform Target(const Collider& c,float& length) const {
        const Vec d=c.tip-c.base;length=std::sqrt(math::Dot(d,d));
        const b3Quat q=length>1e-4f?b3ComputeQuatBetweenUnitVectors({0,1,0},B(d*(1/length))):b3Quat{{0,0,0},1};
        b3WorldTransform t{};t.p=B(c.base-origin);t.q=q;return t;
    }
    // Before this call's steps: each weapon heads for its pose over time
    // seconds. A new or resized one, or a warp, is placed without sweeping.
    void Drive(float time) {
        for(auto& p:pushers) {
            if(!Valid(p.target)){Destroy(p.body);p.shape=b3_nullShapeId;continue;}
            float length=0;const auto target=Target(p.target,length);
            const float radius=std::clamp(p.target.radius,MinColliderRadius,MaxColliderRadius);
            if(B3_IS_NULL(p.body)||!b3Body_IsValid(p.body)||std::fabs(length-p.length)>.01f||std::fabs(radius-p.radius)>.005f) {
                Destroy(p.body);
                auto def=b3DefaultBodyDef();def.type=b3_kinematicBody;def.position=target.p;def.rotation=target.q;
                p.body=b3CreateBody(world,&def);
                auto shape=b3DefaultShapeDef();shape.enableHitEvents=true;shape.baseMaterial.friction=WeaponFriction;
                shape.filter.categoryBits=PusherBit;
                const b3Capsule capsule{{0,0,0},{0,std::max(length,.001f),0},radius};
                p.shape=b3CreateCapsuleShape(p.body,&shape,&capsule);
                p.length=length;p.radius=radius;continue;
            }
            const auto now=b3Body_GetTransform(p.body);
            const Vec base=F(now.p),tip=base+math::Rotate(F(now.q),{0,length,0});
            const Vec toBase=F(target.p)-base,toTip=(F(target.p)+math::Rotate(F(target.q),{0,length,0}))-tip;
            const float moved=std::sqrt(std::max(math::Dot(toBase,toBase),math::Dot(toTip,toTip)));
            if(moved>ColliderJump||moved>ColliderMaxSpeed*time) {
                b3Body_SetTransform(p.body,target.p,target.q);
                b3Body_SetLinearVelocity(p.body,{0,0,0});b3Body_SetAngularVelocity(p.body,{0,0,0});
            } else b3Body_SetTargetTransform(p.body,target,time,true);
        }
    }
    // A weapon stops where it is until its next pose: never coast on.
    void Halt() {
        for(auto& p:pushers)if(!B3_IS_NULL(p.body)&&b3Body_IsValid(p.body)) {
            b3Body_SetLinearVelocity(p.body,{0,0,0});b3Body_SetAngularVelocity(p.body,{0,0,0});
        }
    }
    void Hits() {
        const auto events=b3World_GetContactEvents(world);
        for(int i=0;i<events.hitCount;++i) {
            const auto& e=events.hitEvents[i];
            for(auto& p:pushers)if(!B3_IS_NULL(p.shape)&&(B3_ID_EQUALS(e.shapeIdA,p.shape)||B3_ID_EQUALS(e.shapeIdB,p.shape)))
                p.struck=std::max(p.struck,e.approachSpeed);
        }
    }
    void Restart(uint64_t s,float f,float u,Vec around) {
        Clear();
        session=s;floor=f;upm=u;
        origin={std::round(around.x),f,std::round(around.z)};
        auto def=b3DefaultBodyDef();def.position={0,-.5f,0};
        ground=b3CreateBody(world,&def);
        auto box=b3MakeBoxHull(FloorHalf,.5f,FloorHalf);
        auto shape=b3DefaultShapeDef();shape.baseMaterial.friction=FloorFriction;shape.baseMaterial.restitution=FloorBounce;
        b3CreateHullShape(ground,&shape,&box.base);
        BuildWalls();
    }
    void BuildWalls() {
        if(!havePatch||patch.session!=session||B3_IS_NULL(ground))return;
        std::array<Rect,MaxRects> next{};
        const unsigned n=MergeWalls(patch,next);
        if(!B3_IS_NULL(walls)&&n==rectCount&&std::equal(next.begin(),next.begin()+n,rects.begin()))return;
        Destroy(walls);rects=next;rectCount=n;
        if(!n)return;
        auto def=b3DefaultBodyDef();
        walls=b3CreateBody(world,&def);
        auto shape=b3DefaultShapeDef();shape.baseMaterial.friction=WallFriction;shape.baseMaterial.restitution=WallBounce;
        const float hy=(WallHeight+.5f)*.5f; // from half a metre below the floor
        for(unsigned i=0;i<n;++i) {
            const auto& r=rects[i];
            const auto centre=CellCorner(r.x+r.w*.5f,r.z+r.d*.5f,floor,upm)-origin;
            auto box=b3MakeOffsetBoxHull(r.w/upm,hy,r.d/upm,{centre.x,hy-.5f,centre.z});
            b3CreateHullShape(walls,&shape,&box.base);
        }
    }
    Vec Position(const Bottle& b,float alpha) const {return origin+b.p0+(b.p1-b.p0)*alpha;}
    bool Busy() const {return count||!pieces.empty()||!B3_IS_NULL(toss.body);}
    // The world for a release here, restarted for another floor, game or
    // scale unless props are moving (they would stop in the air).
    bool Prepare(const Release& r) {
        const Vec offset=r.p-origin;
        if(!Live()||r.session!=session||std::fabs(r.floor-floor)>.05f||
           std::fabs(r.unitsPerMetre-upm)>.01f*upm||std::hypot(offset.x,offset.z)>FloorHalf*.5f) {
            if(MovingCount())return false;
            Restart(r.session,r.floor,r.unitsPerMetre,r.p);
        }
        return !B3_IS_NULL(ground);
    }
    // A bottle body: an 18 cm cylinder of the model's radius, released with the hand's motion.
    b3BodyId Body(const Release& r,bool events,b3ShapeId& made) {
        const float radius=Radius(r.radius);
        Vec p=r.p-origin;
        p.y=std::max(p.y,std::max(radius,Height*.5f)+.005f); // never start inside the floor
        auto def=b3DefaultBodyDef();
        def.type=b3_dynamicBody;def.position=B(p);def.rotation=B(r.q);
        def.linearVelocity=B(r.v);def.angularVelocity=B(r.w);
        def.linearDamping=.05f;def.angularDamping=.05f;
        const auto body=b3CreateBody(world,&def);
        if(B3_IS_NULL(body))return body;
        auto* hull=b3CreateCylinder(Height,radius,-Height*.5f,Sides);
        if(!hull){b3DestroyBody(body);return b3_nullBodyId;}
        auto shape=b3DefaultShapeDef();shape.density=Density;
        shape.baseMaterial.friction=GlassFriction;shape.baseMaterial.restitution=GlassBounce;shape.baseMaterial.rollingResistance=GlassRolling;
        shape.filter.maskBits=~PusherBit;shape.enableContactEvents=events;
        made=b3CreateHullShape(body,&shape,hull); // the world keeps its own copy
        b3DestroyHull(hull);
        if(B3_IS_NULL(made)){b3DestroyBody(body);return b3_nullBodyId;}
        return body;
    }
    // After a step: the thrown bottle's first touch of anything lands it.
    void Landing() {
        if(B3_IS_NULL(toss.body))return;
        const auto events=b3World_GetContactEvents(world);
        bool touched=false;
        for(int i=0;i<events.beginCount&&!touched;++i)
            touched=B3_ID_EQUALS(events.beginEvents[i].shapeIdA,toss.shape)||B3_ID_EQUALS(events.beginEvents[i].shapeIdB,toss.shape);
        const auto t=b3Body_GetTransform(toss.body);
        toss.p1=F(t.p);toss.q1=F(t.q);
        if(!Finite(toss.p1)||toss.p1.y<-1){toss.p1=Finite(toss.p0)?toss.p0:Vec{};toss.p1.y=0;touched=true;}
        if(touched){Destroy(toss.body);toss.shape=b3_nullShapeId;toss.landed=true;toss.p0=toss.p1;toss.q0=toss.q1;}
    }
    // ---- Level props ----
    // The frozen shape's bounds in physics space, for waking it.
    props::Box Reach(const Piece& p) const {
        const props::Placement at{F(b3Body_GetTransform(p.body).p),F(b3Body_GetRotation(p.body)),{1,1,1}};
        float m[16];props::Compose(at,m);
        return props::WorldBounds({p.centre-p.half,p.centre+p.half},m);
    }
    bool Add(const Prop& prop) {
        Piece piece;piece.prop=prop;
        piece.centre=(prop.bounds.lo+prop.bounds.hi)*.5f;
        const Vec h=(prop.bounds.hi-prop.bounds.lo)*.5f;
        auto inset=[](float v){return std::max(MinHalf,v-std::min(Inset,v*InsetShare));};
        piece.half={inset(h.x),inset(h.y),inset(h.z)};
        auto def=b3DefaultBodyDef();
        def.type=b3_staticBody;def.position=B(prop.p-origin);def.rotation=B(prop.q);
        def.linearDamping=.05f;def.angularDamping=.1f;
        piece.body=b3CreateBody(world,&def);
        if(B3_IS_NULL(piece.body))return false;
        const auto& m=props::Properties[unsigned(prop.material)];
        auto shape=b3DefaultShapeDef();shape.density=m.density;
        shape.baseMaterial.friction=m.friction;shape.baseMaterial.restitution=m.bounce;shape.baseMaterial.rollingResistance=m.rolling;
        const Vec c=piece.centre,e=piece.half;
        b3ShapeId made=b3_nullShapeId;
        if(prop.shape==props::Shape::Box) {
            auto box=b3MakeOffsetBoxHull(e.x,e.y,e.z,B(c));
            made=b3CreateHullShape(piece.body,&shape,&box.base);
        } else if(prop.shape==props::Shape::Ball) {
            const b3Sphere sphere{B(c),std::max(MinHalf,(e.x+e.y+e.z)/3)};
            made=b3CreateSphereShape(piece.body,&shape,&sphere);
        } else {
            // Round: an upright cylinder, elliptical when the bounds are (a handle).
            std::array<b3Vec3,PropSides*2> points{};
            for(int i=0;i<PropSides;++i) {
                const float a=2*3.14159265f*i/PropSides,x=c.x+e.x*std::cos(a),z=c.z+e.z*std::sin(a);
                points[2*i]={x,c.y-e.y,z};points[2*i+1]={x,c.y+e.y,z};
            }
            if(auto* hull=b3CreateHull(points.data(),int(points.size()),int(points.size()))) {
                made=b3CreateHullShape(piece.body,&shape,hull); // the world keeps its own copy
                b3DestroyHull(hull);
            }
        }
        if(B3_IS_NULL(made)){Destroy(piece.body);return false;}
        piece.p0=piece.p1=prop.p-origin;piece.q0=piece.q1=prop.q;
        piece.reach=Reach(piece);
        pieces.push_back(piece);
        return true;
    }
    // One static plane per distinct surface the props rest on.
    void BuildSurfaces() {
        std::vector<props::Support> wanted;
        for(const auto& p:pieces) {
            const auto& s=p.prop.support;
            if(s.floor||s.onProp||s.x1-s.x0<.01f||s.z1-s.z0<.01f||s.y<=floor+.005f)continue;
            const bool known=std::any_of(wanted.begin(),wanted.end(),[&](const props::Support& w){
                return std::fabs(w.y-s.y)<.01f&&std::fabs(w.x0-s.x0)<.01f&&std::fabs(w.z0-s.z0)<.01f&&
                       std::fabs(w.x1-s.x1)<.01f&&std::fabs(w.z1-s.z1)<.01f;});
            if(!known)wanted.push_back(s);
        }
        std::vector<Surface> next;
        for(const auto& w:wanted) {
            auto same=std::find_if(surfaces.begin(),surfaces.end(),[&](const Surface& s){return s.support==w;});
            if(same!=surfaces.end()){next.push_back(*same);same->body=b3_nullBodyId;continue;}
            Surface s;s.support=w;
            auto def=b3DefaultBodyDef();
            const Vec centre=Vec{(w.x0+w.x1)*.5f,w.y-SurfaceHalf,(w.z0+w.z1)*.5f}-origin;
            def.position=B(centre);
            s.body=b3CreateBody(world,&def);
            auto shape=b3DefaultShapeDef();shape.baseMaterial.friction=SurfaceFriction;shape.baseMaterial.restitution=SurfaceBounce;
            auto box=b3MakeBoxHull((w.x1-w.x0)*.5f,SurfaceHalf,(w.z1-w.z0)*.5f);
            b3CreateHullShape(s.body,&shape,&box.base);
            next.push_back(s);
        }
        for(auto& s:surfaces)Destroy(s.body);
        surfaces=std::move(next);
    }
    void Knocked(Piece& p,int slot,float speed,double time) {
        b3Body_SetType(p.body,b3_dynamicBody);
        b3Body_SetAwake(p.body,true);
        p.moving=true;p.rested=false;p.since=time;p.still=0;
        const auto t=b3Body_GetTransform(p.body);p.anchor=F(t.p);p.turned=F(t.q);p.anchoredAt=time;
        knocks.push_back({p.prop.entity,p.prop.kind,slot,speed});
    }
    // Before the steps: a weapon about to pass within reach of a frozen prop,
    // or a fast body about to reach one, sets it going. With as many moving
    // as allowed, a weapon still does: the prop moving longest is frozen.
    void Wake(float time,double now) {
        if(pieces.empty())return;
        unsigned moving=MovingCount();
        struct Sweep {Vec a0,b0,a1,b1;float r,speed;int slot;};
        std::array<Sweep,Colliders> sweeps{};unsigned n=0;
        for(int i=0;i<Colliders;++i) {
            const auto& p=pushers[i];
            if(!Valid(p.target))continue;
            const Vec a1=p.target.base-origin,b1=p.target.tip-origin;
            Vec a0=a1,b0=b1;
            if(!B3_IS_NULL(p.body)&&b3Body_IsValid(p.body)) {
                const auto now=b3Body_GetTransform(p.body);
                a0=F(now.p);b0=a0+math::Rotate(F(now.q),{0,p.length,0});
            }
            const Vec da=a1-a0,db=b1-b0;
            const float moved=std::sqrt(std::max(math::Dot(da,da),math::Dot(db,db)));
            if(moved>ColliderJump||moved>ColliderMaxSpeed*time){a0=a1;b0=b1;} // a warp: no sweep
            sweeps[n++]={a0,b0,a1,b1,std::clamp(p.target.radius,MinColliderRadius,MaxColliderRadius)+WakeMargin,
                         time>0?std::min(moved,ColliderJump)/time:0,i};
        }
        std::vector<props::Box> movers;
        auto mover=[&](b3BodyId body) {
            const Vec v=F(b3Body_GetLinearVelocity(body));const float speed=std::sqrt(math::Dot(v,v));
            if(!b3Body_IsAwake(body)||speed<WakeMoverSpeed)return;
            const auto box=b3Body_ComputeAABB(body);const float grow=speed*time+WakeMargin;
            movers.push_back({F(box.lowerBound)-Vec{grow,grow,grow},F(box.upperBound)+Vec{grow,grow,grow}});
        };
        for(unsigned i=0;i<count;++i)mover(bottles[i].body);
        if(!B3_IS_NULL(toss.body))mover(toss.body);
        for(const auto& p:pieces)if(p.moving)mover(p.body);
        for(auto& p:pieces) {
            if(p.moving)continue;
            const auto t=b3Body_GetTransform(p.body);const Vec at=F(t.p);const Quat q=F(t.q);
            int slot=-1;float speed=0;
            for(unsigned s=0;s<n&&slot<0;++s)for(int k=0;k<WakeSamples;++k) {
                const float f=float(k)/(WakeSamples-1);
                if(props::CapsuleTouchesBox(Lerp(sweeps[s].a0,sweeps[s].a1,f),Lerp(sweeps[s].b0,sweeps[s].b1,f),sweeps[s].r,at,q,p.centre,p.half)) {
                    slot=sweeps[s].slot;speed=sweeps[s].speed;break;
                }
            }
            const bool bumped=slot<0&&moving<props::MaxMoving&&
                std::any_of(movers.begin(),movers.end(),[&](const props::Box& b){return props::Overlap(b,p.reach);});
            if(slot<0&&!bumped)continue;
            if(moving>=props::MaxMoving) {
                Piece* oldest=nullptr;
                for(auto& o:pieces)if(o.moving&&(!oldest||o.since<oldest->since))oldest=&o;
                if(!oldest)continue;
                Freeze(*oldest);--moving;
            }
            Knocked(p,slot,speed,now);++moving;
            if(slot>=0)pushers[slot].struck=std::max(pushers[slot].struck,speed);
        }
    }
    void Freeze(Piece& p) {
        b3Body_SetType(p.body,b3_staticBody);
        const auto t=b3Body_GetTransform(p.body);
        p.p0=p.p1=F(t.p);p.q0=p.q1=F(t.q);
        p.moving=false;p.rested=true;p.reach=Reach(p);
    }
    // After the steps: a moving prop that has stopped, or barely shifted over
    // the last drift window (settling in place), or moved too long or too far
    // away, is frozen where it is.
    void Settle(double now,double elapsed) {
        for(auto& p:pieces) {
            if(!p.moving)continue;
            auto t=b3Body_GetTransform(p.body);
            Vec at=F(t.p);
            if(!Finite(at)||at.y<-1) {
                // Lost through the floor: back onto it, upright.
                const Vec good=Finite(p.p0)?p.p0:p.prop.p-origin;
                at={good.x,p.half.y-p.centre.y,good.z};
                b3Body_SetTransform(p.body,B(at),B(p.prop.q));
                p.p0=p.p1=at;p.q0=p.q1=p.prop.q;
                Freeze(p);continue;
            }
            const Vec v=F(b3Body_GetLinearVelocity(p.body)),w=F(b3Body_GetAngularVelocity(p.body));
            p.still=math::Dot(v,v)<PropRestSpeed*PropRestSpeed&&math::Dot(w,w)<RestTurn*RestTurn?p.still+elapsed:0;
            const Vec world=origin+at;
            const bool away=std::hypot(world.x-player.x,world.z-player.z)>Range;
            bool settled=false;
            if(now-p.anchoredAt>=PropDriftTime) {
                const Vec d=at-p.anchor;const Quat q=F(t.q);
                const float dot=std::fabs(q.x*p.turned.x+q.y*p.turned.y+q.z*p.turned.z+q.w*p.turned.w);
                settled=math::Dot(d,d)<PropDrift*PropDrift&&2*std::acos(std::min(1.f,dot))<PropDriftTurn;
                p.anchor=at;p.turned=q;p.anchoredAt=now;
            }
            if(!b3Body_IsAwake(p.body)||p.still>=PropRestTime||settled||now-p.since>PropMoveLimit||away)Freeze(p);
        }
    }
};
World::World():impl(std::make_unique<Impl>()) {}
World::~World()=default;
bool World::Ready() const {return b3World_IsValid(impl->world);}
bool World::Drop(const Release& release) {
    auto& m=*impl;
    // Props moving on another floor keep theirs; the bottle waits for them.
    if(!Ready()||!Valid(release)||!m.Prepare(release))return false;
    if(m.count==MaxBottles)m.Remove(0);
    b3ShapeId shape=b3_nullShapeId;
    const auto body=m.Body(release,false,shape);
    if(B3_IS_NULL(body))return false;
    const Vec p=F(b3Body_GetPosition(body));
    m.bottles[m.count++]={body,release.time,release.code,p,p,release.q,release.q,shape,false};
    return true;
}
bool World::Toss(const Release& release,uint64_t id) {
    auto& m=*impl;
    if(!Ready()||!id||!Valid(release)||!m.Prepare(release))return false;
    Impl::Destroy(m.toss.body);m.toss={};
    b3ShapeId shape=b3_nullShapeId;
    const auto body=m.Body(release,true,shape);
    if(B3_IS_NULL(body))return false;
    const Vec p=F(b3Body_GetPosition(body));
    m.toss={body,shape,id,false,release.time,p,p,release.q,release.q};
    return true;
}
bool World::Tossed(Thrown& out) const {
    const auto& m=*impl;
    if(!m.toss.id)return false;
    const float alpha=m.toss.landed?1.f:float(std::clamp(m.accumulator/Step,0.,1.));
    out={m.toss.id,m.toss.landed,m.origin+m.toss.p0+(m.toss.p1-m.toss.p0)*alpha,Blend(m.toss.q0,m.toss.q1,alpha)};
    return true;
}
void World::EndToss() {auto& m=*impl;Impl::Destroy(m.toss.body);m.toss={};}
bool World::Prepare(uint64_t session,float floor,float unitsPerMetre,Vec around) {
    Release r;r.session=session;r.floor=floor;r.unitsPerMetre=unitsPerMetre;r.p=around;
    return Ready()&&session&&std::isfinite(floor)&&unitsPerMetre>=1&&unitsPerMetre<=12&&Finite(around)&&impl->Prepare(r);
}
bool World::Trace(Vec p,Vec v,float radius,double maxTime,Vec& hit) const {
    const auto& m=*impl;
    if(!Ready()||!m.Live()||!Finite(p)||!Finite(v)||!(radius>0)||!(maxTime>0))return false;
    const b3Vec3 centre{0,0,0};
    const b3ShapeProxy ball{&centre,1,radius};
    auto filter=b3DefaultQueryFilter();filter.maskBits=~PusherBit;
    struct Closest {float fraction=1;bool hit=false;} closest;
    auto keep=[](b3ShapeId,b3Pos,b3Vec3,float fraction,uint64_t,int,int,void* context)->float {
        auto* c=static_cast<Closest*>(context);c->hit=true;c->fraction=fraction;return fraction;
    };
    // The arc in short straight pieces, each cast in turn.
    constexpr double Piece=1/60.;
    for(double t=0;t<maxTime;t+=Piece) {
        const auto at=[&](double s){return p+v*float(s)+Vec{0,-.5f*9.81f*float(s*s),0}-m.origin;};
        const Vec a=at(t),b=at(std::min(t+Piece,maxTime));
        closest={};
        b3World_CastShape(m.world,B(a),&ball,B(b-a),filter,keep,&closest);
        if(closest.hit){hit=m.origin+a+(b-a)*closest.fraction;return true;}
    }
    return false;
}
void World::Walls(const Patch& patch) {
    auto& m=*impl;
    if(!patch.session||(m.havePatch&&patch.serial==m.patch.serial&&patch.session==m.patch.session))return;
    m.patch=patch;m.havePatch=true;
    if(m.Live()&&patch.session==m.session)m.BuildWalls();
}
void World::Props(const PropScan& scan) {
    auto& m=*impl;
    if(!Ready())return;
    auto drop=[&](auto&& gone){
        for(size_t k=m.pieces.size();k-->0;)if(gone(m.pieces[k])){Impl::Destroy(m.pieces[k].body);m.pieces.erase(m.pieces.begin()+ptrdiff_t(k));}
    };
    const auto lost=std::span(scan.lostEntities).first(std::min<unsigned>(scan.lost,props::MaxLost));
    drop([&](const Impl::Piece& p){return std::find(lost.begin(),lost.end(),p.prop.entity)!=lost.end();});
    if(!scan.session||!std::isfinite(scan.floor)||!(scan.unitsPerMetre>=1&&scan.unitsPerMetre<=12)||!Finite(scan.centre)) {
        drop([](const Impl::Piece& p){return !p.moving;}); // props off or not wanted: frozen ones go
        m.BuildSurfaces();return;
    }
    const Vec offset=scan.centre-m.origin;
    if(!m.Live()||scan.session!=m.session||std::fabs(scan.floor-m.floor)>.05f||
       std::fabs(scan.unitsPerMetre-m.upm)>.01f*m.upm||std::hypot(offset.x,offset.z)>FloorHalf*.5f) {
        // Restarting would leave moving props in the air, and bottles keep
        // the floor they fell on: the props wait for both.
        if(m.MovingCount()||(m.count&&m.session==scan.session))return;
        if(!scan.count) {
            if(m.Live()&&!m.count){drop([](const Impl::Piece&){return true;});m.BuildSurfaces();}
            return;
        }
        m.Restart(scan.session,scan.floor,scan.unitsPerMetre,scan.centre);
    }
    std::vector<uint32_t> seen;
    for(unsigned i=0;i<std::min<unsigned>(scan.count,props::MaxSeen);++i) {
        const auto& prop=scan.props[i];
        const Vec at=prop.p-m.origin;
        if(!Valid(prop)||std::hypot(at.x,at.z)>FloorHalf*.9f||std::find(lost.begin(),lost.end(),prop.entity)!=lost.end())continue;
        seen.push_back(prop.entity);
        if(std::any_of(m.pieces.begin(),m.pieces.end(),[&](const Impl::Piece& p){return p.prop.entity==prop.entity;}))continue;
        if(m.pieces.size()<props::MaxSeen+props::MaxMoving)m.Add(prop);
    }
    // Out of reach: frozen ones go; moving ones finish first.
    drop([&](const Impl::Piece& p){return !p.moving&&std::find(seen.begin(),seen.end(),p.prop.entity)==seen.end();});
    m.BuildSurfaces();
}
void World::Advance(double time,Vec player) {
    auto& m=*impl;
    if(Finite(player))m.player=player;
    for(auto& p:m.pieces)p.rested=false;
    if(!Ready()||!m.Live()||!m.Busy()||!std::isfinite(time)){m.timed=false;return;}
    if(!m.timed){m.last=time;m.timed=true;m.accumulator=0;}
    double dt=time-m.last;m.last=time;
    if(!(dt>=0)||dt>.25)dt=Step; // a pause or clock jump resumes in place
    m.accumulator+=dt;
    int planned=0;
    for(double left=m.accumulator;left>=Step&&planned<MaxSteps;left-=Step)++planned;
    if(planned){m.Wake(float(planned*Step),time);m.Drive(float(planned*Step));}
    int steps=0;
    while(m.accumulator>=Step&&steps<MaxSteps) {
        for(unsigned i=0;i<m.count;++i){m.bottles[i].p0=m.bottles[i].p1;m.bottles[i].q0=m.bottles[i].q1;}
        for(auto& p:m.pieces)if(p.moving){p.p0=p.p1;p.q0=p.q1;}
        if(!B3_IS_NULL(m.toss.body)){m.toss.p0=m.toss.p1;m.toss.q0=m.toss.q1;}
        b3World_Step(m.world,float(Step),4);
        m.Hits();m.Landing();
        for(unsigned i=0;i<m.count;++i) {
            const auto t=b3Body_GetTransform(m.bottles[i].body);
            m.bottles[i].p1=F(t.p);m.bottles[i].q1=F(t.q);
        }
        for(auto& p:m.pieces)if(p.moving){const auto t=b3Body_GetTransform(p.body);p.p1=F(t.p);p.q1=F(t.q);}
        m.accumulator-=Step;++steps;
    }
    if(steps==MaxSteps)m.accumulator=0;
    if(steps){m.Halt();m.Settle(time,steps*Step);}
    // Out of the hand long enough: weapons and fists push it again.
    for(unsigned i=0;i<m.count;++i)if(!m.bottles[i].armed&&time-m.bottles[i].born>=FreshTime) {
        b3Shape_SetFilter(m.bottles[i].shape,b3DefaultFilter(),true);m.bottles[i].armed=true;
    }
    for(unsigned i=m.count;i-->0;) {
        const auto& b=m.bottles[i];
        const Vec p=m.origin+b.p1;
        const bool lost=!Finite(p)||b.p1.y<-1||time-b.born>Lifetime||
            !Finite(player)||std::hypot(p.x-player.x,p.z-player.z)>Range;
        if(lost)m.Remove(i);
    }
}
unsigned World::Show(std::array<Shown,MaxBottles>& out) const {
    const auto& m=*impl;
    const float alpha=float(std::clamp(m.accumulator/Step,0.,1.));
    for(unsigned i=0;i<m.count;++i)out[i]={m.Position(m.bottles[i],alpha),Blend(m.bottles[i].q0,m.bottles[i].q1,alpha),m.bottles[i].code};
    return m.count;
}
unsigned World::Moving(std::array<Moved,props::MaxMoving>& out) const {
    const auto& m=*impl;
    const float alpha=float(std::clamp(m.accumulator/Step,0.,1.));
    unsigned n=0;
    for(const auto& p:m.pieces) {
        if((!p.moving&&!p.rested)||n>=out.size())continue;
        out[n++]={p.prop.entity,m.origin+p.p0+(p.p1-p.p0)*alpha,Blend(p.q0,p.q1,alpha),p.prop.scale,p.prop.unitsPerMetre,p.rested};
    }
    return n;
}
unsigned World::Knocks(std::array<Knock,props::MaxMoving>& out) {
    auto& k=impl->knocks;
    const unsigned n=unsigned(std::min(k.size(),out.size()));
    std::copy_n(k.begin(),n,out.begin());k.clear();
    return n;
}
bool World::WallsWanted(WallRequest& out) const {
    const auto& m=*impl;
    if(!m.Live()||!m.Busy())return false;
    // The newest bottle still moving; once all rest, the newest. Without
    // bottles, a moving prop, else around the player among the props, so
    // the walls are there before a prop is knocked toward them.
    Vec at=m.player;
    if(m.count) {
        unsigned pick=m.count-1;
        for(unsigned i=m.count;i-->0;)if(b3Body_IsAwake(m.bottles[i].body)){pick=i;break;}
        at=m.origin+m.bottles[pick].p1;
    } else if(!B3_IS_NULL(m.toss.body))at=m.origin+m.toss.p1;
    else for(const auto& p:m.pieces)if(p.moving)at=m.origin+p.p1;
    out={m.session,at,m.floor,m.upm};
    return true;
}
void World::Weapons(const std::array<Collider,Colliders>& colliders) {
    for(int i=0;i<Colliders;++i)impl->pushers[i].target=colliders[i];
}
float World::Struck(int slot) {
    if(slot<0||slot>=Colliders)return 0;
    return std::exchange(impl->pushers[slot].struck,0.f);
}
unsigned World::Pushers() const {
    unsigned n=0;for(const auto& p:impl->pushers)n+=!B3_IS_NULL(p.body);return n;
}
unsigned World::Count() const {return impl->count;}
unsigned World::Frozen() const {
    return unsigned(std::count_if(impl->pieces.begin(),impl->pieces.end(),[](const Impl::Piece& p){return !p.moving;}));
}
unsigned World::Supports() const {return unsigned(impl->surfaces.size());}
bool World::Idle() const {return !impl->Live()||!impl->Busy();}
unsigned World::Shapes() const {return impl->rectCount;}
void World::Reset() {auto& m=*impl;m.Clear();m.session=0;m.havePatch=false;m.patch={};for(auto& p:m.pushers)p={};}
}
