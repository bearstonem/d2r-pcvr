// Collection tabs, step 3: affinity. See collection_store.h.
#include "collection_store.h"

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <vector>

#include "../steamvr/collection_view.h"
#include "collection_model.h"
#include "collection_tab.h"
#include "logging.h"

namespace framevr::collection {
namespace {

const D2RL::PluginContext*    g_ctx       = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ItemService*      g_items     = nullptr;
const D2RL::ThreadService*    g_threads   = nullptr;

std::atomic<bool>     g_affinity{false};
std::atomic<int>      g_firstTab{5};
std::atomic<int>      g_tabs{24};
std::atomic<uint64_t> g_lastSweep{0};
std::atomic<bool>     g_sweeping{false};
std::atomic<int>      g_movedTotal{0};

// Where each category's items live inside the collection's tab range. The counts
// add up to the 24 tabs the mod asks D2RLoader for.
struct Band {
    Category category;
    int      first;
    int      count;
};
constexpr Band  kBands[] = {
    {Category::Uniques,   0, 12},
    {Category::Sets,     12,  5},
    {Category::Runewords, 17, 3},
    {Category::Materials, 20, 4},
};
constexpr int   kBandTabs = 24;
constexpr char  kLog[96]  = "Frame collection: ";

void Log(const std::string& text) { framevr::logging::Write(text.c_str()); }

const Band* BandOf(Category category) {
    for (const Band& band : kBands)
        if (band.category == category) return &band;
    return nullptr;
}

// One shared-stash item as the sweep sees it.
struct Seen {
    D2RL::ItemHandle handle   = 0;
    uint32_t         code     = 0;
    uint32_t         recordId = UINT32_MAX;
    uint32_t         page     = UINT32_MAX;
    bool             unique   = false;
    bool             set      = false;
};

struct Sweep {
    std::vector<Seen> items;
    bool              ok = false;
};

D2RL::Inventory::IterationAction __cdecl Collect(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* userData) noexcept {
    auto& sweep = *static_cast<Sweep*>(userData);
    if (info == nullptr) return D2RL::Inventory::IterationAction::Continue;
    Seen seen;
    seen.handle = info->handle;
    seen.code   = info->code;
    seen.page   = info->sharedStashPage;
    seen.unique = info->quality == D2RL::Items::Quality::Unique;
    seen.set    = info->quality == D2RL::Items::Quality::Set;
    if (info->qualityRecordId >= 0 && (seen.unique || seen.set)) seen.recordId = uint32_t(info->qualityRecordId);
    sweep.items.push_back(seen);
    return D2RL::Inventory::IterationAction::Continue;
}

// Moves one item to the first cell of the category's first tab that takes it.
bool MoveToBand(const D2RL::PlayerHandle player, const D2RL::ItemHandle item, Category category) {
    const Band* band = BandOf(category);
    if (band == nullptr || g_items == nullptr) return false;
    for (int tab = 0; tab < band->count; ++tab) {
        D2RL::Items::ExistingItemOperation operation {};
        operation.structSize         = D2RL::Items::ExistingItemOperationSize;
        operation.kind               = D2RL::Items::ExistingItemOperationKind::Move;
        operation.item               = item;
        operation.move.destination.structSize     = D2RL::Items::ItemDestinationSize;
        operation.move.destination.container      = D2RL::Items::ItemContainer::SharedStash;
        operation.move.destination.placement      = D2RL::Items::Placement::Automatic;
        operation.move.destination.sharedStashPage = uint32_t(g_firstTab.load() + band->first + tab);

        D2RL::Items::ExistingItemTransaction transaction {};
        transaction.structSize     = D2RL::Items::ExistingItemTransactionSize;
        transaction.player         = player;
        transaction.operationCount = 1;
        transaction.operations     = &operation;
        D2RL::Items::ExistingItemTransactionResult result {};
        result.structSize = D2RL::Items::ExistingItemTransactionResultSize;
        if (g_items->executeExistingItemTransaction(g_ctx, &transaction, &result) == D2RL::Items::Result::Success) return true;
    }
    return false;
}

// Runs on the game thread: what the shared stash holds, and what of it is the
// collection's business.
void SweepOnGameThread(const D2RL::PluginContext*, void*) noexcept {
    if (g_ctx == nullptr || g_inventory == nullptr || g_items == nullptr) {
        g_sweeping.store(false);
        return;
    }
    D2RL::PlayerHandle player {};
    if (g_inventory->getLocalPlayer(g_ctx, &player) != D2RL::Inventory::Result::Success) {
        g_sweeping.store(false);
        return;
    }

    Sweep sweep;
    D2RL::Inventory::ItemFilter filter {};
    filter.structSize    = D2RL::Inventory::ItemFilterSize;
    filter.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::SharedStash);
    const auto read = g_inventory->forEachInventoryItem(g_ctx, player, &filter, &Collect, &sweep);
    if (read != D2RL::Inventory::Result::Success) {
        g_sweeping.store(false);
        return;
    }

