// Exercise the actual native adapter and dispatch with synthetic units/functions.
#include "../vr/breakables.cpp"
#undef NDEBUG
#include <cassert>
#include <thread>
#include <vector>
using namespace breakables;
namespace {
std::array<uint8_t,0x4000> game{};
std::array<uint8_t,0x200> player{},object{},other{},playerPath{},objectPath{},otherPath{};
std::array<uint8_t,0x168> row{};
uintptr_t objectData=0;
bool signatures=false,hookOk=false,wall=false,live=true,allowedNow=true,accept=true,kill=false,removeOther=false,weaponOk=true;
unsigned checks=0,installs=0,operations=0,kicks=0,lineCalls=0;
int pulses[2]{};
uint64_t failGuard=0;
uintptr_t G(){return reinterpret_cast<uintptr_t>(game.data());}
uintptr_t P(){return reinterpret_cast<uintptr_t>(player.data());}
uintptr_t O(){return reinterpret_cast<uintptr_t>(object.data());}
uintptr_t Q(){return reinterpret_cast<uintptr_t>(other.data());}
template<class T>void Put(uintptr_t p,size_t at,T value){std::memcpy(reinterpret_cast<void*>(p+at),&value,sizeof value);}
template<class T>T Get(uintptr_t p,size_t at){T value{};assert(Read(p+at,value));return value;}
int FakeMode(uintptr_t g,uintptr_t p,uintptr_t skill,int mode,int type,uint32_t id,int a,int b) {
    assert(g==G()&&p==P()&&skill==123&&mode==8&&type==2&&id==41&&a==3&&b==4);++kicks;return 7;
}
int FakeLine(uintptr_t p,uintptr_t u,int mask){assert(p==P()&&(u==O()||u==Q())&&mask==0x804);++lineCalls;return wall;}
int FakeOperate(uintptr_t g,uintptr_t p,uintptr_t u) {
    assert(g==G()&&p==P()&&(u==O()||u==Q()));++operations;
    assert(physical.game==g&&physical.player==p&&physical.object==Get<uint32_t>(u,8));
    if(row[0x15e]==5)assert(StartModeAt(abi::BarrelKickReturnRva,g,p,123,8,2,Get<uint32_t>(u,8),3,4)==1);
    if(!accept)return 1;
    Put(u,0xc,uint32_t(1)); // native operation changes the object's mode
    if(kill)Put(P(),0xc,uint32_t(17));
    if(removeOther)Put(Q(),0,uint32_t(6));
    return 0;
}
bool Allowed(const Strike&){return allowedNow;}
Strike StrikeAt(int hand=0,uint64_t stroke=1) {
    Strike r;r.session=1;r.at=100;r.sweep.stroke=stroke;
    auto& s=r.sample;s.active=true;s.handIndex=hand;s.unitsPerMetre=4;s.player={50,0,50};
    if(hand)s.weapons={{50,51},{D2RVR_TYPE_SWORD,D2RVR_TYPE_AXE},{50,51}};
    r.sweep.before={{50,.4f,49.3f},{50.6f,.4f,49.3f},.04f};
    r.sweep.after={{50,.4f,50.7f},{50.6f,.4f,50.7f},.04f};return r;
}
void Reset(uint8_t operation=3) {
    game={};player={};object={};other={};playerPath={};objectPath={};otherPath={};row={};
    objectData=reinterpret_cast<uintptr_t>(row.data());
    Put(P(),0xc,uint32_t(1));Put(P(),0xd8,G());
    Put(P(),0x38,reinterpret_cast<uintptr_t>(playerPath.data()));
    Put(reinterpret_cast<uintptr_t>(playerPath.data()),0,uint32_t(100<<16));
    Put(reinterpret_cast<uintptr_t>(playerPath.data()),4,uint32_t(100<<16));
    Put(reinterpret_cast<uintptr_t>(playerPath.data()),0x20,uintptr_t(0x123456));
    for(auto u:{O(),Q()}) {
        const auto path=reinterpret_cast<uintptr_t>(u==O()?objectPath.data():otherPath.data());
        Put(u,0,uint32_t(2));Put(u,4,uint32_t(7));Put(u,8,uint32_t(u==O()?41:42));
        Put(u,0x10,reinterpret_cast<uintptr_t>(&objectData));Put(u,0x38,path);Put(u,0xd8,G());
        Put(u,0x124,uint32_t(2));
        Put(path,0,uintptr_t(0x123456));Put(path,0x10,101);Put(path,0x14,100);
    }
    Put(G(),0x546*8,O());
    row[0xe8]=row[0x85]=1;row[0x15e]=operation;
    Put(reinterpret_cast<uintptr_t>(row.data()),0x90,1);Put(reinterpret_cast<uintptr_t>(row.data()),0x94,1);
    wall=kill=removeOther=false;live=allowedNow=accept=weaponOk=true;operations=kicks=lineCalls=0;pulses[0]=pulses[1]=0;attempted={};
}
void Swing(const Strike& r){Apply(G(),P(),{&r,1},&Allowed);}
}
namespace d2rsig {
bool Check(uint64_t rva,const void*,uint32_t){++checks;return signatures&&rva!=failGuard;}
bool Hook(uint64_t rva,const void*,uint32_t,void* detour,void** original) {
    assert(rva==abi::ModeRva&&detour==reinterpret_cast<void*>(&StartMode));++installs;
    if(!hookOk)return false;*original=reinterpret_cast<void*>(&FakeMode);return true;
}
uintptr_t Addr(uint64_t rva) {
    if(rva==abi::OperateRva)return reinterpret_cast<uintptr_t>(&FakeOperate);
    if(rva==abi::LineRva)return reinterpret_cast<uintptr_t>(&FakeLine);
    if(rva==abi::BarrelKickReturnRva)return rva;
    assert(false);return 0;
}
}
namespace framevr::logging {void Write(const char*){}}
namespace framevr {void MeleeImpact(int hand){assert(hand==0||hand==1);++pulses[hand];}}
namespace melee {
bool LocalServer(uintptr_t& g,uintptr_t& p){g=G();p=P();return live;}
uintptr_t ServerUnit(uintptr_t g,int type,uint32_t id){assert(g==G()&&type==2);return id==41?O():id==42?Q():0;}
}
namespace meleeweapon {
uintptr_t Select(uintptr_t p,int hand,const framevr::dualwield::Equipment& e) {
    assert(p==P()&&(hand==0||hand==1));
    static uint32_t item[3]={4,0,0};item[2]=e.displayed[hand];return weaponOk?reinterpret_cast<uintptr_t>(item):0;
}
}
int main() {
    Reset();auto r=StrikeAt();Swing(r);assert(!operations);
    Install();assert(!ready&&installs==0);signatures=true;
    for(const auto& g:abi::Guards){failGuard=g.rva;Install();assert(!ready&&installs==0);}
    failGuard=0;Install();assert(!ready&&installs==1);hookOk=true;Install();assert(ready&&installs==2);
    const auto checked=checks;Install();assert(checks==checked&&installs==2);

    for(uint8_t op:{3,5,7}) {
        Reset(op);Swing(r);assert(operations==1&&pulses[0]==1&&pulses[1]==0&&kicks==0);
        Swing(r);assert(operations==1); // already broken / continuing blade contact
    }
    for(uint8_t op:{0,1,2,4,8,15,19,23,25,49,53,68,255}) {
        Reset(op);Swing(r);assert(!operations&&!lineCalls); // never operate unrelated objects
    }
    Reset();row[0xe8]=0;Swing(r);assert(!operations);
    Reset();row[0x85]=0;Swing(r);assert(!operations);
    Reset();Put(O(),0x124,uint32_t(0));Swing(r);assert(!operations);
    Reset();Put(O(),0,uint32_t(1));Swing(r);assert(!operations); // monster ID can overlap
    Reset();Put(O(),0xd8,G()+1);Swing(r);assert(!operations);
    Reset();Put(O(),0xc,uint32_t(2));Swing(r);assert(!operations);
    Reset();Put(reinterpret_cast<uintptr_t>(row.data()),0x90,0);Swing(r);assert(!operations);
    Reset();Put(reinterpret_cast<uintptr_t>(objectPath.data()),0,uintptr_t(0));Swing(r);assert(!operations);
    Reset();wall=true;Swing(r);assert(!operations&&lineCalls==1&&!pulses[0]);
    Reset();live=false;Swing(r);assert(!operations);
    Reset();allowedNow=false;Swing(r);assert(!operations); // menus, stale input, tracking/equipment changes
    Reset();r.sample.active=false;Swing(r);assert(!operations);r=StrikeAt();
    Reset();r.sample.shield=true;Swing(r);assert(!operations);r=StrikeAt();
    Reset();r.sample.handIndex=2;Swing(r);assert(!operations);r=StrikeAt();
    Reset();r.sample.handIndex=1;Swing(r);assert(!operations);r=StrikeAt(); // no offhand weapon
    Reset();r.sample.player.x+=3;Swing(r);assert(!operations);r=StrikeAt(); // warp guard
    Reset();r.sweep.before.tip.y=r.sweep.before.base.y=r.sweep.after.tip.y=r.sweep.after.base.y=3;
    Swing(r);assert(!operations);r=StrikeAt(); // swinging above a pot does not smash it
    Reset();accept=false;Swing(r);Swing(r);assert(operations==1&&!pulses[0]); // refusal once per stroke
    r.sweep.stroke++;Swing(r);assert(operations==2);r=StrikeAt();
    Reset();auto left=StrikeAt(1);Swing(left);assert(operations==1&&pulses[1]==1&&!pulses[0]);
    Reset();weaponOk=false;Swing(left);assert(!operations);
    Reset();Strike both[]={r,left};Apply(G(),P(),both,&Allowed);assert(operations==1); // no double loot
    Reset();Put(O(),0x158,Q());Swing(r);assert(operations==2&&pulses[0]==2); // one sweep may smash both pots
    Reset(7);Put(O(),0x158,Q());kill=true;Swing(r);assert(operations==1); // native trap kills hero
    Reset(7);Put(O(),0x158,Q());removeOther=true;Swing(r);assert(operations==1); // chain explosion/unload
    Reset();Put(O(),0x158,O());Swing(r);assert(operations==1); // corrupt cycle is bounded

    // Exact native barrel kick call is bypassed ONLY inside our physical scope.
    Reset();const auto kick=[&](uintptr_t caller=abi::BarrelKickReturnRva){return StartModeAt(caller,G(),P(),123,8,2,41,3,4);};
    assert(kick()==7&&kicks==1);
    {Scope a(G(),P(),41);assert(kick()==1&&kicks==1);assert(kick(0x9999)==7&&kicks==2);
     {Scope b(G(),P(),42);assert(kick()==7&&kicks==3);}
     assert(kick()==1&&kicks==3);
     std::thread thread([&]{assert(kick()==7);});thread.join();assert(kicks==4);}
    assert(kick()==7&&kicks==5&&!physical.player);
    assert(StartMode(G(),P(),123,8,2,41,3,4)==7&&kicks==6); // ordinary hooked calls forward all arguments
}
