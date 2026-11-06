#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "frame_math.h"
#include "respec_button.h"

// The VR keyboard: letters for the game's text boxes - a new character's name
// first - without taking the headset off. D2R shows no on-screen keyboard of
// its own there (tested 2026-11-05: the name box does nothing under native
// controller navigation in VR). Its own quad hinged under the menu, tilted
// toward the player like a keyboard on a desk (Board): one picture of every
// key, small letters or, with Shift, capitals; a lit copy cropped to the key
// under the laser, which hits the board itself. Its letters go in through
// SendInput tagged KeyboardKeyTag
// (panel_input_mode.h), so TypedKeys lets the keyboard layout in and the box
// takes them like letters from a real keyboard.
namespace framevr::keyboard {

// When the name box is on screen: character creation (CharacterCreatePanel)
// with a hero picked. The game's UI messages say both (2026-11-06 log, Quest 3):
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

// What a key does. Char types its letter; Shift makes the next letter a
// capital (once); Close hides the keyboard until the text box goes away.
enum class Action : uint8_t {Char,Backspace,Enter,Shift,Close};
struct Key {
    float col,row,span;   // in key units from the keyboard's top left
    Action action;
    char ch;              // Action::Char: the lower-case letter, '-' or '_'
    const wchar_t* label;
};
// Character names take letters and one '-' or '_' (the game checks the rest),
// so no digits and no space. Ten keys a row.
inline constexpr Key Keys[]={
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
constexpr int KeyCount=int(sizeof Keys/sizeof Keys[0]);
constexpr float Columns=10,Rows=4;

// The picture: KeyTexels a key unit, Pad texels of stone-free gap round each
// key, so the lit copy's crop never shows a neighbour's edge.
constexpr int KeyTexels=64,Pad=3;
constexpr int TexelWidth=int(Columns)*KeyTexels,TexelHeight=int(Rows)*KeyTexels;
struct Rect {int x=0,y=0,w=0,h=0;};
inline Rect KeyTexelRect(int i) {
    if(i<0||i>=KeyCount)return {};
    const Key& k=Keys[i];
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
inline Board Place(math::Vec surface,math::Quat facing,float width,float height) {
    if(!(width>0)||!(height>0))return {};
    const float workspace=std::max(width,height*16.f/9.f);
    const float w=WidthOfWorkspace*workspace,h=w*Rows/Columns,t=TiltDegrees*3.14159265f/180;
    const math::Vec up{0,std::cos(t),-std::sin(t)};   // the board's up: its top edge away from the player
    const math::Vec top{0,-height*.5f-GapOfWorkspace*workspace,0};
    const math::Quat tilt{-std::sin(t*.5f),0,0,std::cos(t*.5f)};   // about the panel's x axis
    return {surface+math::Rotate(facing,top-up*(h*.5f)),math::Multiply(facing,tilt),w,h};
}
// The key at the laser's hit on the board (u, v from its top left, 0-1: the
// board's own math::IntersectPanel), or -1: in the gap between two keys.
inline int KeyAt(float u,float v) {
    const float col=u*Columns,row=v*Rows,pad=float(Pad)/KeyTexels;
    for(int i=0;i<KeyCount;++i) {
        const Key& k=Keys[i];
        if(col>=k.col+pad&&col<k.col+k.span-pad&&row>=k.row+pad&&row<k.row+1-pad)return i;
    }
    return -1;
}
// A key's lit copy on the board: its centre from the board's centre (x right,
// y up, metres) and its size.
struct Spot {float x=0,y=0,width=0,height=0;};
inline Spot KeySpot(const Board& b,int i) {
    if(i<0||i>=KeyCount||!(b.height>0))return {};
    const Key& k=Keys[i];
    const float unit=b.height/Rows,pad=float(Pad)/KeyTexels*unit;
    return {(k.col+k.span*.5f)*unit-b.width*.5f,b.height*.5f-(k.row+.5f)*unit,k.span*unit-2*pad,unit-2*pad};
}
// What a key shows: a letter small, or a capital while Shift waits (Typist);
// the other keys their name.
inline std::wstring Label(int i,bool capitals) {
    if(i<0||i>=KeyCount)return {};
    const Key& k=Keys[i];
    if(k.action!=Action::Char)return k.label;
    return std::wstring(1,wchar_t(capitals&&k.ch>='a'&&k.ch<='z'?k.ch-'a'+'A':k.ch));
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
    Output Press(int i) {
        Output out;
        if(i<0||i>=KeyCount)return out;
        const Key& k=Keys[i];
        switch(k.action) {
            case Action::Char:
                out.send=Send::Char;
                out.ch=wchar_t(shift&&k.ch>='a'&&k.ch<='z'?k.ch-'a'+'A':k.ch);
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
// not, premultiplied RGBA, TexelWidth x TexelHeight. Labels (Label) are stamped
// over each key on Windows (transport.cpp, GDI), as on that button.
// With capitals the Shift key is lit in both, so the waiting Shift shows.
inline std::vector<uint8_t> PaintKeys(bool lit,bool capitals=false) {
    std::vector<uint8_t> px(size_t(TexelWidth)*TexelHeight*4,0);
    for(int i=0;i<KeyCount;++i) {
        const Rect r=KeyTexelRect(i);
        const auto face=respecbutton::PaintFace(r.w,r.h,lit||(capitals&&Keys[i].action==Action::Shift));
        for(int y=0;y<r.h;++y)
            std::copy_n(&face[size_t(y)*r.w*4],size_t(r.w)*4,&px[(size_t(r.y+y)*TexelWidth+r.x)*4]);
    }
    return px;
}
}
