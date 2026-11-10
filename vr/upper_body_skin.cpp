// Remote-only skinning substitution adapted from diablo2-vr-port-main
// vr/head_shadow.cpp (MPL-2.0); no local head hiding or gait dependencies.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <D2RLPlugin/context.h>
#include "upper_body_skin.h"
#include "sigscan.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace upperbodyskin { namespace {
constexpr uint64_t kCompositeRva=0x1614DA0;
const uint8_t kSig[16]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
constexpr int kMaxBones=1024,kSlots=16;
constexpr ULONGLONG kTtlMs=250;
using CompositeFn=uintptr_t(*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
CompositeFn original=nullptr;
std::atomic<bool> enabled{false};
SRWLOCK lock=SRWLOCK_INIT;
struct Slot{uintptr_t cur=0,prev=0,skeleton=0,owner=0,matrices[2]{};int count=0;ULONGLONG at=0;float m[2][kMaxBones][16]{};};
Slot slots[kSlots];
// Granny packs pointers to four-byte alignment. Copy the complete observed
// 32-byte header, replacing ONLY the unaligned matrix pointer at byte four.
alignas(16) thread_local float scratch[kMaxBones][16];
alignas(16) thread_local uint8_t header[32];
bool Read(uintptr_t a,void*out,size_t n) noexcept {
 if(a<0x10000||!out)return false;SIZE_T got=0;
 return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(a),out,n,&got)&&got==n;
}
bool Valid(const float*p,int n){
 if(!p||n<=0||n>kMaxBones)return false;
 for(size_t i=0;i<size_t(n)*16;++i)if(!std::isfinite(p[i]))return false;
 return true;
}
uintptr_t Hook(uintptr_t skel,uintptr_t pose,uintptr_t ids,uintptr_t count,uintptr_t out){
 if(!original)return 0;
 int n=0;uintptr_t expectedMatrices=0,owner=0;
 if(enabled.load(std::memory_order_relaxed)){
  const auto now=GetTickCount64();
  AcquireSRWLockShared(&lock);
  for(const auto&s:slots){
   if(!s.cur||s.skeleton!=skel||now-s.at>kTtlMs)continue;
   const int which=pose==s.cur?0:pose==s.prev&&s.prev?1:-1;
   if(which>=0){n=s.count;expectedMatrices=s.matrices[which];owner=s.owner;std::memcpy(scratch,s.m[which],size_t(n)*64);break;}
  }
  ReleaseSRWLockShared(&lock);
 }
 // Do not make a guarded process-memory read for every unrelated mesh draw.
 if(n&&Read(pose,header,sizeof header)){
  int32_t actual=0;std::memcpy(&actual,header,4);
  uintptr_t actualMatrices=0;std::memcpy(&actualMatrices,header+4,8);
  bool bound=true;
  if(owner){
   uintptr_t cur=0,prev=0,model=0,skeleton=0;
   bound=Read(owner+0x20,&cur,8)&&Read(owner+0x28,&prev,8)&&(pose==cur||pose==prev)&&
         Read(owner+8,&model,8)&&Read(model+8,&skeleton,8)&&skeleton==skel;
  }
  if(actual==n&&actualMatrices==expectedMatrices&&bound){const uintptr_t ptr=reinterpret_cast<uintptr_t>(scratch);std::memcpy(header+4,&ptr,8);return original(skel,reinterpret_cast<uintptr_t>(header),ids,count,out);}
 }
 return original?original(skel,pose,ids,count,out):0;
}
}
void Init(const D2RL::PluginContext*ctx){
 if(original){enabled=true;return;}
 const bool ok=d2rsig::Addr(kCompositeRva)&&d2rsig::Check(kCompositeRva,kSig,sizeof kSig)&&
  d2rsig::Hook(kCompositeRva,kSig,sizeof kSig,reinterpret_cast<void*>(&Hook),reinterpret_cast<void**>(&original))&&original;
 enabled=ok;
 if(ctx)ctx->LogInfo(ok?"PCVR upper body: remote skinning hook installed":"PCVR upper body: skinning signature unavailable; remote visuals disabled");
}
void Publish(uintptr_t cur,uintptr_t prev,const float*m,int n,uintptr_t skeleton,uintptr_t owner){
 if(!enabled||cur<0x10000||!Valid(m,n))return;
 uintptr_t matrices[2]{};
 if(!Read(cur+4,&matrices[0],8)||matrices[0]<0x10000)return;
 if(prev&&(!Read(prev+4,&matrices[1],8)||matrices[1]<0x10000))prev=0;
 const auto now=GetTickCount64();AcquireSRWLockExclusive(&lock);
 if(!enabled){ReleaseSRWLockExclusive(&lock);return;}
 int ix=-1;
 for(int i=0;i<kSlots;++i)if(slots[i].cur==cur||(prev&&slots[i].cur==prev)||slots[i].prev==cur){ix=i;break;}
 if(ix<0)for(int i=0;i<kSlots;++i)if(!slots[i].cur||now-slots[i].at>kTtlMs){ix=i;break;}
 if(ix<0){ix=0;for(int i=1;i<kSlots;++i)if(slots[i].at<slots[ix].at)ix=i;}
 Slot&s=slots[ix];
 const bool fresh=s.cur&&s.owner==owner&&s.skeleton==skeleton&&s.count==n&&now-s.at<=kTtlMs;
 // Buffers swap each frame: previous points at the formerly current pose.
 // Multiple publishes of the same current buffer must not advance history.
 if(fresh&&prev==s.cur&&cur!=s.cur)std::memcpy(s.m[1],s.m[0],size_t(n)*64);
 else if(!fresh||prev!=s.prev)std::memcpy(s.m[1],m,size_t(n)*64);
 std::memcpy(s.m[0],m,size_t(n)*64);
 // Retire any alias from another slot before publishing the new binding.
 for(int i=0;i<kSlots;++i)if(i!=ix){
  auto&other=slots[i];
  if(other.cur==cur||other.prev==cur||(prev&&(other.cur==prev||other.prev==prev)))other.cur=other.prev=0;
 }
 s.count=n;s.cur=cur;s.prev=prev>=0x10000?prev:0;s.at=now;
 s.skeleton=skeleton;s.owner=owner;s.matrices[0]=matrices[0];s.matrices[1]=matrices[1];
 ReleaseSRWLockExclusive(&lock);
}
void Clear(){AcquireSRWLockExclusive(&lock);for(auto&s:slots){s.cur=s.prev=0;s.count=0;s.at=0;}ReleaseSRWLockExclusive(&lock);}
void Shutdown(){
 enabled=false;Clear();
 // Inline detours are owned by D2RLoader, like the existing skeleton hooks.
 // The pinned SDK exposes installation, not individual removal. Keep the
 // trampoline intact and pass through during shutdown; no manual byte restore.
}
}
