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
    std::puts("Voice command freshness/session/cooldown and busy-discard tests passed");
}
