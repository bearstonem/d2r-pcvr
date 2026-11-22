#pragma once
// Collection view: the loaded catalog, what the scan found, and what the panel
// is looking at. Pure data and text - collection_catalog.cpp opens the file and
// the painter draws - so this all runs in the tests.
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "collection_model.h"
#include "collection_panel.h"

namespace framevr::collection {

// One catalog entry, as collection.tsv holds it.
struct Entry {
    Category    category = Category::Uniques;
    uint32_t    code     = 0;   // four ASCII bytes packed as Items::MakeItemCode does
    uint32_t    recordId = 0;
    uint32_t    level    = 0;
    std::string name;
    std::string type;
    std::string extra;          // a set's name, or a runeword's runes
    std::string base;           // the base item's name (Shako, Cap, ...); empty in an older catalog
    std::string stats;          // the tooltip's stat lines with their ranges, '|' between them (from the game's tables)
    uint32_t    nameId   = 0;   // a runeword's name string id (the item's prefixIds[0]); 0 otherwise or in an older catalog
    uint32_t    hands    = 0;   // a weapon's base: 1 one-handed, 2 two-handed (weapons.txt 2handed); 0 not a weapon / unknown
    // A set piece's set bonuses, '|' between lines, each "<when>:<text>": "i2".."i6" its own with that
    // many pieces, "s2".."s5" the set's, "sF" the whole set's (prepare_collection.py). Empty otherwise.
    std::string bonus;
};

// The item-type tile an entry belongs on: by its type, a weapon by how it is held.
inline int GroupOf(const Entry& entry) {
    const int group = panel::GroupOfType(entry.type);
    if ((group == panel::OneHanded || group == panel::TwoHanded) && entry.hands != 0)
        return entry.hands == 2 ? panel::TwoHanded : panel::OneHanded;
    return group;
}

// Quest items the game's unique table also lists (Amulet of the Viper, the Horadric
// Staff, the Staff of Kings, the Hell Forge Hammer, both Khalim's Flails): not
// collectibles, so a catalog line for one is skipped (an older collection.tsv has them).
inline bool QuestUnique(std::string_view code) {
    for (const char* quest : {"vip", "hst", "msf", "hfh", "qf1", "qf2"})
        if (code == quest) return true;
    return false;
}

// The catalog plus the found flag per entry. Entries keep the file's order and
// are grouped by category; the order inside a category is the slot the panel
// draws, so a rebuilt catalog moves a slot only when its own category changed.
class Library {
public:
    // One line of collection.tsv. Comment and blank lines are ignored and count
    // as loaded; false means a malformed data line.
    bool LoadLine(std::string_view line);

    // Groups the loaded entries by category. Call once after the last line.
    void Finish();

    uint32_t     Size(Category category) const;
    uint32_t     Found(Category category) const;
    uint32_t     Total() const;
    const Entry* At(Category category, uint32_t slot) const;

    // Marks the entry with this record id as found and returns its slot, or
    // UINT32_MAX when the id is not in that category.
    uint32_t MarkFound(Category category, uint32_t recordId);
    // The same by base item code: a material (a gem or rune) carries no record
    // id, so its slot is found by the code. UINT32_MAX when the code is not in
    // that category.
    uint32_t MarkFoundCode(Category category, uint32_t code);
    // The opposite: the entry stops being found (a stored copy was taken out). The
    // item's slot, or UINT32_MAX when the id is not in that category.
    uint32_t MarkMissing(Category category, uint32_t recordId);
    uint32_t MarkMissingCode(Category category, uint32_t code);
    void     ClearFound();
    bool     IsFound(Category category, uint32_t slot) const;

    // The slot of the entry with this record id, or UINT32_MAX when it is not in
    // that category.
    uint32_t SlotOf(Category category, uint32_t recordId) const;
    // The slot of the entry with this base item code (materials are base items,
    // so they carry no record id), or UINT32_MAX.
    uint32_t SlotOfCode(Category category, uint32_t code) const;
    // A runeword's record id by its name string id (what the item keeps), or UINT32_MAX.
    uint32_t RecordOfName(Category category, uint32_t nameId) const {
        for (uint32_t slot = 0; nameId != 0 && slot < Size(category); ++slot)
            if (const Entry* entry = At(category, slot); entry != nullptr && entry->nameId == nameId) return entry->recordId;
        return UINT32_MAX;
    }

