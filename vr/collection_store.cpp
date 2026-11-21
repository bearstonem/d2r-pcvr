// Collection tabs, step 3: affinity. See collection_store.h.
#include "collection_store.h"

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "../steamvr/collection_view.h"
#include "../steamvr/stash_slots.h"
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
std::atomic<bool>     g_depositing{false};   // a click on the page with an item in hand

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
    bool             identified = true;   // an unidentified item never enters the collection (owner 2026-11-20)
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
    seen.identified = (info->stateFlags & D2RL::Items::ItemStateIdentified) != 0;
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

// Moves one item onto the player's cursor: a stored copy taken out lands in the hand,
// like a native pick-up, instead of in the first free inventory cell.
bool MoveToCursor(const D2RL::PlayerHandle player, const D2RL::ItemHandle item) {
    if (g_items == nullptr) return false;
    D2RL::Items::ExistingItemOperation operation {};
    operation.structSize = D2RL::Items::ExistingItemOperationSize;
    operation.kind       = D2RL::Items::ExistingItemOperationKind::Move;
    operation.item       = item;
    operation.move.destination.structSize = D2RL::Items::ItemDestinationSize;
    operation.move.destination.container  = D2RL::Items::ItemContainer::Cursor;
    operation.move.destination.placement  = D2RL::Items::Placement::Automatic;

    D2RL::Items::ExistingItemTransaction transaction {};
    transaction.structSize     = D2RL::Items::ExistingItemTransactionSize;
    transaction.player         = player;
    transaction.operationCount = 1;
    transaction.operations     = &operation;
    D2RL::Items::ExistingItemTransactionResult result {};
    result.structSize = D2RL::Items::ExistingItemTransactionResultSize;
    return g_items->executeExistingItemTransaction(g_ctx, &transaction, &result) == D2RL::Items::Result::Success;
}

// The item the game holds on its cursor now: the store-carried wrapper (getCursorItem).
struct CursorItem {
    uint64_t handle   = 0;
    uint32_t code     = 0;
    uint32_t recordId = UINT32_MAX;
    uint32_t quality  = UINT32_MAX;
    bool     unique   = false;
    bool     set      = false;
    bool     identified = true;
    bool     ok       = false;
};

D2RL::Inventory::IterationAction __cdecl CollectCursor(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* userData) noexcept {
    auto& out = *static_cast<CursorItem*>(userData);
    if (info == nullptr || out.ok) return D2RL::Inventory::IterationAction::Continue;
    out.handle = info->handle;
    out.code   = info->code;
    out.quality = uint32_t(info->quality);
    out.unique = info->quality == D2RL::Items::Quality::Unique;
    out.set    = info->quality == D2RL::Items::Quality::Set;
    out.identified = (info->stateFlags & D2RL::Items::ItemStateIdentified) != 0;
    if (info->qualityRecordId >= 0 && (out.unique || out.set)) out.recordId = uint32_t(info->qualityRecordId);
    out.ok = true;
    return D2RL::Inventory::IterationAction::Continue;
}

// What the hand holds, for the page to draw under the laser (CarriedIconKey): read on the
// game thread a few times a second while the stash is open.
std::mutex            g_carriedLock;
std::string           g_carriedKey;
std::atomic<bool>     g_carriedPolling{false};
std::atomic<uint64_t> g_carriedAt{0};
std::atomic<uint32_t> g_carriedCode{0};   // the hand's item base code (CodeKey packing), 0 for none
void PollCarriedOnGameThread(const D2RL::PluginContext*, void*) noexcept {
    std::string key;
    uint32_t code = 0;
    D2RL::PlayerHandle player {};
    if (g_ctx != nullptr && g_inventory != nullptr &&
        g_inventory->getLocalPlayer(g_ctx, &player) == D2RL::Inventory::Result::Success) {
        CursorItem cursor;
        D2RL::Inventory::ItemFilter filter {};
        filter.structSize    = D2RL::Inventory::ItemFilterSize;
        filter.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Cursor);
        if (g_inventory->forEachInventoryItem(g_ctx, player, &filter, &CollectCursor, &cursor) == D2RL::Inventory::Result::Success && cursor.ok) {
            code = cursor.code;
            if (cursor.unique && cursor.recordId != UINT32_MAX)   key = panel::IconKey(Category::Uniques, cursor.recordId, 0);
            else if (cursor.set && cursor.recordId != UINT32_MAX) key = panel::IconKey(Category::Sets, cursor.recordId, 0);
            else                                                  key = panel::IconKey(Category::Materials, 0, cursor.code);
        }
    }
    {
        std::lock_guard lock(g_carriedLock);
        if (key != g_carriedKey) {
            char line[128];
            std::snprintf(line, sizeof(line), "Frame collection: the hand now holds %s", key.empty() ? "nothing" : key.c_str());
            Log(line);
        }
        g_carriedKey = std::move(key);
    }
    g_carriedCode.store(code);
    g_carriedPolling.store(false);
}

bool MoveToInventory(const D2RL::PlayerHandle player, const D2RL::ItemHandle item);   // defined below

// ---- The hand's way in and out -------------------------------------------------------
// D2RLoader moves no item to or from the game's cursor in the shared stash (the log: "could not
// take the cursor item into the inventory"). The game itself does: one click of its own on an
// inventory cell puts the held item down there or picks the item there up. So the page passes an
// item through a free inventory spot - in: the hand's item is clicked down into the inventory and
// moved from there into the collection; out: the stored copy is moved into the inventory and
// clicked up into the hand. The frame thread makes the click (TakeInventoryClick); the moves run
// here, on the game thread. Owner 2026-11-20: only the hand moves items, never a card click.

