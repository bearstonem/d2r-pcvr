#pragma once
#include <cstdint>
namespace framevr::clienthit {
// Only physical native melee input arms feedback. A confirmed server event
// consumes the hand once; old sessions or slow replies cannot replay it.
struct Gate {
 uint64_t session=0,at=0;int hand=-1;
 void Clear(){session=at=0;hand=-1;}
 void Arm(uint64_t s,int h,uint64_t now){if(!s||h<0||h>1){Clear();return;}session=s;hand=h;at=now;}
 int Confirm(uint64_t s,uint64_t now){
  if(now<at)return -1;
  if(!s||s!=session||now-at>750){Clear();return -1;}
  const int h=hand;Clear();return h;
 }
};
struct LifeLoss {
 uintptr_t target=0;bool lost=false;
 void Write(uintptr_t unit,int stat,int layer,bool nativeDamageWrite,int before,int after){
  if(target&&unit==target&&stat==6&&layer==0&&nativeDamageWrite&&before>0&&after<before)lost=true;
 }
};
// Require an observed native life write, not a recomputed damage-record total.
// Preserve the native return and never request another hit.
template<class Read,class Call,class Applied,class Publish>
uint64_t Observe(uintptr_t attacker,uintptr_t defender,int flag,uintptr_t damage,Read read,Call call,Applied applied,Publish publish){
 uint32_t a[4]{},d[4]{};
 const bool eligible=!flag&&damage&&read(attacker,a,sizeof a)&&read(defender,d,sizeof d)&&a[0]==0&&d[0]==1;
 const uint64_t result=call();
 uint16_t flags=0;
 if(eligible&&applied()&&read(damage+4,&flags,sizeof flags)&&
    (flags&1)&&!(flags&0x10))publish(a[2],d[2]);
 return result;
}
}
