#include "active_effects.h"
#include "interaction_paint.h"
#include <cassert>
#include <cstdio>
using namespace framevr;
int main() {
    std::array<bool,effects::MaxStates> native{};
    auto sample=[&]{return effects::Capture(232,[&](int state){return int(native[state]);});};
    auto empty=sample();assert(empty.known&&effects::Count(empty)==0);
    // An actual aura, received shout, armor buff and poison coexist. There is
    // deliberately no class, skill catalog, trigger or selected-skill input.
    for(int state:{33,32,101,2})native[state]=true;
    auto active=sample();assert(active.known&&effects::Count(active)==4);
    const auto order=effects::Ordered(active);assert(order[0]->state==2);
    for(int state:{33,32,101})assert(active.Has(state));
    // Switching an aura / expiration / cleansing immediately changes the next sample.
    native[33]=false;native[49]=true;native[32]=false;native[2]=false;
    auto next=sample();assert(!next.Has(33)&&!next.Has(32)&&!next.Has(2)&&next.Has(49)&&next.Has(101));
    // Permanent passives, internal flags and learned skills are not status icons.
    native[70]=true;native[91]=true;native[185]=true;assert(sample()==next);
    // An aura on the player (Fanaticism 49 here); Holy Shield, curses and monster rows are not
    assert(effects::AnyAura(next)&&effects::AnyAura(active)&&!effects::AnyAura(empty));
    {effects::Status s;s.known=true;for(int state:{101,29,44,32,2})s.Set(state);assert(!effects::AnyAura(s));
     s.Set(43);assert(effects::AnyAura(s));s.known=false;assert(!effects::AnyAura(s));}
    unsigned calls=0;
    for(unsigned n:{0u,179u,513u})assert(!effects::Capture(n,[&](int){++calls;return 0;}).known);
    assert(!calls);
    auto oldBank=effects::Capture(185,[&](int id){assert(id<185);return id==33;});
    assert(oldBank.known&&oldBank.Has(33)&&!oldBank.Has(206));
    assert(!effects::Capture(232,[](int id){return id==33?-1:1;}).known);
    // No duplicate indicators, invalid IDs, or missing keys in the bounded bitset.
    std::array<bool,effects::MaxStates> seen{};effects::Status crowded;crowded.known=true;
    for(const auto& d:effects::Definitions){assert(d.state>0&&unsigned(d.state)<seen.size()&&!seen[d.state]);seen[d.state]=true;crowded.Set(d.state);}
    const auto crowdedOrder=effects::Ordered(crowded);bool passedHarm=false;
    for(const auto* d:crowdedOrder){assert(d);if(d->kind!=effects::Kind::Harm)passedHarm=true;else assert(!passedHarm);}
    assert(effects::Count(crowded)>effects::MaxCells&&effects::Cells(crowded)==21&&effects::Rows(crowded)==3);
    crowded.known=false;assert(!effects::Count(crowded));
    interaction::Snapshot s;s.session=5;s.at=200;s.effects=active;
    auto paint=interaction::Context(s,{},true);
    assert(interaction::CompanionImageCurrent(paint,s)); // works without pets
    s.effects=next;assert(!interaction::CompanionImageCurrent(paint,s));
    s.effects=active;s.session=6;assert(!interaction::CompanionImageCurrent(paint,s));
    s.session=5;s.effects={};assert(!interaction::CompanionImageCurrent(paint,s));
    s.effects=active;paint.fresh=false;assert(!interaction::CompanionImageCurrent(paint,s));
    // The complete map remains above a combined footer, even at its bounded maximum.
    companions::Roster pets;pets.known=true;pets.relevant=companions::Bit(4);pets.alive[4]=8;
    assert(wriststatus::Rows(pets,active)==2&&wriststatus::UsedHeight({},empty)==0);
    assert(wriststatus::UsedHeight(pets,active)<=wriststatus::Height);
    for(bool palm:{false,true})for(bool vitals:{false,true})for(int rows:{1,2,3,4,5,6}) {
        interaction::Pose hand{{.1f,.9f,-.4f},math::Yaw(.3f)},head{{0,1.6f,0},{0,0,0,1}};
        const auto map=interaction::NavigationMapPanel(hand,head,palm,vitals,interaction::CompanionSpace(rows));
        const auto footer=interaction::NavigationCompanionsPanel(hand,head,palm,vitals,rows);
        const auto delta=math::Rotate(math::Conjugate(map.q),footer.p-map.p);
        assert(std::fabs(delta.y+(interaction::WristMapSize+interaction::CompanionHeight(rows))*.5f+.006f)<.0001f);
        if(vitals){const auto p=interaction::VitalsPanel(hand,head,palm);const float y=math::Rotate(math::Conjugate(p.q),footer.p-p.p).y;
            assert(y-interaction::CompanionHeight(rows)*.5f>(palm?interaction::PalmHeight:interaction::WristHeight)*.5f+.009f);}
    }
    std::puts("Active effects: received buffs, curses, expiration, bank bounds, filtering, stale images and combined layout passed");
}
