#pragma once
#include <array>
#include <algorithm>
#include <string_view>
#include "limb_tuning.h"

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
inline constexpr int KurastSewersChoice=35,UpperKurastChoice=36;
// A session switch, like the debug draws: [stereo] turns_keep_own (off, near things, all) for A/B.
inline constexpr int TurnsKeepOwnChoice=37;
inline constexpr int ThrowingChoice=38;
inline constexpr int PotionsChoice=39;
inline constexpr int UnlockQuestsChoice=40;
// A session switch like TurnsKeepOwnChoice: [stereo] turns_sharp (sharp, soft) for A/B.
inline constexpr int TurnsSharpChoice=41;
// And [stereo] turns_fill (fixed, as before).
inline constexpr int TurnsFillChoice=42;
inline constexpr int TorchChoice=43;
// #292's gas and oil potions; on HERO, GEAR being full.
inline constexpr int ThrowingPotionsChoice=44;
// And [stereo] turns_rims (moved with things, left with the sky); on HERO too.
inline constexpr int TurnsRimsChoice=45;
// And [sky] cover_rims (on, off): the sky over that rim in the picture itself; on HERO too.
inline constexpr int SkyRimsChoice=46;
// Pushable bodies (limb_tuning.h): one value each, stepped and saved to
// [bodies] in the dev INI; the last puts them all back to their defaults.
inline constexpr int BodyKnobChoice=47,BodyResetChoice=BodyKnobChoice+limbs::Knobs;
inline int BodyKnob(int choice){return choice>=BodyKnobChoice&&choice<BodyResetChoice?choice-BodyKnobChoice:-1;}
// Loot beam test (owner, 2026-10-31): one item for each beam rule, to drop on
// the floor (LootBeamKit below).
inline constexpr int LootBeamsChoice=BodyResetChoice+1;
// Tal Rasha's Tomb (owner, 2026-11-08, to test #466's joined dungeon rooms there): its first tomb, level 66.
inline constexpr int TalRashaTombChoice=LootBeamsChoice+1;
// Item-fired bows (owner, 2026-11-08, to test #432): the three uniques whose basic attack fires another
// missile - Witherstring (Fires Magic Arrows), Raven Claw (Fires Explosive Arrows), Demon Machine (a
// crossbow, Fires Explosive Bolts) - with arrows and bolts (ItemBowUniques below).
inline constexpr int ItemBowsChoice=TalRashaTombChoice+1;
// The Frozen Tundra crash (2026-11-12; two reports from a player, the game's effect-ended listener): the player
// took the Glacial Trail waypoint (level 115, an ice cave with the game-drawn ceiling), walked out into the Frozen
// Tundra (level 117; the ceiling put away there) and fought with Bone Spear. Both levels, and a step that goes to
// one and then the other by turns, a press a step (TundraReproStep), each step logged.
inline constexpr int GlacialTrailChoice=ItemBowsChoice+1,FrozenTundraChoice=ItemBowsChoice+2,TundraReproChoice=ItemBowsChoice+3;
inline constexpr int TundraReproSteps=10;
// The 2026-11-19 player reports (#591-#595): the Worldstone Keep, where the ceiling came down to a raised hand (the
// Throne of Destruction, the Worldstone Chamber and Mephisto's chamber are here already), and a kit of flails with life
// and mana steal (FlailKit below).
inline constexpr int WorldstoneKeepChoice=TundraReproChoice+1,FlailsChoice=TundraReproChoice+2;
// The stash's Collection (owner, 2026-11-25, to test #608): four kits of named items
// (CollectionKit below), on the STASH page - uniques, set items, runeword bases with their runes, gems and runes.
inline constexpr int CollectUniquesChoice=FlailsChoice+1,CollectSetsChoice=FlailsChoice+2,
                     CollectRunewordsChoice=FlailsChoice+3,CollectGemsChoice=FlailsChoice+4;
