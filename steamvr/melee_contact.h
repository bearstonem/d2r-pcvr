#pragma once
#include "gameplay.h"
#include <array>

namespace framevr::melee {
using math::Vec;using gameplay::Pose;using gameplay::Length;
struct Blade {Vec base{},tip{};float radius=.04f;};
struct Body {Vec bottom{},top{};float radius=.3f;};
inline float SegmentDistanceSquared(Vec p,Vec q,Vec a,Vec b) {
    const Vec d=q-p,e=b-a,r=p-a;
    const float dd=math::Dot(d,d),ee=math::Dot(e,e),de=math::Dot(d,e),dr=math::Dot(d,r),er=math::Dot(e,r);
    float s=0,t=0;
    if(dd<1e-8f&&ee<1e-8f)return math::Dot(r,r);
    if(dd<1e-8f)t=std::clamp(er/ee,0.f,1.f);
    else if(ee<1e-8f)s=std::clamp(-dr/dd,0.f,1.f);
    else {
        const float denominator=dd*ee-de*de;
        if(denominator>1e-8f)s=std::clamp((de*er-dr*ee)/denominator,0.f,1.f);
        t=(de*s+er)/ee;
        if(t<0){t=0;s=std::clamp(-dr/dd,0.f,1.f);}
        else if(t>1){t=1;s=std::clamp((de-dr)/dd,0.f,1.f);}
    }
    const Vec delta=r+d*s-e*t;return math::Dot(delta,delta);
}
// Adaptive bounded sweep. Inflation by half a sample's endpoint travel closes
// gaps between samples (including a fast blade passing entirely through a body).
// This is a conservative capsule test, not a triangle-mesh collision claim.
inline bool Touches(Blade before,Blade after,Body body) {
    const Vec a=before.tip-before.base,b=after.tip-after.base;
    const float la=Length(a),lb=Length(b);
    const float angle=la>.001f&&lb>.001f?std::acos(std::clamp(math::Dot(a,b)/(la*lb),-1.f,1.f)):0;
    // Interpolating the two tips along a chord shortens the blade halfway
    // through a fast slash and misses targets on its outer arc. Keep its
    // length while rotating between the observed directions. An exactly
    // opposite pair has no known rotation plane; retain the chord fallback.
    const bool arc=angle>.001f&&angle<3.13f;
    const float travel=arc?Length(after.base-before.base)+std::fabs(lb-la)+std::max(la,lb)*angle:
        std::max(Length(after.base-before.base),Length(after.tip-before.tip));
    const int steps=std::clamp(int(std::ceil(travel/.03f)),1,64);
    const float radius=body.radius+std::max(before.radius,after.radius)+travel/(2*steps);
    const float sinAngle=arc?std::sin(angle):1;
    for(int i=0;i<=steps;++i) {
        const float t=float(i)/steps;
        const Vec base=before.base+(after.base-before.base)*t;
        const Vec tip=arc?base+(a*(std::sin((1-t)*angle)/(la*sinAngle))+b*(std::sin(t*angle)/(lb*sinAngle)))*(la+(lb-la)*t):
            before.tip+(after.tip-before.tip)*t;
        if(SegmentDistanceSquared(base,tip,body.bottom,body.top)<=radius*radius)return true;
    }
    return false;
}
struct Sample {
    double time=0;
    bool active=false;
    uint64_t epoch=0;uint32_t equipment=0;
    Pose hand{}; // absolute tracking-space pose; camera movement cannot power a hit
    Blade blade{}; // world metres (game coordinates divided by unitsPerMetre)
    Vec player{};float unitsPerMetre=1;
    bool shield=false;
    float minWeaponSpeed=12.f;
    int skillSlot=-1,skillOwner=-1;
    uint64_t skillContext=0;
};
// The renderer may finish posing an older input after a newer XR sample has
// arrived. Match the blade to the hand that actually posed it, never to the
// latest hand velocity. Calls are serialized by the transport's input mutex.
class PoseHistory {
    struct Entry {uint32_t stamp=0;Sample sample{};};
    std::array<Entry,16> entries{};unsigned next=0,count=0;
public:
    void Clear(){*this=PoseHistory{};}
    void Push(uint32_t stamp,const Sample& sample) {
        entries[next]={stamp,sample};next=(next+1)%entries.size();
        count=std::min(count+1,unsigned(entries.size()));
    }
    bool Find(uint32_t stamp,double now,Sample& out) const {
        if(!count||!std::isfinite(now))return false;
        const auto& current=entries[(next+entries.size()-1)%entries.size()].sample;
        if(!current.active||now<current.time||now-current.time>.1)return false;
        for(unsigned age=0;age<count;++age) {
            const auto& e=entries[(next+entries.size()-1-age)%entries.size()];
            if(e.stamp!=stamp)continue;
            if(!e.sample.active||e.sample.epoch!=current.epoch||e.sample.equipment!=current.equipment||
               e.sample.shield!=current.shield||e.sample.skillSlot!=current.skillSlot||e.sample.skillOwner!=current.skillOwner||e.sample.skillContext!=current.skillContext||
               now<e.sample.time||now-e.sample.time>.1)return false;
            out=e.sample;return true;
        }
        return false;
    }
};
struct Sweep {Blade before{},after{};uint64_t stroke=0;float speed=0;};
struct Diagnostics {
    unsigned discontinuities=0,recovering=0,invalidPose=0,belowSpeed=0,onsetRejected=0;
    float peakSpeed=0;
};
inline constexpr float WeaponStrokeTravel=.18f;
inline float WeaponContactSpeed(float value) {
    return std::isfinite(value)?std::max(value,1.5f):12.f;
}
class Engine {
    bool primed=false;
    Sample previous{};
    double quietSince=0,strokeAt=0,recoverUntil=0;
    Vec direction{},angularDirection{};
    uint64_t serial=0,stroke=0;
    Diagnostics diagnostics{};
    struct Point {Pose hand;Blade blade;double time=0;float distance=0;};
    std::array<Point,32> recent{};unsigned next=0,points=0;
    void Remember(const Sample& s,float distance=0) {
        recent[next]={s.hand,s.blade,s.time,distance};next=(next+1)%recent.size();
        points=std::min(points+1,unsigned(recent.size()));
    }
    void RestartPath(const Sample& s){next=points=0;Remember(s);}
    static float SwingAngle(const Pose& before,const Blade& beforeBlade,const Sample& after) {
        const float handAngle=gameplay::RotationDistance(before.q,after.hand.q);
        if(after.shield)return handAngle; // preserve the tested shield gesture
        const Vec a=beforeBlade.tip-beforeBlade.base,b=after.blade.tip-after.blade.base;
        const float lengths=Length(a)*Length(b);
        if(lengths<1e-8f)return 0;
        // Rolling around the shaft does not swing its collision segment.
        // World/camera turns alone cannot supply energy: cap the geometric
        // axis change by rotation measured in tracking space.
        const float bladeAngle=std::acos(std::clamp(math::Dot(a,b)/lengths,-1.f,1.f));
        return std::min(handAngle,bladeAngle);
    }
    bool Onset(const Sample& s,float lever,Blade& from) const {
        float travel=0;
        for(unsigned back=1;back<points;++back) {
            travel+=recent[(next+recent.size()-back)%recent.size()].distance;
            const auto& p=recent[(next+recent.size()-back-1)%recent.size()];
            if(s.time-p.time>.12)break;
            const float excursion=Length(s.hand.p-p.hand.p)+lever*SwingAngle(p.hand,p.blade,s);
            if(excursion>=(s.shield?.08f:WeaponStrokeTravel)&&excursion>=travel*.60f){from=p.blade;return true;}
        }
        return false;
    }
public:
    void Reset(){const auto s=serial;const auto d=diagnostics;*this=Engine{};serial=s;diagnostics=d;}
    Diagnostics TakeDiagnostics(){const auto d=diagnostics;diagnostics={};return d;}
    bool Step(const Sample& in,Sweep& out) {
        out={};
        if(!in.active||!gameplay::Valid(in.hand)||!std::isfinite(in.time)||
           !gameplay::Finite(in.blade.base)||!gameplay::Finite(in.blade.tip)||!gameplay::Finite(in.player)||
           !std::isfinite(in.unitsPerMetre)||in.unitsPerMetre<1||in.unitsPerMetre>12||Length(in.blade.base-in.player)>2.8f||Length(in.blade.tip-in.player)>4.5f||
           !std::isfinite(in.blade.radius)||in.blade.radius<=0||in.blade.radius>.2f) {
            if(in.active)++diagnostics.invalidPose;
            Reset();return false;
        }
        if(primed&&in.time==previous.time)return false;
        const double dt=in.time-previous.time;
        const float angle=gameplay::RotationDistance(in.hand.q,previous.hand.q);
        const float lever=std::min(1.5f,Length(in.blade.tip-in.blade.base));
        const float handTravel=Length(in.hand.p-previous.hand.p),playerTravel=Length(in.player-previous.player);
        const float bladeTravel=std::max(Length(in.blade.base-previous.blade.base),Length(in.blade.tip-previous.blade.tip));
        if(!primed||dt<=0||dt>.1||in.epoch!=previous.epoch||in.equipment!=previous.equipment||
           in.shield!=previous.shield||in.minWeaponSpeed!=previous.minWeaponSpeed||in.skillSlot!=previous.skillSlot||in.skillOwner!=previous.skillOwner||in.skillContext!=previous.skillContext||
           handTravel>std::max(.12f,10.f*float(dt))||angle>gameplay::MeleeAngularTrackingLimit*dt||playerTravel>.5f||
           std::fabs(in.unitsPerMetre-previous.unitsPerMetre)>.05f||
           bladeTravel>std::max(1.2f,handTravel+angle*lever+playerTravel+.1f)) {
            if(primed)++diagnostics.discontinuities;
            Reset();primed=true;previous=in;RestartPath(in);
            recoverUntil=in.time+.08;return false;
        }
        const Sample was=previous;previous=in;
        // Discard the discontinuity and require a short run of fresh poses,
        // rather than waiting indefinitely for an almost motionless hand.
        // Real slashes at ~44 Hz crossed the old 35 rad/s / 18 m/s guards;
        // continued combat then never satisfied the quiet-hand rearm.
        if(in.time<recoverUntil) {++diagnostics.recovering;RestartPath(in);return false;}
        const Vec delta=in.hand.p-was.hand.p;
        const float distance=Length(delta)+SwingAngle(was.hand,was.blade,in)*lever,speed=distance/float(dt);
        diagnostics.peakSpeed=std::max(diagnostics.peakSpeed,speed);
        if(speed<.45f) {
            if(!quietSince)quietSince=in.time;
            Remember(in,distance);
            if(in.time-quietSince>=.08) {stroke=0;RestartPath(in);direction={};angularDirection={};}
            return false;
        }
        quietSince=0;
        // Translation AND rotation can reverse a stroke. Wrist-led backhands
        // otherwise kept the first stroke id forever while the wrist stayed
        // nearly in place, suppressing later hits against the same creature.
        const auto dq=math::Multiply(in.hand.q,math::Conjugate(was.hand.q));
        const Vec angular=math::Normalized(Vec{dq.x,dq.y,dq.z}*(dq.w<0?-1.f:1.f));
        const bool reversed=(Length(delta)>.008f&&math::Dot(math::Normalized(delta),direction)<-.55f)||
                            (angle>.025f&&math::Dot(angular,angularDirection)<-.55f);
        if(stroke&&in.time-strokeAt>.18&&reversed) {
            stroke=0;RestartPath(was);direction={};angularDirection={};
        }
        Remember(in,distance);
        const bool beginning=!stroke;
        Blade startBlade{};
        if(beginning) {
            // Judge this stroke's recent motion. Accumulating all jitter and
            // aborted windups since the last pause made its net/travel ratio
            // impossible to satisfy, causing another indefinite lockout.
            if(speed<(in.shield?1.2f:WeaponContactSpeed(in.minWeaponSpeed))){++diagnostics.belowSpeed;return false;}
            if(!Onset(in,lever,startBlade)){++diagnostics.onsetRejected;return false;}
            stroke=++serial;strokeAt=in.time;direction=math::Normalized(delta);angularDirection=angular;
        }
        // No delayed taps from holding the weapon inside an enemy, and no
        // slowly walking/turning the camera into one with a stationary hand.
        if(speed<(in.shield?.8f:WeaponContactSpeed(in.minWeaponSpeed)))return false;
        // Weapon motion history arms a stroke; it must not retroactively
        // damage something brushed during the earlier low-energy approach.
        // Keep the latest swept segment so a fast between-frame hit survives.
        out={beginning&&in.shield?startBlade:was.blade,in.blade,stroke,speed};return true;
    }
};
class HitSet {
    uint64_t epoch=0;
    struct Entry {uint64_t stroke=0;uint32_t id=0;};
    std::array<Entry,64> entries{};unsigned next=0;
public:
    bool Contains(uint64_t session,uint64_t stroke,uint32_t id) const {
        if(epoch!=session)return false;
        for(const auto& e:entries)if(e.stroke==stroke&&e.id==id)return true;
        return false;
    }
    bool Insert(uint64_t session,uint64_t stroke,uint32_t id) {
        if(epoch!=session){entries={};next=0;epoch=session;}
        if(Contains(session,stroke,id))return false;
        entries[next++%entries.size()]={stroke,id};return true;
    }
};
}