// An item's size in inventory cells, by its base code: armor.txt, weapons.txt and misc.txt from
// the tables D2RLoader extracts beside the game (<game>\d2rloader\data\compiler\<version>).
std::map<uint32_t, std::pair<int, int>> g_sizes;
bool g_sizesRead = false;

uint32_t CodeKey(const std::string& code) {
    uint32_t key = 0;
    for (int i = 0; i < 4; ++i) {
        const unsigned char c = i < int(code.size()) ? static_cast<unsigned char>(code[size_t(i)]) : ' ';
        key |= uint32_t(c) << (8 * i);
    }
    return key;
}

void ReadSizes() {
    if (g_sizesRead) return;
    g_sizesRead = true;
    wchar_t exe[MAX_PATH] {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return;
    std::error_code error;
    const std::filesystem::path compiler = std::filesystem::path(exe).parent_path() / L"d2rloader" / L"data" / L"compiler";
    std::filesystem::path newest, excel;
    for (const auto& version : std::filesystem::directory_iterator(compiler, error)) {
        const auto candidate = version.path() / L"data" / L"global" / L"excel";
        if (std::filesystem::exists(candidate / L"armor.txt", error) && (newest.empty() || version.path() > newest)) {
            newest = version.path();
            excel  = candidate;
        }
    }
    if (excel.empty()) { Log("Frame collection: no extracted item tables beside the game; item sizes unknown (2 x 4 assumed)"); return; }
    for (const wchar_t* name : {L"armor.txt", L"weapons.txt", L"misc.txt"}) {
        std::ifstream file(excel / name);
        std::string line;
        if (!std::getline(file, line)) continue;
        std::vector<std::string> header;
        { std::stringstream cells(line); std::string cell; while (std::getline(cells, cell, '\t')) header.push_back(cell); }
        const auto column = [&](const char* wanted) {
            for (size_t i = 0; i < header.size(); ++i) if (header[i] == wanted) return int(i);
            return -1;
        };
        const int code = column("code"), width = column("invwidth"), height = column("invheight");
        if (code < 0 || width < 0 || height < 0) continue;
        while (std::getline(file, line)) {
            std::vector<std::string> row;
            std::stringstream cells(line); std::string cell;
            while (std::getline(cells, cell, '\t')) row.push_back(cell);
            if (int(row.size()) <= std::max({code, width, height}) || row[size_t(code)].empty()) continue;
            const int w = std::atoi(row[size_t(width)].c_str()), h = std::atoi(row[size_t(height)].c_str());
            if (w >= 1 && w <= 4 && h >= 1 && h <= 4) g_sizes[CodeKey(row[size_t(code)])] = {w, h};
        }
    }
    char line[96];
    std::snprintf(line, sizeof(line), "Frame collection: %u item sizes read", unsigned(g_sizes.size()));
    Log(line);
}

std::pair<int, int> SizeOf(uint32_t code) {
    ReadSizes();
    const auto found = g_sizes.find(code);
    return found != g_sizes.end() ? found->second : std::pair<int, int> {2, 4};
}

constexpr int kInventoryWidth = 10, kInventoryHeight = 4;

struct Occupancy {
    bool cell[kInventoryHeight][kInventoryWidth] {};
};
D2RL::Inventory::IterationAction __cdecl MarkOccupied(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* userData) noexcept {
    auto& grid = *static_cast<Occupancy*>(userData);
    if (info == nullptr || info->container != D2RL::Items::ItemContainer::Inventory) return D2RL::Inventory::IterationAction::Continue;
    const auto [w, h] = SizeOf(info->code);
    for (int y = info->y; y < info->y + h; ++y)
        for (int x = info->x; x < info->x + w; ++x)
            if (x >= 0 && x < kInventoryWidth && y >= 0 && y < kInventoryHeight) grid.cell[y][x] = true;
    return D2RL::Inventory::IterationAction::Continue;
}

// ---- Room in a full inventory ----------------------------------------------------------
// The way in and out needs a free spot in the inventory. When there is none (owner 2026-11-21:
// "a full inventory and the item cannot come out"), the items covering the spot that needs the
// fewest moved go to the collection's last page for the moment, and come back to the very
// cells they had once the item has passed.
struct Placed {
    D2RL::ItemHandle handle = 0;
    int x = 0, y = 0, w = 1, h = 1;
};
D2RL::Inventory::IterationAction __cdecl CollectPlaced(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* userData) noexcept {
    if (info != nullptr && info->container == D2RL::Items::ItemContainer::Inventory) {
        const auto [w, h] = SizeOf(info->code);
        static_cast<std::vector<Placed>*>(userData)->push_back({info->handle, info->x, info->y, w, h});
    }
    return D2RL::Inventory::IterationAction::Continue;
}

bool MoveExisting(const D2RL::PlayerHandle player, const D2RL::ItemHandle item, D2RL::Items::ItemContainer container,
                  D2RL::Items::Placement placement, int x, int y, uint32_t page) {
    if (g_items == nullptr) return false;
    D2RL::Items::ExistingItemOperation operation {};
    operation.structSize = D2RL::Items::ExistingItemOperationSize;
    operation.kind       = D2RL::Items::ExistingItemOperationKind::Move;
    operation.item       = item;
    operation.move.destination.structSize      = D2RL::Items::ItemDestinationSize;
    operation.move.destination.container       = container;
    operation.move.destination.placement       = placement;
    operation.move.destination.x               = uint32_t(std::max(0, x));
    operation.move.destination.y               = uint32_t(std::max(0, y));
    operation.move.destination.sharedStashPage = page;
    D2RL::Items::ExistingItemTransaction transaction {};
    transaction.structSize     = D2RL::Items::ExistingItemTransactionSize;
    transaction.player         = player;
    transaction.operationCount = 1;
    transaction.operations     = &operation;
    D2RL::Items::ExistingItemTransactionResult result {};
    result.structSize = D2RL::Items::ExistingItemTransactionResultSize;
    return g_items->executeExistingItemTransaction(g_ctx, &transaction, &result) == D2RL::Items::Result::Success;
}

std::mutex          g_roomLock;
std::vector<Placed> g_displaced;   // set aside for a full inventory; back at their own cells after

uint32_t SparePage() {
    const int tabs = std::min(g_tabs.load(), kBandTabs);
    return uint32_t(g_firstTab.load() + std::max(1, tabs) - 1);
}

// Puts back what MakeRoom set aside, each at its own cell (anywhere free if that is taken).
void RestoreDisplaced(const D2RL::PlayerHandle player) {
    std::vector<Placed> back;
    { std::lock_guard lock(g_roomLock); back.swap(g_displaced); }
    for (const Placed& item : back) {
        if (MoveExisting(player, item.handle, D2RL::Items::ItemContainer::Inventory, D2RL::Items::Placement::Exact, item.x, item.y, 0)) continue;
        if (MoveExisting(player, item.handle, D2RL::Items::ItemContainer::Inventory, D2RL::Items::Placement::Automatic, 0, 0, 0)) continue;
        Log("Frame collection: an item set aside for the move could not go back to the inventory; it waits on the collection's last page");
    }
}

// A w x h spot in a full inventory: the block whose items are fewest, those set aside. False
// (nothing moved) when no block can be cleared.
bool MakeRoom(const D2RL::PlayerHandle player, int w, int h, int& bx, int& by) {
    std::vector<Placed> all;
    D2RL::Inventory::ItemFilter filter {};
    filter.structSize    = D2RL::Inventory::ItemFilterSize;
    filter.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Inventory);
    g_inventory->forEachInventoryItem(g_ctx, player, &filter, &CollectPlaced, &all);
    int best = -1;
    std::vector<Placed> bestCover;
    for (int x = 0; x + w <= kInventoryWidth; ++x)
        for (int y = 0; y + h <= kInventoryHeight; ++y) {
            std::vector<Placed> cover;
            for (const Placed& item : all)
                if (item.x < x + w && item.x + item.w > x && item.y < y + h && item.y + item.h > y) cover.push_back(item);
            if (best < 0 || int(cover.size()) < best) { best = int(cover.size()); bestCover = cover; bx = x; by = y; }
        }
    if (best < 0) return false;
    std::vector<Placed> moved;
    for (const Placed& item : bestCover) {
        if (MoveExisting(player, item.handle, D2RL::Items::ItemContainer::SharedStash, D2RL::Items::Placement::Automatic, 0, 0, SparePage())) {
            moved.push_back(item);
            continue;
        }
        { std::lock_guard lock(g_roomLock); g_displaced.insert(g_displaced.end(), moved.begin(), moved.end()); }
        RestoreDisplaced(player);
        Log("Frame collection: the inventory is full and could not be cleared for the move");
        return false;
    }
    { std::lock_guard lock(g_roomLock); g_displaced.insert(g_displaced.end(), moved.begin(), moved.end()); }
    char line[128];
    std::snprintf(line, sizeof(line), "Frame collection: the inventory is full; %u item(s) set aside for the move", unsigned(moved.size()));
    Log(line);
    return true;
}

