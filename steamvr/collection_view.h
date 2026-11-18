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
};

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
    void     ClearFound();
    bool     IsFound(Category category, uint32_t slot) const;

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
    panel::Size  size     = panel::Size::Small;
    panel::Show  show     = panel::Show::All;
    int          page     = 0;
    int          hovered  = -1;   // slot on this page, -1 none

    // Entries the filter keeps. Walking the category is cheap (a few hundred)
    // and it is asked for once a frame, not once a slot.
    int VisibleCount(const Library& library) const {
        const uint32_t count = library.Size(category);
        if (show == panel::Show::All) return int(count);
        int kept = 0;
        for (uint32_t slot = 0; slot < count; ++slot) {
            const bool found = library.IsFound(category, slot);
            if ((show == panel::Show::Missing && !found) || (show == panel::Show::Found && found)) ++kept;
        }
        return kept;
    }
    int  Pages(const Library& library) const { return panel::PagesFor(VisibleCount(library), size); }
    void Clamp(const Library& library) {
        page    = std::clamp(page, 0, std::max(0, Pages(library) - 1));
        hovered = -1;
    }
    void SelectCategory(const Library& library, Category want) {
        category = want;
        page     = 0;
        hovered  = -1;
        Clamp(library);
    }
    // A bigger slot shows fewer of them, and a filter shows fewer entries, so the
    // page the same entry lands on changes: the caller keeps what it was looking at.
    void SelectSize(const Library& library, panel::Size want) {
        if (size == want) return;
        size    = want;
        hovered = -1;
        Clamp(library);
    }
    void SelectShow(const Library& library, panel::Show want) {
        if (show == want) return;
        show    = want;
        page    = 0;
        hovered = -1;
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
        slots.push_back(slot);
    }
    return slots;
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
    std::string_view field[7];
    int              count = 0;
    std::size_t      at    = 0;
    while (count < 7) {
        const std::size_t tab = line.find('\t', at);
        field[count++] = line.substr(at, tab == std::string_view::npos ? std::string_view::npos : tab - at);
        if (tab == std::string_view::npos) break;
        at = tab + 1;
    }
    if (count < 6) return false;
    Category category = Category::Count;
    if (!CategoryFromTag(field[0], category)) return false;
    entries_.push_back({category, CodeOf(field[1]), detail::ParseU32(field[2]), detail::ParseU32(field[4]),
                        std::string(field[3]), std::string(field[5]), count > 6 ? std::string(field[6]) : std::string()});
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

inline void Library::ClearFound() { std::fill(found_.begin(), found_.end(), uint8_t(0)); }

}   // namespace framevr::collection
