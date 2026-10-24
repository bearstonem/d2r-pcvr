#pragma once
// VR Settings drawing layer: Direct2D on a software (WIC) bitmap plus
// DirectWrite with the menu's embedded fonts (Cinzel, Alegreya Sans; SIL OFL).
// Runs without a GPU device, so the settings worker thread can paint with it.
//
// Files: vr_settings_draw.{h,cpp}, assets, vr_settings_assets.rc. The page
// painter (vr_settings_ui.cpp) draws only through this API: it knows roles
// and states, never Direct2D, brushes or hex colors. The look is the
// redesign's Horadric theme: bronze frame, dark stone, crimson and gold.
// No Windows header here, so tests and the layout can include it.
//
// How the page painter paints a frame (one call per layout Role; every control draws its own
// text, icon, state, ring and default mark):
//   Begin(); Panel();                          stone, frame, gems, cartouche, rail (cached bitmap)
//   Tab / PageTitle / Sigil / HeaderLink       rail and band
//   SectionHeading / Row / Label               rows (Row first: the pointed slab and the carved divider)
//   Socket (Chip) / Toggle / StepButton / ValueBox (Value + its Slider fields) / Swatch / Button
//   Card + Text / TextFit / Spans / IconText   the help card (CardTitle, Help, Strong styles)
//   PresetCard / ReadoutBox / DrainBar / Pill
//   End()                                      straight RGBA, CanvasWidth x CanvasHeight
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace framevr::settings::draw {
constexpr int CanvasWidth=1280,CanvasHeight=1024;
// Fixed panel geometry the cached background bakes in: the cast
// frame band, the rail's right edge (its bronze seam is the last 6 px).
constexpr float FrameWidth=22.f,RailRight=300.f,RailSeam=6.f;

// ---- redesign API: value types ----

// A rectangle in canvas pixels (top-left origin, y down).
struct RectF {
    float x=0,y=0,w=0,h=0;
    float Right()const{return x+w;}
    float Bottom()const{return y+h;}
};

// Theme colors as named tokens (the Horadric theme). The painter names a
// role, never a hex value.
enum class Color : uint8_t {
    Stone,       // #1A1612 panel base under the stone texture
    Rail,        // #110E0B tab rail
    Slab,        // #2A2119 the pointed row's slab / pointed preset card
    Socket,      // #0F0C0A empty socket
    IdleFill,    // #14100C idle control fill
    CarveLight,  // #3A3022 row divider, light half
    CarveDark,   // #0A0807 row divider, dark half
    Ink,         // #070504 outlines and bevel shadows
    FrameA,      // #4A3820 cast bronze frame gradient: dark
    FrameB,      // #A07D47 mid
    FrameC,      // #5A4426 end
    FrameHi,     // #E8C77E frame highlight edge
    Bronze,      // #8D6C3F idle control edge, default mark, stepper fill
    BronzeHi,    // #DDB56C gem bezel, pointed ring (--ring)
    Text,        // #FFF6E3 labels and values (D2 normal white)
    Quiet,       // #CCC3B0 sub lines, idle tab names
    Dim,         // #949494 disabled text
    EdgeOff,     // #7D7364 disabled control edge
    Section,     // #C9A262 section headings
    Gold,        // #C7B377 On / chosen / custom (D2 unique gold), active tab
    GoldHi,      // #E4D49C gold highlight; page title fill
    SelRim,      // #C7B377 chosen socket rim
    SelGlint,    // #F6E2A8 chosen socket top glint
    RingHi,      // #F2DFA6 pointed ring, 3 px outside the control
    Crimson1,    // #7A1712 chosen socket gradient top (--sel1)
    Crimson2,    // #5E120E chosen socket gradient bottom (--sel2)
    Magic,       // #AFB7FF help card body line, links (D2 magic blue)
    Warn,        // #F0E06A warn line under a label (D2 rare yellow)
    Ok,          // #4ED24E "Changes applied" (D2 set green)
    Changed,     // #FFA800 changed, not applied: bar, tab diamond, Apply count (D2 crafted)
    Danger,      // #FF6A5A Discard, "Needs ..." reasons
    Comfort,     // #FF5A4A comfort lines in the preset preview
    Tip,         // #090706 help card field (drawn at 86%)
    TipBody,     // #F0F0F0 help card body text
    GemTop,      // #C23A2B corner gem highlight
    Count
};
// 0xRRGGBB for a token (Horadric theme). B may tune values, never the names.
constexpr uint32_t Rgb(Color c){
    constexpr uint32_t table[]={0x1A1612,0x110E0B,0x2A2119,0x0F0C0A,0x14100C,0x3A3022,0x0A0807,0x070504,0x4A3820,0xA07D47,0x5A4426,0xE8C77E,
        0x8D6C3F,0xDDB56C,0xFFF6E3,0xCCC3B0,0x949494,0x7D7364,0xC9A262,0xC7B377,0xE4D49C,0xC7B377,0xF6E2A8,0xF2DFA6,0x7A1712,0x5E120E,
        0xAFB7FF,0xF0E06A,0x4ED24E,0xFFA800,0xFF6A5A,0xFF5A4A,0x090706,0xF0F0F0,0xC23A2B};
    static_assert(sizeof table/sizeof table[0]==size_t(Color::Count),"one value per color token");
    return table[size_t(c)];
}

// Text styles (nothing under 22 px). Every style turns
// on lining figures (lnum) and draws a 2 px black shadow.
enum class TextStyle : uint8_t {
    Title,       // Cinzel 700 44 px, 0.06em: the page title (PageTitle draws it cast gold)
    Section,     // Cinzel 700 30 px, 0.10em: section heading between bronze rules
    CardTitle,   // Cinzel 700 34 px, 0.02em: the help card's title, preset names
    Label,       // Alegreya Sans 500 36 px: row labels
    Value,       // Alegreya Sans 500 30 px: values, chips, buttons, sub lines, state lines
    Help,        // Alegreya Sans 500 30 px: help card body (may wrap; 1.1 line height)
    Small,       // Alegreya Sans 500 26 px: "1 of 4", class tags, pills
    Readout,     // Alegreya Sans 500 22 px: the Performance readout lines
    Strong,      // Alegreya Sans 700 30 px: the card's blue property line, old -> new values
    Count
};
// The style's font size in px (fit helpers shrink from here).
constexpr float FontSize(TextStyle s){
    constexpr float sizes[]={44,30,34,36,30,30,26,22,30};
    static_assert(sizeof sizes/sizeof sizes[0]==size_t(TextStyle::Count),"one size per text style");
    return sizes[size_t(s)];
}
constexpr float MinFontSize=22.f;   // the fit helpers never go below this
// Swatch hues besides 0-360: Off (a slashed ring), By range (the outline's
// red-orange-yellow range gradient) and none (no swatch in a value box).
constexpr float SwatchOff=-1.f,SwatchNone=-2.f,SwatchByRange=-3.f;
enum class Align : uint8_t { Leading, Center, Trailing };

// Icons: the menu's own 48 x 48 silhouettes (assets/menu/icons-v2.inc), kept
// as Direct2D path geometries and tinted at draw time. Names follow its keys.
enum class Icon : uint8_t {
    None,
    // pages and tools
    Presets, Body, Combat, Aim, Targets, Shield, Wrist, Interface, Survival, World, Haptics, Performance, Controls, Notes,
    // presets
    Comfort, Classic, FullVr, Fast,
    // controls and states
    Plus, Minus, Chevron, ChevronLeft, ChevronUp, ChevronDown, Check, Close, Reset, Undo, Default, Feel,
    Padlock, Warning, Info, Changed, Star, StarOutline, Search, SoundOn, SoundOff,
    Count
};

// How a control looks. Pointed: 3 px RingHi ring outside it.
// Pressed: drawn 2 px down with a dark 6 px inset band. Chosen: crimson socket,
// gold rim and top glint. Locked: unlit socket, dashed gray ring when pointed,
// padlock, Dim text, still shows the real value. Disabled: the locked look
// without the padlock (an end stop, "No changes"). Changed: not applied (an
// ember on the control; Label and Tab put it beside the name). IsDefault: the
// bronze triangle hanging under the control.
struct ControlState {
    bool pointed=false,pressed=false,chosen=false,locked=false,changed=false,isDefault=false,disabled=false;
};
// A plate for Button: the idle socket, Apply's cast-bronze plate, or Discard's red rim.
enum class Plate : uint8_t { Socket, Primary, Danger };
// One run of a Spans line ("Shield roll: 0° → +90°" with the new value orange and bold).
struct Span {
    std::string_view text;
    Color color=Color::TipBody;
    bool strong=false;
};

// A measured text block (DirectWrite metrics), for fitting and the fit tests.
struct TextSize {
    float width=0,height=0;
    int lines=0;
    bool fits=true;   // false: it needed more than the box (wrap off: wider; wrap on: taller)
    float size=0;     // the font size used (the fit helpers may shrink it)
};

// One painter per thread: it initializes COM on the thread that creates it and
// must be used and destroyed there. Creation loads the embedded fonts.
class Painter {
public:
    Painter();
    ~Painter();
    Painter(const Painter&)=delete;
    Painter& operator=(const Painter&)=delete;
    bool Ready() const;
    const std::string& Error() const;
    const std::string& FontInfo() const;   // families and matched weights

    // A frame: Begin clears the canvas to transparent, End finishes and returns
    // CanvasWidth x CanvasHeight straight RGBA (empty on failure; Error says why).
    // Every draw call below must sit between Begin and End.
    bool Begin();
    std::vector<uint8_t> End();
    // The whole panel: stone (30%) and vignette, the rail (Rail + 40% stone and
    // its bronze seam up to RailRight), the 22 px cast-bronze frame, the corner
    // brackets with their gems and the Horadric cartouche in the top frame.
    // Built once and cached as a bitmap; each call is one blit. The panel is
    // opaque to its edges (the corner brackets fill the corners).
    void Panel();
    // Stone texture over a rect (rail: Color::Rail at 40%; content: 30%).
    void Stone(RectF r,Color base,float textureOpacity);
    // Flat shapes and lines.
    void Fill(RectF r,Color c,float alpha=1.f);
    void Line(float x0,float y0,float x1,float y1,Color c,float width=1.f);
    void Outline(RectF r,Color c,float width=1.f,bool dashed=false);
    // A carved row divider: CarveDark 1 px over CarveLight 2 px, ending at y (the row's bottom).
    void Divider(float x0,float x1,float y);
    // A section heading: Cinzel caps centered in r between bronze rules with diamond ends.
    void SectionHeading(RectF r,std::string_view utf8);
    // Text in a box, UTF-8. One line is centered vertically in the box; wrap
    // word-wraps to the box width from the top (1.1 line height). Never
    // ellipsized or clipped: the layout must have measured it to fit.
    TextSize Text(std::string_view utf8,TextStyle style,RectF box,Color c,Align align=Align::Leading,bool wrap=false);
    // Measure without drawing (also for the layout's fit and the tests).
    // size>0 measures at that font size instead of the style's.
    TextSize Measure(std::string_view utf8,TextStyle style,float maxWidth,bool wrap=false,float maxHeight=0,float size=0);
    // Fit or shrink: the largest size from FontSize(style) down to minSize (1 px
    // steps) at which the text fits the box (wrap off: its width; on: its height).
    // Returns minSize when even that does not fit (the TextSize says fits=false).
    TextSize FitSize(std::string_view utf8,TextStyle style,RectF box,bool wrap=false,float minSize=MinFontSize);
    // Text drawn at its fitted size.
    TextSize TextFit(std::string_view utf8,TextStyle style,RectF box,Color c,Align align=Align::Leading,bool wrap=false,float minSize=MinFontSize);
    // A line of colored runs in one layout (old -> new, pending lists).
    TextSize Spans(const Span* spans,size_t count,TextStyle style,RectF box,Align align=Align::Leading,bool wrap=false);
    // An icon (sized to the style's cap height + a little) followed by text, the
    // pair aligned as one in the box: state lines, sub lines with a padlock or warning.
    TextSize IconText(RectF box,Icon icon,std::string_view utf8,TextStyle style,Color text,Color iconColor,Align align=Align::Leading);

    // Controls. Each draws its own text and state; the painter passes words, not pixels.
    // Chip, value box, cycler value: a chamfered socket with centered text
    // (chosen: crimson with bold text). An icon goes before the text.
    void Socket(RectF r,std::string_view utf8,ControlState s,Icon icon=Icon::None);
    void Toggle(RectF r,bool on,ControlState s);                                       // 156 px pill: gem socket + "On"/"Off"
    // A stepper's - or +; chevron: a cycler's < (plus=false) or > (plus=true).
    void StepButton(RectF r,bool plus,ControlState s,bool chevron=false);
    // A slider / stepper bar: fill 0-1 (signed ranges fill from zeroAt), mark 0-1
    // the default tick (<0: none). Bar draws the bar alone in r (10 px tall).
    void Bar(RectF r,float fill,float mark,ControlState s,float zeroAt=-1.f);
    // The value box of a stepper, slider or cycler in one call: the socket, the
    // value (bold), then either the bar (fill>=0: Slider fields) or the sub line
    // ("1 of 4"). hue>=0 puts that hue's swatch before the value, SwatchOff the
    // slashed Off swatch, SwatchByRange the range gradient, SwatchNone none.
    // An empty value with a swatch: the swatch alone, larger, centered.
    void ValueBox(RectF r,std::string_view value,std::string_view sub,ControlState s,float fill=-1.f,float mark=-1.f,float zeroAt=-1.f,float hue=SwatchNone);
    // A hue disc filling r (the color grid puts it inside a Socket); SwatchByRange:
    // the red-orange-yellow range gradient; any other hue<0: Off (slashed ring).
    void Swatch(RectF r,float hue,ControlState s);
    // A color grid's dot: the hue's disc in r (scaled 1.15 pointed, 0.92 pressed),
    // a 4 px light ring when chosen, the pointed ring outside that, the default mark under it.
    void Dot(RectF r,float hue,ControlState s);
    // Footer and card buttons: icon + text centered as a pair. textSize 0: Value (30 px).
    void Button(RectF r,std::string_view utf8,Icon icon,ControlState s,Plate plate=Plate::Socket,float textSize=0);
    void IconAt(Icon icon,RectF r,Color c,float alpha=1.f);
    // A rail tab: icon + name. chosen: the current page's tongue (crimson notch,
    // gold, bold) out to the seam; pointed: slab + ring; changed: the ember on the icon.
    void Tab(RectF r,Icon icon,std::string_view name,ControlState s);
    // The band: the page icon and its cast-gold Cinzel title, left-aligned in r.
    void PageTitle(RectF r,Icon icon,std::string_view title);
    // The faint 56 px Horadric sigil (45%) at the band's end. Not pressable.
    void Sigil(RectF r);
    // Guide / What's new: icon + underlined quiet text, no box. chosen: the
    // open page (gold, bold, 3 px gold underline); pointed: ring inside r.
    void HeaderLink(RectF r,Icon icon,std::string_view utf8,ControlState s);
    // A row: pointed draws the Slab; divider the carved line at its bottom.
    void Row(RectF r,bool pointed,bool divider=true);
    // A row's label (36 px) and its optional sub line (30 px, subColor, subIcon
    // before it), centered as a block in r. changed: ember after the label;
    // locked: Dim label and a padlock before the sub line.
    void Label(RectF r,std::string_view label,std::string_view sub,ControlState s,Icon subIcon=Icon::None,Color subColor=Color::Quiet);
    // A preset: icon (64 px gold), Cinzel name and a wrapped Quiet blurb. No box
    // at rest (a carved divider), the slab and ring when pointed. chosen: gold
    // name (applied); changed: orange name (picked, not applied).
    void PresetCard(RectF r,Icon icon,std::string_view name,std::string_view blurb,ControlState s);
    // A small outlined tag: "Recommended: On", "PR only", the Mono audio warning.
    void Pill(RectF r,std::string_view utf8,Color c,Icon icon=Icon::None,ControlState s={});
    // The help card: a Diablo II item tooltip (86% black field, 1 px hairline).
    void Card(RectF r);
    // The Performance readout box (fixed 110 px under the Performance rows).
    void ReadoutBox(RectF r);
    // A 6 px bar draining from full (1) to empty (0): Undo reset's 6 s.
    void DrainBar(RectF r,float left,Color c);
    // Ornaments the controls use, for D's own spots.
    void Gem(float cx,float cy,float radius=12.f);       // a faceted crimson gem in a bronze bezel
    void DefaultMark(float cx,float y);                  // the bronze triangle hanging from y
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
// Premultiplied BGRA (Direct2D's target format) to straight RGBA (the settings
// layer is submitted with XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT).
void PremultipliedBgraToStraightRgba(const uint8_t* bgra,size_t pixels,uint8_t* rgba);

// The component sheet for vr-settings-preview --draw-sheet: four 1280 x 1024
// canvases in a 2 x 2 grid (straight RGBA, 2560 x 2048): an Aim page and a
// Shield page as the menu draws them, every control in every state,
// and every icon. repaintMs: the average full Aim-page repaint (Begin to End,
// cached background) over `repaints` frames.
struct Sheet {
    std::vector<uint8_t> rgba;
    int width=0,height=0;
    double firstMs=0,repaintMs=0,sheetMs=0;   // sheetMs: all four canvases, warm
    std::string error;
};
Sheet DrawSheet(int repaints=50);
}
