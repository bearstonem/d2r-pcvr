#pragma once
#include <string_view>
namespace assassincharges {
inline bool Asset(std::string_view path) {
    // Only charge-up orbits: never finishers, attack overlays, missiles, Venom
    // or Blade Shield. Paths audited against the native overlay definitions.
    constexpr std::string_view stems[]={
        "common/progressive_damage_", "common/progressive_steal_",
        "common/progressive_fire_", "common/progressive_lightning_",
        "common/progressive_cold_", "common/progressive_other_"};
    char normalized[160]{};if(path.size()>=sizeof normalized)return false;
    for(size_t i=0;i<path.size();++i) {
        char c=path[i];if(c>='A'&&c<='Z')c=char(c+'a'-'A');if(c=='\\')c='/';normalized[i]=c;
    }
    const std::string_view p(normalized,path.size());
    constexpr std::string_view prefix="data/hd/vfx/particles/overlays/";
    if(!p.starts_with(prefix))return false;
    const auto rest=p.substr(prefix.size());
    for(auto stem:stems)for(char level='1';level<='3';++level) {
        const auto slash=rest.find('/'); // common/<stem><level>/vfx_<stem><level>.particles
        if(!rest.starts_with(stem)||rest.size()<=stem.size()+2||rest[stem.size()]!=level||rest[stem.size()+1]!='/')continue;
        const auto leaf=rest.substr(stem.size()+2);
        const auto name=stem.substr(slash+1);
        if(leaf.starts_with("vfx_")&&leaf.substr(4).starts_with(name)&&
           leaf.size()==4+name.size()+1+10&&leaf[4+name.size()]==level&&leaf.ends_with(".particles"))return true;
    }
    // Tiger/Cobra use additional skill-specific orbit definitions.
    for(char level='1';level<='3';++level) {
        constexpr std::string_view tiger="common/tigerstrike_",cobra="assassin/cobrastrike_";
        const bool t=rest.starts_with(tiger),c=rest.starts_with(cobra);if(!t&&!c)continue;
        const auto stem=t?tiger:cobra;
        if(rest.size()<=stem.size()+2||rest[stem.size()]!=level||rest[stem.size()+1]!='/')continue;
        const auto leaf=rest.substr(stem.size()+2);
        const auto name=t?std::string_view("vfx_assassin_tiger_strike_"):std::string_view("vfx_assassin_cobrastrike_");
        if(leaf.size()==name.size()+1+10&&leaf.starts_with(name)&&leaf[name.size()]==level&&leaf.ends_with(".particles"))return true;
    }
    return false;
}
}