    // Four ASCII bytes packed the way Items::MakeItemCode does (first byte low),
    // shorter text padded with spaces.
    static uint32_t CodeOf(std::string_view text);

    // A unique's extra copy card (panel::CopyStep): not in the counts.
    static bool ExtraCopy(const Entry& entry) { return entry.category == Category::Uniques && entry.recordId >= panel::CopyStep; }
    uint32_t Counted(Category category) const {
        uint32_t n = 0;
        for (uint32_t slot = 0; slot < Size(category); ++slot) n += ExtraCopy(*At(category, slot)) ? 0 : 1;
        return n;
    }
    uint32_t FoundCounted(Category category) const {
        uint32_t n = 0;
        for (uint32_t slot = 0; slot < Size(category); ++slot) n += IsFound(category, slot) && !ExtraCopy(*At(category, slot)) ? 1 : 0;
        return n;
    }
    // How many cards the item with this record id has (1, or its copies).
    uint32_t CopiesOf(Category category, uint32_t recordId) const {
        uint32_t n = 0;
        while (n < 16 && SlotOf(category, panel::BaseRecord(recordId) + panel::CopyStep * n) != UINT32_MAX) ++n;
        return std::max<uint32_t>(1, n);
    }
    // A stored copy found: its preferred card (a Hellfire Torch's class) when it has one, else
    // the first of its cards not yet found. The card's record id, or UINT32_MAX.
    uint32_t FreeCopy(Category category, uint32_t recordId, int preferred) const {
        const uint32_t base = panel::BaseRecord(recordId), copies = CopiesOf(category, base);
        if (preferred >= 0 && uint32_t(preferred) < copies) {
            const uint32_t id = base + panel::CopyStep * uint32_t(preferred);
            return IsFound(category, SlotOf(category, id)) ? UINT32_MAX : id;
        }
        for (uint32_t k = 0; k < copies; ++k) {
            const uint32_t id = base + panel::CopyStep * k;
            if (!IsFound(category, SlotOf(category, id))) return id;
        }
        return UINT32_MAX;
    }
    uint32_t MarkFoundCopy(Category category, uint32_t recordId, int preferred) {
        const uint32_t id = FreeCopy(category, recordId, preferred);
        return id == UINT32_MAX ? UINT32_MAX : MarkFound(category, id);
    }

    // The copy stored for a found entry: its base item code and its item type with every
    // type above it ("swor blde mele weap") - a runeword's picture and which tiles it counts in.
    struct Made {
        uint32_t    code = 0;
        std::string types;
    };
    void SetMade(Category category, uint32_t recordId, uint32_t code, std::string types) {
        const uint32_t slot = SlotOf(category, recordId);
        if (slot == UINT32_MAX || code == 0) return;
        if (made_.size() != entries_.size()) made_.resize(entries_.size());
        made_[start_[static_cast<std::size_t>(category)] + slot] = {code, std::move(types)};
    }
    // Null when unknown (nothing stored, or an older store that did not say).
    const Made* MadeAt(Category category, uint32_t slot) const {
        if (!finished_ || slot >= Size(category)) return nullptr;
        const std::size_t index = start_[static_cast<std::size_t>(category)] + slot;
        return index < made_.size() && made_[index].code != 0 ? &made_[index] : nullptr;
    }

private:
    std::vector<Made>    made_;
    std::vector<Entry>   entries_;
    std::vector<uint8_t> found_;
    uint32_t             start_[CategoryCount + 1] {};
    bool                 finished_ = false;
};

// What the panel shows: the open category, the slot size, the filter, the page
// inside it and the pointed slot. The slot grid is collection_panel.h's.
struct ViewState {
    Category     category = Category::Uniques;
    panel::Size  size     = panel::Size::Medium;
    panel::Show  show     = panel::Show::All;
    panel::Sort  sort     = panel::Sort::Name;
    int          group    = panel::NoGroup;   // an item-type tile filtering the grid, or NoGroup; on Sets a set tile of the band
    int          band     = 0;                // Sets: the level range whose sets the tiles show
    int          kind     = panel::NoGroup;   // Runewords: the top-row tile (what they are made in); group is its sub-tile
    panel::Drop  open     = panel::Drop::None;   // the footer list that is open
    int          page     = 0;
    panel::Hit   hover {};   // what the laser is over: the slot or control to light

