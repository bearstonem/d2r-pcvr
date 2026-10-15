#pragma once
#include <array>
#include <cstdio>
#include <vector>
#include <openxr/openxr.h>

namespace framevr::haptics {
// Keep output routing independent of shared input/subaction ordering. Each
// action has exactly one suggested output, including on SteamVR. Both apply
// and timed stop use Info(), so one hand's timeout cannot stop the other.
class Actions {
    std::array<XrAction,2> actions{}; // right, left (transport wire order)
public:
    XrResult Create(XrActionSet set,PFN_xrCreateAction create) {
        for(int hand=0;hand<2;++hand) {
            XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
            std::snprintf(ci.actionName,sizeof ci.actionName,"feedback_%s",hand==0?"right":"left");
            std::snprintf(ci.localizedActionName,sizeof ci.localizedActionName,"%s hand vibration",hand==0?"Right":"Left");
            ci.actionType=XR_ACTION_TYPE_VIBRATION_OUTPUT;
            // No subaction filter: the action itself selects the only output.
            const auto result=create(set,&ci,&actions[hand]);
            if(XR_FAILED(result))return result;
        }
        return XR_SUCCESS;
    }
    XrResult Bind(XrInstance instance,PFN_xrStringToPath path,std::vector<XrActionSuggestedBinding>& bindings) const {
        for(int hand=0;hand<2;++hand) {
            XrPath output=XR_NULL_PATH;
            const auto result=path(instance,hand==0?"/user/hand/right/output/haptic":"/user/hand/left/output/haptic",&output);
            if(XR_FAILED(result))return result;
            bindings.push_back({actions[hand],output});
        }
        return XR_SUCCESS;
    }
    XrHapticActionInfo Info(int hand) const {
        XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
        if(hand>=0&&hand<2)info.action=actions[hand];
        return info;
    }
    void Reset(){actions={};} // owning action set destroys the handles
};
}
