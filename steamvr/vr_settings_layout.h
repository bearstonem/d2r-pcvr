#pragma once
// VR Settings layout: one element list per frame that the painter draws, the
// pointer hits and the help card explains, so paint, hit test and help can
// never disagree (the old menu kept three copies of every rectangle).
//
// With vr_settings.h and vr_settings_help.h. Build reads only the
// model, the content tables and the frame's input; it draws nothing and has no
// Windows dependency, so the tests run it for every view, class and state.
//
// settings::Kind and settings::Hit are the click vocabulary: Press below
// handles Open, Set, Undo, Default, Preset, ResetPage, UndoReset,
// ConfirmClose, KeepEditing, ApplyClose and Card, and passes Change, Apply and
// Close's plain case to Model::Click (its steps and the save request).
// Coordinates are canvas pixels (0-1280, 0-1024); the help card is inside
// the panel, so the picture is Width wide.
//
// Geometry: the redesign's page layout with the VR target floor:
// every pressable element is at least 64 x 64 px and 12 px from the next.
// Rail 24-294; content 328-1248; band 22-86 (drawn in 22-72); rows from 86; footer 932-996.
// Rows: 84 px pitch (66 px controls) when they fit, down to 76 (64 px
// controls) so the help card keeps about 144 px; at most 8 rows a page.
//
// For the painter (D): text and sub are final words; nothing here is
// ellipsized. Build measures with TextMeasure (pass Painter::Measure through
// it; the default is a conservative estimate). Where a role carries more:
//   Row       sub: the "Recommended: On" pill (Flag::Recommended); Hovered: the pointed slab
//   Label     text: label; sub: lock reason, warn, comfort, class tag or "Detected: ..."
//   Value     fill/mark/zeroAt: the bar (fill<0: none); hue>=-1: a swatch (-1 Off / By range);
//             sub: "n of m" (cyclers, swatches)
//   Readout   Warn: amber; Selected: a live reading (teal); else quiet
//   GuideLine text: the control (button names resolved); sub: what it does
//   PresetCard text: name; sub: blurb; Selected: applied; Changed: picked, not applied
//   ChangeLine an old -> new line on the card (Warn: a comfort line)
//   CardTitle Changed: not applied; Locked: locked; Selected: your setting (custom)
#include "vr_settings_help.h"
#include "vr_settings_content.h"
#include "vr_settings_hooks.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
namespace framevr::settings::layout {
constexpr int PanelWidth=Width,PanelHeight=Height;   // 1280 x 1024, the canvas
constexpr int MinTarget=64;                          // every pressable element is at least this tall and wide
constexpr int MinGap=12;                             // and this far from the next pressable element
// A numeric row's value drags (PointerFilter): one drag unit per DragMaxStepPx
// of laser travel, or fewer px so the whole range fits the sweep from the bar's
// left end (where the handle sits at the minimum) to the panel's right edge:
// the laser leaving the panel ends a drag. Never under DragMinStepPx, so hand
// tremor does not flicker the value. A range of more than DragCoarseAbove
// steps drags at least DragCoarseSteps steps a unit, and any range drags the
// fewest steps a unit (1, 2, 5, 10...) that fit the sweep at DragMinStepPx;
// coarse units land on multiples of it. The - / + keep the fine step.
constexpr float DragMaxStepPx=24,DragMinStepPx=8,BarInset=14;
constexpr int DragCoarseAbove=60,DragCoarseSteps=5;
inline int DragSteps(float min,float max,float step){   // the row's steps end to end; 0: unbounded
    if(!(max>min)||!(step>0)||!std::isfinite(max)||max>1e30f)return 0;
    return int(std::lround((max-min)/step));
}
inline int DragCoarse(float min,float max,float step,float sweepPx){
    const int steps=DragSteps(min,max,step);
    if(!steps)return 1;
    for(int c:{1,2,5,10,20,50,100,200,500,1000})
        if((steps<=DragCoarseAbove||c>=DragCoarseSteps)&&float((steps+c-1)/c)*DragMinStepPx<=sweepPx)return c;
    return 1000;
}
inline float DragStepPx(float min,float max,float step,float sweepPx){
    const int steps=DragSteps(min,max,step);
    if(!steps)return DragMaxStepPx;   // unbounded: no bar, plain steps
    const int coarse=DragCoarse(min,max,step,sweepPx),units=(steps+coarse-1)/coarse;
    return std::clamp(sweepPx/float(std::max(1,units)),DragMinStepPx,DragMaxStepPx);
}

// ---- geometry (canvas pixels) ----
constexpr int Inner=22;                                          // the bronze frame's inner edge
constexpr Rect PanelInner{Inner,Inner,Width-2*Inner,Height-2*Inner};
constexpr int RailX=24,RailW=270,TabY=29,TabPitch=78,TabH=66;    // 12 tabs: 29-953
constexpr int ContentX=328,ContentW=920,ContentRight=ContentX+ContentW;
constexpr int RowPad=20;                                         // label inset; controls end this far from the slab's right
// The band's links keep 64 px hit areas (22-86); the title and links are drawn
// centered in its top 50 px (center 47), and the view starts
// right under the links' hit areas (nothing may overlap them).
constexpr int BandY=Inner,BandH=64,BandInkH=50,ViewTop=BandY+BandH;   // the view starts at 86
constexpr int FooterY=932,FooterH=64,RegionBottom=FooterY-8;     // rows, readout and card live in 86-924; the footer ends at 996
constexpr int DrainH=5;                                          // Undo reset's bar under the footer, inside the frame (997-1002)
constexpr int StatusH=34,StatusY=FooterY-6-StatusH;              // save state strip 890-924
constexpr int SectionH=30,SectionCompactH=24,IntroH=44,ReadoutH=112;
constexpr int CardMin=144;                                       // the card space rows give way to
constexpr int PitchRoomy=84,PitchMin=76;
constexpr int ToggleW=150,StepW=64,StepGap=18,ChipGap=MinGap,ChipMin=96,ChipPad=34;   // ChipPad: both sides of a chip's words
constexpr int ValueMin=160,ValueMax=300;
constexpr int LabelEmberW=46;                                    // Painter::Label's changed ember after the label (10 + 36)
constexpr int CardPad=8,CardTitleH=36,CardLineH=33,CardStateH=32,CardColumnW=330,CardColumnPad=14,CardButtonH=64;
constexpr int GuideHeadH=36,GuideLineMax=68,NotesLineMax=66,NotesAuthorGap=16;
constexpr int PresetH=128,PresetPitch=140;
// Named lists: chips up to MaxChips options when they fit, else a popover: one
// column of PopCell px cells up to PopListMax options (two when that would pass
// PopMaxH), a grid of 2 columns up to 12 and 3 up to 24 (content::MaxOptions).
constexpr int MaxChips=6,PopListMax=8,PopCell=66,PopCellGap=12,PopPad=12,PopTitleH=44,PopHintH=72,PopMaxH=600,PopMinW=420,PopGap=8;
// A popover that fits neither below nor above its row goes compact: PopCellMin cells (the 64 px target floor,
// above the 56 px a ray needs), no title, a shorter hint; the color grid 8 a row (a third of the wheel) instead of 6.
constexpr int PopCellMin=MinTarget,PopHintCompact=52,PopHintColor=40;
constexpr Rect FooterLeft{330,FooterY,304,FooterH},FooterMid{682,FooterY,204,FooterH},FooterRight{950,FooterY,274,FooterH};
constexpr Rect GuideLink{890,BandY,140,BandH},NotesLink{1046,BandY,192,BandH};
constexpr Rect GuideIcon{1098,BandY,64,BandH},NotesIcon{1174,BandY,64,BandH},MonoWarning{730,BandY,352,BandInkH};
// More's link ("3 more", only while More has rows); with the Mono audio warning, icon-only and the warning moves left.
constexpr Rect MoreLink{890,BandY,190,BandH},MoreIcon{1022,BandY,64,BandH},MonoWarningMore{654,BandY,352,BandInkH};

// What an element is. The painter picks the look from the role and flags.
enum class Role : uint8_t {
    None,
    // rail and band
    Tab,            // a rail tab (Presets and the 11 pages): icon + name; hit Open
    GroupLine,      // the thin divider between rail groups
    PageTitle,      // the band's icon + cast-gold title
    HeaderLink,     // Guide / What's new in the band; hit Open
    Warning,        // the band's Windows Mono audio warning (help HelpMonoAudio)
    Section,        // a section heading between bronze rules
    // a row
    Row,            // the row's whole slab (pointed: Slab); never pressable itself
    Label,          // the row label; text: label, sub: class tag / "Needs ..." / warn line
    Chip,           // one option of a Choice; hit Set (option = its index)
    StepMinus,      // - of a stepper / slider, < of a cycler / swatch; hit Change -1, Flag::Repeat
    Value,          // the value box between them; Slider/Stepper: Flag::Drag (press does nothing, drag scrubs)
    StepPlus,       // + / >; hit Change +1, Flag::Repeat
    Toggle,         // the On/Off pill; hit Set (0 or 1)
    Slider,         // unused: a bar is the Value element's fill/mark (kept for the painter's switch)
    Swatch,         // unused: a swatch is the Value element's hue (kept for the painter's switch)
    Button,         // a row action (Calibrate body, Set eye height, Defer GPU wait offer); hit Calibrate / Recenter / Set
    // footer (fixed slots, never move)
    FooterResetPage,  // Reset page; becomes Undo reset (UndoReset) for 6 s in the same slot
    FooterRecenter,   // unused: Set eye height is a button on Body's Measurements row
    FooterClose,      // Close; with a draft: ConfirmClose flow
    FooterApply,      // Apply n changes / No changes
    ConfirmDiscard,   // confirm row: Discard n changes (where Reset page was)
    ConfirmKeep,      // confirm row: Keep editing (where Close was)
    ConfirmApply,     // confirm row: Apply and close (where Apply was)
    DrainBar,         // Undo reset's 6 px draining bar (fill: the time left, 1 to 0)
    StatusStrip,      // save state above the footer (Saving / Saved / Failed / Conflict / unavailable)
    // help card
    HelpCard,       // the card's field; hit Card (swallows presses)
    CardTitle,      // title, colored by state (default / custom / not applied / locked)
    CardText,       // body text (wraps)
    CardState,      // "At default" / "Not applied. Applied: ..." / "Locked. Now: ..."
    CardButton,     // Undo / Default / Feel it / Open <page>; hit Undo / Default / TestHaptics / Open
    PendingEntry,   // idle card: a change not yet applied ("Shield roll: 0° → +90°"); hit Open
    // other views
    PresetCard,     // a preset: icon, name, blurb; hit Preset
    PresetLine,     // unused: the pointed preset's lines are ChangeLine on the card
    GuideLine,      // a Controller guide line
    NotesText,      // What's new text (the release title is a Section)
    Readout,        // a Performance readout line (frame times, verdict, warnings)
    // appended by A (step 1)
    Intro,          // a view's one-line intro under the band
    ReadoutBox,     // the Performance readout's box (help HelpPerformance)
    ChangeLine,     // an old -> new line on the card (preset preview, confirm close, hidden drafts)
    CalibStep,      // body calibration: one of the three steps
    CalibHint,      // body calibration: the hint box
    CalibProgress,  // body calibration: the hold's progress bar (fill)
    CalibNote,      // body calibration: the measured values / notes
    // the popover (a named list too long for chips; docs/VR_SETTINGS_MENU.md), over everything else
    PopoverDim,     // the whole panel dimmed 40%; hit PopoverClose (an outside press closes, without a change, and goes no further)
    Popover,        // the floating card; hit Card (swallows presses between its options)
    PopoverTitle,   // the row's label
    PopoverOption,  // one option: hit Set, drawn as a chip; its hit box is the whole cell
    PopoverHint,    // the pointed (else the chosen) option's hint, at the bottom
    ColorDot,       // a color popover's hue: hit Set, hue the dot's; drawn 46 px in its 66 px cell (its hit box)
    Count
};

// Element state bits.
enum Flag : uint16_t {
    Enabled=1<<0,      // can be pressed now (not locked, model ready, not saving)
    Selected=1<<1,     // the chosen chip / On toggle / current tab / chosen preset
    Changed=1<<2,      // differs from the applied value (Changed accents)
    Pressed=1<<3,      // the trigger is held on it (LayoutInput::pressed)
    Hovered=1<<4,      // pointed at (LayoutInput::hover)
    Locked=1<<5,       // shown disabled with a reason (padlock, real value)
    IsDefault=1<<6,    // this option / value is the default (bronze mark)
    Recommended=1<<7,  // the mod's advice (pill)
    Repeat=1<<8,       // hold repeats the press (steppers)
    Drag=1<<9,         // a drag across it scrubs (Value of a Slider / Stepper)
    Warn=1<<10,        // carries a warn line (sub)
};

// One thing on the panel.
struct Element {
    Rect rect{0,0,0,0};       // canvas pixels
    Role role=Role::None;
    Hit hit{};                // what a press does (Kind::None: not pressable)
    int help=NoHelp;          // the help card's subject when pointed at (a Key, or a Help* id)
    Key key=Count;            // the row's setting, or Count
    int option=-1;            // Chip / Swatch: option index; CardButton: which action
    uint16_t flags=0;         // Flag bits
    draw::Icon icon=draw::Icon::None;
    std::string text;         // the main words, already formatted ("65 cm", "Apply 3 changes")
    std::string sub;          // second line: class tag, warn, "1 of 4", reason
    float fill=-1;            // Slider: 0-1 fill; <0 none
    float mark=-1;            // Slider: 0-1 default tick; <0 none
    float zeroAt=-1;          // Slider: signed ranges fill from here (0-1); <0 from the left
    float hue=draw::SwatchNone;   // a hue swatch in the value box: degrees, draw::SwatchOff / SwatchByRange, SwatchNone: none
    float dragPx=0;           // Flag::Drag: canvas px of laser travel per drag unit (0: PointerFilter's default)
    int dragCoarse=1;         // Flag::Drag: steps per drag unit (DragCoarseSteps on long ranges)
    int dragIndex=-1;         // Flag::Drag: the value's step from the minimum (-1: unknown, no alignment or clamp)
    int dragSteps=0;          // Flag::Drag: the range's steps end to end (0: unbounded)
    Rect hitRect{0,0,0,0};    // where a press lands, when larger than what is drawn (a row's controls: the full row height,
                              // meeting their neighbors in the gap; a popover cell); w 0: rect
    bool Has(Flag f)const{return (flags&f)!=0;}
    const Rect& HitBox()const{return hitRect.w>0?hitRect:rect;}
    bool operator==(const Element&)const=default;
};

// Body calibration replaces the panel while it runs (main's vr_settings_ui.cpp).
struct CalibrationView {
    bool active=false;
    int hint=0;                // calibration::Hint (Pose, Release, Floor, Height, Tracking, Upright, Arms, Still, Saving, Saved, Failed, Conflict)
    float progress=0;          // the hold, 0-1
    bool measured=false;       // Saving / Saved: show the measurements
    float eyeHeight=0,armSpan=0;   // meters
    bool operator==(const CalibrationView&)const=default;
};
// What's new: the caller fills it from release_notes.h (releasenotes::), which
// needs a generated header the layout does not include.
struct NotesView {
    const char* title=nullptr;     // ReleaseTitle
    const char* author=nullptr;    // Author
    const char* url=nullptr;       // ProjectUrl
    const char* credits=nullptr;   // Credits
    const char* const* lines=nullptr;int lineCount=0;   // ReleaseLines
    bool operator==(const NotesView&)const=default;
};

// The frame's inputs that are not in the model.
struct LayoutInput {
    Hit hover{};              // pointed element (PointerFilter's hover)
    Hit pressed{};            // held element (PointerFilter's pressed)
    int card=NoHelp;          // what the help card shows (CardFocus::Shown)
    bool cardArmed=true;      // card buttons take presses (CardFocus::ButtonsArmed)
    uint64_t nowMs=0;         // for the confirm row guard, Undo reset's drain bar
    bool monoAudio=false;
    GameVideo game{};
    PerformanceReadout performance{};
    framegen::Readout frameGen{};   // what OFXR Bridge is doing: the Frame generation row's sub line and card (hooks)
    std::string runtimeStatus;      // foveation / resolution status line (Performance readout)
    controllers::Labels detected{}; // "Detected: ..." under Button names; guide button names
    CalibrationView calibration{};
    NotesView notes{};
    bool operator==(const LayoutInput&)const=default;
};

struct Layout {
    View view=View::Guide;
    std::vector<Element> elements;   // paint order: back to front
    Rect card{0,0,0,0};              // the help card's rectangle (CardFocus freezes inside it)
    Rect popover{0,0,0,0};           // an open popover's card (w 0: none); pointer::PopoverWatch closes it when the laser stays away
    Rect popoverRow{0,0,0,0};        // the row it edits (pointing there keeps it open too)
};

// ---- text measure: the painter's, or an estimate ----
inline float StyleSize(draw::TextStyle s){
    using draw::TextStyle;
    switch(s){case TextStyle::Title:return 44;case TextStyle::Section:return 30;case TextStyle::CardTitle:return 32;case TextStyle::Label:return 36;
        case TextStyle::Value:case TextStyle::Help:return 30;case TextStyle::Small:return 26;case TextStyle::Readout:return 22;default:return 30;}
}
// Conservative widths for Cinzel caps and Alegreya Sans (wider than the
// painter measures), so its real text fits where the estimate does.
inline float EstimateWidth(std::string_view utf8,draw::TextStyle s){
    using draw::TextStyle;
    float perEm=.46f;
    switch(s){case TextStyle::Title:perEm=.74f;break;case TextStyle::Section:case TextStyle::CardTitle:perEm=.66f;break;
        case TextStyle::Value:perEm=.50f;break;case TextStyle::Help:perEm=.44f;break;default:break;}
    int glyphs=0;for(unsigned char c:utf8)if((c&0xC0)!=0x80)++glyphs;
    return float(glyphs)*StyleSize(s)*perEm;
}
struct TextMeasure {
    float (*width)(const void* context,std::string_view utf8,draw::TextStyle style)=nullptr;   // e.g. Painter::Measure(...).width
    const void* context=nullptr;
    float operator()(std::string_view utf8,draw::TextStyle style)const{return width?width(context,utf8,style):EstimateWidth(utf8,style);}
};

// ---- the content tables (C's), or a test's own ----
struct Tables {
    const content::ContentRow* rows=nullptr;size_t rowCount=0;
    const content::ContentSection* sections=nullptr;size_t sectionCount=0;
    const content::ContentPreset* presets=nullptr;size_t presetCount=0;
    const content::GuideLine* guide=nullptr;size_t guideCount=0;
    const content::ContentTool* tools=nullptr;size_t toolCount=0;
    const content::ContentRow* Row(Key key)const{for(size_t i=0;i<rowCount;++i)if(rows[i].key==key)return &rows[i];return nullptr;}
    // A view's index-th section title (SectionTable lists each page's sections in order), or nullptr.
    const char* Section(View view,int index)const{
        int n=0;for(size_t i=0;i<sectionCount;++i)if(sections[i].page==view&&n++==index)return sections[i].title;
        return nullptr;
    }
    const content::ContentTool* Tool(Kind kind)const{for(size_t i=0;i<toolCount;++i)if(tools[i].kind==kind)return &tools[i];return nullptr;}
};
// The tables of a merged menu (content::BuildMenu): the real one, or a test's.
inline Tables TablesOf(const content::MenuData& m){
    return Tables{m.rows.data(),m.rows.size(),m.sections.data(),m.sections.size(),m.presets.data(),m.presets.size(),
        content::GuideTable,std::size(content::GuideTable),content::ToolTable,std::size(content::ToolTable)};
}
inline const Tables& ContentTables(){
    static const Tables tables=TablesOf(content::Menu());   // Definitions merged with the overrides
    return tables;
}

// ---- rows ----
constexpr int MaxViewRows=8;
// The rows a view shows now, top to bottom (content order, RowShown and the
// character applied). Replaces settings::Rows for the new views.
struct ViewRows {
    std::array<Key,MaxViewRows> keys{};
    int count=0;
};
// Whether a row shows now: its rule, its class, and an offer (Defer GPU wait)
// only while its setting is still Off.
inline bool RowVisible(const content::ContentRow& r,const Values& values,const Context& context){
    if(r.kind==content::RowKind::Offer&&values[r.key]>=.5f)return false;
    return RowShown(r.key,values,context)&&content::ForCharacter(r,context);
}
// Every row the view would show, uncapped (the tests check it stays <= 8).
inline int CountRows(View view,const Values& values,const Context& context,const Tables& t=ContentTables()){
    int n=0;for(size_t i=0;i<t.rowCount;++i)if(t.rows[i].page==view&&RowVisible(t.rows[i],values,context))++n;
    return n;
}
// skip: the visible rows before this page of a paged view (More: Model::morePage x MaxViewRows).
inline ViewRows RowsOf(View view,const Values& values,const Context& context,const Tables& t=ContentTables(),int skip=0){
    ViewRows list;
    for(size_t i=0;i<t.rowCount;++i){
        const auto& r=t.rows[i];
        if(r.page==view&&list.count<MaxViewRows&&RowVisible(r,values,context)&&skip--<=0)list.keys[size_t(list.count++)]=r.key;
    }
    return list;
}
// The More view pages its rows, MaxViewRows a page (What's new's pager).
inline int MorePages(const Values& values,const Context& context,const Tables& t=ContentTables()){
    return std::max(1,(CountRows(View::More,values,context,t)+MaxViewRows-1)/MaxViewRows);
}
inline int MoreSkip(const Model& m,const Tables& t=ContentTables()){
    return m.view==View::More?std::clamp(m.morePage,0,MorePages(m.Get(),m.context,t)-1)*MaxViewRows:0;
}
// Whether a key's row shows now in its own view.
inline bool KeyVisible(Key key,const Values& values,const Context& context,const Tables& t=ContentTables()){
    const auto* r=t.Row(key);return r&&RowVisible(*r,values,context);
}

// Busy (saving) or not ready: no control that changes a value takes presses;
// tabs, Close and the guide still work.
inline bool Blocked(const Model& m){return !m.open||!m.ready||m.Busy();}
// Why a shown row is locked now, or nullptr.
inline const char* LockReason(const Model& m,Key key){
    if(hooks::BodyMeasurements(key)){
        if(!m.ready)return "Unavailable";
        if(m.Busy())return "Saving...";
        if(m.Dirty())return "Apply or discard first";
        return nullptr;
    }
    if(!RowEnabled(m.Get(),key))return hooks::LockedReason(key);
    return nullptr;
}
inline bool Editable(const Model& m,Key key){
    return key>=0&&key<Count&&!Blocked(m)&&RowEnabled(m.Get(),key);
}
// Offline difficulty without the offline scaling adapter: main shows its
// notice and leaves the row editable (the saved value applies once it loads).
inline bool OfflineNotice(const Model& m,Key key){return hooks::OfflineAdapterNotice(key)&&!m.offlineDifficultyAvailable;}
// A class without bow skills sees Spells go to the aimed monster as Off / On
// (main's LockStep): only the spell bit changes, a saved bow bit stays.
inline bool SpellLockOnOff(Key key,const Context& c){return hooks::SpellsOnly(key,c);}
inline float ClassValue(Key key,float value,float current,const Context& c){return hooks::ClassValue(key,value,current,c);}
inline const char* LabelOf(const content::ContentRow& r,const Context& c){return content::LabelFor(r,c);}
inline std::string ValueOf(const content::ContentRow& r,float value,const Context& c){return content::ValueText(r,value,c);}
// The options a row shows (content::OptionsFor). Spells go to the aimed
// monster's Off / On carry the saved bow-skills bit, so a chip's value is
// the ini value it leaves.
struct ShownOption {float value;const char* label;const char* hint;};
inline std::vector<ShownOption> OptionsOf(const content::ContentRow& r,float current,const Context& c){
    std::vector<ShownOption> list;int count=0;
    const content::Option* options=content::OptionsFor(r,c,count);
    const float bow=content::SpellsOnly(r,c)?float(int(current)&2):0.f;
    for(int i=0;i<count;++i)list.push_back({options[i].value+bow,options[i].label,options[i].hint});
    return list;
}
inline int OptionIndex(const std::vector<ShownOption>& list,float value){
    for(size_t i=0;i<list.size();++i)if(std::fabs(list[i].value-value)<.001f)return int(i);
    return -1;
}
inline bool Same(float a,float b){return std::fabs(a-b)<.0001f;}
// Lists wrap: a cycler, a choice shown as a cycler and a color swatch step from
// the last option to the first and back. Numbers (cm, m/s, %, degrees, speeds)
// stop at their ends; wrapping a measurement would surprise.
inline bool WrapsRound(const content::ContentRow& r){
    using content::RowKind;
    return r.wraps||r.kind==RowKind::Cycler||r.kind==RowKind::Choice||r.kind==RowKind::Swatch;
}
// The swatch for a color row's value, from its words: a value named "Off" (or
// any negative hue, which Normalize reads as Off) the slashed Off swatch,
// another named value (By range) the range gradient, else the hue itself.
inline float SwatchHue(const content::ContentRow& r,float v){
    for(int i=0;i<r.nameCount;++i)if(std::fabs(r.names[i].value-v)<.001f)return std::string_view(r.names[i].label)=="Off"?draw::SwatchOff:draw::SwatchByRange;
    return v<0?draw::SwatchOff:v;
}
inline bool Finite(float v){return std::isfinite(v)&&v<1e30f;}

// Changes not applied, per view: the count of visible rows on a view that
// differ from the applied values, and one bit per View with any.
inline int ChangedCount(const Model& m,View view,const Context& c,const Tables& t=ContentTables()){
    int n=0;const ChangeMask changed=settings::Changed(m.Applied(),m.Get());
    for(size_t i=0;i<t.rowCount;++i){
        const auto& r=t.rows[i];
        if(r.page==view&&r.kind!=content::RowKind::Button&&(changed&(ChangeMask{1}<<r.key))&&(RowVisible(r,m.Get(),c)||r.kind==content::RowKind::Offer))++n;
    }
    return n;
}
inline uint32_t ChangedViews(const Model& m,const Context& c,const Tables& t=ContentTables()){
    uint32_t mask=0;for(int v=0;v<int(View::Count);++v)if(ChangedCount(m,View(v),c,t))mask|=1u<<v;
    return mask;
}
// Every key the draft changed, in Key order.
inline std::vector<Key> DirtyKeys(const Model& m){
    std::vector<Key> keys;const ChangeMask changed=settings::Changed(m.Applied(),m.Get());
    for(int k=0;k<Count;++k)if(changed&(ChangeMask{1}<<k))keys.push_back(Key(k));
    return keys;
}

// ---- presets ----
// Presets never touch these (the owner's calls, and calibration's own keys).
inline bool PresetSkips(Key key){return hooks::PresetSkips(key);}
// Whether a preset sets this key for this character: not skipped, and its row
// (if any) is not hidden for the class or weapon.
inline bool PresetSets(Key key,const Context& c,const Tables& t=ContentTables()){
    if(key<0||key>=Count||PresetSkips(key)||!RowForClass(key,c))return false;
    const auto* r=t.Row(key);
    return !r||content::ForCharacter(*r,c);
}
// The value a preset gives a key for this character, from `current`.
inline float PresetValue(Key key,float value,float current,const Context& c){return Normalize(key,ClassValue(key,value,current,c));}
enum class PresetState { None, Applied, Picked, Edited };
inline PresetState StateOf(const Model& m,int index,const Context& c,const Tables& t,int* differ=nullptr){
    if(index<0||size_t(index)>=t.presetCount)return PresetState::None;
    const auto& p=t.presets[index];
    bool saved=true,draft=true;int n=0;
    for(int i=0;i<p.count;++i){
        const Key k=p.values[i].key;if(!PresetSets(k,c,t))continue;
        if(!Same(m.Applied()[k],PresetValue(k,p.values[i].value,m.Applied()[k],c)))saved=false;
        if(!Same(m.Get()[k],PresetValue(k,p.values[i].value,m.Get()[k],c))){draft=false;++n;}
    }
    if(differ)*differ=n;
    if(saved&&draft)return PresetState::Applied;
    if(draft)return PresetState::Picked;
    if(m.pickedPreset==index||saved)return PresetState::Edited;
    return PresetState::None;
}

// ---- small helpers ----
inline std::string Printf(const char* format,...)
#if defined(__clang__)||defined(__GNUC__)
    __attribute__((format(printf,1,2)))
#endif
;
inline std::string Printf(const char* format,...){
    char text[512];va_list args;va_start(args,format);std::vsnprintf(text,sizeof text,format,args);va_end(args);
    return text;
}
inline std::string Plural(int n,const char* one,const char* many){return std::to_string(n)+" "+(n==1?one:many);}
inline constexpr const char* Arrow=" \xe2\x86\x92 ";
// Word-wrap estimate: the lines `text` takes in `width`.
inline std::vector<std::string> Wrap(std::string_view text,float width,draw::TextStyle style,const TextMeasure& measure){
    std::vector<std::string> lines;std::string line;
    size_t at=0;
    while(at<text.size()){
        size_t end=text.find(' ',at);if(end==std::string_view::npos)end=text.size();
        const std::string_view word=text.substr(at,end-at);
        std::string next=line.empty()?std::string(word):line+" "+std::string(word);
        if(!line.empty()&&measure(next,style)>width){lines.push_back(line);line=std::string(word);}
        else line=std::move(next);
        at=end+1;
    }
    if(!line.empty())lines.push_back(line);
    return lines;
}
// Sentences of a help text ("A. B." -> "A.", "B.").
inline std::vector<std::string> Sentences(std::string_view text){
    std::vector<std::string> list;size_t start=0;
    for(size_t i=0;i+1<text.size();++i)
        if((text[i]=='.'||text[i]=='!'||text[i]=='?')&&text[i+1]==' '&&i+2<text.size()&&((text[i+2]>='A'&&text[i+2]<='Z')||(text[i+2]>='0'&&text[i+2]<='9'))){
            list.emplace_back(text.substr(start,i+1-start));start=i+2;
        }
    if(start<text.size())list.emplace_back(text.substr(start));
    return list;
}
inline std::string ClassTag(uint16_t classes){
    static constexpr const char* names[]={"Amazon","Sorceress","Necromancer","Paladin","Barbarian","Druid","Assassin"};
    std::vector<const char*> list;
    for(int i=0;i<7;++i)if(classes&(1u<<i))list.push_back(names[i]);
    if(classes&content::WithBow)list.push_back("bow");
    if(classes&content::WithStaff)list.push_back("staff");
    if(classes&content::WithThrown)list.push_back("thrown weapon");
    if(list.empty())return {};
    if(list.size()>3)return std::to_string(list.size())+" classes";
    std::string tag;for(size_t i=0;i<list.size();++i)tag+=(i?", ":"")+std::string(list[i]);
    return tag+" only";
}
inline const char* HintText(int hint){
    static constexpr const char* hints[]={"Ready. Hold both triggers and keep still.","Release both triggers before starting.",
        "Waiting for floor-relative tracking. If this persists, check SteamVR room setup, or press B to skip.",
        "Stand upright. Waiting for a plausible standing eye height above the floor.","Waiting for tracked headset and both controllers.",
        "Keep your head upright and look straight ahead.","Arms straight out to your sides, level with your shoulders. Then release and hold both triggers.",
        "Movement detected. Release both triggers and try again, keeping still.","Saving your measurements...","Saved. Release both triggers to return to the game.",
        "Save failed. Release and hold both triggers to retry, or press B to cancel.","Settings changed elsewhere. Release and hold both triggers to try again."};
    return hint>=0&&hint<int(std::size(hints))?hints[hint]:hints[0];
}
// ---- control choice (docs/VR_SETTINGS_MENU.md): the control comes from the data ----
// A named list shows chips when it has at most MaxChips options for this
// character and every one fits beside its label (with room for the changed
// ember); else its value opens a popover: a list up to PopListMax options, a
// grid up to content::MaxOptions. Decided per row and character, never per
// value, so a change never turns chips into a popover under the laser.
inline bool ChipsFit(const content::ContentRow& r,const Values& values,const Context& c,const TextMeasure& measure={}){
    const auto options=OptionsOf(r,values[r.key],c);
    if((r.kind!=content::RowKind::Choice&&r.kind!=content::RowKind::Cycler&&!content::SpellsOnly(r,c))||options.size()<2||options.size()>size_t(MaxChips))return false;   // spells only: Off / On chips
    float chips=float(ChipGap)*float(options.size()-1);
    for(const auto& o:options)chips+=std::max(float(ChipMin),measure(o.label,draw::TextStyle::Value)+ChipPad);
    return measure(LabelOf(r,c),draw::TextStyle::Label)+LabelEmberW+24+chips<=ContentW-2*RowPad;
}
enum class Control : uint8_t { Toggle, Chips, Popover, Number, Color, Button, Offer };
inline Control ControlOf(const content::ContentRow& r,const Values& values,const Context& c,const TextMeasure& measure={}){
    using content::RowKind;
    switch(r.kind){
        case RowKind::Toggle:return Control::Toggle;
        case RowKind::Button:return Control::Button;
        case RowKind::Offer:return Control::Offer;
        case RowKind::Swatch:return Control::Color;
        case RowKind::Choice:case RowKind::Cycler:return ChipsFit(r,values,c,measure)?Control::Chips:Control::Popover;
        default:return content::SpellsOnly(r,c)?Control::Chips:Control::Number;
    }
}
// ---- the build ----
namespace detail {
struct Builder {
    const Model& m;const Context& c;const LayoutInput& in;const Tables& t;const TextMeasure& measure;
    Layout out;
    bool blocked;
    Element& Add(Role role,Rect r,Hit hit={},int help=NoHelp,uint16_t flags=0,std::string text={},std::string sub={}){
        Element e;e.rect=r;e.role=role;e.hit=hit;e.help=help;e.flags=flags;e.text=std::move(text);e.sub=std::move(sub);
        out.elements.push_back(std::move(e));return out.elements.back();
    }
    float W(std::string_view s,draw::TextStyle style)const{return measure(s,style);}
    // A fallback row's label (upstream's words) too wide for its room ends in "...".
    std::string CutToFit(std::string text,float room,draw::TextStyle style)const{
        if(W(text,style)<=room)return text;
        while(!text.empty()){
            while(!text.empty()&&(static_cast<unsigned char>(text.back())&0xC0)==0x80)text.pop_back();   // whole UTF-8 characters
            if(!text.empty())text.pop_back();
            while(!text.empty()&&text.back()==' ')text.pop_back();
            if(W(text+"...",style)<=room)return text+"...";
        }
        return "...";
    }
    bool Dirty(Key k)const{return !Same(m.Get()[k],m.Applied()[k]);}