struct ReproStep {int step;const char* command;const char* place;};
// The press'th press (0 first): steps 1 to TundraReproSteps and round again, odd ones to the ice cave.
inline ReproStep TundraReproStep(unsigned press){
    const int step=int(press%TundraReproSteps)+1;
    return step%2?ReproStep{step,"goto 115","Glacial Trail"}:ReproStep{step,"goto 117","Frozen Tundra"};
}
inline constexpr int PageSize=14;
inline constexpr std::array<Choice,CollectGemsChoice+1> Choices{{
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
    {"Crossbow + bolts", "Equip both; Right Trigger fires bolts", nullptr},
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
    {"Kurast Sewers", "Level 1 / take stairs back out to inspect entrances", "goto 92"},
    {"Upper Kurast", "Act III / second sewer-entrance region", "goto 81"},
    {"By turns own picture", "Eye not drawn: FX / near+FX / off / near / all", nullptr},
    {"Throwing kit", "Javelin, Pilum, Glaive, knives and axes", nullptr},
    {"Potion kit", "Health, Rejuvenation, Antidote, Thawing / belt them to throw", nullptr},
    {"Unlock Quests & Waypoints", "Complete quests through Act V / unlock all waypoints", nullptr},
    {"By turns filter", "Eye not drawn read sharp (Catmull-Rom) / soft (bilinear)", nullptr},
    {"By turns ghosts", "Strips beside moving things: frame-old ghosts removed / kept", nullptr},
    {"Torch kit", "One-hand club torch / test lighting and melee", nullptr},
    {"Throwing potions", "Gas and oil potions / equip a stack to throw", nullptr},
    {"By turns rims", "Dark rim round roofs and trees against the sky: moved with them / left", nullptr},
    {"Sky over rims", "Sky painted over the dark rim round roofs and trees / rim kept", nullptr},
    // Their titles show the value (interaction_ui.cpp); the details are limb_tuning.h's.
    {"Limb spring back", "How fast a bent limb swings back to the animation", nullptr},
    {"Body spring back", "How fast a shoved body returns to its place and lean", nullptr},
    {"Bounce", "Low wobbles past the pose; 1 settles without overshoot", nullptr},
    {"Push strength", "The most a weapon or fist pushes with", nullptr},
    {"Joint strength", "How hard each joint holds the pose, per kg it holds", nullptr},
    {"Body steadiness", "How hard the hips hold their place, per kg of the body", nullptr},
    {"Hit kick", "A landed hit's push on the struck limb (0 = none)", nullptr},
    {"Most bend", "How far a limb may swing off the animation", nullptr},
    {"Most shove", "How far the whole body may be pushed", nullptr},
    {"Ease back", "When the physics lets a body go, how long it eases back", nullptr},
    {"Bodies: defaults", "Every value above back to its default", nullptr},
    {"Loot beam test", "Unique, set, rare, magic, white, 5 runes / drop them", nullptr},
    // Its stairs and ledges are where dungeon rooms stood a step apart over void (room stitch).
    {"Tal Rasha's Tomb", "Act II / stairs, ledges, joined rooms", "goto 66"},
    {"Item-fired bows", "Witherstring, Raven Claw, Demon Machine / arrows, bolts", nullptr},
    {"Glacial Trail", "Act V / ice cave; walk out to the Frozen Tundra", "goto 115"},
    {"Frozen Tundra", "Act V / Tundra crash repro: fight (Bone Spear)", "goto 117"},
    // Its destination is the step's (TundraReproStep), not a fixed command.
    {"Tundra repro step", "Glacial Trail and Frozen Tundra by turns", nullptr},
    // levels.txt 128 (129 and 130 below it, then the Throne 131 and the Worldstone Chamber 132)
    {"Worldstone Keep", "Act V / Level 1: its ceiling over the pillars and stairs", "goto 128"},
    {"Flails + steal", "Tan Do Li Ga (mana steal), Bloodrise (life steal), Flail, Scourge", nullptr},
    {"Collection uniques", "SoJ twice, charms, Facet, Ormus', Windforce, a Shako unread", nullptr},
    {"Collection sets", "Angelic Raiment, all four / Cathan's Seal and Sigil", nullptr},
    {"Collection runewords", "Socketed bases and runes: Stealth, Spirit sword and shield", nullptr},
    {"Collection gems + runes", "Seven gems, El, Ral, Ber, Jah, Zod / the stash's own tabs", nullptr},
}};
static_assert(limbs::Knobs==10,"one BODIES choice per knob");
// Stable action IDs are independent of their presentation order.
inline constexpr int PageCount=7;
inline constexpr const char* PageNames[]={"HERO","GEAR","ACT I-II","ACT III","ACT IV-V","BODIES","STASH"};
inline constexpr const char* PageHelp[]={
    "Test presets replace stats and skills; gear and button bindings stay as they are.",
    "Starter kits work at low levels; unique kits are for level 90. Equip in Inventory.",
    "Area entrances and inspection spots. Bosses depend on this test character's save.",
    "Kurast Sewers: take the stairs back outside to inspect the entrance and interaction.",
    "Area entrances. Travel does not complete quests, reset bosses or change difficulty.",
    "Each press steps a value and saves it in the dev INI's [bodies]. Then push a body to feel it.",
    "For the stash's Collection. Empty the inventory first, then open the stash in Harrogath (ACT IV-V)."
};
inline constexpr std::array<std::array<int,PageSize>,PageCount> Pages{{
    {{Level10Choice,Level30Choice,Level60Choice,0,HealChoice,UnlockQuestsChoice,ThrowingPotionsChoice,TurnsRimsChoice,SkyRimsChoice,LootBeamsChoice,ItemBowsChoice,FlailsChoice,-1,-1}},
    {{StarterPrimaryChoice,StarterAlternateChoice,1,2,CrossbowChoice,BowChoice,SmallShieldChoice,DebugDrawsChoice,TurnsKeepOwnChoice,ThrowingChoice,PotionsChoice,TurnsSharpChoice,TurnsFillChoice,TorchChoice}},
    {{4,11,14,15,16,5,3,TalRashaTombChoice,8,9,-1,-1,-1,-1}},
    {{17,UpperKurastChoice,KurastSewersChoice,18,19,26,27,-1,-1,-1,-1,-1,-1,-1}},
    {{6,7,20,21,22,23,24,25,GlacialTrailChoice,FrozenTundraChoice,TundraReproChoice,WorldstoneKeepChoice,-1,-1}},
    {{BodyKnobChoice,BodyKnobChoice+1,BodyKnobChoice+2,BodyKnobChoice+3,BodyKnobChoice+4,BodyKnobChoice+5,BodyKnobChoice+6,
      BodyKnobChoice+7,BodyKnobChoice+8,BodyKnobChoice+9,BodyResetChoice,-1,-1,-1}},
    {{CollectUniquesChoice,CollectSetsChoice,CollectRunewordsChoice,CollectGemsChoice,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}}
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
// The loot beam test (loot_beams.h): a unique ring, a set amulet and a rare ring (the
// gold, green and yellow beams), a magic amulet and a white dagger (no beam), Vex
// (r26, the first high rune: orange), Gul (r25, one below: none) and Ohm, Lo and Sur
// (r27-r29: more orange). The rare ring is identified and the other magic-or-better
// items are not, as fresh drops are. 12 inventory cells; Kit() holds at most 10 items.
enum class KitQuality {Normal,Magic,Rare,Set,Unique};
struct KitItem {const char* code;KitQuality quality;int itemLevel;bool identified;};
inline constexpr std::array<KitItem,10> LootBeamKit{{
    {"rin",KitQuality::Unique,99,false},{"amu",KitQuality::Set,99,false},{"rin",KitQuality::Rare,99,true},
    {"amu",KitQuality::Magic,30,false},{"dgr",KitQuality::Normal,1,true},{"r26",KitQuality::Normal,1,true},
    {"r25",KitQuality::Normal,1,true},{"r27",KitQuality::Normal,1,true},{"r28",KitQuality::Normal,1,true},
    {"r29",KitQuality::Normal,1,true}
}};
// The item-fired bows kit's forced UniqueItems rows (zero-based, the table's *ID column; D2R 3.3's own
// uniqueitems.txt, read 2026-11-08): Witherstring (hbw) 60, Raven Claw (index "Rimeraven", lbw) 61,
// Demon Machine (8rx) 199 - the "Expansion" line between them is not a row.
inline constexpr std::array<uint32_t,3> ItemBowUniques{60,61,199};
// Flails + steal (2026-11-19, to test #593's leech line and #594's flail reach and chain): two named uniques, forced by
// their UniqueItems rows (D2R's own uniqueitems.txt, Steam 3.4.95041, read 2026-11-19), and two normal flails. The
// General's Tan Do Li Ga (fla, row 21: 5% mana steal, level 21) is a flail for one hand; Bloodrise (mst, row 20: 5% life
// steal, level 15) a mace with it; a Flail (str 41) for the other hand; a Scourge (7fl, str 125, level 57) for the elite
// model at level 60 or 90. 24 inventory cells.
inline constexpr std::array<KitItem,4> FlailKit{{
    {"fla",KitQuality::Unique,99,true},{"mst",KitQuality::Unique,99,true},{"fla",KitQuality::Normal,1,true},{"7fl",KitQuality::Normal,1,true}
}};
inline constexpr std::array<uint32_t,2> FlailKitUniques{21,20};
// The Collection kits (#608, owner 2026-11-25): named items. A unique or set item is forced by its row, zero-based as
// the SDK takes it: the UniqueItems / SetItems *ID column, which is the Collection catalog's recordId
// (steamvr/prepare_collection.py; read from Steam 3.4.95041's own tables on 2026-11-25). Any other item is made from
// its code alone (row -1). Sockets are exact (0: none). Every item is made at item level 99 and identified unless
// marked. Each kit fits an empty 40-cell inventory.
struct NamedItem {const char* code;KitQuality quality;int row;int sockets;bool identified;};
inline constexpr size_t NamedKitMost=16;
using NamedKit=std::array<NamedItem,NamedKitMost>;
// Uniques. Two Stones of Jordan: only one copy of an entry is kept. Nagelring and Mara's Kaleidoscope. The three
// charms: Annihilus, the Hellfire Torch (a card per class) and Gheed's Fortune. A Rainbow Facet (eight rows) and
// Ormus' Robes (five cards). Windforce, a two-handed bow. A Harlequin Crest left unidentified, which is never
// collected. 29 cells.
inline constexpr NamedKit CollectUniquesKit{{
    {"rin",KitQuality::Unique,122,0,true},{"rin",KitQuality::Unique,122,0,true},{"rin",KitQuality::Unique,120,0,true},
    {"amu",KitQuality::Unique,272,0,true},{"cm1",KitQuality::Unique,381,0,true},{"cm2",KitQuality::Unique,400,0,true},
    {"cm3",KitQuality::Unique,359,0,true},{"jew",KitQuality::Unique,392,0,true},{"uui",KitQuality::Unique,358,0,true},
    {"6lw",KitQuality::Unique,266,0,true},{"uap",KitQuality::Unique,248,0,false}
}};
// Sets. All of Angelic Raiment (Sickle, Mantle, Halo, Wings) for the full set's bonus. Two of Cathan's Traps' five
// (Seal, Sigil) for a part set's dots and bonuses. 13 cells.
inline constexpr NamedKit CollectSetsKit{{
    {"sbr",KitQuality::Set,50,0,true},{"rng",KitQuality::Set,51,0,true},{"rin",KitQuality::Set,52,0,true},
    {"amu",KitQuality::Set,53,0,true},{"rin",KitQuality::Set,29,0,true},{"amu",KitQuality::Set,28,0,true}
}};
// Runewords, made by hand in the game from normal bases:
// - Stealth: a 2-socket Quilted Armor with Tal and Eth.
// - Spirit's two cards: a 4-socket Crystal Sword and a 4-socket Monarch, with Tal, Thul, Ort and Amn for each.
// 28 cells.
inline constexpr NamedKit CollectRunewordsKit{{
    {"qui",KitQuality::Normal,-1,2,true},{"crs",KitQuality::Normal,-1,4,true},{"uit",KitQuality::Normal,-1,4,true},
    {"r07",KitQuality::Normal,-1,0,true},{"r05",KitQuality::Normal,-1,0,true},
    {"r07",KitQuality::Normal,-1,0,true},{"r10",KitQuality::Normal,-1,0,true},{"r09",KitQuality::Normal,-1,0,true},{"r11",KitQuality::Normal,-1,0,true},
    {"r07",KitQuality::Normal,-1,0,true},{"r10",KitQuality::Normal,-1,0,true},{"r09",KitQuality::Normal,-1,0,true},{"r11",KitQuality::Normal,-1,0,true}
}};
// Gems and runes, which the collection leaves to the stash's own Gems and Runes tabs. One of each gem kind, from
// chipped to perfect: Perfect Amethyst, Flawless Ruby, Topaz, Flawed Sapphire, Chipped Emerald, Perfect Diamond,
// Perfect Skull. Then El, Ral, Ber, Jah and Zod. 12 cells.
inline constexpr NamedKit CollectGemsKit{{
    {"gpv",KitQuality::Normal,-1,0,true},{"glr",KitQuality::Normal,-1,0,true},{"gsy",KitQuality::Normal,-1,0,true},
    {"gfb",KitQuality::Normal,-1,0,true},{"gcg",KitQuality::Normal,-1,0,true},{"gpw",KitQuality::Normal,-1,0,true},
    {"skz",KitQuality::Normal,-1,0,true},{"r01",KitQuality::Normal,-1,0,true},{"r08",KitQuality::Normal,-1,0,true},
    {"r30",KitQuality::Normal,-1,0,true},{"r31",KitQuality::Normal,-1,0,true},{"r33",KitQuality::Normal,-1,0,true}
}};
inline const NamedKit* CollectionKit(int choice){
    return choice==CollectUniquesChoice?&CollectUniquesKit:choice==CollectSetsChoice?&CollectSetsKit:
           choice==CollectRunewordsChoice?&CollectRunewordsKit:choice==CollectGemsChoice?&CollectGemsKit:nullptr;
}
inline size_t NamedCount(const NamedKit& kit){size_t n=0;for(const auto& item:kit)n+=item.code!=nullptr;return n;}
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
    // Hunter's Bow, Long Bow and Chu-Ko-Nu as the three uniques (ItemBowUniques), arrows and bolts. 26 cells.
    if(ValidClass(cls)&&choice==ItemBowsChoice)return {"hbw","lbw","8rx","aqv","cqv"};
    // Keep this separate: the Paladin's full kit already occupies 37 cells.
    // Adding a 2x2 buckler would exceed the 40-cell inventory even when empty.
    if(ValidClass(cls)&&choice==SmallShieldChoice)return {"buc"};
    // Hand-throw tests (#221), any class: one stack of each thrown missile -
    // Javelin (1), Pilum (371), Glaive (37), Throwing Knife (36) and Axe (35).
    // 14 inventory cells.
    if(ValidClass(cls)&&choice==ThrowingChoice)return {"jav","pil","glv","tkf","tax"};
    // Potion toss tests, any class: the four kinds the hireling takes from a
    // throw (Health, Rejuvenation, Antidote, Thawing), for the belt. 10 cells.
    if(ValidClass(cls)&&choice==PotionsChoice)return {"hp5","hp5","hp5","hp5","rvl","rvl","yps","yps","wms","wms"};
    // Throwing potion tests, any class: one stack of each gas and oil potion
    // (Rancid, Choking, Strangling Gas; Oil, Exploding, Fulminating). 6 cells.
    if(ValidClass(cls)&&choice==ThrowingPotionsChoice)return {"gps","gpm","gpl","ops","opm","opl"};
    if(ValidClass(cls)&&choice==TorchChoice)return {"clb","clb","gcr","r08","clb","clb"};
    if(ValidClass(cls)&&choice==FlailsChoice) {
        std::array<const char*,10> codes{};
        for(size_t i=0;i<FlailKit.size();++i)codes[i]=FlailKit[i].code;
        return codes;
    }
    if(ValidClass(cls)&&choice==LootBeamsChoice) {
        std::array<const char*,10> codes{};
        static_assert(LootBeamKit.size()<=10,"Kit() returns at most 10 codes");
        for(size_t i=0;i<LootBeamKit.size();++i)codes[i]=LootBeamKit[i].code;
        return codes;
    }
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
// Normal quality at item level 1, usable at once; the other kits roll uniques at 99.
inline bool NormalKit(int choice){return StarterKit(choice)||choice==ThrowingChoice||choice==PotionsChoice||choice==TorchChoice||choice==ThrowingPotionsChoice||choice==LootBeamsChoice;}
inline bool IsKit(int choice){return choice==1||choice==2||choice==CrossbowChoice||choice==BowChoice||choice==SmallShieldChoice||choice==ItemBowsChoice||choice==FlailsChoice||NormalKit(choice)||CollectionKit(choice);}
inline bool Isolated(std::string_view mod,std::string_view metadata){return mod==Mod&&metadata==ModInfo;}
}
