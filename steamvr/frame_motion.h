#pragma once
#include "frame_math.h"
#include "turn_warp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <d3d12.h>
#include <wrl/client.h>

// Frame generation's game motion (issue #308, step 3): OFXR Bridge makes
// every other headset frame. From optical flow it guesses how each pixel
// moved - worst for head turns in the dark, for what is near and far at once
// and for thin weapons. With its DLSS-style vector input instead it is told:
// for each pixel of an eye's image, where that point was in the same eye's
// image a frame back (in pixels; the bridge turns the rest by the poses).
//
// The mod knows that exactly for everything standing still in the world: the
// scene's depth puts each pixel back in the world (infinite reverse-Z,
// distance = near plane / depth), and the same eye's camera a frame back
// sees it there (TurnMove). What the hero carries (hands, weapon, body) went
// along with the hero's step and the body's turn (TurnCarry), as By turns
// moves it. What moves by itself - a monster walking - comes from the game's
// own vectors (the target DLSS gets: uv of the drawn part, where a point is
// less where it was a render back): less the camera's part against that
// render (the other eye's, Both eyes), what is left is the point's own,
// twice over for the same eye's frame back (two renders). Only from a
// distance on, where the surface's texels agree and above a pixel and a half
// (DLSS's jitter), and only while the head and the hero are nearly still: in
// By turns' trial (#281) the camera's part explained 98-99% of the vectors
// from 2 m out then, and much less while turning or strafing.
namespace framevr::framemotion {
// The pass's constants: rows of [R | t] taking a point of this eye's view to
// the view a frame back (the world's; what is carried), and to the game's
// render before (its vectors' camera); the tangents of each view's FOV
// (left, right, down, up); the near plane, the depth's drawn part and how
// many renders of each point's own motion the eye's frame back is; the carry
// reach (nearer: carried, farther: the world's), from how far the game's
// vectors are taken and under how many pixels they count as none; the
// depth's drawn size in texels and how far apart (pixels) one surface's
// vectors may be.
struct Constants {
    float back[12]{},carry[12]{},was[12]{};
    float nowTan[4]{},backTan[4]{},wasTan[4]{};
    float nearPlane=0,scale[2]{1,1},renders=0;
    float reach[2]{0,0},ownFrom=0,stillPixels=1.5f;
    float pixels[2]{0,0},agree=1,pad=0;
};
static_assert(sizeof(Constants)==60*4,"root constants");
inline constexpr uint32_t ConstantCount=60;

// One eye's camera for a render: where it stood and how it was turned in the
// game's world (View), its FOV, its near plane, the body's turn into the
// world (View's base) and the hero's position (TurnCarry).
struct Camera {math::Vec at{};math::Quat turn{0,0,0,1};math::Fov fov{};float nearPlane=0;math::Quat base{0,0,0,1};math::Vec hero{};bool ok=false;};

inline void Rows(float out[12],const std::array<float,12>& m){std::copy(m.begin(),m.end(),out);}
inline void Tangents(float out[4],math::Fov f){out[0]=std::tan(f.left);out[1]=std::tan(f.right);out[2]=std::tan(f.down);out[3]=std::tan(f.up);}
// The angle between two turns, radians.
inline float TurnAngle(math::Quat a,math::Quat b){
    const auto d=math::Multiply(math::Conjugate(a),b);
    return 2*std::acos(std::clamp(std::fabs(d.w),0.f,1.f));
}
inline float Distance(math::Vec a,math::Vec b){const auto d=a-b;return std::sqrt(math::Dot(d,d));}

// The game's own motion is used while the camera moved less than this
// between its renders (By turns' trial: a quarter of a degree, half a
// centimetre of the hero's step).
inline constexpr float StillTurn=.25f*3.14159265f/180,StillStep=.005f;
// now: this render's camera; back: the same eye's a frame back; was: the
// game's render before this one (its vectors' camera; ok false: no vectors);
// upm: the world's units a metre; carried/world: the carry reach in metres;
// ownFrom: the game's vectors from this many metres out; depthSize: the
// depth's size in texels, scale its drawn part; renders: renders between the
// same eye's frames.
inline Constants Build(const Camera& now,const Camera& back,const Camera& was,float upm,float carried,float world,float ownFrom,
                       const uint32_t depthSize[2],const float scale[2],float renders) {
    Constants c;
    Rows(c.back,TurnMove(now.at,now.turn,back.at,back.turn));
    // the hero's step and the body's turn since, undone: the carried point
    // where it was in the eye a frame back
    Rows(c.carry,TurnCarry(now.at,now.turn,now.hero,now.base,back.hero,back.base,back.at,back.turn));
    Tangents(c.nowTan,now.fov);Tangents(c.backTan,back.fov);
    c.nearPlane=now.nearPlane;c.scale[0]=scale[0];c.scale[1]=scale[1];
    c.reach[0]=carried*upm;c.reach[1]=world*upm;c.ownFrom=ownFrom*upm;
    c.pixels[0]=float(depthSize[0])*scale[0];c.pixels[1]=float(depthSize[1])*scale[1];
    const bool still=was.ok&&TurnAngle(now.turn,was.turn)<StillTurn&&Distance(now.hero,was.hero)<StillStep*upm;
    if(still) {
        Rows(c.was,TurnMove(now.at,now.turn,was.at,was.turn));Tangents(c.wasTan,was.fov);
        c.renders=renders;
    }
    return c;
}

// The pass as the GPU runs it, for one point: uv this image's (0..1, v
// down), depth its scene depth (0: as far as can be), game the game's vector
// there (uv units), agree whether its surface's texels agree. Returns where
// it was in the same eye's image a frame back less where it is now (uv).
struct Float2 {float x=0,y=0;};
inline math::Vec Apply(const float r[12],math::Vec p,float w=1){
    return {r[0]*p.x+r[1]*p.y+r[2]*p.z+r[3]*w,r[4]*p.x+r[5]*p.y+r[6]*p.z+r[7]*w,r[8]*p.x+r[9]*p.y+r[10]*p.z+r[11]*w};
}
inline Float2 Project(math::Vec p,const float t[4]){return {(-p.x/p.z-t[0])/(t[1]-t[0]),(t[3]+p.y/p.z)/(t[3]-t[2])};}
inline Float2 Motion(const Constants& c,Float2 uv,float depth,Float2 game={},bool agree=true) {
    const math::Vec ray{c.nowTan[0]+uv.x*(c.nowTan[1]-c.nowTan[0]),c.nowTan[3]+uv.y*(c.nowTan[2]-c.nowTan[3]),-1};
    if(depth<=0) {   // the sky: turned only
        const auto q=Apply(c.back,ray,0);
        if(q.z>=0)return {};
        const auto p=Project(q,c.backTan);return {p.x-uv.x,p.y-uv.y};
    }
    const float away=c.nearPlane/depth;
    const auto point=ray*away;
    const float free=std::clamp((away-c.reach[0])/std::max(c.reach[1]-c.reach[0],1e-6f),0.f,1.f);
    const auto w=Apply(c.back,point),h=Apply(c.carry,point);
    const math::Vec q=h+(w-h)*free;
    if(q.z>=0)return {};
    const auto p=Project(q,c.backTan);
    Float2 m{p.x-uv.x,p.y-uv.y};
    if(c.renders>0&&away>c.ownFrom&&agree) {
        const auto b=Apply(c.was,point);
        if(b.z<0) {
            const auto seen=Project(b,c.wasTan);
            const Float2 own{game.x-(uv.x-seen.x),game.y-(uv.y-seen.y)};
            if(std::hypot(own.x*c.pixels[0],own.y*c.pixels[1])>=c.stillPixels){m.x-=own.x*c.renders*free;m.y-=own.y*c.renders*free;}
        }
    }
    return m;
}

// Whether the pass's shaders compile (with d3dcompiler_47); the first
// failure's messages into `errors`.
bool ShadersCompile(std::string* errors=nullptr);
// The GPU pass: each eye's vectors (R16G16_FLOAT, half the depth's drawn
// size, uv units) from its depth and, with renders > 0, the game's vectors.
class Pass {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sources,targets;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,2> vectors;
    std::array<uint32_t,2> width{},height{};
public:
    bool Initialize(ID3D12Device*,void (*log)(const char*));
    bool Ready() const {return pipeline!=nullptr;}
    // depth: readable (`depthState`), `depthFormat` its shader format; game:
    // the game's vectors in `gameState` (null: none); both left as they were.
    // Leaves the eye's vectors in PIXEL_SHADER_RESOURCE. Recorded after the
    // eye's last use completed (Copy waits for it): its descriptors are free.
    bool Render(ID3D12Device*,ID3D12GraphicsCommandList*,int eye,ID3D12Resource* depth,DXGI_FORMAT depthFormat,D3D12_RESOURCE_STATES depthState,
                ID3D12Resource* game,D3D12_RESOURCE_STATES gameState,const Constants&);
    // The eye's vectors to none (the bridge turns the image by the poses
    // alone), once there are any; false before the first Render.
    bool Clear(ID3D12Device*,ID3D12GraphicsCommandList*,int eye);
    ID3D12Resource* Vectors(int eye) const {return eye==0||eye==1?vectors[size_t(eye)].Get():nullptr;}
    uint32_t Width(int eye) const {return width[size_t(eye&1)];}
    uint32_t Height(int eye) const {return height[size_t(eye&1)];}
};
}