// What the frame thread is asked to click, and where the hand's item is on its way.
enum class Phase : int { Idle, DepositPrepare, DepositClick, DepositClicking, DepositStore, DepositStoring, TakePrepare, TakeClick, TakeClicking };
std::atomic<int>      g_phase{int(Phase::Idle)};
std::atomic<uint64_t> g_phaseAt{0};
std::atomic<uint64_t> g_clickedAt{0};   // the game's click on the inventory cell was sent
std::mutex            g_clickLock;
InventoryClick        g_click;
Category              g_handCategory = Category::Uniques;
uint32_t              g_handIdentity = 0;
bool                  g_handByCode   = false;

void SetPhase(Phase phase) { g_phaseAt.store(GetTickCount64()); g_phase.store(int(phase)); }

// Runs on the game thread: an item in the hand and a click on the page. It finds the item's
// catalog slot (its own category, whichever category the page shows) and a free inventory spot
// it fits in, for the frame thread's click.
void DepositPrepareOnGameThread(const D2RL::PluginContext*, void*) noexcept {
    if (g_ctx == nullptr || g_inventory == nullptr || g_items == nullptr) { SetPhase(Phase::Idle); return; }
    D2RL::PlayerHandle player {};
    if (g_inventory->getLocalPlayer(g_ctx, &player) != D2RL::Inventory::Result::Success) { SetPhase(Phase::Idle); return; }
    CursorItem cursor;
    D2RL::Inventory::ItemFilter filter {};
    filter.structSize    = D2RL::Inventory::ItemFilterSize;
    filter.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Cursor);
    if (g_inventory->forEachInventoryItem(g_ctx, player, &filter, &CollectCursor, &cursor) != D2RL::Inventory::Result::Success || !cursor.ok) {
        Log("Frame collection: nothing on the cursor to store");
        SetPhase(Phase::Idle);
        return;
    }
    if ((cursor.unique || cursor.set) && !cursor.identified) {
        Log("Frame collection: the hand's item is not identified; it stays in hand");
        SetPhase(Phase::Idle);
        return;
    }
    const Library& library = TabLibrary();
    Category category = Category::Uniques;
    uint32_t identity = 0;
    bool     byCode   = false;
    if (cursor.unique && cursor.recordId != UINT32_MAX && library.SlotOf(Category::Uniques, cursor.recordId) != UINT32_MAX) {
        category = Category::Uniques; identity = cursor.recordId;
    } else if (cursor.set && cursor.recordId != UINT32_MAX && library.SlotOf(Category::Sets, cursor.recordId) != UINT32_MAX) {
        category = Category::Sets; identity = cursor.recordId;
    } else if (stashslots::Find(cursor.code) != nullptr) {
        // The game's own Gems / Materials / Runes tab takes it (the frame thread clicks it there).
        Log("Frame collection: a gem, rune or material goes to the game's own tab, not the collection; it stays in hand");
        SetPhase(Phase::Idle);
        return;
    } else if (library.SlotOfCode(Category::Materials, cursor.code) != UINT32_MAX) {
        category = Category::Materials; identity = cursor.code; byCode = true;
    } else {
        Log("Frame collection: the cursor item is not a catalog collectible; it stays in hand");
        SetPhase(Phase::Idle);
        return;
    }
    Occupancy grid;
    filter.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Inventory);
    g_inventory->forEachInventoryItem(g_ctx, player, &filter, &MarkOccupied, &grid);
    const auto [w, h] = SizeOf(cursor.code);
    for (int x = 0; x + w <= kInventoryWidth; ++x)
        for (int y = 0; y + h <= kInventoryHeight; ++y) {
            bool free = true;
            for (int yy = y; yy < y + h && free; ++yy)
                for (int xx = x; xx < x + w && free; ++xx) free = !grid.cell[yy][xx];
            if (!free) continue;
            {
                std::lock_guard lock(g_clickLock);
                g_click = InventoryClick {x, y, w, h};
                g_handCategory = category; g_handIdentity = identity; g_handByCode = byCode;
            }
            char line[192];
            std::snprintf(line, sizeof(line), "Frame collection: storing the hand's %s item (id %u) through inventory cell %d,%d (%d x %d)",
                          CategoryName(category), unsigned(identity), x, y, w, h);
            Log(line);
            SetPhase(Phase::DepositClick);
            return;
        }
    int x = 0, y = 0;
    if (MakeRoom(player, w, h, x, y)) {
        {
            std::lock_guard lock(g_clickLock);
            g_click = InventoryClick {x, y, w, h};
            g_handCategory = category; g_handIdentity = identity; g_handByCode = byCode;
        }
        SetPhase(Phase::DepositClick);
        return;
    }
    Log("Frame collection: no room in the inventory to pass the hand's item through; it stays in hand");
    SetPhase(Phase::Idle);
}

