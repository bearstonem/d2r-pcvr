#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D12
#define XR_NO_PROTOTYPES
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <reshade.hpp>
#include <xinput.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "afr_eye_shared.h"
#include "d2r_vr_shared.h"
#include "pad_mirror_shared.h"
#include "transport.h"
#include "stereo_replay.h"
#include "../vr/bow.h"
#include "../vr/melee.h"
#include "frame_math.h"
#include "composition_layers.h"
#include "swapchain_format.h"
#include "input.h"
#include "window_focus.h"
#include "gameplay.h"
#include "quick_cast.h"
#include "interaction_ui.h"
#include "interaction_paint.h"
#include "latest_worker.h"
#include "interaction_icons.h"
#include "potion_models.h"
#include "vr_settings_ui.h"
#include "../vr/settings_menu.h"
#include "journal.h"
#include "native_panels.h"
#include "menu_controls.h"
#include "ui_blit.h"
#include "ui_world.h"
#include "../vr/interactionstate.h"
#include "../vr/journal_ui.h"
#include "../vr/panel_input.h"
#include "d2r_vr_state.h"
#include "foveation.h"
#include "gaze.h"
#include "d2rcam.h"
#include "profile.h"
#include "../vr/skeletons.h"

namespace gamestate {uint32_t WeaponType();uint32_t HandsKey();uint32_t WeaponSet();uint32_t WeaponClass();uint32_t TwoHanded();uint64_t LocalPlayer();uint32_t MenuEpoch();}
namespace gamecmd {void RequestPotion(int slot);void CancelPotion();}
namespace gamestate {void MenuMap(bool wanted);}
namespace gamestate {bool LeftShield();}

namespace framevr {
using Microsoft::WRL::ComPtr;
namespace {
constexpr float kRadToDeg=57.295779513f;
#define XR_FUNCTIONS(X) \
 X(EnumerateInstanceExtensionProperties) X(CreateInstance) X(DestroyInstance) \
 X(GetSystem) X(GetSystemProperties) X(GetInstanceProperties) X(GetD3D12GraphicsRequirementsKHR) \
 X(CreateSession) X(DestroySession) X(PollEvent) X(BeginSession) X(EndSession) \
 X(CreateReferenceSpace) X(DestroySpace) X(LocateSpace) X(LocateViews) \
 X(WaitFrame) X(BeginFrame) X(EndFrame) X(EnumerateSwapchainFormats) \
 X(CreateSwapchain) X(DestroySwapchain) X(EnumerateSwapchainImages) \
 X(AcquireSwapchainImage) X(WaitSwapchainImage) X(ReleaseSwapchainImage) \
 X(StringToPath) X(CreateActionSet) X(DestroyActionSet) X(CreateAction) \
 X(CreateActionSpace) X(SuggestInteractionProfileBindings) X(AttachSessionActionSets) \
 X(SyncActions) X(GetActionStateBoolean) X(GetActionStateFloat) X(GetActionStateVector2f) X(GetActionStatePose) X(ApplyHapticFeedback)
#define DECLARE(name) PFN_xr##name p##name=nullptr;
XR_FUNCTIONS(DECLARE)
#undef DECLARE
PFN_xrGetInstanceProcAddr getProc=nullptr;
HMODULE loader=nullptr;
bool wineOpenXR=false;
LogFn logger=nullptr;
PanelFn wantsPanel=nullptr;
PanelFn inWorld=nullptr;
PanelFn toggleMenus=nullptr;
FrameTiming timing,lastTiming;
std::atomic<uint32_t> pipelineCreates{0};
std::atomic<double> maxPipelineMs{0};
std::atomic<bool> deferredWait{false};
std::atomic<bool> symmetricProjection{true};
std::atomic<bool> eyeTracking{false};
std::atomic<bool> motionControls{false};
bool replayCopiesFinished=false;
gameplay::Engine motion;
skills::QuickCast quickCast;
gameplay::Input motionInput;
gameplay::Output motionOutput;
interaction::Engine interactions;
interaction::Input interactionInput;
interaction::Output interactionOutput;
XrSwapchain interactionChain=XR_NULL_HANDLE;
XrSwapchain settingsChain=XR_NULL_HANDLE;
settings::Model settingsModel;
calibration::Flow bodyCalibration;
calibration::ReleaseGate calibrationRelease;
bool calibrationPrompted=false,floorSpace=false;
uint64_t calibrationSession=0;
int calibrationPaintKey=-1;
math::Vec calibrationOrigin{};
settings::InputGate settingsInputGate;
settings::Hit settingsHover;
std::atomic<bool> settingsOpen{false};
bool settingsAtlasPending=false,settingsImageReady=false;
uint64_t settingsPaintRevision=~0ull,settingsSession=0;
int settingsPaintHover=-1,settingsPaintStatus=-1;
ULONGLONG settingsPaintAt=0;
XrPosef settingsPose{};
constexpr float settingsWidth=.95f,settingsHeight=settingsWidth*settings::Height/settings::Width;
DXGI_FORMAT atlasFormat=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
uint32_t layerLimit=16;
uint64_t interactionPaintKey=~0ull;
uint64_t interactionRequestedKey=~0ull,interactionPaintRequest=0;
interaction::PaintContext interactionPaintContext{},interactionRequestedContext{};
bool interactionPaintValid=false;
LatestWorker<interaction::PaintJob,interaction::PaintedAtlas> interactionPainter([](const interaction::PaintJob& job) {
    const auto start=hookmetrics::Nanos();
    auto pixels=interaction::Paint(job.snapshot,job.output,job.selection);
    return interaction::PaintedAtlas{std::move(pixels),double(hookmetrics::Nanos()-start)/1e6};
});
uint64_t interactionPrefetchSession=0;
uint32_t interactionPrefetchRevision=~0u;
ULONGLONG interactionPaintAt=0;
bool interactionAtlasPending=false;
struct AtlasUpload {
    ComPtr<ID3D12Resource> upload;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    uint64_t fence=0;
};
std::vector<AtlasUpload> atlasUploads;

melee::PoseHistory meleePoses;
melee::PoseHistory shieldPoses;
struct ShieldCast {
    weaponskills::ContactContext context{};uint64_t at=0;uint32_t target=0;
    math::Vec gridTarget{};
    bool issued=false,taken=false,cancelled=false;
};
ShieldCast shieldCast;
weaponskills::Pulse shieldPulse;
uint64_t shieldFeedbackAt=0,cueChangedAt=0,volleyRequestedAt=0;
bool shieldRefused=false;
interaction::SkillCue previousCue{};
uint64_t meleeEpoch=1;
uint32_t bowWorldEpoch=0;
std::atomic<uint64_t> bowRenderTime{0};
uint64_t bowSequence=0;
bowflight::Shot bowPending{},bowReadyShot{},bowPreviewPose{};
bool bowPreviewVisible=false;
bool bowNeedsWorld=false,bowHasShot=false;
std::atomic<ULONGLONG> meleeImpactAt{0},shieldImpactAt{0};
XrSwapchain gameplayChain=XR_NULL_HANDLE;
XrPosef palettePose{};
constexpr float paletteWidth=.45f,paletteHeight=.45f;
ULONGLONG motionAt=0;
bool motionHeadTracked=false;
bool gazeExtension=false,gazeSupported=false,gazeReady=false,gazeValid=false;
XrAction gazeAction=XR_NULL_HANDLE;
XrSpace gazeSpace=XR_NULL_HANDLE;
std::array<gaze::UV,2> gazeCenters{};
gaze::Stability gazeStability;
double Milliseconds() {
    static const double scale=[](){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return 1000.0/double(f.QuadPart);}();
    LARGE_INTEGER t{};QueryPerformanceCounter(&t);return double(t.QuadPart)*scale;
}
bool worldWasLoaded=false,worldCalibrationPending=false,worldRecenter=false;
std::atomic<bool> captureRequested{false};
bool capturePair=false;
uint32_t captureEyes=0,captureSerial=0;
std::recursive_mutex mutex;
DeferredWindowFocus windowFocus;
XrInstance instance=XR_NULL_HANDLE;
XrSystemId system=XR_NULL_SYSTEM_ID;
XrSession session=XR_NULL_HANDLE;
XrSpace space=XR_NULL_HANDLE, headSpace=XR_NULL_HANDLE;
XrSessionState state=XR_SESSION_STATE_UNKNOWN;
bool running=false, frame=false, valid=false, render=false, registered=false, failed=false, pairDrawing=false;
XrFrameState frameState{XR_TYPE_FRAME_STATE};
std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};
XrPosef head{};
math::Vec origin{};
bool haveOrigin=false;
HeightResetButton heightResetButton;
MenuTriggerGate menuTriggerGate;
PressEdge inventoryButton,menuButton;
float lastRecenter=0;
uint32_t width=0,height=0;
DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
reshade::api::swapchain* gameSwapchain=nullptr;
ComPtr<ID3D12Device> device;
ComPtr<ID3D12CommandQueue> queue;
ComPtr<ID3D12Fence> fence;
HANDLE fenceEvent=nullptr;
uint64_t fenceValue=0;
ULONGLONG retryAt=0;
struct Eye {
    XrSwapchain chain=XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D12KHR> images;
    uint32_t index=0;
    bool acquired=false, copied=false;
    uint64_t copyFence=0;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
};
std::array<Eye,2> eyes;
Eye uiEye;
Eye mapEye;
bool mapSupported=false;
constexpr uint32_t mapSide=512;
std::atomic<bool> uiSupported{false};
bool journalMode=false,spatialMode=false,workspacePlaced=false,opaqueMenuMode=false;
UiBlit uiBlit;
UiBlit mapBlit; // Own descriptors: native menu and map copies may share a frame.
potions::Models potionModels;
potions::Scene potionScene;
panels::MouseGate panelMouse;
menus::ReleaseGate menuControls;
menus::Navigation menuNavigation;
menus::Pad nativeMenuPad;
menus::LaserClick laserClick;
panels::ScrollGate panelScroll;
bool rightMouseDown=false,controlDown=false,shiftDown=false;
unsigned uiMissingFrames=0;
journal::Placement journalPlacement;
uint32_t uiWidth=0,uiHeight=0;
int32_t uiCrop[4]{};
XrPosef uiPose{};
float uiSize=.46f;

// Optional timestamp diagnostics. The ordinary pair fence already guarantees
// completion; reading these queries never introduces another GPU wait.
struct GpuProfile {
    ComPtr<ID3D12QueryHeap> heap;
    ComPtr<ID3D12Resource> readback;
    ComPtr<ID3D12CommandAllocator> allocator[2];
    ComPtr<ID3D12GraphicsCommandList> commands[2];
    uint64_t frequency=0;
    unsigned started=0,effects=0,resolved=0,eye=0;
    bool failed=false;
} gpuProfile;
FlatVRAfrEye* afr=nullptr;
HANDLE afrHandle=nullptr;

template<class T> struct Block {
    HANDLE handle=nullptr;
    T* data=nullptr;
    bool Open(const wchar_t* name) {
        handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(T),name);
        if (handle) data=static_cast<T*>(MapViewOfFile(handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(T)));
        if (data) std::memset(data,0,sizeof(T));
        return data!=nullptr;
    }
    void Close() { if(data) UnmapViewOfFile(data);if(handle) CloseHandle(handle);data=nullptr;handle=nullptr; }
};
Block<D2RVR_Shared> tracking;
Block<BWPadMirror> pad;
Block<D2RVR_Actions> actions;
XrActionSet actionSet=XR_NULL_HANDLE;
XrAction gripPose=XR_NULL_HANDLE, stick=XR_NULL_HANDLE, trigger=XR_NULL_HANDLE, squeeze=XR_NULL_HANDLE;
XrAction aimPose=XR_NULL_HANDLE;
XrAction haptic=XR_NULL_HANDLE;
XrAction primary=XR_NULL_HANDLE, secondary=XR_NULL_HANDLE, stickClick=XR_NULL_HANDLE, menu=XR_NULL_HANDLE;
XrPath hands[2]{}; // right, left, matching the existing wire format.
XrSpace handSpaces[2]{};
XrSpace aimSpace=XR_NULL_HANDLE;
XrPosef menuPose{};
bool menuPlaced=false;
bool panelMode=false,pointerVisible=false,pointerHit=false,mouseDown=false,triggerWasDown=false,cancelWasDown=false;
HWND gameWindow=nullptr;
WORD heldKeyScan=0;
ULONGLONG keyReleaseAt=0;
constexpr float panelWidth=2.0f,panelDistance=1.4f;
XrSwapchain pointerChain=XR_NULL_HANDLE;
XrPosef beamPose{},dotPose{};
float beamLength=0;

