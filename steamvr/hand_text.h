#pragma once
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Left-handed play (hand_role.h, #307): the words. Owner, 2026-11-09: "Did you make sure that the
// left-handed support also updates all the motion controls and their descriptions or any other description
// we have in the game for referencing right-hand things? ... motions that use right arm, for example, should
// be left arm ... Everything should be mirrored".
//
// Every text the mod shows is written once, for a right-handed player: "right" is the main hand (the weapon,
// the laser, A / B, the turn stick) and "left" the off hand (the shield, the menu, the wrist HUD, X / Y, the
// walk stick). While the hands are mirrored the same text is shown through Mirror, which exchanges the two
// sides word by word, so no sentence is kept twice and none can be forgotten in one of two copies:
//   right <-> left (Right / Left, RIGHT / LEFT), wherever it stands as a word of its own: "right hand",
//     "left-hand skills", "Right Grip", "the left controller", "your left shoulder", "RIGHT HAND'S";
//   a button named with its controller: Right A <-> Left X, Right B <-> Left Y (the letters are the usual
//     ones for that controller: A / B on the right, X / Y on the left);
//   the pairs A / B <-> X / Y, and A or B alone before what it does ("A picks up", "B cancels") or after
//     press / hold ("press B to cancel");
//   RT <-> LT, RB <-> LB.
// What says left or right without meaning a hand is kept (Kept): "time left", "left alone", "the left eye",
// "Right-click", "upper right", "Left-handed" itself. A text about a fixed, physical side (the Left-handed and
// Dominant hand rows) is simply not passed through.
//
// Right-handed, T and S give the text back untouched (the same pointer): nothing changes with the mode off.
// Value-only apart from the mode flag and T's pool: steamvr/hand_text_test.cpp.
namespace framevr::handtext {
namespace detail {
constexpr bool Letter(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
constexpr bool Word(char c){return Letter(c)||(c>='0'&&c<='9')||c=='_';}
// A whole word at i: not inside a longer one.
inline bool WordAt(std::string_view text,size_t i,std::string_view word) {
    if(text.compare(i,word.size(),word)!=0||i+word.size()>text.size())return false;
    if(i>0&&Word(text[i-1]))return false;
    const size_t end=i+word.size();
    return end>=text.size()||!Word(text[end]);
}
// Said without meaning a hand, or naming something that does not change sides.
inline constexpr std::string_view Kept[]={
    "Left-handed","left-handed","Right-handed","right-handed","LEFT-HANDED","RIGHT-HANDED","left_handed",
    "time left","s left","left out","left alone","left it","left open","left over","left to the game","left the chest",
    "left eye","right eye","Left eye","Right eye",
    "Right-click","right-click","Left-click","left-click",
    "upper right","upper left","lower right","lower left",
    "left or right","right or left","left and right","right and left","Left or right","Left and right",
};
struct Pair {std::string_view a,b;};
// Whole phrases exchanged (either way).
inline constexpr Pair Phrases[]={
    {"A / B","X / Y"},{"A or B","X or Y"},{"A and B","X and Y"},
    {"RT","LT"},{"RB","LB"},
};
inline constexpr Pair Sides[]={{"right","left"},{"Right","Left"},{"RIGHT","LEFT"}};
// What a button named by its letter alone does: "A picks up the item", "B cancels".
inline constexpr std::string_view ButtonVerbs[]={"picks","takes","cancels","closes","puts","does","also","opens","attacks","drinks","casts"};
constexpr char OtherButton(char c){return c=='A'?'X':c=='B'?'Y':c=='X'?'A':c=='Y'?'B':c;}
// A button's letter standing alone at i.
inline bool ButtonAt(std::string_view text,size_t i) {
    if(i>=text.size()||OtherButton(text[i])==text[i])return false;
    if(i>0&&Word(text[i-1]))return false;
    return i+1>=text.size()||!Word(text[i+1]);
}
inline bool VerbAfter(std::string_view text,size_t i) {   // i: just past the letter
    if(i>=text.size()||text[i]!=' ')return false;
    for(const auto verb:ButtonVerbs)if(WordAt(text,i+1,verb))return true;
    return false;
}
// "press B", "Hold Y": the letter at i is what is pressed.
inline constexpr std::string_view ButtonAsks[]={"press","Press","hold","Hold","pressing","holding"};
inline bool AskBefore(std::string_view text,size_t i) {
    if(i<2||text[i-1]!=' ')return false;
    for(const auto ask:ButtonAsks)if(i-1>=ask.size()&&WordAt(text,i-1-ask.size(),ask))return true;
    return false;
}
}
// The text for a player whose hands are exchanged. Its own inverse for what it changes.
inline std::string Mirror(std::string_view text) {
    using namespace detail;
    std::vector<bool> kept(text.size(),false);
    for(const auto phrase:Kept)
        for(size_t i=text.find(phrase);i!=std::string_view::npos;i=text.find(phrase,i+1))
            if(WordAt(text,i,phrase))for(size_t k=0;k<phrase.size();++k)kept[i+k]=true;
    std::string out;out.reserve(text.size()+8);
    size_t i=0;
    while(i<text.size()) {
        if(kept[i]){out.push_back(text[i++]);continue;}
        bool done=false;
        for(const auto& p:Phrases) {
            for(int way=0;way<2&&!done;++way) {
                const auto from=way?p.b:p.a,to=way?p.a:p.b;
                if(WordAt(text,i,from)){out.append(to);i+=from.size();done=true;}
            }
            if(done)break;
        }
        if(done)continue;
        for(const auto& p:Sides) {
            for(int way=0;way<2&&!done;++way) {
                const auto from=way?p.b:p.a,to=way?p.a:p.b;
                if(!WordAt(text,i,from))continue;
                out.append(to);i+=from.size();done=true;
                // "Right B", "Left X": the button's letter goes with its controller
                if(i+1<text.size()&&text[i]==' '&&!kept[i+1]&&ButtonAt(text,i+1)){out.push_back(' ');out.push_back(OtherButton(text[i+1]));i+=2;}
            }
            if(done)break;
        }
        if(done)continue;
        if(ButtonAt(text,i)&&(VerbAfter(text,i+1)||AskBefore(text,i))){out.push_back(OtherButton(text[i]));++i;continue;}
        out.push_back(text[i++]);
    }
    return out;
}
// The mode: the hands are mirrored now (handrole::State::map.left), so the words are. The transport sets
// it as the roles change; a weapon that plays right-handed whole (hand_role.h, Fallback) reads right-handed.
inline std::atomic<bool>& Mode(){static std::atomic<bool> left{false};return left;}
inline bool Left(){return Mode().load(std::memory_order_relaxed);}
inline void SetLeft(bool left){Mode().store(left,std::memory_order_relaxed);}
// A text as shown now: itself right-handed, mirrored left-handed.
inline std::string S(std::string_view text){return Left()?Mirror(text):std::string(text);}
// The same for a literal or a table's string, as a pointer that stays good: the text itself right-handed
// (and whenever mirroring changes nothing), else its mirrored copy, kept for the life of the process.
// M: the mirrored one whatever the mode (for a table mirrored once, ahead of time).
inline const char* M(const char* text) {
    if(!text)return text;
    static std::mutex mutex;
    static std::unordered_map<std::string,std::unique_ptr<std::string>> pool;
    std::lock_guard lock(mutex);
    const auto found=pool.find(text);
    if(found!=pool.end())return found->second?found->second->c_str():text;
    auto mirrored=Mirror(text);
    if(mirrored==text){pool.emplace(text,nullptr);return text;}
    return pool.emplace(text,std::make_unique<std::string>(std::move(mirrored))).first->second->c_str();
}
inline const char* T(const char* text){return Left()?M(text):text;}
// A pad binding's name, nothing but buttons ("B", "LT + X", "LT + RB"): each is the other controller's
// (A <-> X, B <-> Y, LT <-> RT, LB <-> RB). MirrorPad whatever the mode, Pad as shown now.
inline std::string MirrorPad(std::string_view name) {
    std::string out;
    for(size_t i=0;i<name.size();) {
        if(!detail::Word(name[i])){out.push_back(name[i++]);continue;}
        size_t end=i;while(end<name.size()&&detail::Word(name[end]))++end;
        const auto word=name.substr(i,end-i);
        if(word.size()==1)out.push_back(detail::OtherButton(word[0]));
        else if(word=="LT")out+="RT";else if(word=="RT")out+="LT";else if(word=="LB")out+="RB";else if(word=="RB")out+="LB";
        else out.append(word);
        i=end;
    }
    return out;
}
inline std::string Pad(std::string_view name){return Left()?MirrorPad(name):std::string(name);}
// Sets the mode for a scope (tests, previews).
struct Scope {
    bool was;
    explicit Scope(bool left):was(Left()){SetLeft(left);}
    ~Scope(){SetLeft(was);}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
}
