#pragma once
#include <string>
#include <string_view>
namespace framevr::reshadeaddons {
// ReShade.ini [ADDON] DisabledAddons: the names of the add-ons ReShade does not
// load, separated by commas. ReShade reads it once, when it loads.
inline constexpr std::wstring_view GenericDepth=L"Generic Depth";
namespace detail {
inline std::wstring_view Trim(std::wstring_view s) {
    while(!s.empty()&&(s.front()==L' '||s.front()==L'\t'))s.remove_prefix(1);
    while(!s.empty()&&(s.back()==L' '||s.back()==L'\t'))s.remove_suffix(1);
    return s;
}
inline bool Same(std::wstring_view a,std::wstring_view b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i){
        const auto lower=[](wchar_t c){return c>=L'A'&&c<=L'Z'?wchar_t(c-L'A'+L'a'):c;};
        if(lower(a[i])!=lower(b[i]))return false;
    }
    return true;
}
template<class F> void Each(std::wstring_view list,F&& f) {
    while(!list.empty()) {
        const size_t comma=list.find(L',');
        const auto name=Trim(list.substr(0,comma));
        if(!name.empty())f(name);
        if(comma==std::wstring_view::npos)break;
        list.remove_prefix(comma+1);
    }
}
}
inline bool Disabled(std::wstring_view list,std::wstring_view name) {
    bool found=false;detail::Each(list,[&](std::wstring_view n){found=found||detail::Same(n,name);});
    return found;
}
// The list with `name` enabled (left out) or disabled (listed once); every
// other add-on keeps its place.
inline std::wstring WithEnabled(std::wstring_view list,std::wstring_view name,bool enabled) {
    std::wstring out;
    detail::Each(list,[&](std::wstring_view n){if(!detail::Same(n,name)){if(!out.empty())out+=L',';out+=n;}});
    if(!enabled){if(!out.empty())out+=L',';out+=name;}
    return out;
}
}