    // Entries the filters keep. Walking the category is cheap (a few hundred).
    int VisibleCount(const Library& library) const;
    int  Pages(const Library& library) const { return panel::PagesFor(VisibleCount(library), size); }
    void Clamp(const Library& library) {
        page    = std::clamp(page, 0, std::max(0, Pages(library) - 1));
        hover = {};
    }
    void SelectCategory(const Library& library, Category want) {
        if (category != want) group = kind = panel::NoGroup;   // a tile means another thing on each page
        category = want;
        page     = 0;
        hover = {};
        Clamp(library);
    }
    // A bigger slot shows fewer of them, and a filter shows fewer entries, so the
    // page the same entry lands on changes: the caller keeps what it was looking at.
    void SelectSize(const Library& library, panel::Size want) {
        if (size == want) return;
        size    = want;
        hover = {};
        Clamp(library);
    }
    // A footer list: opened by its box, closed by the box again or by a choice.
    void ToggleDrop(panel::Drop list) { open = open == list ? panel::Drop::None : list; }
    void SelectShow(const Library& library, panel::Show want) {
        open = panel::Drop::None;
        if (show == want) return;
        show    = want;
        page    = 0;
        hover = {};
        Clamp(library);
    }
    void SelectSort(const Library& library, panel::Sort want) {
        open = panel::Drop::None;
        if (sort == want) return;
        sort  = want;
        page  = 0;
        hover = {};
        Clamp(library);
    }
    // A tile picked again lets go: all types.
    void ToggleGroup(const Library& library, int want) {
        group = group == want ? panel::NoGroup : want;
        page  = 0;
        hover = {};
        Clamp(library);
    }
    // Runewords: a top-row tile; another one (or the same again) lets the sub-tile go.
    void ToggleKind(const Library& library, int want) {
        kind  = kind == want ? panel::NoGroup : want;
        group = panel::NoGroup;
        page  = 0;
        hover = {};
        Clamp(library);
    }
    // Sets: another level range shows other set tiles, so a picked one lets go.
    void SelectBand(const Library& library, int want) {
        want = std::clamp(want, 0, panel::BandCount - 1);
        if (band == want) return;
        band  = want;
        group = panel::NoGroup;
        page  = 0;
        hover = {};
        Clamp(library);
    }
    void Step(const Library& library, int by) {
        page += by;
        Clamp(library);
    }
};

// One set as its tile shows it: the name, the pieces found of all, and the
// highest piece level, which places it in a band.
struct SetInfo {
    std::string_view name;
    uint32_t         level = 0, found = 0, total = 0;
};
// The level each band ends at, sorted by a set's highest piece: 34 sets, at most
// 12 a band, class sets mixed in with the rest.
inline constexpr uint32_t BandTop[panel::BandCount] = {8, 28, 69, 999};
inline constexpr const char* BandName(int band) {
    constexpr const char* names[panel::BandCount] = {"1-8", "9-28", "42-69", "70+"};
    return band >= 0 && band < panel::BandCount ? names[band] : "";
}
inline int BandOfLevel(uint32_t level) {
    for (int band = 0; band < panel::BandCount; ++band)
        if (level <= BandTop[band]) return band;
    return panel::BandCount - 1;
}
inline std::vector<SetInfo> AllSets(const Library& library) {
    std::vector<SetInfo> sets;
    for (uint32_t slot = 0; slot < library.Size(Category::Sets); ++slot) {
        const Entry* entry = library.At(Category::Sets, slot);
        if (entry == nullptr || entry->extra.empty()) continue;
        auto it = std::find_if(sets.begin(), sets.end(), [&](const SetInfo& s) { return s.name == entry->extra; });
        if (it == sets.end()) it = sets.insert(sets.end(), SetInfo{entry->extra});
        it->level = std::max(it->level, entry->level);
        ++it->total;
        if (library.IsFound(Category::Sets, slot)) ++it->found;
    }
    std::stable_sort(sets.begin(), sets.end(), [](const SetInfo& a, const SetInfo& b) {
        return a.level != b.level ? a.level < b.level : a.name < b.name;
    });
    return sets;
}
// The sets of one band, lowest first; at most GroupCount, the tiles there are.
inline std::vector<SetInfo> SetsInBand(const Library& library, int band) {
    std::vector<SetInfo> sets;
    for (const SetInfo& set : AllSets(library))
        if (BandOfLevel(set.level) == band && int(sets.size()) < panel::GroupCount) sets.push_back(set);
    return sets;
}

// Runewords' tiles. A tile keeps the runewords that can be made in every item it
// stands for: its accepts are its own item type and every type above it in the
// game's itemtypes.txt (a sword is a blde, a mele and a weap), and a runeword fits
// when one of its own types (Entry::base) is among them.
// A top-row tile keeps what can be made in ANY item of its kind (owner 2026-11-22: Honor, a
// melee runeword, belongs under All weapons too): its accepts are the kind's types above and
// below it. own is the tile's own item type: a made runeword counts on the tiles whose own type
// its stored copy is (an Honor in a sword counts on Swords, not on Wands).
// A sub-tile (a weapon type, a class's shields) keeps only the runewords made for that kind of
// item (owner 2026-11-22: Honor, made in any melee weapon, showed under Daggers, Katars ...):
// none of their types is a broad one (any weapon, any melee weapon, any shield ...), and one of
// them is the tile's (only). Insight's melee card (polearms or staves) is under both.
struct RunewordTile {
    const char* name;
    const char* own;
    const char* accepts;
    const char* only = nullptr;
};
inline constexpr const char* BroadTypes = "weap mele miss blde sppl blun rod shld armo";
inline constexpr RunewordTile RunewordKinds[panel::KindCount] = {
    {"All weapons", "weap", "weap mele miss blde sppl blun rod swor axe knif pole spea hamm club scep mace h2h wand staf bow xbow orb jave"},
    {"Melee", "mele", "mele weap blde sppl blun rod swor axe knif pole spea hamm club scep mace h2h wand staf"},
    {"Missile", "miss", "miss weap bow xbow"},
    {"Body armor", "tors", "tors armo"},
    {"Helms", "helm", "helm armo pelt phlm circ"},
    {"Shields", "shld", "shld armo shie ashd pala head grim"},
};
inline constexpr RunewordTile MeleeTypes[panel::GroupCount] = {
    {"Swords", "swor", "swor blde mele weap", "swor"},       {"Axes", "axe", "axe mele weap", "axe"},
    {"Daggers", "knif", "knif blde mele weap", "knif"},      {"Polearms", "pole", "pole sppl mele weap", "pole"},
    {"Spears", "spea", "spea sppl mele weap", "spea aspe"},  {"Hammers", "hamm", "hamm blun mele weap", "hamm"},
    {"Clubs", "club", "club blun mele weap", "club"},        {"Scepters", "scep", "scep rod blun mele weap", "scep"},
    {"Maces", "mace", "mace blun mele weap", "mace"},        {"Katars", "h2h", "h2h mele weap", "h2h h2h2"},
    {"Wands", "wand", "wand rod blun mele weap", "wand"},    {"Staves", "staf", "staf rod blun mele weap", "staf"},
};
inline constexpr RunewordTile ShieldTypes[] = {
    {"Paladin", "ashd", "ashd pala shld armo", "ashd pala"}, {"Necromancer", "head", "head shld armo", "head"},
    {"Warlock", "grim", "grim shld armo", "grim"},
};
// The sub-tiles under a top-row tile: the melee weapon types under All weapons and
// Melee, the class shields under Shields; none under the rest.
inline int RunewordSubCount(int kind) {
    return kind == 0 || kind == 1 ? panel::GroupCount : kind == 5 ? int(std::size(ShieldTypes)) : 0;
}
inline const RunewordTile* RunewordSub(int kind, int sub) {
    if (sub < 0 || sub >= RunewordSubCount(kind)) return nullptr;
    return kind == 5 ? &ShieldTypes[sub] : &MeleeTypes[sub];
}
// The open tile: the sub-tile, else the top-row tile; null for no filter.
inline const RunewordTile* RunewordOpenTile(int kind, int sub) {
    if (kind < 0 || kind >= panel::KindCount) return nullptr;
    if (const RunewordTile* tile = RunewordSub(kind, sub)) return tile;
    return &RunewordKinds[kind];
}
inline bool RunewordFits(std::string_view accepts, std::string_view types) {
    const auto has = [&](std::string_view type) {
        size_t at = 0;
        while (at < accepts.size()) {
            const size_t end = std::min(accepts.find(' ', at), accepts.size());
            if (accepts.substr(at, end - at) == type) return true;
            at = end + 1;
        }
        return false;
    };
    size_t at = 0;
    while (at < types.size()) {
        const size_t end = std::min(types.find(' ', at), types.size());
        if (end > at && has(types.substr(at, end - at))) return true;
        at = end + 1;
    }
    return false;
}

// Whether a runeword slot belongs on a tile: by what it can be made in, or - made ones on the
// Made filter - by what its stored copy is (an older store that said nothing: by the former).
inline bool RunewordInTile(const Library& library, uint32_t slot, const RunewordTile& tile, bool byMade) {
    const Entry* entry = library.At(Category::Runewords, slot);
    if (entry == nullptr) return false;
    if (tile.only != nullptr) {   // a sub-tile: runewords made for its kind of item alone
        bool mine = false;
        size_t at = 0;
        const std::string_view types = entry->base;
        while (at < types.size()) {
            const size_t end = std::min(types.find(' ', at), types.size());
            const std::string_view type = types.substr(at, end - at);
            if (!type.empty()) {
                if (RunewordFits(BroadTypes, type)) return false;
                mine = mine || RunewordFits(tile.only, type);
            }
            at = end + 1;
        }
        return mine;
    }
    if (byMade && library.IsFound(Category::Runewords, slot))
        if (const Library::Made* made = library.MadeAt(Category::Runewords, slot)) return RunewordFits(tile.own, made->types);
    return RunewordFits(tile.accepts, entry->base);
}

// The top-row tile that shows a runeword just stored (owner 2026-11-22: put away while a
// tile without it is on, the tiles turn to it): the narrowest kind its copy is, else the
// narrowest it can be made in; NoGroup when none.
inline int RunewordKindFor(const Library& library, uint32_t slot) {
    constexpr int order[panel::KindCount] = {3, 4, 5, 2, 1, 0};
    const Library::Made* made = library.MadeAt(Category::Runewords, slot);
    for (int kind : order)
        if (made != nullptr && RunewordFits(RunewordKinds[kind].own, made->types)) return kind;
    for (int kind : order)
        if (RunewordInTile(library, slot, RunewordKinds[kind], false)) return kind;
    return panel::NoGroup;
}

// The catalog slots the filter keeps, in order: the page's slots are positions
// in this list, not catalog slots. Built once per paint.
inline std::vector<uint32_t> VisibleSlots(const Library& library, const ViewState& view) {
    std::vector<uint32_t> slots;
    const uint32_t        count = library.Size(view.category);
    slots.reserve(count);
    const RunewordTile* runewordTile = view.category == Category::Runewords ? RunewordOpenTile(view.kind, view.group) : nullptr;
    // Sets: the band's sets; a picked tile only its set's pieces.
    const bool          sets = view.category == Category::Sets;
    std::vector<SetInfo> band;
    if (sets) band = SetsInBand(library, view.band);
    const bool picked = sets && view.group >= 0 && view.group < int(band.size());
    for (uint32_t slot = 0; slot < count; ++slot) {
        const bool found = library.IsFound(view.category, slot);
        if (view.show == panel::Show::Missing && found) continue;
        if (view.show == panel::Show::Found && !found) continue;
        if (sets) {
            const Entry* entry = library.At(view.category, slot);
            // the level range's sets, or the picked one of them (owner 2026-11-22: the range
            // buttons left the grid as it was)
            if (entry == nullptr) continue;
            if (picked ? entry->extra != band[size_t(view.group)].name
                       : std::none_of(band.begin(), band.end(), [&](const SetInfo& set) { return set.name == entry->extra; }))
                continue;
        } else if (view.category == Category::Runewords) {
            if (runewordTile != nullptr && !RunewordInTile(library, slot, *runewordTile, view.show == panel::Show::Found)) continue;
        } else if (view.group != panel::NoGroup) {
            const Entry* entry = library.At(view.category, slot);
            if (entry == nullptr || GroupOf(*entry) != view.group) continue;
        }
        slots.push_back(slot);
    }
    // Name: alphabetical; Level: the item's level, then the name. Stable, so equal
    // names keep the catalog's order.
    std::stable_sort(slots.begin(), slots.end(), [&](uint32_t a, uint32_t b) {
        const Entry* ea = library.At(view.category, a);
        const Entry* eb = library.At(view.category, b);
        if (ea == nullptr || eb == nullptr) return false;
        if (view.sort == panel::Sort::Level && ea->level != eb->level) return ea->level < eb->level;
        if (ea->name != eb->name) return ea->name < eb->name;
        return ea->recordId < eb->recordId;   // a runeword's two cards: its first kind first
    });
    return slots;
}

inline int ViewState::VisibleCount(const Library& library) const { return int(VisibleSlots(library, *this).size()); }

// One item-type tile's share: how many of the open category's entries of that
// type are found, out of how many there are.
struct GroupShare {
    uint32_t found = 0, total = 0;
};
inline GroupShare ShareOf(const Library& library, Category category, int group) {
    GroupShare share;
    for (uint32_t slot = 0; slot < library.Size(category); ++slot) {
        const Entry* entry = library.At(category, slot);
        if (entry == nullptr || GroupOf(*entry) != group || Library::ExtraCopy(*entry)) continue;
        ++share.total;
        if (library.IsFound(category, slot)) ++share.found;
    }
    return share;
}

// A runeword tile's share: the runewords that fit it, and how many of them are found.
// total: those that can be made in it; found: those made in it (RunewordInTile by the copy).
inline GroupShare RunewordShare(const Library& library, const RunewordTile& tile) {
    GroupShare share;
    for (uint32_t slot = 0; slot < library.Size(Category::Runewords); ++slot) {
        if (RunewordInTile(library, slot, tile, false)) ++share.total;
        if (library.IsFound(Category::Runewords, slot) && RunewordInTile(library, slot, tile, true)) ++share.found;
    }
    return share;
}
// The stored copy of the entry a panel slot shows, or null.
inline const Library::Made* SlotMade(const Library& library, const ViewState& view, const std::vector<uint32_t>& visible, int slot) {
    const int slots = panel::SlotsFor(view.size);
    if (slot < 0 || slot >= slots) return nullptr;
    const size_t at = size_t(view.page) * size_t(slots) + size_t(slot);
    return at < visible.size() && library.IsFound(view.category, visible[at]) ? library.MadeAt(view.category, visible[at]) : nullptr;
}

// The entry a panel slot shows, or null when the page has fewer entries.
inline const Entry* SlotEntry(const Library& library, const ViewState& view, const std::vector<uint32_t>& visible, int slot) {
    const int slots = panel::SlotsFor(view.size);
    if (slot < 0 || slot >= slots) return nullptr;
    const size_t at = size_t(view.page) * size_t(slots) + size_t(slot);
    if (at >= visible.size()) return nullptr;
    return library.At(view.category, visible[at]);
}

inline bool SlotFound(const Library& library, const ViewState& view, const std::vector<uint32_t>& visible, int slot) {
    const int slots = panel::SlotsFor(view.size);
    if (slot < 0 || slot >= slots) return false;
    const size_t at = size_t(view.page) * size_t(slots) + size_t(slot);
    if (at >= visible.size()) return false;
    return library.IsFound(view.category, visible[at]);
}

// "122 / 403" for the footer, for the open category.
inline std::string CounterText(const Library& library, const ViewState& view) {
    return std::to_string(library.FoundCounted(view.category)) + " / " + std::to_string(library.Counted(view.category));
}

// A runeword's recipe ("r08 r09 r07") in the runes' own names ("Ral Ort Tal").
inline std::string RuneNames(std::string_view codes) {
    constexpr const char* names[] = {"El", "Eld", "Tir", "Nef", "Eth", "Ith", "Tal", "Ral", "Ort", "Thul", "Amn",
                                     "Sol", "Shael", "Dol", "Hel", "Io", "Lum", "Ko", "Fal", "Lem", "Pul", "Um",
                                     "Mal", "Ist", "Gul", "Vex", "Ohm", "Lo", "Sur", "Ber", "Jah", "Cham", "Zod"};
    std::string out;
    size_t at = 0;
    while (at < codes.size()) {
        size_t end = codes.find(' ', at);
        if (end == std::string_view::npos) end = codes.size();
        const std::string_view word = codes.substr(at, end - at);
        std::string name(word);
        if (word.size() == 3 && word[0] == 'r' && word[1] >= '0' && word[1] <= '9' && word[2] >= '0' && word[2] <= '9') {
            const int number = (word[1] - '0') * 10 + (word[2] - '0');
            if (number >= 1 && number <= 33) name = names[number - 1];
        }
        if (!name.empty()) out += (out.empty() ? "" : " ") + name;
        at = end + 1;
    }
    return out;
}

// A runeword's item types (runes.txt itype codes, "swor axe") as words ("Swords, Axes").
inline std::string TypeNames(std::string_view codes) {
    struct Pair { const char* code; const char* name; };
    constexpr Pair pairs[] = {
        {"weap", "Weapons"}, {"mele", "Melee Weapons"}, {"miss", "Missile Weapons"}, {"swor", "Swords"},
        {"axe", "Axes"}, {"mace", "Maces"}, {"club", "Clubs"}, {"hamm", "Hammers"}, {"scep", "Scepters"},
        {"pole", "Polearms"}, {"spea", "Spears"}, {"staf", "Staves"}, {"knif", "Daggers"}, {"h2h", "Claws"},
        {"wand", "Wands"}, {"orb", "Orbs"}, {"bow", "Bows"}, {"xbow", "Crossbows"}, {"tors", "Body Armor"},
        {"helm", "Helms"}, {"circ", "Circlets"}, {"pelt", "Druid Pelts"}, {"phlm", "Barbarian Helms"},
        {"shld", "Shields"}, {"ashd", "Paladin Shields"}, {"pala", "Paladin Shields"}, {"head", "Necromancer Heads"},
        {"grim", "Grimoires"},
    };
    std::string out;
    size_t at = 0;
    while (at < codes.size()) {
        size_t end = codes.find(' ', at);
        if (end == std::string_view::npos) end = codes.size();
        const std::string_view word = codes.substr(at, end - at);
        std::string name(word);
        for (const Pair& pair : pairs)
            if (word == pair.code) name = pair.name;
        if (!word.empty() && out.find(name) == std::string::npos) out += (out.empty() ? "" : ", ") + name;
        at = end + 1;
    }
    return out;
}

namespace detail {
inline uint32_t ParseU32(std::string_view text) {
    uint32_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return 0;
        value = value * 10 + uint32_t(c - '0');
    }
    return value;
}
}   // namespace detail