    // The rail: Presets and the 11 pages, a thin line between groups.
    void Rail(){
        const uint32_t changed=ChangedViews(m,c,t);
        content::Group group=content::PageOf(View::Presets).group;
        for(int i=0;i<=int(View::Performance);++i){
            const View v=View(i);const auto& p=content::PageOf(v);
            const int y=TabY+i*TabPitch;
            if(p.group!=group){Add(Role::GroupLine,{RailX+16,y-7,RailW-32,2});group=p.group;}
            // Presets until content::PresetsReady: drawn grayed out, no press; pointing still explains why.
            const bool open=v!=View::Presets||content::PresetsReady;
            auto& e=Add(Role::Tab,{RailX,y,RailW,TabH},{Kind::Open,i,-1},HelpTabFirst+i,uint16_t((open?Enabled:0)|(m.view==v?Selected:0)|(changed&(1u<<i)?Changed:0)),p.name);
            e.icon=p.icon;
        }
    }
    void Band(){
        const auto& p=content::PageOf(m.view);
        // More's link shows only while it has rows (docs/VR_SETTINGS_MENU.md):
        // then Guide and What's new go icon-only, and with the
        // Mono audio warning More does too and the warning moves left.
        const int more=CountRows(View::More,m.Get(),c,t);
        const bool icons=in.monoAudio||more>0;
        const Rect warning=more>0?MonoWarningMore:MonoWarning;
        const int limit=(in.monoAudio?warning.x:more>0?MoreLink.x:GuideLink.x)-16-330;
        const float want=56+12+W(p.name,draw::TextStyle::Title)*(in.monoAudio?38.f/44.f:1.f);
        auto& title=Add(Role::PageTitle,{330,BandY,std::min(limit,int(std::ceil(want))),BandInkH},{},NoHelp,0,p.name);
        title.icon=p.icon;
        if(in.monoAudio){
            auto& w=Add(Role::Warning,warning,{},HelpMonoAudio,Warn,MonoAudioWarning);w.icon=draw::Icon::Warning;
        }
        if(more>0){
            auto& link=Add(Role::HeaderLink,in.monoAudio?MoreIcon:MoreLink,{Kind::Open,int(View::More),-1},HelpTabFirst+int(View::More),
                uint16_t(Enabled|(m.view==View::More?Selected:0)),in.monoAudio?"":Printf("%d more",more));
            link.icon=content::PageOf(View::More).icon;
        }
        auto& guide=Add(Role::HeaderLink,icons?GuideIcon:GuideLink,{Kind::Open,int(View::Guide),-1},HelpGuide,uint16_t(Enabled|(m.view==View::Guide?Selected:0)),icons?"":content::PageOf(View::Guide).name);
        guide.icon=content::PageOf(View::Guide).icon;
        auto& notes=Add(Role::HeaderLink,icons?NotesIcon:NotesLink,{Kind::Open,int(View::WhatsNew),-1},HelpWhatsNew,uint16_t(Enabled|(m.view==View::WhatsNew?Selected:0)),icons?"":content::PageOf(View::WhatsNew).name);
        notes.icon=content::PageOf(View::WhatsNew).icon;
    }
    int RegionEnd()const{return Status()?StatusY-6:RegionBottom;}
    const char* Status()const{
        if(!m.ready)return "Configuration unavailable; controls help still works.";
        switch(m.result){
            case Result::Saving:return "Saving...";case Result::Saved:return "Saved.";
            case Result::Failed:return "Save failed. Your edits are still unsaved.";
            case Result::Conflict:return "Settings changed elsewhere. Current values reloaded.";
            default:return nullptr;
        }
    }

