#include "controller_labels.h"
#include "interaction_paint.h"
#include <cassert>
#include <cstdio>
using namespace framevr;
int main(){
    using namespace controllers;
    assert(Detect("/interaction_profiles/oculus/touch_controller")==Layout::Touch);
    assert(Detect("/interaction_profiles/meta/touch_controller_plus")==Layout::Touch);
    assert(Detect("/interaction_profiles/facebook/touch_controller_pro")==Layout::Touch);
    assert(Detect("/interaction_profiles/valve/index_controller")==Layout::Index);
    assert(Detect(FrameProfile)==Layout::Frame);
    assert(Detect("")==Layout::Generic&&Detect("/unknown/touch_controller")==Layout::Generic);
    Labels detected{{Layout::Touch,Layout::Index}};
    const auto automatic=Resolve(Choice::Automatic,detected);
    assert(automatic==detected); // Mixed controllers, not an HMD-brand guess.
    assert(automatic.Slot(5)=="Left A"&&automatic.Slot(9)=="Left Grip + B");
    const auto quest=Resolve(Choice::Quest,detected);
    assert(quest.Slot(5)=="Left X"&&quest.Slot(6)=="Left Y");
    assert(quest.Slot(7)=="Left Grip + X"&&quest.Slot(9)=="Left Grip + Y");
    assert(quest.Slot(8)=="Right A"&&quest.Slot(1)=="Right B");
    assert(quest.Slot(2)=="Right Grip + A"&&quest.Slot(3)=="Right Grip + B");
    assert(quest.Slot(0)=="Right Trigger"&&quest.Slot(4)=="Left Trigger");
    const auto frame=Resolve(Choice::Frame,detected);
    assert(frame.Slot(5)=="Left D-pad Down"&&frame.Slot(9)=="Left Grip + D-pad Up");
    assert(std::string(FacePath(Layout::Frame,1,false))=="dpad_down/click");
    assert(std::string(FacePath(Layout::Frame,1,true))=="dpad_up/click");
    assert(std::string(FacePath(Layout::Touch,1,false))=="x/click");
    assert(std::string(FacePath(Layout::Index,1,true))=="b/click");
    assert(Labels{}.Slot(5)=="Left Primary"&&Labels{}.Slot(6)=="Left Secondary");
    assert(Labels{}.Slot(-1).empty()&&Labels{}.Slot(skills::Count).empty());
    assert(Resolve(Choice::Generic,detected)==Labels{});
    const Labels wmr{{Layout::Motion,Layout::Motion}};
    assert(wmr.Slot(9)=="Left Unbound");
    // A pending atlas must be rejected after either detection or override changes.
    interaction::Snapshot s;interaction::Output o;
    assert(interaction::Context(s,o,true,quest)!=interaction::Context(s,o,true,frame));
    auto changed=detected;changed.hands[0]=Layout::Generic;
    assert(interaction::Context(s,o,true,changed)!=interaction::Context(s,o,true,detected));
    {   // Left-handed play (hand_role.h): the main hand's slots are cast from the LEFT controller, so they
        // are named by its buttons, and the off hand's by the right controller's.
        auto left=quest;left.leftMain=true;
        assert(left.Slot(0)=="Left Trigger"&&left.Slot(8)=="Left X"&&left.Slot(1)=="Left Y"&&left.Slot(2)=="Left Grip + X"&&left.Slot(3)=="Left Grip + Y");
        assert(left.Slot(4)=="Right Trigger"&&left.Slot(5)=="Right A"&&left.Slot(6)=="Right B"&&left.Slot(7)=="Right Grip + A"&&left.Slot(9)=="Right Grip + B");
        assert(left.Button(0,1)=="X"&&left.Button(1,1)=="A"&&quest.Button(0,1)=="A"&&quest.Button(1,1)=="X");
        auto mixed=automatic;mixed.leftMain=true;   // right Touch, left Index: each hand keeps its own controller's names
        assert(mixed.Slot(8)=="Left A"&&mixed.Slot(5)=="Right A"&&mixed.Slot(6)=="Right B");
        assert(!(left==quest)&&interaction::Context(s,o,true,left)!=interaction::Context(s,o,true,quest));   // the wheel repaints
        assert(!Labels{}.leftMain&&Labels{}.Physical(0)==0&&Labels{}.Physical(1)==1);   // off: as before
    }
    puts("Controller detection, per-hand labels, overrides, bindings, atlas invalidation and left-handed names passed");
}
