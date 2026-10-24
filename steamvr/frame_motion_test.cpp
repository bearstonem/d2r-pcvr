#include "frame_motion.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
using namespace framevr;
using namespace framevr::framemotion;

static bool Near(float a,float b,float e=1e-4f){return std::fabs(a-b)<=e;}

int main() {
    const float T=1.f;   // tan of a 45-degree half FOV either way
    const math::Fov fov{-std::atan(T),std::atan(T),-std::atan(T),std::atan(T)};
    const float upm=10,nearPlane=2;   // world units a metre; the projection's near plane
    const uint32_t size[2]{2048,2048};const float scale[2]{1,1};
    Camera back{{0,16,0},{0,0,0,1},fov,nearPlane,{0,0,0,1},{0,0,0},true};
    const auto depthAt=[&](float distance){return nearPlane/distance;};

    // Nothing moved: no motion anywhere, near or far or the sky.
    {
        const auto c=Build(back,back,Camera{},upm,1,1.6f,2,size,scale,2);
        for(float u:{.1f,.5f,.9f})for(float d:{0.f,depthAt(3),depthAt(300)}){
            const auto m=Motion(c,{u,.4f},d);assert(Near(m.x,0)&&Near(m.y,0));
        }
        assert(c.renders==0);   // no render before: none of the game's own motion
    }
    // The head turned left by 10 degrees: what is ahead now was left of the
    // middle a frame back, whatever its distance (DLSS: where it was less where it is).
    {
        const float a=10*3.14159265f/180;
        Camera now=back;now.turn=math::Yaw(a);
        const auto c=Build(now,back,Camera{},upm,1,1.6f,2,size,scale,2);
        const float expected=-std::tan(a)/(2*T);
        for(float d:{0.f,depthAt(5),depthAt(500)}){const auto m=Motion(c,{.5f,.5f},d);assert(Near(m.x,expected)&&Near(m.y,0));}
    }
    // Walked forward half a metre: the middle stays; to the side, a point 4 m
    // ahead was nearer the middle (farther away) a frame back.
    {
        Camera now=back;now.at={0,16,-5};now.hero={0,0,-5};
        const auto c=Build(now,back,Camera{},upm,1,1.6f,2,size,scale,2);
        const float D=40,s=5;   // 4 m ahead, half a metre
        auto m=Motion(c,{.5f,.5f},depthAt(D));assert(Near(m.x,0)&&Near(m.y,0));
        m=Motion(c,{.75f,.5f},depthAt(D));
        assert(Near(m.x,.25f*(D/(D+s)-1))&&Near(m.y,0));
        // What the hero carries (nearer than a metre) went along: no motion.
        m=Motion(c,{.75f,.6f},depthAt(.5f*upm));assert(Near(m.x,0)&&Near(m.y,0));
    }
    // The body turned with the stick: what is carried turned along, the world did not.
    {
        Camera now=back;now.turn=math::Yaw(.2f);now.base=math::Yaw(.2f);
        const auto c=Build(now,back,Camera{},upm,1,1.6f,2,size,scale,2);
        auto m=Motion(c,{.6f,.7f},depthAt(.5f*upm));assert(Near(m.x,0,1e-3f)&&Near(m.y,0,1e-3f));
        m=Motion(c,{.6f,.5f},depthAt(30*upm));assert(m.x<-.05f);
    }
    // A monster walking with everything still: the game's vector (uv, a
    // render's worth) is its own, twice over for the same eye's frame back.
    {
        Camera was=back;was.at={.64f,16,0};   // the other eye, 6.4 cm over
        const auto c=Build(back,back,was,upm,1,1.6f,2,size,scale,2);
        assert(c.renders==2);
        // where the other eye saw a point 10 m ahead: the game's vector there is the camera's part alone
        const auto b=Apply(c.was,math::Vec{0,0,-100});const auto seen=Project(b,c.wasTan);
        const Float2 cameraOnly{.5f-seen.x,.5f-seen.y};
        auto m=Motion(c,{.5f,.5f},depthAt(100),cameraOnly);assert(Near(m.x,0)&&Near(m.y,0));
        const Float2 walking{cameraOnly.x+.01f,cameraOnly.y};
        m=Motion(c,{.5f,.5f},depthAt(100),walking);assert(Near(m.x,-.02f)&&Near(m.y,0));
        // under a pixel and a half of its own, none; nor where its texels disagree; nor nearer than 2 m
        m=Motion(c,{.5f,.5f},depthAt(100),{cameraOnly.x+.5f/2048,cameraOnly.y});assert(Near(m.x,0));
        m=Motion(c,{.5f,.5f},depthAt(100),walking,false);assert(Near(m.x,0));
        m=Motion(c,{.5f,.5f},depthAt(1.5f*upm),walking);assert(Near(m.x,0,1e-3f));
    }
    // The head turning or the hero stepping between renders: the game's own
    // motion is not taken (its camera's part no longer comes out).
    {
        Camera was=back;was.turn=math::Yaw(.01f);
        assert(Build(back,back,was,upm,1,1.6f,2,size,scale,2).renders==0);
        was=back;was.hero={.1f,0,0};
        assert(Build(back,back,was,upm,1,1.6f,2,size,scale,2).renders==0);
        assert(TurnAngle(math::Yaw(.3f),math::Yaw(.1f))>.19f&&TurnAngle(math::Yaw(.3f),math::Yaw(.1f))<.21f);
    }
    // The pass's constants fit the root signature, and its shaders compile.
    static_assert(ConstantCount==60);
    std::string errors;
    if(!ShadersCompile(&errors)){std::fprintf(stderr,"frame motion shaders: %s\n",errors.c_str());return 1;}
    return 0;
}
