// Collection tabs, step 2: the plugin side. See collection_tab.h.
#include "collection_tab.h"

#include <D2RLPlugin/api.h>

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

#include "../steamvr/collection_draw.h"
#include "../steamvr/collection_view.h"
#include "../steamvr/interaction_icons.h"
#include "../steamvr/vr_settings_draw.h"
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

namespace {

// A plain 24-bit BMP: no encoder and no COM, so it works on any thread.
bool SaveBmp(const std::filesystem::path& path, const std::vector<uint8_t>& rgba, int width, int height) {
    if (width <= 0 || height <= 0 || rgba.size() < size_t(width) * size_t(height) * 4) return false;
    const uint32_t row = (uint32_t(width) * 3 + 3) & ~3u, size = row * uint32_t(height);
    FILE*          file = _wfopen(path.c_str(), L"wb");
    if (file == nullptr) return false;
    uint8_t head[54] = {'B', 'M'};
    const auto put = [&](int at, uint32_t value) { for (int i = 0; i < 4; ++i) head[at + i] = uint8_t(value >> (8 * i)); };
    put(2, 54 + size); put(10, 54); put(14, 40); put(18, uint32_t(width)); put(22, uint32_t(height));
    head[26] = 1; head[28] = 24; put(34, size);
    bool                 ok = fwrite(head, 1, 54, file) == 54;
    std::vector<uint8_t> line(row, 0);
    for (uint32_t y = 0; ok && y < uint32_t(height); ++y) {
        const uint8_t* from = rgba.data() + size_t(height - 1 - int(y)) * size_t(width) * 4;
        for (uint32_t x = 0; x < uint32_t(width); ++x) {   // RGBA in, BGR out
            line[x * 3 + 0] = from[x * 4 + 2];
            line[x * 3 + 1] = from[x * 4 + 1];
            line[x * 3 + 2] = from[x * 4 + 0];
        }
        ok = fwrite(line.data(), 1, row, file) == row;
    }
    fclose(file);
    return ok;
}

}   // namespace

void TabDumpPanel() {
    // The icons decode on the mod's worker, so the first calls would draw dots:
    // wait about a second (TickStore runs every 250 ms) before writing.
    static std::atomic<int>  calls{0};
    static std::atomic<bool> done{false};
    if (done.load() || calls.fetch_add(1) < 4) return;
    if (done.exchange(true)) return;

    Library    library;
    ViewState  view;
    {
        std::lock_guard lock(g_lock);
        if (!g_loaded) return;
        library = g_library;
        view    = g_view;
    }

    thread_local std::unique_ptr<framevr::settings::draw::Painter> painter;
    if (!painter) painter = std::make_unique<framevr::settings::draw::Painter>();
    if (!painter->Ready() || !painter->Begin()) return;

    const auto lookup = [](const std::string& key) -> const draw::IconPixels* {
        static thread_local std::shared_ptr<const interaction::icons::Image> held;
        static thread_local draw::IconPixels                                 view;
        held = interaction::icons::Get(key);
        if (!held || held->rgba.empty()) return nullptr;
        view.rgba = held->rgba.data(); view.width = int(held->width); view.height = int(held->height);
        return &view;
    };
    draw::Paint(*painter, library, view, lookup);
    const auto pixels = painter->End();
    if (pixels.empty()) return;

    const auto path = PluginFolder() / L"collection-panel.bmp";
    if (SaveBmp(path, pixels, int(settings::draw::CanvasWidth), int(settings::draw::CanvasHeight)))
        Log("collection tab: wrote collection-panel.bmp beside the plugin");
    else
        Log("collection tab: could not write collection-panel.bmp");
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

void TabSetShow(panel::Show show) {
    std::lock_guard lock(g_lock);
    g_view.SelectShow(g_library, show);
}

void TabSetHover(panel::Hit hit) {
    std::lock_guard lock(g_lock);
    g_view.hover = hit;
}

void TabShowEntry(Category category, uint32_t recordId) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) return;
    // The entry's slot inside its category, then its position in the filtered
    // list: the page is where that position lands at the current slot size.
    uint32_t slot = UINT32_MAX;
    for (uint32_t at = 0; at < g_library.Size(category); ++at) {
        const Entry* entry = g_library.At(category, at);
        if (entry != nullptr && entry->recordId == recordId) { slot = at; break; }
    }
    if (slot == UINT32_MAX) return;
    g_view.SelectCategory(g_library, category);
    if (g_view.show != panel::Show::All) {
        int position = 0;
        for (uint32_t at = 0; at < slot; ++at) {
            const bool found = g_library.IsFound(category, at);
            if ((g_view.show == panel::Show::Missing && !found) || (g_view.show == panel::Show::Found && found)) ++position;
        }
        g_view.page = position / panel::SlotsFor(g_view.size);
    } else {
        g_view.page = int(slot) / panel::SlotsFor(g_view.size);
    }
    g_view.Clamp(g_library);
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
