#include "billboards.h"
#include "billboard_signatures.h"
#include "portal_billboard.h"
#include "sigscan.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#if defined(D2RVR_DEVELOPER) && defined(D2RVR_NATIVE_OPENXR)
#include "shrine_trace.h"
#include <cstdio>
#endif
#ifdef D2RVR_NATIVE_OPENXR
#include "memory.h"
#include "shrine_placement.h"
#include "overlay_view.h"
#include "bone_armor_height.h"
#include <cstdio>
#endif

namespace billboards {
namespace {
// PopcornFX Screen/View Quad::Align share (this, view context, strided batch).
// The view-position implementation uses context+0x30 and the engine's world
// up axis. It retains native size, rotation, UVs, normals and tangent generation.
using Align = void (*)(void*, const void*, void*);
Align original=nullptr,viewAligned=nullptr;
void (*logger)(const char*)=nullptr;
bool (*wanted)()=nullptr;
bool installed=false;
using ScriptView=void (*)(void*,const void*,const void*);
ScriptView originalDirection=nullptr,originalPosition=nullptr;
using LocalPosition=void (*)(void*,const void*,uint32_t,const void*);
LocalPosition originalLocalPosition=nullptr;
std::atomic<bool> portalReady{false},portalReported{false};
std::atomic<bool> centerEnabled{true},centerReported{false};
uintptr_t gameBase=0;
SRWLOCK centerLock=SRWLOCK_INIT;
float center[3]{};ULONGLONG centerAt=0;
std::atomic<bool> reported{false};
bool Read(void* to,const void* from,size_t bytes) {
#ifdef D2RVR_NATIVE_OPENXR
    return framevr::ReadMemory(to,from,bytes);
#else
    __try { std::memcpy(to,from,bytes);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
#endif
}
bool ReadAt(void* to,uint64_t address,size_t bytes) {
    return address>=0x10000&&address<0x0000800000000000ull&&
        bytes<0x10000&&Read(to,reinterpret_cast<const void*>(address),bytes);
}
bool Head(portal::Vec& eye) {
    ULONGLONG at;
    AcquireSRWLockShared(&centerLock);std::memcpy(&eye,center,sizeof eye);at=centerAt;ReleaseSRWLockShared(&centerLock);
    return at&&GetTickCount64()-at<=250&&portal::Finite(eye);
}
bool PortalDefinition(uint64_t definition,bool offsets=false) {
    // Script callbacks run on worker threads. Keep a small, bounded cache of
    // classifications; no allocations or asset reads in the rendering path.
    struct Entry {uint64_t definition=0;ULONGLONG at=0;bool portal=false,offsets=false;};
    thread_local Entry cache[32];
    auto& e=cache[(definition>>6)%32];const auto now=GetTickCount64();
    if(e.definition!=definition||now-e.at>1000) {
        const bool portal=portal::IsPortal(ReadAt,definition,gameBase);
        e={definition,now,portal,portal&&portal::IsPortal(ReadAt,definition,gameBase,true)};
    }
    return offsets?e.offsets:e.portal;
}
#ifdef D2RVR_NATIVE_OPENXR
std::atomic<bool> shrineReported{false};
bool ShrineDefinition(uint64_t definition) {
    struct Entry {uint64_t definition=0;ULONGLONG at=0;bool shrine=false;};
    thread_local Entry cache[32];
    auto& e=cache[(definition>>6)%32];const auto now=GetTickCount64();
    if(e.definition!=definition||now-e.at>1000) {
        char path[160]{};
        e={definition,now,portal::DefinitionPath(ReadAt,definition,gameBase,path)&&
            shrine_placement::Asset(path)};
    }
    return e.shrine;
}
// Bone Armor's orbit centre (bone_armor_height.h): [render] bone_armor_height.
std::atomic<float> boneArmorHeight{bone_armor::Default};
std::atomic<bool> boneArmorReported{false};
bool BoneArmorDefinition(uint64_t definition) {
    struct Entry {uint64_t definition=0;ULONGLONG at=0;bool bone=false;};
    thread_local Entry cache[32];
    auto& e=cache[(definition>>6)%32];const auto now=GetTickCount64();
    if(e.definition!=definition||now-e.at>1000) {
        char path[160]{};
        e={definition,now,portal::DefinitionPath(ReadAt,definition,gameBase,path)&&bone_armor::Asset(path)};
    }
    return e.bone;
}
bool BoneArmorWanted() {return std::fabs(boneArmorHeight.load(std::memory_order_relaxed)-bone_armor::Native)>=1e-4f;}
// Per lane, as CorrectCenters: mixed batches never lend one effect's
// classification to another's lane.
bool LowerBoneArmor(portal::Vec* points,uint32_t count,const portal::Context& c) {
    const float height=boneArmorHeight.load(std::memory_order_relaxed);
    struct Entry {int32_t id=-1;bool bone=false;};Entry cache[8];bool changed=false;
    for(uint32_t i=0;i<count;++i) {
        portal::Vec lowered;if(!bone_armor::Lowered(points[i],height,lowered))continue;
        int32_t id=c.instance;
        if(id<0&&(i>=c.count||!ReadAt(&id,c.indices+4ull*i,4)))continue;
        if(id<0)continue;
        auto& entry=cache[uint32_t(id)%8];
        if(entry.id!=id) {
            portal::Instance instance{};entry.id=id;
            entry.bone=portal::Resolve(ReadAt,c,i,gameBase,instance)&&BoneArmorDefinition(instance.definition);
        }
        if(entry.bone){points[i]=lowered;changed=true;}
    }
    return changed;
}
// Paladin aura overlays (overlay_view.h). The game camera's direction is
// written on the render thread each frame and read by PopcornFX workers.
SRWLOCK forwardLock=SRWLOCK_INIT;
portal::Vec gameForward{};ULONGLONG forwardAt=0;
std::atomic<bool> overlayReported{false};std::atomic<ULONGLONG> overlayLogAt{0};
std::atomic<int> overlayLogBudget{12};
struct OverlayEntry {uint64_t definition=0;ULONGLONG at=0;bool overlay=false;char path[160]{};};
const OverlayEntry& OverlayDefinition(uint64_t definition) {
    thread_local OverlayEntry cache[32];
    auto& e=cache[(definition>>6)%32];const auto now=GetTickCount64();
    if(e.definition!=definition||now-e.at>1000) {
        e.definition=definition;e.at=now;e.path[0]=0;
        e.overlay=portal::DefinitionPath(ReadAt,definition,gameBase,e.path)&&overlay_view::Asset(e.path);
    }
    return e;
}
// Aura overlays take part of the game camera's vertical as view.direction,
// for every view: an effect that spawns with the flat camera's view is
// pushed sideways in VR too.
void CorrectOverlayView(void* output,const void* context) {
    framevr::hookmetrics::Scope measured(framevr::hookmetrics::PortalView);
    if(!portalReady.load()||!wanted||!wanted())return;
    portal::Vec forward,value;ULONGLONG at;
    AcquireSRWLockShared(&forwardLock);forward=gameForward;at=forwardAt;ReleaseSRWLockShared(&forwardLock);
    if(!at||GetTickCount64()-at>250||!overlay_view::Upright(forward,value))return;
    struct Stream {uint64_t data;uint32_t count,stride;};
    Stream out{};portal::Context c{};
    if(!Read(&out,output,sizeof out)||!Read(&c,context,sizeof c)||
       !out.data||!out.count||out.count>1024||out.stride!=12)return;
    int32_t lastId=-1;const OverlayEntry* entry=nullptr;
    for(uint32_t i=0;i<out.count;++i) {
        int32_t id=c.instance;
        if(id<0&&(i>=c.count||!ReadAt(&id,c.indices+4ull*i,4)))continue;
        if(id<0)continue;
        if(id!=lastId) {
            lastId=id;entry=nullptr;portal::Instance instance{};
            if(portal::Resolve(ReadAt,c,i,gameBase,instance))entry=&OverlayDefinition(instance.definition);
        }
        if(!entry||!entry->overlay)continue;
        // Frame aura: the direction the effect asked for and what it got, at
        // most every 5 s and 12 times per session.
        portal::Vec native{};bool log=false;
        if(logger&&overlayLogBudget.load(std::memory_order_relaxed)>0) {
            const auto now=GetTickCount64();auto last=overlayLogAt.load();
            log=now-last>=5000&&overlayLogAt.compare_exchange_strong(last,now)&&
                ReadAt(&native,out.data+12ull*i,sizeof native)&&overlayLogBudget.fetch_sub(1)>0;
        }
        if(!framevr::WriteMemory(reinterpret_cast<void*>(out.data+12ull*i),&value,sizeof value))continue;
        if(log) {
            char line[360];
            std::snprintf(line,sizeof line,"Frame aura: overlay view direction native=(%.3f,%.3f,%.3f) -> (0,%.3f,0), no sideways push; asset=%s",
                native.x,native.y,native.z,value.y,entry->path);
            logger(line);
        }
        if(!overlayReported.exchange(true)&&logger)
            logger("VR overlays: Paladin aura overlays keep a small lift without the sideways push toward the camera");
    }
}
#endif
#if defined(D2RVR_DEVELOPER) && defined(D2RVR_NATIVE_OPENXR)
SRWLOCK shrineLock=SRWLOCK_INIT;
shrine::Budget shrineBudget;
std::atomic<bool> shrineExhausted{false};
struct ShrineBatch {shrine::Sample samples[shrine::MaxLanes];uint32_t count=0;};
void PrepareShrineTrace(ShrineBatch& batch,void* output,const void* input,
        const void* context,shrine::Kind kind) {
    portal::Vec head;
    if(shrineExhausted.load()||!logger||!portalReady.load()||!wanted||!wanted()||!Head(head))return;
    shrine::Stream out{},in{};portal::Context c{};
    if(!Read(&out,output,sizeof out)||!Read(&in,input,sizeof in)||
       !Read(&c,context,sizeof c)||!shrine::Streams(kind,out,in))return;
    // Diagnostic sampling only: cap per-callback work even in large batches.
    // Missing samples are not evidence that an effect never used these helpers.
    for(uint32_t i=0;i<out.count&&i<shrine::MaxLanes;++i) {
        shrine::Sample s;
        if(!shrine::Capture(ReadAt,c,gameBase,kind,out,in,i,head,s))continue;
        if(!TryAcquireSRWLockExclusive(&shrineLock))continue;
        const bool keep=shrineBudget.Claim(s,GetTickCount64());
        shrineExhausted.store(shrineBudget.used>=shrine::MaxSamples);
        ReleaseSRWLockExclusive(&shrineLock);
        if(keep)batch.samples[batch.count++]=s;
    }
}
void FinishShrineTrace(const ShrineBatch& batch) {
    for(uint32_t i=0;i<batch.count;++i) {
        const auto& s=batch.samples[i];portal::Vec value{};
        const bool valid=ReadAt(&value,s.output,sizeof value)&&portal::Finite(value);
        const char* kind=s.kind==shrine::Kind::Direction?"direction":
            s.kind==shrine::Kind::Position?"position":"local19";
        char line[580];
        std::snprintf(line,sizeof line,"Frame shrine trace: sample=%u kind=%s layer=%s instance=%llx definition=%llx stream=%llx lane=%u count=%u stride=%u asset=%s",
            s.serial,kind,s.layer==shrine::Layer::Symbol?"symbol":"environment",static_cast<unsigned long long>(s.instance.address),
            static_cast<unsigned long long>(s.instance.definition),static_cast<unsigned long long>(s.stream),s.lane,s.count,s.stride,s.path);
        logger(line);
        std::snprintf(line,sizeof line,"Frame shrine values: sample=%u view=%d camera_match=%d head=(%.6g,%.6g,%.6g) native_eye=(%.6g,%.6g,%.6g) local=(%.6g,%.6g,%.6g) output_ok=%d output=(%.6g,%.6g,%.6g)",
            s.serial,s.view,int(s.cameraMatches),s.head.x,s.head.y,s.head.z,s.nativeEye.x,s.nativeEye.y,s.nativeEye.z,
            s.local.x,s.local.y,s.local.z,int(valid),value.x,value.y,value.z);
        logger(line);
        const auto* m=s.matrix;
        std::snprintf(line,sizeof line,"Frame shrine transform: sample=%u origin=(%.6g,%.6g,%.6g) basis=(%.6g,%.6g,%.6g;%.6g,%.6g,%.6g;%.6g,%.6g,%.6g)",
            s.serial,m[12],m[13],m[14],m[0],m[1],m[2],m[4],m[5],m[6],m[8],m[9],m[10]);
        logger(line);
        if(s.serial==shrine::MaxSamples)logger("Frame shrine trace: sample budget exhausted; restart developer game to capture again");
    }
}
#endif
void CenterPosition(void* output,const void* input,uint32_t mask,const void* worker) {
#ifdef D2RVR_NATIVE_OPENXR
    framevr::hookmetrics::Scope measured(framevr::hookmetrics::PortalCenter);
#endif
    portal::Vec head;
    // 19 is the observed native local-point -> world-point path. Keep all
    // other conversions and non-center particle positions exactly native.
#ifdef D2RVR_NATIVE_OPENXR
    const bool bones=BoneArmorWanted();
#else
    const bool bones=false;
#endif
    const bool centers=centerEnabled.load();
    if(mask!=19||!portalReady.load()||(!centers&&!bones)||!wanted||!wanted()||!Head(head)) {
        originalLocalPosition(output,input,mask,worker);return;
    }
    struct Stream {uint64_t data;uint32_t count,stride;};
    Stream out{},in{};portal::Context c{};portal::Vec points[64];
    if(!Read(&out,output,sizeof out)||!Read(&in,input,sizeof in)||
       !ReadAt(&c,reinterpret_cast<uint64_t>(worker)+0x58,sizeof c)||
       c.worker!=reinterpret_cast<uint64_t>(worker)||!out.data||!in.data||
       !out.count||out.count>64||out.stride!=12||
       (in.stride!=0&&in.stride!=12)||!in.count||
       (in.stride&&in.count<out.count)) {
        originalLocalPosition(output,input,mask,worker);return;
    }
    // Read a constant stream once, or one contiguous batch. Mixed instances
    // still require per-instance classification; never assume the whole batch
    // belongs to a portal just because one lane does.
    if(!ReadAt(points,in.data,in.stride?out.count*sizeof points[0]:sizeof points[0])) {
        originalLocalPosition(output,input,mask,worker);return;
    }
    if(!in.stride)for(uint32_t i=1;i<out.count;++i)points[i]=points[0];
    const bool portals=centers&&portal::CorrectCenters(points,out.count,c,gameBase,ReadAt,
        [](uint64_t definition){return PortalDefinition(definition,true);});
    bool lowered=false;
#ifdef D2RVR_NATIVE_OPENXR
    lowered=bones&&LowerBoneArmor(points,out.count,c);
#endif
    if(!portals&&!lowered){originalLocalPosition(output,input,mask,worker);return;}
    // A private input stream keeps the native transform/interpolation path and
    // never edits shared VM inputs. Resolve separately for mixed-instance lanes.
    const Stream local{reinterpret_cast<uint64_t>(points),out.count,12};
    originalLocalPosition(output,&local,mask,worker);
#ifdef D2RVR_NATIVE_OPENXR
    if(lowered&&!boneArmorReported.exchange(true)&&logger) {
        char line[160];
        std::snprintf(line,sizeof line,"VR overlays: Bone Armor orbit centre lowered to %.2f units (native %.2f); native transform retained",
            boneArmorHeight.load(),bone_armor::Native);
        logger(line);
    }
#endif
    if(portals&&!centerReported.exchange(true)&&logger)
        logger("VR portals: targeted center-offset correction active; native local-to-world transform retained");
}
void CorrectScriptView(void* output,const void* input,const void* context,bool direction) {
#ifdef D2RVR_NATIVE_OPENXR
    framevr::hookmetrics::Scope measured(framevr::hookmetrics::PortalView);
#endif
    if(!portalReady.load()||!wanted||!wanted())return;
    portal::Vec head;
    if(!Head(head))return;
    struct Stream {uint64_t data;uint32_t count,stride;};
    Stream out{},in{};portal::Context c{};
    if(!Read(&out,output,sizeof out)||!Read(&in,input,sizeof in)||!Read(&c,context,sizeof c)||
       !out.data||!in.data||!out.count||out.count>1024||out.stride!=12||
       in.count!=out.count||(in.stride!=0&&in.stride!=4))return;
    struct Views {uint64_t data;uint32_t count,capacity;} views{};
    if(!ReadAt(&views,c.scene+0x310,sizeof views)||!views.data||!views.count||views.count>8)return;
    // Native callbacks use tightly packed float3 output, including constant
    // inputs (stride zero). Resolve each effect independently in mixed batches.
    int32_t constantView=-1;
    if(!in.stride&&!ReadAt(&constantView,in.data,4))return;
    int32_t lastView=-1;bool cameraMatches=false;
    int32_t lastInstance=-1;portal::Instance instance{};portal::Vec value{};bool valid=false;
#ifdef D2RVR_NATIVE_OPENXR
    bool shrineCorrection=false;
#endif
    for(uint32_t i=0;i<out.count;++i) {
        int32_t view=constantView,id=c.instance;
        if((in.stride&&!ReadAt(&view,in.data+uint64_t(i)*in.stride,4))||view<0||uint32_t(view)>=views.count)continue;
        if(view!=lastView) {
            portal::Vec nativeEye{};lastView=view;cameraMatches=false;
            if(ReadAt(&nativeEye,views.data+uint64_t(view)*0x1f8+0x70,sizeof nativeEye)) {
                const float x=nativeEye.x-head.x,y=nativeEye.y-head.y,z=nativeEye.z-head.z;
                const float distance=x*x+y*y+z*z;
                cameraMatches=std::isfinite(distance)&&distance<=4.f;
            }
        }
        if(!cameraMatches)continue;
        if(id<0&&(i>=c.count||!ReadAt(&id,c.indices+4ull*i,4)))continue;
        if(id<0)continue;
        if(id!=lastInstance) {
            lastInstance=id;valid=false;
#ifdef D2RVR_NATIVE_OPENXR
            shrineCorrection=false;
#endif
            portal::Vec origin{};
            const bool resolved=portal::Resolve(ReadAt,c,i,gameBase,instance);
            if(resolved&&PortalDefinition(instance.definition)&&
               portal::Origin(ReadAt,instance,origin)&&portal::Direction(head,origin,value)) {
                if(!direction)value=head;
                valid=true;
            }
#ifdef D2RVR_NATIVE_OPENXR
            else if(resolved&&direction&&ShrineDefinition(instance.definition)) {
                shrineCorrection=true;valid=true;
            }
#endif
        }
        if(!valid)continue;
#ifdef D2RVR_NATIVE_OPENXR
        // Read each lane's actual native output. Mixed views/instances must not
        // inherit another lane's vertical component or a portal's replacement.
        if(shrineCorrection) {
            portal::Vec native{};
            if(!ReadAt(&native,out.data+12ull*i,sizeof native)||
               !shrine_placement::HorizontalDirection(native,value))continue;
        }
#endif
#ifdef D2RVR_NATIVE_OPENXR
        if(!framevr::WriteMemory(reinterpret_cast<void*>(out.data+12ull*i),&value,sizeof value))continue;
#else
        __try {std::memcpy(reinterpret_cast<void*>(out.data+12ull*i),&value,sizeof value);}
        __except(EXCEPTION_EXECUTE_HANDLER) {continue;}
#endif
#ifdef D2RVR_NATIVE_OPENXR
        if(shrineCorrection) {
            if(!shrineReported.exchange(true)&&logger)
                logger("VR shrines: mana/stamina horizontal direction removed; native vertical component retained");
            continue;
        }
#endif
        if(direction&&!portalReported.exchange(true)&&logger)
            logger("VR portals: native mesh scripts now use shared head position and upright portal-origin facing");
    }
}
void TracedLocalPosition(void* out,const void* in,uint32_t mask,const void* worker) {
#if defined(D2RVR_DEVELOPER) && defined(D2RVR_NATIVE_OPENXR)
    ShrineBatch trace{};portal::Context c{};
    if(mask==19&&ReadAt(&c,reinterpret_cast<uint64_t>(worker)+0x58,sizeof c)&&
       c.worker==reinterpret_cast<uint64_t>(worker))
        PrepareShrineTrace(trace,out,in,&c,shrine::Kind::LocalPosition);
#endif
    CenterPosition(out,in,mask,worker);
#if defined(D2RVR_DEVELOPER) && defined(D2RVR_NATIVE_OPENXR)
    FinishShrineTrace(trace);
#endif
}
void ViewDirection(void* out,const void* in,const void* context) {
#if defined(D2RVR_DEVELOPER) && defined(D2RVR_NATIVE_OPENXR)
    ShrineBatch trace{};PrepareShrineTrace(trace,out,in,context,shrine::Kind::Direction);
#endif
    originalDirection(out,in,context);
#if defined(D2RVR_DEVELOPER) && defined(D2RVR_NATIVE_OPENXR)
    FinishShrineTrace(trace);
#endif
    CorrectScriptView(out,in,context,true);
#ifdef D2RVR_NATIVE_OPENXR
    CorrectOverlayView(out,context);
#endif
}
void ViewPosition(void* out,const void* in,const void* context) {
#if defined(D2RVR_DEVELOPER) && defined(D2RVR_NATIVE_OPENXR)
    ShrineBatch trace{};PrepareShrineTrace(trace,out,in,context,shrine::Kind::Position);
#endif
    originalPosition(out,in,context);
#if defined(D2RVR_DEVELOPER) && defined(D2RVR_NATIVE_OPENXR)
    FinishShrineTrace(trace);
#endif
    CorrectScriptView(out,in,context,false);
}
void ScreenAlign(void* self,const void* view,void* batch) {
    if(!wanted||!wanted()||!viewAligned||!view||!batch){original(self,view,batch);return;}
    alignas(16) float local[16];float eye[3];ULONGLONG at;
    AcquireSRWLockShared(&centerLock);std::memcpy(eye,center,sizeof eye);at=centerAt;ReleaseSRWLockShared(&centerLock);
    if(!at||GetTickCount64()-at>250||!Read(local,view,sizeof local)){original(self,view,batch);return;}
    float distance=0;
    for(int i=0;i<3;++i){const float d=local[12+i]-eye[i];distance+=d*d;}
    // Verify the native context is this VR camera before changing its position;
    // other views (shadow/UI/etc.) keep their native screen-aligned behavior.
    if(!std::isfinite(distance)||distance>4.f){original(self,view,batch);return;}
    std::memcpy(local+12,eye,sizeof eye);
    viewAligned(self,local,batch);
    if(!reported.exchange(true)&&logger)logger("VR billboards: screen-facing quads now face the shared head position; native world-up and particle animation retained");
}
}
void SetCenter(const float position[3]) {
    AcquireSRWLockExclusive(&centerLock);
    std::memcpy(center,position,sizeof center);centerAt=GetTickCount64();
    ReleaseSRWLockExclusive(&centerLock);
}
void ConfigurePortalCenters(bool enabled) {centerEnabled.store(enabled);}
void ConfigureBoneArmorHeight(float units) {
#ifdef D2RVR_NATIVE_OPENXR
    boneArmorHeight.store(bone_armor::Height(units));
#else
    (void)units;
#endif
}
void SetGameForward(const float forward[3]) {
#ifdef D2RVR_NATIVE_OPENXR
    AcquireSRWLockExclusive(&forwardLock);
    gameForward={forward[0],forward[1],forward[2]};forwardAt=GetTickCount64();
    ReleaseSRWLockExclusive(&forwardLock);
#else
    (void)forward;
#endif
}
void Install(void (*log)(const char*),bool (*active)()) {
    if(installed&&portalReady.load()&&originalLocalPosition)return;logger=log;wanted=active;
    using namespace signatures;
    if(!portalReady.load()&&
       (originalDirection||d2rsig::Check(DirectionRva,DirectionSig,sizeof DirectionSig))&&
       (originalPosition||d2rsig::Check(PositionRva,PositionSig,sizeof PositionSig))&&
       d2rsig::Check(DirectionViewsRva,DirectionViewsSig,sizeof DirectionViewsSig)&&
       d2rsig::Check(InstanceLookupRva,InstanceLookupSig,sizeof InstanceLookupSig)&&
       d2rsig::Check(InstancePositionRva,InstancePositionSig,sizeof InstancePositionSig)) {
        gameBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if(!originalDirection)d2rsig::Hook(DirectionRva,DirectionSig,sizeof DirectionSig,reinterpret_cast<void*>(&ViewDirection),reinterpret_cast<void**>(&originalDirection));
        if(!originalPosition)d2rsig::Hook(PositionRva,PositionSig,sizeof PositionSig,reinterpret_cast<void*>(&ViewPosition),reinterpret_cast<void**>(&originalPosition));
        if(originalDirection&&originalPosition) {
            portalReady.store(true);
            if(logger)logger("VR portals: verified native scripted-mesh facing hooks installed; town-portal assets only");
        }
    }
    if(portalReady.load()&&!originalLocalPosition&&
       d2rsig::Check(LocalPositionContextRva,LocalPositionContextSig,sizeof LocalPositionContextSig)&&
       d2rsig::Hook(LocalPositionRva,LocalPositionSig,sizeof LocalPositionSig,reinterpret_cast<void*>(&TracedLocalPosition),reinterpret_cast<void**>(&originalLocalPosition))) {
        if(logger)logger("VR portals: verified native center-position hook installed; NewStuff center offsets only");
#if defined(D2RVR_DEVELOPER) && defined(D2RVR_NATIVE_OPENXR)
        if(logger)logger("Frame shrine trace: read-only native-input capture enabled; floating symbols and environment, view queries and mask19; max1024 samples (128 environment), 8 lanes per callback; mana/stamina correction runs AFTER direction capture");
#endif
    }
    if(installed)return;
    if(!d2rsig::Check(ScreenRva,ScreenSig,sizeof ScreenSig)||
       !d2rsig::Check(ViewRva,ViewSig,sizeof ViewSig)||
       !d2rsig::Check(ViewPositionRva,ViewPositionSig,sizeof ViewPositionSig)||
       !d2rsig::Check(ViewRotateRva,ViewRotateSig,sizeof ViewRotateSig)||
       !d2rsig::Check(ScreenBatchRva,ScreenBatchSig,sizeof ScreenBatchSig))return;
    viewAligned=reinterpret_cast<Align>(d2rsig::Addr(ViewRva));
    installed=d2rsig::Hook(ScreenRva,ScreenSig,sizeof ScreenSig,reinterpret_cast<void*>(&ScreenAlign),reinterpret_cast<void**>(&original));
    if(installed&&logger)logger("VR billboards: verified native screen-to-view-position hook installed");
}
}
