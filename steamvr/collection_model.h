#pragma once
// Collection tabs: the catalog model behind the extra stash pages.
//
// The player asked for one extra bar of stash tabs - Uniques, Sets, Runewords -
// where every collected item owns one fixed cell, missing entries show as a
// greyed outline and the stored copy can be taken out or swapped for a better
// roll. This header holds only the arithmetic and the tables that layout needs:
// no game calls, no plugin services, no drawing. Everything here is unit tested
// by collection_model_test.cpp without D2R running.
//
// A page is a D2RLoader custom player page. D2S stores each item coordinate in
// four bits, so one page is at most 16 x 16 cells (D2RLPlugin/inventory.h).
// A slot is the catalog entry's index inside its category; the cell it owns is
// derived from that index, which keeps the mapping stable as long as the
// category's entry order is stable (prepare_collection.py writes that order).
#include <cstddef>
#include <cstdint>

namespace framevr::collection {

// The categories the player asked for. The order is the tab order and must stay
// stable once pages are saved: the enum value selects the page group.
enum class Category : uint32_t {
    Uniques   = 0,
    Sets      = 1,
    Runewords = 2,
    Count     = 3,
};

inline constexpr std::size_t CategoryCount = static_cast<std::size_t>(Category::Count);

// Tab labels. The VR layer draws these; keep them short (the tab bar shares the
// stash's 16:9 band).
inline constexpr const wchar_t* CategoryLabel(Category category) {
    switch (category) {
    case Category::Uniques:   return L"Uniques";
    case Category::Sets:      return L"Sets";
    case Category::Runewords: return L"Runewords";
    default:                  return L"?";
    }
}

// The logical id a custom page is registered under (inventory.h
// PlayerPageRegistration::logicalId). It identifies the page inside a save, so
// it must never change between releases, and it must differ per category.
inline constexpr const char* PageLogicalId(Category category) {
    switch (category) {
    case Category::Uniques:   return "collection-uniques";
    case Category::Sets:      return "collection-sets";
    case Category::Runewords: return "collection-runewords";
    default:                  return "collection-unknown";
    }
}

inline constexpr uint32_t PageWidth    = 16;   // four-bit D2S coordinates
inline constexpr uint32_t PageHeight   = 16;
inline constexpr uint32_t CellsPerPage = PageWidth * PageHeight;

// Pages one category needs for entryCount entries, at least one.
inline constexpr uint32_t PagesFor(uint32_t entryCount) {
    return entryCount == 0 ? 1 : (entryCount + CellsPerPage - 1) / CellsPerPage;
}

// Where a catalog slot lives: the page index inside the category (0-based, added
// to the category's first page handle) and the cell inside that page.
struct SlotLocation {
    uint32_t page;
    uint32_t cell;
    uint32_t x;
    uint32_t y;
};

inline constexpr SlotLocation Locate(uint32_t slotIndex) {
    const uint32_t cell = slotIndex % CellsPerPage;
    return {slotIndex / CellsPerPage, cell, cell % PageWidth, cell / PageWidth};
}

// One catalog entry, as prepare_collection.py writes it. code is the four-byte
// D2 item code of the base item (padded with spaces, matching
// Items::MakeItemCode); recordId is the zero-based UniqueItems or SetItems row
// for named qualities, and the Runes.txt row for a runeword. nameId is the
// *ID string id the game localizes the name from. level is the item's lvl.
struct CatalogEntry {
    uint32_t code;
    uint32_t recordId;
    uint32_t nameId;
    uint32_t level;
};

// A read-only view of the generated catalog: entries sorted by category, plus
// the start and count of each category.
struct Catalog {
    const CatalogEntry* entries = nullptr;
    const uint32_t*     offsets = nullptr;   // CategoryCount + 1 entries
    uint32_t            count   = 0;

    constexpr bool Valid() const { return entries != nullptr && offsets != nullptr; }

    constexpr uint32_t Start(Category category) const { return offsets ? offsets[static_cast<uint32_t>(category)] : 0; }
    constexpr uint32_t End(Category category) const { return offsets ? offsets[static_cast<uint32_t>(category) + 1] : 0; }
    constexpr uint32_t Size(Category category) const { return End(category) - Start(category); }

    // The slot index inside the category, or UINT32_MAX when the item is not in
    // the catalog. recordId is matched only when the entry carries one, so a
    // base-code lookup can pass UINT32_MAX to match any record.
    constexpr uint32_t SlotOf(Category category, uint32_t itemCode, uint32_t recordId) const {
        for (uint32_t i = Start(category); i < End(category); ++i) {
            if (entries[i].code != itemCode) continue;
            if (recordId != UINT32_MAX && entries[i].recordId != recordId) continue;
            return i - Start(category);
        }
        return UINT32_MAX;
    }

    constexpr const CatalogEntry* Entry(Category category, uint32_t slot) const {
        return slot < Size(category) ? &entries[Start(category) + slot] : nullptr;
    }
};

// Progress per category, filled by the scan and later by the storage layer.
struct Progress {
    uint32_t total[CategoryCount] {};
    uint32_t found[CategoryCount] {};

    constexpr void Reset(const Catalog& catalog) {
        for (std::size_t i = 0; i < CategoryCount; ++i) {
            total[i] = catalog.Size(static_cast<Category>(i));
            found[i] = 0;
        }
    }
};

// The quality a category stores. Runewords are ordinary Normal items whose
// identity comes from the Runes.txt recipe, so they are matched by rune count
// and base type instead (storage layer), not by Items::Quality.
inline constexpr bool StoresNamedQuality(Category category) {
    return category == Category::Uniques || category == Category::Sets;
}

}   // namespace framevr::collection
