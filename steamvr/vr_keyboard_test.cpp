#include "vr_keyboard.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace framevr;
static void Check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
// The surface u, v of a point given in key units from the keyboard's top left.
static void At(keyboard::Spot s,float width,float height,float col,float row,float& u,float& v) {
    const float unit=s.height/keyboard::Rows;
    const float x=s.x-s.width*.5f+col*unit,y=s.y+s.height*.5f-row*unit;
    u=x/width+.5f;v=.5f-y/height;
}
static int Find(char ch){for(int i=0;i<keyboard::KeyCount;++i)if(keyboard::Keys[i].action==keyboard::Action::Char&&keyboard::Keys[i].ch==ch)return i;return -1;}
static int Find(keyboard::Action a){for(int i=0;i<keyboard::KeyCount;++i)if(keyboard::Keys[i].action==a)return i;return -1;}
int main(){
    using namespace keyboard;
    // Every key a name needs, once each, and no key reaching past the board or over another.
    for(char c='a';c<='z';++c)Check(Find(c)>=0,"every letter has a key");
    Check(Find('-')>=0&&Find('_')>=0,"the two marks a name may hold have keys");
    for(auto a:{Action::Backspace,Action::Enter,Action::Shift,Action::Close})Check(Find(a)>=0,"Backspace, Enter, Shift and Close have keys");
    for(int i=0;i<KeyCount;++i) {
        const Key& k=Keys[i];
        Check(k.col>=0&&k.col+k.span<=Columns+1e-4f&&k.row>=0&&k.row+1<=Rows,"every key inside the board");
        for(int j=i+1;j<KeyCount;++j)
            if(Keys[j].row==k.row)Check(Keys[j].col>=k.col+k.span-1e-4f||k.col>=Keys[j].col+Keys[j].span-1e-4f,"no two keys overlap");
        const Rect r=KeyTexelRect(i);
        Check(r.w>0&&r.h>0&&r.x>=0&&r.y>=0&&r.x+r.w<=TexelWidth&&r.y+r.h<=TexelHeight,"every key's picture inside the keyboard's");
    }

    // On the menu surface: inside the band, above its lower half (the name box, OK and Exit).
    const float width=2.0f,height=1.125f;   // a 16:9 surface: the band is all of it
    const Spot s=Place(width,height);
    Check(s.width>0&&s.height>0&&std::fabs(s.width/s.height-Columns/Rows)<1e-4f,"the quad has the picture's aspect");
    Check(std::fabs(float(TexelWidth)/TexelHeight-Columns/Rows)<1e-4f,"the picture has the board's aspect");
    Check(s.x-s.width*.5f>-width*.5f&&s.x+s.width*.5f<width*.5f,"inside the band left to right");
    Check(s.y+s.height*.5f<height*.5f&&s.y-s.height*.5f>0,"inside the band's upper half");
    Check(!Place(0,1).width&&!Place(1,0).width,"no surface, no keyboard");

    // The laser: the middle of each key hits it; the gaps and outside hit nothing.
    for(int i=0;i<KeyCount;++i) {
        float u,v;At(s,width,height,Keys[i].col+Keys[i].span*.5f,Keys[i].row+.5f,u,v);
        Check(Hit(s,width,height,u,v)==i,"a key's middle hits that key");
        const Spot k=KeySpot(s,i);
        Check(std::fabs(k.x-((u-.5f)*width))<1e-4f&&std::fabs(k.y-((.5f-v)*height))<1e-4f,"the lit copy lies over the key it lights");
        Check(k.width>0&&k.height>0&&k.height<s.height/Rows,"the lit copy is the key's size, not a whole row");
    }
    {
        float u,v;At(s,width,height,1,.5f,u,v);   // the seam between Q and W
        Check(Hit(s,width,height,u,v)==-1,"the gap between two keys hits nothing (no press on the wrong letter)");
        At(s,width,height,0.2f,1.5f,u,v);          // left of A: the row starts half a key in
        Check(Hit(s,width,height,u,v)==-1,"the empty half key left of A hits nothing");
        At(s,width,height,-.5f,1,u,v);Check(Hit(s,width,height,u,v)==-1,"left of the board: nothing");
        At(s,width,height,5,-.5f,u,v);Check(Hit(s,width,height,u,v)==-1,"above the board: nothing");
        At(s,width,height,5,Rows+.5f,u,v);Check(Hit(s,width,height,u,v)==-1,"below the board: nothing");
        Check(Hit({},width,height,.5f,.5f)==-1,"no keyboard placed: nothing");
    }

    // Typing: lower case, Shift once for a capital, Backspace, Enter, Close.
    Typist t;
    auto out=t.Press(Find('k'));
    Check(out.send==Send::Char&&out.ch==L'k'&&!out.close,"a letter types its lower case");
    Check(t.Press(Find(Action::Shift)).send==Send::None&&t.Shifted(),"Shift sends nothing and waits for a letter");
    out=t.Press(Find('k'));
    Check(out.send==Send::Char&&out.ch==L'K'&&!t.Shifted(),"Shift makes one capital, then letters are small again");
    Check(t.Press(Find('k')).ch==L'k',"the next letter is small");
    t.Press(Find(Action::Shift));t.Press(Find(Action::Shift));
    Check(!t.Shifted(),"Shift twice takes it back");
    t.Press(Find(Action::Shift));
    out=t.Press(Find('-'));
    Check(out.send==Send::Char&&out.ch==L'-'&&!t.Shifted(),"Shift leaves '-' as it is, and is used up");
    Check(t.Press(Find('_')).ch==L'_',"'_' types itself");
    Check(t.Press(Find(Action::Backspace)).send==Send::Backspace,"Backspace sends Backspace");
    t.Press(Find(Action::Shift));
    Check(t.Press(Find(Action::Enter)).send==Send::Enter&&!t.Shifted(),"Enter sends Enter and drops a waiting Shift");
    t.Press(Find(Action::Shift));
    out=t.Press(Find(Action::Close));
    Check(out.close&&out.send==Send::None&&!t.Shifted(),"Close hides the keyboard, sends nothing");
    Check(t.Press(-1).send==Send::None&&t.Press(KeyCount).send==Send::None,"no key: nothing");

    // The picture: every key painted, the gaps between them clear.
    const auto px=PaintKeys(false),lit=PaintKeys(true);
    Check(px.size()==size_t(TexelWidth)*TexelHeight*4&&lit.size()==px.size(),"one picture the keyboard's size, lit or not");
    for(int i=0;i<KeyCount;++i) {
        const Rect r=KeyTexelRect(i);
        const size_t middle=(size_t(r.y+r.h/2)*TexelWidth+r.x+r.w/2)*4;
        Check(px[middle+3]==255&&lit[middle+3]==255,"each key's middle is solid stone");
        Check(lit[middle]+lit[middle+1]+lit[middle+2]>=px[middle]+px[middle+1]+px[middle+2],"the lit copy is no darker");
    }
    Check(px[3]==0&&px[(size_t(KeyTexels)*TexelWidth+KeyTexels/4)*4+3]==0,"the gaps and the empty half key are clear");
    std::puts("VR keyboard layout, laser hits, typing and picture passed");
}