    const int      tabs   = std::min(g_tabs.load(), kBandTabs);
    const uint32_t first  = uint32_t(g_firstTab.load());
    const uint32_t beyond = first + uint32_t(std::max(0, tabs));

    // What the collection's own tabs already hold, and what is still loose in
    // the player's own tabs.
    const Library& library = TabLibrary();
    std::vector<uint32_t> heldUnique, heldSet, heldMaterial;
    struct Loose {
        Seen     seen;
        Category category;
        uint32_t identity;
        bool     byCode;
    };
    std::vector<Loose> loose;
    for (const Seen& item : sweep.items) {
        const bool ours = item.page >= first && item.page < beyond;
        if (item.unique && item.recordId != UINT32_MAX && library.SlotOf(Category::Uniques, item.recordId) != UINT32_MAX) {
            if (ours) { heldUnique.push_back(item.recordId); continue; }
            loose.push_back({item, Category::Uniques, item.recordId, false});
        } else if (item.set && item.recordId != UINT32_MAX && library.SlotOf(Category::Sets, item.recordId) != UINT32_MAX) {
            if (ours) { heldSet.push_back(item.recordId); continue; }
            loose.push_back({item, Category::Sets, item.recordId, false});
        } else if (library.SlotOfCode(Category::Materials, item.code) != UINT32_MAX) {
            if (ours) { heldMaterial.push_back(item.code); continue; }
            loose.push_back({item, Category::Materials, item.code, true});
        }
    }

    int moved = 0;
    for (const Loose& item : loose) {
        // One copy per entry: the collection keeps the first one it was given.
        std::vector<uint32_t>& held = item.category == Category::Uniques ? heldUnique
                                    : item.category == Category::Sets    ? heldSet
                                                                         : heldMaterial;
        if (std::find(held.begin(), held.end(), item.identity) != held.end()) continue;
        if (!MoveToBand(player, item.seen.handle, item.category)) continue;
        held.push_back(item.identity);
        ++moved;
        // A unique or set brings itself into view; a material does not.
        if (item.category == Category::Uniques || item.category == Category::Sets)
            TabShowEntry(item.category, item.identity);
    }

    if (moved > 0) {
        g_movedTotal.fetch_add(moved);
        char line[192];
        std::snprintf(line, sizeof(line), "Frame collection: affinity moved %d item%s to the collection's tabs (from tab %u)",
                      moved, moved == 1 ? "" : "s", unsigned(first));
        Log(line);
    }
    g_sweeping.store(false);
}

}   // namespace

void InitStore(const D2RL::PluginContext* ctx, bool affinity, int firstTab, int tabs) {
    g_ctx = ctx;
    g_affinity.store(affinity);
    g_firstTab.store(std::max(0, firstTab));
    g_tabs.store(std::clamp(tabs, 0, kBandTabs));
    if (ctx == nullptr) return;

    if (ctx->QueryService(&g_inventory) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize)) {
        g_inventory = nullptr;
    }
    if (ctx->QueryService(&g_items) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasItemServiceField(g_items, D2RL::ItemServiceRequiredSize)) {
        g_items = nullptr;
    }
    if (ctx->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize)) {
        g_threads = nullptr;
    }

    const bool canWrite = g_items != nullptr
        && D2RL::HasItemServiceCapability(g_items, D2RL::Items::ItemServiceCapability::SharedStashWrite);
    if (affinity && !canWrite) {
        g_affinity.store(false);
        Log("Frame collection: affinity is on but this loader build cannot write the shared stash; affinity stays off");
    }
    if (affinity) {
        char line[192];
        std::snprintf(line, sizeof(line), "Frame collection: affinity is on; the collection owns shared tabs %d..%d",
                      firstTab, firstTab + std::max(0, tabs) - 1);
        Log(line);
    }
}

void ShutdownStore() {
    g_ctx       = nullptr;
    g_inventory = nullptr;
    g_items     = nullptr;
    g_threads   = nullptr;
    g_sweeping.store(false);
}

void TickStore(bool stashOpen) {
    if (!g_affinity.load() || !stashOpen || g_sweeping.load()) return;
    const uint64_t now = GetTickCount64();
    const uint64_t last = g_lastSweep.load();
    if (now - last < 500) return;
    g_lastSweep.store(now);
    g_sweeping.store(true);
    if (g_threads != nullptr && g_threads->runOnGameThread != nullptr
        && g_threads->runOnGameThread(g_ctx, &SweepOnGameThread, nullptr) == D2RL::Threads::Result::Success) {
        return;
    }
    // No thread service: run where we are, which is what the sweep needs anyway.
    SweepOnGameThread(g_ctx, nullptr);
}

void StoreReport() {
    char line[192];
    std::snprintf(line, sizeof(line), "Frame collection: affinity %s, collection tabs %d..%d, %d moved so far",
                  g_affinity.load() ? "on" : "off", g_firstTab.load(),
                  g_firstTab.load() + g_tabs.load() - 1, g_movedTotal.load());
    Log(line);
}

}   // namespace framevr::collection