inline uint32_t Library::CodeOf(std::string_view text) {
    uint32_t code = 0;
    for (int i = 0; i < 4; ++i)
        code |= uint32_t(uint8_t(i < int(text.size()) ? text[size_t(i)] : ' ')) << (8 * i);
    return code;
}

inline bool Library::LoadLine(std::string_view line) {
    if (line.empty() || line[0] == '#') return true;   // a comment, not a data line
    std::string_view field[12];
    int              count = 0;
    std::size_t      at    = 0;
    while (count < 12) {
        const std::size_t tab = line.find('\t', at);
        field[count++] = line.substr(at, tab == std::string_view::npos ? std::string_view::npos : tab - at);
        if (tab == std::string_view::npos) break;
        at = tab + 1;
    }
    if (count < 6) return false;
    Category category = Category::Count;
    if (!CategoryFromTag(field[0], category)) return false;
    if (category == Category::Uniques && QuestUnique(field[1])) return true;   // a quest item: not a collectible
    entries_.push_back({category, CodeOf(field[1]), detail::ParseU32(field[2]), detail::ParseU32(field[4]),
                        std::string(field[3]), std::string(field[5]), count > 6 ? std::string(field[6]) : std::string(),
                        count > 7 ? std::string(field[7]) : std::string(),
                        count > 8 ? std::string(field[8]) : std::string(),
                        count > 9 ? detail::ParseU32(field[9]) : 0u,
                        count > 10 ? detail::ParseU32(field[10]) : 0u,
                        count > 11 ? std::string(field[11]) : std::string()});
    return true;
}

