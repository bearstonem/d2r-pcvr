#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace d2rcam::monsterbars {
// VR Settings > Interface > Labels > Monster names ([hud] monster_names): whose
// name and health plates the game draws over monsters in native VR. NPCs, the
// mercenary, players, objects and items keep their labels with every choice.
enum class Names : uint8_t {All=0,BossesElites=1,None=2};
inline constexpr Names DefaultNames=Names::BossesElites;
inline const char* NameOf(Names names) {
    return names==Names::All?"All":names==Names::None?"None":"Bosses and elites";
}
// The ini: [hud] monster_names (0 All, 1 Bosses and elites, 2 None) when it is
// there, else the older [hud] ordinary_mob_bars (1 All, 0 Bosses and elites;
// "Weak monster bars", 2026-10-13), else the default. A key that is absent is
// passed as a negative value; a saved choice is only ever read, never rewritten.
inline Names FromIni(float monsterNames,float ordinaryMobBars) {
    if(monsterNames>=0&&monsterNames<1e6f)return Names(monsterNames<.5f?0:monsterNames<1.5f?1:2);
    if(ordinaryMobBars>=0&&ordinaryMobBars<1e6f)return ordinaryMobBars!=0?Names::All:Names::BossesElites;
    return DefaultNames;
}

// What plate a unit has. Other: no monster plate - not a monster, an NPC,
// interactive or town monster, a row that is not an enabled killable monster,
// or the mercenary. Unknown: a monster whose records did not read as expected;
// its plate is left to the game. Elite: a superunique, champion, unique or a
// minion of one (the special-monster flags). Boss: an act boss or other boss row.
enum class Kind : uint8_t {Other,Unknown,Ordinary,Elite,Boss};
inline bool Monster(Kind kind) {return kind!=Kind::Other;}
inline bool Hidden(Names names,Kind kind) {
    switch(kind) {
    case Kind::Ordinary:return names!=Names::All;
    case Kind::Elite:case Kind::Boss:return names==Names::None;
    default:return false;
    }
}

// D2R 3.3.93787: UnitAny type at +0, class at +4, pMonsterData at +0x10, unit
// flags at +0x124; MonsterData's MonStats pointer at +0, special-monster flags
// at +0x1a; the MonStats row's id at +0, flags at +0x3c. Unit flag bit 9 is the
// one the game's own target routine tests to give a unit a name label instead
// of the monster health panel (the mercenary). See docs/monster-bars.md for the
// layout evidence and validation limits. Read is injected so failed and stale
// reads can be covered without a game; natively each read is a system call:
// one per record (the unit's first 0x18 bytes, the monster data's first 0x1b,
// the row's first 0x40) and four bytes of the unit's flags, four for a monster,
// one for a unit that is no monster.
inline constexpr size_t UnitBytes=0x18,DataBytes=0x1b,RowBytes=0x40,UnitFlagsAt=0x124;
inline constexpr uint32_t HirelingFlag=1u<<9;
inline constexpr uint32_t RowRequired=(1u<<25)|(1u<<15);              // enabled, killable
inline constexpr uint32_t RowLabelOnly=(1u<<8)|(1u<<9)|(1u<<10);       // npc, interact, inTown
inline constexpr uint32_t RowBoss=(1u<<6)|(1u<<7);                     // boss, primeevil
template<class T,size_t N> T Field(const std::array<uint8_t,N>& bytes,size_t at) {
    T value{};std::memcpy(&value,bytes.data()+at,sizeof value);return value;
}
template<class Read> Kind Classify(uintptr_t unit,Read read) {
    if(unit<0x10000)return Kind::Other;
    std::array<uint8_t,UnitBytes> unitBytes{};
    if(!read(unit,unitBytes)||Field<uint32_t>(unitBytes,0)!=1)return Kind::Other;
    const uint32_t classId=Field<uint32_t>(unitBytes,4);
    const uintptr_t data=Field<uintptr_t>(unitBytes,0x10);
    if(classId>0xffff||data<0x10000)return Kind::Unknown;
    std::array<uint8_t,DataBytes> dataBytes{};
    if(!read(data,dataBytes))return Kind::Unknown;
    const uint8_t special=Field<uint8_t>(dataBytes,0x1a);
    const uintptr_t row=Field<uintptr_t>(dataBytes,0);
    if(row<0x10000)return Kind::Unknown;
    std::array<uint8_t,RowBytes> rowBytes{};
    if(!read(row,rowBytes)||Field<uint16_t>(rowBytes,0)!=classId)return Kind::Unknown;
    const uint32_t flags=Field<uint32_t>(rowBytes,0x3c);
    if((flags&RowRequired)!=RowRequired||(flags&RowLabelOnly))return Kind::Other;
    std::array<uint8_t,4> unitFlags{};
    if(!read(unit+UnitFlagsAt,unitFlags))return Kind::Unknown;
    if(Field<uint32_t>(unitFlags,0)&HirelingFlag)return Kind::Other;
    if(flags&RowBoss)return Kind::Boss;
    return special?Kind::Elite:Kind::Ordinary;
}
// The overhead anchor (UnitToScreen, above the unit) in a native stereo UI pass:
// true takes it away, so the game draws no name for the unit and hands its
// health panel no place. All reads nothing. kind, when given, receives the
// unit's Kind (Other when nothing was read). d2rcam's hook makes the same
// decision from Classify and Hidden itself, classifying each unit once a pass
// for both of the game's anchor requests and for PanelGate under every choice.
template<class Read> bool Hide(bool stereoUi,Names names,bool above,uintptr_t unit,Read read,Kind* kind=nullptr) {
    if(kind)*kind=Kind::Other;
    if(!stereoUi||names==Names::All||!above)return false;
    const Kind k=Classify(unit,read);
    if(kind)*kind=k;
    return Hidden(names,k);
}

// The hovered monster's panel (the game's MonsterHealth panel: name, health,
// immunities). The game's target routine is run for the hovered unit, then (in
// controller layout, which VR keeps) for the controller's target and second
// target; for each monster it asks for the overhead anchor and sends the panel
// that monster's info with the anchor. The panel keeps the last info, and moves
// only to an anchor that was found: with none it stays where an earlier monster,
// eye or frame left it. Fed every monster anchor of an eye pass in order, this
// says whether the panel may be painted in that pass: never with a monster
// whose plate the setting hides, never at a place this pass did not give it
// (one plate, at this eye's anchor). A pass with no monster anchor leaves the
// panel to the game. Other units (NPCs, the mercenary, players, objects, items)
// get the game's name label instead, so they leave the verdict as it is.
struct PanelGate {
    enum class Verdict : uint8_t {Native,Paint,Hidden,Unplaced};
    Verdict verdict=Verdict::Native;
    void BeginPass() {verdict=Verdict::Native;}
    void Anchor(Kind kind,bool hidden,bool placed) {
        if(!Monster(kind))return;
        verdict=hidden?Verdict::Hidden:placed?Verdict::Paint:Verdict::Unplaced;
    }
    bool Paint()const {return verdict==Verdict::Native||verdict==Verdict::Paint;}
};
}
