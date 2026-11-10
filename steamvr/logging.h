#pragma once
#include <windows.h>
#include <cstdint>
namespace framevr::logging {
// Starts this session's d2r-vr-frame.log in the logs folder beside the plugin's: the session before is kept as
// d2r-vr-frame.previous.log, and the file stops at sessionCap bytes (log_rotation.h; 0 = the default, 64 MB).
bool Start(HMODULE plugin,uint64_t sessionCap=0);
void Write(const char* text);
void Stop();
}
