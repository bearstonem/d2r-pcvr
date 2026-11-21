#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h> // Exercise Windows macros used by the real vrcam translation unit.
#endif
#include "entry_facing.h"
#include "frame_math.h"
#include <cstdlib>
#include <initializer_list>
#include <cstdio>
#include <unordered_map>
using namespace framevr::entryfacing;
void Check(bool ok){if(!ok){std::puts("entry facing check failed");std::exit(1);}}
int main(){
    State s;float h=0;
    // Verify against the actual native quaternion composition, not the legacy
    // TargetYaw expression. Includes physical head turns, stick/mouse turns,
    // recenter offsets and pitched/rolled heads on either side of the portal.
    constexpr float rad=.017453292519943295f;
    for(float camera:{-135.f,45.f})for(float body:{-170.f,0.f,120.f})
    for(float head:{-179.f,-90.f,0.f,45.f,179.f})for(float recenter:{-60.f,0.f,90.f})
    for(float target:{-90.f,0.f,70.f,180.f}){
        using namespace framevr::math;
        const auto pitch=Quat{std::sin(.2f),0,0,std::cos(.2f)};
        const auto roll=Quat{0,0,std::sin(.1f),std::cos(.1f)};
        const auto pose=Multiply(Yaw(head*rad),Multiply(pitch,roll));
        const float measured=Heading(pose)/rad;
        const float delta=NativeTurnDelta(camera,body,measured,recenter,target);
        const auto actual=Multiply(Yaw((camera-body-delta-recenter)*rad),pose);
        Check(std::abs(std::remainder(Heading(actual)/rad-target,360.f))<.001f);
    }
    // Every act uses its own NPC, with unrelated NPCs ignored.
    for(auto pair:{std::pair{1,146},std::pair{40,178},std::pair{75,245},std::pair{103,257}}){
        TownTargets targets;
        targets.Add(pair.first,999,100,90);
        Check(!targets.Heading(pair.first,100,100,h));
        targets.Add(pair.first,pair.second,110,100);
        Check(targets.Heading(pair.first,100,100,h)&&std::abs(h+90)<.01f);
    }
    TownTargets traders;
    traders.Add(103,405,110,110);traders.Add(103,257,110,90);
    Check(traders.Heading(103,100,100,h)&&std::abs(h+90)<.01f);
    TownTargets malah;malah.Add(109,513,110,110);
    Check(malah.Heading(109,100,100,h));
    const float radians=h*.0174532925f;
    const float fx=-std::sin(radians),fy=-std::cos(radians);
    Check(std::abs(fx*10+fy*10)<.001f); // Malah perpendicular to forward.
    Check(-fy*10+fx*10>14.f);          // On the right, never on the left.
    Check(!malah.Heading(109,110,110,h));
    for(int town:{1,40,75,103,109}){
        Check(TownPortalArrival(8,town,3));
        Check(!TownPortalArrival(0,town,3));
        Check(!TownPortalArrival(40,town,3));
        Check(!TownPortalArrival(8,town,10));
        Check(!TownPortalArrival(8,town,NAN));
    }
    Check(!TownPortalArrival(8,9,1));
    // Loaded room graph: find an NPC two rooms away, deduplicate cycles,
    // skip other levels and stop a malformed cyclic unit chain.
    std::unordered_map<uintptr_t,unsigned char> memory;
    auto put=[&](uintptr_t at,const auto& value){
        auto p=reinterpret_cast<const unsigned char*>(&value);
        for(size_t i=0;i<sizeof value;++i)memory[at+i]=p[i];
    };
    auto read=[&](uintptr_t at,auto& value){
        auto p=reinterpret_cast<unsigned char*>(&value);
        for(size_t i=0;i<sizeof value;++i){auto it=memory.find(at+i);if(it==memory.end())return false;p[i]=it->second;}
        return true;
    };
    for(uintptr_t r:{0x10000u,0x20000u,0x30000u,0x40000u}){
        put(r,r+0x1000);put(r+0x40,uint32_t(2));put(r+0xa8,uintptr_t(0));
        put(r+0x1000,uintptr_t(0x10000));put(r+0x1008,r+0x10000);
    }
    put(0x30000+0xa8,uintptr_t(0x90000));
    uint32_t npc[4]{1,178,1,0};put(0x90000,npc);put(0x90000+0x160,uintptr_t(0x90000));
    put(0x40000+0xa8,uintptr_t(0x90000)); // Must not visit the other level.
    int found=0;
    WalkTown(0x10000,40,read,[](uintptr_t r){return r==0x40000?41:40;},
        [&](uintptr_t,const uint32_t* header){Check(header[1]==178);++found;});
    Check(found==1);
    Check(!s.Observe(1,2,0));Check(!s.Observe(1,0,10));
    Check(s.Observe(1,8,100));Check(!s.Observe(1,8,100));Check(!s.Observe(1,8,180));
    Check(s.Observe(1,2,300));Check(!s.Observe(1,2,1800));
    Check(!s.Observe(2,8,2000));Check(!s.Observe(0,0,2100));
    Check(Direction(100,100,98,100,[](int,int){return true;},h));Check(std::abs(h+90)<.01f);
    // A farther obstacle makes a diagonal ray longer, but the doorway's
    // forward direction is already clear. Neither side should pull the view.
    for(int side:{-1,1}) {
        Check(Direction(100,100,98,100,[&](int x,int y){return x<104||(y-100)*side>0;},h));
        Check(std::abs(h+90)<.01f);
    }
    // Cave spawn exactly on the warp: west is a wall, east leads inside.
    Check(Direction(100,100,100,100,[](int x,int y){return x>=100&&y>=99&&y<=101;},h));Check(std::abs(h+90)<.01f);
    // Leaving a cave: the entrance to the east must be behind the view.
    Check(Direction(100,100,102,100,[](int x,int){return x<=100;},h));Check(std::abs(h-90)<.01f);
    Check(!Direction(100,100,100,100,[](int,int){return false;},h));
    // Jail landings vary sideways and along the stairs. The native doorway
    // direction must stay +Y even when the spawn-to-tile vector is diagonal.
    for(uint32_t warp:{13u,14u})for(float x:{99.5f,100.f,100.5f})for(float y:{100.8f,102.f,104.f}) {
        float direction=0;
        Check(NativeExitHeading(warp,direction)||Direction(x,y,100,100,[](int,int){return true;},direction));
        const float radians=direction*.0174532925f;
        Check(std::abs(std::sin(radians))<.001f&&-std::cos(radians)>.999f);
    }
    Check(!NativeExitHeading(57,h)); // Other entrances retain their existing resolver.
    for(float base:{-170.f,0.f,135.f})for(float yaw:{-180.f,30.f,179.f}){
        const float turn=TurnDelta(base,yaw,h);
        Check(std::abs(std::remainder(base-yaw-turn-h,360.f))<.001f);
    }
    std::puts("Town portal targets (all five acts), bounded room traversal, first-frame entry/exit, cave/Jail and yaw checks passed");
}
