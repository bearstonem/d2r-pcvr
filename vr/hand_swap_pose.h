#pragma once
#include "bow_pose.h"
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Left-handed play (steamvr/hand_role.h, #307): the main weapon drawn in the left hand, the off-hand
// weapon in the right. The game hangs each item on its own attach bone (R_weapon_attach,
// L_weapon_attach) and draws it from that bone's matrix as it is, so the bone is posed at the other
// wrist. Two facts shape how:
//   - the grip that is known good is the one the right-handed game shows, the attach bone relative
//     to its own wrist; the other hand's is its mirror image through the body's middle plane;
//   - a mirror image cannot be drawn: an item drawn from an attach matrix of the wrong handedness
//     (determinant sign) is inside out and culled (skeletons.cpp, bothAttach, 2026-10-06).
// So the bone is put where the mirror image has it and one of its own axes is turned over: the item
// is TURNED in the other hand, not mirrored (owner, 2026-11-09: "the asymmetric items are fine if
// they look rotated, not mirrored"). Row-vector matrices, a bone's axes in rows 0..2, as bow::Mat.
namespace skel::handswap {
// The handedness of a bone matrix: the determinant of its axes.
inline float Det(const bow::Mat& a) {
    const float* m=a.m;
    return m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
}
// One of the bone's own axes (0 x, 1 y, 2 z) turned over: the handedness changes, the origin stays.
inline void TurnOver(bow::Mat& a,int axis) {
    if(axis<0||axis>2)return;
    for(int c=0;c<3;++c)a.m[axis*4+c]=-a.m[axis*4+c];
}
// Gives m the handedness of `like` (the matrix the game itself animates that bone with) by turning
// `axis` over when they differ; true when it turned. Nothing to compare against (a zero determinant)
// leaves m alone.
inline bool MatchHandedness(bow::Mat& m,const bow::Mat& like,int axis) {
    const float a=Det(m),b=Det(like);
    if(!std::isfinite(a)||!std::isfinite(b)||a==0||b==0||(a<0)==(b<0))return false;
    TurnOver(m,axis);return true;
}
// The grip `attach` (an attach bone relative to its own wrist, bindWrist that wrist's bind matrix) as
// its mirror image has it relative to the other wrist (bindOther): the model is mirrored through its
// middle plane (x = 0, "its left is +x"), which carries one hand's bind pose onto the other's. The
// result has the opposite handedness: MatchHandedness after everything else has been applied in it
// (Weapon Adjust's offsets and turns are then mirrored with it, with no signs to keep).
inline bool MirrorGrip(const bow::Mat& attach,const bow::Mat& bindWrist,const bow::Mat& bindOther,bow::Mat& result) {
    bow::Mat inverse;
    if(!bow::Affine(attach)||!bow::Affine(bindWrist)||!bow::Inverse(bindOther,inverse))return false;
    bow::Mat mirrored=bow::Multiply(attach,bindWrist);
    for(int r=0;r<4;++r)mirrored.m[r*4]=-mirrored.m[r*4];   // x -> -x: each axis and the origin
    result=bow::Multiply(mirrored,inverse);return bow::Affine(result);
}
// Which of the attach bone's axes to turn over. A weapon runs along the bone's y (blade, shaft); held
// in a fist its flat faces the way the palm does and its edge the way the knuckles do. Turning the
// flat's axis over keeps the edge leading, so of x and z the one nearer the palm's normal is taken.
// attachBind: the attach bone in the bind pose's space (grip times the wrist's bind matrix).
inline int FlatAxis(const bow::Mat& attachBind,bow::Vec palmNormal) {
    const bow::Vec x=bow::Normalized({attachBind.m[0],attachBind.m[1],attachBind.m[2]}),z=bow::Normalized({attachBind.m[8],attachBind.m[9],attachBind.m[10]});
    const float n=bow::Length(palmNormal);
    if(!(n>1e-6f)||!std::isfinite(n))return 0;
    const float ax=std::fabs(bow::Dot(x,palmNormal)),az=std::fabs(bow::Dot(z,palmNormal));
    return std::isfinite(ax+az)&&az>ax?2:0;
}
// [hands] left_handed_turn_axis: 0 = FlatAxis (the default), 1..3 = the bone's x, y or z, for comparing.
inline int TurnAxis(int setting,int flatAxis) {return setting>=1&&setting<=3?setting-1:flatAxis==2?2:0;}

// ---------------------------------------------------------------------------------------------------
// Phase 3 (bows, crossbows, staves, spears, polearms, two hands on one weapon): the MIRROR WORLD.
//
// What these weapons are posed with is long and written for a right-handed player: the staff in the
// right fist and the left hand sliding along it, the crossbow in the right controller's frame, the
// two-hand solver, the support hand's latch, Weapon Adjust (skeletons.cpp, PoseBody). Left-handed play
// does not rewrite it. The scene it works on - the arms as posed, the controllers, the eye - is
// reflected through the model's middle plane (x = 0), which makes a left-handed player's scene a
// right-handed one; that code runs on the reflection unchanged, and what it posed is reflected back.
// So the left-handed pose is, by construction, the mirror image of the right-handed pose for the
// mirrored controllers: the weapon as far along the hand, the support hand at the same place on the
// shaft, every [weapon_<kind>] and [hands] offset meaning the same thing on the other side, with no
// signs to keep. Only an item cannot be reflected (MatchHandedness above): its attach bone gets one of
// its own axes turned over after coming back, the axis the item is most nearly symmetric across.
//
// A bone and its twin (R_... / L_..., ..._R_JNT / ..._L_JNT) have unrelated axis conventions, so a
// bone's matrix is carried across through the bind pose: Across(carrier, twin now) is this bone where
// the reflection of its twin has it - a proper (drawable) matrix whenever the twin's is. It relies on
// the bind pose's joints being mirror images of each other only; the round trip is exact regardless.
template<class M> inline bow::Mat ToBow(const M& m){bow::Mat b;std::memcpy(&b,&m,sizeof b);return b;}
template<class M> inline void FromBow(const bow::Mat& b,M& m){std::memcpy(&m,&b,sizeof b);}
// a * X: every axis and the origin with x -> -x.
inline bow::Mat MirrorX(bow::Mat a){for(int r=0;r<4;++r)a.m[r*4]=-a.m[r*4];return a;}
// A controller's turn (quaternion x y z w) and a point or direction, reflected. The reflected left
// controller is a right controller: its grip axes keep OpenXR's convention (x to the user's right in
// a natural hold), which is what X R X gives.
inline void MirrorQuat(float q[4]){q[1]=-q[1];q[2]=-q[2];}
inline void MirrorPoint(float v[3]){v[0]=-v[0];}
// The twin's name: R_x <-> L_x, x_R_y <-> x_L_y, x_R <-> x_L. A name with no side is itself.
inline std::string TwinName(std::string_view name) {
    std::string s(name);
    auto side=[](char c){return c=='R'||c=='L';};
    auto flip=[](char& c){c=c=='R'?'L':'R';};
    if(s.size()>2&&side(s[0])&&s[1]=='_'){flip(s[0]);return s;}
    for(size_t i=1;i+2<s.size();++i)if(s[i-1]=='_'&&side(s[i])&&s[i+1]=='_'){flip(s[i]);return s;}
    if(s.size()>2&&s[s.size()-2]=='_'&&side(s.back()))flip(s.back());
    return s;
}
// Every bone's twin by name (itself when it has none or the pairing is not mutual).
inline std::vector<int> Twins(const std::vector<std::string>& names) {
    std::unordered_map<std::string,int> byName;
    for(int i=int(names.size())-1;i>=0;--i)byName[names[i]]=i;   // the first bone of a name wins
    std::vector<int> twin(names.size());
    for(size_t i=0;i<names.size();++i) {
        twin[i]=int(i);
        const auto other=TwinName(names[i]);
        if(other==names[i]||byName[names[i]]!=int(i))continue;
        if(const auto it=byName.find(other);it!=byName.end())twin[i]=it->second;
    }
    for(size_t i=0;i<twin.size();++i)if(twin[size_t(twin[i])]!=int(i))twin[i]=int(i);
    return twin;
}
// What a bone does at the mirror world's edge.
enum class Cross:char {
    Across,   // the body and the arms: carried to its twin (a middle bone to itself)
    Finger,   // under a hand, with a twin: in the mirror world it keeps the right-handed animation's
              // grip on its own hand, and comes back across - the weapon hand's fingers on the left hand
    Item,     // a weapon attach bone (and anything under it): as the right-handed code seats it, then
              // reflected and turned, never carried
    Kept      // no twin, under an arm or a hand (shield_attach, a one-sided armor bone): rides its parent
};
struct MirrorRig {
    bool ok=false;
    std::vector<int> twin;
    std::vector<Cross> cross;
    std::vector<bow::Mat> carrier;   // bind * X * bind(twin)^-1
    float asymmetry=0;               // how far the wrists' bind joints are from mirror images, model units
};
// names, parents (parents before children) and the bind pose per bone; inHand: under a wrist; the arms'
// bones ([0] right, [1] left) and the two weapon attach bones.
template<class M> inline MirrorRig LearnMirror(const std::vector<std::string>& names,const std::vector<int>& parents,const std::vector<M>& bind,
        const std::vector<char>& inHand,const int upper[2],const int lower[2],const int hand[2],const int weapon[2]) {
    MirrorRig rig;const int n=int(names.size());
    if(n<=0||int(parents.size())!=n||int(bind.size())!=n||int(inHand.size())!=n)return rig;
    for(int s=0;s<2;++s)if(upper[s]<0||lower[s]<0||hand[s]<0||upper[s]>=n||lower[s]>=n||hand[s]>=n)return rig;
    for(int i=0;i<n;++i)if(parents[i]>=i)return rig;
    rig.twin=Twins(names);
    {   // the arms are twins whatever they are called
        const int pairs[3][2]={{upper[0],upper[1]},{lower[0],lower[1]},{hand[0],hand[1]}};
        for(const auto& p:pairs) {
            for(int i=0;i<n;++i)if(rig.twin[i]==p[0]||rig.twin[i]==p[1])rig.twin[i]=i;
            rig.twin[p[0]]=p[1];rig.twin[p[1]]=p[0];
        }
    }
    std::vector<char> underArm(n,0);
    rig.cross.assign(n,Cross::Across);
    for(int i=0;i<n;++i) {
        const int p=parents[i];
        if(i==upper[0]||i==upper[1]||(p>=0&&underArm[p]))underArm[i]=1;
        const bool item=i==weapon[0]||i==weapon[1]||(p>=0&&rig.cross[p]==Cross::Item);
        const bool kept=p>=0&&rig.cross[p]==Cross::Kept;
        const bool twinned=rig.twin[i]!=i;
        if(item)rig.cross[i]=Cross::Item;
        else if(kept||(!twinned&&(inHand[i]||underArm[i])))rig.cross[i]=Cross::Kept;
        else if(inHand[i])rig.cross[i]=Cross::Finger;
    }
    // a twin pair crosses the same way (a finger's twin is a finger); anything else rides its parent
    for(int i=0;i<n;++i) {
        const int t=rig.twin[i];
        if(t==i)continue;
        const bool pair=rig.cross[t]==rig.cross[i]&&(rig.cross[i]==Cross::Across||rig.cross[i]==Cross::Finger);
        if(pair)continue;
        rig.twin[i]=i;rig.twin[t]=t;
        for(const int b:{i,t})if(rig.cross[b]!=Cross::Item)rig.cross[b]=inHand[b]||underArm[b]?Cross::Kept:Cross::Across;
    }
    for(int i=0;i<n;++i) {   // what rides a kept bone is kept
        const int p=parents[i];
        if(p>=0&&rig.cross[p]==Cross::Kept&&rig.cross[i]!=Cross::Kept) {
            if(const int t=rig.twin[i];t!=i){rig.twin[t]=t;if(rig.cross[t]!=Cross::Item)rig.cross[t]=Cross::Kept;}
            rig.twin[i]=i;rig.cross[i]=Cross::Kept;
        }
    }
    rig.carrier.resize(n);
    for(int i=0;i<n;++i) {
        bow::Mat inverse;
        if(!bow::Affine(ToBow(bind[i]))||!bow::Inverse(ToBow(bind[rig.twin[i]]),inverse))return rig;
        rig.carrier[i]=bow::Multiply(MirrorX(ToBow(bind[i])),inverse);
        if(!bow::Affine(rig.carrier[i]))return rig;
    }
    const bow::Vec a=bow::Position(ToBow(bind[hand[0]])),b=bow::Position(ToBow(bind[hand[1]]));
    rig.asymmetry=bow::Length(bow::Vec{a.x+b.x,a.y-b.y,a.z-b.z});
    rig.ok=std::isfinite(rig.asymmetry);
    return rig;
}
// This bone where the reflection of its twin's matrix has it, and the same back (the two are one map).
inline bow::Mat Across(const bow::Mat& carrier,const bow::Mat& twinNow){return MirrorX(bow::Multiply(carrier,twinNow));}
// The pose (and the pose its local offsets are read from) taken into the mirror world. Bones under a
// hand keep the relation to their parent they have in `real` - the right-handed animation's.
template<class M> inline void EnterMirror(const MirrorRig& rig,const std::vector<int>& parents,const std::vector<M>& real,std::vector<M>& mirrored) {
    const int n=int(rig.twin.size());mirrored=real;
    for(int i=0;i<n;++i) {
        const int p=parents[i];
        if(rig.cross[i]==Cross::Across||p<0){FromBow(Across(rig.carrier[i],ToBow(real[rig.twin[i]])),mirrored[i]);continue;}
        bow::Mat inverse;
        if(!bow::Inverse(ToBow(real[p]),inverse))continue;
        FromBow(bow::Multiply(bow::Multiply(ToBow(real[i]),inverse),ToBow(mirrored[p])),mirrored[i]);
    }
}
// Back: what the right-handed code changed in the mirror world (`mirrored` against `entered`, the
// world as EnterMirror made it) is carried across into `real`; fingers always come back across (they
// hold the other hand's grip now); an item bone is the reflection of where it was seated, still of
// the wrong handedness (MatchHandedness next); a kept bone rides its parent as it did in `real`.
template<class M> inline void LeaveMirror(const MirrorRig& rig,const std::vector<int>& parents,const std::vector<M>& entered,
        const std::vector<M>& mirrored,std::vector<M>& real) {
    const int n=int(rig.twin.size());const std::vector<M> before=real;
    std::vector<char> moved(n,0);
    for(int i=0;i<n;++i) {
        const Cross c=rig.cross[i];
        if(c==Cross::Item){FromBow(MirrorX(ToBow(mirrored[i])),real[i]);moved[i]=1;}
        else if(c==Cross::Finger||(c==Cross::Across&&std::memcmp(&mirrored[i],&entered[i],sizeof(M))!=0)) {
            const int t=rig.twin[i];
            FromBow(Across(rig.carrier[t],ToBow(mirrored[i])),real[t]);moved[t]=1;
        }
    }
    for(int i=0;i<n;++i) {   // after the rest: a kept bone's parent may have come back from either side
        const int p=parents[i];
        if(rig.cross[i]!=Cross::Kept||p<0||!moved[p])continue;
        bow::Mat inverse;
        if(!bow::Inverse(ToBow(before[p]),inverse))continue;
        FromBow(bow::Multiply(bow::Multiply(ToBow(before[i]),inverse),ToBow(real[p])),real[i]);moved[i]=1;
    }
}
// The item's axis to turn over, of all three: the one nearest the holding palm's normal, never
// `exclude` (a shaft's own axis: turned over, the weapon would point backwards). A bow's limbs and
// arrow lie in the plane the fist closes round, a blade's flat faces the palm, a crossbow lies in the
// plane of its stock and its top: across that plane each is its own mirror image, near enough.
// attachBind as FlatAxis; a palm of no length gives the first axis allowed.
inline int SymmetryAxis(const bow::Mat& attachBind,bow::Vec palmNormal,int exclude=-1) {
    int best=exclude==0?1:0;float most=-1;
    const float n=bow::Length(palmNormal);
    if(!(n>1e-6f)||!std::isfinite(n))return best;
    for(int axis=0;axis<3;++axis) {
        if(axis==exclude)continue;
        const bow::Vec row{attachBind.m[axis*4],attachBind.m[axis*4+1],attachBind.m[axis*4+2]};
        const float length=bow::Length(row);
        if(!(length>1e-6f))continue;
        const float d=std::fabs(bow::Dot(row,palmNormal))/(length*n);
        if(std::isfinite(d)&&d>most){most=d;best=axis;}
    }
    return best;
}
// Which of a bone's own axes a direction runs along, the direction given in another frame and `toBone`
// taking that frame into the bone's (an item's model space into its attach bone's: a bow's own plane
// from its rig, bow_pose.h Rig::AcrossPlane). alignment: how nearly, 0..1. -1 when there is none.
inline int NearestAxis(const bow::Mat& toBone,bow::Vec direction,float* alignment=nullptr) {
    const bow::Vec local=bow::Vector(toBone,direction);
    const float length=bow::Length(local),parts[3]={std::fabs(local.x),std::fabs(local.y),std::fabs(local.z)};
    if(alignment)*alignment=0;
    if(!(length>1e-6f)||!std::isfinite(length))return -1;
    const int axis=parts[0]>=parts[1]&&parts[0]>=parts[2]?0:parts[1]>=parts[2]?1:2;
    if(alignment)*alignment=parts[axis]/length;
    return axis;
}
// [hands] left_handed_<kind>_turn_axis: 0 = the choice above, 1..3 = the bone's x, y or z, for comparing.
inline int ItemTurnAxis(int setting,int chosen){return setting>=1&&setting<=3?setting-1:chosen<0?0:chosen>2?2:chosen;}
// The bone as its mirror image has it, for what reads its axes as directions (a blade's line, a
// staff's casting end, a crossbow's bolt): the turned axis turned back. axis < 0: the bone as it is.
inline bow::Mat AsMirrored(bow::Mat drawn,int axis){TurnOver(drawn,axis);return drawn;}
}
