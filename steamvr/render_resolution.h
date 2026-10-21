#pragma once
#include <windows.h>
#include "vr_resolution.h"
namespace framevr::resolution {
void Register(void (*log)(const char*));
void Attach(HWND window);
bool Handle(HWND window,UINT message,WPARAM w,LPARAM& l,WNDPROC original,LRESULT& result);
void Update(HWND window,bool xrRunning,Size recommended,Size maximum,Size source,int choice);
void Restore();
// Native D2R Size ABI: width in the low 32 bits, height in the high 32.
// Fit a widescreen band in the applied size, not a queued request. Desktop
// remains a passthrough; the world source and pointer conversion stay full-size.
uint64_t UiSize(uint64_t nativeSize);
// Pointer bounds cover the whole source, including pixels below the UI band.
uint64_t CursorSize(uint64_t nativeSize);
// Reflow once if the signature-guarded UI hook became available after resize.
void UiSizeReady();
// Called before native UI dispatch; recovery is deferred until after Apply.
void OptionsApplied();
void Describe(char* text,size_t capacity,Size source);
// The game window's own client area in physical pixels; none until attached.
Size WindowClient();
}