    // ---- a settings page ----
    struct Planned {const content::ContentRow* row;int section;};
    int ValueWidth(const std::vector<Planned>& rows)const{
        float w=ValueMin;
        for(const auto& p:rows){
            const auto& r=*p.row;const auto& d=Definitions[r.key];const float v=m.Get()[r.key];
            auto fit=[&](const std::string& s,float extra){w=std::max(w,W(s,draw::TextStyle::Value)+40+extra);};
            switch(r.kind){
                case content::RowKind::Cycler:case content::RowKind::Choice:
                    if(UsesChips(r))break;
                    for(const auto& o:OptionsOf(r,v,c))fit(o.label,0);
                    if(r.optionCount==0)fit(ValueOf(r,v,c),0);
                    break;
                case content::RowKind::Stepper:case content::RowKind::Slider:case content::RowKind::Swatch:{
                    const float extra=r.kind==content::RowKind::Swatch?44.f:0.f;
                    if(r.kind==content::RowKind::Swatch){for(const char* name:content::HueNames)fit(name,extra);for(int i=0;i<r.nameCount;++i)fit(r.names[i].label,extra);}
                    fit(ValueOf(r,v,c),extra);fit(ValueOf(r,d.min,c),extra);fit(ValueOf(r,d.defaultValue,c),extra);
                    if(Finite(d.max))fit(ValueOf(r,d.max,c),extra);
                    break;}
                default:break;
            }
        }
        return int(std::ceil(std::min(w,float(ValueMax))));
    }
    bool UsesChips(const content::ContentRow& r)const{return ChipsFit(r,m.Get(),c,measure);}
    std::string SubLine(const content::ContentRow& r,const char* locked,uint16_t& flags)const{
        const Key k=r.key;const float v=m.Get()[k];
        if(locked){flags|=Locked;return locked;}
        if(hooks::ShowsDetectedControllers(k))return content::DetectedText(in.detected);
        if(OfflineNotice(m,k))return content::OfflineUnavailableShort;   // main's notice; the row stays editable
        if(hooks::ShowsFrameGenStatus(k)&&in.frameGen.state!=framegen::State::Off){   // main's Perf status line, short: "Now: active"
            if(framegen::Warning(in.frameGen))flags|=Warn;
            return std::string("Now: ")+framegen::StateName(in.frameGen.state);
        }
        if(r.warnWhen.text&&v>r.warnWhen.above){flags|=Warn;return r.warnWhen.text;}
        if(r.warn){flags|=Warn;return r.warn;}
        if(r.classes!=content::AllClasses&&c.heroClass<0)return ClassTag(r.classes);
        return {};
    }
    void Row(const content::ContentRow& r,int y,int pitch,int vw){
        const Key k=r.key;const auto& d=Definitions[k];const float v=m.Get()[k];
        const int ctlH=pitch>=PitchRoomy?66:64,cy=y+(pitch-ctlH)/2,right=ContentRight-RowPad;
        const char* locked=LockReason(m,k);
        const bool live=!blocked&&!locked,dirty=Dirty(k);
        std::vector<Element> controls;
        auto control=[&](Role role,Rect rect,Hit hit,uint16_t flags,std::string text={},std::string sub={})->Element&{
            Element e;e.rect=rect;e.role=role;e.hit=hit;e.help=k;e.key=k;e.flags=flags;e.text=std::move(text);e.sub=std::move(sub);
            controls.push_back(std::move(e));return controls.back();
        };
        int left=right;
        using content::RowKind;
        const bool chips=UsesChips(r);
        if(r.kind==RowKind::Toggle){
            const bool on=v>.5f&&!locked;
            left=right-ToggleW;
            control(Role::Toggle,{left,cy,ToggleW,ctlH},{Kind::Set,k,0,on?0.f:1.f},uint16_t((live?Enabled:0)|(on?Selected:0)|(Same(v,d.defaultValue)?IsDefault:0)|(dirty?Changed:0)|(locked?Locked:0)),on?"On":"Off");
        }else if(chips){
            const auto options=OptionsOf(r,v,c);
            std::vector<int> widths;for(const auto& o:options)widths.push_back(std::max(ChipMin,int(std::ceil(W(o.label,draw::TextStyle::Value)))+ChipPad));
            int x=right;for(int w:widths)x-=w;x-=ChipGap*int(options.size()-1);
            left=x;
            for(size_t i=0;i<options.size();++i){
                const bool chosen=Same(options[i].value,v);
                const bool isDefault=SpellLockOnOff(k,c)?(int(options[i].value)&1)==(int(d.defaultValue)&1):Same(options[i].value,d.defaultValue);
                auto& e=control(Role::Chip,{x,cy,widths[i],ctlH},{Kind::Set,k,0,options[i].value},uint16_t((live?Enabled:0)|(chosen?Selected:0)|(isDefault?IsDefault:0)|(chosen&&dirty?Changed:0)|(locked?Locked:0)),options[i].label);
                e.option=int(i);x+=widths[i]+ChipGap;
            }
        }else if(r.kind==RowKind::Button){
            // Body's Measurements row: Calibrate body, then Set eye height (eye height left the footer).
            const auto* recenter=t.Tool(Kind::Recenter);const auto* calibrate=t.Tool(Kind::Calibrate);
            const std::string second=recenter&&recenter->label?recenter->label:"Set eye height",first=calibrate&&calibrate->label?calibrate->label:"Calibrate body";
            const int w2=std::max(MinTarget,int(std::ceil(W(second,draw::TextStyle::Value)))+48),w1=std::max(MinTarget,int(std::ceil(W(first,draw::TextStyle::Value)))+48);
            const int x2=right-w2,x1=x2-52-w1;left=x1;
            auto& a=control(Role::Button,{x1,cy,w1,ctlH},{Kind::Calibrate},uint16_t((m.CanCalibrate()?Enabled:0)|(locked?Locked:0)),first);a.help=HelpCalibrate;
            auto& b=control(Role::Button,{x2,cy,w2,ctlH},{Kind::Recenter},Enabled,second);b.help=HelpRecenter;
        }else{
            // [-][value][+]: steppers and sliders step Definitions' step and stop at
            // their ends; cyclers and swatches (lists) wrap round (WrapsRound).
            const auto options=OptionsOf(r,v,c);
            const bool cycler=!options.empty();
            const bool swatch=r.kind==RowKind::Swatch;
            const bool wraps=WrapsRound(r);
            bool canDown=true,canUp=true;std::string sub;int index=-1,count=0;
            if(cycler){
                index=OptionIndex(options,v);count=int(options.size());
                if(!wraps){canDown=index>0;canUp=index<0||index<count-1;}
                // the value names the option and opens the popover; < > still step and wrap
            }else if(swatch){   // a color: a dot and its name; the value opens the color grid, < > step and wrap
                if(!wraps){canDown=v>d.min+.0001f;canUp=v<d.max-.0001f;}
            }else if(!wraps){canDown=v>d.min+.0001f;canUp=!Finite(d.max)||v<d.max-.0001f;}
            const int vx=right-StepW-StepGap-vw;left=vx-StepGap-StepW;
            control(Role::StepMinus,{left,cy,StepW,ctlH},{Kind::Change,k,-1},uint16_t((live&&canDown?Enabled:0)|Repeat|(locked?Locked:0)));
            const bool bar=!cycler&&!swatch&&Finite(d.max)&&d.max>d.min;
            // A swatch shows the color and its name.
            auto& value=control(Role::Value,{vx,cy,vw,ctlH},bar||(!cycler&&!swatch)?Hit{Kind::Change,k,0}:Hit{Kind::Popover,k},
                uint16_t((live&&(!cycler&&!swatch)?Enabled|Drag:0)|(live&&(cycler||swatch)?Enabled:0)|(Same(v,d.defaultValue)?IsDefault:0)|(dirty?Changed:0)|(locked?Locked:0)),ValueOf(r,v,c),sub);
            if(bar){
                const float span=d.max-d.min;
                value.fill=std::clamp((v-d.min)/span,0.f,1.f);value.mark=std::clamp((d.defaultValue-d.min)/span,0.f,1.f);
                if(d.min<0&&d.max>0)value.zeroAt=-d.min/span;
            }
            if(swatch)value.hue=SwatchHue(r,v);
            if(!cycler&&!swatch){   // a number: drag it along its bar
                const float sweep=float(PanelInner.x+PanelInner.w-(vx+int(BarInset))-8);
                value.dragPx=DragStepPx(d.min,d.max,d.step,sweep);
                value.dragSteps=DragSteps(d.min,d.max,d.step);
                if(value.dragSteps){value.dragCoarse=DragCoarse(d.min,d.max,d.step,sweep);value.dragIndex=std::clamp(int(std::lround((v-d.min)/d.step)),0,value.dragSteps);}
            }
            control(Role::StepPlus,{vx+vw+StepGap,cy,StepW,ctlH},{Kind::Change,k,1},uint16_t((live&&canUp?Enabled:0)|Repeat|(locked?Locked:0)));
        }
        uint16_t labelFlags=dirty&&r.kind!=RowKind::Button?Changed:0;
        std::string sub=SubLine(r,locked,labelFlags);
        auto& slab=Add(Role::Row,{ContentX,y,ContentW,pitch},{},k,dirty?Changed:0);slab.key=k;
        if(std::isfinite(r.recommended)&&!Same(v,r.recommended)&&!locked){slab.flags|=Recommended;labelFlags|=Recommended;slab.sub="Recommended: "+ValueOf(r,r.recommended,c);}
        const int lx=ContentX+RowPad;
        const int labelW=std::max(0,left-16-lx);
        std::string text=LabelOf(r,c);
        if(r.fallback)text=CutToFit(text,float(labelW-((labelFlags&Changed)?LabelEmberW:0)),draw::TextStyle::Label);   // upstream's words: only these may be cut
        auto& label=Add(Role::Label,{lx,y,labelW,pitch},{},k,labelFlags,std::move(text),std::move(sub));label.key=k;
        // Hit boxes: the full row height, meeting the neighbor control halfway across the gap.
        for(size_t i=0;i<controls.size();++i){
            Rect& r0=controls[i].rect;
            const int leftGap=i?r0.x-(controls[i-1].rect.x+controls[i-1].rect.w):0,rightGap=i+1<controls.size()?controls[i+1].rect.x-(r0.x+r0.w):0;
            controls[i].hitRect={r0.x-leftGap/2,y,r0.w+leftGap/2+(rightGap-rightGap/2),pitch};
        }
        if(k==m.popoverKey)out.popoverRow={ContentX,y,ContentW,pitch};
        for(auto& e:controls)out.elements.push_back(std::move(e));
    }
    void Readout(int y,const content::ContentRow* offer){
        Add(Role::ReadoutBox,{ContentX,y,ContentW,ReadoutH},{},HelpPerformance);
        int textRight=ContentRight-16;
        if(offer){
            const std::string label=offer->label?offer->label:"Defer GPU wait";
            const int w=std::max(MinTarget,int(std::ceil(W(label,draw::TextStyle::Value)))+48);
            auto& b=Add(Role::Button,{ContentRight-12-w,y+4,w,64},{Kind::Set,offer->key,0,1},offer->key,uint16_t(Editable(m,offer->key)?Enabled:0),label);
            b.key=offer->key;textRight=b.rect.x-16;
        }
        const char* video=GameVideoWarning(in.game);
        const bool defer=offer!=nullptr;
        struct Line {std::string text;uint16_t flags;};
        const Line first=defer&&video?Line{DeferWarning,Warn}:Line{in.runtimeStatus,0};
        const Line second=video?Line{video,Warn}:defer?Line{DeferWarning,Warn}:Line{{},0};
        const std::string now=PerformanceLine(in.performance),verdict=PerformanceVerdict(in.performance);
        const int x=ContentX+16;
        if(!first.text.empty())Add(Role::Readout,{x,y+4,textRight-x,30},{},HelpPerformance,first.flags,first.text);
        if(!second.text.empty())Add(Role::Readout,{x,y+36,textRight-x,30},{},HelpPerformance,second.flags,second.text);
        Add(Role::Readout,{x,y+68,ContentRight-16-x,22},{},HelpPerformance,in.performance.valid?Selected:0,now);
        if(!verdict.empty())Add(Role::Readout,{x,y+90,ContentRight-16-x,22},{},HelpPerformance,in.performance.CpuMs()>in.performance.budgetMs?Warn:0,verdict);
    }
    int Page(){
        const auto& page=content::PageOf(m.view);
        std::vector<Planned> rows;const content::ContentRow* offer=nullptr;
        const ViewRows shown=RowsOf(m.view,m.Get(),c,t,MoreSkip(m,t));
        for(int i=0;i<shown.count;++i){
            const auto* r=t.Row(shown.keys[size_t(i)]);
            if(r->kind==content::RowKind::Offer){offer=r;continue;}
            rows.push_back({r,r->section});
        }
        int sections=0;for(size_t i=0;i<rows.size();++i)if(i==0||rows[i].section!=rows[i-1].section)if(t.Section(m.view,rows[i].section))++sections;
        const bool readout=m.view==View::Performance;
        const int morePages=m.view==View::More?MorePages(m.Get(),c,t):1,pagerH=morePages>1?MinTarget+MinGap:0;
        const int n=int(rows.size()),region=RegionEnd()-ViewTop-pagerH;
        int cardRoom=CardMin;   // the card space rows give way to: enough for the page's longest help
        for(const auto& p:rows)cardRoom=std::max(cardRoom,RowCardNeed(*p.row));
        // Rows close up to PitchMin first; a page still short of card room draws its section headings compact.
        const int sectionH=region-sections*SectionH-(readout?ReadoutH+2:0)-n*PitchMin>=cardRoom?SectionH:SectionCompactH;
        const int fixed=sections*sectionH+(readout?ReadoutH:0);
        const int pitch=std::clamp(n?(region-cardRoom-fixed)/n:PitchRoomy,PitchMin,PitchRoomy);
        int y=ViewTop;
        if(n<=6&&region-fixed-n*pitch-IntroH>=200+CardMin/2){Add(Role::Intro,{ContentX,y,ContentW,IntroH},{},NoHelp,0,page.intro);y+=IntroH;}
        const int vw=ValueWidth(rows);
        for(size_t i=0;i<rows.size();++i){
            if(i==0||rows[i].section!=rows[i-1].section)
                if(const char* title=t.Section(m.view,rows[i].section)){Add(Role::Section,{ContentX,y,ContentW,sectionH},{},NoHelp,0,title);y+=sectionH;}
            Row(*rows[i].row,y,pitch,vw);y+=pitch;
        }
        if(readout){y+=2;Readout(y,offer);y+=ReadoutH;}
        if(morePages>1)y=Pager(y,std::clamp(m.morePage,0,morePages-1),morePages,HelpTabFirst+int(View::More));
        return y;
    }
    // Previous / Next and "Page n of m" under a paged view's lines or rows (What's new, More).
    int Pager(int y,int page,int pages,int help){
        constexpr int PagerW=220;y+=MinGap/2;
        Add(Role::Button,{ContentX,y,PagerW,MinTarget},{Kind::NotesPage,0,-1},help,uint16_t(page>0?Enabled:0),"Previous");
        Add(Role::Button,{ContentRight-PagerW,y,PagerW,MinTarget},{Kind::NotesPage,0,1},help,uint16_t(page<pages-1?Enabled:0),"Next");
        auto& label=Add(Role::NotesText,{ContentX+PagerW+MinGap,y+14,ContentW-2*(PagerW+MinGap),36},{},help,0,Printf("Page %d of %d",page+1,pages));
        label.option=0;   // drawn centered
        return y+MinTarget+MinGap/2;
    }
    // ---- other views ----
    int Presets(){
        int y=ViewTop;bool any=false;
        for(size_t i=0;i<t.presetCount;++i)if(StateOf(m,int(i),c,t)!=PresetState::None)any=true;
        Add(Role::Intro,{ContentX,y,ContentW,IntroH},{},NoHelp,0,any?"Point at a play style to see what it changes.":"Custom: your settings match no play style.");
        y+=IntroH;
        for(size_t i=0;i<t.presetCount&&i<size_t(MaxPresets);++i){
            const auto& p=t.presets[i];int differ=0;
            const PresetState state=StateOf(m,int(i),c,t,&differ);
            auto& e=Add(Role::PresetCard,{ContentX,y,ContentW,PresetH},{Kind::Preset,int(i)},HelpPresetFirst+int(i),
                uint16_t((blocked?0:Enabled)|(state==PresetState::Applied?Selected:0)|(state==PresetState::Picked?Changed:0)),p.name,p.blurb);
            e.icon=p.icon;e.option=int(i);
            const char* words=state==PresetState::Applied?"Applied":state==PresetState::Picked?"Picked. Not applied":nullptr;
            std::string text=words?words:state==PresetState::Edited?"Edited: "+Plural(differ,"row differs","rows differ"):std::string{};
            if(!text.empty()){
                const int w=int(std::ceil(W(text,draw::TextStyle::Small)))+16;
                Add(Role::Label,{ContentRight-RowPad-w,y+8,w,32},{},HelpPresetFirst+int(i),state==PresetState::Picked?Changed:0,{},std::move(text));
            }
            y+=PresetPitch;
        }
        return y;
    }
    int Guide(){
        const auto labels=controllers::Resolve(hooks::ButtonNames(m.Get()),in.detected);
        std::vector<const content::GuideLine*> left,right;std::string panels;
        for(size_t i=0;i<t.guideCount;++i){
            const auto& g=t.guide[i];
            if(g.column==content::GuideColumn::Left)left.push_back(&g);
            else if(g.column==content::GuideColumn::Right)right.push_back(&g);
            else panels+=(panels.empty()?"Game menus: ":" \xc2\xb7 ")+content::GuideControl(g,labels)+" = "+g.action;
        }
        const int colW=(ContentW-20)/2,x0=ContentX,x1=ContentX+colW+20;
        int y=ViewTop;
        Add(Role::Section,{x0,y,colW,GuideHeadH},{},NoHelp,0,"Left hand");
        Add(Role::Section,{x1,y,colW,GuideHeadH},{},NoHelp,0,"Right hand");
        y+=GuideHeadH;
        const size_t n=std::max<size_t>({left.size(),right.size(),1});
        const int panelsH=panels.empty()?0:64,space=RegionEnd()-y-panelsH-CardMin;
        const int pitch=std::min(GuideLineMax,space/int(n));
        for(size_t i=0;i<n;++i){
            if(i<left.size())Add(Role::GuideLine,{x0,y+int(i)*pitch,colW,pitch-2},{},HelpGuide,0,content::GuideControl(*left[i],labels),left[i]->action);
            if(i<right.size())Add(Role::GuideLine,{x1,y+int(i)*pitch,colW,pitch-2},{},HelpGuide,0,content::GuideControl(*right[i],labels),right[i]->action);
        }
        y+=int(n)*pitch+4;
        if(!panels.empty()){Add(Role::NotesText,{ContentX+12,y,ContentW-24,panelsH-4},{},HelpGuide,0,panels);y+=panelsH;}   // may wrap to two lines
        return y;
    }
    int Notes(){
        const auto& n=in.notes;int y=ViewTop;
        Add(Role::Section,{ContentX,y,ContentW,SectionH+4},{},HelpWhatsNew,0,n.title?n.title:content::PageOf(View::WhatsNew).name);y+=SectionH+4;
        if(n.author||n.url){
            const int half=ContentW/2;
            if(n.author)Add(Role::NotesText,{ContentX+12,y,half-24,36},{},HelpWhatsNew,0,n.author);
            if(n.url)Add(Role::NotesText,{ContentX+half,y,half-12,36},{},HelpWhatsNew,Selected,n.url);
            y+=36+NotesAuthorGap;   // a clear space between the byline and the first release line
        }
        // A long release pages its lines, NotesPerPage a page (main's Notes tab),
        // with Previous / Next and the page number under them.
        const int pages=NotesPages(n.lineCount),page=std::clamp(m.notesPage,0,pages-1);
        const int first=pages>1?page*NotesPerPage:0,lines=pages>1?std::min(NotesPerPage,n.lineCount-first):n.lineCount;
        const int creditsH=n.credits?40:0,pagerH=pages>1?MinTarget+MinGap:0;
        const int space=RegionEnd()-y-creditsH-pagerH-CardMin;
        // Paged, every page keeps a full page's pitch, so Previous / Next stay put while you turn.
        const int slots=pages>1?NotesPerPage:lines;
        const int pitch=slots?std::min(NotesLineMax,space/std::max(1,slots)):NotesLineMax;
        const int shown=pitch>=40?lines:std::max(0,space/40);
        const int step=pitch>=40?pitch:40;
        for(int i=0;i<shown&&i<lines;++i)Add(Role::NotesText,{ContentX,y+i*step,ContentW,step-2},{},HelpWhatsNew,0,n.lines[first+i]?n.lines[first+i]:"");
        y+=(pages>1?slots:std::min(shown,lines))*step;
        if(pages>1)y=Pager(y,page,pages,HelpWhatsNew);
        if(n.credits){Add(Role::NotesText,{ContentX+12,y+4,ContentW-24,32},{},HelpWhatsNew,0,std::string("Credits: ")+n.credits);y+=creditsH;}
        return y;
    }

