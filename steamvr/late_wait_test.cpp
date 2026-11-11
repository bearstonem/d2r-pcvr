#include "late_wait.h"
#include <cassert>
#include <cstring>
using namespace framevr::latewait;

int main() {
    constexpr int64_t P=8'333'333,D=2*P;   // a 120 Hz headset's frame, and the doubled one
    const auto ask=[](double now){Ask a;a.wanted=a.generating=a.pair=a.steamVr=true;a.nowMs=now;return a;};

    // A new session knows nothing: the wait comes first.
    Tracker t;
    assert(t.Decide(ask(0)).why==Why::NoFrameBefore&&!t.Decide(ask(0)).Late());

    // The session's first waits give the headset's own period: not doubled, the wait first.
    t.Waited(1'000'000'000,P,true);t.Posed(100);
    assert(t.shortest==P&&!t.Doubled());
    assert(t.Decide(ask(108)).why==Why::PeriodNotDoubled);

    // The bridge's presenter is up: the period doubles, and the next pair's wait may come late,
    // its poses for the last wait's time and one doubled period on.
    t.Waited(1'016'666'666,D,true);t.Posed(108.3);
    assert(t.Doubled()&&t.shortest==P);
    auto p=t.Decide(ask(125.0));
    assert(p.Late()&&p.why==Why::On&&p.time==1'016'666'666+D&&p.period==D);
    assert(std::strstr(Reason(p.why),"after the first eye"));

    // Every condition alone keeps the wait first, the key before all.
    {auto a=ask(125);a.wanted=false;assert(t.Decide(a).why==Why::KeyOff);}
    {auto a=ask(125);a.generating=false;assert(t.Decide(a).why==Why::NotGenerating);}
    {auto a=ask(125);a.pair=false;assert(t.Decide(a).why==Why::NotPair);}
    {auto a=ask(125);a.steamVr=false;assert(t.Decide(a).why==Why::NotSteamVr);}
    {Ask a;a.nowMs=125;assert(t.Decide(a).why==Why::KeyOff&&t.Decide(a).time==0);}

    // A pair that ran a headset frame over: the time one frame further; jitter of a few ms does not move it.
    assert(t.Expected(16.7)==1'016'666'666+D);
    assert(t.Expected(14.0)==1'016'666'666+D&&t.Expected(19.5)==1'016'666'666+D);
    assert(t.Expected(25.0)==1'016'666'666+D+P);
    assert(t.Expected(33.4)==1'016'666'666+D+2*P);
    assert(t.Expected(200)==1'016'666'666+D+8*P);   // bounded

    // A gap (a menu, loading, a hitch): the wait first again, once.
    assert(t.Decide(ask(108.3+GapMs)).why==Why::Gap);
    assert(t.Decide(ask(108.3+GapMs-1)).Late());

    // The runtime did not want the last frame drawn: the wait first.
    t.Waited(1'033'333'332,D,false);t.Posed(125);
    assert(t.Decide(ask(141)).why==Why::NotDrawn);

    // A wait or begin that failed: the next frame waits first, then late again.
    t.Waited(1'050'000'000,D,true);t.Posed(141);
    assert(t.Decide(ask(158)).Late());
    t.Failed();
    assert(t.Decide(ask(158)).why==Why::NoFrameBefore);
    t.Waited(1'066'666'666,D,true);t.Posed(158);
    assert(t.Decide(ask(175)).Late());

    // The presenter stopped (a menu's frame without the eyes): the period is the headset's again.
    t.Waited(1'083'333'333,P,true);t.Posed(175);
    assert(t.Decide(ask(183)).why==Why::PeriodNotDoubled);

    // A period SteamVR alone made longer reads as doubled too; a period of three frames as well.
    t.Waited(1'100'000'000,3*P,true);t.Posed(183);
    assert(t.Decide(ask(200)).Late());
    // ... but one and a half frames does not.
    t.Waited(1'100'000'000,P+P/2,true);t.Posed(200);
    assert(!t.Decide(ask(210)).Late());

    // A wait without a period is nothing to go by, and never the shortest.
    t.Waited(1'200'000'000,0,true);
    assert(t.shortest==P&&t.Decide(ask(210)).why==Why::NoFrameBefore);

    // A new session: its periods are its own.
    t.Reset();
    assert(!t.shortest&&!t.known&&t.Decide(ask(300)).why==Why::NoFrameBefore);
    // A session that only ever saw the doubled period has no shortest to compare with.
    t.Waited(2'000'000'000,D,true);t.Posed(300);
    assert(t.Decide(ask(316)).why==Why::PeriodNotDoubled);
    return 0;
}
