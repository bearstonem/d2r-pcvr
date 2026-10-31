#pragma once
#include <cstdint>
namespace gamestate {
bool JournalOnly();
bool OpaqueMenuOpen();
bool UpperBandMenuOpen();
bool LeatherMenuOpen();   // the tabbed menu (or a cube from it) with no world-opened panel: leather_menu.h
bool CursorItemHeld();    // an item on the game's cursor, read in the last 600 ms (a drop outside the menu)
uint64_t CursorItemReadAt();   // when CursorItemHeld's value was read (GetTickCount64)
bool StatsButtonShown();  // the tabbed menu alone (not a cube or world panel): its Advanced Stats button, stats_button.h
uint32_t PanelChanges();  // counts every panel opened or closed (leather::Back)
bool CloseLeatherMenu();  // queue: close the tabbed menu if only it and its pages are open, the cursor empty
bool StowLeatherMenu();   // queue: the held menu put away over the left shoulder (any page or dialog; not with an item on the cursor)
bool NpcChoicesOnly();
bool NpcChoicesRect(int32_t out[4]);
void ReadNpcChoicesRect(); // UI thread only
bool JournalRect(int32_t out[4]);
void ReadJournalRect(); // UI thread only
}
namespace hud {
bool LayerFound();
void UiFrame(bool active,bool workspace,int eye);
// Hide the native HUDPanel for the entire XR session, including menus/loading.
// Per-eye gameplay cropping is separate so native menu content is not erased.
void NativeSession(bool active);
void NativeGameplay(bool active);
void EndNativeEye();
bool NativeToolbarHidden();
// Menu map lease is maintained by the game's UI thread. Each XR pair must
// capture a new native map before its texture can be submitted in either eye.
void MenuMapCapture(bool active);
void MapFrame();
// full (optional): the texture is the game's full map's box (Map view = Full map); gameZoom: the
// game zoom it was drawn at (0: the game's own).
uint64_t MapTexture(uint32_t& width,uint32_t& height,bool* full=nullptr,float* gameZoom=nullptr);
// Owned copy of UI only, in R16G16B16A16_FLOAT, shader-resource state.
// RGB is premultiplied, alpha is world transmittance; convert before XR use.
uint64_t UiTexture(uint32_t& width,uint32_t& height,int32_t crop[4]);
// By turns: the interface (labels over monsters, names, chat) taken out of each
// eye's picture once its pass drew it, for the transport to lay back over each
// eye. TakenInterface: this eye pass's copy, as UiTexture's, if it was taken
// out; 0 if the interface is still in the picture.
void TakeInterface(bool on);
uint64_t TakenInterface(uint32_t& width,uint32_t& height);
}
