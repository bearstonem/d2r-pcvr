#pragma once
#include <cstdint>
namespace panelinput {
void Install(); // timer thread; retries while native code is still encrypted
void OwnPointer(); // before native menu pointer injection; no game-thread calls
void Typed(uintptr_t extra); // game window thread: a key-down and its extra information (framevr::panels::TypedKeys)
void UseController(); // restore native focus/snapping, retain protection for queued mouse releases
void ItemHover(bool active,bool hit=false,bool dragging=false); // XR thread: valid laser hover only
void WorldInput(bool active); // XR thread: bounded ownership of tracked gameplay input
void MaintainWorldController(bool player,bool menu); // UI thread: restore native interaction targeting
bool BlocksWorld(); // includes queued release after the menu closes
bool OverItem(); // the game's cursor is on an item (its tooltip was asked for in the last 150 ms)
bool ControllerReady(); // UI thread: request native controller mode, report when committed
bool ControllerAction(void (*action)(void*),void*); // synchronous native binding operation, controller bank only
}
