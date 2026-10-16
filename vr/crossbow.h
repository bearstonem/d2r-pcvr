#pragma once
#include "../steamvr/crossbow_pose.h"
#include <array>
#include <cstdint>

namespace crossbowvr {
// Timer publishes whether native first-person motion input currently owns aim.
void Gate(bool active,uint64_t session,uint32_t equipment,uint64_t epoch);
bool MakeShot(const framevr::crossbow::Pose& pose,uint64_t epoch,framevr::crossbow::Launch& shot);
// Copies native creation parameters; never changes the caller's request.
bool Prepare(uintptr_t params,std::array<uint8_t,0x98>& adjusted,framevr::crossbow::Launch& shot,bool server=false);
void BeginClient(const framevr::crossbow::Launch* shot);
void EndClient();
void Report();
void ServerDone(const framevr::crossbow::Launch& shot,bool accepted);
const framevr::bowflight::Shot* Creating();
}
