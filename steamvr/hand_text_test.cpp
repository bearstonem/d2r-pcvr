// Left-handed play's words (hand_text.h, #307): every text is written once, for a right-handed player,
// and shown through Mirror while the hands are mirrored. Here: the mirror itself (sides, buttons, what is
// kept), the mode's switch (off: every text untouched, the same pointer), and the texts that do not live
// in VR Settings - every motion's cue, every skill's usage line and card - each checked to change sides
// completely and to come back unchanged from a second mirror. VR Settings' rows, help and Guide are swept
// in vr_settings_layout_test.cpp, the controller labels in controller_labels_test.cpp.
#include "hand_text.h"
#include "spell_motion.h"
#include "skill_slots.h"
#include "skill_usage.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace framevr;
using handtext::Mirror;
static int checks=0;
static void Check(bool ok,const std::string& what){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",what.c_str());std::exit(1);}}
static void Reads(const char* written,const char* leftHanded) {
    const auto got=Mirror(written);
    Check(got==leftHanded,std::string("\"")+written+"\" reads \""+leftHanded+"\" left-handed, not \""+got+"\"");
    Check(Mirror(got)==written,std::string("and back: \"")+written+"\"");
}
static void Kept(const char* written){Check(Mirror(written)==written,std::string("kept as written: \"")+written+"\", not \""+Mirror(written)+"\"");}
// A whole word's count, any case.
static int Count(const std::string& text,const char* word) {
    const auto lower=[](std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;};
    const std::string t=lower(text),w=lower(word);int n=0;
    for(size_t i=t.find(w);i!=std::string::npos;i=t.find(w,i+1)) {
        const bool before=i==0||!handtext::detail::Word(t[i-1]),after=i+w.size()>=t.size()||!handtext::detail::Word(t[i+w.size()]);
        if(before&&after)++n;
    }
    return n;
}
// A text with no kept phrase in it: every right becomes a left and every left a right, and twice is itself.
static void Swaps(const std::string& text,const std::string& where) {
    const auto mirrored=Mirror(text);
    Check(Count(mirrored,"left")==Count(text,"right")&&Count(mirrored,"right")==Count(text,"left"),where+": every side changed in \""+text+"\" -> \""+mirrored+"\"");
    Check(Mirror(mirrored)==text,where+": mirrored twice is itself: \""+text+"\"");
    Check(mirrored.size()+Count(text,"right")==text.size()+Count(text,"left"),where+": nothing else changed");
}
int main() {
    // ---- sides ----
    Reads("right hand","left hand");Reads("Right hand","Left hand");Reads("RIGHT HAND'S","LEFT HAND'S");
    Reads("Your left-hand skills","Your right-hand skills");Reads("Left-hand strikes","Right-hand strikes");
    Reads("How far each right-stick flick turns you","How far each left-stick flick turns you");
    Reads("Reach your left hand over your left shoulder and grip","Reach your right hand over your right shoulder and grip");
    Reads("Feel it: Weapon buzzes the right controller, Shield the left.","Feel it: Weapon buzzes the left controller, Shield the right.");
    Reads("Turn your left wrist toward your face, like checking a watch.","Turn your right wrist toward your face, like checking a watch.");
    Reads("Raise your open left palm toward you, grip and trigger released.","Raise your open right palm toward you, grip and trigger released.");
    Reads("Grip + right trigger","Grip + left trigger");Reads("Left Trigger","Right Trigger");Reads("Right Grip","Left Grip");
    Reads("The left stick walks where you look, or where the left controller points","The right stick walks where you look, or where the right controller points");
    Reads("Druid or dual wield: the left claw or blade casts its own trigger skill or the right trigger's",
          "Druid or dual wield: the right claw or blade casts its own trigger skill or the left trigger's");
    Reads("The skill a circle drawn with the left hand casts; -1 the right hand's","The skill a circle drawn with the right hand casts; -1 the left hand's");
    Reads("Make sure the menu left edge is held in the hand","Make sure the menu right edge is held in the hand");
    Reads(" (right arm)"," (left arm)");Reads("left","right");Reads("Right trigger skill","Left trigger skill");
    // ---- the motions' words (the owner: "motions that use right arm, for example, should be left arm") ----
    Reads("right arm out, aim the controller's arc where you want to land, then yank the fist back",
          "left arm out, aim the controller's arc where you want to land, then yank the fist back");
    Reads("sweep the left hand from the right hand up to the right shoulder","sweep the right hand from the left hand up to the left shoulder");
    Reads("right fist to the mouth a moment, then thrust it straight up over the head","left fist to the mouth a moment, then thrust it straight up over the head");
    Reads("draw the right fist in, then punch it straight out at a monster","draw the left fist in, then punch it straight out at a monster");
    // ---- buttons ----
    Reads("Hold both triggers when asked; Right B cancels.","Hold both triggers when asked; Left Y cancels.");
    Reads("Right laser to use it; Right B or its close button puts it away.","Left laser to use it; Left Y or its close button puts it away.");
    Reads("Hold Left Y while editing","Hold Right B while editing");Reads("Right A","Left X");Reads("Left X","Right A");
    Reads("Right B: skip / cancel. Reopen any time","Left Y: skip / cancel. Reopen any time");
    Reads(" not applied. Right B: Keep editing."," not applied. Left Y: Keep editing.");
    Reads("A picks up the item you point at, not the nearest","X picks up the item you point at, not the nearest");
    Reads("Hold your arm out and point at an item: A picks up that one, not the nearest, and its name shows. Too far away, A takes nothing.",
          "Hold your arm out and point at an item: X picks up that one, not the nearest, and its name shows. Too far away, X takes nothing.");
    Reads("check SteamVR room setup, or press B to skip.","check SteamVR room setup, or press Y to skip.");
    Reads("Triggers, grips, A / B with X / Y and the sticks swap","Triggers, grips, X / Y with A / B and the sticks swap");
    Reads("RT clicks, LT is Ctrl, RB grabs","LT clicks, RT is Ctrl, LB grabs");
    // a letter that is no button
    Kept("A bow, crossbow, staff, spear, polearm or two-handed weapon");Kept("A hit feels like what you struck");
    Kept("A weapon or fist this close beside a monster still hits");Kept("A button you hold keeps showing the monster its cast took");
    Kept("A casting hand held out and pointing aims alone");Kept("Set I / II");Kept("X 3");
    // ---- kept: not a hand ----
    Kept("It goes away after 6 seconds; the bar shows the time left.");Kept("still recharging (0.6 s left)");
    Kept("Fallen bodies are left alone.");Kept("the buzz stopping means you left it and the 2 seconds start over");
    Kept("Monitor shows the left eye only; one extra GPU copy per frame");Kept("Right-click in game menus");
    Kept("choose them on the GLYPHS page (the box at the upper right of the wheel)");
    Kept("keeping its direction left or right");Kept("Stick left or right");
    Kept("Left-handed (experimental)");Kept("the roles of a kind that is not mirrored stay right-handed");
    Kept("upright");Kept("brightness");Kept("copyright");Kept("leftover");Kept("Stand upright and look straight ahead.");
    Reads("Fallen bodies are left alone, your left hand is not.","Fallen bodies are left alone, your right hand is not.");
    // ---- a pad binding's name ----
    Check(handtext::MirrorPad("B")=="Y"&&handtext::MirrorPad("X")=="A"&&handtext::MirrorPad("RB")=="LB"&&handtext::MirrorPad("LT + B")=="RT + Y"&&
          handtext::MirrorPad("LT + RB")=="RT + LB"&&handtext::MirrorPad("LT + A")=="RT + X","a pad binding is the other controller's, button by button");
    for(int slot=0;slot<skills::Count;++slot){const std::string name=skills::NativeLabel(slot);Check(handtext::MirrorPad(handtext::MirrorPad(name))==name&&handtext::MirrorPad(name)!=name,"every native slot name changes and comes back: "+name);}
    // ---- the mode ----
    {
        const char* written="Right hand: point & trigger";
        Check(!handtext::Left(),"right-handed until told");
        Check(handtext::T(written)==written&&handtext::S(written)==written&&handtext::Pad("LT + B")=="LT + B","OFF: the text itself, the same pointer, byte for byte");
        Check(handtext::T(nullptr)==nullptr,"no text, none");
        {
            handtext::Scope left(true);
            const char* shown=handtext::T(written);
            Check(std::string(shown)=="Left hand: point & trigger"&&handtext::S(written)=="Left hand: point & trigger","left-handed: mirrored");
            Check(handtext::T(written)==shown,"the mirrored text is kept: the same pointer each time");
            const char* plain="Point & trigger";
            Check(handtext::T(plain)==plain,"a text with no side in it is itself either way");
            Check(handtext::Pad("LT + B")=="RT + Y","and a pad binding with it");
            Check(std::string(handtext::M("left"))=="right","M mirrors whatever the mode");
        }
        Check(!handtext::Left()&&handtext::T(written)==written,"and off again after the scope");
        Check(std::string(handtext::M(written))=="Left hand: point & trigger","M mirrors with the mode off too (a table mirrored ahead of time)");
    }
    // ---- Sticks stay right-handed (hand_role.h Sticks): the hands mirrored, the sticks' names kept ----
    {
        const auto keeps=[](const char* written,const char* shown){
            const auto got=Mirror(written,false);
            Check(got==shown,std::string("sticks kept: \"")+written+"\" reads \""+shown+"\", not \""+got+"\"");
            Check(Mirror(got,false)==written,std::string("sticks kept, and back: \"")+written+"\"");
        };
        keeps("On turns you smoothly with the right stick.","On turns you smoothly with the right stick.");
        keeps("How far each right-stick flick turns you","How far each right-stick flick turns you");
        keeps("Hold the right stick down to show every nearby name.","Hold the right stick down to show every nearby name.");
        keeps("Look: the left stick walks where you look. Controller: where that stick's controller points",
              "Look: the left stick walks where you look. Controller: where that stick's controller points");
        keeps("Right stick up switches between walking and running","Right stick up switches between walking and running");
        // the hands, buttons and controllers around them still change sides
        keeps("Point the right controller at the map and push the right stick up to zoom","Point the left controller at the map and push the right stick up to zoom");
        keeps("Right hand: the left stick walks; Right B cancels","Left hand: the left stick walks; Left Y cancels");
        keeps("Left Grip + left stick click","Right Grip + left stick click");   // no in-headset text names a hand's own click by side
        Check(Mirror("Zoom: right stick up / down.")=="Zoom: left stick up / down.","with the sticks mirrored, as before");
        const char* turn="On turns you smoothly with the right stick.";const char* hand="Right hand: point & trigger";
        Check(!handtext::SticksKept(),"not until told");
        {
            handtext::Scope kept(true,true);
            Check(handtext::Left()&&handtext::SticksKept(),"the scope sets both");
            Check(handtext::T(turn)==turn&&std::string(handtext::T(hand))=="Left hand: point & trigger"&&handtext::S("the right stick")=="the right stick",
                  "T and S: the stick as written, the hand mirrored");
            Check(handtext::T(hand)==handtext::T(hand)&&handtext::Pad("LT + B")=="RT + Y","the same pointer each time; the buttons still mirrored");
        }
        {
            handtext::Scope plain(true);
            Check(!handtext::SticksKept()&&std::string(handtext::T(turn))=="On turns you smoothly with the left stick.","left-handed alone: the sticks mirrored");
        }
        {
            handtext::Scope off(false,true);
            Check(!handtext::SticksKept()&&handtext::T(turn)==turn&&handtext::T(hand)==hand,"right-handed the sticks' mode changes nothing");
        }
        Check(!handtext::Left()&&!handtext::SticksKept(),"and off again after the scopes");
        Check(std::string(handtext::M(turn))=="On turns you smoothly with the left stick."&&handtext::M(turn,false)==turn,"M: the two mirrors, whatever the mode");
    }
    // ---- every motion's cue, every class's: no hand, arm, fist or shoulder keeps its side ----
    int cues=0,sided=0;
    for(int m=1;m<=spellmotion::MotionCount;++m) {
        const std::string cue=spellmotion::Cue(spellmotion::Motion(m));
        Check(!cue.empty()&&cue!="no motion","motion "+std::to_string(m)+" has a cue");
        Swaps(cue,"motion "+std::to_string(m)+"'s cue");++cues;
        if(Mirror(cue)!=cue)++sided;
    }
    Check(cues==spellmotion::MotionCount&&sided>=14,"the one-sided motions read the other way round (" + std::to_string(sided) + " of " + std::to_string(cues) + ")");
    for(int c=0;c<7;++c){const auto page=spellmotion::PageOf(c);for(int i=0;i<page.count;++i)Swaps(spellmotion::CardArm(page.skills[i]),"a card's arm");}
    Check(Mirror(spellmotion::CardArm(75))==" (left arm)"&&Mirror(spellmotion::CardArm(85))==" (right arm)","the golems' cards name the arm that raises them: the main arm's is the left then");
    // ---- every skill's usage line and card, on every slot ----
    int lines=0,changed=0;
    for(int skill=0;skill<400;++skill)for(int slot=0;slot<skills::Count;++slot) {
        const std::string line=skillusage::Line(skill,slot),card=skillusage::Details(skill,slot);
        Swaps(line,"skill "+std::to_string(skill)+" line");Swaps(card,"skill "+std::to_string(skill)+" card");
        lines+=2;if(Mirror(line)!=line)++changed;if(Mirror(card)!=card)++changed;
    }
    Check(changed>0,"some usage texts name a side, and change");
    std::printf("hand text checks passed: %d (%d motion cues, %d usage texts, %d of them sided)\n",checks,cues,lines,changed);
    return 0;
}
