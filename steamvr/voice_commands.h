#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
namespace framevr::voice {
inline constexpr int Count=6,Rate=16000,Bands=20;
inline constexpr const char* Names[Count]={"HUUUU","HAAAAA","ATTACK","FALL BACK","RRRRAAAAA","DIE"};
using Frame=std::array<float,Bands>;
using Pattern=std::vector<Frame>;
// Log-spaced filter energies: matching personal acoustic examples, not words
// or a cloud language model. No transcript or raw recording is retained.
inline Pattern Features(const std::vector<int16_t>& pcm){
    Pattern out;
    if(pcm.size()<3200||pcm.size()>Rate*4)return out;
    for(size_t start=0;start+400<=pcm.size();start+=160){
        Frame f{};float mean=0;
        for(int b=0;b<Bands;++b){
            const float hz=180.f*std::pow(6000.f/180.f,float(b)/(Bands-1));
            const float coeff=2*std::cos(6.2831853f*hz/Rate);
            float s0=0,s1=0,s2=0;
            for(int n=0;n<400;++n){
                const float window=.5f-.5f*std::cos(6.2831853f*n/399);
                s0=pcm[start+n]/32768.f*window+coeff*s1-s2;s2=s1;s1=s0;
            }
            f[b]=std::log(1e-6f+std::max(0.f,s1*s1+s2*s2-coeff*s1*s2));mean+=f[b];
        }
        mean/=Bands;float norm=0;
        for(float& v:f){v-=mean;norm+=v*v;}
        norm=std::sqrt(norm+1e-6f);for(float& v:f)v/=norm;
        out.push_back(f);
    }
    return out;
}
inline float Distance(const Pattern& a,const Pattern& b){
    if(a.empty()||b.empty()||a.size()>400||b.size()>400)return 1e6f;
    const float ratio=float(a.size())/b.size();if(ratio<.4f||ratio>2.5f)return 1e6f;
    std::vector<float> prev(b.size()+1,1e6f),next(prev);prev[0]=0;
    for(size_t i=1;i<=a.size();++i){
        std::fill(next.begin(),next.end(),1e6f);
        for(size_t j=1;j<=b.size();++j){
            if(std::fabs(float(i)/a.size()-float(j)/b.size())>.35f)continue;
            float d=0;for(int k=0;k<Bands;++k){float e=a[i-1][k]-b[j-1][k];d+=e*e;}
            next[j]=d+std::min({prev[j],next[j-1],prev[j-1]});
        }
        prev.swap(next);
    }
    return prev.back()/float(a.size()+b.size())+.03f*std::fabs(std::log(ratio));
}
struct Match {int label=-1;float best=1e6f,second=1e6f;};
using Templates=std::array<std::vector<Pattern>,Count>;
inline Match Recognize(const Pattern& p,const Templates& patterns,float threshold=.20f){
    Match m;int candidate=-1,trained=0;
    for(int label=0;label<Count;++label){
        if(patterns[label].size()<3)continue;
        ++trained;float distance=1e6f;
        for(const auto& sample:patterns[label])distance=std::min(distance,Distance(p,sample));
        if(distance<m.best){m.second=m.best;m.best=distance;candidate=label;}
        else m.second=std::min(m.second,distance);
    }
    if(trained>=2&&m.best<threshold&&m.best<m.second*.8f)m.label=candidate;
    return m;
}
// 20 ms frames, pre-roll, 400 ms trailing silence. Overlong speech is discarded
// until silence, so a continuing conversation cannot be sliced into commands.
struct Segmenter {
    std::vector<int16_t> speech,pre;int silent=0;bool overflow=false;
    std::vector<int16_t> Feed(const std::vector<int16_t>& chunk,float level){
        double power=0;for(auto x:chunk)power+=double(x)*x;
        const bool loud=!chunk.empty()&&std::sqrt(power/chunk.size())/32768>level;
        if(speech.empty()&&!overflow){
            if(!loud){pre=chunk;return {};}
            speech=pre;pre.clear();
        }
        if(!overflow)speech.insert(speech.end(),chunk.begin(),chunk.end());
        silent=loud?0:silent+1;
        if(speech.size()>Rate*3){speech.clear();overflow=true;}
        if(silent<20)return {};
        silent=0;
        if(overflow){overflow=false;return {};}
        if(speech.size()>6400)speech.resize(speech.size()-6080); // retain one silent trailing frame
        auto result=std::move(speech);speech.clear();return result;
    }
};
struct Command {uint64_t sequence=0,at=0,session=0;int skill=-1;};
struct Gate {
    uint64_t seen=0,last=0;
    bool Take(const Command& c,uint64_t session,uint64_t now,bool allowed){
        if(!c.sequence||c.sequence==seen)return false;
        seen=c.sequence; // Busy, stale and menu commands are discarded, never deferred.
        if(!allowed||!session||c.session!=session||now<c.at||now-c.at>400||c.skill<0||
           (last&&(now<last||now-last<1500)))return false;
        last=now;return true;
    }
};
}
