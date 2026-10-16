#pragma once
#include <array>
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
inline constexpr std::array<Choice,12> Choices{{
    {"Prepare current class", "Level 90 / all 30 class skills at level 20", nullptr},
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
}};
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
    {"Barbarian","Prepare Barbarian","Sword gear","Throwing gear","Two unique swords / combat armor","Two throwing weapons / combat armor",
     {"7gd","7gd"}, {"7ts","7b8"}, "baa", {{
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
    if(index==0)return {p.prepare,Choices[0].detail,nullptr};
    if(index==1)return {p.primary,p.primaryDetail,nullptr};
    if(index==2)return {p.alternate,p.alternateDetail,nullptr};
    return Choices[index];
}
// Attributes cover the supplied equipment. No class conversion, inventory
// deletion, quest completion or save editing. Dispatch is not proof of success.
inline constexpr const char* Preparation[]={
    "cheat level 90", "cheat stat strength,300", "cheat stat dexterity,300",
    "cheat stat vitality,400", "cheat stat energy,300"
};
// Native unique generation selects eligible unique rows and random stat rolls.
// nullptr skips an off-hand item for a two-handed staff/maul.
inline std::array<const char*,10> Kit(int cls,int choice){
    // Small, class-independent kit for comparing native trigger assignments.
    // Leave equipped items and bindings alone; the player equips both pieces.
    if(ValidClass(cls)&&choice==CrossbowChoice)return {"lxb","cqv"};
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
inline bool IsKit(int choice){return choice==1||choice==2||choice==CrossbowChoice;}
inline bool Isolated(std::string_view mod,std::string_view metadata){return mod==Mod&&metadata==ModInfo;}
}
