#pragma once
// The Ring skill menu's doors into transport (steamvr/transport.cpp): one direct call per place it reads or changes a frame, no hook
// table. Each is called once a frame whether a wheel is open or not (the setting's Off edge drains on the next frame); with the
// Ring skill menu Off every one returns at its first line and upstream's wheel runs exactly as it always did.
// The frame structs hold transport's own values by plain pointers: OpenXR's types stay out of this header (poses and the quad
// are untyped pointers to transport's XrPosef and XrCompositionLayerQuad; the request and reply are interaction::Request and
// interaction::Reply). wheelHand: handControls.Wheel (0 right, 1 left, -1 closed); page: int(interactions.Page).
#include <cstdint>
#include <d3d12.h>

namespace framevr::ringwheel::host {
// Pointer: transport Input, the line after MotionPointer: the pointer as MotionPointer left it, edited in place. palettePose,
// beamPose, dotPose: XrPosef*; beamLength (m); pointerHit; hover: motionInput.hover (an upstream WheelCell id, -1 none); time:
// motionInput.time (s); head, aim: the head and the pointer hand's aim ray, room meters as x,y,z then x,y,z,w; pointerVisible: the
// beam shown; cell: upstream's WheelCell for this frame's page and state at (u, v) of upstream's 768 px picture; aims / aimsTracked
// / hands: both hands' aim poses (const gameplay::Pose[2]), their tracked bits and each hand's buttons (const skills::HandInput*).
struct PointerFrame {
    int wheelHand, page; double time;
    void* palettePose; void* beamPose; void* dotPose;
    float& beamLength; bool& pointerHit; int& hover;
    float head[7]; float aim[7]; bool aimTracked; bool& pointerVisible;
    int (*cell)(float u, float v);
    void (*say)(const char*);
    const void* aims; const bool* aimsTracked; const void* hands;
};
// Input: transport Input, the line before interactions.Step: what upstream's wheel engine reads this frame, edited in place: hover
// and trigger (the pointer hand's, 0-1); hands (const skills::HandInput*), snapshot (const interaction::Snapshot*), layout
// (controllers::Layout, transport's controllerLabels); engineIn: interaction::Input*, put back by Request.
struct InputFrame { int wheelHand, page; int& hover; float& trigger; void (*say)(const char*);
                    const void* hands; const void* snapshot; int layout; void* engineIn; };
// Tracking: transport Input, the line after `handInput.active=...`: the world-locked ring's wheel hand is excused while the pointing
// hand is tracked (active edited in place), time and context (handInput's, edited in place while the wheel is open), the binding
// session, the epoch, upstream's lootGate terms and whether a local player exists.
struct TrackingFrame { int wheelHand; bool base; const bool* tracked; const bool* aim; bool* active; void (*say)(const char*);
                       double* time; uint64_t* context; uint64_t session, epoch; uint32_t gate; bool localPlayer; };
// Blip: transport Input, before upstream's unfocused ClearInput; true skips this frame's input (a blip keeps the open wheel).
// ending: the session is stopping or lost (never held); name: the close's reason.
struct BlipFrame { int wheelHand; bool ending; uint64_t nowMs; const char* name = nullptr; };
// Request: transport Input, the line after interactions.Step: the engine's request this frame (interaction::Request*, action None
// when it sent none; the ring may fill one) and the latest reply (const interaction::Reply*).
struct RequestFrame { int wheelHand; void* request; const void* reply; void (*say)(const char*);
                      const void* snapshot; uint64_t session, now; void* engineIn; };
// Layer: transport EndPair, its own statement right after the if that submits upstream's wheel quad, once a headset frame. open:
// interactionVisible and the wheel open; palette: that quad (XrCompositionLayerQuad*); session, create, destroy, enumerate,
// acquire, wait, release: transport's own XrSession* and PFN_xr* functions; device, queue: transport's; format: its atlasFormat
// (DXGI_FORMAT); palettePose: XrPosef*; output: const interaction::Output*; snapshot: const interaction::Snapshot*; quad: adds an
// XrCompositionLayerQuad (q) at pose (x,y,z then x,y,z,w) w x h of chain's rect through transport's motionQuad (premultiplied).
struct LayerFrame { bool open; void* palette; uint64_t nowMs; void (*say)(const char*);
                    const void* session; const void* create; const void* destroy; const void* enumerate;
                    const void* acquire; const void* wait; const void* release;
                    ID3D12Device* device; void* queue; int format; void* palettePose; const void* output; const void* snapshot;
                    void* quadCtx; void (*quad)(void* ctx, void* q, const float* pose, float w, float h, const int* rect, uint64_t chain);
                    bool swapchainsShort = false; };   // transport's swapchainsShort: the runtime has no swapchain left for a panel (the ring gives its own back)
// Beam: transport EndPair, its own statement right after the if that submits the laser and its dot, once a headset frame: the
// non-owner hand's thin beam. open: the wheel's laser could show; quadCtx / quad: motionQuad (the last argument: premultiplied);
// chain: pointerChain; beamRect: the beam's place in the pointer atlas (x, y, w, h); count / limit: the frame's layers so far and
// transport's contentLimit (the beam keeps 2 slots for the blood and strain quads).
struct BeamFrame { bool open; void* quadCtx; void (*quad)(void* ctx, void* q, const float* pose, float w, float h, const int* rect, uint64_t chain, bool premultiplied);
                   uint64_t chain; bool premultiplied; int beamRect[4]; uint32_t count, limit; void (*say)(const char*); };
// Cast: transport Input, the line before glyph casting's press step: a quick cast queued by the ring starts through upstream's own
// glyph press. cast (glyphcast::Cast*), snapshot (const interaction::Snapshot*, quickSnapshot), fresh, active (motionInput.active),
// wheel / block (interactionOutput's), session, now (the glyph clock, s), park (interactionstate::Park), press (sets glyphPress to
// this skill, a const interaction::Skill*), pulse (a haptic pulse on a hand: strength 0-1, ns).
struct CastFrame {
    void* cast; const void* snapshot; bool fresh, active, wheel, block; uint64_t session; double now;
    bool (*park)(uint64_t session, int skill, int owner);
    void (*press)(const void* skill);
    void (*pulse)(int hand, float strength, uint64_t ns);
    void (*say)(const char*);
};
// Hands: transport Input, the line before handControls.Step: hands (skills::HandInput*, handInput), edited in place: a sub-menu's
// button press sets click[h] for one frame, so HandControls opens that hand's wheel as a stick click does. lx, ly, rx, ry: the pad's
// sticks, edited in place. say: transport's Say.
struct HandsFrame { void* hands; int wheelHand; int16_t& lx; int16_t& ly; int16_t& rx; int16_t& ry; void (*say)(const char*); };

bool Active();    // transport calls a door only while this is true: the setting is On, or the ring still has state to drain after an Off edge (Off: one cached read a frame)
bool Enabled();   // the Ring skill menu setting (VR Settings > Menus): Off is upstream's wheel exactly
void Pointer(PointerFrame f);
void Input(InputFrame f);
void Tracking(TrackingFrame f);
bool Blip(BlipFrame f);
void Request(RequestFrame f);
void Layer(LayerFrame f);
void Beam(BeamFrame f);
void Cast(CastFrame f);
void Hands(HandsFrame f);
void Cleared();      // transport's ClearInput: the open wheel's close line when the input is cleared
void Session();      // transport's session start (a new OpenXR session: the ring forgets what it said about upstream)
void Reset();        // shutdown: frees what the ring holds, enabled or not
}  // namespace framevr::ringwheel::host
