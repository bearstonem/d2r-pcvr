#pragma once
#include <array>
#include <string_view>

namespace framevr::dev {
inline constexpr char Mod[]="pcvr-dev";
// Exact metadata is also checked by the runtime before enabling mutations.
inline constexpr char ModInfo[]="{\"name\":\"pcvr-dev\",\"savepath\":\"pcvr-dev/\"}\n";
struct Choice {const char* name;const char* detail;const char* command;};
inline constexpr std::array<Choice,8> Choices{{
    {"Prepare Paladin", "Level 90 / Zeal, Smite, Fanaticism", nullptr},
    {"Combat gear", "Unique sword, shield and armor kit", nullptr},
    {"Caster gear", "Unique scepter, shield and armor kit", nullptr},
    {"Arcane Sanctuary", "Area entrance; find the Summoner", "goto 74"},
    {"Rogue Encampment", "Act I town", "goto 1"},
    {"Lut Gholein", "Act II town", "goto 40"},
    {"River of Flame", "Act IV", "goto 107"},
    {"Chaos Sanctuary", "Act IV", "goto 108"},
}};
// Set values rather than repeatedly adding points. No class changes, inventory
// deletion, quest completion or save editing. Dispatch is not proof of success.
inline constexpr const char* Preparation[]={
    "cheat level 90", "cheat stat strength,200", "cheat stat dexterity,150",
    "cheat stat vitality,300", "cheat stat energy,100",
    "cheat sklvl Sacrifice,20", "cheat sklvl Smite,20", "cheat sklvl Might,1",
    "cheat sklvl Holy Bolt,1", "cheat sklvl Prayer,1", "cheat sklvl Defiance,1",
    "cheat sklvl Cleansing,1", "cheat sklvl Charge,1", "cheat sklvl Blessed Aim,1",
    "cheat sklvl Concentration,1", "cheat sklvl Blessed Hammer,1",
    "cheat sklvl Zeal,20", "cheat sklvl Holy Shield,20", "cheat sklvl Fanaticism,20", "heal"
};
// Native unique generation uses the base code and live game's eligible rows.
// Phase Blade may roll Lightsabre or Azurewrath; jewelry also has native rolls.
inline constexpr std::array<const char*,10> CombatKit={"7cr","pa9","uap","xpl","uvg","xhb","umc","rin","rin","amu"};
inline constexpr std::array<const char*,10> CasterKit={"7ws","pa9","uap","xea","xmg","xlb","ulc","rin","rin","amu"};
inline bool ValidChoice(int choice){return choice>=0&&choice<int(Choices.size());}
inline bool IsKit(int choice){return choice==1||choice==2;}
inline bool Isolated(std::string_view mod,std::string_view metadata){return mod==Mod&&metadata==ModInfo;}
}
