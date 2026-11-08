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
    float whooshLp=0,whooshLp2=0;

    float Noise(){rng=rng*1664525u+1013904223u;return float(rng>>9)/float(1u<<23)*2.f-1.f;}
    float Uniform(){rng=rng*1664525u+1013904223u;return float(rng>>8)/float(1u<<24);}
    void Burst(){burstT=0;}

    // frames of mono samples; ambient and burst gains 0..1 (the game's volume already in them)
    void Render(std::vector<int16_t>& out,int frames,float ambient,float burstGain) {
        out.resize(size_t(frames));
        for(int i=0;i<frames;++i) {
            // the roar: brownish noise, its loudness wandering like the flames
            const float n=Noise();
            roarLow+=.012f*(n-roarLow);roarMid+=.09f*(n-roarMid);roarMid2+=.03f*(roarMid-roarMid2);
            if(--flickerLeft<=0){flickerLeft=int(Rate*(.05f+.25f*Uniform()));flickerTo=.35f+.65f*Uniform();}
            flicker+=.0004f*(flickerTo-flicker);
            float s=(roarLow*3.2f+(roarMid-roarMid2)*.9f)*flicker*.22f;

            // crackles: on average ~5 a second, many more just after a click
            const double t=burstT;
            const float boost=t<2.5?float(std::exp(-t/0.6))*14.f:0.f;
            if(clicksLeft<=0&&Uniform()<(5.f+boost*3.f)/Rate) {
                clicksLeft=1+int(Uniform()*5);clickGap=0;clickAmp=.18f+.55f*Uniform()*Uniform();
            }
            if(clicksLeft>0&&--clickGap<=0) {
                clickEnv=clickAmp*(.5f+.5f*Uniform());clickDecay=.80f+.15f*Uniform();
                clickGap=int(Rate*(.002f+.014f*Uniform()));--clicksLeft;
            }
            const float raw=Noise()*clickEnv;clickEnv*=clickDecay;
            clickHp+=.35f*(raw-clickHp);
            const float click=raw-clickHp;   // high-passed: a dry, sharp crack

            if(pop<=.001f&&Uniform()<(.4f+boost*.4f)/Rate){pop=.35f+.4f*Uniform();popFreq=70.f+90.f*Uniform();popPhase=0;}
            popPhase+=6.2831853f*popFreq/Rate;const float popS=pop*std::sin(popPhase);pop*=.9985f;

            float amb=(s+click*.9f+popS*.5f)*ambient;

            // the whoosh: noise whose brightness rises then falls, over about a second and a half
            float wh=0;
            if(t<2.0) {
                const float env=float((1.0-std::exp(-t/.04))*std::exp(-t/.45));
                const float bright=.02f+.25f*float(std::exp(-t/.35));
                whooshLp+=bright*(n-whooshLp);whooshLp2+=bright*.6f*(whooshLp-whooshLp2);
                wh=(whooshLp*2.2f+whooshLp2*1.5f)*env*.9f+click*.6f*float(std::exp(-t/.8));
            }
            burstT+=1.0/Rate;
            const float v=amb+wh*burstGain;
            out[size_t(i)]=int16_t(std::lround(std::clamp(std::tanh(v*1.2f),-1.f,1.f)*32000.f));
        }
    }
};
}
