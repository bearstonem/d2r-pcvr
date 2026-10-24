#include "notice_card.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
using namespace framevr;
using namespace framevr::notice;
namespace {
bool Near(float a,float b,float e=1e-4f){return std::abs(a-b)<=e;}
}
int main() {
    // The Start glyph becomes the three-bar sign; nothing else changes.
    char text[64]="Press \xEE\x80\x9D or \xEE\x80\x9D.";
    StartGlyph(text);
    assert(!std::strcmp(text,"Press \xE2\x89\xA1 or \xE2\x89\xA1."));
    char plain[16]="No glyph";StartGlyph(plain);assert(!std::strcmp(plain,"No glyph"));

    // Placed a metre out at the head's heading, below the eyes, facing them.
    const math::Vec head{.3f,1.6f,-.2f};
    for(float heading:{0.f,1.2f,-2.5f}) {
        const auto look=math::Multiply(math::Yaw(heading),panels::Pitch(.4f)); // pitch never moves it
        const auto p=Place(head,look);
        const auto toward=p.position-head;
        assert(Near(toward.y,-Drop));
        assert(Near(std::hypot(toward.x,toward.z),Distance));
        assert(Near(math::Heading(look),std::atan2(-toward.x,-toward.z)));
        const auto normal=math::Rotate(p.orientation,{0,0,1});
        const auto back=head-p.position;const float n=std::sqrt(math::Dot(back,back));
        assert(Near(normal.x,back.x/n)&&Near(normal.y,back.y/n)&&Near(normal.z,back.z/n));
        const auto up=math::Rotate(p.orientation,{0,1,0});
        assert(up.y>.9f); // upright, never rolled
    }

    // One card at a time, each ShowMs from when it is first shown.
    {
        Cards cards;assert(cards.Step(0).kind<0);
        cards.Push(Quest,100);cards.Push(Skills,100);cards.Push(Stats,100);
        auto c=cards.Step(150);assert(c.kind==Quest&&c.place);const auto first=c.sequence;
        cards.Shown(200);
        c=cards.Step(250);assert(c.kind==Quest&&!c.place&&c.sequence==first);
        c=cards.Step(200+ShowMs-1);assert(c.kind==Quest);
        c=cards.Step(200+ShowMs);assert(c.kind==Skills&&c.sequence!=first&&!c.place); // follows: same place
        cards.Shown(200+ShowMs);
        c=cards.Step(200+2*ShowMs);assert(c.kind==Stats&&!c.place);
        cards.Shown(200+2*ShowMs+5);
        c=cards.Step(205+3*ShowMs);assert(c.kind<0);
        // Later, a new card is placed afresh where the player looks.
        cards.Push(Quest,300+3*ShowMs+FollowMs);
        c=cards.Step(300+3*ShowMs+FollowMs);assert(c.kind==Quest&&c.place);
    }
    // A card that never reaches the screen is dropped after WaitMs.
    {
        Cards cards;cards.Push(Quest,0);cards.Push(Skills,0);
        assert(cards.Step(WaitMs-1).kind==Quest);
        auto c=cards.Step(WaitMs);assert(c.kind==Skills&&c.place); // nothing shown before it
        cards.Clear();assert(cards.Step(WaitMs+1).kind<0);
    }
    // A bounded queue: the current card and four waiting; extra cards are not kept.
    {
        Cards cards;for(int i=0;i<10;++i)cards.Push(i%Kinds,0);
        int shown=0;uint64_t now=0;
        for(auto c=cards.Step(now);c.kind>=0;c=cards.Step(now+=ShowMs)){cards.Shown(now);++shown;}
        assert(shown==5);
        cards.Push(-1,now);cards.Push(CardKinds,now);assert(cards.Step(now).kind<0);
    }
    // The mod's own message (a VR Settings paint failure) takes the same queue.
    {
        Cards cards;cards.Push(Message,0);
        auto c=cards.Step(0);assert(c.kind==Message&&c.place);
        cards.Shown(0);assert(cards.Step(ShowMs-1).kind==Message);assert(cards.Step(ShowMs).kind<0);
    }

    // Counted quest updates and level-ups within one session add cards.
    {
        Watch watch;std::vector<int> pushed;auto push=[&](int k){pushed.push_back(k);};
        watch.Step(7,true,4,3,true,0,push);assert(pushed.empty()); // a new session adopts level and count
        watch.Step(7,true,4,3,true,10,push);assert(pushed.empty());
        watch.Step(7,true,5,3,true,20,push);assert((pushed==std::vector<int>{Skills,Stats}));pushed.clear();
        watch.Step(7,true,5,3,true,30,push);assert(pushed.empty());
        watch.Step(7,false,0,4,false,40,push);assert((pushed==std::vector<int>{Quest}));pushed.clear(); // level unknown: kept
        watch.Step(7,true,5,4,false,50,push);assert(pushed.empty());
        // The native panel may open a little after the level changes...
        watch.Step(7,true,6,4,false,100,push);assert(pushed.empty());
        watch.Step(7,true,6,4,false,100+LevelWaitMs-1,push);assert(pushed.empty());
        watch.Step(7,true,6,4,true,100+LevelWaitMs-1,push);assert(pushed.size()==2);pushed.clear();
        // ...but a level with no native notification (not a level-up) adds nothing.
        watch.Step(7,true,8,4,false,5000,push);watch.Step(7,true,8,4,false,5000+LevelWaitMs,push);
        watch.Step(7,true,8,4,true,5001+LevelWaitMs,push);assert(pushed.empty());
        watch.Step(8,true,9,6,true,9000,push);assert(pushed.empty()); // another character
        watch.Step(0,false,0,6,true,9010,push);watch.Step(8,true,10,7,true,9020,push);assert(pushed.empty()); // session lost, then back: adopted
        watch.Step(8,true,10,8,true,9030,push);assert((pushed==std::vector<int>{Quest}));pushed.clear();
        Watch late;late.Step(9,false,0,0,true,0,push);late.Step(9,true,12,0,true,1,push);assert(pushed.empty()); // first known level: no card
        late.Step(9,true,13,0,true,2,push);assert(pushed.size()==2);
    }
    return 0;
}
