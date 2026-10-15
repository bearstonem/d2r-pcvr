#include "bow_flight.h"
#include <cstdio>
#include <cstdlib>
using namespace framevr::bowflight;
static void Check(bool ok,const char* what) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
static bool Near(float a,float b){return std::fabs(a-b)<.0002f;}
static Shot Release() {
    Shot s;s.origin={100,1.4f,100};s.direction={0,0,-1};s.player={100,0,100};
    s.unitsPerMetre=4;s.draw=1;s.session=11;s.epoch=7;s.sequence=1;s.at=1000;s.equipment=42;return s;
}
static Vec Point(const float* m,Vec p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
int main() {
    {
        const auto shot=Release();const Gate gate{true,11,7,1000,42};Once once;
        Check(Valid(shot)&&Allowed(shot,gate,1100),"fresh coherent release accepted");
        Check(once.Take(shot,gate,1100)&&!once.Take(shot,gate,1100),"one native fire per release sequence");
        auto s=shot;s.sequence=2;Check(once.Take(s,gate,1100),"new draw can shoot");
        Check(!Allowed(s,gate,1151)&&!Allowed(s,gate,999),"expired/future input cannot spend ammo");
        for(int i=0;i<5;++i) {
            auto g=gate;
            if(i==0)g.active=false;
            if(i==1)++g.session;
            if(i==2)++g.epoch;
            if(i==3)++g.equipment;
            if(i==4)g.at=800;
            Check(!Allowed(s,g,1100),"menu/tracking, session, recenter, equipment, stale gate cancel release");
        }
        s=shot;s.origin.y=100;Check(!Valid(s),"teleported release rejected");
        s=shot;s.direction={0,0,0};Check(!Valid(s),"invalid aim rejected");
        s=shot;s.origin.x=std::numeric_limits<float>::quiet_NaN();Check(!Valid(s),"nonfinite release rejected");
    }
    {
        const Vec bottom{0,.3f,0},top{0,1.6f,0};float t=0;
        Check(CapsuleEntry({-5,1,0},{5,1,0},bottom,top,.3f,t)&&Near(t,.47f),"fast arrow cannot tunnel through torso");
        Check(CapsuleEntry({0,4,0},{0,-4,0},bottom,top,.3f,t)&&Near(t,.2625f),"vertical arrow hits top cap");
        Check(CapsuleEntry({0,-4,0},{0,4,0},bottom,top,.3f,t)&&Near(t,.5f),"vertical arrow hits bottom cap");
        Check(!CapsuleEntry({-5,2,0},{5,2,0},bottom,top,.3f,t),"arrow above head misses despite same ground path");
        Check(!CapsuleEntry({-5,1,.4f},{5,1,.4f},bottom,top,.3f,t),"side clearance does not damage");
        Check(CapsuleEntry({0,1,0},{0,1,0},bottom,top,.3f,t)&&t==0,"inside start handled without divide by zero");
        Check(!CapsuleEntry({3,1,0},{3,1,0},bottom,top,.3f,t),"stationary arrow outside body misses");
        Check(CapsuleEntry({-2,1,-2},{2,1,2},bottom,top,.3f,t)&&t>.44f&&t<.46f,"oblique high-speed sweep");
        Check(CapsuleEntry({-1,1,0},{1,1,0},top,top,.7f,t),"degenerate capsule is a sphere");
        for(float speed:{20.f,40.f,100.f}) {
            const float step=speed*.04f;
            Check(CapsuleEntry({-step*.5f,1,0},{step*.5f,1,0},bottom,top,.12f,t),"small creature hit across full 25Hz step");
        }
    }
    {
        const Vec from{10.25f,1,10.25f},to{30.25f,1,10.25f};
        const auto wall=[](int x,int){return x==15;};
        const float stop=WallEntry(from,to,2,wall);
        Check(Near(stop,4.75f/20),"DDA catches one-cell wall in long segment");
        Check(WallEntry(from,to,2,[](int,int){return false;})==1,"clear segment reaches endpoint");
        Check(WallEntry(from,from,2,[](int,int){return false;})==1,"vertical trajectory has stable ground cell");
        Check(WallEntry(from,to,2,[](int x,int){return x>=12;})<.1f,"unloaded cells stop flight");
        Check(Near(WallEntry(from,{8.25f,1,8.25f},2,[](int x,int z){return x==9&&z==10;}),.125f),"corner touches both neighbouring cells");
        Check(Near(WallEntry(from,{20.25f,1,20.25f},2,[](int x,int z){return x==11&&z==10;}),.075f),"positive diagonal cannot slip through corner");
        Check(WallEntry({-1,1,1},to,2,wall)==0,"out-of-world release blocked");
        float body=0;
        Check(CapsuleEntry(from,to,{18,.2f,10.25f},{18,1.7f,10.25f},.3f,body)&&stop<body,"wall occludes body behind it");
    }
    {
        // Paired native/render cave position from the earlier melee capture.
        // A release is valid here even though the old flat comparison rejects
        // it by >0.65 m. Teleports still reject the saved release.
        auto shot=Release();shot.unitsPerMetre=3.8899f;
        shot.player=Vec{15081.331055f,3.042981f,10365.330078f}*(1/shot.unitsPerMetre);
        shot.origin=shot.player+Vec{0,1.4f,0};
        const Vec server{15077.417175f,0,10361.417175f};
        Check(Length(Vec{server.x/shot.unitsPerMetre-shot.player.x,0,server.z/shot.unitsPerMetre-shot.player.z})>.65f,
            "negative control reproduces old underground release refusal");
        Check(framevr::coordinates::MatchesPlayer(server,shot.player,shot.unitsPerMetre),"cave release passes terrain-correct player guard");
        Check(!framevr::coordinates::MatchesPlayer(server+Vec{shot.unitsPerMetre*2,0,0},shot.player,shot.unitsPerMetre),
            "release from before a warp cannot spend ammo at new position");
        // Overworld -> cave -> deeper floor -> overworld -> cave. Each shot
        // owns its floor; no current-player offset is applied to old arrows.
        for(Kind kind:{Kind::Arrow,Kind::Javelin})for(float upm:{2.f,3.8899f,8.f})for(float floor:{0.f,3.03f,12.f,-6.f,0.f,3.03f}) {
            const Vec playerGrid{80.5f,0,120.5f},enemyGrid=playerGrid+Vec{0,0,-3*upm};
            auto s=Release();s.kind=kind;s.unitsPerMetre=upm;
            s.player=framevr::coordinates::RenderGround(playerGrid*(1/upm),floor/upm);
            s.origin=s.player+Vec{0,1.2f,0};
            Check(Valid(s)&&framevr::coordinates::MatchesPlayer(playerGrid,s.player,upm),"new draw works after each floor transition");
            const auto ground=RenderGround(s,enemyGrid),from=s.origin,to=Tip(s,5);
            float contact=0,miss=0;
            Check(CapsuleEntry(from,to,ground+Vec{0,.2f,0},ground+Vec{0,1.5f,0},.2f,contact),
                "rendered arrow intersects cave creature at visible position");
            Check(!CapsuleEntry(from+Vec{1,0,0},to+Vec{1,0,0},ground+Vec{0,.2f,0},ground+Vec{0,1.5f,0},.2f,miss),
                "sideways cave miss does not become a hit");
            if(std::fabs(floor)>=3) {
                const auto oldGround=Vec{enemyGrid.x/upm,s.player.y,enemyGrid.z/upm};
                Check(!CapsuleEntry(from,to,oldGround+Vec{0,.2f,0},oldGround+Vec{0,1.5f,0},.2f,miss),
                    "negative control reproduces invisible offset enemy hitbox");
            }
            const int wallZ=int((playerGrid.z-4*upm)*.5f);
            const auto wall=[&](int x,int z){return x!=40||z<=wallZ;};
            const auto gridFrom=GridPoint(s,from),gridTo=GridPoint(s,to);
            const auto stop=WallEntry(gridFrom,gridTo,upm,wall);
            Check(stop>contact&&stop<1,"native cave wall is after visible creature");
            Check(Near(WallEntry(gridFrom,gridTo,upm,[](int,int){return false;}),1),"clear cave rooms permit full flight");
            Check(WallEntry(gridFrom,gridTo,upm,[](int,int){return true;})==0,"unloaded cave room remains solid");
            if(std::fabs(floor)>=3)
                Check(WallEntry(from,to,upm,wall)==0,"negative control reproduces wrong room query without floor correction");
            // Client visual allocation and native shot creation must request
            // the same cells, regardless of airborne height or vertical aim.
            for(const Vec direction:{Vec{0,0,-1},Vec{0,1,0},Vec{0,-1,0},Vec{.6f,0,.8f}}) {
                s.direction=direction;
                for(float distance:{0.f,1.f,5.f}) {
                    const auto tip=Tip(s,distance),grid=GridPoint(s,tip)*upm;
                    const auto expected=playerGrid+Vec{(tip.x-s.player.x)*upm,0,(tip.z-s.player.z)*upm};
                    Check(Length(grid-expected)<.0002f,"spawn and visual cells use launch floor, not arrow height");
                    Check(Length(GridPoint(s,tip+Vec{0,10,0})-GridPoint(s,tip))<.0002f,"vertical flight cannot drift into neighbouring native cells");
                }
            }
        }
    }
    {
        Check(Near(Speed(4608,1),20)&&Near(Speed(9216,1),40),"configured speed maps into physical metres per second");
        Check(Near(Speed(2304,1),10)&&Speed(0,1)==0,"native slow/stop velocity preserved");
        Check(Speed(9216,.3f)<Speed(9216,1)&&Speed(100000,1)==100,"draw scales speed with bounded maximum");
        Flight f;f.shot=Release();f.speed=40;f.updatedAt=1000;f.alive=true;
        Check(f.Remember(0)&&!f.Remember(0)&&f.Seen(0),"piercing cannot hit the same native id twice, including zero");
        for(uint32_t i=1;i<64;++i)Check(f.Remember(i),"bounded pierce ledger");
        Check(!f.Remember(64),"full ledger fails closed");
        Check(Near(f.TailAt(1020).z,99.2f)&&Near(f.TailAt(9000).z,98.4f),"render prediction capped to one game tick during pause/stall");
        f.alive=false;Check(Near(f.TailAt(9000).z,100),"ended arrow cannot extrapolate through hit");
        f.alive=true;f.distance=2;
        for(const Vec d:{Vec{0,0,-1},Vec{1,0,0},Vec{0,1,0},Vec{0,-1,0},Vec{.6f,0,.8f}}) {
            f.shot.direction=d;float m[16]{};ArrowMatrix(f,1000,m);
            const auto tail=Point(m,{0,0,-.98391676f}),tip=Point(m,{0,0,1});
            Check(Length(tail-f.TailAt(1000)*4)<.0002f,"mesh tail follows frozen physical release");
            Check(Length((tip-tail)-d*(ArrowLength*4))<.0002f,"mesh axis agrees with flight direction at every pitch/yaw");
            for(const float v:m)Check(std::isfinite(v),"vertical aim produces finite visual transform");
        }
    }
    {
        for(float upm:{2.f,3.8899f,8.f})for(const Vec d:{Vec{0,0,-1},Vec{1,0,0},Vec{0,1,0},Vec{0,-1,0},Vec{.6f,0,.8f}}) {
            Flight f;f.shot=Release();f.shot.kind=Kind::Javelin;f.shot.unitsPerMetre=upm;f.shot.direction=d;
            f.shot.origin=f.shot.origin-d*JavelinGrip;
            float m[16]{};ArrowMatrix(f,1000,m);
            Check(Valid(f.shot)&&BasicMissile(1)&&!BasicMissile(2),"javelin kind is valid without claiming skill missiles");
            Check(Length(Point(m,{0,0,-1})-f.shot.origin*upm)<.0002f,"native javelin tail follows captured hand offset");
            Check(Length(Point(m,{0,0,1})-Tip(f.shot,0)*upm)<.0002f,"native javelin tip and physical contact agree at every pitch/yaw");
            auto bad=f.shot;bad.kind=Kind(2);Check(!Valid(bad),"unknown projectile cannot reach native firing path");
        }
    }
    {
        // The nocked mesh must match the tracked guide even when the native
        // bow's grip/animation rig differs. Releasing starts at that same pose.
        for(float upm:{2.f,3.8899f,8.f})for(const Vec direction:{Vec{0,0,-1},Vec{1,0,0},Vec{0,1,0},Vec{0,-1,0},Vec{.6f,0,.8f}}) {
            auto pose=Release();pose.unitsPerMetre=upm;pose.direction=direction;pose.origin={12,2.1f,30};
            float held[16]{},released[16]{},hidden[16]{};
            PreviewMatrix(pose,true,held);PreviewMatrix(pose,false,hidden);
            Flight flight;flight.shot=pose;flight.alive=true;flight.updatedAt=1000;flight.speed=40;
            ArrowMatrix(flight,1000,released);
            const auto tail=Point(held,{0,0,-.98391676f}),tip=Point(held,{0,0,1});
            Check(Length(tail-pose.origin*upm)<.0002f,"native preview tail is at tracked string hand");
            Check(Length(tip-Tip(pose,0)*upm)<.0002f,"native preview tip matches guide and contact tip");
            for(int i=0;i<16;++i)Check(Near(held[i],released[i]),"release cannot jump from nocked pose or use bow animation");
            Check(Length(Point(hidden,{0,0,1})-Point(hidden,{0,0,-.98391676f}))<.01f,"idle preview does not occlude the bow");
            const auto saved=pose.origin;flight.distance=2;
            pose.origin={-20,10,-40};pose.direction={0,0,1};
            ArrowMatrix(flight,1000,released);
            Check(Length(Point(released,{0,0,-.98391676f})-(saved+direction*2)*upm)<.0002f,"post-release hand motion cannot move native arrow");
        }
    }
    {
        // arrow.json: a unit root plus a child mesh translated +5 native Y
        // and scaled 1.25. The initializer visits both; either order must bind
        // the mesh so this authored transform is replaced, never inherited.
        constexpr uint32_t root=0x10002,mesh=0x10003,helper=0x10004;
        Flight f;f.shot=Release();float flight[16]{};ArrowMatrix(f,1000,flight);
        for(bool rootFirst:{false,true}) {
            VisualBinding binding;
            const auto visit=[&](uint32_t entity){return binding.Bind(entity,entity==mesh);};
            if(rootFirst)Check(!visit(root),"parent without release component cannot claim arrow");
            Check(visit(mesh),"visible release mesh claims arrow");
            Check(!visit(root)&&!visit(helper),"later root/helper cannot overwrite mesh binding");
            Check(visit(mesh),"repeat initialization of same mesh is stable");
            Check(!binding.Bind(helper,true),"second component cannot steal established mesh");
            // Apply the same runtime override to a small two-node hierarchy.
            // A root override would place the child above the release; a mesh
            // override must put the actual rendered tail at the shot origin.
            const auto renderTail=[&](const VisualBinding& owner) {
                const Vec localTail{0,5,-.98391676f*1.25f};
                const Vec inherited=owner.Matches(root)?Point(flight,localTail):localTail;
                return owner.Matches(mesh)?Point(flight,{0,0,-.98391676f}):inherited;
            };
            Check(Length(renderTail(binding)-f.shot.origin*4)<.0002f,"child height and scale do not displace released arrow");
            VisualBinding wrong;Check(wrong.Bind(root,true),"negative control can reproduce old parent binding");
            Check(renderTail(wrong).y-f.shot.origin.y*4>5,"old parent binding reproduces above-player origin");
            constexpr uint32_t recycled=mesh+(1u<<20);
            Check(!binding.Matches(recycled)&&!binding.Bind(recycled,true),"reused entity index cannot inherit flight transform");
            binding.Clear();
            Check(!binding.Bound()&&!binding.Matches(mesh)&&binding.Bind(recycled,true),"new arrow may bind new generation after cleanup");
        }
    }
    {
        // Reproduce #93: the model initially renders, then its mesh stops
        // updating while the client pointer remains alive. That pointer used
        // to prevent any retry, even though the fallback was already visible.
        PreviewRecovery recovery;VisualBinding binding;
        constexpr uint32_t mesh=0x10003,recycled=mesh+(1u<<20);
        recovery.Observe(true,0,1000);
        Check(recovery.Due(false,1000),"visible bow with no client requests a preview");
        recovery.Attempt(1000);
        Check(binding.Bind(mesh,true),"first preview binds its native mesh");
        for(uint64_t now=1050;now<=10000;now+=50) {
            recovery.Observe(true,now,now);
            Check(!recovery.Due(true,now),"healthy retained preview never gets recreated");
        }
        Check(PreviewRecovery::Rendered(10000,10149)&&!PreviewRecovery::Rendered(10000,10150),
            "fallback appears when native render readiness expires");
        recovery.Observe(true,10000,10150);
        Check(!recovery.Due(true,13149),"brief render gaps have a recovery grace period");
        Check(recovery.Due(true,13150),"stale mesh recovers even though native client still exists");
        // A render arriving after scheduling but before the UI callback wins.
        recovery.Observe(true,13151,13151);
        Check(!recovery.Due(true,13151),"UI recheck preserves a preview that resumed rendering");
        recovery.Observe(true,13151,13301);
        Check(recovery.Due(true,16301),"later sustained render loss can recover again");
        recovery.Attempt(16301);binding.Clear();
        recovery.Observe(true,0,16302);
        Check(!recovery.Due(false,16800)&&recovery.Due(false,16801),"retirement and replacement are rate limited");
        recovery.Attempt(16801);
        Check(binding.Bind(recycled,true)&&!binding.Matches(mesh),"replacement owns only its new mesh generation");
        Check(!recovery.Due(true,19800),"replacement gets time for asynchronous mesh loading");
        recovery.Observe(true,19800,19800);
        Check(PreviewRecovery::Rendered(19800,19801)&&!recovery.Due(true,19801),"rendered replacement removes fallback and ends recovery");
    }
    {
        PreviewRecovery recovery;
        recovery.Observe(true,0,1000);recovery.Attempt(1000);
        Check(!recovery.Due(false,1499)&&recovery.Due(false,1500),"failed allocations cannot retry every frame or fired arrow");
        recovery.Attempt(1500);
        Check(!recovery.Due(true,4499)&&recovery.Due(true,4500),"never-rendered native client can also recover after loading grace");
        recovery.Observe(false,0,4500);
        Check(!recovery.Due(true,60000)&&!recovery.Due(false,60000),"menus, holstering and tracking loss suspend recovery and creation");
        recovery.Observe(true,0,60000);
        Check(!recovery.Due(true,62999)&&recovery.Due(true,63000),"returning to bow starts a fresh visible grace period");
        recovery.Observe(false,0,63000);recovery.Observe(true,0,64000);
        recovery.Observe(true,64001,64001);
        Check(!recovery.Due(true,70000),"resuming rendering cancels a queued recovery");
        recovery={};recovery.Observe(true,0,100);
        Check(recovery.Due(false,100)&&!recovery.Due(true,100),"new session forgets prior attempts and missing-render history");
        Check(!PreviewRecovery::Rendered(0,100)&&!PreviewRecovery::Rendered(101,100),"missing and future render timestamps cannot hide fallback");
    }
    {
        // A delayed gameplay timer must not pin the nocked arrow to an older
        // player position. Camera movement and hand draw arrive every render
        // pair, including a camera refresh within a pair and both eye passes.
        const auto timerPose=Release();PreviewRenderPose render;
        Flight fired;fired.shot=timerPose;fired.alive=true;fired.updatedAt=1000;
        float firedBefore[16]{};ArrowMatrix(fired,1000,firedBefore);
        for(int step=1;step<=40;++step) {
            const uint64_t now=1000+step*25;
            auto live=timerPose;live.at=now;
            const Vec walk{step*.07f,0,-step*.11f};live.player=live.player+walk;live.origin=live.origin+walk;
            live.direction={.6f,0,-.8f};
            const Gate gate{true,live.session,live.epoch,now,live.equipment};
            render.Publish(live,gate,true,now);
            for(int eye=0;eye<2;++eye) {
                Shot pose;bool visible=false;float matrix[16]{};
                Check(render.Read(live.session,now,now,pose,visible)&&visible,"both eyes consume this pair's held arrow pose");
                PreviewMatrix(pose,visible,matrix);
                const auto tail=Point(matrix,{0,0,-.98391676f})*(1/live.unitsPerMetre);
                Check(Length(tail-live.origin)<.0002f,"nocked arrow follows walking and strafing without waiting for timer");
                Check(Length(tail-timerPose.origin)>.1f,"negative control distinguishes stale timer pose from moving bow");
            }
            const auto revision=render.revision;
            live.player.x+=.2f;live.origin.x+=.2f;
            render.Publish(live,gate,true,now);
            Shot latest;bool visible=false;
            Check(render.Read(live.session,now,now,latest,visible)&&render.revision!=revision&&
                Length(latest.origin-live.origin)<.0001f,"later camera refresh in same pair replaces earlier world anchor");
        }
        float firedAfter[16]{};ArrowMatrix(fired,1000,firedAfter);
        for(int i=0;i<16;++i)Check(firedBefore[i]==firedAfter[i],"render preview updates cannot drag a released arrow with the player");
        auto live=timerPose;live.at=2200;Gate gate{true,live.session,live.epoch,2200,live.equipment};
        Shot pose;bool visible=false;
        render.Publish(live,gate,true,2200);
        Check(!render.Read(live.session,2201,2201,pose,visible),"a missing current camera sample cannot reuse last pair's pose");
        Check(!render.Read(live.session+1,2200,2200,pose,visible),"preview cannot cross character sessions");
        Check(!render.Read(live.session,2200,2351,pose,visible),"stale tracking cannot keep a nocked arrow visible");
        gate.active=false;render.Publish(live,gate,true,2200);
        Check(!render.Read(live.session,2200,2200,pose,visible),"menus and inactive controls hide preview");
        gate.active=true;gate.bowActive=false;render.Publish(live,gate,true,2200);
        Check(!render.Read(live.session,2200,2200,pose,visible),"switching away from bow hides preview");
        gate.bowActive=true;++gate.epoch;render.Publish(live,gate,true,2200);
        Check(!render.Read(live.session,2200,2200,pose,visible),"recenter context mismatch rejects old hand pose");
        --gate.epoch;render.Publish(live,gate,false,2200);
        Check(render.Read(live.session,2200,2200,pose,visible)&&!visible,"out-of-reach preview remains hidden with valid tracking");
    }
    std::puts("Physical bow: release ownership, cancellation, cave coordinates, swept contacts, walls, speed, visual-axis, mesh-binding, preview recovery and render-pose tests passed");
}