math::Quat Q(XrQuaternionf q) { return {q.x,q.y,q.z,q.w}; }
math::Vec V(XrVector3f p) { return {p.x,p.y,p.z}; }
void Say(const char* msg) { if(logger) logger(msg); }
struct PipelineStart {reshade::api::device* device=nullptr;uint64_t layout=0;double at=0;};
thread_local PipelineStart pipelineStart;
bool PipelineCreating(reshade::api::device* dev,reshade::api::pipeline_layout layout,uint32_t,const reshade::api::pipeline_subobject*) {
    pipelineStart={dev,layout.handle,Milliseconds()};return false; // observe; do not change creation
}
void PipelineCreated(reshade::api::device* dev,reshade::api::pipeline_layout layout,uint32_t,const reshade::api::pipeline_subobject*,reshade::api::pipeline) {
    if(pipelineStart.device!=dev||pipelineStart.layout!=layout.handle||!pipelineStart.at)return;
    const double elapsed=Milliseconds()-pipelineStart.at;pipelineStart={};
    ++pipelineCreates;
    double previous=maxPipelineMs.load();while(elapsed>previous&&!maxPipelineMs.compare_exchange_weak(previous,elapsed)){}
    if(elapsed>20) {
        static std::atomic<ULONGLONG> lastLog{0};const auto now=GetTickCount64();auto last=lastLog.load();
        if(now-last>1000&&lastLog.compare_exchange_strong(last,now)) {
            char line[140];std::snprintf(line,sizeof line,"Frame pipeline: native pipeline creation took %.1f ms on thread %lu",elapsed,GetCurrentThreadId());Say(line);
        }
    }
}
bool Check(XrResult r,const char* name) {
    if(XR_SUCCEEDED(r)) return true;
    char b[160];std::snprintf(b,sizeof b,"Frame OpenXR: %s failed (%d)",name,int(r));Say(b);return false;
}
bool Hr(HRESULT r,const char* name) {
    if(SUCCEEDED(r)) return true;
    char b[160];std::snprintf(b,sizeof b,"Frame D3D12: %s failed (0x%08lx)",name,(unsigned long)r);Say(b);return false;
}
bool PrepareGpuProfile() {
    if(gpuProfile.failed)return false;
    if(gpuProfile.heap)return true;
    auto& p=gpuProfile;
    D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=10;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=10*sizeof(uint64_t);
    r.Height=r.DepthOrArraySize=r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bool ok=SUCCEEDED(queue->GetTimestampFrequency(&p.frequency))&&p.frequency&&
        SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&p.readback)));
    for(int e=0;ok&&e<2;++e)ok=SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&p.allocator[e])))&&
        SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,p.allocator[e].Get(),nullptr,IID_PPV_ARGS(&p.commands[e])))&&SUCCEEDED(p.commands[e]->Close());
    if(ok)ok=SUCCEEDED(device->CreateQueryHeap(&q,IID_PPV_ARGS(&p.heap)));
    if(!ok){p.failed=true;Say("Frame profile: GPU timestamps unavailable; CPU diagnostics remain enabled");}
    return ok;
}
void StartGpuEye(unsigned eye) {
    auto& p=gpuProfile;p.eye=eye;
    if(!profile::Active()||!profile::GpuEnabled()||!PrepareGpuProfile())return;
    if(FAILED(p.allocator[eye]->Reset())||FAILED(p.commands[eye]->Reset(p.allocator[eye].Get(),nullptr))){p.failed=true;return;}
    p.commands[eye]->EndQuery(p.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,eye*5);
    if(FAILED(p.commands[eye]->Close())){p.failed=true;return;}
    ID3D12CommandList* lists[]={p.commands[eye].Get()};queue->ExecuteCommandLists(1,lists);p.started|=1u<<eye;
}
void ProfileEffects(reshade::api::effect_runtime* rt,reshade::api::command_list* cl,reshade::api::resource_view,reshade::api::resource_view) {
    auto& p=gpuProfile;
    if(!profile::Active()||!pairDrawing||!(p.started&(1u<<p.eye))||!gameSwapchain||
       rt->get_current_back_buffer().handle!=gameSwapchain->get_current_back_buffer().handle)return;
    reinterpret_cast<ID3D12GraphicsCommandList*>(cl->get_native())->EndQuery(p.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,p.eye*5+1);
    p.effects|=1u<<p.eye;
}
void ReadGpuProfile() {
    auto& p=gpuProfile;if(!profile::Active()||p.resolved!=3||p.failed)return;
    void* mapped=nullptr;D3D12_RANGE range{0,10*sizeof(uint64_t)};
    if(FAILED(p.readback->Map(0,&range,&mapped)))return;
    uint64_t ts[10];std::memcpy(ts,mapped,sizeof ts);D3D12_RANGE noWrites{0,0};p.readback->Unmap(0,&noWrites);
    bool valid=true;for(unsigned e=0;e<2;++e)for(unsigned i=1;i<5;++i)valid&=ts[e*5+i]>=ts[e*5+i-1];
    if(!valid)return;
    const double k=1000.0/p.frequency;
    for(unsigned e=0;e<2;++e){const auto* t=ts+e*5;timing.gpuSceneMs[e]=(t[1]-t[0])*k;timing.gpuEffectsMs[e]=(t[2]-t[1])*k;timing.gpuCopyMs[e]=(t[4]-t[3])*k;}
    timing.gpuValid=true;
}
void ReleaseUiKey() {
    if(!heldKeyScan)return;
    INPUT key{};key.type=INPUT_KEYBOARD;key.ki.wScan=heldKeyScan;
    key.ki.dwFlags=KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
    SendInput(1,&key,sizeof key);
    heldKeyScan=0;keyReleaseAt=0;
}
void PulseUiKey(WORD virtualKey) {
    ReleaseUiKey();
    const WORD scan=WORD(MapVirtualKeyW(virtualKey,MAPVK_VK_TO_VSC));
    if(!scan)return;
    INPUT key{};key.type=INPUT_KEYBOARD;key.ki.wScan=scan;key.ki.dwFlags=KEYEVENTF_SCANCODE;
    if(SendInput(1,&key,sizeof key)!=1){Say("Frame OpenXR: UI key injection failed");return;}
    heldKeyScan=scan;keyReleaseAt=GetTickCount64()+125;
    Say(virtualKey==VK_ESCAPE?"Frame OpenXR: menu Escape scan code down":"Frame OpenXR: inventory I scan code down");
}
void PanelModifier(WORD key,bool wanted,bool& held) {
    if(wanted==held)return;
    INPUT input{};input.type=INPUT_KEYBOARD;
    input.ki.wScan=WORD(MapVirtualKeyW(key,MAPVK_VK_TO_VSC));
    input.ki.dwFlags=KEYEVENTF_SCANCODE|(wanted?0:KEYEVENTF_KEYUP);
    if(SendInput(1,&input,sizeof input)==1)held=wanted;
}
void ReleasePanelInput() {
    if(mouseDown||rightMouseDown||controlDown||shiftDown)panelinput::OwnPointer();
    for(int i=0;i<2;++i) {
        bool& held=i?rightMouseDown:mouseDown;
        if(held){INPUT up{};up.type=INPUT_MOUSE;up.mi.dwFlags=i?MOUSEEVENTF_RIGHTUP:MOUSEEVENTF_LEFTUP;SendInput(1,&up,sizeof up);held=false;}
    }
    PanelModifier(VK_CONTROL,false,controlDown);PanelModifier(VK_SHIFT,false,shiftDown);
    panelMouse.Suspend();panelScroll={};
}
weaponskills::ContactContext ShieldContext(const melee::Sample& sample,uint64_t session) {
    return {session,sample.epoch,sample.skillContext,sample.equipment,sample.skillSlot,sample.skillOwner};
}
weaponskills::ContactContext LiveShieldContext() {
    return {gamestate::LocalPlayer(),(meleeEpoch<<32)|bowWorldEpoch,motionInput.skillContext,
        motionInput.equipment,motion.Selection(),motionInput.selectedOwner};
}
void CancelShieldCast() {
    shieldPoses.Clear();shieldPulse.Cancel();
    // A native cast may already be in its wind-up. Preserve its cancellation
    // during its bounded native wind-up instead of falling back to a target.
    if(shieldCast.issued)shieldCast.cancelled=true;else shieldCast={};
}
void ClearInput() {
    panelinput::WorldInput(false);
    gamestate::MenuMap(false);
    menuControls.Suspend();menuNavigation.Reset();nativeMenuPad={};laserClick={};panelinput::UseController();
    quickCast.Suspend();
    bodyCalibration.Suspend();
    settingsInputGate.Suspend();
    ++meleeEpoch;motion.Cancel();motionOutput={};motionAt=0;gamecmd::CancelPotion();
    interactions.Reset();interactionOutput={};interactionstate::Cancel();skel::SetInteractionHidden(0);
    potionScene={};
    journalPlacement.LoseTracking();
    meleePoses.Clear();CancelShieldCast();
    bowNeedsWorld=bowHasShot=false;
    skel::SetMotionBow(false,false,0);
    ReleaseUiKey();
    ReleasePanelInput();
    triggerWasDown=cancelWasDown=pointerVisible=pointerHit=false;
    heightResetButton={};
    // Tracking loss ends any pending mouse press, but a held trigger must
    // still be released before it can become a fresh world attack.
    menuTriggerGate.consumed=true;
    if(tracking.data) { tracking.data->headValid=0;tracking.data->handsValid=0;++tracking.data->counter; }
    if(pad.data) { const auto c=pad.data->counter;*pad.data={};pad.data->version=BW_PAD_MIRROR_VERSION;pad.data->writer_pid=GetCurrentProcessId();pad.data->reserved[0]=uint32_t(menus::Mode::Blocked);pad.data->reserved[1]=gamestate::MenuEpoch();pad.data->counter=c+1; }
    if(actions.data) { actions.data->held=0;++actions.data->counter; }
}
bool LoadFunctions() {
    // Proton's WineOpenXR exports the runtime negotiation entrypoint.
    // Negotiate it directly, or use a standard Windows loader as a fallback.
    loader=LoadLibraryW(L"wineopenxr.dll");
    wineOpenXR=loader!=nullptr;
    if(!loader) loader=LoadLibraryW(L"openxr_loader.dll");
    if(!loader) { Say("Frame OpenXR: no WineOpenXR/Windows OpenXR loader; use Proton 11 or Experimental");return false; }
    getProc=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader,"xrGetInstanceProcAddr"));
    if(!getProc) {
        auto negotiate=reinterpret_cast<PFN_xrNegotiateLoaderRuntimeInterface>(GetProcAddress(loader,"xrNegotiateLoaderRuntimeInterface"));
        if(!negotiate) {Say("Frame OpenXR: runtime exports neither getProc nor negotiation");return false;}
        XrNegotiateLoaderInfo li{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO,XR_LOADER_INFO_STRUCT_VERSION,sizeof(XrNegotiateLoaderInfo),1,1,XR_API_VERSION_1_0,XR_API_VERSION_1_0};
        XrNegotiateRuntimeRequest rr{XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST,XR_RUNTIME_INFO_STRUCT_VERSION,sizeof(XrNegotiateRuntimeRequest)};
        if(!Check(negotiate(&li,&rr),"negotiate WineOpenXR")||!rr.getInstanceProcAddr)return false;
        getProc=rr.getInstanceProcAddr;
    }
    if(!Check(getProc(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",reinterpret_cast<PFN_xrVoidFunction*>(&pEnumerateInstanceExtensionProperties)),"get enumerate extensions") ||
       !Check(getProc(XR_NULL_HANDLE,"xrCreateInstance",reinterpret_cast<PFN_xrVoidFunction*>(&pCreateInstance)),"get create instance")) return false;
    uint32_t count=0;
    if(!Check(pEnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr),"enumerate extensions")) return false;
    std::vector<XrExtensionProperties> ext(count,{XR_TYPE_EXTENSION_PROPERTIES});
    if(!Check(pEnumerateInstanceExtensionProperties(nullptr,count,&count,ext.data()),"enumerate extensions")) return false;
    const auto supports=[&](const char* name){return std::any_of(ext.begin(),ext.end(),[&](const auto& e){return !std::strcmp(e.extensionName,name);});};
    char capabilities[180];
    std::snprintf(capabilities,sizeof capabilities,"Frame capability: eye gaze advertised=%d; FB foveation=%d; META eye-tracked foveation=%d (not enabled)",
        supports("XR_EXT_eye_gaze_interaction"),supports("XR_FB_foveation"),supports("XR_META_foveation_eye_tracked"));
    Say(capabilities);
    if(std::none_of(ext.begin(),ext.end(),[](const auto& e){return !std::strcmp(e.extensionName,XR_KHR_D3D12_ENABLE_EXTENSION_NAME);})) {
        Say("Frame OpenXR: runtime does not expose XR_KHR_D3D12_enable");return false;
    }
    // Bind the optional capability at session creation so the in-headset
    // toggle can enable it later. Samples are read only while eyeTracking is on.
    gazeExtension=supports(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
    const char* enabled[]={XR_KHR_D3D12_ENABLE_EXTENSION_NAME,XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME};
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(ci.applicationInfo.applicationName,"D2R Steam Frame prototype");
    ci.applicationInfo.applicationVersion=1;ci.applicationInfo.apiVersion=XR_API_VERSION_1_0;
    ci.enabledExtensionCount=gazeExtension?2:1;ci.enabledExtensionNames=enabled;
    if(!Check(pCreateInstance(&ci,&instance),"create instance")) return false;
#define LOAD(name) if(!Check(getProc(instance,"xr" #name,reinterpret_cast<PFN_xrVoidFunction*>(&p##name)),"load " #name)) return false;
    XR_FUNCTIONS(LOAD)
#undef LOAD
    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};pGetInstanceProperties(instance,&props);
    char b[192];std::snprintf(b,sizeof b,"Frame OpenXR: runtime %s",props.runtimeName);Say(b);
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if(!Check(pGetSystem(instance,&si,&system),"get headset system"))return false;
    XrSystemProperties limits{XR_TYPE_SYSTEM_PROPERTIES};
    if(XR_SUCCEEDED(pGetSystemProperties(instance,system,&limits)))layerLimit=std::min(32u,limits.graphicsProperties.maxLayerCount);
    if(gazeExtension) {
        XrSystemEyeGazeInteractionPropertiesEXT gazeProps{XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT};
        XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};sp.next=&gazeProps;
        gazeSupported=Check(pGetSystemProperties(instance,system,&sp),"query eye-gaze system")&&gazeProps.supportsEyeGazeInteraction;
        Say(gazeSupported?"Frame gaze: system supports eye tracking; awaiting tracked samples":"Frame gaze: system has no usable eye tracker; full-quality fallback");
    } else if(eyeTracking.load())Say("Frame gaze: extension unavailable; full-quality fallback");
    return true;
}
XrPath Path(const char* p) { XrPath out=XR_NULL_PATH;pStringToPath(instance,p,&out);return out; }
bool MakeAction(XrAction& out,const char* name,XrActionType type) {
    XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
    std::snprintf(ci.actionName,sizeof ci.actionName,"%s",name);
    std::snprintf(ci.localizedActionName,sizeof ci.localizedActionName,"%s",name);
    ci.actionType=type;ci.countSubactionPaths=2;ci.subactionPaths=hands;
    return Check(pCreateAction(actionSet,&ci,&out),"create controller action");
}
bool SetupActions() {
    hands[0]=Path("/user/hand/right");hands[1]=Path("/user/hand/left");
    XrActionSetCreateInfo ci{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(ci.actionSetName,"d2r");std::strcpy(ci.localizedActionSetName,"Diablo II");
    if(!Check(pCreateActionSet(instance,&ci,&actionSet),"create action set")) return false;
    if(!MakeAction(gripPose,"hands",XR_ACTION_TYPE_POSE_INPUT) || !MakeAction(stick,"sticks",XR_ACTION_TYPE_VECTOR2F_INPUT) ||
       !MakeAction(aimPose,"menu_aim",XR_ACTION_TYPE_POSE_INPUT) || !MakeAction(haptic,"feedback",XR_ACTION_TYPE_VIBRATION_OUTPUT) ||
       !MakeAction(trigger,"triggers",XR_ACTION_TYPE_FLOAT_INPUT) || !MakeAction(squeeze,"grips",XR_ACTION_TYPE_FLOAT_INPUT) ||
       !MakeAction(primary,"primary",XR_ACTION_TYPE_BOOLEAN_INPUT) || !MakeAction(secondary,"secondary",XR_ACTION_TYPE_BOOLEAN_INPUT) ||
       !MakeAction(stickClick,"stick_click",XR_ACTION_TYPE_BOOLEAN_INPUT) || !MakeAction(menu,"menu",XR_ACTION_TYPE_BOOLEAN_INPUT)) return false;
    for(const auto* profile:{"/interaction_profiles/valve/index_controller","/interaction_profiles/oculus/touch_controller","/interaction_profiles/microsoft/motion_controller"}) {
        const bool index=std::strstr(profile,"index"), touch=std::strstr(profile,"touch");
        std::vector<XrActionSuggestedBinding> bindings;
        for(int i=0;i<2;++i) {
            const std::string base=i==0?"/user/hand/right/input/":"/user/hand/left/input/";
            auto bind=[&](XrAction a,const char* suffix){bindings.push_back({a,Path((base+suffix).c_str())});};
            bind(gripPose,"grip/pose");bind(stick,"thumbstick");bind(stickClick,"thumbstick/click");bind(trigger,"trigger/value");
            bind(aimPose,"aim/pose");
            bindings.push_back({haptic,Path((std::string(i==0?"/user/hand/right":"/user/hand/left")+"/output/haptic").c_str())});
            // Motion controllers expose squeeze/click (boolean), so omit our float grip there.
            if(index||touch) bind(squeeze,index?"squeeze/force":"squeeze/value");
            if(index||touch) {
                bind(primary,touch&&i==1?"x/click":"a/click");
                bind(secondary,touch&&i==1?"y/click":"b/click");
            }
            if((touch&&i==1)||(!index&&!touch)) bind(menu,"menu/click");
        }
        XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggestion.interactionProfile=Path(profile);suggestion.countSuggestedBindings=uint32_t(bindings.size());suggestion.suggestedBindings=bindings.data();
        Check(pSuggestInteractionProfileBindings(instance,&suggestion),profile);
    }
    if(gazeSupported) {
        XrActionCreateInfo gci{XR_TYPE_ACTION_CREATE_INFO};gci.actionType=XR_ACTION_TYPE_POSE_INPUT;
        std::strcpy(gci.actionName,"foveation_gaze");std::strcpy(gci.localizedActionName,"Foveated rendering gaze");
        if(Check(pCreateAction(actionSet,&gci,&gazeAction),"create gaze action")) {
            XrActionSuggestedBinding binding{gazeAction,Path("/user/eyes_ext/input/gaze_ext/pose")};
            XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            suggestion.interactionProfile=Path("/interaction_profiles/ext/eye_gaze_interaction");
            suggestion.countSuggestedBindings=1;suggestion.suggestedBindings=&binding;
            gazeReady=Check(pSuggestInteractionProfileBindings(instance,&suggestion),"bind eye gaze");
        }
    }
    XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};ai.countActionSets=1;ai.actionSets=&actionSet;
    if(!Check(pAttachSessionActionSets(session,&ai),"attach action set")) return false;
    for(int i=0;i<2;++i) {
        XrActionSpaceCreateInfo sci{XR_TYPE_ACTION_SPACE_CREATE_INFO};sci.action=gripPose;sci.subactionPath=hands[i];sci.poseInActionSpace.orientation.w=1;
        if(!Check(pCreateActionSpace(session,&sci,&handSpaces[i]),"create hand space")) return false;
    }
    XrActionSpaceCreateInfo sci{XR_TYPE_ACTION_SPACE_CREATE_INFO};sci.action=aimPose;sci.subactionPath=hands[0];sci.poseInActionSpace.orientation.w=1;
    if(!Check(pCreateActionSpace(session,&sci,&aimSpace),"create menu aim space"))return false;
    if(gazeReady) {
        XrActionSpaceCreateInfo gi{XR_TYPE_ACTION_SPACE_CREATE_INFO};gi.action=gazeAction;gi.poseInActionSpace.orientation.w=1;
        gazeReady=Check(pCreateActionSpace(session,&gi,&gazeSpace),"create gaze space");
    }
    return true;
}
bool WaitGpu();
bool WaitGpuValue(uint64_t value);
bool UploadAtlas(XrSwapchain& chain,DXGI_FORMAT eyeFormat,int atlasWidth,int atlasHeight,const std::vector<uint8_t>& pixels,bool dynamic=false,bool* pending=nullptr) {
    // A tiny static atlas is independent of ReShade's effect compiler.
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.createFlags=dynamic?0:XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format=eyeFormat;ci.width=atlasWidth;ci.height=atlasHeight;ci.sampleCount=ci.faceCount=ci.arraySize=ci.mipCount=1;
    if(!chain&&!Check(pCreateSwapchain(session,&ci,&chain),"create atlas"))return false;
    uint32_t count=0;
    if(!Check(pEnumerateSwapchainImages(chain,0,&count,nullptr),"count static atlas images"))return false;
    std::vector<XrSwapchainImageD3D12KHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    if(!Check(pEnumerateSwapchainImages(chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"get static atlas images"))return false;
    uint32_t index=0;XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(!Check(pAcquireSwapchainImage(chain,&ai,&index),"acquire static atlas atlas"))return false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;
    if(!Check(pWaitSwapchainImage(chain,&wi),"wait static atlas atlas"))return false;
    auto* dest=images[index].texture;const auto desc=dest->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
    device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC uploadDesc{};uploadDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;uploadDesc.Width=bytes;
    uploadDesc.Height=uploadDesc.DepthOrArraySize=uploadDesc.MipLevels=uploadDesc.SampleDesc.Count=1;uploadDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upload;
    if(!Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"create static atlas upload"))return false;
    uint8_t* data=nullptr;D3D12_RANGE readRange{};
    if(!Hr(upload->Map(0,&readRange,reinterpret_cast<void**>(&data)),"map static atlas upload"))return false;
    std::memset(data,0,size_t(bytes));
    const bool bgra=eyeFormat==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||eyeFormat==DXGI_FORMAT_B8G8R8A8_UNORM;
    for(int y=0;y<atlasHeight;++y) {
      if(!bgra) {std::memcpy(data+footprint.Offset+y*footprint.Footprint.RowPitch,pixels.data()+y*atlasWidth*4,atlasWidth*4);continue;}
      for(int x=0;x<atlasWidth;++x) {
        auto* pixel=data+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;
        const auto* rgba=pixels.data()+(y*atlasWidth+x)*4;
        pixel[0]=rgba[bgra?2:0];pixel[1]=rgba[1];pixel[2]=rgba[bgra?0:2];pixel[3]=rgba[3];
      }
    }
    upload->Unmap(0,nullptr);
    ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;
    if(!Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)),"create static atlas allocator")||
       !Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)),"create static atlas list"))return false;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={dest,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_DEST};
    list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=dest;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
    if(!Hr(list->Close(),"close static atlas list"))return false;
    ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    if(!Hr(queue->Signal(fence.Get(),++fenceValue),"signal atlas fence"))return false;
    if(dynamic) {
        atlasUploads.push_back({upload,alloc,list,fenceValue});
        if(pending)*pending=true;else interactionAtlasPending=true;
        return true; // Released at EndPair, after the pair's existing GPU fence.
    }
    if(!WaitGpu())return false;
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return Check(pReleaseSwapchainImage(chain,&ri),"release static atlas atlas");
}
bool SetupPointer(DXGI_FORMAT eyeFormat) {
    std::vector<uint8_t> pixels(64*96*4);
    for(int y=0;y<96;++y)for(int x=0;x<64;++x) {
        auto* p=pixels.data()+(y*64+x)*4;p[0]=40;p[1]=210;p[2]=255;
        const float radius=std::hypot((x-31.5f)/30.f,(y-63.5f)/30.f);
        p[3]=y<32?200:uint8_t(std::clamp((1-radius)*10,0.f,1.f)*255);
    }
    return UploadAtlas(pointerChain,eyeFormat,64,96,pixels);
}
// Static text atlas: allocated/uploaded once, never repainted in the frame loop.
// Native skill icons, names and belt counts require a separate read-only UI adapter.
bool SetupGameplay(DXGI_FORMAT eyeFormat) {
    if(!interactionPainter.Start())Say("Frame UI: background painter unavailable; interaction artwork cannot refresh");
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=1024;info.bmiHeader.biHeight=-1024;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap||!bits) {if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return false;}
    auto old=SelectObject(dc,bitmap);std::memset(bits,0,1024*1024*4);
    auto fill=[&](RECT r,COLORREF color) {HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);};
    fill({0,0,1024,768},RGB(15,23,30));
    HFONT font=CreateFontW(33,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
    auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(225,235,240));
    auto label=[&](const wchar_t* text,RECT rect) {DrawTextW(dc,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);};
    label(L"SKILLS  /  EQUIPMENT  /  BELT",{0,0,1024,96});
    const wchar_t* names[]={L"B",L"X",L"Y",L"RB",L"LT + B",L"LT + X",L"LT + Y",L"LT + RB",
        L"Melee / Bow",L"Swap I / II",L"",L"",L"Belt 1",L"Belt 2",L"Belt 3",L"Belt 4"};
    for(int i=0;i<16;++i) {
        RECT cell{(i%4)*256+7,96+(i/4)*144+7,(i%4+1)*256-7,96+(i/4+1)*144-7};
        fill(cell,i>=12?RGB(42,34,25):RGB(27,43,54));label(names[i],cell);
    }
    label(L"Trigger: select skill   /   Grip: take belt potion",{0,672,1024,768});
    // Neutral bottle, labelled by belt column. Colour/type is deliberately not
    // guessed: the native game decides which item occupies that column.
    for(int i=0;i<4;++i) {
        const int x=i*128;
        fill({x+45,780,x+83,828},RGB(210,200,160));
        fill({x+22,828,x+106,996},RGB(45,140,170));
        const wchar_t name[]={wchar_t(L'1'+i),0};label(name,{x+22,840,x+106,980});
    }
    // Arrow points along the quad's +Y axis. Two crossed planes give the
    // nocked arrow volume at negligible cost and no per-frame texture work.
    fill({540,795,548,1005},RGB(200,160,95));
    HBRUSH arrowhead=CreateSolidBrush(RGB(235,240,245));auto oldBrush=SelectObject(dc,arrowhead);
    POINT tip[]={{544,771},{562,803},{526,803}};Polygon(dc,tip,3);
    POINT feather[]={{540,975},{520,997},{520,1017},{542,1000}};Polygon(dc,feather,4);
    POINT feather2[]={{548,975},{568,997},{568,1017},{546,1000}};Polygon(dc,feather2,4);
    SelectObject(dc,oldBrush);DeleteObject(arrowhead);
    label(L"Trigger: nock",{576,768,1024,853});
    label(L"Pull back",{576,853,1024,938});
    label(L"Release to shoot",{576,938,1024,1024});
    GdiFlush();
    std::vector<uint8_t> rgba(1024*1024*4);const auto* b=static_cast<const uint8_t*>(bits);
    for(int i=0;i<1024*1024;++i) {
        rgba[4*i]=b[4*i+2];rgba[4*i+1]=b[4*i+1];rgba[4*i+2]=b[4*i];
        rgba[4*i+3]=(b[4*i]||b[4*i+1]||b[4*i+2])?255:0;
    }
    SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    return UploadAtlas(gameplayChain,eyeFormat,1024,1024,rgba);
}
bool Initialize(reshade::api::command_queue* rq,reshade::api::swapchain* swap) {
    if(rq->get_device()->get_api()!=reshade::api::device_api::d3d12) { Say("Frame OpenXR requires D3D12");return false; }
    if(!LoadFunctions()) return false;
    device=reinterpret_cast<ID3D12Device*>(rq->get_device()->get_native());
    queue=reinterpret_cast<ID3D12CommandQueue*>(rq->get_native());
    // This MinGW SDK defines OPTIONS6 but omits its data structure. Match the
    // Windows SDK's five 32-bit fields (the tier enum has UINT representation).
    struct VrsFeatureData {
        BOOL AdditionalShadingRatesSupported;
        BOOL PerPrimitiveShadingRateSupportedWithViewportIndexing;
        UINT VariableShadingRateTier;
        UINT ShadingRateImageTileSize;
        BOOL BackgroundProcessingSupported;
    } vrs{};
    static_assert(sizeof(VrsFeatureData)==20);
    if(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6,&vrs,sizeof vrs))) {
        char capabilities[160];
        std::snprintf(capabilities,sizeof capabilities,"Frame capability: D3D12 VRS tier=%d tile=%u additional rates=%d (not enabled)",
            int(vrs.VariableShadingRateTier),vrs.ShadingRateImageTileSize,int(vrs.AdditionalShadingRatesSupported));
        Say(capabilities);
    } else Say("Frame capability: D3D12 VRS feature query unsupported (not enabled)");
    XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    if(!Check(pGetD3D12GraphicsRequirementsKHR(instance,system,&req),"get D3D12 requirements")) return false;
    const LUID actual=device->GetAdapterLuid();
    if(actual.HighPart!=req.adapterLuid.HighPart||actual.LowPart!=req.adapterLuid.LowPart) { Say("Frame OpenXR: game and SteamVR are using different GPUs");return false; }
    D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};levels.NumFeatureLevels=1;levels.pFeatureLevelsRequested=&req.minFeatureLevel;
    if(!Hr(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS,&levels,sizeof levels),"check feature level")) return false;
    XrGraphicsBindingD3D12KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};binding.device=device.Get();binding.queue=queue.Get();
    XrSessionCreateInfo ci{XR_TYPE_SESSION_CREATE_INFO};ci.systemId=system;ci.next=&binding;
    if(!Check(pCreateSession(instance,&ci,&session),"create D3D12 session")) return false;
    XrReferenceSpaceCreateInfo sci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};sci.poseInReferenceSpace.orientation.w=1;sci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;
    floorSpace=XR_SUCCEEDED(pCreateReferenceSpace(session,&sci,&space));
    if(!floorSpace) { sci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;if(!Check(pCreateReferenceSpace(session,&sci,&space),"create local space"))return false; }
    sci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
    if(!Check(pCreateReferenceSpace(session,&sci,&headSpace),"create head space")||!SetupActions()) return false;
    auto* buffer=reinterpret_cast<ID3D12Resource*>(swap->get_current_back_buffer().handle);
    const auto desc=buffer->GetDesc();width=uint32_t(desc.Width);height=desc.Height;format=desc.Format;
    foveation::Initialize(device.Get(),queue.Get(),width,height);
    uint32_t n=0;pEnumerateSwapchainFormats(session,0,&n,nullptr);
    std::vector<int64_t> formats(n);
    if(!Check(pEnumerateSwapchainFormats(session,n,&n,formats.data()),"enumerate formats")) return false;
    const DXGI_FORMAT eyeFormat=CopiedSwapchainFormat(format,formats);
    if(eyeFormat==DXGI_FORMAT_UNKNOWN) {
        std::string message="Frame OpenXR: no color-correct copy format for game backbuffer "+std::to_string(int(format))+"; runtime formats:";
        for(auto f:formats)message+=" "+std::to_string(f);
        Say(message.c_str());return false;
    }
    {char message[128];std::snprintf(message,sizeof message,"Frame OpenXR: game format %d, eye format %d",int(format),int(eyeFormat));Say(message);}
    uiSupported=std::find(formats.begin(),formats.end(),int64_t(DXGI_FORMAT_R16G16B16A16_FLOAT))!=formats.end()&&uiBlit.Initialize(device.Get(),&Say);
    mapSupported=std::find(formats.begin(),formats.end(),int64_t(DXGI_FORMAT_R16G16B16A16_FLOAT))!=formats.end()&&mapBlit.Initialize(device.Get(),&Say);
    for(auto& e:eyes) {
        XrSwapchainCreateInfo c{XR_TYPE_SWAPCHAIN_CREATE_INFO};c.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        c.format=eyeFormat;c.width=width;c.height=height;c.sampleCount=1;c.faceCount=1;c.arraySize=1;c.mipCount=1;
        if(!Check(pCreateSwapchain(session,&c,&e.chain),"create eye swapchain")) return false;
        n=0;pEnumerateSwapchainImages(e.chain,0,&n,nullptr);e.images.resize(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        if(!Check(pEnumerateSwapchainImages(e.chain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data())),"enumerate eye textures")) return false;
        if(!Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&e.allocator)),"create eye copy allocator")||
           !Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,e.allocator.Get(),nullptr,IID_PPV_ARGS(&e.commands)),"create eye copy list")||
           !Hr(e.commands->Close(),"close initial eye copy list"))return false;
    }
    if(!Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"create fence")) return false;
    fenceEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!fenceEvent) return false;
    if(!SetupPointer(eyeFormat))return false;
    atlasFormat=eyeFormat;
    if(!SetupGameplay(eyeFormat))Say("Frame gameplay: palette atlas unavailable; motion controls remain disabled");
    potionModels.Initialize(device.Get(),queue.Get(),eyeFormat,width,height,&Say);
    if(!tracking.Open(D2RVR_SHARED_NAME)||!pad.Open(BW_PAD_MIRROR_NAME)||!actions.Open(D2RVR_ACTIONS_NAME)) return false;
    tracking.data->version=D2RVR_SHARED_VERSION;pad.data->version=BW_PAD_MIRROR_VERSION;actions.data->version=D2RVR_ACTIONS_VERSION;
    gameSwapchain=swap;
    char b[160];std::snprintf(b,sizeof b,"Frame OpenXR: D3D12 session ready, eyes %ux%u format %d (game format %d)",width,height,int(eyeFormat),int(format));Say(b);
    return true;
}
bool Bool(XrAction action,int hand) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};gi.action=action;gi.subactionPath=hands[hand];
    XrActionStateBoolean v{XR_TYPE_ACTION_STATE_BOOLEAN};return XR_SUCCEEDED(pGetActionStateBoolean(session,&gi,&v))&&v.isActive&&v.currentState;
}
float Float(XrAction action,int hand) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};gi.action=action;gi.subactionPath=hands[hand];
    XrActionStateFloat v{XR_TYPE_ACTION_STATE_FLOAT};return XR_SUCCEEDED(pGetActionStateFloat(session,&gi,&v))&&v.isActive?v.currentState:0;
}
XrVector2f Stick(int hand) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};gi.action=stick;gi.subactionPath=hands[hand];
    XrActionStateVector2f v{XR_TYPE_ACTION_STATE_VECTOR2F};if(XR_FAILED(pGetActionStateVector2f(session,&gi,&v))||!v.isActive)return {};return v.currentState;
}
bool FocusGameForUi() {
    // Called only for an intentional menu action in a focused XR session.
    // Hovering the laser must not steal focus from another desktop window.
    if(!gameWindow||!IsWindow(gameWindow))return false;
    if(GetForegroundWindow()==gameWindow)return true;
    // SetForegroundWindow re-entered D2R's focus handler while its input lock
    // was held on native Windows (Akara dialogue hang). Finish this render
    // callback first; withhold injection until a later frame confirms focus.
    windowFocus.Request(gameWindow);
    return false;
}
void MenuPointer(const menus::Pad& controls) {
    nativeMenuPad={};
    pointerVisible=pointerHit=false;
    if(settingsModel.open||bodyCalibration.Active()||calibrationRelease.blocked)return;
    const bool spatialReady=spatialMode&&(journalMode?journalPlacement.Placed():workspacePlaced)&&uiCrop[2]>0;
    const bool uiActive=panelMode||spatialReady;
    const auto surfacePose=spatialMode?uiPose:menuPose;
    const float surfaceWidth=spatialMode?uiSize:panelWidth;
    const float surfaceHeight=spatialMode?uiSize*(uiCrop[2]>0?float(uiCrop[3])/uiCrop[2]:1.3f):panelWidth*height/width;
    const bool pressed=controls.rt>140;
    bool focused=gameWindow&&GetForegroundWindow()==gameWindow;
    if(uiActive&&menus::Busy(controls)&&!focused)focused=FocusGameForUi();
    XrSpaceLocation aim{XR_TYPE_SPACE_LOCATION};
    const auto flags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
        XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    const bool aimTracked=uiActive&&XR_SUCCEEDED(pLocateSpace(aimSpace,space,frameState.predictedDisplayTime,&aim))&&(aim.locationFlags&flags)==flags;
    math::PanelHit hit{};
    if(aimTracked) {
        const auto ray=V(aim.pose.position),direction=math::Rotate(Q(aim.pose.orientation),{0,0,-1});
        pointerHit=math::IntersectPanel(ray,direction,V(surfacePose.position),Q(surfacePose.orientation),surfaceWidth,surfaceHeight,hit);
        beamLength=pointerHit?hit.distance:panelDistance*1.8f;
        const auto mid=ray+direction*(beamLength*.5f);
        const auto rot=math::BeamOrientation(direction,V(head.position)-mid);
        beamPose.position={mid.x,mid.y,mid.z};beamPose.orientation={rot.x,rot.y,rot.z,rot.w};pointerVisible=true;
        if(pointerHit) {
            const auto point=hit.point+math::Rotate(Q(surfacePose.orientation),{0,0,.006f});
            dotPose=surfacePose;dotPose.position={point.x,point.y,point.z};
        }
    }
    const bool wasNative=menuNavigation.Native();
    nativeMenuPad=menuNavigation.Step(uiActive&&focused,controls,pointerHit,hit.u,hit.v,mouseDown||rightMouseDown);
    if(wasNative!=menuNavigation.Native()&&uiActive&&focused)
        Say(menuNavigation.Native()?"Frame panels: native controller navigation":"Frame panels: laser navigation");
    const bool laser=!menuNavigation.Native();
    if(!laser){panelinput::UseController();pointerVisible=pointerHit=false;}
    const bool active=uiActive&&focused&&aimTracked&&laser;
    if(active)panelinput::OwnPointer();
    if(active&&pointerHit) {
        RECT client{};
        if(GetClientRect(gameWindow,&client)) {
            math::Vec native{hit.u,hit.v,0};
            if(spatialMode&&!journal::Pixel({uiCrop[0],uiCrop[1],uiCrop[2],uiCrop[3]},hit.u,hit.v,uiWidth,uiHeight,native))pointerHit=false;
            POINT pixel{LONG(native.x*(client.right-1)),LONG(native.y*(client.bottom-1))};
            if(pointerHit&&ClientToScreen(gameWindow,&pixel))SetCursorPos(pixel.x,pixel.y);
            else pointerHit=false;
        }
    }
    const bool modifiers=active&&(pointerHit||mouseDown||rightMouseDown);
    PanelModifier(VK_CONTROL,modifiers&&controls.lt>140,controlDown);
    PanelModifier(VK_SHIFT,modifiers&&(controls.buttons&menus::RightBumper),shiftDown);
    laserClick.Step(pressed,(controls.buttons&menus::LeftBumper)!=0);
    const auto edges=panelMouse.Step(active,pointerHit,laserClick.Left(),laserClick.Right());
    for(int i=0;i<2;++i) {
        bool& held=i?rightMouseDown:mouseDown;
        if(edges.release[i]&&held) {INPUT up{};up.type=INPUT_MOUSE;up.mi.dwFlags=i?MOUSEEVENTF_RIGHTUP:MOUSEEVENTF_LEFTUP;SendInput(1,&up,sizeof up);held=false;}
        if(edges.press[i]) {
            INPUT down{};down.type=INPUT_MOUSE;down.mi.dwFlags=i?MOUSEEVENTF_RIGHTDOWN:MOUSEEVENTF_LEFTDOWN;
            held=SendInput(1,&down,sizeof down)==1;
            if(!held)panelMouse.down[i]=false;
        }
    }
    triggerWasDown=pressed;cancelWasDown=(controls.buttons&menus::B)!=0;
}