    // ---- the help card ----
    struct CardButtonSpec {std::vector<std::string> names;Hit hit;draw::Icon icon;bool enabled;int action;};
    struct CardSpec {
        int subject=NoHelp;
        std::string title;uint16_t titleFlags=0;
        std::string text;
        std::string state;uint16_t stateFlags=0;
        std::string shortState;   // the state in fewer words, for the button column (the title's color says the rest)
        std::vector<std::pair<std::string,uint16_t>> lines;   // ChangeLine
        std::vector<std::pair<std::string,Hit>> entries;      // PendingEntry
        std::vector<CardButtonSpec> buttons;
    };
    std::string OldNew(Key k,float from,float to)const{
        const auto* r=t.Row(k);
        if(!r)return std::string(Definitions[k].label)+": "+Printf("%g",double(from))+Arrow+Printf("%g",double(to));
        return std::string(LabelOf(*r,c))+": "+ValueOf(*r,from,c)+Arrow+ValueOf(*r,to,c);
    }
    std::string HiddenTag(Key k)const{
        const auto* r=t.Row(k);
        const std::string tag=r?ClassTag(r->classes):std::string{};
        return tag.empty()?" (hidden)":" ("+tag+")";
    }
    bool Armed()const{return in.cardArmed&&!blocked;}
    // The height help text takes in `width` at the largest size from Help's
    // 30 px down to the 22 px floor at which it fits `space`; -1 if none does.
    // The measure is at 30 px; widths scale with the size, less HelpMargin so
    // the painter's own wrap never needs a line more than this one.
    static constexpr float HelpMargin=.95f;
    int HelpHeight(const std::string& text,int width,int space)const{
        const float full=draw::FontSize(draw::TextStyle::Help);
        for(float px=full;px>=draw::MinFontSize-.01f;px-=1){
            const int n=int(Wrap(text,float(width)*HelpMargin*full/px,draw::TextStyle::Help,measure).size());
            const int h=int(std::ceil(float(n)*px*1.1f))+4;
            if(h<=space)return h;
        }
        return -1;
    }
    // The same at the 22 px floor, however tall.
    int HelpHeightAtFloor(const std::string& text,int width)const{
        const float full=draw::FontSize(draw::TextStyle::Help),px=draw::MinFontSize;
        const int n=int(Wrap(text,float(width)*HelpMargin*full/px,draw::TextStyle::Help,measure).size());
        return int(std::ceil(float(n)*px*1.1f))+4;
    }
    // The card height a row's help needs to show whole at the floor: at its
    // value (full width, title, help, state line) and changed (help beside the
    // Undo / Default column, the state line in the column), and each option's
    // hint. Page reserves the largest on the page, so the card grows (the rows
    // close up to PitchMin) instead of cutting a help short.
    int RowCardNeed(const content::ContentRow& r)const{
        const int fullW=ContentW-2*CardPad,sideW=ContentW-CardPad-16-CardColumnW-CardColumnPad;
        const std::string help=RowHelp(r.key);
        int need=CardTitleH+HelpHeightAtFloor(help,fullW)+(r.kind==content::RowKind::Button?0:CardStateH);
        if(r.kind!=content::RowKind::Button){
            need=std::max(need,CardTitleH+HelpHeightAtFloor(help,sideW)+CardStateH);   // a long state line stays under the help
            need=std::max(need,CardStateH+8+CardButtonH);   // the column: state over the buttons side by side
            int count=0;const content::Option* options=content::OptionsFor(r,c,count);
            for(int i=0;i<count;++i)if(options[i].hint)need=std::max(need,CardTitleH+HelpHeightAtFloor(options[i].hint,fullW)+CardStateH);
        }
        return need+2*CardPad;
    }
    // A row's help: its content help (else the old help text); Offline
    // difficulty leads with main's note, or without the adapter main's notice instead.
    std::string RowHelp(Key k)const{
        const auto* r=t.Row(k);
        std::string help=r&&r->help?r->help:HelpText(k);
        if(hooks::OfflineAdapterNotice(k))help=std::string(m.offlineDifficultyAvailable?content::OfflineNote:content::OfflineUnavailable)+" "+help;
        return help;
    }
    CardSpec RowCard(Key k)const{
        CardSpec s;s.subject=k;
        const auto* r=t.Row(k);const float v=m.Get()[k];const auto& d=Definitions[k];
        s.title=r?LabelOf(*r,c):HelpTitle(k);
        std::string help=RowHelp(k);
        if(!r||r->kind==content::RowKind::Button){s.text=help;return s;}
        // A pointed chip: its hint, and whether it is chosen.
        if(in.hover.kind==Kind::Set&&in.hover.index==k&&r->kind!=content::RowKind::Toggle&&r->kind!=content::RowKind::Offer){
            const auto options=OptionsOf(*r,v,c);const int i=OptionIndex(options,in.hover.value);
            if(i>=0){help=options[size_t(i)].hint?options[size_t(i)].hint:help;s.state=std::string(options[size_t(i)].label)+(Same(options[size_t(i)].value,v)?": chosen":": press to choose");}
        }
        if(in.hover.kind==Kind::Popover&&in.hover.index==k)s.state="Now: "+ValueOf(*r,v,c)+". Press to see all "+std::to_string(OptionsOf(*r,v,c).size())+" choices";
        s.text=help;
        if(LockReason(m,k)){
            s.titleFlags=Locked;s.stateFlags=Locked;
            s.state="Locked. Now: "+ValueOf(*r,v,c);
            return s;
        }
        if(r->kind==content::RowKind::Offer)s.state=v>.5f?"Pressed: Apply saves it":DeferWarning;
        else if(Dirty(k)){s.titleFlags=Changed;s.stateFlags=Changed;s.state="Not applied. Applied: "+ValueOf(*r,m.Applied()[k],c);s.shortState="Applied: "+ValueOf(*r,m.Applied()[k],c);}
        else if(!Same(v,d.defaultValue)){s.titleFlags=Selected;if(s.state.empty()){s.state="Your setting. Default: "+ValueOf(*r,d.defaultValue,c);s.shortState="Default: "+ValueOf(*r,d.defaultValue,c);}}
        else if(s.state.empty())s.state="At default";
        // A color row's value box shows only the swatch: the card names the color.
        if(r->kind==content::RowKind::Swatch){const std::string now="Now: "+ValueOf(*r,v,c)+". ";s.state=now+s.state;if(!s.shortState.empty())s.shortState=now+s.shortState;}
        if(hooks::ShowsFrameGenStatus(k)&&!Dirty(k)&&in.frameGen.state!=framegen::State::Off){   // what OFXR Bridge is doing: main's Perf status line
            std::string line=framegen::Line(in.frameGen);constexpr std::string_view prefix="Frame generation: ";
            if(line.rfind(prefix,0)==0)line.erase(0,prefix.size());
            if(!line.empty())line[0]=char(std::toupper(static_cast<unsigned char>(line[0])));
            s.state=line;s.shortState.clear();   // the whole line, under the help when the button column has no room for it
            if(framegen::Warning(in.frameGen))s.stateFlags=Warn;
        }
        const int category=HapticCategory(k);
        if(category>=0)s.buttons.push_back({{"Feel it at "+ValueOf(*r,v,c),"Feel it"},{Kind::TestHaptics,category,1},draw::Icon::Feel,Armed()&&m.CanTestHaptics(category),0});
        if(Dirty(k)){const auto was=ValueOf(*r,m.Applied()[k],c);s.buttons.push_back({{"Undo: back to "+was,"Undo: "+was,"Undo"},{Kind::Undo,k},draw::Icon::Undo,Armed(),1});}
        if(r->kind!=content::RowKind::Offer&&!Same(v,d.defaultValue)){
            const auto def=ValueOf(*r,d.defaultValue,c);
            s.buttons.push_back({{std::string(LabelOf(*r,c))+" to "+def,"Default: "+def,"Default"},{Kind::Default,k},draw::Icon::Default,Armed()&&Editable(m,k),2});
        }
        return s;
    }
    CardSpec PendingCard()const{
        CardSpec s;s.subject=HelpPending;s.title=HelpTitle(HelpPending);s.titleFlags=Changed;s.stateFlags=Changed;
        int hidden=0;
        for(Key k:DirtyKeys(m)){
            const auto* r=t.Row(k);
            const std::string line=OldNew(k,m.Applied()[k],m.Get()[k]);
            if(r&&(KeyVisible(k,m.Get(),c,t)||r->kind==content::RowKind::Offer))s.entries.push_back({line,{Kind::Open,int(r->page),int(k)}});
            else{s.lines.push_back({line+HiddenTag(k),0});++hidden;}
        }
        s.state=(hidden?std::to_string(hidden)+" hidden for this class. ":std::string{})+"Press one to go to it.";
        return s;
    }
    CardSpec ConfirmCard()const{
        CardSpec s;s.subject=HelpKeepEditing;s.title="Close without applying?";s.titleFlags=Changed;s.stateFlags=Changed;
        const auto keys=DirtyKeys(m);
        for(Key k:keys)s.lines.push_back({OldNew(k,m.Applied()[k],m.Get()[k])+(KeyVisible(k,m.Get(),c,t)||(t.Row(k)&&t.Row(k)->kind==content::RowKind::Offer)?"":" (hidden)"),0});
        s.state=Plural(int(keys.size()),"change","changes")+" not applied. Right B: Keep editing.";
        return s;
    }
    CardSpec PresetCardSpec(int index)const{
        CardSpec s;s.subject=HelpPresetFirst+index;
        const auto& p=t.presets[index];s.title=p.name;
        if(StateOf(m,index,c,t)==PresetState::Applied)s.titleFlags=Selected;
        auto isComfort=[](Key k){return hooks::ComfortKey(k);};
        std::vector<std::pair<std::string,uint16_t>> first,rest;int other=0;
        for(int i=0;i<p.count;++i){
            const Key k=p.values[i].key;
            if(k<0||k>=Count||PresetSkips(k))continue;
            if(!PresetSets(k,c,t)){++other;continue;}
            const float to=PresetValue(k,p.values[i].value,m.Get()[k],c);
            if(Same(to,m.Get()[k]))continue;
            (isComfort(k)?first:rest).push_back({OldNew(k,m.Get()[k],to),uint16_t(isComfort(k)?Warn:0)});
        }
        s.lines=std::move(first);for(auto& l:rest)s.lines.push_back(std::move(l));
        if(s.lines.empty()){s.text=p.blurb;s.state="Nothing to change. ";}
        s.state+=(other?Plural(other,"other-class row","other-class rows")+" kept. ":std::string{})+"Press to pick; Apply saves.";
        return s;
    }
    CardSpec IdleCard()const{
        CardSpec s;
        if(!DirtyKeys(m).empty()&&m.view!=View::Guide&&m.view!=View::WhatsNew)return PendingCard();
        // The band already names the page: the idle card says what to do
        // instead, plus the page's intro when the page did not draw it.
        const auto& p=content::PageOf(m.view);
        bool introShown=false;for(const auto& e:out.elements)if(e.role==Role::Intro)introShown=true;
        switch(m.view){
            case View::Guide:s.subject=HelpGuide;s.title="Wheels and skills";s.text="Each stick opens its hand's wheel. Point the other hand's laser at a wedge to assign a skill.";s.state="Names follow Interface: Button names";break;
            case View::WhatsNew:s.subject=HelpWhatsNew;s.title="This version";
                s.text=NotesPages(in.notes.lineCount)>1?"One line per change; Previous and Next turn the pages. Press What's new again to go back to your page.":"One line per change. Press What's new again to go back to your page.";
                break;
            case View::Presets:s.subject=HelpPresets;s.title="How presets work";s.text="Each sets only the rows it lists, comfort changes first. Pick one; Apply saves it.";break;
            default:{
                s.subject=HelpTabFirst+int(m.view);s.title="Point at a setting";
                const std::string what="Its help shows here, with Undo and Default when they apply.";
                s.text=introShown||!p.intro?what:std::string(p.intro)+" "+what;
                break;
            }
        }
        return s;
    }
    CardSpec SubjectCard(int id)const{
        if(id>=0&&id<Count)return RowCard(Key(id));
        if(id>=HelpTabFirst&&id<HelpTabFirst+int(View::Count)){
            const View v=View(id-HelpTabFirst);const auto& p=content::PageOf(v);
            CardSpec s;s.subject=id;s.title=p.name;s.text=v==View::Presets?(content::PresetsReady?HelpText(HelpPresets):content::PresetsLater):p.intro;
            if(const int n=ChangedCount(m,v,c,t)){s.state=std::to_string(n)+" not applied";s.titleFlags=s.stateFlags=Changed;}
            return s;
        }
        if(id>=HelpPresetFirst&&id<HelpPresetFirst+MaxPresets&&size_t(id-HelpPresetFirst)<t.presetCount)return PresetCardSpec(id-HelpPresetFirst);
        if((id==HelpApply||id==HelpPending||id==HelpApplyClose)&&!DirtyKeys(m).empty()&&id!=HelpApplyClose)return PendingCard();
        if(id==HelpApply){CardSpec s;s.subject=id;s.title=HelpTitle(id);s.text="Nothing to apply. Changes stay a draft until you press Apply.";return s;}
        if(id==HelpKeepEditing)return ConfirmCard();
        if(id==HelpDiscard){
            CardSpec s=ConfirmCard();s.subject=id;s.title="Discard "+Plural(int(DirtyKeys(m).size()),"change","changes");s.titleFlags=Warn;
            s.text=HelpText(HelpDiscard);s.state.clear();return s;
        }
        CardSpec s;s.subject=id;s.title=HelpTitle(id);
        const content::ContentTool* tool=nullptr;
        switch(id){case HelpApply:tool=t.Tool(Kind::Apply);break;case HelpClose:tool=t.Tool(Kind::Close);break;case HelpRecenter:tool=t.Tool(Kind::Recenter);break;
            case HelpCalibrate:tool=t.Tool(Kind::Calibrate);break;case HelpResetPage:tool=t.Tool(Kind::ResetPage);break;case HelpUndoReset:tool=t.Tool(Kind::UndoReset);break;default:break;}
        s.text=tool&&tool->help?tool->help:id==HelpClose?CloseAsksHelp:HelpText(id);
        if(id==HelpCalibrate&&m.Dirty()){s.titleFlags=Locked;s.stateFlags=Locked;s.state="Locked while changes are not applied";}
        if(id==HelpMonoAudio)s.titleFlags=Warn;
        return s;
    }
    void Card(int top){
        const int bottom=RegionEnd();
        const int avail=bottom-top;
        if(avail<CardTitleH+2*CardPad){out.card={ContentX,top,ContentW,std::max(0,avail)};return;}
        CardSpec s=m.confirmClose?ConfirmCard():in.card!=NoHelp?SubjectCard(in.card):IdleCard();
        if(s.title.empty())s=IdleCard();
        const int x=ContentX,w=ContentW,inner=avail-2*CardPad,textX=x+CardPad,fullW=w-2*CardPad;
        const int buttonsN=int(s.buttons.size());
        auto lineCount=[&](const std::string& text,int width){return int(Wrap(text,float(width),draw::TextStyle::Help,measure).size());};
        // Lists (ChangeLine / PendingEntry) in two columns when there are two or more.
        // No room for a 64 px pending entry under the title: plain old -> new lines instead.
        if(!s.entries.empty()&&inner-CardTitleH-(s.state.empty()?0:CardStateH)<CardButtonH){
            std::vector<std::pair<std::string,uint16_t>> lines;
            for(auto& entry:s.entries)lines.push_back({std::move(entry.first),0});
            for(auto& l:s.lines)lines.push_back(std::move(l));
            s.lines=std::move(lines);s.entries.clear();
            s.state="Apply saves them; the marked tabs hold them.";
        }
        const bool entries=!s.entries.empty();
        const int listItems=entries?int(s.entries.size()):int(s.lines.size())+0;
        const int itemH=entries?CardButtonH:30,itemPitch=entries?CardButtonH+MinGap:32;
        // Two columns when every line fits one at the painter's smallest step
        // (Readout, 22 px; the new value is bold: 6% more), else one.
        auto listFits=[&](int colW){
            const int room=entries?colW-32:colW;
            if(entries){for(const auto& e:s.entries)if(W(e.first,draw::TextStyle::Value)*22.f/30.f*1.06f>float(room))return false;}
            else for(const auto& l:s.lines)if(W(l.first,draw::TextStyle::Value)*22.f/30.f*1.06f>float(room))return false;
            return true;
        };
        const int columns=listItems>=2&&listFits((fullW-MinGap)/2)?2:1;   // lists come without card buttons: the full width
        const int listRows=(listItems+columns-1)/columns;
        const int textLines=s.text.empty()?0:lineCount(s.text,fullW);
        const int roomy=CardTitleH+textLines*CardLineH+listRows*itemPitch+(s.state.empty()?0:CardStateH)+(buttonsN?MinGap+CardButtonH:0);
        bool side=buttonsN&&roomy>inner;
        // Side column (8-row pages): the help keeps its width minus the buttons.
        const int textW=side?w-CardPad-16-CardColumnW-CardColumnPad:fullW;
        int y=top+CardPad;
        // Height of the card: what it needs, up to what is free.
        int need=roomy;
        if(side)need=inner;
        const int cardH=std::min(avail,std::max(need+2*CardPad,CardTitleH+2*CardPad+CardLineH));
        out.card={x,top,w,cardH};
        Add(Role::HelpCard,out.card,{Kind::Card},NoHelp,Enabled);
        const int end=top+cardH-CardPad;
        auto& title=Add(Role::CardTitle,{textX,y,textW,CardTitleH},{},NoHelp,s.titleFlags,s.title);(void)title;
        y+=CardTitleH;
        // The side column: buttons (and the state line above a single one).
        bool stateInColumn=false;
        if(side){
            // The column sits CardColumnPad in from the card's edge, its stack
            // centered in the card's height; the state line goes above the
            // buttons when it fits there (it then leaves the help more room).
            const int cx=x+w-CardColumnPad-CardColumnW,half=(CardColumnW-MinGap)/2;
            // The state line fits the column at the painter's smallest step (Readout, 22 px) with its icon.
            auto columnFits=[&](const std::string& text){return !text.empty()&&W(text,draw::TextStyle::Value)*draw::MinFontSize/draw::FontSize(draw::TextStyle::Value)*1.03f+36<=float(CardColumnW);};
            if(!columnFits(s.state)&&columnFits(s.shortState))s.state=s.shortState;
            const bool state=columnFits(s.state);
            std::vector<std::pair<CardButtonSpec,Rect>> placed;
            auto stack=[&](int h){return top+CardPad+std::max(0,(inner-h)/2);};
            auto stateAt=[&](int cy){Add(Role::CardState,{cx,cy,CardColumnW,CardStateH},{},NoHelp,s.stateFlags,s.state);stateInColumn=true;return cy+CardStateH+8;};
            if(buttonsN==1){
                const bool withState=state&&inner>=CardStateH+8+CardButtonH;
                int cy=stack(CardButtonH+(withState?CardStateH+8:0));
                if(withState)cy=stateAt(cy);
                placed.push_back({s.buttons[0],{cx,cy,CardColumnW,CardButtonH}});
            }else if(buttonsN==2){
                const int stacked=2*CardButtonH+MinGap;
                if(state&&inner>=CardStateH+8+stacked){int cy=stateAt(stack(CardStateH+8+stacked));
                    placed.push_back({s.buttons[0],{cx,cy,CardColumnW,CardButtonH}});placed.push_back({s.buttons[1],{cx,cy+CardButtonH+MinGap,CardColumnW,CardButtonH}});}
                else if(state&&inner>=CardStateH+8+CardButtonH){int cy=stateAt(stack(CardStateH+8+CardButtonH));
                    placed.push_back({s.buttons[0],{cx,cy,half,CardButtonH}});placed.push_back({s.buttons[1],{cx+half+MinGap,cy,half,CardButtonH}});}
                else if(inner>=stacked){const int cy=stack(stacked);
                    placed.push_back({s.buttons[0],{cx,cy,CardColumnW,CardButtonH}});placed.push_back({s.buttons[1],{cx,cy+CardButtonH+MinGap,CardColumnW,CardButtonH}});}
                else{const int cy=stack(CardButtonH);
                    placed.push_back({s.buttons[0],{cx,cy,half,CardButtonH}});placed.push_back({s.buttons[1],{cx+half+MinGap,cy,half,CardButtonH}});}
            }else if(inner<2*CardButtonH+MinGap||(state&&inner<CardStateH+8+2*CardButtonH+MinGap&&inner>=CardStateH+8+CardButtonH)){
                // a short card: Feel it, Undo and Default side by side (under the state line when it fits)
                const int third=(CardColumnW-2*MinGap)/3;
                int cy=stack(CardButtonH);
                if(state&&inner>=CardStateH+8+CardButtonH)cy=stateAt(stack(CardStateH+8+CardButtonH));
                for(int b=0;b<3;++b)placed.push_back({s.buttons[size_t(b)],{cx+b*(third+MinGap),cy,third,CardButtonH}});
            }else{   // Feel it over Undo and Default
                const int stacked=2*CardButtonH+MinGap;
                int cy=stack(stacked);
                if(state&&inner>=CardStateH+8+stacked)cy=stateAt(stack(CardStateH+8+stacked));
                placed.push_back({s.buttons[0],{cx,cy,CardColumnW,CardButtonH}});
                placed.push_back({s.buttons[1],{cx,cy+CardButtonH+MinGap,half,CardButtonH}});
                placed.push_back({s.buttons[2],{cx+half+MinGap,cy+CardButtonH+MinGap,half,CardButtonH}});
            }
            for(auto& [b,r]:placed)PlaceButton(b,r,s.subject);
        }
        // Body text: whole sentences that fit (never the first one cut short when it fits alone).
        const int stateH=(s.state.empty()||stateInColumn)?0:CardStateH;
        const int buttonsH=(!side&&buttonsN)?MinGap+CardButtonH:0;
        const int listH=listRows*itemPitch;
        int textSpace=end-y-stateH-buttonsH-std::min(listH,itemPitch);   // keep room for one list row
        if(listItems==0)textSpace=end-y-stateH-buttonsH;
        // The whole help, at the largest size from Help's 30 px down to the
        // 22 px floor that fits (the painter's TextFit picks the same or larger).
        // Too long even then: whole sentences only, never one cut mid-way; the
        // page reserved room for its rows' help (CardNeed), so that is a last resort.
        if(!s.text.empty()){
            int h=HelpHeight(s.text,textW,textSpace);
            std::string text=s.text;
            if(h<0){
                text.clear();
                for(const auto& sentence:Sentences(s.text)){
                    const std::string next=text.empty()?sentence:text+" "+sentence;
                    if(HelpHeight(next,textW,textSpace)<0)break;
                    text=next;
                }
                h=text.empty()?-1:HelpHeight(text,textW,textSpace);
            }
            if(h>0){Add(Role::CardText,{textX,y,textW,h},{},NoHelp,0,text);y+=h;}
        }
        // The list: as many rows as fit; "And n more" in the state line.
        if(listItems){
            const int colW=columns==2?(textW-MinGap)/2:textW;
            const int room=std::max(0,(end-y-stateH-buttonsH+(itemPitch-itemH))/itemPitch);
            const int fitRows=std::min(listRows,room);
            const int shown=std::min(listItems,fitRows*columns);
            for(int i=0;i<shown;++i){
                const Rect r{textX+(i%columns)*(colW+MinGap),y+(i/columns)*itemPitch,colW,itemH};
                if(entries)Add(Role::PendingEntry,r,s.entries[size_t(i)].second,s.subject,uint16_t((Armed()?Enabled:0)|Changed),s.entries[size_t(i)].first);
                else Add(Role::ChangeLine,r,{},s.subject,s.lines[size_t(i)].second,s.lines[size_t(i)].first);
            }
            y+=fitRows*itemPitch;
            if(shown<listItems)s.state="And "+std::to_string(listItems-shown)+" more. "+s.state;
            if(entries&&!s.lines.empty()&&shown==listItems){   // hidden-class drafts under the entries, as one line
                std::string hidden;for(const auto& l:s.lines)hidden+=(hidden.empty()?"":"; ")+l.first;
                if(end-y-stateH-buttonsH>=CardLineH&&lineCount(hidden,textW)==1){Add(Role::ChangeLine,{textX,y,textW,30},{},s.subject,0,hidden);y+=32;}
            }
        }
        if(stateH&&end-y-buttonsH>=CardStateH){Add(Role::CardState,{textX,y,textW,CardStateH},{},NoHelp,s.stateFlags,s.state);y+=CardStateH;}
        if(!side&&buttonsN){
            // Centered row under the state line.
            const int n=std::min(buttonsN,3);
            const int bw=std::min(CardColumnW,(fullW-(n-1)*MinGap)/n);
            int bx=x+(w-(n*bw+(n-1)*MinGap))/2;const int by=end-CardButtonH;
            for(int i=0;i<n;++i){PlaceButton(s.buttons[size_t(i)],{bx,by,bw,CardButtonH},s.subject);bx+=bw+MinGap;}
        }
    }
    void PlaceButton(const CardButtonSpec& b,Rect r,int subject){
        // The longest name that fits the button with room around it (icon +
        // padding take 92 px; a narrow button has no icon and keeps 24 px each side).
        std::string name=b.names.back();
        for(const auto& option:b.names)if(W(option,draw::TextStyle::Value)<=float(r.w-(r.w>=200?92:48))){name=option;break;}
        auto& e=Add(Role::CardButton,r,b.hit,subject,uint16_t(b.enabled?Enabled:0),name);
        e.icon=r.w>=200?b.icon:draw::Icon::None;e.option=b.action;
        if(b.hit.kind==Kind::Undo||b.hit.kind==Kind::Default)e.key=Key(b.hit.index);
    }

