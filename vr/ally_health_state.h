#pragma once
#include "../steamvr/ally_health.h"
#include "../steamvr/party_health.h"
namespace allyhealthstate {
// Value-only UI-thread acquisition; never dereference native units from XR.
void Capture(uintptr_t verifiedClientPlayer,uint64_t session,uint32_t localId);
void Clear();
void PublishMerc(const framevr::partyhealth::Report&);
framevr::allyhealth::List Current(uint64_t session,uint64_t now);
}
