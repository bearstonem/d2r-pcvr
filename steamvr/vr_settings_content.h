#pragma once
// VR Settings content: the words of the redesigned menu (labels, help, option
// names and hints, units, warn lines, pages, sections, presets, guide), minus
// MenuPointer (not on upstream main).
//
// Data only: no drawing, no layout, no input. The menu's data rules
// (docs/VR_SETTINGS_MENU.md):
// - The setting is the source. Every row comes from its Definitions entry
//   (vr_settings.h: ini section and key, default, min, max, step, label,
//   detail); the Overrides table below only adds our words and placement,
//   keyed by the ini id ("gameplay.highlight_left_hue"), never by Key, so an
//   upstream rename or removal leaves an orphan override (ignored, reported),
//   never a build error. Presets are keyed by ini id too.
// - Never restate a range or default. Min, max, step and default always come
//   from Definitions[key] (ini units); a row only says how to show them
//   (iniScale, unit, format). Saved ini files load unchanged.
// - Every override field but the id is optional; .page=NotInMenu keeps a
//   setting off the menu. Table order is page order; .section is a title from
//   SectionTable.
// - At most 8 rows a page can show (title screen: every class row), 3 sections.
// - Labels <= 26 characters (Skills go to the aimed monster: main's own 30),
//   help <= 160, warn <= 56, option labels <= 14, sentence case, American
//   spelling, one vocabulary across pages.
// - Every help sentence is true of the code (vr_settings_help.h holds the long
//   form); where a draft said more than the code does, the code wins.
// - Presets leave out StereoRendering, DeferGpuWait, CompanionAi and the
//   calibration keys (the owner's calls).
// The checks at the end of this file enforce these at compile time.
#include "vr_settings.h"
#include "vr_settings_draw.h"
#include "vr_settings_help.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
namespace framevr::settings::content {
using draw::Icon;

// Rail groups: Gameplay to Graphics each have a rail row (GroupTable) that opens to
// their pages. Tools: Presets heads the rail on its own row; Header: Guide,
// What's new and More, opened from the page band.
enum class Group : uint8_t { Tools, Gameplay, Comfort, Controls, Interface, Graphics, Header };

// How a row is controlled.
enum class RowKind : uint8_t {
    Toggle,    // Off / On pill (Definitions max==1)
    Choice,    // named options: chips (up to 6 that fit beside the label), else a popover list or grid
    Cycler,    // retired (read as Choice): the layout picks chips or a popover from the data
    Stepper,   // - value + with no bar (no upper limit, e.g. Melee hit speed)
    Slider,    // - value + with a fill bar and the default tick; drag to scrub
    Swatch,    // hue: < swatch name > cycler, or the card's color grid
    Button,    // an action, not a value (Calibrate body)
    Offer,     // a one-way button shown while the setting is Off (Defer GPU wait)
    Filter,    // its own number as a slider, then a Filter button opening a card of the On/Off switches (the inFilter rows after it): Loot beams
    Auto,      // an override's default: the kind comes from the data (DerivedKind)
};

// Which classes see a row (hidden in game for others; all show when the class
// is unknown, with a class tag). A weapon in hand also shows it (Bow/Staff/Thrown).
enum ClassMask : uint16_t {
    AllClasses=0,
    Amazon=1<<heroclass::Amazon,Sorceress=1<<heroclass::Sorceress,Necromancer=1<<heroclass::Necromancer,
    Paladin=1<<heroclass::Paladin,Barbarian=1<<heroclass::Barbarian,Druid=1<<heroclass::Druid,Assassin=1<<heroclass::Assassin,
    WithBow=1<<8,WithStaff=1<<9,WithThrown=1<<10,
};

// One named choice: its ini value, its name on the chip (<= ~14 chars) and
// the hint the help card shows while it is pointed at or chosen.
struct Option {
    float value;
    const char* label;
    const char* hint;
};
// A warn line under the label while the value is above a threshold ("Can
// cause motion sickness" while Smooth turning is On).
struct WarnWhen {
    float above=std::numeric_limits<float>::infinity();
    const char* text=nullptr;
};
// A word shown instead of a number for one value: { 0, "Off" }.
struct ValueName {
    float value;
    const char* label;
};

// How a number reads: shown = ini / iniScale,
// printed with up to `decimals` (trailing zeros dropped), then the unit. A space
// goes before word units, cm, m and m/s ("65 cm"), none before %, ° and °/s
// ("45%", "30°"). A word unit ending in s drops it for 1 ("1 ring").
struct Format {
    const char* unit="";        // "cm", "°", "%", "°/s", "m/s", "m", "" ...
    float iniScale=1;           // ini value = shown value x iniScale (Waist: .01)
    uint8_t decimals=0;         // the most the step needs; "12 m/s", "12.25 m/s"
    bool sign=false;            // signed ranges show "+"
    bool hueNames=false;        // a hue: swatch + color name ("Azure"), never a number
};

// Placement words besides a View: no row at all (eye height, legacy keys, a
// setting upstream keeps off its menu), or "no page given" (the setting's
// owner page; Fallback below).
inline constexpr View NotInMenu=View(0xFE),OwnerPage=View(0xFF);

// One override: our words and placement for one setting, keyed by its ini id
// ("section.key" as in Definitions). Every field but the id is optional;
// kind Auto takes the control from the data. Never a default, min, max or step.
struct Override {
    const char* id;
    View page=OwnerPage;
    const char* section=nullptr;     // a SectionTable title of that page; nullptr: the page's first
    RowKind kind=RowKind::Auto;
    const char* label=nullptr;       // nullptr: Definitions' label
    const char* help=nullptr;        // nullptr: Definitions' detail
    const char* warn=nullptr;
    const Option* options=nullptr;
    uint8_t optionCount=0;
    Format format{};
    const ValueName* names=nullptr;
    uint8_t nameCount=0;
    uint16_t classes=AllClasses;
    float recommended=std::numeric_limits<float>::quiet_NaN();
    bool wraps=false;
    WarnWhen warnWhen{};
    bool beside=false;               // a toggle or a number drawn as the right half of the previous override's row (main's PairOf)
    bool instead=false;              // shows only when the previous override's row does not (RowShown: snap / smooth turn speed)
    const char* besideLabel=nullptr; // beside: the word before its value in the box ("Big:" -> "Big: 600"); nullptr: the label; "": the value alone
    bool inFilter=false;             // a toggle that is one switch in the previous Filter row's card, no row of its own; its label is its word there (Loot beams: Unique, Set, Rare, Runes)
    View alsoOn=View::Count;         // the same live row drawn on this later page too, at its top with no section: one setting, one ini key, one Row(key)
};

// One row of the menu as the layout reads it: a Definitions entry merged with
// its override (Menu() below builds these once).
struct ContentRow {
    Key key;                    // the setting; its ini section/key and range: Definitions[key]
    View page;                  // the page it is on
    uint8_t section;            // index of its section within that page (the merged section list's order)
    RowKind kind;
    const char* label;          // <= 26 chars
    const char* help;           // <= 160 chars, the card's body
    const char* warn=nullptr;   // amber line under the label where a choice can hurt; nullptr none
    const Option* options=nullptr;   // Choice / Cycler: every option in order
    uint8_t optionCount=0;
    Format format{};            // Stepper / Slider / Swatch
    const ValueName* names=nullptr;  // words for some values ({0,"Off"}), checked before the number
    uint8_t nameCount=0;
    uint16_t classes=AllClasses;     // ClassMask bits; AllClasses: everyone
    float recommended=std::numeric_limits<float>::quiet_NaN();   // ini value where advice differs from the default; NaN none
    bool wraps=false;           // steps wrap round (hues, Lock on); otherwise they stop at the ends
    const char* id=nullptr;     // its ini id ("section.key")
    WarnWhen warnWhen{};        // a warn line while the value is above a threshold (the override's)
    bool fallback=false;        // no override placed it: upstream's words on its owner page or More
    bool beside=false;          // a toggle or a number on the previous row's right half (Override::beside), no row of its own
    const char* besideLabel=nullptr;   // the word before its value there (Override::besideLabel)
    bool inFilter=false;        // one switch in the previous Filter row's card, no row of its own (Override::inFilter)
    View alsoOn=View::Count;    // the second page that draws this row (Override::alsoOn)
};
// Whether a page draws this row: its own, or the one it is also on.
constexpr bool OnPage(const ContentRow& r,View v){return r.page==v||r.alsoOn==v;}

// A section: 2-5 rows that share an object, titled in sentence case.
struct ContentSection {
    View page;
    const char* title;
};

// A view: a settings page, Presets, Guide or What's new.
struct ContentPage {
    View id;
    const char* name;           // the rail tab / band title: one word for settings pages
    Group group;
    Icon icon;
    const char* intro;          // one line: the page's purpose (the card's idle text)
    bool settings;              // has rows (false: Presets, Guide, WhatsNew)
};

// A preset changes only the keys it lists, in ini units; every other setting
// stays as it is. Rows hidden for the current class are skipped (and said so).
// Authored by ini id (PresetSetting); Menu() resolves them to Keys (PresetValue),
// skipping and reporting an id upstream removed.
struct PresetSetting {
    const char* id;
    float value;
};
struct PresetSpec {
    const char* id;
    const char* name;
    Icon icon;
    const char* blurb;
    const PresetSetting* values;
    uint8_t count;
};
struct PresetValue {
    Key key;
    float value;
};
struct ContentPreset {
    const char* id;
    const char* name;
    Icon icon;
    const char* blurb;
    const PresetValue* values;
    uint8_t count;
};

// Footer and tool buttons: their words and help, keyed by what they do.
struct ContentTool {
    Kind kind;
    const char* label;
    const char* help;
};

// One line of the Controller guide: the control and what it does, in one of
// three columns. Button names follow the Button names setting at paint time:
// GuideControl replaces {R1}-{R4} / {L1}-{L4} with controllers::Labels::Button
// (hand 0 right / 1 left; 1 primary face, 2 secondary, 3 and 4 the same with Grip).
enum class GuideColumn : uint8_t { Left, Right, Panels };
struct GuideLine {
    const char* control;
    const char* action;
    GuideColumn column=GuideColumn::Right;
};

// ---- pages ----

inline constexpr ContentPage PageTable[]={
    {View::Presets,"Presets",Group::Tools,Icon::Presets,"Each preset sets only the rows it lists. Point at one to see what it changes.",false},
    {View::Combat,"Combat",Group::Gameplay,Icon::Combat,"How your real swings, stabs and throws hit.",true},
    {View::Targeting,"Targeting",Group::Gameplay,Icon::Aim,"Which monster or spot your spells and skills go to. Rows for other classes stay hidden.",true},
    {View::Shield,"Shield",Group::Gameplay,Icon::Shield,"Bash, block and seat a shield on your arm.",true},
    {View::Monsters,"Monsters",Group::Gameplay,Icon::Survival,"How tough offline monsters are, your mercenary and your summons. Outline and health bars: Interface.",true},
    {View::Physics,"Physics",Group::Gameplay,Icon::World,"Things your hands and weapons can knock, roll and push.",true},
    {View::Moving,"Moving",Group::Comfort,Icon::Fast,"Turning and how fast you move.",true},
    {View::Body,"Body",Group::Comfort,Icon::Body,"Your height, arm span, play space and potion belt.",true},
    {View::Hands,"Hands",Group::Controls,Icon::Controls,"Your hands and controllers: which hand does what, and whose button names.",true},
    {View::Vibration,"Vibration",Group::Controls,Icon::Haptics,"Controller vibration. Feel it uses the strength shown here, before you apply.",true},
    {View::Wrist,"Wrist",Group::Interface,Icon::Wrist,"What your left hand shows when you look at it.",true},
    {View::Menus,"Menus",Group::Interface,Icon::Interface,"How game menus open, and how soon item details show.",true},
    {View::Labels,"Labels",Group::Interface,Icon::Notes,"Names, health bars and loot beams in the world.",true},
    {View::Outlines,"Outlines",Group::Interface,Icon::Targets,"Outline the monster you aim at. Turn it on to pick colors.",true},
    {View::Warnings,"Warnings",Group::Interface,Icon::Survival,"Warnings when your life or mana runs low.",true},
    {View::Performance,"Performance",Group::Graphics,Icon::Performance,"Image detail and how far you see, with the last second's frame times below.",true},
    {View::Frames,"Frames",Group::Graphics,Icon::Fast,"How each frame is made: the eyes drawn, generated frames and the GPU wait.",true},
    {View::Environment,"Environment",Group::Graphics,Icon::World,"Extra sights and sound the mod adds to the game world. Far objects: Graphics > Performance.",true},
    {View::Guide,"Guide",Group::Header,Icon::Controls,"Each stick opens its hand's wheel. Point the other hand's laser at a wedge to assign a skill.",false},
    {View::WhatsNew,"What's new",Group::Header,Icon::Notes,"Changes in this version of the mod.",false},
    // A safety net: settings no override placed whose owner page is full or unknown
    // (Fallback). Not meant as a home for settings.
    {View::More,"More",Group::Header,Icon::Info,"Any setting not yet given a page.",true},
};
static_assert(std::size(PageTable)==size_t(View::Count),"one page per view, in View order");

// The rail under Presets: a row per group, in this order; the open group's
// pages hang under its row in PageTable order. A new page joins a group by its
// PageTable line alone.
struct ContentGroup {
    Group id;
    const char* name;           // the group row's Cinzel word: it must fit the row's 162 px (vr-settings-fit-test measures it)
    Icon icon;
};
inline constexpr ContentGroup GroupTable[]={
    {Group::Gameplay,"Gameplay",Icon::Combat},
    {Group::Comfort,"Comfort",Icon::Comfort},
    {Group::Controls,"Controls",Icon::Controls},
    {Group::Interface,"Interface",Icon::Interface},
    {Group::Graphics,"Graphics",Icon::Performance},
};
// The rail's rows at most (the layout's 78 px pitch from y 29 down to the frame):
// Presets, every group row and the largest group's pages.
constexpr int MaxRailRows=12,MaxGroupPages=6;
// A rail group's entry, or nullptr (Tools and Header have no row).
constexpr const ContentGroup* GroupOf(Group g){
    for(const auto& r:GroupTable)if(r.id==g)return &r;
    return nullptr;
}

// Every settings page's sections, in page order; a row's `section` counts
// within its page.
inline constexpr ContentSection SectionTable[]={
    {View::Combat,"Melee"},{View::Combat,"Skills and throws"},
    {View::Targeting,"Target selection"},{View::Targeting,"Aiming"},{View::Targeting,"Ground targeting"},
    {View::Shield,"Bash and block"},{View::Shield,"Fit on the arm"},
    {View::Monsters,"Offline difficulty"},{View::Monsters,"Mercenary"},{View::Monsters,"Summons"},
    {View::Physics,"Props"},{View::Physics,"Monsters and townsfolk"},{View::Physics,"Struck monsters"},
    {View::Moving,"Turning"},{View::Moving,"Speed"},
    {View::Body,"Play space"},{View::Body,"Potion belt"},
    {View::Hands,"Hands and buttons"},
    {View::Vibration,"Strength"},
    {View::Wrist,"Life and mana"},{View::Wrist,"Map"},
    {View::Menus,"Game menus"},
    {View::Labels,"Names and bars"},{View::Labels,"Loot beams"},
    {View::Outlines,"Monster outline"},
    {View::Warnings,"Low life"},{View::Warnings,"Low mana"},
    {View::Performance,"Image"},{View::Performance,"Draw distance"},
    {View::Frames,"Frame generation"},
    {View::Environment,"Sky and dungeons"},{View::Environment,"Glow"},
};

// ---- options and value words (ini order: option i has value min + i) ----

// Skills go to the aimed monster, spell_lock bits: 1 spells, 2 bow skills.
inline constexpr Option SpellLockOptions[]={
    {0,"Off","Spells and bow skills fly along your casting hand."},
    {1,"Spells","Spells go to the monster you aim at; bow skills fly along your hand."},
    {2,"Bow skills","Bow skills from a button go to the aimed monster; spells fly along your hand."},
    {3,"Both","Spells and bow skills go to the monster you aim at."},
};
// A class without bow skills sees Off / On, which flips the spell bit and keeps
// a saved bow-skills bit (LockStep): match these by the spell bit (SpellLockOption).
inline constexpr Option SpellLockSpellsOnly[]={
    {0,"Off","Spells fly along your casting hand."},
    {1,"On","Spells go to the monster you aim at."},
};
inline constexpr Option AimAssistOptions[]={
    {0,"Off","Spells fly exactly where your hand aims."},
    {1,"Light","A launch within 4\xc2\xb0 of a monster turns toward it."},
    {2,"Strong","A launch within 12\xc2\xb0 turns to the monster, and the spell gently homes in. Charged Bolt gets the launch turn only."},
};
inline constexpr Option GroundAimOptions[]={
    {0,"Game's aim","The game places ground skills its own way."},
    {1,"Pointed","On the floor where your casting hand points, with a landing circle."},
    {2,"Monster","On the monster you aim at, else where you point, with a landing circle."},
};
inline constexpr Option HighlightsOptions[]={
    {0,"Main hand only","One outline: your dominant hand's monster, else the one you look at."},
    {1,"Both hands","Each hand's monster gets its own outline in its own color."},
};
inline constexpr Option VitalsLocationOptions[]={
    {0,"Wrist","Turn your left wrist toward your face, like checking a watch."},
    {1,"Palm","Raise your open left palm toward you, grip and trigger released."},
    {2,"Wrist or palm","Either gesture shows it."},
    {3,"Off","Not shown on your hand."},
};
// map_top_down 0-3 (MapViewTopDown, MapViewFull): the corner minimap or the game's full map.
inline constexpr Option MapViewOptions[]={
    {0,"Isometric","The game's own slanted minimap, with NPC and exit names."},
    {1,"Top-down","The minimap laid flat like a floor plan. NPC and exit names are hidden."},
    {2,"Full map","The game's full map at its widest. Look at it and push the right stick up to zoom in, up to 16x."},
    {3,"Full top-down","The game's full map laid flat like a floor plan, zoomed the same way."},
};
inline constexpr Option MenuDistanceOptions[]={
    {0,"Near","Menus 55 cm away, the main menu 70 cm."},
    {1,"Medium","Menus 85 cm away, the main menu 1 m."},
    {2,"Far","Menus 1.15 m away, the main menu 1.3 m."},
};
// Echo (world_sound.h): the game's own reverb for the place you stand in, as the game mixes it or louder.
inline constexpr Option EchoOptions[]={
    {0,"Game's own","The echo as the game mixes it, made for speakers: faint in a headset."},
    {1,"More","The game's echo twice as loud: caves, crypts and halls ring around you."},
    {2,"Strong","Four times as loud, and it rings a quarter longer."},
};
inline constexpr Option TooltipOptions[]={
    {0,"Instant","Details show as soon as the laser reaches an item."},
    {1,"Short","Details show after the laser rests on an item for 0.3 s."},
    {2,"Game's timing","Details use the game's own delay."},
};
inline constexpr Option ButtonNamesOptions[]={
    {0,"Auto","Uses the names of the controllers you are holding."},
    {1,"Quest","Meta Quest Touch names, whatever is detected."},
    {2,"Index","Valve Index names, whatever is detected."},
    {3,"Steam Frame","Steam Frame names, whatever is detected."},
    {4,"Generic","Plain names that fit any controller."},
};
inline constexpr Option StereoOptions[]={
    {0,"Both","Each frame drawn twice, once per eye. True 3D, the most work."},
    {1,"One by turns","One eye a frame, taking turns. About half the work; fast monsters can look doubled."},
};
inline constexpr Option ResolutionOptions[]={
    {0,"Monitor size","Each eye uses the game window's size."},
    {1,"Headset size","Each eye uses the size your headset asks for, up to 4096."},
    {2,"1536 x 1536","A fixed size per eye. Larger is sharper and costs more."},
    {3,"2048 x 2048","A fixed size per eye. Larger is sharper and costs more."},
    {4,"2560 x 2560","A fixed size per eye. Larger is sharper and costs more."},
    {5,"3072 x 3072","A fixed size per eye. Larger is sharper and costs more."},
    {6,"4096 x 4096","A fixed size per eye. Larger is sharper and costs more."},
};
inline constexpr Option FoveationOptions[]={
    {0,"Off","Full detail everywhere."},
    {1,"Fixed","Less detail toward the lens edges; the center stays sharp."},
    {2,"Eye tracked","The sharp area follows your eyes, on headsets that share eye tracking. Full detail while no gaze arrives."},
};
inline constexpr Option FoveationStrengthOptions[]={
    {0,"Low","Edges barely reduced. The smallest saving."},
    {1,"Medium","A balance of edge detail and speed."},
    {2,"High","The same edge area, coarser."},
    {3,"Maximum","The coarsest edges and a smaller sharp center. The biggest saving."},
};
// [render] frame_generation 0 / 1 / 2, frame_generation_preset, frame_generation_scale (frame_generation.h).
inline constexpr Option FrameGenOptions[]={
    {0,"Off","No generated frames: the headset shows the frames the game draws."},
    {1,"FidelityFX","AMD FidelityFX optical flow on any graphics card. Dark areas can smear as you turn your head."},
    {2,"NVIDIA","The optical flow of RTX and GTX 1660 cards (not GTX 1650 or 10-series). Its quality and scale show below."},
};
inline constexpr Option FrameGenPresetOptions[]={
    {0,"Slow","The most careful search for motion, and the most graphics card time."},
    {1,"Medium","OFXR's suggested balance of quality and graphics card time."},
    {2,"Fast","The least graphics card time; OFXR still calls it a test setting."},
};
inline constexpr Option FrameGenScaleOptions[]={
    {0,"100%","Full size: the finest motion, but it usually costs more than it gains."},
    {1,"75%","Between full and half size."},
    {2,"50%","Half size: OFXR's suggested balance."},
};

inline constexpr ValueName OffAtZero[]={{0,"Off"}};
inline constexpr ValueName WalkNames[]={{0,"Look"},{1,"Controller"}};   // walk_direction.h: where you look, or where the left controller points
inline constexpr ValueName LeatherNames[]={{0,"Off"},{1,"Leather"},{2,"Menu only"}};   // leather::Style; owner 2026-10-29: "Hold Menu > Off/Leather/Menu Only"
inline constexpr ValueName ByRangeAtZero[]={{0,"By range"}};
inline constexpr ValueName LaserOffName[]={{LaserOff,"Off"}};
inline constexpr ValueName ShieldFollowNames[]={{0,"Forearm"},{100,"Wrist"}};
inline constexpr ValueName OfflinePlayersNames[]={{0,"Game's setting"},{1,"1 player"}};
// Main's Movement speed words (ValueLabel): Walk, Base run, Run N%, Full run (run_speed.h stops).
inline constexpr ValueName RunSpeedNames[]={{-10,"Walk"},{0,"Base run"},{10,"Run 10%"},{20,"Run 20%"},{30,"Run 30%"},{40,"Run 40%"},
    {50,"Run 50%"},{60,"Run 60%"},{70,"Run 70%"},{80,"Run 80%"},{90,"Run 90%"},{100,"Full run"}};

// The unit strings (° is \xc2\xb0: the build has no /utf-8).
inline constexpr const char* Degrees="\xc2\xb0";
inline constexpr const char* DegreesPerSecond="\xc2\xb0/s";

#define FRAMEVR_OPTIONS(list) .options=list,.optionCount=uint8_t(std::size(list))
#define FRAMEVR_NAMES(list) .names=list,.nameCount=uint8_t(std::size(list))

// ---- rows, page by page, section by section (layout::RowsOf keeps this order) ----

inline constexpr Override Overrides[]={
    // Combat: Melee
    {.id="gameplay.motion_controls",.page=View::Combat,.section="Melee",.kind=RowKind::Toggle,.label="Motion combat",
     .help="Swing weapons, draw bows, throw and use the belt with your hands. Keep this On: it is what makes the mod play in VR.",
     .warn="Off turns off swings, bow draw, throws and the belt",.recommended=1},
    {.id="gameplay.melee_hit_speed",.page=View::Combat,.section="Melee",.kind=RowKind::Stepper,.label="Swing speed to hit",
     .help="How fast a swing must move to strike. Lower makes hits easier; higher asks for real swings, with no upper limit. Shield bash has its own speed.",
     .format={.unit="m/s",.decimals=2}},
    {.id="gameplay.melee_thrust_speed",.page=View::Combat,.section="Melee",.kind=RowKind::Slider,.label="Stab speed to hit",
     .help="How fast a forward stab along the blade must move to strike. Pull back before you stab again.",
     .format={.unit="m/s",.decimals=2}},
    // Melee reach (Discord, 2026-11-08: hits "not registering because my body is desynchronized just slightly ...
    // needlessly tight"): [gameplay] melee_reach_cm pads each monster's body for weapon and fist contacts. 0 by default.
    {.id="gameplay.melee_reach_cm",.page=View::Combat,.section="Melee",.kind=RowKind::Slider,.label="Melee reach",
     .help="A weapon or fist that passes this close beside a monster still hits it. Raise it if near misses don't count. Walls still block.",
     .format={.unit="cm"}},
    // Weapon heft and Bow bash (#443; Discord #ideas, 2026-11-04): one row, the page's last free one (8 at most).
    // [gameplay] weapon_heft and bow_bash, both Off by default.
    {.id="gameplay.weapon_heft",.page=View::Combat,.section="Melee",.kind=RowKind::Toggle,.label="Weapon heft",
     .help="Heavy weapons need a long stroke of your arm for a full hit; a quick flick lands lighter. Daggers, claws and fists still hit fully with a flick."},
    {.id="gameplay.bow_bash",.page=View::Combat,.section="Melee",.kind=RowKind::Toggle,.label="Bow bash",
     .help="Swing your bow, string not drawn, into a monster to stagger it while you step back and aim. No damage; every 1.5 s at most, never act bosses.",
     .beside=true,.besideLabel="Bow bash"},
    // Combat: Skills and throws
    {.id="gameplay.skill_gestures",.page=View::Combat,.section="Skills and throws",.kind=RowKind::Toggle,.label="Skills on your moves",
     .help="A trigger skill powers your real move: melee skills on a landed strike, Smite on a bash, arrow skills on a release. Off, every swing is plain."},
    {.id="interactions.weapon_skill_indicators",.page=View::Combat,.section="Skills and throws",.kind=RowKind::Toggle,.label="Skill icons on weapons",
     .help="Shows each trigger skill's icon and name beside the weapon that casts it. Turn it On while you learn your setup."},
    {.id="gameplay.throw_lift_deg",.page=View::Combat,.section="Skills and throws",.kind=RowKind::Stepper,.label="Thrown weapon lift",
     .help="Your hand dips as you let go. This lifts javelins, knives, axes and thrown potions above that path. Minus lowers them; Off follows your hand.",
     .format={.unit=Degrees,.sign=true},FRAMEVR_NAMES(OffAtZero),.classes=Amazon|Barbarian|Assassin|WithThrown},
    // Targeting: Target selection
    {.id="gameplay.spell_lock",.page=View::Targeting,.section="Target selection",.kind=RowKind::Choice,.label="Skills go to the aimed monster",
     .help="Casts go to the monster your hand points at, else the other hand's, else the one you look at, and stay on it until you let go.",
     FRAMEVR_OPTIONS(SpellLockOptions),.wraps=true},
    {.id="gameplay.projectile_aim_assist",.page=View::Targeting,.section="Target selection",.kind=RowKind::Choice,.label="Spell aim assist",
     .help="Helps supported hand spells find their monster. Bows and crossbows are always aimed by you alone.",
     FRAMEVR_OPTIONS(AimAssistOptions)},
    // Targeting: Aiming
    {.id="gameplay.planar_spell_aim",.page=View::Targeting,.section="Aiming",.kind=RowKind::Toggle,.label="Keep spell shots level",
     .help="On keeps straight spell shots level with the ground. Off lets you aim them up or down."},
    {.id="gameplay.staff_point",.page=View::Targeting,.section="Aiming",.kind=RowKind::Toggle,.label="Aim staff along arm",
     .help="An upright staff casts along your arm, from shoulder through hand. Held out flat like a wand, it casts from its tip as before.",
     .classes=Sorceress|Druid|WithStaff},
    {.id="gameplay.aim_follows_view",.page=View::Targeting,.section="Aiming",.kind=RowKind::Toggle,.label="Aim with your view",
     .help="Only monsters in front of your face can be aimed at. A hand held out and pointing still picks its own monster."},
    // Aim with your eyes (gaze_aim.h; owner 2026-11-08): the right half of Aim with your view's row, shown only on a
    // headset that shares eye tracking (RowShown: Context::eyeTracker).
    {.id="gameplay.aim_with_eyes",.page=View::Targeting,.section="Aiming",.kind=RowKind::Toggle,.label="Aim with your eyes",
     .help="The monster you look at is aimed at by where your eyes look, not your head. A blink keeps it; if tracking is lost, your head aims.",
     .beside=true,.besideLabel="Eyes"},
    {.id="bow.path_laser_hue",.page=View::Targeting,.section="Aiming",.kind=RowKind::Swatch,.label="Arrow path line",
     .help="At full draw, a faint line shows the arrow's flight, up to 15 m, through walls. Not for crossbows, Multiple Shot, Guided Arrow or Strafe.",
     .format={.hueNames=true},FRAMEVR_NAMES(LaserOffName),.classes=Amazon|WithBow,.wraps=true},
    // Targeting: Ground targeting
    {.id="gameplay.ground_aim",.page=View::Targeting,.section="Ground targeting",.kind=RowKind::Choice,.label="Ground skill aim",
     .help="Where skills like Blizzard, Meteor, Fire Wall, traps and Bone Wall land.",
     FRAMEVR_OPTIONS(GroundAimOptions),.classes=Sorceress|Assassin|Druid|Necromancer},
    {.id="gameplay.landing_circles",.page=View::Targeting,.section="Ground targeting",.kind=RowKind::Toggle,.label="Landing circles",
     .help="A circle on the floor in the skill's color shows where it will land. It fills up until the skill hits.",
     .classes=Sorceress|Assassin|Druid|Necromancer},
    // Shield: Bash and block
    {.id="gameplay.shield_bash_speed",.page=View::Shield,.section="Bash and block",.kind=RowKind::Slider,.label="Bash speed to hit",
     .help="How fast your shield hand must move to bash, Smite included. Move at least 12 cm in one stroke; twisting the wrist never bashes.",
     .format={.unit="m/s",.decimals=2}},
    {.id="gameplay.guard_strain_vignette",.page=View::Shield,.section="Bash and block",.kind=RowKind::Toggle,.label="Guard strain shadow",
     .help="Blocking many hits in a row darkens the edge of your view, faster for rarer attackers. Off hides the shadow; strain still builds."},
    // Shield: Fit on the arm
    {.id="gameplay.shield_grip_cm",.page=View::Shield,.section="Fit on the arm",.kind=RowKind::Slider,.label="Shield along forearm",
     .help="Slides the shield toward your elbow (+) so your hand reaches its strap, or toward your hand (-). About 3 cm suits a buckler.",
     .format={.unit="cm",.sign=true}},
    {.id="gameplay.shield_out_cm",.page=View::Shield,.section="Fit on the arm",.kind=RowKind::Slider,.label="Shield out from arm",
     .help="Moves the shield away from your forearm (+) or into it (-). It blocks where you see it.",
     .format={.unit="cm",.sign=true}},
    {.id="gameplay.shield_up_cm",.page=View::Shield,.section="Fit on the arm",.kind=RowKind::Slider,.label="Shield across forearm",
     .help="Moves the shield sideways across your forearm. Use it with the two rows above to seat the shield on your arm.",
     .format={.unit="cm",.sign=true}},
    {.id="gameplay.shield_follow_pct",.page=View::Shield,.section="Fit on the arm",.kind=RowKind::Slider,.label="Shield wrist turn",
     .help="0% straps the shield to your forearm, so bending the wrist leaves it still. 100% turns it with your hand.",
     .format={.unit="%"},FRAMEVR_NAMES(ShieldFollowNames)},
    {.id="gameplay.shield_roll_deg",.page=View::Shield,.section="Fit on the arm",.kind=RowKind::Slider,.label="Shield roll",
     .help="Turns the shield around your forearm; + turns it toward your little finger. +90\xc2\xb0 suits a controller held palm down.",
     .format={.unit=Degrees,.sign=true}},
    // Monsters: Offline difficulty
    {.id="gameplay.offline_players",.page=View::Monsters,.section="Offline difficulty",.kind=RowKind::Stepper,.label="Offline difficulty",
     .help="Offline monsters scale for this many players, with more experience and loot.",
     .format={.unit="players"},FRAMEVR_NAMES(OfflinePlayersNames)},
    {.id="gameplay.enemy_sight_pct",.page=View::Monsters,.section="Offline difficulty",.kind=RowKind::Slider,.label="Monster sight range",
     .help="How far away monsters notice you offline. 100% is the game's range; 300% is three times as far.",
     .format={.unit="%"}},
    // Monsters: Mercenary
    {.id="gameplay.companion_ai",.page=View::Monsters,.section="Mercenary",.kind=RowKind::Toggle,.label="Smarter mercenary",
     .help="Your mercenary keeps close in fights, spots monsters as far as they spot you, and is moved behind you if it gets stuck."},
    {.id="gameplay.companion_tactical_commands",.page=View::Monsters,.section="Mercenary",.kind=RowKind::Toggle,.label="Tactical commands",
     .help="Point at your mercenary, hold Grip and pull Trigger, aim and let go: hold ground, prioritize monsters, or regroup.",
     .beside=true,.besideLabel="Commands"},
    // Mercenary potion assistance, independent of class and outline settings (#424, RobinHeggelund).
    {.id="gameplay.merc_potion_assist",.page=View::Monsters,.section="Mercenary",.kind=RowKind::Toggle,.label="Merc potion throw autoaim",
     .help="On: thrown potions feed your living mercenary wherever they land. Off: land within 4 m of its feet; misses keep the potion. Slow releases return it."},
    // Monsters: Summons (#426, RobinHeggelund)
    {.id="gameplay.summon_range_multiplier",.page=View::Monsters,.section="Summons",.kind=RowKind::Slider,.label="Summon range",
     .help="Multiplies summon target search and owner-follow ranges together. Mercenaries and attack reach stay native. Apply, then restart the game.",.warn="Requires restart",.format={.unit="x",.decimals=2}},
    // Physics: Props
    {.id="interactions.bottle_physics",.page=View::Physics,.section="Props",.kind=RowKind::Toggle,.label="Roll and break bottles",
     .help="Empty bottles drop and roll. Potions thrown at your mercenary break where they hit; thrown traps and gas or oil potions stop at walls."},
    {.id="interactions.prop_physics",.page=View::Physics,.section="Props",.kind=RowKind::Toggle,.label="Knock over small props",
     .help="Cups, vases, books and stools tumble when your weapon, hand or shield hits them. Just for looks."},
    {.id="interactions.body_physics",.page=View::Physics,.section="Props",.kind=RowKind::Toggle,.label="Pushable bodies",
     .help="Your weapon, shield or fist pushes aside the limbs of monsters and townsfolk it meets, and they spring back. Just for looks. Motion combat in first person."},
    // Physics: Monsters and townsfolk. Heads turn to you (look_at.h; owner, 2026-11-09), On by default. On this page
    // beside Pushable bodies, the other drawn-only change to their poses (Monsters already has its three sections).
    {.id="gameplay.look_at_you",.page=View::Physics,.section="Monsters and townsfolk",.kind=RowKind::Toggle,.label="Heads turn to you",
     .help="Monsters, mercenaries and townsfolk within about 7 m turn their heads to your real head as you move. Just for looks. First person only."},
    // Physics: Struck monsters (hit_reactions.h; [gameplay] hit_stagger and hit_knockback, both On by default)
    {.id="gameplay.hit_stagger",.page=View::Physics,.section="Struck monsters",.kind=RowKind::Toggle,.label="Monsters stagger",
     .help="A long, committed stroke that lands makes the monster flinch, whatever the damage. Each monster at most every 1.2 s; act bosses never."},
    {.id="gameplay.hit_knockback",.page=View::Physics,.section="Struck monsters",.kind=RowKind::Toggle,.label="Heavy hits knock back",
     .help="A full stroke of a two-handed weapon, spear, polearm or staff shoves the monster back. Each at most every 2.5 s; never bosses, uniques or champions."},
    // Moving: Turning
    {.id="turn.smooth",.page=View::Moving,.section="Turning",.kind=RowKind::Toggle,.label="Smooth turning",
     .help="On turns you smoothly with the right stick. Off turns you in steps, which many players find easier on the stomach.",
     .warnWhen={.5f,"Can cause motion sickness"}},
    {.id="turn.snap",.page=View::Moving,.section="Turning",.kind=RowKind::Stepper,.label="Snap turn angle",
     .help="How far each flick of the right stick turns you. Smaller angles give finer steps.",
     .format={.unit=Degrees}},
    {.id="turn.speed",.page=View::Moving,.section="Turning",.kind=RowKind::Slider,.label="Smooth turn speed",
     .help="How fast the right stick turns you, in degrees per second. Lower is gentler.",
     .format={.unit=DegreesPerSecond},.warnWhen={150,"Fast turns are harder on the stomach"},.instead=true},
    // Moving: Speed
    // Movement speed (main's #339). Its Stick toggle (#346) is on Hands: it changes what the stick does.
    {.id="gameplay.run_speed_pct",.page=View::Moving,.section="Speed",.kind=RowKind::Slider,.label="Movement speed",
     .help="Walk, or run from Base run (your class's run, no speed bonus) up to Full run (default: your gear's and skills' speed). Movement skills keep their own speed.",
     FRAMEVR_NAMES(RunSpeedNames)},
    // Body: Play space
    {.id="interactions.seated",.page=View::Body,.section="Play space",.kind=RowKind::Toggle,.label="Seated play",
     .help="Raises the potion belt so you can reach it sitting down. Your body measurements still come from standing."},
    // Room follows you (room_steps.h; Discord, 2026-11-04: "my character's feet stay anchored near the calibration
    // point while my body follows the headset"): the right half of Seated play's row, shown while it is off (RowShown).
    {.id="gameplay.room_steps",.page=View::Body,.section="Play space",.kind=RowKind::Toggle,.label="Room follows you",
     .help="Your hero's spot follows you as you move in your room, so monsters attack where you stand. Walls stop it. Off: it stays where you calibrated.",
     .beside=true,.besideLabel="Follow"},
    {.id="calibration.version",.page=View::Body,.section="Play space",.kind=RowKind::Button,.label="Height and arm span",
     .help="Stand straight, then hold your arms out in a T. Measures your eye height and arm span so the world and your arms fit you."},
    // T cross to calibrate (#423, beadlebytes): its own row under Height and arm span, the calibration it opens
    // (Room follows you has Seated play's right half). It does nothing seated, and is left out then.
    {.id="interactions.t_cross_calibrate",.page=View::Body,.section="Play space",.kind=RowKind::Toggle,.label="T cross to calibrate",
     .help="Hold a T cross for 2 seconds, nothing pressed: the controllers buzz while you hold it, then Calibrate body opens. No menu needed. Off in Seated play."},
    // Body: Potion belt
    {.id="interactions.waist_below_eyes",.page=View::Body,.section="Potion belt",.kind=RowKind::Slider,.label="Belt below eyes",
     .help="How far below your eyes the potion belt sits when standing. Lower numbers raise the belt.",
     .format={.unit="cm",.iniScale=.01f}},
    {.id="interactions.belt_forward",.page=View::Body,.section="Potion belt",.kind=RowKind::Slider,.label="Belt forward reach",
     .help="How far in front of your body the belt sits. Raise it if the belt is too close; lower it if you reach too far.",
     .format={.unit="cm",.iniScale=.01f,.decimals=1}},
    // Potion drinking by trigger, beside the mouth gesture (#425, RobinHeggelund).
    {.id="interactions.click_to_drink",.page=View::Body,.section="Potion belt",.kind=RowKind::Toggle,.label="Click to drink",
     .help="On: hold a flask and press that hand's trigger to drink. The mouth gesture still works. Off: bring the opening to your mouth and tip to drink."},
    // Hands: Hands and buttons
    // Walk direction (owner, 2026-11-08: "moving where the controller points versus where you look, that should be a
    // setting"): a row of its own since Dominant hand's row went (2026-11-10). [stick] walk_direction.
    {.id="stick.walk_direction",.page=View::Hands,.section="Hands and buttons",.kind=RowKind::Stepper,.label="Walk direction",
     .help="Look: the left stick walks where you look. Controller: where the left controller points, so you can look around while you walk.",
     FRAMEVR_NAMES(WalkNames)},
    // Left-handed play (hand_role.h, #307): the one switch for the hands. [interactions] left_handed.
    {.id="interactions.left_handed",.page=View::Hands,.section="Hands and buttons",.kind=RowKind::Toggle,.label="Left-handed (experimental)",
     .help="On: weapon in the left hand, shield on the right; buttons, sticks, laser, wrist HUD and class motions mirror. A bow sits in the right hand, drawn by the left."},
    {.id="gameplay.run_walk_toggle",.page=View::Hands,.section="Hands and buttons",.kind=RowKind::Toggle,.label="Stick toggle",
     .help="On: right stick up switches between walking and the run set in Comfort > Moving (from Walk, the full run). Not with a wheel open or looking at the wrist map."},
    {.id="interactions.controller_labels",.page=View::Hands,.section="Hands and buttons",.kind=RowKind::Choice,.label="Button names",
     .help="Which controller's button names appear in help and menus. Only the names change, not what the buttons do.",
     FRAMEVR_OPTIONS(ButtonNamesOptions)},
    // Vibration: Strength
    {.id="haptics.strength",.page=View::Vibration,.section="Strength",.kind=RowKind::Slider,.label="Overall vibration",
     .help="Scales every kind of vibration below on both controllers. 100% is normal; Off stops all vibration.",
     .format={.unit="%"},FRAMEVR_NAMES(OffAtZero)},
    {.id="haptics.melee",.page=View::Vibration,.section="Strength",.kind=RowKind::Slider,.label="Melee hits",
     .help="Weapon and shield impacts. Feel it: Weapon buzzes the right controller, Shield the left.",
     .format={.unit="%"},FRAMEVR_NAMES(OffAtZero)},
    {.id="haptics.short_melee",.page=View::Vibration,.section="Strength",.kind=RowKind::Toggle,.label="Short melee taps",
     .help="On uses very short, sharp taps for hits. Some controllers barely feel them: leave it Off if hits feel weak."},
    // Richer haptics: what a strike feels like by what it struck (haptic_impact.h), in Short melee taps' row: the page's 8 rows are taken.
    {.id="haptics.hit_patterns",.page=View::Vibration,.section="Strength",.kind=RowKind::Toggle,.label="Hit feel by target",
     .help="A hit feels like what you struck: a thud for flesh, clicks for bone, a shiver for spirits, a crack and crumble for pots, a ring for a parry. Off: one tap.",
     .beside=true,.besideLabel="By target"},
    {.id="haptics.bow_draw",.page=View::Vibration,.section="Strength",.kind=RowKind::Slider,.label="Bow draw",
     .help="Nocking an arrow, pulling the string and reaching full draw.",
     .format={.unit="%"},FRAMEVR_NAMES(OffAtZero)},
    {.id="haptics.bow_release",.page=View::Vibration,.section="Strength",.kind=RowKind::Slider,.label="Bow and throw release",
     .help="Letting an arrow fly or throwing a javelin.",
     .format={.unit="%"},FRAMEVR_NAMES(OffAtZero)},
    {.id="haptics.spells",.page=View::Vibration,.section="Strength",.kind=RowKind::Slider,.label="Spell casts",
     .help="One short buzz in the casting hand each time a supported hand spell casts.",
     .format={.unit="%"},FRAMEVR_NAMES(OffAtZero)},
    {.id="haptics.interactions",.page=View::Vibration,.section="Strength",.kind=RowKind::Slider,.label="Loot, potions and menus",
     .help="Picking up loot, handling potions, holsters and pressing menu buttons.",
     .format={.unit="%"},FRAMEVR_NAMES(OffAtZero)},
    {.id="haptics.alerts",.page=View::Vibration,.section="Strength",.kind=RowKind::Slider,.label="Low life and mana alerts",
     .help="The low health and mana buzz on the left controller. Feel it works anyway; real alerts also need Interface > Warnings > Low life and mana alert On.",
     .format={.unit="%"},FRAMEVR_NAMES(OffAtZero)},
    // Wrist: Life and mana
    {.id="interactions.vitals_location",.page=View::Wrist,.section="Life and mana",.kind=RowKind::Choice,.label="Life and mana",
     .help="Which left-hand gesture shows your health and mana.",
     FRAMEVR_OPTIONS(VitalsLocationOptions)},
    {.id="interactions.vitals_tilt",.page=View::Wrist,.section="Life and mana",.kind=RowKind::Slider,.label="Life and mana tilt",
     .help="Tilts the health and mana display on your hand; + tips its top toward you. The minimap keeps its own angle.",
     .format={.unit=Degrees,.sign=true}},
    {.id="interactions.skill_cooldowns",.page=View::Wrist,.section="Life and mana",.kind=RowKind::Toggle,.label="Skill cooldowns",
     .help="Dimmed icons of up to three recharging skills between the orbs, filling back with color. A glyph or motion cast of one is refused; a tick says it is ready."},
    // Wrist: Map
    {.id="interactions.navigation_location",.page=View::Wrist,.section="Map",.kind=RowKind::Choice,.label="Map and compass",
     .help="Which left-hand gesture shows the minimap and compass, separately from health and mana.",
     FRAMEVR_OPTIONS(VitalsLocationOptions)},
    {.id="interactions.map_top_down",.page=View::Wrist,.section="Map",.kind=RowKind::Choice,.label="Map view",
     .help="The minimap or the game's full map, slanted or flat. The gold arrow shows which way you face. Look at the map and push the right stick to zoom.",
     FRAMEVR_OPTIONS(MapViewOptions)},
    {.id="interactions.map_rotate",.page=View::Wrist,.section="Map",.kind=RowKind::Toggle,.label="Turn map with you",
     .help="On keeps the way you face at the top of the map. Off keeps north at the top and turns the arrow. Top-down views only."},
    {.id="interactions.companion_counts",.page=View::Wrist,.section="Map",.kind=RowKind::Toggle,.label="Buffs and companions",
     .help="Under the minimap: your companion counts, and the buffs, auras and harmful effects on you. Follows the minimap's gesture."},
    // Menus: Game menus
    {.id="hud.menu_distance",.page=View::Menus,.section="Game menus",.kind=RowKind::Choice,.label="Menu distance",
     .help="How far away merchants, the stash and the main menu open, and the inventory with Hold menu Off. Point at one, hold Right Grip and push or pull it.",
     FRAMEVR_OPTIONS(MenuDistanceOptions)},
    // The leather menu (main's #360): its right half, as main pairs them (PairOf). Off / On / Menu only
    // (owner 2026-10-29: the menu held without the hide), a stepper so it still fits beside.
    {.id="interactions.leather_menu",.page=View::Menus,.section="Game menus",.kind=RowKind::Stepper,.label="Hold menu",
     .help="Leather: the inventory and other tabs on a hide in your left hand; reach over your left shoulder and grip. Menu only: no hide. Off: in front of you.",
     FRAMEVR_NAMES(LeatherNames),.beside=true,.besideLabel=""},   // the value alone: "Hold: Menu only" squeezed Menu distance's label
    {.id="hud.tooltip_timing",.page=View::Menus,.section="Game menus",.kind=RowKind::Choice,.label="Item details delay",
     .help="How soon an item's details pop up when you point the laser at it in a menu.",
     FRAMEVR_OPTIONS(TooltipOptions)},
    // Labels: Names and bars
    {.id="hud.labels_size",.page=View::Labels,.section="Names and bars",.kind=RowKind::Slider,.label="Loot name size",
     .help="Size of item names on the ground; 45% is the game's size. Hold the right stick down to show every nearby name.",
     .format={.unit="%"}},
    // Loot pick (#257; loot_aim.h): which dropped item A takes, On by default. Off: the game's nearest. The right
    // half of Loot name size's row.
    {.id="interactions.loot_pick",.page=View::Labels,.section="Names and bars",.kind=RowKind::Toggle,.label="Loot pick",
     .help="Hold your arm out and point at an item: A picks up that one, not the nearest, and its name shows. Too far away, A takes nothing.",
     .beside=true},
    {.id="hud.ordinary_mob_bars",.page=View::Labels,.section="Names and bars",.kind=RowKind::Toggle,.label="Weak monster bars",
     .help="Names and health bars over ordinary monsters. Bosses and elites always show theirs. Off keeps the view clear."},
    // Labels: Loot beams
    // Loot beams (vr/loot_beam_scan.cpp): one row - its brightness slider, then a Filter button
    // opening a card of four switches (owner 2026-10-31: "one line item that says loot beams and
    // then the button says filter", a sub-menu "selecting on or off for each type of beam";
    // 2026-11-01: a slider on that row "so players can decrease the current vibrancy").
    {.id="loot.beams_brightness",.page=View::Labels,.section="Loot beams",.kind=RowKind::Filter,.label="Loot beams",
     .help="Pillars of light over unique, set and rare items and runes Vex and up. The slider sets their brightness; Filter chooses which.",
     .format={.unit="%"}},
    {.id="loot.beams_unique",.page=View::Labels,.section="Loot beams",.kind=RowKind::Toggle,.label="Unique",
     .help="A gold beam over unique items lying near you, until you pick them up.",.inFilter=true},
    {.id="loot.beams_set",.page=View::Labels,.section="Loot beams",.kind=RowKind::Toggle,.label="Set",
     .help="A green beam over set items lying near you, until you pick them up.",.inFilter=true},
    {.id="loot.beams_rare",.page=View::Labels,.section="Loot beams",.kind=RowKind::Toggle,.label="Rare",
     .help="A yellow beam over rare items lying near you. Rares drop often: Off lights only the rarer finds.",.inFilter=true},
    {.id="loot.beams_runes",.page=View::Labels,.section="Loot beams",.kind=RowKind::Toggle,.label="Runes",
     .help="An orange beam over high runes lying near you, Vex and up.",.inFilter=true},
    // Outlines: Monster outline
    {.id="gameplay.target_outline_pct",.page=View::Outlines,.section="Monster outline",.kind=RowKind::Slider,.label="Monster outline",
     .help="Outlines the monster you aim at, as strongly as the percentage. A hand held out picks its monster; otherwise the one you look at.",
     .format={.unit="%"},FRAMEVR_NAMES(OffAtZero)},
    {.id="gameplay.aim_highlights",.page=View::Outlines,.section="Monster outline",.kind=RowKind::Choice,.label="Outlines shown",
     .help="Outline only your dominant hand's monster, or one for each hand.",
     FRAMEVR_OPTIONS(HighlightsOptions)},
    {.id="gameplay.highlight_left_hue",.page=View::Outlines,.section="Monster outline",.kind=RowKind::Swatch,.label="Left hand outline color",
     .help="The left hand's outline color. By range: red near, orange mid, yellow far. Pick a color unlike the monsters' own.",
     .format={.hueNames=true},FRAMEVR_NAMES(ByRangeAtZero)},
    {.id="gameplay.highlight_right_hue",.page=View::Outlines,.section="Monster outline",.kind=RowKind::Swatch,.label="Right hand outline color",
     .help="The right hand's outline color. By range: red near, orange mid, yellow far. Pick a color unlike the monsters' own.",
     .format={.hueNames=true},FRAMEVR_NAMES(ByRangeAtZero)},
    // Warnings: Low life
    {.id="interactions.critical_alerts",.page=View::Warnings,.section="Low life",.kind=RowKind::Toggle,.label="Low life and mana alert",
     .help="A brief warning and a buzz when health or mana runs low. Set the buzz under Controls > Vibration > Low life and mana alerts."},
    {.id="gameplay.low_life_heart_start",.page=View::Warnings,.section="Low life",.kind=RowKind::Stepper,.label="Heartbeat starts at",
     .help="The life level where a heartbeat starts. It grows louder and faster with every 10% you lose. Off silences it.",
     .format={.unit="%"},FRAMEVR_NAMES(OffAtZero)},
    // Richer haptics: the same heart felt in both controllers (haptic_heartbeat.h), in the heartbeat's own row.
    {.id="haptics.heartbeat",.page=View::Warnings,.section="Low life",.kind=RowKind::Toggle,.label="Felt heartbeat",
     .help="Both controllers beat like a heart while the heartbeat sounds, faster and harder as life drops. Vibration > Low life and mana alerts sets its strength.",
     .beside=true,.besideLabel="Felt"},
    {.id="hud.blood_visor",.page=View::Warnings,.section="Low life",.kind=RowKind::Toggle,.label="Blood on visor",
     .help="Blood splashes the edges of your view as life drops below 60%, 40% and 20%. Healing back up fades it."},
    {.id="hud.blood_wipe",.page=View::Warnings,.section="Low life",.kind=RowKind::Toggle,.label="Wipe blood by hand",
     .help="Wave a hand side to side in front of your face, trigger released, to wipe the blood away. It does not heal you."},
    // Warnings: Low mana
    {.id="hud.arcane_decay",.page=View::Warnings,.section="Low mana",.kind=RowKind::Toggle,.label="Arcane Decay",
     .help="A subtle spectral vignette when mana is low, warning of magical depletion without obscuring your view."},
    {.id="hud.arcane_fade",.page=View::Warnings,.section="Low mana",.kind=RowKind::Toggle,.label="Fade animation",
     .help="Smoothly fade the arcane vignette in and out as mana changes.",
     .beside=true,.besideLabel="Fade"},
    {.id="hud.mana_threshold_pct",.page=View::Warnings,.section="Low mana",.kind=RowKind::Slider,.label="Starts below",
     .help="Mana percentage below which the arcane decay warning begins to appear.",
     .format={.unit="%"}},
    {.id="hud.arcane_intensity_pct",.page=View::Warnings,.section="Low mana",.kind=RowKind::Slider,.label="Effect intensity",
     .help="Overall brightness and opacity scale of the arcane warning effect.",
     .format={.unit="%"},.beside=true,.besideLabel="Strength"},   // beside Starts below
    // Performance: Image
    {.id="stereo.rendering",.page=View::Performance,.section="Image",.kind=RowKind::Choice,.label="Eyes drawn per frame",
     .help="The biggest speed setting: drawing one eye a frame can nearly double the frame rate. Menus and views other than first person draw both eyes.",
     FRAMEVR_OPTIONS(StereoOptions),.warnWhen={.5f,"Fast monsters can look doubled"},.alsoOn=View::Frames},   // Frames' rows depend on it
    {.id="render.vr_resolution",.page=View::Performance,.section="Image",.kind=RowKind::Choice,.label="Render resolution",
     .help="The image size for each eye. Higher is sharper but costs graphics card time. DLSS is in the game's own Options.",
     FRAMEVR_OPTIONS(ResolutionOptions)},
    {.id="render.fixed_foveation",.page=View::Performance,.section="Image",.kind=RowKind::Choice,.label="Lower detail at edges",
     .help="Draws the edges of the view, where you see least, in less detail to save graphics card time.",
     FRAMEVR_OPTIONS(FoveationOptions)},
    {.id="render.foveation_strength",.page=View::Performance,.section="Image",.kind=RowKind::Choice,.label="Edge detail reduction",
     .help="How much detail the edges lose. Stronger saves more time but is easier to notice.",
     FRAMEVR_OPTIONS(FoveationStrengthOptions)},
    // Performance: Draw distance
    {.id="render.rings",.page=View::Performance,.section="Draw distance",.kind=RowKind::Stepper,.label="Terrain distance",
     .help="How many rings of map tiles stay drawn around you. Lower it first if big outdoor areas run slowly.",
     .format={.unit="rings"}},
    {.id="render.model_radius",.page=View::Performance,.section="Draw distance",.kind=RowKind::Slider,.label="Scenery distance",
     .help="How far away buildings and props stay visible; Big objects reach farther. Lower helps busy areas such as Act III."},
    // Big objects (main's #366): the right half of Scenery distance's row, as main pairs them (PairOf). Off, then 300 - 1000 (farobjects::Normalize).
    {.id="render.big_radius",.page=View::Performance,.section="Draw distance",.kind=RowKind::Slider,.label="Big objects",
     .help="Trees, houses and walls stay drawn past Scenery distance: the bigger, the farther, out to this. Needs Terrain distance 3 or more. Costs frames in forests.",
     FRAMEVR_NAMES(OffAtZero),.beside=true,.besideLabel="Big:"},
    // Frames: Frame generation (main's #313, OFXR Bridge). Eyes drawn per frame is drawn above it
    // (.alsoOn): it runs with Both eyes only, so By turns grays these rows, and PageReason says why.
    // NVIDIA's quality and scale show while NVIDIA is chosen. Its status: the row's sub line and card
    // (hooks). Defer GPU wait is drawn as a button in the readout of both Graphics pages (Page()).
    {.id="render.frame_generation",.page=View::Frames,.section="Frame generation",.label="Frame generation",
     .help="Experimental (OFXR Bridge by tig3rmast3r): makes every other frame. Needs Both above; on: a moment of black. With Virtual Desktop, set its SSW to Disabled.",
     .warn="Experimental: on reloads VR",FRAMEVR_OPTIONS(FrameGenOptions)},
    {.id="render.frame_generation_preset",.page=View::Frames,.section="Frame generation",.label="NVIDIA flow quality",
     .help="How carefully NVIDIA's optical flow looks for the motion between two frames. Medium is the suggested start. Changes apply while you play.",
     FRAMEVR_OPTIONS(FrameGenPresetOptions)},
    {.id="render.frame_generation_scale",.page=View::Frames,.section="Frame generation",.label="NVIDIA flow scale",
     .help="The size NVIDIA's optical flow works at. 50% is the suggested start; 100% usually costs more than it gains. Changes apply while you play.",
     FRAMEVR_OPTIONS(FrameGenScaleOptions)},
    {.id="render.defer_gpu_wait",.page=View::Frames,.section="Frame generation",.kind=RowKind::Offer,.label="Defer GPU wait",
     .help="Starts the next frame while the graphics card finishes the last; without it, about half the frame rate. Once on, the menu cannot turn it off.",
     .warn=DeferWarning,.recommended=1},
    // Environment: Sky and dungeons
    {.id="sky.enabled",.page=View::Environment,.section="Sky and dungeons",.kind=RowKind::Toggle,.label="Outdoor sky",
     .help="Adds a sky above outdoor areas. It does not show more of the map."},
    {.id="fog.enabled",.page=View::Environment,.section="Sky and dungeons",.kind=RowKind::Toggle,.label="Distance fog",
     .help="Adds haze to distant outdoor scenery. Off makes far things sharper."},
    // Fog distance (main's #366): the right half of Distance fog's row, as main pairs them (PairOf);
    // shown while the fog is on (RowShown). A number beside a row opens the list of its steps.
    {.id="fog.end",.page=View::Environment,.section="Sky and dungeons",.kind=RowKind::Slider,.label="Fog distance",
     .help="Where the fog is complete; farther shows more of the distance. With Big objects on, set it past their distance to see them. The fog still starts near you.",
     .beside=true,.besideLabel="Ends at"},
    {.id="ceiling.enabled",.page=View::Environment,.section="Sky and dungeons",.kind=RowKind::Toggle,.label="Dungeon ceilings",
     .help="Covers the open black space above you in supported dungeons. Building roofs are the game's own."},
    // Dungeon torchlight (owner 2026-11-01): the game's own lights ([ceiling] torches), the right half of
    // Dungeon ceilings' row - they are gathered only under it (RowEnabled).
    {.id="ceiling.torches",.page=View::Environment,.section="Sky and dungeons",.kind=RowKind::Toggle,.label="Dungeon torchlight",
     .help="The game's own torches and braziers light the dungeon around them. Finding them scans game memory every 1.5 s: turn it off if dungeons stutter.",
     .beside=true,.besideLabel="Torches"},
    // Echo (world_sound.h; owner 2026-11-09): the game's own reverb per place, raised for the headset. With the
    // dungeons' rows: a third section would leave the page no room for the fallback's "New".
    {.id="audio.echo",.page=View::Environment,.section="Sky and dungeons",.kind=RowKind::Choice,.label="Echo",
     .help="How loud the game's own echo of the place is: caves, crypts and the Cathedral ring, the open moor barely. It follows every monster, footstep and voice.",
     FRAMEVR_OPTIONS(EchoOptions)},
    // Environment: Glow. Spell glow on hands left the page and the settings on 2026-11-04: always on for a
    // Sorceress (the owner; [interactions] elemental_hands is ignored).
    {.id="torch.flame",.page=View::Environment,.section="Glow",.kind=RowKind::Toggle,.label="Torch flame",
     .help="A one-handed club with fire damage is a torch that lights the dark around you. This shows or hides its flame; the light stays either way."},
    // Spell hand light (owner, 2026-11-06: "too bright ... it makes the surrounding area even darker looking in
    // contrast ... at least make it adjustable"): [torch] spell_hands_pct, 40% by default (it was full).
    {.id="torch.spell_hands_pct",.page=View::Environment,.section="Glow",.kind=RowKind::Slider,.label="Spell hand light",
     .help="How brightly the spells in a Sorceress's hands light the dark around you. Lower it if the light dazzles; Off and they only glow.",
     .format={.unit="%"},FRAMEVR_NAMES(OffAtZero),.classes=Sorceress},
    // Light Radius boost (#440: "if it did it could even be buffed a bit for the sake of VR"): [light]
    // radius_boost_pct, how much each Light Radius point widens the mod's dungeon light (light_radius.h).
    {.id="light.radius_boost_pct",.page=View::Environment,.section="Glow",.kind=RowKind::Slider,.label="Light Radius boost",
     .help="How much +Light Radius items and skills widen your light in dungeons. 100% is about the game's own amount; more is easier to see in VR.",
     .format={.unit="%"}},
    // Settings with an ini key but no row: set by Calibrate body, legacy, the
    // ini only on main ([render] desktop_one_eye), or the skill wheel's Left
    // claw strikes box ([gameplay] left_hand_strikes).
    {.id="calibration.eye_height_m",.page=NotInMenu},
    {.id="calibration.arm_span_m",.page=NotInMenu},
    {.id="interactions.wrist_map",.page=NotInMenu},
    {.id="interactions.wrist_compass",.page=NotInMenu},
    {.id="render.desktop_one_eye",.page=NotInMenu},
    {.id="gameplay.left_hand_strikes",.page=NotInMenu},
    // Dominant hand (owner, 2026-11-10: with Left-handed mirroring everything it "doesn't make any sense"): its
    // row is gone and the key is no longer read; the swap hand is the main hand (handrole::Dominant).
    {.id="interactions.dominant_hand",.page=NotInMenu},
    // Glyph casting: edited on the wheel (the button picker's "Draw glyph", the GLYPHS page).
    {.id="glyphs.draw_slot",.page=NotInMenu},{.id="glyphs.circle",.page=NotInMenu},{.id="glyphs.triangle",.page=NotInMenu},
    {.id="glyphs.square",.page=NotInMenu},{.id="glyphs.lightning",.page=NotInMenu},{.id="glyphs.spiral",.page=NotInMenu},
    {.id="glyphs.v",.page=NotInMenu},{.id="glyphs.star",.page=NotInMenu},{.id="glyphs.wave",.page=NotInMenu},
    {.id="glyphs.second_draw_slot",.page=NotInMenu},{.id="glyphs.left_circle",.page=NotInMenu},{.id="glyphs.left_triangle",.page=NotInMenu},
    {.id="glyphs.left_square",.page=NotInMenu},{.id="glyphs.left_lightning",.page=NotInMenu},{.id="glyphs.left_spiral",.page=NotInMenu},
    {.id="glyphs.left_v",.page=NotInMenu},{.id="glyphs.left_star",.page=NotInMenu},{.id="glyphs.left_wave",.page=NotInMenu},
    // Motions: edited on the wheel's MOTIONS page.
    {.id="motions.off",.page=NotInMenu},{.id="motions.paladin_off",.page=NotInMenu},{.id="motions.druid_off",.page=NotInMenu},
    {.id="motions.amazon_off",.page=NotInMenu},{.id="motions.necromancer_off",.page=NotInMenu},
    {.id="motions.barbarian_off",.page=NotInMenu},{.id="motions.assassin_off",.page=NotInMenu},{.id="motions.warlock_off",.page=NotInMenu},
};
#undef FRAMEVR_OPTIONS
#undef FRAMEVR_NAMES

// ---- presets (ini ids and units; only the settings listed change) ----
// Left out of the presets: StereoRendering (Comfort, Smooth frames), DeferGpuWait
// (Smooth frames), CompanionAi (Classic, Full VR), and the
// PR-only LootPick / MenuPointer.

inline constexpr PresetSetting ComfortValues[]={
    {"turn.smooth",0},{"turn.snap",30},
    {"gameplay.motion_controls",1},{"gameplay.melee_hit_speed",8},
    {"gameplay.projectile_aim_assist",2},{"gameplay.spell_lock",3},{"gameplay.ground_aim",2},{"gameplay.landing_circles",1},
    {"gameplay.target_outline_pct",50},{"gameplay.aim_highlights",1},
    {"hud.menu_distance",1},{"hud.tooltip_timing",0},{"hud.labels_size",60},
    {"hud.blood_visor",0},{"gameplay.low_life_heart_start",0},{"interactions.critical_alerts",1},
};
inline constexpr PresetSetting ClassicValues[]={
    {"gameplay.motion_controls",1},
    {"gameplay.projectile_aim_assist",0},{"gameplay.spell_lock",0},{"gameplay.planar_spell_aim",0},{"gameplay.ground_aim",0},{"gameplay.target_outline_pct",0},
    {"hud.tooltip_timing",2},{"hud.labels_size",45},{"hud.ordinary_mob_bars",1},
    {"gameplay.offline_players",0},{"gameplay.enemy_sight_pct",100},
    {"hud.blood_visor",0},{"gameplay.low_life_heart_start",0},
    {"sky.enabled",0},{"fog.enabled",0},{"ceiling.enabled",0},{"interactions.bottle_physics",0},{"interactions.prop_physics",0},
    {"interactions.map_top_down",0},
};
inline constexpr PresetSetting FullVrValues[]={
    {"gameplay.motion_controls",1},{"gameplay.skill_gestures",1},{"interactions.weapon_skill_indicators",1},
    {"turn.smooth",1},{"turn.speed",120},
    {"gameplay.spell_lock",3},{"gameplay.projectile_aim_assist",1},{"gameplay.staff_point",1},{"gameplay.ground_aim",1},{"gameplay.landing_circles",1},
    {"gameplay.target_outline_pct",60},{"gameplay.aim_highlights",1},{"bow.path_laser_hue",45},
    {"hud.blood_visor",1},{"hud.blood_wipe",1},{"gameplay.low_life_heart_start",60},{"interactions.critical_alerts",1},
    {"sky.enabled",1},{"fog.enabled",1},{"ceiling.enabled",1},{"interactions.bottle_physics",1},{"interactions.prop_physics",1},
    {"interactions.companion_counts",1},
};
inline constexpr PresetSetting SmoothFramesValues[]={
    {"render.vr_resolution",1},
    {"render.fixed_foveation",1},{"render.foveation_strength",2},
    {"render.rings",2},{"render.model_radius",210},
    {"sky.enabled",0},{"fog.enabled",0},
};
// Presets are off for now: the values above are proposals that no player has
// confirmed yet. While off, the Presets tab is drawn grayed out, opens nothing
// and no preset can be applied by any path (layout::Press); its help card says
// PresetsLater. Set PresetsReady to true to turn them back on: everything else
// (the data, the Presets view, the checks below) stays as it is.
inline constexpr bool PresetsReady=false;
inline constexpr const char* PresetsLater="Presets are coming in a later update. Until then, each page sets its own rows.";
inline constexpr PresetSpec PresetSpecs[]={
    {"comfort","Comfort",Icon::Comfort,"Gentle turns, strong aim help, no blood or heartbeat.",ComfortValues,uint8_t(std::size(ComfortValues))},
    {"classic","Classic",Icon::Classic,"The game's own aim and looks, with only what VR needs.",ClassicValues,uint8_t(std::size(ClassicValues))},
    {"fullvr","Full VR",Icon::FullVr,"Everything physical and visible: the mod as it was meant to feel.",FullVrValues,uint8_t(std::size(FullVrValues))},
    {"fast","Smooth frames",Icon::Fast,"Less to draw, for older PCs: headset resolution, coarser edges, short distances, no sky or fog.",SmoothFramesValues,uint8_t(std::size(SmoothFramesValues))},
};

// ---- tools: footer, confirm row, card buttons ----

inline constexpr ContentTool ToolTable[]={
    {Kind::Apply,"Apply","Saves and uses your changes. Until you press it, changes are only a draft. Your other settings and calibration stay as they are."},
    {Kind::Close,"Close","Back to the game. If some changes are not applied, Close asks first."},
    {Kind::Recenter,"Set eye height","Sets your eye height for where you stand or sit now. Your body measurements stay saved."},
    {Kind::Calibrate,"Calibrate body","Measures your standing eye height and arm span in a T cross. Apply or discard changes first. Hold both triggers when asked; Right B cancels."},
    {Kind::ResetPage,"Reset page","Sets every row you can see on this page back to its default. Nothing is saved until you press Apply."},
    {Kind::UndoReset,"Undo reset","Puts back the values this page had before you pressed Reset page. It goes away after 6 seconds."},
    // The confirm row; the layout may print the count ("Discard 3 changes").
    {Kind::ConfirmClose,"Discard changes","Closes the menu and puts every changed row back to its applied value."},
    {Kind::KeepEditing,"Keep editing","Back to the page. Your changes stay as a draft."},
    {Kind::ApplyClose,"Apply and close","Saves your changes, then closes once they are saved."},
    // The help card's buttons.
    {Kind::Undo,"Undo","Puts this row back to its applied value."},
    {Kind::Default,"Default","Puts this row back to its default."},
    {Kind::TestHaptics,"Feel it","Vibrates at the strength shown here, before you apply."},
    {Kind::Open,"Open","Goes to the row or page this one needs."},
    {Kind::Preset,"Use preset","Sets only the rows this preset lists. Nothing is saved until you press Apply."},
};

// ---- the Controller guide (main's Controls page, vr_settings_ui.cpp) ----

inline constexpr GuideLine GuideTable[]={
    {"Stick","Walk",GuideColumn::Left},
    {"{L1} or {L2}","Your left-hand skills",GuideColumn::Left},
    {"{L3} or {L4}","More left-hand skills",GuideColumn::Left},
    {"Trigger","Left-hand skill",GuideColumn::Left},
    {"Stick click","Open or close the left wheel",GuideColumn::Left},
    {"Grip + stick click","Inventory, character and skills",GuideColumn::Left},
    {"Grip near the belt","Grab a potion",GuideColumn::Left},
    {"Grip + right trigger","Right-click in game menus",GuideColumn::Left},
    {"Stick left or right","Turn",GuideColumn::Right},
    {"Stick click","Open or close the right wheel",GuideColumn::Right},
    {"Trigger","Right-hand skill, or grab a bow string",GuideColumn::Right},
    {"Stick up","Run or walk",GuideColumn::Right},
    {"Hold stick down","Show all loot names",GuideColumn::Right},
    {"{R1} or {R2}","Interact, or your skills",GuideColumn::Right},
    {"{R3} or {R4}","More right-hand skills",GuideColumn::Right},
    {"Main-hand grip at a shoulder","Swap weapon sets",GuideColumn::Right},
    {"Trigger with bow / javelin","Draw, release, throw",GuideColumn::Right},
    {"Left Trigger","Ctrl in game menus",GuideColumn::Panels},
    {"Right Grip","Shift; push or pull a menu",GuideColumn::Panels},
    {"Right {R2}","Back",GuideColumn::Panels},
};
// What's new: the existing release_notes.h (releasenotes::), unchanged.

// ---- status lines (main's words, vr_settings_ui.cpp) ----

// Under Offline difficulty: the card leads with the note; without the
// offline scaling adapter, main's notice instead (row and card), and the row
// stays editable as on main.
inline constexpr const char* OfflineNote="Only new monsters change. Save and Exit, then rejoin with the same character to refresh them all.";
inline constexpr const char* OfflineUnavailable="Offline scaling adapter unavailable. Game difficulty remains unchanged.";
inline constexpr const char* OfflineUnavailableShort="Offline scaling adapter unavailable";
// Under Button names: the controllers the headset reports.
inline std::string DetectedText(const controllers::Labels& detected){
    return std::string("Detected: left ")+controllers::Name(detected.hands[1])+" / right "+controllers::Name(detected.hands[0]);
}

// ---- color names: one per 15-degree hue step (the swatch rows and the grid) ----

inline constexpr const char* HueNames[24]={
    "Red","Vermilion","Orange","Gold","Yellow","Lime","Chartreuse","Leaf","Green","Jade","Spring","Mint",
    "Cyan","Sky","Azure","Cobalt","Blue","Indigo","Violet","Purple","Magenta","Pink","Rose","Crimson"};
inline const char* HueNameOf(float hue){
    const int step=int(std::lround(hue/15.f));
    return HueNames[((step%24)+24)%24];
}

// ---- ini ids ----
namespace check {
constexpr size_t Length(const char* s){size_t n=0;while(s&&s[n])++n;return n;}
constexpr bool SameText(const char* a,const char* b){
    if(!a||!b)return a==b;
    size_t i=0;while(a[i]&&a[i]==b[i])++i;
    return a[i]==b[i];
}
}
// Whether an ini id ("section.key") names this Definitions entry.
constexpr bool IdIs(const char* id,const Definition& d){
    if(!id)return false;
    size_t i=0;for(;d.section[i];++i)if(id[i]!=char(d.section[i]))return false;
    if(id[i]!='.')return false;
    const char* key=id+i+1;size_t j=0;
    for(;d.key[j];++j)if(key[j]!=char(d.key[j]))return false;
    return key[j]==0;
}
// The Key an ini id names, or -1 (removed or renamed upstream, or a typo).
constexpr int KeyOfId(const char* id){
    for(int k=0;k<Count;++k)if(IdIs(id,Definitions[k]))return k;
    return -1;
}
inline std::string IdOf(Key key){
    std::string id;const auto& d=Definitions[key];
    for(const wchar_t* p=d.section;*p;++p)id+=char(*p);
    id+='.';
    for(const wchar_t* p=d.key;*p;++p)id+=char(*p);
    return id;
}

// ---- the fallback: where and how a setting with no override shows ----
inline constexpr int MaxPageRows=8,MaxPageSections=3,MaxOptions=24;
inline constexpr const char* NewSection="New";   // the fallback rows' section, at the bottom of a page
// The owner page of an ini section (docs/VR_SETTINGS_MENU.md); any other: More.
inline View OwnerOf(const wchar_t* section){
    const std::wstring_view s=section;
    if(s==L"turn")return View::Moving;
    if(s==L"calibration")return View::Body;
    if(s==L"bow")return View::Targeting;
    if(s==L"hud")return View::Menus;
    if(s==L"sky"||s==L"fog"||s==L"ceiling"||s==L"torch"||s==L"audio")return View::Environment;
    if(s==L"haptics")return View::Vibration;
    if(s==L"render"||s==L"stereo")return View::Performance;
    return View::More;   // gameplay, interactions and any new section
}
// Whether an override gave a number format of its own.
constexpr bool FormatSet(const Format& f){return (f.unit&&*f.unit)||f.iniScale!=1||f.decimals||f.sign||f.hueNames;}
// The format the ini key's unit suffix asks for (_pct %, _cm cm, _deg °,
// _m m, _ms ms, _hue a color), decimals from the step, "+" on signed ranges.
inline Format SuffixFormat(const Definition& d){
    Format f;
    const std::wstring_view key=d.key;
    auto ends=[&](std::wstring_view suffix){return key.size()>suffix.size()&&key.substr(key.size()-suffix.size())==suffix;};
    if(ends(L"_pct"))f.unit="%";
    else if(ends(L"_cm"))f.unit="cm";
    else if(ends(L"_deg"))f.unit=Degrees;
    else if(ends(L"_ms"))f.unit="ms";
    else if(ends(L"_m"))f.unit="m";
    else if(ends(L"_hue"))f.hueNames=true;
    for(int decimals=0;decimals<=3;++decimals){
        const double scaled=double(d.step)*std::pow(10.,decimals);
        if(std::fabs(scaled-std::round(scaled))<1e-4){f.decimals=uint8_t(decimals);break;}
        f.decimals=3;
    }
    f.sign=d.min<0&&!f.hueNames;
    return f;
}
// ---- the merge: Definitions + Overrides -> the menu's rows ----
// Built once (Menu()); a test builds its own from made-up tables (BuildMenu).
struct MenuData {
    std::vector<ContentRow> rows;          // page by page, section by section (layout::RowsOf keeps this order)
    std::vector<ContentSection> sections;  // SectionTable
    std::vector<ContentPreset> presets;
    std::vector<std::string> report;       // non-fatal: work to do and upstream drift (vr-settings-test --tidy)
    // Storage the rows and presets point into (never moved once built).
    std::deque<std::vector<Option>> optionLists;
    std::deque<std::vector<ValueName>> nameLists;
    std::deque<std::vector<PresetValue>> presetLists;
    std::deque<std::string> words;
};
// The kind the data asks for when an override names none (or one that no
// longer fits the setting): named choices are a list, 0 / 1 a switch, a
// _hue key a color, other numbers a slider (a stepper with no upper limit).
inline RowKind DerivedKind(const Definition& d,const Override& o){
    if(o.options&&o.optionCount)return RowKind::Choice;
    if(d.min==0&&d.max==1&&d.step==1)return RowKind::Toggle;
    if(o.format.hueNames)return RowKind::Swatch;
    if(!(d.max<std::numeric_limits<float>::max()))return RowKind::Stepper;
    return RowKind::Slider;
}
// Whether an authored kind still fits its setting.
inline bool KindFits(RowKind k,const Definition& d,const Override& o){
    const bool open=!(d.max<std::numeric_limits<float>::max());
    switch(k){
        case RowKind::Toggle:case RowKind::Offer:case RowKind::Button:return d.min==0&&d.max==1&&!o.optionCount;
        case RowKind::Filter:return !open&&!o.optionCount&&d.max>d.min;   // its own number is a slider
        case RowKind::Choice:case RowKind::Cycler:return o.options&&o.optionCount&&d.step==1&&!open;
        case RowKind::Slider:return !open&&!o.optionCount;
        case RowKind::Stepper:return !o.optionCount;
        case RowKind::Swatch:return o.format.hueNames&&!o.optionCount;
        default:return false;
    }
}
inline MenuData BuildMenu(const Override* overrides,size_t overrideCount,const PresetSpec* presetSpecs,size_t presetCount){
    MenuData m;
    m.sections.assign(std::begin(SectionTable),std::end(SectionTable));
    auto note=[&](std::string line){m.report.push_back(std::move(line));};
    // The sections of a page that hold a row (an empty one shows nothing).
    auto sectionCount=[&](View page){int n=0,index=0;for(const auto& s:m.sections)if(s.page==page){for(const auto& r:m.rows)if(r.page==page&&r.section==index){++n;break;}++index;}return n;};
    auto sectionIndex=[&](View page,const char* title)->int{
        int n=0;
        for(const auto& s:m.sections)if(s.page==page){if(!title||check::SameText(s.title,title))return n;++n;}
        return -1;
    };
    // One row: the setting's Definitions entry, our words where we have them.
    auto make=[&](const Override& o,Key k,View page,int section,bool fallback)->ContentRow{
        const Definition& d=Definitions[size_t(k)];
        const std::string id=IdOf(k);
        m.words.push_back(id);
        ContentRow r{};
        r.key=k;r.page=page;r.id=m.words.back().c_str();r.fallback=fallback;
        r.section=uint8_t(std::max(0,section));
        r.format=FormatSet(o.format)?o.format:SuffixFormat(d);
        Override shown=o;shown.format=r.format;
        r.kind=o.kind==RowKind::Auto?DerivedKind(d,shown):o.kind;
        if(!KindFits(r.kind,d,shown)){note(id+": its row kind no longer fits the setting; the data's own control");r.kind=DerivedKind(d,shown);}
        r.label=o.label?o.label:d.label;r.warn=o.warn;
        r.help=o.help?o.help:HelpText(k)[0]?HelpText(k):d.detail;   // no words of ours: upstream's long help, else its detail
        r.classes=o.classes;r.wraps=o.wraps;r.warnWhen=o.warnWhen;r.beside=o.beside&&(r.kind==RowKind::Toggle||r.kind==RowKind::Slider||r.kind==RowKind::Stepper);r.besideLabel=o.besideLabel;
        r.inFilter=o.inFilter&&r.kind==RowKind::Toggle;r.alsoOn=o.alsoOn;
        r.recommended=o.recommended==o.recommended&&o.recommended>=d.min&&o.recommended<=d.max?o.recommended:std::numeric_limits<float>::quiet_NaN();
        // Value words in range only.
        if(o.nameCount){
            bool all=true;for(int n=0;n<o.nameCount;++n)if(o.names[n].value<d.min||o.names[n].value>d.max)all=false;
            if(all){r.names=o.names;r.nameCount=o.nameCount;}
            else{
                auto& list=m.nameLists.emplace_back();
                for(int n=0;n<o.nameCount;++n)if(o.names[n].value>=d.min&&o.names[n].value<=d.max)list.push_back(o.names[n]);
                r.names=list.data();r.nameCount=uint8_t(list.size());
                note(id+": value words past the range dropped");
            }
        }
        // Named choices: one option per ini value, min to max. Missing names
        // read "Choice n"; names past the range are dropped. More than
        // MaxOptions is no list: the number control, and the report says split it.
        if(r.kind==RowKind::Choice||r.kind==RowKind::Cycler){
            const int values=int(std::lround(d.max-d.min))+1;
            if(values>MaxOptions){
                note(id+": "+std::to_string(values)+" named choices are too many for one list (at most 24): split the setting");
                r.kind=d.max<std::numeric_limits<float>::max()?RowKind::Slider:RowKind::Stepper;
                return r;
            }
            bool exact=o.optionCount==values;
            for(int n=0;exact&&n<o.optionCount;++n)if(o.options[n].value!=d.min+float(n))exact=false;
            if(exact){r.options=o.options;r.optionCount=o.optionCount;}
            else{
                auto& list=m.optionLists.emplace_back();
                for(int v=0;v<values;++v){
                    const float value=d.min+float(v);const Option* named=nullptr;
                    for(int n=0;n<o.optionCount;++n)if(o.options[n].value==value)named=&o.options[n];
                    if(named)list.push_back(*named);
                    else{m.words.push_back("Choice "+std::to_string(v+1));list.push_back({value,m.words.back().c_str(),"No words for this choice yet."});}
                }
                r.options=list.data();r.optionCount=uint8_t(list.size());
                note(id+": "+std::to_string(o.optionCount)+" names, "+std::to_string(values)+" values");
            }
        }
        return r;
    };
    std::vector<int> placed(size_t(Count),0);              // 1: a row, 2: NotInMenu
    std::vector<const Override*> words(size_t(Count),nullptr);   // every key's override, placed or not
    for(size_t i=0;i<overrideCount;++i){
        const Override& o=overrides[i];
        const int k=KeyOfId(o.id);
        if(k<0){note(std::string("orphan override: ")+(o.id?o.id:"(no id)")+" (no such setting; removed upstream or a typo)");continue;}
        if(words[size_t(k)]){note(std::string("second override ignored: ")+o.id);continue;}
        words[size_t(k)]=&o;
        if(o.page==NotInMenu){placed[size_t(k)]=2;continue;}
        if(o.page==OwnerPage)continue;   // words without a page: the fallback places them
        const int s=sectionIndex(o.page,o.section);
        if(s<0)note(std::string(o.id)+": no section \""+(o.section?o.section:"")+"\" on its page; the first one");
        m.rows.push_back(make(o,Key(k),o.page,s,false));
        placed[size_t(k)]=1;
    }
    // The fallback: a setting no override placed goes to its owner page's
    // "New" section, at the bottom, or to More when that page is full (8 rows,
    // or 3 sections and no "New" yet) or has no owner.
    int more=0;
    for(int k=0;k<Count;++k){
        if(placed[size_t(k)])continue;
        View page=OwnerOf(Definitions[size_t(k)].section);
        int rows=0;for(const auto& r:m.rows)if(OnPage(r,page)&&!r.beside&&!r.inFilter)++rows;   // a side half or a filter switch takes no row
        if(page!=View::More&&(rows>=MaxPageRows||(sectionIndex(page,NewSection)<0&&sectionCount(page)>=MaxPageSections)))page=View::More;
        if(sectionIndex(page,NewSection)<0)m.sections.push_back({page,NewSection});
        const Override none{.id=nullptr};
        m.rows.push_back(make(words[size_t(k)]?*words[size_t(k)]:none,Key(k),page,sectionIndex(page,NewSection),true));
        if(page==View::More)++more;
        note((words[size_t(k)]&&words[size_t(k)]->label?"no page: ":"no words yet: ")+IdOf(Key(k))+" (\""+m.rows.back().label+"\") on "+
             PageTable[size_t(page)].name+(page==View::More?"":" > New"));
    }
    if(more)note("the More page is not empty: "+std::to_string(more)+" setting"+(more==1?"":"s")+" to sort");
    // Presets: ids resolved; a missing setting is skipped and reported.
    for(size_t p=0;p<presetCount;++p){
        const PresetSpec& spec=presetSpecs[p];
        auto& values=m.presetLists.emplace_back();
        for(int i=0;i<spec.count;++i){
            const int k=KeyOfId(spec.values[i].id);
            if(k<0){note(std::string("orphan preset entry: ")+spec.id+" "+spec.values[i].id);continue;}
            const Definition& d=Definitions[size_t(k)];const float v=spec.values[i].value;
            if(v<d.min||v>d.max){note(std::string("preset value out of range: ")+spec.id+" "+spec.values[i].id);continue;}
            values.push_back({Key(k),v});
        }
        m.presets.push_back({spec.id,spec.name,spec.icon,spec.blurb,values.data(),uint8_t(values.size())});
    }
    return m;
}
// Left-handed play (hand_text.h): a menu's words mirrored, once. Every row but the one about the physical
// hands themselves (Left-handed): its label, help, warnings,
// the word beside it, its options and their hints, its value names; the presets' names and blurbs.
constexpr bool PhysicalSides(Key key){return key==LeftHanded;}
inline MenuData MirroredMenu(const MenuData& written){
    MenuData menu=written;
    for(auto& r:menu.rows){
        if(PhysicalSides(r.key))continue;
        r.label=handtext::M(r.label);r.help=handtext::M(r.help);r.warn=handtext::M(r.warn);
        r.warnWhen.text=handtext::M(r.warnWhen.text);r.besideLabel=handtext::M(r.besideLabel);
        if(r.options&&r.optionCount){
            auto& list=menu.optionLists.emplace_back(r.options,r.options+r.optionCount);
            for(auto& o:list){o.label=handtext::M(o.label);o.hint=handtext::M(o.hint);}
            r.options=list.data();
        }
        if(r.names&&r.nameCount){
            auto& list=menu.nameLists.emplace_back(r.names,r.names+r.nameCount);
            for(auto& n:list)n.label=handtext::M(n.label);
            r.names=list.data();
        }
    }
    for(auto& s:menu.sections)s.title=handtext::M(s.title);
    for(auto& p:menu.presets){p.name=handtext::M(p.name);p.blurb=handtext::M(p.blurb);}
    return menu;
}
// The menu as written, for a right-handed player, and as shown now.
inline const MenuData& WrittenMenu(){
    static const MenuData menu=BuildMenu(Overrides,std::size(Overrides),PresetSpecs,std::size(PresetSpecs));
    return menu;
}
inline const MenuData& Menu(){
    if(!handtext::Left())return WrittenMenu();
    static const MenuData menu=MirroredMenu(WrittenMenu());
    return menu;
}
// Preview and test data only: the real overrides less a few, as if upstream
// had added those settings since the menu was sorted. Torch flame and Terrain
// distance land in their pages' New sections; ten gameplay and interactions
// settings fill More past one page (Map view without its names: a number).
inline const MenuData& FallbackDemo(){
    static const MenuData menu=[]{
        constexpr const char* unsorted[]={"torch.flame","render.rings","render.frame_generation","gameplay.shield_grip_cm","gameplay.shield_out_cm","gameplay.shield_up_cm",
            "gameplay.shield_follow_pct","gameplay.shield_roll_deg","gameplay.companion_ai","gameplay.enemy_sight_pct","gameplay.offline_players",
            "gameplay.low_life_heart_start","interactions.map_top_down"};
        std::vector<Override> list;
        for(const auto& o:Overrides){
            bool drop=false;for(const char* id:unsorted)if(check::SameText(o.id,id))drop=true;
            if(!drop)list.push_back(o);
        }
        return BuildMenu(list.data(),list.size(),PresetSpecs,std::size(PresetSpecs));
    }();
    return menu;
}

// ---- lookups (trivial over the tables; the checks below and the tests check the tables) ----

inline const ContentPage& PageOf(View v){
    if(!handtext::Left())return PageTable[size_t(v)];
    static const std::vector<ContentPage> mirrored=[]{
        std::vector<ContentPage> pages(std::begin(PageTable),std::end(PageTable));
        for(auto& p:pages){p.name=handtext::M(p.name);p.intro=handtext::M(p.intro);}
        return pages;
    }();
    return mirrored[size_t(v)];
}
// The row of a key, or nullptr (NotInMenu).
inline const ContentRow* RowOf(Key key){
    for(const auto& r:Menu().rows)if(r.key==key)return &r;
    return nullptr;
}
// Whether a row is for this character: a class bit or the weapon in hand.
// Unknown class (-1): every row.
inline bool ForCharacter(const ContentRow& r,const Context& c){
    if(r.classes==AllClasses||c.heroClass<0)return true;
    if(c.heroClass<8&&(r.classes&(1u<<c.heroClass)))return true;
    return (c.bow&&(r.classes&WithBow))||(c.staff&&(r.classes&WithStaff))||(c.thrown&&(r.classes&WithThrown));
}
// The row's label for this character: Skills go to the aimed monster reads
// "Spells go to the aimed monster" for a class without bow skills (RowLabel).
inline const char* LabelFor(const ContentRow& r,const Context& c={}){
    return r.key==SpellLock&&!BowSkillClass(c)?"Spells go to the aimed monster":r.label;
}
// Whether this character sees the spells-only Off / On of Skills go to the aimed monster.
inline bool SpellsOnly(const ContentRow& r,const Context& c){return r.key==SpellLock&&!BowSkillClass(c);}
// The options this character steps through (count in `count`).
inline const Option* OptionsFor(const ContentRow& r,const Context& c,int& count){
    if(SpellsOnly(r,c)){count=int(std::size(SpellLockSpellsOnly));return SpellLockSpellsOnly;}
    count=r.optionCount;return r.options;
}
// The option a value selects, or nullptr.
inline const Option* OptionOf(const ContentRow& r,float value){
    for(int i=0;i<r.optionCount;++i)if(std::fabs(r.options[i].value-value)<.001f)return &r.options[i];
    return nullptr;
}
// The same for this character: the spells-only list matches by the spell bit.
inline const Option* OptionOf(const ContentRow& r,float value,const Context& c){
    if(SpellsOnly(r,c))return &SpellLockSpellsOnly[LockSpells(value)?1:0];
    return OptionOf(r,value);
}
// The value as the row shows it ("65 cm", "Off", "Strong", "+10°", "Azure", "3 players").
inline std::string ValueText(const ContentRow& r,float value,const Context& c={}){
    if(!std::isfinite(value))return "";
    if(const Option* o=OptionOf(r,value,c))return o->label;
    for(int i=0;i<r.nameCount;++i)if(std::fabs(r.names[i].value-value)<.001f)return r.names[i].label;
    if(r.key==BowLaser&&value<0)return "Off";   // any negative hue is Off (Normalize)
    if(r.kind==RowKind::Toggle||r.kind==RowKind::Offer)return value>.5f?"On":"Off";
    if(r.format.hueNames)return HueNameOf(value);
    const float scale=r.format.iniScale>0?r.format.iniScale:1.f;
    const double shown=double(value)/double(scale)+0.;
    char number[48];
    std::snprintf(number,sizeof number,"%.*f",int(r.format.decimals),shown);
    std::string text=number;
    if(text.find('.')!=std::string::npos){   // no needless decimals: 12.50 -> 12.5, 12.00 -> 12
        while(!text.empty()&&text.back()=='0')text.pop_back();
        if(!text.empty()&&text.back()=='.')text.pop_back();
    }
    if(text=="-0")text="0";
    if(r.format.sign&&text!="0"&&text[0]!='-')text="+"+text;
    const std::string unit=r.format.unit?r.format.unit:"";
    if(unit.empty())return text;
    if(unit[0]=='%'||unit[0]=='\xc2')return text+unit;   // %, °, °/s: no space
    bool word=true;
    for(char ch:unit)if(ch<'a'||ch>'z'){word=false;break;}
    if(word&&unit.size()>1&&unit.back()=='s'&&(text=="1"||text=="+1"||text=="-1"))return text+" "+unit.substr(0,unit.size()-1);
    return text+" "+unit;
}
// ToolTable as shown now (std::size(ToolTable) entries): its words mirrored while the hands are.
inline const ContentTool* Tools(){
    if(!handtext::Left())return ToolTable;
    static const std::vector<ContentTool> mirrored=[]{
        std::vector<ContentTool> tools(std::begin(ToolTable),std::end(ToolTable));
        for(auto& t:tools){t.label=handtext::M(t.label);t.help=handtext::M(t.help);}
        return tools;
    }();
    return mirrored.data();
}
inline const ContentTool* ToolOf(Kind kind){
    const auto* tools=Tools();
    for(size_t i=0;i<std::size(ToolTable);++i)if(tools[i].kind==kind)return &tools[i];
    return nullptr;
}
// A guide line's control with this player's button names.
inline std::string GuideControl(const GuideLine& g,const controllers::Labels& labels){
    std::string out;
    // left-handed: the words first ("Right {R2}" is the left controller's then), then that hand's buttons
    for(const char* p=handtext::T(g.control);*p;){
        if(p[0]=='{'&&(p[1]=='L'||p[1]=='R')&&p[2]>='1'&&p[2]<='4'&&p[3]=='}'){
            out+=labels.Button(p[1]=='L'?1:0,p[2]-'0');p+=4;
        }else out+=*p++;
    }
    return out;
}

// ---- compile-time checks of our own words and placement ----
// Only what this file controls can stop the build: text lengths, one override
// per ini id, known pages and sections, page order and size, presets' excluded
// settings. Upstream's ranges and keys never do: an override that no longer
// fits its setting, a removed setting or a preset entry for it is skipped at
// run time and listed by the report (MenuData::report, vr-settings-test --tidy).
namespace check {
constexpr bool IsSettingsPage(View v){return size_t(v)<std::size(PageTable)&&PageTable[size_t(v)].settings;}
constexpr int SectionCount(View v){int n=0;for(const auto& s:SectionTable)if(s.page==v)++n;return n;}
constexpr int SectionOf(View v,const char* title){
    int n=0;for(const auto& s:SectionTable)if(s.page==v){if(!title||SameText(s.title,title))return n;++n;}
    return -1;
}
// Lengths: labels 26 (the one long main label 30), help and hints 160, warn 56, option names 14.
constexpr bool TextsFit(){
    for(const auto& r:Overrides){
        if(r.label&&(!Length(r.label)||Length(r.label)>(SameText(r.id,"gameplay.spell_lock")?30u:26u)))return false;
        if(r.help&&(!Length(r.help)||Length(r.help)>160))return false;
        if(r.warn&&Length(r.warn)>56)return false;
        if(r.warnWhen.text&&Length(r.warnWhen.text)>56)return false;
        for(int i=0;i<r.optionCount;++i)
            if(!Length(r.options[i].label)||Length(r.options[i].label)>14||!Length(r.options[i].hint)||Length(r.options[i].hint)>160)return false;
        for(int i=0;i<r.nameCount;++i)if(!Length(r.names[i].label)||Length(r.names[i].label)>14)return false;
        if(r.inFilter&&r.label&&Length(r.label)>14)return false;   // a card switch's word: an option name's length
    }
    for(const auto& p:PageTable)if(!Length(p.name)||Length(p.intro)>160)return false;
    for(const auto& s:SectionTable)if(!Length(s.title)||Length(s.title)>26)return false;
    for(const auto& t:ToolTable)if(!Length(t.label)||Length(t.label)>20||!Length(t.help)||Length(t.help)>160)return false;
    for(const auto& p:PresetSpecs)if(!Length(p.name)||Length(p.name)>14||!Length(p.blurb)||Length(p.blurb)>100)return false;
    for(const auto& o:SpellLockSpellsOnly)if(Length(o.label)>14||Length(o.hint)>160)return false;
    for(const auto& g:GuideTable)if(!Length(g.control)||Length(g.control)>30||!Length(g.action)||Length(g.action)>40)return false;
    return true;
}
// One override per ini id.
constexpr bool IdsUnique(){
    for(size_t i=0;i<std::size(Overrides);++i){
        if(!Length(Overrides[i].id))return false;
        for(size_t j=0;j<i;++j)if(SameText(Overrides[i].id,Overrides[j].id))return false;
    }
    return true;
}
// A placed override names a settings page and a section title of that page;
// options and value words keep their own order; the scale is positive.
constexpr bool PlacesKnown(){
    for(const auto& r:Overrides){
        if(r.format.iniScale<=0)return false;
        if(r.page==NotInMenu||r.page==OwnerPage)continue;
        if(!IsSettingsPage(r.page)||SectionOf(r.page,r.section)<0)return false;
        for(int i=1;i<r.optionCount;++i)if(!(r.options[i-1].value<r.options[i].value))return false;
    }
    return true;
}
// Overrides run page by page (View order) and section by section; at most 8
// placed rows and 3 sections a page.
constexpr bool OrderOk(){
    View page=View(0);int section=0;
    for(const auto& r:Overrides){
        if(r.page==NotInMenu||r.page==OwnerPage)continue;
        const int s=SectionOf(r.page,r.section);
        if(r.page<page||(r.page==page&&s<section))return false;
        page=r.page;section=s;
    }
    // An instead row follows a row of its page and section (vr-settings-test checks RowShown never shows both).
    for(size_t i=0;i<std::size(Overrides);++i)if(Overrides[i].instead&&(!i||Overrides[i-1].page!=Overrides[i].page||!SameText(Overrides[i-1].section,Overrides[i].section)))return false;
    // A beside toggle or number follows a placed row of its page and section, and shares that row.
    for(size_t i=0;i<std::size(Overrides);++i)if(Overrides[i].beside){
        const auto& r=Overrides[i];
        if(!i||(r.kind!=RowKind::Toggle&&r.kind!=RowKind::Slider&&r.kind!=RowKind::Stepper)||Overrides[i-1].beside||Overrides[i-1].inFilter||Overrides[i-1].kind==RowKind::Filter||
           Overrides[i-1].page!=r.page||!SameText(Overrides[i-1].section,r.section))return false;
    }
    // A filter switch is a toggle after its Filter row (or the switch before it) on that page and section;
    // a Filter row has 1 to 6 of them (its card); neither is beside nor instead.
    for(size_t i=0;i<std::size(Overrides);++i){
        const auto& r=Overrides[i];
        if(r.inFilter&&(!i||r.kind!=RowKind::Toggle||r.beside||r.instead||(Overrides[i-1].kind!=RowKind::Filter&&!Overrides[i-1].inFilter)||
                        Overrides[i-1].page!=r.page||!SameText(Overrides[i-1].section,r.section)))return false;
        if(r.kind==RowKind::Filter){
            if(r.beside||r.inFilter||r.instead)return false;
            int n=0;while(i+1+size_t(n)<std::size(Overrides)&&Overrides[i+1+size_t(n)].inFilter)++n;
            if(n<1||n>6)return false;
        }
    }
    // A row on a second page is a whole row, and its own page comes first: table order then draws it at
    // the top of the second page (RowsOf).
    for(const auto& r:Overrides)if(r.alsoOn!=View::Count&&(r.alsoOn<=r.page||r.beside||r.instead||r.inFilter||r.kind==RowKind::Offer||!IsSettingsPage(r.alsoOn)))return false;
    for(const auto& p:PageTable){
        int rows=0;for(const auto& r:Overrides)if((r.page==p.id||r.alsoOn==p.id)&&!r.beside&&!r.instead&&!r.inFilter)++rows;   // the slabs that can show at once
        if(rows>MaxPageRows||SectionCount(p.id)>MaxPageSections)return false;
    }
    for(size_t i=1;i<std::size(SectionTable);++i)if(SectionTable[i-1].page>SectionTable[i].page)return false;
    return true;
}
// Presets: each id once, never the owner's excluded settings.
constexpr bool PresetsOk(){
    constexpr const char* excluded[]={"stereo.rendering","render.defer_gpu_wait","gameplay.companion_ai",
                                      "calibration.eye_height_m","calibration.arm_span_m","calibration.version"};
    for(const auto& p:PresetSpecs)for(int i=0;i<p.count;++i){
        for(const char* x:excluded)if(SameText(p.values[i].id,x))return false;
        for(int j=0;j<i;++j)if(SameText(p.values[i].id,p.values[j].id))return false;
    }
    return true;
}
// The rail: each group once, in the order of its pages, which follow one
// another in View order (so its pages hang under it in rail order); every page
// but Presets and the band's in a group with 1 or more settings pages; the
// groups fit MaxRailGroups and the rows fit the rail. A name's width needs the font: vr-settings-fit-test.
constexpr bool GroupsOk(){
    if(size_t(Group::Header)>=size_t(MaxRailGroups))return false;   // Model::groupPage and the help ids hold every group
    size_t most=0;int last=-1;
    for(size_t i=0;i<std::size(GroupTable);++i){
        const auto& g=GroupTable[i];
        if(g.id==Group::Tools||g.id==Group::Header||!Length(g.name))return false;   // its width: vr-settings-fit-test
        for(size_t j=0;j<i;++j)if(GroupTable[j].id==g.id)return false;
        int first=-1,end=-1;size_t pages=0;
        for(const auto& p:PageTable)if(p.group==g.id){
            if(!p.settings||(end>=0&&int(p.id)!=end))return false;   // settings pages, one run
            if(first<0)first=int(p.id);
            end=int(p.id)+1;++pages;
        }
        if(!pages||pages>size_t(MaxGroupPages)||first<last)return false;
        last=first;most=std::max(most,pages);
    }
    for(const auto& p:PageTable)if(p.group!=Group::Header&&p.group!=Group::Tools&&!GroupOf(p.group))return false;
    for(const auto& p:PageTable)if(p.group==Group::Tools&&p.id!=View::Presets)return false;
    return 1+std::size(GroupTable)+most<=size_t(MaxRailRows);
}
// Every settings page holds at least 3 settings, a side half or a filter switch included (More is only a safety net).
constexpr bool PagesFilled(){
    for(const auto& p:PageTable)if(p.settings&&p.id!=View::More){
        int n=0;for(const auto& r:Overrides)if(r.page==p.id)++n;
        if(n<3)return false;
    }
    return true;
}
static_assert(TextsFit(),"labels <= 26 (30), help and hints <= 160, warn <= 56, option names <= 14");
static_assert(GroupsOk(),"rail groups: each once, its pages in one run of settings pages in rail order, at most 6 pages, Presets + groups + the largest group <= 12 rows");
static_assert(PagesFilled(),"every settings page but More holds at least 3 settings");
static_assert(IdsUnique(),"one override per ini id");
static_assert(PlacesKnown(),"a placed override names a settings page and one of its section titles");
static_assert(OrderOk(),"overrides in page and section order; at most 8 rows and 3 sections a page; filter switches follow their Filter row");
static_assert(PresetsOk(),"presets list each setting once, never the owner's excluded settings");
}
}