void SampleGaze() {
    gazeValid=false;
    if(!eyeTracking.load()||!render||panelMode){gazeStability.Reset();return;}
    // These diagnostics report only validity/age categories, never gaze poses,
    // coordinates, fixation targets or a recorded gaze trajectory.
    enum {Unavailable,Inactive,Untracked,Untimed,Stale,Outside,Recovering,Usable,Count};
    int status=Unavailable;
    if(gazeReady&&gazeSpace) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};gi.action=gazeAction;
        XrActionStatePose action{XR_TYPE_ACTION_STATE_POSE};status=Inactive;
        if(XR_SUCCEEDED(pGetActionStatePose(session,&gi,&action))&&action.isActive) {
            XrEyeGazeSampleTimeEXT sample{XR_TYPE_EYE_GAZE_SAMPLE_TIME_EXT};
            XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};loc.next=&sample;status=Untracked;
            constexpr auto needed=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
                XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
            if(XR_SUCCEEDED(pLocateSpace(gazeSpace,space,frameState.predictedDisplayTime,&loc))&&(loc.locationFlags&needed)==needed) {
                status=sample.time?Stale:Untimed;
                if(gaze::Fresh(frameState.predictedDisplayTime,sample.time)) {
                    bool projected=true;
                    for(unsigned i=0;i<2;++i) {
                        const auto& f=views[i].fov;
                        projected&=gaze::Project(Q(loc.pose.orientation),Q(views[i].pose.orientation),
                            {f.angleLeft,f.angleRight,f.angleDown,f.angleUp},gazeCenters[i]);
                    }
                    status=projected?Recovering:Outside;
                    gazeValid=gazeStability.Update(projected,gazeCenters);
                    if(gazeValid)status=Usable;
                }
            }
        }
    }
    if(status!=Recovering&&status!=Usable)gazeStability.Reset();
    static std::array<uint32_t,Count> counts{};static ULONGLONG reportAt=0;
    ++counts[status];const auto now=GetTickCount64();
    if(now>=reportAt) {
        char line[300];std::snprintf(line,sizeof line,
            "Frame gaze: samples usable=%u recovery/jump=%u unavailable=%u inactive=%u untracked=%u untimed=%u stale=%u outside=%u; invalid uses full quality",
            counts[Usable],counts[Recovering],counts[Unavailable],counts[Inactive],counts[Untracked],counts[Untimed],counts[Stale],counts[Outside]);
        Say(line);counts={};reportAt=now+5000;
    }
}
gameplay::Pose MotionPose(XrPosef pose) {return {V(pose.position),Q(pose.orientation)};}
XrPosef XrPose(gameplay::Pose p) {return {{p.q.x,p.q.y,p.q.z,p.q.w},{p.p.x,p.p.y,p.p.z}};}
void MotionPointer() {
    motionInput.hover=-1;
    if(!motionInput.active||!interactions.WheelVisible(motionInput.palette)||!motionInput.tracked[1]||!motionInput.aimTracked)return;
    // Attached to the left hand with its orientation; raise the palm to face
    // the palette toward the eyes. No camera-locked panel following the head.
    auto pose=motionInput.hand[1];
    const math::Quat palmTilt{-.70710678f,0,0,.70710678f};
    pose.q=math::Multiply(pose.q,palmTilt);
    pose.p=pose.p+math::Rotate(pose.q,{0,.12f,.025f});palettePose=XrPose(pose);
    const auto ray=motionInput.aim.p,direction=math::Rotate(motionInput.aim.q,{0,0,-1});
    math::PanelHit hit;
    pointerHit=math::IntersectPanel(ray,direction,pose.p,pose.q,paletteWidth,paletteHeight,hit);
    beamLength=pointerHit?hit.distance:.65f;
    const auto mid=ray+direction*(beamLength*.5f);
    beamPose=XrPose({mid,math::BeamOrientation(direction,V(head.position)-mid)});pointerVisible=true;
    if(pointerHit) {
        motionInput.hover=interaction::WheelCell(hit.u,hit.v,interactionOutput.hover,interactions.Page());
        dotPose=XrPose({hit.point+math::Rotate(pose.q,{0,0,.003f}),pose.q});
    }
}
void MotionPulse(int hand,float strength,XrDuration duration) {
    if(!haptic||!pApplyHapticFeedback)return;
    XrHapticActionInfo ai{XR_TYPE_HAPTIC_ACTION_INFO};ai.action=haptic;ai.subactionPath=hands[hand];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.duration=duration;vibration.frequency=XR_FREQUENCY_UNSPECIFIED;vibration.amplitude=strength;
    const auto result=pApplyHapticFeedback(session,&ai,reinterpret_cast<XrHapticBaseHeader*>(&vibration));
    static bool reported[2]{};static ULONGLONG errorAt=0;
    if(XR_SUCCEEDED(result)&&!reported[hand]) {
        Say(hand==0?"Frame gameplay: right-hand haptic accepted":"Frame gameplay: left-hand haptic accepted");reported[hand]=true;
    } else if(XR_FAILED(result)&&GetTickCount64()>=errorAt) {
        char message[128];std::snprintf(message,sizeof message,"Frame gameplay: haptic failed hand=%d result=%d",hand,int(result));Say(message);
        errorAt=GetTickCount64()+5000;
    }
}
void MotionFeedback(const gameplay::Output& out) {
    static double lastPull=0;static float previousDraw=0;
    const unsigned event=out.feedback;
    if(event) {
        MotionPulse(0,event==6?.12f:event==4?.2f:event==2||event==3?.65f:.45f,event==4||event==6?20000000:45000000);
        if(out.bowReady&&(event==1||event==2||event==5))MotionPulse(1,event==2?.65f:.25f,35000000);
        lastPull=motionInput.time;previousDraw=out.draw;
    } else if(out.bowDrawing&&motionInput.time-lastPull>.07&&std::fabs(out.draw-previousDraw)>.018f) {
        // A light ratchet felt while pulling, rather than only the native
        // attack rumble after release. No repeated buzz while held still.
        MotionPulse(0,.22f+.28f*out.draw,25000000);
        MotionPulse(1,.1f+.15f*out.draw,20000000);
        lastPull=motionInput.time;previousDraw=out.draw;
    }
    if(!out.bowDrawing)previousDraw=0;
}
gameplay::Weapon MotionWeapon() {
    switch(gamestate::WeaponType()) {
        case D2RVR_TYPE_BOW:return gameplay::Weapon::Bow;
        case D2RVR_TYPE_CROSSBOW:return gameplay::Weapon::Crossbow;
        case D2RVR_TYPE_JAVELIN:return gameplay::Weapon::Javelin;
        case D2RVR_TYPE_UNARMED:case D2RVR_TYPE_SWORD:case D2RVR_TYPE_AXE:case D2RVR_TYPE_MACE:
        case D2RVR_TYPE_SCEPTER:case D2RVR_TYPE_WAND:case D2RVR_TYPE_STAFF:case D2RVR_TYPE_POLEARM:
        case D2RVR_TYPE_SPEAR:case D2RVR_TYPE_DAGGER:case D2RVR_TYPE_CLAW:
        case D2RVR_TYPE_ORB:case D2RVR_TYPE_THROWING:return gameplay::Weapon::Melee;
        case D2RVR_TYPE_UNKNOWN:return gameplay::Weapon::Unknown;
        default:return gamestate::WeaponClass()==D2RVR_WEAPON_MELEE?gameplay::Weapon::Melee:gameplay::Weapon::Other;
    }
}
gameplay::MeleeStyle MotionMeleeStyle() {
    switch(gamestate::WeaponType()) {
        case D2RVR_TYPE_UNARMED:return gameplay::MeleeStyle::Unarmed;
        case D2RVR_TYPE_DAGGER:case D2RVR_TYPE_CLAW:case D2RVR_TYPE_ORB:return gameplay::MeleeStyle::Short;
        case D2RVR_TYPE_STAFF:case D2RVR_TYPE_POLEARM:case D2RVR_TYPE_SPEAR:return gameplay::MeleeStyle::Long;
        default:return (gamestate::TwoHanded()&D2RVR_TWO_HANDS_ON)?gameplay::MeleeStyle::Long:gameplay::MeleeStyle::OneHand;
    }
}
void ResetSettingsInput() {
    panelinput::WorldInput(false);
    gamestate::MenuMap(false);
    motion.Cancel();motionOutput={};interactions.Reset();interactionstate::Cancel();
    meleePoses.Clear();CancelShieldCast();++meleeEpoch;bowNeedsWorld=bowHasShot=false;
    skel::SetMotionBow(false,false,0);gamecmd::CancelPotion();menuTriggerGate.consumed=true;
    settingsInputGate.Consume();ReleaseUiKey();
    ReleasePanelInput();
}
void OpenSettings() {
    settingsImageReady=false;settingsPaintRevision=~0ull;settingsPaintAt=0;
    settingsModel.Open(vrsettings::Read());settingsOpen=true;settingsSession=gamestate::LocalPlayer();
    const auto q=math::Yaw(math::Heading(Q(head.orientation)));
    const auto p=V(head.position)+math::Rotate(q,{0,-.06f,-1.05f});
    settingsPose=XrPose({p,q});settingsHover={};ResetSettingsInput();
    Say("Frame VR settings: panel opened");
}
void CloseSettings() {
    settingsModel.Close();settingsOpen=false;settingsImageReady=false;ResetSettingsInput();
    Say("Frame VR settings: panel closed; held input consumed");
}
void OpenBodyCalibration() {
    if(settingsModel.open)CloseSettings();
    bodyCalibration.Begin();calibrationRelease.Consume();calibrationPrompted=true;
    calibrationSession=gamestate::LocalPlayer();calibrationPaintKey=-1;
    settingsOpen=true;settingsImageReady=false;settingsPaintAt=0;
    const auto q=math::Yaw(math::Heading(Q(head.orientation)));
    settingsPose=XrPose({V(head.position)+math::Rotate(q,{0,-.06f,-1.05f}),q});
    ResetSettingsInput();Say("Frame body calibration: started; upright T-pose + both triggers; Right B cancels");
}
void CalibrationInput() {
    if(!bodyCalibration.Active())return;
    if(!inWorld||!inWorld()||calibrationSession!=gamestate::LocalPlayer()){
        bodyCalibration.Cancel();settingsOpen=false;settingsImageReady=false;ResetSettingsInput();return;
    }
    const auto phaseBefore=bodyCalibration.phase;
    bodyCalibration.Observe(vrsettings::Read(),motionInput.time);
    if(phaseBefore==calibration::Phase::Saving&&bodyCalibration.phase==calibration::Phase::Saved){
        origin=calibrationOrigin;haveOrigin=true;worldRecenter=true;
        MotionPulse(0,.45f,70000000);MotionPulse(1,.45f,70000000);
        const auto m=bodyCalibration.Get();char message[180];
        std::snprintf(message,sizeof message,"Frame body calibration: saved eye height %.3f m, controller span %.3f m",m.eyeHeight,m.armSpan);Say(message);
    }
    calibration::Sample sample;sample.time=motionInput.time;sample.floor=floorSpace;
    sample.headTracked=motionHeadTracked;sample.head=V(head.position);sample.rotation=Q(head.orientation);
    sample.cancel=Bool(secondary,0);
    for(int i=0;i<2;++i){sample.handTracked[i]=motionInput.tracked[i];sample.hand[i]=motionInput.hand[i].p;sample.trigger[i]=Float(trigger,i);}
    if(bodyCalibration.Step(sample)){
        const auto request=calibration::SaveRequest(vrsettings::Read(),bodyCalibration.Get());
        calibrationOrigin=V(head.position);
        if(request.sequence&&request.changed&&vrsettings::Submit(request))bodyCalibration.Submitted(request.sequence);
        else if(request.sequence&&!request.changed){
            bodyCalibration.Submitted(request.sequence);auto unchanged=vrsettings::Read();
            unchanged.sequence=request.sequence;unchanged.result=settings::Result::Saved;
            bodyCalibration.Observe(unchanged,motionInput.time);origin=calibrationOrigin;haveOrigin=true;worldRecenter=true;
        }else bodyCalibration.Failed();
    }
    if(!bodyCalibration.Active()){
        settingsOpen=false;settingsImageReady=false;settingsPaintRevision=~0ull;ResetSettingsInput();
        Say("Frame body calibration: closed; held input consumed");return;
    }
    const int key=int(bodyCalibration.hint)*100+int(bodyCalibration.progress*20);
    const auto now=GetTickCount64();
    if(key!=calibrationPaintKey&&now>=settingsPaintAt&&!settingsAtlasPending&&atlasUploads.size()<3){
        auto pixels=settings::Paint(settingsModel,{},"",&bodyCalibration);
        if(!pixels.empty()&&UploadAtlas(settingsChain,atlasFormat,settings::Width,settings::Height,pixels,true,&settingsAtlasPending))calibrationPaintKey=key;
        settingsPaintAt=now+100;
    }
}
void SettingsPointer(const settings::InputGate::Edges& edges) {
    if(!settingsModel.open)return;
    settingsModel.Observe(vrsettings::Read());settingsHover={};pointerVisible=pointerHit=false;
    XrSpaceLocation aim{XR_TYPE_SPACE_LOCATION};
    const auto needed=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
                      XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    if(motionHeadTracked&&XR_SUCCEEDED(pLocateSpace(aimSpace,space,frameState.predictedDisplayTime,&aim))&&(aim.locationFlags&needed)==needed){
        const auto ray=V(aim.pose.position),direction=math::Rotate(Q(aim.pose.orientation),{0,0,-1});math::PanelHit hit;
        pointerHit=math::IntersectPanel(ray,direction,V(settingsPose.position),Q(settingsPose.orientation),settingsWidth,settingsHeight,hit);
        beamLength=pointerHit?hit.distance:1.5f;const auto mid=ray+direction*(beamLength*.5f);
        beamPose=XrPose({mid,math::BeamOrientation(direction,V(head.position)-mid)});pointerVisible=true;
        if(pointerHit){settingsHover=settings::HitTest(settingsModel.page,hit.u,hit.v);dotPose=settingsPose;
            const auto p=hit.point+math::Rotate(Q(settingsPose.orientation),{0,0,.004f});dotPose.position={p.x,p.y,p.z};}
    }
    if(edges.click&&pointerHit&&settingsImageReady&&settingsPaintRevision==settingsModel.revision){
        if(settingsHover.kind==settings::Kind::Close)CloseSettings();
        else if(settingsHover.kind==settings::Kind::Recenter){ResetHeight();MotionPulse(1,.4f,35000000);}
        else if(settingsHover.kind==settings::Kind::Calibrate&&settingsModel.ready&&!settingsModel.Busy()&&!settingsModel.Dirty())OpenBodyCalibration();
        else {
            const auto request=settingsModel.Click(settingsHover);
            if(request.sequence&&!vrsettings::Submit(request)){
                auto result=vrsettings::Read();result.sequence=request.sequence;result.result=settings::Result::Failed;settingsModel.Observe(result);
            }
        }
    }
    if(!settingsModel.open)return;
    const int hover=int(settingsHover.kind)*100+(settingsHover.index+1)*3+settingsHover.delta;
    const int status=!foveation::Available()?0:!gazeSupported?1:gazeValid?3:2;
    const int paintStatus=status+(!std::strcmp(stereoreplay::Status(),"Unavailable")?4:0);
    const char* descriptions[]={"Foveated shading unavailable on this GPU/runtime; full-quality rendering.",
        "Foveated shading available. Eye tracking unavailable on this headset/runtime.",
        "Foveated shading available. Gaze inactive or waiting for tracked samples.",
        "Foveated shading available. Eye-tracking samples are active."};
    const auto now=GetTickCount64();
    if(now>=settingsPaintAt&&!settingsAtlasPending&&atlasUploads.size()<3&&
       (settingsPaintRevision!=settingsModel.revision||settingsPaintHover!=hover||settingsPaintStatus!=paintStatus)){
        auto pixels=settings::Paint(settingsModel,settingsHover,descriptions[status],nullptr,paintStatus>=4);
        if(!pixels.empty()&&UploadAtlas(settingsChain,atlasFormat,settings::Width,settings::Height,pixels,true,&settingsAtlasPending)){
            settingsPaintRevision=settingsModel.revision;settingsPaintHover=hover;settingsPaintStatus=paintStatus;
        }
        settingsPaintAt=now+100;
    }
}
void Input() {
    gazeValid=false;
    if(!tracking.data) return;
    if(!valid||state!=XR_SESSION_STATE_FOCUSED) {gazeStability.Reset();ClearInput();return; }
    XrActiveActionSet active{actionSet,XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};si.countActiveActionSets=1;si.activeActionSets=&active;
    if(pSyncActions(session,&si)!=XR_SUCCESS) {gazeStability.Reset();ClearInput();return;}
    const bool nativeUi=panelMode||spatialMode;
    const uint32_t inputEpoch=gamestate::MenuEpoch();
    menus::Pad physical;
    if(Bool(primary,0))physical.buttons|=menus::A;
    if(Bool(secondary,0))physical.buttons|=menus::B;
    if(Bool(primary,1))physical.buttons|=menus::X;
    if(Bool(secondary,1))physical.buttons|=menus::Y;
    if(Bool(menu,0)||Bool(menu,1))physical.buttons|=menus::Start;
    if(Bool(stickClick,1))physical.buttons|=menus::LeftStick;
    if(Bool(stickClick,0))physical.buttons|=menus::RightStick;
    if(Float(squeeze,1)>.6f)physical.buttons|=menus::LeftBumper;
    if(Float(squeeze,0)>.6f)physical.buttons|=menus::RightBumper;
    physical.lt=uint8_t(std::clamp(Float(trigger,1),0.f,1.f)*255);
    physical.rt=uint8_t(std::clamp(Float(trigger,0),0.f,1.f)*255);
    const auto physicalLeft=Stick(1),physicalRight=Stick(0);
    const auto axis=[](float value){return int16_t(std::clamp(value,-1.f,1.f)*32767.f);};
    physical.lx=axis(physicalLeft.x);physical.ly=axis(physicalLeft.y);
    physical.rx=axis(physicalRight.x);physical.ry=axis(physicalRight.y);
    const bool modUi=settingsModel.open||bodyCalibration.Active()||calibrationRelease.blocked;
    const auto controls=menuControls.Step(modUi?menus::Mode::Blocked:nativeUi?menus::Mode::Native:menus::Mode::World,inputEpoch,physical);
    SampleGaze();
    atlasUploads.erase(std::remove_if(atlasUploads.begin(),atlasUploads.end(),[](const auto& a){return fence->GetCompletedValue()>=a.fence;}),atlasUploads.end());
    if(!calibrationPrompted&&!settingsModel.open&&!panelMode&&!spatialMode&&inWorld&&inWorld()){
        const auto config=vrsettings::Read();
        if(config.ready&&config.result!=settings::Result::Saving){calibrationPrompted=true;if(!calibration::Stored(config.values))OpenBodyCalibration();}
    }
    const uint8_t quickFaces=((controls.buttons&menus::A)?1:0)|((controls.buttons&menus::B)?2:0)|
        ((controls.buttons&menus::X)?4:0)|((controls.buttons&menus::Y)?8:0);
    const bool quickAllowed=motionControls.load()&&gameplayChain&&motionHeadTracked&&
        !panelMode&&!spatialMode&&!settingsModel.open&&!bodyCalibration.Active()&&!calibrationRelease.blocked&&!interactionOutput.wheel&&
        interactionOutput.bottle<0&&interactionOutput.owner[0]!=interaction::Owner::Holster&&
        interactionOutput.owner[1]!=interaction::Owner::Holster&&inWorld&&inWorld();
    quickCast.Capture(quickAllowed,controls.lt/255.f,quickFaces);
    const bool inventoryPressed=inventoryButton.Update((controls.buttons&menus::X)!=0)&&!quickCast.Suppress(2);
    const bool menuPressed=menuButton.Update((controls.buttons&menus::Start)!=0);
    const bool settingsChord=menus::SettingsChord(physical,quickCast.Suppress(3));
    const auto settingsEdges=settingsInputGate.Step(motionHeadTracked,settingsChord,Float(trigger,0),Bool(secondary,0)&&!quickCast.Suppress(1));
    bool settingsOwned=settingsModel.open||calibrationRelease.Step(bodyCalibration.Active(),Float(trigger,0),Float(trigger,1),physical.buttons&(menus::A|menus::B|menus::X|menus::Y|menus::Start));
    if(settingsModel.open&&settingsSession!=gamestate::LocalPlayer()){CloseSettings();settingsOwned=true;}
    if(settingsEdges.chord&&!bodyCalibration.Active()&&!calibrationRelease.blocked){if(settingsModel.open)CloseSettings();else OpenSettings();settingsOwned=true;}
    else if(settingsModel.open&&(settingsEdges.cancel||menuPressed)){CloseSettings();settingsOwned=true;}
    settingsOwned=settingsOwned||settingsModel.open;
    bool focused=gameWindow&&GetForegroundWindow()==gameWindow;
    if(!settingsOwned&&(menuPressed||inventoryPressed)&&!focused)focused=FocusGameForUi();
    if(heldKeyScan&&(!focused||GetTickCount64()>=keyReleaseAt))ReleaseUiKey();
    if(!settingsOwned&&!nativeUi&&focused&&menuPressed)PulseUiKey(VK_ESCAPE);
    if(!settingsOwned&&!nativeUi&&inventoryPressed&&inWorld&&inWorld()) {
        // Keyboard shortcuts can be gated after switching input layouts or
        // panels. Queue the game's own panel actions on its UI thread instead.
        // This action targets D2R directly and only requires XR focus (checked
        // above), so a desktop terminal cannot silently swallow Left X.
        if(toggleMenus&&toggleMenus())Say("Frame OpenXR: Left X menu toggle queued");
        else Say("Frame OpenXR: Left X menu toggle unavailable");
    }
    if(journalMode) {
        XrSpaceLocation left{XR_TYPE_SPACE_LOCATION};
        const auto needed=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
                          XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        const bool tracked=XR_SUCCEEDED(pLocateSpace(handSpaces[1],space,frameState.predictedDisplayTime,&left))&&(left.locationFlags&needed)==needed;
        uiSize=.46f;
        journalPlacement.Step({V(left.pose.position),Q(left.pose.orientation)},tracked,Float(squeeze,1)>.6f);
        const auto p=journalPlacement.Get();uiPose=XrPose({p.p,p.q});
    }
    if(!settingsOwned&&!settingsChord)MenuPointer(controls);
    else {nativeMenuPad={};menuNavigation.Reset();panelinput::UseController();pointerVisible=pointerHit=false;triggerWasDown=Float(trigger,0)>.2f;cancelWasDown=Bool(secondary,0);}
    SettingsPointer(settingsEdges);
    settingsOwned=settingsOwned||bodyCalibration.Active();
    auto& s=*tracking.data;s.version=D2RVR_SHARED_VERSION;s.headValid=1;
    const auto q=Q(head.orientation);
    const auto forward=math::Rotate(q,{0,0,-1});
    const float heading=math::Heading(q);
    const auto level=math::Multiply(math::Conjugate(math::Yaw(heading)),q);
    const auto right=math::Rotate(level,{1,0,0});
    s.headYawDeg=heading*kRadToDeg;s.headPitchDeg=std::asin(std::clamp(forward.y,-1.f,1.f))*kRadToDeg;
    s.headRollDeg=-std::atan2(right.y,right.x)*kRadToDeg;s.pitchValid=s.rollValid=1;
    s.headHeightM=head.position.y;s.sampleStamp=D2RVRStampNow();s.sampleStampMagic=D2RVR_SAMPLE_STAMP_MAGIC;
    s.roomMagic=D2RVR_ROOM_MAGIC;s.headRoom[0]=head.position.x;s.headRoom[1]=head.position.y;s.headRoom[2]=head.position.z;s.headYawRoomDeg=s.headYawDeg;
    s.gripMagic=D2RVR_GRIP_MAGIC;s.rightGrip=Float(squeeze,0);s.leftGrip=Float(squeeze,1);s.handsValid=0;
    if(heightResetButton.Update((controls.buttons&menus::LeftStick)!=0,s.leftGrip)&&!settingsOwned&&!nativeUi)ResetHeight();
    motionInput={};motionInput.time=Milliseconds()/1000.;motionInput.head=MotionPose(head);
    motionInput.active=motionControls.load()&&gameplayChain&&motionHeadTracked&&!panelMode&&!spatialMode&&!settingsOwned&&!settingsChord&&!inventoryPressed&&!menuPressed&&inWorld&&inWorld();
    motionInput.palette=(controls.buttons&menus::Y)&&!quickCast.Suppress(3);
    motionInput.grip[0]=(physical.buttons&menus::RightBumper)&&!(controls.buttons&menus::RightBumper)?0:s.rightGrip;
    motionInput.grip[1]=(physical.buttons&menus::LeftBumper)&&!(controls.buttons&menus::LeftBumper)?0:s.leftGrip;
    motionInput.cancel=(controls.buttons&menus::B)&&!quickCast.Suppress(1);
    motionInput.quickCast=quickCast.Layer();
    motionInput.trigger=controls.rt/255.f;motionInput.weapon=MotionWeapon();
    motionInput.meleeStyle=MotionMeleeStyle();
    motionInput.equipment=gamestate::HandsKey()^(gamestate::WeaponSet()<<30);
    const auto undo=math::Conjugate(math::Yaw(heading));
    for(int i=0;i<2;++i) {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        const XrSpaceLocationFlags required=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if(XR_SUCCEEDED(pLocateSpace(handSpaces[i],space,frameState.predictedDisplayTime,&loc))&&(loc.locationFlags&required)==required) {
            const auto tracked=XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
            motionInput.tracked[i]=(loc.locationFlags&tracked)==tracked;
            motionInput.hand[i]=MotionPose(loc.pose);
            s.handsValid|=1u<<i;
            const auto p=math::Rotate(undo,V(loc.pose.position)-V(head.position));
            const auto rot=math::Multiply(undo,Q(loc.pose.orientation));
            float* dest=i==0?s.rightHand:s.leftHand;dest[0]=p.x;dest[1]=p.y;dest[2]=p.z;
            float* r=i==0?s.rightRot:s.leftRot;r[0]=rot.x;r[1]=rot.y;r[2]=rot.z;r[3]=rot.w;
        }
    }
    CalibrationInput();
    XrSpaceLocation aimed{XR_TYPE_SPACE_LOCATION};
    const auto aimFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
                        XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    if(XR_SUCCEEDED(pLocateSpace(aimSpace,space,frameState.predictedDisplayTime,&aimed))&&(aimed.locationFlags&aimFlags)==aimFlags) {
        motionInput.aim=MotionPose(aimed.pose);motionInput.aimTracked=true;
    }
    MotionPointer();
    interactionInput={};interactionInput.time=motionInput.time;interactionInput.session=gamestate::LocalPlayer();
    interactionInput.at=GetTickCount64();interactionInput.epoch=meleeEpoch;interactionInput.active=motionInput.active;
    interactionInput.head={motionInput.head.p,motionInput.head.q};
    interactionInput.snapshot=interactionstate::Read();interactionInput.reply=interactionstate::Status();
    interactionInput.spell=motion.Selection()>=0;interactionInput.wheel=motionInput.palette;interactionInput.cancel=motionInput.cancel;
    interactionInput.quickCast=motionInput.quickCast;
    interactionInput.reservedLeft=(controls.buttons&menus::LeftStick)!=0;interactionInput.trigger=motionInput.trigger;interactionInput.hover=motionInput.hover;
    interactionInput.leftTrigger=controls.lt/255.f;
    if(!interactionPaintValid||interactionPaintContext!=interaction::Context(interactionInput.snapshot,interactionOutput,
        interaction::Fresh(interactionInput.snapshot,interactionInput.session,interactionInput.at)))interactionInput.hover=-1;
    for(int i=0;i<2;++i){interactionInput.tracked[i]=motionInput.tracked[i];interactionInput.grip[i]=motionInput.grip[i];
        interactionInput.hand[i]={motionInput.hand[i].p,motionInput.hand[i].q};}
    interactionOutput=interactions.Step(interactionInput);
    if(interactionOutput.settings){OpenSettings();settingsOwned=true;interactionInput.active=motionInput.active=false;}
    gamestate::MenuMap(mapSupported&&interaction::MapWanted(interactionInput,interactionOutput,interactionInput.at));
    if(interactionOutput.select>=-1)motion.Select(interactionOutput.select);
    if(interactionOutput.cancelPending)interactionstate::Cancel();
    if(interactionOutput.request.action!=interaction::Action::None)interactionstate::Request(interactionOutput.request);
    if(interactionOutput.feedback)MotionPulse(interactionOutput.feedbackHand,interactionOutput.feedback==2?.65f:.25f,interactionOutput.feedback>=3?100000000:35000000);
    motionInput.palette=false; // Dedicated interaction owner handles the new wheel/belt.
    motionInput.active=motionInput.active&&!interactionOutput.block;
    const auto& quickSnapshot=interactionInput.snapshot;
    const bool quickFresh=interaction::Fresh(quickSnapshot,interactionInput.session,interactionInput.at);
    uint8_t quickAvailable=0;
    if(quickFresh)for(int i=0;i<skills::Count;++i)
        if(quickSnapshot.skills[i].known&&quickSnapshot.skills[i].available&&quickSnapshot.skills[i].id>=0)quickAvailable|=1u<<i;
    motionInput.quickSkill=quickCast.Resolve(motionInput.active&&motionInput.tracked[0]&&motionInput.tracked[1]&&motionInput.aimTracked&&quickFresh,
        motionInput.grip[1]>.6f,quickAvailable,quickSnapshot.session,meleeEpoch,motionInput.equipment,quickSnapshot.bindingBank,motionInput.time);
    const int selected=motion.Selection();
    if(quickFresh&&selected>=0&&selected<skills::Count) {
        const auto& skill=quickSnapshot.skills[selected];
        motionInput.selectedSkill=skill.id;motionInput.selectedOwner=skill.owner;
        motionInput.selectedAvailable=skill.known&&skill.available&&skill.motion;
    }
    motionInput.skillContext=interactionInput.session^(uint64_t(quickSnapshot.bindingBank+1)<<48)^(meleeEpoch<<16);
    motionInput.shieldEquipped=gamestate::LeftShield()&&::melee::ShieldReady();
    motionOutput=motion.Step(motionInput);motionAt=GetTickCount64();
    const bool shieldValid=motionInput.active&&!motionOutput.block&&!motionInput.quickCast&&
        motionInput.shieldEquipped&&motionInput.selectedAvailable&&motionInput.selectedSkill==weaponskills::Smite&&
        weaponskills::Current(shieldCast.context,LiveShieldContext(),shieldCast.at,motionAt);
    if(!shieldValid){
        if(shieldCast.issued&&motionAt>=shieldCast.at&&motionAt-shieldCast.at<=1500)shieldCast.cancelled=true;
        else shieldCast={};
        shieldPulse.Cancel();
    }
    else if(!shieldCast.issued) {
        if(motionAt-shieldCast.at<=150&&shieldPulse.Start(selected,weaponskills::Smite,motionInput.selectedOwner,motionInput.skillContext,motionInput.time))shieldCast.issued=true;
        else {shieldCast={};shieldPulse.Cancel();}
    }
    if(shieldCast.issued&&!shieldCast.cancelled) {
        motionOutput.skill=shieldPulse.Get(selected,weaponskills::Smite,motionInput.selectedOwner,motionInput.skillContext,motionInput.time);
        motionOutput.meleeContact=false;
        // Keep observing the left stroke during native wind-up. Resetting it
        // here would allow the same continuous swing to become another bash.
    }
    if(motionOutput.nativeBowRelease&&motionInput.selectedSkill==weaponskills::Strafe)volleyRequestedAt=motionAt;
    interaction::SkillCue cue;
    cue.slot=motionInput.quickSkill>=0?motionInput.quickSkill:motion.Selection();
    cue.temporary=motionInput.quickSkill>=0;
    if(cue.slot>=0&&cue.slot<skills::Count&&quickFresh) {
        const auto& skill=quickSnapshot.skills[cue.slot];cue.id=skill.id;cue.owner=skill.owner;
        cue.known=skill.known;cue.available=skill.known&&skill.available&&skill.id>=0;
        std::snprintf(cue.name,sizeof cue.name,"%s",skill.known?skill.name:"Unknown skill");
        std::snprintf(cue.hint,sizeof cue.hint,"%s",cue.available?(cue.temporary?"Point & trigger":
            weaponskills::Hint(skill.id,skill.motion&&motionInput.weapon==gameplay::Weapon::Bow,skill.motion&&motionInput.shieldEquipped)):"Assignment unavailable");
        cue.motionUnavailable=!cue.temporary&&cue.available&&skill.id==weaponskills::Smite&&gamestate::LeftShield()&&!::melee::ShieldReady();
        if(cue.motionUnavailable)std::snprintf(cue.hint,sizeof cue.hint,"Shield bash unavailable; use trigger");
    } else if(cue.slot<0) {
        cue.known=cue.available=true;
        std::snprintf(cue.name,sizeof cue.name,"%s",motionInput.weapon==gameplay::Weapon::Bow?"Basic Arrow":"Basic Attack");
        std::snprintf(cue.hint,sizeof cue.hint,"%s",motionInput.weapon==gameplay::Weapon::Bow?"Draw & release":motionInput.weapon==gameplay::Weapon::Javelin?"Swing / throw":"Swing / trigger");
    } else {
        std::snprintf(cue.name,sizeof cue.name,"Skill unavailable");std::snprintf(cue.hint,sizeof cue.hint,"Refresh assignments");
    }
    cue.busy=(shieldCast.issued&&!shieldCast.cancelled)||(cue.id==weaponskills::Strafe&&quickFresh&&quickSnapshot.actionKnown&&quickSnapshot.actionMode==16&&
        volleyRequestedAt&&motionAt>=volleyRequestedAt&&motionAt-volleyRequestedAt<10000);
    if(cue.busy)std::snprintf(cue.hint,sizeof cue.hint,"%s",cue.id==weaponskills::Strafe?"Volley active":"Shield bash active");
    if(shieldFeedbackAt&&motionAt-shieldFeedbackAt<1200&&cue.id==weaponskills::Smite&&shieldRefused)
        std::snprintf(cue.hint,sizeof cue.hint,"Action refused");
    if(cue!=previousCue){cueChangedAt=motionAt;previousCue=cue;}
    cue.expanded=motionAt-cueChangedAt<1800||cue.busy||!cue.available||cue.motionUnavailable;
    interactionOutput.cue=cue;
    panelinput::WorldInput(motionInput.active&&motionInput.tracked[0]&&motionInput.tracked[1]&&motionInput.aimTracked);
    if(motionInput.quickCast&&(motionOutput.block||!motionOutput.rayValid))quickCast.Suspend();
    const uint32_t hidden=interactionOutput.stowed?3u:journalMode?2u:interactionOutput.bottleHand>=0?1u<<interactionOutput.bottleHand:0;
    skel::SetInteractionHidden(hidden);
    potionScene=potions::MakeScene(interactionInput,interactionOutput);
    if(interactionInput.active) {
        const bool snapshotFresh=interaction::Fresh(interactionInput.snapshot,interactionInput.session,interactionInput.at);
        if(snapshotFresh&&(interactionPrefetchSession!=interactionInput.session||interactionPrefetchRevision!=interactionInput.snapshot.revision)) {
            interaction::icons::Prefetch(interactionInput.snapshot);
            interactionPrefetchSession=interactionInput.session;interactionPrefetchRevision=interactionInput.snapshot.revision;
        }
        const uint64_t key=(interaction::icons::Revision()*0x9e3779b97f4a7c15ull)^(uint64_t(interactionOutput.editor)<<60)^(uint64_t(interactionOutput.editSlot+1)<<56)^(uint64_t(interactionOutput.editPage)<<48)^(uint64_t(interactionInput.snapshot.revision)<<32)^(uint64_t(interactionOutput.utility)<<24)^(
            uint64_t(interactionOutput.hover+1)<<16)^(uint64_t(motion.Selection()+1)<<8)^(uint64_t(interactionOutput.result)<<4)^uint64_t(interactionOutput.stowed)^(uint64_t(interactionOutput.map)<<1)^(uint64_t(interactionOutput.nearHolster+1)<<26)^(uint64_t(interactionOutput.bottle+1)<<28)^(uint64_t(interactionOutput.bottleEmpty)<<2)^(uint64_t(snapshotFresh)<<3);
        const auto now=GetTickCount64();
        const bool visible=motionInput.active||interactionOutput.wheel||interactionOutput.wrist||interactionOutput.critical||interactionOutput.bottle>=0||
            interactionOutput.nearBelt>=0||interactionOutput.nearHolster>=0||math::Rotate(Q(head.orientation),{0,0,-1}).y<-.60f;
        const auto context=interaction::Context(interactionInput.snapshot,interactionOutput,snapshotFresh);
        if(visible&&(!interactionPaintValid||key!=interactionPaintKey||context!=interactionPaintContext)) {
            if(now>=interactionPaintAt&&(!interactionPaintRequest||key!=interactionRequestedKey||context!=interactionRequestedContext)) {
                interactionPaintRequest=interactionPainter.Submit({snapshotFresh?interactionInput.snapshot:interaction::Snapshot{},interactionOutput,motion.Selection()});
                interactionRequestedKey=key;interactionRequestedContext=context;interactionPaintAt=now+100;
            }
            interaction::PaintedAtlas painted;
            if(key==interactionRequestedKey&&context==interactionRequestedContext&&atlasUploads.size()<3&&!interactionAtlasPending&&
               interactionPainter.Take(interactionPaintRequest,painted)) {
                const auto uploadStart=Milliseconds();
                if(!painted.pixels.empty()&&UploadAtlas(interactionChain,atlasFormat,1024,1024,painted.pixels,true)) {
                    interactionPaintKey=key;interactionPaintContext=context;interactionPaintValid=true;
                }
                interactionPaintRequest=0;
                const auto uploadMs=Milliseconds()-uploadStart;
                static ULONGLONG reportAt=0;
                if((profile::Active()||uploadMs>4)&&now>=reportAt) {
                    char message[180];std::snprintf(message,sizeof message,"Frame UI refresh: background paint %.2f ms, frame-thread upload %.2f ms",painted.paintMs,uploadMs);Say(message);reportAt=now+1000;
                }
            }
        }
    }
    {
        melee::Sample sample;
        sample.time=motionInput.time;sample.hand=motionInput.hand[0];sample.epoch=meleeEpoch;
        sample.equipment=motionInput.equipment;sample.active=motionInput.active&&motionOutput.meleeContact;
        meleePoses.Push(s.sampleStamp,sample);
        sample.hand=motionInput.hand[1];sample.shield=true;sample.active=motionInput.active&&motionOutput.shieldContact;
        sample.skillSlot=motion.Selection();sample.skillOwner=motionInput.selectedOwner;sample.skillContext=motionInput.skillContext;
        shieldPoses.Push(s.sampleStamp,sample);
    }
    skel::SetMotionBow(motionInput.active&&motionOutput.bowReady,motionOutput.bowDrawing,motionOutput.draw,s.rightHand,s.leftHand,s.sampleStamp);
    if(!motionInput.active||(!motionOutput.bowReady&&!motionOutput.javelinReady))bowNeedsWorld=bowHasShot=false;
    if(motionOutput.bowRelease||motionOutput.javelinRelease) {
        bowPending={};bowPending.origin=motionInput.hand[0].p;bowPending.direction=motionOutput.rayDirection;
        if(motionOutput.javelinRelease) {
            bowPending.kind=bowflight::Kind::Javelin;
            bowPending.origin=bowPending.origin-bowPending.direction*bowflight::JavelinGrip;
        }
        bowPending.draw=motionOutput.draw;bowPending.at=GetTickCount64();bowPending.stamp=s.sampleStamp;
        bowPending.session=gamestate::LocalPlayer();bowPending.equipment=motionInput.equipment;
        bowPending.epoch=(meleeEpoch<<32)|bowWorldEpoch;bowPending.sequence=++bowSequence;
        bowNeedsWorld=true;
    }
    MotionFeedback(motionOutput);
    if(const auto at=meleeImpactAt.exchange(0);at&&motionInput.active&&GetTickCount64()-at<150)
        MotionPulse(0,.72f,45000000);
    if(const auto at=shieldImpactAt.exchange(0);at&&motionInput.active&&GetTickCount64()-at<150)
        MotionPulse(1,.72f,45000000);
    if(motionOutput.potion>=0)gamecmd::RequestPotion(motionOutput.potion);
    if(!motionInput.active)gamecmd::CancelPotion();
    if(motionOutput.feedback) {
        char message[144];std::snprintf(message,sizeof message,"Frame gameplay: event=%u selection=%d potion request=%d swap=%d bow=%d",
            motionOutput.feedback,motion.Selection(),motionOutput.potion,motionOutput.swap,motionOutput.bowDrawing);Say(message);
    }
    // Rate-limited readiness diagnostics: failed grabs were previously silent.
    // No room poses are recorded. Log only gates, action counts and grab range.
    if(motionControls.load()&&inWorld&&inWorld()) {
        static ULONGLONG reportAt=0;
        if(GetTickCount64()>=reportAt) {
            char message[600];std::snprintf(message,sizeof message,
                "Frame gameplay ready: active=%d head=%d hands=%d%d aim=%d panel=%d atlas=%d selection=%d weapon=%d trigger=%d blocked=%d bow ready=%d reach=%d drawing=%d charged=%d native=%d anchored=%d draw=%.2f melee contact=%d grip=%d skill=%d motion=%d shield equipped=%d hook=%d contact=%d",
                motionInput.active,motionHeadTracked,motionInput.tracked[0],motionInput.tracked[1],motionInput.aimTracked,panelMode,
                gameplayChain!=XR_NULL_HANDLE,motion.Selection(),int(motionInput.weapon),motionInput.trigger>.55f,motionOutput.block,
                motionOutput.bowReady,motionOutput.bowInReach,motionOutput.bowDrawing,motionOutput.bowCharged,skel::MotionBowPosed(),skel::MotionBowAnchored(),motionOutput.draw,
                motionOutput.meleeContact,motionInput.grip[0]>.6f,motionInput.selectedSkill,motionInput.selectedAvailable,
                gamestate::LeftShield(),::melee::ShieldReady(),motionOutput.shieldContact);Say(message);
            reportAt=GetTickCount64()+5000;
        }
    }
    ++s.counter;
    auto& p=*pad.data;p.writer_pid=GetCurrentProcessId();p.buttons=0;
    // Right A goes only through semantic interact; a raw A bypasses its
    // no-target guard and accidentally attacks when pointing at empty space.
    p.buttons=menus::WorldButtons(controls);
    if(heightResetButton.MapPressed())p.buttons|=XINPUT_GAMEPAD_LEFT_THUMB;
    p.thumb_lx=controls.lx;p.thumb_ly=controls.ly;p.thumb_rx=controls.rx;p.thumb_ry=controls.ry;
    p.left_trigger=controls.lt;
    const float rightTrigger=controls.rt/255.f;
    const bool attackAllowed=menuTriggerGate.Update(rightTrigger>.1f,panelMode||spatialMode||settingsOwned||settingsChord||inventoryPressed||menuPressed);
    p.right_trigger=attackAllowed?uint8_t(std::clamp(rightTrigger,0.f,1.f)*255):0;
    auto& a=*actions.data;a.held=0;
    if(p.right_trigger>128)a.held|=D2RVR_ACT_ATTACK;
    if((controls.buttons&menus::A)&&!quickCast.Suppress(0))a.held|=D2RVR_ACT_INTERACT;
    if(heightResetButton.MapPressed())a.held|=D2RVR_ACT_MAP;
    // Physical right-stick click belongs to native menus (hold-to-sort).
    // Gameplay swaps are requested by the shoulder gesture or wheel only.
    if(motionControls.load()&&gameplayChain) {
        p.buttons=0;p.left_trigger=p.right_trigger=0;
        a.held&=~D2RVR_ACT_ATTACK;
        if(motionOutput.attack)a.held|=D2RVR_ACT_ATTACK;
        if(motionOutput.swap)a.held|=D2RVR_ACT_SWAP;
        if(motionOutput.skill>=0) {
            p.buttons|=gameplay::SkillButtons(motionOutput.skill);
            p.left_trigger=gameplay::SkillModifier(motionOutput.skill)?255:0;
        }
        if(motionOutput.block||!motionInput.active||!motionOutput.rayValid) {
            a.held=motionOutput.swap?D2RVR_ACT_SWAP:0;p.buttons=0;p.left_trigger=0;
        }
        if(interactionOutput.wheel) {p.thumb_lx=p.thumb_ly=p.thumb_rx=p.thumb_ry=0;}
    }
    if(panelMode||spatialMode||settingsOwned||settingsChord) {
        // Native UI receives a separate report; a laser click has no RT/A
        // companion, and semantic gameplay actions never enter either menu.
        const auto ui=(settingsOwned||settingsChord)?menus::Pad{}:nativeMenuPad;
        p.buttons=ui.buttons;p.left_trigger=ui.lt;p.right_trigger=ui.rt;
        p.thumb_lx=ui.lx;p.thumb_ly=ui.ly;p.thumb_rx=ui.rx;p.thumb_ry=ui.ry;a.held=0;
    }
    p.reserved[0]=uint32_t(settingsOwned||settingsChord?menus::Mode::Blocked:nativeUi?menus::Mode::Native:menus::Mode::World);
    p.reserved[1]=inputEpoch;
    ++p.counter;++a.counter;
}
void Events() {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while(pPollEvent(instance,&event)==XR_SUCCESS) {
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto& s=*reinterpret_cast<XrEventDataSessionStateChanged*>(&event);state=s.state;
            char b[96];std::snprintf(b,sizeof b,"Frame OpenXR: session state %d",int(state));Say(b);
            if(state==XR_SESSION_STATE_READY) { XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;running=Check(pBeginSession(session,&bi),"begin session"); }
            if(state==XR_SESSION_STATE_STOPPING) { if(frame)EndPair();pEndSession(session);running=false;ClearInput(); }
            if(state==XR_SESSION_STATE_LOSS_PENDING||state==XR_SESSION_STATE_EXITING) {running=false;valid=false;ClearInput();}
        } else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {running=false;valid=false;ClearInput();}
        event={XR_TYPE_EVENT_DATA_BUFFER};
    }
}
bool Begin() {
    if(!session||frame) return frame;
    Events();if(!running)return false;
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};frameState={XR_TYPE_FRAME_STATE};
    timing={};const double waitStart=Milliseconds();
    const auto waited=pWaitFrame(session,&wi,&frameState);timing.waitFrameMs=Milliseconds()-waitStart;
    if(!Check(waited,"wait frame"))return false;
    timing.displayPeriodMs=double(frameState.predictedDisplayPeriod)*1e-6;
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    if(!Check(pBeginFrame(session,&bi),"begin frame"))return false;
    frame=true;render=frameState.shouldRender;valid=false;motionHeadTracked=false;
    bowRenderTime=GetTickCount64();
    const bool loaded=inWorld&&inWorld();
    if(loaded&&!worldWasLoaded)worldCalibrationPending=true;
    if(!loaded)worldCalibrationPending=worldRecenter=false;
    worldWasLoaded=loaded;
    const bool spatial=SpatialMenu();
    const bool book=spatial&&gamestate::JournalOnly();
    if(spatial!=spatialMode||book!=journalMode) {
        workspacePlaced=false;uiMissingFrames=0;std::fill_n(uiCrop,4,0);
        if(book)journalPlacement.Open();
        // An attack held when an NPC opens its dialog must not select an option.
        ReleasePanelInput();panelMouse.Suspend();
        Say(spatial?(book?"Frame panels: handheld quest log":"Frame panels: native menus in stereo world"):"Frame panels: native surface closed");
    }
    const bool opaque=spatial&&gamestate::OpaqueMenuOpen();
    if(opaque!=opaqueMenuMode)Say(opaque?"Frame panels: shared black menu background":"Frame panels: spatial background restored");
    spatialMode=spatial;journalMode=book;opaqueMenuMode=opaque;uiEye.copied=false;
    mapEye.copied=false;hud::MapFrame();
    hud::UiFrame(spatialMode,!journalMode,0);
    const bool panel=wantsPanel&&wantsPanel()&&!spatialMode;
    if(panel!=panelMode){ReleasePanelInput();panelMouse.Suspend();panelMode=panel;menuPlaced=false;Say(panel?"Frame OpenXR: flat menu panel active":"Frame OpenXR: stereo gameplay active");}
    for(auto& e:eyes)e.copied=false;
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;li.displayTime=frameState.predictedDisplayTime;li.space=space;
    XrViewState vs{XR_TYPE_VIEW_STATE};uint32_t count=0;
    const double locateStart=Milliseconds();
    const auto flags=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    XrSpaceLocation hs{XR_TYPE_SPACE_LOCATION};
    if(XR_SUCCEEDED(pLocateViews(session,&li,&vs,2,&count,views.data()))&&count==2&&(vs.viewStateFlags&flags)==flags&&
       XR_SUCCEEDED(pLocateSpace(headSpace,space,li.displayTime,&hs))&&
       (hs.locationFlags&(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))==(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        head=hs.pose;valid=true;
        const auto trackedHead=XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        motionHeadTracked=(hs.locationFlags&trackedHead)==trackedHead;
        // Latch once per pair: camera matrices, foveation and xrEndFrame must
        // all describe the same frustum, including a live settings change.
        // LocateViews refreshes the original runtime FOVs on every frame.
        const bool centered=symmetricProjection.load();
        if(centered) {
            const auto& l=views[0].fov;const auto& r=views[1].fov;
            const auto f=math::EnclosingSymmetricFov(
                {l.angleLeft,l.angleRight,l.angleDown,l.angleUp},
                {r.angleLeft,r.angleRight,r.angleDown,r.angleUp});
            for(auto& view:views)view.fov={f.left,f.right,f.up,f.down};
        }
        static int lastProjectionMode=-1;
        if(lastProjectionMode!=int(centered)) {
            const auto& f=views[0].fov;char b[192];
            std::snprintf(b,sizeof b,"Frame OpenXR: projection %s; left FOV %.5f %.5f %.5f %.5f",
                centered?"symmetric lighting trial":"runtime recommended",f.angleLeft,f.angleRight,f.angleUp,f.angleDown);
            Say(b);lastProjectionMode=int(centered);
        }
        if(!haveOrigin){origin=V(head.position);haveOrigin=true;}
        if(worldCalibrationPending&&state==XR_SESSION_STATE_FOCUSED) {
            origin=V(head.position);worldCalibrationPending=false;worldRecenter=true;
            Say("Frame OpenXR: loaded game calibrated from current headset position");
        }
        if(spatialMode&&!journalMode&&!workspacePlaced) {
            const auto yaw=math::Yaw(math::Heading(Q(head.orientation)));
            const auto pos=V(head.position)+math::Rotate(yaw,{0,-.10f,-panels::Distance});
            uiPose.position={pos.x,pos.y,pos.z};uiPose.orientation={yaw.x,yaw.y,yaw.z,yaw.w};
            uiSize=panels::Width;workspacePlaced=true;
        }
        if(panelMode&&!menuPlaced) {
            const auto yaw=math::Yaw(math::Heading(Q(head.orientation)));
            const auto pos=V(head.position)+math::Rotate(yaw,{0,0,-panelDistance});
            menuPose.position={pos.x,pos.y,pos.z};menuPose.orientation={yaw.x,yaw.y,yaw.z,yaw.w};menuPlaced=true;
        }
    }
    timing.locateMs=Milliseconds()-locateStart;
    const double inputStart=Milliseconds();Input();timing.inputMs=Milliseconds()-inputStart;return true;
}
bool Acquire(Eye& e) {
    if(e.acquired)return true;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(!Check(pAcquireSwapchainImage(e.chain,&ai,&e.index),"acquire eye"))return false;
    e.acquired=true;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;
    return Check(pWaitSwapchainImage(e.chain,&wi),"wait eye");
}
bool WaitGpu() {
    return WaitGpuValue(fenceValue);
}
bool WaitGpuValue(uint64_t value) {
    if(!value||!fence||fence->GetCompletedValue()>=value)return true;
    if(!Hr(fence->SetEventOnCompletion(value,fenceEvent),"fence completion"))return false;
    // Bounded: a device loss must not hang the game's render thread forever.
    const double start=Milliseconds();const auto result=WaitForSingleObject(fenceEvent,5000);
    timing.waitGpuMs+=Milliseconds()-start;
    if(result!=WAIT_OBJECT_0) {Say("Frame D3D12: GPU copy fence timed out");return false;}
    return true;
}
bool Copy(ID3D12Resource* src,Eye& e,reshade::api::command_queue* rq) {
    // The runtime orders images on our bound D3D12 queue. Our allocator has a
    // separate lifetime: never reset it until its own previous copy completed.
    // Waiting here allows game/CPU work to overlap the preceding GPU frame.
    if(e.copied)return true;
    const auto eye=unsigned(&e-eyes.data());const double acquireStart=Milliseconds();
    if(!Acquire(e))return false;
    timing.acquireMs[eye]+=Milliseconds()-acquireStart;
    if(!WaitGpuValue(e.copyFence))return false;
    const double copyStart=Milliseconds();
    const bool gpu=profile::Active()&&(gpuProfile.effects&(1u<<eye))&&!gpuProfile.failed;
    if(gpu)reinterpret_cast<ID3D12GraphicsCommandList*>(rq->get_immediate_command_list()->get_native())->EndQuery(gpuProfile.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,eye*5+2);
    rq->flush_immediate_command_list(); // finish all ReShade writes before using the native queue.
    if(!Hr(e.allocator->Reset(),"reset allocator")||!Hr(e.commands->Reset(e.allocator.Get(),nullptr),"reset copy list"))return false;
    ID3D12Resource* dest=e.images[e.index].texture;
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for(auto& b:barriers){b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
    barriers[0].Transition.pResource=src;barriers[0].Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barriers[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.pResource=dest;barriers[1].Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;barriers[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
    if(gpu)e.commands->EndQuery(gpuProfile.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,eye*5+3);
    e.commands->ResourceBarrier(2,barriers);e.commands->CopyResource(dest,src);
    for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
    e.commands->ResourceBarrier(2,barriers);
    if(!panelMode&&interactionInput.active&&potionModels.Ready()) {
        const auto& v=views[eye];
        potionModels.Render(e.commands.Get(),dest,eye,{V(v.pose.position),Q(v.pose.orientation)},
            {v.fov.angleLeft,v.fov.angleRight,v.fov.angleDown,v.fov.angleUp},potionScene);
    }
    if(gpu){
        e.commands->EndQuery(gpuProfile.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,eye*5+4);
        e.commands->ResolveQueryData(gpuProfile.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,eye*5,5,gpuProfile.readback.Get(),eye*5*sizeof(uint64_t));
        gpuProfile.resolved|=1u<<eye;
    }
    if(!Hr(e.commands->Close(),"close copy list"))return false;
    ID3D12CommandList* lists[]={e.commands.Get()};queue->ExecuteCommandLists(1,lists);
    if(!Hr(queue->Signal(fence.Get(),++fenceValue),"signal copy fence"))return false;
    e.copyFence=fenceValue;
    timing.copySubmitMs[eye]+=Milliseconds()-copyStart;
    e.copied=true;return true;
}
bool CopyMap(reshade::api::command_queue* rq) {
    if(!mapSupported||mapEye.copied||!interaction::MapWanted(interactionInput,interactionOutput,GetTickCount64()))return true;
    uint32_t w=0,h=0;
    auto* src=reinterpret_cast<ID3D12Resource*>(hud::MapTexture(w,h));
    if(!src||!w||!h)return true; // No current map; never display the last area's texture.
    auto& e=mapEye;
    if(!e.chain) {
        XrSwapchainCreateInfo c{XR_TYPE_SWAPCHAIN_CREATE_INFO};c.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        c.format=DXGI_FORMAT_R16G16B16A16_FLOAT;c.width=c.height=mapSide;c.sampleCount=c.faceCount=c.arraySize=c.mipCount=1;
        if(XR_FAILED(pCreateSwapchain(session,&c,&e.chain)))return false;
        uint32_t n=0;
        if(XR_FAILED(pEnumerateSwapchainImages(e.chain,0,&n,nullptr))||!n)return false;
        e.images.resize(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        if(XR_FAILED(pEnumerateSwapchainImages(e.chain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data()))))return false;
        if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&e.allocator)))||
           FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,e.allocator.Get(),nullptr,IID_PPV_ARGS(&e.commands)))||FAILED(e.commands->Close()))return false;
        Say("Frame menu map: 512x512 GPU surface ready; native explored map shared by both eyes");
    }
    if(!Acquire(e)||!WaitGpuValue(e.copyFence))return false;
    rq->flush_immediate_command_list();
    if(FAILED(e.allocator->Reset())||FAILED(e.commands->Reset(e.allocator.Get(),nullptr)))return false;
    if(!mapBlit.Render(device.Get(),e.commands.Get(),src,e.images[e.index].texture,true,true)) {
        e.commands->Close();return false;
    }
    if(FAILED(e.commands->Close()))return false;
    ID3D12CommandList* lists[]={e.commands.Get()};queue->ExecuteCommandLists(1,lists);
    if(FAILED(queue->Signal(fence.Get(),++fenceValue)))return false;
    e.copyFence=fenceValue;e.copied=true;return true;
}
bool CopyUi(reshade::api::command_queue* rq) {
    if(!spatialMode||uiEye.copied)return true;
    uint32_t w=0,h=0;int32_t crop[4]{};
    auto* src=reinterpret_cast<ID3D12Resource*>(hud::UiTexture(w,h,crop));
    if(!src||!w||!h){
        if(++uiMissingFrames>12){uiSupported=false;Say("Frame panels: UI capture unavailable; falling back to native menu");}
        return true;
    }
    uiMissingFrames=0;
    auto& e=uiEye;
    if(e.chain&&(w!=uiWidth||h!=uiHeight)) {
        if(!WaitGpuValue(e.copyFence))return false;
        pDestroySwapchain(e.chain);e={};
    }
    if(!e.chain) {
        XrSwapchainCreateInfo c{XR_TYPE_SWAPCHAIN_CREATE_INFO};c.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        c.format=DXGI_FORMAT_R16G16B16A16_FLOAT;c.width=w;c.height=h;c.sampleCount=c.faceCount=c.arraySize=c.mipCount=1;
        if(XR_FAILED(pCreateSwapchain(session,&c,&e.chain))) {uiSupported=false;Say("Frame panels: UI format unavailable; native menu retained");return true;}
        uint32_t n=0;pEnumerateSwapchainImages(e.chain,0,&n,nullptr);e.images.resize(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        if(XR_FAILED(pEnumerateSwapchainImages(e.chain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data()))))return false;
        if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&e.allocator)))||
           FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,e.allocator.Get(),nullptr,IID_PPV_ARGS(&e.commands)))||FAILED(e.commands->Close()))return false;
        uiWidth=w;uiHeight=h;
        char message[160];std::snprintf(message,sizeof message,"Frame panels: native UI surface %ux%u ready; one shared image for both eyes",w,h);Say(message);
    }
    if(!Acquire(e)||!WaitGpuValue(e.copyFence))return false;
    rq->flush_immediate_command_list();
    if(FAILED(e.allocator->Reset())||FAILED(e.commands->Reset(e.allocator.Get(),nullptr)))return false;
    ID3D12Resource* dest=e.images[e.index].texture;
    if(!uiBlit.Render(device.Get(),e.commands.Get(),src,dest,opaqueMenuMode)) {
        e.commands->Close();uiSupported=false;
        Say("Frame panels: conversion rejected; native flat menu fallback next frame");return true;
    }
    if(FAILED(e.commands->Close()))return false;
    ID3D12CommandList* lists[]={e.commands.Get()};queue->ExecuteCommandLists(1,lists);
    if(FAILED(queue->Signal(fence.Get(),++fenceValue)))return false;
    e.copyFence=fenceValue;e.copied=true;std::copy_n(crop,4,uiCrop);return true;
}
void Capture(reshade::api::command_queue* rq,reshade::api::swapchain* swap) {
    std::lock_guard lock(mutex);
    if(failed)return;
    if(!session)return; // Prepare owns initialization and its bounded retry interval.
    if(swap!=gameSwapchain)return;
    auto* src=reinterpret_cast<ID3D12Resource*>(swap->get_current_back_buffer().handle);
    const auto desc=src->GetDesc();
    if(desc.Width!=width||desc.Height!=height||desc.Format!=format) {
        Say("Frame OpenXR: window resized; recreating eye swapchains");Shutdown();failed=false;return;
    }
    if(!afr) {
        afrHandle=OpenFileMappingW(FILE_MAP_READ,FALSE,FLATVR_AFR_EYE_NAME);
        if(afrHandle)afr=static_cast<FlatVRAfrEye*>(MapViewOfFile(afrHandle,FILE_MAP_READ,0,0,sizeof(FlatVRAfrEye)));
    }
    const bool stereo=afr&&afr->version==FLATVR_AFR_EYE_VERSION&&afr->enabled&&(!(wantsPanel&&wantsPanel())||SpatialMenu());
    if(!Begin()||!render||!valid) {if(!stereo)EndPair();return;}
    const uint32_t eye=stereo?((afr->eye^afr->swap)&1u):0u;
    if(eye==0&&!CopyMap(rq)) {mapSupported=false;mapEye.copied=false;Say("Frame menu map: copy unavailable; optional map suspended");}
    if((eye==0&&!CopyUi(rq))||!Copy(src,eyes[eye],rq)){failed=true;EndPair();ClearInput();return;}
    if(!stereo) {
        if(!menuPlaced) {
            const auto pos=V(head.position)+math::Rotate(Q(head.orientation),{0,0,-1.6f});
            menuPose=head;menuPose.position={pos.x,pos.y,pos.z};menuPlaced=true;
        }
        EndPair();
    } else if(!pairDrawing&&eyes[0].copied&&eyes[1].copied) {
        EndPair();
    } else if(eye==1&&!eyes[0].copied) {
        // Missed left eye: submit zero layers, never present an old/mono image as stereo.
        EndPair();
    }
}
void Prepare(reshade::api::command_queue* rq,reshade::api::swapchain* swap,const reshade::api::rect*,const reshade::api::rect*,uint32_t,const reshade::api::rect*) {
    static bool once=false;if(!once){once=true;Say("Frame OpenXR: first game Present callback");}
    std::lock_guard lock(mutex);
    if(!failed&&!session&&GetTickCount64()>=retryAt&&!Initialize(rq,swap)){
        Shutdown();retryAt=GetTickCount64()+5000;
        Say("Frame OpenXR: initialization deferred; retrying in 5 seconds");
    }
}
void AfterEffects(reshade::api::effect_runtime* rt) {
    // ReShade's 'present' event is BEFORE its effects. Capture at its later
    // 'reshade_present' event so the sky/fog/HUD edits are included.
    static bool once=false;if(!once){once=true;Say("Frame OpenXR: first effects callback");}
    std::lock_guard lock(mutex);
    if(replayCopiesFinished)return;
    gameWindow=static_cast<HWND>(rt->get_hwnd());
    if(gameSwapchain&&rt->get_current_back_buffer().handle==gameSwapchain->get_current_back_buffer().handle) {
        if(capturePair&&pairDrawing&&afr&&afr->enabled&&!panelMode) {
            const unsigned eye=(afr->eye^afr->swap)&1u;
            if(!(captureEyes&(1u<<eye))) {
                char name[80];std::snprintf(name,sizeof name,"Frame-pair-%u-%s",captureSerial,eye?"right":"left");
                rt->save_screenshot(name);captureEyes|=1u<<eye;
                char line[240];const auto& v=views[eye];
                std::snprintf(line,sizeof line,"Frame capture: %s; pose %.4f %.4f %.4f; FOV %.5f %.5f %.5f %.5f",name,v.pose.position.x,v.pose.position.y,v.pose.position.z,v.fov.angleLeft,v.fov.angleRight,v.fov.angleUp,v.fov.angleDown);Say(line);
            }
        }
        Capture(rt->get_command_queue(),gameSwapchain);
    }
}
void DestroySwap(reshade::api::swapchain* swap,bool) {
    std::lock_guard lock(mutex);if(swap==gameSwapchain){Shutdown();failed=false;}
}
} // namespace

