#pragma once
#include <array>
#include <string_view>
namespace framevr::skillcategories {
constexpr int Count=5,All=0,General=4;
// Native class order; page membership audited against the installed game's
// Skills -> SkillDesc.SkillPage and CharStats skill tabs (3.3.93787).
// Store only ID/category facts, never game tables, descriptions or artwork.
struct Class {int first;const char* pages;std::array<const char*,3> names;};
inline constexpr std::array<Class,8> Classes{{
    {6,"112231123312233112331122312233",{{"Bow & Crossbow","Passive & Magic","Javelin & Spear"}}},
    {36,"112331223311223112231223311233",{{"Fire","Lightning","Cold"}}},
    {66,"122331122311233112231123312233",{{"Curses","Poison & Bone","Summoning"}}},
    {96,"112331223311233112231122312233",{{"Combat","Offensive Auras","Defensive Auras"}}},
    {126,"122233112223311231123123311233",{{"Combat","Masteries","Warcries"}}},
    {221,"112231123312233112231223311233",{{"Summoning","Shape Shifting","Elemental"}}},
    {251,"122331123311223122331123311223",{{"Traps","Shadow Disciplines","Martial Arts"}}},
    {373,"111111111122222222223333333333",{{"Demon","Eldritch","Chaos"}}},
}};
constexpr bool ValidClass(int cls){return cls>=0&&cls<int(Classes.size());}
constexpr const char* Name(int cls,int category){
    if(category==All)return "All";
    if(category==General)return "General / Items";
    return ValidClass(cls)&&category>=1&&category<=3?Classes[cls].names[category-1]:"Class skills";
}
constexpr int Category(int cls,int id,int owner=-1){
    if(owner!=-1||!ValidClass(cls))return General;
    if(id>=3901&&id<=3907)return cls==2?3:cls==5?1:General;
    const auto& c=Classes[cls];
    return id>=c.first&&id<c.first+int(std::string_view(c.pages).size())?c.pages[id-c.first]-'0':General;
}
}
