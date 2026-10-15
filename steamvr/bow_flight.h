#pragma once
#include "frame_math.h"
#include "world_coordinates.h"
#include "spell_cast.h"
#include <array>
#include <cstdint>
#include <limits>

// Physical arrows use metres and a single release pose. This layer contains no
// native pointers: server, client and renderer exchange ids plus these values.
namespace framevr::bowflight {
using math::Vec;
inline constexpr float ArrowLength=.75f; // same tail/tip as the tracked draw guide
inline constexpr float JavelinLength=1.5f,JavelinGrip=.6f;
enum class Kind : uint32_t { Arrow=0,Javelin=1,Spell=2 };
inline float Length(Vec v) { return std::sqrt(math::Dot(v,v)); }
inline bool Finite(Vec v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
struct Shot {
    Kind kind=Kind::Arrow;
    Vec origin{},direction{},player{};
    float unitsPerMetre=0,draw=0;
    uint64_t session=0,epoch=0,sequence=0,at=0;
    uint32_t equipment=0,stamp=0;
    int skill=-1,owner=-1,hand=0,slot=-1,missile=-1,level=0,member=0;
    uint64_t binding=0,activation=0;
    int castSound=0;
    int aimAssist=0;
    bool planar=true;
};
inline float ProjectileLength(const Shot& s) {return s.kind==Kind::Spell?0:s.kind==Kind::Javelin?JavelinLength:ArrowLength;}
inline uint32_t MissileId(const Shot& s){return s.kind==Kind::Spell?uint32_t(s.missile):uint32_t(s.kind);}
inline bool BasicMissile(uint32_t id){return id<=1;}
// Flight stays in rendered metres. Native paths/rooms use the unsheared grid.
// Remove only the launch FLOOR's shear, never the arrow's airborne height or
// the current player's floor after an area transition.
inline Vec GridPoint(const Shot& shot,Vec renderMetres) {
    return coordinates::GridPlane({renderMetres.x,shot.player.y,renderMetres.z});
}
inline Vec RenderGround(const Shot& shot,Vec gridWorld) {
    return coordinates::RenderGround(gridWorld*(1/shot.unitsPerMetre),shot.player.y);
}
inline bool Valid(const Shot& s) {
    const bool kind=BasicMissile(uint32_t(s.kind))||(s.kind==Kind::Spell&&spellcast::Supported(s.skill)&&s.hand>=0&&s.hand<2&&s.slot>=0&&s.slot<10);
    return kind&&s.session&&s.sequence&&Finite(s.origin)&&Finite(s.direction)&&Finite(s.player)&&
        std::isfinite(s.unitsPerMetre)&&s.unitsPerMetre>=1&&s.unitsPerMetre<=12&&
        std::isfinite(s.draw)&&s.draw>=0&&s.draw<=1&&
        std::fabs(Length(s.direction)-1)<.01f&&Length(s.origin-s.player)<3.5f;
}
struct Gate {
    bool active=false;
    uint64_t session=0,epoch=0,at=0;
    uint32_t equipment=0;
    bool bowActive=true;
};
inline bool Allowed(const Shot& s,const Gate& g,uint64_t now) {
    return Valid(s)&&g.active&&(s.kind==Kind::Spell||g.bowActive)&&s.session==g.session&&s.epoch==g.epoch&&s.equipment==g.equipment&&
        now>=s.at&&now-s.at<=150&&now>=g.at&&now-g.at<=150;
}
class Once {
    uint64_t session=0,sequence=0;
public:
    bool Take(const Shot& s,const Gate& g,uint64_t now) {
        if(!Allowed(s,g,now))return false;
        if(session==s.session&&s.sequence<=sequence)return false;
        session=s.session;sequence=s.sequence;return true;
    }
};

// Earliest point where the moving arrow tip enters a creature capsule. The
// complete segment is tested even when an arrow crosses a body in one tick.
inline bool SphereEntry(Vec from,Vec delta,Vec center,float radius,float& t) {
    const Vec q=from-center;
    const double a=math::Dot(delta,delta),b=math::Dot(q,delta),c=math::Dot(q,q)-radius*radius;
    if(c<=0){t=0;return true;}
    const double d=b*b-a*c;
    if(a<1e-12||d<0)return false;
    const double root=(-b-std::sqrt(d))/a;
    if(root<0||root>1)return false;
    t=float(root);return true;
}
inline bool CapsuleEntry(Vec from,Vec to,Vec bottom,Vec top,float radius,float& t) {
    if(!Finite(from)||!Finite(to)||!Finite(bottom)||!Finite(top)||!std::isfinite(radius)||radius<=0)return false;
    const Vec axis=top-bottom,delta=to-from,q=from-bottom;
    const float h2=math::Dot(axis,axis);
    if(h2<1e-8f)return SphereEntry(from,delta,bottom,radius,t);
    const float projection=std::clamp(math::Dot(q,axis)/h2,0.f,1.f);
    if(Length(q-axis*projection)<=radius){t=0;return true;}
    float best=std::numeric_limits<float>::infinity(),candidate=0;
    if(SphereEntry(from,delta,bottom,radius,candidate))best=candidate;
    if(SphereEntry(from,delta,top,radius,candidate))best=std::min(best,candidate);
    const Vec d=delta-axis*(math::Dot(delta,axis)/h2),p=q-axis*(math::Dot(q,axis)/h2);
    const double a=math::Dot(d,d),b=math::Dot(p,d),c=math::Dot(p,p)-radius*radius,disc=b*b-a*c;
    if(a>1e-12&&disc>=0) {
        const double root=(-b-std::sqrt(disc))/a;
        const double along=(math::Dot(q,axis)+root*math::Dot(delta,axis))/h2;
        if(root>=0&&root<=1&&along>=0&&along<=1)best=std::min(best,float(root));
    }
    if(best>1)return false;
    t=best;return true;
}

// Supercover grid traversal: sample both neighbours at corner crossings. A
// blocking/unloaded native cell is never skipped by a high-speed arrow.
template<class Blocked> float WallEntry(Vec from,Vec to,float unitsPerMetre,Blocked blocked) {
    const double scale=unitsPerMetre*.5,fx=from.x*scale,fz=from.z*scale;
    const double dx=(to.x-from.x)*scale,dz=(to.z-from.z)*scale;
    if(!Finite(from)||!Finite(to)||!std::isfinite(scale)||scale<=0||
       fx<1||fz<1||fx>65534||fz>65534)return 0;
    int x=int(std::floor(fx)),z=int(std::floor(fz));
    if(blocked(x,z))return 0;
    const int sx=dx>0?1:dx<0?-1:0,sz=dz>0?1:dz<0?-1:0;
    const double inf=std::numeric_limits<double>::infinity();
    double tx=sx?(x+(sx>0?1:0)-fx)/dx:inf,tz=sz?(z+(sz>0?1:0)-fz)/dz:inf;
    const double ax=sx?1/std::fabs(dx):inf,az=sz?1/std::fabs(dz):inf;
    for(int step=0;step<512;++step) {
        const double t=std::min(tx,tz);
        if(t>1)return 1;
        const bool both=std::fabs(tx-tz)<1e-9;
        if(both) {
            if(blocked(x+sx,z)||blocked(x,z+sz))return float(t);
            x+=sx;z+=sz;tx+=ax;tz+=az;
        } else if(tx<tz) {x+=sx;tx+=ax;} else {z+=sz;tz+=az;}
        if(x<1||z<1||x>65534||z>65534||blocked(x,z))return float(t);
    }
    return 0; // never approve an unbounded/unexamined segment
}

// Ordinary arrow velocity is 24 << 8, reduced to 75% by native creation.
// Map that to a 20 m/s baseline before the configured multiplier; the native
// Slow Missiles reduction therefore also slows physical flight.
inline float Speed(int nativeVelocity,float draw) {
    if(nativeVelocity<=0||!std::isfinite(draw))return 0;
    return std::clamp(float(nativeVelocity)*(20.f/4608.f)*(.7f+.3f*std::clamp(draw,0.f,1.f)),0.f,100.f);
}
inline float FlightSpeed(const Shot& shot,int nativeVelocity) {
    // Native direction has magnitude 4096; (direction * velocity) >> 8
    // produces 16.16 cells/tick. A cell is two world units, at 25 ticks/s.
    return shot.kind==Kind::Spell?std::clamp(nativeVelocity*(50.f/4096.f)/shot.unitsPerMetre,0.f,100.f):Speed(nativeVelocity,shot.draw);
}
inline Vec Tip(const Shot& s,float distance) {return s.origin+s.direction*(ProjectileLength(s)+distance);}
struct Flight {
    Shot shot{};
    float distance=0,speed=0;
    uint64_t updatedAt=0;
    bool alive=false;
    // A curved spell is a sequence of straight swept segments. Keep the
    // immutable muzzle pose and total distance; changing heading must never
    // rotate the distance already travelled around the original hand.
    Vec segmentOrigin{},segmentDirection{};
    float segmentAt=0;
    bool redirected=false;
    Vec Direction() const {return redirected?segmentDirection:shot.direction;}
    Vec PointAt(float travelled) const {
        return redirected?segmentOrigin+segmentDirection*(travelled-segmentAt):shot.origin+shot.direction*travelled;
    }
    Vec TipAt(float travelled) const {return PointAt(travelled+ProjectileLength(shot));}
    void Redirect(Vec direction) {
        if(shot.kind!=Kind::Spell||NativePath()||!Finite(direction)||std::fabs(Length(direction)-1)>.001f)return;
        segmentOrigin=PointAt(distance);segmentAt=distance;segmentDirection=direction;redirected=true;
    }
    Vec nativePoint{},nativePrevious{},nativeStart{};
    bool NativePath() const {return shot.kind==Kind::Spell&&spellcast::NativePath(shot.skill);}
    // Ground spells retain the muzzle's horizontal placement, but not its
    // height. Use the launch floor, including the first interpolated frame.
    Vec NativeOrigin() const {return {shot.origin.x,shot.player.y,shot.origin.z};}
    void BeginNativePath(Vec gridWorld,uint64_t now) {
        nativeStart=gridWorld;nativePoint=nativePrevious=NativeOrigin();updatedAt=now;
    }
    // Preserve native wandering displacement without relocating/resetting its
    // path. Raising the casting hand must not lift Charged Bolt off the floor.
    void NativePosition(Vec gridWorld,uint64_t now) {
        nativePrevious=nativePoint;
        nativePoint=NativeOrigin()+(gridWorld-nativeStart)*(1/shot.unitsPerMetre);
        updatedAt=now;
    }
    Vec Point() const {return NativePath()?nativePoint:PointAt(distance);}
    std::array<uint32_t,64> hit{};
    size_t hits=0;
    bool Seen(uint32_t id) const {return std::find(hit.begin(),hit.begin()+hits,id)!=hit.begin()+hits;}
    bool Remember(uint32_t id) {if(Seen(id)||hits==hit.size())return false;hit[hits++]=id;return true;}
    Vec TailAt(uint64_t now) const {
        if(NativePath()) {
            if(!alive)return nativePoint;
            const float t=now>=updatedAt?std::min(float(now-updatedAt)/40.f,1.f):0;
            return nativePrevious+(nativePoint-nativePrevious)*t;
        }
        const float extra=alive&&now>=updatedAt?std::min(float(now-updatedAt)/1000.f,.04f)*speed:0;
        return PointAt(distance+extra);
    }
};
// The asset initializer visits both the unit root and its visible mesh. Only
// the mesh has TrackReleasePoint; binding the root would compose the mesh's
// authored height/scale on top of our world-space flight pose. Keep the full
// entity generation and never let a later parent/helper callback rebind it.
class VisualBinding {
    uint32_t entity=UINT32_MAX;
public:
    bool Bound() const {return entity!=UINT32_MAX;}
    bool Matches(uint32_t candidate) const {return Bound()&&entity==candidate;}
    bool Bind(uint32_t candidate,bool hasReleasePoint) {
        if(!hasReleasePoint||(candidate&0xfffff)==0xfffff)return false;
        if(Bound())return Matches(candidate);
        entity=candidate;return true;
    }
    bool BindRoot(uint32_t candidate,bool isRoot) {
        // Particle spells use the unit root. Descendant meshes inherit its
        // translation; binding a mesh as well would apply the offset twice.
        return isRoot&&Bind(candidate,true);
    }
    void Clear(){entity=UINT32_MAX;}
};
// A retained client unit does not guarantee that its HD mesh still renders.
// Recover only after sustained visible demand, with time for asynchronous
// loading. Menus, holstering and tracking loss cancel that demand. The UI
// thread rechecks this before retiring anything; queuing work is not an attempt.
class PreviewRecovery {
    bool missing=false,attempted=false;
    uint64_t missingAt=0,attemptAt=0;
public:
    static bool Rendered(uint64_t at,uint64_t now) {return at&&now>=at&&now-at<150;}
    void Observe(bool wanted,uint64_t renderedAt,uint64_t now) {
        if(!wanted||Rendered(renderedAt,now)){missing=false;return;}
        if(!missing){missing=true;missingAt=now;}
    }
    bool Due(bool hasClient,uint64_t now) const {
        return missing&&(!attempted||(now>=attemptAt&&now-attemptAt>=500))&&
            (!hasClient||(now>=missingAt&&now-missingAt>=3000));
    }
    void Attempt(uint64_t now) {attempted=true;attemptAt=now;missingAt=now;}
};
// A held arrow follows the camera's current world anchor, not a pose copied by
// the asynchronous gameplay timer. Only this render pair may consume it.
struct PreviewRenderPose {
    Shot pose{};
    Gate gate{};
    uint64_t frame=0,revision=0;
    bool visible=false;
    void Publish(const Shot& shot,const Gate& input,bool shown,uint64_t renderFrame) {
        pose=shot;gate=input;visible=shown;frame=renderFrame;++revision;
    }
    bool Read(uint64_t session,uint64_t renderFrame,uint64_t now,Shot& shot,bool& shown) const {
        shown=false;
        if(!frame||frame!=renderFrame||pose.session!=session||!Allowed(pose,gate,now))return false;
        shot=pose;shown=visible;return true;
    }
};
// HD projectile models point along local +Z: arrow tail at -.98391676,
// javelin tail at -1, and both tips at +1 (confirmed in the headset).
// Its entity's stock 45-degree yaw/release-bone offset is replaced, not added.
inline void ArrowMatrix(const Flight& f,uint64_t now,float out[16]) {
    const Vec forward=f.shot.direction;
    const Vec guide=std::fabs(forward.y)<.95f?Vec{0,1,0}:Vec{1,0,0};
    const Vec right=math::Normalized(math::Cross(guide,forward)),up=math::Cross(forward,right);
    const float modelTail=f.shot.kind==Kind::Javelin?-1.f:-.98391676f;
    const float modelLength=1-modelTail;
    const float scale=ProjectileLength(f.shot)*f.shot.unitsPerMetre/modelLength;
    const Vec origin=f.TailAt(now)*f.shot.unitsPerMetre-forward*(modelTail*scale);
    out[0]=right.x*scale;out[1]=right.y*scale;out[2]=right.z*scale;out[3]=0;
    out[4]=up.x*scale;out[5]=up.y*scale;out[6]=up.z*scale;out[7]=0;
    out[8]=forward.x*scale;out[9]=forward.y*scale;out[10]=forward.z*scale;out[11]=0;
    out[12]=origin.x;out[13]=origin.y;out[14]=origin.z;out[15]=1;
}
inline void SpellMatrix(const Flight& f,uint64_t now,float out[16]) {
    const auto p=f.TailAt(now)*f.shot.unitsPerMetre;
    const Vec forward=f.Direction();
    const Vec guide=std::fabs(forward.y)<.95f?Vec{0,1,0}:Vec{1,0,0};
    const Vec right=math::Normalized(math::Cross(guide,forward)),up=math::Cross(forward,right);
    const Vec basis[3]={right,up,forward};
    for(int i=0;i<3;++i) {
        const float scale=Length({out[i*4],out[i*4+1],out[i*4+2]});
        out[i*4]=basis[i].x*scale;out[i*4+1]=basis[i].y*scale;out[i*4+2]=basis[i].z*scale;
    }
    // The straight VFX have a +4-unit child height; Charged Bolt has -.25.
    // Fireball also has +2.3 forward. Cancel authored offsets at the root
    // so the visible emitter starts at the hand for straight shots and on the
    // floor for Charged Bolt. Values verified in owned HD definitions; no assets ship.
    const float ahead=f.shot.skill==47?2.3f:0;
    const float height=f.shot.skill==38?-.25f:4.f;
    out[12]=p.x-out[4]*height-out[8]*ahead;
    out[13]=p.y-out[5]*height-out[9]*ahead;
    out[14]=p.z-out[6]*height-out[10]*ahead;out[15]=1;
}
// A nocked game mesh uses the guide's exact tail, direction and length, without
// native bow animation or prediction. Hidden preview geometry cannot occlude.
inline void PreviewMatrix(const Shot& pose,bool visible,float out[16]) {
    Flight held;held.shot=pose;ArrowMatrix(held,0,out);
    if(!visible)for(int row=0;row<3;++row)for(int col=0;col<3;++col)out[row*4+col]*=.001f;
}
}
