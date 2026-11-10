#include "spell_impacts.h"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace framevr;
using namespace framevr::spellimpacts;
using math::Vec;
namespace {
bool Close(float a,float b,float e=1e-4f) {return std::fabs(a-b)<=e;}
float Length(Vec v) {return std::sqrt(math::Dot(v,v));}
Passage Flight(uint32_t missile,Point from,Point to,uint64_t at=1000) {
    Passage p;p.session=7;p.at=at;p.missile=missile;p.from=from;p.to=to;return p;
}
}
int main() {
    // The table: the listed spells, in id order, each once; nothing else.
    {
        for(size_t i=1;i<std::size(Profiles);++i)assert(Profiles[i-1].missile<Profiles[i].missile);
        for(const auto& p:Profiles) {
            assert(Of(p.missile)==&p&&p.name[0]);
            assert(p.reach>0||p.blast>0);                   // each one does something
            assert((p.blast>0)==(p.blastImpulse>0));
            assert(p.impulse>=0&&p.impulse<=Heavy&&p.blast<=8);
        }
        for(uint32_t bolt:{56u,58u,59u,62u,90u,91u,93u,96u,98u,114u,119u,192u})assert(Of(bolt)&&Of(bolt)->reach>0);
        assert(Of(62)->blast>0&&Of(96)->blast>0&&Of(101)->blast>0&&!(Of(101)->reach>0)); // Fire Ball, Glacial Spike, Meteor burst
        assert(!(Of(58)->blast>0)&&!(Of(192)->blast>0));
        // Arrows, bolts, javelins, potions, Holy Bolt (it heals) and unknown rows are not spells here.
        for(uint32_t other:{0u,1u,12u,27u,31u,41u,44u,45u,55u,29u,82u,4095u,~0u})assert(!Of(other));
        assert(Of(62)->impulse>Of(58)->impulse&&Of(58)->impulse>Of(60)->impulse);
    }
    // Containers on the grid plane.
    {
        assert(Close(ContainerRadius(1,1),1)&&Close(ContainerRadius(2,1),2)&&Close(ContainerRadius(8,8),2)&&Close(ContainerRadius(0,0),.6f));
        assert(Close(Nearest({0,0},{10,0},{4,3}),.4f)&&Close(Nearest({0,0},{10,0},{-5,0}),0)&&Close(Nearest({0,0},{10,0},{50,1}),1));
        assert(Close(Nearest({3,3},{3,3},{9,9}),0)&&Close(Distance({3,3},{3,3},{3,7}),4));
        assert(Close(Distance({0,0},{10,0},{4,3}),3)&&Close(Distance({0,0},{10,0},{13,4}),5));
        const auto& bolt=*Of(58);const auto& ball=*Of(62);const auto& meteor=*Of(101);
        // A bolt's step across a pot, beside it, and stopping short of it.
        assert(PathBreaks(bolt,{0,0},{10,0},{5,1.5f},1));
        assert(PathBreaks(bolt,{0,0},{10,0},{5,2},1));          // just touching: its half cell and the pot's radius
        assert(!PathBreaks(bolt,{0,0},{10,0},{5,2.1f},1));
        assert(!PathBreaks(bolt,{0,0},{10,0},{13,0},1)&&PathBreaks(bolt,{0,0},{10,0},{12,0},1));
        assert(PathBreaks(bolt,{0,0},{10,0},{5,2.9f},2));        // a wide barrel is reached from farther
        assert(PathBreaks(bolt,{4,4},{4,4},{4,5},1));            // a bolt that has not moved yet
        assert(!PathBreaks(bolt,{0,0},{40,0},{20,0},1));         // a jump is not a flight
        assert(!PathBreaks(meteor,{0,0},{0,0},{0,0},1));         // falling: nothing until it lands
        const float nan=std::numeric_limits<float>::quiet_NaN();
        assert(!PathBreaks(bolt,{nan,0},{10,0},{5,0},1)&&!PathBreaks(bolt,{0,0},{10,0},{5,0},0));
        // Bursts.
        assert(BlastBreaks(ball,{10,10},{14,13},1)&&BlastBreaks(ball,{10,10},{16,10},1));
        assert(!BlastBreaks(ball,{10,10},{16.1f,10},1)&&!BlastBreaks(bolt,{10,10},{10,10},1));
        assert(BlastBreaks(meteor,{0,0},{9,0},1)&&!BlastBreaks(meteor,{0,0},{9.1f,0},1));
        assert(!BlastBreaks(ball,{nan,10},{10,10},1));
    }
    // Following the hero's missiles.
    {
        Followed f;
        assert(!f.Find(1,5,58,0));
        f.Add(1,5,58,{2,3},100);f.Add(1,6,62,{4,5},100);f.Add(0,7,58,{0,0},100);f.Add(1,0,58,{0,0},100);
        f.Add(1,8,58,{std::numeric_limits<float>::infinity(),0},100);
        assert(f.Count()==2);
        auto* e=f.Find(1,5,58,150);
        assert(e&&Close(e->at.x,2)&&Close(e->at.z,3)&&e->missile==58);
        e->at={9,9};e->seen=150;
        assert(Close(f.Find(1,5,58,160)->at.x,9));
        assert(!f.Find(2,5,58,160)&&f.Count()==2);               // another game's unit 5
        f.Add(1,5,59,{1,1},200);assert(f.Count()==2&&f.Find(1,5,59,200)&&Close(f.Find(1,5,59,200)->at.x,1)); // made again
        // The id handed to another missile, or not stepped for a long time: forgotten.
        assert(!f.Find(1,6,58,200)&&f.Count()==1);
        assert(!f.Find(1,5,59,200+Followed::Stale+1)&&f.Count()==0);
        f.Add(1,9,58,{0,0},10);f.Remove(*f.Find(1,9,58,10));assert(f.Count()==0&&!f.Find(1,9,58,10));
        // Full of live flights: the one stepped longest ago makes room; a stale one does first.
        for(unsigned i=0;i<Followed::Most;++i)f.Add(1,100+i,58,{float(i),0},1000+i);
        assert(f.Count()==Followed::Most);
        f.Add(1,9000,58,{0,0},2000);
        assert(f.Count()==Followed::Most&&!f.Find(1,100,58,2000)&&f.Find(1,101,58,2000)&&f.Find(1,9000,58,2000));
        f.Clear();assert(f.Count()==0);
        f.Add(1,1,58,{0,0},0);f.Add(1,2,58,{0,0},Followed::Stale+5);
        assert(f.Count()==1&&f.Find(1,2,58,Followed::Stale+5)); // took the stale one's place
    }
    // The mailbox: newest kept, stale and foreign dropped, emptied by a take.
    {
        Mailbox box;std::array<Passage,Mailbox::Most> out{};
        assert(box.Take(7,1000,out)==0);
        box.Post(Flight(58,{0,0},{1,0}));
        box.Post(Flight(55,{0,0},{1,0}));                        // not a listed spell
        auto other=Flight(58,{0,0},{1,0});other.session=0;box.Post(other);
        box.Post(Flight(58,{std::numeric_limits<float>::quiet_NaN(),0},{1,0}));
        assert(box.Waiting()==1);
        assert(box.Take(7,1100,out)==1&&out[0].missile==58&&box.Waiting()==0&&box.Take(7,1100,out)==0);
        box.Post(Flight(58,{0,0},{1,0},1000));box.Post(Flight(62,{0,0},{1,0},1200));
        assert(box.Take(7,1000+Mailbox::Fresh+1,out)==1&&out[0].missile==62); // the first is too old
        box.Post(Flight(58,{0,0},{1,0},1000));assert(box.Take(8,1000,out)==0&&box.Waiting()==0); // another session's
        box.Post(Flight(58,{0,0},{1,0},2000));assert(box.Take(7,1000,out)==0); // a clock that went back
        for(unsigned i=0;i<Mailbox::Most+10;++i)box.Post(Flight(58,{float(i),0},{float(i)+1,0}));
        assert(box.Waiting()==Mailbox::Most);
        assert(box.Take(7,1000,out)==Mailbox::Most&&Close(out[0].from.x,10)&&Close(out[Mailbox::Most-1].from.x,Mailbox::Most+9));
    }
    // A passage in the rendered world: the grid plane on the floor, sheared as the game draws it.
    {
        const float upm=4,floor=2;
        auto p=Flight(58,{40,80},{44,80});p.flat=false;p.fromHeight=1.2f;p.toHeight=1.5f;
        auto push=ToWorld(p,floor,upm);
        const Vec ground=coordinates::RenderGround({10,0,20},floor);
        assert(Valid(push)&&!push.blast&&!push.flat&&Close(push.radius,BoltRadius)&&Close(push.impulse,Light));
        assert(Close(push.a.x,ground.x)&&Close(push.a.y,floor+1.2f)&&Close(push.a.z,ground.z));
        assert(Close(push.b.x,ground.x+1)&&Close(push.b.y,floor+1.5f)&&Close(push.b.z,ground.z));
        // And back: the point's cell is the passage's.
        const auto grid=coordinates::GridPlane({push.b.x,floor,push.b.z});
        assert(Close(grid.x*upm,44,1e-3f)&&Close(grid.z*upm,80,1e-3f));
        p.missile=62;assert(Close(ToWorld(p,floor,upm).radius,HeavyRadius)&&Close(ToWorld(p,floor,upm).impulse,Heavy));
        p.flat=true;push=ToWorld(p,floor,upm);
        assert(push.flat&&Close(push.radius,FlatRadius)&&Close(push.a.y,floor+FlatHeight)&&Close(push.b.y,floor+FlatHeight));
        p.blast=true;p.flat=false;push=ToWorld(p,floor,upm);
        assert(push.blast&&Close(push.radius,5/upm)&&Close(push.impulse,Burst)&&push.a==push.b&&Close(push.b.y,floor+1.5f));
        p.missile=58;assert(!Valid(ToWorld(p,floor,upm)));       // a bolt has no burst
        p.missile=55;p.blast=false;assert(!Valid(ToWorld(p,floor,upm)));
        p.missile=62;assert(!Valid(ToWorld(p,floor,0))&&!Valid(ToWorld(p,std::numeric_limits<float>::quiet_NaN(),upm)));
        p.missile=101;p.blast=true;assert(ToWorld(p,floor,1).radius<=MaxRadius);
    }
    // What a push does to a body.
    {
        Push bolt;bolt.a={0,1,0};bolt.b={1,1,0};bolt.radius=.12f;bolt.impulse=Light;
        assert(Valid(bolt));
        auto k=KickOn(bolt,{.5f,1.05f,.1f},.05f);
        assert(k.hit&&Close(Length(k.impulse),Light)&&k.impulse.x>0&&k.impulse.y>0&&Close(k.impulse.z,0)&&k.impulse.x>k.impulse.y);
        assert(!KickOn(bolt,{.5f,1,.5f},.05f).hit);              // beside it
        assert(!KickOn(bolt,{.5f,0,0},.05f).hit);                // under it: a cup on the floor below a bolt at 1 m
        assert(!KickOn(bolt,{1.5f,1,0},.05f).hit&&KickOn(bolt,{1.1f,1,0},.05f).hit); // past its end
        assert(KickOn(bolt,{.5f,1,.3f},.2f).hit);                // a big body reaches it
        auto flat=bolt;flat.flat=true;
        assert(KickOn(flat,{.5f,0,0},.05f).hit&&KickOn(flat,{.5f,2.4f,.1f},.05f).hit&&!KickOn(flat,{.5f,0,.5f},.05f).hit);
        auto still=bolt;still.b=still.a;                          // not moving: away from itself
        k=KickOn(still,{0,1,.1f},.05f);assert(k.hit&&k.impulse.z>0&&Close(k.impulse.x,0));
        Push blast;blast.a=blast.b={0,1,0};blast.radius=1.5f;blast.impulse=Burst;blast.blast=true;
        const auto centre=KickOn(blast,{0,1,0},.05f),nearby=KickOn(blast,{.5f,1,0},.05f),edge=KickOn(blast,{0,1,-1.5f},.05f);
        assert(centre.hit&&Close(Length(centre.impulse),Burst)&&centre.impulse.y>0);
        assert(nearby.hit&&nearby.impulse.x>0&&nearby.impulse.y>0&&Length(nearby.impulse)<Burst&&Length(nearby.impulse)>Length(edge.impulse));
        assert(edge.hit&&edge.impulse.z<0&&Close(Length(edge.impulse),Burst*EdgeShare));
        assert(!KickOn(blast,{0,1,1.7f},.05f).hit&&KickOn(blast,{0,1,1.7f},.3f).hit);
        assert(!KickOn(blast,{0,3,0},.05f).hit);                 // a burst is a ball, not a column
        auto bad=bolt;bad.radius=0;assert(!Valid(bad)&&!KickOn(bad,{.5f,1,0},.05f).hit);
        bad=bolt;bad.impulse=std::numeric_limits<float>::infinity();assert(!Valid(bad));
        bad=bolt;bad.b={100,1,0};assert(!Valid(bad));
        assert(!KickOn(bolt,{std::numeric_limits<float>::quiet_NaN(),1,0},.05f).hit);
        // The speed it gives: light things are capped, heavy ones barely move.
        assert(Close(KickSpeed(Light,.25f),2)&&Close(KickSpeed(Heavy,.01f),MaxKickSpeed)&&KickSpeed(Light,5)<.11f);
        assert(KickSpeed(0,1)==0&&KickSpeed(1,0)==0&&KickSpeed(std::numeric_limits<float>::quiet_NaN(),1)==0);
    }
    std::puts("spell impacts: ok");
}
