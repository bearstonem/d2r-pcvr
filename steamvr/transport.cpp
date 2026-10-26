#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D12
#define XR_NO_PROTOTYPES
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <reshade.hpp>
#include <xinput.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "afr_eye_shared.h"
#include "d2r_vr_shared.h"
#include "pad_mirror_shared.h"
#include "transport.h"
#include "../vr/bow.h"
#include "../vr/crossbow.h"
#include "../vr/melee.h"
#include "frame_math.h"
#include "composition_layers.h"
#include "swapchain_format.h"
#include "input.h"
#include "dropped_item_labels.h"
#include "stick_gestures.h"
#include "window_focus.h"
#include "gameplay.h"
#include "strike_skills.h"
#include "bow_laser.h"
#include "hand_controls.h"
#include "interaction_ui.h"
#include "sewer_prompt_art.h"
#include "notice_card_art.h"
#include "blood_screen.h"
#include "shapeshift_gesture.h"
#include "guard_strain.h"
#include "guard_strain_paint.h"
#include "heartbeat.h"
#include "wrist_compass.h"
#include "panel_atlas.h"
#include "map_zoom.h"
#include "wrist_full_map.h"
#include "interaction_paint.h"
#include "latest_worker.h"
#include "haptic_pulse.h"
#include "haptic_actions.h"
#include "xinput_rumble.h"
#include "interaction_icons.h"
#include "potion_models.h"
#include "dropped_bottles.h"
#include "../vr/wall_cells.h"
#include "../vr/prop_scan.h"
#include "elemental_hands_renderer.h"
#include "vr_settings_ui.h"
#include "frame_generation.h"
#include "../vr/settings_menu.h"
#include "journal.h"
#include "native_panels.h"
#include "npc_dialogue.h"
#include "native_hud.h"
#include "light_bounds.h"
#include "render_resolution.h"
#include "menu_controls.h"
#include "ui_blit.h"
#include "turn_warp.h"
#include "turn_capture.h"
#include "dxgi_views.h"
#include "scene_depth.h"
#include "ui_world.h"
#include "../vr/interactionstate.h"
#include "../vr/journal_ui.h"
#include "../vr/panel_input.h"
#include "d2r_vr_state.h"
#include "foveation.h"
#include "effects_layer.h"
#include "gaze.h"
#include "d2rcam.h"
#include "profile.h"
#include "../vr/skeletons.h"
#include "spell_aim.h"
#include "aim_rules.h"
#include "toss.h"

namespace gamestate {uint32_t WeaponType();uint32_t HandsKey();uint32_t WeaponSet();uint32_t WeaponClass();uint32_t TwoHanded();uint64_t LocalPlayer();uint32_t MenuEpoch();}
namespace gamecmd {void RequestPotion(int slot);void CancelPotion();}
namespace gamestate {void MenuMap(bool wanted);uint32_t QuestNotices();bool NoticePanelOpen();bool NoticeText(int kind,char* out,uint32_t size);}
namespace gamestate {bool LeftShield();bool RightThrowingPotion();uint32_t RightThrowingPotionCode();framevr::dualwield::Equipment MeleeEquipment();}
namespace combat {float GameVolume();}
namespace projectiles {int ObservedGait(uint64_t& at,bool* tired);void StickFullRun(bool on);}

namespace framevr {
using Microsoft::WRL::ComPtr;
namespace {
constexpr float kRadToDeg=57.295779513f;
#define XR_FUNCTIONS(X) \
 X(EnumerateInstanceExtensionProperties) X(CreateInstance) X(DestroyInstance) \
 X(GetSystem) X(GetSystemProperties) X(GetInstanceProperties) X(GetD3D12GraphicsRequirementsKHR) X(EnumerateViewConfigurationViews) \
 X(CreateSession) X(DestroySession) X(PollEvent) X(BeginSession) X(EndSession) \
 X(CreateReferenceSpace) X(DestroySpace) X(LocateSpace) X(LocateViews) \
 X(WaitFrame) X(BeginFrame) X(EndFrame) X(EnumerateSwapchainFormats) \
 X(CreateSwapchain) X(DestroySwapchain) X(EnumerateSwapchainImages) \
 X(AcquireSwapchainImage) X(WaitSwapchainImage) X(ReleaseSwapchainImage) \
 X(StringToPath) X(PathToString) X(GetCurrentInteractionProfile) X(CreateActionSet) X(DestroyActionSet) X(CreateAction) \
 X(CreateActionSpace) X(SuggestInteractionProfileBindings) X(AttachSessionActionSets) \
 X(SyncActions) X(GetActionStateBoolean) X(GetActionStateFloat) X(GetActionStateVector2f) X(GetActionStatePose) X(ApplyHapticFeedback) X(StopHapticFeedback)
#define DECLARE(name) PFN_xr##name p##name=nullptr;
XR_FUNCTIONS(DECLARE)
#undef DECLARE
PFN_xrGetInstanceProcAddr getProc=nullptr;
HMODULE loader=nullptr;
bool wineOpenXR=false;
// Frame generation (frame_generation.h): the settings now, packed as
// mode*100+preset*10+scale (-1 before the INI is read), how this instance
// started with OFXR Bridge and the layer's control API while it is loaded.
// frameGenMutex keeps the layer's entry points from being called while
// Shutdown destroys the instance that unloads it.
std::atomic<int> frameGenWanted{-1};
std::mutex frameGenMutex;
framegen::Start frameGenStart=framegen::Start::NotAsked;
framegen::SnapshotFn frameGenSnapshot=nullptr;
framegen::RequestFn frameGenRequest=nullptr;
std::wstring frameGenFolder;   // plugins\ofxr beside this plugin
framegen::Readout frameGenReadout;bool frameGenRead=false,frameGenInstalled=false;
bool frameGenRunAsked=true;   // the layer was last asked to run (Both eyes), not to step aside
ULONGLONG frameGenReadAt=0,frameGenInstalledAt=0;
// On, installed and with Both eyes, but this instance has no OFXR Bridge (it was off at
// the start): FrameGenNow asks the next frame to rebuild the session the way a window
// resize does (Capture), and the new instance names the layer (LoadFunctions). The owner
// found a resolution change started it (2026-10-24); before, it waited for a restart.
// Once each time it is turned on (frameGenRebuildArmed), so it never loops; cleared by
// every instance made meanwhile.
std::atomic<bool> frameGenRebuild{false},frameGenRebuildArmed{true};
// A live request asked the loaded layer to run (Both eyes again, or new settings): if it
// fails, FrameGenNow has the session rebuilt once (framegen::RetriesWithNewSession).
bool frameGenRetry=false;
// VDXR: its own halving under the bridge, from the predicted rate (framegen::HalvingWatch).
framegen::HalvingWatch frameGenHalving;
std::atomic<bool> runtimeVdxr{false};
// OFXR Bridge takes over every color swapchain whose images are listed
// through it: three more runtime swapchains and an optical flow pipeline
// each. With this mod's panels (menus, map, VR Settings, notices, blood,
// strain, the bow laser, the belt and companions) SteamVR ran out on the
// owner's PC (2026-10-23: "create atlas failed (-10)" 839 times, menus back
// on the flat panel). Only the eyes need it, so while it is loaded the
// panels' images are listed by the runtime's own function, below the bridge
// (neither the loader nor the bridge wraps handles); the bridge passes their
// acquire, wait and release on untouched and never builds anything for them.
PFN_xrEnumerateSwapchainImages runtimeEnumerateImages=nullptr;
LogFn logger=nullptr;
PanelFn wantsPanel=nullptr;
PanelFn inWorld=nullptr;
PanelFn toggleMenus=nullptr;
FrameTiming timing,lastTiming;
std::atomic<uint32_t> pipelineCreates{0};
std::atomic<double> maxPipelineMs{0};
std::atomic<bool> deferredWait{false};
std::atomic<bool> symmetricProjection{true};
std::atomic<bool> turnsFromDrawnEye{true};
std::atomic<int> turnsKeepOwn{3};   // ConfigureTurnsKeepOwn: 0 off, 1 near things only, 2 everywhere, 3 the game's effects, 4 near things and effects
std::atomic<float> turnsKeepGap{48},turnsKeepReach{96};   // ConfigureTurnsKeepNear: a 4096-wide eye's pixels
std::atomic<float> turnsEffectsLevel{.02f},turnsEffectsReach{32};   // ConfigureTurnsKeepEffects: the layer's level; a 4096-wide eye's pixels
std::atomic<bool> turnsSharp{true};   // ConfigureTurnsSharp: the pictures read Catmull-Rom (TurnWarp::Sharpen)
std::atomic<bool> turnsFill{true};    // ConfigureTurnsFill: no frame-old ghosts in the strips beside near things (TurnWarp::Fill)
std::atomic<bool> turnsRims{true};    // ConfigureTurnsRims: the rims round things against the void go with them (TurnWarp::Rims)
std::atomic<bool> eyeTracking{false};
std::atomic<bool> motionControls{false};
// Stamped each focused XR input while motion controls are on (menus included).
// Lock-free for other threads; desktop and flat play never stamp it.
std::atomic<uint64_t> vrGameplayAt{0};
gameplay::Engine motion;
skills::HandControls handControls;
skills::HandOutput handOutput;
gameplay::Pose handAim[2]{};
bool handAimTracked[2]{};
int castHand=0;
controllers::Labels detectedControllers{},controllerLabels{};
bool controllerProfilesDirty=true,frameControllerExtension=false;
interaction::Skill shieldSkill;
constexpr int shieldSlot=skills::TriggerSlot(1);
gameplay::Input motionInput;
gameplay::Output motionOutput;
interaction::Engine interactions;
interaction::Input interactionInput;
interaction::Output interactionOutput;
// Blood on the visor: state on the frame thread, artwork composed in the
// background, one head-locked quad directly above the world.
blood::Engine bloodScreen;
blood::Output bloodOutput;
struct BloodJob {uint32_t visible=0;std::array<float,blood::Placements> age{},opacity{};};
struct BloodImage {std::vector<uint8_t> pixels;uint32_t visible=0;};
LatestWorker<BloodJob,BloodImage> bloodPainter([](const BloodJob& job) {
    // Decoded and placed once, on this thread; read-only afterwards.
    static const auto patches=[]{
        const auto strip=interaction::BloodArt(blood::StripWidth,blood::StripHeight);
        return blood::Prepare(strip.empty()?blood::FallbackStrip():strip);
    }();
    return BloodImage{blood::Compose(patches,job.visible,job.age,job.opacity),job.visible};
});
uint64_t bloodPaintKey=~0ull,bloodRequestedKey=~0ull,bloodPaintRequest=0;
uint32_t bloodShown=0; // splats in the uploaded image
ULONGLONG bloodPaintAt=0;
bool bloodAtlasPending=false,bloodImageReady=false;
// Quest / level-up notices: a card standing in the room for a few seconds
// instead of the native panel at the eyes (notice_card.h). Each card is
// painted once, in the background, when it comes up.
struct NoticeJob {char text[256]{};};
LatestWorker<NoticeJob,std::vector<uint8_t>> noticePainter([](const NoticeJob& job) {
    std::vector<uint8_t> pixels;notice::Paint(job.text,pixels);return pixels;
});
notice::Watch noticeWatch;
notice::Cards noticeCards;
notice::Card noticeCard;
panels::Placement noticePlacement{};
uint64_t noticePaintRequest=0,noticeRequestedSequence=0,noticePaintedSequence=0;
// The skill wheel's Left claw strikes box saves [gameplay] left_hand_strikes
// itself; until the save lands (Poll, about 0.5 s) the box shows the choice made.
uint64_t leftStrikesSequence=0,leftStrikesAt=0;float leftStrikesWanted=0;
// VR Settings > VR > Movement speed ([gameplay] run_speed_pct): its WALK stop walks and every
// other stop runs. The game's run / walk toggle is kept in line with it from the gait the
// plugin sees and the stick (runspeed::GaitSync). With its Stick toggle on, right stick up
// switches between walking and that run (runspeed::StickToggle).
runspeed::GaitSync gaitSync;
runspeed::StickToggle stickToggle;
bool movementWalks=false;   // the hero should walk (this step): Movement speed's WALK, or the stick's switch
bool noticeAtlasPending=false,noticeImageReady=false;
// Guard strain light: soft white at the rim, directly beneath the blood. Fed by
// the game thread's blocks and the guard tick; painted small in the background.
strain::Engine guardStrain;
strain::Output strainOutput;
std::mutex strainTuningMutex;strain::Tuning strainTuning;
std::atomic<float> strainRecoverSeconds{1.5f};   // [gameplay] guard_strain_recover_ms; 0 = only lowering drains
std::atomic<unsigned> guardBlockEvents{0},guardParryEvents{0},guardBlockWeight{0}; // weight in 1/1000ths
std::atomic<bool> guardBroken{false};   // the game thread's chip check
std::atomic<ULONGLONG> guardStateAt{0};std::atomic<bool> guardEligible{false},guardRaised{false};
strain::Painter strainPainter;
uint64_t strainPaintKey=~0ull,strainPaintSession=0;
float strainShownReach[2]={1,1};   // the uploaded image's reach (guard_strain.h): its quad is the visor times this
ULONGLONG strainPaintAt=0;
bool strainAtlasPending=false,strainImageReady=false,strainSlotReported=false;
// Bow path laser: its own small atlas, repainted only when its color changes.
float bowLaserHue=-1;
bool bowLaserAtlasPending=false,bowLaserImageReady=false;
// Low-life heartbeat: its own waveOut stream, since PlaySound plays one sound
// per process and would cut off the riposte. Frame thread only. Each buffer is
// one beat period; the next is queued just before the current one ends, so
// loudness and tempo follow life beat by beat and stop with it.
std::mutex heartMutex;heartbeat::Tuning heartTuning;
std::shared_ptr<const heartbeat::Pcm> heartSource=std::make_shared<heartbeat::Pcm>(heartbeat::BuiltIn());
std::wstring heartFile=L"?";   // never a real path: the first configure loads the built-in beat
const char* heartSourceName="built-in recording"; // for the first "started" line (under heartMutex)
heartbeat::Level heartLevel;double heartAt=0;
heartbeat::Hush heartHush;float heartHealth=-1;   // wiped visor; last fresh life, for hits
void Say(const char* msg);   // logging, defined below
class HeartStream {
    using OpenFn=MMRESULT(WINAPI*)(LPHWAVEOUT,UINT,LPCWAVEFORMATEX,DWORD_PTR,DWORD_PTR,DWORD);
    using HeaderFn=MMRESULT(WINAPI*)(HWAVEOUT,LPWAVEHDR,UINT);
    using PositionFn=MMRESULT(WINAPI*)(HWAVEOUT,LPMMTIME,UINT);
    using HandleFn=MMRESULT(WINAPI*)(HWAVEOUT);
    OpenFn open=nullptr;HeaderFn prepare=nullptr,unprepare=nullptr,write=nullptr;PositionFn position=nullptr;HandleFn reset=nullptr,close=nullptr;
    bool loaded=false;
    HWAVEOUT out=nullptr;int rate=0,channels=0;
    uint64_t queuedEnd=0; // frames written since open
    struct Buffer {WAVEHDR header{};std::vector<int16_t> pcm;};
    std::deque<std::unique_ptr<Buffer>> buffers;
    bool Load() {
        if(loaded)return open!=nullptr;
        loaded=true;
        if(HMODULE m=LoadLibraryW(L"winmm.dll")) {
            open=reinterpret_cast<OpenFn>(GetProcAddress(m,"waveOutOpen"));prepare=reinterpret_cast<HeaderFn>(GetProcAddress(m,"waveOutPrepareHeader"));
            unprepare=reinterpret_cast<HeaderFn>(GetProcAddress(m,"waveOutUnprepareHeader"));write=reinterpret_cast<HeaderFn>(GetProcAddress(m,"waveOutWrite"));
            position=reinterpret_cast<PositionFn>(GetProcAddress(m,"waveOutGetPosition"));
            reset=reinterpret_cast<HandleFn>(GetProcAddress(m,"waveOutReset"));close=reinterpret_cast<HandleFn>(GetProcAddress(m,"waveOutClose"));
        }
        if(!(open&&prepare&&unprepare&&write&&position&&reset&&close)){open=nullptr;Say("Frame low-life heartbeat: winmm waveOut unavailable; heartbeat silent");}
        return open!=nullptr;
    }
    uint64_t Played() {
        MMTIME t{};t.wType=TIME_SAMPLES;
        if(!out||position(out,&t,sizeof t)!=MMSYSERR_NOERROR||t.wType!=TIME_SAMPLES)return queuedEnd;
        return t.u.sample;
    }
public:
    void Reap() {
        while(!buffers.empty()&&(buffers.front()->header.dwFlags&WHDR_DONE)) {
            unprepare(out,&buffers.front()->header,sizeof(WAVEHDR));buffers.pop_front();
        }
    }
    // Frames still to play of what is queued.
    uint64_t Remaining() {if(!out||buffers.empty())return 0;const auto p=Played();return queuedEnd>p?queuedEnd-p:0;}
    bool Queue(int sourceRate,int sourceChannels,std::vector<int16_t>&& pcm) {
        if(pcm.empty()||!Load())return false;
        if(out&&(sourceRate!=rate||sourceChannels!=channels))Stop();
        if(!out) {
            WAVEFORMATEX f{};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=WORD(sourceChannels);f.nSamplesPerSec=DWORD(sourceRate);
            f.wBitsPerSample=16;f.nBlockAlign=WORD(sourceChannels*2);f.nAvgBytesPerSec=f.nSamplesPerSec*f.nBlockAlign;
            if(open(&out,WAVE_MAPPER,&f,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){out=nullptr;return false;}
            rate=sourceRate;channels=sourceChannels;queuedEnd=0;
        }
        auto b=std::make_unique<Buffer>();b->pcm=std::move(pcm);
        b->header.lpData=reinterpret_cast<LPSTR>(b->pcm.data());b->header.dwBufferLength=DWORD(b->pcm.size()*sizeof(int16_t));
        if(prepare(out,&b->header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR)return false;
        if(write(out,&b->header,sizeof(WAVEHDR))!=MMSYSERR_NOERROR){unprepare(out,&b->header,sizeof(WAVEHDR));return false;}
        queuedEnd+=b->pcm.size()/size_t(channels);buffers.push_back(std::move(b));
        return true;
    }
    int Rate() const {return rate;}
    void Stop() {
        if(!out)return;
        reset(out);   // marks every queued buffer done
        for(auto& b:buffers)unprepare(out,&b->header,sizeof(WAVEHDR));
        buffers.clear();close(out);out=nullptr;rate=channels=0;queuedEnd=0;
    }
    ~HeartStream(){Stop();}
} heartStream;
bool heartPlaying=false;
// Guard break: a bone snap on its own stream, so neither the heartbeat nor the
// riposte (PlaySound) cuts it off. Frame thread only; source under breakMutex.
HeartStream breakStream;
std::mutex breakMutex;std::shared_ptr<const heartbeat::Pcm> breakSource;float breakVolume=1;
std::wstring breakFile=L"?";   // never a real path: the first configure loads the built-in snap
void PlayGuardBreak() {
    std::shared_ptr<const heartbeat::Pcm> source;float volume=0;
    {std::lock_guard lock(breakMutex);source=breakSource;volume=breakVolume;}
    if(!source||source->samples.empty()||!(volume>0))return;
    const float gain=volume*combat::GameVolume();   // the game's Master x Combat sliders
    if(!(gain>0))return;
    std::vector<int16_t> pcm(source->samples.size());
    // The heartbeat's soft limiter: guard_break_volume above 100 bends peaks instead of clipping.
    for(size_t i=0;i<pcm.size();++i)pcm[i]=int16_t(std::lround(heartbeat::Limit(float(source->samples[i])*gain/32768.f)*32767.f));
    breakStream.Reap();
    if(!breakStream.Queue(source->rate,source->channels,std::move(pcm)))Say("Frame guard break: sound did not play");
}
XrSwapchain interactionChain=XR_NULL_HANDLE;
XrSwapchain companionChain=XR_NULL_HANDLE;
bool companionAtlasPending=false,companionPaintValid=false;
interaction::PaintContext companionPaintContext{};
XrSwapchain compassChain=XR_NULL_HANDLE;
compass::Heading wristHeading;
bool compassFrame=false,compassAtlasReady=false;
// VR Settings, the quest card, the blood visor, the strain light and the bow
// laser share one swapchain (panelatlas): each its region of sharedMaster, a
// plain texture their pictures are uploaded into; at the end of a frame in
// which one of them changed the swapchain gets a copy of the whole of it.
XrSwapchain sharedChain=XR_NULL_HANDLE;
std::vector<XrSwapchainImageD3D12KHR> sharedImages;
ComPtr<ID3D12Resource> sharedMaster;
ComPtr<ID3D12CommandAllocator> sharedAllocator;
ComPtr<ID3D12GraphicsCommandList> sharedCommands;
uint64_t sharedFence=0;bool sharedDirty=false;
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
uint64_t settingsSession=0;
settings::HoverHelp settingsHelp;
int settingsHelpVisible=settings::NoHelp;
// The game's own video options for the Perf page's warnings and line, re-read at most
// every 2 s while VR Settings is open and only when the game has saved them:
// the mod's own Settings.json (settings::ModSettingsFile), or the unmodded
// game's while the mod has none yet.
settings::GameVideo GameVideoOptions(ULONGLONG now) {
    static std::wstring modded,unmodded;
    static const bool found=[]{
        PWSTR dir=nullptr;std::wstring savedGames;
        if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames,0,nullptr,&dir)))savedGames=dir;
        if(dir)CoTaskMemFree(dir);
        if(savedGames.empty())return false;
        wchar_t plugin[MAX_PATH]{};HMODULE self=nullptr;
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(&GameVideoOptions),&self))GetModuleFileNameW(self,plugin,MAX_PATH);
        unmodded=savedGames+L"\\Diablo II Resurrected\\Settings.json";
        modded=settings::ModSettingsFile(savedGames,plugin);
        if(modded.empty())modded=unmodded;
        char line[400];std::snprintf(line,sizeof line,"Frame VR settings: the game's video options from %ls",modded.c_str());
        Say(line);
        return true;
    }();
    static settings::GameVideo seen{};static ULONGLONG checkAt=0;static FILETIME written{};
    if(now<checkAt||!found)return seen;
    checkAt=now+2000;
    const std::wstring& path=GetFileAttributesW(modded.c_str())!=INVALID_FILE_ATTRIBUTES?modded:unmodded;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&a)){written={};return seen={};}
    if(!CompareFileTime(&a.ftLastWriteTime,&written))return seen;
    std::string json;
    if(FILE* f=_wfopen(path.c_str(),L"rb")){char buf[4096];size_t n;while((n=fread(buf,1,sizeof buf,f))>0&&json.size()<256*1024)json.append(buf,n);fclose(f);}
    else return seen;
    written=a.ftLastWriteTime;seen=settings::ParseGameVideo(json);
    return seen;
}
ULONGLONG settingsPaintAt=0;
// VR Settings and body calibration are painted on a worker: GDI text over the
// whole 1744 x 1024 page, with a new bitmap and fonts each time, held the frame
// thread on every hover change. The frame thread uploads the finished picture,
// and the painted state is what that picture shows, so a click still waits for
// the page it lands on.
struct SettingsPaintJob {
    settings::Model model;settings::Hit hover;std::string status;controllers::Labels detected;
    int help=settings::NoHelp;settings::GameVideo game{};bool monoAudio=false;
    settings::PerformanceReadout performance{};framegen::Readout frameGen{};
    bool calibrating=false;calibration::Flow body;
};
struct SettingsPainted {std::vector<uint8_t> pixels;double paintMs=0;};
LatestWorker<SettingsPaintJob,SettingsPainted> settingsPainter([](const SettingsPaintJob& job) {
    const auto start=hookmetrics::Nanos();
    auto pixels=job.calibrating?settings::Paint(job.model,{},"",&job.body):
        settings::Paint(job.model,job.hover,job.status.c_str(),nullptr,job.detected,job.help,job.game,job.monoAudio,job.performance,job.frameGen);
    return SettingsPainted{std::move(pixels),double(hookmetrics::Nanos()-start)/1e6};
});
struct SettingsPicture {
    uint64_t revision=~0ull;int hover=-1,status=-1,help=settings::NoHelp,game=-1;
    uint64_t performance=0;settings::GameVideo gameVideo{};framegen::Readout frameGen{};   // the Perf page's
    bool operator==(const SettingsPicture&)const=default;
};
SettingsPicture settingsPainted;     // what the picture shown has
SettingsPicture settingsRequested;   // what the outstanding settings request paints
int calibrationRequestedKey=-1;      // the same for calibration
// The Perf page's numbers: each second's frames as NoteFrame gets them.
struct PerformanceWindow {double game=0,wait=0,draw=0,vr=0,period=0,budget=0;uint32_t frames=0,periods=0,turns=0,budgets=0;ULONGLONG since=0,last=0;} performanceWindow;
double performanceBudget=0;   // the headset's frame, from the last frame that had one
settings::PerformanceReadout performance;
uint64_t performanceSerial=0;ULONGLONG performanceAt=0;
uint64_t settingsPaintRequest=0;
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
    auto pixels=interaction::Paint(job.snapshot,job.output,job.selection,job.labels);
    auto companionPixels=wriststatus::Rows(job.snapshot.companions,job.snapshot.effects)?interaction::PaintWristStatus(job.snapshot.companions,job.snapshot.effects):std::vector<uint8_t>{};
    return interaction::PaintedAtlas{std::move(pixels),double(hookmetrics::Nanos()-start)/1e6,std::move(companionPixels)};
});
uint64_t interactionPrefetchSession=0;
uint32_t interactionPrefetchRevision=~0u;
ULONGLONG interactionPaintAt=0;
bool interactionAtlasPending=false;
// An atlas upload's buffer (kept mapped) and command list, reused once the GPU
// is past them: making a 4 MB upload heap and a command list on each refresh,
// and clearing the buffer first, held the frame thread 5.4-7 ms a refresh
// (Frame UI refresh, 2026-10-19).
struct AtlasStaging {
    ComPtr<ID3D12Resource> upload;uint8_t* data=nullptr;UINT64 bytes=0;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
};
struct AtlasUpload {
    AtlasStaging staging;
    uint64_t fence=0;
};
std::vector<AtlasUpload> atlasUploads;
std::vector<AtlasStaging> atlasStagingFree;   // the GPU is done with these
constexpr size_t AtlasStagingKept=6;
// Where an upload's frame-thread time went; UploadAtlas adds to it.
struct AtlasTiming {double image=0,buffers=0,copy=0,record=0,submit=0;int made=0;} atlasTiming;

melee::PoseHistory meleePoses,offhandPoses;
melee::PoseHistory shieldPoses;
struct ShieldCast {
    weaponskills::ContactContext context{};uint64_t at=0;uint32_t target=0;
    math::Vec gridTarget{};
    bool issued=false,taken=false,cancelled=false;
};
ShieldCast shieldCast;
weaponskills::Pulse shieldPulse;
// [shapeshift] Druid gestures pulse whichever slot holds Werewolf / Werebear.
shapeshift::Engine shapeshiftGesture;
weaponskills::Pulse shapePulse;
int shapeSlot=-1,shapeId=-1,shapeOwner=-1;
// [bow] quiver: face-button arrow skills load the following physical arrows.
std::atomic<bool> quiverEnabled{true},quiverPreview{true};
weaponskills::Quiver quiver;
weaponskills::Loaded quiverArrow{}; // the skill the next release carries, if any
int quiverReportedSlot=-1; // one diagnostic line per face-button press
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
// Double Throw (#224): the left hand's own release, beside the right's.
bowflight::Shot throwLeftPending{},throwLeftReady{};
bool leftNeedsWorld=false,leftHasShot=false,leftThrowArmed=false;
// A trap or bomb thrown to place (#224): where its arc landed, in grid cells,
// for its cast's aim (TossTarget).
struct TossLanding {int skill=-1;float x=0,y=0;uint64_t at=0,session=0;} tossLanding;
// A gas or oil potion thrown as a physical bottle (#292): its body in the
// bottles' world until it first touches anything, where it shatters. The
// game's own Throw is pressed as it leaves the hand, aimed where the same
// world says it lands (the trace), and its potion flies hidden
// (bowvr::ExpectPotion): the two fly together, so the burst follows the
// shatter closely rather than a throw and a flight later.
struct PotionFlight {uint64_t id=0,session=0;uint32_t code=0;double started=0;bool shown=false;interaction::Pose pose{};} potionFlight;
uint64_t potionFlightSerial=0;
constexpr double PotionFlightMax=3;   // seconds; a bottle still in the air then goes
// The latest centre-view stage-to-world frame (View, eye<0). Input reads it
// for the room height of the world floor and to place a thrown bottle's
// landing point in world metres; both run under the transport mutex.
struct StageWorld {bool valid=false;math::Vec origin{},hero{};math::Quat base{0,0,0,1};float upm=0,eyeHeight=0;uint64_t at=0;} stageWorld;
bool crossbowFire=false;
uint64_t crossbowActivation=0,crossbowTakenAt=0;
bowflight::Shot spellReadyShot[2]{};
bool spellHasShot[2]{};
uint64_t spellResolvedAt[2]{};
std::atomic<ULONGLONG> meleeImpactAt[2]{},shieldImpactAt{0},parryMissAt{0};
XrSwapchain gameplayChain=XR_NULL_HANDLE;
std::mutex sewerPromptMutex;
sewerprompt::Target sewerPrompt;
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
GripStickButton leftMenuButton;
stickgestures::Gestures rightStickGestures;   // right stick up: run / walk (Movement speed's Stick toggle); down: loot labels
MenuTriggerGate menuTriggerGate;
PressEdge inventoryButton,menuButton;
float lastRecenter=0;
uint32_t width=0,height=0;
resolution::Size recommendedResolution{},maximumResolution{};
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
// The wrist map's view: Isometric (the game's) or Top-down, and on the
// top-down map whether it turns with you. Configured under VR Settings > HUD.
bool MapTopDownOn(){return settings::MapViewTopDown(vrsettings::Read().values[settings::MapTopDown]);}
// Map view = Full map / Full top-down: the wrist shows the game's full map (wrist_full_map.h)
bool MapFullOn(){return settings::MapViewFull(vrsettings::Read().values[settings::MapTopDown]);}
bool MapTurnOn(){return vrsettings::Read().values[settings::MapRotate]>.5f;}
bool mapSupported=false;
constexpr uint32_t mapSide=512;
// The map's surface is made the first time a map shows. With OFXR Bridge
// loaded that failed in every session it was tried (frame log, 2026-10-23 to
// 25): one failure suspended the map for the session, the minimap gone from
// the hand. A failure now says which step and why, and is tried again a few
// seconds later; only the third in a session suspends the map.
ULONGLONG mapRetryAt=0;int mapFailures=0;
constexpr int MapAttempts=3;constexpr ULONGLONG MapRetryMs=5000;
// SteamVR gives a session 16 swapchains. OFXR Bridge takes three more for each
// eye's: the panels sharing them (panelatlas) leave room for all of them, but
// should a swapchain still be refused (XR_ERROR_LIMIT_REACHED), the native
// menus' panel and the shared panels take the map's surface, the map waits
// while the native menus are open, and their surface goes back when they close.
bool swapchainsShort=false,releaseUiChain=false;
// Right-stick zoom of that map while it is looked at (map_zoom.h). wristMapShown:
// the wrist / palm map quad went into the last frame's layers, centered at
// wristMapCenter (interaction space, like interactionInput.head).
mapzoom::Gesture mapZoom;
bool wristMapShown=false,wristMapLooked=false;
math::Vec wristMapCenter{};
uint64_t mapZoomLogAt=0;
bool mapZoomLogged=false;
// True while the player looks at the shown wrist / palm map: the right stick's
// up / down then zooms the map and is cleared for everything else that frame.
bool MinimapLooked(){return wristMapLooked;}
// Desktop mirror: in stereo the game window presents alternating eye frames,
// so the monitor flickers between both eyes (double vision). After the headset
// takes each right frame, the left eye image (still acquired until EndPair) is
// copied back over it, so the monitor shows only the left eye. [render]
// desktop_one_eye turns it off; eye images need TRANSFER_SRC for it.
bool mirrorSupported=false,mirrorMono=false;
// Eyes by turns: a frame draws one eye. The other is turned from its kept
// image to this frame's head (TurnEye) and goes at this frame's pose, as from
// a pair; without the GPU pass, its last released image goes again at the
// pose and FOV it was drawn with (lastEyes). A frame without a stereo layer
// forgets both, so the next draws a pair.
bool turnFrame=false;
// Replay (Stereo rendering 3): this pair's right eye is the left one moved by
// replayGap (metres) along the left eye's x - ReplayRight puts views[1] there,
// so the camera, the potions and the layer all have it. replayShift: that gap
// in the world's units, from View.
bool replayPair=false;
float replayGap=0,replayShift=0;
// Where an eye stood and how it was turned in the game's world (View), and its
// near plane (Projection): By turns moves the eye not drawn from there.
struct EyeWorld {math::Vec at{};math::Quat turn{0,0,0,1};float nearPlane=0;bool ok=false;math::Vec hero{};};
// base: the body's turn into the world the eye was drawn with (View), so a
// stick, mouse or recentre turn since is turned out as well as the head's.
struct LastEye {XrPosef pose{};XrFovf fov{};bool ok=false;math::Quat base{0,0,0,1};EyeWorld world{};};
std::array<LastEye,2> lastEyes{};
std::array<math::Quat,2> viewBase{{{0,0,0,1},{0,0,0,1}}};   // each eye's, as View last built it
std::array<EyeWorld,2> viewWorld{};   // each eye's this frame
// While the option is on, each eye's image as shown, kept by Copy (KeepTurn):
// a released swapchain image is the runtime's. Also the desktop mirror's left
// image on the frames that draw the right.
bool keepTurns=false;
std::array<ComPtr<ID3D12Resource>,2> turnImages;
std::array<bool,2> turnKept{};
bool turnImagesFailed=false,turnWarpTried=false;
DXGI_FORMAT turnFormat=DXGI_FORMAT_UNKNOWN;
TurnWarp turnWarp;
// Built once in the background as a session is set up, long before a frame
// first turns an eye: that compiles its shaders for the process and has the
// driver build its pipelines once. On the render thread these held it about
// 215 ms in a new process and 112 ms at every later session; then about 3 ms
// (RTX 5090 / Ryzen 9 5950X, 2026-10-20).
struct TurnWarmJob {ComPtr<ID3D12Device> device;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;};
LatestWorker<TurnWarmJob,bool> turnCompiler([](const TurnWarmJob& job){TurnWarp warm;return warm.Initialize(job.device.Get(),job.format,nullptr);});
// By turns: each eye's scene depth as drawn, beside its kept image (KeepDepth).
bool keepDepth=false;
struct KeptDepth {ComPtr<ID3D12Resource> texture;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;float scale[2]{1,1};bool ok=false;};
std::array<KeptDepth,2> turnDepths;
// By turns: each eye's interface as its pass drew it (hud::TakenInterface),
// laid back over its picture by Copy and, turned to this frame's head, over
// the eye not drawn by TurnEye: into turnTarget (typeless, so the interface
// goes over it UNORM as the game blends it), then to its swapchain image.
std::array<ComPtr<ID3D12Resource>,2> turnLayers;
std::array<bool,2> layerKept{};
ComPtr<ID3D12Resource> turnTarget;
bool layersFailed=false;
TurnCounts turnCounts;
// By turns capture (turn_capture.h): armed by the stereo capture of a pair
// while Stereo rendering is By turns, then each of the next frames by turns
// read back after TurnEye (CaptureTurn) and written on a thread of its own.
// TurnEye notes how it moved the eye not drawn, for the frame's sidecar.
struct TurnCapture {
    int left=0,frame=0;unsigned serial=0;uint64_t passes=0;
    bool noted=false,fromDrawn=false,moved=false,reported=true;
    TurnDepth other{},own{};std::array<float,9> turn{};
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Resource> buffer;UINT64 size=0;
    std::wstring folder;
} turnCapture;
std::atomic<int> turnCaptureWriting{0},turnCaptureWritten{0},turnCaptureFailed{0};
std::atomic<bool> uiSupported{false};
bool journalMode=false,spatialMode=false,workspacePlaced=false,opaqueMenuMode=false,upperBandMenuMode=false;
bool npcChoicesMode=false;
UiBlit uiBlit;
UiBlit mapBlit; // Own descriptors: native menu and map copies may share a frame.
potions::Models potionModels;
potions::Scene potionScene;
// Emptied bottles let go of: Box3D bodies, made on the first one ([interactions] bottle_physics).
std::unique_ptr<bottles::World> droppedBottles;
std::atomic<bool> bottlePhysics{true};
uint64_t droppedSession=0;
interaction::Carry playerCarry; // the player's world velocity, for throws and let-go bottles
// The level's small props share that world ([interactions] prop_physics).
std::atomic<bool> propPhysics{true};
bool propsWanted=false;
uint64_t propPoseSerial=0;
// The weapons that push them, posed on the vrcam update thread.
struct BottleWeaponPose {melee::Blade blade{};ULONGLONG at=0;bool valid=false;};
std::mutex bottleWeaponMutex;
std::array<BottleWeaponPose,bottles::Colliders> bottleWeapons{};
ULONGLONG bottleStruckAt[2]{};
elements::Renderer elementalRenderer;
elements::State elementalState;
elements::Scene elementalScene;
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
haptics::Actions hapticActions;
haptics::Pulses hapticPulses;
haptics::CastEvents spellHaptics;
haptics::RumbleLease controllerRumbleLease;
XrAction primary=XR_NULL_HANDLE, secondary=XR_NULL_HANDLE, stickClick=XR_NULL_HANDLE, menu=XR_NULL_HANDLE;
XrPath hands[2]{}; // right, left, matching the existing wire format.
XrSpace handSpaces[2]{};
math::Vec handVelocity[2]{};bool handVelocityKnown[2]{};
XrSpace aimSpace=XR_NULL_HANDLE,leftAimSpace=XR_NULL_HANDLE;
XrPosef menuPose{};
bool menuPlaced=false;
bool panelMode=false,pointerVisible=false,pointerHit=false,mouseDown=false,triggerWasDown=false,cancelWasDown=false;
HWND gameWindow=nullptr;
WORD heldKeyScan=0;
ULONGLONG keyReleaseAt=0;
constexpr float panelWidth=2.0f,panelDistance=panels::MainDistance;
XrSwapchain pointerChain=XR_NULL_HANDLE;
// The pictures painted once share one static image (panelatlas): pointerChain,
// compassChain and gameplayChain are it, or none where a picture failed.
XrSwapchain staticChain=XR_NULL_HANDLE;
XrPosef beamPose{},dotPose{};
float beamLength=0;
// 2026-10-19: one remembered push/pull for every game menu, kept as an
// offset on each default placement (the main menu sits 15 cm beyond the
// workspace). Choosing another Menu distance preset clears it. VR Settings
// keeps its own free grab, relative to where you face when it opens.
panels::Grab panelGrab;
panels::Offset menuOffset,settingsOffset;
panels::Placement uiBase{},menuBase{},settingsBase{};

math::Quat Q(XrQuaternionf q) { return {q.x,q.y,q.z,q.w}; }
math::Vec V(XrVector3f p) { return {p.x,p.y,p.z}; }
XrPosef PanelPose(panels::Placement p) {
    return {{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},{p.position.x,p.position.y,p.position.z}};
}
XrPosef Placed(panels::Placement base,panels::Offset offset) {
    return PanelPose(panels::Apply(base,panels::Limit(base,offset,V(head.position))));
}
// The grabbing hand is the laser's own pose; hit tests read the moved pose.
bool GrabPanel(XrPosef& pose,panels::Placement base,panels::Offset& offset,bool grip,bool hit,bool click,const XrPosef& hand,float pushOnly=0) {
    const bool owned=panelGrab.Step(grip,hit,click,{V(hand.position),Q(hand.orientation)},base,V(head.position),offset,pushOnly);
    if(owned)pose=PanelPose(panels::Apply(base,offset));
    return owned;
}
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
    panelinput::ItemHover(false);
    if(mouseDown||rightMouseDown||controlDown||shiftDown)panelinput::OwnPointer();
    for(int i=0;i<2;++i) {
        bool& held=i?rightMouseDown:mouseDown;
        if(held){INPUT up{};up.type=INPUT_MOUSE;up.mi.dwFlags=i?MOUSEEVENTF_RIGHTUP:MOUSEEVENTF_LEFTUP;SendInput(1,&up,sizeof up);held=false;}
    }
    PanelModifier(VK_CONTROL,false,controlDown);PanelModifier(VK_SHIFT,false,shiftDown);
    panelMouse.Suspend();panelScroll={};panelGrab.Suspend();
}
weaponskills::ContactContext ShieldContext(const melee::Sample& sample,uint64_t session) {
    return {session,sample.epoch,sample.skillContext,sample.equipment,sample.skillSlot,sample.skillOwner};
}
weaponskills::ContactContext LiveShieldContext() {
    return {gamestate::LocalPlayer(),(meleeEpoch<<32)|bowWorldEpoch,motionInput.skillContext,
        motionInput.equipment,shieldSlot,shieldSkill.owner};
}
void CancelShieldCast() {
    shieldPoses.Clear();shieldPulse.Cancel();
    // A native cast may already be in its wind-up. Preserve its cancellation
    // during its bounded native wind-up instead of falling back to a target.
    if(shieldCast.issued)shieldCast.cancelled=true;else shieldCast={};
}
void ClearInput() {
    settingsHelp.Clear();settingsHelpVisible=settings::NoHelp;
    controllerRumbleLease.Update(0,false);
    hapticPulses.Cancel();
    spellHaptics.Clear();
    lootlabels::display.Clear();
    rightStickGestures.Reset();
    meleeImpactAt[0]=meleeImpactAt[1]=shieldImpactAt=parryMissAt=0;
    wristHeading.Reset();compassFrame=false;mapZoom.Reset();wristMapLooked=false;
    panelinput::WorldInput(false);
    gamestate::MenuMap(false);
    menuControls.Suspend();menuNavigation.Reset();nativeMenuPad={};laserClick={};panelinput::UseController();
    handControls.Suspend();
    bodyCalibration.Suspend();
    settingsInputGate.Suspend();
    ++meleeEpoch;motion.Cancel();motionOutput={};motionAt=0;gamecmd::CancelPotion();
    interactions.Reset();interactionOutput={};interactionstate::Cancel();skel::SetInteractionHidden(0);
    potionScene={};if(droppedBottles)droppedBottles->Reset();
    elementalState.Reset();elementalScene={};
    journalPlacement.LoseTracking();
    meleePoses.Clear();offhandPoses.Clear();CancelShieldCast();
    bowNeedsWorld=bowHasShot=leftNeedsWorld=leftHasShot=false;
    skel::SetMotionBow(false,false,0);
    ReleaseUiKey();
    ReleasePanelInput();
    triggerWasDown=cancelWasDown=pointerVisible=pointerHit=false;
    leftMenuButton={};
    // Tracking loss ends any pending mouse press, but a held trigger must
    // still be released before it can become a fresh world attack.
    menuTriggerGate.consumed=true;
    if(tracking.data) { tracking.data->headValid=0;tracking.data->handsValid=0;++tracking.data->counter; }
    if(pad.data) { const auto c=pad.data->counter;*pad.data={};pad.data->version=BW_PAD_MIRROR_VERSION;pad.data->writer_pid=GetCurrentProcessId();pad.data->reserved[0]=uint32_t(menus::Mode::Blocked);pad.data->reserved[1]=gamestate::MenuEpoch();pad.data->counter=c+1; }
    if(actions.data) { actions.data->held=0;++actions.data->counter; }
}
framegen::Options FrameGenWanted() {
    const int packed=frameGenWanted.load();
    return packed<0?framegen::Options{}:framegen::Options{framegen::Mode(packed/100),packed/10%10,packed%10};
}
// Names OFXR Bridge's folder to the Khronos loader for this process only, as
// a place for explicit layers (XR_API_LAYER_PATH, added to any path already
// set; a later instance finds it there). Nothing loads until the instance
// names the layer.
bool FrameGenOffer(const std::wstring& folder) {
    const std::wstring manifest=folder+L"\\"+framegen::ManifestFile;
    if(GetFileAttributesW(manifest.c_str())==INVALID_FILE_ATTRIBUTES) {
        Say("Frame generation: on, but OFXR Bridge is not installed (no layer manifest in plugins\\ofxr); starting without it");
        return false;
    }
    std::wstring value(32768,L'\0');
    const DWORD n=GetEnvironmentVariableW(L"XR_API_LAYER_PATH",value.data(),DWORD(value.size()));
    value.resize(n<value.size()?n:0);
    if(value.find(folder)==std::wstring::npos)value=value.empty()?folder:value+L";"+folder;
    return SetEnvironmentVariableW(L"XR_API_LAYER_PATH",value.c_str())!=FALSE;
}
bool FrameGenListed() {
    PFN_xrEnumerateApiLayerProperties enumerate=nullptr;
    uint32_t count=0;
    if(XR_FAILED(getProc(XR_NULL_HANDLE,"xrEnumerateApiLayerProperties",reinterpret_cast<PFN_xrVoidFunction*>(&enumerate)))||!enumerate||
       XR_FAILED(enumerate(0,&count,nullptr)))return false;
    std::vector<XrApiLayerProperties> layers(count,{XR_TYPE_API_LAYER_PROPERTIES});
    if(XR_FAILED(enumerate(count,&count,layers.data())))return false;
    return std::any_of(layers.begin(),layers.begin()+std::min<size_t>(count,layers.size()),[](const auto& l){return !std::strcmp(l.layerName,framegen::LayerName);});
}
// The runtime under the layers: the one loaded module that offers the
// runtime negotiation (SteamVR's vrclient_x64.dll, VDXR's) and is no layer.
// Its xrEnumerateSwapchainImages for this instance, else none.
PFN_xrEnumerateSwapchainImages FrameGenRuntimeEnumerate() {
    const HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
    if(modules==INVALID_HANDLE_VALUE)return nullptr;
    PFN_xrNegotiateLoaderRuntimeInterface negotiate=nullptr;int found=0;
    MODULEENTRY32W module{};module.dwSize=sizeof module;
    for(BOOL more=Module32FirstW(modules,&module);more;more=Module32NextW(modules,&module)) {
        const auto candidate=reinterpret_cast<PFN_xrNegotiateLoaderRuntimeInterface>(GetProcAddress(module.hModule,"xrNegotiateLoaderRuntimeInterface"));
        if(!candidate||module.hModule==loader||GetProcAddress(module.hModule,"xrNegotiateLoaderApiLayerInterface"))continue;
        negotiate=candidate;++found;
    }
    CloseHandle(modules);
    if(found!=1)return nullptr;
    // The API range the Khronos loader offers: VDXR refuses XR_API_VERSION_1_0 alone
    // (XR_ERROR_INITIALIZATION_FAILED; the owner's PC, 2026-10-24), so its panels went through the bridge.
    XrNegotiateLoaderInfo li{XR_LOADER_INTERFACE_STRUCT_LOADER_INFO,XR_LOADER_INFO_STRUCT_VERSION,sizeof(XrNegotiateLoaderInfo),1,1,
                             XR_MAKE_VERSION(1,0,0),XR_MAKE_VERSION(1,0x3ff,0xfff)};
    XrNegotiateRuntimeRequest rr{XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST,XR_RUNTIME_INFO_STRUCT_VERSION,sizeof(XrNegotiateRuntimeRequest)};
    PFN_xrEnumerateSwapchainImages enumerate=nullptr;
    if(XR_FAILED(negotiate(&li,&rr))||!rr.getInstanceProcAddr||
       XR_FAILED(rr.getInstanceProcAddr(instance,"xrEnumerateSwapchainImages",reinterpret_cast<PFN_xrVoidFunction*>(&enumerate))))return nullptr;
    return enumerate;
}
// The panels' images: below the bridge while it is loaded, else as usual.
XrResult EnumeratePanelImages(XrSwapchain chain,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* images) {
    return (runtimeEnumerateImages?runtimeEnumerateImages:pEnumerateSwapchainImages)(chain,capacity,count,images);
}
// Only if the runtime's own list gives the left eye's images as the bridge
// passed them on; else the panels go through the bridge as before.
void FrameGenCheckPanelImages(XrSwapchain chain,const std::vector<XrSwapchainImageD3D12KHR>& passed) {
    if(!runtimeEnumerateImages)return;
    uint32_t n=0;
    bool same=XR_SUCCEEDED(runtimeEnumerateImages(chain,0,&n,nullptr))&&n==passed.size();
    std::vector<XrSwapchainImageD3D12KHR> own(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    same=same&&XR_SUCCEEDED(runtimeEnumerateImages(chain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(own.data())));
    for(uint32_t i=0;same&&i<n;++i)same=own[i].texture==passed[i].texture;
    if(!same)runtimeEnumerateImages=nullptr;
    Say(same?"Frame generation: the panels' images are listed below OFXR Bridge; it takes over the eyes only":
             "Frame generation: the runtime's own image list differs; the panels go through OFXR Bridge (SteamVR may run out of swapchains)");
}
// Asks the loaded layer for the settings (caller holds frameGenMutex); live: not as
// the instance starts, so a failure to run is retried with a new session.
void FrameGenAsk(const framegen::Options& o,bool live=true) {
    if(!frameGenRequest)return;
    // Both eyes only: by turns each eye would get a quarter of the frames.
    const bool bothEyes=StereoRendering()==0;
    const auto control=framegen::Control(o,bothEyes);
    frameGenRunAsked=bothEyes;frameGenRetry=live&&control.enabled;
    const bool saved=frameGenRequest(&control)!=0;
    const std::string what=!o.On()?"bypass (Off until the game restarts)":!bothEyes?"a pause (By turns; it runs with Both eyes)":
                           framegen::Describe(control.backend,control.preset,control.scale);
    char line[200];std::snprintf(line,sizeof line,"Frame generation: asked OFXR Bridge for %s%s",what.c_str(),saved?"":" (not saved to its ofxr_bridge.ini)");Say(line);
    frameGenReadAt=0;
}
// The layer in the instance's chain: its control API, then the settings now.
framegen::Start FrameGenAttach() {
    const HMODULE layer=GetModuleHandleW(framegen::LibraryFile);
    const auto version=layer?reinterpret_cast<framegen::VersionFn>(GetProcAddress(layer,framegen::VersionExport)):nullptr;
    const auto snapshot=layer?reinterpret_cast<framegen::SnapshotFn>(GetProcAddress(layer,framegen::SnapshotExport)):nullptr;
    const auto request=layer?reinterpret_cast<framegen::RequestFn>(GetProcAddress(layer,framegen::RequestExport)):nullptr;
    if(!version||!snapshot||!request||version()!=framegen::ApiVersion) {
        Say("Frame generation: OFXR Bridge is in the OpenXR chain without the control API this mod knows; its own ofxr_bridge.ini applies");
        return framegen::Start::NoControl;
    }
    std::lock_guard lock(frameGenMutex);
    frameGenSnapshot=snapshot;frameGenRequest=request;
    FrameGenAsk(FrameGenWanted(),false);
    return framegen::Start::Loaded;
}
// What the Perf page and the log say of frame generation. The layer is asked
// at most twice a second; each change is logged. predictedHz: a second's
// runtime-predicted rate (NoteFrame), 0 from the Perf page.
framegen::Readout FrameGenNow(ULONGLONG now,float predictedHz=0) {
    std::lock_guard lock(frameGenMutex);
    if(predictedHz>0&&runtimeVdxr.load())frameGenHalving.Note(predictedHz);
    if(frameGenRead&&now-frameGenReadAt<500)return frameGenReadout;
    frameGenReadAt=now;
    const auto wanted=FrameGenWanted();
    // Stereo rendering switched: run with Both eyes, step aside by turns, live.
    const bool bothEyes=StereoRendering()==0;
    if(frameGenRequest&&wanted.On()&&bothEyes!=frameGenRunAsked)FrameGenAsk(wanted);
    // Installed since the start? Its manifest, at most every 5 s, only while it matters.
    if(wanted.On()&&frameGenStart!=framegen::Start::Loaded&&(!frameGenInstalledAt||now-frameGenInstalledAt>=5000)) {
        frameGenInstalled=!frameGenFolder.empty()&&GetFileAttributesW((frameGenFolder+L"\\"+framegen::ManifestFile).c_str())!=INVALID_FILE_ATTRIBUTES;
        frameGenInstalledAt=now;
    }
    framegen::ControlSnapshotV1 snapshot;
    const bool answered=frameGenSnapshot&&frameGenSnapshot(&snapshot)!=0;
    auto readout=framegen::Classify(frameGenStart,frameGenInstalled,wanted,answered?&snapshot:nullptr,bothEyes);
    readout.runtimeHalves=readout.state==framegen::State::Active&&frameGenHalving.halved;
    // It would run, but this instance has no OFXR Bridge: a new session loads it (frameGenRebuild).
    if(framegen::LoadsWithNewSession(readout.state)&&frameGenRebuildArmed.exchange(false))frameGenRebuild.store(true);
    // It failed a live request: a new session sets it up afresh, once (frameGenRetry).
    if(framegen::RetriesWithNewSession(readout)&&frameGenRetry) {
        frameGenRetry=false;frameGenRebuild.store(true);
        char line[160];std::snprintf(line,sizeof line,"Frame generation: OFXR Bridge failed a live request (0x%08X); a new OpenXR session sets it up again",
                                     unsigned(readout.lastError));Say(line);
    } else if(readout.state==framegen::State::Active)frameGenRetry=false;
    if(!frameGenRead||!(readout==frameGenReadout)) {
        char line[260];std::snprintf(line,sizeof line,"%s [%s; sessions %u, ready %u, bypass %u, errors %u]",framegen::Line(readout).c_str(),
            framegen::StateName(readout.state),readout.sessions,readout.ready,readout.bypass,readout.errors);
        Say(line);
    }
    frameGenReadout=readout;frameGenRead=true;
    return readout;
}
bool LoadFunctions() {
    // Proton's WineOpenXR exports the runtime negotiation entrypoint.
    // Negotiate it directly, or use a standard Windows loader as a fallback.
    loader=LoadLibraryW(L"wineopenxr.dll");
    wineOpenXR=loader!=nullptr;
    // Frame generation: OFXR Bridge is offered to the Khronos loader before it
    // loads; WineOpenXR has no API layers.
    const auto frameGen=FrameGenWanted();
    frameGenRebuild.store(false);   // this instance is made with the settings now
    auto frameGenStarted=wineOpenXR?framegen::Start::Unavailable:framegen::Start::NotAsked;
    bool frameGenOffered=false;
    {std::lock_guard lock(frameGenMutex);frameGenStart=frameGenStarted;frameGenSnapshot=nullptr;frameGenRequest=nullptr;frameGenRead=false;
     frameGenRetry=false;frameGenHalving={};}
    runtimeEnumerateImages=nullptr;
    if(wineOpenXR&&frameGen.On())Say("Frame generation: WineOpenXR has no OpenXR API layers; OFXR Bridge stays off under Proton");
    // The mod's own loader beside this plugin (mods\D2R-PCVR\d2rloader\plugins), then
    // the usual search (the game folder of an older install).
    if(!loader) {
        HMODULE self=nullptr;
        wchar_t path[MAX_PATH]{};
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&LoadFunctions),&self)) {
            DWORD n=GetModuleFileNameW(self,path,MAX_PATH);
            if(n&&n<MAX_PATH) {
                while(n&&path[n-1]!=L'\\')--n;
                path[n]=0;
                frameGenFolder=std::wstring(path)+framegen::Folder;
                if(frameGen.On()) {
                    frameGenOffered=FrameGenOffer(frameGenFolder);
                    if(!frameGenOffered)frameGenStarted=framegen::Start::Missing;
                }
                if(wcscat_s(path,L"openxr_loader.dll")==0&&GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES)
                    loader=LoadLibraryExW(path,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
            }
        }
    }
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
    frameControllerExtension=supports(controllers::FrameExtension);
    std::vector<const char*> enabled{XR_KHR_D3D12_ENABLE_EXTENSION_NAME};
    if(gazeExtension)enabled.push_back(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
    if(frameControllerExtension)enabled.push_back(controllers::FrameExtension);
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(ci.applicationInfo.applicationName,"D2R Steam Frame prototype");
    ci.applicationInfo.applicationVersion=1;ci.applicationInfo.apiVersion=XR_API_VERSION_1_0;
    ci.enabledExtensionCount=uint32_t(enabled.size());ci.enabledExtensionNames=enabled.data();
    // Frame generation names OFXR Bridge for this instance only. If the loader
    // does not list it (a game run as administrator ignores XR_API_LAYER_PATH)
    // or the instance fails with it, the game starts without it.
    const char* const frameGenLayers[]={framegen::LayerName};
    bool frameGenLayer=frameGenOffered&&FrameGenListed();
    if(frameGenOffered&&!frameGenLayer) {
        Say("Frame generation: the OpenXR loader does not list OFXR Bridge (a game run as administrator ignores XR_API_LAYER_PATH); starting without it");
        frameGenStarted=framegen::Start::Failed;
    }
    if(frameGenLayer){ci.enabledApiLayerCount=1;ci.enabledApiLayerNames=frameGenLayers;}
    XrResult created=pCreateInstance(&ci,&instance);
    if(frameGenLayer&&XR_FAILED(created)) {
        char line[200];std::snprintf(line,sizeof line,"Frame generation: xrCreateInstance with OFXR Bridge failed (%d); starting without it",int(created));Say(line);
        frameGenLayer=false;frameGenStarted=framegen::Start::Failed;instance=XR_NULL_HANDLE;
        ci.enabledApiLayerCount=0;ci.enabledApiLayerNames=nullptr;created=pCreateInstance(&ci,&instance);
    }
    if(frameGenLayer) {
        frameGenStarted=FrameGenAttach();
        runtimeEnumerateImages=FrameGenRuntimeEnumerate();
        if(!runtimeEnumerateImages)Say("Frame generation: no runtime found under OFXR Bridge; the panels go through it (SteamVR may run out of swapchains)");
    }
    {std::lock_guard lock(frameGenMutex);frameGenStart=frameGenStarted;frameGenRead=false;}
    if(frameGenLayer)Say("Frame generation: OFXR Bridge is in this OpenXR instance (experimental)");
    if(!Check(created,"create instance")) return false;
#define LOAD(name) if(!Check(getProc(instance,"xr" #name,reinterpret_cast<PFN_xrVoidFunction*>(&p##name)),"load " #name)) return false;
    XR_FUNCTIONS(LOAD)
#undef LOAD
    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};pGetInstanceProperties(instance,&props);
    char b[192];std::snprintf(b,sizeof b,"Frame OpenXR: runtime %s",props.runtimeName);Say(b);
    runtimeVdxr.store(std::strstr(props.runtimeName,"VirtualDesktop")!=nullptr);
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
       !MakeAction(aimPose,"menu_aim",XR_ACTION_TYPE_POSE_INPUT) || !Check(hapticActions.Create(actionSet,pCreateAction),"create hand haptics") ||
       !MakeAction(trigger,"triggers",XR_ACTION_TYPE_FLOAT_INPUT) || !MakeAction(squeeze,"grips",XR_ACTION_TYPE_FLOAT_INPUT) ||
       !MakeAction(primary,"primary",XR_ACTION_TYPE_BOOLEAN_INPUT) || !MakeAction(secondary,"secondary",XR_ACTION_TYPE_BOOLEAN_INPUT) ||
       !MakeAction(stickClick,"stick_click",XR_ACTION_TYPE_BOOLEAN_INPUT) || !MakeAction(menu,"menu",XR_ACTION_TYPE_BOOLEAN_INPUT)) return false;
    Say("Frame haptics: separate feedback_right and feedback_left actions; apply and timed stop use each hand's own output");
    for(const auto* profile:{"/interaction_profiles/valve/index_controller","/interaction_profiles/oculus/touch_controller","/interaction_profiles/microsoft/motion_controller",controllers::FrameProfile}) {
        const bool frameController=std::string_view(profile)==controllers::FrameProfile;
        if(frameController&&!frameControllerExtension)continue;
        const bool index=std::strstr(profile,"index"), touch=std::strstr(profile,"touch");
        const auto layout=controllers::Detect(profile);
        std::vector<XrActionSuggestedBinding> bindings;
        if(!Check(hapticActions.Bind(instance,pStringToPath,bindings),"bind hand haptics"))return false;
        for(int i=0;i<2;++i) {
            const std::string base=i==0?"/user/hand/right/input/":"/user/hand/left/input/";
            auto bind=[&](XrAction a,const char* suffix){bindings.push_back({a,Path((base+suffix).c_str())});};
            bind(gripPose,"grip/pose");bind(stick,"thumbstick");bind(stickClick,"thumbstick/click");bind(trigger,"trigger/value");
            bind(aimPose,"aim/pose");
            // Motion controllers expose squeeze/click (boolean), so omit our float grip there.
            if(index||touch||frameController) bind(squeeze,index?"squeeze/force":"squeeze/value");
            if(index||touch||frameController) {
                bind(primary,controllers::FacePath(layout,i,false));
                bind(secondary,controllers::FacePath(layout,i,true));
            }
            if(frameController)bind(menu,i==1?"view/click":"menu/click");
            else if((touch&&i==1)||(!index&&!touch)) bind(menu,"menu/click");
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
    sci.subactionPath=hands[1];if(!Check(pCreateActionSpace(session,&sci,&leftAimSpace),"create left aim space"))return false;
    if(gazeReady) {
        XrActionSpaceCreateInfo gi{XR_TYPE_ACTION_SPACE_CREATE_INFO};gi.action=gazeAction;gi.poseInActionSpace.orientation.w=1;
        gazeReady=Check(pCreateActionSpace(session,&gi,&gazeSpace),"create gaze space");
    }
    return true;
}
bool WaitGpu();
bool WaitGpuValue(uint64_t value);
// The hand map's surface given up for the native menus' panel or the shared
// panels (swapchainsShort); false while this frame still holds one of its images.
// It is made again once they close.
bool YieldMapSurface(const char* forWhat) {
    if(!mapEye.chain||mapEye.acquired||!WaitGpuValue(mapEye.copyFence))return false;
    pDestroySwapchain(mapEye.chain);mapEye={};mapRetryAt=GetTickCount64()+MapRetryMs;
    char line[160];std::snprintf(line,sizeof line,"Frame menu map: its surface given up for %s (no swapchain left); made again once there is room",forWhat);Say(line);
    return true;
}
bool RecordUpload(ID3D12Resource* dest,int x,int y,int w,int h,const std::vector<uint8_t>& pixels,D3D12_RESOURCE_STATES state,AtlasStaging& staging,double& at);
// A panel's region of its shared image, or a rect within the panel (panelatlas).
XrRect2Di Rect(panelatlas::Region r){return {{r.x,r.y},{r.w,r.h}};}
XrRect2Di At(panelatlas::Region r,XrRect2Di within){return {{r.x+within.offset.x,r.y+within.offset.y},within.extent};}
bool UploadAtlas(XrSwapchain& chain,DXGI_FORMAT eyeFormat,int atlasWidth,int atlasHeight,const std::vector<uint8_t>& pixels,bool dynamic=false,bool* pending=nullptr) {
    // A tiny static atlas is independent of ReShade's effect compiler.
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.createFlags=dynamic?0:XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format=eyeFormat;ci.width=atlasWidth;ci.height=atlasHeight;ci.sampleCount=ci.faceCount=ci.arraySize=ci.mipCount=1;
    if(!chain) {
        XrResult r=pCreateSwapchain(session,&ci,&chain);
        if(r==XR_ERROR_LIMIT_REACHED) {
            if(!swapchainsShort)Say("Frame OpenXR: no swapchain left for a panel (the runtime's limit; frame generation's bridge takes some)");
            swapchainsShort=true;
        }
        if(!Check(r,"create atlas")){chain=XR_NULL_HANDLE;return false;}
    }
    uint32_t count=0;
    if(!Check(EnumeratePanelImages(chain,0,&count,nullptr),"count static atlas images"))return false;
    std::vector<XrSwapchainImageD3D12KHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    if(!Check(EnumeratePanelImages(chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"get static atlas images"))return false;
    double at=Milliseconds();
    const auto stage=[&](double& into){const double t=Milliseconds();into+=t-at;at=t;};
    uint32_t index=0;XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(!Check(pAcquireSwapchainImage(chain,&ai,&index),"acquire static atlas atlas"))return false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;
    if(!Check(pWaitSwapchainImage(chain,&wi),"wait static atlas atlas"))return false;
    stage(atlasTiming.image);
    AtlasStaging staging;
    if(!RecordUpload(images[index].texture,0,0,atlasWidth,atlasHeight,pixels,D3D12_RESOURCE_STATE_RENDER_TARGET,staging,at))return false;
    if(dynamic) {
        atlasUploads.push_back({std::move(staging),fenceValue});
        if(pending)*pending=true;else interactionAtlasPending=true;
        return true; // Released at EndPair, after the pair's existing GPU fence.
    }
    if(!WaitGpu())return false;
    if(atlasStagingFree.size()<AtlasStagingKept)atlasStagingFree.push_back(std::move(staging));
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return Check(pReleaseSwapchainImage(chain,&ri),"release static atlas atlas");
}
// `pixels` (RGBA, w x h) into dest at (x, y): a kept upload buffer (the
// smallest that fits, else a new one, mapped for good), copied on the queue;
// dest in `state` before and after. The staging is the caller's to keep until
// the GPU is done with it.
bool RecordUpload(ID3D12Resource* dest,int x,int y,int w,int h,const std::vector<uint8_t>& pixels,D3D12_RESOURCE_STATES state,AtlasStaging& staging,double& at) {
    const auto stage=[&](double& into){const double t=Milliseconds();into+=t-at;at=t;};
    auto desc=dest->GetDesc();desc.Width=UINT64(w);desc.Height=UINT(h);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
    device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
    {
        auto best=atlasStagingFree.end();
        for(auto it=atlasStagingFree.begin();it!=atlasStagingFree.end();++it)
            if(it->bytes>=bytes&&(best==atlasStagingFree.end()||it->bytes<best->bytes))best=it;
        if(best!=atlasStagingFree.end()){staging=std::move(*best);atlasStagingFree.erase(best);}
    }
    if(!staging.upload) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC uploadDesc{};uploadDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;uploadDesc.Width=bytes;
        uploadDesc.Height=uploadDesc.DepthOrArraySize=uploadDesc.MipLevels=uploadDesc.SampleDesc.Count=1;uploadDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(!Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&staging.upload)),"create static atlas upload"))return false;
        D3D12_RANGE readRange{};
        if(!Hr(staging.upload->Map(0,&readRange,reinterpret_cast<void**>(&staging.data)),"map static atlas upload"))return false;
        staging.bytes=bytes;
        if(!Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&staging.allocator)),"create static atlas allocator")||
           !Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,staging.allocator.Get(),nullptr,IID_PPV_ARGS(&staging.commands)),"create static atlas list")||
           !Hr(staging.commands->Close(),"close new static atlas list"))return false;
        ++atlasTiming.made;
    }
    stage(atlasTiming.buffers);
    // Only the footprint's rows are copied, so the rest of the buffer need not be cleared.
    uint8_t* const data=staging.data;
    const bool bgra=desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS;
    for(int row=0;row<h;++row) {
      if(!bgra) {std::memcpy(data+footprint.Offset+row*footprint.Footprint.RowPitch,pixels.data()+size_t(row)*w*4,size_t(w)*4);continue;}
      for(int column=0;column<w;++column) {
        auto* pixel=data+footprint.Offset+row*footprint.Footprint.RowPitch+column*4;
        const auto* rgba=pixels.data()+(size_t(row)*w+column)*4;
        pixel[0]=rgba[bgra?2:0];pixel[1]=rgba[1];pixel[2]=rgba[bgra?0:2];pixel[3]=rgba[3];
      }
    }
    stage(atlasTiming.copy);
    auto* list=staging.commands.Get();
    if(!Hr(staging.allocator->Reset(),"reset static atlas allocator")||!Hr(list->Reset(staging.allocator.Get(),nullptr),"reset static atlas list"))return false;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={dest,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,state,D3D12_RESOURCE_STATE_COPY_DEST};
    if(state!=D3D12_RESOURCE_STATE_COPY_DEST)list->ResourceBarrier(1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=staging.upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=dest;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst,UINT(x),UINT(y),0,&src,nullptr);
    std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
    if(state!=D3D12_RESOURCE_STATE_COPY_DEST)list->ResourceBarrier(1,&barrier);
    if(!Hr(list->Close(),"close static atlas list"))return false;
    stage(atlasTiming.record);
    ID3D12CommandList* lists[]={list};queue->ExecuteCommandLists(1,lists);
    if(!Hr(queue->Signal(fence.Get(),++fenceValue),"signal atlas fence"))return false;
    stage(atlasTiming.submit);
    return true;
}
// The shared panels' swapchain and master (panelatlas), made once a session:
// at the start, so VR Settings never waits for room.
bool MakeShared() {
    if(sharedChain)return true;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format=atlasFormat;ci.width=panelatlas::SharedWidth;ci.height=panelatlas::SharedHeight;ci.sampleCount=ci.faceCount=ci.arraySize=ci.mipCount=1;
    XrResult r=pCreateSwapchain(session,&ci,&sharedChain);
    if(r==XR_ERROR_LIMIT_REACHED) {
        if(!swapchainsShort)Say("Frame OpenXR: no swapchain left for a panel (the runtime's limit; frame generation's bridge takes some)");
        swapchainsShort=true;
        if(YieldMapSurface("the shared panels"))r=pCreateSwapchain(session,&ci,&sharedChain);
    }
    if(!Check(r,"create shared panel atlas")){sharedChain=XR_NULL_HANDLE;return false;}
    const auto fail=[&](const char* what){Say(what);pDestroySwapchain(sharedChain);sharedChain=XR_NULL_HANDLE;sharedImages.clear();sharedMaster.Reset();return false;};
    uint32_t n=0;
    if(XR_FAILED(EnumeratePanelImages(sharedChain,0,&n,nullptr))||!n)return fail("Frame panels: the shared atlas's images unavailable");
    sharedImages.assign(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    if(XR_FAILED(EnumeratePanelImages(sharedChain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(sharedImages.data()))))return fail("Frame panels: the shared atlas's images unavailable");
    // The master as the images are (format, size): copied whole into one a frame.
    auto desc=sharedImages[0].texture->GetDesc();desc.Flags=D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&sharedMaster)))||
       FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&sharedAllocator)))||
       FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,sharedAllocator.Get(),nullptr,IID_PPV_ARGS(&sharedCommands)))||
       FAILED(sharedCommands->Close()))return fail("Frame panels: the shared atlas's master unavailable");
    sharedFence=0;sharedDirty=false;
    char line[200];std::snprintf(line,sizeof line,"Frame panels: one %dx%d image shared by VR Settings, the quest card, the blood visor, the strain light and the bow laser",
        panelatlas::SharedWidth,panelatlas::SharedHeight);Say(line);
    return true;
}
// A shared panel's picture into its region of the master; the swapchain gets
// it at the frame's end (FlushShared), when `pending` is turned into ready.
bool UploadShared(panelatlas::Region region,const std::vector<uint8_t>& pixels,bool* pending) {
    if(pixels.size()!=size_t(region.w)*region.h*4||!MakeShared())return false;
    double at=Milliseconds();
    AtlasStaging staging;
    if(!RecordUpload(sharedMaster.Get(),region.x,region.y,region.w,region.h,pixels,D3D12_RESOURCE_STATE_COPY_DEST,staging,at))return false;
    atlasUploads.push_back({std::move(staging),fenceValue});
    *pending=true;sharedDirty=true;
    return true;
}
// This frame's new shared pictures to the swapchain: the whole master into an
// image, released at once (after the copy, on the session's queue).
bool FlushShared() {
    if(!sharedDirty)return true;
    if(!sharedChain)return false;
    uint32_t index=0;XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(!Check(pAcquireSwapchainImage(sharedChain,&ai,&index),"acquire shared panel atlas"))return false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(!Check(pWaitSwapchainImage(sharedChain,&wi),"wait shared panel atlas")||!WaitGpuValue(sharedFence)||
       FAILED(sharedAllocator->Reset())||FAILED(sharedCommands->Reset(sharedAllocator.Get(),nullptr))) {
        pReleaseSwapchainImage(sharedChain,&ri);return false;
    }
    auto* image=sharedImages[index].texture;
    D3D12_RESOURCE_BARRIER b[2]{};
    for(auto& x:b){x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
    b[0].Transition.pResource=sharedMaster.Get();b[0].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[1].Transition.pResource=image;b[1].Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;b[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
    sharedCommands->ResourceBarrier(2,b);
    sharedCommands->CopyResource(image,sharedMaster.Get());
    for(auto& x:b)std::swap(x.Transition.StateBefore,x.Transition.StateAfter);
    sharedCommands->ResourceBarrier(2,b);
    bool ok=SUCCEEDED(sharedCommands->Close());
    if(ok){ID3D12CommandList* lists[]={sharedCommands.Get()};queue->ExecuteCommandLists(1,lists);ok=SUCCEEDED(queue->Signal(fence.Get(),++fenceValue));}
    if(ok)sharedFence=fenceValue;
    ok=Check(pReleaseSwapchainImage(sharedChain,&ri),"release shared panel atlas")&&ok;
    if(ok)sharedDirty=false;
    return ok;
}
// The pointer: the beam (rows 0-31) and the dot (32-95), 64 wide.
std::vector<uint8_t> PaintPointer() {
    std::vector<uint8_t> pixels(64*96*4);
    for(int y=0;y<96;++y)for(int x=0;x<64;++x) {
        auto* p=pixels.data()+(y*64+x)*4;p[0]=40;p[1]=210;p[2]=255;
        const float radius=std::hypot((x-31.5f)/30.f,(y-63.5f)/30.f);
        p[3]=y<32?200:uint8_t(std::clamp((1-radius)*10,0.f,1.f)*255);
    }
    return pixels;
}
// Static text atlas: painted once, never repainted in the frame loop; empty if
// GDI could not. Native skill icons, names and belt counts require a separate
// read-only UI adapter.
std::vector<uint8_t> PaintGameplay() {
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=1024;info.bmiHeader.biHeight=-1024;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap||!bits) {if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return {};}
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
    sewerprompt::Paint(dc,sewerprompt::AtlasX,sewerprompt::AtlasY);
    GdiFlush();
    std::vector<uint8_t> rgba(1024*1024*4);const auto* b=static_cast<const uint8_t*>(bits);
    for(int i=0;i<1024*1024;++i) {
        rgba[4*i]=b[4*i+2];rgba[4*i+1]=b[4*i+1];rgba[4*i+2]=b[4*i];
        rgba[4*i+3]=(b[4*i]||b[4*i+1]||b[4*i+2])?255:0;
    }
    SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    return rgba;
}
// The painters' threads, and the pictures painted once into one static image
// (panelatlas): false if it could not be made. The compass or the gameplay art
// alone may be missing; their chains then stay none.
bool SetupStatic(DXGI_FORMAT eyeFormat) {
    if(!interactionPainter.Start())Say("Frame UI: background painter unavailable; interaction artwork cannot refresh");
    if(!bloodPainter.Start())Say("Frame blood visor: background painter unavailable; blood stays hidden");
    if(!noticePainter.Start())Say("Frame notices: background painter unavailable; quest and level-up cards stay hidden");
    if(!strainPainter.Start())Say("Frame guard strain: background painter unavailable; the light stays hidden");
    if(!settingsPainter.Start())Say("Frame VR settings: background painter unavailable; VR Settings and calibration cannot be shown");
    using namespace panelatlas;
    std::vector<uint8_t> pixels(size_t(StaticWidth)*StaticHeight*4);
    const auto put=[&](const std::vector<uint8_t>& from,Region r) {
        if(from.size()!=size_t(r.w)*r.h*4)return false;
        for(int y=0;y<r.h;++y)std::memcpy(&pixels[(size_t(r.y+y)*StaticWidth+r.x)*4],&from[size_t(y)*r.w*4],size_t(r.w)*4);
        return true;
    };
    put(PaintPointer(),Pointer);
    const bool compassPainted=put(compass::Paint(),Compass),gameplayPainted=put(PaintGameplay(),Gameplay);
    if(!UploadAtlas(staticChain,eyeFormat,StaticWidth,StaticHeight,pixels))return false;
    pointerChain=staticChain;
    compassAtlasReady=compassPainted;compassChain=compassPainted?staticChain:XR_NULL_HANDLE;
    gameplayChain=gameplayPainted?staticChain:XR_NULL_HANDLE;
    if(!compassPainted)Say("Frame wrist compass: static atlas unavailable");
    if(!gameplayPainted)Say("Frame gameplay: palette atlas unavailable; motion controls remain disabled");
    return true;
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
    XrViewConfigurationView resolutionViews[2]{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    uint32_t resolutionCount=0;recommendedResolution=maximumResolution={};
    if(XR_SUCCEEDED(pEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&resolutionCount,resolutionViews))&&resolutionCount==2){
        recommendedResolution={std::max(resolutionViews[0].recommendedImageRectWidth,resolutionViews[1].recommendedImageRectWidth),
            std::max(resolutionViews[0].recommendedImageRectHeight,resolutionViews[1].recommendedImageRectHeight)};
        maximumResolution={std::min(resolutionViews[0].maxImageRectWidth,resolutionViews[1].maxImageRectWidth),
            std::min(resolutionViews[0].maxImageRectHeight,resolutionViews[1].maxImageRectHeight)};
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
        if(XR_SUCCEEDED(pGetSystemProperties(instance,system,&properties))){
            maximumResolution.w=std::min(maximumResolution.w,properties.graphicsProperties.maxSwapchainImageWidth);
            maximumResolution.h=std::min(maximumResolution.h,properties.graphicsProperties.maxSwapchainImageHeight);
        }else maximumResolution={};
    }
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
    mapRetryAt=0;mapFailures=0;swapchainsShort=releaseUiChain=false;
    mirrorSupported=true;
    for(auto& e:eyes) {
        XrSwapchainCreateInfo c{XR_TYPE_SWAPCHAIN_CREATE_INFO};c.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        c.format=eyeFormat;c.width=width;c.height=height;c.sampleCount=1;c.faceCount=1;c.arraySize=1;c.mipCount=1;
        if(&e==&eyes[0]) {
            c.usageFlags|=XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT; // desktop mirror source
            if(XR_FAILED(pCreateSwapchain(session,&c,&e.chain))) {
                e.chain=XR_NULL_HANDLE;mirrorSupported=false;c.usageFlags&=~XrSwapchainUsageFlags(XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT);
                Say("Frame OpenXR: eye image cannot be a copy source; desktop shows both eyes");
            }
        }
        if(!e.chain&&!Check(pCreateSwapchain(session,&c,&e.chain),"create eye swapchain")) return false;
        n=0;pEnumerateSwapchainImages(e.chain,0,&n,nullptr);e.images.resize(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        if(!Check(pEnumerateSwapchainImages(e.chain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data())),"enumerate eye textures")) return false;
        if(&e==&eyes[0])FrameGenCheckPanelImages(e.chain,e.images);
        if(!Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&e.allocator)),"create eye copy allocator")||
           !Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,e.allocator.Get(),nullptr,IID_PPV_ARGS(&e.commands)),"create eye copy list")||
           !Hr(e.commands->Close(),"close initial eye copy list"))return false;
    }
    if(!Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"create fence")) return false;
    fenceEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!fenceEvent) return false;
    if(!SetupStatic(eyeFormat))return false;
    atlasFormat=eyeFormat;
    if(!MakeShared())Say("Frame panels: the shared atlas unavailable; VR Settings, the quest card, blood, the strain light and the bow laser stay hidden");
    potionModels.Initialize(device.Get(),queue.Get(),eyeFormat,width,height,&Say);
    turnFormat=eyeFormat;
    elementalRenderer.Initialize(device.Get(),queue.Get(),eyeFormat,width,height,&Say);
    if(!tracking.Open(D2RVR_SHARED_NAME)||!pad.Open(BW_PAD_MIRROR_NAME)||!actions.Open(D2RVR_ACTIONS_NAME)) return false;
    tracking.data->version=D2RVR_SHARED_VERSION;pad.data->version=BW_PAD_MIRROR_VERSION;actions.data->version=D2RVR_ACTIONS_VERSION;
    gameSwapchain=swap;
    char b[160];std::snprintf(b,sizeof b,"Frame OpenXR: D3D12 session ready, eyes %ux%u format %d (game format %d)",width,height,int(eyeFormat),int(format));Say(b);
    // Only for a session that started: a failed one is shut down at once, and
    // that would wait here for the warm-up.
    if(turnCompiler.Start())turnCompiler.Submit({device,turnFormat});   // else the first turned eye builds it all
    return true;
}
void UpdateControllerLabels() {
    if(controllerProfilesDirty){
        bool retry=false;
        controllers::Labels detected;
        for(int hand=0;hand<2;++hand){
            XrInteractionProfileState current{XR_TYPE_INTERACTION_PROFILE_STATE};
            char path[XR_MAX_PATH_LENGTH]{};uint32_t size=0;
            if(XR_FAILED(pGetCurrentInteractionProfile(session,hands[hand],&current))){retry=true;continue;}
            if(current.interactionProfile!=XR_NULL_PATH){
                if(XR_FAILED(pPathToString(instance,current.interactionProfile,sizeof path,&size,path))){retry=true;continue;}
                detected.hands[hand]=controllers::Detect(path);
            }
        }
        if(detectedControllers!=detected){
            detectedControllers=detected;++settingsModel.revision;
            char line[120];std::snprintf(line,sizeof line,"Frame controllers: right=%s left=%s",
                controllers::Name(detected.hands[0]),controllers::Name(detected.hands[1]));Say(line);
        }
        controllerProfilesDirty=retry;
    }
    controllerLabels=controllers::Resolve(controllers::Choice(int(vrsettings::Read().values[settings::ControllerLabels])),detectedControllers);
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
panels::Size UiSurfaceSize() {
    // The handheld journal keeps its book size; native workspaces fit in both axes.
    if(journalMode)return {uiSize,uiSize*(uiCrop[2]>0?float(uiCrop[3])/uiCrop[2]:1.3f)};
    return panels::Fit(uiCrop[2]>0?unsigned(uiCrop[2]):0,uiCrop[3]>0?unsigned(uiCrop[3]):0);
}
XrPosef UiSurfacePose() {
    auto pose=uiPose;
    if(upperBandMenuMode&&!journalMode) {
        const auto p=V(pose.position)+math::Rotate(Q(pose.orientation),{0,panels::UpperBandOffset(UiSurfaceSize()),0});
        pose.position={p.x,p.y,p.z};
    }
    return pose;
}
void MenuPointer(const menus::Pad& controls) {
    nativeMenuPad={};
    pointerVisible=pointerHit=false;
    if(settingsModel.open||bodyCalibration.Active()||calibrationRelease.blocked){panelinput::ItemHover(false);return;}
    const bool spatialReady=spatialMode&&(journalMode?journalPlacement.Placed():workspacePlaced)&&uiCrop[2]>0;
    const bool uiActive=(panelMode&&menuPlaced)||spatialReady;
    const auto surfacePose=spatialMode?UiSurfacePose():menuPose;
    const auto surface=spatialMode?UiSurfaceSize():panels::Fit(width,height,panelWidth);
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
        pointerHit=math::IntersectPanel(ray,direction,V(surfacePose.position),Q(surfacePose.orientation),surface.width,surface.height,hit);
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
    // The handheld journal has its own left-grip placement.
    const bool grip=aimTracked&&laser&&!journalMode&&(controls.buttons&menus::RightBumper);
    const bool grabbing=GrabPanel(spatialMode?uiPose:menuPose,spatialMode?uiBase:menuBase,menuOffset,
        grip,pointerHit,pressed||mouseDown||rightMouseDown,aim.pose,
        panels::MenuDistance(int(vrsettings::Read().values[settings::MenuDistance]),!spatialMode));
    const bool active=uiActive&&focused&&aimTracked&&laser;
    if(active)panelinput::OwnPointer();
    if(active&&pointerHit) {
        RECT client{};
        if(GetClientRect(gameWindow,&client)) {
            math::Vec native{hit.u,hit.v,0};
            if(spatialMode&&!journal::Pixel({uiCrop[0],uiCrop[1],uiCrop[2],uiCrop[3]},hit.u,hit.v,uiWidth,uiHeight,native))pointerHit=false;
            POINT pixel{LONG(native.x*(client.right-1)),LONG(native.y*(client.bottom-1))};
            if(pointerHit&&ClientToScreen(gameWindow,&pixel))pointerHit=SetCursorPos(pixel.x,pixel.y)!=FALSE;
            else pointerHit=false;
        } else pointerHit=false;
    }
    const bool modifiers=active&&(pointerHit||mouseDown||rightMouseDown);
    PanelModifier(VK_CONTROL,modifiers&&controls.lt>140,controlDown);
    PanelModifier(VK_SHIFT,modifiers&&!grabbing&&(controls.buttons&menus::RightBumper),shiftDown);
    laserClick.Step(pressed&&!grabbing,(controls.buttons&menus::LeftBumper)!=0);
    const auto edges=panelMouse.Step(active,pointerHit,laserClick.Left(),laserClick.Right());
    panelinput::ItemHover(active,pointerHit,panelMouse.down[0]||panelMouse.down[1]);
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
    if(!motionInput.active||handControls.Wheel()<0||!motionInput.tracked[handControls.Wheel()]||!handAimTracked[1-handControls.Wheel()])return;
    const int hand=handControls.Wheel(),pointer=1-hand;
    auto pose=motionInput.hand[hand];
    const auto normal=math::Normalized(motionInput.head.p-pose.p);
    auto right=math::Normalized(math::Cross({0,1,0},normal));
    if(math::Dot(right,right)<.5f)return;
    pose.q=math::FromAxes(right,math::Cross(normal,right),normal);
    pose.p=pose.p+math::Vec{0,.12f,0};palettePose=XrPose(pose);
    const auto ray=handAim[pointer].p,direction=math::Rotate(handAim[pointer].q,{0,0,-1});
    math::PanelHit hit;
    pointerHit=math::IntersectPanel(ray,direction,pose.p,pose.q,paletteWidth,paletteHeight,hit);
    beamLength=pointerHit?hit.distance:.65f;
    const auto mid=ray+direction*(beamLength*.5f);
    beamPose=XrPose({mid,math::BeamOrientation(direction,V(head.position)-mid)});pointerVisible=true;
    if(pointerHit) {
        motionInput.hover=interaction::WheelCell(hit.u,hit.v,interactionOutput.hover,interactions.Page(),hand,interactionInput.snapshot.developer,interactions.DeveloperPage(),interactionOutput.leftStrikes>=0);
        dotPose=XrPose({hit.point+math::Rotate(pose.q,{0,0,.003f}),pose.q});
    }
}
bool MotionPulse(int hand,haptics::Category category,float strength,XrDuration duration,float frequency=XR_FREQUENCY_UNSPECIFIED,const haptics::Strengths* preview=nullptr) {
    const auto ai=hapticActions.Info(hand);
    if(!ai.action||!pApplyHapticFeedback||!pStopHapticFeedback||!session||!running)return false;
    hapticPulses.Configure(settings::HapticLevels(vrsettings::Read().values));
    // Capture handles by value. Shutdown joins the timer before destroying
    // these handles; the worker never takes the transport/render mutex.
    const auto xrSession=session;const auto outputs=hapticActions;const auto stop=pStopHapticFeedback;const auto log=logger;
    if(!hapticPulses.Start([xrSession,outputs,stop,log,reported=std::array<bool,2>{}](int hand) mutable {
        const auto ai=outputs.Info(hand);
        const auto result=stop(xrSession,&ai);
        if(!reported[hand]&&log) {
            char message[128];std::snprintf(message,sizeof message,"Frame haptics: explicit timed stop hand=%d result=%d",hand,int(result));log(message);reported[hand]=true;
        }
    }))return false;
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.duration=duration;vibration.frequency=frequency;
    XrResult result=XR_SUCCESS;
    const bool applied=hapticPulses.Pulse(hand,category,std::chrono::nanoseconds(duration),strength,[&](float amplitude){
        vibration.amplitude=amplitude;
        result=pApplyHapticFeedback(session,&ai,reinterpret_cast<XrHapticBaseHeader*>(&vibration));return result==XR_SUCCESS;
    },preview);
    if(preview) {
        char message[192];std::snprintf(message,sizeof message,
            "Frame haptics test: hand=%d category=%d amplitude=%.3f duration_ms=%lld frequency=%.0f sent=%d result=%d",
            hand,int(category),vibration.amplitude,static_cast<long long>(duration/1000000),frequency,applied,int(result));
        Say(message);
    }
    static bool reported[2]{};static ULONGLONG errorAt=0;
    if(applied&&!reported[hand]) {
        Say(hand==0?"Frame gameplay: right-hand haptic accepted":"Frame gameplay: left-hand haptic accepted");reported[hand]=true;
    } else if(XR_FAILED(result)&&GetTickCount64()>=errorAt) {
        char message[128];std::snprintf(message,sizeof message,"Frame gameplay: haptic failed hand=%d result=%d",hand,int(result));Say(message);
        errorAt=GetTickCount64()+5000;
    }
    return applied;
}
void MotionPattern(haptics::Category category,const haptics::Pattern& pattern,const haptics::Strengths* preview=nullptr) {
    for(int hand=0;hand<2;++hand){
        const auto& sample=pattern[hand];
        if(sample.milliseconds>0)MotionPulse(hand,category,sample.amplitude,XrDuration(sample.milliseconds)*1000000,sample.frequency,preview);
    }
}
void MotionFeedback(const gameplay::Output& out) {
    static double lastPull=0;static float previousDraw=0;
    if(out.leftFeedback)MotionPulse(1,haptics::MotionCategory(out.leftFeedback,false,true),out.leftFeedback==2?.65f:.45f,45000000);
    const unsigned event=out.feedback;
    if(event) {
        const auto category=haptics::MotionCategory(event,out.bowReady,out.javelinReady);
        if(event==2&&(out.bowReady||out.javelinReady))MotionPattern(category,haptics::Release(out.bowReady));
        else {
            MotionPulse(0,category,event==6?.12f:event==4?.2f:event==2||event==3?.65f:.45f,event==4||event==6?20000000:45000000);
            if(out.bowReady&&(event==1||event==5))MotionPulse(1,category,.25f,35000000);
        }
        lastPull=motionInput.time;previousDraw=out.draw;
    } else if(out.bowDrawing&&motionInput.time-lastPull>.07&&std::fabs(out.draw-previousDraw)>.018f) {
        // A light ratchet felt while pulling, rather than only the native
        // attack rumble after release. No repeated buzz while held still.
        MotionPattern(haptics::Category::BowDraw,haptics::BowPull(out.draw));
        lastPull=motionInput.time;previousDraw=out.draw;
    }
    if(!out.bowDrawing)previousDraw=0;
}
// A druid's shapeshift form draws only claws: its weapon and shield are stats
// without a visual (1 Werewolf, 2 Werebear; vr/interactionstate.cpp).
bool InShapeForm() {return interactionstate::ShapeForm(gamestate::LocalPlayer(),GetTickCount64())!=0;}
gameplay::Weapon MotionWeapon() {
    if(InShapeForm())return gameplay::Weapon::Melee;   // no bow draw, crossbow or javelin in form
    switch(gamestate::WeaponType()) {
        case D2RVR_TYPE_BOW:return gameplay::Weapon::Bow;
        case D2RVR_TYPE_CROSSBOW:return gameplay::Weapon::Crossbow;
        case D2RVR_TYPE_JAVELIN:case D2RVR_TYPE_THROWING:return gameplay::Weapon::Javelin;   // thrown by hand, swung with the trigger released
        case D2RVR_TYPE_UNARMED:case D2RVR_TYPE_SWORD:case D2RVR_TYPE_AXE:case D2RVR_TYPE_MACE:
        case D2RVR_TYPE_SCEPTER:case D2RVR_TYPE_WAND:case D2RVR_TYPE_STAFF:case D2RVR_TYPE_POLEARM:
        case D2RVR_TYPE_SPEAR:case D2RVR_TYPE_DAGGER:case D2RVR_TYPE_CLAW:
        case D2RVR_TYPE_ORB:return gameplay::Weapon::Melee;
        case D2RVR_TYPE_UNKNOWN:return gameplay::Weapon::Unknown;
        default:return gamestate::WeaponClass()==D2RVR_WEAPON_MELEE?gameplay::Weapon::Melee:gameplay::Weapon::Other;
    }
}
gameplay::MeleeStyle MotionMeleeStyle() {
    if(InShapeForm())return gameplay::MeleeStyle::Short;   // claws
    switch(gamestate::WeaponType()) {
        case D2RVR_TYPE_UNARMED:return gameplay::MeleeStyle::Unarmed;
        case D2RVR_TYPE_DAGGER:case D2RVR_TYPE_CLAW:case D2RVR_TYPE_ORB:return gameplay::MeleeStyle::Short;
        case D2RVR_TYPE_STAFF:case D2RVR_TYPE_POLEARM:case D2RVR_TYPE_SPEAR:return gameplay::MeleeStyle::Long;
        default:return (gamestate::TwoHanded()&D2RVR_TWO_HANDS_ON)?gameplay::MeleeStyle::Long:gameplay::MeleeStyle::OneHand;
    }
}
void ResetSettingsInput() {
    settingsHelp.Clear();settingsPainted.help=settingsHelpVisible=settings::NoHelp;
    hapticPulses.Cancel();
    spellHaptics.Clear();
    panelinput::WorldInput(false);
    gamestate::MenuMap(false);
    motion.Cancel();motionOutput={};interactions.Reset();interactionstate::Cancel();
    meleePoses.Clear();offhandPoses.Clear();CancelShieldCast();++meleeEpoch;bowNeedsWorld=bowHasShot=leftNeedsWorld=leftHasShot=false;
    skel::SetMotionBow(false,false,0);gamecmd::CancelPotion();menuTriggerGate.consumed=true;
    settingsInputGate.Consume();ReleaseUiKey();
    ReleasePanelInput();
}
void OpenSettings() {
    settingsImageReady=false;settingsPainted={};settingsPaintAt=0;settingsPaintRequest=0;settingsRequested={};
    settingsModel.Open(vrsettings::Read());settingsModel.SetNotesPages(settings::ReleaseNotesPages());settingsOpen=true;settingsSession=gamestate::LocalPlayer();
    const auto q=math::Yaw(math::Heading(Q(head.orientation)));
    settingsBase={V(head.position)+math::Rotate(q,{0,-.06f,-1.05f}),q};
    settingsPose=Placed(settingsBase,settingsOffset);settingsHover={};ResetSettingsInput();
    Say("Frame VR settings: panel opened");
}
void CloseSettings() {
    settingsModel.Close();settingsOpen=false;settingsImageReady=false;ResetSettingsInput();
    Say("Frame VR settings: panel closed; held input consumed");
}
void OpenBodyCalibration() {
    if(settingsModel.open)CloseSettings();
    bodyCalibration.Begin();calibrationRelease.Consume();calibrationPrompted=true;
    calibrationSession=gamestate::LocalPlayer();calibrationPaintKey=calibrationRequestedKey=-1;
    settingsOpen=true;settingsImageReady=false;settingsPaintAt=0;settingsPaintRequest=0;
    const auto q=math::Yaw(math::Heading(Q(head.orientation)));
    settingsPose=XrPose({V(head.position)+math::Rotate(q,{0,-.06f,-1.05f}),q});
    ResetSettingsInput();Say("Frame body calibration: started; upright T-pose + both triggers; Right B cancels");
}
// Uploads the picture last asked of the settings painter once it is done;
// true if it was uploaded.
bool TakeSettingsPicture() {
    SettingsPainted painted;
    if(!settingsPaintRequest||settingsAtlasPending||atlasUploads.size()>=3||!settingsPainter.Take(settingsPaintRequest,painted))return false;
    settingsPaintRequest=0;
    const auto uploadStart=Milliseconds();
    const bool uploaded=!painted.pixels.empty()&&UploadShared(panelatlas::Settings,painted.pixels,&settingsAtlasPending);
    static ULONGLONG reportAt=0;const auto now=GetTickCount64();
    if(uploaded&&now>=reportAt) {
        char message[160];std::snprintf(message,sizeof message,"Frame VR settings: page painted in %.2f ms on the background painter, uploaded in %.2f ms",
            painted.paintMs,Milliseconds()-uploadStart);Say(message);reportAt=now+5000;
    }
    return uploaded;
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
        MotionPulse(0,haptics::Category::Interaction,.45f,70000000);MotionPulse(1,haptics::Category::Interaction,.45f,70000000);
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
        settingsOpen=false;settingsImageReady=false;settingsPainted={};ResetSettingsInput();
        Say("Frame body calibration: closed; held input consumed");return;
    }
    const int key=int(bodyCalibration.hint)*100+int(bodyCalibration.progress*20);
    const auto now=GetTickCount64();
    if(key!=calibrationPaintKey&&now>=settingsPaintAt&&!settingsPaintRequest){
        SettingsPaintJob job;job.model=settingsModel;job.calibrating=true;job.body=bodyCalibration;
        settingsPaintRequest=settingsPainter.Submit(std::move(job));calibrationRequestedKey=key;settingsPaintAt=now+100;
    }
    if(TakeSettingsPicture())calibrationPaintKey=calibrationRequestedKey;
}
// Windows Mono audio (HKCU Software\Microsoft\Multimedia\Audio,
// AccessibilityMonoMixState 1) for the title row's warning, read at most every
// 2 s while VR Settings is open; logged when it changes.
bool MonoAudioOn(ULONGLONG now) {
    static bool on=false,told=false;static ULONGLONG checkAt=0;
    if(now<checkAt)return on;
    checkAt=now+2000;
    DWORD value=0,size=sizeof value;
    const bool mono=RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Multimedia\\Audio",L"AccessibilityMonoMixState",
                                 RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS&&value!=0;
    if(mono!=on||!told) {
        told=true;
        Say(mono?"Frame VR settings: Windows Mono audio is on - 3D sounds cannot follow the head; VR Settings warns":
                 "Frame VR settings: Windows Mono audio is off");
    }
    on=mono;return on;
}
void SettingsPointer(const settings::InputGate::Edges& edges,bool grip) {
    if(!settingsModel.open)return;
    const bool monoAudio=MonoAudioOn(GetTickCount64());
    settingsModel.Observe(vrsettings::Read());settingsHover={};pointerVisible=pointerHit=false;
    {   // the character, for rows that only apply to some classes or weapons
        settings::Context context;
        const auto bindings=interactionstate::Read();
        if(interaction::Fresh(bindings,gamestate::LocalPlayer(),GetTickCount64()))context.heroClass=bindings.skillClass;
        const auto weapon=gamestate::WeaponType();
        context.bow=weapon==D2RVR_TYPE_BOW;context.staff=weapon==D2RVR_TYPE_STAFF;
        context.thrown=weapon==D2RVR_TYPE_JAVELIN||weapon==D2RVR_TYPE_THROWING||gamestate::RightThrowingPotion();   // a throwing potion's toss is lifted too
        settingsModel.SetContext(context);
    }
    int helpTarget=settings::NoHelp;
    const auto previousPage=settingsModel.page;
    const bool previousMenu=settingsModel.controllerMenu;
    XrSpaceLocation aim{XR_TYPE_SPACE_LOCATION};
    const auto needed=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
                      XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    if(motionHeadTracked&&XR_SUCCEEDED(pLocateSpace(aimSpace,space,frameState.predictedDisplayTime,&aim))&&(aim.locationFlags&needed)==needed){
        const auto ray=V(aim.pose.position),direction=math::Rotate(Q(aim.pose.orientation),{0,0,-1});math::PanelHit hit;
        pointerHit=math::IntersectPanel(ray,direction,V(settingsPose.position),Q(settingsPose.orientation),settingsWidth,settingsHeight,hit);
        beamLength=pointerHit?hit.distance:1.5f;const auto mid=ray+direction*(beamLength*.5f);
        beamPose=XrPose({mid,math::BeamOrientation(direction,V(head.position)-mid)});pointerVisible=true;
        if(pointerHit){const auto rows=settingsModel.Rows();settingsHover=settings::HitTest(settingsModel.page,hit.u,hit.v,settingsModel.controllerMenu,&rows);dotPose=settingsPose;
            helpTarget=settings::HelpAt(settingsModel.page,hit.u,hit.v,settingsModel.controllerMenu,&settingsModel.Get(),monoAudio,&settingsModel.context);
            const auto p=hit.point+math::Rotate(Q(settingsPose.orientation),{0,0,.004f});dotPose.position={p.x,p.y,p.z};}
    }
    const bool grabbing=GrabPanel(settingsPose,settingsBase,settingsOffset,grip&&pointerVisible,pointerHit,Float(trigger,0)>.2f,aim.pose);
    if(edges.click&&!grabbing&&pointerHit&&settingsImageReady&&settingsPainted.revision==settingsModel.revision){
        if(settingsHover.kind==settings::Kind::Close)CloseSettings();
        else if(settingsHover.kind==settings::Kind::Recenter){ResetHeight();MotionPulse(1,haptics::Category::Interaction,.4f,35000000);}
        else if(settingsHover.kind==settings::Kind::Calibrate&&settingsModel.ready&&!settingsModel.Busy()&&!settingsModel.Dirty())OpenBodyCalibration();
        else if(settingsHover.kind==settings::Kind::TestHaptics&&settingsModel.CanTestHaptics(settingsHover.index)){
            const auto levels=settings::HapticLevels(settingsModel.Get());
            const auto category=haptics::Category(settingsHover.index);
            MotionPattern(category,haptics::Preview(category,settingsHover.delta-1,settingsModel.Get()[settings::HapticShortMelee]>.5f),&levels);
        }
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
    const char* descriptions[]={"Foveated shading unavailable on this GPU/runtime; full-quality rendering.",
        "Foveated shading available. Eye tracking unavailable on this headset/runtime.",
        "Foveated shading available. Gaze inactive or waiting for tracked samples.",
        "Foveated shading available. Eye-tracking samples are active."};
    const auto now=GetTickCount64();
    if(previousPage!=settingsModel.page||previousMenu!=settingsModel.controllerMenu)helpTarget=settings::NoHelp;
    const int help=settingsHelpVisible=settingsHelp.Step(helpTarget,now);
    const auto game=GameVideoOptions(now);
    const int warnings=game.Code()|(monoAudio?4:0);
    // One request at a time, at most every 100 ms as when it was painted here.
    // Each picture is shown when done, even if the pointer moved on meanwhile:
    // the painted state names what it shows, and the next request catches up.
    // The Perf page repaints with each second's numbers; the others never for them.
    const bool performancePage=settingsModel.page==settings::Page::Performance;
    const bool fresh=performance.valid&&now-performanceAt<=2000;
    const auto frameGen=performancePage?FrameGenNow(now):framegen::Readout{};
    const SettingsPicture wanted{settingsModel.revision,hover,status,help,warnings,performancePage&&fresh?performanceSerial:0,
                                 performancePage?game:settings::GameVideo{},frameGen};
    if(!(wanted==settingsPainted)&&now>=settingsPaintAt&&!settingsPaintRequest){
        char resolutionStatus[160];resolution::Describe(resolutionStatus,sizeof resolutionStatus,{width,height});
        settingsPaintRequest=settingsPainter.Submit({settingsModel,settingsHover,performancePage?resolutionStatus:descriptions[status],
                                                    detectedControllers,help,game,monoAudio,fresh?performance:settings::PerformanceReadout{},frameGen});
        settingsRequested=wanted;settingsPaintAt=now+100;
    }
    if(TakeSettingsPicture())settingsPainted=settingsRequested;
}
// An emptied bottle let go of falls into the world as a Box3D body: onto the
// floor under the player, against the game's wall cells around it. It stays
// until its time is up or the player leaves it behind. Drawn only.
// The level's catalogued small props near the player join the same world,
// frozen until a weapon or a moving body reaches them; the renderer is given
// their new matrices while they move (vr/prop_scan.cpp).
bottles::Prop Metres(const props::Seen& s,float upm) {
    bottles::Prop p;props::Placement at;
    if(!props::Decompose(s.world,at)||!(upm>=1))return p;
    p.entity=s.entity;p.kind=s.kind;p.shape=s.shape;p.material=s.material;
    p.p=at.p*(1/upm);p.q=at.q;p.scale=at.scale;
    const auto scaled=[&](math::Vec v){return math::Vec{v.x*at.scale.x/upm,v.y*at.scale.y/upm,v.z*at.scale.z/upm};};
    p.bounds={scaled(s.local.lo),scaled(s.local.hi)};
    p.support=s.support;
    p.support.y/=upm;p.support.x0/=upm;p.support.z0/=upm;p.support.x1/=upm;p.support.z1/=upm;
    p.unitsPerMetre=upm;
    return p;
}
void StepDroppedBottles() {
    const auto& in=interactionInput;const auto& out=interactionOutput;
    const bool bottlesOn=bottlePhysics.load(),propsOn=propPhysics.load();
    if((!bottlesOn&&!propsOn)||!in.session||in.session!=droppedSession) {
        if(droppedBottles)droppedBottles->Reset();
        droppedSession=in.session;propsWanted=false;
        potionFlight={};   // its bottle went with the world; the game's throw is already on its way
        if(!bottlesOn&&!propsOn)return;
    }
    const float upm=stageWorld.upm;
    const bool world=in.floorKnown&&in.stage.known&&upm>=1&&upm<=12;
    const float floor=world?in.stage.anchor.y+(in.floor-in.stage.origin.y):0;
    // Walls ready before the bottle leaves the hand.
    if(bottlesOn&&world&&out.bottleEmpty&&out.bottleHand>=0)
        wallcells::Want({in.session,in.stage.World(in.hand[out.bottleHand].p),floor,upm},in.at);
    if(bottlesOn&&out.discard.active&&world) {
        if(!droppedBottles)droppedBottles=std::make_unique<bottles::World>();
        const auto& d=out.discard;
        const bottles::Release release{in.session,in.time,d.p,d.v,d.w,d.q,d.floor,upm,bottles::Radius(potionModels.Radius(d.code)),d.code};
        const bool dropped=droppedBottles->Ready()&&droppedBottles->Drop(release);
        char line[200];
        if(dropped)std::snprintf(line,sizeof line,"Frame dropped bottles: let go at %.1f m/s, turning %.1f rad/s, radius %.1f cm; %u in the world",
            std::sqrt(math::Dot(d.v,d.v)),std::sqrt(math::Dot(d.w,d.w)),release.radius*100,droppedBottles->Count());
        else std::snprintf(line,sizeof line,"Frame dropped bottles: release refused (physics %s)",droppedBottles->Ready()?"ready":"unavailable");
        Say(line);
    }
    // A thrown potion: a body until it first touches anything. Without the
    // world now (another floor while props move) the engine flies the arc.
    if(bottlesOn&&out.toss.active&&world) {
        if(!droppedBottles)droppedBottles=std::make_unique<bottles::World>();
        const auto& d=out.toss;
        const bottles::Release release{in.session,in.time,d.p,d.v,d.w,d.q,d.floor,upm,bottles::Radius(potionModels.Radius(d.code)),d.code};
        const bool tossed=droppedBottles->Ready()&&droppedBottles->Toss(release,d.id);
        char line[200];
        std::snprintf(line,sizeof line,"Frame potion toss: %s at %.1f m/s, turning %.1f rad/s",tossed?"thrown into the physics world":"physics unavailable, the arc",
            std::sqrt(math::Dot(d.v,d.v)),std::sqrt(math::Dot(d.w,d.w)));
        Say(line);
    }
    // Landed, cancelled or back on the arc: the body goes.
    bottles::Thrown thrown;
    if(droppedBottles&&droppedBottles->Tossed(thrown)&&(!out.flying||out.flightId!=thrown.id)&&thrown.id!=potionFlight.id)droppedBottles->EndToss();
    // Props are looked for around the player while physical melee can reach them.
    const bool wantProps=propsOn&&world&&motionControls.load()&&stageWorld.valid;
    if(wantProps) {
        const auto& w=stageWorld;
        propscan::Want({in.session,{w.hero.x,w.hero.y,w.hero.z},w.hero.y,upm},in.at);
    }
    props::Scan found;
    if(propscan::Latest(found)&&wantProps) {
        if(!droppedBottles)droppedBottles=std::make_unique<bottles::World>();
        bottles::PropScan scan;
        scan.session=found.session?in.session:0;scan.serial=found.serial;scan.unitsPerMetre=found.upm;
        scan.floor=found.upm>=1?found.floor/found.upm:0;
        scan.centre={stageWorld.hero.x/upm,stageWorld.hero.y/upm,stageWorld.hero.z/upm};
        for(unsigned i=0;i<std::min<unsigned>(found.count,props::MaxSeen);++i)
            if(const auto p=Metres(found.seen[i],found.upm);p.entity)scan.props[scan.count++]=p;
        scan.lost=found.lost;scan.lostEntities=found.lostEntities;
        if(droppedBottles->Ready())droppedBottles->Props(scan);
    }
    if(propsWanted&&!wantProps&&droppedBottles)droppedBottles->Props({}); // frozen props go; moving ones finish
    propsWanted=wantProps;
    // Walls reach the world even while nothing moves in it (a trap in hand).
    bottles::Patch patch;
    if(droppedBottles&&wallcells::Latest(patch))droppedBottles->Walls(patch);
    if(!droppedBottles||droppedBottles->Idle()||!in.stage.known)return; // menus and loading hold them still
    std::array<bottles::Collider,bottles::Colliders> weapons{};
    {
        std::lock_guard lock(bottleWeaponMutex);const auto now=GetTickCount64();
        for(int i=0;i<bottles::Colliders;++i) {
            const auto& w=bottleWeapons[i];
            if(w.valid&&now>=w.at&&now-w.at<=100)weapons[i]={true,w.blade.base,w.blade.tip,w.blade.radius};
        }
    }
    // A hand without a weapon capsule (empty, or the off hand without dual
    // wielding or a shield) still pushes, as a fist at the controller.
    for(int h=0;h<2&&motionControls.load();++h) {
        if(weapons[size_t(h)].active||!in.tracked[h]||out.bottleHand==h)continue;
        const auto p=in.stage.World(in.hand[h].p);
        const auto forward=math::Rotate(in.stage.base,math::Rotate(in.hand[h].q,{0,0,-1}));
        weapons[size_t(h)]={true,p-forward*.03f,p+forward*.08f,.045f};
    }
    droppedBottles->Weapons(weapons);
    droppedBottles->Advance(in.time,in.stage.anchor);
    // Draw a thrown potion where this frame's steps left it.
    if(interactionOutput.flying&&interactionOutput.flightId&&droppedBottles->Tossed(thrown)&&thrown.id==interactionOutput.flightId&&!thrown.landed)
        interactionOutput.flight={in.stage.Room(thrown.p),math::Multiply(math::Conjugate(in.stage.base),thrown.q)};
    // A thrown gas or oil potion (#292): drawn where this frame's steps left it,
    // until it first touches anything (or is still flying after PotionFlightMax,
    // or another throw took the world's one thrown body).
    if(potionFlight.id) {
        bottles::Thrown flight;
        const bool mine=droppedBottles->Tossed(flight)&&flight.id==potionFlight.id;
        if(mine&&!flight.landed&&in.time-potionFlight.started<PotionFlightMax) {
            potionFlight.pose={in.stage.Room(flight.p),math::Multiply(math::Conjugate(in.stage.base),flight.q)};potionFlight.shown=true;
        } else {
            float x=0,y=0;const bool cell=mine&&world&&toss::Cell(flight.p,floor,upm,x,y);
            char line[200];std::snprintf(line,sizeof line,"Frame potion throw: the bottle %s after %.2f s%s %.1f,%.1f",
                !mine?"was replaced":flight.landed?"shattered":"was still flying",in.time-potionFlight.started,cell?" at cells":"",x,y);
            Say(line);
            if(mine)droppedBottles->EndToss();
            potionFlight={};
        }
    }
    // A light tick in the hand whose weapon struck a bottle or a prop.
    for(int slot=0;slot<bottles::Colliders;++slot) {
        const float speed=droppedBottles->Struck(slot);const int hand=slot?1:0;const auto now=GetTickCount64();
        if(speed>1&&now-bottleStruckAt[hand]>=80) {
            bottleStruckAt[hand]=now;
            MotionPulse(hand,haptics::Category::Melee,std::clamp(.12f+speed*.02f,.12f,.35f),15000000);
        }
    }
    // Moving props: their matrices for the renderer's next transform pass.
    std::array<bottles::Moved,props::MaxMoving> moved{};
    if(const unsigned n=droppedBottles->Moving(moved)) {
        props::Poses poses;poses.serial=++propPoseSerial;
        for(unsigned i=0;i<n;++i) {
            poses.poses[poses.count].entity=moved[i].entity;
            props::Compose({moved[i].p*moved[i].unitsPerMetre,moved[i].q,moved[i].scale},poses.poses[poses.count++].world);
        }
        propscan::Publish(poses);
    }
    // Each weapon knock is logged; knocks by moving bottles and props are counted.
    std::array<bottles::Knock,props::MaxMoving> knocks{};
    unsigned struck=0,chained=0;
    for(unsigned i=0,n=droppedBottles->Knocks(knocks);i<n;++i) {
        static const char* const by[]{"right hand","left hand","shield"};
        if(knocks[i].slot<0){++chained;continue;}
        char line[160];++struck;
        std::snprintf(line,sizeof line,"Frame props: knocked a %s (%s, %.1f m/s)",propscan::KindName(knocks[i].kind),by[knocks[i].slot%3],knocks[i].speed);
        Say(line);
    }
    if(struck||chained)propscan::CountKnocks(struck,chained);
    bottles::WallRequest want;
    if(droppedBottles->WallsWanted(want))wallcells::Want(want,in.at);
}
void Input() {
    gazeValid=false;
    if(!tracking.data) return;
    if(!valid||state!=XR_SESSION_STATE_FOCUSED) {gazeStability.Reset();ClearInput();return; }
    if(motionControls.load())vrGameplayAt.store(GetTickCount64());
    hapticPulses.Configure(settings::HapticLevels(vrsettings::Read().values));
    XrActiveActionSet active{actionSet,XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};si.countActiveActionSets=1;si.activeActionSets=&active;
    if(pSyncActions(session,&si)!=XR_SUCCESS) {gazeStability.Reset();ClearInput();return;}
    controllerRumbleLease.Update(GetTickCount64(),motionControls.load());
    UpdateControllerLabels();
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
    auto controls=menuControls.Step(modUi?menus::Mode::Blocked:nativeUi?menus::Mode::Native:menus::Mode::World,inputEpoch,physical);
    SampleGaze();
    for(auto upload=atlasUploads.begin();upload!=atlasUploads.end();) {
        if(fence->GetCompletedValue()<upload->fence){++upload;continue;}
        if(atlasStagingFree.size()<AtlasStagingKept)atlasStagingFree.push_back(std::move(upload->staging));
        upload=atlasUploads.erase(upload);
    }
    if(!calibrationPrompted&&!settingsModel.open&&!panelMode&&!spatialMode&&inWorld&&inWorld()){
        const auto config=vrsettings::Read();
        if(config.ready&&config.result!=settings::Result::Saving){calibrationPrompted=true;if(!calibration::Stored(config.values))OpenBodyCalibration();}
    }
    const bool gripMenuPressed=leftMenuButton.Update((controls.buttons&menus::LeftStick)!=0,
        (controls.buttons&menus::LeftBumper)?1.f:0.f);
    // Keep the entire modified click out of wheels and native sort/map actions,
    // including when the grip is released before the stick.
    if(leftMenuButton.Consumed())controls.buttons&=~menus::LeftStick;
    const bool dualBindings=motionControls.load()&&gameplayChain;
    const bool inventoryPressed=(inventoryButton.Update((controls.buttons&menus::X)!=0)&&!dualBindings)||gripMenuPressed;
    const bool menuPressed=menuButton.Update((controls.buttons&menus::Start)!=0);
    const bool settingsChord=!dualBindings&&menus::SettingsChord(physical,false);
    const auto settingsEdges=settingsInputGate.Step(motionHeadTracked,settingsChord,Float(trigger,0),Bool(secondary,0));
    bool settingsOwned=settingsModel.open||calibrationRelease.Step(bodyCalibration.Active(),Float(trigger,0),Float(trigger,1),physical.buttons&(menus::A|menus::B|menus::X|menus::Y|menus::Start));
    if(settingsModel.open&&settingsSession!=gamestate::LocalPlayer()){CloseSettings();settingsOwned=true;}
    if(settingsEdges.chord&&!bodyCalibration.Active()&&!calibrationRelease.blocked){if(settingsModel.open)CloseSettings();else OpenSettings();settingsOwned=true;}
    else if(settingsModel.open&&(settingsEdges.cancel||menuPressed||gripMenuPressed)){
        if(!settingsModel.DismissControllerMenu())CloseSettings();settingsOwned=true;
    }
    settingsOwned=settingsOwned||settingsModel.open;
    bool focused=gameWindow&&GetForegroundWindow()==gameWindow;
    if(!settingsOwned&&(menuPressed||inventoryPressed)&&!focused)focused=FocusGameForUi();
    if(heldKeyScan&&(!focused||GetTickCount64()>=keyReleaseAt))ReleaseUiKey();
    if(!settingsOwned&&!nativeUi&&focused&&menuPressed)PulseUiKey(VK_ESCAPE);
    if(!settingsOwned&&(!nativeUi||gripMenuPressed)&&inventoryPressed&&inWorld&&inWorld()) {
        // Keyboard shortcuts can be gated after switching input layouts or
        // panels. Queue the game's own panel actions on its UI thread instead.
        // This action targets D2R directly and only requires XR focus (checked
        // above), so a desktop terminal cannot silently swallow Left X.
        if(toggleMenus&&toggleMenus())Say("Frame OpenXR: inventory menu toggle queued");
        else Say("Frame OpenXR: inventory menu toggle unavailable");
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
    SettingsPointer(settingsEdges,(controls.buttons&menus::RightBumper)!=0);
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
    motionInput={};motionInput.time=Milliseconds()/1000.;motionInput.head=MotionPose(head);
    motionInput.active=motionControls.load()&&gameplayChain&&motionHeadTracked&&!panelMode&&!spatialMode&&!settingsOwned&&!settingsChord&&!inventoryPressed&&!menuPressed&&inWorld&&inWorld();
    motionInput.handBindings=true;
    motionInput.grip[0]=(physical.buttons&menus::RightBumper)&&!(controls.buttons&menus::RightBumper)?0:s.rightGrip;
    motionInput.grip[1]=(physical.buttons&menus::LeftBumper)&&!(controls.buttons&menus::LeftBumper)?0:s.leftGrip;
    motionInput.cancel=false;
    motionInput.quickCast=false;
    motionInput.trigger=controls.rt/255.f;motionInput.weapon=MotionWeapon();
    motionInput.meleeStyle=MotionMeleeStyle();
    motionInput.equipment=gamestate::HandsKey()^(gamestate::WeaponSet()<<30);
    const auto undo=math::Conjugate(math::Yaw(heading));
    for(int i=0;i<2;++i) {
        XrSpaceVelocity velocity{XR_TYPE_SPACE_VELOCITY};
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION,&velocity};
        const XrSpaceLocationFlags required=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        handVelocityKnown[i]=false;
        if(XR_SUCCEEDED(pLocateSpace(handSpaces[i],space,frameState.predictedDisplayTime,&loc))&&(loc.locationFlags&required)==required) {
            const auto tracked=XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
            motionInput.tracked[i]=(loc.locationFlags&tracked)==tracked;
            motionInput.hand[i]=MotionPose(loc.pose);
            // The runtime's own linear velocity, in the same reference space.
            handVelocityKnown[i]=(velocity.velocityFlags&XR_SPACE_VELOCITY_LINEAR_VALID_BIT)!=0;
            if(handVelocityKnown[i])handVelocity[i]=V(velocity.linearVelocity);
            s.handsValid|=1u<<i;
            const auto p=math::Rotate(undo,V(loc.pose.position)-V(head.position));
            const auto rot=math::Multiply(undo,Q(loc.pose.orientation));
            float* dest=i==0?s.rightHand:s.leftHand;dest[0]=p.x;dest[1]=p.y;dest[2]=p.z;
            float* r=i==0?s.rightRot:s.leftRot;r[0]=rot.x;r[1]=rot.y;r[2]=rot.z;r[3]=rot.w;
        }
    }
    CalibrationInput();
    const auto aimFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
                        XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    for(int h=0;h<2;++h){
        XrSpaceLocation aimed{XR_TYPE_SPACE_LOCATION};
        handAimTracked[h]=XR_SUCCEEDED(pLocateSpace(h?leftAimSpace:aimSpace,space,frameState.predictedDisplayTime,&aimed))&&(aimed.locationFlags&aimFlags)==aimFlags;
        if(handAimTracked[h])handAim[h]=MotionPose(aimed.pose);
    }
    const auto bindingSnapshot=interactionstate::Read();
    uint64_t bindingContext=bindingSnapshot.session^(uint64_t(bindingSnapshot.bindingBank+1)<<48)^(meleeEpoch<<16)^motionInput.equipment;
    uint64_t bindingLayout=0;
    for(const auto& skill:bindingSnapshot.skills)bindingLayout=(bindingLayout^uint32_t(skill.id+2)^uint32_t(skill.owner)^
        (uint64_t(skill.known)<<40)^(uint64_t(skill.available)<<41)^(uint64_t(skill.motion)<<42)^(uint64_t(skill.physical)<<43))*1099511628211ull;
    skills::HandInput handInput;handInput.time=motionInput.time;handInput.context=bindingContext;
    handInput.layout=bindingLayout;
    if(bowvr::SpellReady()&&interaction::Fresh(bindingSnapshot,gamestate::LocalPlayer(),GetTickCount64()))
        for(int i=0;i<skills::Count;++i)if(bindingSnapshot.skills[i].physical&&bindingSnapshot.skills[i].available)
            handInput.independentSlots|=1u<<i;
    handInput.layout^=uint64_t(handInput.independentSlots)<<48;
    handInput.active=motionInput.active&&motionInput.tracked[0]&&motionInput.tracked[1]&&handAimTracked[0]&&handAimTracked[1]&&gamestate::LocalPlayer();
    handInput.occupied=interactionOutput.bottle>=0||interactionOutput.owner[0]==interaction::Owner::Holster||interactionOutput.owner[1]==interaction::Owner::Holster;
    const auto lootNow=GetTickCount64(),lootSession=gamestate::LocalPlayer();
    const bool lootWorld=motionHeadTracked&&!nativeUi&&!settingsOwned&&!settingsChord&&inWorld&&inWorld()&&lootSession;
    // Right stick up switches run / walk with Movement speed's Stick toggle on (owned
    // either way, so it never turns); down, held, shows the loot labels
    // (Show items, Alt). Not while a wheel is open, nor while the wrist map is
    // looked at: its zoom has the stick then (map_zoom.h). A vertical push turns nothing.
    const auto rightStick=rightStickGestures.Step(mapzoom::WorldGestures(lootWorld&&handControls.Wheel()<0,wristMapLooked),
        controls.rx/32767.f,controls.ry/32767.f);
    if(rightStick.owned)controls.rx=controls.ry=0;
    const bool lootHeld=rightStick.showLoot;
    const bool wasLootHeld=lootlabels::display.ShowAll(lootNow);
    lootlabels::display.Refresh(lootSession,lootNow,lootWorld,lootHeld);
    if(lootHeld!=wasLootHeld) {
        Say(lootHeld?"Frame loot labels: Show Items (Alt) held (right stick down)":"Frame loot labels: normal drop/hover behavior");
        if(lootHeld)MotionPulse(0,haptics::Category::Interaction,.45f,45000000);
    }
    motionInput.trigger=controls.rt/255.f;
    for(int h=0;h<2;++h){
        // Double Throw's left trigger throws the left hand's weapon, not its skill.
        handInput.trigger[h]=h?(leftThrowArmed?0:controls.lt/255.f):controls.rt/255.f;handInput.grip[h]=motionInput.grip[h];
        handInput.click[h]=(controls.buttons&(h?menus::LeftStick:menus::RightStick))!=0;
        if(h==1&&motionInput.grip[1]>.6f)handInput.click[h]=false;
        handInput.face[h][0]=(controls.buttons&(h?menus::X:menus::A))!=0;
        handInput.face[h][1]=(controls.buttons&(h?menus::Y:menus::B))!=0;
    }
    handOutput=handControls.Step(handInput);
    {
        // Diagnostics: a bow face press that never became a hand slot was
        // taken earlier (wheel, holster, potion, loot shortcut or a block).
        static bool faceWas[2][2]{};
        for(int h=0;h<2;++h)for(int b=0;b<2;++b) {
            const bool down=handInput.face[h][b];
            const bool faceSlot=handOutput.slot>=0&&handOutput.slot!=skills::TriggerSlot(0)&&handOutput.slot!=shieldSlot;
            if(down&&!faceWas[h][b]&&motionInput.weapon==gameplay::Weapon::Bow&&!faceSlot&&quiverEnabled.load()) {
                char line[240];std::snprintf(line,sizeof line,"Frame bow quiver: raw %s %s press without a face slot (slot=%d wheel=%d occupied=%d active=%d grip=%.2f)",
                    h?"left":"right",b?"secondary":"primary",handOutput.slot,handOutput.wheel,int(handInput.occupied),int(handInput.active),handInput.grip[h]);
                Say(line);
            }
            faceWas[h][b]=down;
        }
    }
    motionInput.palette=handOutput.wheel>=0;
    motionInput.aim=handAim[0];motionInput.aimTracked=handAimTracked[0];
    MotionPointer();
    interactionInput={};interactionInput.time=motionInput.time;interactionInput.session=gamestate::LocalPlayer();
    interactionInput.at=GetTickCount64();interactionInput.epoch=meleeEpoch;interactionInput.active=motionInput.active;
    interactionInput.head={motionInput.head.p,motionInput.head.q};
    interactionInput.snapshot=bindingSnapshot;interactionInput.reply=interactionstate::Status();
    interactionInput.spell=motion.Selection()>=0;interactionInput.wheel=motionInput.palette;interactionInput.cancel=motionInput.cancel;
    interactionInput.quickCast=handOutput.slot>=0||handOutput.spells[0]>=0||handOutput.spells[1]>=0;
    interactionInput.wheelHand=handOutput.wheel;
    // Right stick up switches running / walking only with Movement speed's Stick toggle on
    // (below, off by default). The push is owned either way, so it never snap-turns.
    interactionInput.reservedLeft=(controls.buttons&menus::LeftStick)!=0;interactionInput.trigger=handOutput.wheel==0?controls.lt/255.f:controls.rt/255.f;interactionInput.hover=motionInput.hover;
    const auto hudSettings=vrsettings::Read();
    interactionInput.navigationLocation=vitals::Location(int(hudSettings.values[settings::NavigationLocation]));
    interactionInput.vitalsLocation=vitals::Location(int(hudSettings.values[settings::VitalsLocation]));
    interactionInput.vitalsTilt=hudSettings.values[settings::VitalsTilt];
    interactionInput.criticalAlerts=hudSettings.values[settings::CriticalAlerts]>.5f;
    if(leftStrikesSequence&&((hudSettings.sequence==leftStrikesSequence&&hudSettings.result!=settings::Result::Saving)||interactionInput.at-leftStrikesAt>2000))
        leftStrikesSequence=0;   // saved, refused, or overtaken by another save: the profile's value again
    interactionInput.leftStrikes=hudSettings.ready&&bindingSnapshot.skillClass==settings::heroclass::Druid?
        int(leftStrikesSequence?leftStrikesWanted:hudSettings.values[settings::LeftHandStrikes]):-1;
    {
        uint64_t seenAt=0;bool tired=false;const int gait=projectiles::ObservedGait(seenAt,&tired);   // 1 walking, 2 running, 0 not seen
        const float stick=std::min(1.f,std::hypot(float(controls.lx),float(controls.ly))/32767.f);
        const int movement=runspeed::NormalizePercent(hudSettings.values[settings::RunSpeed]);
        const bool stickToggles=hudSettings.ready&&hudSettings.values[settings::RunWalkToggle]>.5f;
        movementWalks=hudSettings.ready&&stickToggle.Step(interactionInput.session,stickToggles,movement,rightStick.toggleRun);
        projectiles::StickFullRun(stickToggle.FullRun());
        if(stickToggles&&rightStick.toggleRun) {
            Say(movementWalks?"Frame right stick: up - walk (Stick toggle)":"Frame right stick: up - run (Stick toggle)");
            MotionPulse(0,haptics::Category::Interaction,.45f,45000000);
        }
        interactionInput.gaitToggle=gaitSync.Step(interactionInput.session,gait,seenAt,stick,movementWalks,hudSettings.ready,GetTickCount64(),tired);
    }
    interactionInput.leftCasting=handOutput.spells[1]>=0||(handOutput.slot>=0&&handOutput.hand==1);
    interactionInput.leftTrigger=controls.lt/255.f;
    // A thrown bottle lands on the world floor in room space. Without a recent
    // view frame (menus, loading) throwing is off; a release re-belts as before.
    {
        const auto age=interactionInput.at>=stageWorld.at?interactionInput.at-stageWorld.at:0;
        interactionInput.floorKnown=stageWorld.valid&&stageWorld.upm>0&&age<=250;
        if(interactionInput.floorKnown) {
            const auto& w=stageWorld;
            interactionInput.floor=w.origin.y-w.eyeHeight/w.upm;
            // Same mapping as resolveBow in View: room metres to world metres.
            interactionInput.stage={true,w.origin,{w.hero.x/w.upm,(w.hero.y+w.eyeHeight)/w.upm,w.hero.z/w.upm},w.base};
        }
        // Running carries what you throw or let go of.
        if(interactionInput.floorKnown)playerCarry.Update(interactionInput.time,interactionInput.stage.anchor);
        else playerCarry.Reset();
        interactionInput.carry=playerCarry.known?playerCarry.velocity:math::Vec{};
    }
    if(!interactionPaintValid||interactionPaintContext!=interaction::Context(interactionInput.snapshot,interactionOutput,
        interaction::Fresh(interactionInput.snapshot,interactionInput.session,interactionInput.at),controllerLabels))interactionInput.hover=-1;
    for(int i=0;i<2;++i){interactionInput.tracked[i]=motionInput.tracked[i];interactionInput.grip[i]=motionInput.grip[i];
        interactionInput.hand[i]={motionInput.hand[i].p,motionInput.hand[i].q};
        interactionInput.handVelocity[i]=handVelocity[i];interactionInput.velocityKnown[i]=handVelocityKnown[i];}
    // Thrown potions fly in the bottles' physics world while it is on; the
    // engine follows the last one's body and lands it where it first touches.
    interactionInput.physicsThrows=bottlePhysics.load();
    {
        bottles::Thrown thrown;
        interactionInput.tossed=droppedBottles&&droppedBottles->Tossed(thrown)?
            interaction::Input::Tossed{thrown.id,thrown.landed,thrown.p,thrown.q}:interaction::Input::Tossed{};
    }
    interactionOutput=interactions.Step(interactionInput);
    if(interactionOutput.toggleLeftStrikes) {
        const auto current=vrsettings::Read();
        settings::Request request{current.values,current.values,0,current.sequence+1};
        request.values[settings::LeftHandStrikes]=float(interactionInput.leftStrikes==1?0:1);
        request.changed=settings::Changed(request.base,request.values);
        if(!leftStrikesSequence&&current.ready&&request.changed&&vrsettings::Submit(request)) {
            leftStrikesSequence=request.sequence;leftStrikesAt=interactionInput.at;leftStrikesWanted=request.values[settings::LeftHandStrikes];
            Say(leftStrikesWanted>.5f?"Frame wheel: left claw strikes = right trigger skill":"Frame wheel: left claw strikes = own trigger skill");
        } else interactionOutput.feedback=3;   // a save is still running: refuse, never queue a second flip
    }
    if(interactionInput.gaitToggle&&interactionOutput.request.action==interaction::Action::RunWalk) {
        gaitSync.Sent(movementWalks,GetTickCount64());
        Say(movementWalks?"Frame movement: game toggle on run, walking wanted - run / walk toggled":"Frame movement: game toggle on walk, running wanted - run / walk toggled");
    }
    StepDroppedBottles();
    // Quest / level-up notices: queue a card per message, paint the current one.
    {
        const auto now=GetTickCount64();
        const auto& xp=interactionInput.snapshot.experience;
        const bool levelKnown=interaction::Fresh(interactionInput.snapshot,interactionInput.session,interactionInput.at)&&xp.Valid();
        noticeWatch.Step(interactionInput.session,levelKnown,xp.level,gamestate::QuestNotices(),gamestate::NoticePanelOpen(),now,[&](int kind) {
            noticeCards.Push(kind,now);
            char line[80];std::snprintf(line,sizeof line,"Frame notices: %s card queued",
                kind==notice::Quest?"quest":kind==notice::Skills?"skill points":"stat points");Say(line);
        });
        noticeCard=noticeCards.Step(now);
        // A text the UI thread has not read yet is tried again next frame.
        NoticeJob job;
        if(noticeCard.kind>=0&&noticeCard.sequence!=noticeRequestedSequence&&gamestate::NoticeText(noticeCard.kind,job.text,sizeof job.text)) {
            noticePaintRequest=noticePainter.Submit(job);noticeRequestedSequence=noticeCard.sequence;
        }
        std::vector<uint8_t> painted;
        if(noticePaintRequest&&!noticeAtlasPending&&atlasUploads.size()<3&&noticePainter.Take(noticePaintRequest,painted)) {
            if(!painted.empty()&&UploadShared(panelatlas::Notice,painted,&noticeAtlasPending))
                noticePaintedSequence=noticeRequestedSequence;
            noticePaintRequest=0;
        }
    }
    // Blood on the visor follows life bands; a side-to-side scrub at the face
    // wipes what healing has uncovered. A scrubbing hand is not a weapon.
    {
        blood::Input in;
        in.time=interactionInput.time;in.session=interactionInput.session;in.epoch=interactionInput.epoch;
        in.enabled=hudSettings.values[settings::BloodVisor]>.5f;
        in.wipe=hudSettings.values[settings::BloodWipe]>.5f;
        in.active=interactionInput.active&&!interactionOutput.wheel&&!interactionOutput.settings;
        in.fresh=interaction::Fresh(interactionInput.snapshot,interactionInput.session,interactionInput.at);
        in.vitalsKnown=interactionInput.snapshot.vitalsKnown;
        in.health=interactionInput.snapshot.health;in.maxHealth=interactionInput.snapshot.maxHealth;
        in.head=interactionInput.head.p;in.headRotation=interactionInput.head.q;
        for(int h=0;h<2;++h) {
            in.hand[h]=interactionInput.hand[h].p;in.tracked[h]=interactionInput.tracked[h];
            in.busy[h]=interactionOutput.bottleHand==h||handInput.trigger[h]>.3f||(h==0&&motionOutput.bowDrawing);
        }
        bloodOutput=bloodScreen.Step(in);
        for(int h=0;h<2;++h)if(bloodOutput.stroke[h]) {
            MotionPulse(h,haptics::Category::Interaction,bloodOutput.clean?.45f:.18f,bloodOutput.clean?45000000:20000000);
            char line[128];std::snprintf(line,sizeof line,"Frame blood visor: wipe hand=%d coverage=%.2f band=%.2f",h,bloodOutput.coverage,bloodOutput.band);Say(line);
        }
        if(bloodOutput.healed) {
            char line[96];std::snprintf(line,sizeof line,"Frame blood visor: healed, fading to band=%.2f",bloodOutput.band);Say(line);
        }
        // Repaint only when something visibly changes: 25 Hz while splats land
        // or fade, otherwise at most 8 Hz for the slow drips.
        const auto now=GetTickCount64();
        const auto key=blood::PaintKey(bloodOutput.visible,bloodOutput.age,bloodOutput.opacity);
        if(bloodOutput.visible&&key!=bloodPaintKey&&now>=bloodPaintAt&&(!bloodPaintRequest||key!=bloodRequestedKey)) {
            bloodPaintRequest=bloodPainter.Submit({bloodOutput.visible,bloodOutput.age,bloodOutput.opacity});bloodRequestedKey=key;
            bloodPaintAt=now+(bloodOutput.fading?40:125);
        }
        BloodImage painted;
        if(bloodPaintRequest&&!bloodAtlasPending&&atlasUploads.size()<3&&bloodPainter.Take(bloodPaintRequest,painted)) {
            if(!painted.pixels.empty()&&UploadShared(panelatlas::Blood,painted.pixels,&bloodAtlasPending)) {
                bloodPaintKey=bloodRequestedKey;bloodShown=painted.visible;
            }
            bloodPaintRequest=0;
        }
    }
    // Bow path laser color (VR Settings > Combat > Bow path laser); Off keeps the last.
    if(const float hue=hudSettings.values[settings::BowLaser];hue>=0&&hue!=bowLaserHue&&!bowLaserAtlasPending&&atlasUploads.size()<3) {
        if(UploadShared(panelatlas::BowLaser,bowlaser::Paint(hue),&bowLaserAtlasPending))bowLaserHue=hue;
    }
    // Guard strain light (Unleashed shield): blocks strain the guard, light
    // creeps in from the rim; it drains with the shield lowered, or raised with
    // no hit on it for guard_strain_recover_ms.
    {
        const auto now=GetTickCount64();
        strain::Tuning tuning;{std::lock_guard lock(strainTuningMutex);tuning=strainTuning;}
        const float lightOn=tuning.brightness;   // [gameplay] guard_strain_light
        tuning.brightness=1;   // strain itself runs regardless of the light; only the drawing is scaled
        tuning.recoverSeconds=strainRecoverSeconds.load(std::memory_order_relaxed);
        strain::Input in;
        in.time=interactionInput.time;in.session=interactionInput.session;
        in.enabled=motionControls.load();   // melee is Unleashed only
        in.eligible=guardEligible.load()&&now-guardStateAt.load()<=250;
        in.raised=guardRaised.load();
        in.blocks=guardBlockEvents.exchange(0);in.parries=guardParryEvents.exchange(0);
        in.weight=float(guardBlockWeight.exchange(0))/1000.f;   // rank-weighted strain of those blocks
        strainOutput=guardStrain.Step(in,tuning);
        guardBroken=strainOutput.broken;
        strainOutput.alpha*=std::clamp(lightOn,0.f,1.f);strainOutput.visible=strainOutput.alpha>.004f;
        if(strainOutput.brokeNow) {
            MotionPulse(1,haptics::Category::Alert,.6f,120000000);
            Say("Frame guard strain: guard broken; it clears once hits stop or the shield is lowered");
            PlayGuardBreak();
        }
        if(strainOutput.clearedNow)Say("Frame guard strain: cleared");
        // Consume completed images before scheduling more work. Never replace an
        // in-flight request: at <=25 FPS that discarded every animated result.
        if(in.session!=strainPaintSession||!strainOutput.visible) {
            strainPainter.Discard();strainPaintKey=~0ull;strainImageReady=false;
            strainPaintAt=0;strainPaintSession=in.session;
        }
        if(strainOutput.visible) {
            strain::PaintImage painted;
            if(!strainAtlasPending&&atlasUploads.size()<3&&strainPainter.Take(painted)) {
                if(!painted.pixels.empty()&&UploadShared(panelatlas::Strain,painted.pixels,&strainAtlasPending)) {
                    strainPaintKey=painted.key;strainShownReach[0]=painted.reachX;strainShownReach[1]=painted.reachY;
                }
            }
            // Reach: the whole view over the blood's visor, per axis, eyes turned
            // against the head included. 1 where the visor already is the whole
            // view (Quest 3); wider headsets get the light out to their rim.
            // Latched, so tracking noise in a canted eye's turn never repaints.
            static float reach[2]={1,1};
            {
                const auto& lf=views[0].fov;const auto& rf=views[1].fov;
                const math::Fov l{lf.angleLeft,lf.angleRight,lf.angleDown,lf.angleUp},r{rf.angleLeft,rf.angleRight,rf.angleDown,rf.angleUp};
                const auto toHead=math::Conjugate(Q(head.orientation));
                const auto visor=blood::VisorExtent(l,r);
                const auto view=blood::ViewExtent(l,r,math::Multiply(toHead,Q(views[0].pose.orientation)),math::Multiply(toHead,Q(views[1].pose.orientation)));
                if(visor.width>0&&visor.height>0&&view.width>0&&view.height>0) {
                    const float x=strain::Reach(view.width/visor.width),y=strain::Reach(view.height/visor.height);
                    if(std::fabs(x-reach[0])>.01f||std::fabs(y-reach[1])>.01f) {
                        reach[0]=x;reach[1]=y;
                        char message[128];std::snprintf(message,sizeof message,"Frame guard strain: light reaches %.2f x %.2f of the visor to cover the view",x,y);
                        Say(message);
                    }
                }
            }
            const float reachX=reach[0],reachY=reach[1];
            const auto key=strain::PaintKey(strainOutput.edge,strainOutput.alpha,tuning.feather,tuning.color,tuning.density,reachX,reachY);
            if(key!=strainPaintKey&&now>=strainPaintAt&&!strainPainter.Busy()&&
               strainPainter.Submit({strainOutput.edge,strainOutput.alpha,tuning.feather,tuning.color,tuning.density,reachX,reachY}))strainPaintAt=now+40;
        }
    }
    // Low-life heartbeat (Classic and Unleashed): queue the next beat ~80 ms before
    // the current one ends, at the loudness and tempo of the 10 % step of life below
    // VR Settings > Combat > Heartbeat starts at. Above it, Off, dead or stale vitals:
    // the last beat finishes, then silence.
    {
        heartStream.Reap();breakStream.Reap();   // breakStream: the guard-break bone snap
        heartbeat::Tuning heart;std::shared_ptr<const heartbeat::Pcm> source;const char* sourceName=nullptr;
        {std::lock_guard lock(heartMutex);heart=heartTuning;source=heartSource;sourceName=heartSourceName;}
        const auto& vitals=interactionInput.snapshot;
        const bool lifeKnown=vitals.vitalsKnown&&interaction::Fresh(vitals,interactionInput.session,interactionInput.at);
        const float life=lifeKnown?heartbeat::LifeLevel(vitals.health,vitals.maxHealth,int(hudSettings.values[settings::HeartbeatStart])):0.f;
        // Tied to the blood: a scrub that clears the visor fades it out; the next hit
        // (the blood visor's own hit size) brings it back at its step.
        const bool hit=lifeKnown&&heartHealth>=0&&heartHealth-vitals.health>=blood::HitFraction*vitals.maxHealth;
        if(lifeKnown)heartHealth=vitals.health;
        const bool wasHushed=heartHush.On();
        const float target=heartHush.Step(life,bloodOutput.clean,hit);
        if(heartHush.On()!=wasHushed)Say(heartHush.On()?"Frame low-life heartbeat: visor wiped, fading":"Frame low-life heartbeat: struck again, resuming");
        // Its own level: winds down slower than a heal, quieter and slower beat by beat.
        const float level=heartLevel.Step(target,heartAt>0?interactionInput.time-heartAt:0,heart.fadeSeconds);
        heartAt=interactionInput.time;
        if(level>0&&heart.volume>0&&source) {
            if(heartStream.Remaining()<uint64_t(std::max(1,source->rate)*8/100)) {
                heart.volume*=combat::GameVolume();
                auto beat=heartbeat::Beat(*source,level,heart);
                if(!beat.empty()&&heartStream.Queue(source->rate,source->channels,std::move(beat))&&!heartPlaying) {
                    heartPlaying=true;
                    static bool namedSource=false;   // the first start says which beat is playing
                    char line[120];
                    if(!namedSource){namedSource=true;snprintf(line,sizeof line,"Frame low-life heartbeat: started life=%.0f%% (%s)",
                        vitals.maxHealth>0?100*vitals.health/vitals.maxHealth:-1.f,sourceName?sourceName:"?");}
                    else snprintf(line,sizeof line,"Frame low-life heartbeat: started life=%.0f%%",vitals.maxHealth>0?100*vitals.health/vitals.maxHealth:-1.f);
                    Say(line);
                }
            }
        } else if(heartPlaying&&!heartStream.Remaining()){heartPlaying=false;Say("Frame low-life heartbeat: stopped");}
    }
    // Bounded diagnostics distinguish gesture misses from native refusal.
    static ULONGLONG potionReportAt=0;
    if(interactionOutput.bottleHand>=0&&GetTickCount64()>=potionReportAt) {
        const auto& hand=interactionInput.hand[interactionOutput.bottleHand];
        const auto mouth=interactionInput.head.p+math::Rotate(interactionInput.head.q,{0,-.10f,-.08f});
        const auto delta=potions::Opening(hand.p,hand.q)-mouth;
        char line[200];std::snprintf(line,sizeof line,"Frame potion sip: hand=%d neck_gap=%.3f m up=%.3f ready=%d empty=%d",
            interactionOutput.bottleHand,std::sqrt(math::Dot(delta,delta)),math::Rotate(potions::HeldRotation(hand.q),{0,1,0}).y,
            potions::AtMouth(hand.p,hand.q,interactionInput.head.p,interactionInput.head.q),interactionOutput.bottleEmpty);
        Say(line);potionReportAt=GetTickCount64()+2000;
    }
    if(interactionOutput.settings){OpenSettings();settingsOwned=true;interactionInput.active=motionInput.active=false;}
    {
        // Minimap zoom: while the wrist / palm map is shown (the wrist turned
        // toward you, or the palm raised) and you look at it, the right stick's
        // up / down zooms it and does nothing else; left / right still turns,
        // except during a zoom push. Each new game starts at 1x.
        static uint64_t mapZoomSession=0;
        if(mapzoom::NewGame(mapZoomSession,interactionInput.session)&&mapZoom.Zoom()!=mapzoom::Min) {
            mapZoom.NewGame();Say("Frame map zoom: new game, back to 1.00x");
        }
        const bool shown=wristMapShown&&mapSupported&&interaction::NavigationWanted(interactionInput,interactionOutput,interactionInput.at);
        wristMapLooked=shown&&mapzoom::Looking(interactionInput.head.p,interactionInput.head.q,wristMapCenter,wristMapLooked);
        mapzoom::Input in;in.time=interactionInput.time;in.looking=MinimapLooked();
        in.stick=controls.ry/32767.f;in.x=controls.rx/32767.f;
        // Map view = Full map: zoomed in from the game's widest view, to 16x; the game is asked
        // for the zoom that keeps it sharp (wrist_full_map.h). Off: nothing asked, the hooks idle.
        const bool fullMap=MapFullOn();
        fullmap::g_on.store(fullMap,std::memory_order_relaxed);
        in.max=fullMap?mapzoom::FullMax:mapzoom::Max;
        const auto zoom=mapZoom.Step(in);
        fullmap::g_wanted.store(fullMap?fullmap::Base*zoom.zoom:0.f,std::memory_order_relaxed);
        if(zoom.owned)controls.ry=0;
        if(zoom.turnOwned)controls.rx=0;
        if(zoom.tick)MotionPulse(0,haptics::Category::Interaction,zoom.tick==2?.35f:.15f,zoom.tick==2?30000000:12000000);
        // One line a gesture: a start at most every half second, and its stop.
        const uint64_t tick=GetTickCount64();
        if(zoom.started&&tick>=mapZoomLogAt){
            char line[80];std::snprintf(line,sizeof line,"Frame map zoom: start at %.2fx (%s)",zoom.zoom,in.stick>0?"in":"out");Say(line);
            mapZoomLogAt=tick+500;mapZoomLogged=true;
        }
        if(zoom.stopped&&mapZoomLogged){
            char line[160];
            if(fullMap)std::snprintf(line,sizeof line,"Frame map zoom: stop at %.2fx%s (full map: game zoom %.3f asked, %.3f drawn)",zoom.zoom,wristMapLooked?"":" (looked away)",
                                     fullmap::GameStep(fullmap::Base*zoom.zoom),fullmap::g_applied.load(std::memory_order_relaxed));
            else std::snprintf(line,sizeof line,"Frame map zoom: stop at %.2fx%s",zoom.zoom,wristMapLooked?"":" (looked away)");
            Say(line);
            mapZoomLogged=false;
        }
    }
    gamestate::MenuMap(mapSupported&&interaction::MapWanted(interactionInput,interactionOutput,interactionInput.at));
    if(interactionOutput.inventory&&toggleMenus)toggleMenus();
    if(handOutput.wheel>=0&&!interactionOutput.wheel){handControls.Close();handOutput={};}
    if(interactionOutput.cancelPending)interactionstate::Cancel();
    if(interactionOutput.request.action==interaction::Action::FeedMerc&&stageWorld.valid&&stageWorld.upm>0) {
        // The engine already landed the bottle in world metres; add the
        // rendered player and scale for the game thread's grid conversion.
        auto& r=interactionOutput.request;const auto& w=stageWorld;
        r.player=w.hero*(1/w.upm);r.unitsPerMetre=w.upm;
    }
    if(interactionOutput.request.action!=interaction::Action::None)interactionstate::Request(interactionOutput.request);
    if(interactionOutput.feedback)MotionPulse(interactionOutput.feedbackHand,interactionOutput.feedback==4?haptics::Category::Alert:haptics::Category::Interaction,
        interactionOutput.feedback==2?.65f:.25f,interactionOutput.feedback>=3?100000000:35000000);
    motionInput.palette=false; // Dedicated interaction owner handles the new wheel/belt.
    motionInput.active=motionInput.active&&!interactionOutput.block&&!bloodOutput.block[0]&&!bloodOutput.block[1];
    const auto& quickSnapshot=interactionInput.snapshot;
    const bool quickFresh=interaction::Fresh(quickSnapshot,interactionInput.session,interactionInput.at);
    const auto& rightSkill=quickSnapshot.skills[skills::TriggerSlot(0)];
    const auto gestureSettings=vrsettings::Read().values;
    const int selected=rightSkill.known&&rightSkill.id>=0?skills::TriggerSlot(0):-1;
    if(motion.Selection()!=selected)motion.Select(selected);
    motionInput.selectedSkill=rightSkill.id;motionInput.selectedOwner=rightSkill.owner;
    motionInput.selectedAvailable=quickFresh&&rightSkill.known&&rightSkill.available&&rightSkill.motion;
    shieldSkill=quickSnapshot.skills[shieldSlot];
    motionInput.bowSlot=gestureSettings[settings::SkillGestures]>.5f&&shieldSkill.known&&shieldSkill.available&&shieldSkill.motion&&weaponskills::Bow(shieldSkill.id)?shieldSlot:-1;
    motionInput.bowSkill=motionInput.bowSlot>=0?shieldSkill.id:-1;motionInput.bowOwner=shieldSkill.owner;
    int requested=handOutput.slot;
    {
        // A held face button whose skill is a quiver arrow loads, swaps or
        // unloads it once per press, instead of a native animated cast.
        const bool bow=quiverEnabled.load()&&motionInput.active&&gestureSettings[settings::SkillGestures]>.5f&&
            motionInput.weapon==gameplay::Weapon::Bow&&quickFresh;
        // A thrown weapon loads javelin skills the same way (#224).
        const bool thrown=quiverEnabled.load()&&motionInput.active&&gestureSettings[settings::SkillGestures]>.5f&&
            motionInput.weapon==gameplay::Weapon::Javelin&&quickFresh;
        // Raw buttons, not the native single-owner cast: loading works while
        // the right trigger holds the string, and with the bow hand gripped.
        const auto press=handOutput.wheel<0&&!handInput.occupied?skills::QuiverFace(handInput,[&](int slot){
            return quickSnapshot.skills[slot].known?quickSnapshot.skills[slot].id:-1;}):skills::FacePress{};
        const bool face=press.slot>=0;
        const auto& held=quickSnapshot.skills[face?press.slot:0];
        const auto step=quiver.Step(gamestate::LocalPlayer(),press.slot,held.id,held.owner,held.missile,
            (bow||thrown)&&face&&held.known&&held.available,thrown);
        if(face&&press.slot!=quiverReportedSlot&&(motionInput.weapon==gameplay::Weapon::Bow||motionInput.weapon==gameplay::Weapon::Javelin)) {
            // Every bow face press says who owns it and why, so a headset
            // report can be matched to the exact eligibility check.
            char line[320];std::snprintf(line,sizeof line,
                "Frame bow quiver: press slot=%d hand=%d skill=%d owner=%d missile=%d known=%d available=%d enabled=%d active=%d gestures=%d fresh=%d -> %s",
                press.slot,press.hand,held.id,held.owner,held.missile,int(held.known),int(held.available),int(quiverEnabled.load()),
                int(motionInput.active),int(gestureSettings[settings::SkillGestures]>.5f),int(quickFresh),
                step.change==weaponskills::Quiver::Change::Loaded?"loaded":step.change==weaponskills::Quiver::Change::Unloaded?"unloaded":
                step.consumed?"held":"native");
            Say(line);
        }
        quiverReportedSlot=press.slot;
        // The same physical press must not also start a native face cast;
        // a trigger draw already in progress keeps going.
        if(step.consumed&&requested>=0&&requested!=skills::TriggerSlot(0)&&skills::Hand(requested)==press.hand){requested=-1;handOutput.interact=false;}
        if(step.change!=weaponskills::Quiver::Change::None)
            MotionPulse(press.hand,haptics::Category::Interaction,step.change==weaponskills::Quiver::Change::Loaded?.45f:.25f,35000000);
        quiverArrow={};
        if(bow) {
            // A loaded skill wins; otherwise a quiver skill on the left trigger
            // (the bow's gesture skill) uses the same physical release.
            const auto& loaded=quiver.Current();
            if(loaded.Active()&&weaponskills::Loadable(loaded.skill,loaded.missile,false))quiverArrow=loaded;
            else if(shieldSkill.known&&shieldSkill.available&&weaponskills::QuiverSkill(shieldSkill.id)&&weaponskills::QuiverMissile(shieldSkill.missile))
                quiverArrow={shieldSkill.id,shieldSkill.owner,shieldSkill.missile};
        } else if(thrown) {
            // A loaded javelin skill wins; otherwise a javelin skill on the right
            // trigger (which arms the hand's throw) rides the throw.
            const auto& loaded=quiver.Current();
            if(loaded.Active()&&weaponskills::Loadable(loaded.skill,loaded.missile,true))quiverArrow=loaded;
            else if(rightSkill.known&&rightSkill.available&&weaponskills::Loadable(rightSkill.id,rightSkill.missile,true))
                quiverArrow={rightSkill.id,rightSkill.owner,rightSkill.missile};
        }
        if(quiverArrow.Active()){motionInput.bowSlot=-1;motionInput.bowSkill=-1;}
        // Double Throw (#224): with a throwing weapon in each hand, the left
        // trigger arms the left hand's own throw instead of its skill.
        leftThrowArmed=thrown&&motionOutput.javelinReady&&quiverArrow.skill==weaponskills::DoubleThrow&&
            dualwield::ThrownPair(skel::HeroClass(),gamestate::MeleeEquipment());
        if(leftThrowArmed&&requested==shieldSlot){requested=-1;handOutput.interact=false;}
    }
    const bool requestedKnown=requested>=0&&quickFresh&&quickSnapshot.skills[requested].known&&quickSnapshot.skills[requested].available;
    handOutput.interact=requestedKnown&&quickSnapshot.skills[requested].id==357;
    const bool quick=requested>=0&&requested!=skills::TriggerSlot(0);
    motionInput.quickCast=quick||handOutput.interact;
    motionInput.quickSkill=quick&&requestedKnown&&!handOutput.interact?requested:-1;
    motionInput.trigger=requested==skills::TriggerSlot(0)&&!handOutput.interact?controls.rt/255.f:0;
    if(quick||handOutput.interact){motionInput.aim=handAim[handOutput.hand];motionInput.aimTracked=handAimTracked[handOutput.hand];}
    castHand=quick||handOutput.interact?handOutput.hand:0;
    motionInput.skillContext=bindingContext^bindingLayout;
    // In form the claws are the only hands: no shield, hand throw or trap toss.
    const bool shapeForm=InShapeForm();
    motionInput.shieldEquipped=gamestate::LeftShield()&&::melee::ShieldReady()&&!shapeForm;
    motionInput.leftThrow=leftThrowArmed&&!shapeForm;motionInput.leftTrigger=controls.lt/255.f;
    motionInput.throwLift=gestureSettings[settings::ThrowLift];
    // A throwing potion in the right hand, with Attack or Throw on the right
    // trigger, is tossed the same way: the game's own Throw lands it at the spot.
    const bool tossReady=!shapeForm&&gestureSettings[settings::SkillGestures]>.5f&&quickFresh&&rightSkill.known&&rightSkill.available;
    motionInput.potion=tossReady&&gamestate::RightThrowingPotion()&&weaponskills::PotionThrow(rightSkill.id);
    motionInput.toss=tossReady&&(toss::Skill(rightSkill.id)||motionInput.potion);
    // With Bottle physics and the potion's model cached, it leaves the hand as a
    // physical bottle; the press waits for it to shatter (StepDroppedBottles).
    const uint32_t potionCode=motionInput.potion?gamestate::RightThrowingPotionCode():0u;
    const bool potionBottle=potionCode&&bottlePhysics.load()&&potionModels.Radius(potionCode)>0;
    motionOutput=motion.Step(motionInput);motionAt=GetTickCount64();
    if(motionOutput.tossHeld)tossLanding={};   // a new trap in hand: nothing placed yet
    const auto& tossStage=interactionInput.stage;
    const bool tossWorld=interactionInput.floorKnown&&tossStage.known&&stageWorld.upm>=1&&stageWorld.upm<=12;
    const float tossFloor=tossStage.anchor.y+(interactionInput.floor-tossStage.origin.y);
    // With bottle physics, a trap in hand readies the physics world here and
    // its walls, so the throw can be traced through them when it leaves.
    if(motionOutput.tossHeld&&tossWorld&&bottlePhysics.load()) {
        if(!droppedBottles)droppedBottles=std::make_unique<bottles::World>();
        const auto session=gamestate::LocalPlayer();
        if(droppedBottles->Ready()&&droppedBottles->Prepare(session,tossFloor,stageWorld.upm,tossStage.anchor))
            wallcells::Want({session,tossStage.anchor,tossFloor,stageWorld.upm},GetTickCount64());
    }
    if(motionOutput.tossRelease) {
        // Where the arc first meets anything in the physics world (a wall, a
        // table, a prop or the floor), else where it reaches the floor under
        // the player at release (world metres). Running carries it along.
        const auto carry=playerCarry.known?playerCarry.velocity:math::Vec{};
        float x=0,y=0;bool landed=false,traced=false,flying=false;
        if(tossWorld) {
            math::Vec hit{};
            const auto worldP=tossStage.World(motionOutput.tossOrigin),worldV=toss::WorldVelocity(tossStage,motionOutput.tossVelocity,carry);
            traced=bottlePhysics.load()&&droppedBottles&&droppedBottles->Trace(worldP,worldV,toss::Radius,interaction::ThrowMaxFlight,hit)&&
                toss::Cell(hit,tossFloor,stageWorld.upm,x,y);
            landed=traced||toss::Landing(tossStage,tossFloor,stageWorld.upm,motionOutput.tossOrigin,motionOutput.tossVelocity,x,y,carry);
            // A gas or oil potion leaves the hand as its own bottle (#292), held as
            // the hand held it, tumbling end over end along its throw.
            if(potionBottle&&traced&&droppedBottles&&droppedBottles->Ready()) {
                const auto held=potions::HeldRotation(motionInput.hand[0].q);
                const auto across=math::Cross(worldV,math::Vec{0,1,0});const float spin=gameplay::Length(across);
                const bottles::Release release{gamestate::LocalPlayer(),interactionInput.time,tossStage.World(potions::Center(motionOutput.tossOrigin,motionInput.hand[0].q)),
                    worldV,spin>1e-4f?across*(6.f/spin):math::Vec{},math::Multiply(tossStage.base,held),tossFloor,stageWorld.upm,
                    bottles::Radius(potionModels.Radius(potionCode)),potionCode};
                const uint64_t id=(uint64_t(1)<<62)|++potionFlightSerial;
                flying=droppedBottles->Toss(release,id);
                if(flying){potionFlight={id,release.session,potionCode,interactionInput.time};bowvr::ExpectPotion(GetTickCount64());}
            }
        }
        if(landed)tossLanding={rightSkill.id,x,y,motionAt,gamestate::LocalPlayer()};
        char line[240];std::snprintf(line,sizeof line,"Frame toss: skill=%d%s thrown at %.1f m/s (running %.1f m/s), %s %.1f,%.1f",rightSkill.id,
            flying?" (throwing potion, its bottle flies)":motionInput.potion?" (throwing potion)":"",
            gameplay::Length(motionOutput.tossVelocity),gameplay::Length(carry),
            !landed?"no landing (room unknown), cast where you point":traced?"first hit at cells":"lands on the floor at cells",x,y);
        Say(line);
    }
    motionOutput.shieldContact=motionInput.active&&!motionOutput.block&&!motionInput.quickCast&&motionInput.trigger<.2f&&
        motionInput.shieldEquipped&&gestureSettings[settings::SkillGestures]>.5f&&quickFresh&&shieldSkill.available&&shieldSkill.motion&&shieldSkill.id==weaponskills::Smite;
    if(motionOutput.skill>=0&&(!quickFresh||!quickSnapshot.skills[motionOutput.skill].available))motionOutput.skill=-1;
    // Bow skills leave the bow: the arrow is in the left hand's model, so the
    // target, the lock and the shot follow the bow whichever button casts. A
    // drawn string (or the shot just loosed) keeps its own line. Only with VR
    // Settings > Aim > Bow skills go to the aimed monster: Off, a bow skill
    // aims along its button's hand, as before the aim rules (#159 review).
    if(settings::LockBowSkills(gestureSettings[settings::SpellLock])&&motionInput.weapon==gameplay::Weapon::Bow&&motionOutput.skill>=0&&motionInput.tracked[1]&&
       weaponskills::Bow(quickSnapshot.skills[motionOutput.skill].id)&&gameplay::Length(motionOutput.rayOrigin-motionInput.hand[1].p)>.05f) {
        motionOutput.rayOrigin=motionInput.hand[1].p;motionOutput.rayDirection=gameplay::BowForward(motionInput.hand[1]);motionOutput.rayValid=true;
    }
    {
        crossbow::Pose pose;
        const bool ready=motionControls.load()&&motionInput.weapon==gameplay::Weapon::Crossbow&&bowvr::MotionReady()&&
            skel::CrossbowPose(pose)&&crossbow::Fresh(pose,motionAt,gamestate::LocalPlayer(),motionInput.equipment,bowWorldEpoch);
        const int slot=motionOutput.skill;
        const bool known=slot>=0&&slot<skills::Count&&quickFresh&&quickSnapshot.skills[slot].known&&quickSnapshot.skills[slot].available;
        const bool firing=gameplay::RouteCrossbow(motionInput,motionOutput,castHand==0,ready,known,known?quickSnapshot.skills[slot].id:-1);
        if(firing&&!crossbowFire)++crossbowActivation;
        crossbowFire=firing;
    }
    const bool shieldValid=motionInput.active&&!motionOutput.block&&!motionInput.quickCast&&motionInput.trigger<.2f&&gestureSettings[settings::SkillGestures]>.5f&&
        motionInput.shieldEquipped&&shieldSkill.available&&shieldSkill.motion&&shieldSkill.id==weaponskills::Smite&&
        weaponskills::Current(shieldCast.context,LiveShieldContext(),shieldCast.at,motionAt);
    if(!shieldValid){
        if(shieldCast.issued&&motionAt>=shieldCast.at&&motionAt-shieldCast.at<=1500)shieldCast.cancelled=true;
        else shieldCast={};
        shieldPulse.Cancel();
    }
    else if(!shieldCast.issued) {
        if(motionAt-shieldCast.at<=150&&shieldPulse.Start(shieldSlot,weaponskills::Smite,shieldSkill.owner,motionInput.skillContext,motionInput.time))shieldCast.issued=true;
        else {shieldCast={};shieldPulse.Cancel();}
    }
    if(shieldCast.issued&&!shieldCast.cancelled) {
        motionOutput.skill=shieldPulse.Get(shieldSlot,weaponskills::Smite,shieldSkill.owner,motionInput.skillContext,motionInput.time);
        motionOutput.meleeContact=false;
        // Keep observing the left stroke during native wind-up. Resetting it
        // here would allow the same continuous swing to become another bash.
    }
    // Druid shapeshift gestures. The performance casts the bound shape skill
    // through its own slot's button; the game owns the transformation, mana
    // and the return to human form (the same skill cast again).
    {
        shapeshift::Input in;
        in.time=interactionInput.time;in.session=interactionInput.session;in.epoch=interactionInput.epoch;
        in.active=motionInput.active&&!motionOutput.block&&!motionInput.quickCast&&quickFresh&&
            gestureSettings[settings::SkillGestures]>.5f&&!interactionOutput.wheel&&!interactionOutput.settings;
        in.head=interactionInput.head.p;in.headRotation=interactionInput.head.q;
        for(int h=0;h<2;++h) {
            in.hand[h]=interactionInput.hand[h].p;in.tracked[h]=interactionInput.tracked[h];
            in.handVelocity[h]=interactionInput.handVelocity[h];in.velocityKnown[h]=interactionInput.velocityKnown[h];
            in.busy[h]=interactionOutput.bottleHand==h||handInput.trigger[h]>.3f||(h==0&&motionOutput.bowDrawing)||bloodOutput.block[h];
        }
        const auto shape=shapeshiftGesture.Step(in);
        if(shape.dropped) {
            char line[160];std::snprintf(line,sizeof line,"Frame shapeshift: dropped at %s: %s",shape.stage,shape.dropped);Say(line);
        }
        if(shape.cast!=shapeshift::Form::None) {
            const int wanted=shape.cast==shapeshift::Form::Werewolf?weaponskills::Werewolf:weaponskills::Werebear;
            int slot=-1;
            for(int i=0;i<skills::Count;++i)if(quickSnapshot.skills[i].known&&quickSnapshot.skills[i].id==wanted){slot=i;break;}
            char line[160];
            if(slot<0) {
                std::snprintf(line,sizeof line,"Frame shapeshift: %s gesture, skill %d not assigned to a button",shape.cast==shapeshift::Form::Werewolf?"Werewolf":"Werebear",wanted);Say(line);
            }
            else if(!quickSnapshot.skills[slot].available) {
                std::snprintf(line,sizeof line,"Frame shapeshift: %s gesture, skill unavailable on slot %d",shape.cast==shapeshift::Form::Werewolf?"Werewolf":"Werebear",slot);Say(line);
            }
            // Alternate-bank slots need the modifier prepared first: hold like Smite.
            else if(shapePulse.Start(slot,wanted,quickSnapshot.skills[slot].owner,motionInput.skillContext,motionInput.time,skills::Alternate(slot)?.180:.065)) {
                shapeSlot=slot;shapeId=wanted;shapeOwner=quickSnapshot.skills[slot].owner;
                MotionPulse(0,haptics::Category::Spell,.6f,60000000);MotionPulse(1,haptics::Category::Spell,.6f,60000000);
                std::snprintf(line,sizeof line,"Frame shapeshift: %s gesture cast via slot %d",shape.cast==shapeshift::Form::Werewolf?"Werewolf":"Werebear",slot);Say(line);
            }
        }
        if(shapeSlot>=0) {
            const int pulse=shapePulse.Get(shapeSlot,shapeId,shapeOwner,motionInput.skillContext,motionInput.time);
            if(pulse<0)shapeSlot=shapeId=shapeOwner=-1;
            else if(motionOutput.skill<0&&quickFresh&&quickSnapshot.skills[pulse].available)motionOutput.skill=pulse;
        }
    }
    if(motionOutput.nativeBowRelease&&motionInput.bowSkill==weaponskills::Strafe)volleyRequestedAt=motionAt;
    // Gesture skills on a trigger are labelled GESTURE on the hand wheel and
    // say how in the hand's cue, as Smite on a shield and arrow skills on a bow
    // do: a strike skill (strike_skills.h) casts with the hand's landed strikes
    // (the right hand's weapon or claw; the left's second weapon or claw unless
    // left_hand_strikes follows the right trigger), and a trap or bomb on the
    // right trigger is thrown to place (#224).
    const bool strikeWeapon=motionInput.weapon==gameplay::Weapon::Melee||motionInput.weapon==gameplay::Weapon::Javelin;
    const auto strikes=[&](const auto& skill){
        return strikeWeapon&&strikeskills::Armed(gestureSettings[settings::SkillGestures]>.5f,quickFresh,skill.known,skill.available,skill.strike,skill.id)>=0;
    };
    const bool leftStrikes=strikeskills::StrikeTrigger(1,strikeskills::LeftChoice(gestureSettings[settings::LeftHandStrikes]))==1&&
        (InShapeForm()||dualwield::Eligible(skel::HeroClass(),gamestate::MeleeEquipment()))&&strikes(quickSnapshot.skills[shieldSlot]);
    interaction::SkillCue cue;
    cue.slot=motionInput.quickSkill>=0&&castHand==0?motionInput.quickSkill:motion.Selection();
    cue.temporary=motionInput.quickSkill>=0&&castHand==0;
    if(cue.slot>=0&&cue.slot<skills::Count&&quickFresh) {
        const auto& skill=quickSnapshot.skills[cue.slot];cue.id=skill.id;cue.owner=skill.owner;
        cue.known=skill.known;cue.available=skill.known&&skill.available&&skill.id>=0;
        std::snprintf(cue.name,sizeof cue.name,"%s",skill.known?skill.name:"Unknown skill");
        std::snprintf(cue.hint,sizeof cue.hint,"%s",cue.available?(cue.temporary?"Right hand shortcut":"Right hand: point & trigger"):"Assignment unavailable");
        if(cue.available&&!cue.temporary&&cue.slot==skills::TriggerSlot(0)&&(motionInput.toss||strikes(skill))) {
            cue.gesture=true;std::snprintf(cue.hint,sizeof cue.hint,"%s",motionInput.toss?"Throw to place":"Swing to strike");
        }
    } else if(cue.slot<0) {
        cue.known=cue.available=true;
        std::snprintf(cue.name,sizeof cue.name,"%s",motionInput.weapon==gameplay::Weapon::Bow?"Basic Arrow":"Basic Attack");
        std::snprintf(cue.hint,sizeof cue.hint,"%s",motionInput.weapon==gameplay::Weapon::Bow?"Draw & release":motionInput.weapon==gameplay::Weapon::Javelin?"Swing / throw":"Swing / trigger");
    } else {
        std::snprintf(cue.name,sizeof cue.name,"Skill unavailable");std::snprintf(cue.hint,sizeof cue.hint,"Refresh assignments");
    }
    if(quiverArrow.Active()&&!cue.temporary&&(motionInput.weapon==gameplay::Weapon::Bow||motionInput.weapon==gameplay::Weapon::Javelin)) {
        const bool javelin=motionInput.weapon==gameplay::Weapon::Javelin;
        const char* name=nullptr;
        for(const auto& skill:quickSnapshot.skills)if(skill.known&&skill.id==quiverArrow.skill&&skill.owner==quiverArrow.owner){name=skill.name;break;}
        cue.id=quiverArrow.skill;cue.owner=quiverArrow.owner;cue.known=cue.available=true;
        std::snprintf(cue.name,sizeof cue.name,"%s",name?name:javelin?"Javelin skill":"Skill arrow");
        std::snprintf(cue.hint,sizeof cue.hint,"%s",javelin?(quiver.Current().Active()?"Loaded: throw":"Throw"):
            quiver.Current().Active()?"Loaded: draw & release":"Draw & release");
        cue.gesture=false;   // the loaded skill, not the trigger's
    }
    cue.busy=(shieldCast.issued&&!shieldCast.cancelled)||(cue.id==weaponskills::Strafe&&quickFresh&&quickSnapshot.actionKnown&&quickSnapshot.actionMode==16&&
        volleyRequestedAt&&motionAt>=volleyRequestedAt&&motionAt-volleyRequestedAt<10000);
    if(cue.busy)std::snprintf(cue.hint,sizeof cue.hint,"%s",cue.id==weaponskills::Strafe?"Volley active":"Shield bash active");
    if(shieldFeedbackAt&&motionAt-shieldFeedbackAt<1200&&cue.id==weaponskills::Smite&&shieldRefused)
        std::snprintf(cue.hint,sizeof cue.hint,"Action refused");
    if(cue!=previousCue){cueChangedAt=motionAt;previousCue=cue;}
    cue.expanded=motionAt-cueChangedAt<1800||cue.busy||!cue.available||cue.motionUnavailable;
    interactionOutput.cue=cue;
    auto& leftCue=interactionOutput.leftCue;
    leftCue.slot=motionInput.quickSkill>=0&&castHand==1?motionInput.quickSkill:shieldSlot;
    leftCue.temporary=motionInput.quickSkill>=0&&castHand==1;
    const auto& leftSkill=quickSnapshot.skills[leftCue.slot];leftCue.id=leftSkill.id;leftCue.owner=leftSkill.owner;
    leftCue.known=quickFresh&&leftSkill.known;leftCue.available=leftCue.known&&leftSkill.available;
    std::snprintf(leftCue.name,sizeof leftCue.name,"%s",leftCue.known?leftSkill.name:"Unavailable");
    leftCue.gesture=!leftCue.temporary&&leftCue.available&&((gestureSettings[settings::SkillGestures]>.5f&&leftSkill.motion&&
        ((weaponskills::Bow(leftSkill.id)&&motionInput.weapon==gameplay::Weapon::Bow)||(leftSkill.id==weaponskills::Smite&&motionInput.shieldEquipped)))||leftStrikes);
    std::snprintf(leftCue.hint,sizeof leftCue.hint,"%s",leftCue.gesture?(leftStrikes?"Swing to strike":weaponskills::Hint(leftSkill.id,
        motionInput.weapon==gameplay::Weapon::Bow,motionInput.shieldEquipped)):leftCue.temporary?"Left hand shortcut":"Left hand: point & trigger");
    leftCue.expanded=leftCue.temporary;
    leftCue.busy=shieldCast.issued&&!shieldCast.cancelled;
    if(leftThrowArmed&&!leftCue.temporary) {
        // Double Throw: the left trigger throws the left hand's weapon.
        leftCue.id=quiverArrow.skill;leftCue.owner=quiverArrow.owner;leftCue.known=leftCue.available=true;
        std::snprintf(leftCue.name,sizeof leftCue.name,"%s",cue.name);std::snprintf(leftCue.hint,sizeof leftCue.hint,"Throw");
    }

    panelinput::WorldInput(motionInput.active&&motionInput.tracked[0]&&motionInput.tracked[1]&&motionInput.aimTracked);
    if(!motionInput.active||motionOutput.block||!motionOutput.rayValid)handOutput.interact=false;
    if(motionInput.quickCast&&(motionOutput.block||!motionOutput.rayValid))handControls.Close();
    const uint32_t hidden=interactionOutput.stowed?3u:journalMode?2u:interactionOutput.bottleHand>=0?1u<<interactionOutput.bottleHand:0;
    skel::SetInteractionHidden(hidden);
    potionScene=potions::MakeScene(interactionInput,interactionOutput);
    if(potionFlight.id&&potionFlight.shown)potions::AddThrown(potionScene,potionFlight.pose,potionFlight.code);   // #292
    if(droppedBottles&&droppedBottles->Count()&&interactionInput.stage.known) {
        const auto& stage=interactionInput.stage;
        std::array<bottles::Shown,bottles::MaxBottles> shown{};
        for(unsigned i=0,n=droppedBottles->Show(shown);i<n;++i)
            potions::AddDropped(potionScene,{stage.Room(shown[i].p),math::Multiply(math::Conjugate(stage.base),shown[i].q)},shown[i].code);
    }
    // The trap or bomb in the throwing hand while a toss is armed (#224).
    if(motionOutput.tossHeld&&motionInput.active&&!panelMode)
        potions::AddHeld(potionScene,{motionInput.hand[0].p,motionInput.hand[0].q},toss::Model(interactionInput.snapshot.skills[skills::TriggerSlot(0)].id));
    // The game's arrow on the bow while the game's own nocked arrow is not
    // showing (in town the game stops drawing missiles): tail at the nock or
    // drawing hand, along the draw. Without the cached model, the draw guide.
    if(potionModels.HasArrow()&&!panelMode&&motionControls.load()&&motionInput.active&&motionOutput.rayValid&&motionOutput.bowReady&&
       !bowvr::PreviewReady()&&(motionOutput.bowDrawing||motionOutput.bowInReach)) {
        const auto tail=motionOutput.bowDrawing?motionInput.hand[0].p:motionOutput.nockPoint;
        const auto direction=motionOutput.bowDrawing?motionOutput.rayDirection:motionOutput.bowForward;
        const auto middle=tail+direction*(bowflight::ArrowLength*.5f);
        potions::AddArrow(potionScene,{middle,math::BeamOrientation(direction,V(head.position)-middle)});
    }
    elements::Input elementalInput;
    elementalInput.context={interactionInput.session,(meleeEpoch<<32)|bowWorldEpoch,motionInput.skillContext,motionInput.equipment};
    elementalInput.at=interactionInput.at;
    elementalInput.enabled=hudSettings.values[settings::ElementalHands]>.5f;
    elementalInput.active=motionInput.active&&!interactionOutput.wheel&&!interactionOutput.stowed&&!journalMode;
    elementalInput.head=interactionInput.head;
    for(int h=0;h<2;++h) {
        elementalInput.hands[h]=interactionInput.hand[h];elementalInput.tracked[h]=interactionInput.tracked[h];
        elementalInput.occupied[h]=interactionOutput.bottleHand==h||interactionOutput.owner[h]!=interaction::Owner::Combat;
        elementalInput.casting[h]=handOutput.spells[h]>=0?handOutput.spells[h]:handOutput.slot>=0&&handOutput.hand==h?handOutput.slot:-1;
        elementalInput.activation[h]=handOutput.activation[h];
    }
    elementalScene=elementalState.Step(elementalInput,interactionInput.snapshot);
    // A torch club's flame (VR Settings > Video > Torch flame); its light is
    // vrcam's and stays either way.
    const bool torchFlame=hudSettings.values[settings::TorchFlame]>.5f;
    for(int h=0;h<2&&torchFlame;++h) {
        uint32_t torchFlags=0;
        if(interaction::HasTorchInHand(interactionInput.snapshot,h,torchFlags)) {
            elementalScene.hands[h].element=elements::Element::Fire;
            elementalScene.hands[h].opacity=1.0f;
            elementalScene.hands[h].pulse=0.5f;
            elementalScene.hands[h].skill=framevr::torch::TorchSkill;

            math::Vec tipPos{};
            math::Quat tipRot = interactionInput.tracked[h] ? interactionInput.hand[h].q : interactionInput.head.q;
            bool haveTip = false;
#ifdef D2RVR_NATIVE_OPENXR
            // Primary: track the physical club weapon bone rendered by the hero model.
            // wpose.tipHead is from the eye in the camera's turn-only (heading) frame.
            // Rotating by headYaw (pure azimuth, zero pitch/roll) anchors the flame directly to the 3D club in room space!
            skel::MeleeWeaponPose wpose;
            if (skel::MeleeWeapon(wpose, h) && (wpose.dirHead[0]*wpose.dirHead[0] + wpose.dirHead[1]*wpose.dirHead[1] + wpose.dirHead[2]*wpose.dirHead[2]) > 0.01f) {
                const float heading = math::Heading(interactionInput.head.q);
                const auto headYaw = math::Yaw(heading);
                const math::Vec tipFromEye{wpose.tipHead[0], wpose.tipHead[1], wpose.tipHead[2]};
                tipPos = interactionInput.head.p + math::Rotate(headYaw, tipFromEye);

                haveTip = true;
            }
#endif
            if (!haveTip && interactionInput.tracked[h]) {
                tipPos = framevr::torch::ComputeTorchTip(interactionInput.hand[h].p, interactionInput.hand[h].q, framevr::torch::DefaultShaftOffset);
                haveTip = true;
            }
            if (haveTip) {
                elementalScene.hands[h].pose = { tipPos, tipRot };
                elementalScene.session = gamestate::LocalPlayer();
                elementalScene.at = interactionInput.at;
                if(elementalScene.time==0.0f)elementalScene.time=float(interactionInput.at%60000)/1000.f;
            }
        }
    }
    if(interactionInput.active) {
        const bool snapshotFresh=interaction::Fresh(interactionInput.snapshot,interactionInput.session,interactionInput.at);
        if(snapshotFresh&&(interactionPrefetchSession!=interactionInput.session||interactionPrefetchRevision!=interactionInput.snapshot.revision)) {
            interaction::icons::Prefetch(interactionInput.snapshot);
            interactionPrefetchSession=interactionInput.session;interactionPrefetchRevision=interactionInput.snapshot.revision;
        }
        const uint64_t key=(interaction::icons::Revision()*0x9e3779b97f4a7c15ull)^(uint64_t(interactionOutput.editor)<<60)^(uint64_t(interactionOutput.editSlot+1)<<56)^(uint64_t(interactionOutput.editPage)<<48)^(uint64_t(interactionInput.snapshot.revision)<<32)^(uint64_t(interactionOutput.utility)<<24)^(
            uint64_t(interactionOutput.hover+1)<<16)^(uint64_t(motion.Selection()+1)<<8)^(uint64_t(interactionOutput.result)<<4)^uint64_t(interactionOutput.stowed)^(uint64_t(interactionOutput.map)<<1)^(uint64_t(interactionOutput.nearHolster+1)<<26)^(uint64_t(interactionOutput.bottle+1)<<28)^(uint64_t(interactionOutput.bottleEmpty)<<2)^(uint64_t(snapshotFresh)<<3);
        const auto now=GetTickCount64();
        const bool visible=motionInput.active||interactionOutput.wheel||interactionOutput.wrist||interactionOutput.vitals||interactionOutput.critical||interactionOutput.bottle>=0||
            interactionOutput.nearBelt>=0||interactionOutput.nearHolster>=0||math::Rotate(Q(head.orientation),{0,0,-1}).y<-.60f;
        const auto context=interaction::Context(interactionInput.snapshot,interactionOutput,snapshotFresh,controllerLabels);
        const bool missingCompanions=snapshotFresh&&wriststatus::Rows(context.companions,context.effects)&&!companionPaintValid;
        if(visible&&(!interactionPaintValid||missingCompanions||key!=interactionPaintKey||context!=interactionPaintContext)) {
            if(now>=interactionPaintAt&&(!interactionPaintRequest||key!=interactionRequestedKey||context!=interactionRequestedContext)) {
                interactionPaintRequest=interactionPainter.Submit({snapshotFresh?interactionInput.snapshot:interaction::Snapshot{},interactionOutput,motion.Selection(),controllerLabels});
                interactionRequestedKey=key;interactionRequestedContext=context;interactionPaintAt=now+100;
            }
            interaction::PaintedAtlas painted;
            if(key==interactionRequestedKey&&context==interactionRequestedContext&&atlasUploads.size()<2&&!interactionAtlasPending&&
               !companionAtlasPending&&interactionPainter.Take(interactionPaintRequest,painted)) {
                const auto uploadStart=Milliseconds();atlasTiming={};
                if(!painted.pixels.empty()&&UploadAtlas(interactionChain,atlasFormat,1024,1024,painted.pixels,true)) {
                    interactionPaintKey=key;interactionPaintContext=context;interactionPaintValid=true;
                    companionPaintValid=!painted.companions.empty()&&UploadAtlas(companionChain,atlasFormat,
                        wriststatus::Width,wriststatus::Height,painted.companions,true,&companionAtlasPending);
                    if(companionPaintValid)companionPaintContext=context;
                }
                interactionPaintRequest=0;
                const auto uploadMs=Milliseconds()-uploadStart;
                const auto& t=atlasTiming;
                static ULONGLONG reportAt=0;
                if((profile::Active()||uploadMs>4)&&now>=reportAt) {
                    char message[300];std::snprintf(message,sizeof message,"Frame UI refresh: background paint %.2f ms, frame-thread upload %.2f ms "
                        "(image wait %.2f, buffers %.2f%s, copy %.2f, commands %.2f, submit %.2f)",
                        painted.paintMs,uploadMs,t.image,t.buffers,t.made?" new":"",t.copy,t.record,t.submit);Say(message);reportAt=now+1000;
                }
                // Every 10 s, all refreshes: the spikes above alone do not show the usual cost.
                static ULONGLONG summaryFrom=0;static int refreshes=0,made=0;static double sumMs=0,maxMs=0;static AtlasTiming sum{};
                if(!summaryFrom)summaryFrom=now;
                ++refreshes;sumMs+=uploadMs;maxMs=std::max(maxMs,uploadMs);made+=t.made;
                sum.image+=t.image;sum.buffers+=t.buffers;sum.copy+=t.copy;sum.record+=t.record;sum.submit+=t.submit;
                if(now-summaryFrom>=10000) {
                    const double n=1.0/refreshes;char message[300];
                    std::snprintf(message,sizeof message,"Frame UI refresh: %d in %.0f s, frame-thread upload mean %.2f ms, max %.2f ms | a refresh: "
                        "image wait %.2f, buffers %.2f (%d new), copy %.2f, commands %.2f, submit %.2f ms",
                        refreshes,(now-summaryFrom)/1000.0,sumMs*n,maxMs,sum.image*n,sum.buffers*n,made,sum.copy*n,sum.record*n,sum.submit*n);
                    Say(message);summaryFrom=now;refreshes=made=0;sumMs=maxMs=0;sum={};
                }
            }
        }
    }
    {
        melee::Sample sample;
        sample.time=motionInput.time;sample.hand=motionInput.hand[0];sample.epoch=meleeEpoch;
        sample.equipment=motionInput.equipment;sample.active=motionInput.active&&motionOutput.meleeContact;
        sample.minWeaponSpeed=gestureSettings[settings::MeleeHitSpeed];
        sample.minThrustSpeed=gestureSettings[settings::MeleeThrustSpeed];
        sample.minShieldBashSpeed=gestureSettings[settings::ShieldBashSpeed];
        const auto weapons=gamestate::MeleeEquipment();
        // A shapeshift form strikes with both claws; its weapon is a stat without a visual.
        const bool claws=InShapeForm();
        const bool dual=!claws&&dualwield::Eligible(skel::HeroClass(),weapons);
        if(dual)sample.weapons=weapons;
        sample.claws=claws;
        // Melee-skill contact adapter: each hand's strikes carry its own trigger
        // skill when that is a confirmed strike skill (strike_skills.h). The
        // assignment identity rides the sample, so a rebind drops a queued stroke.
        // [gameplay] left_hand_strikes (skill wheel, lower left) picks the left hand's: its own or the right's.
        const auto strike=[&](int hand) {
            const int trigger=strikeskills::StrikeTrigger(hand,strikeskills::LeftChoice(gestureSettings[settings::LeftHandStrikes]));
            const int slot=skills::TriggerSlot(trigger<0?hand:trigger);const auto& skill=quickSnapshot.skills[slot];
            sample.skillSlot=slot;sample.skillOwner=skill.owner;sample.skillContext=motionInput.skillContext;
            sample.strikeSkill=trigger<0?-1:strikeskills::Armed(gestureSettings[settings::SkillGestures]>.5f,quickFresh,
                skill.known,skill.available,skill.strike,skill.id);
        };
        strike(0);
        meleePoses.Push(s.sampleStamp,sample);
        sample.hand=motionInput.hand[1];sample.handIndex=1;
        sample.active=sample.active&&(dual||claws)&&controls.lt<51&&controls.rt<51;
        strike(1);
        offhandPoses.Push(s.sampleStamp,sample);
        sample.weapons={};sample.claws=false;sample.strikeSkill=-1;
        sample.hand=motionInput.hand[1];sample.shield=true;sample.active=motionInput.active&&motionOutput.shieldContact;
        sample.skillSlot=shieldSlot;sample.skillOwner=shieldSkill.owner;sample.skillContext=motionInput.skillContext;
        shieldPoses.Push(s.sampleStamp,sample);
    }
    skel::SetMotionBow(motionInput.active&&motionOutput.bowReady,motionOutput.bowDrawing,motionOutput.draw,s.rightHand,s.leftHand,s.sampleStamp);
    if(!motionInput.active||(!motionOutput.bowReady&&!motionOutput.javelinReady))bowNeedsWorld=bowHasShot=leftNeedsWorld=leftHasShot=false;
    if(motionOutput.bowRelease||motionOutput.javelinRelease) {
        bowPending={};bowPending.origin=motionInput.hand[0].p;bowPending.direction=motionOutput.rayDirection;
        if(motionOutput.javelinRelease) {
            // At the hand; the launch moves it back to the weapon's tail once
            // the native Throw names the weapon's missile (bowflight::ResolveThrown).
            bowPending.kind=bowflight::Kind::Javelin;
            if(quiverArrow.Active()){bowPending.skill=quiverArrow.skill;bowPending.owner=quiverArrow.owner;bowPending.missile=quiverArrow.missile;}
        } else if(quiverArrow.Active()) {
            bowPending.skill=quiverArrow.skill;bowPending.owner=quiverArrow.owner;bowPending.missile=quiverArrow.missile;
        }
        if((motionOutput.bowRelease||motionOutput.javelinRelease)&&(quiverArrow.Active()||quiver.Current().Active())) {
            char line[200];std::snprintf(line,sizeof line,"Frame bow quiver: release carries skill=%d missile=%d (loaded=%d)",
                quiverArrow.skill,quiverArrow.missile,quiver.Current().skill);
            Say(line);
        }
        bowPending.draw=motionOutput.draw;bowPending.at=GetTickCount64();bowPending.stamp=s.sampleStamp;
        bowPending.session=gamestate::LocalPlayer();bowPending.equipment=motionInput.equipment;
        bowPending.epoch=(meleeEpoch<<32)|bowWorldEpoch;bowPending.sequence=++bowSequence;
        bowNeedsWorld=true;
    }
    if(motionOutput.leftRelease&&leftThrowArmed) {
        // Double Throw: the left hand throws its own weapon (the launch
        // scopes the native weapon selector to it).
        auto& p=throwLeftPending;p={};p.kind=bowflight::Kind::Javelin;p.hand=1;
        p.origin=motionOutput.leftOrigin;p.direction=motionOutput.leftDirection;p.draw=motionOutput.leftDraw;
        p.skill=quiverArrow.skill;p.owner=quiverArrow.owner;
        p.at=GetTickCount64();p.stamp=s.sampleStamp;p.session=gamestate::LocalPlayer();p.equipment=motionInput.equipment;
        p.epoch=(meleeEpoch<<32)|bowWorldEpoch;p.sequence=++bowSequence;
        leftNeedsWorld=true;
        char line[120];std::snprintf(line,sizeof line,"Frame double throw: left release carries skill=%d",p.skill);Say(line);
    }
    MotionFeedback(motionOutput);
    // One sharp tap per confirmed impact. Consume the event once; do not
    // sustain or refresh it while the weapon remains inside the creature.
    auto impactPulse=[&](int hand,bool shield=false){
        const auto impact=settings::HapticSample(gestureSettings,haptics::Category::Melee,shield?1:0);
        const bool sent=MotionPulse(hand,haptics::Category::Melee,impact.amplitude,XrDuration(impact.milliseconds)*1000000,impact.frequency);
        static ULONGLONG reportAt[2]{};const auto now=GetTickCount64();
        if(now>=reportAt[hand]) {
            char line[160];std::snprintf(line,sizeof line,"Frame melee haptics: source=%s output=%s duration_ms=%d frequency=%.0f sent=%d",
                shield?"shield":"weapon",hand==0?"right":"left",impact.milliseconds,impact.frequency,sent);Say(line);reportAt[hand]=now+1000;
        }
    };
    for(int hand=0;hand<2;++hand)if(const auto at=meleeImpactAt[hand].exchange(0);at&&motionInput.active&&GetTickCount64()-at<150)
        impactPulse(hand);
    if(const auto at=shieldImpactAt.exchange(0);at&&motionInput.active&&GetTickCount64()-at<150)
        impactPulse(1,true);
    // A parry swing that missed: a soft, dull buzz on the shield hand, unlike a
    // block's sharp tap. Scaled by the Melee haptic strength setting.
    if(const auto at=parryMissAt.exchange(0);at&&motionInput.active&&GetTickCount64()-at<150)
        MotionPulse(1,haptics::Category::Melee,.3f,70000000,60);
    for(int hand=0;hand<2;++hand)if(spellHaptics.Take(hand,gamestate::LocalPlayer(),GetTickCount64(),motionInput.active)) {
        const auto sample=haptics::Samples[int(haptics::Category::Spell)];
        MotionPulse(hand,haptics::Category::Spell,sample.amplitude,XrDuration(sample.milliseconds)*1000000,sample.frequency);
    }
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
                "Frame gameplay ready: active=%d head=%d hands=%d%d aim=%d panel=%d atlas=%d selection=%d weapon=%d trigger=%d blocked=%d bow ready=%d reach=%d drawing=%d charged=%d native=%d anchored=%d arrow_preview=%d draw=%.2f melee contact=%d grip=%d skill=%d motion=%d shield equipped=%d hook=%d contact=%d",
                motionInput.active,motionHeadTracked,motionInput.tracked[0],motionInput.tracked[1],motionInput.aimTracked,panelMode,
                gameplayChain!=XR_NULL_HANDLE,motion.Selection(),int(motionInput.weapon),motionInput.trigger>.55f,motionOutput.block,
                motionOutput.bowReady,motionOutput.bowInReach,motionOutput.bowDrawing,motionOutput.bowCharged,skel::MotionBowPosed(),skel::MotionBowAnchored(),bowvr::PreviewReady(),motionOutput.draw,
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
    if(!dualBindings&&leftMenuButton.PlainPressed())p.buttons|=XINPUT_GAMEPAD_LEFT_THUMB;
    p.thumb_lx=controls.lx;p.thumb_ly=controls.ly;p.thumb_rx=controls.rx;p.thumb_ry=controls.ry;
    p.left_trigger=controls.lt;
    const float rightTrigger=controls.rt/255.f;
    const bool attackAllowed=menuTriggerGate.Update(rightTrigger>.1f,panelMode||spatialMode||settingsOwned||settingsChord||inventoryPressed||menuPressed);
    p.right_trigger=attackAllowed?uint8_t(std::clamp(rightTrigger,0.f,1.f)*255):0;
    auto& a=*actions.data;a.held=0;
    if(p.right_trigger>128)a.held|=D2RVR_ACT_ATTACK;
    if(handOutput.interact||(!dualBindings&&(controls.buttons&menus::A)))a.held|=D2RVR_ACT_INTERACT;
    if(!dualBindings&&leftMenuButton.PlainPressed())a.held|=D2RVR_ACT_MAP;

    // Stick clicks toggle wheels here and retain native hold-to-sort in menus.
    // Gameplay swaps remain shoulder or equipment-wheel actions.
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
        // Wheel ownership consumes combat, never locomotion or turning.
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
void PlaceMainMenu() {
    if(!panels::CanAnchor(menuPlaced,render&&valid,state==XR_SESSION_STATE_FOCUSED,motionHeadTracked))return;
    menuBase=panels::AtEyeLevel(V(head.position),Q(head.orientation),
        panels::MenuDistance(int(vrsettings::Read().values[settings::MenuDistance]),true));
    menuPose=Placed(menuBase,menuOffset);menuPlaced=true;
    char b[160];std::snprintf(b,sizeof b,"Frame panels: main menu anchored from tracked head at eye height %.3f m",head.position.y);Say(b);
}
void Events() {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while(pPollEvent(instance,&event)==XR_SUCCESS) {
        if(event.type==XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED)controllerProfilesDirty=true;
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto& s=*reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
            if(s.state==XR_SESSION_STATE_FOCUSED&&state!=s.state) {
                // Startup/reconnection can change the runtime's head origin.
                menuPlaced=workspacePlaced=false;
            }
            state=s.state;
            char b[96];std::snprintf(b,sizeof b,"Frame OpenXR: session state %d",int(state));Say(b);
            if(state==XR_SESSION_STATE_READY) { XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;running=Check(pBeginSession(session,&bi),"begin session"); }
            if(state==XR_SESSION_STATE_STOPPING) { if(frame)EndPair();ClearInput();pEndSession(session);running=false;resolution::Restore(); }
            if(state==XR_SESSION_STATE_LOSS_PENDING||state==XR_SESSION_STATE_EXITING) {running=false;valid=false;resolution::Restore();ClearInput();}
        } else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {running=false;valid=false;resolution::Restore();ClearInput();}
        hud::NativeSession(running);
        event={XR_TYPE_EVENT_DATA_BUFFER};
    }
}
// Replay: the right eye drawn as the left one moved along the left view's own
// x (the camera fold folds that move into its projection) - and submitted so:
// the left eye's turn, its place moved by the eyes' gap along that x. A
// headset whose eyes are turned apart or set off along y or z is shown the
// right image reprojected from there.
void ReplayRight() {
    const auto q0=Q(views[0].pose.orientation),q1=Q(views[1].pose.orientation);
    const auto apart=math::Rotate(math::Conjugate(q0),V(views[1].pose.position)-V(views[0].pose.position));
    static bool told=false;
    if(!told){
        told=true;
        const float dot=std::min(1.f,std::fabs(q0.x*q1.x+q0.y*q1.y+q0.z*q1.z+q0.w*q1.w));
        char b[224];std::snprintf(b,sizeof b,"Frame OpenXR: replay - right eye %.1f mm along the left eye's x (%.1f mm up, %.1f mm back), the eyes turned %.2f degrees apart",
            apart.x*1000,apart.y*1000,apart.z*1000,2*std::acos(dot)*57.2957795f);
        Say(b);
    }
    replayGap=apart.x;
    const auto at=V(views[0].pose.position)+math::Rotate(q0,{apart.x,0,0});
    views[1].pose.orientation=views[0].pose.orientation;
    views[1].pose.position={at.x,at.y,at.z};
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
    frame=true;render=frameState.shouldRender;valid=false;motionHeadTracked=false;compassFrame=false;
    bowRenderTime=GetTickCount64();
    const bool loaded=inWorld&&inWorld();
    if(loaded&&!worldWasLoaded)worldCalibrationPending=true;
    if(!loaded)worldCalibrationPending=worldRecenter=false;
    worldWasLoaded=loaded;
    const bool spatial=SpatialMenu();
    const bool book=spatial&&gamestate::JournalOnly();
    const bool npc=spatial&&gamestate::NpcChoicesOnly();
    const bool surfaceChanged=spatial!=spatialMode||book!=journalMode;
    if(surfaceChanged) {
        workspacePlaced=false;uiMissingFrames=0;std::fill_n(uiCrop,4,0);
        if(book)journalPlacement.Open();
        Say(spatial?(book?"Frame panels: handheld quest log":"Frame panels: native menus in stereo world"):"Frame panels: native surface closed");
        releaseUiChain=!spatial&&swapchainsShort;   // short of swapchains: its surface back for the map (at the frame's end)
    }
    // Rearm pointer input when story/choices change, keeping the same anchored
    // canvas. A trigger that dismisses speech must not also choose an option.
    if(surfaceChanged||npc!=npcChoicesMode){ReleasePanelInput();panelMouse.Suspend();}
    const bool opaque=spatial&&gamestate::OpaqueMenuOpen();
    if(opaque!=opaqueMenuMode)Say(opaque?"Frame panels: shared black menu background":"Frame panels: spatial background restored");
    spatialMode=spatial;journalMode=book;npcChoicesMode=npc;opaqueMenuMode=opaque;upperBandMenuMode=spatial&&gamestate::UpperBandMenuOpen();uiEye.copied=false;
    mapEye.copied=false;hud::MapFrame();
    hud::UiFrame(spatialMode,!journalMode,0);
    const bool panel=wantsPanel&&wantsPanel()&&!spatialMode;
    if(panel!=panelMode){ReleasePanelInput();panelMouse.Suspend();panelMode=panel;menuPlaced=false;Say(panel?"Frame OpenXR: flat menu panel active":"Frame OpenXR: stereo gameplay active");}
    for(auto& e:eyes)e.copied=false;
    for(auto& w:viewWorld)w.ok=false;
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
        if(replayPair)ReplayRight();
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
        const int distancePreset=int(vrsettings::Read().values[settings::MenuDistance]);
        static int previousDistance=-1;
        if(distancePreset!=previousDistance){workspacePlaced=menuPlaced=false;previousDistance=distancePreset;menuOffset={};}
        if(spatialMode&&!journalMode&&panels::CanAnchor(workspacePlaced,render,state==XR_SESSION_STATE_FOCUSED,motionHeadTracked)) {
            uiBase=panels::AtEyeLevel(V(head.position),Q(head.orientation),panels::MenuDistance(distancePreset));
            uiPose=Placed(uiBase,menuOffset);
            uiSize=panels::Width;workspacePlaced=true;
        }
        if(panelMode)PlaceMainMenu();
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
bool KeepTurn(ID3D12GraphicsCommandList* list,ID3D12Resource* shown,int eye,D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_RENDER_TARGET) {
    if(turnImagesFailed)return false;
    if(!turnWarpTried){turnWarpTried=true;turnWarp.Initialize(device.Get(),turnFormat,&Say);}
    // The eyes' size is the session's: a new size comes with a new session.
    auto& kept=turnImages[eye];
    if(!kept) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        auto r=shown->GetDesc();r.Alignment=0;r.Flags=D3D12_RESOURCE_FLAG_NONE;r.Format=turnFormat;
        if(!Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&kept)),"create kept eye image")) {
            turnImagesFailed=true;turnKept={};return false;   // the other may still be read on the GPU: kept to Shutdown
        }
    }
    D3D12_RESOURCE_BARRIER b[2]{};
    for(auto& x:b){x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
    b[0].Transition.pResource=shown;b[0].Transition.StateBefore=state;b[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[1].Transition.pResource=kept.Get();b[1].Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;b[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
    const UINT first=state==D3D12_RESOURCE_STATE_COPY_SOURCE?1:0;   // already readable
    list->ResourceBarrier(2-first,b+first);list->CopyResource(kept.Get(),shown);
    for(auto& x:b)std::swap(x.Transition.StateBefore,x.Transition.StateAfter);
    list->ResourceBarrier(2-first,b+first);
    return true;
}
// By turns: this present's scene depth (scenedepth's copy, made for it before
// the effects) kept beside the eye's image, as the depth it was drawn with.
bool KeepDepth(ID3D12GraphicsCommandList* list,int eye) {
    scenedepth::Kept latest;
    if(!scenedepth::Take(latest)) {
        static bool told=false;
        if(!told){told=true;Say("Frame OpenXR: by turns - no scene depth for an eye yet; moving draws both eyes until there is");}
        return false;
    }
    constexpr auto readable=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    auto* source=reinterpret_cast<ID3D12Resource*>(latest.resource);
    auto& kept=turnDepths[eye];
    const auto desc=source->GetDesc();
    if(kept.texture) {   // a new DLSS mode: a new size. The last turn may still read the old one.
        const auto k=kept.texture->GetDesc();
        if(k.Width!=desc.Width||k.Height!=desc.Height||k.Format!=desc.Format){if(!WaitGpu())return false;kept.texture.Reset();}
    }
    if(!kept.texture) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        auto r=desc;r.Alignment=0;r.Flags=D3D12_RESOURCE_FLAG_NONE;
        if(!Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&r,readable,nullptr,IID_PPV_ARGS(&kept.texture)),"create kept eye depth"))return false;
        char line[192];std::snprintf(line,sizeof line,"Frame OpenXR: by turns - each eye's scene depth kept: %llux%u format %u, the scene in %.3f x %.3f of it",
            (unsigned long long)desc.Width,desc.Height,latest.format,latest.scale[0],latest.scale[1]);Say(line);
    }
    D3D12_RESOURCE_BARRIER b[2]{};
    for(auto& x:b){x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
    b[0].Transition.pResource=source;b[0].Transition.StateBefore=readable;b[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[1].Transition.pResource=kept.texture.Get();b[1].Transition.StateBefore=readable;b[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
    list->ResourceBarrier(2,b);list->CopyResource(kept.texture.Get(),source);
    for(auto& x:b)std::swap(x.Transition.StateBefore,x.Transition.StateAfter);
    list->ResourceBarrier(2,b);
    kept.format=DXGI_FORMAT(latest.format);kept.scale[0]=latest.scale[0];kept.scale[1]=latest.scale[1];
    return true;
}
// By turns: this present's effects layer (the game's own targets, as
// effectslayer last saw them) into the eye's effects blocks, while the
// turns keep the eye's own picture where the effects are.
void KeepEffects(ID3D12GraphicsCommandList* list,int eye) {
    effectslayer::Layer layer;
    if(turnsKeepOwn.load()<3||!effectslayer::Latest(layer)||
       !turnWarp.Effects(device.Get(),list,layer.targets,layer.states,eye,turnsEffectsReach.load()*float(layer.width)/4096)) {
        turnWarp.ForgetEffects(eye);return;
    }
    static bool told=false;
    if(!told){told=true;Say("Frame OpenXR: by turns - the game's effects layer kept for each eye (first frame doing so)");}
}
// By turns: this eye's interface (hud's copy, in a shader-resource state) kept
// beside its image, for TurnEye to lay over the eye when it is not drawn.
bool KeepLayer(ID3D12GraphicsCommandList* list,ID3D12Resource* layer,int eye) {
    auto& kept=turnLayers[eye];
    const auto desc=layer->GetDesc();
    if(kept) {   // the game's interface took a new size: the last turn may still read the old one
        const auto k=kept->GetDesc();
        if(k.Width!=desc.Width||k.Height!=desc.Height||k.Format!=desc.Format){if(!WaitGpu())return false;kept.Reset();}
    }
    if(!kept) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        auto r=desc;r.Alignment=0;r.Flags=D3D12_RESOURCE_FLAG_NONE;
        if(!Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&kept)),"create kept eye interface"))return false;
        char line[160];std::snprintf(line,sizeof line,"Frame OpenXR: by turns - the game's interface taken out of each eye's picture and laid back over it (%llux%u)",
            (unsigned long long)desc.Width,desc.Height);Say(line);
    }
    constexpr auto readable=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_BARRIER b[2]{};
    for(auto& x:b){x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
    b[0].Transition.pResource=layer;b[0].Transition.StateBefore=readable;b[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[1].Transition.pResource=kept.Get();b[1].Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;b[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
    list->ResourceBarrier(2,b);list->CopyResource(kept.Get(),layer);
    for(auto& x:b)std::swap(x.Transition.StateBefore,x.Transition.StateAfter);
    list->ResourceBarrier(2,b);
    return true;
}
// The interface over target (in `state`, and left in it): TurnWarp::Interface.
bool LayInterface(ID3D12GraphicsCommandList* list,ID3D12Resource* layer,ID3D12Resource* target,const std::array<float,9>& homography,int at,D3D12_RESOURCE_STATES state) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={target,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,state,D3D12_RESOURCE_STATE_RENDER_TARGET};
    const bool moved=state!=D3D12_RESOURCE_STATE_RENDER_TARGET;
    if(moved)list->ResourceBarrier(1,&b);
    const bool laid=turnWarp.Interface(device.Get(),list,layer,target,homography,at);
    std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
    if(moved)list->ResourceBarrier(1,&b);
    return laid;
}
// The eye not drawn is turned here when its interface goes over it (TurnEye).
bool TurnTarget(ID3D12Resource* image) {
    const auto d=image->GetDesc();
    if(turnTarget){const auto t=turnTarget->GetDesc();return t.Width==d.Width&&t.Height==d.Height;}   // one size a session
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    auto r=d;r.Alignment=0;r.Format=TypelessFamily(turnFormat);r.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if(r.Format==DXGI_FORMAT_UNKNOWN||
       !Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&turnTarget)),"create turned eye target")) {
        turnTarget.Reset();layersFailed=true;layerKept={};
        Say("Frame OpenXR: by turns - no target to lay the interface over the eye not drawn; it stays in the picture");
        return false;
    }
    return true;
}
// The eye not drawn this frame, turned from its kept image to this frame's
// head into its swapchain. False leaves it unacquired: its last released
// image goes again at the pose it was drawn with.
// By turns: nearer than CarriedNear metres along the view the eye not drawn
// moves with the hero (TurnDepth::carry), farther than CarriedFar with the
// world. Hands and weapons sit within the first; the ground at the feet,
// even looking straight down, past the second.
constexpr float CarriedNear=1.0f,CarriedFar=1.6f;
bool TurnEye(int i) {
    auto& e=eyes[i];
    // All of a swapchain's images are alike: checked before one is acquired,
    // as an acquired image nothing was written to must never be released.
    if(!turnKept[i]||!lastEyes[i].ok||e.acquired||e.images.empty()||!turnWarp.Fits(turnImages[i].Get(),e.images[0].texture))return false;
    if(!Acquire(e)||!WaitGpuValue(e.copyFence)||
       !Hr(e.allocator->Reset(),"reset turn allocator")||!Hr(e.commands->Reset(e.allocator.Get(),nullptr),"reset turn list")){failed=true;return false;}
    // In the world: the body's turn then the head's, now the drawn eye's body turn.
    const auto& was=lastEyes[i];const auto& now=views[i];
    const auto h=TurnHomography(math::Multiply(was.base,Q(was.pose.orientation)),{was.fov.angleLeft,was.fov.angleRight,was.fov.angleDown,was.fov.angleUp},
                                math::Multiply(viewBase[i^1],Q(now.pose.orientation)),{now.fov.angleLeft,now.fov.angleRight,now.fov.angleDown,now.fov.angleUp});
    // By turns: moved as well, with the depth it was drawn with, to where View
    // puts this eye this frame (the hero's step, the head's, the body's turn).
    TurnDepth depth{},other{};const TurnDepth* moving=nullptr;const TurnDepth* fromDrawn=nullptr;
    const auto& w=stageWorld;
    const bool placed=keepDepth&&turnWarp.DepthReady()&&w.valid&&w.upm>0&&GetTickCount64()-w.at<250;
    const auto offset=math::Rotate(w.base,V(now.pose.position)-w.origin)*w.upm;
    const math::Vec at={w.hero.x+offset.x,w.hero.y+w.eyeHeight+offset.y,w.hero.z+offset.z};
    // The drawn eye's picture of this frame (kept by Copy, before this on the
    // queue), moved across with its depth from where View put it.
    const auto& drawnAt=viewWorld[i^1];
    if(placed&&turnsFromDrawnEye.load()&&eyes[i^1].copied&&turnKept[i^1]&&turnDepths[i^1].ok&&drawnAt.ok&&drawnAt.nearPlane>0) {
        const auto& f=views[i^1].fov;
        other.image=turnImages[i^1].Get();
        other.depth=turnDepths[i^1].texture.Get();other.format=turnDepths[i^1].format;
        other.scale[0]=turnDepths[i^1].scale[0];other.scale[1]=turnDepths[i^1].scale[1];
        other.nearPlane=drawnAt.nearPlane;
        other.move=TurnMove(drawnAt.at,drawnAt.turn,at,math::Multiply(w.base,Q(now.pose.orientation)));
        other.drawnFov={f.angleLeft,f.angleRight,f.angleDown,f.angleUp};
        other.nowFov={now.fov.angleLeft,now.fov.angleRight,now.fov.angleDown,now.fov.angleUp};
        const auto gap=drawnAt.at-at;other.apart=std::sqrt(gap.x*gap.x+gap.y*gap.y+gap.z*gap.z);
        if(const int keep=turnsKeepOwn.load()) {   // this eye's own picture where it shows the same surface
            other.keep=TurnEdgePixels;
            const float wide=float(turnImages[i^1]->GetDesc().Width)/4096;
            if(keep==1||keep==4){other.keepNear=turnsKeepGap.load()*wide;other.keepReach=turnsKeepReach.load()*wide;}   // round near things
            if(keep>=3)other.keepEffects=turnsEffectsLevel.load();   // where the game's effects are
        }
        fromDrawn=&other;
    }
    if(placed&&was.world.ok&&turnDepths[i].ok) {
        depth.depth=turnDepths[i].texture.Get();depth.format=turnDepths[i].format;
        depth.scale[0]=turnDepths[i].scale[0];depth.scale[1]=turnDepths[i].scale[1];
        depth.nearPlane=was.world.nearPlane;
        depth.move=TurnMove(was.world.at,was.world.turn,at,math::Multiply(w.base,Q(now.pose.orientation)));
        depth.drawnFov={was.fov.angleLeft,was.fov.angleRight,was.fov.angleDown,was.fov.angleUp};
        depth.nowFov={now.fov.angleLeft,now.fov.angleRight,now.fov.angleDown,now.fov.angleUp};
        // Within reach (the hands, the weapon, the body): taken along by the
        // hero's step and the body's turn since the eye was drawn.
        depth.carry=TurnCarry(was.world.at,was.world.turn,was.world.hero,was.base,w.hero,w.base,at,math::Multiply(w.base,Q(now.pose.orientation)));
        depth.reach[0]=CarriedNear*w.upm;depth.reach[1]=CarriedFar*w.upm;
        moving=&depth;
    }
    static bool keepTold=false;
    if(moving&&fromDrawn&&other.keep>0&&!keepTold){keepTold=true;Say("Frame OpenXR: by turns - the eye not drawn keeps its own picture where the drawn eye's shows the same surface (first frame doing so)");}
    D3D12_RESOURCE_BARRIER b[2]{};const UINT kept=fromDrawn?2:1;
    for(UINT k=0;k<kept;++k){b[k].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[k].Transition={turnImages[i^k].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};}
    e.commands->ResourceBarrier(kept,b);
    // With its interface kept (the game's labels, out of its picture) the eye
    // is turned into turnTarget, its interface laid over it there as the game
    // blends it (UNORM), and then it goes to its swapchain image.
    ID3D12Resource* image=e.images[e.index].texture;
    const bool layered=layerKept[i]&&TurnTarget(image);
    turnWarp.Sharpen(turnsSharp.load());turnWarp.Fill(turnsFill.load());turnWarp.Rims(turnsRims.load());
    const bool turned=turnWarp.Render(device.Get(),e.commands.Get(),turnImages[i].Get(),layered?turnTarget.Get():image,h,i,moving,fromDrawn);
    if(turnCapture.left>0){auto& c=turnCapture;c.noted=true;c.fromDrawn=fromDrawn;c.moved=moving;c.other=other;c.own=depth;c.turn=h;}
    for(UINT k=0;k<kept;++k)std::swap(b[k].Transition.StateBefore,b[k].Transition.StateAfter);
    e.commands->ResourceBarrier(kept,b);
    if(turned&&layered) {
        // Its own interface as its pass drew it, turned as its picture was:
        // each label where this eye last saw it, at its own depth, never moved
        // with the scene behind it or cut along a monster's edge.
        LayInterface(e.commands.Get(),turnLayers[i].Get(),turnTarget.Get(),h,i*2,D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_RESOURCE_BARRIER c[2]{};
        for(auto& x:c){x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
        c[0].Transition.pResource=turnTarget.Get();c[0].Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;c[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        c[1].Transition.pResource=image;c[1].Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;c[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        e.commands->ResourceBarrier(2,c);e.commands->CopyResource(image,turnTarget.Get());
        for(auto& x:c)std::swap(x.Transition.StateBefore,x.Transition.StateAfter);
        e.commands->ResourceBarrier(2,c);
    }
    // The belt's potion models, left out of the kept images (Copy), drawn fresh
    // at this eye's pose: moved with the depth behind them they flickered and
    // smeared (owner, 2026-10-19). The eye's copy fence was waited above.
    if(turned&&interactionInput.active&&potionModels.Ready())
        potionModels.Render(e.commands.Get(),image,unsigned(i),{V(now.pose.position),Q(now.pose.orientation)},
            {now.fov.angleLeft,now.fov.angleRight,now.fov.angleDown,now.fov.angleUp},potionScene);
    // The elemental hands likewise: Copy draws them after the image is kept.
    if(turned&&!panelMode&&!spatialMode&&!settingsModel.open&&interactionInput.active&&
       elements::Current(elementalScene,gamestate::LocalPlayer(),GetTickCount64()))
        elementalRenderer.Render(e.commands.Get(),image,unsigned(i),{V(now.pose.position),Q(now.pose.orientation)},
            {now.fov.angleLeft,now.fov.angleRight,now.fov.angleDown,now.fov.angleUp},elementalScene);
    if(!Hr(e.commands->Close(),"close turn list")||!turned){failed=true;return false;}
    ID3D12CommandList* lists[]={e.commands.Get()};queue->ExecuteCommandLists(1,lists);
    if(!Hr(queue->Signal(fence.Get(),++fenceValue),"signal turn fence")){failed=true;return false;}
    e.copyFence=fenceValue;
    if(moving)++turnCounts.moved;else ++turnCounts.turned;
    if(fromDrawn)++turnCounts.drawn;
    return true;
}
// By turns capture: its files go next to the frame log, in
// d2rloader\logs\turn-captures\<the time of this game's first capture> (this
// plugin is in d2rloader\plugins): the serial starts again each game, and a
// new game's first capture wrote over the last one's.
std::wstring TurnCaptureFolder() {
    HMODULE self=nullptr;wchar_t path[MAX_PATH]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&TurnCaptureFolder),&self))return {};
    const DWORD n=GetModuleFileNameW(self,path,MAX_PATH);
    if(!n||n>=MAX_PATH)return {};
    std::wstring folder(path,n);
    for(int up=0;up<2;++up){const auto cut=folder.find_last_of(L"\\/");if(cut==std::wstring::npos)return {};folder.resize(cut);}
    folder+=L"\\logs";CreateDirectoryW(folder.c_str(),nullptr);
    folder+=L"\\turn-captures";CreateDirectoryW(folder.c_str(),nullptr);
    static const std::wstring game=[]{SYSTEMTIME t{};GetLocalTime(&t);wchar_t b[32];
        std::swprintf(b,32,L"\\%04u%02u%02u-%02u%02u%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);return std::wstring(b);}();
    folder+=game;CreateDirectoryW(folder.c_str(),nullptr);
    const DWORD a=GetFileAttributesW(folder.c_str());
    return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)?folder+L"\\":std::wstring{};
}
std::string Utf8(const std::wstring& s) {
    std::string out(size_t(std::max(0,WideCharToMultiByte(CP_UTF8,0,s.c_str(),int(s.size()),nullptr,0,nullptr,nullptr))),'\0');
    if(!out.empty())WideCharToMultiByte(CP_UTF8,0,s.c_str(),int(s.size()),out.data(),int(out.size()),nullptr,nullptr);
    return out;
}
void Append(std::string& s,const char* format,...) {
    char line[512];va_list a;va_start(a,format);const int n=std::vsnprintf(line,sizeof line,format,a);va_end(a);
    if(n>0)s.append(line,size_t(std::min(n,int(sizeof line)-1)));
}
// A frame's read-back texels, converted and written by WriteTurnCapture on a
// thread of its own: the render thread only copies them out of the readback.
struct TurnCaptureSource {std::wstring path;std::vector<uint8_t> texels;uint32_t pitch=0,width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;bool depth=false;};
struct TurnCaptureJob {std::vector<TurnCaptureSource> sources;std::wstring textPath;std::string text;};
bool SaveBytes(const std::wstring& path,const void* data,size_t bytes) {
    FILE* f=_wfopen(path.c_str(),L"wb");
    if(!f)return false;
    const bool ok=std::fwrite(data,1,bytes,f)==bytes;
    return std::fclose(f)==0&&ok;
}
void WriteTurnCapture(TurnCaptureJob job) {
    int written=0,lost=0;
    for(auto& s:job.sources) {
        bool ok=false;
        if(s.depth) {
            const auto depth=turncapture::Depths(s.texels.data(),s.pitch,s.width,s.height,turncapture::DepthOf(s.format));
            ok=SaveBytes(s.path,depth.data(),depth.size()*sizeof(float));
        } else {
            auto bgr=turncapture::Bgr(s.texels.data(),s.pitch,s.width,s.height,s.format);
            ok=turncapture::SavePng(s.path.c_str(),bgr,s.width,s.height);
        }
        s.texels={};
        ++(ok?written:lost);
    }
    ++(SaveBytes(job.textPath,job.text.data(),job.text.size())?written:lost);
    turnCaptureWritten+=written;turnCaptureFailed+=lost;--turnCaptureWriting;
}
// By turns capture, after TurnEye turned `turned`: both eyes as they go to
// the headset, the drawn eye's picture as the warp read it (kept by Copy) and
// its depth. Read back now: the frame waits for the GPU, so exclude these
// frames from performance comparison.
void CaptureTurn(int turned) {
    auto& c=turnCapture;
    const int frame=c.frame++,drawn=turned^1;--c.left;
    char line[1024];
    const auto skip=[&](const char* why){std::snprintf(line,sizeof line,"Frame capture: by turns frame %d of %d not recorded - %s",frame,turncapture::Frames,why);Say(line);};
    if(c.folder.empty())c.folder=TurnCaptureFolder();
    if(c.folder.empty()){skip("no folder beside the frame log");return;}
    if(!eyes[0].acquired||!eyes[1].acquired){skip("an eye was not in its swapchain image");return;}
    constexpr auto readable=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    struct Source {ID3D12Resource* resource;D3D12_RESOURCE_STATES state;std::string name;bool depth;};
    std::vector<Source> sources;
    for(int i=0;i<2;++i)sources.push_back({eyes[i].images[eyes[i].index].texture,D3D12_RESOURCE_STATE_RENDER_TARGET,turncapture::Name(c.serial,frame,i,i==drawn?"drawn":"turned","png"),false});
    if(turnKept[drawn]&&turnImages[drawn])sources.push_back({turnImages[drawn].Get(),D3D12_RESOURCE_STATE_COMMON,turncapture::Name(c.serial,frame,drawn,"kept","png"),false});
    if(turnDepths[drawn].ok&&turnDepths[drawn].texture)sources.push_back({turnDepths[drawn].texture.Get(),readable,turncapture::Name(c.serial,frame,drawn,"depth","bin"),true});
    const size_t n=sources.size();
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> at(n);std::vector<D3D12_RESOURCE_DESC> descs(n);
    UINT64 total=0;
    for(size_t k=0;k<n;++k) {
        descs[k]=sources[k].resource->GetDesc();
        if(sources[k].depth?turncapture::DepthOf(descs[k].Format)==turncapture::Depth::None:!turncapture::Readable(descs[k].Format)) {
            char why[96];std::snprintf(why,sizeof why,"format %d of %s is not read back",int(descs[k].Format),sources[k].name.c_str());skip(why);return;
        }
        UINT rows=0;UINT64 rowBytes=0,bytes=0;
        device->GetCopyableFootprints(&descs[k],0,1,0,&at[k],&rows,&rowBytes,&bytes);
        at[k].Offset=total;
        total=(total+bytes+D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT-1)/D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT*D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    }
    if(!c.buffer||c.size<total) {
        c.buffer.Reset();c.size=0;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=total;r.Height=1;r.DepthOrArraySize=1;r.MipLevels=1;
        r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(!Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&c.buffer)),"create turn capture readback")){skip("no readback buffer");return;}
        c.size=total;
    }
    if(!c.list&&(!Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&c.allocator)),"create turn capture allocator")||
                 !Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,c.allocator.Get(),nullptr,IID_PPV_ARGS(&c.list)),"create turn capture list")||
                 !Hr(c.list->Close(),"close new turn capture list"))){c.list.Reset();skip("no command list");return;}
    if(!Hr(c.allocator->Reset(),"reset turn capture allocator")||!Hr(c.list->Reset(c.allocator.Get(),nullptr),"reset turn capture list")){skip("no command list");return;}
    std::vector<D3D12_RESOURCE_BARRIER> b(n);
    for(size_t k=0;k<n;++k){b[k].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b[k].Transition={sources[k].resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,sources[k].state,D3D12_RESOURCE_STATE_COPY_SOURCE};}
    c.list->ResourceBarrier(UINT(n),b.data());
    for(size_t k=0;k<n;++k) {
        D3D12_TEXTURE_COPY_LOCATION to{},from{};
        to.pResource=c.buffer.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=at[k];
        from.pResource=sources[k].resource;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=0;   // a depth's plane 0
        c.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    }
    for(auto& x:b)std::swap(x.Transition.StateBefore,x.Transition.StateAfter);
    c.list->ResourceBarrier(UINT(n),b.data());
    if(!Hr(c.list->Close(),"close turn capture list")){skip("no command list");return;}
    const double start=Milliseconds();
    ID3D12CommandList* lists[]={c.list.Get()};queue->ExecuteCommandLists(1,lists);
    if(!Hr(queue->Signal(fence.Get(),++fenceValue),"signal turn capture")||!WaitGpu()){failed=true;skip("the GPU did not finish");return;}
    const double waited=Milliseconds()-start;
    void* mapped=nullptr;const D3D12_RANGE range{0,SIZE_T(total)};
    if(!Hr(c.buffer->Map(0,&range,&mapped),"map turn capture readback")){skip("the readback did not map");return;}
    TurnCaptureJob job;
    for(size_t k=0;k<n;++k) {
        const auto& f=at[k].Footprint;const auto* rows=static_cast<const uint8_t*>(mapped)+at[k].Offset;
        TurnCaptureSource s{c.folder+std::wstring(sources[k].name.begin(),sources[k].name.end()),{},f.RowPitch,f.Width,f.Height,descs[k].Format,sources[k].depth};
        s.texels.assign(rows,rows+size_t(f.RowPitch)*f.Height);
        job.sources.push_back(std::move(s));
    }
    const D3D12_RANGE none{0,0};c.buffer->Unmap(0,&none);
    if(c.left<=0){c.buffer.Reset();c.size=0;}
    // The frame's warp, to replay it (turn_warp.h's TurnDepth): `drawn` moved
    // the drawn eye's picture across, `own` this eye's older one.
    auto& t=job.text;
    const auto pose=[&](const char* what,const XrPosef& p,const XrFovf& v){
        Append(t,"%s position %.6f %.6f %.6f orientation %.7f %.7f %.7f %.7f fov %.6f %.6f %.6f %.6f\n",what,
            p.position.x,p.position.y,p.position.z,p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w,v.angleLeft,v.angleRight,v.angleDown,v.angleUp);};
    const auto floats=[&](const char* what,const float* v,int count){Append(t,"%s",what);for(int k=0;k<count;++k)Append(t," %.7g",v[k]);t+='\n';};
    const auto fov=[&](const char* what,const math::Fov& v){Append(t,"%s %.6f %.6f %.6f %.6f\n",what,v.left,v.right,v.down,v.up);};
    Append(t,"serial %u frame %d pass %llu drawn %s turned %s\n",c.serial,frame,(unsigned long long)c.passes,drawn?"right":"left",drawn?"left":"right");
    Append(t,"size %llu %u colour format %d depth format %d (read as %d) depth scale %.4f %.4f upm %.4f\n",(unsigned long long)descs[0].Width,descs[0].Height,int(descs[0].Format),
        int(turnDepths[drawn].format),sources.back().depth?int(descs.back().Format):0,turnDepths[drawn].scale[0],turnDepths[drawn].scale[1],stageWorld.upm);
    pose("view left",views[0].pose,views[0].fov);pose("view right",views[1].pose,views[1].fov);
    pose("turned eye's own picture drawn at",lastEyes[turned].pose,lastEyes[turned].fov);
    floats("turn homography",c.turn.data(),9);
    Append(t,"noted %d from drawn %d moved %d\n",int(c.noted),int(c.fromDrawn),int(c.moved));
    Append(t,"drawn near %.6g apart %.6g keep %.3g keepNear %.3g keepReach %.3g keepEffects %.3g scale %.4f %.4f\n",c.other.nearPlane,c.other.apart,c.other.keep,
        c.other.keepNear,c.other.keepReach,c.other.keepEffects,c.other.scale[0],c.other.scale[1]);
    floats("drawn move",c.other.move.data(),12);fov("drawn drawnFov",c.other.drawnFov);fov("drawn nowFov",c.other.nowFov);
    Append(t,"own near %.6g reach %.6g %.6g\n",c.own.nearPlane,c.own.reach[0],c.own.reach[1]);
    floats("own move",c.own.move.data(),12);floats("own carry",c.own.carry.data(),12);fov("own drawnFov",c.own.drawnFov);fov("own nowFov",c.own.nowFov);
    Append(t,"settings keep_own %d sharp %d fill %d rims %d\n",turnsKeepOwn.load(),int(turnsSharp.load()),int(turnsFill.load()),int(turnsRims.load()));
    Append(t,"files: PNGs 24-bit as shown (sRGB bytes); depth .bin float32 little-endian, width x height rows from the top, reverse-Z (distance = near / depth, world units), 0 sky\n");
    job.textPath=c.folder+[&]{const auto s=turncapture::Sidecar(c.serial,frame);return std::wstring(s.begin(),s.end());}();
    const auto& l=views[0];const auto& r=views[1];
    std::snprintf(line,sizeof line,"Frame capture: by turns frame %d of %d (pass %llu) drew %s, turned %s (from the drawn eye %d, moved %d); left %.4f %.4f %.4f fov %.4f %.4f %.4f %.4f; right %.4f %.4f %.4f fov %.4f %.4f %.4f %.4f; "
        "apart %.2f cm (upm %.2f); keep_own %d, sharp %d, fill %d, rims %d; waited %.1f ms for the GPU (diagnostic pause, exclude from performance comparison) -> %s",
        frame,turncapture::Frames,(unsigned long long)c.passes,drawn?"right":"left",drawn?"left":"right",int(c.fromDrawn),int(c.moved),
        l.pose.position.x,l.pose.position.y,l.pose.position.z,l.fov.angleLeft,l.fov.angleRight,l.fov.angleDown,l.fov.angleUp,
        r.pose.position.x,r.pose.position.y,r.pose.position.z,r.fov.angleLeft,r.fov.angleRight,r.fov.angleDown,r.fov.angleUp,
        stageWorld.upm>0?c.other.apart/stageWorld.upm*100:0.f,stageWorld.upm,turnsKeepOwn.load(),int(turnsSharp.load()),int(turnsFill.load()),int(turnsRims.load()),waited,Utf8(c.folder).c_str());
    Say(line);
    ++turnCaptureWriting;
    try{std::thread(WriteTurnCapture,std::move(job)).detach();}catch(...){--turnCaptureWriting;skip("no thread to write it");}
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
    e.commands->ResourceBarrier(2,barriers);
    // Kept before the game's interface and the belt's potion models go over
    // the picture: neither is in the scene's depth, so by turns the eye not
    // drawn lays its own over (TurnEye) instead of moving these with the
    // depth behind them.
    turnKept[eye]=keepTurns&&KeepTurn(e.commands.Get(),src,int(eye),D3D12_RESOURCE_STATE_COPY_SOURCE);
    turnDepths[eye].ok=keepDepth&&turnKept[eye]&&KeepDepth(e.commands.Get(),int(eye));
    // and where the game's effects were in it (TurnDepth::keepEffects)
    if(turnDepths[eye].ok)KeepEffects(e.commands.Get(),int(eye));else turnWarp.ForgetEffects(int(eye));
    // By turns the game's interface was taken out of its picture: laid back as
    // the game lays it, before the headset and the window get the picture.
    uint32_t layerWidth=0,layerHeight=0;
    auto* layer=reinterpret_cast<ID3D12Resource*>(hud::TakenInterface(layerWidth,layerHeight));
    layerKept[eye]=layer&&turnKept[eye]&&KeepLayer(e.commands.Get(),layer,int(eye));
    if(layer&&!LayInterface(e.commands.Get(),layerKept[eye]?turnLayers[eye].Get():layer,src,TurnIdentity,int(eye)*2,D3D12_RESOURCE_STATE_COPY_SOURCE)) {
        layersFailed=true;layerKept={};
        Say("Frame OpenXR: by turns - the game's interface could not be laid back; from the next frame it stays in the picture");
    }
    e.commands->CopyResource(dest,src);
    const bool pairLeft=!turnFrame&&mirrorSupported&&eyes[0].acquired&&eyes[0].copied;
    if(eye==1&&mirrorMono&&(pairLeft||(turnFrame&&turnKept[0]))) {
        // The headset already has this right frame; the window gets the left
        // image back: this pair's, still acquired until EndPair releases both
        // eyes, or by turns the image kept from the last left frame.
        D3D12_RESOURCE_BARRIER m[2]{barriers[0],barriers[1]};
        m[0].Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;m[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        m[1].Transition.pResource=pairLeft?eyes[0].images[eyes[0].index].texture:turnImages[0].Get();m[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        if(!pairLeft)m[1].Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;
        e.commands->ResourceBarrier(2,m);e.commands->CopyResource(src,m[1].Transition.pResource);
        std::swap(m[1].Transition.StateBefore,m[1].Transition.StateAfter);e.commands->ResourceBarrier(1,&m[1]);
        // By turns the kept left image is the scene alone: its interface over it.
        if(!pairLeft&&layerKept[0])LayInterface(e.commands.Get(),turnLayers[0].Get(),src,TurnIdentity,3,D3D12_RESOURCE_STATE_COPY_DEST);
        barriers[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
    }
    for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
    e.commands->ResourceBarrier(2,barriers);
    if(!panelMode&&interactionInput.active&&potionModels.Ready()) {
        const auto& v=views[eye];
        potionModels.Render(e.commands.Get(),dest,eye,{V(v.pose.position),Q(v.pose.orientation)},
            {v.fov.angleLeft,v.fov.angleRight,v.fov.angleDown,v.fov.angleUp},potionScene);
    }
    if(!panelMode&&!spatialMode&&!settingsModel.open&&interactionInput.active&&
       elements::Current(elementalScene,gamestate::LocalPlayer(),GetTickCount64())) {
        const auto& v=views[eye];
        elementalRenderer.Render(e.commands.Get(),dest,eye,{V(v.pose.position),Q(v.pose.orientation)},
            {v.fov.angleLeft,v.fov.angleRight,v.fov.angleDown,v.fov.angleUp},elementalScene);
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
    if(swapchainsShort&&spatialMode)return true;   // the native menus' panel may need its surface: no map while they are open
    uint32_t w=0,h=0;
    bool fullMap=false;float drawnZoom=0;   // Map view = Full map: the share is the full map's box, drawn at that game zoom
    auto* src=reinterpret_cast<ID3D12Resource*>(hud::MapTexture(w,h,&fullMap,&drawnZoom));
    if(!src||!w||!h)return true; // No current map; never display the last area's texture.
    auto& e=mapEye;
    if(!e.chain) {
        if(GetTickCount64()<mapRetryAt)return true;   // after a failure: again a little later
        // A step that failed: what and why, the half-made surface gone; true, as the eyes' copies go on.
        const auto failed=[&](const char* step,long code) {
            if(e.chain)pDestroySwapchain(e.chain);
            e={};
            char line[200];
            std::snprintf(line,sizeof line,"Frame menu map: %s failed (%ld)%s",step,code,
                          ++mapFailures<MapAttempts?"; trying again in 5 s":"; optional map suspended");
            Say(line);
            if(mapFailures>=MapAttempts)mapSupported=false;else mapRetryAt=GetTickCount64()+MapRetryMs;
            return true;
        };
        // The UI panel's usage, which OFXR Bridge has always let through.
        XrSwapchainCreateInfo c{XR_TYPE_SWAPCHAIN_CREATE_INFO};c.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        c.format=DXGI_FORMAT_R16G16B16A16_FLOAT;c.width=c.height=mapSide;c.sampleCount=c.faceCount=c.arraySize=c.mipCount=1;
        if(const XrResult r=pCreateSwapchain(session,&c,&e.chain);XR_FAILED(r)) {
            e.chain=XR_NULL_HANDLE;
            if(r!=XR_ERROR_LIMIT_REACHED)return failed("creating its swapchain",long(r));
            // No swapchain left (swapchainsShort): no fault of the map's; again later, however often.
            if(!swapchainsShort)Say("Frame OpenXR: no swapchain left for a panel (the runtime's limit; frame generation's bridge takes some)");
            swapchainsShort=true;mapRetryAt=GetTickCount64()+MapRetryMs;
            static ULONGLONG toldAt=0;
            if(GetTickCount64()>=toldAt){toldAt=GetTickCount64()+60000;Say("Frame menu map: no swapchain left for its surface; trying again every 5 s");}
            return true;
        }
        // Its images below the bridge as the other panels' (EnumeratePanelImages); should that
        // list refuse it, through the bridge as before the panels went below it.
        auto list=EnumeratePanelImages;
        uint32_t n=0;XrResult r=list(e.chain,0,&n,nullptr);
        if((XR_FAILED(r)||!n)&&runtimeEnumerateImages) {
            char line[160];std::snprintf(line,sizeof line,"Frame menu map: its images below OFXR Bridge refused (%ld); listed through it instead",long(r));Say(line);
            list=pEnumerateSwapchainImages;n=0;r=list(e.chain,0,&n,nullptr);
        }
        if(XR_FAILED(r)||!n)return failed("counting its images",long(r));
        e.images.resize(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        if(const XrResult got=list(e.chain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data()));XR_FAILED(got))return failed("listing its images",long(got));
        if(const HRESULT hr=device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&e.allocator));FAILED(hr))return failed("its command allocator",long(hr));
        if(const HRESULT hr=device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,e.allocator.Get(),nullptr,IID_PPV_ARGS(&e.commands));FAILED(hr))return failed("its command list",long(hr));
        if(const HRESULT hr=e.commands->Close();FAILED(hr))return failed("closing its command list",long(hr));
        Say(mapFailures?"Frame menu map: 512x512 GPU surface ready after a failed try; native explored map shared by both eyes":
                        "Frame menu map: 512x512 GPU surface ready; native explored map shared by both eyes");
    }
    if(!Acquire(e)||!WaitGpuValue(e.copyFence))return false;
    rq->flush_immediate_command_list();
    if(FAILED(e.allocator->Reset())||FAILED(e.commands->Reset(e.allocator.Get(),nullptr)))return false;
    // The heading arrow over the hero (while the compass knows the heading),
    // the map turning with you (top-down only), and top-down itself.
    const uint32_t flags=(compassFrame?1u:0u)|(compassFrame&&MapTurnOn()&&MapTopDownOn()?4u:0u)|(MapTopDownOn()?8u:0u);
    // The full map: a square of `span` share pixels around the X, the zoom's remainder after the
    // game zoom this share was really drawn at (so a step landing a frame late never jumps).
    // The corner map keeps its own zoom, up to 4x.
    const bool turning=(flags&4)!=0,topDown=(flags&8)!=0;
    const float span=fullMap?fullmap::Span(w,h,turning,topDown,mapZoom.Zoom(),drawnZoom):0.f;
    // the full map's X searched only where the game draws it, sized to it; the corner map: the wide search
    const auto search=fullMap?fullmap::XSearchAt(drawnZoom,h):fullmap::XSearch{};
    if(fullMap) {   // a line when the share's size or its drawn zoom changes (at most 60 a session)
        static uint32_t toldW=0,toldH=0;static float toldZoom=-1;static int told=0;
        if((w!=toldW||h!=toldH||drawnZoom!=toldZoom)&&told<60) {
            ++told;toldW=w;toldH=h;toldZoom=drawnZoom;
            char line[300];std::snprintf(line,sizeof line,"Frame full map: share %ux%u drawn at game zoom %.3f%s; wrist %.2fx, span %.0f px (%s%s); the X searched %.0f px each way from %+.0f,%+.0f px off the share's center%s",
                w,h,drawnZoom>0?drawnZoom:fullmap::Own,drawnZoom>0?"":" (the game's own: the hook gave none)",mapZoom.Zoom(),span,topDown?"top-down":"flat",turning?", turning":"",
                search.reach>0?search.reach:float(int(float(std::max(w,h))*.08f)),search.right,search.down,search.reach>0?"":" (wide: no drawn zoom)");
            Say(line);
        }
    }
    if(!mapBlit.Render(device.Get(),e.commands.Get(),src,e.images[e.index].texture,true,true,flags,float(wristHeading.bearing),1.f,
                       fullMap?1.f:std::min(mapZoom.Zoom(),mapzoom::Max),span,search.reach,search.right,search.down)) {
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
    static bool completeNpcLogged=false;
    if(npcChoicesMode) {
        const auto canvas=dialogue::Canvas(int(w),int(h));
        crop[0]=canvas.x;crop[1]=canvas.y;crop[2]=canvas.w;crop[3]=canvas.h;
        if(!completeNpcLogged) {
            char b[180];std::snprintf(b,sizeof b,"Frame NPC choices: complete %ux%u source canvas; widget crop disabled",w,h);Say(b);
        }
    }
    completeNpcLogged=npcChoicesMode;
    auto& e=uiEye;
    if(e.chain&&(w!=uiWidth||h!=uiHeight)) {
        if(!WaitGpuValue(e.copyFence))return false;
        pDestroySwapchain(e.chain);e={};
    }
    if(!e.chain) {
        XrSwapchainCreateInfo c{XR_TYPE_SWAPCHAIN_CREATE_INFO};c.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        c.format=DXGI_FORMAT_R16G16B16A16_FLOAT;c.width=w;c.height=h;c.sampleCount=c.faceCount=c.arraySize=c.mipCount=1;
        XrResult r=pCreateSwapchain(session,&c,&e.chain);
        if(r==XR_ERROR_LIMIT_REACHED) {   // no swapchain left (swapchainsShort): the map's, else again next frame
            if(!swapchainsShort)Say("Frame OpenXR: no swapchain left for a panel (the runtime's limit; frame generation's bridge takes some)");
            swapchainsShort=true;
            if(YieldMapSurface("the native menus"))r=pCreateSwapchain(session,&c,&e.chain);
            if(r==XR_ERROR_LIMIT_REACHED){e.chain=XR_NULL_HANDLE;return true;}   // the flat menu this frame
        }
        if(XR_FAILED(r)){e.chain=XR_NULL_HANDLE;uiSupported=false;Say("Frame panels: UI format unavailable; native menu retained");return true;}
        uint32_t n=0;EnumeratePanelImages(e.chain,0,&n,nullptr);e.images.resize(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        if(XR_FAILED(EnumeratePanelImages(e.chain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data()))))return false;
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
        Say("Frame OpenXR: window resized; recreating eye swapchains");Shutdown(true);failed=false;return;
    }
    if(frameGenRebuild.exchange(false)) {   // frame generation turned on, or it failed: a new instance names OFXR Bridge
        Say("Frame generation: restarting the OpenXR session for OFXR Bridge");Shutdown(true);failed=false;return;
    }
    if(!afr) {
        afrHandle=OpenFileMappingW(FILE_MAP_READ,FALSE,FLATVR_AFR_EYE_NAME);
        if(afrHandle)afr=static_cast<FlatVRAfrEye*>(MapViewOfFile(afrHandle,FILE_MAP_READ,0,0,sizeof(FlatVRAfrEye)));
    }
    const bool stereo=afr&&afr->version==FLATVR_AFR_EYE_VERSION&&afr->enabled&&(!(wantsPanel&&wantsPanel())||SpatialMenu());
    if(!Begin()||!render||!valid) {if(!stereo)EndPair();return;}
    const uint32_t eye=stereo?((afr->eye^afr->swap)&1u):0u;
    mirrorMono=stereo&&vrsettings::Read().values[settings::DesktopOneEye]>.5f;
    // The map and menu surfaces are copied once a frame: from the left eye's
    // pass, or by turns from whichever eye this frame draws.
    const bool shared=eye==0||(pairDrawing&&turnFrame);
    if(shared&&!CopyMap(rq)) {mapSupported=false;mapEye.copied=false;Say("Frame menu map: copy unavailable; optional map suspended");}
    if((shared&&!CopyUi(rq))||!Copy(src,eyes[eye],rq)){failed=true;EndPair();ClearInput();return;}
    if(!stereo) {
        PlaceMainMenu();
        EndPair();
    } else if(!pairDrawing&&eyes[0].copied&&eyes[1].copied) {
        EndPair();
    } else if(eye==1&&!eyes[0].copied&&!turnFrame) {
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
    if(gameSwapchain==swap&&session&&running)resolution::Update(static_cast<HWND>(swap->get_hwnd()),!failed,
        recommendedResolution,maximumResolution,{width,height},int(vrsettings::Read().values[settings::RenderResolution]));
}
void AfterEffects(reshade::api::effect_runtime* rt) {
    // ReShade's 'present' event is BEFORE its effects. Capture at its later
    // 'reshade_present' event so the sky/fog/HUD edits are included.
    static bool once=false;if(!once){once=true;Say("Frame OpenXR: first effects callback");}
    std::lock_guard lock(mutex);
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
void DestroySwap(reshade::api::swapchain* swap,bool resize) {
    std::lock_guard lock(mutex);if(swap==gameSwapchain){Shutdown(resize);failed=false;}
}
} // namespace

void CaptureEffects(reshade::api::effect_runtime* rt){AfterEffects(rt);}
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
    effectslayer::Register(log);
    resolution::Register(log);
    registered=true;Say("Frame OpenXR: registered D3D12 capture; awaiting game Present");
}
bool SpatialMenu(){return uiSupported.load()&&uiworld::Ready()&&hud::LayerFound()&&inWorld&&inWorld()&&wantsPanel&&wantsPanel();}
bool Active() {std::lock_guard lock(mutex);return frame&&valid&&render&&!failed&&!panelMode;}
bool VrGameplay(uint64_t now) {const auto at=vrGameplayAt.load();return at&&now>=at&&now-at<=1500;}
void PublishSewerPrompt(const sewerprompt::Target& target) {
    std::lock_guard lock(sewerPromptMutex);sewerPrompt=target;
}
void ConfigureDeferredWait(bool on) {
    if(deferredWait.exchange(on)!=on)Say(on?"Frame OpenXR: GPU wait deferred until copy allocator reuse":"Frame OpenXR: conservative GPU wait before image release");
}
void ConfigureSymmetricProjection(bool on) {symmetricProjection.store(on);}
void ConfigureFrameGeneration(float mode,float preset,float scale) {
    const auto o=framegen::Read(mode,preset,scale);
    const int packed=int(o.mode)*100+o.preset*10+o.scale;
    const int was=frameGenWanted.exchange(packed);
    if(was==packed)return;
    const bool wasOn=was>=0&&was/100!=0;
    std::lock_guard lock(frameGenMutex);
    if(frameGenRequest){FrameGenAsk(o);return;}   // loaded: live, Off bypasses it
    frameGenReadAt=0;
    if(was<0){if(o.On())Say("Frame generation: on (OFXR Bridge, experimental); it starts with OpenXR");return;}
    if(o.On()==wasOn)return;
    if(!o.On()){frameGenRebuild.store(false);Say("Frame generation: off");return;}
    // Not loaded: OFXR Bridge loads with an OpenXR instance, so FrameGenNow has a new one made.
    frameGenRebuildArmed.store(true);
    Say("Frame generation: on; OFXR Bridge loads with a new OpenXR session (the headset goes dark for a moment)");
}
void ConfigureTurnsFromDrawnEye(bool on) {
    if(turnsFromDrawnEye.exchange(on)!=on)Say(on?"Frame OpenXR: by turns - the eye not drawn mostly from the drawn eye's picture":"Frame OpenXR: by turns - the eye not drawn from its own image alone");
}
void SetTurnsKeepOwn(int mode) {
    static const char* const said[]={"Frame OpenXR: by turns - the eye not drawn takes the drawn eye's picture wherever it reaches",
        "Frame OpenXR: by turns - the eye not drawn keeps its own picture where it shows the same surface, round near things only",
        "Frame OpenXR: by turns - the eye not drawn keeps its own picture wherever it shows the same surface",
        "Frame OpenXR: by turns - the eye not drawn keeps its own picture where it shows the same surface, where the game's effects are",
        "Frame OpenXR: by turns - the eye not drawn keeps its own picture where it shows the same surface, round near things and where the game's effects are"};
    mode=std::clamp(mode,0,4);
    if(turnsKeepOwn.exchange(mode)!=mode)Say(said[mode]);
}
int TurnsKeepOwn() {return turnsKeepOwn.load();}
void ConfigureTurnsKeepOwn(int mode) {
    // Every ini reload passes it (VR Settings' Apply too): only a change of
    // the ini's own value overrides the DEV switch.
    static std::atomic<int> read{-1};
    mode=std::clamp(mode,0,4);
    if(read.exchange(mode)!=mode)SetTurnsKeepOwn(mode);
}
void ConfigureTurnsKeepNear(float gap,float reach) {turnsKeepGap.store(std::clamp(gap,1.f,1000.f));turnsKeepReach.store(std::clamp(reach,16.f,1024.f));}
void ConfigureTurnsKeepEffects(float level,float reach) {turnsEffectsLevel.store(std::clamp(level,1e-4f,100.f));turnsEffectsReach.store(std::clamp(reach,0.f,512.f));}
void SetTurnsSharp(bool on) {
    if(turnsSharp.exchange(on)!=on)Say(on?"Frame OpenXR: by turns - the eye not drawn read sharper (Catmull-Rom)":"Frame OpenXR: by turns - the eye not drawn read bilinear");
}
bool TurnsSharp() {return turnsSharp.load();}
void SetTurnsFill(bool on) {
    if(turnsFill.exchange(on)!=on)Say(on?"Frame OpenXR: by turns - the strips beside near things lose frame-old ghosts":"Frame OpenXR: by turns - the strips beside near things keep the eye's own picture");
}
bool TurnsFill() {return turnsFill.load();}
void ConfigureTurnsFill(bool on) {
    static std::atomic<int> read{-1};   // as ConfigureTurnsKeepOwn: only a change of the ini's own value overrides the DEV switch
    if(read.exchange(on?1:0)!=(on?1:0))SetTurnsFill(on);
}
void SetTurnsRims(bool on) {
    if(turnsRims.exchange(on)!=on)Say(on?"Frame OpenXR: by turns - the rims round things against the sky go with them":"Frame OpenXR: by turns - the rims round things against the sky stay with the sky");
}
bool TurnsRims() {return turnsRims.load();}
void ConfigureTurnsRims(bool on) {
    static std::atomic<int> read{-1};   // as ConfigureTurnsKeepOwn: only a change of the ini's own value overrides the DEV switch
    if(read.exchange(on?1:0)!=(on?1:0))SetTurnsRims(on);
}
void ConfigureTurnsSharp(bool on) {
    static std::atomic<int> read{-1};   // as ConfigureTurnsKeepOwn: only a change of the ini's own value overrides the DEV switch
    if(read.exchange(on?1:0)!=(on?1:0))SetTurnsSharp(on);
}
void ConfigureEyeTracking(bool on) {eyeTracking.store(on);}
void BeginEye(int eye) {
    hud::UiFrame(spatialMode,!journalMode,eye);
    std::lock_guard lock(mutex);
    // By turns the game's interface comes out of the picture: Copy and TurnEye lay it back.
    hud::TakeInterface(Active()&&keepDepth&&!layersFailed&&turnWarp.InterfaceReady()&&
        RenderTargetCompatible(format,UnormView(turnFormat))&&TypelessFamily(turnFormat)!=DXGI_FORMAT_UNKNOWN);
    hud::NativeGameplay(nativehud::Gameplay(Active(),inWorld&&inWorld(),wantsPanel&&wantsPanel(),spatialMode));
    lightbounds::SetActive(Active()&&inWorld&&inWorld());
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
    meleePoses.Clear();offhandPoses.Clear();CancelShieldCast();
    bowNeedsWorld=bowHasShot=leftNeedsWorld=leftHasShot=false;
    skel::SetMotionBow(false,false,0);
    if(!valid||!haveOrigin)return;
    origin=math::ResetHeight(origin,V(head.position));
    Say("Frame OpenXR: eye height reset to current headset position");
}
void Recenter() {
    std::lock_guard lock(mutex);
    ++meleeEpoch;motion.Cancel();motionOutput={};gamecmd::CancelPotion();
    meleePoses.Clear();offhandPoses.Clear();CancelShieldCast();
    bowNeedsWorld=bowHasShot=leftNeedsWorld=leftHasShot=false;
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
int StereoRendering() {return std::clamp(int(std::lround(vrsettings::Read().values[settings::StereoRendering])),0,1);}
bool GpuWaitDeferred() {return deferredWait.load();}
bool ReplayReady() {
    std::lock_guard lock(mutex);
    return !captureRequested.load()&&!d2rcam::LabelCapturePending();
}
bool ReplayPair() {std::lock_guard lock(mutex);return replayPair&&!failed;}
float ReplayShift() {std::lock_guard lock(mutex);return replayShift;}
float ReplayProjShift() {
    std::lock_guard lock(mutex);
    const auto off=[](const XrFovf& f){const float l=std::tan(f.angleLeft),r=std::tan(f.angleRight);return (r+l)/(r-l);};
    return .5f*(off(views[0].fov)-off(views[1].fov));
}
bool TurnDepthWanted() {return StereoRendering()==1;}
bool TurnDepthReady() {
    std::lock_guard lock(mutex);
    return turnWarp.DepthReady()&&turnDepths[0].ok&&turnDepths[1].ok&&lastEyes[0].world.ok&&lastEyes[1].world.ok;
}
TurnCounts TakeTurnCounts() {
    std::lock_guard lock(mutex);
    const auto counts=turnCounts;turnCounts={};return counts;
}
bool TurnReady() {
    std::lock_guard lock(mutex);
    return lastEyes[0].ok&&lastEyes[1].ok&&!captureRequested.load()&&!d2rcam::LabelCapturePending();
}
void BeginPair(bool byTurns,bool replay) {
    std::lock_guard lock(mutex);
    lastTiming={};
    if(session&&!failed){
        if(frame)EndPair();
        const int mode=StereoRendering();
        turnFrame=byTurns;replayPair=replay&&!byTurns;
        keepTurns=byTurns||mode==1;keepDepth=mode==1;Begin();pairDrawing=true;
        if(replayPair&&!(frame&&valid&&render&&!panelMode))replayPair=false;
        gpuProfile.started=gpuProfile.effects=gpuProfile.resolved=0;
        // The profile and stereo captures take both eyes from two passes.
        if(Active()&&!byTurns&&!replayPair)profile::BeginPair();else profile::SkipPair();
        if(Active()&&!byTurns&&!replayPair&&(captureRequested.exchange(false)||d2rcam::TakeLabelCapture())) {
            capturePair=true;captureEyes=0;++captureSerial;
            Say("Frame capture: recording one stereo pair (readback pause is diagnostic, exclude from performance comparison)");
            // By turns: the frames by turns after this pair as well (CaptureTurn)
            if(mode==1&&turnCaptureWriting.load()>0)Say("Frame capture: by turns - the last capture is still being written; this one is the pair only");
            else if(mode==1) {
                auto& c=turnCapture;c.left=turncapture::Frames;c.frame=0;c.passes=0;c.serial=captureSerial;c.noted=false;c.reported=false;
                turnCaptureWritten=0;turnCaptureFailed=0;
                char line[200];std::snprintf(line,sizeof line,"Frame capture: by turns - then the next %d frames by turns, both eyes as they go to the headset (each waits for the GPU: exclude them from performance comparison)",turncapture::Frames);
                Say(line);
            }
        }
    } else profile::SkipPair();
}
void EndPair() {
    hud::EndNativeEye();
    hud::TakeInterface(false);
    lightbounds::SetActive(false);
    foveation::EndWorld();
    d2rcam::SetStereoUiEye(-1);
    std::lock_guard lock(mutex);pairDrawing=false;const bool turn=turnFrame;turnFrame=false;replayPair=false;if(!frame||!session)return;
    d2rcam::TraceStereoLabels(-1);
    if(capturePair){Say(captureEyes==3?"Frame capture: both eye screenshots requested":"Frame capture: incomplete pair; do not use for stereo comparison");capturePair=false;}
    // By turns: the eye not drawn, turned to this frame's head.
    int turned=-1;
    if(turn&&render&&valid&&!panelMode&&!failed)
        for(int i=0;i<2;++i)if(!eyes[i].copied&&eyes[i^1].copied&&TurnEye(i))turned=i;
    // [debug] capture_pair by turns: the frames by turns after the pair; a
    // pair drawn between (moving, a menu) only counts as a pass.
    if(turnCapture.left>0) {
        auto& c=turnCapture;++c.passes;
        if(turn&&turned>=0&&!failed)CaptureTurn(turned);
        c.noted=false;
        if(c.left>0&&c.passes>900){c.left=0;c.buffer.Reset();c.size=0;Say("Frame capture: by turns - stopped, no more frames by turns came (a menu, or moving)");}
    }
    if(!turnCapture.reported&&turnCapture.left<=0&&turnCaptureWriting.load()==0) {
        turnCapture.reported=true;
        char line[640];std::snprintf(line,sizeof line,"Frame capture: by turns - %d files written, %d failed, in %s",turnCaptureWritten.load(),turnCaptureFailed.load(),Utf8(turnCapture.folder).c_str());
        Say(line);
    }
    // All image writes and state restoration have been submitted on the queue
    // bound to xrCreateSession. OpenXR orders its work after those commands;
    // it does not require a CPU completion wait before xrReleaseSwapchainImage.
    // WineOpenXR also drains that queue before its release/layout commands.
    // Keep the old path for A/B, and for synchronous diagnostic query readback.
    const bool waitNow=!deferredWait.load()||gpuProfile.started!=0;
    if(waitNow&&fence&&!WaitGpu()){failed=true;for(auto& e:eyes)e.copied=false;turned=-1;}
    if(!failed)ReadGpuProfile();
    gpuProfile.started=gpuProfile.effects=gpuProfile.resolved=0;
    const double releaseStart=Milliseconds();
    for(auto& e:eyes)if(e.acquired){
        const int i=int(&e-eyes.data());
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if(!Check(pReleaseSwapchainImage(e.chain,&ri),"release copied eye")){failed=true;e.copied=false;if(i==turned)turned=-1;}
        if(!e.copied&&i!=turned)lastEyes[i].ok=false;
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
    const auto drawn=[&](int i){return eyes[i].copied||(turn&&lastEyes[i].ok);};
    if(render&&valid&&!panelMode&&drawn(0)&&drawn(1)&&(eyes[0].copied||eyes[1].copied)) {
        for(int i=0;i<2;++i){
            if(eyes[i].copied)lastEyes[i]={views[i].pose,views[i].fov,true,viewBase[i],viewWorld[i]};
            // drawn or turned this frame: this frame's pose; else as drawn
            const bool now=eyes[i].copied||i==turned;
            pv[i].pose=now?views[i].pose:lastEyes[i].pose;pv[i].fov=now?views[i].fov:lastEyes[i].fov;
            pv[i].subImage.swapchain=eyes[i].chain;pv[i].subImage.imageRect={{0,0},{int32_t(width),int32_t(height)}};
        }
        projection.space=space;projection.viewCount=2;projection.views=pv.data();layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);menuPlaced=false;
    } else lastEyes={};
    if(!layer&&render&&valid&&eyes[0].copied&&menuPlaced) {
        const auto size=panels::Fit(width,height,panelWidth);
        quad.space=space;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.pose=menuPose;quad.size={size.width,size.height};
        quad.subImage.swapchain=eyes[0].chain;quad.subImage.imageRect={{0,0},{int32_t(width),int32_t(height)}};layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
    }
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};ei.displayTime=frameState.predictedDisplayTime;ei.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    std::array<const XrCompositionLayerBaseHeader*,32> layers{};uint32_t count=0;
    // The shared panels' pictures uploaded this frame, all in one copy (panelatlas).
    if(sharedDirty) {
        const bool flushed=FlushShared();
        for(auto* p:{&settingsAtlasPending,&bloodAtlasPending,&strainAtlasPending,&bowLaserAtlasPending,&noticeAtlasPending})if(*p) {
            *p=false;
            if(p==&settingsAtlasPending)settingsImageReady=flushed;else if(p==&bloodAtlasPending)bloodImageReady=flushed;
            else if(p==&strainAtlasPending)strainImageReady=flushed;else if(p==&bowLaserAtlasPending)bowLaserImageReady=flushed;else noticeImageReady=flushed;
        }
    }
    // The native menus closed short of swapchains: their surface goes back for the map.
    if(releaseUiChain&&!spatialMode&&uiEye.chain&&!uiEye.acquired&&WaitGpu()) {
        pDestroySwapchain(uiEye.chain);uiEye={};releaseUiChain=false;
        Say("Frame panels: the native menus' surface given back while closed (no swapchain to spare)");
    }
    if(interactionAtlasPending) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        Check(pReleaseSwapchainImage(interactionChain,&release),"release interaction atlas");interactionAtlasPending=false;
    }
    if(companionAtlasPending) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        companionPaintValid=Check(pReleaseSwapchainImage(companionChain,&release),"release companion atlas")&&companionPaintValid;companionAtlasPending=false;
    }
    // Tabbed, trade and waypoint menus use the same black background in both eyes.
    // Submit just their shared opaque quad: no different cached world per eye.
    const bool blackMenu=opaqueMenuMode&&workspacePlaced&&uiEye.copied&&uiCrop[2]>0&&!settingsModel.open&&!bodyCalibration.Active();
    if(layer&&!blackMenu)layers[count++]=layer;
    const uint32_t capacity=std::min(uint32_t(layers.size()),layerLimit);
    const bool padQuads=composition::NeedsQuadPadding(wineOpenXR,layer,pointerChain);
    const uint32_t contentLimit=padQuads&&capacity>1?capacity-1:capacity;
    XrCompositionLayerQuad palette{XR_TYPE_COMPOSITION_LAYER_QUAD},bottle{XR_TYPE_COMPOSITION_LAYER_QUAD},chosen{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<XrCompositionLayerQuad,2> arrow{{{XR_TYPE_COMPOSITION_LAYER_QUAD},{XR_TYPE_COMPOSITION_LAYER_QUAD}}};
    XrCompositionLayerQuad bowLaser{XR_TYPE_COMPOSITION_LAYER_QUAD},bowLaserEnd{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad nock{XR_TYPE_COMPOSITION_LAYER_QUAD},bowHint{XR_TYPE_COMPOSITION_LAYER_QUAD},activeSkill{XR_TYPE_COMPOSITION_LAYER_QUAD},leftActiveSkill{XR_TYPE_COMPOSITION_LAYER_QUAD};
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
        interactionInput.snapshot,interactionOutput,interaction::Fresh(interactionInput.snapshot,interactionInput.session,interactionInput.at),controllerLabels))
        motionQuad(palette,palettePose,paletteWidth,paletteHeight,{{0,0},{768,768}},interactionChain);
    XrCompositionLayerQuad menuMap{XR_TYPE_COMPOSITION_LAYER_QUAD};
    wristMapShown=false;
    if(palette.subImage.swapchain&&mapEye.copied&&interaction::MenuMapWanted(interactionInput,interactionOutput,GetTickCount64())) {
        const auto bounds=interaction::MapContent;
        auto pose=MotionPose(palettePose);
        pose.p=pose.p+math::Rotate(pose.q,{((bounds.left+bounds.right)*.5f-.5f)*paletteWidth,
            (.5f-(bounds.top+bounds.bottom)*.5f)*paletteHeight,.002f});
        motionQuad(menuMap,XrPose(pose),(bounds.right-bounds.left)*paletteWidth,(bounds.bottom-bounds.top)*paletteHeight,
            {{0,0},{int32_t(mapSide),int32_t(mapSide)}},mapEye.chain);
    }
    auto interactionPose=[](interaction::Pose p){return XrPose({p.p,p.q});};
    std::array<XrCompositionLayerQuad,4> beltQuads;
    XrCompositionLayerQuad wrist{XR_TYPE_COMPOSITION_LAYER_QUAD},wristMap{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad wristCompass{XR_TYPE_COMPOSITION_LAYER_QUAD},compassMarker{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad navigationInfo{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad companionRow{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(interactionVisible) {
        if(!potionModels.Ready()&&interactionOutput.bottle>=0&&interactionOutput.bottleHand>=0) {
            auto pose=motionInput.hand[interactionOutput.bottleHand];pose.q=potions::HeldRotation(pose.q);
            pose.p=pose.p+math::Rotate(pose.q,{0,.055f,0});
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
        if((interactionOutput.vitals||interactionOutput.critical)&&!interactionOutput.wheel) {
            const bool palm=interactionOutput.palm;
            const auto panel=interaction::VitalsPanel(interactionInput.hand[1],interactionInput.head,palm,interactionInput.vitalsTilt);
            auto pose=motionInput.hand[1];pose.p=panel.p;pose.q=panel.q;
            if(interactionOutput.critical&&!interactionOutput.vitals){pose.p=V(head.position)+math::Rotate(Q(head.orientation),{-.20f,-.22f,-.55f});pose.q=Q(head.orientation);}
            motionQuad(wrist,XrPose(pose),palm?interaction::PalmWidth:interaction::WristWidth,
                palm?interaction::PalmHeight:interaction::WristHeight,{{512,768},{512,256}},interactionChain);
        }
        // The area label and compass remain usable during a missing map capture.
        // Never submit a label painted for another session or a previous area.
        const uint32_t navigationLayers=2+uint32_t(mapEye.copied);
        const bool locationReady=interactionPaintValid&&count+navigationLayers+1<=contentLimit&&
            interaction::LocationImageCurrent(interactionPaintContext,interactionInput.snapshot);
        if(compassAtlasReady&&compassFrame&&count+navigationLayers<=contentLimit&&
           interaction::NavigationWanted(interactionInput,interactionOutput,GetTickCount64())) {
            const bool palm=interactionOutput.navigationPalm;
            const bool withVitals=interactionOutput.vitals&&interactionOutput.palm==palm;
            const bool companionsReady=vrsettings::Read().values[settings::CompanionCounts]>.5f&&companionPaintValid&&
                companionChain&&count+navigationLayers+uint32_t(locationReady)+1<=contentLimit&&
                interaction::CompanionImageCurrent(companionPaintContext,interactionInput.snapshot);
            const int companionRows=companionsReady?wriststatus::Rows(interactionInput.snapshot.companions,interactionInput.snapshot.effects):0;
            const float footer=interaction::CompanionSpace(companionRows);
            if(mapEye.copied){
                const auto mapPanel=interaction::NavigationMapPanel(interactionInput.hand[1],interactionInput.head,palm,withVitals,footer);
                wristMapShown=true;wristMapCenter=mapPanel.p;
                motionQuad(wristMap,interactionPose(mapPanel),
                    interaction::WristMapSize,interaction::WristMapSize,{{0,0},{int32_t(mapSide),int32_t(mapSide)}},mapEye.chain);
            }
            auto compassPose=interaction::NavigationCompassPanel(interactionInput.hand[1],interactionInput.head,palm,withVitals,footer);
            motionQuad(wristCompass,interactionPose(compassPose),interaction::WristCompassWidth,interaction::WristCompassHeight,
                At(panelatlas::Compass,{{compass::Crop(wristHeading.bearing),0},{compass::Window,compass::StripHeight}}),compassChain);
            constexpr float markerWidth=interaction::WristCompassWidth*compass::MarkerWidth/compass::Window;
            constexpr float markerHeight=interaction::WristCompassHeight*compass::MarkerHeight/compass::StripHeight;
            compassPose.p=compassPose.p+math::Rotate(compassPose.q,{0,(interaction::WristCompassHeight-markerHeight)*.5f,.001f});
            motionQuad(compassMarker,interactionPose(compassPose),markerWidth,markerHeight,
                At(panelatlas::Compass,{{0,compass::StripHeight},{compass::MarkerWidth,compass::MarkerHeight}}),compassChain);
            if(locationReady)motionQuad(navigationInfo,interactionPose(interaction::NavigationInfoPanel(
                interactionInput.hand[1],interactionInput.head,palm,withVitals,footer)),interaction::NavigationInfoWidth,interaction::NavigationInfoHeight,
                {{navigation::AtlasX,navigation::AtlasY},{navigation::Width,navigation::Height}},interactionChain);
            if(companionRows)motionQuad(companionRow,interactionPose(interaction::NavigationCompanionsPanel(
                interactionInput.hand[1],interactionInput.head,palm,withVitals,companionRows)),interaction::WristMapSize,interaction::CompanionHeight(companionRows),
                {{0,0},{companions::Width,wriststatus::UsedHeight(interactionInput.snapshot.companions,interactionInput.snapshot.effects)}},companionChain);
        }
    }
    const bool weaponSkillIndicators=vrsettings::Read().values[settings::WeaponSkillIndicators]>.5f;
    if(weaponSkillIndicators&&gameplayVisible&&!interactionOutput.wheel&&!interactionOutput.stowed&&interactionOutput.bottle<0&&
       interactionPaintValid&&interactionPaintContext.cue==interactionOutput.cue&&interactionChain) {
        const auto& cue=interactionOutput.cue;
        const int side=0;
        const auto hand=motionInput.hand[side];
        const auto at=hand.p+math::Rotate(hand.q,{0,-.10f,.08f});
        const auto pose=XrPose({at,Q(head.orientation)});
        if(cue.expanded)motionQuad(activeSkill,pose,.24f,.18f,{{768,0},{256,192}},interactionChain);
        else motionQuad(activeSkill,pose,.075f,.075f,{{768,0},{96,96}},interactionChain);
    }
    if(weaponSkillIndicators&&gameplayVisible&&!interactionOutput.wheel&&!interactionOutput.stowed&&interactionOutput.bottle<0&&
       interactionPaintValid&&interactionPaintContext.leftCue==interactionOutput.leftCue&&interactionChain) {
        const auto& cue=interactionOutput.leftCue;
        if(cue.known&&cue.id>=0){
            const auto hand=motionInput.hand[1];const auto at=hand.p+math::Rotate(hand.q,{0,-.10f,.08f});
            const auto pose=XrPose({at,Q(head.orientation)});
            if(cue.expanded)motionQuad(leftActiveSkill,pose,.24f,.18f,{{768,200},{256,192}},interactionChain);
            else motionQuad(leftActiveSkill,pose,.075f,.075f,{{768,200},{96,96}},interactionChain);
        }
    }
    XrCompositionLayerQuad sewerHint{XR_TYPE_COMPOSITION_LAYER_QUAD};
    bool sewerHintShown=false;
    if(interactionVisible&&!interactionOutput.wheel&&!settingsModel.open&&!bodyCalibration.Active()&&gameplayChain) {
        sewerprompt::Target target;
        {std::lock_guard lock(sewerPromptMutex);target=sewerPrompt;}
        const auto now=GetTickCount64();math::Vec at{};
        const bool spaceFresh=stageWorld.valid&&now>=stageWorld.at&&now-stageWorld.at<=250;
        if(spaceFresh&&sewerprompt::Fresh(target,gamestate::LocalPlayer(),now,interactionInput.snapshot.location.level)&&
           sewerprompt::Position(target,stageWorld.origin,stageWorld.hero,stageWorld.base,stageWorld.upm,
                                 stageWorld.eyeHeight,V(head.position),Q(head.orientation),at)) {
            const auto before=count;
            motionQuad(sewerHint,XrPose({at,Q(head.orientation)}),sewerprompt::PanelWidth,sewerprompt::PanelHeight,
                {{sewerprompt::AtlasX,sewerprompt::AtlasY},{sewerprompt::Width,sewerprompt::Height}},gameplayChain);
            sewerHintShown=count>before;
        }
    }
    // The quest / level-up card stays where it came up, like the native
    // dialogue surfaces; opaque menus and VR Settings hold it back.
    XrCompositionLayerQuad noticeQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(noticeImageReady&&sharedChain&&noticeCard.kind>=0&&noticePaintedSequence==noticeCard.sequence&&
       layer&&!panelMode&&!blackMenu&&!settingsModel.open&&!bodyCalibration.Active()) {
        if(noticeCard.place)noticePlacement=notice::Place(V(head.position),Q(head.orientation));
        const auto before=count;
        motionQuad(noticeQuad,XrPose({noticePlacement.position,noticePlacement.orientation}),notice::PanelWidth,notice::PanelHeight,
            Rect(panelatlas::Notice),sharedChain);
        if(count>before){noticeCards.Shown(GetTickCount64());noticeCard.place=false;}
    }
    static bool sewerHintWasShown=false;
    if(sewerHintShown!=sewerHintWasShown){Say(sewerHintShown?"Frame sewer prompt: shown":"Frame sewer prompt: hidden");sewerHintWasShown=sewerHintShown;}
    if(gameplayVisible&&motionOutput.bowReady) {
        const auto bow=motionInput.hand[1];
        const auto center=motionOutput.bowDrawing?motionInput.hand[0].p:motionOutput.nockPoint;
        // The game's bow mesh supplies the string. Do not draw the teal
        // V-string guide over it, even when its rig cannot follow the hand.
        if(!bowvr::PreviewReady()&&!potionModels.HasArrow()&&(motionOutput.bowDrawing||motionOutput.bowInReach)) {
            const auto direction=motionOutput.bowDrawing?motionOutput.rayDirection:motionOutput.bowForward;
            const auto mid=center+direction*(bowflight::ArrowLength*.5f);
            const auto orient=math::BeamOrientation(direction,V(head.position)-mid);
            motionQuad(arrow[0],XrPose({mid,orient}),.055f,bowflight::ArrowLength,{{512,768},{64,256}},gameplayChain);
            motionQuad(arrow[1],XrPose({mid,math::Multiply(orient,math::Yaw(1.57079633f))}),.055f,bowflight::ArrowLength,{{512,768},{64,256}},gameplayChain);
        }
        // Native bows have a visible string to grab. The extra idle marker
        // correlated with compositor pose errors; omit it on this path.
        if(!skel::MotionBowPosed()&&!motionOutput.bowDrawing) {
            const float size=motionOutput.bowInReach?.045f:.025f;
            motionQuad(nock,XrPose({center,Q(head.orientation)}),size,size,At(panelatlas::Pointer,{{0,32},{64,64}}),pointerChain);
        }
        const int hint=motionOutput.bowDrawing?(motionOutput.bowCharged?2:1):0;
        motionQuad(bowHint,XrPose({bow.p+math::Vec{0,-.25f,0},Q(head.orientation)}),.24f,.045f,
            {{576,768+hint*85},{448,85}},gameplayChain);
    }
    // Bow path laser: the release's own tail and direction, in room space like
    // the arrow guide; resolveBow only yaws and scales it, so it stays straight.
    // Added after the bow's own quads, so it can never displace them.
    bool bowLaserShown=false,bowLaserFloor=false;float bowLaserLength=0;
    if(gameplayVisible&&!settingsModel.open&&!interactionOutput.wheel&&bowLaserImageReady&&motionOutput.bowPhysical&&
       settings::LaserOn(vrsettings::Read().values)&&
       bowlaser::Shown(motionOutput.bowCharged,motionOutput.rayDirection,motionOutput.bowForward)) {
        const auto now=GetTickCount64();
        const bool floorKnown=stageWorld.valid&&stageWorld.upm>0&&now>=stageWorld.at&&now-stageWorld.at<=250;
        const auto path=bowlaser::Trace(motionInput.hand[0].p,motionOutput.rayDirection,floorKnown,
            floorKnown?stageWorld.origin.y-stageWorld.eyeHeight/stageWorld.upm:0);
        if(path.length>.05f) {
            const auto mid=path.start+motionOutput.rayDirection*(path.length*.5f);
            const auto orient=math::BeamOrientation(motionOutput.rayDirection,V(head.position)-mid);
            const auto before=count;
            using namespace bowlaser;
            motionQuad(bowLaser,XrPose({mid,orient}),.004f,path.length,
                At(panelatlas::BowLaser,path.floor?XrRect2Di{{0,SteadyY},{AtlasWidth,SteadyHeight}}:XrRect2Di{{0,FadeY},{AtlasWidth,FadeHeight}}),sharedChain);
            if(path.floor)motionQuad(bowLaserEnd,XrPose({path.end,Q(head.orientation)}),.05f,.05f,At(panelatlas::BowLaser,{{0,DotY},{DotSize,DotSize}}),sharedChain);
            bowLaserShown=count>before;bowLaserLength=path.length;bowLaserFloor=path.floor;
        }
    }
    static bool bowLaserWasShown=false;static ULONGLONG bowLaserReportAt=0;
    if(bowLaserShown&&!bowLaserWasShown&&GetTickCount64()>=bowLaserReportAt) {
        char message[96];std::snprintf(message,sizeof message,"Frame bow laser: shown (length %.1f m%s)",bowLaserLength,bowLaserFloor?", floor":"");
        Say(message);bowLaserReportAt=GetTickCount64()+2000;
    }
    bowLaserWasShown=bowLaserShown;
    XrCompositionLayerQuad book{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(layer&&!settingsModel.open&&!bodyCalibration.Active()&&spatialMode&&(journalMode?journalPlacement.Placed():workspacePlaced)&&uiEye.copied&&uiCrop[2]>0) {
        const auto size=UiSurfaceSize();
        motionQuad(book,UiSurfacePose(),size.width,size.height,
            {{uiCrop[0],uiCrop[1]},{uiCrop[2],uiCrop[3]}},uiEye.chain);
        book.layerFlags=opaqueMenuMode?0:XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    }
    XrCompositionLayerQuad settingsPanel{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(layer&&(settingsModel.open||bodyCalibration.Active())&&settingsImageReady){
        // Extend to the right, without moving or shrinking any control. Input
        // still intersects the original panel. Calibration uses only that crop.
        // The card while help is wanted and the picture shown has one: from one
        // card to the next, the old stays until the new is painted, not a narrow frame.
        const bool showHelp=!bodyCalibration.Active()&&settingsHelpVisible!=settings::NoHelp&&settingsPainted.help!=settings::NoHelp;
        const int pixels=showHelp?settings::AtlasWidth:settings::Width;
        const float fullWidth=settingsWidth*pixels/settings::Width;
        auto pose=settingsPose;
        const auto shift=math::Rotate(Q(pose.orientation),{(fullWidth-settingsWidth)*.5f,0,0});
        pose.position.x+=shift.x;pose.position.y+=shift.y;pose.position.z+=shift.z;
        motionQuad(settingsPanel,pose,fullWidth,settingsHeight,At(panelatlas::Settings,{{0,0},{pixels,settings::Height}}),sharedChain);
    }
    XrCompositionLayerQuad laser{XR_TYPE_COMPOSITION_LAYER_QUAD},dot{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(layer&&(settingsModel.open||panelMode||spatialMode||(interactionVisible&&interactionOutput.wheel))&&pointerChain&&pointerVisible) {
        // The pointer needs the same finite-pose checks and normalization as
        // every other optional layer: one bad laser must not hide the menu.
        motionQuad(laser,beamPose,.003f,beamLength,At(panelatlas::Pointer,{{0,0},{64,32}}),pointerChain);
        if(pointerHit)motionQuad(dot,dotPose,.018f,.018f,At(panelatlas::Pointer,{{0,32},{64,64}}),pointerChain);
    }
    // Head-locked blood: VIEW space, with a bounded angular size so large render
    // frusta cannot push the rim out of sight. It goes directly above
    // the world and below every HUD quad, and only into a spare layer slot.
    XrCompositionLayerQuad bloodQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(bloodImageReady&&bloodShown&&bloodOutput.visible&&layer&&!panelMode&&!blackMenu&&!settingsModel.open&&
       !bodyCalibration.Active()&&headSpace&&count<contentLimit) {
        const auto extent=blood::VisorExtent(
            {views[0].fov.angleLeft,views[0].fov.angleRight,views[0].fov.angleDown,views[0].fov.angleUp},
            {views[1].fov.angleLeft,views[1].fov.angleRight,views[1].fov.angleDown,views[1].fov.angleUp});
        const float width=extent.width,height=extent.height;
        if(std::isfinite(width)&&std::isfinite(height)&&width>0&&height>0) {
            bloodQuad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
            bloodQuad.space=headSpace;bloodQuad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
            bloodQuad.pose.orientation.w=1;bloodQuad.pose.position.z=-blood::VisorDistance;bloodQuad.size={width,height};
            bloodQuad.subImage.swapchain=sharedChain;bloodQuad.subImage.imageRect=Rect(panelatlas::Blood);
            const uint32_t below=count&&layers[0]==layer?1:0;
            for(uint32_t i=count;i>below;--i)layers[i]=layers[i-1];
            layers[below]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&bloodQuad);++count;
        }
    }
    // Guard strain light: head-locked, its shape on the blood's visor extent and
    // the quad grown by the painted image's reach to the edge of the view;
    // inserted at the same slot after the blood, so it sits directly above the
    // world and beneath the blood.
    XrCompositionLayerQuad strainQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(strainImageReady&&strainPaintKey&&strainPaintKey!=~0ull&&strainOutput.visible&&layer&&!panelMode&&!blackMenu&&!settingsModel.open&&
       !bodyCalibration.Active()&&headSpace) {
        auto extent=blood::VisorExtent(
            {views[0].fov.angleLeft,views[0].fov.angleRight,views[0].fov.angleDown,views[0].fov.angleUp},
            {views[1].fov.angleLeft,views[1].fov.angleRight,views[1].fov.angleDown,views[1].fov.angleUp});
        extent.width*=strain::Reach(strainShownReach[0]);extent.height*=strain::Reach(strainShownReach[1]);
        if(count>=contentLimit) {
            if(!strainSlotReported){Say("Frame guard strain: no spare composition layer; light hidden");strainSlotReported=true;}
        } else if(std::isfinite(extent.width)&&std::isfinite(extent.height)&&extent.width>0&&extent.height>0) {
            strainQuad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
            strainQuad.space=headSpace;strainQuad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
            strainQuad.pose.orientation.w=1;strainQuad.pose.position.z=-blood::VisorDistance;strainQuad.size={extent.width,extent.height};
            strainQuad.subImage.swapchain=sharedChain;strainQuad.subImage.imageRect=Rect(panelatlas::Strain);
            const uint32_t below=count&&layers[0]==layer?1:0;
            for(uint32_t i=count;i>below;--i)layers[i]=layers[i-1];
            layers[below]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&strainQuad);++count;
        }
    }
    XrCompositionLayerQuad winePadding{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if(padQuads&&composition::PrependQuadPadding(layers.data(),count,capacity,winePadding,headSpace,pointerChain,{panelatlas::Pointer.x,panelatlas::Pointer.y+32})) {
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
    timing.submitMs=Milliseconds()-start;timing.layers=count;
    timing.pipelines=pipelineCreates.exchange(0);timing.maxPipelineMs=maxPipelineMs.exchange(0);
    lastTiming=timing;frame=false;
}
FrameTiming LastTiming() {std::lock_guard lock(mutex);return lastTiming;}
void NoteFrame(double gameMs,double waitMs,double drawMs,double vrMs,double periodMs,double budgetMs,bool byTurns) {
    std::lock_guard lock(mutex);
    auto& w=performanceWindow;const auto now=GetTickCount64();
    // A gap in frames (menus, loading) begins a new second: what came before
    // it is not averaged in. Switching between turns and pairs is no gap.
    if(!w.since||now-w.last>250){w={};w.since=now;}
    w.last=now;
    if(budgetMs>0){performanceBudget=budgetMs;w.budget+=budgetMs;++w.budgets;}
    w.wait+=waitMs;w.draw+=drawMs;w.vr+=vrMs;++w.frames;w.turns+=byTurns?1:0;
    if(periodMs>0){w.game+=gameMs;w.period+=periodMs;++w.periods;}
    if(now-w.since<1000)return;
    const double k=1.0/w.frames;
    performance.valid=true;performance.byTurns=w.turns*2>=w.frames;performance.mixed=w.turns&&w.turns<w.frames;
    performance.fps=w.periods?float(w.periods*1000.0/w.period):0.f;performance.budgetMs=float(performanceBudget);
    performance.gameMs=w.periods?float(w.game/w.periods):0.f;performance.waitMs=float(w.wait*k);
    performance.drawMs=float(w.draw*k);performance.vrMs=float(w.vr*k);
    const float predictedHz=w.budgets?float(w.budgets*1000.0/w.budget):0.f;
    ++performanceSerial;performanceAt=now;w={};w.since=w.last=now;
    FrameGenNow(now,predictedHz);   // logs what OFXR Bridge does, once a second of frames
}
void ConfigureGameplay(bool enabled) {
    std::lock_guard lock(mutex);
    if(!enabled)gamestate::MenuMap(false);
    if(!enabled)panelinput::WorldInput(false);
    if(!enabled){skel::SetMotionBow(false,false,0);skel::SetInteractionHidden(0);interactions.Reset();interactionstate::Cancel();CancelShieldCast();}
    if(motionControls.exchange(enabled)!=enabled)
        Say(enabled?"Frame gameplay: motion controls enabled (right-handed prototype)":"Frame gameplay: motion controls disabled; legacy buttons restored");
}
void ConfigureQuiver(bool enabled,bool preview) {
    std::lock_guard lock(mutex);
    quiverPreview=preview;
    if(quiverEnabled.exchange(enabled)!=enabled) {
        quiver.Clear();quiverArrow={};
        Say(enabled?"Frame bow quiver: arrow skills load physical arrows":"Frame bow quiver: disabled; arrow skills use native buttons");
    }
}
void ConfigureInteractions(bool seated,float waist,float forward,int dominant) {
    static interaction::Config previous;static bool configured=false;
    interaction::Config c;c.seated=seated;c.waistBelowEyes=std::clamp(waist,.30f,.95f);
    c.beltForward=std::clamp(forward,.03f,.5f);c.holsters=false;c.dominant=dominant==1?1:0;
    if(configured&&c.seated==previous.seated&&c.waistBelowEyes==previous.waistBelowEyes&&
       c.beltForward==previous.beltForward&&c.dominant==previous.dominant)return;
    std::lock_guard lock(mutex);interactions.Configure(c);interactionstate::Cancel();previous=c;configured=true;
}
void ConfigureBottlePhysics(bool on){bottlePhysics=on;}
void ConfigurePropPhysics(bool on){propPhysics=on;propscan::Configure(on);}
void BottleWeapon(int slot,const melee::Blade* blade) {
    if(slot<0||slot>=bottles::Colliders)return;
    std::lock_guard lock(bottleWeaponMutex);
    bottleWeapons[slot]=blade?BottleWeaponPose{*blade,GetTickCount64(),true}:BottleWeaponPose{};
}
bool SettingsOpen(){return settingsOpen.load();}
bool SpellSelected() {
    std::lock_guard lock(mutex);
    return motionControls.load()&&motionInput.active&&!motionOutput.block&&motionOutput.rayValid&&
        (motion.Selection()>=0||motionOutput.skill>=0)&&GetTickCount64()-motionAt<150;
}
bool LeftHandAiming(){std::lock_guard lock(mutex);return motionControls.load()&&castHand==1&&motionInput.active&&GetTickCount64()-motionAt<150;}
bool CrossbowAiming() {
    std::lock_guard lock(mutex);
    if(!motionControls.load()||!motionInput.active||motionOutput.block||castHand==1||
       motionInput.weapon!=gameplay::Weapon::Crossbow||GetTickCount64()-motionAt>100)return false;
    if(crossbowFire)return true;
    if(motionOutput.attack)return true;
    const int slot=motionOutput.skill;
    if(slot<0||slot>=skills::Count||!interaction::Fresh(interactionInput.snapshot,gamestate::LocalPlayer(),GetTickCount64()))return false;
    const auto& skill=interactionInput.snapshot.skills[slot];return skill.known&&skill.available&&skill.id==0;
}
bool NativeSkillCasting() {
    std::lock_guard lock(mutex);
    return motionControls.load()&&gameplay::CastingSkill(motionInput,motionOutput,GetTickCount64()-motionAt);
}
bool NativeAimOwnership() {
    std::lock_guard lock(mutex);
    const bool physical=motionInput.active&&!motionOutput.block&&GetTickCount64()-motionAt<100&&
        (handOutput.spells[0]>=0||handOutput.spells[1]>=0);
    return motionControls.load()&&(physical||interactionOutput.wheel||gameplay::CastingSkill(motionInput,motionOutput,GetTickCount64()-motionAt));
}
int NativeCastSkill() {
    std::lock_guard lock(mutex);
    const auto now=GetTickCount64();
    if(!motionControls.load()||!gameplay::CastingSkill(motionInput,motionOutput,now-motionAt)||
       !interaction::Fresh(interactionInput.snapshot,gamestate::LocalPlayer(),now))return -1;
    const auto& skill=interactionInput.snapshot.skills[motionOutput.skill];
    return skill.known&&skill.available?skill.id:-1;
}
bool DirectedSpellActive() {
    std::lock_guard lock(mutex);
    const int slot=motionOutput.skill;
    const auto now=GetTickCount64();
    if(!motionControls.load()||!gameplay::CastingSkill(motionInput,motionOutput,now-motionAt)||
       !interaction::Fresh(interactionInput.snapshot,gamestate::LocalPlayer(),now))return false;
    const auto& skill=interactionInput.snapshot.skills[slot];
    if(motionOutput.bowReady&&skill.id==weaponskills::Strafe)return false;
    return skill.known&&skill.available&&skill.id>=0&&(skill.directed||
        (motionOutput.bowReady&&weaponskills::Bow(skill.id)&&skill.id!=weaponskills::Strafe));
}
bool BowSkillActive() {
    std::lock_guard lock(mutex);
    const int slot=motionOutput.skill;
    const auto now=GetTickCount64();
    if(!motionControls.load()||!motionOutput.bowReady||!gameplay::CastingSkill(motionInput,motionOutput,now-motionAt)||
       !interaction::Fresh(interactionInput.snapshot,gamestate::LocalPlayer(),now))return false;
    const auto& skill=interactionInput.snapshot.skills[slot];
    return skill.known&&skill.available&&weaponskills::Bow(skill.id);
}
int GroundSpellSkill() {
    // Every ground skill lands where it is aimed, Fire Blast like the rest.
    std::lock_guard lock(mutex);
    const int slot=motionOutput.skill;
    const auto now=GetTickCount64();
    if(!motionControls.load()||!gameplay::CastingSkill(motionInput,motionOutput,now-motionAt)||
       !interaction::Fresh(interactionInput.snapshot,gamestate::LocalPlayer(),now))return -1;
    const auto& skill=interactionInput.snapshot.skills[slot];
    if(!skill.known||!skill.available)return -1;
    return spellaim::GroundTarget(skill.id)?skill.id:-1;
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
// Aim rules: each hand's own aim, in GameplayRay's frame (the head's turn
// undone). The left hand holding a bow aims along the bow (the arrow is in
// its model).
bool HandAimRay(int hand,float outOrigin[3],float outDirection[3]) {
    std::lock_guard lock(mutex);
    if(hand<0||hand>1||!motionControls.load()||!motionInput.active||GetTickCount64()-motionAt>150)return false;
    math::Vec p,d;
    if(hand==1&&motionInput.weapon==gameplay::Weapon::Bow&&motionInput.tracked[1]){p=motionInput.hand[1].p;d=gameplay::BowForward(motionInput.hand[1]);}
    else if(handAimTracked[hand]){p=handAim[hand].p;d=math::Rotate(handAim[hand].q,{0,0,-1});}
    else return false;
    const auto undo=math::Conjugate(math::Yaw(math::Heading(Q(head.orientation))));
    const auto o=math::Rotate(undo,p-V(head.position)),r=math::Rotate(undo,d);
    outOrigin[0]=o.x;outOrigin[1]=o.y;outOrigin[2]=o.z;
    outDirection[0]=r.x;outDirection[1]=r.y;outDirection[2]=r.z;
    return std::isfinite(o.x+o.y+o.z+r.x+r.y+r.z);
}
int CastButtonHand() {
    std::lock_guard lock(mutex);
    return motionControls.load()&&handOutput.slot>=0?handOutput.hand:-1;
}
bool HandSpellHeld(int hand) {
    std::lock_guard lock(mutex);
    return hand>=0&&hand<2&&motionControls.load()&&handOutput.spells[hand]>=0;
}
namespace {
// The fresh native cast's skill row, or nullptr.
const interaction::Skill* CastSkillRow() {
    const int slot=motionOutput.skill;
    const auto now=GetTickCount64();
    if(!motionControls.load()||slot<0||slot>=skills::Count||!gameplay::CastingSkill(motionInput,motionOutput,now-motionAt)||
       !interaction::Fresh(interactionInput.snapshot,gamestate::LocalPlayer(),now))return nullptr;
    const auto& skill=interactionInput.snapshot.skills[slot];
    return skill.known&&skill.available&&skill.id>=0?&skill:nullptr;
}
}
bool TossTarget(int skill,float& x,float& y) {
    std::lock_guard lock(mutex);
    const auto now=GetTickCount64();const auto& t=tossLanding;
    if(t.skill<0||now<t.at||now-t.at>700||t.session!=gamestate::LocalPlayer()||(skill>=0&&skill!=t.skill))return false;
    x=t.x;y=t.y;return true;
}
int AimRulesSkill() {
    std::lock_guard lock(mutex);
    const auto* skill=CastSkillRow();return skill?skill->id:-1;
}
bool AimRulesDirected() {
    std::lock_guard lock(mutex);
    const auto* skill=CastSkillRow();return skill&&skill->directed;
}
// A skill the rules place at a point: ground skills, curses, Corpse Explosion,
// Charge, Teleport...
int PointSpellSkill() {
    std::lock_guard lock(mutex);
    const auto* skill=CastSkillRow();
    if(!skill)return -1;
    const auto use=aimrules::Of(skill->id,skill->directed);
    return aimrules::Placed(use)?skill->id:-1;
}
bool MeleeSample(uint32_t poseStamp,melee::Sample& sample,int hand) {
    std::lock_guard lock(mutex);
    if(!motionControls.load()||!motionInput.active||!motionOutput.meleeContact||GetTickCount64()-motionAt>100)return false;
    if(hand<0||hand>1||!(hand?offhandPoses:meleePoses).Find(poseStamp,Milliseconds()/1000.,sample))return false;
    // the off hand strikes with a second weapon - or, in a druid's form, with the left claw
    if(hand==1&&!sample.claws&&!dualwield::Eligible(skel::HeroClass(),gamestate::MeleeEquipment()))return false;
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
       GetTickCount64()-motionAt>150||!weaponskills::Current(shieldCast.context,LiveShieldContext(),shieldCast.at,GetTickCount64())||!shieldSkill.available||!shieldSkill.motion||
       shieldSkill.id!=weaponskills::Smite||!motionInput.shieldEquipped||motionOutput.block)return -1;
    target=shieldCast.target;shieldCast.taken=true;return 1;
}
bool ShieldAim(float& x,float& y) {
    std::lock_guard lock(mutex);
    if(!shieldCast.issued||shieldCast.cancelled||shieldCast.taken||!motionInput.active||motionOutput.block||motionInput.quickCast||
       motionOutput.skill!=shieldCast.context.slot||shieldSkill.id!=weaponskills::Smite||
       !shieldSkill.available||!shieldSkill.motion||!motionInput.shieldEquipped||GetTickCount64()-motionAt>150||
       !weaponskills::Current(shieldCast.context,LiveShieldContext(),shieldCast.at,GetTickCount64()))return false;
    x=shieldCast.gridTarget.x;y=shieldCast.gridTarget.z;return std::isfinite(x+y);
}
void FinishShieldSkill(bool accepted) {
    std::lock_guard lock(mutex);shieldCast={};shieldPulse.Cancel();
    shieldFeedbackAt=GetTickCount64();shieldRefused=!accepted;
    if(accepted)shieldImpactAt=shieldFeedbackAt;
}
void MeleeImpact(int hand) {if(hand>=0&&hand<2)meleeImpactAt[hand]=GetTickCount64();}
void ShieldGuardImpact(bool parry,float weight) {
    shieldImpactAt=GetTickCount64();
    if(parry){guardParryEvents.fetch_add(1);return;}
    guardBlockWeight.fetch_add(unsigned(std::clamp(std::isfinite(weight)?weight:1.f,0.f,16.f)*1000.f+.5f));
    guardBlockEvents.fetch_add(1);
}
bool GuardBroken() {return guardBroken.load();}
void ShieldParryMiss() {parryMissAt=GetTickCount64();}
void ShieldGuardState(bool eligible,bool raised) {guardStateAt=GetTickCount64();guardEligible=eligible;guardRaised=raised;}
void ConfigureGuardStrain(float perBlock,float resetSeconds,float clear,float breatheSeconds,float softness,float brightness,uint32_t color,float density) {
    strain::Tuning t;
    t.perBlock=std::isfinite(perBlock)?std::clamp(perBlock,0.f,1.f):t.perBlock;
    t.resetSeconds=std::isfinite(resetSeconds)?std::clamp(resetSeconds,0.f,10.f):t.resetSeconds;
    t.clear=std::isfinite(clear)?std::clamp(clear,0.f,strain::FullEdge):t.clear;
    t.breatheSeconds=std::isfinite(breatheSeconds)?std::clamp(breatheSeconds,1.f,10.f):t.breatheSeconds;
    t.feather=std::isfinite(softness)?std::clamp(softness,.05f,1.f):t.feather;
    t.brightness=std::isfinite(brightness)?std::clamp(brightness,0.f,1.f):t.brightness;
    t.color=color&0xFFFFFFu;
    t.density=std::isfinite(density)?std::clamp(density,1.f,6.f):1.f;
    std::lock_guard lock(strainTuningMutex);strainTuning=t;
}
void ConfigureGuardStrainRecover(float seconds) {
    strainRecoverSeconds=std::isfinite(seconds)?std::clamp(seconds,0.f,30.f):1.5f;
}
void ConfigureHeartbeat(float volume,float bpmMin,float bpmMax,float fadeSeconds,const wchar_t* file) {
    heartbeat::Tuning t;
    t.fadeSeconds=std::isfinite(fadeSeconds)?std::clamp(fadeSeconds,0.f,30.f):t.fadeSeconds;
    t.volume=std::isfinite(volume)?std::clamp(volume,0.f,4.f):t.volume;
    t.bpmMin=std::isfinite(bpmMin)?std::clamp(bpmMin,30.f,200.f):t.bpmMin;
    t.bpmMax=std::isfinite(bpmMax)?std::clamp(bpmMax,t.bpmMin,200.f):std::max(t.bpmMax,t.bpmMin);
    const std::wstring path=file?file:L"";
    bool reload=false;
    {std::lock_guard lock(heartMutex);heartTuning=t;reload=path!=heartFile;if(reload)heartFile=path;}
    if(!reload)return;
    auto pcm=std::make_shared<heartbeat::Pcm>();
    bool fromFile=false;
    if(!path.empty()) {
        std::vector<uint8_t> bytes;
        if(FILE* f=_wfopen(path.c_str(),L"rb")){uint8_t buf[65536];size_t n;while((n=fread(buf,1,sizeof buf,f))>0&&bytes.size()<16u<<20)bytes.insert(bytes.end(),buf,buf+n);fclose(f);}
        fromFile=heartbeat::ParseWav(bytes,*pcm);
    }
    bool embedded=false;
    if(!fromFile) {   // the recording embedded by interaction_assets.rc; synthesized if it is missing
        HMODULE module=nullptr;std::vector<uint8_t> bytes;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ConfigureHeartbeat),&module);
        if(HRSRC res=module?FindResourceW(module,L"D2RVR_HEARTBEAT_SOUND",MAKEINTRESOURCEW(10)):nullptr)
            if(HGLOBAL h=LoadResource(module,res))
                if(const auto* p=static_cast<const uint8_t*>(LockResource(h)))bytes.assign(p,p+SizeofResource(module,res));
        embedded=heartbeat::ParseWav(bytes,*pcm);
        if(!embedded)*pcm=heartbeat::BuiltIn();
    }
    {std::lock_guard lock(heartMutex);heartSource=pcm;heartSourceName=fromFile?"ini WAV":embedded?"built-in recording":"synthesized beat";}
    Say(fromFile?"Frame low-life heartbeat: ini WAV loaded":
        !path.empty()?(embedded?"Frame low-life heartbeat: ini WAV missing or not 16-bit PCM; built-in recording":
                                "Frame low-life heartbeat: ini WAV missing or not 16-bit PCM; synthesized beat"):
        embedded?"Frame low-life heartbeat: built-in recording":"Frame low-life heartbeat: recording missing; synthesized beat");
}
void ConfigureGuardBreakSound(float volume,const wchar_t* file) {
    const std::wstring path=file?file:L"";
    bool reload=false;
    {std::lock_guard lock(breakMutex);breakVolume=std::isfinite(volume)?std::clamp(volume,0.f,4.f):1.f;reload=path!=breakFile;if(reload)breakFile=path;}
    if(!reload)return;
    auto pcm=std::make_shared<heartbeat::Pcm>();
    bool fromFile=false,embedded=false;
    if(!path.empty()) {
        std::vector<uint8_t> bytes;
        if(FILE* f=_wfopen(path.c_str(),L"rb")){uint8_t buf[65536];size_t n;while((n=fread(buf,1,sizeof buf,f))>0&&bytes.size()<16u<<20)bytes.insert(bytes.end(),buf,buf+n);fclose(f);}
        fromFile=heartbeat::ParseWav(bytes,*pcm);
    }
    if(!fromFile) {   // the recording embedded by interaction_assets.rc
        HMODULE module=nullptr;std::vector<uint8_t> bytes;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ConfigureGuardBreakSound),&module);
        if(HRSRC res=module?FindResourceW(module,L"D2RVR_GUARD_BREAK_SOUND",MAKEINTRESOURCEW(10)):nullptr)
            if(HGLOBAL h=LoadResource(module,res))
                if(const auto* p=static_cast<const uint8_t*>(LockResource(h)))bytes.assign(p,p+SizeofResource(module,res));
        embedded=heartbeat::ParseWav(bytes,*pcm);
    }
    {std::lock_guard lock(breakMutex);breakSource=fromFile||embedded?pcm:nullptr;}
    Say(fromFile?"Frame guard break: ini WAV loaded":
        embedded?(path.empty()?"Frame guard break: built-in bone snap":"Frame guard break: ini WAV missing or not 16-bit PCM; built-in bone snap"):
        "Frame guard break: sound missing; silent");
}
bool OwnsControllerRumble() {return motionControls.load()&&controllerRumbleLease.Owns(GetTickCount64());}
void SpellCast(int hand,uint64_t session) {spellHaptics.Publish(hand,session,GetTickCount64());}
void SpellVisualCast(const bowflight::Shot& shot) {
    if(shot.kind!=bowflight::Kind::Spell)return;
    std::lock_guard lock(mutex);
    elementalState.Accepted(shot.hand,{{shot.session,shot.epoch,shot.binding,shot.equipment},
        GetTickCount64(),shot.activation,shot.slot,shot.skill,shot.owner});
}
void SetBowWorldEpoch(uint32_t epoch) {
    std::lock_guard lock(mutex);
    if(bowWorldEpoch==epoch)return;
    bowWorldEpoch=epoch;bowNeedsWorld=bowHasShot=leftNeedsWorld=leftHasShot=false;CancelShieldCast();
    elementalState.Reset();elementalScene={};
    if(motionInput.weapon==gameplay::Weapon::Bow||motionInput.weapon==gameplay::Weapon::Javelin){motion.Cancel();motionOutput={};}
}
uint64_t BowRenderTime(){return bowRenderTime.load();}
bowflight::Gate BowGate() {
    std::lock_guard lock(mutex);
    return {motionControls.load()&&motionInput.active&&!motionOutput.block,
        gamestate::LocalPlayer(),(meleeEpoch<<32)|bowWorldEpoch,motionAt,motionInput.equipment,
        motionOutput.bowReady||motionOutput.javelinReady};
}
bool TakeBowShot(bowflight::Shot& shot) {
    std::lock_guard lock(mutex);
    // The older release first: the bow refuses an older sequence after a newer one.
    if(bowHasShot&&(!leftHasShot||bowReadyShot.sequence<throwLeftReady.sequence)){bowHasShot=false;shot=bowReadyShot;return true;}
    if(leftHasShot){leftHasShot=false;shot=throwLeftReady;return true;}
    return false;
}
bool TakeCrossbowShot(bowflight::Shot& shot) {
    std::lock_guard lock(mutex);const auto now=GetTickCount64();
    if(!crossbowFire||!motionControls.load()||!motionInput.active||motionOutput.block||settingsOpen.load()||
       now-motionAt>100||crossbowTakenAt==motionAt)return false;
    crossbowTakenAt=motionAt;
    crossbow::Pose pose;crossbow::Launch launch;
    if(!skel::CrossbowPose(pose)||!crossbow::Fresh(pose,now,gamestate::LocalPlayer(),motionInput.equipment,bowWorldEpoch)||
       !crossbowvr::MakeShot(pose,(meleeEpoch<<32)|bowWorldEpoch,launch))return false;
    shot=launch.flight;shot.binding=motionInput.skillContext;shot.activation=crossbowActivation;return true;
}
bool CrossbowHeld(const bowflight::Shot& shot) {
    std::lock_guard lock(mutex);const auto now=GetTickCount64();
    return shot.kind==bowflight::Kind::Crossbow&&crossbowFire&&motionControls.load()&&motionInput.active&&!motionOutput.block&&
        !settingsOpen.load()&&now-motionAt<=100&&motionInput.weapon==gameplay::Weapon::Crossbow&&
        shot.session==gamestate::LocalPlayer()&&shot.equipment==motionInput.equipment&&shot.binding==motionInput.skillContext&&
        shot.activation==crossbowActivation&&shot.epoch==((meleeEpoch<<32)|bowWorldEpoch);
}
bool TakeSpellShot(int hand,bowflight::Shot& shot) {
    std::lock_guard lock(mutex);
    if(hand<0||hand>1||!spellHasShot[hand])return false;
    spellHasShot[hand]=false;shot=spellReadyShot[hand];return true;
}
bool SpellHeld(const bowflight::Shot& shot) {
    std::lock_guard lock(mutex);
    const auto now=GetTickCount64();
    if(shot.kind!=bowflight::Kind::Spell||shot.hand<0||shot.hand>1||shot.slot<0||shot.slot>=skills::Count||
       !motionControls.load()||!motionInput.active||motionOutput.block||now-motionAt>100||
       handOutput.spells[shot.hand]!=shot.slot||handOutput.activation[shot.hand]!=shot.activation||shot.binding!=motionInput.skillContext||
       shot.epoch!=((meleeEpoch<<32)|bowWorldEpoch)||shot.equipment!=motionInput.equipment||
       !interaction::Fresh(interactionInput.snapshot,shot.session,now)||shot.session!=gamestate::LocalPlayer())return false;
    const auto& skill=interactionInput.snapshot.skills[shot.slot];
    return skill.known&&skill.available&&skill.physical&&skill.id==shot.skill&&skill.owner==shot.owner;
}
bool BowPreviewPose(bowflight::Shot& pose,bool& visible) {
    std::lock_guard lock(mutex);
    const bowflight::Gate gate{motionControls.load()&&motionInput.active&&motionOutput.bowReady,
        gamestate::LocalPlayer(),(meleeEpoch<<32)|bowWorldEpoch,motionAt,motionInput.equipment};
    if(!bowflight::Allowed(bowPreviewPose,gate,GetTickCount64()))return false;
    pose=bowPreviewPose;visible=bowPreviewVisible;return true;
}
bool View(float out[16],const float hero[3],float eyeHeight,float userEyeHeight,float bodyHeading,float recenterYaw,int eye,float mapNorth,float scaleHeight) {
    std::lock_guard lock(mutex);if(!Active())return false;
    if(std::fabs(recenterYaw-lastRecenter)>.001f){origin=V(head.position);lastRecenter=recenterYaw;}
    const float upm=(scaleHeight>0?scaleHeight:eyeHeight)/std::max(userEyeHeight,.5f);
    const auto base=math::Yaw(bodyHeading-recenterYaw);
    if(eye<0)compassFrame=wristHeading.Update(gamestate::LocalPlayer(),motionHeadTracked,
        Q(head.orientation),bodyHeading-recenterYaw,mapNorth);
    if(eye<0)stageWorld={true,origin,{hero[0],hero[1],hero[2]},base,upm,eyeHeight,GetTickCount64()};
    const auto resolveBow=[&](bowflight::Shot shot) {
        const auto offset=math::Rotate(base,shot.origin-origin);
        shot.origin={hero[0]/upm+offset.x,(hero[1]+eyeHeight)/upm+offset.y,hero[2]/upm+offset.z};
        shot.direction=math::Normalized(math::Rotate(base,shot.direction));
        shot.player={hero[0]/upm,hero[1]/upm,hero[2]/upm};shot.unitsPerMetre=upm;
        return shot;
    };
    for(int h=0;h<2;++h) {
        const int slot=handOutput.spells[h];
        if(!motionControls.load()||!motionInput.active||motionOutput.block||slot<0||!handAimTracked[h]) {
            spellHasShot[h]=false;continue;
        }
        if(spellResolvedAt[h]==motionAt)continue; // same frozen pose for both eyes
        spellResolvedAt[h]=motionAt;
        const auto& skill=interactionInput.snapshot.skills[slot];
        bowflight::Shot shot;shot.kind=bowflight::Kind::Spell;
        shot.direction=math::Rotate(handAim[h].q,{0,0,-1});
        shot.origin=handAim[h].p+shot.direction*.05f;
        shot.at=motionAt;shot.session=gamestate::LocalPlayer();shot.sequence=++bowSequence;
        shot.epoch=(meleeEpoch<<32)|bowWorldEpoch;shot.equipment=motionInput.equipment;
        shot.hand=h;shot.slot=slot;shot.skill=skill.id;shot.owner=skill.owner;shot.binding=motionInput.skillContext;
        shot.activation=handOutput.activation[h];
        spellReadyShot[h]=resolveBow(shot);spellHasShot[h]=true;
    }
    bowflight::Shot held;
    held.origin=motionOutput.bowDrawing?motionInput.hand[0].p:motionOutput.nockPoint;
    held.direction=motionOutput.bowDrawing?motionOutput.rayDirection:motionOutput.bowForward;
    held.draw=motionOutput.draw;held.at=motionAt;held.session=gamestate::LocalPlayer();
    held.epoch=(meleeEpoch<<32)|bowWorldEpoch;held.equipment=motionInput.equipment;held.sequence=1;
    if(quiverArrow.Active()&&quiverPreview.load()){held.skill=quiverArrow.skill;held.owner=quiverArrow.owner;held.missile=quiverArrow.missile;}
    bowPreviewPose=resolveBow(held);bowPreviewVisible=motionOutput.bowDrawing||motionOutput.bowInReach;
    const bowflight::Gate previewGate{motionControls.load()&&motionInput.active&&!motionOutput.block,
        held.session,held.epoch,motionAt,motionInput.equipment,motionOutput.bowReady};
    bowvr::PublishPreviewPose(bowPreviewPose,previewGate,bowPreviewVisible,bowRenderTime.load());
    // Resolve the release once in the SAME stage-to-world frame as this view.
    // Absolute room poses avoid mixing an old weapon bone with a newer head yaw.
    const bowflight::Gate releaseGate{motionControls.load()&&motionInput.active&&(motionOutput.bowReady||motionOutput.javelinReady),
        gamestate::LocalPlayer(),(meleeEpoch<<32)|bowWorldEpoch,motionAt,motionInput.equipment};
    if(leftNeedsWorld) {
        leftNeedsWorld=false;
        const auto shot=resolveBow(throwLeftPending);
        if(bowflight::Allowed(shot,releaseGate,GetTickCount64())){throwLeftReady=shot;leftHasShot=true;}
    }
    if(bowNeedsWorld) {
        bowNeedsWorld=false;
        const auto& gate=releaseGate;
        const auto shot=resolveBow(bowPending);
        if(bowflight::Allowed(shot,gate,GetTickCount64())){bowReadyShot=shot;bowHasShot=true;}
        else if(bowflight::SkillRelease(shot)) {
            char line[200];std::snprintf(line,sizeof line,"Frame bow quiver: release rejected skill=%d missile=%d valid=%d",
                shot.skill,shot.missile,int(bowflight::Valid(shot)));
            Say(line);
        }
    }
    const auto& pose=eye<0?head:views[eye&1].pose;
    const auto offset=math::Rotate(base,V(pose.position)-origin)*upm;
    const math::Vec position={hero[0]+offset.x,hero[1]+eyeHeight+offset.y,hero[2]+offset.z};
    const auto turn=math::Multiply(base,Q(pose.orientation));
    if(eye>=0){viewBase[eye&1]=base;auto& w=viewWorld[eye&1];w.at=position;w.turn=turn;w.ok=true;w.hero={hero[0],hero[1],hero[2]};}
    if(eye==0){   // the replay's eye gap in this view's units: along the left eye's x
        const auto apart=math::Rotate(math::Conjugate(Q(views[0].pose.orientation)),V(views[1].pose.position)-V(views[0].pose.position));
        replayShift=apart.x*upm;
    }
    math::View(position,turn,out);return true;
}
bool Projection(float out[16],float nearWorld,int eye) {
    std::lock_guard lock(mutex);if(!Active())return false;
    viewWorld[eye&1].nearPlane=nearWorld;
    const auto& f=views[eye&1].fov;
    math::Projection(f.angleLeft,f.angleRight,f.angleDown,f.angleUp,nearWorld,out);return true;
}
void Shutdown(bool preserveResolution) {
    std::lock_guard lock(mutex);
    hapticPulses.Stop();
    // Keep the HUD hidden through an intentional eye-target/session rebuild.
    if(!preserveResolution)hud::NativeSession(false);
    if(!preserveResolution)resolution::Restore();
    windowFocus.Cancel();
    EndPair();ClearInput();interactionPainter.Stop();bloodPainter.Stop();strainPainter.Stop();noticePainter.Stop();settingsPainter.Stop();turnCompiler.Stop();interaction::icons::Shutdown();
    // Completion is mandatory before destroying our commands/resources, even
    // when ordinary frame submission uses queue ordering without a CPU wait.
    if(fence&&!WaitGpu())failed=true;
    if(uiEye.chain&&pDestroySwapchain)pDestroySwapchain(uiEye.chain);uiEye={};journalMode=spatialMode=workspacePlaced=false;hud::UiFrame(false,false,0);uiSupported=false;uiBlit={};potionModels.Reset();potionScene={};
    elementalRenderer.Reset();elementalState.Reset();elementalScene={};
    if(mapEye.chain&&pDestroySwapchain)pDestroySwapchain(mapEye.chain);mapEye={};mapSupported=false;mapBlit={};gamestate::MenuMap(false);hud::MenuMapCapture(false);
    settingsModel.Close();settingsOpen=false;settingsInputGate.Suspend();settingsPainted={};settingsAtlasPending=settingsImageReady=false;
    settingsPaintRequest=0;settingsRequested={};
    bodyCalibration.Cancel();calibrationRelease={};calibrationPrompted=floorSpace=false;calibrationSession=0;calibrationPaintKey=calibrationRequestedKey=-1;
    if(sharedChain&&pDestroySwapchain)pDestroySwapchain(sharedChain);sharedChain=XR_NULL_HANDLE;
    sharedImages.clear();sharedMaster.Reset();sharedCommands.Reset();sharedAllocator.Reset();sharedFence=0;sharedDirty=false;
    atlasUploads.clear();atlasStagingFree.clear();interactionPaintKey=~0ull;interactionPrefetchSession=0;interactionPrefetchRevision=~0u;interactions.Reset();interactionstate::Cancel();
    interactionPaintRequest=0;interactionRequestedKey=~0ull;interactionPaintValid=false;interactionPaintContext={};interactionRequestedContext={};interactionPaintAt=0;
    if(interactionChain&&pDestroySwapchain)pDestroySwapchain(interactionChain);interactionChain=XR_NULL_HANDLE;
    if(companionChain&&pDestroySwapchain)pDestroySwapchain(companionChain);companionChain=XR_NULL_HANDLE;
    companionAtlasPending=companionPaintValid=false;companionPaintContext={};
    compassChain=XR_NULL_HANDLE;wristHeading.Reset();compassFrame=compassAtlasReady=false;   // staticChain, below
    gameplayChain=XR_NULL_HANDLE;
    bloodScreen.Reset();bloodOutput={};bloodShown=0;bloodPaintKey=bloodRequestedKey=~0ull;bloodPaintRequest=0;bloodPaintAt=0;bloodAtlasPending=bloodImageReady=false;
    bowLaserHue=-1;bowLaserAtlasPending=bowLaserImageReady=false;
    noticeWatch={};noticeCards.Clear();noticeCard={};noticePaintRequest=noticeRequestedSequence=noticePaintedSequence=0;noticeAtlasPending=noticeImageReady=false;
    heartStream.Stop();heartPlaying=false;heartLevel.Reset();heartAt=0;heartHush.Reset();heartHealth=-1;
    guardStrain.Reset();strainOutput={};strainPaintKey=~0ull;strainPaintSession=0;strainPaintAt=0;strainAtlasPending=strainImageReady=strainSlotReported=false;
    if(staticChain&&pDestroySwapchain)pDestroySwapchain(staticChain);staticChain=pointerChain=XR_NULL_HANDLE;
    if(aimSpace&&pDestroySpace)pDestroySpace(aimSpace);aimSpace=XR_NULL_HANDLE;
    if(leftAimSpace&&pDestroySpace)pDestroySpace(leftAimSpace);leftAimSpace=XR_NULL_HANDLE;
    if(gazeSpace&&pDestroySpace)pDestroySpace(gazeSpace);gazeSpace=XR_NULL_HANDLE;
    gazeAction=XR_NULL_HANDLE;gazeReady=gazeValid=gazeExtension=gazeSupported=false;gazeStability.Reset();
    detectedControllers={};controllerLabels={};controllerProfilesDirty=true;frameControllerExtension=false;
    for(auto& e:eyes){if(e.chain&&pDestroySwapchain)pDestroySwapchain(e.chain);e={};}
    mirrorSupported=mirrorMono=false;
    turnFrame=keepTurns=keepDepth=false;lastEyes={};viewBase={{{0,0,0,1},{0,0,0,1}}};viewWorld={};turnDepths={};turnCounts={};turnImages={};turnKept={};turnImagesFailed=turnWarpTried=false;turnWarp={};
    turnLayers={};layerKept={};turnTarget.Reset();layersFailed=false;
    turnCapture.left=0;turnCapture.buffer.Reset();turnCapture.size=0;turnCapture.list.Reset();turnCapture.allocator.Reset();
    for(auto& s:handSpaces){if(s&&pDestroySpace)pDestroySpace(s);s=XR_NULL_HANDLE;}
    if(headSpace&&pDestroySpace)pDestroySpace(headSpace);if(space&&pDestroySpace)pDestroySpace(space);
    if(session&&pDestroySession)pDestroySession(session);if(actionSet&&pDestroyActionSet)pDestroyActionSet(actionSet);
    // Destroying the instance unloads OFXR Bridge: its entry points go first.
    {std::lock_guard lock(frameGenMutex);frameGenSnapshot=nullptr;frameGenRequest=nullptr;frameGenStart=framegen::Start::NotAsked;frameGenRead=false;}
    runtimeEnumerateImages=nullptr;
    if(instance&&pDestroyInstance)pDestroyInstance(instance);
    session=XR_NULL_HANDLE;instance=XR_NULL_HANDLE;actionSet=XR_NULL_HANDLE;headSpace=space=XR_NULL_HANDLE;
    hapticActions.Reset();
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
