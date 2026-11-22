"""Execute the production per-player scope with native-independent state."""
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parent.parent
text = (root/'vr/melee.cpp').read_text()
def section(start, end):
    i = text.index(start)
    return text[i:text.index(end, i)]
source = r'''
#include <atomic>
#include <array>
#include <cassert>
#include <cstdio>
#include "steamvr/melee_hand_state.h"
#include "steamvr/melee_dispatch_wire.h"
#include "steamvr/strike_skills.h"
#include "steamvr/hit_reactions.h"
#include "steamvr/bow_bash.h"
using namespace framevr::melee;
namespace combat {uintptr_t player=0;uintptr_t RemotePlayer(uintptr_t p){auto old=player;player=p;return old;}}
''' + section('struct RemoteInput {', 'uint64_t CombatSession()') + r'''
PlayerCombat localCombat;
framevr::strikeskills::Sequence sequence;
framevr::strikeskills::DualCast dualCast;
framevr::hitreact::Board reactBoard;
framevr::bowbash::Board bashBoard;
HitSet shieldHits,bashJudged;
struct CleanRate {uint64_t session=0;int attackRate=0;};
CleanRate cleanRates[2];uint64_t doubleSwingStatUntil=0;
struct DualMemo {uint64_t session=0,stroke=0;int hand=-1,skill=-1;uint32_t cycle=0,cast=0;};
DualMemo dualMemo;
std::atomic<uint64_t> sequenceDueAt{0};
''' + section('struct RemoteState {', 'bool ApplyRemoteContact(') + r'''
int main(){
 RemoteState a,b;a.input.player=101;b.input.player=202;
 localCombat.hands[0].hits.Insert(7,1,900);
 dualMemo.hand=0;cleanRates[0]={7,100};doubleSwingStatUntil=4000;sequenceDueAt=123;
 {
  RemoteScope scope(a);assert(remoteInput==&a.input&&combat::player==101);
  assert(!localCombat.hands[0].hits.Contains(7,1,900));
  localCombat.hands[0].hits.Insert(8,2,901);dualMemo.hand=1;cleanRates[0]={8,130};doubleSwingStatUntil=6000;
  SetSequenceDue(555);assert(a.input.due==555&&sequenceDueAt==123);
  {
   RemoteScope nested(b);assert(combat::player==202&&!localCombat.hands[0].hits.Contains(8,2,901));
   localCombat.hands[1].hits.Insert(9,3,902);SetSequenceDue(777);
  }
  assert(combat::player==101&&remoteInput==&a.input&&localCombat.hands[0].hits.Contains(8,2,901));
 }
 assert(combat::player==0&&!remoteInput&&localCombat.hands[0].hits.Contains(7,1,900));
 assert(dualMemo.hand==0&&cleanRates[0].attackRate==100&&doubleSwingStatUntil==4000&&sequenceDueAt==123);
 {RemoteScope scope(a);assert(localCombat.hands[0].hits.Contains(8,2,901)&&dualMemo.hand==1&&cleanRates[0].attackRate==130&&doubleSwingStatUntil==6000);}
 {RemoteScope scope(b);assert(localCombat.hands[1].hits.Contains(9,3,902)&&remoteInput->due==777);}
 puts("production remote scope: host/peer identity, hits, cadence state, dual cache, due times and nested restoration passed");
}
'''
out = Path(sys.argv[2]);out.mkdir(parents=True, exist_ok=True)
cpp = out/'remote_scope.cpp';exe = out/'remote_scope.exe'
cpp.write_text(source)
subprocess.run([sys.argv[1], '-std=c++20', '-O2', '-static', '-UNDEBUG', '-I'+str(root), str(cpp), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
