#pragma once
#include <cstdint>
namespace D2RL {struct PluginContext;}
namespace roomsubscription {
struct Status {bool ready=false;int requested=0,state=0;unsigned held=0,subscribed=0,depth=0;bool truncated=false;uint64_t transitions=0,updatePasses=0,removalPasses=0;unsigned updateRooms=0;};
void Poll(const D2RL::PluginContext*,const wchar_t* ini,bool vr);
Status ReadStatus();
}
