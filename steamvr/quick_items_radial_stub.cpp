// Stands in for the radial's glue (vr/quick_radial.cpp) in quick-items-test, which compiles vr/quick_items.cpp without the DLL.
#include "../vr/quick_radial.h"
namespace framevr::quickradial::rt {
void QuickUse(const void*, uint32_t&) {}
}
