#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "controller_labels.h"

namespace framevr::controllers {
// The interaction profiles bindings are suggested for, in this order. The Steam Frame's needs its
// extension (FrameExtension); the transport skips it when the runtime does not offer that.
inline constexpr const char* Profiles[]={"/interaction_profiles/valve/index_controller","/interaction_profiles/oculus/touch_controller",
    "/interaction_profiles/microsoft/motion_controller",FrameProfile};
// Which action a suggested path goes to. Haptic is that hand's own vibration output (haptic_actions.h);
// the others are one action each, made with both hands as subaction paths.
enum class Bind { Haptic, Grip, Stick, StickClick, Trigger, Aim, Squeeze, Primary, Secondary, Menu };
struct Suggestion { Bind action; int hand; std::string path; };   // hand: 0 right, 1 left (physical)
// Every binding suggested for one profile. xrSuggestInteractionProfileBindings refuses the whole
// list when one path is not the profile's, and both hands' vibration goes with it, so each path
// here has to be one the profile defines (controller_labels_test checks them against its list).
inline std::vector<Suggestion> Suggestions(std::string_view profile) {
    const auto layout=Detect(profile);
    const bool index=layout==Layout::Index,touch=layout==Layout::Touch,frame=layout==Layout::Frame;
    std::vector<Suggestion> out;
    for(int hand=0;hand<2;++hand)out.push_back({Bind::Haptic,hand,hand==0?"/user/hand/right/output/haptic":"/user/hand/left/output/haptic"});
    for(int hand=0;hand<2;++hand) {
        const std::string base=hand==0?"/user/hand/right/input/":"/user/hand/left/input/";
        auto add=[&](Bind action,const char* suffix){out.push_back({action,hand,base+suffix});};
        add(Bind::Grip,"grip/pose");add(Bind::Stick,"thumbstick");add(Bind::StickClick,"thumbstick/click");add(Bind::Trigger,"trigger/value");
        add(Bind::Aim,"aim/pose");
        // Motion controllers expose squeeze/click (boolean), so omit our float grip there.
        if(index||touch||frame)add(Bind::Squeeze,index?"squeeze/force":"squeeze/value");
        if(index||touch||frame) {
            add(Bind::Primary,FacePath(layout,hand,false));
            add(Bind::Secondary,FacePath(layout,hand,true));
        }
        if(frame)add(Bind::Menu,hand==1?"view/click":"menu/click");
        else if((touch&&hand==1)||(!index&&!touch))add(Bind::Menu,"menu/click");
    }
    return out;
}
}
