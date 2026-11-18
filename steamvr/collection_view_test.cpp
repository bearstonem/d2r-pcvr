// Collection view: the loaded catalog, the found flags and the panel's page
// state. Pure data, so this runs without D2R.
#include "collection_view.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace framevr::collection;

namespace {

std::string Line(const char* category, const char* code, int recordId, const char* name, int level, const char* type, const char* extra = "") {
    return std::string(category) + "\t" + code + "\t" + std::to_string(recordId) + "\t" + name + "\t"
         + std::to_string(level) + "\t" + type + "\t" + extra;
}

// 100 uniques (two panel pages), 2 sets, 1 runeword.
Library Build() {
    Library library;
    assert(library.LoadLine("# collection 1"));
    assert(library.LoadLine("# category\tcode\trecordId\tname\tlevel\ttype\textra"));
    assert(library.LoadLine(""));
    for (int i = 0; i < 100; ++i)
        assert(library.LoadLine(Line("uniques", "rin", 1000 + i, ("Unique " + std::to_string(i)).c_str(), i % 90, "ring")));
    assert(library.LoadLine(Line("sets", "lrg", 56, "Civerb's Ward", 3, "shie", "Civerb's Vestments")));
    assert(library.LoadLine(Line("sets", "amu", 57, "Civerb's Icon", 3, "amul", "Civerb's Vestments")));
    assert(library.LoadLine(Line("runewords", "", 3, "Beast", 0, "axe", "r30 r03 r22 r23 r17")));
    library.Finish();
    return library;
}

}   // namespace

