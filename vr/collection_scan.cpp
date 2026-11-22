// Collection tabs, step 1 (spike): the read-only report. See collection_scan.h.
#include "collection_scan.h"

#include <D2RLPlugin/api.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "collection_model.h"
#include "collection_store.h"
#include "collection_tab.h"
#include "collection_tooltip_provider.h"
#include "logging.h"

namespace framevr::collection {
namespace {

const D2RL::PluginContext*    g_ctx       = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;
const D2RL::ThreadService*    g_threads   = nullptr;
const D2RL::LifecycleService* g_lifecycle = nullptr;

// The report runs by itself once per game session, so the owner only has to
// load a character. The console command stays as a manual re-run.
uint64_t g_scannedSession = 0;

constexpr uint32_t kContainerSlots = 16;   // ItemContainer values are small

// What one container (or the whole character) held during a scan.
struct Tally {
    uint32_t items              = 0;
    uint32_t uniques            = 0;
    uint32_t sets               = 0;
    uint32_t runewordCandidates = 0;
    std::vector<uint32_t> uniqueRows;
    std::vector<uint32_t> setRows;
    std::vector<uint32_t> runewordNames;
};

struct ScanState {
    Tally    perContainer[kContainerSlots];
    uint32_t containersRead = 0;
    uint32_t readFailures   = 0;
};

void Note(std::vector<uint32_t>& list, uint32_t value) {
    if (std::find(list.begin(), list.end(), value) == list.end()) list.push_back(value);
}

const char* ContainerName(D2RL::Items::ItemContainer container) {
    switch (container) {
    case D2RL::Items::ItemContainer::Equipment:     return "equipment";
    case D2RL::Items::ItemContainer::Cursor:        return "cursor";
    case D2RL::Items::ItemContainer::Belt:          return "belt";
    case D2RL::Items::ItemContainer::Inventory:     return "inventory";
    case D2RL::Items::ItemContainer::Cube:          return "cube";
    case D2RL::Items::ItemContainer::Trade:         return "trade";
    case D2RL::Items::ItemContainer::PersonalStash: return "personal stash";
    case D2RL::Items::ItemContainer::SharedStash:   return "shared stash";
    case D2RL::Items::ItemContainer::CustomPage:    return "custom page";
    case D2RL::Items::ItemContainer::Ground:        return "ground";
    default:                                        return "?";
    }
}

D2RL::Inventory::IterationAction __cdecl CountItem(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void* userData) noexcept {
    if (item == nullptr || userData == nullptr) return D2RL::Inventory::IterationAction::Continue;
    auto& tally = *static_cast<Tally*>(userData);
    ++tally.items;

    const uint32_t record = item->qualityRecordId < 0 ? UINT32_MAX : static_cast<uint32_t>(item->qualityRecordId);
    // Found = stored in the collection's own tabs (the copy the page can take out); a copy
    // anywhere else (inventory, personal stash, the vanilla shared tabs) is still missing
    // there. Its text is remembered wherever it lies.
    const bool stored = item->container == D2RL::Items::ItemContainer::SharedStash && InCollectionBand(item->sharedStashPage);
    // A completed runeword (a Normal or Superior item): found by its Runes row.
    if (item->quality == D2RL::Items::Quality::Normal || item->quality == D2RL::Items::Quality::Superior) {
        if (const uint32_t runeword = RunewordRecordOf(*item); runeword != UINT32_MAX) {
            ++tally.runewordCandidates;
            if (stored) {
                TabSetMade(Category::Runewords, runeword, item->code, ItemTypeChain(item->code));
                TabMarkFound(Category::Runewords, runeword);
            }
            tooltip::RememberHandle(Category::Runewords, runeword, item->handle);
            return D2RL::Inventory::IterationAction::Continue;
        }
    }
    switch (item->quality) {
    case D2RL::Items::Quality::Unique:
        ++tally.uniques;
        Note(tally.uniqueRows, record);
        if (record != UINT32_MAX) {
            // a unique with a card per copy: the next free card (a Hellfire Torch: its class's)
            if (stored) TabMarkFoundCopy(Category::Uniques, record, item->code == TorchCode() ? ItemClassSkills(item->handle) : -1);
            tooltip::RememberHandle(Category::Uniques, record, item->handle);   // its exact text, later
        }
        break;
    case D2RL::Items::Quality::Set:
        ++tally.sets;
        Note(tally.setRows, record);
        if (record != UINT32_MAX) {
            if (stored) TabMarkFound(Category::Sets, record);
            tooltip::RememberHandle(Category::Sets, record, item->handle);
        }
        break;
    case D2RL::Items::Quality::Normal:
        // A completed runeword carries its name string id in prefixIds[0]
        // instead of an affix id (D2RLPlugin/item.h), so a Normal item with a
        // prefix here is a runeword candidate for the catalog's third tab.
        if (item->prefixIds[0] != 0) {
            ++tally.runewordCandidates;
            Note(tally.runewordNames, item->prefixIds[0]);
        } else {
            // Otherwise a plain base item: a gem or rune the stash holds is a
            // found material (its slot is keyed by the code, not a record id).
            if (stored) TabMarkFoundCode(Category::Materials, item->code);
        }
        break;
    default:
        break;
    }
    return D2RL::Inventory::IterationAction::Continue;
}

// The report belongs in the mod's own session log (d2r-vr-frame.log), the file
// the team reads, not in the loader's startup log.
void LogLine(const char* text) { framevr::logging::Write(text); }

void ListRows(const char* what, const std::vector<uint32_t>& rows, uint32_t limit) {
    char        line[512];
    const auto  shown = std::min<uint32_t>(static_cast<uint32_t>(rows.size()), limit);
    int         at    = std::snprintf(line, sizeof(line), "collection:   %s (%u):", what, static_cast<unsigned>(rows.size()));
    for (uint32_t i = 0; i < shown && at > 0 && at < static_cast<int>(sizeof(line)); ++i)
        at += std::snprintf(line + at, sizeof(line) - static_cast<size_t>(at), " %u", static_cast<unsigned>(rows[i]));
    if (at > 0 && at < static_cast<int>(sizeof(line)) && rows.size() > shown)
        std::snprintf(line + at, sizeof(line) - static_cast<size_t>(at), " ...");
    LogLine(line);
}

void Report(const ScanState& state) {
    Tally        totals;
    char         line[512];
    const D2RL::Items::ItemContainer containers[] = {
        D2RL::Items::ItemContainer::Inventory,     D2RL::Items::ItemContainer::Cube,
        D2RL::Items::ItemContainer::PersonalStash, D2RL::Items::ItemContainer::SharedStash,
        D2RL::Items::ItemContainer::CustomPage,    D2RL::Items::ItemContainer::Belt,
        D2RL::Items::ItemContainer::Equipment,
    };
    for (auto container : containers) {
        const auto& tally = state.perContainer[static_cast<uint32_t>(container)];
        if (tally.items == 0 && container != D2RL::Items::ItemContainer::Inventory) continue;
        std::snprintf(line, sizeof(line), "collection:   %-14s %4u items (%u unique, %u set, %u runeword candidate)",
                      ContainerName(container), static_cast<unsigned>(tally.items), static_cast<unsigned>(tally.uniques),
                      static_cast<unsigned>(tally.sets), static_cast<unsigned>(tally.runewordCandidates));
        LogLine(line);
        totals.items += tally.items;
        totals.uniques += tally.uniques;
        totals.sets += tally.sets;
        totals.runewordCandidates += tally.runewordCandidates;
        for (uint32_t row : tally.uniqueRows) Note(totals.uniqueRows, row);
        for (uint32_t row : tally.setRows) Note(totals.setRows, row);
        for (uint32_t name : tally.runewordNames) Note(totals.runewordNames, name);
    }

    std::snprintf(line, sizeof(line), "collection: scan done - %u containers read, %u read failures, %u items: %u unique, %u set, %u runeword candidate",
                  static_cast<unsigned>(state.containersRead), static_cast<unsigned>(state.readFailures),
                  static_cast<unsigned>(totals.items), static_cast<unsigned>(totals.uniques),
                  static_cast<unsigned>(totals.sets), static_cast<unsigned>(totals.runewordCandidates));
    LogLine(line);
    std::snprintf(line, sizeof(line), "collection: distinct found - %u unique rows, %u set rows, %u runeword names",
                  static_cast<unsigned>(totals.uniqueRows.size()), static_cast<unsigned>(totals.setRows.size()),
                  static_cast<unsigned>(totals.runewordNames.size()));
    LogLine(line);
    ListRows("unique rows", totals.uniqueRows, 60);
    ListRows("set rows", totals.setRows, 40);
    ListRows("runeword names", totals.runewordNames, 40);
    TabReport();
}

void RunScan() {
    if (g_ctx == nullptr || g_inventory == nullptr) {
        LogLine("collection: scan skipped - the inventory service is unavailable in this loader build");
        return;
    }

    D2RL::PlayerHandle player {};
    const auto playerResult = g_inventory->getLocalPlayer(g_ctx, &player);
    if (playerResult != D2RL::Inventory::Result::Success) {
        char line[256];
        std::snprintf(line, sizeof(line), "collection: no local player yet (result %u) - load an offline character, then run `collection` again",
                      static_cast<unsigned>(playerResult));
        LogLine(line);
        return;
    }

    ScanState state;
    TabClearFound();
    tooltip::ForgetHandles();   // the handles below replace last scan's
    const D2RL::Items::ItemContainer containers[] = {
        D2RL::Items::ItemContainer::Inventory,     D2RL::Items::ItemContainer::Cube,
        D2RL::Items::ItemContainer::PersonalStash, D2RL::Items::ItemContainer::SharedStash,
        D2RL::Items::ItemContainer::CustomPage,    D2RL::Items::ItemContainer::Belt,
        D2RL::Items::ItemContainer::Equipment,
    };
    for (auto container : containers) {
        D2RL::Inventory::ItemFilter filter {};
        filter.structSize    = D2RL::Inventory::ItemFilterSize;
        filter.containerMask = D2RL::Items::ContainerBit(container);
        const auto result = g_inventory->forEachInventoryItem(g_ctx, player, &filter, &CountItem,
                                                              &state.perContainer[static_cast<uint32_t>(container)]);
        if (result == D2RL::Inventory::Result::Success) {
            ++state.containersRead;
        } else {
            ++state.readFailures;
            char line[256];
            std::snprintf(line, sizeof(line), "collection:   %s read failed (result %u)", ContainerName(container),
                          static_cast<unsigned>(result));
            LogLine(line);
        }
    }
    Report(state);
}

void __cdecl ScanOnUiThread(const D2RL::PluginContext*, void*) noexcept { RunScan(); }

// Gameplay listeners already run on the UI thread, which is where the
// inventory reads belong, so the report runs straight from the event.
void __cdecl OnGameplay(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
    if (event == nullptr || event->kind != D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady) return;
    if (g_scannedSession == event->sessionGeneration) return;
    g_scannedSession = event->sessionGeneration;
    LogLine("collection: local player ready - reporting the catalog progress for this character");
    RunScan();
}

void QueueScan() {
    if (g_threads != nullptr && g_threads->runOnUiThread != nullptr
        && g_threads->runOnUiThread(g_ctx, &ScanOnUiThread, nullptr) == D2RL::Threads::Result::Success) {
        LogLine("collection: scan queued on the UI thread");
        return;
    }
    // No thread service: run inline. The console command already runs on the
    // game thread, which is what the inventory reads need.
    RunScan();
}

D2RL::ConsoleCommandResult __cdecl CmdCollection(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept {
    const char* args = (command != nullptr && command->args != nullptr) ? command->args : "";
    while (*args == ' ' || *args == '\t') ++args;
    if (*args == 0 || std::strncmp(args, "scan", 4) == 0) {
        QueueScan();
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (std::strncmp(args, "tab", 3) == 0) {
        const char* what = args + 3;
        while (*what == ' ' || *what == '\t') ++what;
        if (std::strncmp(what, "on", 2) == 0) { TabSetWanted(true); return D2RL::ConsoleCommandResult::Handled; }
        if (std::strncmp(what, "off", 3) == 0) { TabSetWanted(false); return D2RL::ConsoleCommandResult::Handled; }
        if (std::strncmp(what, "next", 4) == 0) { TabStep(1); return D2RL::ConsoleCommandResult::Handled; }
        if (std::strncmp(what, "prev", 4) == 0) { TabStep(-1); return D2RL::ConsoleCommandResult::Handled; }
        if (std::strncmp(what, "uniques", 7) == 0) { TabSelectCategory(Category::Uniques); return D2RL::ConsoleCommandResult::Handled; }
        if (std::strncmp(what, "sets", 4) == 0) { TabSelectCategory(Category::Sets); return D2RL::ConsoleCommandResult::Handled; }
        if (std::strncmp(what, "runewords", 9) == 0) { TabSelectCategory(Category::Runewords); return D2RL::ConsoleCommandResult::Handled; }
    }
    if (std::strncmp(args, "status", 6) == 0) {
        if (g_ctx != nullptr)
            g_ctx->WriteConsoleMessage(g_inventory != nullptr ? "collection: the inventory service is available; run `collection scan`"
                                                             : "collection: the inventory service is unavailable in this loader build");
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (g_ctx != nullptr) g_ctx->WriteConsoleWarning("collection: usage: collection [scan|status]");
    return D2RL::ConsoleCommandResult::InvalidArguments;
}

}   // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    if (ctx == nullptr) return;

    if (ctx->QueryService(&g_inventory) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize)) {
        g_inventory = nullptr;
        ctx->LogWarn("collection: the inventory service is unavailable; the collection report stays off");
    }
    if (ctx->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasThreadServiceField(g_threads, D2RL::ThreadServiceRequiredSize)) {
        g_threads = nullptr;
    }
    if (ctx->QueryService(&g_lifecycle) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceField(g_lifecycle, D2RL::LifecycleServiceRequiredSize)) {
        g_lifecycle = nullptr;
    }

    if (g_lifecycle != nullptr && g_lifecycle->registerGameplayEventListener != nullptr) {
        D2RL::Lifecycle::GameplayEventListener listener {};
        listener.structSize = D2RL::Lifecycle::GameplayEventListenerSize;
        listener.kind       = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady;
        listener.callback   = &OnGameplay;
        D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
        if (g_lifecycle->registerGameplayEventListener(ctx, &listener, &handle) != D2RL::Lifecycle::Result::Success)
            ctx->LogWarn("collection: the local-player listener did not register; run `collection` from the console instead");
    }

    if (!ctx->RegisterConsoleCommand("collection", &CmdCollection,
                                     "collection - the unique/set/runeword catalog: scan (report what this character carries)"))
        ctx->LogError("collection: the console command did not register");
    // Also in the mod's own session log, so a session says at a glance whether
    // the report is active.
    LogLine("collection: spike ready - the report runs when a character loads, and `collection scan` repeats it");
}

void Shutdown() {
    g_ctx            = nullptr;
    g_inventory      = nullptr;
    g_threads        = nullptr;
    g_lifecycle      = nullptr;
    g_scannedSession = 0;
}

}   // namespace framevr::collection
