#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include "companions.h"

namespace framevr::effects {
// Native state IDs, audited against the supported game's states/skills tables.
// These describe effects on the player, never learned skills or button bindings.
enum class Kind { Benefit, Harm, Other };
constexpr int HolyShieldState=101;   // states.txt holyshield (the Paladin skill 117)
struct Definition {int state,skill;const char* name;Kind kind=Kind::Benefit;};
inline constexpr Definition Definitions[]={
    {1,-1,"Frozen",Kind::Harm},{2,-1,"Poisoned",Kind::Harm},
    {11,-1,"Chilled",Kind::Harm},{21,-1,"Stunned",Kind::Harm},{24,-1,"Slowed",Kind::Harm},
    {9,66,"Amplify Damage",Kind::Harm},{19,72,"Weaken",Kind::Harm},
    {23,71,"Dim Vision",Kind::Harm},{27,137,"Taunted",Kind::Harm},
    {29,123,"Convicted",Kind::Harm},{44,114,"Holy Freeze",Kind::Harm},
    {52,-1,"Healing blocked",Kind::Harm},{55,76,"Iron Maiden",Kind::Harm},
    {56,77,"Terror",Kind::Harm},{57,86,"Attract",Kind::Harm},
    {58,82,"Life Tap",Kind::Harm},{59,81,"Confuse",Kind::Harm},
    {60,87,"Decrepify",Kind::Harm},{61,91,"Lower Resist",Kind::Harm},
    {62,-1,"Open Wounds",Kind::Harm},{84,-1,"Mana drain",Kind::Harm},
    {85,-1,"Mana regen blocked",Kind::Harm},{87,17,"Slow Missiles",Kind::Harm},
    {89,146,"Battle Cry",Kind::Harm},{112,238,"Rabies",Kind::Harm},
    {113,-1,"Defense curse",Kind::Harm},{114,-1,"Blood Mana",Kind::Harm},
    {115,-1,"Burning",Kind::Harm},{156,264,"Cloaked",Kind::Harm},
    {17,8,"Inner Sight",Kind::Harm},
    {33,98,"Might"},{34,99,"Prayer"},{3,100,"Resist Fire"},
    {35,102,"Holy Fire"},{36,103,"Thorns"},{37,104,"Defiance"},
    {4,105,"Resist Cold"},{40,108,"Blessed Aim"},{45,109,"Cleansing"},
    {5,110,"Resist Lightning"},{42,113,"Concentration"},{43,114,"Holy Freeze"},
    {41,115,"Vigor"},{101,117,"Holy Shield"},{46,118,"Holy Shock"},
    {47,119,"Sanctuary"},{48,120,"Meditation"},{49,122,"Fanaticism"},
    {28,123,"Conviction"},{50,124,"Redemption"},{8,125,"Salvation"},
    {10,40,"Frozen Armor"},{13,46,"Blaze"},{88,50,"Shiver Armor"},
    {16,52,"Enchant"},{38,57,"Thunder Storm"},{30,58,"Energy Shield"},
    {20,60,"Chilling Armor"},{14,68,"Bone Armor"},
    {26,138,"Shout"},{15,144,"Concentrate"},{94,147,"Frenzy"},
    {32,149,"Battle Orders"},{95,152,"Berserk",Kind::Other},{51,155,"Battle Command"},
    {139,223,"Werewolf",Kind::Other},{140,228,"Werebear",Kind::Other},
    {120,232,"Feral Rage"},{117,233,"Maul"},{151,235,"Cyclone Armor"},
    {145,249,"Armageddon"},{144,250,"Hurricane"},
    {149,226,"Oak Sage"},{148,236,"Heart of Wolverine"},{147,246,"Spirit of Barbs"},
    {122,254,"Tiger Strike"},{157,258,"Burst of Speed"},{125,259,"Fists of Fire"},
    {153,264,"Cloak of Shadows"},{123,265,"Cobra Strike"},{159,267,"Fade"},
    {127,269,"Claws of Thunder"},{126,274,"Blades of Ice"},
    {158,277,"Blade Shield"},{31,278,"Venom"},{124,280,"Phoenix Strike"},
    {100,-1,"Healing potion"},{106,-1,"Mana potion"},{178,-1,"Antidote"},
    {179,-1,"Thawing"},{180,-1,"Stamina potion"},
    {128,-1,"Armor shrine"},{129,-1,"Combat shrine"},
    {130,-1,"Lightning shrine"},{131,-1,"Fire shrine"},{132,-1,"Cold shrine"},
    {133,-1,"Poison shrine"},{134,-1,"Skill shrine"},{135,-1,"Mana regen shrine"},
    {136,-1,"Stamina shrine"},{137,-1,"Experience shrine"},
    {177,-1,"Delirium",Kind::Other},{190,-1,"Mark of the Bear"},{191,-1,"Mark of the Wolf"},
    {198,385,"Hex Bane",Kind::Other},{200,386,"Hex Siphon",Kind::Other},
    {202,389,"Hex Purge",Kind::Other},{206,387,"Psychic Ward"},
    {208,381,"Consume"},{211,379,"Engorge"},
    {199,385,"Hex Bane",Kind::Harm},{201,386,"Hex Siphon",Kind::Harm},
    {217,389,"Hex Purge",Kind::Harm},{192,393,"Lethargy",Kind::Harm},
    {193,396,"Rancor",Kind::Harm},{194,400,"Death sigil",Kind::Harm}
};
constexpr unsigned MaxStates=512;
struct Status {
    bool known=false;
    std::array<uint32_t,MaxStates/32> bits{};
    bool operator==(const Status&) const=default;
    bool Has(int state)const{return known&&state>0&&state<int(MaxStates)&&(bits[state/32]&(1u<<(state%32)));}
    void Set(int state){if(state>0&&state<int(MaxStates))bits[state/32]|=1u<<(state%32);}
};
// The caller verifies a live local player, table bank and native accessor.
// Out-of-bank states are absent; failed reads discard the whole sample.
template<class Check>Status Capture(unsigned stateCount,Check check) {
    Status result;if(stateCount<180||stateCount>MaxStates)return result;
    for(const auto& d:Definitions)if(unsigned(d.state)<stateCount) {
        const int on=check(d.state);if(on!=0&&on!=1)return {};
        if(on)result.Set(d.state);
    }
    result.known=true;return result;
}
inline int Count(const Status& s){int n=0;for(const auto& d:Definitions)if(s.Has(d.state))++n;return n;}
// The Paladin auras' skills (also from runewords), as the
// table's own player states above; Holy Shield (117) is not an aura, and the
// monsters' Convicted / Holy Freeze rows are harms, not the player's aura.
inline bool AuraSkill(int skill){
    constexpr int auras[]={98,99,100,102,103,104,105,108,109,110,113,114,115,118,119,120,122,123,124,125};
    return std::find(std::begin(auras),std::end(auras),skill)!=std::end(auras);
}
inline bool AnyAura(const Status& s){
    for(const auto& d:Definitions)if(d.kind==Kind::Benefit&&AuraSkill(d.skill)&&s.Has(d.state))return true;
    return false;
}
inline auto Ordered(const Status& s) {
    std::array<const Definition*,std::size(Definitions)> result{};unsigned n=0;
    for(bool harmful:{true,false})for(const auto& d:Definitions)
        if(s.Has(d.state)&&(d.kind==Kind::Harm)==harmful)result[n++]=&d;
    return result;
}
// Keep unusually crowded status displays bounded. Harmful effects come first;
// the last cell reports additional active effects rather than silently dropping them.
constexpr int MaxCells=21;
inline int Cells(const Status& s){return std::min(Count(s),MaxCells);}
inline int Rows(const Status& s){return (Cells(s)+companions::Columns-1)/companions::Columns;}
}

namespace framevr::wriststatus {
constexpr int Width=companions::Width,Height=companions::Height+3*companions::RowHeight;
inline int Rows(const companions::Roster& r,const effects::Status& e){return companions::Rows(r)+effects::Rows(e);}
inline int UsedHeight(const companions::Roster& r,const effects::Status& e){return Rows(r,e)*companions::RowHeight;}
}
