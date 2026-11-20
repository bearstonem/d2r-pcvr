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
};

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

    // Four ASCII bytes packed the way Items::MakeItemCode does (first byte low),
    // shorter text padded with spaces.
    static uint32_t CodeOf(std::string_view text);

private:
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
    int          group    = panel::NoGroup;   // an item-type tile filtering the grid, or NoGroup
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
    void Step(const Library& library, int by) {
        page += by;
        Clamp(library);
    }
};

// The catalog slots the filter keeps, in order: the page's slots are positions
// in this list, not catalog slots. Built once per paint.
inline std::vector<uint32_t> VisibleSlots(const Library& library, const ViewState& view) {
    std::vector<uint32_t> slots;
    const uint32_t        count = library.Size(view.category);
    slots.reserve(count);
    for (uint32_t slot = 0; slot < count; ++slot) {
        const bool found = library.IsFound(view.category, slot);
        if (view.show == panel::Show::Missing && found) continue;
        if (view.show == panel::Show::Found && !found) continue;
        if (view.group != panel::NoGroup) {
            const Entry* entry = library.At(view.category, slot);
            if (entry == nullptr || panel::GroupOfType(entry->type) != view.group) continue;
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
        return ea->name < eb->name;
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
        if (entry == nullptr || panel::GroupOfType(entry->type) != group) continue;
        ++share.total;
        if (library.IsFound(category, slot)) ++share.found;
    }
    return share;
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
    return std::to_string(library.Found(view.category)) + " / " + std::to_string(library.Size(view.category));
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
    std::string_view field[8];
    int              count = 0;
    std::size_t      at    = 0;
    while (count < 8) {
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
                        count > 7 ? std::string(field[7]) : std::string()});
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

inline void Library::ClearFound() { std::fill(found_.begin(), found_.end(), uint8_t(0)); }

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