inline void Library::Finish() {
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const Entry& a, const Entry& b) { return a.category < b.category; });
    uint32_t counts[CategoryCount] {};
    for (const Entry& entry : entries_) ++counts[static_cast<std::size_t>(entry.category)];
    start_[0] = 0;
    for (std::size_t i = 0; i < CategoryCount; ++i) start_[i + 1] = start_[i] + counts[i];
    found_.assign(entries_.size(), 0);
    finished_ = true;
}

inline uint32_t Library::Size(Category category) const {
    const auto index = static_cast<std::size_t>(category);
    return index < CategoryCount ? start_[index + 1] - start_[index] : 0;
}

inline uint32_t Library::Total() const { return uint32_t(entries_.size()); }

inline const Entry* Library::At(Category category, uint32_t slot) const {
    if (!finished_ || slot >= Size(category)) return nullptr;
    return &entries_[start_[static_cast<std::size_t>(category)] + slot];
}

inline bool Library::IsFound(Category category, uint32_t slot) const {
    if (!finished_ || slot >= Size(category)) return false;
    return found_[start_[static_cast<std::size_t>(category)] + slot] != 0;
}

inline uint32_t Library::Found(Category category) const {
    uint32_t count = 0;
    for (uint32_t slot = 0; slot < Size(category); ++slot)
        if (IsFound(category, slot)) ++count;
    return count;
}

