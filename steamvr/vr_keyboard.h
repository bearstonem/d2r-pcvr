#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "frame_math.h"
#include "respec_button.h"

// The VR keyboard: letters for the game's text boxes - a new character's name,
// and D2RLoader's multiplayer panel (its game name, password, description,
// filter, address and port) - without taking the headset off. D2R shows no
// on-screen keyboard of its own there (tested 2026-11-05: the name box does
// nothing under native controller navigation in VR). Its own quad hinged under
// the menu, tilted toward the player like a keyboard on a desk (Board): one
// picture of every key, small letters or, with Shift, capitals; a lit copy
// cropped to the key under the laser, which hits the board itself. Two layouts
// (Layout): the name's letters, and the multiplayer panel's with a digit row,
// . : and a space. Its letters go in through SendInput tagged KeyboardKeyTag
// (panel_input_mode.h), so TypedKeys lets the keyboard layout in and the box
// takes them like letters from a real keyboard.
namespace framevr::keyboard {

// When the name box is on screen: character creation (CharacterCreatePanel)
// with a hero picked. The game's UI messages say both (2026-11-06 log):
// PanelManager opens / unloads CharacterCreatePanel; CharacterCreate Select
// names the hero clicked, and switching heroes sends Select for the new one
// BEFORE Unselect for the old. Fed every UI message (vr/gamestate.cpp).
// epoch counts each time the box comes up anew, so Close lasts only until then.
class NameBox {
    bool panel=false;std::string hero;uint32_t shownAt=0;
public:
    void Message(std::string_view target,std::string_view command,std::string_view text) {
        const bool before=Shown();
        if(target=="PanelManager") {
            if(text=="CharacterCreatePanel") {
                if(command=="OpenPanel"||command=="OpenExclusivePanel")panel=true;
                else if(command=="ClosePanel"||command=="UnloadPanel")panel=false;
                hero.clear();
            } else if(command=="UnloadAll"||command=="CloseAll"){panel=false;hero.clear();}
        } else if(target=="CharacterCreate"&&panel) {
            if(command=="Select"&&!text.empty()){if(!hero.empty()&&hero!=text)++shownAt;hero=text;}   // another hero: anew
            else if(command=="Unselect"&&text==hero)hero.clear();
        }
        if(Shown()&&!before)++shownAt;
    }
    bool Shown() const{return panel&&!hero.empty();}
    uint32_t Epoch() const{return shownAt;}
};

// When D2RLoader's multiplayer panel is on screen (the main menu's TCP/IP
// button opens D2RLoaderMultiplayerPanel). Each of its views has text boxes:
// Host Game its name, password and description, Browse Games its filter,
// Direct join its address and port, a passworded game its password. Neither
// the loader's SDK nor the game says which box has the focus: the owner's
// 2026-11-17 log has no new UI message while the Host Game boxes were clicked
// (ShowHost 21:23:06 to OpenDirectJoin 21:24:04). So the keyboard shows while
// the panel is open; the laser's click picks the box, and the keys go to it.
// The game's UI messages say when: PanelManager opens and closes the panel by
// name (D2RLoader closes it itself once a game is made or joined), a game
// starting unloads every panel; the panel's own channel names the view picked.
// Character creation over it (Create New in the list beside it) is the name
// box's: no multiplayer keyboard there. epoch counts each time the panel comes
// up anew or another view is picked, so Close lasts only until then.
class MultiplayerPanel {
    bool open=false,creation=false;uint32_t shownAt=0;
public:
    static constexpr std::string_view Panel="D2RLoaderMultiplayerPanel",Channel="D2RLoaderMultiplayerPanelMessage";
    // A command on the panel's channel that brings a view or dialog up or takes one down (the
    // loader's ShowHost, ShowBrowse, ShowGameInfo / Players / Plugins, OpenDirectJoin,
    // CloseDirectJoin, CancelDirectJoin, RetryDirectJoin, CloseJoinPassword, JoinSelected,
    // JoinDirect) - not a toggle, a stepper or a box's own text (FilterGames, LimitPasswords).
    static bool NewView(std::string_view command) {
        for(std::string_view start:{"Show","Open","Close","Cancel","Retry","Join"})
            if(command.substr(0,start.size())==start)return true;
        return false;
    }
    void Message(std::string_view target,std::string_view command,std::string_view text) {
        const bool before=Shown();
        if(target=="PanelManager") {
            const bool opens=command=="OpenPanel"||command=="OpenExclusivePanel",closes=command=="ClosePanel"||command=="UnloadPanel";
            if(text==Panel){if(opens)open=true;else if(closes)open=false;}
            else if(text=="CharacterCreatePanel"){if(opens)creation=true;else if(closes)creation=false;}
            else if(command=="UnloadAll"||command=="CloseAll")open=creation=false;
        } else if(target=="FrontEndNavigation"&&(command=="ToGame"||command=="ToExit"))open=creation=false;
        else if(target==Channel&&Shown()&&NewView(command))++shownAt;
        if(Shown()&&!before)++shownAt;
    }
    bool Open() const{return open;}
    bool Shown() const{return open&&!creation;}
    uint32_t Epoch() const{return shownAt;}
};
// Which keyboard the boxes on screen want: the name box's over all, then the multiplayer panel's.
enum class Want : uint8_t {None,Name,Text};
inline Want Wanted(const NameBox& name,const MultiplayerPanel& multiplayer) {
    return name.Shown()?Want::Name:multiplayer.Shown()?Want::Text:Want::None;
}

// What a key does. Char types its letter; Shift makes the next letter a
// capital (once), or a digit its symbol; Close hides the keyboard until the
// text box comes up anew.
enum class Action : uint8_t {Char,Backspace,Enter,Shift,Close};
struct Key {
    float col,row,span;   // in key units from the keyboard's top left
    Action action;
    char ch;              // Action::Char: the lower-case letter, digit, mark or space
    const wchar_t* label;
    char shifted=0;       // Action::Char: what it types with Shift if not its capital ('1': '!')
};
// Character names take letters and one '-' or '_' (the game checks the rest),
// so no digits and no space. Ten keys a row.
inline constexpr Key NameKeys[]={
    {0,0,1,Action::Char,'q',L"Q"},{1,0,1,Action::Char,'w',L"W"},{2,0,1,Action::Char,'e',L"E"},
    {3,0,1,Action::Char,'r',L"R"},{4,0,1,Action::Char,'t',L"T"},{5,0,1,Action::Char,'y',L"Y"},
    {6,0,1,Action::Char,'u',L"U"},{7,0,1,Action::Char,'i',L"I"},{8,0,1,Action::Char,'o',L"O"},
    {9,0,1,Action::Char,'p',L"P"},
    {.5f,1,1,Action::Char,'a',L"A"},{1.5f,1,1,Action::Char,'s',L"S"},{2.5f,1,1,Action::Char,'d',L"D"},
    {3.5f,1,1,Action::Char,'f',L"F"},{4.5f,1,1,Action::Char,'g',L"G"},{5.5f,1,1,Action::Char,'h',L"H"},
    {6.5f,1,1,Action::Char,'j',L"J"},{7.5f,1,1,Action::Char,'k',L"K"},{8.5f,1,1,Action::Char,'l',L"L"},
    {0,2,1.5f,Action::Shift,0,L"Shift"},
    {1.5f,2,1,Action::Char,'z',L"Z"},{2.5f,2,1,Action::Char,'x',L"X"},{3.5f,2,1,Action::Char,'c',L"C"},
    {4.5f,2,1,Action::Char,'v',L"V"},{5.5f,2,1,Action::Char,'b',L"B"},{6.5f,2,1,Action::Char,'n',L"N"},
    {7.5f,2,1,Action::Char,'m',L"M"},
    {8.5f,2,1.5f,Action::Backspace,0,L"Back"},
    {0,3,2,Action::Close,0,L"Close"},{2,3,1.5f,Action::Char,'-',L"-"},{3.5f,3,1.5f,Action::Char,'_',L"_"},
    {5,3,5,Action::Enter,0,L"Enter"},
};
// The multiplayer panel's boxes: a game's name, password and description, the
// list's filter, an address and port (192.168.1.20 and 4000, or 192.168.1.20:4000).
// The name's rows under a digit row - Shift on a digit types its symbol as on a
// US keyboard (the game's own layout picks the key that types it, SendKeyboard) -
// and a last row with the marks an address and a name want, and a space.
inline constexpr Key TextKeys[]={
    {0,0,1,Action::Char,'1',L"1",'!'},{1,0,1,Action::Char,'2',L"2",'@'},{2,0,1,Action::Char,'3',L"3",'#'},
    {3,0,1,Action::Char,'4',L"4",'$'},{4,0,1,Action::Char,'5',L"5",'%'},{5,0,1,Action::Char,'6',L"6",'^'},
    {6,0,1,Action::Char,'7',L"7",'&'},{7,0,1,Action::Char,'8',L"8",'*'},{8,0,1,Action::Char,'9',L"9",'('},
    {9,0,1,Action::Char,'0',L"0",')'},
    {0,1,1,Action::Char,'q',L"Q"},{1,1,1,Action::Char,'w',L"W"},{2,1,1,Action::Char,'e',L"E"},
    {3,1,1,Action::Char,'r',L"R"},{4,1,1,Action::Char,'t',L"T"},{5,1,1,Action::Char,'y',L"Y"},
    {6,1,1,Action::Char,'u',L"U"},{7,1,1,Action::Char,'i',L"I"},{8,1,1,Action::Char,'o',L"O"},
    {9,1,1,Action::Char,'p',L"P"},
    {.5f,2,1,Action::Char,'a',L"A"},{1.5f,2,1,Action::Char,'s',L"S"},{2.5f,2,1,Action::Char,'d',L"D"},
    {3.5f,2,1,Action::Char,'f',L"F"},{4.5f,2,1,Action::Char,'g',L"G"},{5.5f,2,1,Action::Char,'h',L"H"},
    {6.5f,2,1,Action::Char,'j',L"J"},{7.5f,2,1,Action::Char,'k',L"K"},{8.5f,2,1,Action::Char,'l',L"L"},
    {0,3,1.5f,Action::Shift,0,L"Shift"},
    {1.5f,3,1,Action::Char,'z',L"Z"},{2.5f,3,1,Action::Char,'x',L"X"},{3.5f,3,1,Action::Char,'c',L"C"},
    {4.5f,3,1,Action::Char,'v',L"V"},{5.5f,3,1,Action::Char,'b',L"B"},{6.5f,3,1,Action::Char,'n',L"N"},
    {7.5f,3,1,Action::Char,'m',L"M"},
    {8.5f,3,1.5f,Action::Backspace,0,L"Back"},
    {0,4,1.5f,Action::Close,0,L"Close"},{1.5f,4,1,Action::Char,'.',L"."},{2.5f,4,1,Action::Char,':',L":"},
    {3.5f,4,1,Action::Char,'-',L"-"},{4.5f,4,1,Action::Char,'_',L"_"},{5.5f,4,2.5f,Action::Char,' ',L"Space"},
    {8,4,2,Action::Enter,0,L"Enter"},
};
// A layout: its keys and how many rows they take, Columns key units a row.
struct Layout {const Key* keys;int count;int rows;};
inline constexpr Layout Name{NameKeys,int(sizeof NameKeys/sizeof NameKeys[0]),4};
inline constexpr Layout Text{TextKeys,int(sizeof TextKeys/sizeof TextKeys[0]),5};
constexpr float Columns=10;

// The picture: KeyTexels a key unit, Pad texels of stone-free gap round each
// key, so the lit copy's crop never shows a neighbour's edge.
constexpr int KeyTexels=64,Pad=3;
constexpr int TexelWidth=int(Columns)*KeyTexels;
constexpr int TexelHeight(const Layout& l){return l.rows*KeyTexels;}
struct Rect {int x=0,y=0,w=0,h=0;};
inline Rect KeyTexelRect(const Layout& l,int i) {
    if(i<0||i>=l.count)return {};
    const Key& k=l.keys[i];
    return {int(std::lround(k.col*KeyTexels))+Pad,int(std::lround(k.row*KeyTexels))+Pad,
            int(std::lround(k.span*KeyTexels))-2*Pad,KeyTexels-2*Pad};
}


// Where it sits: under the menu, out of the game's picture (owner 2026-11-06:
// over the heroes it hid their names, and the picture's sides are cut at some
// aspect ratios, so a spot on it is not the same for every headset). Its top
// edge is Gap below the panel's bottom edge, the board tilted Tilt toward the
// player, its lower edge nearer, facing up to the eyes. Sizes are fractions of
// the 16:9 workspace's width (panels::Fit): the same on a wide or a square
// panel, and growing with the menu distance, so a key stays as easy to hit.
constexpr float WidthOfWorkspace=.5f,GapOfWorkspace=.02f,TiltDegrees=30;
struct Board {math::Vec position{};math::Quat orientation{0,0,0,1};float width=0,height=0;};
inline Board Place(const Layout& l,math::Vec surface,math::Quat facing,float width,float height) {
    if(!(width>0)||!(height>0))return {};
    const float workspace=std::max(width,height*16.f/9.f);
    const float w=WidthOfWorkspace*workspace,h=w*float(l.rows)/Columns,t=TiltDegrees*3.14159265f/180;
    const math::Vec up{0,std::cos(t),-std::sin(t)};   // the board's up: its top edge away from the player
    const math::Vec top{0,-height*.5f-GapOfWorkspace*workspace,0};
    const math::Quat tilt{-std::sin(t*.5f),0,0,std::cos(t*.5f)};   // about the panel's x axis
    return {surface+math::Rotate(facing,top-up*(h*.5f)),math::Multiply(facing,tilt),w,h};
}
// Over the menu instead: the main menu's board in its 3D scene ([frontend]),
// which stands low before the player and leans back to face the eyes - under
// it the keys would be down by the knees (its lower edge a metre under the
// eyes at the default size). Its lower edge Gap over the board's top edge,
// turned up about that edge by `upright` radians (the board's lean back, so
// it stands upright, facing the player at about eye height), a little smaller
// than under a flat panel since the board is wide.
constexpr float AboveWidthOfWorkspace=.4f;
inline Board PlaceAbove(const Layout& l,math::Vec surface,math::Quat facing,float width,float height,float upright) {
    if(!(width>0)||!(height>0))return {};
    const float workspace=std::max(width,height*16.f/9.f);
    const float w=AboveWidthOfWorkspace*workspace,h=w*float(l.rows)/Columns;
    const math::Vec up{0,std::cos(upright),std::sin(upright)};   // the board's up: its top edge toward the player
    const math::Vec bottom{0,height*.5f+GapOfWorkspace*workspace,0};
    const math::Quat tilt{std::sin(upright*.5f),0,0,std::cos(upright*.5f)};   // about the panel's x axis
    return {surface+math::Rotate(facing,bottom+up*(h*.5f)),math::Multiply(facing,tilt),w,h};
}
// The game's pointer while the multiplayer keyboard is up (parked): a click gives a
// box the focus, and in the game's mouse layout a button the pointer then passes over
// takes it (focusOnMouseOver; the game's loot filter turns it off on the Back button
// beside its name box). On its way up to this keyboard the laser crosses the panel's
// tabs and buttons, so the pointer stays where the laser last clicked. A pull on the
// board moves it to the laser (jump) and the click goes the frame after; while a mouse
// button is held it follows (a drag). Not parked, it follows the laser as anywhere.
struct PointerMove { bool move,jump; };
constexpr PointerMove ParkedPointer(bool parked,bool pullStarts,bool buttonHeld) {
    if(!parked||buttonHeld)return {true,false};
    return {pullStarts,pullStarts};
}
// The key at the laser's hit on the board (u, v from its top left, 0-1: the
// board's own math::IntersectPanel), or -1: in the gap between two keys.
inline int KeyAt(const Layout& l,float u,float v) {
    const float col=u*Columns,row=v*float(l.rows),pad=float(Pad)/KeyTexels;
    for(int i=0;i<l.count;++i) {
        const Key& k=l.keys[i];
        if(col>=k.col+pad&&col<k.col+k.span-pad&&row>=k.row+pad&&row<k.row+1-pad)return i;
    }
    return -1;
}
// A key's lit copy on the board: its centre from the board's centre (x right,
// y up, metres) and its size.
struct Spot {float x=0,y=0,width=0,height=0;};
inline Spot KeySpot(const Layout& l,const Board& b,int i) {
    if(i<0||i>=l.count||!(b.height>0))return {};
    const Key& k=l.keys[i];
    const float unit=b.height/float(l.rows),pad=float(Pad)/KeyTexels*unit;
    return {(k.col+k.span*.5f)*unit-b.width*.5f,b.height*.5f-(k.row+.5f)*unit,k.span*unit-2*pad,unit-2*pad};
}
// What a character key types: itself, or with Shift its capital or its symbol.
inline wchar_t Typed(const Key& k,bool shift) {
    if(!shift)return wchar_t(k.ch);
    if(k.ch>='a'&&k.ch<='z')return wchar_t(k.ch-'a'+'A');
    return wchar_t(k.shifted?k.shifted:k.ch);
}
// What a key shows: what it types now - a letter small, or a capital (a digit
// its symbol) while Shift waits (Typist); the other keys their name.
inline std::wstring Label(const Layout& l,int i,bool capitals) {
    if(i<0||i>=l.count)return {};
    const Key& k=l.keys[i];
    if(k.action!=Action::Char||k.ch==' ')return k.label;
    return std::wstring(1,Typed(k,capitals));
}

// What a press sends: a character (ch), Backspace or Enter (key), or nothing
// (Shift, Close). close: hide the keyboard.
enum class Send : uint8_t {None,Char,Backspace,Enter};
struct Output {Send send=Send::None;wchar_t ch=0;bool close=false;};
class Typist {
    bool shift=false;
public:
    bool Shifted() const{return shift;}
    void Reset(){shift=false;}
    Output Press(const Layout& l,int i) {
        Output out;
        if(i<0||i>=l.count)return out;
        const Key& k=l.keys[i];
        switch(k.action) {
            case Action::Char:
                out.send=Send::Char;
                out.ch=Typed(k,shift);
                shift=false;break;
            case Action::Shift:shift=!shift;break;
            case Action::Backspace:out.send=Send::Backspace;break;
            case Action::Enter:out.send=Send::Enter;shift=false;break;
            case Action::Close:out.close=true;shift=false;break;
        }
        return out;
    }
};

// The picture: one stone plaque per key (Reset Skills & Stats' face), lit or
// not, premultiplied RGBA, TexelWidth x TexelHeight(l). Labels (Label) are
// stamped over each key on Windows (transport.cpp, GDI), as on that button.
// With capitals the Shift key is lit in both, so the waiting Shift shows.
inline std::vector<uint8_t> PaintKeys(const Layout& l,bool lit,bool capitals=false) {
    std::vector<uint8_t> px(size_t(TexelWidth)*TexelHeight(l)*4,0);
    for(int i=0;i<l.count;++i) {
        const Rect r=KeyTexelRect(l,i);
        const auto face=respecbutton::PaintFace(r.w,r.h,lit||(capitals&&l.keys[i].action==Action::Shift));
        for(int y=0;y<r.h;++y)
            std::copy_n(&face[size_t(y)*r.w*4],size_t(r.w)*4,&px[(size_t(r.y+y)*TexelWidth+r.x)*4]);
    }
    return px;
}
}
