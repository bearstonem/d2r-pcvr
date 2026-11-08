#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// The campfire's own sound (owner 2026-11-08: "dźwięk paleniska"), made here, no recording: a low
// roar of the flames with crackles over it while the character screen's fire burns, and on a click
// at the fire (vrcam CampfirePick) a whoosh with a flurry of crackles - the sparks' sound.
// Value-only: the transport streams it on its own waveOut stream (HeartStream), mono 16-bit.
namespace framevr::fireaudio {
constexpr int Rate=44100;

struct Synth {
    uint32_t rng=0x2545F491u;
    float roarLow=0,roarMid=0,roarMid2=0,flicker=.6f,flickerTo=.6f;int flickerLeft=0;
    // crackles: a few clicks in a row, each a short decaying burst of bright noise
    int clicksLeft=0,clickGap=0;float clickAmp=0,clickEnv=0,clickDecay=.9f,clickHp=0;
    float pop=0,popPhase=0,popFreq=0;   // a low pop now and then (a knot bursting)
    float burst=0;double burstT=99;     // the whoosh, seconds since the click
    float whooshLp=0,whooshLp2=0,burstLevel=1;

    float Noise(){rng=rng*1664525u+1013904223u;return float(rng>>9)/float(1u<<23)*2.f-1.f;}
    float Uniform(){rng=rng*1664525u+1013904223u;return float(rng>>8)/float(1u<<24);}
    void Burst(){burstT=0;}

    // frames of mono samples; ambient and burst gains 0..1 (the game's volume already in them)
    void Render(std::vector<int16_t>& out,int frames,float ambient,float burstGain) {
        out.resize(size_t(frames));
        burstLevel=std::max(0.f,burstGain/std::max(ambient,1e-4f));   // fire_burst_volume against fire_volume: how big the flurry
        for(int i=0;i<frames;++i) {
            // the roar: brownish noise, its loudness wandering like the flames
            const float n=Noise();
            roarLow+=.012f*(n-roarLow);roarMid+=.09f*(n-roarMid);roarMid2+=.03f*(roarMid-roarMid2);
            if(--flickerLeft<=0){flickerLeft=int(Rate*(.05f+.25f*Uniform()));flickerTo=.35f+.65f*Uniform();}
            flicker+=.0004f*(flickerTo-flicker);
            float s=(roarLow*1.6f+(roarMid-roarMid2)*.35f)*flicker*.06f;   // only a breath of the flames: the crackle carries it

            // crackles: on average ~5 a second, many more just after a click
            const double t=burstT;
            // a pine cone in the fire: it catches over half a second, crackles hard, then settles over ~3 s
            const float boost=t<3.5?float((t/0.35)*std::exp(1.0-t/0.35))*30.f:0.f;
            if(clicksLeft<=0&&Uniform()<(7.f+boost*3.f*burstLevel)/Rate) {
                clicksLeft=1+int(Uniform()*4);clickGap=0;const float u=Uniform();clickAmp=(.05f+.45f*u*u*u)*(1.f+boost*.03f*burstLevel);
            }
            if(clicksLeft>0&&--clickGap<=0) {
                clickEnv=clickAmp*(.5f+.5f*Uniform());clickDecay=.80f+.15f*Uniform();
                clickGap=int(Rate*(.002f+.014f*Uniform()));--clicksLeft;
            }
            const float raw=Noise()*clickEnv;clickEnv*=clickDecay;
            clickHp+=.35f*(raw-clickHp);
            const float click=raw-clickHp;   // high-passed: a dry, sharp crack

            if(pop<=.001f&&Uniform()<(.4f+boost*.3f*burstLevel)/Rate){pop=.35f+.4f*Uniform();popFreq=70.f+90.f*Uniform();popPhase=0;}
            popPhase+=6.2831853f*popFreq/Rate;const float popS=pop*std::sin(popPhase);pop*=.9985f;

            float amb=(s+click*.9f+popS*.5f)*ambient;

            // a click: no whoosh of its own - only the fire's own crackle, a great deal of it at once (boost)
            const float wh=0.f;
            burstT+=1.0/Rate;
            const float v=amb+wh*burstGain;
            out[size_t(i)]=int16_t(std::lround(std::clamp(std::tanh(v*1.2f),-1.f,1.f)*32000.f));
        }
    }
};
}
