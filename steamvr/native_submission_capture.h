#pragma once
#include "stereo_submission_plan.h"
#include <cstdint>
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D12CommandAllocator;

namespace framevr::nativecapture {
struct Report {
    uint32_t batches=0,lists=0,signals=0,waits=0,cpuSignals=0;
    uint32_t unknownLists=0,queueOverlap=0,boundaryCalls=0,foreignCalls=0,apiFailures=0;
    uint32_t hookErrors=0,observerFaults=0,capacityFailures=0,newFences=0;
    uint32_t listResets=0,allocatorResets=0,retainedAllocators=0,retainedFences=0;
    uint32_t internalWaits=0,externalWaits=0;
    stereoreplay::PlanStatus atPresent=stereoreplay::PlanStatus::Empty,atEnd=stereoreplay::PlanStatus::Empty;
    bool sealed=false;
    // Only dependency/lifetime metadata completeness, never replay safety.
    // Actual native resource states, camera coverage and GPU lifetime leases
    // still need validation before any game recording can be replayed.
    bool DependenciesComplete()const;
};
// Default inert: no hooks until Start. No submission parameters are changed,
// no signals/waits are added or delayed, and no command list is replayed.
bool Start()noexcept;
void ObserveDevice(ID3D12Device*)noexcept;
void ObserveQueue(ID3D12CommandQueue*)noexcept;
void ObserveList(ID3D12GraphicsCommandList*,ID3D12CommandAllocator* initialAllocator=nullptr)noexcept;
void ForgetList(uint64_t nativeList)noexcept;
void ForgetQueue(uint64_t nativeQueue)noexcept;
void BeginEye(uint32_t owningThread)noexcept;
void SealPresent()noexcept;
Report EndEye()noexcept;
void Stop()noexcept; // Stops collection; installed detours become pass-through.
bool Shutdown()noexcept; // After ReShade unregisters callbacks; removes only owned hooks.
uint32_t InstalledHooks()noexcept;
const char* StatusName(stereoreplay::PlanStatus)noexcept;
}
