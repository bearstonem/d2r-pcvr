#include "spell_motion.h"
#include "motion_sim.h"
#include <cstdio>
using namespace framevr;
using spellmotion::Motion;
using motionsim::Check;
using motionsim::Sim;
int main() {
    for(const auto m:spellmotion::necromancer::Order) {
        Sim s(m);s.Hold(1.f);
        Check(s.casts==0,"resting hands cast nothing");
    }
    std::puts("motions-necromancer-test: ok");
    return 0;
}