// Runs on the game thread after the click: the item now in the inventory goes to its slot.
struct LandedMatch {
    Category         category = Category::Uniques;
    uint32_t         identity = 0;
    int              x = 0, y = 0;
    D2RL::ItemHandle exact = 0, any = 0;
};
D2RL::Inventory::IterationAction __cdecl FindLanded(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* userData) noexcept {
    auto& match = *static_cast<LandedMatch*>(userData);
    if (info == nullptr || info->container != D2RL::Items::ItemContainer::Inventory) return D2RL::Inventory::IterationAction::Continue;
    const int32_t record = info->qualityRecordId;
    const bool matches = (match.category == Category::Uniques   && info->quality == D2RL::Items::Quality::Unique && record >= 0 && uint32_t(record) == match.identity)
                      || (match.category == Category::Sets      && info->quality == D2RL::Items::Quality::Set    && record >= 0 && uint32_t(record) == match.identity)
                      || (match.category == Category::Materials && info->code == match.identity);
    if (!matches) return D2RL::Inventory::IterationAction::Continue;
    if (info->x == match.x && info->y == match.y) match.exact = info->handle;
    else if (match.any == 0) match.any = info->handle;
    return D2RL::Inventory::IterationAction::Continue;
}

void DepositStoreOnGameThread(const D2RL::PluginContext*, void*) noexcept {
    InventoryClick click; Category category; uint32_t identity; bool byCode;
    {
        std::lock_guard lock(g_clickLock);
        click = g_click; category = g_handCategory; identity = g_handIdentity; byCode = g_handByCode;
    }
    D2RL::PlayerHandle player {};
    if (g_ctx == nullptr || g_inventory == nullptr || g_inventory->getLocalPlayer(g_ctx, &player) != D2RL::Inventory::Result::Success) {
        SetPhase(Phase::Idle);
        return;
    }
    LandedMatch match;
    match.category = category; match.identity = identity; match.x = click.x; match.y = click.y;
    D2RL::Inventory::ItemFilter filter {};
    filter.structSize    = D2RL::Inventory::ItemFilterSize;
    filter.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Inventory);
    g_inventory->forEachInventoryItem(g_ctx, player, &filter, &FindLanded, &match);
    const D2RL::ItemHandle landed = match.exact != 0 ? match.exact : match.any;
    // Not landed yet: the game takes the click on its next tick or two. Asked again on the next
    // update (no fixed wait - the item showed in the inventory for half a second with one).
    if (landed == 0 && GetTickCount64() - g_clickedAt.load() < 1000) {
        // Asked again on the game's next tick, not the next update (the item would sit there).
        if (!(g_threads != nullptr && g_threads->runOnGameThread != nullptr
              && g_threads->runOnGameThread(g_ctx, &DepositStoreOnGameThread, nullptr) == D2RL::Threads::Result::Success))
            g_phase.store(int(Phase::DepositStore));
        return;
    }
    char line[192];
    if (landed == 0) {
        std::snprintf(line, sizeof(line), "Frame collection: the click did not put the hand's %s item (id %u) into the inventory",
                      CategoryName(category), unsigned(identity));
    } else if (!MoveToBand(player, landed, category)) {
        std::snprintf(line, sizeof(line), "Frame collection: could not store the %s item (id %u); it stays in the inventory",
                      CategoryName(category), unsigned(identity));
    } else {
        if (byCode) TabMarkFoundCode(category, identity);
        else { TabMarkFound(category, identity); TabShowEntry(category, identity); }
        std::snprintf(line, sizeof(line), "Frame collection: stored the hand's %s item (id %u) in its slot",
                      CategoryName(category), unsigned(identity));
    }
    Log(line);
    RestoreDisplaced(player);   // a full inventory's items back at their cells
    SetPhase(Phase::Idle);
}
std::atomic<uint32_t> g_actionCategory{UINT32_MAX};
std::atomic<uint32_t> g_actionId{0};
std::atomic<bool>     g_acting{false};

