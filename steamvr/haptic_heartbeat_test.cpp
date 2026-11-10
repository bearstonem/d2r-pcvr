#include "haptic_heartbeat.h"
#include "heartbeat.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace framevr;
using namespace framevr::haptics;
static void Check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static bool Near(double a,double b,double e=1e-4){return std::fabs(a-b)<e;}
struct Beat {double at;int beat;Sample sample;};
// Frames at 90 Hz from `from` for `seconds`, at one level and tempo.
static std::vector<Beat> Run(Heartbeat& h,double from,double seconds,float level,float bpm,bool felt=true) {
    std::vector<Beat> out;
    for(double t=from;t<from+seconds;t+=1./90){const auto p=h.Step(t,level,bpm,felt);if(p.beat)out.push_back({t,p.beat,p.sample});}
    return out;
}
int main() {
    const heartbeat::Tuning tuning;
    {   // Calm: nothing, ever.
        Heartbeat h;Check(Run(h,0,5,0,70).empty()&&!h.Beating(),"no beat above the start");
        Check(Run(h,0,5,NAN,70).empty()&&Run(h,0,5,-1,70).empty(),"nor for a bad level");
        Check(!h.Step(NAN,1,70,true).beat,"nor a bad clock");
    }
    {   // Lub-dub: the lub at once, the dub about a third of a beat later, then the rest of the beat in silence.
        Heartbeat h;const auto beats=Run(h,10,3,.2f,70);
        Check(beats.size()>=6&&beats[0].beat==1&&Near(beats[0].at,10),"the first lub is felt at once");
        for(size_t i=0;i<beats.size();++i)Check(beats[i].beat==(i%2?2:1),"lub, dub, lub, dub");
        Check(h.Beating(),"it is beating");
        const double period=60./70;
        Check(Near(beats[1].at-beats[0].at,.21,.012),"the dub 0.21 s after the lub at a resting tempo (the recording's own spacing)");
        Check(Near(beats[2].at-beats[0].at,period,.012)&&Near(beats[4].at-beats[2].at,period,.012),"one lub a beat");
        Check(Near(beats[4].at-beats[0].at,2*period,.012),"the rhythm does not drift by a frame each beat");
        Check(beats[0].sample.amplitude>beats[1].sample.amplitude&&beats[0].sample.milliseconds>beats[1].sample.milliseconds,"the dub is softer and shorter");
        Check(beats[0].sample.frequency==HeartHz&&beats[0].sample.milliseconds<int(1000*HeartDubDelay(70)),"a low thump that ends before the dub");
    }
    {   // It quickens and strengthens as life drops, at the audible heartbeat's own level and tempo.
        float lastBpm=0,lastLub=0,lastDub=0;
        for(int percent=55;percent>0;percent-=10) {
            const float level=heartbeat::LifeLevel(float(percent),100,60),bpm=heartbeat::Bpm(level,tuning);
            Check(level>0&&bpm>lastBpm&&HeartLub(level).amplitude>lastLub&&HeartDub(level).amplitude>lastDub,"each 10 % of life lost: faster and harder");
            lastBpm=bpm;lastLub=HeartLub(level).amplitude;lastDub=HeartDub(level).amplitude;
            Heartbeat h;const auto beats=Run(h,0,4,level,bpm);
            int lubs=0;for(const auto& b:beats)if(b.beat==1)++lubs;
            Check(std::abs(lubs-int(4*bpm/60)-1)<=1,"as many lubs as the tempo asks");
        }
        Check(heartbeat::LifeLevel(65,100,60)==0&&heartbeat::LifeLevel(59,100,60)>0,"it starts under Heartbeat starts at (60 %)");
        Check(Near(HeartLub(0).amplitude,.30)&&Near(HeartLub(1).amplitude,.80)&&HeartLub(1).milliseconds==60&&HeartLub(0).milliseconds==45,"lub: 0.30 for 45 ms, to 0.80 for 60 ms");
        Check(Near(HeartDub(0).amplitude,.20)&&Near(HeartDub(1).amplitude,.55)&&HeartDub(1).milliseconds==45&&HeartDub(0).milliseconds==35,"dub: 0.20 for 35 ms, to 0.55 for 45 ms");
        Check(Near(HeartLub(9).amplitude,.80)&&Near(HeartLub(NAN).amplitude,.30),"levels clamp");
        Check(Near(heartbeat::Bpm(heartbeat::LifeLevel(55,100,60),tuning),77.5)&&Near(heartbeat::Bpm(1,tuning),115),"77.5 bpm at the first step, 115 under 10 %");
        Check(Near(HeartDubDelay(70),.21)&&Near(HeartDubDelay(115),.1565,.001)&&Near(HeartDubDelay(200),.14)&&Near(HeartDubDelay(NAN),.21),"the dub closes in as the heart races, never under 0.14 s");
        Check(HeartDefaultStart==60,"with the audible heartbeat Off, the felt one starts at that setting's default");
    }
    {   // A menu, death, Off or unknown life stops it at once: no dub left hanging, and it starts again on a lub.
        Heartbeat h;
        Check(h.Step(0,1,115,true).beat==1,"lub");
        Check(!h.Step(.05,1,115,false).beat&&!h.Beating(),"a menu opens: stopped");
        Check(Run(h,.06,2,1,115,false).empty(),"nothing while it is open, the dub included");
        const auto again=h.Step(2.1,1,115,true);Check(again.beat==1&&h.Beating(),"closed: a fresh lub at once");
        Check(!h.Step(2.15,0,115,true).beat&&!h.Beating(),"healed above the start: stopped");
        Check(Run(h,2.2,1,0,115).empty(),"and stays stopped");
    }
    {   // Stalls: a dub that is late is dropped rather than felt against the next lub; a long stall starts over.
        Heartbeat h;Check(h.Step(0,1,115,true).beat==1,"lub");
        const auto late=h.Step(.30,1,115,true);Check(late.beat==0,"the dub was due at 0.157 s: 0.14 s late, dropped");
        const auto next=Run(h,.31,1,1,115);Check(!next.empty()&&next[0].beat==1&&Near(next[0].at,60./115,.012),"the next lub keeps its time");
        Heartbeat g;Check(g.Step(100,1,115,true).beat==1,"lub");
        const auto after=g.Step(130,1,115,true);Check(after.beat==1,"thirty seconds later (a load): a fresh lub, not a burst of missed beats");
        Check(!g.Step(130.01,1,115,true).beat,"one lub only");
        Heartbeat back;Check(back.Step(500,1,115,true).beat==1&&back.Step(20,1,115,true).beat==1,"a clock that jumped back starts over too");
    }
    {   // The tempo follows life from one beat to the next.
        Heartbeat h;Check(h.Step(0,.2f,70,true).beat==1,"a slow lub");
        const auto fast=Run(h,1./90,3,1,115);
        Check(fast.size()>=4&&fast[0].beat==2&&Near(fast[0].sample.amplitude,HeartDub(.2f).amplitude),"the beat under way finishes as it began");
        Check(fast[1].beat==1&&Near(fast[1].at,60./70,.012)&&Near(fast[1].sample.amplitude,HeartLub(1).amplitude),"then the next lub is the harder one");
        Check(fast[3].beat==1&&Near(fast[3].at-fast[1].at,60./115,.012),"at the faster tempo");
    }
    std::puts("Haptic heartbeat: calm, lub-dub timing, level and tempo by life, stops, stalls and tempo changes passed");
    return 0;
}
