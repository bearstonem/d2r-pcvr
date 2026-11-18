#pragma once
// Collection tabs: the categories and the stash geometry behind them.
//
// The player asked for one extra bar of stash tabs - Uniques, Sets, Runewords -
// where every collected item owns one slot on our own panel, missing entries
// show as a greyed outline and the stored copy can be taken out or swapped for a
// better roll. This header holds only constants: no game calls, no plugin
// services, no drawing, no I/O. collection_view.h adds the loaded catalog and
// collection_panel.h the on-screen layout; both build on this.
//
// The items live in the shared stash - a separate .d2i file, shared by the
// account - so nothing here touches a character save.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace framevr::collection {

// The categories the player asked for. The order is the tab order and must stay
// stable once a stash is in use: the value selects the page group.
enum class Category : uint32_t {
    Uniques   = 0,
    Sets      = 1,
    Runewords = 2,
    Materials = 3,   // gems and runes: stackable, so one slot per kind
    Count     = 4,
};

inline constexpr std::size_t CategoryCount = static_cast<std::size_t>(Category::Count);

// Tab labels, UTF-8 for the painter (the mod draws UTF-8 everywhere).
inline constexpr const char* CategoryName(Category category) {
    switch (category) {
    case Category::Uniques:   return "Uniques";
    case Category::Sets:      return "Sets";
    case Category::Runewords: return "Runewords";
    case Category::Materials: return "Materials";
    default:                  return "?";
    }
}

// The tag a catalog line carries for this category (collection.tsv's first
// column). It is how a loaded line finds its category, so it must not change.
inline constexpr const char* CategoryTag(Category category) {
    switch (category) {
    case Category::Uniques:   return "uniques";
    case Category::Sets:      return "sets";
    case Category::Runewords: return "runewords";
    case Category::Materials: return "materials";
    default:                  return "";
    }
}

// Parses a tag back to its category; false when it names none.
inline constexpr bool CategoryFromTag(std::string_view tag, Category& out) {
    for (std::size_t i = 0; i < CategoryCount; ++i) {
        const Category     category  = static_cast<Category>(i);
        const std::string_view candidate = CategoryTag(category);
        if (candidate.size() != tag.size()) continue;
        bool same = true;
        for (std::size_t c = 0; c < tag.size(); ++c)
            if (tag[c] != candidate[c]) { same = false; break; }
        if (same) { out = category; return true; }
    }
    return false;
}

// One shared stash tab is a 10 x 10 grid of cells; an item takes its own
// footprint inside it, so the storage layer packs found items rather than
// giving each catalog entry a cell. The panel's slots are ours and do not
// depend on this.
inline constexpr uint32_t StashTabColumns = 10;
inline constexpr uint32_t StashTabRows    = 10;
inline constexpr uint32_t StashTabCells   = StashTabColumns * StashTabRows;

// The quality a category stores. Runewords are ordinary Normal items whose
// identity comes from the Runes.txt recipe, so the storage layer matches them by
// their rune recipe and base type instead of Items::Quality.
inline constexpr bool StoresNamedQuality(Category category) {
    return category == Category::Uniques || category == Category::Sets;
}

}   // namespace framevr::collection
