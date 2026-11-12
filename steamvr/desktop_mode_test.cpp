#include "desktop_mode.h"
#include <cassert>
#include <cstring>
int main(int argc,char**argv){
 const bool desktop=argc>1&&!std::strcmp(argv[1],"desktop");
 SetEnvironmentVariableW(L"D2R_PCVR_DESKTOP",desktop?L"1":L"0");
 assert(pcvrdesktop::Enabled()==desktop);
 SetEnvironmentVariableW(L"D2R_PCVR_DESKTOP",desktop?L"0":L"1");
 assert(pcvrdesktop::Enabled()==desktop); // launch-only, no live mode switching
}