bool HandleWindowFocusMessage(void* window,uint32_t message,uint64_t token,int64_t deadline) {
    return windowFocus.Handle(static_cast<HWND>(window),message,WPARAM(token),LPARAM(deadline));
}

void Register(LogFn log,PanelFn panel,PanelFn world,PanelFn toggle) {
    std::lock_guard lock(mutex);logger=log;wantsPanel=panel;inWorld=world;toggleMenus=toggle;if(registered)return;
    reshade::register_event<reshade::addon_event::present>(&Prepare);
    reshade::register_event<reshade::addon_event::reshade_present>(&AfterEffects);
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(&ProfileEffects);
    reshade::register_event<reshade::addon_event::destroy_swapchain>(&DestroySwap);
    reshade::register_event<reshade::addon_event::create_pipeline>(&PipelineCreating);
    reshade::register_event<reshade::addon_event::init_pipeline>(&PipelineCreated);
    foveation::Register(log);
    registered=true;Say("Frame OpenXR: registered D3D12 capture; awaiting game Present");
}
bool SpatialMenu(){return uiSupported.load()&&uiworld::Ready()&&hud::LayerFound()&&inWorld&&inWorld()&&wantsPanel&&wantsPanel();}
bool Active() {std::lock_guard lock(mutex);return frame&&valid&&render&&!failed&&!panelMode;}
bool CaptureReplay(reshade::api::command_queue* rq,reshade::api::swapchain* swap,unsigned eye) {
    std::lock_guard lock(mutex);
    if(eye>1||!Active()||swap!=gameSwapchain||!pairDrawing)return false;
    Capture(rq,swap);return !failed&&eyes[eye].copied;
}
void FinishReplayCapture(){std::lock_guard lock(mutex);replayCopiesFinished=eyes[0].copied&&eyes[1].copied;}
void ConfigureDeferredWait(bool on) {
    if(deferredWait.exchange(on)!=on)Say(on?"Frame OpenXR: GPU wait deferred until copy allocator reuse":"Frame OpenXR: conservative GPU wait before image release");
}
void ConfigureSymmetricProjection(bool on) {symmetricProjection.store(on);}
void ConfigureEyeTracking(bool on) {eyeTracking.store(on);}
void BeginEye(int eye) {
    hud::UiFrame(spatialMode,!journalMode,eye);
    std::lock_guard lock(mutex);
    d2rcam::SetStereoUiEye(Active()?(eye&1):-1);
    d2rcam::TraceStereoLabels(capturePair?eye:-1);
    if(!Active()){foveation::EndWorld();return;}
    profile::BeginEye(eye&1);
    StartGpuEye(eye&1);
    const auto& f=views[eye&1].fov;
    const float l=std::tan(f.angleLeft),r=std::tan(f.angleRight),d=std::tan(f.angleDown),u=std::tan(f.angleUp);
    if(eyeTracking.load()) {
        if(gazeValid)foveation::BeginEye(eye&1,gazeCenters[eye&1].x,gazeCenters[eye&1].y,true);
        else foveation::EndWorld();
    } else foveation::BeginEye(eye&1,-l/(r-l),u/(u-d));
}
void ResetHeight() {
    std::lock_guard lock(mutex);
    ++meleeEpoch;motion.Cancel();motionOutput={};gamecmd::CancelPotion();
    meleePoses.Clear();CancelShieldCast();
    bowNeedsWorld=bowHasShot=false;
    skel::SetMotionBow(false,false,0);
    if(!valid||!haveOrigin)return;
    origin=math::ResetHeight(origin,V(head.position));
    Say("Frame OpenXR: eye height reset to current headset position");
}
void Recenter() {
    std::lock_guard lock(mutex);
    ++meleeEpoch;motion.Cancel();motionOutput={};gamecmd::CancelPotion();
    meleePoses.Clear();CancelShieldCast();
    bowNeedsWorld=bowHasShot=false;
    skel::SetMotionBow(false,false,0);
    if(!valid)return;
    origin=V(head.position);haveOrigin=true;
    if(panelMode)menuPlaced=false;
    Say("Frame OpenXR: headset position recentered");
}
bool TakeWorldRecenter() {
    std::lock_guard lock(mutex);const bool take=worldRecenter;worldRecenter=false;return take;
}
void RequestStereoCapture(){captureRequested.store(true);}
void BeginPair() {
    std::lock_guard lock(mutex);
    replayCopiesFinished=false;
    lastTiming={};
    if(session&&!failed){
        if(frame)EndPair();Begin();pairDrawing=true;
        gpuProfile.started=gpuProfile.effects=gpuProfile.resolved=0;
        if(Active())profile::BeginPair();else profile::SkipPair();
        if(Active()&&(captureRequested.exchange(false)||d2rcam::TakeLabelCapture())) {
            capturePair=true;captureEyes=0;++captureSerial;
            Say("Frame capture: recording one stereo pair (readback pause is diagnostic, exclude from performance comparison)");
        }
    } else profile::SkipPair();
}
void EndPair() {
    foveation::EndWorld();
    d2rcam::SetStereoUiEye(-1);
    std::lock_guard lock(mutex);pairDrawing=false;if(!frame||!session)return;
    d2rcam::TraceStereoLabels(-1);
    if(capturePair){Say(captureEyes==3?"Frame capture: both eye screenshots requested":"Frame capture: incomplete pair; do not use for stereo comparison");capturePair=false;}
    // All image writes and state restoration have been submitted on the queue
    // bound to xrCreateSession. OpenXR orders its work after those commands;
    // it does not require a CPU completion wait before xrReleaseSwapchainImage.
    // WineOpenXR also drains that queue before its release/layout commands.
    // Keep the old path for A/B, and for synchronous diagnostic query readback.
    const bool waitNow=!deferredWait.load()||gpuProfile.started!=0;
    if(waitNow&&fence&&!WaitGpu()){failed=true;for(auto& e:eyes)e.copied=false;}
    if(!failed)ReadGpuProfile();
    gpuProfile.started=gpuProfile.effects=gpuProfile.resolved=0;
    const double releaseStart=Milliseconds();
    for(auto& e:eyes)if(e.acquired){
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if(!Check(pReleaseSwapchainImage(e.chain,&ri),"release copied eye")){failed=true;e.copied=false;}
        e.acquired=false;
    }
    if(uiEye.acquired){XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        Check(pReleaseSwapchainImage(uiEye.chain,&release),"release journal");uiEye.acquired=false;}
    if(mapEye.acquired){XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if(!Check(pReleaseSwapchainImage(mapEye.chain,&release),"release menu map"))mapEye.copied=false;mapEye.acquired=false;}
    timing.releaseMs=Milliseconds()-releaseStart;
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::array<XrCompositionLayerProjectionView,2> pv{{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const XrCompositionLayerBaseHeader* layer=nullptr;
    if(render&&valid&&!panelMode&&eyes[0].copied&&eyes[1].copied) {
        for(int i=0;i<2;++i){pv[i].pose=views[i].pose;pv[i].fov=views[i].fov;pv[i].subImage.swapchain=eyes[i].chain;pv[i].subImage.imageRect={{0,0},{int32_t(width),int32_t(height)}};}
        projection.space=space;projection.viewCount=2;projection.views=pv.data();layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);menuPlaced=false;
    } else if(render&&valid&&eyes[0].copied&&menuPlaced) {
        quad.space=space;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.pose=menuPose;quad.size={panelWidth,panelWidth*height/width};
        quad.subImage.swapchain=eyes[0].chain;quad.subImage.imageRect={{0,0},{int32_t(width),int32_t(height)}};layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
    }
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};ei.displayTime=frameState.predictedDisplayTime;ei.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    std::array<const XrCompositionLayerBaseHeader*,32> layers{};uint32_t count=0;
    if(settingsAtlasPending){
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        settingsImageReady=Check(pReleaseSwapchainImage(settingsChain,&release),"release settings atlas");settingsAtlasPending=false;
    }
    if(interactionAtlasPending) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        Check(pReleaseSwapchainImage(interactionChain,&release),"release interaction atlas");interactionAtlasPending=false;
    }
    // Tabbed and trade menus use the same black background in both eyes.
    // Submit just their shared opaque quad: no different cached world per eye.
    const bool blackMenu=opaqueMenuMode&&workspacePlaced&&uiEye.copied&&uiCrop[2]>0&&!settingsModel.open&&!bodyCalibration.Active();
    if(layer&&!blackMenu)layers[count++]=layer;
    const uint32_t capacity=std::min(uint32_t(layers.size()),layerLimit);
    const bool padQuads=composition::NeedsQuadPadding(wineOpenXR,layer,pointerChain);
    const uint32_t contentLimit=padQuads&&capacity>1?capacity-1:capacity;
    XrCompositionLayerQuad palette{XR_TYPE_COMPOSITION_LAYER_QUAD},bottle{XR_TYPE_COMPOSITION_LAYER_QUAD},chosen{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<XrCompositionLayerQuad,2> bowString{{{XR_TYPE_COMPOSITION_LAYER_QUAD},{XR_TYPE_COMPOSITION_LAYER_QUAD}}};
    std::array<XrCompositionLayerQuad,2> arrow{{{XR_TYPE_COMPOSITION_LAYER_QUAD},{XR_TYPE_COMPOSITION_LAYER_QUAD}}};
    XrCompositionLayerQuad nock{XR_TYPE_COMPOSITION_LAYER_QUAD},bowHint{XR_TYPE_COMPOSITION_LAYER_QUAD},activeSkill{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const bool gameplayVisible=layer&&!panelMode&&motionControls.load()&&motionInput.active&&gameplayChain&&motionOutput.rayValid;
    auto motionQuad=[&](XrCompositionLayerQuad& q,XrPosef pose,float w,float h,XrRect2Di rect,XrSwapchain chain) {
        if(count>=contentLimit||!gameplay::Valid(MotionPose(pose))||!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0)return;
        // Normalize generated billboards before submission; a bad optional
        // guide must not invalidate the headset's entire projection layer.
        const auto r=pose.orientation;const float len=std::sqrt(r.x*r.x+r.y*r.y+r.z*r.z+r.w*r.w);
        pose.orientation={r.x/len,r.y/len,r.z/len,r.w/len};
        q.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        q.space=space;q.eyeVisibility=XR_EYE_VISIBILITY_BOTH;q.pose=pose;q.size={w,h};
        q.subImage.swapchain=chain;q.subImage.imageRect=rect;
        layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&q);
    };
    const bool interactionVisible=layer&&!panelMode&&interactionInput.active&&interactionChain;
    if(interactionVisible&&interactionOutput.wheel&&interactionPaintValid&&interactionPaintContext==interaction::Context(
        interactionInput.snapshot,interactionOutput,interaction::Fresh(interactionInput.snapshot,interactionInput.session,interactionInput.at)))
        motionQuad(palette,palettePose,paletteWidth,paletteHeight,{{0,0},{768,768}},interactionChain);
    XrCompositionLayerQuad menuMap{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(palette.subImage.swapchain&&mapEye.copied&&interaction::MapWanted(interactionInput,interactionOutput,GetTickCount64())) {
        const auto bounds=interaction::MapContent;
        auto pose=MotionPose(palettePose);
        pose.p=pose.p+math::Rotate(pose.q,{((bounds.left+bounds.right)*.5f-.5f)*paletteWidth,
            (.5f-(bounds.top+bounds.bottom)*.5f)*paletteHeight,.002f});
        motionQuad(menuMap,XrPose(pose),(bounds.right-bounds.left)*paletteWidth,(bounds.bottom-bounds.top)*paletteHeight,
            {{0,0},{int32_t(mapSide),int32_t(mapSide)}},mapEye.chain);
    }
    auto interactionPose=[](interaction::Pose p){return XrPose({p.p,p.q});};
    std::array<XrCompositionLayerQuad,4> beltQuads;
    XrCompositionLayerQuad wrist{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(interactionVisible) {
        if(!potionModels.Ready()&&interactionOutput.bottle>=0&&interactionOutput.bottleHand>=0) {
            auto pose=motionInput.hand[interactionOutput.bottleHand];pose.p=pose.p+math::Rotate(pose.q,{0,.055f,0});
            motionQuad(bottle,XrPose(pose),.075f,.15f,{{768,512},{128,256}},interactionChain);
        }
        const bool lookingDown=math::Rotate(Q(head.orientation),{0,0,-1}).y<-.60f;
        if(lookingDown||interactionOutput.nearBelt>=0)for(int i=0;i<4;++i) {
            beltQuads[i]={XR_TYPE_COMPOSITION_LAYER_QUAD};auto pose=interactionOutput.belt[i];pose.q=Q(head.orientation);
            if(potionModels.Ready()) {
                // Only the small count/type label is a billboard. The actual
                // bottle stays torso-oriented, with proper per-eye geometry.
                pose.p.y-=.125f;
                motionQuad(beltQuads[i],interactionPose(pose),.08f,.0506f,{{i*128,943},{128,81}},interactionChain);
            } else motionQuad(beltQuads[i],interactionPose(pose),.065f,.13f,{{i*128,768},{128,256}},interactionChain);
        }
        if((interactionOutput.wrist||interactionOutput.critical)&&!interactionOutput.wheel) {
            const auto panel=interaction::WristPanel(interactionInput.hand[1],interactionInput.head);
            auto pose=motionInput.hand[1];pose.p=panel.p;pose.q=panel.q;
            if(interactionOutput.critical&&!interactionOutput.wrist){pose.p=V(head.position)+math::Rotate(Q(head.orientation),{-.20f,-.22f,-.55f});pose.q=Q(head.orientation);}
            motionQuad(wrist,XrPose(pose),.24f,.12f,{{512,768},{512,256}},interactionChain);
        }
    }
    if(gameplayVisible&&!interactionOutput.wheel&&!interactionOutput.stowed&&interactionOutput.bottle<0&&
       interactionPaintValid&&interactionPaintContext.cue==interactionOutput.cue&&interactionChain) {
        const auto& cue=interactionOutput.cue;
        const int side=motionInput.weapon==gameplay::Weapon::Bow||cue.id==weaponskills::Smite?1:0;
        const auto hand=motionInput.hand[side];
        const auto at=hand.p+math::Rotate(hand.q,{0,-.10f,.08f});
        const auto pose=XrPose({at,Q(head.orientation)});
        if(cue.expanded)motionQuad(activeSkill,pose,.24f,.18f,{{768,0},{256,192}},interactionChain);
        else motionQuad(activeSkill,pose,.075f,.075f,{{768,0},{96,96}},interactionChain);
    }
    if(gameplayVisible&&motionOutput.bowReady) {
        const auto bow=motionInput.hand[1];
        const auto center=motionOutput.bowDrawing?motionInput.hand[0].p:motionOutput.nockPoint;
        auto up=math::Rotate(bow.q,{0,0,-1});
        if(std::fabs(math::Dot(up,motionOutput.bowForward))>.8f)up=math::Rotate(bow.q,{0,1,0});
        up=math::Normalized(up-motionOutput.bowForward*math::Dot(up,motionOutput.bowForward));
        // Unrecognized rigs retain the draw guides. Once the actual model is
        // posed, remove these overlapping copies of its string and arrow.
        const bool nativeBow=skel::MotionBowPosed();
        for(int i=0;i<2&&!nativeBow;++i) {
            const auto end=bow.p+up*(i==0?.40f:-.40f),mid=(center+end)*.5f;
            motionQuad(bowString[i],XrPose({mid,math::BeamOrientation(end-center,V(head.position)-mid)}),
                .0015f,gameplay::Length(end-center),{{0,0},{64,32}},pointerChain);
        }
        if(!bowvr::PreviewReady()&&(motionOutput.bowDrawing||motionOutput.bowInReach)) {
            const auto direction=motionOutput.bowDrawing?motionOutput.rayDirection:motionOutput.bowForward;
            const auto mid=center+direction*(bowflight::ArrowLength*.5f);
            const auto orient=math::BeamOrientation(direction,V(head.position)-mid);
            motionQuad(arrow[0],XrPose({mid,orient}),.055f,bowflight::ArrowLength,{{512,768},{64,256}},gameplayChain);
            motionQuad(arrow[1],XrPose({mid,math::Multiply(orient,math::Yaw(1.57079633f))}),.055f,bowflight::ArrowLength,{{512,768},{64,256}},gameplayChain);
        }
        // Native bows have a visible string to grab. The extra idle marker
        // correlated with compositor pose errors; omit it on this path.
        if(!nativeBow&&!motionOutput.bowDrawing) {
            const float size=motionOutput.bowInReach?.045f:.025f;
            motionQuad(nock,XrPose({center,Q(head.orientation)}),size,size,{{0,32},{64,64}},pointerChain);
        }
        const int hint=motionOutput.bowDrawing?(motionOutput.bowCharged?2:1):0;
        motionQuad(bowHint,XrPose({bow.p+math::Vec{0,-.25f,0},Q(head.orientation)}),.24f,.045f,
            {{576,768+hint*85},{448,85}},gameplayChain);
    }
    XrCompositionLayerQuad book{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(layer&&!settingsModel.open&&!bodyCalibration.Active()&&spatialMode&&(journalMode?journalPlacement.Placed():workspacePlaced)&&uiEye.copied&&uiCrop[2]>0) {
        motionQuad(book,uiPose,uiSize,uiSize*float(uiCrop[3])/uiCrop[2],
            {{uiCrop[0],uiCrop[1]},{uiCrop[2],uiCrop[3]}},uiEye.chain);
        book.layerFlags=opaqueMenuMode?0:XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    }
    XrCompositionLayerQuad settingsPanel{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(layer&&(settingsModel.open||bodyCalibration.Active())&&settingsImageReady)
        motionQuad(settingsPanel,settingsPose,settingsWidth,settingsHeight,{{0,0},{settings::Width,settings::Height}},settingsChain);
    XrCompositionLayerQuad laser{XR_TYPE_COMPOSITION_LAYER_QUAD},dot{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(layer&&(settingsModel.open||panelMode||spatialMode||(interactionVisible&&interactionOutput.wheel))&&pointerChain&&pointerVisible) {
        // The pointer needs the same finite-pose checks and normalization as
        // every other optional layer: one bad laser must not hide the menu.
        motionQuad(laser,beamPose,.003f,beamLength,{{0,0},{64,32}},pointerChain);
        if(pointerHit)motionQuad(dot,dotPose,.018f,.018f,{{0,32},{64,64}},pointerChain);
    }
    XrCompositionLayerQuad winePadding{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(padQuads&&composition::PrependQuadPadding(layers.data(),count,capacity,winePadding,headSpace,pointerChain)) {
        static bool reported=false;
        if(!reported){Say("Frame OpenXR: transparent quad prefix prevents Wine mixed-layer allocation overlap");reported=true;}
    }
    ei.layerCount=count;ei.layers=count?layers.data():nullptr;
    const double start=Milliseconds();const auto submitted=pEndFrame(session,&ei);Check(submitted,"end frame");
    if(interactionInput.active) {
        static ULONGLONG reportAt=0;const auto now=GetTickCount64();
        if(now>=reportAt) {
            const auto& hand=interactionInput.hand[1];const auto& eye=interactionInput.head;
            const auto toward=eye.p-hand.p;
            const bool poseValid=interaction::Valid(hand)&&interaction::Valid(eye);
            const auto& snapshot=interactionInput.snapshot;
            char message[560];std::snprintf(message,sizeof message,
                "Frame wrist: wanted=%d critical=%d leftTracked=%d rightTracked=%d pose=%d distance=%.3f backFacing=%.3f atlas=%d wheel=%d layer=%d budget=%u/%u submit=%d snapshot_age_ms=%llu fresh=%d session=%llu snapshot_session=%llu vitals=%d hp=%.1f/%.1f mana=%.1f/%.1f belt=%d potions=%u",
                interactionOutput.wrist,interactionOutput.critical,interactionInput.tracked[1],interactionInput.tracked[0],poseValid,
                poseValid?std::sqrt(math::Dot(toward,toward)):-1.f,
                poseValid?math::Dot(interaction::WristNormal(hand),math::Normalized(toward)):-1.f,
                interactionChain!=XR_NULL_HANDLE,interactionOutput.wheel,wrist.subImage.swapchain!=XR_NULL_HANDLE,count,capacity,int(submitted),
                (unsigned long long)(now-snapshot.at),interaction::Fresh(snapshot,interactionInput.session,now),
                (unsigned long long)interactionInput.session,(unsigned long long)snapshot.session,snapshot.vitalsKnown,
                snapshot.health,snapshot.maxHealth,snapshot.mana,snapshot.maxMana,snapshot.beltKnown,potionScene.count);
            Say(message);reportAt=now+5000;
            const auto& xp=snapshot.experience;
            std::snprintf(message,sizeof message,"Frame wrist HUD: xp_known=%d level=%u xp=%u start=%u next=%u percent=%.1f",
                xp.Valid(),xp.level,xp.current,xp.start,xp.next,xp.Progress()*100);
            Say(message);
        }
    }
    if(interactionOutput.wheel&&interactionOutput.map) {
        static uint64_t reportAt=0;const auto now=GetTickCount64();
        if(now>=reportAt) {
            char message[192];std::snprintf(message,sizeof message,"Frame menu map: copied=%d layer=%d palette=%d fresh=%d submit=%d",
                mapEye.copied,menuMap.subImage.swapchain!=XR_NULL_HANDLE,palette.subImage.swapchain!=XR_NULL_HANDLE,
                interaction::Fresh(interactionInput.snapshot,interactionInput.session,now),int(submitted));
            Say(message);reportAt=now+5000;
        }
    }
    static uint64_t reportedSettingsRevision=~0ull;
    if(settingsModel.open&&settingsImageReady&&reportedSettingsRevision!=settingsModel.revision) {
        char message[160];std::snprintf(message,sizeof message,"Frame VR settings: submit result=%d layers=%u panel=%d pointer=%d",
            int(submitted),count,settingsPanel.subImage.swapchain!=XR_NULL_HANDLE,laser.subImage.swapchain!=XR_NULL_HANDLE);Say(message);
        reportedSettingsRevision=settingsModel.revision;
    }
    if(submitted==XR_ERROR_POSE_INVALID) {
        static ULONGLONG reportAt=0;const auto now=GetTickCount64();
        if(now>=reportAt) {
            for(uint32_t i=0;i<count;++i)if(layers[i]->type==XR_TYPE_COMPOSITION_LAYER_QUAD) {
                const auto& q=*reinterpret_cast<const XrCompositionLayerQuad*>(layers[i]);const auto& r=q.pose.orientation;
                char message[200];std::snprintf(message,sizeof message,"Frame layer diagnostic: quad=%u norm2=%.9g finitePosition=%d size=%.3f,%.3f settings=%d laser=%d",
                    i,r.x*r.x+r.y*r.y+r.z*r.z+r.w*r.w,gameplay::Finite(V(q.pose.position)),q.size.width,q.size.height,&q==&settingsPanel,&q==&laser);Say(message);
            }
            reportAt=now+3000;
        }
    }
    timing.submitMs=Milliseconds()-start;
    timing.pipelines=pipelineCreates.exchange(0);timing.maxPipelineMs=maxPipelineMs.exchange(0);
    lastTiming=timing;frame=false;
}
FrameTiming LastTiming() {std::lock_guard lock(mutex);return lastTiming;}
void ConfigureGameplay(bool enabled) {
    std::lock_guard lock(mutex);
    if(!enabled)gamestate::MenuMap(false);
    if(!enabled)panelinput::WorldInput(false);
    if(!enabled){skel::SetMotionBow(false,false,0);skel::SetInteractionHidden(0);interactions.Reset();interactionstate::Cancel();CancelShieldCast();}
    if(motionControls.exchange(enabled)!=enabled)
        Say(enabled?"Frame gameplay: motion controls enabled (right-handed prototype)":"Frame gameplay: motion controls disabled; legacy buttons restored");
}
void ConfigureInteractions(bool seated,float waist,float forward,int dominant) {
    static interaction::Config previous;static bool configured=false;
    interaction::Config c;c.seated=seated;c.waistBelowEyes=std::clamp(waist,.30f,.95f);
    c.beltForward=std::clamp(forward,.03f,.5f);c.holsters=false;c.dominant=dominant==1?1:0;
    if(configured&&c.seated==previous.seated&&c.waistBelowEyes==previous.waistBelowEyes&&
       c.beltForward==previous.beltForward&&c.dominant==previous.dominant)return;
    std::lock_guard lock(mutex);interactions.Configure(c);interactionstate::Cancel();previous=c;configured=true;
}
bool SettingsOpen(){return settingsOpen.load();}
bool SpellSelected() {
    std::lock_guard lock(mutex);
    return motionControls.load()&&motionInput.active&&!motionOutput.block&&motionOutput.rayValid&&
        (motion.Selection()>=0||motionOutput.skill>=0)&&GetTickCount64()-motionAt<150;
}
bool DirectedSpellActive() {
    std::lock_guard lock(mutex);
    const int slot=motionOutput.skill;
    const auto now=GetTickCount64();
    if(!motionControls.load()||!motionInput.active||motionOutput.block||!motionOutput.rayValid||
       now-motionAt>=150||slot<0||slot>=skills::Count||
       !interaction::Fresh(interactionInput.snapshot,gamestate::LocalPlayer(),now))return false;
    const auto& skill=interactionInput.snapshot.skills[slot];
    if(motionOutput.bowReady&&skill.id==weaponskills::Strafe)return false;
    return skill.known&&skill.available&&skill.id>=0&&(skill.directed||
        (motionOutput.bowReady&&weaponskills::Bow(skill.id)&&skill.id!=weaponskills::Strafe));
}
bool GameplayRay(float outOrigin[3],float outDirection[3]) {
    std::lock_guard lock(mutex);
    if(!motionControls.load()||!motionOutput.rayValid||!motionInput.active||GetTickCount64()-motionAt>150)return false;
    const auto undo=math::Conjugate(math::Yaw(math::Heading(Q(head.orientation))));
    const auto o=math::Rotate(undo,motionOutput.rayOrigin-V(head.position));
    const auto d=math::Rotate(undo,motionOutput.rayDirection);
    outOrigin[0]=o.x;outOrigin[1]=o.y;outOrigin[2]=o.z;
    outDirection[0]=d.x;outDirection[1]=d.y;outDirection[2]=d.z;
    return true;
}
bool MeleeSample(uint32_t poseStamp,melee::Sample& sample) {
    std::lock_guard lock(mutex);
    if(!motionControls.load()||!motionInput.active||!motionOutput.meleeContact||GetTickCount64()-motionAt>100)return false;
    if(!meleePoses.Find(poseStamp,Milliseconds()/1000.,sample))return false;
    return sample.epoch==meleeEpoch&&sample.equipment==motionInput.equipment;
}
bool ShieldSample(uint32_t poseStamp,melee::Sample& sample) {
    std::lock_guard lock(mutex);
    if(!motionControls.load()||!motionInput.active||!motionOutput.shieldContact||GetTickCount64()-motionAt>100)return false;
    return shieldPoses.Find(poseStamp,Milliseconds()/1000.,sample)&&sample.epoch==meleeEpoch&&sample.equipment==motionInput.equipment;
}
bool RequestShieldSkill(const melee::Sample& sample,uint32_t target,math::Vec gridTarget) {
    std::lock_guard lock(mutex);
    const auto context=ShieldContext(sample,gamestate::LocalPlayer());const auto now=GetTickCount64();
    if(!motionControls.load()||!motionInput.active||!motionOutput.shieldContact||shieldCast.context.session||
       !weaponskills::Current(context,LiveShieldContext(),motionAt,now,100)||!gameplay::Finite(gridTarget))return false;
    shieldCast={context,now,target,gridTarget,false,false,false};return true;
}
int TakeShieldTarget(uint32_t& target) {
    std::lock_guard lock(mutex);
    if(!shieldCast.issued||shieldCast.taken)return 0;
    if(shieldCast.cancelled||!motionControls.load()||!motionInput.active||motionInput.quickCast||
       GetTickCount64()-motionAt>150||!weaponskills::Current(shieldCast.context,LiveShieldContext(),shieldCast.at,GetTickCount64())||!motionInput.selectedAvailable||
       motionInput.selectedSkill!=weaponskills::Smite||!motionInput.shieldEquipped||motionOutput.block)return -1;
    target=shieldCast.target;shieldCast.taken=true;return 1;
}
bool ShieldAim(float& x,float& y) {
    std::lock_guard lock(mutex);
    if(!shieldCast.issued||shieldCast.cancelled||shieldCast.taken||!motionInput.active||motionOutput.block||motionInput.quickCast||
       motionOutput.skill!=shieldCast.context.slot||motionInput.selectedSkill!=weaponskills::Smite||
       !motionInput.selectedAvailable||!motionInput.shieldEquipped||GetTickCount64()-motionAt>150||
       !weaponskills::Current(shieldCast.context,LiveShieldContext(),shieldCast.at,GetTickCount64()))return false;
    x=shieldCast.gridTarget.x;y=shieldCast.gridTarget.z;return std::isfinite(x+y);
}
void FinishShieldSkill(bool accepted) {
    std::lock_guard lock(mutex);shieldCast={};shieldPulse.Cancel();
    shieldFeedbackAt=GetTickCount64();shieldRefused=!accepted;
    if(accepted)shieldImpactAt=shieldFeedbackAt;
}
void MeleeImpact() {meleeImpactAt=GetTickCount64();}
void SetBowWorldEpoch(uint32_t epoch) {
    std::lock_guard lock(mutex);
    if(bowWorldEpoch==epoch)return;
    bowWorldEpoch=epoch;bowNeedsWorld=bowHasShot=false;CancelShieldCast();
    if(motionInput.weapon==gameplay::Weapon::Bow||motionInput.weapon==gameplay::Weapon::Javelin){motion.Cancel();motionOutput={};}
}
uint64_t BowRenderTime(){return bowRenderTime.load();}
bowflight::Gate BowGate() {
    std::lock_guard lock(mutex);
    return {motionControls.load()&&motionInput.active&&(motionOutput.bowReady||motionOutput.javelinReady),
        gamestate::LocalPlayer(),(meleeEpoch<<32)|bowWorldEpoch,motionAt,motionInput.equipment};
}
bool TakeBowShot(bowflight::Shot& shot) {
    std::lock_guard lock(mutex);
    if(!bowHasShot)return false;
    bowHasShot=false;shot=bowReadyShot;return true;
}
bool BowPreviewPose(bowflight::Shot& pose,bool& visible) {
    std::lock_guard lock(mutex);
    const bowflight::Gate gate{motionControls.load()&&motionInput.active&&motionOutput.bowReady,
        gamestate::LocalPlayer(),(meleeEpoch<<32)|bowWorldEpoch,motionAt,motionInput.equipment};
    if(!bowflight::Allowed(bowPreviewPose,gate,GetTickCount64()))return false;
    pose=bowPreviewPose;visible=bowPreviewVisible;return true;
}
bool View(float out[16],const float hero[3],float eyeHeight,float userEyeHeight,float bodyHeading,float recenterYaw,int eye) {
    std::lock_guard lock(mutex);if(!Active())return false;
    if(std::fabs(recenterYaw-lastRecenter)>.001f){origin=V(head.position);lastRecenter=recenterYaw;}
    const float upm=eyeHeight/std::max(userEyeHeight,.5f);
    const auto base=math::Yaw(bodyHeading-recenterYaw);
    const auto resolveBow=[&](bowflight::Shot shot) {
        const auto offset=math::Rotate(base,shot.origin-origin);
        shot.origin={hero[0]/upm+offset.x,(hero[1]+eyeHeight)/upm+offset.y,hero[2]/upm+offset.z};
        shot.direction=math::Normalized(math::Rotate(base,shot.direction));
        shot.player={hero[0]/upm,hero[1]/upm,hero[2]/upm};shot.unitsPerMetre=upm;
        return shot;
    };
    bowflight::Shot held;
    held.origin=motionOutput.bowDrawing?motionInput.hand[0].p:motionOutput.nockPoint;
    held.direction=motionOutput.bowDrawing?motionOutput.rayDirection:motionOutput.bowForward;
    held.draw=motionOutput.draw;held.at=motionAt;held.session=gamestate::LocalPlayer();
    held.epoch=(meleeEpoch<<32)|bowWorldEpoch;held.equipment=motionInput.equipment;held.sequence=1;
    bowPreviewPose=resolveBow(held);bowPreviewVisible=motionOutput.bowDrawing||motionOutput.bowInReach;
    // Resolve the release once in the SAME stage-to-world frame as this view.
    // Absolute room poses avoid mixing an old weapon bone with a newer head yaw.
    if(bowNeedsWorld) {
        bowNeedsWorld=false;
        const bowflight::Gate gate{motionControls.load()&&motionInput.active&&(motionOutput.bowReady||motionOutput.javelinReady),
            gamestate::LocalPlayer(),(meleeEpoch<<32)|bowWorldEpoch,motionAt,motionInput.equipment};
        const auto shot=resolveBow(bowPending);
        if(bowflight::Allowed(shot,gate,GetTickCount64())){bowReadyShot=shot;bowHasShot=true;}
    }
    const auto& pose=eye<0?head:views[eye&1].pose;
    const auto offset=math::Rotate(base,V(pose.position)-origin)*upm;
    const math::Vec position={hero[0]+offset.x,hero[1]+eyeHeight+offset.y,hero[2]+offset.z};
    math::View(position,math::Multiply(base,Q(pose.orientation)),out);return true;
}
bool Projection(float out[16],float nearWorld,int eye) {
    std::lock_guard lock(mutex);if(!Active())return false;
    const auto& f=views[eye&1].fov;
    math::Projection(f.angleLeft,f.angleRight,f.angleDown,f.angleUp,nearWorld,out);return true;
}
void Shutdown() {
    std::lock_guard lock(mutex);
    windowFocus.Cancel();
    EndPair();ClearInput();interactionPainter.Stop();interaction::icons::Shutdown();
    // Completion is mandatory before destroying our commands/resources, even
    // when ordinary frame submission uses queue ordering without a CPU wait.
    if(fence&&!WaitGpu())failed=true;
    if(uiEye.chain&&pDestroySwapchain)pDestroySwapchain(uiEye.chain);uiEye={};journalMode=spatialMode=workspacePlaced=false;hud::UiFrame(false,false,0);uiSupported=false;uiBlit={};potionModels.Reset();potionScene={};
    if(mapEye.chain&&pDestroySwapchain)pDestroySwapchain(mapEye.chain);mapEye={};mapSupported=false;mapBlit={};gamestate::MenuMap(false);hud::MenuMapCapture(false);
    settingsModel.Close();settingsOpen=false;settingsInputGate.Suspend();settingsPaintRevision=~0ull;settingsAtlasPending=settingsImageReady=false;
    bodyCalibration.Cancel();calibrationRelease={};calibrationPrompted=floorSpace=false;calibrationSession=0;calibrationPaintKey=-1;
    if(settingsChain&&pDestroySwapchain)pDestroySwapchain(settingsChain);settingsChain=XR_NULL_HANDLE;
    atlasUploads.clear();interactionPaintKey=~0ull;interactionPrefetchSession=0;interactionPrefetchRevision=~0u;interactions.Reset();interactionstate::Cancel();
    interactionPaintRequest=0;interactionRequestedKey=~0ull;interactionPaintValid=false;interactionPaintContext={};interactionRequestedContext={};interactionPaintAt=0;
    if(interactionChain&&pDestroySwapchain)pDestroySwapchain(interactionChain);interactionChain=XR_NULL_HANDLE;
    if(gameplayChain&&pDestroySwapchain)pDestroySwapchain(gameplayChain);gameplayChain=XR_NULL_HANDLE;
    if(pointerChain&&pDestroySwapchain)pDestroySwapchain(pointerChain);pointerChain=XR_NULL_HANDLE;
    if(aimSpace&&pDestroySpace)pDestroySpace(aimSpace);aimSpace=XR_NULL_HANDLE;
    if(gazeSpace&&pDestroySpace)pDestroySpace(gazeSpace);gazeSpace=XR_NULL_HANDLE;
    gazeAction=XR_NULL_HANDLE;gazeReady=gazeValid=gazeExtension=gazeSupported=false;gazeStability.Reset();
    for(auto& e:eyes){if(e.chain&&pDestroySwapchain)pDestroySwapchain(e.chain);e={};}
    for(auto& s:handSpaces){if(s&&pDestroySpace)pDestroySpace(s);s=XR_NULL_HANDLE;}
    if(headSpace&&pDestroySpace)pDestroySpace(headSpace);if(space&&pDestroySpace)pDestroySpace(space);
    if(session&&pDestroySession)pDestroySession(session);if(actionSet&&pDestroyActionSet)pDestroyActionSet(actionSet);
    if(instance&&pDestroyInstance)pDestroyInstance(instance);
    session=XR_NULL_HANDLE;instance=XR_NULL_HANDLE;actionSet=XR_NULL_HANDLE;headSpace=space=XR_NULL_HANDLE;
    running=frame=valid=render=haveOrigin=menuPlaced=panelMode=false;state=XR_SESSION_STATE_UNKNOWN;gameWindow=nullptr;
    worldWasLoaded=worldCalibrationPending=worldRecenter=false;
    if(fenceEvent)CloseHandle(fenceEvent);fenceEvent=nullptr;
    fence.Reset();queue.Reset();device.Reset();fenceValue=0;
    gpuProfile={};
    foveation::Shutdown();
    tracking.Close();pad.Close();actions.Close();
    if(afr)UnmapViewOfFile(afr);if(afrHandle)CloseHandle(afrHandle);afr=nullptr;afrHandle=nullptr;gameSwapchain=nullptr;
    if(loader)FreeLibrary(loader);loader=nullptr;
}
} // namespace framevr
