// Collection tabs: the catalog model - category table, page geometry, slot
// placement and lookup. Pure arithmetic, so this runs without D2R.
#include "collection_model.h"
#include <cassert>
#include <cstdio>
#include <string_view>

using namespace framevr::collection;

namespace {

// Two uniques, one set, one runeword, so the per-category offsets are exercised.
constexpr uint32_t Code(char a, char b = ' ', char c = ' ', char d = ' ') {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8U)
         | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16U) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24U);
}

constexpr CatalogEntry kCatalog[] = {
    {Code('u', 'i', 'd', '1'), 0, 1001, 5},   // unique slot 0
    {Code('u', 'i', 'd', '2'), 1, 1002, 9},   // unique slot 1
    {Code('s', 'i', 'd', '1'), 0, 2001, 3},   // set slot 0
    {Code('r', 'w', 'd', '1'), 0, 3001, 1},   // runeword slot 0
};
constexpr uint32_t kOffsets[] = {0, 2, 3, 4};   // CategoryCount + 1
constexpr Catalog kView {kCatalog, kOffsets, 4};

static_assert(CategoryCount == 3);
static_assert(CellsPerPage == 256);
static_assert(PagesFor(0) == 1);
static_assert(PagesFor(1) == 1);
static_assert(PagesFor(256) == 1);
static_assert(PagesFor(257) == 2);
static_assert(PagesFor(439) == 2);   // uniques on 3.4.0
static_assert(PagesFor(141) == 1);   // sets
static_assert(PagesFor(99) == 1);    // completed runewords
static_assert(StoresNamedQuality(Category::Uniques));
static_assert(StoresNamedQuality(Category::Sets));
static_assert(!StoresNamedQuality(Category::Runewords));

}   // namespace

int main() {
    // Labels and logical ids: present, distinct, stable strings.
    for (std::size_t i = 0; i < CategoryCount; ++i) {
        const auto category = static_cast<Category>(i);
        assert(CategoryLabel(category) != nullptr && CategoryLabel(category)[0] != L'?');
        assert(PageLogicalId(category) != nullptr);
        for (std::size_t j = i + 1; j < CategoryCount; ++j)
            assert(std::string_view(PageLogicalId(category)) != std::string_view(PageLogicalId(static_cast<Category>(j))));
    }

    // A slot owns exactly one cell, and the cell is stable.
    const auto first = Locate(0);
    assert(first.page == 0 && first.cell == 0 && first.x == 0 && first.y == 0);
    const auto lastOfPage = Locate(255);
    assert(lastOfPage.page == 0 && lastOfPage.cell == 255 && lastOfPage.x == 15 && lastOfPage.y == 15);
    const auto next = Locate(256);
    assert(next.page == 1 && next.cell == 0 && next.x == 0 && next.y == 0);
    const auto nextOne = Locate(257);
    assert(nextOne.page == 1 && nextOne.cell == 1 && nextOne.x == 1 && nextOne.y == 0);
    // Every slot up to the largest category lands inside its page.
    for (uint32_t slot = 0; slot < 439; ++slot) {
        const auto at = Locate(slot);
        assert(at.page == slot / CellsPerPage);
        assert(at.x < PageWidth && at.y < PageHeight);
        assert(at.cell == at.y * PageWidth + at.x);
    }

    // Lookup: per-category offsets, exact record match, base-code match, misses.
    assert(kView.Valid());
    assert(kView.Size(Category::Uniques) == 2);
    assert(kView.Size(Category::Sets) == 1);
    assert(kView.Size(Category::Runewords) == 1);
    assert(kView.SlotOf(Category::Uniques, Code('u', 'i', 'd', '2'), 1) == 1);
    assert(kView.SlotOf(Category::Uniques, Code('u', 'i', 'd', '1'), UINT32_MAX) == 0);
    assert(kView.SlotOf(Category::Uniques, Code('u', 'i', 'd', '2'), 0) == UINT32_MAX);   // wrong record
    assert(kView.SlotOf(Category::Sets, Code('u', 'i', 'd', '1'), UINT32_MAX) == UINT32_MAX);   // wrong category
    assert(kView.SlotOf(Category::Runewords, Code('r', 'w', 'd', '1'), UINT32_MAX) == 0);
    assert(kView.Entry(Category::Sets, 0) != nullptr && kView.Entry(Category::Sets, 0)->nameId == 2001);
    assert(kView.Entry(Category::Sets, 1) == nullptr);

    // Progress counts what the catalog declares.
    Progress progress;
    progress.Reset(kView);
    assert(progress.total[0] == 2 && progress.total[1] == 1 && progress.total[2] == 1);
    assert(progress.found[0] == 0 && progress.found[1] == 0 && progress.found[2] == 0);

    std::puts("Collection model: categories, page geometry, slot placement, lookup and progress passed");
}
