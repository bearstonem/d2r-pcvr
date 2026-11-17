// Collection tabs, step 1 (spike): the page probe. See collection_pages.h.
#include "collection_pages.h"

#include <D2RLPlugin/api.h>

#include <cstdio>
#include <cstring>

#include "collection_model.h"

namespace framevr::collection {
namespace {

const D2RL::PluginContext*    g_ctx       = nullptr;
const D2RL::InventoryService* g_inventory = nullptr;

D2RL::Inventory::PageRegistrationHandle g_pages[CategoryCount] {};
bool                                    g_registered = false;

void LogLine(const char* text) {
    if (g_ctx != nullptr) g_ctx->LogInfo(text);
}

const char* StateName(D2RL::Inventory::PageRegistrationState state) {
    switch (state) {
    case D2RL::Inventory::PageRegistrationState::Staged:  return "staged";
    case D2RL::Inventory::PageRegistrationState::Active:  return "active";
    case D2RL::Inventory::PageRegistrationState::Closing: return "closing";
    default:                                              return "unknown";
    }
}

const char* ConfigurationName(D2RL::Inventory::PageConfigurationState state) {
    switch (state) {
    case D2RL::Inventory::PageConfigurationState::Reserved:   return "reserved";
    case D2RL::Inventory::PageConfigurationState::Configured: return "configured";
    case D2RL::Inventory::PageConfigurationState::Closing:    return "closing";
    case D2RL::Inventory::PageConfigurationState::Faulted:    return "faulted";
    default:                                                  return "unknown";
    }
}

bool Ready() {
    if (g_ctx == nullptr || g_inventory == nullptr) {
        LogLine("collectionpages: the inventory service is unavailable in this loader build");
        return false;
    }
    if (!D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceConfigurePlayerPageFieldEnd)
        || g_inventory->registerPlayerPage == nullptr || g_inventory->configurePlayerPage == nullptr) {
        LogLine("collectionpages: this loader build has no custom player pages");
        return false;
    }
    return true;
}

void LogPage(const char* what, D2RL::Inventory::PageRegistrationHandle handle) {
    char line[256];
    D2RL::Inventory::PageRegistrationInfo info {};
    info.structSize = D2RL::Inventory::PageRegistrationInfoSize;
    if (g_inventory->getRegistrationInfo(g_ctx, handle, &info) == D2RL::Inventory::Result::Success)
        std::snprintf(line, sizeof(line), "collectionpages:   %s handle %llu state %s", what,
                      static_cast<unsigned long long>(handle), StateName(info.state));
    else
        std::snprintf(line, sizeof(line), "collectionpages:   %s handle %llu state unreadable", what,
                      static_cast<unsigned long long>(handle));
    LogLine(line);

    D2RL::Inventory::PlayerPageConfigurationInfo configuration {};
    configuration.structSize = D2RL::Inventory::PlayerPageConfigurationInfoSize;
    if (g_inventory->getPlayerPageConfigurationInfo(g_ctx, handle, &configuration) == D2RL::Inventory::Result::Success) {
        std::snprintf(line, sizeof(line), "collectionpages:   %s grid %ux%u, %s", what,
                      static_cast<unsigned>(configuration.width), static_cast<unsigned>(configuration.height),
                      ConfigurationName(configuration.state));
        LogLine(line);
    }
}

void RegisterPages() {
    if (!Ready()) return;
    if (g_registered) {
        LogLine("collectionpages: already registered this session");
        return;
    }

    char line[256];
    for (std::size_t i = 0; i < CategoryCount; ++i) {
        const auto category = static_cast<Category>(i);
        D2RL::Inventory::PlayerPageRegistration registration {};
        registration.structSize = D2RL::Inventory::PlayerPageRegistrationSize;
        registration.logicalId  = PageLogicalId(category);

        D2RL::Inventory::PageRegistrationHandle handle = D2RL::Inventory::InvalidHandle;
        const auto registered = g_inventory->registerPlayerPage(g_ctx, &registration, &handle);
        std::snprintf(line, sizeof(line), "collectionpages: register %s (%s) -> result %u", PageLogicalId(category),
                      "16x16", static_cast<unsigned>(registered));
        LogLine(line);
        if (registered != D2RL::Inventory::Result::Success) {
            // Conflict is the expected answer in retail mode: one custom page
            // only. A second registration failing here is the measurement.
            std::snprintf(line, sizeof(line),
                          "collectionpages: %s did not register - %s",
                          PageLogicalId(category),
                          registered == D2RL::Inventory::Result::Conflict
                              ? "Conflict: this save allows one custom page only (retail mode)"
                              : "see the loader log for the reason");
            LogLine(line);
            continue;
        }
        g_pages[i] = handle;

        D2RL::Inventory::PlayerPageConfiguration grid {};
        grid.structSize = D2RL::Inventory::PlayerPageConfigurationSize;
        grid.width      = PageWidth;
        grid.height     = PageHeight;
        const auto configured = g_inventory->configurePlayerPage(g_ctx, handle, &grid);
        std::snprintf(line, sizeof(line), "collectionpages: configure %s -> result %u", PageLogicalId(category),
                      static_cast<unsigned>(configured));
        LogLine(line);
        LogPage(PageLogicalId(category), handle);
    }

    g_registered = true;
    LogLine("collectionpages: pages registered - open the stash and check whether the new tabs appear, then save and reload the character");
}

void ShowInfo() {
    if (!Ready()) return;
    if (!g_registered) {
        LogLine("collectionpages: no pages registered this session; run `collectionpages register`");
        return;
    }
    for (std::size_t i = 0; i < CategoryCount; ++i)
        if (g_pages[i] != D2RL::Inventory::InvalidHandle) LogPage(PageLogicalId(static_cast<Category>(i)), g_pages[i]);
}

void MoveCursorToPage() {
    if (!Ready()) return;
    if (!g_registered || g_pages[0] == D2RL::Inventory::InvalidHandle) {
        LogLine("collectionpages: register a page first (`collectionpages register`)");
        return;
    }

    char line[256];
    D2RL::Inventory::LocalPlayerPageMoveRequest request {};
    request.structSize = D2RL::Inventory::LocalPlayerPageMoveRequestSize;
    request.kind       = D2RL::Inventory::LocalPlayerPageMoveKind::CursorToPage;
    request.x          = 0;
    request.y          = 0;
    const auto toPage  = g_inventory->executeLocalPlayerMove(g_ctx, g_pages[0], &request);
    std::snprintf(line, sizeof(line), "collectionpages: cursor -> page 0 cell (0,0) -> result %u (hold an item on the cursor first)", static_cast<unsigned>(toPage));
    LogLine(line);
    if (toPage != D2RL::Inventory::Result::Success) return;

    request.kind       = D2RL::Inventory::LocalPlayerPageMoveKind::PageToCursor;
    const auto toCursor = g_inventory->executeLocalPlayerMove(g_ctx, g_pages[0], &request);
    std::snprintf(line, sizeof(line), "collectionpages: page 0 cell (0,0) -> cursor -> result %u", static_cast<unsigned>(toCursor));
    LogLine(line);
}

void UnregisterPages() {
    if (!Ready()) return;
    if (!g_registered) {
        LogLine("collectionpages: nothing to unregister");
        return;
    }
    for (std::size_t i = 0; i < CategoryCount; ++i) {
        if (g_pages[i] == D2RL::Inventory::InvalidHandle) continue;
        char line[256];
        const auto result = g_inventory->unregisterPlayerPage(g_ctx, g_pages[i]);
        std::snprintf(line, sizeof(line), "collectionpages: unregister %s -> result %u", PageLogicalId(static_cast<Category>(i)), static_cast<unsigned>(result));
        LogLine(line);
        g_pages[i] = D2RL::Inventory::InvalidHandle;
    }
    g_registered = false;
}

D2RL::ConsoleCommandResult __cdecl CmdCollectionPages(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept {
    const char* args = (command != nullptr && command->args != nullptr) ? command->args : "";
    while (*args == ' ' || *args == '\t') ++args;

    if (*args == 0 || std::strncmp(args, "info", 4) == 0) {
        ShowInfo();
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (std::strncmp(args, "register", 8) == 0) {
        RegisterPages();
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (std::strncmp(args, "move", 4) == 0) {
        MoveCursorToPage();
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (std::strncmp(args, "unregister", 10) == 0) {
        UnregisterPages();
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (g_ctx != nullptr)
        g_ctx->WriteConsoleWarning("collectionpages: usage: collectionpages [info|register|move|unregister]");
    return D2RL::ConsoleCommandResult::InvalidArguments;
}

}   // namespace

void InitPages(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    if (ctx == nullptr) return;

    if (ctx->QueryService(&g_inventory) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasInventoryServiceField(g_inventory, D2RL::InventoryServiceRequiredSize)) {
        g_inventory = nullptr;
        ctx->LogWarn("collectionpages: the inventory service is unavailable; the page probe stays off");
    }

    if (!ctx->RegisterConsoleCommand("collectionpages", &CmdCollectionPages,
                                     "collectionpages - the custom-page probe: info / register / move / unregister"))
        ctx->LogError("collectionpages: the console command did not register");
}

void ShutdownPages() {
    if (g_inventory != nullptr && g_registered) {
        for (std::size_t i = 0; i < CategoryCount; ++i) {
            if (g_pages[i] == D2RL::Inventory::InvalidHandle) continue;
            g_inventory->unregisterPlayerPage(g_ctx, g_pages[i]);
            g_pages[i] = D2RL::Inventory::InvalidHandle;
        }
        g_registered = false;
    }
    g_ctx       = nullptr;
    g_inventory = nullptr;
}

}   // namespace framevr::collection
