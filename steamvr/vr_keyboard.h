#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "respec_button.h"

// The VR keyboard: letters for the game's text boxes - a new character's name
// first - without taking the headset off. D2R shows no on-screen keyboard of
// its own there (tested 2026-11-05: the name box does nothing under native
// controller navigation in VR). A stamped panel on the menu surface like Reset
// Skills & Stats (respec_button.h): one picture of every key, a lit copy of it
// cropped to the key under the laser, and the laser's hit in the surface's
// u, v. Its letters go in through SendInput tagged KeyboardKeyTag
// (panel_input_mode.h), so TypedKeys lets the keyboard layout in and the box
// takes them like letters from a real keyboard.
namespace framevr::keyboard {

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

// Where it sits: the upper middle of the menu's 16:9 band (the band
// respec_button.h measures), over the character creation scene's sky, clear of
// the name box and the OK / Exit buttons along the bottom. A key unit is
// KeyHeight of the band: a little taller than the Advanced Stats button
// (6.2%, about 2 cm at arm's length), so a key is easy to hit with the laser.
constexpr float KeyHeight=.075f,CenterX=.5f,CenterY=.27f;
struct Spot {float x=0,y=0,width=0,height=0;};   // centre from the surface's centre (y up), size; metres
inline Spot Place(float width,float height) {
    if(!(width>0)||!(height>0))return {};
    const float band=std::min(height,width*9.f/16.f),bandWidth=band*16.f/9.f;
    const float unit=KeyHeight*band;
    return {(CenterX-.5f)*bandWidth,height*.5f-CenterY*band,unit*Columns,unit*Rows};
}
// The key under the laser's hit on the surface (u, v from its top left, 0-1),
// or -1: off the keyboard, or in the gap between two keys.
inline int Hit(Spot s,float width,float height,float u,float v) {
    if(!(s.width>0)||!(s.height>0))return -1;
    const float x=(u-.5f)*width-(s.x-s.width*.5f),y=(s.y+s.height*.5f)-(.5f-v)*height;   // from the keyboard's top left
    const float unit=s.height/Rows,col=x/unit,row=y/unit,pad=float(Pad)/KeyTexels;
    for(int i=0;i<KeyCount;++i) {
        const Key& k=Keys[i];
        if(col>=k.col+pad&&col<k.col+k.span-pad&&row>=k.row+pad&&row<k.row+1-pad)return i;
    }
    return -1;
}
// A key's middle as a quad on the surface (its centre from the surface's
// centre, metres, y up), for the lit copy laid over it.
inline Spot KeySpot(Spot s,int i) {
    if(i<0||i>=KeyCount||!(s.height>0))return {};
    const Key& k=Keys[i];
    const float unit=s.height/Rows,pad=float(Pad)/KeyTexels*unit;
    return {s.x-s.width*.5f+(k.col+k.span*.5f)*unit,s.y+s.height*.5f-(k.row+.5f)*unit,k.span*unit-2*pad,unit-2*pad};
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
// not, premultiplied RGBA, TexelWidth x TexelHeight. Labels are stamped over
// each key on Windows (transport.cpp, GDI), as on that button.
inline std::vector<uint8_t> PaintKeys(bool lit) {
    std::vector<uint8_t> px(size_t(TexelWidth)*TexelHeight*4,0);
    for(int i=0;i<KeyCount;++i) {
        const Rect r=KeyTexelRect(i);
        const auto face=respecbutton::PaintFace(r.w,r.h,lit);
        for(int y=0;y<r.h;++y)
            std::copy_n(&face[size_t(y)*r.w*4],size_t(r.w)*4,&px[(size_t(r.y+y)*TexelWidth+r.x)*4]);
    }
    return px;
}
}