// Moves one item back to the player's inventory: a stored copy taken out.
bool MoveToInventory(const D2RL::PlayerHandle player, const D2RL::ItemHandle item) {
    if (g_items == nullptr) return false;
    D2RL::Items::ExistingItemOperation operation {};
    operation.structSize = D2RL::Items::ExistingItemOperationSize;
    operation.kind       = D2RL::Items::ExistingItemOperationKind::Move;
    operation.item       = item;
    operation.move.destination.structSize = D2RL::Items::ItemDestinationSize;
    operation.move.destination.container  = D2RL::Items::ItemContainer::Inventory;
    operation.move.destination.placement  = D2RL::Items::Placement::Automatic;

    D2RL::Items::ExistingItemTransaction transaction {};
    transaction.structSize     = D2RL::Items::ExistingItemTransactionSize;
    transaction.player         = player;
    transaction.operationCount = 1;
    transaction.operations     = &operation;
    D2RL::Items::ExistingItemTransactionResult result {};
    result.structSize = D2RL::Items::ExistingItemTransactionResultSize;
    return g_items->executeExistingItemTransaction(g_ctx, &transaction, &result) == D2RL::Items::Result::Success;
}

// The item a slot action is about: the stored copy (in the collection's band)
// wins, else a copy the player carries.
struct SlotMatch {
    Category category = Category::Uniques;
    uint32_t identity = 0;
    uint32_t first    = 0;
    uint32_t beyond   = 0;
    uint64_t stored   = 0;
    uint64_t carried  = 0;
};

D2RL::Inventory::IterationAction __cdecl FindSlotItem(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* userData) noexcept {
    auto& match = *static_cast<SlotMatch*>(userData);
    if (info == nullptr) return D2RL::Inventory::IterationAction::Continue;
    const int32_t record = info->qualityRecordId;
    const bool matches = (match.category == Category::Uniques && info->quality == D2RL::Items::Quality::Unique && record >= 0 && uint32_t(record) == match.identity)
                      || (match.category == Category::Sets    && info->quality == D2RL::Items::Quality::Set    && record >= 0 && uint32_t(record) == match.identity)
                      || (match.category == Category::Materials && info->code == match.identity);
    if (!matches) return D2RL::Inventory::IterationAction::Continue;
    const bool inBand = info->container == D2RL::Items::ItemContainer::SharedStash
                     && info->sharedStashPage >= match.first && info->sharedStashPage < match.beyond;
    if (inBand) { if (match.stored  == 0) match.stored  = info->handle; }
    else        { if (match.carried == 0) match.carried = info->handle; }
    return D2RL::Inventory::IterationAction::Continue;
}

struct HandleCell {
    D2RL::ItemHandle handle = 0;
    int              x = -1, y = -1;
};
std::atomic<uint64_t> g_noRoomAt{0};    // when a take-out last found the inventory full
std::atomic<uint64_t> g_taken{0};       // the item on its way out, lifted by the game's click
std::atomic<uint64_t> g_liftedAt{0};
D2RL::Inventory::IterationAction __cdecl FindHandleCell(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* userData) noexcept;

// Runs on the game thread after the lift: once the item has left the inventory (it is in the
// hand), what a full inventory set aside goes back to its cells. Asked each tick for a second.
void RestoreAfterLiftOnGameThread(const D2RL::PluginContext*, void*) noexcept {
    D2RL::PlayerHandle player {};
    if (g_ctx == nullptr || g_inventory == nullptr || g_inventory->getLocalPlayer(g_ctx, &player) != D2RL::Inventory::Result::Success) return;
    HandleCell cell;
    cell.handle = g_taken.load();
    D2RL::Inventory::ItemFilter filter {};
    filter.structSize    = D2RL::Inventory::ItemFilterSize;
    filter.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Inventory);
    g_inventory->forEachInventoryItem(g_ctx, player, &filter, &FindHandleCell, &cell);
    if (cell.x >= 0 && GetTickCount64() - g_liftedAt.load() < 1000 && g_threads != nullptr && g_threads->runOnGameThread != nullptr &&
        g_threads->runOnGameThread(g_ctx, &RestoreAfterLiftOnGameThread, nullptr) == D2RL::Threads::Result::Success)
        return;
    RestoreDisplaced(player);
}
D2RL::Inventory::IterationAction __cdecl FindHandleCell(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* userData) noexcept {
    auto& found = *static_cast<HandleCell*>(userData);
    if (info != nullptr && info->handle == found.handle && info->container == D2RL::Items::ItemContainer::Inventory) {
        found.x = info->x;
        found.y = info->y;
    }
    return D2RL::Inventory::IterationAction::Continue;
}

