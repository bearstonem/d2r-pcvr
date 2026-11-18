// Collection tabs, step 2: the plugin side. See collection_tab.h.
#include "collection_tab.h"

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "../steamvr/collection_view.h"
#include "logging.h"

namespace framevr::collection {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;

// The scan runs on the UI thread and the panel will paint on a worker, so the
// catalog and its found flags are read and written under one lock.
std::mutex  g_lock;
Library     g_library;
ViewState   g_view;
bool        g_loaded = false;
std::atomic<bool> g_wanted{false};   // the panel is open

// The catalog sits beside the plugin DLL, like the icon cache.
std::filesystem::path PluginFolder() {
    HMODULE module = nullptr;
    wchar_t path[32768] {};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&PluginFolder), &module);
    GetModuleFileNameW(module, path, 32768);
    return std::filesystem::path(path).parent_path();
}

void Log(const std::string& text) { framevr::logging::Write(text.c_str()); }

}   // namespace

void InitTab(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;

    const auto path = PluginFolder() / L"collection.tsv";
    std::ifstream input(path);
    if (!input) {
        Log("collection tab: collection.tsv is not beside the plugin; the tab stays empty");
        return;
    }
    Library  library;
    uint32_t lines = 0, bad = 0;
    std::string line;
    while (std::getline(input, line)) {
        ++lines;
        if (!library.LoadLine(line)) {
            ++bad;
            if (bad <= 3) Log("collection tab: malformed catalog line: " + line);
        }
    }
    library.Finish();

    {
        std::lock_guard lock(g_lock);
        g_library = std::move(library);
        g_view    = ViewState {};
        g_loaded  = true;
    }

    char summary[256];
    std::snprintf(summary, sizeof(summary), "collection tab: catalog loaded from %u lines (%u malformed): %u uniques, %u sets, %u runewords",
                  lines, bad, g_library.Size(Category::Uniques), g_library.Size(Category::Sets), g_library.Size(Category::Runewords));
    Log(summary);
}

void ShutdownTab() {
    std::lock_guard lock(g_lock);
    g_library = Library {};
    g_view    = ViewState {};
    g_loaded  = false;
    g_ctx     = nullptr;
}

const Library& TabLibrary() {
    return g_library;   // callers hold no lock: the scan writes before the panel reads
}

const ViewState& TabView() { return g_view; }

void TabSelectCategory(Category category) {
    std::lock_guard lock(g_lock);
    g_view.SelectCategory(g_library, category);
}

void TabStep(int pages) {
    std::lock_guard lock(g_lock);
    g_view.Step(g_library, pages);
}

void TabSetPage(int page) {
    std::lock_guard lock(g_lock);
    g_view.page = page;
    g_view.Clamp(g_library);
}

void TabSetSize(panel::Size size) {
    std::lock_guard lock(g_lock);
    g_view.SelectSize(g_library, size);
}

bool TabWanted() { return g_wanted.load(); }

void TabSetWanted(bool wanted) { g_wanted.store(wanted); }

void TabClearFound() {
    std::lock_guard lock(g_lock);
    if (g_loaded) g_library.ClearFound();
}

void TabMarkFound(Category category, uint32_t recordId) {
    std::lock_guard lock(g_lock);
    if (g_loaded) g_library.MarkFound(category, recordId);
}

void TabReport() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) {
        Log("collection tab: no catalog loaded");
        return;
    }
    char line[256];
    std::snprintf(line, sizeof(line), "collection tab: found %u / %u uniques, %u / %u sets, %u / %u runewords",
                  g_library.Found(Category::Uniques), g_library.Size(Category::Uniques),
                  g_library.Found(Category::Sets), g_library.Size(Category::Sets),
                  g_library.Found(Category::Runewords), g_library.Size(Category::Runewords));
    Log(line);
}

}   // namespace framevr::collection
