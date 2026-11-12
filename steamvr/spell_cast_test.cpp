#include "spell_cast.h"
#include "holy_bolt.h"
#include "bow_flight.h"
#include "hand_controls.h"
#include <cassert>
#include <cstdio>
using namespace framevr;
struct Sim {
    skills::HandControls controls;
    skills::HandInput input;skills::HandOutput output;
    Sim(){input.context=input.layout=1;input.active=true;input.independentSlots=(1u<<skills::TriggerSlot(0))|(1u<<skills::TriggerSlot(1));Step(3);}
    void Step(int n=1){while(n--){input.time+=.02;output=controls.Step(input);}}
};
// Production invokes the same wrapper: verify all callback arguments land in
// their proper slots, including the leading bank absent from legacy callbacks.
unsigned filterCalls=0;
int NativeFilter(uint8_t bank,uintptr_t target,const holybolt::CollisionArgs* args) {
    ++filterCalls;
    assert(bank>=1&&bank<=3&&target==0x50000&&args);
    assert(args->game==0x10000&&args->missile==0x20000&&args->owner==0x30000&&args->row==0x40000);
    return bank==2?1:0;
}
int main() {
    const holybolt::CollisionArgs filterArgs{0x10000,0x20000,0x30000,0x40000};
    for(uint8_t bank:{1,2,3})assert(holybolt::CanCollide(NativeFilter,bank,0x50000,filterArgs)==(bank==2));
    assert(filterCalls==3);
    for(uint8_t bank:{0,4})assert(!holybolt::CanCollide(NativeFilter,bank,0x50000,filterArgs));
    assert(!holybolt::CanCollide(nullptr,2,0x50000,filterArgs)&&filterCalls==3);
    spellcast::CastCue cue;
    assert(cue.Take(1,0,1,100,100));
    for(int member=1;member<24;++member)assert(!cue.Take(1,0,1,100,110));
    assert(cue.Take(1,1,1,100,110)&&cue.Take(1,0,2,120,120));
    assert(!cue.Take(1,0,1,100,120));
    assert(!cue.Take(1,0,3,100,351)&&!cue.Take(1,0,3,100,99));
    assert(cue.Take(2,0,1,400,400));
    assert(!cue.Take(0,0,1,400,400)&&!cue.Take(2,2,1,400,400));
    {
        using spellcast::Failure;
        for(bool town:{false,true})for(bool mana:{false,true}) {
            bool selected=true;unsigned calls=0;
            const auto result=spellcast::StartWithFeedback([&]{
                assert(selected);return spellcast::Refusal(town,mana);
            },[&]{++calls;selected=false;return false;});
            assert(calls==1&&!result.accepted);
            assert(result.failure==(town?Failure::Town:mana?Failure::None:Failure::Mana));
        }
        const auto accepted=spellcast::StartWithFeedback([]{return Failure::Mana;},[]{return true;});
        assert(accepted.accepted&&accepted.failure==Failure::None);
        spellcast::CastWarning warning;
        warning.Request(1,7,1000,Failure::Mana);
        warning.Request(1,7,1001,Failure::Town);
        assert(warning.Take(1,7,1002,true)==Failure::Town);
        warning.Request(1,7,1100,Failure::Town);
        assert(warning.Take(1,7,1100,true)==Failure::None);
        warning.Request(1,7,1300,Failure::Town);
        assert(warning.Take(1,7,1801,true)==Failure::None);
        warning.Request(1,7,1900,Failure::None);
        assert(warning.Take(1,7,1900,true)==Failure::None);
    }

    spellcast::PlanarAim aim[2];
    for(int yaw=0;yaw<360;++yaw)for(int pitch:{-80,-40,0,40,80}) {
        const float y=yaw*.01745329252f,p=pitch*.01745329252f;
        const math::Vec expected={std::sin(y),0,std::cos(y)};
        const auto flat=aim[0].Update(1,1,{std::cos(p)*expected.x,std::sin(p),std::cos(p)*expected.z},{});
        assert(flat.y==0&&math::Dot(flat,expected)>.99999f);
    }
    aim[0].Update(1,1,{1,0,0},{});
    assert(aim[0].Update(1,1,{.001f,1,.001f},{}).x==1);
    assert(aim[1].Update(1,1,{0,1,0},{0,0,-1}).z==-1);
    assert(math::Dot(aim[0].Update(1,2,{0,1,0},{}),{1,1,1})==0);
    assert(aim[0].Update(2,2,{0,-1,0},{0,0,1}).z==1);
    assert(aim[0].Update(2,2,{std::numeric_limits<float>::quiet_NaN(),0,0},{}).z==1);
    assert(aim[0].Update(2,2,{0,std::numeric_limits<float>::infinity(),0},{}).z==1);
    for(float pitch:{-.9f,.9f}) {
        const math::Vec direction={std::cos(pitch),std::sin(pitch),0};
        const auto free=aim[0].Update(3,1,direction,{},false);
        assert(free.x==direction.x&&free.y==direction.y&&free.z==direction.z);
        const auto flat=aim[0].Update(3,1,direction,{},true);
        assert(flat.x==1&&flat.y==0&&flat.z==0);
    }
    assert(aim[0].Update(3,1,{0,1,0},{},false).y==1);
    for(int id:{36,39,45,47})assert(spellcast::Candidate(1,id,0,0,56,0));
    assert(spellcast::Candidate(1,38,0,17,-1,0,56));
    assert(!spellcast::Candidate(1,38,0,0,56,0));
    assert(!spellcast::Candidate(1,38,0,17,-1,0,57));
    assert(!spellcast::Candidate(1,38,0,17,56,0,56));
    assert(!spellcast::Candidate(0,38,0,17,-1,0,56));
    assert(spellcast::LaunchCapacity(38)==24&&spellcast::LaunchCapacity(47)==1);
    assert(!spellcast::Candidate(0,47,0,0,56,0));
    assert(!spellcast::Candidate(1,47,1,0,56,0));
    assert(!spellcast::Candidate(1,47,0,8,56,0));
    assert(!spellcast::Candidate(1,47,0,0,56,2));
    assert(!spellcast::Supported(219)&&!spellcast::Supported(64));
    // From the hand (owner, 2026-11-09): Lightning flies as Fire Bolt; Chain Lightning keeps its server function 26
    // and its native leaps, only its start and heading moved to the hand (one missile a cast, as Fire Bolt's).
    assert(spellcast::Supported(49)&&spellcast::Candidate(1,49,0,0,57,0)&&!spellcast::NativePath(49)&&spellcast::LaunchCapacity(49)==1);
    assert(spellcast::Supported(53)&&spellcast::NativePath(53)&&spellcast::LaunchCapacity(53)==1);
    assert(spellcast::Candidate(1,53,0,26,60,0));
    assert(!spellcast::Candidate(1,53,0,0,60,0)&&!spellcast::Candidate(1,53,0,26,-1,0)&&!spellcast::Candidate(1,53,0,17,-1,0,56));
    assert(!spellcast::Candidate(0,53,0,26,60,0)&&!spellcast::Candidate(1,53,1,26,60,0)&&!spellcast::Candidate(1,53,0,26,60,2));
    assert(spellcast::Candidate(2,84,0,0,192,0xc0020400u));
    for(unsigned character:{0u,1u,3u,4u,5u,6u})assert(!spellcast::Candidate(character,84,0,0,192,0xc0020400u));
    assert(!spellcast::Candidate(2,84,0,0,58,0)&&!spellcast::Candidate(2,84,0,8,192,0));
    assert(!spellcast::Candidate(2,47,0,0,62,0));
    assert(!spellcast::Supported(78)&&!spellcast::Supported(88)); // placements retain native casting
    assert(spellcast::Candidate(3,101,0,0,55,0xc6020400u)&&!spellcast::NativePath(101));
    for(int hand:{0,1}) {
        Sim s;s.input.independentSlots=0;
        if(spellcast::Candidate(3,101,0,0,55,0xc6020400u))s.input.independentSlots|=1u<<skills::TriggerSlot(hand);
        s.Step(3);s.input.trigger[hand]=1;s.Step();
        assert(s.output.spells[hand]==skills::TriggerSlot(hand)&&s.output.slot<0);
        s.input.trigger[hand]=0;s.Step();assert(s.output.spells[hand]<0);
    }
    for(unsigned character:{0u,1u,2u,4u,5u,6u})assert(!spellcast::Candidate(character,101,0,0,55,0xc6020400u));
    assert(!spellcast::Candidate(3,101,0,0,192,0)&&!spellcast::Candidate(3,101,0,7,55,0));
    // Assist must ignore beasts, NPCs and malformed tables, while retaining
    // both undead flags, demons and a friendly mercenary's direct healing ray.
    std::array<uint8_t,508> monster{};
    for(int flags=0;flags<256;++flags) {
        monster[0x3d]=uint8_t(flags);
        const bool attackable=(flags&0x80)&&!(flags&1);
        assert(holybolt::AssistTarget(monster,true)==(attackable&&(flags&0x38)?holybolt::Target::Enemy:holybolt::Target::Ignore));
        assert(holybolt::AssistTarget(monster,false)==(attackable?holybolt::Target::Friend:holybolt::Target::Ignore));
    }
    assert(holybolt::AssistTarget(std::span(monster).first(507),true)==holybolt::Target::Ignore);
    constexpr int breaks[]={0,9,20,37,63,105,200};
    for(unsigned i=0;i<7;++i){
        assert(spellcast::CastFrames(breaks[i])==13-i);
        if(i)assert(spellcast::CastFrames(breaks[i]-1)==14-i);
    }
    assert(spellcast::CastFrames(-1)==13&&spellcast::CastFrames(100000)==7);
    constexpr int necroBreaks[]={0,9,18,30,48,75,125};
    for(unsigned i=0;i<7;++i){
        assert(spellcast::CastFrames(necroBreaks[i],2)==15-i);
        assert(spellcast::CastFrames(necroBreaks[i],3)==15-i);
        if(i)assert(spellcast::CastFrames(necroBreaks[i]-1,2)==16-i);
    }
    {
        bowflight::Flight spear;spear.shot.kind=bowflight::Kind::Spell;spear.shot.skill=84;
        spear.shot.origin={10,1.4f,10};spear.shot.player={10,0,10};spear.shot.direction={0,0,1};
        spear.shot.unitsPerMetre=4;spear.shot.session=spear.shot.sequence=1;spear.shot.slot=0;
        assert(bowflight::Valid(spear.shot)&&!spear.NativePath());
        float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        bowflight::SpellMatrix(spear,0,matrix);
        const math::Vec emitter={matrix[12]+4*matrix[4]-3*matrix[8],matrix[13]+4*matrix[5]-3*matrix[9],matrix[14]+4*matrix[6]-3*matrix[10]};
        assert(bowflight::Length(emitter-spear.shot.origin*spear.shot.unitsPerMetre)<.0001f);
    }
    spellcast::Cadence cadence;
    assert(cadence.Ready(1,0,0)&&cadence.Ready(1,1,0));
    cadence.Fired(1,0,100,13);
    assert(!cadence.Ready(1,0,112)&&cadence.Ready(1,1,100));
    cadence.Fired(1,1,100,8);
    assert(!cadence.Ready(1,1,107)&&cadence.Ready(1,1,108)&&cadence.Ready(1,0,113));
    assert(cadence.Ready(2,0,1));
    cadence.Fired(2,0,UINT32_MAX-5,13);
    assert(!cadence.Ready(2,0,6)&&cadence.Ready(2,0,7));
    {
        spellcast::CastWarning warning;
        warning.Request(0,7,1000,spellcast::Failure::Mana);assert((warning.Take(0,7,1000,true)==spellcast::Failure::None));
        warning.Request(1,7,1000,spellcast::Failure::Mana); // right hand fails
        warning.Request(1,7,1000,spellcast::Failure::Mana); // left hand fails in the same tick
        assert(warning.Take(1,7,1020,true)==spellcast::Failure::Mana);
        assert((warning.Take(1,7,1020,true)==spellcast::Failure::None));
        warning.Request(1,7,1100,spellcast::Failure::Mana);assert((warning.Take(1,7,1120,true)==spellcast::Failure::None));
        warning.Request(1,7,1250,spellcast::Failure::Mana);assert(warning.Take(1,7,1250,true)==spellcast::Failure::Mana);
        warning.Request(1,7,1500,spellcast::Failure::Mana);assert((warning.Take(1,7,2001,true)==spellcast::Failure::None)); // stale UI callback
        warning.Request(1,7,2100,spellcast::Failure::Mana);assert((warning.Take(1,7,2100,false)==spellcast::Failure::None)); // menu/death
        assert((warning.Take(1,7,2101,true)==spellcast::Failure::None)); // no delayed warning after reopening
        warning.Request(1,7,2400,spellcast::Failure::Mana);assert((warning.Take(2,7,2400,true)==spellcast::Failure::None)); // same ID, new session
        warning.Request(2,7,2401,spellcast::Failure::Mana);assert(warning.Take(2,7,2401,true)==spellcast::Failure::Mana);
        warning.Request(2,7,2700,spellcast::Failure::Mana);assert((warning.Take(2,8,2700,true)==spellcast::Failure::None)); // wrong player
        warning.Request(2,8,2701,spellcast::Failure::Mana);assert(warning.Take(2,8,2701,true)==spellcast::Failure::Mana);
        warning.Request(2,8,3000,spellcast::Failure::Mana);assert((warning.Take(2,8,2999,true)==spellcast::Failure::None)); // clock regression
        warning.Request(3,0,0,spellcast::Failure::Mana);assert(warning.Take(3,0,0,true)==spellcast::Failure::Mana); // ID/timestamp zero are valid
        // Feedback never advances either hand's casting cadence.
        assert(cadence.Ready(3,0,0)&&cadence.Ready(3,1,0));
    }
    {
        Sim s;s.input.trigger[0]=s.input.trigger[1]=1;s.Step();
        assert(s.output.slot==-1&&s.output.spells[0]==skills::TriggerSlot(0)&&s.output.spells[1]==skills::TriggerSlot(1));
        const auto left=s.output.activation[1],right=s.output.activation[0];
        s.input.trigger[0]=0;s.Step();assert(s.output.spells[0]<0&&s.output.activation[1]==left);
        s.input.trigger[0]=1;s.Step();assert(s.output.activation[0]!=right&&s.output.activation[1]==left);
        s.input.click[1]=true;s.Step();assert(s.output.wheel==1&&s.output.spells[0]<0&&s.output.spells[1]<0);
    }
    // Fireball and Charged Bolt enter the same independent-input path. Neither
    // produces a native cast slot (and therefore neither requests its animation).
    {
        Sim s;s.input.independentSlots=0;
        if(spellcast::Candidate(1,47,0,0,62,0))s.input.independentSlots|=1u<<skills::TriggerSlot(0);
        if(spellcast::Candidate(1,38,0,17,-1,0,56))s.input.independentSlots|=1u<<skills::TriggerSlot(1);
        s.Step(3);s.input.trigger[0]=s.input.trigger[1]=1;s.Step();
        assert(s.output.slot==-1&&s.output.spells[0]>=0&&s.output.spells[1]>=0);
        s.Step(50);assert(s.output.slot==-1&&s.output.spells[0]>=0&&s.output.spells[1]>=0);
    }
    for(int event=0;event<6;++event){
        Sim s;s.input.trigger[0]=s.input.trigger[1]=1;s.Step();
        if(event==0)s.input.active=false;if(event==1)++s.input.context;if(event==2)++s.input.layout;
        if(event==3)s.input.occupied=true;if(event==4)s.input.time+=1;if(event==5)s.input.independentSlots=0;
        s.Step();assert(s.output.spells[0]<0&&s.output.spells[1]<0);
        s.input.active=true;s.input.occupied=false;s.Step(3);assert(s.output.spells[0]<0&&s.output.spells[1]<0&&s.output.slot<0);
    }
    {
        Sim s;s.input.face[1][0]=true;s.Step();assert(s.output.slot>=0);
        s.input.trigger[0]=1;s.Step();assert(s.output.spells[0]<0);
        s.input.face[1][0]=false;s.Step(3);assert(s.output.spells[0]<0); // no delayed cast
    }
    bowflight::Shot shot;shot.kind=bowflight::Kind::Spell;shot.skill=47;shot.slot=0;shot.hand=0;
    shot.session=shot.sequence=1;shot.unitsPerMetre=4;shot.origin={0,1.6f,0};shot.direction={0,1,0};
    assert(bowflight::Valid(shot));
    bowflight::Gate gate{true,1,0,0,0,false};
    assert(bowflight::Allowed(shot,gate,0));
    for(float pitch:{-.9f,.9f})for(bool planar:{false,true}) {
        auto holy=shot;holy.skill=101;holy.missile=55;holy.planar=planar;
        holy.direction=aim[0].Update(4,1,{std::cos(pitch),std::sin(pitch),0},{},planar);
        assert(bowflight::Valid(holy)&&bowflight::Allowed(holy,gate,0));
        bowflight::Flight f;f.shot=holy;f.distance=3;
        assert(!f.NativePath());
        assert(std::fabs((f.Point().y-holy.origin.y)-(planar?0:3*std::sin(pitch)))<.0001f);
    }
    auto arrow=shot;arrow.kind=bowflight::Kind::Arrow;
    assert(!bowflight::Allowed(arrow,gate,0)); // widening spell eligibility must not release a canceled arrow
    const auto tip=bowflight::Tip(shot,0);assert(tip.y==shot.origin.y); // no arrow-length offset
    bowflight::Flight flight;flight.shot=shot;flight.distance=3;
    float matrix[16]={2,0,0,0,0,3,0,0,0,0,4,0,0,0,0,1};
    bowflight::SpellMatrix(flight,0,matrix);
    for(int i=0;i<3;++i)assert(std::fabs(bowflight::Length({matrix[i*4],matrix[i*4+1],matrix[i*4+2]})-(i+2))<.001f);
    const math::Vec emitter={matrix[12]+4*matrix[4]+2.3f*matrix[8],matrix[13]+4*matrix[5]+2.3f*matrix[9],matrix[14]+4*matrix[6]+2.3f*matrix[10]};
    assert(std::fabs(emitter.y-18.4f)<.001f&&std::fabs(emitter.x)<.001f&&std::fabs(emitter.z)<.001f);
    for(int skill:{36,39,45,47,84,101})for(int yaw=0;yaw<360;yaw+=5)for(int pitch:{-90,-40,0,40,90}) {
        const float y=yaw*.01745329252f,p=pitch*.01745329252f;
        flight.shot.skill=skill;flight.shot.direction={std::cos(p)*std::sin(y),std::sin(p),std::cos(p)*std::cos(y)};
        float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};bowflight::SpellMatrix(flight,0,m);
        const float z=skill==47?2.3f:skill==84?-3.f:0;
        const math::Vec visible={m[12]+4*m[4]+z*m[8],m[13]+4*m[5]+z*m[9],m[14]+4*m[6]+z*m[10]};
        assert(bowflight::Length(visible-bowflight::Tip(flight.shot,3)*shot.unitsPerMetre)<.0001f);
        assert(math::Dot({m[8],m[9],m[10]},flight.shot.direction)>.9999f);
    }
    assert(std::fabs(bowflight::FlightSpeed(shot,4096)-12.5f)<.001f);
    // Native wandering displacement is independent of current controller aim,
    // retains horizontal muzzle placement, and does not overshoot a wall or hit.
    bowflight::Flight charged;charged.shot=shot;charged.shot.skill=38;charged.shot.direction={1,0,0};
    charged.BeginNativePath({100,0,200},60);charged.alive=true;
    charged.NativePosition({102,0,201},100);
    assert(charged.Point().x==.5f&&charged.Point().y==0&&charged.Point().z==.25f);
    assert(charged.TailAt(120).x==.25f&&charged.TailAt(200).x==.5f);
    charged.NativePosition({101,0,202},140);
    assert(charged.Point().x==.25f&&charged.Point().z==.5f);
    charged.alive=false;assert(charged.TailAt(140).z==.5f);
    float cm[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    bowflight::SpellMatrix(charged,140,cm);
    const math::Vec chargedEmitter={cm[12]-.25f*cm[4],cm[13]-.25f*cm[5],cm[14]-.25f*cm[6]};
    assert(bowflight::Length(chargedEmitter-charged.Point()*shot.unitsPerMetre)<.0001f);
    // First frame, native updates, interpolation and VFX all stay on the
    // launch floor, regardless of hand height or elevated/sheared world floor.
    for(float floor:{0.f,1.25f,-.5f})for(float handHeight:{.3f,1.6f,2.2f}) {
        charged.shot.player={10,floor,20};
        charged.shot.origin={10.15f,floor+handHeight,20.35f};
        const auto grid=bowflight::GridPoint(charged.shot,charged.shot.origin)*shot.unitsPerMetre;
        charged.BeginNativePath(grid,100);charged.alive=true;
        assert(charged.Point().y==floor&&charged.TailAt(100).y==floor);
        charged.NativePosition(grid+math::Vec{2,0,-1},140);
        for(uint64_t at:{140,160,180}) {
            assert(charged.TailAt(at).y==floor);
            bowflight::SpellMatrix(charged,at,cm);
            assert(std::fabs((cm[13]-.25f*cm[5])-floor*shot.unitsPerMetre)<.0001f);
        }
        const auto impact=bowflight::GridPoint(charged.shot,charged.Point())*shot.unitsPerMetre;
        assert(bowflight::Length(impact-(grid+math::Vec{2,0,-1}))<.0001f);
        // Straight projectile heights are unaffected by the ground-spell rule.
        for(int skill:{39,47}) {
            auto straight=charged;straight.shot.skill=skill;
            assert(straight.TailAt(140).y==floor+handHeight);
            float m[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            bowflight::SpellMatrix(straight,140,m);
            assert(std::fabs((m[13]+4*m[5])-(floor+handHeight)*shot.unitsPerMetre)<.0001f);
        }
    }
    const auto frozen=bowflight::Tip(flight.shot,3);shot.direction={1,0,0};
    assert(bowflight::Tip(flight.shot,3).y==frozen.y);
    bowflight::VisualBinding binding;
    assert(!binding.BindRoot(14,false)&&binding.BindRoot(12,true)&&!binding.BindRoot(13,false));
    puts("Hand spells: planar yaw, dual Fireball/Charged Bolt, native wandering, cancellation, cast cadence and eligibility passed");
}