// Runs on the game thread: an empty hand and a click on a lit card. The stored copy goes into a
// free inventory spot, and the frame thread's click there lifts it into the hand. A card of an
// item the player carries elsewhere does nothing: only the hand stores (owner 2026-11-20).
void SlotActionOnGameThread(const D2RL::PluginContext*, void*) noexcept {
    const uint32_t categoryValue = g_actionCategory.exchange(UINT32_MAX);
    const uint32_t identity      = g_actionId.load();
    g_acting.store(false);
    if (categoryValue == UINT32_MAX) { SetPhase(Phase::Idle); return; }
    const Category category = static_cast<Category>(categoryValue);
    if (g_ctx == nullptr || g_inventory == nullptr || g_items == nullptr) { SetPhase(Phase::Idle); return; }
    D2RL::PlayerHandle player {};
    if (g_inventory->getLocalPlayer(g_ctx, &player) != D2RL::Inventory::Result::Success) { SetPhase(Phase::Idle); return; }

    const int      tabs   = std::min(g_tabs.load(), kBandTabs);
    SlotMatch match;
    match.category = category;
    match.identity = identity;
    match.first    = uint32_t(g_firstTab.load());
    match.beyond   = match.first + uint32_t(std::max(0, tabs));
    D2RL::Inventory::ItemFilter filter {};
    filter.structSize    = D2RL::Inventory::ItemFilterSize;
    filter.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::SharedStash);
    g_inventory->forEachInventoryItem(g_ctx, player, &filter, &FindSlotItem, &match);

    char line[224];
    if (match.stored == 0) {
        std::snprintf(line, sizeof(line), "Frame collection: the %s item (id %u) is not in the collection; nothing to take out",
                      CategoryName(category), unsigned(identity));
        Log(line);
        SetPhase(Phase::Idle);
        return;
    }
    if (!MoveToInventory(player, match.stored)) {
        // A full inventory: the item stays stored and the card says so. Making room by setting
        // items aside and putting them back while the item rode the cursor left the hand's
        // container invisible and stuck until relog (owner 2026-11-21).
        g_noRoomAt.store(GetTickCount64());
        std::snprintf(line, sizeof(line), "Frame collection: no room in the inventory to take the %s item (id %u) out",
                      CategoryName(category), unsigned(identity));
        Log(line);
        SetPhase(Phase::Idle);
        return;
    }
    g_taken.store(match.stored);
    HandleCell cell;
    cell.handle = match.stored;
    filter.containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Inventory);
    g_inventory->forEachInventoryItem(g_ctx, player, &filter, &FindHandleCell, &cell);
    if (cell.x < 0) RestoreDisplaced(player);
    if (category == Category::Materials) TabMarkMissingCode(category, identity);
    else TabMarkMissing(category, identity);
    if (cell.x < 0) {
        std::snprintf(line, sizeof(line), "Frame collection: took the %s item (id %u) out, but could not find it in the inventory to lift it",
                      CategoryName(category), unsigned(identity));
        Log(line);
        SetPhase(Phase::Idle);
        return;
    }
    {
        std::lock_guard lock(g_clickLock);
        g_click = InventoryClick {cell.x, cell.y, 1, 1};
    }
    std::snprintf(line, sizeof(line), "Frame collection: took the %s item (id %u) out to inventory cell %d,%d; lifting it into the hand",
                  CategoryName(category), unsigned(identity), cell.x, cell.y);
    Log(line);
    SetPhase(Phase::TakeClick);
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

    // One line a session: which shared pages exist and what the collection claims,
    // so a wrong first_tab is visible at once.
    {
        static bool reported = false;
        if (!reported) {
            reported = true;
            uint32_t lowest = UINT32_MAX, highest = 0;
            for (const Seen& item : sweep.items) {
                if (item.page == UINT32_MAX) continue;
                lowest  = std::min(lowest, item.page);
                highest = std::max(highest, item.page);
            }
            char line[224];
            std::snprintf(line, sizeof(line),
                          "Frame collection: shared stash holds %u items over pages %u..%u; the collection claims pages %u..%u",
                          unsigned(sweep.items.size()), lowest == UINT32_MAX ? 0u : unsigned(lowest), unsigned(highest),
                          unsigned(first), unsigned(beyond - 1));
            Log(line);
            // Which page holds how many, so the tab numbering can be read off.
            uint32_t counts[16] {};
            for (const Seen& item : sweep.items)
                if (item.page != UINT32_MAX && item.page < 16) ++counts[item.page];
            std::snprintf(line, sizeof(line), "Frame collection: pages with items: 0:%u 1:%u 2:%u 3:%u 4:%u 5:%u 6:%u 7:%u 8:%u 9:%u 10:%u 11:%u",
                          unsigned(counts[0]), unsigned(counts[1]), unsigned(counts[2]), unsigned(counts[3]),
                          unsigned(counts[4]), unsigned(counts[5]), unsigned(counts[6]), unsigned(counts[7]),
                          unsigned(counts[8]), unsigned(counts[9]), unsigned(counts[10]), unsigned(counts[11]));
            Log(line);
        }
    }

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
    std::vector<Seen>  strays;   // in the collection's hidden tabs but not the collection's
    for (const Seen& item : sweep.items) {
        const bool ours = item.page >= first && item.page < beyond;
        if (!ours && (item.unique || item.set) && !item.identified) continue;   // unidentified: never collected
        if (item.unique && item.recordId != UINT32_MAX && library.SlotOf(Category::Uniques, item.recordId) != UINT32_MAX) {
            if (ours) { heldUnique.push_back(item.recordId); continue; }
            loose.push_back({item, Category::Uniques, item.recordId, false});
        } else if (item.set && item.recordId != UINT32_MAX && library.SlotOf(Category::Sets, item.recordId) != UINT32_MAX) {
            if (ours) { heldSet.push_back(item.recordId); continue; }
            loose.push_back({item, Category::Sets, item.recordId, false});
        } else if (stashslots::Find(item.code) != nullptr) {
            // A gem, rune or material belongs in the game's own Gems / Materials / Runes tab
            // (owner 2026-11-21): never collected, and one found in the hidden tabs goes back.
            if (ours) strays.push_back(item);
        } else if (library.SlotOfCode(Category::Materials, item.code) != UINT32_MAX) {
            if (ours) { heldMaterial.push_back(item.code); continue; }
            loose.push_back({item, Category::Materials, item.code, true});
        } else if (ours) {
            strays.push_back(item);   // no collectible at all (an item set aside and never put back)
        }
    }
    // The strays out of the hidden tabs, to the player's own shared pages (or the inventory),
    // only while no item is on its way (a full inventory's set-aside items wait there then).
    bool settled = g_phase.load() == int(Phase::Idle);
    { std::lock_guard lock(g_roomLock); settled = settled && g_displaced.empty(); }
    int returned = 0;
    for (const Seen& item : settled ? strays : std::vector<Seen> {}) {
        bool done = false;
        for (uint32_t page = 0; page < first && !done; ++page)
            done = MoveExisting(player, item.handle, D2RL::Items::ItemContainer::SharedStash, D2RL::Items::Placement::Automatic, 0, 0, page);
        if (!done) done = MoveExisting(player, item.handle, D2RL::Items::ItemContainer::Inventory, D2RL::Items::Placement::Automatic, 0, 0, 0);
        char line[160];
        std::snprintf(line, sizeof(line), "Frame collection: a stray item (code %.3s) %s hidden tab %u",
                      reinterpret_cast<const char*>(&item.code), done ? "moved out of" : "could not leave", unsigned(item.page));
        Log(line);
        returned += done ? 1 : 0;
    }
    if (returned > 0) {
        char line[128];
        std::snprintf(line, sizeof(line), "Frame collection: %d stray item(s) back on the player's own pages", returned);
        Log(line);
    }

    int moved = 0;
    for (const Loose& item : loose) {
        // One copy per entry: the collection keeps the first one it was given.
        std::vector<uint32_t>& held = item.category == Category::Uniques ? heldUnique
                                    : item.category == Category::Sets    ? heldSet
                                                                         : heldMaterial;
        if (std::find(held.begin(), held.end(), item.identity) != held.end()) continue;
        if (!MoveToBand(player, item.seen.handle, item.category)) {
            char line[192];
            std::snprintf(line, sizeof(line), "Frame collection: could not move a %s item (id %u) out of page %u",
                          CategoryName(item.category), unsigned(item.identity), unsigned(item.seen.page));
            Log(line);
            continue;
        }
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

void RequestSlotAction(Category category, uint32_t identity) {
    int idle = int(Phase::Idle);
    if (!g_phase.compare_exchange_strong(idle, int(Phase::TakePrepare))) return;   // one item on its way at a time
    g_phaseAt.store(GetTickCount64());
    g_actionId.store(identity);
    g_actionCategory.store(uint32_t(category));
}

void DepositCarried() {
    int idle = int(Phase::Idle);
    if (!g_phase.compare_exchange_strong(idle, int(Phase::DepositPrepare))) return;
    g_phaseAt.store(GetTickCount64());
    if (!(g_threads != nullptr && g_threads->runOnGameThread != nullptr
          && g_threads->runOnGameThread(g_ctx, &DepositPrepareOnGameThread, nullptr) == D2RL::Threads::Result::Success))
        g_depositing.store(true);   // the next update runs it
}

bool HandBusy() { return g_phase.load() != int(Phase::Idle); }
bool NoRoomRecently() { const uint64_t at = g_noRoomAt.load(); return at != 0 && GetTickCount64() - at < 3000; }

bool PassingBlock(InventoryClick& block) {
    const int phase = g_phase.load();
    if (phase == int(Phase::Idle) || phase == int(Phase::DepositPrepare) || phase == int(Phase::TakePrepare)) return false;
    std::lock_guard lock(g_clickLock);
    block = g_click;
    return true;
}

bool TakeInventoryClick(InventoryClick& click) {
    int expected = int(Phase::DepositClick);
    if (!g_phase.compare_exchange_strong(expected, int(Phase::DepositClicking))) {
        expected = int(Phase::TakeClick);
        if (!g_phase.compare_exchange_strong(expected, int(Phase::TakeClicking))) return false;
    }
    g_phaseAt.store(GetTickCount64());
    std::lock_guard lock(g_clickLock);
    click = g_click;
    return true;
}

void InventoryClickDone() {
    int expected = int(Phase::DepositClicking);
    if (g_phase.compare_exchange_strong(expected, int(Phase::DepositStoring))) {
        g_phaseAt.store(GetTickCount64());
        g_clickedAt.store(GetTickCount64());
        // Straight to the game thread: waiting for the next update left the item in sight.
        if (!(g_threads != nullptr && g_threads->runOnGameThread != nullptr
              && g_threads->runOnGameThread(g_ctx, &DepositStoreOnGameThread, nullptr) == D2RL::Threads::Result::Success))
            g_phase.store(int(Phase::DepositStore));
        return;
    }
    expected = int(Phase::TakeClicking);
    if (g_phase.compare_exchange_strong(expected, int(Phase::Idle))) {
        g_phaseAt.store(GetTickCount64());
        Log("Frame collection: the game's click lifted the item into the hand");
        bool aside = false;
        { std::lock_guard lock(g_roomLock); aside = !g_displaced.empty(); }
        if (aside && g_threads != nullptr && g_threads->runOnGameThread != nullptr) {
            g_liftedAt.store(GetTickCount64());
            g_threads->runOnGameThread(g_ctx, &RestoreAfterLiftOnGameThread, nullptr);
        }
    }
}

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
    // The panel's monitor dump is not written here: painting it and writing a BMP on
    // the render thread every 250 ms hitched the frame and made the panel flicker.
    // Use the flat session, or the collection console command, to write it.
    // A click on a slot: take the stored copy out, or store a carried one. It runs
    // whether or not affinity is on.
    if (g_actionCategory.load() != UINT32_MAX && !g_acting.load()) {
        g_acting.store(true);
        if (!(g_threads != nullptr && g_threads->runOnGameThread != nullptr
              && g_threads->runOnGameThread(g_ctx, &SlotActionOnGameThread, nullptr) == D2RL::Threads::Result::Success))
            SlotActionOnGameThread(g_ctx, nullptr);
    }
    if (g_depositing.exchange(false)) {
        if (!(g_threads != nullptr && g_threads->runOnGameThread != nullptr
              && g_threads->runOnGameThread(g_ctx, &DepositPrepareOnGameThread, nullptr) == D2RL::Threads::Result::Success))
            DepositPrepareOnGameThread(g_ctx, nullptr);
    }
    // The click has put the hand's item into the inventory: the move into the collection, asked
    // on every update until the game has landed it (DepositStoreOnGameThread).
    {
        const uint64_t since = GetTickCount64() - g_phaseAt.load();
        int store = int(Phase::DepositStore);
        if (g_phase.compare_exchange_strong(store, int(Phase::DepositStoring))) {
            if (!(g_threads != nullptr && g_threads->runOnGameThread != nullptr
                  && g_threads->runOnGameThread(g_ctx, &DepositStoreOnGameThread, nullptr) == D2RL::Threads::Result::Success))
                DepositStoreOnGameThread(g_ctx, nullptr);
        }
        // Nothing may wait forever: a click the frame thread never made (the stash closed, the
        // laser left the game) frees the hand's way after three seconds.
        const int phase = g_phase.load();
        if (phase != int(Phase::Idle) && since > 3000) {
            char line[128];
            std::snprintf(line, sizeof(line), "Frame collection: the hand's item move timed out (step %d); the way is free again", phase);
            Log(line);
            SetPhase(Phase::Idle);
            g_liftedAt.store(0);   // anything set aside for a full inventory goes back now
            if (g_threads != nullptr && g_threads->runOnGameThread != nullptr)
                g_threads->runOnGameThread(g_ctx, &RestoreAfterLiftOnGameThread, nullptr);
        }
    }
    // What the hand holds, while the stash is open (the page draws it under the laser).
    if (stashOpen && !g_carriedPolling.load() && GetTickCount64() - g_carriedAt.load() >= 150) {
        g_carriedAt.store(GetTickCount64());
        g_carriedPolling.store(true);
        if (!(g_threads != nullptr && g_threads->runOnGameThread != nullptr
              && g_threads->runOnGameThread(g_ctx, &PollCarriedOnGameThread, nullptr) == D2RL::Threads::Result::Success))
            PollCarriedOnGameThread(g_ctx, nullptr);
    } else if (!stashOpen) {
        std::lock_guard lock(g_carriedLock);
        g_carriedKey.clear();
        g_carriedCode.store(0);
    }
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

bool InCollectionBand(uint32_t sharedStashPage) {
    const uint32_t first = uint32_t(std::max(0, g_firstTab.load()));
    return sharedStashPage >= first && sharedStashPage < first + uint32_t(std::max(0, std::min(g_tabs.load(), kBandTabs)));
}

std::string CarriedIconKey() {
    std::lock_guard lock(g_carriedLock);
    return g_carriedKey;
}
uint32_t CarriedCode() { return g_carriedCode.load(); }

void StoreReport() {
    char line[192];
    std::snprintf(line, sizeof(line), "Frame collection: affinity %s, collection tabs %d..%d, %d moved so far",
                  g_affinity.load() ? "on" : "off", g_firstTab.load(),
                  g_firstTab.load() + g_tabs.load() - 1, g_movedTotal.load());
    Log(line);
}

}   // namespace framevr::collection
