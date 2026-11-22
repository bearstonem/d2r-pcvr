#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace framevr::areabanner {
// Both children are unconditionally dereferenced, even when the timer expires.
inline bool CanUpdate(bool readable,uintptr_t name,uintptr_t terror) {
    return readable&&name&&terror;
}
// Native automap query result: exact terror level, active flag, herald tier.
// The level is undefined for an inactive area; never infer it from hero level.
struct TerrorInfo {int32_t level=0;uint8_t active=0;uint8_t padding[3]{};int32_t tier=0;};
static_assert(sizeof(TerrorInfo)==12&&offsetof(TerrorInfo,active)==4&&offsetof(TerrorInfo,tier)==8);
inline bool WithLevel(std::string_view native,const TerrorInfo& info,char* out,size_t size) {
    if(!info.active||info.level<=0||info.level>255||native.empty()||native.size()>512||
       native.find('\0')!=std::string_view::npos||!out||size<native.size()+16)return false;
    std::memcpy(out,native.data(),native.size());
    std::snprintf(out+native.size(),size-native.size()," (%d)",info.level);
    return true;
}
}
