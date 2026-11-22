#pragma once
#include <windows.h>
namespace pcvrdesktop {
// Process-local launch selection. Never changes saved VR settings.
inline bool Enabled(){
 static const bool value=[](){wchar_t text[8]{};return GetEnvironmentVariableW(L"D2R_PCVR_DESKTOP",text,8)==1&&text[0]==L'1';}();
 return value;
}
}
