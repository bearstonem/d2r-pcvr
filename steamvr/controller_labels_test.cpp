#include "controller_labels.h"
#include "controller_profiles.h"
#include "interaction_paint.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iterator>
#include <set>
#include <string>
#include <utility>
using namespace framevr;
// controller_profiles.h: every path suggested for a profile must be one that profile defines. A runtime
// refuses a profile's whole suggestion over one path it does not have, and the controller's vibration
// goes with it. The lists are the OpenXR 1.0 interaction profiles' (paths with their components); the
// Steam Frame's is the set its runtime accepts.
static void SuggestedBindings() {
    using namespace controllers;
    using Set=std::set<std::string>;
    auto both=[](const Set& inputs){
        Set out{"/user/hand/right/output/haptic","/user/hand/left/output/haptic"};
        for(const auto& s:inputs){out.insert("/user/hand/right/input/"+s);out.insert("/user/hand/left/input/"+s);}
        return out;
    };
    auto only=[](Set out,const char* hand,const Set& inputs){for(const auto& s:inputs)out.insert(std::string("/user/hand/")+hand+"/input/"+s);return out;};
    const Set index=both({"system/click","system/touch","a/click","a/touch","b/click","b/touch","squeeze/value","squeeze/force",
        "trigger/click","trigger/value","trigger/touch","thumbstick","thumbstick/x","thumbstick/y","thumbstick/click","thumbstick/touch",
        "trackpad","trackpad/x","trackpad/y","trackpad/force","trackpad/touch","grip/pose","aim/pose"});
    const Set touch=only(only(both({"squeeze/value","trigger/value","trigger/touch","thumbstick","thumbstick/x","thumbstick/y",
        "thumbstick/click","thumbstick/touch","thumbrest/touch","grip/pose","aim/pose"}),
        "left",{"x/click","x/touch","y/click","y/touch","menu/click"}),"right",{"a/click","a/touch","b/click","b/touch","system/click"});
    const Set wmr=both({"menu/click","squeeze/click","trigger/value","thumbstick","thumbstick/x","thumbstick/y","thumbstick/click",
        "trackpad","trackpad/x","trackpad/y","trackpad/click","trackpad/touch","grip/pose","aim/pose"});
    const Set frame=only(only(both({"grip/pose","thumbstick","thumbstick/click","trigger/value","aim/pose","squeeze/value"}),
        "right",{"a/click","b/click","menu/click"}),"left",{"dpad_down/click","dpad_up/click","view/click"});
    const std::pair<const char*,const Set*> known[]={{"/interaction_profiles/valve/index_controller",&index},
        {"/interaction_profiles/oculus/touch_controller",&touch},{"/interaction_profiles/microsoft/motion_controller",&wmr},{FrameProfile,&frame}};
    assert(std::size(Profiles)==std::size(known));
    for(size_t p=0;p<std::size(known);++p) {
        assert(std::string_view(Profiles[p])==known[p].first);
        const auto list=Suggestions(Profiles[p]);
        Set seen;int outputs[2]{};
        for(const auto& s:list) {
            assert(known[p].second->count(s.path));   // a path this profile defines
            assert(seen.insert(s.path).second);       // suggested once
            assert(s.hand==0||s.hand==1);
            assert(s.path.starts_with(s.hand?"/user/hand/left/":"/user/hand/right/"));
            const bool output=s.path.find("/output/")!=std::string::npos;
            assert(output==(s.action==Bind::Haptic));
            if(output)++outputs[s.hand];
            // Boolean actions take clicks; the float trigger and grip take values (the Index's grip its force).
            if(s.action==Bind::StickClick||s.action==Bind::Primary||s.action==Bind::Secondary||s.action==Bind::Menu)assert(s.path.ends_with("/click"));
            if(s.action==Bind::Trigger)assert(s.path.ends_with("trigger/value"));
            if(s.action==Bind::Squeeze)assert(s.path.ends_with("squeeze/value")||s.path.ends_with("squeeze/force"));
        }
        assert(outputs[0]==1&&outputs[1]==1);   // both hands' vibration, in every profile
        for(int hand=0;hand<2;++hand)for(const auto action:{Bind::Haptic,Bind::Grip,Bind::Stick,Bind::StickClick,Bind::Trigger,Bind::Aim})
            assert(std::count_if(list.begin(),list.end(),[&](const Suggestion& s){return s.hand==hand&&s.action==action;})==1);
    }
    // The lists as the transport built them before they moved to controller_profiles.h.
    auto paths=[](std::string_view profile){Set out;for(const auto& s:Suggestions(profile))out.insert(s.path);return out;};
    const auto r=[](const char* s){return std::string("/user/hand/right/input/")+s;};
    const auto l=[](const char* s){return std::string("/user/hand/left/input/")+s;};
    Set common{"/user/hand/right/output/haptic","/user/hand/left/output/haptic"};
    for(const char* s:{"grip/pose","thumbstick","thumbstick/click","trigger/value","aim/pose"}){common.insert(r(s));common.insert(l(s));}
    auto with=[](Set set,std::initializer_list<std::string> more){set.insert(more.begin(),more.end());return set;};
    assert(paths("/interaction_profiles/valve/index_controller")==with(common,{r("squeeze/force"),l("squeeze/force"),r("a/click"),r("b/click"),l("a/click"),l("b/click")}));
    assert(paths("/interaction_profiles/oculus/touch_controller")==with(common,{r("squeeze/value"),l("squeeze/value"),r("a/click"),r("b/click"),l("x/click"),l("y/click"),l("menu/click")}));
    assert(paths("/interaction_profiles/microsoft/motion_controller")==with(common,{r("menu/click"),l("menu/click")}));
    assert(paths(FrameProfile)==with(common,{r("squeeze/value"),l("squeeze/value"),r("a/click"),r("b/click"),l("dpad_down/click"),l("dpad_up/click"),r("menu/click"),l("view/click")}));
    assert(Suggestions("/interaction_profiles/valve/index_controller").size()==18&&Suggestions(FrameProfile).size()==20);
}
int main(){
    SuggestedBindings();
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
    puts("Controller detection, per-hand labels, overrides, bindings, every profile's suggested paths and both vibration outputs, atlas invalidation and left-handed names passed");
}
