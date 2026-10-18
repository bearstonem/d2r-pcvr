#pragma once
#include <array>
#include <algorithm>
#include <string_view>

namespace framevr::dev {
inline constexpr char Mod[]="pcvr-dev";
// Exact metadata is also checked by the runtime before enabling mutations.
inline constexpr char ModInfo[]="{\"name\":\"pcvr-dev\",\"savepath\":\"pcvr-dev/\"}\n";
struct Choice {const char* name;const char* detail;const char* command;};
inline constexpr int TyraelChoice=8;
inline constexpr int DurielChoice=9;
inline constexpr int CrossbowChoice=10;
inline constexpr int BloodMoorChoice=11;
inline constexpr int BowChoice=12;
inline constexpr int SmallShieldChoice=13;
inline constexpr int Level10Choice=28,Level30Choice=29,Level60Choice=30,HealChoice=31;
inline constexpr int StarterPrimaryChoice=32,StarterAlternateChoice=33;
inline constexpr int DebugDrawsChoice=34;
inline constexpr int PageSize=14;
inline constexpr std::array<Choice,35> Choices{{
    {"Level 90", "All class skills rank 20 / test attributes", nullptr},
    {"Primary gear", "Class-specific unique equipment", nullptr},
    {"Alternate gear", "Equipment for the other class abilities", nullptr},
    {"Arcane Sanctuary", "Area entrance; find the Summoner", "goto 74"},
    {"Rogue Encampment", "Act I town", "goto 1"},
    {"Lut Gholein", "Act II town", "goto 40"},
    {"River of Flame", "Act IV", "goto 107"},
    {"Chaos Sanctuary", "Act IV", "goto 108"},
    // D2R 3.3.93787: level 73 starts at tile (4500,3100); duriel.ds1
    // places Tyrael at subtile (78,88). Arrive six subtiles beside him,
    // on his platform, rather than at the boss-room entrance.
    {"Tyrael - Act II", "Tyrael's platform / wings", "goto 73,22578,15594"},
    // Use the level's native arrival point for the boss arena, rather than
    // the explicit Tyrael platform coordinates above. No quest commands.
    {"Duriel - Act II", "Boss-room entrance / rendering", "goto 73"},
    {"Crossbow + bolts", "Equip both; assign Attack to Right Trigger", nullptr},
    {"Blood Moor", "Act I combat / crossbow test", "goto 2"},
    {"Bow + arrows", "Windforce / 250 arrows", nullptr},
    {"Small shield", "Pelta Lunata buckler / compare shield reach", nullptr},
    // Scenic filming stops. Native level entrances, not boss/quest commands.
    // IDs checked against the installed 3.3.93787 levels table.
    {"Tristram", "Act I / burning village and close combat", "goto 38"},
    {"Cathedral", "Act I / gothic interior and lighting", "goto 33"},
    {"Andariel's chamber", "Act I / Catacombs Level 4", "goto 37"},
    {"Kurast Bazaar", "Act III / ruins, stairs and ranged combat", "goto 80"},
    {"Travincal", "Act III / temples and Council arena", "goto 83"},
    {"Mephisto's chamber", "Act III / Durance of Hate Level 3", "goto 102"},
    {"Harrogath", "Act V town / gear and shield close-ups", "goto 109"},
    {"Bloody Foothills", "Act V / open-field melee and archery", "goto 110"},
    {"Crystalline Passage", "Act V / ice cavern and indoor lighting", "goto 113"},
    {"Arreat Summit", "Act V / mountain panorama", "goto 120"},
    {"Throne of Destruction", "Act V / Baal's throne room", "goto 131"},
    {"Worldstone Chamber", "Act V / Worldstone backdrop", "goto 132"},
    {"Spider Forest", "Act III / jungle pool stability test", "goto 76"},
    {"Flayer Jungle", "Act III / explore new jungle rooms", "goto 78"},
    {"Level 10", "Skills through level 6 / test attributes", nullptr},
    {"Level 30", "All skill tiers / level-capped ranks", nullptr},
    {"Level 60", "All class skills rank 20 / test attributes", nullptr},
    {"Heal", "Restore life and mana", "heal"},
    {"Starter primary gear", "Normal base gear / suitable for level 10", nullptr},
    {"Starter alternate gear", "Alternate weapon type / suitable for level 10", nullptr},
    {"Render debug draws", "Toggle shield block outline and hit feedback", nullptr},
}};
// Stable action IDs are independent of their presentation order.
inline constexpr int PageCount=4;
inline constexpr const char* PageNames[]={"CHARACTER","EQUIPMENT","ACT I - III","ACT IV - V"};
inline constexpr const char* PageHelp[]={
    "Test presets replace stats and skills; gear and button bindings stay as they are.",
    "Starter kits work at low levels; unique kits are for level 90. Equip in Inventory.",
    "Area entrances and inspection spots. Bosses depend on this test character's save.",
    "Area entrances. Travel does not complete quests, reset bosses or change difficulty."
};
inline constexpr std::array<std::array<int,PageSize>,PageCount> Pages{{
    {{Level10Choice,Level30Choice,Level60Choice,0,HealChoice,-1,-1,-1,-1,-1,-1,-1,-1,-1}},
    {{StarterPrimaryChoice,StarterAlternateChoice,1,2,CrossbowChoice,BowChoice,SmallShieldChoice,DebugDrawsChoice,-1,-1,-1,-1,-1,-1}},
    {{4,11,14,15,16,5,3,8,9,17,18,19,26,27}},
    {{6,7,20,21,22,23,24,25,-1,-1,-1,-1,-1,-1}}
}};
inline int PageFor(int choice){for(int p=0;p<PageCount;++p)for(int id:Pages[p])if(id==choice&&id>=0)return p;return -1;}
inline int SlotFor(int choice){for(const auto& page:Pages)for(int i=0;i<PageSize;++i)if(page[i]==choice&&choice>=0)return i;return -1;}
inline int LevelFor(int choice){return choice==0?90:choice==Level10Choice?10:choice==Level30Choice?30:choice==Level60Choice?60:0;}
// Required levels checked against the owner's supported local Skills table.
// Barbarian has uneven tiers; Warlock is ordered by tree rather than tier.
inline int SkillRank(int cls,int skill,int level){
    constexpr int tiers[]={1,6,12,18,24,30};
    constexpr int barbarian[]={1,1,1,1,1,1,6,6,6,6,6,6,6,12,12,12,12,18,18,18,18,24,24,24,24,30,30,30,30,30};
    constexpr int warlock[]={1,1,6,12,18,6,24,18,30,30,1,24,1,24,18,12,12,18,6,30,6,6,1,12,24,18,12,24,30,30};
    if(cls<0||cls>=8||skill<0||skill>=30)return 0;
    const int required=cls==4?barbarian[skill]:cls==7?warlock[skill]:tiers[skill/5];
    return std::clamp(level-required+1,0,20);
}
// Native skill identifiers, including internal names that differ from the UI.
// Set exact levels so repeated preparation cannot keep adding skill points.
// Class order is the native unit class ID. A live Warlock already identifies
// a RotW character; item calibration may still select the shared LoD tables.
struct Preset {
    const char *name,*prepare,*primary,*alternate,*primaryDetail,*alternateDetail;
    std::array<const char*,2> primaryWeapons,alternateWeapons;
    const char* helmet;
    std::array<const char*,30> skills;
};
inline constexpr std::array<Preset,8> Presets{{
    {"Amazon","Prepare Amazon","Bow gear","Javelin gear","Unique bow / arrows / armor","Unique javelins / shield / armor",
     {"6lw","aqv"}, {"ama","uit"}, "uap", {{
        "Magic Arrow","Fire Arrow","Inner Sight","Critical Strike","Jab",
        "Cold Arrow","Multiple Shot","Dodge","Power Strike","Poison Javelin",
        "Exploding Arrow","Slow Missiles","Avoid","Impale","Lightning Bolt",
        "Ice Arrow","Guided Arrow","Penetrate","Charged Strike","Plague Javelin",
        "Strafe","Immolation Arrow","Dopplezon","Evade","Fend",
        "Freezing Arrow","Valkyrie","Pierce","Lightning Strike","Lightning Fury"
    }}},
    {"Sorceress","Prepare Sorceress","Orb gear","Staff gear","Unique orb / shield / caster armor","Unique staff / caster armor",
     {"oba","uit"}, {"6ws",nullptr}, "uap", {{
        "Fire Bolt","Warmth","Charged Bolt","Ice Bolt","Frozen Armor",
        "Inferno","Static Field","Telekinesis","Frost Nova","Ice Blast",
        "Blaze","Fire Ball","Nova","Lightning","Shiver Armor",
        "Fire Wall","Enchant","Chain Lightning","Teleport","Glacial Spike",
        "Meteor","Thunder Storm","Energy Shield","Blizzard","Chilling Armor",
        "Fire Mastery","Hydra","Lightning Mastery","Frozen Orb","Cold Mastery"
    }}},
    {"Necromancer","Prepare Necromancer","Wand gear","Dagger gear","Unique wand / head / caster armor","Poison Dagger weapon / head / armor",
     {"7bw","nef"}, {"7dg","nef"}, "uap", {{
        "Amplify Damage","Teeth","Bone Armor","Skeleton Mastery","Raise Skeleton",
        "Dim Vision","Weaken","Poison Dagger","Corpse Explosion","Clay Golem",
        "Iron Maiden","Terror","Bone Wall","Golem Mastery","Raise Skeletal Mage",
        "Confuse","Life Tap","Poison Explosion","Bone Spear","BloodGolem",
        "Attract","Decrepify","Bone Prison","Summon Resist","IronGolem",
        "Lower Resist","Poison Nova","Bone Spirit","FireGolem","Revive"
    }}},
    {"Paladin","Prepare Paladin","Combat gear","Caster gear","Unique sword / shield / combat armor","Unique scepter / shield / caster armor",
     {"7cr","pa9"}, {"7ws","pa9"}, "uap", {{
        "Sacrifice","Smite","Might","Prayer","Resist Fire",
        "Holy Bolt","Holy Fire","Thorns","Defiance","Resist Cold",
        "Zeal","Charge","Blessed Aim","Cleansing","Resist Lightning",
        "Vengeance","Blessed Hammer","Concentration","Holy Freeze","Vigor",
        "Conversion","Holy Shield","Holy Shock","Sanctuary","Meditation",
        "Fist of the Heavens","Fanaticism","Conviction","Redemption","Salvation"
    }}},
    {"Barbarian","Prepare Barbarian","Dual-wield gear","Throwing gear","Unique sword + axe / combat armor","Two throwing weapons / combat armor",
     {"7cr","7wa"}, {"7ts","7b8"}, "baa", {{
        "Bash","Blade Mastery","Axe Mastery","Mace Mastery","Howl",
        "Find Potion","Leap","Double Swing","Pole Arm Mastery","Throwing Mastery",
        "Spear Mastery","Taunt","Shout","Stun","Double Throw",
        "Increased Stamina","Find Item","Leap Attack","Concentrate","Iron Skin",
        "Battle Cry","Frenzy","Increased Speed","Battle Orders","Grim Ward",
        "Whirlwind","Berserk","Natural Resistance","War Cry","Battle Command"
    }}},
    {"Druid","Prepare Druid","Shapeshift gear","Caster gear","Unique two-hand maul / combat armor","Unique dagger / shield / caster armor",
     {"7gm",nullptr}, {"7dg","uit"}, "dra", {{
        "Raven","Plague Poppy","Wearwolf","Shape Shifting","Firestorm",
        "Oak Sage","Summon Spirit Wolf","Wearbear","Molten Boulder","Arctic Blast",
        "Cycle of Life","Feral Rage","Maul","Eruption","Cyclone Armor",
        "Heart of Wolverine","Summon Fenris","Rabies","Fire Claws","Twister",
        "Vines","Hunger","Shock Wave","Volcano","Tornado",
        "Spirit of Barbs","Summon Grizzly","Fury","Armageddon","Hurricane"
    }}},
    {"Assassin","Prepare Assassin","Claw gear","Trap gear","Two unique claws / combat armor","Unique dagger / shield / caster armor",
     {"9tw","7wb"}, {"7dg","uit"}, "uap", {{
        "Fire Trauma","Claw Mastery","Psychic Hammer","Tiger Strike","Dragon Talon",
        "Shock Field","Blade Sentinel","Quickness","Fists of Fire","Dragon Claw",
        "Charged Bolt Sentry","Wake of Fire Sentry","Weapon Block","Cloak of Shadows","Cobra Strike",
        "Blade Fury","Fade","Shadow Warrior","Claws of Thunder","Dragon Tail",
        "Lightning Sentry","Inferno Sentry","Mind Blast","Blades of Ice","Dragon Flight",
        "Death Sentry","Blade Shield","Venom","Shadow Master","Royal Strike"
    }}},
    {"Warlock","Prepare Warlock","Blade gear","Caster gear","Unique two-hand sword / grimoire","Unique dagger / grimoire / caster armor",
     {"7gd","waf"}, {"7dg","wac"}, "xsk", {{
        "Summon Goatman","Demonic Mastery","Death Mark","Summon Tainted","Summon Defiler",
        "Blood Oath","Engorge","Blood Boil","Consume","Bind Demon",
        "Levitate","Eldritch Blast","Hex Bane","Hex Siphon","Psychic Ward",
        "Echoing Strike","Hex Purge","Blade Warp","Cleave","Mirrored Blades",
        "Sigil Lethargy","Ring of Fire","Miasma Bolt","Sigil Rancor","Enhanced Entropy",
        "Flame Wave","Miasma Chains","Sigil Death","Apocalypse","Abyss"
    }}},
}};
inline bool ValidClass(int cls){return cls>=0&&cls<int(Presets.size());}
inline int ExpansionClass(unsigned cls,unsigned bank){
    return (bank==2||bank==3)&&cls<Presets.size()?int(cls):-1;
}
inline Choice ChoiceFor(int cls,int index){
    if(!ValidClass(cls)||index<0||index>=int(Choices.size()))return Choices[0];
    const auto& p=Presets[cls];
    if(index==0)return Choices[0];
    if(index==1)return {p.primary,p.primaryDetail,nullptr};
    if(index==2)return {p.alternate,p.alternateDetail,nullptr};
    return Choices[index];
}
// Attributes cover the supplied equipment. No class conversion, inventory
// deletion, quest completion or save editing. Dispatch is not proof of success.
inline constexpr int PreparationCount=5;
// Native unique generation selects eligible unique rows and random stat rolls.
// nullptr skips an off-hand item for a two-handed staff/maul.
inline std::array<const char*,10> Kit(int cls,int choice){
    if(ValidClass(cls)&&(choice==StarterPrimaryChoice||choice==StarterAlternateChoice)) {
        constexpr const char* primary[8][2]={{"sbw","aqv"},{"sst",nullptr},{"wnd","buc"},{"ssd","buc"},{"ssd","hax"},{"clb","buc"},{"ktr","ktr"},{"2hs",nullptr}};
        constexpr const char* alternate[8][2]={{"jav","buc"},{"sst",nullptr},{"dgr","buc"},{"scp","buc"},{"jav","tax"},{"clb","buc"},{"dgr","buc"},{"dgr","buc"}};
        const auto& weapons=choice==StarterPrimaryChoice?primary[cls]:alternate[cls];
        return {weapons[0],weapons[1],"cap","qui","lgl","lbt","lbl"};
    }
    // Small, class-independent kit for comparing native trigger assignments.
    // Leave equipped items and bindings alone; the player equips both pieces.
    if(ValidClass(cls)&&choice==CrossbowChoice)return {"lxb","cqv"};
    if(ValidClass(cls)&&choice==BowChoice)return {"6lw","aqv"};
    // Keep this separate: the Paladin's full kit already occupies 37 cells.
    // Adding a 2x2 buckler would exceed the 40-cell inventory even when empty.
    if(ValidClass(cls)&&choice==SmallShieldChoice)return {"buc"};
    if(!ValidClass(cls)||(choice!=1&&choice!=2))return {};
    const auto& p=Presets[cls];const bool caster=choice==2&&cls!=0&&cls!=4;
    const auto& weapons=choice==1?p.primaryWeapons:p.alternateWeapons;
    const bool primaryCaster=cls==1||cls==2;
    const bool useCaster=caster||primaryCaster;
    return {weapons[0],weapons[1],p.helmet,useCaster?"xea":"uar",
            useCaster?"tgl":"uvg",useCaster?"xvb":"xhb",useCaster?"ulc":"umc","rin","rin","amu"};
}
inline bool Ammo(std::string_view code){return code=="aqv"||code=="cqv";}
inline bool ValidChoice(int choice){return choice>=0&&choice<int(Choices.size());}
inline bool StarterKit(int choice){return choice==StarterPrimaryChoice||choice==StarterAlternateChoice;}
inline bool IsKit(int choice){return choice==1||choice==2||choice==CrossbowChoice||choice==BowChoice||choice==SmallShieldChoice||StarterKit(choice);}
inline bool Isolated(std::string_view mod,std::string_view metadata){return mod==Mod&&metadata==ModInfo;}
}
