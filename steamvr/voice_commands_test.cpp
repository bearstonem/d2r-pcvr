#include "voice_commands.h"
#include <cassert>
#include <cstdio>
using namespace framevr::voice;
int main(){
    Gate gate;Command c{1,1000,7,149};
    assert(gate.Take(c,7,1100,true));assert(!gate.Take(c,7,1200,true));
    c={2,1400,7,149};assert(!gate.Take(c,7,1400,true));
    c={3,3000,7,149};assert(!gate.Take(c,7,3000,false));assert(!gate.Take(c,7,3001,true));
    c={4,4000,8,149};assert(!gate.Take(c,7,4000,true));
    c={5,4100,7,149};assert(!gate.Take(c,7,4600,true));
    c={6,5000,7,149};assert(gate.Take(c,7,5000,true));
    Templates patterns;Frame a{},b{};a[0]=1;b[1]=1;
    Pattern pa(30,a),pb(30,b);
    patterns[0]={pa,pa,pa};patterns[1]={pb,pb,pb};
    assert(Recognize(pa,patterns).label==0);assert(Recognize(pb,patterns).label==1);
    assert(Recognize(Pattern(45,a),patterns).label==0); // stretched shout
    assert(Recognize(Pattern(200,a),patterns).label==-1); // implausible duration
    patterns[1]={pa,pa,pa};assert(Recognize(pa,patterns).label==-1);
    assert(Recognize({},patterns).label==-1);
    assert(Features(std::vector<int16_t>(100)).empty());
    std::vector<int16_t> tone(Rate/2),other(Rate/2);
    for(int i=0;i<Rate/2;++i){tone[i]=int16_t(6000*std::sin(6.2831853*440*i/Rate));other[i]=int16_t(6000*std::sin(6.2831853*1100*i/Rate));}
    auto f=Features(tone),g=Features(other);assert(!f.empty()&&Distance(f,f)<1e-6f&&Distance(f,g)>.1f);
    patterns={};patterns[0]={f,f,f};patterns[1]={g,g,g};assert(Recognize(f,patterns).label==0);
    Segmenter segment;std::vector<int16_t> quiet(320),loud(320,4000);
    for(int i=0;i<100;++i)assert(segment.Feed(quiet,.012f).empty());
    for(int i=0;i<30;++i)assert(segment.Feed(loud,.012f).empty());
    for(int i=0;i<19;++i)assert(segment.Feed(quiet,.012f).empty());
    auto utterance=segment.Feed(quiet,.012f);assert(utterance.size()>=3200&&utterance.size()<Rate);
    for(int i=0;i<180;++i)assert(segment.Feed(loud,.012f).empty());
    for(int i=0;i<20;++i)assert(segment.Feed(quiet,.012f).empty()); // overlong speech discarded
    for(int i=0;i<20;++i)assert(segment.Feed(loud,.012f).empty());
    for(int i=0;i<19;++i)assert(segment.Feed(quiet,.012f).empty());
    assert(!segment.Feed(quiet,.012f).empty()); // resumes only after silence
    std::puts("Voice matching, ambiguity rejection, command freshness/session/cooldown and busy-discard tests passed");
}
