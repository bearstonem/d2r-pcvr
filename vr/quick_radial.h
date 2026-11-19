#pragma once
// Quick cast radial: what transport hands the radial's glue (vr/quick_radial.cpp), one struct a call site. Pointers are the
// transport's own types behind void* so this header stays light; the glue casts them back. The radial's state shared with the
// ring menu (Enabled, ButtonSlot, SetButton, the link) is in steamvr/quick_radial.h and steamvr/quick_radial_link.h.
#include <cstdint>

namespace framevr::quickradial::rt {
// Input: transport, once a frame, the line before the right stick's gestures. lx, ly, rx, ry: the pad's sticks (controls),
// edited in place: the radial's hand's stick is zeroed while the ring owns it (no walk, turn, run toggle or loot names), and read
// before that as the ring's clock hand. walkHand: the physical hand (0 right, 1 left) whose controller has the pad's left stick.
// srgb: the shared image is sRGB. upload: a picture (const std::vector<uint8_t>*, premultiplied RGBA from the worker) into that
// region of upstream's shared dynamic image (staged now, copied at the frame's end, as upstream's own pictures). say: transport's Say.
struct InputFrame { int16_t& lx; int16_t& ly; int16_t& rx; int16_t& ry; int walkHand; bool srgb;
                    bool (*upload)(int x, int y, int w, int h, const void* pixels); void (*say)(const char*); };
// Press: transport, once a frame, after resolveHandSubtile's definition: the press upstream decided this frame. slot / hand:
// the held hand slot (-1 none) and its hand (0 right, 1 left); wheel: a wheel or the ring menu is open; occupied: the hand took a
// potion or a holster; snapshot: const interaction::Snapshot*; fresh: the snapshot is fresh; glyphDraw: const int[2] (the hands'
// Draw glyph slots); hands: const skills::HandInput*; head: x, y, z then x, y, z, w; time: seconds. requested / interact: edited
// in place while the ring holds the press (the holding hand casts and attacks nothing). pulse: a haptic pulse (0-1, ns).
struct PressFrame { int slot, hand; bool wheel, occupied; const void* snapshot; bool fresh; const int* glyphDraw; const void* hands;
                    float head[7]; bool headTracked; double time; int& requested; bool& interact;
                    void (*pulse)(int hand, float strength, uint64_t ns); void (*say)(const char*); };
// Cast: transport, the line before glyph casting's press step, once a frame. A cast queued by the ring starts here through
// upstream's own glyph press: cast (glyphcast::Cast*), snapshot, fresh, active (motionInput.active), wheel / block
// (interactionOutput's: the cast waits for a shut wheel), session, now (the glyph clock, s), park (interactionstate::Park),
// press (sets glyphPress to this skill, a const interaction::Skill*), pulse, say.
struct CastFrame { void* cast; const void* snapshot; bool fresh, active, wheel, block; uint64_t session; double now;
                   bool (*park)(uint64_t session, int skill, int owner); void (*press)(const void* skill);
                   void (*pulse)(int hand, float strength, uint64_t ns); void (*say)(const char*); };
// Layer: transport EndPair, after every upstream content layer: the ring's quads from the shared image, all or none. layer: the
// projection is submitted, no panel mode; chain: the shared dynamic image; quadCtx / quad: motionQuad; count / limit: the frame's
// layers so far and contentLimit.
struct LayerFrame { bool layer; uint64_t chain; void* quadCtx;
                    void (*quad)(void* ctx, void* q, const float* pose, float w, float h, const int* rect, uint64_t chain, bool premultiplied);
                    uint32_t count, limit; void (*say)(const char*); };
// Request: transport, the line after interactions.Step: the engine's request this frame (interaction::Request*, action None
// when it sent none) and the latest reply (const interaction::Reply*). The ring's hold to assign on its Quick cast entry is
// taken here, and an item's use is filled in here when the request is free.
struct RequestFrame { int wheelHand; void* request; const void* reply; void (*say)(const char*); const void* snapshot; uint64_t session, now; };
void Input(InputFrame f);
void Press(PressFrame f);
void Cast(CastFrame f);
void Layer(const LayerFrame& f);
void Request(RequestFrame f);
// vr/quick_items.cpp Use (UI thread), its first statement: the radial's own in-flight inventory use may name its item code for
// that call. Returns the item code the call may use: `assigned`, or the radial's own request's code; the caller's global is never changed.
uint32_t QuickUse(const void* request, uint32_t assigned);
}  // namespace framevr::quickradial::rt
