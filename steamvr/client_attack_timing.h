#pragma once
#include "skill_slots.h"
#include <cstdint>
#include <mutex>
namespace framevr::clienttiming {
// Diagnostics only. Feedback packets are NOT correlated to individual swings;
// pulses>1 is explicitly ambiguous. Never use these timings to authorize hits.
struct Reading {bool valid=false,padKnown=false;unsigned pulses=0;uint64_t toPad=0,toFeedback=0;int hand=-1,slot=-1;};
class Probe {
 uint64_t session=0,began=0,padAt=0;bool active=false,pending=false;unsigned pulses=0;int hand=-1,slot=-1;
public:
 void Clear(){*this={};}
 bool Pulse(uint64_t s,bool on,int h,int binding,uint64_t now){
  if(!s){Clear();return false;}
  if(s!=session){Clear();session=s;}
  const bool edge=on&&(!active||h!=hand||binding!=slot);active=on;
  if(!edge)return false;
  if(h<0||h>1||binding<0||binding>=skills::Count){active=false;return false;}
  if(pending&&(now<began||now-began>1000))pulses=0;
  began=now;padAt=0;hand=h;slot=binding;pending=true;++pulses;return true;
 }
 bool Submit(uint16_t buttons,bool alternate,uint64_t now){
  if(!active||!pending||padAt||now<began||now-began>1000||alternate!=skills::Alternate(slot)||
     (buttons&skills::Buttons(slot))!=skills::Buttons(slot))return false;
  padAt=now;return true;
 }
 Reading Confirm(uint64_t s,uint64_t now){
  if(!pending||!s||s!=session||now<began||now-began>1000)return {};
  Reading r{true,padAt!=0,pulses,padAt?padAt-began:0,now-began,hand,slot};
  pending=false;pulses=0;return r;
 }
};
// Shared only inside this DLL. Both paths take this lock alone; neither calls
// game/SDK code while holding it. Production counters are bounded per process.
struct Shared {
 std::mutex mutex;Probe probe;unsigned starts=0,submits=0,feedback=0;
 bool Pulse(uint64_t s,bool on,int hand,int slot,uint64_t now){std::lock_guard lock(mutex);const bool edge=probe.Pulse(s,on,hand,slot,now);return edge&&starts++<80;}
 bool Submit(uint16_t buttons,bool alt,uint64_t now){std::lock_guard lock(mutex);const bool fresh=probe.Submit(buttons,alt,now);return fresh&&submits++<80;}
 Reading Confirm(uint64_t s,uint64_t now){std::lock_guard lock(mutex);const auto r=probe.Confirm(s,now);return feedback++<80?r:Reading{};}
};
inline Shared diagnostic;
}
