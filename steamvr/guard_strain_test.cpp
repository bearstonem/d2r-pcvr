#include "guard_strain.h"
#include "guard_strain_paint.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}

namespace {
struct Run {
    strain::Engine engine;strain::Tuning tuning;double time=1;
    strain::Output Step(bool raised,unsigned blocks=0,unsigned parries=0,double dt=1/90.,bool eligible=true,uint64_t session=7) {
        time+=dt;
        strain::Input in;in.time=time;in.session=session;in.enabled=true;in.eligible=eligible;in.raised=raised;
        in.blocks=blocks;in.parries=parries;
        return engine.Step(in,tuning);
    }
    strain::Output Hold(bool raised,double seconds,bool eligible=true) {
        strain::Output out;
        for(double t=0;t<seconds;t+=1/90.)out=Step(raised,0,0,1/90.,eligible);
        return out;
    }
};
}

template<class F> void Until(F predicate){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){
        Check(std::chrono::steady_clock::now()<end,"bounded painter completion");
        std::this_thread::yield();
    }
}
int main() {
    {   // One fixed rate: ten normal blocks break the guard (rank weighting aside).
        Run r;r.Step(true);
        for(int i=0;i<9;++i)Check(!r.Step(true,1).broken,"guard does not reach full strain early");
        Check(r.Step(true,1).broken,"full strain after ten normal blocks");
        Check(r.Hold(false,1.6).strain==0,"clears fully in 1.5 s lowered");
        Check(r.Step(true,0,1).strain==0,"parries stay free");
    }
    {   // A slow frame/worker or full upload queue must not discard every image.
        strain::Painter painter;
        Check(painter.Start(),"start strain painter");
        for(double frame:{1/90.,1/45.,1/25.,1/20.,1/10.}){
            Run r;r.Step(true);
            for(int i=0;i<12;++i){
                const auto out=r.Step(true,1,0,frame);
                strain::PaintJob job{out.edge,out.alpha,.45f,0x000000,2.5f};
                const auto key=strain::PaintKey(job.edge,job.alpha,job.feather,job.color,job.density);
                Check(painter.Submit(job),"schedule one image for a changing vignette");
                // Delay consumption through several new wanted states (as when the GPU is busy).
                job.alpha*=.5f;
                for(int k=0;k<4;++k)Check(!painter.Submit(job),"new frames cannot supersede an unconsumed image");
                strain::PaintImage image;Until([&]{return painter.Take(image);});
                Check(image.key==key&&image.pixels.size()==strain::Size*strain::Size*4,"original completed image survives at every frame rate with its own key");
            }
        }
        Check(painter.Submit({.2f,1,.45f,0,2.5f}),"old full-strain image in flight");
        painter.Discard();strain::PaintImage image;
        Check(!painter.Take(image),"hidden vignette cannot return an old image");
        strain::PaintJob fresh{.9f,.1f,.45f,0x000000,2.5f};
        Check(painter.Submit(fresh),"new character can request its own image");
        Until([&]{return painter.Take(image);});
        Check(image.key==strain::PaintKey(fresh.edge,fresh.alpha,fresh.feather,fresh.color,fresh.density),"previous character's heavy vignette cannot flash on a new block");
        painter.Stop();Check(!painter.Busy()&&!painter.Submit(fresh),"shutdown clears pending images");
    }
    {   // Nothing shows until a block lands; a parry adds no strain.
        Run r;
        auto o=r.Hold(true,1);
        Check(!o.visible&&o.strain==0,"a raised shield with no hits shows nothing");
        o=r.Step(true,0,1);o=r.Hold(true,1);
        Check(!o.visible&&o.strain==0,"a parry adds no strain");
    }
    {   // Blocks build strain; the edge creeps inward and opacity rises.
        Run r;
        r.Step(true,1);auto one=r.Hold(true,1);
        Check(one.visible&&std::fabs(one.strain-.1f)<1e-4f&&!one.broken,"one block: a tenth of the guard, visible at the rim");
        for(int i=0;i<4;++i){r.Step(true,1);r.Hold(true,.3);}
        auto five=r.Hold(true,1);
        Check(five.strain>one.strain&&five.edge<one.edge&&five.alpha>one.alpha,"more blocks: light deeper and brighter");
        Check(five.edge>=strain::FullEdge-1e-4f,"before breaking the light stays outside the full-strain edge");
    }
    {   // Raised: strain holds while hits keep coming, then recedes once they stop.
        Run r;
        Check(r.tuning.recoverSeconds==1.5f&&r.tuning.resetSeconds==1.5f,"1.5 s hold, then full strain drains in 1.5 s, by default");
        for(int i=0;i<3;++i){r.Step(true,1);r.Hold(true,.2);}
        const auto before=r.Hold(true,.1);
        const auto waiting=r.Hold(true,1.1);   // 1.4 s since the last block
        Check(std::fabs(waiting.strain-before.strain)<1e-5f,"strain holds through the wait with the shield up");
        const auto receding=r.Hold(true,.3);   // ~0.2 s into the drain
        Check(receding.strain<before.strain-.05f&&receding.strain>0,"no hits for 1.5 s: it recedes with the shield still up");
        const auto gone=r.Hold(true,.5);
        Check(gone.strain==0&&!gone.visible,"and clears without lowering");
    }
    {   // From full, left alone with the shield up: clear 3 s after the last hit, not 6.
        Run r;
        for(int i=0;i<10;++i){r.Step(true,1);r.Hold(true,.15);}   // last block, then .15 s
        auto o=r.Hold(true,2.75);   // 2.9 s after the last block
        Check(o.strain>0,"not yet clear at 2.9 s");
        o=r.Hold(true,.2);          // 3.1 s
        Check(o.strain==0&&!o.broken,"clear 3 s after the last hit from full");
    }
    {   // Blocks at a monster's attack rhythm (1.2 s apart) still build strain to a break.
        Run r;bool broke=false;
        for(int i=0;i<10;++i){broke=r.Step(true,1).brokeNow||broke;r.Hold(true,1.2);}
        Check(broke,"one monster hitting every 1.2 s still breaks the guard");
    }
    {   // A hit during the drain stops it and restarts the wait; a parry counts as a hit.
        Run r;
        for(int i=0;i<5;++i){r.Step(true,1);r.Hold(true,.2);}
        auto o=r.Hold(true,1.6);   // 1.8 s since the last block
        const float drained=o.strain;
        Check(drained<.45f&&drained>0,"draining after the wait");
        o=r.Step(true,1);
        const float struck=o.strain;
        o=r.Hold(true,1.2);
        Check(std::fabs(o.strain-struck)<1e-5f&&struck>drained,"a block mid-drain adds strain and holds it for the wait");
        o=r.Step(true,0,1);o=r.Hold(true,1.2);   // 1.2 s after the parry, 2.4 s after the block
        Check(std::fabs(o.strain-struck)<1e-5f,"a parry restarts the wait without adding strain");
        o=r.Hold(true,.6);
        Check(o.strain<struck,"then it recedes");
    }
    {   // A broken guard left alone with the shield up: the swell stops and it clears.
        Run r;
        for(int i=0;i<10;++i){r.Step(true,1);r.Hold(true,.15);}
        auto o=r.Hold(true,1.6);
        Check(o.broken&&o.strain<1,"still broken while it drains");
        const float swell=o.swell;
        o=r.Hold(true,.3);
        Check(o.swell<=swell,"the swell does not grow while the guard recovers");
        bool cleared=false;
        for(double t=0;t<1.6;t+=1/90.){o=r.Step(true);cleared=cleared||o.clearedNow;}
        Check(cleared&&!o.broken&&!o.visible,"fully faded with the shield up: the guard resets");
    }
    {   // recoverSeconds 0: the old rule, only lowering drains.
        Run r;r.tuning.recoverSeconds=0;
        for(int i=0;i<3;++i){r.Step(true,1);r.Hold(true,.2);}
        const auto before=r.Hold(true,.1);
        const auto later=r.Hold(true,10);
        Check(std::fabs(later.strain-before.strain)<1e-5f,"0: strain holds while the guard stays raised");
        const auto lowered=r.Hold(false,.6);   // 0.3 strain: 0.45 s of the 1.5 s drain
        Check(lowered.strain==0&&!lowered.visible,"lowering clears it");
    }
    {   // Lowering still drains at once, without the wait.
        Run r;
        for(int i=0;i<3;++i){r.Step(true,1);r.Hold(true,.2);}
        Check(r.Hold(false,.6).strain==0,"lowering drains immediately");
    }
    {   // Break: full strain swells toward the clear centre and breathes.
        Run r;r.tuning.recoverSeconds=0;   // the shield stays up with no hits for >1.5 s here
        strain::Output o;bool broke=false;
        for(int i=0;i<10;++i){o=r.Step(true,1);broke=broke||o.brokeNow;r.Hold(true,.15);}
        Check(broke,"the tenth block breaks the guard");
        o=r.Hold(true,1);
        Check(o.broken&&o.swell>=1-1e-4f,"broken: the swell completes while the shield stays up");
        float lo=10,hi=-10,amax=0;
        for(int i=0;i<300;++i){o=r.Step(true);lo=std::min(lo,o.edge);hi=std::max(hi,o.edge);amax=std::max(amax,o.alpha);}
        Check(lo<r.tuning.clear+.05f&&hi-lo>.02f,"broken: swollen to the clear radius and breathing");
        Check(amax<=r.tuning.brightness+1e-5f,"opacity never exceeds the brightness setting");
        // Partial lowering keeps what is left, and the guard stays broken until clear.
        o=r.Hold(false,.75);
        Check(o.broken&&o.strain>.45f&&o.strain<.55f,"0.75 s down: still broken, half drained");
        const float swell=o.swell;
        o=r.Hold(true,1);
        Check(o.broken&&o.swell>=swell,"re-raised part way: the swell resumes from what is left");
    }
    {   // Re-raise before fully drained: the remaining strain carries on.
        Run r;r.tuning.recoverSeconds=0;   // lowering only
        for(int i=0;i<10;++i){r.Step(true,1);r.Hold(true,.15);}
        auto o=r.Hold(false,.6);
        const float left=o.strain;
        o=r.Hold(true,2);
        Check(o.broken&&std::fabs(o.strain-left)<1e-4f,"re-raising part way keeps the remainder and the break");
        bool cleared=false;
        for(double t=0;t<1.6;t+=1/90.){o=r.Step(false);cleared=cleared||o.clearedNow;}
        Check(cleared&&!o.broken&&!o.visible,"fully faded: the guard resets");
    }
    {   // Not eligible (menu, no shield, third person) counts as lowered.
        Run r;
        for(int i=0;i<4;++i){r.Step(true,1);r.Hold(true,.2);}
        auto o=r.Hold(true,2,false);
        Check(o.strain==0,"a menu or unequipped shield drains like lowering");
        o=r.Step(false,3);
        Check(o.strain==0,"blocks reported while lowered add nothing");
    }
    {   // Pulses: smooth envelope, rapid blocks hold a swell instead of strobing.
        Run r;
        r.Step(true,1);
        float peak=0;strain::Output o;
        for(int i=0;i<20;++i){o=r.Step(true);peak=std::max(peak,o.pulse);}
        Check(peak>=1-1e-4f,"a block's pulse peaks");
        o=r.Hold(true,1);
        Check(o.pulse==0,"and releases within a second");
        // Five blocks a second for two seconds: the pulse never collapses between them.
        float floor=1;
        for(int i=0;i<10;++i){o=r.Step(true,1);for(int k=0;k<18;++k){o=r.Step(true);if(i>0)floor=std::min(floor,o.pulse);}}
        Check(floor>.5f,"rapid blocks hold the swell rather than flashing");
    }
    {   // Rank: rarer attackers strain the guard faster; act bosses break it in 3 at Medium.
        using strain::Rank;
        const auto blocksToBreak=[](Rank rank) {
            Run r;int n=0;
            for(;n<40;){strain::Input in;r.time+=1/90.;in.time=r.time;in.session=7;in.enabled=in.eligible=in.raised=true;
                in.blocks=1;in.weight=strain::RankWeight(rank);++n;if(r.engine.Step(in,r.tuning).broken)break;r.Hold(true,.2);}
            return n;
        };
        Check(blocksToBreak(Rank::Normal)==10&&blocksToBreak(Rank::Minion)==8&&blocksToBreak(Rank::Champion)==5&&
              blocksToBreak(Rank::Unique)==4&&blocksToBreak(Rank::PrimeEvil)==3,"blocks to break: 10/8/5/4/3 by rank");
        // A block's weight can land a step before its count (two atomics): it still strains, once.
        Run r;strain::Input in;r.time+=1/90.;in.time=r.time;in.session=7;in.enabled=in.eligible=in.raised=true;in.weight=2;
        const float early=r.engine.Step(in,r.tuning).strain;
        in.time=(r.time+=1/90.);in.weight=0;in.blocks=1;
        const float late=r.engine.Step(in,r.tuning).strain;
        Check(std::fabs(early-.2f)<1e-5f&&std::fabs(late-early)<1e-5f,"split weight and count strain once");
    }
    {   // Settings: off, sessions, hitches.
        Run r;r.tuning.brightness=0;
        auto o=r.Step(true,5);
        Check(!o.visible&&o.strain==0,"brightness 0 turns the light off");
        Run s;
        for(int i=0;i<5;++i){s.Step(true,1);s.Hold(true,.2);}
        o=s.Step(true,0,0,1/90.,true,8);
        Check(o.strain==0,"a new session starts clean");
        Run h;
        for(int i=0;i<5;++i){h.Step(true,1);h.Hold(true,.2);}
        const float before=h.Hold(true,.1).strain;
        o=h.Step(false,0,0,3.0);
        Check(std::fabs(o.strain-before)<1e-5f,"a loading hitch neither drains nor swells");
    }
    {   // Painter: clear centre, lit rim, opacity bounded, key tracks visible change.
        const auto img=strain::Compose(.6f,.7f);
        auto alpha=[&](int x,int y){return img[(size_t(y)*strain::Size+x)*4+3];};
        Check(alpha(strain::Size/2,strain::Size/2)==0,"the centre stays clear");
        Check(img[0]==0&&img[1]==0&&img[2]==0&&strain::Tuning{}.color==0&&strain::Tuning{}.density==2.5f&&strain::PaintJob{}.color==0,"black only, drawn dense");
        Check(alpha(0,0)>=178&&alpha(0,0)<=179,"the corners carry the full opacity");
        {   // Softness widens the blend about the same midpoint: no hard inner rim.
            auto at=[](float edge,float feather,float r){return strain::Falloff(edge,feather,r);};
            Check(std::fabs(at(.75f,.25f,.875f)-.5f)<.02f&&std::fabs(at(.75f,.60f,.875f)-.5f)<.02f,"half bright at the same radius at any softness");
            Check(at(.75f,.60f,.80f)>at(.75f,.25f,.80f)&&at(.75f,.60f,.95f)<at(.75f,.25f,.95f),"softer: light reaches further in and saturates later");
            float step=0;for(float r=0;r<1.4f;r+=.01f)step=std::max(step,at(.75f,.60f,r+.01f)-at(.75f,.60f,r));
            Check(step<.04f,"no jump larger than 4% per 1% of radius");
        }
        Check(alpha(strain::Size/2,2)>alpha(strain::Size/2,strain::Size/4),"brighter toward the rim");
        Check(strain::PaintKey(.6f,.7f)==strain::PaintKey(.6001f,.7001f),"imperceptible changes do not repaint");
        Check(strain::PaintKey(.6f,.7f)!=strain::PaintKey(.62f,.7f)&&strain::PaintKey(.6f,0)==0,"visible changes repaint; nothing to paint keys 0");
        {   // Density thickens the partly clear band only; the clear centre and full rim stay.
            const auto dense=strain::Compose(.6f,.7f,strain::Feather,0,2.5f);
            auto d=[&](int x,int y){return dense[(size_t(y)*strain::Size+x)*4+3];};
            Check(d(strain::Size/2,strain::Size/2)==0&&d(0,0)==alpha(0,0),"density keeps the centre clear and the rim at full opacity");
            bool thicker=false,never=true;
            for(int x=0;x<strain::Size/2;++x){const int a=alpha(x,strain::Size/2),b=d(x,strain::Size/2);thicker=thicker||b>a+20;never=never&&b>=a;}
            Check(thicker&&never,"density never thins the light and visibly thickens its blend");
            Check(strain::Dense(0,2.5f)==0&&strain::Dense(1,2.5f)==1&&strain::Dense(.5f,1)==.5f,"density keeps 0 and 1, and 1 is the plain curve");
            Check(strain::PaintKey(.6f,.7f,strain::Feather,0,1)!=strain::PaintKey(.6f,.7f,strain::Feather,0,2.5f),"a density change repaints");
        }
        const auto none=strain::Compose(.6f,0);
        bool empty=true;for(size_t i=3;i<none.size();i+=4)empty=empty&&none[i]==0;
        Check(empty,"zero opacity paints nothing");
        {   // Reach: a view wider than the visor gets the same light at the same angles, carried on to its rim.
            Check(strain::Compose(.9f,.5f,.45f,0,2.5f,1,1)==strain::Compose(.9f,.5f,.45f,0,2.5f),"reach 1 is the visor, pixel for pixel");
            Check(strain::Reach(.5f)==1&&strain::Reach(NAN)==1&&strain::Reach(INFINITY)==1&&strain::Reach(99)==strain::MaxReach,"reach never shrinks the light inside the visor and stays bounded");
            const float reach=1.6641f/1.3764f;   // the reported Frame view (59 / 60 degrees) over the 54 / 55 degree visor
            const auto visor=strain::Compose(.9f,.5f,.45f),wide=strain::Compose(.9f,.5f,.45f,0,1,reach,reach);
            auto a=[](const std::vector<uint8_t>& img,int x,int y){return int(img[(size_t(y)*strain::Size+x)*4+3]);};
            // The visor's own edge lands inside the wide image at Size/2*(1-1/reach) from its border.
            const int in=int(std::lround(strain::Size/2*(1-1/reach)));
            Check(std::abs(a(wide,in,strain::Size/2)-a(visor,0,strain::Size/2))<=12&&std::abs(a(wide,strain::Size/2,in)-a(visor,strain::Size/2,0))<=12,
                  "the visor's rim sits at the same angle inside the wider light");
            Check(a(wide,strain::Size/2,strain::Size/2)==0,"the centre stays clear");
            Check(a(wide,0,strain::Size/2)>a(visor,0,strain::Size/2)+40&&a(wide,strain::Size/2,0)>a(visor,strain::Size/2,0)+40,
                  "past the visor the light keeps building out to the view's edge, never a hard stop");
            bool rising=true;for(int x=1;x<strain::Size/2;++x)rising=rising&&a(wide,x-1,strain::Size/2)>=a(wide,x,strain::Size/2);
            Check(rising,"no seam: brighter all the way out");
            Check(strain::PaintKey(.9f,.5f,.45f,0,1,1,1)==strain::PaintKey(.9f,.5f,.45f,0,1)&&
                  strain::PaintKey(.9f,.5f,.45f,0,1,reach,reach)!=strain::PaintKey(.9f,.5f,.45f,0,1)&&
                  strain::PaintKey(.9f,.5f,.45f,0,1,reach,1)!=strain::PaintKey(.9f,.5f,.45f,0,1,1,reach),"a new reach repaints, per axis");
            strain::Painter painter;Check(painter.Start(),"painter starts");
            Check(painter.Submit({.9f,.5f,.45f,0,1,reach,1.1f}),"painter takes a reach");
            strain::PaintImage image;Until([&]{return painter.Take(image);});
            Check(image.reachX==strain::Reach(reach)&&image.reachY==1.1f&&image.pixels==strain::Compose(.9f,.5f,.45f,0,1,reach,1.1f),
                  "the painted image carries the reach it was painted for");
            painter.Stop();
        }
    }
    std::puts("Guard strain light: rim build-up, parries free, hold while hit, recede when hits stop, lower to reset, break swell and breathing, partial re-raise, eligibility, smooth pulses, off, sessions, hitches, painter and wide views passed");
}
