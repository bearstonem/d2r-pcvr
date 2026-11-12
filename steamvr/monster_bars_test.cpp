#include "../cleanroom/camera/monster_bars.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <utility>

using namespace d2rcam::monsterbars;

// Fake game memory: a unit, its monster data and its MonStats row. From `fail`
// to the end of its record nothing reads, as when the rest of a record lies on
// a page no longer mapped; a read fails as a whole when it touches that, or
// leaves the block.
struct Fixture {
    static constexpr uintptr_t base=0x100000,unit=base,data=base+0x200,row=base+0x400;
    std::array<unsigned char,0x500> bytes{};
    uintptr_t fail=0;
    unsigned reads=0;
    template<class T> void Put(uintptr_t address,T value){std::memcpy(bytes.data()+address-base,&value,sizeof value);}
    Fixture(){   // an ordinary monster: enabled, killable, no special flags
        Put(unit,uint32_t(1));Put(unit+4,uint32_t(19));Put(unit+0x10,data);
        Put(data,row);Put(row,uint16_t(19));Put(row+0x3c,uint32_t(RowRequired));
    }
    template<class V> bool Read(uintptr_t at,V& value){
        ++reads;
        const uintptr_t end=fail<data?data:fail<row?row:base+bytes.size();
        if((fail&&at<end&&fail<at+sizeof value)||at<base||at-base+sizeof value>bytes.size())return false;
        std::memcpy(&value,bytes.data()+at-base,sizeof value);return true;
    }
    auto Reader(){return [this](uintptr_t at,auto& value){return Read(at,value);};}
    Kind Classify(){return d2rcam::monsterbars::Classify(unit,Reader());}
    bool Hide(Names names=Names::BossesElites,bool stereo=true,bool above=true){
        return d2rcam::monsterbars::Hide(stereo,names,above,unit,Reader());
    }
};

// The filter as 364cfd9f wrote it (one read per field) and as 0ccea647 rewrote
// it (one read per record), kept here to show the rewrite answers the same.
template<class Read> bool HideBefore0ccea647(bool stereoUi,bool showOrdinary,bool above,uintptr_t unit,Read read) {
    if(!stereoUi||showOrdinary||!above||unit<0x10000)return false;
    uint32_t type=0,classId=0;uintptr_t data=0,row=0;
    uint8_t special=0;uint16_t rowId=0;uint32_t flags=0;
    if(!read(unit,type)||type!=1||!read(unit+4,classId)||classId>0xffff||
       !read(unit+0x10,data)||data<0x10000||!read(data+0x1a,special)||special||
       !read(data,row)||row<0x10000||!read(row,rowId)||rowId!=classId||
       !read(row+0x3c,flags))return false;
    constexpr uint32_t required=(1u<<25)|(1u<<15);
    constexpr uint32_t keep=(1u<<6)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<10);
    return (flags&required)==required&&(flags&keep)==0;
}
template<class Read> bool HideAt0ccea647(bool stereoUi,bool showOrdinary,bool above,uintptr_t unit,Read read) {
    if(!stereoUi||showOrdinary||!above||unit<0x10000)return false;
    std::array<uint8_t,0x18> unitBytes{};
    if(!read(unit,unitBytes)||Field<uint32_t>(unitBytes,0)!=1)return false;
    const uint32_t classId=Field<uint32_t>(unitBytes,4);
    const uintptr_t data=Field<uintptr_t>(unitBytes,0x10);
    if(classId>0xffff||data<0x10000)return false;
    std::array<uint8_t,0x1b> dataBytes{};
    if(!read(data,dataBytes)||Field<uint8_t>(dataBytes,0x1a))return false;
    const uintptr_t row=Field<uintptr_t>(dataBytes,0);
    if(row<0x10000)return false;
    std::array<uint8_t,0x40> rowBytes{};
    if(!read(row,rowBytes)||Field<uint16_t>(rowBytes,0)!=classId)return false;
    const uint32_t flags=Field<uint32_t>(rowBytes,0x3c);
    constexpr uint32_t required=(1u<<25)|(1u<<15);
    constexpr uint32_t keep=(1u<<6)|(1u<<7)|(1u<<8)|(1u<<9)|(1u<<10);
    return (flags&required)==required&&(flags&keep)==0;
}

