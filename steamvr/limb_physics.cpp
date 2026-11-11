#include "limb_physics.h"
#include "physics_worlds.h"
#include <box3d/box3d.h>
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

namespace framevr::limbs {
namespace {
b3Vec3 B(Vec v) {return {v.x,v.y,v.z};}
b3Quat B(Quat q) {return {{q.x,q.y,q.z},q.w};}
Vec F(b3Vec3 v) {return {v.x,v.y,v.z};}
Quat F(b3Quat q) {return {q.v.x,q.v.y,q.v.z,q.s};}
Quat Unit(Quat q) {
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!(n>1e-6f)||!std::isfinite(n))return {0,0,0,1};
    q={q.x/n,q.y/n,q.z/n,q.w/n};
    return q.w<0?Quat{-q.x,-q.y,-q.z,-q.w}:q;
}
float Angle(Quat q) {return 2*std::acos(std::min(1.f,std::fabs(Unit(q).w)));}
// From a toward b, share t of the way.
Quat Toward(Quat a,Quat b,float t) {
    if(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0)b={-b.x,-b.y,-b.z,-b.w};
    return Unit({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t});
}
// The closest points of segments p-q and a-b: s along the first, t along the second.
float SegmentDistance(Vec p,Vec q,Vec a,Vec b,float* s=nullptr,float* t=nullptr) {
    const Vec d=q-p,e=b-a,r=p-a;
    const float dd=math::Dot(d,d),ee=math::Dot(e,e),de=math::Dot(d,e),dr=math::Dot(d,r),er=math::Dot(e,r);
    float u=0,v=0;
    if(dd<1e-8f&&ee<1e-8f){}
    else if(dd<1e-8f)v=std::clamp(er/ee,0.f,1.f);
    else if(ee<1e-8f)u=std::clamp(-dr/dd,0.f,1.f);
    else {
        const float den=dd*ee-de*de;
        if(den>1e-8f)u=std::clamp((de*er-dr*ee)/den,0.f,1.f);
        v=(de*u+er)/ee;
        if(v<0){v=0;u=std::clamp(-dr/dd,0.f,1.f);}
        else if(v>1){v=1;u=std::clamp((de-dr)/dd,0.f,1.f);}
    }
    if(s)*s=u;
    if(t)*t=v;
    return Length(r+d*u-e*v);
}
// Weapons and limbs meet only each other: limbs of one body or of two never
// touch, nor the props' and bottles' world (another Box3D world).
constexpr uint64_t PusherBit=0x2,LimbBit=0x4;
constexpr float Density=985;                          // kg/m3: flesh
// The springs (limb_tuning.h): at each joint the pose between limbs, at most
// Joint strength per kg of the limbs it holds up (a light touch is held, a
// hard hit wins and the limb swings until the spring has it); at the root its
// animated place, at most Body steadiness per kg of the body (and 3/8 of it in
// N m), a little more damped than the joints.
constexpr float RootTorqueShare=.375f,RootExtraDamping=.3f;
constexpr float LinearDamping=.4f,AngularDamping=1.5f;
constexpr float MinRadius=.02f,MaxRadius=.45f;
constexpr float LimbFriction=.4f,WeaponFriction=.3f;
constexpr int MaxSteps=8;
// As dropped_bottles: a warp is placed, never swept; weapon capsules stay thin.
constexpr float ColliderJump=.75f,ColliderMaxSpeed=30,MinColliderRadius=.02f,MaxColliderRadius=.08f;
// A weapon is a body of its own, held to a kinematic hand where the
// controller has it by a joint of limited strength: a light limb gives way at
// once, a heavy body resists and slows it. Caught too far behind its hand, it
// slips: it jumps back to the hand and passes through bodies for a moment.
// Its strength is Push strength (N), a fifth of it in N m.
constexpr float HandHertz=30,HandDamping=1,HandTorqueShare=.2f;
constexpr float WeaponMassPerMetre=4,MinWeaponMass=.8f,MaxWeaponMass=6;   // kg: a sword 2.6, a polearm 6, a fist .8
constexpr float SlipDistance=.2f,SlipTurn=.6f;         // metres, radians behind its hand
constexpr double SlipTime=.25;                         // seconds it passes through bodies after slipping
// A landed hit: an impulse on the struck limb along the swing, Hit kick N s
// per square root of the limb's mass, so a heavy limb moves less.
constexpr float StrikeReach=.15f;                      // metres between the weapon and the limb it struck
constexpr float ContactSpeed=3;                        // m/s: a limb found inside a weapon leaves it this fast
constexpr float StillTurn=.03f,StillShift=.01f;        // radians, metres: back on its animation
bool Valid(const Weapon& w) {
    const Vec d=w.tip-w.base;
    return w.active&&Finite(w.base)&&Finite(w.tip)&&std::isfinite(w.radius)&&w.radius>0&&math::Dot(d,d)<=3*3;
}
float CapsuleVolume(float length,float radius) {return 3.14159265f*radius*radius*(length+radius*4/3);}
}
struct World::Impl {
    b3WorldId world=b3_nullWorldId;
    // Box3D works near its origin: physics position = world - origin.
    Vec origin{};bool placed=false;
    struct Pusher {
        b3BodyId hand=b3_nullBodyId,blade=b3_nullBodyId;   // where the controller has it; the weapon's body
        b3ShapeId shape=b3_nullShapeId;
        float length=0,radius=0;
        Weapon target{};
        Vec velocity{};          // the tip's, between the last two targets (m/s)
        bool passing=false;double passUntil=0;
    };
    std::array<Pusher,Weapons> pushers{};
    struct Body {
        Rig rig{};                                     // the pose its limbs are carried with
        b3BodyId anchor=b3_nullBodyId;                 // the root limb's animated place
        std::array<b3BodyId,MaxLimbs> limb{};
        std::array<b3JointId,MaxLimbs> ball{},hold{};  // [i] limb i joined to its parent, and held to its animated turn there; hold[0] the root to anchor
        float mass=0;
        double seen=0,lively=0;                        // the last pose; the last time it was off its animation or in reach
    };
    std::vector<Body> bodies;
    Tuning tune=Tuning::Standard();
    float Turn() const {return tune[MaxBend]*3.14159265f/180;}
    float Shove() const {return tune[MaxShove]/100;}
    std::vector<Rig> posed;
    // Each posed body's pose when its animation last moved: one held still
    // for StillLife (a corpse: the game holds its last frame) is lifeless,
    // never simulated - a body pushed on the floor only contorted. One first
    // seen is lifeless until its animation moves: a corpse come back to was
    // new again, and a weapon there in its first second woke it.
    struct Watch {Rig ref{};double moved=0,seen=0;};
    std::unordered_map<uint32_t,Watch> watched;
    std::vector<uint32_t> lifeless;
    bool Lifeless(const Rig& r,double time) {
        auto [it,fresh]=watched.try_emplace(r.entity);
        auto& w=it->second;
        w.seen=time;
        if(fresh){w.ref=r;w.moved=-1e9;return true;}
        bool moved=w.ref.count!=r.count||w.ref.layout!=r.layout;
        for(int i=0;i<r.count&&!moved;++i) {
            const auto& now=r.limbs[size_t(i)];const auto& then=w.ref.limbs[size_t(i)];
            moved=Length(now.joint-then.joint)>StillMove||Angle(math::Multiply(now.q,math::Conjugate(then.q)))>StillAngle;
        }
        if(moved){w.ref=r;w.moved=time;}
        return time-w.moved>=StillLife;
    }
    std::vector<Touch> touches;
    unsigned slips=0;
    double last=0,accumulator=0;
    bool timed=false;
    Impl() {
        auto def=b3DefaultWorldDef();
        def.gravity={0,0,0};      // the animation holds the pose up; only pushes move it
        def.workerCount=1;        // serial: Box3D starts no threads in the game process
        def.contactSpeed=ContactSpeed;
        def.enableSleep=false;
        std::lock_guard table(physics::WorldTable());   // physics_worlds.h: Box3D's one table of worlds
        world=b3CreateWorld(&def);
    }
    ~Impl() {std::lock_guard table(physics::WorldTable());if(b3World_IsValid(world))b3DestroyWorld(world);}
    static void Destroy(b3BodyId& id) {if(!B3_IS_NULL(id)&&b3Body_IsValid(id))b3DestroyBody(id);id=b3_nullBodyId;}
    void Drop(Body& b) {   // destroying a body takes its joints with it
        for(int i=0;i<b.rig.count;++i)Destroy(b.limb[size_t(i)]);
        Destroy(b.anchor);
    }
    static void Drop(Pusher& p) {Destroy(p.blade);Destroy(p.hand);p.shape=b3_nullShapeId;p.passing=false;}
    void Clear() {
        for(auto& b:bodies)Drop(b);
        bodies.clear();
        for(auto& p:pushers)Drop(p);
        timed=false;accumulator=0;
    }
    Body* Find(uint32_t entity) {
        for(auto& b:bodies)if(b.rig.entity==entity)return &b;
        return nullptr;
    }
    // A weapon within reach of any of the body's limbs.
    bool Reached(const Rig& r) const {
        for(const auto& p:pushers) {
            if(!Valid(p.target))continue;
            for(int i=0;i<r.count;++i) {
                const auto& l=r.limbs[size_t(i)];
                if(SegmentDistance(p.target.base,p.target.tip,l.a,l.b)-l.radius-p.target.radius<WakeReach)return true;
            }
        }
        return false;
    }
    bool Create(const Rig& r,double time) {
        if(r.count<1||r.count>MaxLimbs)return false;
        Body b;b.rig=r;b.seen=b.lively=time;
        const auto& root=r.limbs[0];
        auto anchor=b3DefaultBodyDef();
        anchor.type=b3_kinematicBody;anchor.position=B(root.joint-origin);anchor.rotation=B(root.q);
        b.anchor=b3CreateBody(world,&anchor);
        if(B3_IS_NULL(b.anchor))return false;
        std::array<float,MaxLimbs> held{};   // each limb's mass and all below it
        for(int i=0;i<r.count;++i) {
            const auto& l=r.limbs[size_t(i)];
            auto def=b3DefaultBodyDef();
            def.type=b3_dynamicBody;def.position=B(l.joint-origin);def.rotation=B(l.q);
            def.gravityScale=0;def.linearDamping=LinearDamping;def.angularDamping=AngularDamping;def.enableSleep=false;
            auto& body=b.limb[size_t(i)];
            body=b3CreateBody(world,&def);
            if(B3_IS_NULL(body)){Drop(b);return false;}
            auto shape=b3DefaultShapeDef();
            shape.density=Density;shape.baseMaterial.friction=LimbFriction;
            shape.filter.categoryBits=LimbBit;shape.filter.maskBits=PusherBit;
            shape.userData=reinterpret_cast<void*>(uintptr_t(r.entity));
            const Quat inverse=math::Conjugate(l.q);
            Vec a=math::Rotate(inverse,l.a-l.joint),e=math::Rotate(inverse,l.b-l.joint);
            if(Length(e-a)<.002f)e=a+Vec{0,.002f,0};   // a ball: a capsule needs two ends
            const b3Capsule capsule{B(a),B(e),std::clamp(l.radius,MinRadius,MaxRadius)};
            if(B3_IS_NULL(b3CreateCapsuleShape(body,&shape,&capsule))){Drop(b);return false;}
            held[size_t(i)]=b3Body_GetMass(body);
            b.mass+=held[size_t(i)];
        }
        for(int i=r.count;i-->1;)if(const int up=r.parent[size_t(i)];up>=0&&up<i)held[size_t(up)]+=held[size_t(i)];
        auto motor=b3DefaultMotorJointDef();
        motor.base.bodyIdA=b.anchor;motor.base.bodyIdB=b.limb[0];
        motor.base.localFrameA=b3Transform_identity;motor.base.localFrameB=b3Transform_identity;
        const float rootDamping=tune[Bounce]+RootExtraDamping;
        motor.linearHertz=tune[BodySpring];motor.linearDampingRatio=rootDamping;motor.maxSpringForce=tune[Steadiness]*b.mass;
        motor.angularHertz=tune[BodySpring];motor.angularDampingRatio=rootDamping;motor.maxSpringTorque=tune[Steadiness]*RootTorqueShare*b.mass;
        motor.maxVelocityForce=0;motor.maxVelocityTorque=0;
        b.hold[0]=b3CreateMotorJoint(world,&motor);
        for(int i=1;i<r.count;++i) {
            const int up=r.parent[size_t(i)];
            if(up<0||up>=i){Drop(b);return false;}
            const auto& l=r.limbs[size_t(i)];const auto& p=r.limbs[size_t(up)];
            const b3Vec3 at=B(math::Rotate(math::Conjugate(p.q),l.joint-p.joint));
            auto ball=b3DefaultSphericalJointDef();
            ball.base.bodyIdA=b.limb[size_t(up)];ball.base.bodyIdB=b.limb[size_t(i)];
            ball.base.localFrameA={at,b3Quat_identity};ball.base.localFrameB=b3Transform_identity;
            b.ball[size_t(i)]=b3CreateSphericalJoint(world,&ball);
            // The animated turn at the joint, held by a spring of limited torque.
            auto hold=b3DefaultMotorJointDef();
            hold.base.bodyIdA=b.limb[size_t(up)];hold.base.bodyIdB=b.limb[size_t(i)];
            hold.base.localFrameA={at,B(math::Multiply(math::Conjugate(p.q),l.q))};hold.base.localFrameB=b3Transform_identity;
            hold.linearHertz=0;hold.maxSpringForce=0;
            hold.angularHertz=tune[LimbSpring];hold.angularDampingRatio=tune[Bounce];hold.maxSpringTorque=tune[JointStrength]*held[size_t(i)];
            hold.maxVelocityForce=0;hold.maxVelocityTorque=0;
            b.hold[size_t(i)]=b3CreateMotorJoint(world,&hold);
        }
        bodies.push_back(b);
        return true;
    }
    // A new pose: each limb carried with its animation, keeping its offset
    // from it (and its motion turned with it); the root's place and the
    // joints' animated pose follow.
    void Carry(Body& b,const Rig& r,double time) {
        for(int i=0;i<r.count;++i) {
            const auto& was=b.rig.limbs[size_t(i)];const auto& now=r.limbs[size_t(i)];
            const auto body=b.limb[size_t(i)];
            const auto t=b3Body_GetTransform(body);
            const Quat offset=math::Multiply(math::Conjugate(was.q),F(t.q));
            const Vec at=math::Rotate(math::Conjugate(was.q),F(t.p)+origin-was.joint);
            b3Body_SetTransform(body,B(now.joint+math::Rotate(now.q,at)-origin),B(Unit(math::Multiply(now.q,offset))));
            const Quat turn=math::Multiply(now.q,math::Conjugate(was.q));
            b3Body_SetLinearVelocity(body,B(math::Rotate(turn,F(b3Body_GetLinearVelocity(body)))));
            b3Body_SetAngularVelocity(body,B(math::Rotate(turn,F(b3Body_GetAngularVelocity(body)))));
            if(i) {
                const auto& p=r.limbs[size_t(r.parent[size_t(i)])];
                const b3Vec3 joint=B(math::Rotate(math::Conjugate(p.q),now.joint-p.joint));
                b3Joint_SetLocalFrameA(b.ball[size_t(i)],{joint,b3Quat_identity});
                b3Joint_SetLocalFrameA(b.hold[size_t(i)],{joint,B(math::Multiply(math::Conjugate(p.q),now.q))});
            }
        }
        b3Body_SetTransform(b.anchor,B(r.limbs[0].joint-origin),B(r.limbs[0].q));
        b.rig=r;b.seen=time;
    }
    b3WorldTransform Target(const Weapon& w,float& length) const {
        const Vec d=w.tip-w.base;length=Length(d);
        const b3Quat q=length>1e-4f?b3ComputeQuatBetweenUnitVectors({0,1,0},B(d*(1/length))):b3Quat{{0,0,0},1};
        b3WorldTransform t{};t.p=B(w.base-origin);t.q=q;return t;
    }
    // A weapon's hand and body where its pose is, still.
    static void Place(Pusher& p,const b3WorldTransform& t) {
        for(const auto id:{p.hand,p.blade}) {
            b3Body_SetTransform(id,t.p,t.q);
            b3Body_SetLinearVelocity(id,{0,0,0});b3Body_SetAngularVelocity(id,{0,0,0});
        }
    }
    void Make(Pusher& p,const b3WorldTransform& t,float length,float radius) {
        Drop(p);
        auto def=b3DefaultBodyDef();def.type=b3_kinematicBody;def.position=t.p;def.rotation=t.q;
        p.hand=b3CreateBody(world,&def);
        auto blade=b3DefaultBodyDef();blade.type=b3_dynamicBody;blade.position=t.p;blade.rotation=t.q;
        blade.gravityScale=0;blade.isBullet=true;blade.enableSleep=false;
        p.blade=b3CreateBody(world,&blade);
        if(B3_IS_NULL(p.hand)||B3_IS_NULL(p.blade)){Drop(p);return;}
        auto shape=b3DefaultShapeDef();shape.enableHitEvents=true;shape.baseMaterial.friction=WeaponFriction;
        shape.filter.categoryBits=PusherBit;shape.filter.maskBits=LimbBit;
        const float extent=std::max(length,.001f);
        shape.density=std::clamp(WeaponMassPerMetre*extent,MinWeaponMass,MaxWeaponMass)/CapsuleVolume(extent,radius);
        const b3Capsule capsule{{0,0,0},{0,extent,0},radius};
        p.shape=b3CreateCapsuleShape(p.blade,&shape,&capsule);
        auto motor=b3DefaultMotorJointDef();
        motor.base.bodyIdA=p.hand;motor.base.bodyIdB=p.blade;
        motor.base.localFrameA=b3Transform_identity;motor.base.localFrameB=b3Transform_identity;
        const float force=tune[PushStrength],torque=force*HandTorqueShare;
        motor.linearHertz=HandHertz;motor.linearDampingRatio=HandDamping;motor.maxSpringForce=force;
        motor.angularHertz=HandHertz;motor.angularDampingRatio=HandDamping;motor.maxSpringTorque=torque;
        motor.maxVelocityForce=force;motor.maxVelocityTorque=torque;
        b3CreateMotorJoint(world,&motor);
        p.length=length;p.radius=radius;p.passing=false;
    }
    // Before this call's steps: each weapon's hand heads for its pose over
    // time seconds and the joint pulls the weapon after it. A new or resized
    // one, or a warp, is placed without sweeping.
    void Drive(float time) {
        for(auto& p:pushers) {
            if(!Valid(p.target)){Drop(p);continue;}
            float length=0;const auto target=Target(p.target,length);
            const float radius=std::clamp(p.target.radius,MinColliderRadius,MaxColliderRadius);
            if(B3_IS_NULL(p.hand)||!b3Body_IsValid(p.hand)||std::fabs(length-p.length)>.01f||std::fabs(radius-p.radius)>.005f) {
                Make(p,target,length,radius);p.velocity={};continue;
            }
            const auto now=b3Body_GetTransform(p.hand);
            const Vec base=F(now.p),tip=base+math::Rotate(F(now.q),{0,length,0});
            const Vec toBase=F(target.p)-base,toTip=(F(target.p)+math::Rotate(F(target.q),{0,length,0}))-tip;
            const float moved=std::sqrt(std::max(math::Dot(toBase,toBase),math::Dot(toTip,toTip)));
            if(moved>ColliderJump||moved>ColliderMaxSpeed*time){Place(p,target);p.velocity={};}
            else {b3Body_SetTargetTransform(p.hand,target,time,true);p.velocity=time>0?toTip*(1/time):Vec{};}
        }
    }
    // After the steps: a hand stops where it is until its next pose, never
    // coasting on. A weapon caught too far behind its hand jumps back to it
    // and passes through bodies for SlipTime; after that it meets them again.
    void Settle(double time) {
        for(auto& p:pushers) {
            if(B3_IS_NULL(p.hand)||!b3Body_IsValid(p.hand))continue;
            b3Body_SetLinearVelocity(p.hand,{0,0,0});b3Body_SetAngularVelocity(p.hand,{0,0,0});
            const auto h=b3Body_GetTransform(p.hand),w=b3Body_GetTransform(p.blade);
            const Vec hb=F(h.p),wb=F(w.p),ht=hb+math::Rotate(F(h.q),{0,p.length,0}),wt=wb+math::Rotate(F(w.q),{0,p.length,0});
            const bool behind=std::max(Length(hb-wb),Length(ht-wt))>SlipDistance||Angle(math::Multiply(math::Conjugate(F(h.q)),F(w.q)))>SlipTurn;
            if(!p.passing&&behind) {
                b3Body_SetTransform(p.blade,h.p,h.q);b3Body_SetLinearVelocity(p.blade,{0,0,0});b3Body_SetAngularVelocity(p.blade,{0,0,0});
                auto filter=b3Shape_GetFilter(p.shape);filter.maskBits=0;b3Shape_SetFilter(p.shape,filter,false);
                p.passing=true;p.passUntil=time+SlipTime;++slips;
            } else if(p.passing&&time>=p.passUntil) {
                auto filter=b3Shape_GetFilter(p.shape);filter.maskBits=LimbBit;b3Shape_SetFilter(p.shape,filter,true);
                p.passing=false;
            }
        }
    }
    void Hits() {
        const auto events=b3World_GetContactEvents(world);
        for(int i=0;i<events.hitCount;++i) {
            const auto& e=events.hitEvents[i];
            for(int slot=0;slot<Weapons;++slot) {
                const auto& p=pushers[size_t(slot)];
                if(B3_IS_NULL(p.shape))continue;
                const bool a=B3_ID_EQUALS(e.shapeIdA,p.shape),b=B3_ID_EQUALS(e.shapeIdB,p.shape);
                if(!a&&!b)continue;
                const auto entity=uint32_t(reinterpret_cast<uintptr_t>(b3Shape_GetUserData(a?e.shapeIdB:e.shapeIdA)));
                const Body* body=Find(entity);
                auto same=std::find_if(touches.begin(),touches.end(),[&](const Touch& t){return t.entity==entity&&t.slot==slot;});
                if(same!=touches.end())same->speed=std::max(same->speed,e.approachSpeed);
                else if(touches.size()<MaxTouches)touches.push_back({entity,body?body->rig.kind:Kind::None,slot,e.approachSpeed});
            }
        }
    }
    // After the steps: no limb bent or body shoved past its limit (pressed
    // there, it stops), and nothing that stopped making sense kept.
    bool Hold(Body& b) {
        for(int i=0;i<b.rig.count;++i) {
            const auto body=b.limb[size_t(i)];const auto& l=b.rig.limbs[size_t(i)];
            const auto t=b3Body_GetTransform(body);
            Quat q=F(t.q);Vec p=F(t.p);
            if(!Finite(p)||!std::isfinite(q.x+q.y+q.z+q.w))return false;
            bool moved=false;
            const float angle=Angle(math::Multiply(q,math::Conjugate(l.q)));
            if(angle>Turn()){q=Toward(l.q,q,Turn()/angle);moved=true;b3Body_SetAngularVelocity(body,{0,0,0});}
            if(!i) {
                const Vec d=p+origin-l.joint;const float shoved=Length(d);
                if(shoved>Shove()){p=l.joint+d*(Shove()/std::max(shoved,1e-6f))-origin;moved=true;b3Body_SetLinearVelocity(body,{0,0,0});}
            }
            if(moved)b3Body_SetTransform(body,B(p),B(q));
        }
        return true;
    }
    // Back on its animation: every limb within StillTurn and the root within StillShift.
    bool Still(const Body& b) const {
        for(int i=0;i<b.rig.count;++i) {
            const auto t=b3Body_GetTransform(b.limb[size_t(i)]);
            if(Angle(math::Multiply(F(t.q),math::Conjugate(b.rig.limbs[size_t(i)].q)))>StillTurn)return false;
            if(!i&&Length(F(t.p)+origin-b.rig.limbs[0].joint)>StillShift)return false;
        }
        return true;
    }
};
World::World():impl(std::make_unique<Impl>()) {}
World::~World()=default;
bool World::Ready() const {return b3World_IsValid(impl->world);}
void World::Bodies(const Rig* rigs,unsigned count,double time) {
    auto& m=*impl;
    m.posed.assign(rigs,rigs+count);
    m.lifeless.clear();
    for(const auto& r:m.posed)if(m.Lifeless(r,time))m.lifeless.push_back(r.entity);
    for(auto it=m.watched.begin();it!=m.watched.end();)it=time-it->second.seen>3?m.watched.erase(it):std::next(it);
    for(auto& b:m.bodies) {
        const auto it=std::find_if(m.posed.begin(),m.posed.end(),[&](const Rig& r){return r.entity==b.rig.entity;});
        if(it==m.posed.end())continue;
        if(it->layout!=b.rig.layout||it->count!=b.rig.count){b.seen=-1e9;continue;}   // another model: rebuilt
        if(std::find(m.lifeless.begin(),m.lifeless.end(),b.rig.entity)!=m.lifeless.end()){b.seen=-1e9;continue;}   // died: let go, eased back
        if(it->at!=b.rig.at)m.Carry(b,*it,time);
        else b.seen=time;
    }
}
void World::Tune(const Tuning& t) {
    auto& m=*impl;
    if(t==m.tune)return;
    m.tune=t;
    m.Clear();   // made again, with these values, as weapons near them; the hook eases their poses back meanwhile
}
void World::Hands(const std::array<Weapon,Weapons>& weapons) {
    for(int i=0;i<Weapons;++i)impl->pushers[size_t(i)].target=weapons[size_t(i)];
}
void World::Advance(double time,Vec player) {
    auto& m=*impl;
    if(!Ready()||!std::isfinite(time)||!Finite(player))return;
    const Vec offset=player-m.origin;
    if(!m.placed||std::hypot(offset.x,offset.z)>Recentre||std::fabs(offset.y)>Recentre) {
        m.Clear();
        m.origin={std::round(player.x),std::round(player.y),std::round(player.z)};m.placed=true;
    }
    // Gone, rebuilt or back at rest out of reach: let go.
    for(size_t k=m.bodies.size();k-->0;) {
        auto& b=m.bodies[k];
        if(m.Reached(b.rig)||!m.Still(b))b.lively=time;
        if(time-b.seen>Unseen||time-b.lively>RestTime){m.Drop(b);m.bodies.erase(m.bodies.begin()+ptrdiff_t(k));}
    }
    // A weapon nearing a body brings it to life, the nearest first.
    std::vector<const Rig*> waking;
    for(const auto& r:m.posed)
        if(!m.Find(r.entity)&&r.count>0&&std::find(m.lifeless.begin(),m.lifeless.end(),r.entity)==m.lifeless.end()&&m.Reached(r))waking.push_back(&r);
    std::sort(waking.begin(),waking.end(),[&](const Rig* a,const Rig* b){
        return Length(a->limbs[0].joint-player)<Length(b->limbs[0].joint-player);});
    for(const auto* r:waking)if(m.bodies.size()<size_t(MaxBodies))m.Create(*r,time);
    if(m.bodies.empty()){m.timed=false;for(auto& p:m.pushers)Impl::Drop(p);return;}
    if(!m.timed){m.last=time;m.timed=true;m.accumulator=0;}
    double dt=time-m.last;m.last=time;
    if(!(dt>=0)||dt>.25)dt=Step;   // a pause or clock jump resumes in place
    m.accumulator+=dt;
    int planned=0;
    for(double left=m.accumulator;left>=Step&&planned<MaxSteps;left-=Step)++planned;
    if(planned)m.Drive(float(planned*Step));
    int steps=0;
    while(m.accumulator>=Step&&steps<MaxSteps) {
        b3World_Step(m.world,float(Step),4);
        m.Hits();
        m.accumulator-=Step;++steps;
    }
    if(steps==MaxSteps)m.accumulator=0;
    if(!steps)return;
    m.Settle(time);
    for(size_t k=m.bodies.size();k-->0;)if(!m.Hold(m.bodies[k])){m.Drop(m.bodies[k]);m.bodies.erase(m.bodies.begin()+ptrdiff_t(k));}
}
bool World::Strike(int slot) {
    auto& m=*impl;
    if(slot<0||slot>=Weapons)return false;
    const auto& p=m.pushers[size_t(slot)];
    if(B3_IS_NULL(p.blade)||!b3Body_IsValid(p.blade))return false;
    const auto w=b3Body_GetTransform(p.blade);
    const Vec base=F(w.p)+m.origin,tip=base+math::Rotate(F(w.q),{0,p.length,0});
    // The limb nearest the weapon, of any body, within reach of it.
    Impl::Body* struck=nullptr;int limb=-1;float best=StrikeReach;Vec on{},from{};
    for(auto& b:m.bodies)for(int i=0;i<b.rig.count;++i) {
        const auto t=b3Body_GetTransform(b.limb[size_t(i)]);
        const auto& l=b.rig.limbs[size_t(i)];
        // The limb's capsule as it is now: its animated ends carried by its offset.
        const Quat turn=math::Multiply(F(t.q),math::Conjugate(l.q));const Vec at=F(t.p)+m.origin;
        const Vec a=at+math::Rotate(turn,l.a-l.joint),e=at+math::Rotate(turn,l.b-l.joint);
        float s=0,u=0;
        const float gap=SegmentDistance(base,tip,a,e,&s,&u)-l.radius-p.radius;
        if(gap<best){best=gap;struck=&b;limb=i;from=base+(tip-base)*s;on=a+(e-a)*u;}
    }
    if(!struck)return false;
    Vec along=Length(p.velocity)>.5f?math::Normalized(p.velocity):math::Normalized(on-from);
    if(math::Dot(along,along)<.5f)return false;
    const auto body=struck->limb[size_t(limb)];
    if(!(m.tune[HitKick]>0))return false;
    const Vec impulse=along*(m.tune[HitKick]*std::sqrt(std::max(b3Body_GetMass(body),.1f)));
    b3Body_ApplyLinearImpulse(body,B(impulse),B(on-m.origin),true);
    return true;
}
unsigned World::Reactions(std::array<Reaction,MaxBodies>& out,uint64_t now) const {
    const auto& m=*impl;
    unsigned n=0;
    for(const auto& b:m.bodies) {
        if(n>=out.size())break;
        auto& r=out[n];r={};
        r.entity=b.rig.entity;r.layout=b.rig.layout;r.at=now;r.count=b.rig.count;
        for(int i=0;i<b.rig.count;++i)
            r.turn[size_t(i)]=Unit(math::Multiply(math::Conjugate(b.rig.limbs[size_t(i)].q),F(b3Body_GetRotation(b.limb[size_t(i)]))));
        const auto& root=b.rig.limbs[0];
        r.shift=math::Rotate(math::Conjugate(root.q),F(b3Body_GetPosition(b.limb[0]))+m.origin-root.joint);
        ++n;
    }
    return n;
}
unsigned World::Touches(std::array<Touch,MaxTouches>& out) {
    auto& t=impl->touches;
    const unsigned n=unsigned(std::min(t.size(),out.size()));
    std::copy_n(t.begin(),n,out.begin());t.clear();
    return n;
}
unsigned World::Slips() {return std::exchange(impl->slips,0u);}
unsigned World::Simulated() const {return unsigned(impl->bodies.size());}
unsigned World::Lifeless() const {return unsigned(impl->lifeless.size());}
float World::Mass(uint32_t entity) const {
    for(const auto& b:impl->bodies)if(b.rig.entity==entity)return b.mass;
    return 0;
}
void World::Reset() {auto& m=*impl;m.Clear();m.posed.clear();m.touches.clear();m.watched.clear();m.lifeless.clear();m.placed=false;}
}
