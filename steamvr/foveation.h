#pragma once
#include <cstdint>
struct ID3D12Device;
struct ID3D12CommandQueue;
namespace reshade::api {struct command_list;}
namespace framevr::foveation {
void Register(void (*log)(const char*));
void Initialize(ID3D12Device*,ID3D12CommandQueue*,uint32_t width,uint32_t height);
// peripheralRate is 2 (2x2) or 4 (4x4 where the device supports additional rates).
void Configure(bool enabled,float innerRadius,float gazeRadius=.35f,int peripheralRate=2);
void BeginEye(int eye,float centerX,float centerY,bool gazeDriven=false);
void EndWorld();
void Suspend(reshade::api::command_list*);
void Shutdown();
bool Available();
// Draws into a full-size colour + depth target pair during eye rendering,
// by the first reason they stayed at full quality.
enum Coverage {Covered,TargetMismatch,UnknownPipeline,NoPixelShader,NoDepthTest,Blended,Multisample,ViewportOther,CoverageCount};
struct Stats {uint32_t shadedDraws=0,fullDraws=0;uint32_t world[CoverageCount]{};};
Stats TakeStats();
// False while foveation is off: its draw callbacks return early and count nothing.
bool Counting();
}