    // ---- footer ----
    void Footer(){
        const int dirty=int(DirtyKeys(m).size());
        if(const char* status=Status())Add(Role::StatusStrip,{ContentX,StatusY,ContentW,StatusH},{},NoHelp,m.result==Result::Failed||m.result==Result::Conflict?Warn:0,status);
        if(m.confirmClose){
            const bool armed=in.nowMs>=m.confirmAt+Model::ConfirmGuardMs;
            auto& a=Add(Role::ConfirmDiscard,FooterLeft,{Kind::ConfirmClose},HelpDiscard,uint16_t(armed?Enabled:0),"Discard "+Plural(dirty,"change","changes"));a.icon=draw::Icon::Close;
            Add(Role::ConfirmKeep,FooterMid,{Kind::KeepEditing},HelpKeepEditing,uint16_t(armed?Enabled:0),"Keep editing");
            auto& c2=Add(Role::ConfirmApply,FooterRight,{Kind::ApplyClose},HelpApplyClose,uint16_t(armed&&!blocked?Enabled:0),"Apply and close");c2.icon=draw::Icon::Check;
            return;
        }
        const bool page=content::PageOf(m.view).settings;
        if(page){
            if(in.nowMs<m.undoResetUntil){
                auto& u=Add(Role::FooterResetPage,FooterLeft,{Kind::UndoReset},HelpUndoReset,uint16_t(!blocked&&in.nowMs>=m.undoResetAt+Model::UndoResetGuardMs?Enabled:0),"Undo reset");
                u.icon=draw::Icon::Undo;
                auto& bar=Add(Role::DrainBar,{FooterLeft.x,FooterLeft.y+FooterLeft.h+1,FooterLeft.w,DrainH});
                bar.fill=std::clamp(float(m.undoResetUntil-in.nowMs)/float(Model::UndoResetMs),0.f,1.f);
            }else{
                bool resettable=false;
                const ViewRows rows=RowsOf(m.view,m.Get(),c,t,MoreSkip(m,t));
                for(int i=0;i<rows.count;++i){
                    const Key k=rows.keys[size_t(i)];const auto* r=t.Row(k);
                    if(r->kind!=content::RowKind::Button&&r->kind!=content::RowKind::Offer&&Editable(m,k)&&!Same(m.Get()[k],ClassValue(k,Definitions[k].defaultValue,m.Get()[k],c)))resettable=true;
                }
                auto& r=Add(Role::FooterResetPage,FooterLeft,{Kind::ResetPage},HelpResetPage,uint16_t(resettable&&!blocked?Enabled:0),"Reset page");r.icon=draw::Icon::Reset;
            }
        }
        auto& close=Add(Role::FooterClose,FooterMid,{Kind::Close},HelpClose,Enabled,"Close");close.icon=draw::Icon::Close;
        if(page||m.view==View::Presets||dirty){
            // "Apply 3 changes"; a count too wide for the plate (bold, beside the check) drops the word.
            std::string words=dirty?"Apply "+Plural(dirty,"change","changes"):"No changes";
            if(dirty&&W(words,draw::TextStyle::Value)*1.06f+72>float(FooterRight.w))words="Apply "+std::to_string(dirty);
            auto& apply=Add(Role::FooterApply,FooterRight,{Kind::Apply},HelpApply,uint16_t((dirty&&!blocked?Enabled:0)|(dirty?Changed:0)),
                m.result==Result::Saving?"Saving...":words);
            apply.icon=draw::Icon::Check;
        }
    }
    // ---- body calibration: replaces the panel ----
    void Calibration(){
        const auto& cv=in.calibration;
        Add(Role::PageTitle,{60,48,1160,65},{},NoHelp,0,"Calibrate your body");
        Add(Role::CalibStep,{80,153,1120,60},{},NoHelp,0,"1. Stand upright and look straight ahead.");
        Add(Role::CalibStep,{80,238,1120,100},{},NoHelp,0,"2. Stretch both arms out sideways at shoulder height (T-pose).");
        Add(Role::CalibStep,{80,348,1120,100},{},NoHelp,0,"3. Release both triggers, then squeeze and hold both for a moment.");
        Add(Role::CalibHint,{80,493,1120,160},{},NoHelp,0,HintText(cv.hint));
        auto& bar=Add(Role::CalibProgress,{110,663,1060,13});bar.fill=std::clamp(cv.progress,0.f,1.f);
        if(cv.measured)Add(Role::CalibNote,{80,744,1120,55},{},NoHelp,0,Printf("Standing eye height: %.1f cm     Controller span: %.1f cm",double(cv.eyeHeight*100),double(cv.armSpan*100)));
        else Add(Role::CalibNote,{80,744,1120,65},{},NoHelp,0,"Height sets world scale. Controller span fits your character's arms.");
        Add(Role::CalibNote,{80,846,1120,80},{},NoHelp,0,"Right B: skip / cancel. Reopen any time: VR Settings > Body > Calibrate body.");
        Add(Role::CalibNote,{80,939,1120,50},{},NoHelp,0,"Controls are blocked. The game world remains live.");
    }
    void Flags(){
        Key pointed=Count;
        for(auto& e:out.elements){
            if(e.hit.kind==Kind::None)continue;
            if(in.hover.kind!=Kind::None&&e.hit==in.hover){e.flags|=Hovered;if(e.key!=Count)pointed=e.key;}
            if(in.pressed.kind!=Kind::None&&e.hit==in.pressed)e.flags|=Pressed;
        }
        for(auto& e:out.elements)if(e.role==Role::Row&&(e.key==pointed||(in.card==int(e.key)&&in.card!=NoHelp)))e.flags|=Hovered;
    }
    // ---- the popover: a named list too long for chips, over the page ----
    // A floating card beside the row it edits, never over that row: below it,
    // or above it when it would reach the footer. The rest of the panel is
    // dimmed and an outside press closes it without a change (PopoverClose);
    // one press on an option picks it and closes. Options take presses
    // Model::PopoverGuardMs after it opened. Each option's hit box is its whole
    // cell; neighbors meet halfway across the gap.
    void Popover(){
        const Key k=Key(m.popoverKey);const auto* r=t.Row(k);
        if(!r||out.popoverRow.w<=0)return;
        if(r->kind==content::RowKind::Swatch){ColorPopover(*r);return;}
        const float v=m.Get()[k];const auto& d=Definitions[k];
        const auto options=OptionsOf(*r,v,c);const int n=int(options.size());
        if(n<1)return;
        const bool armed=in.nowMs>=m.popoverAt+Model::PopoverGuardMs&&Editable(m,k);
        // Columns: one up to PopListMax (two when that would pass PopMaxH); a grid of 2 up to 12, else 3.
        int cellP=PopCell,titleH=PopTitleH,hintH=PopHintH;   // compact (PopCellMin cells, no title) when it fits neither below nor above its row
        auto heightFor=[&](int columns){return PopPad*2+titleH+(n+columns-1)/columns*cellP+hintH;};
        int columns=n<=PopListMax?1:n<=12?2:3;
        if(columns==1&&heightFor(1)>PopMaxH)columns=2;
        if(!PopoverFits(heightFor(columns)))titleH=0;   // its row is right beside it: the title goes first
        if(!PopoverFits(heightFor(columns))){cellP=PopCellMin;hintH=PopHintCompact;}
        float widest=0;for(const auto& o:options)widest=std::max(widest,W(o.label,draw::TextStyle::Strong));
        const int cellW=std::max(180,int(std::ceil(widest))+ChipPad+16);
        const int w=std::clamp(columns*cellW+2*PopPad,PopMinW,ContentW),h=heightFor(columns);
        const int colW=(w-2*PopPad)/columns;
        const Rect card{ContentRight-w,PopoverTop(h),w,h};
        out.popover=card;
        Add(Role::PopoverDim,PanelInner,{Kind::PopoverClose},k,Enabled);   // its help: the row's, while the popover is open
        Add(Role::Popover,card,{Kind::Card},k,Enabled);
        if(titleH)Add(Role::PopoverTitle,{card.x+PopPad,card.y+PopPad,card.w-2*PopPad,titleH},{},k,0,LabelOf(*r,c));
        const int top=card.y+PopPad+titleH;const int drawnH=cellP-PopCellGap;
        int pointed=-1;
        for(int i=0;i<n;++i){
            const int col=i%columns,rowIndex=i/columns;
            const Rect cell{card.x+PopPad+col*colW,top+rowIndex*cellP,colW,cellP};
            const Rect drawn{cell.x+PopCellGap/2,cell.y+PopCellGap/2,cell.w-PopCellGap,drawnH};
            const bool chosen=Same(options[size_t(i)].value,v);
            const bool isDefault=SpellLockOnOff(k,c)?(int(options[size_t(i)].value)&1)==(int(d.defaultValue)&1):Same(options[size_t(i)].value,d.defaultValue);
            const Hit hit{Kind::Set,k,0,options[size_t(i)].value};
            auto& e=Add(Role::PopoverOption,drawn,hit,k,uint16_t((armed?Enabled:0)|(chosen?Selected:0)|(isDefault?IsDefault:0)|(chosen&&Dirty(k)?Changed:0)),options[size_t(i)].label);
            e.key=k;e.option=i;e.hitRect=cell;
            if(in.hover==hit)pointed=i;
        }
        const int shown=pointed>=0?pointed:OptionIndex(options,v);
        const char* hint=shown>=0&&options[size_t(shown)].hint?options[size_t(shown)].hint:"";
        Add(Role::PopoverHint,{card.x+PopPad,card.y+card.h-PopPad-hintH,card.w-2*PopPad,hintH},{},k,0,hint);
    }
    // Where a popover card of height h goes: below its row, else above it,
    // else on the side with more room (inside the content area either way).
    bool PopoverFits(int h)const{const Rect row=out.popoverRow;return row.y+row.h+PopGap+h<=RegionEnd()||row.y-PopGap-h>=ViewTop;}
    int PopoverTop(int h)const{
        const Rect row=out.popoverRow;
        const int below=row.y+row.h+PopGap,above=row.y-PopGap-h,bottom=RegionEnd();
        return below+h<=bottom?below:above>=ViewTop?above:(bottom-below>row.y-ViewTop?bottom-h:ViewTop);
    }
    // The color grid (a hue row): named values outside the hue wheel (By
    // range, Off) as full-width chips above, then every hue as a dot, 6 a row (8 when
    // compact)
    // in hue order (a quarter of the wheel a row: Red ... Crimson), the dot
    // colored as the game draws the outline (HueRgb). The chosen one ringed.
    void ColorPopover(const content::ContentRow& r){
        const Key k=r.key;const float v=m.Get()[k];const auto& d=Definitions[k];
        const bool armed=in.nowMs>=m.popoverAt+Model::PopoverGuardMs&&Editable(m,k);
        std::vector<std::pair<float,const char*>> specials;std::vector<float> hues;
        const int steps=int(std::lround((d.max-d.min)/d.step));
        for(int i=0;i<=steps;++i){
            const float value=d.min+float(i)*d.step;const char* name=nullptr;
            for(int n=0;n<r.nameCount;++n)if(Same(r.names[n].value,value))name=r.names[n].label;
            if(name)specials.push_back({value,name});else hues.push_back(value);
        }
        std::stable_sort(hues.begin(),hues.end(),[](float a,float b){return std::fmod(a,360.f)<std::fmod(b,360.f);});
        int cellP=PopCell,titleH=PopTitleH,hintH=PopHintColor,dotD=46,columns=6;
        auto rowsOf=[&]{return int((hues.size()+size_t(columns)-1)/size_t(columns));};
        auto heightFor=[&]{return PopPad*2+titleH+(specials.empty()?0:cellP)+rowsOf()*cellP+hintH;};
        if(!PopoverFits(heightFor()))titleH=0;   // its row is right beside it: the title goes first
        if(!PopoverFits(heightFor())){cellP=PopCellMin;dotD=44;columns=8;}
        const int rows=rowsOf();(void)rows;
        const int gridW=columns*cellP;
        const int w=std::max(PopMinW,gridW+2*PopPad),h=PopPad*2+titleH+(specials.empty()?0:cellP)+rows*cellP+hintH;
        const Rect card{ContentRight-w,PopoverTop(h),w,h};
        out.popover=card;
        Add(Role::PopoverDim,PanelInner,{Kind::PopoverClose},k,Enabled);
        Add(Role::Popover,card,{Kind::Card},k,Enabled);
        if(titleH)Add(Role::PopoverTitle,{card.x+PopPad,card.y+PopPad,card.w-2*PopPad,titleH},{},k,0,LabelOf(r,c));
        int y=card.y+PopPad+titleH;const int x0=card.x+(card.w-gridW)/2;
        std::string hint;
        auto flags=[&](float value){return uint16_t((armed?Enabled:0)|(Same(value,v)?Selected:0)|(Same(value,d.defaultValue)?IsDefault:0)|(Same(value,v)&&Dirty(k)?Changed:0));};
        if(!specials.empty()){
            const int cw=gridW/int(specials.size());
            for(size_t i=0;i<specials.size();++i){
                const Rect cell{x0+int(i)*cw,y,cw,cellP};
                const Hit hit{Kind::Set,k,0,specials[i].first};
                auto& e=Add(Role::PopoverOption,{cell.x+PopCellGap/2,cell.y+PopCellGap/2,cell.w-PopCellGap,cellP-PopCellGap},hit,k,flags(specials[i].first),specials[i].second);
                e.key=k;e.hitRect=cell;e.hue=SwatchHue(r,specials[i].first);
                if(in.hover==hit||(hint.empty()&&Same(specials[i].first,v)))hint=SwatchHue(r,specials[i].first)==draw::SwatchByRange?"By range: red near, orange mid, yellow far":specials[i].second;
            }
            y+=cellP;
        }
        for(size_t i=0;i<hues.size();++i){
            const Rect cell{x0+int(i%size_t(columns))*cellP,y+int(i/size_t(columns))*cellP,cellP,cellP};
            const Hit hit{Kind::Set,k,0,hues[i]};
            auto& e=Add(Role::ColorDot,{cell.x+(cellP-dotD)/2,cell.y+(cellP-dotD)/2,dotD,dotD},hit,k,flags(hues[i]));
            e.key=k;e.hitRect=cell;e.hue=hues[i];
            if(in.hover==hit)hint=content::HueNameOf(hues[i]);
        }
        if(hint.empty())hint=std::string("Now: ")+ValueOf(r,v,c);
        Add(Role::PopoverHint,{card.x+PopPad,card.y+card.h-PopPad-hintH,card.w-2*PopPad,hintH},{},k,0,hint);
    }
    void Build(){
        out.view=m.view;
        if(in.calibration.active){Calibration();return;}
        Rail();Band();
        int y=ViewTop;
        switch(m.view){
            case View::Presets:y=Presets();break;
            case View::Guide:y=Guide();break;
            case View::WhatsNew:y=Notes();break;
            default:y=Page();break;
        }
        Card(y+4);
        Footer();
        if(m.popoverKey>=0)Popover();
        Flags();
    }
};
}

// The whole panel for this frame: every element in paint order and the card.
inline Layout Build(const Model& model,const Context& context,const LayoutInput& input,const TextMeasure& measure={},const Tables& tables=ContentTables()){
    detail::Builder b{model,context,input,tables,measure,{},Blocked(model)};
    b.Build();
    return std::move(b.out);
}
// The top-most pressable element under (x, y) in canvas pixels, or nullptr.
inline const Element* ElementAt(const Layout& layout,float x,float y){
    for(auto it=layout.elements.rbegin();it!=layout.elements.rend();++it)
        if(it->hit.kind!=Kind::None&&it->HitBox().Contains(x,y))return &*it;
    return nullptr;
}
// What a press at (x, y) does (Kind::None: nothing).
inline Hit HitTest(const Layout& layout,float x,float y){
    const Element* e=ElementAt(layout,x,y);
    return e&&e->Has(Enabled)?e->hit:Hit{};
}
// The help subject at (x, y): the top-most element with help, or NoHelp.
inline int HelpAt(const Layout& layout,float x,float y){
    for(auto it=layout.elements.rbegin();it!=layout.elements.rend();++it)
        if(it->help!=NoHelp&&it->rect.Contains(x,y))return it->help;
    return NoHelp;
}
// The element a hit names (for the pointer's hysteresis rect), or nullptr.
inline const Element* Find(const Layout& layout,const Hit& hit){
    if(hit.kind==Kind::None)return nullptr;
    for(const auto& e:layout.elements)if(e.hit==hit)return &e;
    return nullptr;
}

// ---- presses ----
// One setting of the draft, with the class rule (Spells go to the aimed
// monster keeps a saved bow-skills bit for a class without bow skills).
inline bool SetValue(Model& m,Key key,float value){return m.Set(key,ClassValue(key,value,m.Get()[key],m.context));}
// A stepper, cycler or swatch press: n steps (from a hold or a drag).
inline Request Step(Model& m,const Hit& hit,const Tables& t){
    const Key k=Key(hit.index);
    if(!Editable(m,k)||hit.delta==0)return {};
    const auto* r=t.Row(k);
    const float v=m.Get()[k];
    if(r&&!hooks::ModelSteps(k)&&r->optionCount>0){   // walk the options in their shown order
        const auto options=OptionsOf(*r,v,m.context);const int n=int(options.size());
        int i=OptionIndex(options,v);if(i<0)i=0;
        int next=i+hit.delta;
        next=WrapsRound(*r)?((next%n)+n)%n:std::clamp(next,0,n-1);
        if(next!=i)SetValue(m,k,options[size_t(next)].value);
        return {};
    }
    if(r&&!hooks::ModelSteps(k)&&WrapsRound(*r)){   // a hand color goes round through By range (the arrow path line wraps through Off in Model::Click)
        const auto& d=Definitions[k];const float range=d.max-d.min+d.step;
        float next=v+float(hit.delta)*d.step;
        while(next>d.max+.001f)next-=range;
        while(next<d.min-.001f)next+=range;
        SetValue(m,k,next);
        return {};
    }
    return m.Click(hit);   // Normalize's steps, OfflinePlayers' 0-8, LockStep, the laser's wheel through Off
}
// A preset: sets only the keys it lists, for this character (PresetSets).
// Press calls it only while content::PresetsReady; the tests call it so the
// preset logic stays checked while presets are off.
inline void ApplyPreset(Model& model,int index,const Tables& t=ContentTables()){
    if(Blocked(model)||index<0||size_t(index)>=t.presetCount)return;
    const auto& p=t.presets[index];
    for(int i=0;i<p.count;++i){
        const Key k=p.values[i].key;
        if(PresetSets(k,model.context,t))SetValue(model,k,p.values[i].value);
    }
    model.pickedPreset=index;++model.revision;
}
// A press: the new kinds change the model here; the old ones go to
// Model::Click. Returns the save request Apply makes (sequence 0: none).
// Guards: the confirm row ignores presses for Model::ConfirmGuardMs after it
// appears, Undo reset for UndoResetGuardMs after Reset page and after
// UndoResetMs it is gone. TestHaptics, Calibrate and Recenter change nothing
// here (the caller runs them; Calibrate only while Model::CanCalibrate).
inline Request Press(Model& model,const Hit& hit,uint64_t nowMs,const Tables& t=ContentTables()){
    if(!model.open)return {};
    const bool confirmArmed=model.confirmClose&&nowMs>=model.confirmAt+Model::ConfirmGuardMs;
    const bool keyOk=hit.index>=0&&hit.index<Count;
    // An open popover is modal: Right B (Close) and an outside press close it
    // without a change; a pick sets its row and closes it. Nothing else reaches the page.
    if(model.popoverKey>=0){
        const bool pick=hit.kind==Kind::Set&&hit.index==model.popoverKey;
        if(!pick&&hit.kind!=Kind::Card&&hit.kind!=Kind::TestHaptics){model.popoverKey=-1;++model.revision;return {};}
        if(pick){
            if(nowMs<model.popoverAt+Model::PopoverGuardMs||!Editable(model,Key(hit.index)))return {};
            SetValue(model,Key(hit.index),hit.value);model.popoverKey=-1;++model.revision;return {};
        }
    }
    switch(hit.kind){
        case Kind::Open:{
            if(hit.index<0||hit.index>=int(View::Count))return {};
            View v=View(hit.index);
            if(v==View::Presets&&!content::PresetsReady)return {};   // grayed out until content::PresetsReady
            if(v==model.view&&(v==View::Guide||v==View::WhatsNew))v=model.lastPage;   // the header link again: back to the page
            if(content::PageOf(v).settings)model.lastPage=v;
            model.view=v;model.focusKey=hit.delta>=0&&hit.delta<Count?hit.delta:-1;model.popoverKey=-1;
            if(v==View::More){   // More opens on the page that holds the row it was sent to
                model.morePage=0;int index=0;
                for(size_t i=0;i<t.rowCount&&model.focusKey>=0;++i){
                    const auto& r=t.rows[i];
                    if(r.page!=View::More||!RowVisible(r,model.Get(),model.context))continue;
                    if(r.key==model.focusKey){model.morePage=index/MaxViewRows;break;}
                    ++index;
                }
            }
            ++model.revision;
            return {};
        }
        case Kind::NotesPage:
            if(model.view==View::More){   // the More view's pager; What's new's is Model::Click's
                const int next=std::clamp(model.morePage+(hit.delta<0?-1:1),0,MorePages(model.Get(),model.context,t)-1);
                if(next!=model.morePage){model.morePage=next;++model.revision;}
                return {};
            }
            return model.Click(hit);
        case Kind::Set:
            if(!keyOk||!Editable(model,Key(hit.index)))return {};
            if(const auto* r=t.Row(Key(hit.index));r&&r->kind==content::RowKind::Offer&&hit.value<.5f)return {};   // one way: an offer only turns its setting on
            SetValue(model,Key(hit.index),hit.value);return {};
        case Kind::Change:return keyOk?Step(model,hit,t):Request{};
        case Kind::Undo:
            if(keyOk&&!Blocked(model))model.Set(Key(hit.index),model.Applied()[hit.index]);
            return {};
        case Kind::Default:
            if(keyOk&&!(t.Row(Key(hit.index))&&t.Row(Key(hit.index))->kind==content::RowKind::Offer)&&Editable(model,Key(hit.index)))SetValue(model,Key(hit.index),Definitions[hit.index].defaultValue);
            return {};
        case Kind::Preset:   // no preset applies until content::PresetsReady
            if(content::PresetsReady)ApplyPreset(model,hit.index,t);
            return {};
        case Kind::ResetPage:{
            if(Blocked(model)||!content::PageOf(model.view).settings)return {};
            const Values before=model.Get();ChangeMask changed=0;
            const ViewRows rows=RowsOf(model.view,model.Get(),model.context,t,MoreSkip(model,t));
            for(int i=0;i<rows.count;++i){
                const Key k=rows.keys[size_t(i)];const auto* r=t.Row(k);
                if(r->kind==content::RowKind::Button||r->kind==content::RowKind::Offer||!Editable(model,k))continue;
                if(SetValue(model,k,Definitions[k].defaultValue))changed|=ChangeMask{1}<<k;
            }
            if(changed){model.beforeReset=before;model.resetKeys=changed;model.undoResetAt=nowMs;model.undoResetUntil=nowMs+Model::UndoResetMs;++model.revision;}
            return {};
        }
        case Kind::UndoReset:{
            if(Blocked(model)||nowMs<model.undoResetAt+Model::UndoResetGuardMs||nowMs>=model.undoResetUntil)return {};
            for(int k=0;k<Count;++k)if(model.resetKeys&(ChangeMask{1}<<k))model.Set(Key(k),model.beforeReset[k]);
            model.undoResetUntil=0;model.resetKeys=0;++model.revision;
            return {};
        }
        case Kind::Close:
            if(model.confirmClose){if(confirmArmed){model.confirmClose=false;++model.revision;}return {};}   // Close again (Right B): Keep editing
            if(model.Dirty()){model.confirmClose=true;model.confirmAt=nowMs;++model.revision;return {};}
            return model.Click(hit);
        case Kind::ConfirmClose:
            if(!confirmArmed)return {};
            model.Discard();model.Close();return {};
        case Kind::KeepEditing:
            if(!confirmArmed)return {};
            model.confirmClose=false;++model.revision;return {};
        case Kind::ApplyClose:{
            if(!confirmArmed)return {};
            if(!model.Dirty()){model.Close();return {};}
            const Request request=model.Click({Kind::Apply});
            if(request.sequence){model.confirmClose=false;model.closeWhenSaved=true;}
            return request;
        }
        case Kind::Popover:   // a named list's value: open its popover (on the release of the press)
            if(keyOk&&Editable(model,Key(hit.index))){model.popoverKey=hit.index;model.popoverAt=nowMs;++model.revision;}
            return {};
        case Kind::PopoverClose:return {};   // nothing open: nothing to close
        case Kind::Card:return {};
        default:return model.Click(hit);
    }
}
}
