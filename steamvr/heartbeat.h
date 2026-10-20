#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Low-life heartbeat. Value-only: the transport supplies the published life and
// the player's VR Settings > Combat > Heartbeat starts at (LifeLevel), and plays
// it on its own waveOut stream (PlaySound plays one sound per process and would
// cut off the riposte).
//
// Below the start the heartbeat steps up every 10 % of life lost, louder and
// faster, loudest and fastest below 10 %. Healing back above a step winds it
// down slowly (Level): quieter and slower beat by beat, then silent. The built-in beat is a recording embedded in the plugin
// (steamvr/assets/guard-heartbeat.wav); Synthesize() is the fallback when that
// resource is missing. [gameplay] low_life_heart_file replaces it with a 16-bit
// PCM WAV of one beat (any rate, mono or stereo).
namespace framevr::heartbeat {
constexpr int Rate=44100;
constexpr double Pi=3.14159265358979;

// One "lub-dub", mono 16-bit at Rate. Low thumps with enough upper harmonics
// to read on headset speakers that cannot reproduce 50 Hz.
inline std::vector<int16_t> Synthesize() {
    const int length=int(Rate*.62);
    std::vector<float> mix(size_t(length),0.f);
    auto thump=[&](double start,double base,float amp,double decay) {
        double phase=0;uint32_t noise=0x9e3779b9u^uint32_t(base*1000);float lowpass=0;
        for(int i=int(start*Rate);i<length;++i) {
            const double t=double(i)/Rate-start;
            if(t>decay*8)break;
            const double env=(1-std::exp(-t/.004))*std::exp(-t/decay);
            const double f=base*(1+.45*std::exp(-t/.03));          // a short downward glide: the "thud"
            phase+=2*Pi*f/Rate;
            noise=noise*1664525u+1013904223u;
            lowpass+=.06f*((float(noise>>9)/float(1u<<23)*2-1)-lowpass); // soft body, not hiss
            mix[size_t(i)]+=amp*float(env*(std::sin(phase)+.5*std::sin(2*phase)+.22*std::sin(3*phase))+
                                      lowpass*1.6*std::exp(-t/.014));
        }
    };
    thump(0,52,1.f,.07);      // lub
    thump(.21,64,.72f,.06);   // dub: a little higher and softer
    float peak=0;
    for(float& v:mix){v=std::tanh(1.5f*v);peak=std::max(peak,std::fabs(v));}
    std::vector<int16_t> pcm(mix.size());
    const float scale=peak>0?.9f*32767/peak:0;
    for(size_t i=0;i<mix.size();++i) {
        const float fade=i+400>mix.size()?float(mix.size()-i)/400.f:1.f; // no click at the end
        pcm[i]=int16_t(std::lround(mix[i]*scale*fade));
    }
    return pcm;
}

struct Pcm {int rate=0,channels=0;std::vector<int16_t> samples;};
// 16-bit PCM WAV only (any rate, mono or stereo).
inline bool ParseWav(const std::vector<uint8_t>& w,Pcm& out) {
    out={};
    if(w.size()<44||std::memcmp(w.data(),"RIFF",4)||std::memcmp(w.data()+8,"WAVE",4))return false;
    int format=0,bits=0;
    for(size_t i=12;i+8<=w.size();) {
        uint32_t n=0;std::memcpy(&n,w.data()+i+4,4);
        if(!std::memcmp(w.data()+i,"fmt ",4)&&n>=16&&i+8+16<=w.size()) {
            uint16_t f=0,c=0,b=0;uint32_t r=0;
            std::memcpy(&f,w.data()+i+8,2);std::memcpy(&c,w.data()+i+10,2);std::memcpy(&r,w.data()+i+12,4);std::memcpy(&b,w.data()+i+22,2);
            format=f;out.channels=c;out.rate=int(r);bits=b;
        } else if(!std::memcmp(w.data()+i,"data",4)) {
            if(format!=1||bits!=16||out.channels<1||out.channels>2||out.rate<8000||out.rate>192000)return false;
            const size_t bytes=std::min<size_t>(n,w.size()-(i+8))&~size_t(1);
            out.samples.resize(bytes/2);
            if(bytes)std::memcpy(out.samples.data(),w.data()+i+8,bytes);
            return out.samples.size()>=size_t(out.channels);
        }
        i+=8+size_t(n)+(n&1);
    }
    return false;
}
struct Tuning {
    float volume=2;              // heartbeat % x the game's Master x Combat volume; 0 = off
    float bpmMin=70,bpmMax=115;  // the first step .. below 10 % life
    float quiet=.12f;            // loudness at the first touch (of full)
    float fadeSeconds=4;         // winding down from full to silent, at the slowest
};
// How hard the heart works: start (10..90 %, a multiple of 10; 0 = off) and each
// 10 % below it is one step, and life under k of those steps is k of them all,
// so start 50 gives 1/5 below 50 %, 2/5 below 40 % ... full below 10 %.
// Unknown, dead or off: 0, so it winds down.
inline float LifeLevel(float health,float maxHealth,int startPercent) {
    const int steps=std::clamp(startPercent,0,90)/10;
    if(steps<1||!std::isfinite(health)||!std::isfinite(maxHealth)||health<=0||maxHealth<=0)return 0;
    const float percent=100*health/maxHealth;
    int below=0;
    for(int k=0;k<steps;++k)if(percent<float(steps*10-k*10))++below;
    return float(below)/float(steps);
}
// Wiping the visor clean quiets the heartbeat while still hurt: the target
// drops to 0 and Level fades it out. The next hit brings it straight back at
// its step (Level rises at once). Reaching silence anyway (healed above the
// start, Off, dead) clears it. A hit in the same step as the last wipe stroke wins.
class Hush {
    bool on=false;
public:
    void Reset(){on=false;}
    // cleaned: a scrub removed the last blood this step. hit: life fell by a hit.
    float Step(float level,bool cleaned,bool hit) {
        on=(on||cleaned)&&!hit&&level>0;
        return on?0.f:level;
    }
    bool On() const {return on;}
};
// The heartbeat's own level: rises with LifeLevel at once, but falls no faster
// than 1/fadeSeconds per second, so it keeps beating, quieter and slower, after
// a heal. A hitch over .25 s (a load) snaps it to the target.
class Level {
    float level=0;
public:
    void Reset(){level=0;}
    float Step(float strain,double dt,float fadeSeconds) {
        strain=std::isfinite(strain)?std::clamp(strain,0.f,1.f):0.f;
        if(!std::isfinite(dt)||dt<0||dt>.25||strain>=level||!(fadeSeconds>0))level=strain;
        else level=std::max(strain,level-float(dt)/fadeSeconds);
        return level;
    }
};
// Loudness for this level: quiet at first, full in the worst band. 0 = silent.
inline float Gain(float strain,const Tuning& t) {
    if(!std::isfinite(strain)||strain<=0||!std::isfinite(t.volume)||!(t.volume>0))return 0;
    strain=std::min(strain,1.f);
    return std::min(t.volume,4.f)*(t.quiet+(1-t.quiet)*std::pow(strain,1.3f));
}
inline float Bpm(float strain,const Tuning& t) {
    const float lo=std::clamp(std::isfinite(t.bpmMin)?t.bpmMin:70.f,30.f,200.f);
    const float hi=std::clamp(std::isfinite(t.bpmMax)?t.bpmMax:lo,lo,200.f);
    return lo+(hi-lo)*std::clamp(std::isfinite(strain)?strain:0.f,0.f,1.f);
}
inline Pcm BuiltIn(){return {Rate,1,Synthesize()};}
// Above Knee of full scale, samples bend smoothly toward full scale instead of
// clipping: louder than the recording's own peak without crackle on the thump.
constexpr float Knee=.70f;
inline float Limit(float x) {
    const float a=std::fabs(x);
    if(!(a>Knee))return x;
    const float y=Knee+(1-Knee)*std::tanh((a-Knee)/(1-Knee));
    return x<0?-y:y;
}
// Exactly one beat period at this level's tempo: the beat at this level's
// loudness, then silence. A beat longer than the period is cut with a 10 ms
// fade. Queued back to back, the audio clock itself keeps the rhythm, and
// loudness and tempo follow life from one beat to the next. Level 0: empty.
inline std::vector<int16_t> Beat(const Pcm& source,float strain,const Tuning& t) {
    const float gain=Gain(strain,t);
    if(gain<=0||source.rate<=0||source.channels<1||source.samples.size()<size_t(source.channels))return {};
    const size_t channels=size_t(source.channels),have=source.samples.size()/channels;
    const size_t frames=size_t(std::lround(source.rate*60.0/Bpm(strain,t)));
    const size_t n=std::min(frames,have),fade=have>frames?std::min<size_t>(n,size_t(source.rate/100)):0;
    std::vector<int16_t> out(frames*channels,0);
    for(size_t f=0;f<n;++f) {
        const float k=fade&&f+fade>=n?float(n-f)/float(fade):1.f;
        for(size_t c=0;c<channels;++c)
            out[f*channels+c]=int16_t(std::lround(Limit(source.samples[f*channels+c]*gain*k/32768.f)*32767.f));
    }
    return out;
}
}