inline uint32_t Library::MarkFound(Category category, uint32_t recordId) {
    if (!finished_) return UINT32_MAX;
    for (uint32_t slot = 0; slot < Size(category); ++slot) {
        const uint32_t index = start_[static_cast<std::size_t>(category)] + slot;
        if (entries_[index].recordId != recordId) continue;
        found_[index] = 1;
        return slot;
    }
    return UINT32_MAX;
}

inline uint32_t Library::MarkFoundCode(Category category, uint32_t code) {
    if (!finished_) return UINT32_MAX;
    for (uint32_t slot = 0; slot < Size(category); ++slot) {
        const uint32_t index = start_[static_cast<std::size_t>(category)] + slot;
        if (entries_[index].code != code) continue;
        found_[index] = 1;
        return slot;
    }
    return UINT32_MAX;
}

inline void Library::ClearFound() {
    std::fill(found_.begin(), found_.end(), uint8_t(0));
    made_.clear();
}

inline uint32_t Library::MarkMissing(Category category, uint32_t recordId) {
    if (!finished_) return UINT32_MAX;
    for (uint32_t slot = 0; slot < Size(category); ++slot) {
        const uint32_t index = start_[static_cast<std::size_t>(category)] + slot;
        if (entries_[index].recordId != recordId) continue;
        found_[index] = 0;
        return slot;
    }
    return UINT32_MAX;
}

inline uint32_t Library::MarkMissingCode(Category category, uint32_t code) {
    if (!finished_) return UINT32_MAX;
    for (uint32_t slot = 0; slot < Size(category); ++slot) {
        const uint32_t index = start_[static_cast<std::size_t>(category)] + slot;
        if (entries_[index].code != code) continue;
        found_[index] = 0;
        return slot;
    }
    return UINT32_MAX;
}

inline uint32_t Library::SlotOf(Category category, uint32_t recordId) const {
    if (!finished_) return UINT32_MAX;
    for (uint32_t slot = 0; slot < Size(category); ++slot)
        if (entries_[start_[static_cast<std::size_t>(category)] + slot].recordId == recordId) return slot;
    return UINT32_MAX;
}

inline uint32_t Library::SlotOfCode(Category category, uint32_t code) const {
    if (!finished_) return UINT32_MAX;
    for (uint32_t slot = 0; slot < Size(category); ++slot)
        if (entries_[start_[static_cast<std::size_t>(category)] + slot].code == code) return slot;
    return UINT32_MAX;
}

}   // namespace framevr::collection
