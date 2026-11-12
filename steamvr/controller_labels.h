#pragma once
#include <array>
#include <string>
#include <string_view>
#include "skill_slots.h"

namespace framevr::controllers {
// Persisted choices: append new values rather than reordering existing ones.
enum class Choice { Automatic, Quest, Index, Frame, Generic, Count };
enum class Layout { Generic, Touch, Index, Frame, Motion };
inline constexpr const char* Choices[]={"Automatic", "Quest / Touch", "Valve Index", "Steam Frame", "Generic"};
inline constexpr const char* FrameExtension="XR_VALVE_frame_controller_interaction";
inline constexpr const char* FrameProfile="/interaction_profiles/valve/frame_controller_valve";
inline Layout Detect(std::string_view profile) {
    if(profile=="/interaction_profiles/oculus/touch_controller"||
       profile=="/interaction_profiles/facebook/touch_controller_pro"||
       profile=="/interaction_profiles/meta/touch_controller_plus")return Layout::Touch;
    if(profile=="/interaction_profiles/valve/index_controller")return Layout::Index;
    if(profile==FrameProfile)return Layout::Frame;
    if(profile=="/interaction_profiles/microsoft/motion_controller")return Layout::Motion;
    return Layout::Generic;
}
inline const char* Name(Layout layout) {
    switch(layout){case Layout::Touch:return "Touch";case Layout::Index:return "Index";
        case Layout::Frame:return "Frame";case Layout::Motion:return "WMR";default:return "Unknown";}
}
inline Layout Resolve(Choice choice,Layout detected) {
    switch(choice){case Choice::Quest:return Layout::Touch;case Choice::Index:return Layout::Index;
        case Choice::Frame:return Layout::Frame;case Choice::Generic:return Layout::Generic;default:return detected;}
}
inline const char* Face(Layout layout,int hand,bool secondary) {
    if(layout==Layout::Index)return secondary?"B":"A";
    if(layout==Layout::Touch)return hand?(secondary?"Y":"X"):(secondary?"B":"A");
    if(layout==Layout::Frame)return hand?(secondary?"D-pad Up":"D-pad Down"):(secondary?"B":"A");
    if(layout==Layout::Motion)return "Unbound"; // No face actions in our WMR binding.
    return secondary?"Secondary":"Primary";
}
// These paths also generate the actual suggested bindings, so labels and
// input cannot accidentally disagree about which hand has X/Y or a D-pad.
inline const char* FacePath(Layout layout,int hand,bool secondary) {
    if(layout==Layout::Frame&&hand)return secondary?"dpad_up/click":"dpad_down/click";
    if(layout==Layout::Touch&&hand)return secondary?"y/click":"x/click";
    return secondary?"b/click":"a/click";
}
struct Labels {
    std::array<Layout,2> hands{}; // Right, left (same order as XR actions): physical.
    // Left-handed play (hand_role.h): the main hand (slots 0-3 and 8; hand 0 below) is the left controller,
    // the off hand the right one, so a slot is named by the controller and the button that cast it.
    bool leftMain=false;
    bool operator==(const Labels&) const=default;
    int Physical(int hand)const {return leftMain?1-hand:hand;}
    // hand: 0 the main hand, 1 the off hand (the right and the left unless leftMain).
    std::string Button(int hand,int button)const {
        if(hand<0||hand>1||button<0||button>4)return "";
        if(button==0)return "Trigger";
        const int side=Physical(hand);
        const auto face=Face(hands[side],side,button==2||button==4);
        if(hands[side]==Layout::Motion)return face;
        return std::string(button>=3?"Grip + ":"")+face;
    }
    std::string Slot(int slot)const {
        if(slot<0||slot>=skills::Count)return "";
        const int hand=skills::Hand(slot);
        for(int button=0;button<5;++button)if(skills::HandSlot(hand,button)==slot)
            return std::string(Physical(hand)?"Left ":"Right ")+Button(hand,button);
        return "";
    }
};
inline Labels Resolve(Choice choice,Labels detected) {
    for(auto& hand:detected.hands)hand=Resolve(choice,hand);
    return detected;
}
}
