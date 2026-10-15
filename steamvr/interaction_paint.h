#pragma once
#include "interaction_ui.h"
namespace framevr::interaction {
// Do not display/click obsolete skill contents while a new image is pending.
// Hover-only changes can retain the previous image; the ray decides the cell.
struct PaintContext {
    bool developer=false,devVisible=false;
    int devClass=-1;
    uint64_t session=0;
    uint32_t activeSet=0;
    int hand=-1;
    int editor=0,slot=-1,page=0,count=0,bank=-1;
    bool utility=false,map=false,fresh=false,editable=false,equipmentKnown=false,beltKnown=false;
    std::array<Skill,skills::Count> skills{};
    std::array<Skill,8> options{};
    std::array<std::array<Item,2>,2> sets{};
    std::array<Belt,4> belt{};
    SkillCue cue{},leftCue{};
    controllers::Labels labels{};
    navigation::Location location;
    bool operator==(const PaintContext&) const=default;
};
inline PaintContext Context(const Snapshot& s,const Output& o,bool fresh,const controllers::Labels& labels={}) {
    PaintContext c;c.labels=labels;c.hand=o.wheelHand;c.session=s.session;c.activeSet=s.activeSet;c.editor=o.editor;c.slot=o.editSlot;
    c.developer=o.developer;c.devVisible=s.developer;c.devClass=s.devClass;
    c.page=o.editPage;c.count=o.editCount;c.bank=s.bindingBank;c.utility=o.utility;c.map=o.map;c.fresh=fresh;
    c.editable=s.bindingsEditable;c.equipmentKnown=s.equipmentKnown;c.beltKnown=s.beltKnown;
    c.skills=s.skills;c.options=o.editOptions;c.sets=s.sets;c.belt=s.belt;c.cue=o.cue;c.leftCue=o.leftCue;c.location=s.location;return c;
}
inline bool LocationImageCurrent(const PaintContext& painted,const Snapshot& snapshot) {
    return painted.fresh&&painted.session==snapshot.session&&painted.location==snapshot.location;
}
struct PaintJob {Snapshot snapshot;Output output;int selection=-1;controllers::Labels labels{};};
struct PaintedAtlas {std::vector<uint8_t> pixels;double paintMs=0;};
}
