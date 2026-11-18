#include "vr_keyboard.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
using namespace framevr;
static void Check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
// Where a ray from `eye` through key units (col, row) of the board would hit it.
static math::Vec OnBoard(const keyboard::Layout& l,const keyboard::Board& b,float col,float row) {
    const float unit=b.height/float(l.rows);
    return b.position+math::Rotate(b.orientation,{col*unit-b.width*.5f,b.height*.5f-row*unit,0});
}
static bool Near(math::Vec a,math::Vec b){const auto d=a-b;return math::Dot(d,d)<1e-8f;}
static int Find(const keyboard::Layout& l,char ch){for(int i=0;i<l.count;++i)if(l.keys[i].action==keyboard::Action::Char&&l.keys[i].ch==ch)return i;return -1;}
static int Find(const keyboard::Layout& l,keyboard::Action a){for(int i=0;i<l.count;++i)if(l.keys[i].action==a)return i;return -1;}
// Every key inside the board, none over another, its picture inside the layout's.
static void CheckLayout(const keyboard::Layout& l) {
    using namespace keyboard;
    for(auto a:{Action::Backspace,Action::Enter,Action::Shift,Action::Close})Check(Find(l,a)>=0,"Backspace, Enter, Shift and Close have keys");
    for(int i=0;i<l.count;++i) {
        const Key& k=l.keys[i];
        Check(k.col>=0&&k.col+k.span<=Columns+1e-4f&&k.row>=0&&k.row+1<=float(l.rows),"every key inside the board");
        for(int j=i+1;j<l.count;++j)
            if(l.keys[j].row==k.row)Check(l.keys[j].col>=k.col+k.span-1e-4f||k.col>=l.keys[j].col+l.keys[j].span-1e-4f,"no two keys overlap");
        if(k.action==Action::Char)
            for(int j=i+1;j<l.count;++j)Check(l.keys[j].action!=Action::Char||l.keys[j].ch!=k.ch,"each character on one key");
        const Rect r=KeyTexelRect(l,i);
        Check(r.w>0&&r.h>0&&r.x>=0&&r.y>=0&&r.x+r.w<=TexelWidth&&r.y+r.h<=TexelHeight(l),"every key's picture inside the keyboard's");
    }
    for(int row=0;row<l.rows;++row) {   // each row filled edge to edge but for A-L's half-key indent at both ends
        float span=0;for(int i=0;i<l.count;++i)if(l.keys[i].row==float(row))span+=l.keys[i].span;
        Check(span>=Columns-1-1e-4f&&span<=Columns+1e-4f,"every row as wide as the board");
    }
    Check(std::fabs(float(TexelWidth)/TexelHeight(l)-Columns/float(l.rows))<1e-4f,"the picture has the board's aspect");
}
// The laser, through the board's own intersection from the eyes: each key's middle hits that key,
// and its lit copy lies over it.
static void CheckAim(const keyboard::Layout& l,const keyboard::Board& b,math::Vec eye) {
    using namespace keyboard;
    for(int i=0;i<l.count;++i) {
        const auto at=OnBoard(l,b,l.keys[i].col+l.keys[i].span*.5f,l.keys[i].row+.5f);
        math::PanelHit hit{};
        Check(math::IntersectPanel(eye,math::Normalized(at-eye),b.position,b.orientation,b.width,b.height,hit),"the laser reaches the board from the eyes");
        Check(KeyAt(l,hit.u,hit.v)==i,"a key's middle hits that key");
        Check(Near(hit.point,at),"where the laser says it hit");
        const Spot k=KeySpot(l,b,i);
        Check(Near(b.position+math::Rotate(b.orientation,{k.x,k.y,0}),at),"the lit copy lies over the key it lights");
        Check(k.width>0&&k.height>0&&k.height<b.height/float(l.rows),"the lit copy is the key's size, not a whole row");
    }
}
// Its picture: every key painted, the gaps between them clear, the waiting Shift lit.
static void CheckPicture(const keyboard::Layout& l) {
    using namespace keyboard;
    const auto px=PaintKeys(l,false),lit=PaintKeys(l,true),caps=PaintKeys(l,false,true);
    {
        const Rect r=KeyTexelRect(l,Find(l,Action::Shift)),q=KeyTexelRect(l,Find(l,'q'));
        const size_t shift=(size_t(r.y+2)*TexelWidth+r.x+r.w/2)*4,letter=(size_t(q.y+2)*TexelWidth+q.x+q.w/2)*4;   // their rims
        Check(caps[shift]>px[shift]&&caps[letter]==px[letter],"with capitals the waiting Shift is lit, nothing else");
    }
    Check(px.size()==size_t(TexelWidth)*TexelHeight(l)*4&&lit.size()==px.size(),"one picture the keyboard's size, lit or not");
    for(int i=0;i<l.count;++i) {
        const Rect r=KeyTexelRect(l,i);
        const size_t middle=(size_t(r.y+r.h/2)*TexelWidth+r.x+r.w/2)*4;
        Check(px[middle+3]==255&&lit[middle+3]==255,"each key's middle is solid stone");
        Check(lit[middle]+lit[middle+1]+lit[middle+2]>=px[middle]+px[middle+1]+px[middle+2],"the lit copy is no darker");
    }
    Check(px[3]==0,"the gap at the top left corner is clear");
}
int main(){
    using namespace keyboard;
    const Layout& N=Name;
    // The name's keys: every letter, the two marks a name may hold, nothing a name may not.
    for(char c='a';c<='z';++c)Check(Find(N,c)>=0,"every letter has a key");
    Check(Find(N,'-')>=0&&Find(N,'_')>=0,"the two marks a name may hold have keys");
    Check(Find(N,'1')<0&&Find(N,' ')<0&&N.rows==4&&N.count==32,"the name keyboard as before: no digits, no space, four rows");
    CheckLayout(N);
    // The multiplayer panel's: the name's letters, digits, what an address and port take, a space.
    const Layout& T=Text;
    for(char c='a';c<='z';++c)Check(Find(T,c)>=0,"every letter on the multiplayer keyboard too");
    for(char c='0';c<='9';++c)Check(Find(T,c)>=0&&T.keys[Find(T,c)].row==0,"a digit row on top");
    for(char c:{'.',':','-','_',' '})Check(Find(T,c)>=0,"the marks an address, a port or a game name take, and a space");
    Check(T.rows==N.rows+1,"one row more than the name's");
    CheckLayout(T);

    // Under the menu, out of the game's picture: tilted toward the player, facing up to the eyes.
    const math::Vec menu{0,1.6f,-1.5f};const math::Quat facing{0,0,0,1};   // a panel straight ahead, facing the player
    const math::Vec eye{0,1.7f,0};
    for(const Layout* l:{&N,&T})
    for(auto size:{std::pair{2.0f,1.125f},std::pair{1.125f,1.125f}}) {    // 16:9, and a square eye target's panel
        const float width=size.first,height=size.second;
        const Board b=Place(*l,menu,facing,width,height);
        Check(b.width>0&&b.height>0&&std::fabs(b.width/b.height-Columns/float(l->rows))<1e-4f,"the quad has the picture's aspect");
        Check(std::fabs(b.width-WidthOfWorkspace*2.0f)<1e-4f,"the same size on a wide or a square panel (the 16:9 workspace's width)");
        const auto top=OnBoard(*l,b,Columns*.5f,0),bottom=OnBoard(*l,b,Columns*.5f,float(l->rows));
        Check(top.y<menu.y-height*.5f&&top.y>menu.y-height*.5f-.1f,"its top edge just under the panel's bottom edge");
        Check(std::fabs(top.z-menu.z)<1e-4f,"hinged in the panel's plane");
        Check(bottom.y<top.y&&bottom.z>top.z,"its lower edge lower and nearer the player: a keyboard on a desk");
        const auto normal=math::Rotate(b.orientation,{0,0,1});
        Check(normal.y>0&&normal.z>0&&std::fabs(std::acos(normal.z)*180/3.14159265f-TiltDegrees)<.01f,"facing up toward the eyes, tilted Tilt");
        Check(std::fabs(OnBoard(*l,b,0,0).x+b.width*.5f)<1e-4f,"centred under the menu");
        CheckAim(*l,b,eye);
        Check(!Place(*l,menu,facing,0,1).width&&!Place(*l,menu,facing,1,0).width,"no panel, no keyboard");
    }
    {   // The name board as it was: four rows, its height the same as before the second layout.
        const Board b=Place(N,menu,facing,2.0f,1.125f);
        Check(std::fabs(b.height-WidthOfWorkspace*2.0f*4/10)<1e-4f,"the name keyboard's size unchanged");
    }
    // Over the main menu's 3D board ([frontend] defaults: 1.65 m wide, 1 m ahead, 0.65 m under the eyes,
    // leaning back to face them): standing upright on its top edge, at about eye height, facing the player.
    {
        const float distance=1.0f,drop=.65f,width=1.65f,height=width*9/16,lean=std::atan2(drop,distance);
        const math::Vec head{0,1.7f,0};
        const math::Quat leanBack{-std::sin(lean*.5f),0,0,std::cos(lean*.5f)};   // panels::Pitch(-lean)
        const math::Vec board=head+math::Vec{0,-drop,-distance};
        const Board under=Place(T,board,leanBack,width,height),b=PlaceAbove(T,board,leanBack,width,height,lean);
        Check(OnBoard(T,under,Columns*.5f,float(T.rows)).y<head.y-1.f,"under that board the keys would be over a metre under the eyes");
        Check(b.width>0&&std::fabs(b.width/b.height-Columns/float(T.rows))<1e-4f,"the quad has the picture's aspect");
        Check(std::fabs(b.width-AboveWidthOfWorkspace*width)<1e-4f&&b.width<under.width,"a little smaller than under a panel");
        const auto boardTop=board+math::Rotate(leanBack,{0,height*.5f,0});
        const auto low=OnBoard(T,b,Columns*.5f,float(T.rows)),high=OnBoard(T,b,Columns*.5f,0);
        Check(low.y>boardTop.y&&low.y<boardTop.y+.1f&&std::fabs(low.z-boardTop.z)<.05f,"its lower edge just over the board's top edge");
        Check(high.y>low.y&&std::fabs(high.z-low.z)<1e-4f,"standing upright");
        Check(high.y>head.y-.25f&&high.y<head.y+.25f,"its top about eye height");
        const auto normal=math::Rotate(b.orientation,{0,0,1});
        Check(std::fabs(normal.y)<1e-4f&&normal.z>.999f,"facing the player");
        Check(std::fabs(b.position.x-board.x)<1e-4f,"centred over the board");
        CheckAim(T,b,head);
        Check(!PlaceAbove(T,board,leanBack,0,1,lean).width&&!PlaceAbove(T,board,leanBack,1,0,lean).width,"no board, no keyboard");
        // The laser at the board under it does not reach the keyboard first (and the other way round).
        math::PanelHit hit{};
        Check(!math::IntersectPanel(head,math::Normalized(board-head),b.position,b.orientation,b.width,b.height,hit),"aiming at the board misses the keyboard");
    }
    Check(KeyAt(N,.1f,.5f/4)==-1,"the gap between Q and W hits nothing (no press on the wrong letter)");
    Check(KeyAt(N,.2f/Columns,1.5f/4)==-1,"the empty half key left of A hits nothing");
    Check(KeyAt(N,.05f,.125f)==Find(N,'q')&&KeyAt(N,.95f,.875f)==Find(N,Action::Enter),"the corners: Q and Enter");
    Check(KeyAt(T,.05f,.1f)==Find(T,'1')&&KeyAt(T,.05f,.3f)==Find(T,'q')&&KeyAt(T,.95f,.9f)==Find(T,Action::Enter),"the corners: 1, Q under it, and Enter");
    // Labels: small letters, capitals while Shift waits; digits their symbol; other keys their names.
    Check(Label(N,Find(N,'k'),false)==L"k"&&Label(N,Find(N,'k'),true)==L"K","a letter's key shows it small, a capital with Shift");
    Check(Label(N,Find(N,'-'),true)==L"-"&&Label(N,Find(N,'_'),false)==L"_","marks the same either way");
    Check(Label(N,Find(N,Action::Enter),true)==L"Enter"&&Label(N,-1,false).empty()&&Label(N,N.count,false).empty(),"other keys their name");
    Check(Label(T,Find(T,'1'),false)==L"1"&&Label(T,Find(T,'1'),true)==L"!"&&Label(T,Find(T,'0'),true)==L")","a digit, and its symbol with Shift");
    Check(Label(T,Find(T,'.'),true)==L"."&&Label(T,Find(T,':'),false)==L":"&&Label(T,Find(T,' '),true)==L"Space","the address marks the same either way; the space bar says so");

    // Typing: lower case, Shift once for a capital, Backspace, Enter, Close.
    {
        Typist t;
        auto out=t.Press(N,Find(N,'k'));
        Check(out.send==Send::Char&&out.ch==L'k'&&!out.close,"a letter types its lower case");
        Check(t.Press(N,Find(N,Action::Shift)).send==Send::None&&t.Shifted(),"Shift sends nothing and waits for a letter");
        out=t.Press(N,Find(N,'k'));
        Check(out.send==Send::Char&&out.ch==L'K'&&!t.Shifted(),"Shift makes one capital, then letters are small again");
        Check(t.Press(N,Find(N,'k')).ch==L'k',"the next letter is small");
        t.Press(N,Find(N,Action::Shift));t.Press(N,Find(N,Action::Shift));
        Check(!t.Shifted(),"Shift twice takes it back");
        t.Press(N,Find(N,Action::Shift));
        out=t.Press(N,Find(N,'-'));
        Check(out.send==Send::Char&&out.ch==L'-'&&!t.Shifted(),"Shift leaves '-' as it is, and is used up");
        Check(t.Press(N,Find(N,'_')).ch==L'_',"'_' types itself");
        Check(t.Press(N,Find(N,Action::Backspace)).send==Send::Backspace,"Backspace sends Backspace");
        t.Press(N,Find(N,Action::Shift));
        Check(t.Press(N,Find(N,Action::Enter)).send==Send::Enter&&!t.Shifted(),"Enter sends Enter and drops a waiting Shift");
        t.Press(N,Find(N,Action::Shift));
        out=t.Press(N,Find(N,Action::Close));
        Check(out.close&&out.send==Send::None&&!t.Shifted(),"Close hides the keyboard, sends nothing");
        Check(t.Press(N,-1).send==Send::None&&t.Press(N,N.count).send==Send::None,"no key: nothing");
    }
    {   // An address and port, and a passworded game's password, typed on the multiplayer keyboard.
        Typist t;std::wstring typed;
        const auto type=[&](const char* keys){
            for(const char* c=keys;*c;++c) {
                const auto out=*c=='^'?t.Press(T,Find(T,Action::Shift)):t.Press(T,Find(T,*c));
                if(out.send==Send::Char)typed+=out.ch;
            }
        };
        type("192.168.1.20:4000");
        Check(typed==L"192.168.1.20:4000","an address and its port");
        typed.clear();type("^my game 2");
        Check(typed==L"My game 2","a game's name: a capital, a space, a digit");
        typed.clear();type("^1^2pw");
        Check(typed==L"!@pw"&&!t.Shifted(),"Shift on a digit types its symbol, once");
        Check(t.Press(T,Find(T,Action::Backspace)).send==Send::Backspace&&t.Press(T,Find(T,Action::Enter)).send==Send::Enter,
              "Backspace and Enter on the multiplayer keyboard");
        Check(t.Press(T,Find(T,Action::Close)).close,"its Close hides it");
    }
    CheckPicture(N);
    CheckPicture(T);
    {
        const auto px=PaintKeys(N,false);
        Check(px[(size_t(KeyTexels)*TexelWidth+KeyTexels/4)*4+3]==0,"the empty half key left of A is clear");
    }

    // The name box, from the game's UI messages as the 2026-11-06 log has them.
    {
        NameBox box;
        Check(!box.Shown(),"no character creation: no keyboard");
        box.Message("PanelManager","OpenPanel","CharacterSelectPanel");
        box.Message("CharacterCreate","Select","Barbarian");
        Check(!box.Shown(),"the character list, or a stray Select outside creation: no keyboard");
        box.Message("PanelManager","OpenExclusivePanel","CharacterCreatePanel");
        box.Message("PanelManager","OpenPanel","CandidateListPanel");
        Check(!box.Shown(),"at the campfire before a hero is picked: no name box, no keyboard");
        box.Message("CharacterCreate","Hover","Assassin");box.Message("CharacterCreate","Unhover","Assassin");
        Check(!box.Shown(),"a hero only hovered: no keyboard");
        const auto e0=box.Epoch();
        box.Message("CharacterCreate","Select","Necromancer");
        Check(box.Shown()&&box.Epoch()!=e0,"a hero picked: the name box is up, the keyboard shows");
        const auto e1=box.Epoch();
        box.Message("InputMessage","MouseMove","");box.Message("CharacterCreate","Hover","Paladin");
        Check(box.Shown()&&box.Epoch()==e1,"pointing about keeps it, and a closed keyboard stays closed");
        box.Message("CharacterCreate","Select","Barbarian");box.Message("CharacterCreate","Unselect","Necromancer");
        Check(box.Shown()&&box.Epoch()!=e1,"another hero (Select before the old one's Unselect): still shown, a closed keyboard back");
        box.Message("CharacterCreate","Unselect","Barbarian");
        Check(!box.Shown(),"the hero put back: the name box goes, the keyboard with it");
        box.Message("CharacterCreate","Select","Sorceress");
        Check(box.Shown(),"picked again");
        box.Message("FrontEndNavigation","ToCharacterSelect","");box.Message("PanelManager","UnloadPanel","CharacterCreatePanel");
        Check(!box.Shown(),"back to the list: gone");
        box.Message("CharacterCreate","Select","Sorceress");
        Check(!box.Shown(),"a late Select after the panel went: still gone");
        box.Message("PanelManager","OpenExclusivePanel","CharacterCreatePanel");
        Check(!box.Shown(),"creation opened again starts with no hero picked");
        box.Message("CharacterCreate","Select","Amazon");box.Message("PanelManager","UnloadAll","");
        Check(!box.Shown(),"every panel unloaded (a game starting): gone");
    }
    // D2RLoader's multiplayer panel, from the UI messages of the owner's 2026-11-17 session.
    {
        NameBox name;MultiplayerPanel mp;
        const auto both=[&](const char* target,const char* command,const char* text){name.Message(target,command,text);mp.Message(target,command,text);};
        const auto want=[&]{return Wanted(name,mp);};
        both("FrontEndNavigation","ToFrontEnd","");
        both("PanelManager","OpenExclusivePanel","MainMenuPanel");both("PanelManager","OpenPanel","CharacterSelectPanel");
        Check(!mp.Shown()&&want()==Want::None,"the main menu alone: no keyboard");
        const auto e0=mp.Epoch();
        both("PanelManager","OpenPanel","D2RLoaderMultiplayerPanel");
        Check(mp.Shown()&&mp.Epoch()!=e0&&want()==Want::Text,"the multiplayer panel opened: the keyboard with digits");
        const auto e1=mp.Epoch();
        both("D2RLoaderMultiplayerPanelMessage","FilterHostGameName","");both("D2RLoaderMultiplayerPanelMessage","LimitPasswords","");
        both("D2RLoaderMultiplayerPanelMessage","ToggleLanOnly","");both("D2RLoaderMultiplayerPanelMessage","IncrementMaxPlayers","");
        both("InputMessage","MouseMove","");both("MainMenuHD","UpdateButtonLegend","");
        Check(mp.Shown()&&mp.Epoch()==e1,"its boxes' own text, a checkbox, a stepper, the pointer: a closed keyboard stays closed");
        both("D2RLoaderMultiplayerPanelMessage","ShowBrowse","");both("PanelManager","OpenPanel","CandidateListPanel");
        Check(mp.Shown()&&mp.Epoch()!=e1,"Browse Games (its filter): still up, a closed keyboard back");
        const auto e2=mp.Epoch();
        both("D2RLoaderMultiplayerPanelMessage","ShowHost","");
        Check(mp.Shown()&&mp.Epoch()!=e2,"Host Game: still up, a closed keyboard back");
        const auto e3=mp.Epoch();
        both("D2RLoaderMultiplayerPanelMessage","OpenDirectJoin","");
        Check(mp.Shown()&&mp.Epoch()!=e3,"Direct join's address and port: a closed keyboard back");
        const auto e4=mp.Epoch();
        both("D2RLoaderMultiplayerPanelMessage","CloseDirectJoin","");
        Check(mp.Shown()&&mp.Epoch()!=e4,"Direct join closed, back on the panel's view: still up");
        Check(MultiplayerPanel::NewView("ShowGameInfo")&&MultiplayerPanel::NewView("CloseJoinPassword")&&MultiplayerPanel::NewView("JoinSelected")&&
              !MultiplayerPanel::NewView("FilterGames")&&!MultiplayerPanel::NewView("ToggleUnlisted")&&!MultiplayerPanel::NewView("RefreshGames"),
              "views and dialogs bring a closed keyboard back; toggles, steppers and box texts do not");
        // Create New in the character list beside it: character creation's name box goes first.
        both("PanelManager","OpenExclusivePanel","CharacterCreatePanel");
        Check(mp.Open()&&!mp.Shown()&&want()==Want::None,"character creation over it: no multiplayer keyboard at the campfire");
        both("CharacterCreate","Select","Druid");
        Check(want()==Want::Name,"a hero picked there: the name keyboard");
        both("PanelManager","UnloadPanel","CharacterCreatePanel");
        Check(mp.Shown()&&want()==Want::Text,"creation left with the multiplayer panel still open: its keyboard back");
        both("PanelManager","ClosePanel","D2RLoaderMultiplayerPanel");
        Check(!mp.Shown()&&want()==Want::None,"the panel closed (its own close, Right B, or a game made): gone");
        both("D2RLoaderMultiplayerPanelMessage","ShowHost","");
        Check(!mp.Shown(),"a late view message after the panel went: still gone");
        both("PanelManager","OpenPanel","D2RLoaderMultiplayerPanel");
        Check(mp.Shown(),"opened again");
        both("MainMenuHD","Play","");both("FrontEndNavigation","ToGame","");both("PanelManager","UnloadAll","");
        Check(!mp.Shown()&&want()==Want::None,"a game starting: gone, never in the game");
        both("PanelManager","OpenPanel","D2RLoaderMultiplayerPanel");both("FrontEndNavigation","ToExit","");
        Check(!mp.Shown(),"quitting from it: gone");
        both("PanelManager","OpenPanel","D2RLoaderSettingsPanel");both("PanelManager","OpenPanel","TCPIPPanel");
        both("PanelManager","OpenPanel","ConfirmationModal");both("PanelManager","OpenPanel","PlayerInventoryExpansionLayout");
        Check(want()==Want::None,"unrelated panels: no keyboard");
    }
    {   // the game's pointer while the multiplayer keyboard is up
        constexpr auto free=ParkedPointer(false,false,false),freePull=ParkedPointer(false,true,false);
        static_assert(free.move&&!free.jump&&freePull.move&&!freePull.jump,"not parked: the pointer follows the laser, clicks at once");
        constexpr auto rest=ParkedPointer(true,false,false);
        static_assert(!rest.move&&!rest.jump,"parked: the laser passing over the panel's buttons moves nothing (a box keeps its focus)");
        constexpr auto pull=ParkedPointer(true,true,false);
        static_assert(pull.move&&pull.jump,"parked, a pull: the pointer to the laser first, its click the frame after");
        constexpr auto drag=ParkedPointer(true,false,true);
        static_assert(drag.move&&!drag.jump,"parked with a button held: it follows (a drag), then stays where let go");
        Check(true,"parked pointer");
    }
    std::puts("VR keyboards (name and multiplayer) layout, laser hits, typing, picture and when they show passed");
}
