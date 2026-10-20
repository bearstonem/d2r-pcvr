#include "heartbeat.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr;
static void Check(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static std::vector<uint8_t> Wav(int format,int channels,int rate,int bits,const std::vector<int16_t>& s) {
    std::vector<uint8_t> w;auto u32=[&](uint32_t v){for(int i=0;i<4;++i)w.push_back(uint8_t(v>>(8*i)));};
    auto u16=[&](uint16_t v){w.push_back(uint8_t(v));w.push_back(uint8_t(v>>8));};
    const uint32_t bytes=uint32_t(s.size()*2);
    w.insert(w.end(),{'R','I','F','F'});u32(36+bytes);w.insert(w.end(),{'W','A','V','E'});
    w.insert(w.end(),{'f','m','t',' '});u32(16);u16(uint16_t(format));u16(uint16_t(channels));u32(uint32_t(rate));
    u32(uint32_t(rate*channels*bits/8));u16(uint16_t(channels*bits/8));u16(uint16_t(bits));
    w.insert(w.end(),{'d','a','t','a'});u32(bytes);
    for(int16_t v:s)u16(uint16_t(v));
    return w;
}
static heartbeat::Tuning t0(){return {};}
int main() {
    {   // The built-in beat: two thumps, full scale without clipping, silent at the end.
        const auto beat=heartbeat::Synthesize();
        Check(beat.size()==size_t(heartbeat::Rate*.62),"one lub-dub, 0.62 s");
        int peak=0;for(auto v:beat)peak=std::max(peak,std::abs(int(v)));
        Check(peak>29000&&peak<=32767,"normalised near full scale");
        Check(std::abs(int(beat.back()))<50,"no click at the end");
        auto energy=[&](double a,double b){double e=0;for(size_t i=size_t(a*heartbeat::Rate);i<size_t(b*heartbeat::Rate);++i)e+=double(beat[i])*beat[i];return e;};
        Check(energy(0,.12)>energy(.12,.2)*4&&energy(.21,.33)>energy(.12,.2)*3,"lub, a gap, then dub");
        Check(energy(.5,.62)<energy(0,.12)*.01,"the tail is quiet");
    }
    heartbeat::Tuning t;
    {   // Loudness and tempo follow strain; none means silence.
        Check(heartbeat::Gain(0,t)==0&&heartbeat::Gain(-1,t)==0&&heartbeat::Gain(NAN,t)==0,"level 0, no heartbeat");
        Check(std::fabs(heartbeat::Gain(1e-4f,t)-t.quiet*t.volume)<.02f&&std::fabs(heartbeat::Gain(1,t)-t.volume)<1e-5f,"quiet at first touch, full when broken");
        float last=0;bool rising=true;
        for(float s=.05f;s<=1.0001f;s+=.05f){const float g=heartbeat::Gain(s,t);rising=rising&&g>last;last=g;}
        Check(rising,"louder with every step of strain");
        Check(heartbeat::Bpm(0,t)==70&&heartbeat::Bpm(1,t)==115&&heartbeat::Bpm(.5f,t)==92.5f,"tempo 70 to 115 bpm");
        heartbeat::Tuning off=t;off.volume=0;
        Check(heartbeat::Gain(1,off)==0,"heartbeat 0% is off");
        heartbeat::Tuning odd=t;odd.bpmMin=500;odd.bpmMax=10;
        Check(heartbeat::Bpm(0,odd)==200&&heartbeat::Bpm(1,odd)==200,"tempo bounded and ordered");
    }
    {   // Beats: exactly one period, scaled, padded or cut cleanly.
        const auto src=heartbeat::BuiltIn();
        const auto calm=heartbeat::Beat(src,.01f,t),broken=heartbeat::Beat(src,1,t);
        const size_t calmPeriod=size_t(std::lround(heartbeat::Rate*60.0/heartbeat::Bpm(.01f,t)));
        Check(calm.size()==calmPeriod&&broken.size()==size_t(std::lround(heartbeat::Rate*60.0/115)),"each buffer is one period at the current tempo");
        Check(heartbeat::Beat(src,0,t).empty(),"level 0 queues nothing");
        int cp=0,bp=0;for(auto v:calm)cp=std::max(cp,std::abs(int(v)));for(auto v:broken)bp=std::max(bp,std::abs(int(v)));
        Check(bp>cp*4,"a broken guard pounds; the first strain is a murmur (limited at the top)");
        Check(calm.back()==0,"a slow beat is padded with silence");
        heartbeat::Pcm longBeat{8000,2,std::vector<int16_t>(8000*2*2,12000)};   // 2 s of stereo, longer than any period
        const auto cut=heartbeat::Beat(longBeat,1,t);
        Check(cut.size()==size_t(std::lround(8000*60.0/115))*2,"a long recording is cut to the period (stereo frames)");
        Check(std::abs(int(cut[cut.size()-2]))*50<=std::abs(int(cut[0]))&&std::abs(int(cut[0]))>10000,"cut with a short fade to under 2%, no click");
        heartbeat::Tuning loud=t;loud.volume=4;
        heartbeat::Pcm full{8000,1,std::vector<int16_t>(100,32000)};
        const auto limited=heartbeat::Beat(full,1,loud);
        Check(limited[0]>32000,"over-unity volume bends to full scale instead of wrapping");
        Check(t.volume==2,"heartbeat defaults to 200%");
        // The limiter: untouched below the knee, smooth and bounded above it.
        Check(heartbeat::Limit(.5f)==.5f&&heartbeat::Limit(-.69f)==-.69f,"quiet samples pass unchanged");
        float prev=0,maxStep=0;bool monotonic=true;
        for(float x=0;x<=4;x+=.001f){const float y=heartbeat::Limit(x);monotonic=monotonic&&y>=prev;maxStep=std::max(maxStep,y-prev);prev=y;}
        Check(monotonic&&prev<=1&&prev>.99f&&maxStep<=.0011f,"no clipping corner: rises smoothly, never past full scale");
        Check(heartbeat::Limit(-4)==-heartbeat::Limit(4),"symmetric");
    }
    {   // The heartbeat winds down slower than the life that drives it recovers.
        heartbeat::Level l;
        Check(l.Step(.4f,1/90.,4)==.4f&&l.Step(1,1/90.,4)==1,"rises with strain at once");
        float v=1;double t=0;
        for(;t<1.2;t+=1/90.)v=l.Step(0,1/90.,4);
        Check(v>.65f&&v<.75f,"healed in an instant: 1.2 s later the heartbeat is still at ~70%");
        for(;t<4.1;t+=1/90.)v=l.Step(0,1/90.,4);
        Check(v==0,"silent by the fade time");
        l.Step(1,1/90.,4);v=l.Step(.2f,3,4);
        Check(v==.2f,"a loading hitch snaps to the strain");
        l.Reset();l.Step(1,1/90.,4);v=l.Step(.5f,1/90.,0);
        Check(v==.5f,"fade 0 follows strain exactly");
        // The embedded recording's shape: 0.5 s fits one beat at the fastest default tempo.
        heartbeat::Pcm rec{48000,2,std::vector<int16_t>(24000*2,1000)};
        const auto fast=heartbeat::Beat(rec,1,t0());
        Check(fast.size()>=rec.samples.size()&&fast[rec.samples.size()-1]==2000,"the recorded beat plays whole at 115 bpm");
    }
    {   // WAV override: 16-bit PCM only.
        heartbeat::Pcm p;
        Check(heartbeat::ParseWav(Wav(1,2,22050,16,{1,2,3,4}),p)&&p.rate==22050&&p.channels==2&&p.samples.size()==4&&p.samples[3]==4,"16-bit stereo PCM loads");
        Check(!heartbeat::ParseWav(Wav(3,1,44100,16,{1,2}),p),"float WAV refused");
        Check(!heartbeat::ParseWav(Wav(1,1,44100,8,{1,2}),p),"8-bit WAV refused");
        Check(!heartbeat::ParseWav({'n','o','p','e'},p),"not a WAV refused");
    }
    {   // VR Settings > Combat > Heartbeat starts at: one step per 10 % of life below it.
        auto level=[](float h,int start){return heartbeat::LifeLevel(h,100,start);};
        Check(level(100,50)==0&&level(50,50)==0,"silent at and above the start");
        Check(level(49,50)==.2f&&level(41,50)==.2f,"start 50: the first of five steps from 49 % to 40 %");
        Check(level(39,50)==.4f&&level(29,50)==.6f&&level(19,50)==.8f&&level(9,50)==1,"a step every 10 % lower, full below 10 %");
        Check(level(59,60)>0&&std::fabs(level(59,60)-1/6.f)<1e-6f&&level(90,90)==0&&level(89,90)>0,"60 (default) and 90 starts");
        Check(level(15,10)==0&&level(9,10)==1,"start 10: full at once below 10 %");
        Check(level(5,0)==0,"Off is silent");
        Check(level(0,50)==0&&heartbeat::LifeLevel(50,0,50)==0&&level(NAN,50)==0,"dead or unknown: winds down");
        Check(heartbeat::Gain(level(39,50),t)>heartbeat::Gain(level(49,50),t)&&heartbeat::Bpm(level(39,50),t)>heartbeat::Bpm(level(49,50),t)&&
              heartbeat::Bpm(level(9,50),t)==115,"each step is louder and faster");
    }
    {   // Wiping the visor clean quiets it until the next hit; healing to silence clears it.
        heartbeat::Hush h;
        Check(h.Step(.4f,false,false)==.4f,"beats at its step");
        Check(h.Step(.4f,true,false)==0&&h.Step(.4f,false,false)==0,"a clean visor quiets it and stays quiet");
        Check(h.Step(.4f,false,true)==.4f&&!h.On(),"the next hit resumes at its step");
        h.Step(.6f,true,false);
        Check(h.Step(0,false,false)==0&&!h.On()&&h.Step(.2f,false,false)==.2f,"healing above the start clears it");
        Check(h.Step(.2f,true,true)==.2f,"a hit with the last stroke wins");
        Check(h.Step(0,true,false)==0&&!h.On(),"nothing to quiet: no latch");
    }
    std::puts("Low-life heartbeat: synthesized lub-dub, loudness and tempo follow life, stepping every 10 % below the chosen start, silent when healthy or dead, hushed by a wiped visor until the next hit, one-period beats (padded, cut, stereo, saturation) and WAV loading passed");
}