int main() {
    Library library = Build();
    assert(library.Total() == 103);
    assert(library.Size(Category::Uniques) == 100);
    assert(library.Size(Category::Sets) == 2);
    assert(library.Size(Category::Runewords) == 1);

    // The file's order inside a category is the slot; fields are parsed as written.
    const Entry* first = library.At(Category::Uniques, 0);
    assert(first != nullptr && first->name == "Unique 0" && first->recordId == 1000 && first->type == "ring");
    assert(first->code == Library::CodeOf("rin"));
    assert(library.At(Category::Uniques, 100) == nullptr);
    const Entry* set = library.At(Category::Sets, 0);
    assert(set != nullptr && set->extra == "Civerb's Vestments");
    assert(library.At(Category::Runewords, 0)->extra == "r30 r03 r22 r23 r17");

    // A malformed data line is refused and adds nothing.
    Library broken;
    assert(!broken.LoadLine("uniques\thax\t0\tOnly five fields\t7"));
    assert(broken.LoadLine("# a comment is fine"));
    assert(broken.LoadLine(""));
    broken.Finish();
    assert(broken.Total() == 0 && broken.Size(Category::Uniques) == 0);
    assert(!broken.LoadLine("runes\tx\t1\tUnknown category\t1\tgem"));   // unknown tag
    assert(broken.Total() == 0);

    // Code packing matches Items::MakeItemCode: first byte low, spaces pad.
    assert(Library::CodeOf("hax") == (uint32_t('h') | (uint32_t('a') << 8) | (uint32_t('x') << 16) | (uint32_t(' ') << 24)));
    assert(Library::CodeOf("") == (uint32_t(' ') | (uint32_t(' ') << 8) | (uint32_t(' ') << 16) | (uint32_t(' ') << 24)));

    // Found flags follow the record id, not the slot.
    assert(library.Found(Category::Uniques) == 0);
    assert(library.MarkFound(Category::Uniques, 1042) == 42);
    assert(library.MarkFound(Category::Sets, 57) == 1);
    assert(library.MarkFound(Category::Uniques, 999999) == UINT32_MAX);
    assert(library.IsFound(Category::Uniques, 42));
    assert(!library.IsFound(Category::Uniques, 41));
    assert(library.Found(Category::Uniques) == 1 && library.Found(Category::Sets) == 1);
    library.ClearFound();
    assert(library.Found(Category::Uniques) == 0 && !library.IsFound(Category::Sets, 1));

    // The panel's page state: two pages of uniques (96 slots a page), and the
    // category switch starts at the first page.
    ViewState view;
    assert(view.Pages(library) == 2);          // 100 entries, 96 a page
    assert(view.Pages(library) == panel::PagesFor(100));
    assert(panel::PagesFor(403) == 5);         // the uniques on 3.4.0
    view.page = 99;
    view.Clamp(library);
    assert(view.page == 1);
    view.Step(library, -10);
    assert(view.page == 0);
    view.Step(library, -1);
    assert(view.page == 0);                    // never below the first page
    view.SelectCategory(library, Category::Runewords);
    assert(view.category == Category::Runewords && view.page == 0 && view.hovered == -1);
    assert(view.Pages(library) == 1);
    view.Step(library, 1);
    assert(view.page == 0);                    // one page: nothing to step to

    // Slots map to entries across pages, and stop at the end of the category.
    view.SelectCategory(library, Category::Uniques);
    {
        const auto visible = VisibleSlots(library, view);
        assert(visible.size() == 100);
        assert(SlotEntry(library, view, visible, 0)->recordId == 1000);
        assert(SlotEntry(library, view, visible, 95)->recordId == 1095);
        assert(SlotEntry(library, view, visible, 96) == nullptr);   // the second page, not loaded here
        view.page = 1;
        assert(SlotEntry(library, view, visible, 0)->recordId == 1096);
        assert(SlotEntry(library, view, visible, 3)->recordId == 1099);
        assert(SlotEntry(library, view, visible, 4) == nullptr);    // the 100th unique is the last
        assert(SlotEntry(library, view, visible, -1) == nullptr);
        assert(SlotEntry(library, view, visible, panel::SlotsFor(view.size)) == nullptr);
    }

    // Found state is per slot, and the counter reads the open category.
    view.page = 0;
    library.MarkFound(Category::Uniques, 1042);
    {
        const auto visible = VisibleSlots(library, view);
        assert(!SlotFound(library, view, visible, 41) && SlotFound(library, view, visible, 42));
    }
    assert(CounterText(library, view) == "1 / 100");
    view.SelectCategory(library, Category::Sets);
    library.MarkFound(Category::Sets, 56);
    assert(CounterText(library, view) == "1 / 2");
    {
        const auto visible = VisibleSlots(library, view);
        assert(SlotFound(library, view, visible, 0) && !SlotFound(library, view, visible, 1));
    }

    // The filter: Missing keeps what the stash does not hold, Found what it does,
    // and the pages follow the kept count.
    view.SelectCategory(library, Category::Uniques);
    assert(view.VisibleCount(library) == 100);
    view.SelectShow(library, panel::Show::Found);
    assert(view.show == panel::Show::Found && view.page == 0);
    assert(view.VisibleCount(library) == 1);
    assert(view.Pages(library) == 1);
    {
        const auto visible = VisibleSlots(library, view);
        assert(visible.size() == 1);
        assert(SlotEntry(library, view, visible, 0)->recordId == 1042);   // the one that was marked
        assert(SlotFound(library, view, visible, 0));
    }
    view.SelectShow(library, panel::Show::Missing);
    assert(view.VisibleCount(library) == 99);
    assert(view.Pages(library) == 2);
    {
        const auto visible = VisibleSlots(library, view);
        assert(visible.size() == 99);
        assert(SlotEntry(library, view, visible, 0)->recordId == 1000);
        assert(SlotEntry(library, view, visible, 41)->recordId == 1041);   // 1042 is skipped
        assert(SlotEntry(library, view, visible, 42)->recordId == 1043);
    }
    // A filter that keeps nothing still shows one page, and no slots.
    view.SelectShow(library, panel::Show::Found);
    view.SelectCategory(library, Category::Runewords);
    assert(view.VisibleCount(library) == 0 && view.Pages(library) == 1);
    assert(SlotEntry(library, view, VisibleSlots(library, view), 0) == nullptr);
    view.SelectShow(library, panel::Show::All);
    assert(view.VisibleCount(library) == 1);

    std::puts("Collection view: loading, grouping, found flags, paging, the filter and the counter passed");
}