static void IniMapping(){
    // [hud] monster_names wins; else Weak monster bars' ordinary_mob_bars; else the default.
    assert(FromIni(-1,-1)==Names::BossesElites&&DefaultNames==Names::BossesElites);
    assert(FromIni(-1,1)==Names::All&&FromIni(-1,0)==Names::BossesElites);
    assert(FromIni(0,0)==Names::All&&FromIni(1,1)==Names::BossesElites&&FromIni(2,1)==Names::None&&FromIni(2,0)==Names::None);
    assert(FromIni(7,1)==Names::None&&FromIni(1.4f,-1)==Names::BossesElites);   // as the menu clamps and rounds it
    assert(FromIni(NAN,1)==Names::All&&FromIni(-3,0)==Names::BossesElites);     // unreadable or negative: as missing
    assert(FromIni(-1,0.5f)==Names::All);                                        // the old key was On for any value but 0
    assert(std::strcmp(NameOf(Names::All),"All")==0&&std::strcmp(NameOf(Names::BossesElites),"Bosses and elites")==0&&
           std::strcmp(NameOf(Names::None),"None")==0);
}

static void Kinds(){
    {Fixture f;assert(f.Classify()==Kind::Ordinary&&f.reads==4);}   // unit, monster data, row, unit flags
    {Fixture f;f.Put(Fixture::unit,uint32_t(2));assert(f.Classify()==Kind::Other&&f.reads==1);}   // no monster: its record only
    for(uint32_t type:{0,2,3,4,5}){Fixture f;f.Put(Fixture::unit,type);assert(f.Classify()==Kind::Other);}
    for(unsigned bit=0;bit<8;++bit){Fixture f;f.Put(Fixture::data+0x1a,uint8_t(1u<<bit));assert(f.Classify()==Kind::Elite);}
    for(unsigned bit:{6,7}){Fixture f;f.Put(Fixture::row+0x3c,uint32_t(RowRequired|(1u<<bit)));assert(f.Classify()==Kind::Boss);}
    {Fixture f;f.Put(Fixture::row+0x3c,uint32_t(RowRequired|RowBoss));f.Put(Fixture::data+0x1a,uint8_t(8));assert(f.Classify()==Kind::Boss);}
    // Act bosses are bosses even with no unique or champion flags.
    for(uint32_t boss:{156,211,242,243,544}){
        Fixture f;f.Put(Fixture::unit+4,boss);f.Put(Fixture::row,uint16_t(boss));
        f.Put(Fixture::row+0x3c,uint32_t(RowRequired|RowBoss));assert(f.Classify()==Kind::Boss);
    }
    // NPCs, interactive and town monsters, and rows that are no enabled killable monster: no monster plate.
    for(unsigned bit:{8,9,10}){Fixture f;f.Put(Fixture::row+0x3c,uint32_t(RowRequired|(1u<<bit)));assert(f.Classify()==Kind::Other);}
    for(uint32_t flags:{0u,1u<<15,1u<<25}){Fixture f;f.Put(Fixture::row+0x3c,flags);assert(f.Classify()==Kind::Other);}
    // The mercenary: the unit flag the game's target routine gives a name label for.
    {Fixture f;f.Put(Fixture::unit+UnitFlagsAt,HirelingFlag);assert(f.Classify()==Kind::Other);}
    {Fixture f;f.Put(Fixture::unit+UnitFlagsAt,uint32_t(~HirelingFlag));assert(f.Classify()==Kind::Ordinary);}
    // A monster whose records do not read as expected is left to the game.
    for(uintptr_t at:{Fixture::data,Fixture::data+0x1a,Fixture::row,Fixture::row+0x3c,Fixture::unit+UnitFlagsAt}){
        Fixture f;f.fail=at;assert(f.Classify()==Kind::Unknown);
    }
    for(uintptr_t at:{Fixture::unit,Fixture::unit+4,Fixture::unit+0x10}){Fixture f;f.fail=at;assert(f.Classify()==Kind::Other);}
    {Fixture f;f.Put(Fixture::unit+0x10,uintptr_t(0));assert(f.Classify()==Kind::Unknown);}
    {Fixture f;f.Put(Fixture::data,uintptr_t(0));assert(f.Classify()==Kind::Unknown);}
    {Fixture f;f.Put(Fixture::row,uint16_t(20));assert(f.Classify()==Kind::Unknown);}
    {Fixture f;f.Put(Fixture::unit+4,uint32_t(0x10000));assert(f.Classify()==Kind::Unknown);}
    {Fixture f;assert(d2rcam::monsterbars::Classify(0,f.Reader())==Kind::Other&&f.reads==0);}
}

