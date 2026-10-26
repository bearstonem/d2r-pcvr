#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Guard strain light (Unleashed physical guard). Value-only: the transport
// supplies blocked hits, parries and whether the shield is raised; nothing
// here reads native memory or XR state.
//
// Every blocked hit strains the guard, weighted by the attacker's rank
// (RankWeight). Strain shows as a soft black shadow at the rim of the view that
// creeps inward; each block sends one pulse in from
// the edge. Full strain breaks the guard: the light swells toward the centre
// and breathes slowly. The shield still blocks throughout; what the player
// loses is sight of the fight. Strain drains while the shield is down, and
// also while it stays up once no hit has reached it (block or parry) for
// recoverSeconds; the next hit stops the drain and restarts that wait. A broken
// guard stays broken until it has fully faded. Raising it again part way keeps
// what is left. Parries add nothing.
//
// Comfort: no hard flashes. Pulses are a smooth attack/release envelope that
// re-triggers without dropping, so rapid blocks hold a swell rather than
// strobing; breathing is well under 1 Hz; opacity never exceeds brightness.
namespace framevr::strain {
// The attacker's rank weights the strain of each block it lands on the shield:
// lesser monsters wear the guard down slowly, the rarest fastest. A normal
// monster breaks the guard in 10 blocks, an act boss in 3.
enum class Rank : uint8_t { Normal, Minion, Champion, Unique, PrimeEvil };
inline float RankWeight(Rank r) {
    switch(r){case Rank::Minion:return 1.25f;case Rank::Champion:return 2.f;case Rank::Unique:return 2.5f;case Rank::PrimeEvil:return 4.f;default:return 1.f;}
}
inline const char* RankName(Rank r) {
    switch(r){case Rank::Minion:return "minion";case Rank::Champion:return "champion";case Rank::Unique:return "unique";case Rank::PrimeEvil:return "prime_evil";default:return "normal";}
}
struct Tuning {
    float perBlock=.10f;     // strain per blocked hit (1 = broken)
    float resetSeconds=1.5f; // draining: full strain clears in this long
    float recoverSeconds=1.5f; // raised, no hit on the shield this long: drains as if lowered (0 = only lowering drains)
                             // 1.5 s bridges most gaps between blocks in a fight (median 0.8 s); hold + fade = 3 s from full
    float clear=.10f;        // radius left clear at full swell (1 = visor image edge)
    float brightness=1.f;    // peak opacity, 0 = off ([gameplay] guard_strain_light)
    float breatheSeconds=1.6f; // one slow swell while broken
    float feather=.45f;      // width of the inner blend (softness); the glow's midpoint stays put
    uint32_t color=0x000000; // 0xRRGGBB: black only
    float density=2.5f;      // >1 thickens the partly clear band (guard_strain_dark_density)
};
// Ring geometry, in the visor image's radius (1 = image edge; the headset's
// view ends near .95). The light covers everything outside `edge`.
constexpr float FirstEdge=.90f,FullEdge=.60f,Feather=.60f; // Feather: the default softness
// The glow is half bright at edge+Middle whatever the softness, so softening
// widens the blend both ways instead of pushing the light out of view.
constexpr float Middle=.125f;
constexpr float PulseIn=.10f,PulseAlpha=.25f;       // a block's pulse: edge pulled in, extra opacity (of brightness)
constexpr float PulseAttack=.12f,PulseRelease=.45f; // seconds
constexpr float SwellSeconds=.6f;                   // broken: rim to full swell
constexpr float BreatheEdge=.03f,BreatheAlpha=.15f;

inline float SmoothStep(float a,float b,float x){const float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}

struct Input {
    double time=0;
    uint64_t session=0;
    bool enabled=false;       // Unleashed, light setting above 0
    bool eligible=false;      // first person, in a game, shield equipped, no menu
    bool raised=false;        // the guard is up (shield_guard.h)
    unsigned blocks=0,parries=0; // since the last step
    float weight=-1;          // summed RankWeight of those blocks; <0 = one per block
};
struct Output {
    float strain=0,swell=0,pulse=0;
    float edge=1,alpha=0;     // what to paint
    bool visible=false,broken=false;
    bool brokeNow=false,clearedNow=false;
};
class Engine {
    uint64_t session=0;
    double last=0,pulseAt=-1,hitAt=-1; // hitAt: the last block or parry on the raised shield
    float strain=0,swell=0,pulse=0;
    bool broken=false,attacking=false;
public:
    void Reset(){*this=Engine{};}
    Output Step(const Input& in,const Tuning& t) {
        Output out;
        if(!in.enabled||!std::isfinite(in.time)||!(t.brightness>0)){Reset();return out;}
        if(in.session!=session){Reset();session=in.session;}
        const double raw=last>0?in.time-last:0;
        const float dt=raw>0&&raw<=.25?float(raw):0; // hitches and loads do not drain or swell
        last=in.time;
        const bool up=in.eligible&&in.raised;
        if(up&&(in.blocks||in.parries||in.weight>0))hitAt=in.time;   // any hit on the shield restarts the recovery wait
        if(up&&(in.blocks||in.weight>0)) {   // weight may arrive a step before its count
            const float units=in.weight>=0?in.weight:float(in.blocks);
            strain=std::min(1.f,strain+units*std::max(0.f,t.perBlock));
            attacking=true;pulseAt=in.time;
            if(strain>=1&&!broken){broken=true;out.brokeNow=true;}
        }
        // Raised but left alone: after recoverSeconds without a hit it drains like lowering.
        const bool idle=up&&t.recoverSeconds>0&&(hitAt<0||in.time-hitAt>=t.recoverSeconds);
        if((!up||idle)&&strain>0) {
            strain=t.resetSeconds>0?std::max(0.f,strain-dt/t.resetSeconds):0;
            if(strain<=0&&broken){broken=false;out.clearedNow=true;}
        }
        if(strain<=0)broken=false;
        // Swell: grows while broken and under attack, never outlasts the strain it came from.
        if(broken&&up&&!idle)swell=std::min(1.f,swell+dt/SwellSeconds);
        swell=std::min(swell,strain);
        // Pulse envelope: re-triggered blocks keep rising instead of dropping.
        if(attacking) {
            pulse=dt>0?std::min(1.f,pulse+dt/PulseAttack):std::max(pulse,.5f);
            if(pulse>=1)attacking=false;
        } else pulse=std::max(0.f,pulse-dt/PulseRelease);
        float edge=FirstEdge+(FullEdge-FirstEdge)*strain;
        edge+=(std::clamp(t.clear,0.f,FullEdge)-edge)*swell;
        float alpha=.30f+.45f*strain+.25f*swell;
        if(broken) {
            const float phase=t.breatheSeconds>0?float(std::sin(in.time*6.2831853/t.breatheSeconds)):0.f;
            edge+=BreatheEdge*phase*swell;alpha*=1-BreatheAlpha*swell*(.5f-.5f*phase);
        }
        edge-=PulseIn*pulse*(1-swell*.5f);
        alpha+=PulseAlpha*pulse;
        // Fades in from nothing and back out as the last strain drains.
        alpha*=std::min(1.f,(strain+pulse)*8);
        out.strain=strain;out.swell=swell;out.pulse=pulse;out.broken=broken;
        out.edge=std::clamp(edge,0.f,1.5f);
        out.alpha=std::clamp(alpha,0.f,1.f)*std::clamp(t.brightness,0.f,1.f);
        out.visible=out.alpha>.004f;
        return out;
    }
};

// Light image: Size x Size straight-alpha RGBA. Its shape is set in the same
// visor extent as the blood (blood_screen.h VisorExtent), the footprint
// verified on Quest 3, so the light sits at the same angles on every headset.
// Unlike the splats it must reach the edge of the view: on a wider headset the
// quad grows to the whole view (ViewExtent) and the image `reach` times the
// visor per axis, the field carrying on outward (it only gets brighter). Reach
// 1 is the visor itself, pixel for pixel. A smooth radial field, so a small
// texture is enough. Inward from the rim the light thins
// out over `feather` with an ease-in (smoothstep squared): no visible inner
// rim, just a glow that fades toward the centre. Squared smoothstep is half
// at t=.6436, so the blend starts that far (of feather) inside the midpoint.
constexpr int Size=256;
constexpr float MaxReach=5;        // ~80 degrees per side over the 54 / 55 degree visor
inline float Reach(float r){return std::isfinite(r)?std::clamp(r,1.f,MaxReach):1.f;}
inline float Radius(int x,int y,float reachX=1,float reachY=1) {
    const float u=((x+.5f)/Size*2-1)*Reach(reachX),v=((y+.5f)/Size*2-1)*Reach(reachY);
    return std::sqrt(u*u*.85f+v*v); // same aspect as the blood's clear centre
}
inline float Falloff(float edge,float feather,float radius) {
    feather=std::clamp(feather,.02f,1.f);
    const float start=edge+Middle-.6436f*feather;
    const float a=SmoothStep(start,start+feather,radius);
    return a*a;
}
// Density > 1 lifts the partly clear band toward full cover (1-(1-a)^density):
// a dark rim over a dark scene reads far lighter than a pale one at the same
// opacity, so the black rim is drawn denser to hide as much. 0 and 1 stay put.
inline float Dense(float a,float density) {return density>1?1-std::pow(1-a,density):a;}
inline std::vector<uint8_t> Compose(float edge,float alpha,float feather=Feather,uint32_t color=0x000000,float density=1,float reachX=1,float reachY=1) {
    std::vector<uint8_t> rgba(size_t(Size)*Size*4,0);
    if(!std::isfinite(edge)||!std::isfinite(alpha)||!std::isfinite(feather)||!std::isfinite(density)||alpha<=0)return rgba;
    alpha=std::min(alpha,1.f);
    for(int y=0;y<Size;++y)for(int x=0;x<Size;++x) {
        auto* p=&rgba[(size_t(y)*Size+x)*4];
        p[0]=uint8_t(color>>16);p[1]=uint8_t(color>>8);p[2]=uint8_t(color);
        p[3]=uint8_t(std::lround(255*alpha*Dense(Falloff(edge,feather,Radius(x,y,reachX,reachY)),density)));
    }
    return rgba;
}
// Repaint only on a visible change: edge and softness to 1/256 of the image,
// opacity to 1/128, a new colour, or the view's reach to 1/256. 0: nothing to paint.
inline uint64_t PaintKey(float edge,float alpha,float feather=Feather,uint32_t color=0x000000,float density=1,float reachX=1,float reachY=1) {
    if(!std::isfinite(edge)||!std::isfinite(alpha)||!std::isfinite(feather)||!std::isfinite(density)||alpha<=0)return 0;
    const uint64_t shape=(uint64_t(std::lround(std::clamp(feather,0.f,1.f)*256))<<32)|
           (uint64_t(std::lround(std::clamp(edge,0.f,1.5f)*256))<<16)|uint64_t(std::lround(std::clamp(alpha,0.f,1.f)*128)+1);
    const uint64_t reach=(uint64_t(std::lround(Reach(reachX)*256))<<12)|uint64_t(std::lround(Reach(reachY)*256));
    return (((shape*1000003u+(color&0xFFFFFFu))*31u+uint64_t(std::lround(std::clamp(density,1.f,6.f)*64)))*2654435761u+reach)|1;
}
}
