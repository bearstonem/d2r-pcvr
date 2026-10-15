#pragma once
#include "frame_math.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Blood on the visor. Value-only: the transport supplies the published life
// snapshot and tracked poses; nothing here reads native memory or XR state.
//
// Life falling into a worse band splashes that band's blood. Healing back
// above a band, from any source, fades that band's splats out. With the face
// wipe gesture on, scrubbing a hand side to side in front of the face wipes
// blood at any time; while still hurt the visor then stays clean until the
// next hit splashes the band back.
// Splatter artwork is placed around the rim, worst wounds nearer the centre;
// large blobs grow slow drips that run down and dry while the splat remains.
namespace framevr::blood {
struct Band {float below,coverage;};
// Worst first. Life fraction below `below` leaves at least `coverage`.
inline constexpr std::array<Band,3> Bands{{{.20f,1.f},{.40f,.65f},{.60f,.35f}}};
constexpr float RiseRate=4;         // coverage per second while a new wound spreads
constexpr float SplashRate=8;       // opacity per second as one splat lands
constexpr float HealFadeSeconds=1,WipeFadeSeconds=.2f;
constexpr float HitFraction=.005f;  // life lost between readings that counts as a new hit
constexpr float WipePerStroke=.18f; // a full visor clears in six strokes
// Head-local face volume (OpenXR view: +X right, +Y up, -Z forward). Grip
// poses sit in the palm, several centimetres in front of the visor shell.
constexpr float FaceHalfWidth=.18f,FaceTop=.10f,FaceBottom=-.22f,FaceNear=.02f,FaceFar=-.32f;
constexpr float Reverse=.025f;      // hysteresis before a turn counts
constexpr float MinStroke=.06f;     // lateral travel between turns
constexpr float MaxOffAxis=.8f;     // vertical/depth travel relative to lateral
constexpr float MaxStrokeSpeed=2.5f;// m/s; faster passes are weapon swings
constexpr double MaxStrokeSeconds=.75,EngagedSeconds=.6;

// Unknown or dead: -1 holds the current blood.
inline float Target(float health,float maxHealth) {
    if(!std::isfinite(health)||!std::isfinite(maxHealth)||health<=0||maxHealth<=0)return -1;
    const float fraction=health/maxHealth;
    for(const auto& band:Bands)if(fraction<band.below)return band.coverage;
    return 0;
}
inline bool InFace(math::Vec p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
        std::fabs(p.x)<=FaceHalfWidth&&p.y<=FaceTop&&p.y>=FaceBottom&&p.z<=FaceNear&&p.z>=FaceFar;
}

// One hand. A stroke is the lateral travel between two turns; the entry
// point counts as the first turn. Pausing, drifting, leaving the face or a
// fast weapon-speed pass restarts tracking.
class Scrub {
    math::Vec anchor{},peak{};
    double anchorAt=0,peakAt=0,strokeAt=-1;
    int direction=0;
    bool tracking=false;
public:
    void Reset(){*this=Scrub{};}
    bool Engaged(double now) const {return strokeAt>=0&&now>=strokeAt&&now-strokeAt<=EngagedSeconds;}
    bool Step(math::Vec p,double now,bool allowed) {
        if(!allowed||!InFace(p)||!std::isfinite(now)){Reset();return false;}
        if(!tracking){tracking=true;anchor=peak=p;anchorAt=peakAt=now;return false;}
        if(direction==0) {
            if(std::fabs(p.x-anchor.x)>=Reverse){direction=p.x>anchor.x?1:-1;peak=p;peakAt=now;}
            else if(now-anchorAt>MaxStrokeSeconds){anchor=peak=p;anchorAt=peakAt=now;}
            return false;
        }
        if((p.x-peak.x)*direction>=0){peak=p;peakAt=now;return false;}
        if(now-peakAt>MaxStrokeSeconds){direction=0;anchor=peak=p;anchorAt=peakAt=now;return false;}
        if((peak.x-p.x)*direction<Reverse)return false;
        const float dx=std::fabs(peak.x-anchor.x),off=std::hypot(peak.y-anchor.y,peak.z-anchor.z);
        const double dt=peakAt-anchorAt;
        const bool valid=dx>=MinStroke&&off<=MaxOffAxis*dx&&dt>0&&dt<=MaxStrokeSeconds&&dx/dt<=MaxStrokeSpeed;
        anchor=peak;anchorAt=peakAt;direction=-direction;peak=p;peakAt=now;
        if(valid)strokeAt=now;
        return valid;
    }
};

// Visor texture: one square image stretched over the field of view.
constexpr int Size=1024;
// Splatter art: SpriteCount square cells side by side (assets/blood-splatters.png).
constexpr int SpriteCount=8,Cell=384,StripWidth=SpriteCount*Cell,StripHeight=Cell;
constexpr int Placements=16;
// bearing: degrees, 0 = right, 90 = down. radius: 1 = the image edge.
// Splats appear in threshold order as coverage rises and wipe in reverse.
struct Placement {int sprite;float bearing,radius;int size;float rotation;bool flip;float threshold;};
inline constexpr std::array<Placement,Placements> Layout{{
    {0,225,.86f,430, 15,false,.06f},{3, 45,.86f,450,160,true ,.12f},{6,135,.86f,410,-30,false,.18f},
    {1,315,.86f,440,200,true ,.25f},{4,270,.88f,390, 90,false,.32f},{5,180,.90f,370,-80,true ,.40f},
    {2,  0,.90f,390,100,false,.47f},{7, 90,.88f,410,  0,true ,.54f},{0,295,.66f,350,250,true ,.60f},
    {1,115,.66f,370, 20,false,.65f},{4,205,.62f,330, 40,true ,.72f},{5, 25,.62f,330,140,false,.78f},
    {3,245,.60f,310,-60,false,.84f},{6,335,.60f,320,210,true ,.89f},{2,155,.60f,320, 70,false,.94f},
    {7, 65,.58f,300,300,true ,1.f},
}};
inline uint32_t VisibleFor(float coverage) {
    uint32_t mask=0;
    for(int i=0;i<Placements;++i)if(std::isfinite(coverage)&&coverage>=Layout[i].threshold-1e-4f)mask|=1u<<i;
    return mask;
}

struct Input {
    double time=0;
    uint64_t session=0,epoch=0;
    bool enabled=false,active=false,fresh=false,vitalsKnown=false;
    bool wipe=true;                  // face wipe gesture setting
    float health=0,maxHealth=0;
    math::Vec head{};math::Quat headRotation{0,0,0,1};
    math::Vec hand[2]{};
    bool tracked[2]{},busy[2]{}; // busy: bottle held, trigger squeezed, bow drawn
};
struct Output {
    float coverage=0,band=0;
    uint32_t visible=0;                     // splats with any opacity
    std::array<float,Placements> opacity{}; // 0..1 per splat
    std::array<float,Placements> age{};     // seconds each visible splat has been on the visor
    bool fading=false;                      // a splat is landing or fading: repaint smoothly
    bool stroke[2]{},block[2]{};
    bool clean=false;                       // a scrub removed the last blood
    bool healed=false;                      // life crossed back above a band this step
};
class Engine {
    uint64_t session=0,epoch=0;
    double last=0;
    float coverage=0,band=0,health=-1;      // band and health: last fresh reading
    bool rising=false;
    Scrub scrub[2];
    std::array<float,Placements> opacity{},fadeRate{};
    std::array<bool,Placements> on{};
    std::array<double,Placements> shownAt=[]{std::array<double,Placements> a{};a.fill(-1);return a;}();
public:
    void Reset(){*this=Engine{};}
    Output Step(const Input& in) {
        Output out;
        if(!in.enabled||!std::isfinite(in.time)){Reset();return out;}
        // Loading screens publish no player; keep the stain through them.
        if(in.session&&in.session!=session){const auto keep=last;Reset();session=in.session;last=keep;}
        const double dt=in.time-last;
        const bool continuous=last>0&&dt>0&&dt<=.15&&in.epoch==epoch;
        last=in.time;epoch=in.epoch;
        float fadeOut=1/HealFadeSeconds;
        if(in.fresh&&in.vitalsKnown) {
            const float target=Target(in.health,in.maxHealth);
            if(target>=0) {
                // Only current life moves blood. A max-life change (Battle Orders,
                // Oak Sage, +life gear, or one expiring) shifts the band without
                // splashing or fading; the next hit or heal acts on the new band.
                const bool fell=health<0||in.health<health,rose=health>=0&&in.health>health;
                const bool hit=health>=0&&(health-in.health)>=HitFraction*in.maxHealth;
                if((target>band&&fell)||(hit&&target>0))rising=true;
                if(target<band&&rose&&coverage>target){coverage=target;out.healed=true;}
                if(coverage>=target)rising=false;
                band=target;health=in.health;
            }
        }
        if(rising&&coverage<band) {
            coverage=continuous?std::min(band,coverage+float(dt)*RiseRate):band;
            if(coverage>=band)rising=false;
        }
        const auto inverse=math::Conjugate(in.headRotation);
        for(int h=0;h<2;++h) {
            // Gesture off, or nothing to wipe: scrubbing neither wipes nor claims combat.
            const bool allowed=continuous&&in.wipe&&in.active&&in.tracked[h]&&!in.busy[h]&&coverage>0;
            const auto local=math::Rotate(inverse,in.hand[h]-in.head);
            if(scrub[h].Step(local,in.time,allowed)) {
                coverage=std::max(0.f,coverage-WipePerStroke);rising=false;
                out.stroke[h]=true;out.clean=out.clean||coverage<=0;fadeOut=1/WipeFadeSeconds;
            }
            out.block[h]=allowed&&scrub[h].Engaged(in.time);
        }
        const float step=continuous?float(dt):0;
        for(int i=0;i<Placements;++i) {
            const bool now=std::isfinite(coverage)&&coverage>=Layout[i].threshold-1e-4f;
            if(on[i]&&!now)fadeRate[i]=fadeOut;
            on[i]=now;
            opacity[i]=now?std::min(1.f,opacity[i]+step*SplashRate):std::max(0.f,opacity[i]-step*fadeRate[i]);
            if(opacity[i]<=0){shownAt[i]=-1;continue;}
            if(shownAt[i]<0||shownAt[i]>in.time)shownAt[i]=in.time; // a wiped splat returns fresh
            out.visible|=1u<<i;out.opacity[i]=opacity[i];out.age[i]=float(in.time-shownAt[i]);
            out.fading=out.fading||opacity[i]<1;
        }
        out.coverage=coverage;out.band=band;
        return out;
    }
};

inline float Hash(int x,int y,uint32_t seed) {
    uint32_t h=uint32_t(x)*374761393u+uint32_t(y)*668265263u+seed*2246822519u;
    h=(h^(h>>13))*1274126177u;h^=h>>16;
    return float(h&0xffffffu)/float(0xffffffu);
}
inline float SmoothStep(float a,float b,float x){const float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}

// Drips: up to two per splat, from its largest blobs. Timing is independent of
// the artwork, so the frame thread can tell when a repaint is needed.
constexpr int DripsPerSplat=2;
struct DripMotion {float delay,speed,length,width;}; // s, px/s, px, px at Size
inline DripMotion Motion(int placement,int drip) {
    return {.5f+2.5f*Hash(placement,drip,31),7+9*Hash(placement,drip,32),
            70+170*Hash(placement,drip,33),3.5f+3*Hash(placement,drip,34)};
}
inline int DripLength(int placement,int drip,float age) {
    const auto m=Motion(placement,drip);
    if(!std::isfinite(age)||age<=m.delay)return 0;
    return int(std::min(m.length,(age-m.delay)*m.speed));
}
// Changes only when a splat appears/leaves, its opacity moves a 1/32 step or a
// drip grows a whole pixel. Settled splats with dried drips stop repainting.
inline uint64_t PaintKey(uint32_t visible,const std::array<float,Placements>& age,const std::array<float,Placements>& opacity) {
    uint64_t key=visible;
    for(int i=0;i<Placements;++i)if(visible>>i&1) {
        key=key*1000003u+uint64_t(std::lround(std::clamp(opacity[i],0.f,1.f)*32));
        for(int d=0;d<DripsPerSplat;++d)key=key*1000003u+uint64_t(DripLength(i,d,age[i]))+1;
    }
    return key;
}

struct DripOrigin {float x=0,y=0;bool used=false;};
// One placement rendered into visor pixels once: premultiplied RGBA.
struct Patch {
    int x=0,y=0,w=0,h=0;
    std::vector<uint8_t> rgba;
    std::array<DripOrigin,DripsPerSplat> drips{};
};
inline std::array<Patch,Placements> Prepare(const std::vector<uint8_t>& strip) {
    std::array<Patch,Placements> patches{};
    if(strip.size()!=size_t(StripWidth)*StripHeight*4)return patches;
    auto texel=[&](int sprite,int x,int y,int c)->float{
        if(x<0||y<0||x>=Cell||y>=Cell)return 0;
        const auto* p=&strip[(size_t(y)*StripWidth+sprite*Cell+x)*4];
        return c==3?p[3]:p[c]*(p[3]/255.f); // premultiplied
    };
    for(int i=0;i<Placements;++i) {
        const auto& l=Layout[i];auto& patch=patches[i];
        const float bearing=l.bearing*.0174532925f,rotation=l.rotation*.0174532925f;
        const float cx=(std::cos(bearing)*l.radius+1)*.5f*Size,cy=(std::sin(bearing)*l.radius+1)*.5f*Size;
        const int side=int(l.size*1.42f)+2;
        patch.w=patch.h=side;patch.x=int(cx)-side/2;patch.y=int(cy)-side/2;
        patch.rgba.assign(size_t(side)*side*4,0);
        const float c=std::cos(rotation),s=std::sin(rotation),scale=float(Cell)/l.size;
        for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
            const float dx=x+.5f-side*.5f,dy=y+.5f-side*.5f;
            float sx=(c*dx+s*dy)*scale,sy=(-s*dx+c*dy)*scale;
            if(l.flip)sx=-sx;
            sx+=Cell*.5f-.5f;sy+=Cell*.5f-.5f;
            const int x0=int(std::floor(sx)),y0=int(std::floor(sy));
            const float fx=sx-x0,fy=sy-y0;
            for(int ch=0;ch<4;++ch) {
                const float v=(texel(l.sprite,x0,y0,ch)*(1-fx)+texel(l.sprite,x0+1,y0,ch)*fx)*(1-fy)+
                              (texel(l.sprite,x0,y0+1,ch)*(1-fx)+texel(l.sprite,x0+1,y0+1,ch)*fx)*fy;
                patch.rgba[(size_t(y)*side+x)*4+ch]=uint8_t(std::clamp(std::lround(v),0L,255L));
            }
        }
        // Drip from the bottom of the tallest solid runs: real blobs, not specks.
        std::vector<int> run(side,0),bottom(side,-1);
        for(int x=0;x<side;++x)for(int y=side-1,length=0;y>=0;--y) {
            if(patch.rgba[(size_t(y)*side+x)*4+3]>230){if(bottom[x]<0)bottom[x]=y;++length;run[x]=std::max(run[x],length);}
            else if(bottom[x]>=0)break;
        }
        for(int d=0;d<DripsPerSplat;++d) {
            int best=-1;
            for(int x=0;x<side;++x) {
                if(run[x]<10)continue;
                bool spaced=true;
                for(int k=0;k<d;++k)if(std::fabs(patch.drips[k].x-patch.x-x)<40)spaced=false;
                if(spaced&&(best<0||run[x]>run[best]))best=x;
            }
            if(best>=0)patch.drips[d]={float(patch.x+best),float(patch.y+bottom[best]-2),true};
        }
    }
    return patches;
}
constexpr float MaxAlpha=.88f,ClearRadius=.28f;
inline void Over(uint8_t* d,float r,float g,float b,float a) { // premultiplied source
    const float keep=1-a/255.f;
    d[0]=uint8_t(r+d[0]*keep);d[1]=uint8_t(g+d[1]*keep);d[2]=uint8_t(b+d[2]*keep);d[3]=uint8_t(std::min(255.f,a+d[3]*keep));
}
// Straight-alpha RGBA, Size x Size. The centre of the view is never covered.
inline std::vector<uint8_t> Compose(const std::array<Patch,Placements>& patches,uint32_t visible,
                                    const std::array<float,Placements>& age,const std::array<float,Placements>& opacity) {
    std::vector<uint8_t> rgba(size_t(Size)*Size*4,0);
    for(int i=0;i<Placements;++i) {
        const auto& p=patches[i];
        const float o=std::isfinite(opacity[i])?std::clamp(opacity[i],0.f,1.f):0.f;
        if(!(visible>>i&1)||p.rgba.empty()||o<=0)continue;
        for(int y=std::max(0,-p.y);y<p.h&&p.y+y<Size;++y)for(int x=std::max(0,-p.x);x<p.w&&p.x+x<Size;++x) {
            const auto* s=&p.rgba[(size_t(y)*p.w+x)*4];
            if(s[3])Over(&rgba[(size_t(p.y+y)*Size+p.x+x)*4],s[0]*o,s[1]*o,s[2]*o,s[3]*o);
        }
    }
    for(int i=0;i<Placements;++i)if(visible>>i&1)for(int d=0;d<DripsPerSplat;++d) {
        const auto& o=patches[i].drips[d];
        const int length=DripLength(i,d,age[i]);
        const float fade=std::isfinite(opacity[i])?std::clamp(opacity[i],0.f,1.f):0.f;
        if(!o.used||length<=0||fade<=0)continue;
        const auto m=Motion(i,d);
        auto dab=[&](float cx,float cy,float radius) {
            for(int y=int(cy-radius-1);y<=int(cy+radius+1);++y)for(int x=int(cx-radius-1);x<=int(cx+radius+1);++x) {
                if(x<0||y<0||x>=Size||y>=Size)continue;
                const float a=std::clamp(radius+.5f-std::hypot(x+.5f-cx,y+.5f-cy),0.f,1.f)*240*fade;
                if(a>0)Over(&rgba[(size_t(y)*Size+x)*4],92*a/255,4*a/255,4*a/255,a);
            }
        };
        for(int step=0;step<=length;step+=2) {
            const float t=float(step)/std::max(1,length);
            dab(o.x+1.2f*std::sin(step*.045f+i+d),o.y+step,m.width*.5f*(1-.3f*t));
        }
        dab(o.x+1.2f*std::sin(length*.045f+i+d),o.y+length,m.width*.8f); // bead at the tip
    }
    for(int y=0;y<Size;++y)for(int x=0;x<Size;++x) {
        auto* p=&rgba[(size_t(y)*Size+x)*4];
        if(!p[3])continue;
        const float u=(x+.5f)/Size*2-1,v=(y+.5f)/Size*2-1;
        const float alpha=p[3]*MaxAlpha*SmoothStep(ClearRadius,ClearRadius+.2f,std::sqrt(u*u*.85f+v*v));
        for(int c=0;c<3;++c)p[c]=uint8_t(std::min(255.f,p[c]*255.f/p[3]));
        p[3]=uint8_t(std::lround(alpha));
    }
    return rgba;
}
// Development fallback when the artwork is not embedded: clustered discs.
inline std::vector<uint8_t> FallbackStrip() {
    std::vector<uint8_t> rgba(size_t(StripWidth)*StripHeight*4,0);
    for(int sprite=0;sprite<SpriteCount;++sprite)for(int k=0;k<22;++k) {
        const float t=Hash(sprite,k,7),cx=Cell*(.15f+.7f*t),cy=Cell*(.85f-.7f*t+.25f*(Hash(sprite,k,8)-.5f));
        const float radius=k<3?18+14*Hash(sprite,k,9):2+7*Hash(sprite,k,9);
        for(int y=int(cy-radius-1);y<=int(cy+radius+1);++y)for(int x=int(cx-radius-1);x<=int(cx+radius+1);++x) {
            if(x<0||y<0||x>=Cell||y>=Cell)continue;
            const float a=std::clamp(radius+.5f-std::hypot(x+.5f-cx,y+.5f-cy),0.f,1.f);
            auto* p=&rgba[(size_t(y)*StripWidth+sprite*Cell+x)*4];
            if(a*255>p[3]){p[0]=106;p[1]=6;p[2]=6;p[3]=uint8_t(a*255);}
        }
    }
    return rgba;
}
}