static void Choices(){
    // Which plates each choice keeps (Monster names), by kind.
    for(Kind other:{Kind::Other,Kind::Unknown})for(Names n:{Names::All,Names::BossesElites,Names::None})assert(!Hidden(n,other));
    assert(!Hidden(Names::All,Kind::Ordinary)&&!Hidden(Names::All,Kind::Elite)&&!Hidden(Names::All,Kind::Boss));
    assert(Hidden(Names::BossesElites,Kind::Ordinary)&&!Hidden(Names::BossesElites,Kind::Elite)&&!Hidden(Names::BossesElites,Kind::Boss));
    assert(Hidden(Names::None,Kind::Ordinary)&&Hidden(Names::None,Kind::Elite)&&Hidden(Names::None,Kind::Boss));
    // The overhead anchor: only in native stereo UI passes, only above the unit, nothing read for All.
    Fixture f;
    assert(f.Hide()&&f.Hide());   // the second eye answers the same
    f.reads=0;
    assert(!f.Hide(Names::BossesElites,false)&&!f.Hide(Names::BossesElites,true,false)&&!f.Hide(Names::All));
    assert(f.reads==0);
    Kind kind=Kind::Other;
    assert(d2rcam::monsterbars::Hide(true,Names::None,true,Fixture::unit,f.Reader(),&kind)&&kind==Kind::Ordinary);
    {Fixture boss;boss.Put(Fixture::row+0x3c,uint32_t(RowRequired|RowBoss));
        assert(!boss.Hide(Names::BossesElites)&&boss.Hide(Names::None)&&!boss.Hide(Names::All));}
    {Fixture elite;elite.Put(Fixture::data+0x1a,uint8_t(4));
        assert(!elite.Hide(Names::BossesElites)&&elite.Hide(Names::None));}
    {Fixture npc;npc.Put(Fixture::row+0x3c,uint32_t(RowRequired|(1u<<8)));
        assert(!npc.Hide(Names::BossesElites)&&!npc.Hide(Names::None));}
    {Fixture merc;merc.Put(Fixture::unit+UnitFlagsAt,HirelingFlag);
        assert(!merc.Hide(Names::BossesElites)&&!merc.Hide(Names::None));}
    for(uint32_t type:{0,2,3,4}){Fixture f2;f2.Put(Fixture::unit,type);assert(!f2.Hide(Names::None));}   // players, objects, missiles, items
    // No earlier target decides a later one.
    f.Put(Fixture::data+0x1a,uint8_t(8));assert(!f.Hide());
    f.Put(Fixture::data+0x1a,uint8_t(0));assert(f.Hide());
}

static void HoveredPanel(){
    using V=PanelGate::Verdict;
    // One eye pass: each monster anchor the game asks for, in order; the panel shows the last.
    const auto pass=[](Names names,std::initializer_list<std::pair<Kind,bool>> anchors){
        PanelGate g;g.BeginPass();
        for(auto [kind,placed]:anchors){const bool hidden=Hidden(names,kind);g.Anchor(kind,hidden,!hidden&&placed);}
        return g;
    };
    // An ordinary monster hovered or targeted: no plate at all under Bosses and elites or None,
    // where its panel used to stay, unanchored, wherever an earlier monster had left it.
    assert(!pass(Names::BossesElites,{{Kind::Ordinary,true}}).Paint()&&pass(Names::BossesElites,{{Kind::Ordinary,true}}).verdict==V::Hidden);
    assert(!pass(Names::None,{{Kind::Ordinary,true}}).Paint());
    assert(pass(Names::All,{{Kind::Ordinary,true}}).Paint());
    // Bosses and elites keep theirs under the default; None takes them too.
    for(Kind k:{Kind::Elite,Kind::Boss}){
        assert(pass(Names::BossesElites,{{k,true}}).Paint()&&pass(Names::All,{{k,true}}).Paint());
        assert(!pass(Names::None,{{k,true}}).Paint());
    }
    // One plate, at this eye's anchor: a monster the eye pass could not place (behind you,
    // out of view) gets none, not one where another eye or frame left the panel.
    assert(!pass(Names::All,{{Kind::Boss,false}}).Paint()&&pass(Names::All,{{Kind::Boss,false}}).verdict==V::Unplaced);
    // The game asks twice for each monster; the hovered unit first, then the controller's targets:
    // the panel carries the last monster's info, so its plate decides.
    assert(!pass(Names::BossesElites,{{Kind::Boss,true},{Kind::Boss,true},{Kind::Ordinary,true},{Kind::Ordinary,true}}).Paint());
    assert(pass(Names::BossesElites,{{Kind::Ordinary,true},{Kind::Ordinary,true},{Kind::Elite,true},{Kind::Elite,true}}).Paint());
    // NPCs, the mercenary, players and items get name labels, not the panel: they leave the verdict.
    assert(!pass(Names::BossesElites,{{Kind::Ordinary,true},{Kind::Other,true}}).Paint());
    assert(pass(Names::BossesElites,{{Kind::Boss,true},{Kind::Other,false}}).Paint());
    // A monster that did not read as expected is left to the game, placed or not.
    assert(pass(Names::None,{{Kind::Unknown,true}}).Paint()&&!pass(Names::None,{{Kind::Unknown,false}}).Paint());
    // No monster asked about in the pass (no target): the panel is the game's.
    assert(pass(Names::None,{}).Paint()&&pass(Names::None,{{Kind::Other,true}}).Paint()&&pass(Names::None,{}).verdict==V::Native);
    // Every eye pass starts afresh: nothing carries from one eye or frame to the next.
    PanelGate g;g.Anchor(Kind::Ordinary,true,false);assert(!g.Paint());g.BeginPass();assert(g.Paint());
}

