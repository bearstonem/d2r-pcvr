#pragma once
#include "aura_recipient.h"
#include "aura_assets.h"
#include "psychic_ward_assets.h"
#include "assassin_charge_assets.h"
#include "portal_billboard.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace aurafilter {
using aurarecipient::Unit;using aurarecipient::Friendly;using aurarecipient::Kind;
struct Entry {uint64_t emitter=0,definition=0,component=0,effects=0;uint32_t entity=0;Unit unit{};Kind kind=Kind::Unknown;bool shout=false,ward=false,charge=false;};
struct Snapshot {uint64_t session=0,at=0,registry=0,transforms=0,units=0;std::vector<Entry> entries;};
struct State {std::mutex mutex;std::atomic<unsigned> hidden{0};std::atomic<int> chargeMode{1};Snapshot snapshot;};
inline State& Shared(){static State state;return state;}
inline bool Wanted(){return Shared().hidden.load()!=0;}
inline void Configure(bool playerVisible,bool summonVisible,bool playerShouts=true,bool summonShouts=true,bool hideWard=false,int chargeMode=1){
    auto& s=Shared();s.hidden=(playerVisible?0u:1u)|(summonVisible?0u:2u)|(playerShouts?0u:4u)|(summonShouts?0u:8u)|(hideWard?16u:0u)|(chargeMode==1?0u:32u);s.chargeMode=chargeMode;
    std::lock_guard lock(s.mutex);s.snapshot={};
}
template<class Read>uint64_t Client(Read read,uint64_t base,const Friendly& actor){
    if(actor.type>1)return 0;
    uint64_t unit=0;
    if(!read(&unit,base+0x2a23910+(actor.type*128ull+(actor.id&127))*8,8))return 0;
    for(unsigned i=0;unit&&i<64;++i){
        uint32_t header[4]{};uint64_t next=0;
        if(!read(header,unit,sizeof header)||!read(&next,unit+0x158,8))return 0;
        if(header[0]==actor.type&&header[1]==actor.cls&&header[2]==actor.id)return unit;
        if(next==unit)return 0;unit=next;
    }
    return 0;
}
struct Storage {uint64_t address=0,packed=0,payload=0;unsigned count=0,stride=0;};
template<class Read>bool Load(Read read,uint64_t address,Storage& out){
    uint64_t h[9]{};
    if(!address||!read(h,address,sizeof h)||!h[4]||h[5]<h[4]||(h[5]-h[4])%4||
       !h[7]||h[8]<h[7])return false;
    const auto count=(h[5]-h[4])/4;
    if(!count||count>200000||(h[8]-h[7])%count)return false;
    const auto stride=(h[8]-h[7])/count;
    if(!stride||stride>0x1000)return false;
    out={address,h[4],h[7],unsigned(count),unsigned(stride)};return true;
}
// Called on the existing 200 ms game-thread roster sample. No new hooks,
// nearest-unit matching, pool indices, or global asset-type suppression.
template<class Read>Snapshot Build(Read read,uint64_t base,uint64_t session,uint64_t now,
        uint32_t heroEntity,uint64_t heroTransform,const Friendly* roster,unsigned count){
    Snapshot result{session,now,base+0x2677170};
    if(!session||!roster||!count||count>65||roster[0].type!=0)return {};
    const auto hero=Client(read,base,roster[0]);if(!hero)return {};
    uint64_t pools[2]{};
    if(!read(pools,result.registry,sizeof pools)||!pools[0]||pools[1]<pools[0]||
       (pools[1]-pools[0])%40||(pools[1]-pools[0])/40>4096)return {};
    std::vector<Storage> effects;
    for(uint64_t slot=pools[0];slot<pools[1];slot+=40){
        uint64_t pointer=0;Storage s;
        if(!read(&pointer,slot+32,8)||!Load(read,pointer,s))continue;
        if(s.stride==208&&aurarecipient::Component(read,pointer,heroEntity,208)==heroTransform){
            if(result.transforms&&result.transforms!=pointer)return {};result.transforms=pointer;
        }
        if(s.stride==16){
            const auto component=aurarecipient::Component(read,pointer,heroEntity,16);uint64_t unit=0;
            if(component&&read(&unit,component+8,8)&&unit==hero){
                if(result.units&&result.units!=pointer)return {};result.units=pointer;
            }
        }
        if(s.stride==48&&s.count<=4096)effects.push_back(s);
    }
    if(!result.transforms||!result.units)return {};
    for(const auto& s:effects)for(unsigned i=0;i<s.count;++i){
        uint64_t component=0,emitter=0,header[10]{};uint32_t entity=0;char path[160]{};
        if(!read(&component,s.payload+i*48ull,8)||!component||!read(&emitter,component+16,8)||!emitter||
           !read(header,emitter,sizeof header)||header[0]!=base+0x1da1118||header[6]!=component||
           !billboards::portal::DefinitionPath(read,header[8],base,path)||(!aurarecipient::AuraAsset(path)&&!aurarecipient::ShoutAsset(path)&&!psychicward::Asset(path)&&!assassincharges::Asset(path))||
           !read(&entity,s.packed+i*4ull,4)||aurarecipient::Component(read,s.address,entity,48)!=s.payload+i*48ull)continue;
        Unit unit{};
        if(!aurarecipient::Resolve(read,result.registry,result.transforms,result.units,entity,unit))continue;
        const auto kind=aurarecipient::Classify(unit,roster,count);
        if(kind==Kind::Unknown)continue;
        const auto actor=std::find_if(roster,roster+count,[&](const Friendly& a){return a.type==unit.type&&a.cls==unit.cls&&a.id==unit.id;});
        if(actor==roster+count||Client(read,base,*actor)!=unit.address)continue;
        if(result.entries.size()>=1024)return {}; // incomplete state never suppresses anything
        result.entries.push_back({emitter,header[8],component,s.address,entity,unit,kind,aurarecipient::ShoutAsset(path),psychicward::Asset(path),assassincharges::Asset(path)});
    }
    return result;
}
inline void Publish(Snapshot next){auto& s=Shared();std::lock_guard lock(s.mutex);s.snapshot=std::move(next);}
template<class Read>bool Hidden(Read read,uint64_t base,uint64_t session,uint64_t now,uint64_t emitter,uint64_t definition,bool chargesReady=false){
    auto& s=Shared();if(!Wanted()||!session)return false;
    Entry entry;uint64_t registry=0,transforms=0,units=0;
    {
        std::lock_guard lock(s.mutex);const auto& snap=s.snapshot;
        if(snap.session!=session||now<snap.at||now-snap.at>600)return false;
        auto it=std::find_if(snap.entries.begin(),snap.entries.end(),[&](const Entry& e){return e.emitter==emitter&&e.definition==definition;});
        if(it==snap.entries.end())return false;entry=*it;
        registry=snap.registry;transforms=snap.transforms;units=snap.units;
    }
    const auto recipientBit=entry.kind==Kind::Player?1u:entry.kind==Kind::Summon?2u:0u;
    if(entry.charge&&s.chargeMode.load()==2&&!chargesReady)return false;
    const auto bit=entry.charge?(entry.kind==Kind::Player?32u:0u):entry.ward?(entry.kind==Kind::Player?16u:0u):recipientBit<<(entry.shout?2:0);
    if(!(s.hidden.load()&bit))return false;
    uint64_t back=0;Unit current{};
    const auto effect=aurarecipient::Component(read,entry.effects,entry.entity,48);
    if(!effect||!read(&back,effect,8)||back!=entry.component||
       !read(&back,emitter+48,8)||back!=entry.component||!read(&back,entry.component+16,8)||back!=emitter||
       !aurarecipient::Resolve(read,registry,transforms,units,entry.entity,current)||
       current.address!=entry.unit.address||current.type!=entry.unit.type||current.cls!=entry.unit.cls||current.id!=entry.unit.id)return false;
    return Client(read,base,{current.type,current.cls,current.id})==current.address;
}
}
