#pragma once
#include "../vr/bow_pose.h"
#include "world_coordinates.h"
#include "bow_flight.h"
#include "attack_speed.h"
#include <cstdint>

namespace framevr::crossbow {
using math::Vec;
using skel::bow::Mat;
using skel::bow::Vector;
using skel::bow::Position;
using skel::bow::Multiply;
inline constexpr float BoltTip=.69698172f; // owned x_bow_bolt mesh, local +Z
inline constexpr float SupportDrop=.07f; // palm below the bolt axis, metres
struct FireRate {
    uint64_t session=0,at=0;uint32_t equipment=0,milliseconds=0;
    uint32_t Cycle(uint64_t owner,uint32_t gear,uint64_t now) const {
        return session==owner&&equipment==gear&&now>=at&&now-at<=1500&&milliseconds?
            milliseconds:1000; // unknown/stale class data must never mean unlimited fire
    }
};
struct Line {Vec center{},direction{},up{};float scale=0;};
inline bool FromBolt(const Mat& bolt,Line& out) {
    Mat basis=bolt;Position(basis,{});const Vec center=Position(bolt);
    if(!skel::bow::Affine(basis)||!std::isfinite(center.x+center.y+center.z))return false;
    const Vec z=Vector(bolt,{0,0,1}),y=Vector(bolt,{0,1,0}),x=Vector(bolt,{1,0,0});
    const float scale=std::sqrt(math::Dot(z,z));
    if(!std::isfinite(scale)||scale<.01f||scale>20)return false;
    const Vec direction=math::Normalized(z),up=math::Normalized(y-direction*math::Dot(y,direction));
    if(math::Dot(up,up)<.99f||std::fabs(math::Dot(x,math::Cross(y,z)))<1e-8f)return false;
    out={center,direction,up,scale};return true;
}
// Use the front of the stock, where the two bow limbs join, projected onto
// the authored bolt axis. A helper's bind position is the unloaded bolt's
// tail, not the centre of the flying projectile or the loaded muzzle.
inline bool BarrelInCog(const Mat& inverseCog,const Mat& inverseHelper,const Mat& inverseLeft,
                        const Mat& inverseRight,Mat& out) {
    Mat helper,left,right;
    if(!skel::bow::Affine(inverseCog)||!skel::bow::Inverse(inverseHelper,helper)||
       !skel::bow::Inverse(inverseLeft,left)||!skel::bow::Inverse(inverseRight,right))return false;
    Line line;if(!FromBolt(helper,line))return false;
    const Vec front=(Position(left)+Position(right))*.5f;
    Position(helper,line.center+line.direction*math::Dot(front-line.center,line.direction));
    out=Multiply(helper,inverseCog);return skel::bow::Affine(out);
}
inline Vec Muzzle(const Line& line){return line.center;}
// The renderer can apply attachment overrides between the item and wrist.
// Learn that complete relationship from the rendered item, then reapply it to
// each new wrist pose. Subtract the world anchor before inversion: town/world
// coordinates can exceed the bounded local-matrix validator's range.
inline bool RenderedGeometry(const Mat& boltInItem,const Mat& itemWorld,const Mat& wristWorld,
                             Mat& boltWorld,Mat& boltInWrist) {
    Mat relativeItem=itemWorld,wrist=wristWorld,inverse;
    Position(relativeItem,Position(itemWorld)-Position(wristWorld));Position(wrist,{});
    if(!skel::bow::Affine(boltInItem)||!skel::bow::Affine(relativeItem)||!skel::bow::Inverse(wrist,inverse))return false;
    const Mat local=Multiply(Multiply(boltInItem,relativeItem),inverse);
    const Mat world=Multiply(boltInItem,itemWorld);Line line;
    if(!skel::bow::Affine(local)||!FromBolt(world,line))return false;
    boltWorld=world;boltInWrist=local;return true;
}
inline bool SupportLatched(bool held,bool pressedBefore,bool latched,bool inReach,bool crossbow) {
    return held&&(latched||((crossbow||!pressedBefore)&&inReach));
}
// The support palm is under the actual stock, not on a separately rotated
// controller ray. Project the driving palm onto that line before advancing.
inline Vec Support(const Line& line,Vec rightPalm,float ahead,float unitsPerMetre) {
    const float along=std::min(math::Dot(rightPalm-line.center,line.direction)+ahead*unitsPerMetre,-.06f*unitsPerMetre);
    return line.center+line.direction*along-line.up*(SupportDrop*unitsPerMetre);
}
struct Pose {
    Vec muzzle{},direction{},player{};
    float scale=0;
    uint64_t at=0,session=0;
    uint32_t equipment=0,worldEpoch=0,heroGeneration=0;
    float unitsPerMetre=0;
};
inline bool Fresh(const Pose& p,uint64_t now,uint64_t session,uint32_t equipment,uint32_t epoch) {
    return p.session&&p.at&&now>=p.at&&now-p.at<=100&&p.session==session&&p.equipment==equipment&&p.worldEpoch==epoch&&
        std::isfinite(p.muzzle.x+p.muzzle.y+p.muzzle.z+p.player.x+p.player.y+p.player.z+p.scale)&&
        p.scale>.01f&&p.scale<20&&math::Dot(p.direction,p.direction)>.99f&&math::Dot(p.direction,p.direction)<1.01f;
}
struct Launch {Pose pose{};int x=0,y=0,tx=0,ty=0;bowflight::Shot flight{};};
inline bool Aim(const Pose& pose,Launch& out) {
    const Vec grid=coordinates::GridPlane({pose.muzzle.x,pose.player.y,pose.muzzle.z});
    const float horizontal=std::hypot(pose.direction.x,pose.direction.z);
    if(!std::isfinite(grid.x+grid.z+horizontal)||grid.x<2||grid.z<2||grid.x>131000||grid.z>131000)return false;
    Launch shot;shot.pose=pose;shot.x=int(grid.x*.5f);shot.y=int(grid.z*.5f);
    // The native path owns damage/lifetime only; a vertical shot still needs
    // a valid nonzero native target, while physical flight retains full aim.
    shot.tx=shot.x+(horizontal>.001f?int(pose.direction.x/horizontal*60):0);
    shot.ty=shot.y+(horizontal>.001f?int(pose.direction.z/horizontal*60):1);
    if(shot.tx<1||shot.ty<1||shot.tx>65534||shot.ty>65534)return false;
    out=shot;return true;
}
inline bool Physical(Launch& launch,uint64_t epoch,uint64_t sequence) {
    const auto& p=launch.pose;bowflight::Shot s;
    s.kind=bowflight::Kind::Crossbow;s.unitsPerMetre=p.unitsPerMetre;s.draw=1;
    if(!std::isfinite(p.unitsPerMetre)||p.unitsPerMetre<1||p.unitsPerMetre>12)return false;
    s.boltLength=2*BoltTip*p.scale/p.unitsPerMetre;
    s.direction=p.direction;s.origin=p.muzzle*(1/p.unitsPerMetre)-s.direction*s.boltLength;
    s.player=p.player*(1/p.unitsPerMetre);s.session=p.session;s.epoch=epoch;s.sequence=sequence;
    s.at=p.at;s.equipment=p.equipment;s.planar=false;
    if(!bowflight::Valid(s))return false;
    launch.flight=s;return true;
}
// Support the stock without rotating a cupped palm through a fist alignment.
inline Mat SupportHand(Mat tracked,Vec palmLocal,Vec contact) {
    Position(tracked,contact-Vector(tracked,palmLocal));return tracked;
}
}