// 0ccea647 ("fewer guarded reads") answers as 364cfd9f did, and Bosses and elites answers as
// both did, but for the mercenary (now kept) and a failed read of the unit flags (left to the game).
static void Equivalence(){
    uint64_t seed=0x9e3779b97f4a7c15ull;
    const auto next=[&]{seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;};
    const uintptr_t fields[]={Fixture::unit,Fixture::unit+4,Fixture::unit+0x10,Fixture::data,Fixture::data+0x1a,
                              Fixture::row,Fixture::row+0x3c,Fixture::unit+UnitFlagsAt};
    unsigned hidden=0,kept=0;
    for(int i=0;i<200000;++i){
        Fixture f;const uint64_t r=next();
        if(r&1)f.Put(Fixture::unit,uint32_t(next()%3));
        if((r>>1&7)==0)f.Put(Fixture::unit+4,uint32_t(next()%2?0x10000:20));
        if((r>>4&7)==0)f.Put(Fixture::unit+0x10,uintptr_t(next()%2?0:0x500));
        if((r>>7&3)==0)f.Put(Fixture::data+0x1a,uint8_t(next()));
        if((r>>9&7)==0)f.Put(Fixture::data,uintptr_t(0x800));
        if((r>>12&7)==0)f.Put(Fixture::row,uint16_t(next()%40));
        f.Put(Fixture::row+0x3c,uint32_t(next()&(RowRequired|RowBoss|RowLabelOnly|0x00ff0000u)|((r>>15&3)?RowRequired:0)));
        const bool merc=(r>>17&7)==0;if(merc)f.Put(Fixture::unit+UnitFlagsAt,HirelingFlag);
        if((r>>20&3)==0)f.fail=fields[next()%std::size(fields)]+next()%4;
        for(bool show:{false,true})for(bool stereo:{false,true})for(bool above:{false,true}){
            const bool before=HideBefore0ccea647(stereo,show,above,Fixture::unit,f.Reader());
            const bool rewrite=HideAt0ccea647(stereo,show,above,Fixture::unit,f.Reader());
            assert(before==rewrite);
            const bool now=d2rcam::monsterbars::Hide(stereo,show?Names::All:Names::BossesElites,above,Fixture::unit,f.Reader());
            const bool flagsFailed=f.fail>=Fixture::unit+UnitBytes&&f.fail<Fixture::unit+UnitFlagsAt+4;   // past the unit's record, up to its flags
            if(merc||flagsFailed)assert(!now);
            else assert(now==rewrite);
            (now?hidden:kept)+=1;
        }
    }
    assert(hidden>1000&&kept>1000);   // both answers are well covered
}

int main(){
    IniMapping();
    Kinds();
    Choices();
    HoveredPanel();
    Equivalence();
    std::puts("Monster names: ini mapping, unit kinds, each choice, the hovered monster's panel and the 0ccea647 rewrite passed");
}
