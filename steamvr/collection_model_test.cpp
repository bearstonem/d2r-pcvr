// Collection model: the categories and the stash geometry. Pure constants, so
// this runs without D2R.
#include "collection_model.h"
#include <cassert>
#include <cstdio>
#include <string_view>

using namespace framevr::collection;

int main() {
    static_assert(CategoryCount == 3);
    static_assert(StashTabCells == 100);
    static_assert(StoresNamedQuality(Category::Uniques));
    static_assert(StoresNamedQuality(Category::Sets));
    static_assert(!StoresNamedQuality(Category::Runewords));

    // Every category has a name, a tag, and both are distinct.
    for (std::size_t i = 0; i < CategoryCount; ++i) {
        const auto category = static_cast<Category>(i);
        assert(CategoryName(category) != nullptr && CategoryName(category)[0] != '?');
        assert(std::string_view(CategoryTag(category)).size() > 0);
        for (std::size_t j = i + 1; j < CategoryCount; ++j) {
            assert(std::string_view(CategoryTag(category)) != std::string_view(CategoryTag(static_cast<Category>(j))));
            assert(std::string_view(CategoryName(category)) != std::string_view(CategoryName(static_cast<Category>(j))));
        }
    }

    // A tag round-trips, and an unknown one is refused.
    for (std::size_t i = 0; i < CategoryCount; ++i) {
        const auto expected = static_cast<Category>(i);
        Category   parsed   = Category::Count;
        assert(CategoryFromTag(CategoryTag(expected), parsed));
        assert(parsed == expected);
    }
    Category none = Category::Uniques;
    assert(!CategoryFromTag("runes", none));
    assert(!CategoryFromTag("", none));
    assert(!CategoryFromTag("unique", none));       // not a prefix match
    assert(!CategoryFromTag("uniques ", none));     // not a padded match

    std::puts("Collection model: categories, labels, tags and stash geometry passed");
}
