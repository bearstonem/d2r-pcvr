// Collection tooltip, plugin side. See collection_tooltip_provider.h.
#include "collection_tooltip_provider.h"

#include <D2RLPlugin/api.h>

#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "collection_tooltip.h"
#include "logging.h"

namespace framevr::collection::tooltip {
namespace {

const D2RL::PluginContext* g_ctx     = nullptr;
const D2RL::ItemService*   g_items   = nullptr;
const D2RL::ThreadService* g_threads = nullptr;

// The render thread records a request and reads the answer; the UI thread fills
// it. Everything shared goes through this lock.
std::mutex  g_mutex;
Entry       g_requestEntry;
uint64_t    g_requestRevision = 0;
uint64_t    g_generation      = 0;
bool        g_pending         = false;
bool        g_queued          = false;
std::string g_resultText;
std::string g_resultRanges;
uint64_t    g_resultRevision  = 0;

// The live handles the scan kept, keyed by (category << 32) | recordId. Only the
// UI thread touches this map (the scan and the fetch both run there).
std::unordered_map<uint64_t, uint64_t> g_handles;

uint64_t KeyOf(Category category, uint32_t recordId) { return (uint64_t(category) << 32) | recordId; }

// The tooltip calls share one buffer contract: a null buffer and zero capacity
// report the size the text needs (with its null), then the second call fills it.
template <class Call>
std::string ReadText(Call&& call) {
    uint32_t needed = 0;
    if (call(nullptr, 0, &needed) != D2RL::Items::Result::BufferTooSmall || needed == 0) return {};
    std::vector<char> buffer(static_cast<size_t>(needed));
    uint32_t          written = 0;
    if (call(buffer.data(), uint32_t(buffer.size()), &written) != D2RL::Items::Result::Success) return {};
    return std::string(buffer.data());
}

std::string ReadItemTooltip(uint64_t handle) {
    if (!D2RL::HasItemServiceField(g_items, D2RL::ItemServiceTooltipFieldEnd)) return {};
    return ReadText([&](char* output, uint32_t size, uint32_t* required) {
        return g_items->getItemTooltip(g_ctx, handle, output, size, required);
    });
}

std::string ReadPreviewTooltip(const PreviewSpec& spec) {
    if (!D2RL::HasItemServiceField(g_items, D2RL::ItemServicePreviewTooltipFieldEnd)) return {};
    D2RL::Items::ItemPreviewSpec native {};
    native.structSize        = D2RL::Items::ItemPreviewSpecSize;
    native.flags             = D2RL::Items::ItemPreviewShowStatRanges;   // the ranges a found item would show
    native.code              = spec.code;
    native.runewordRecordId  = UINT32_MAX;
    native.qualityRecordId   = UINT32_MAX;
    native.itemLevel         = spec.level;
    switch (spec.kind) {
    case PreviewKind::Unique: native.quality = D2RL::Items::Quality::Unique; native.qualityRecordId = spec.recordId; break;
    case PreviewKind::Set:    native.quality = D2RL::Items::Quality::Set;    native.qualityRecordId = spec.recordId; break;
    case PreviewKind::Normal: native.quality = D2RL::Items::Quality::Normal; break;
    default: return {};
    }
    return ReadText([&](char* output, uint32_t size, uint32_t* required) {
        return g_items->getItemPreviewTooltip(g_ctx, &native, output, size, required);
    });
}

Answer Fetch(const Entry& entry) {
    Answer answer;
    if (g_ctx == nullptr || g_items == nullptr) return answer;
    const PreviewSpec spec = PreviewOf(entry);
    // A found unique or set: the exact item text through its live handle, and -
    // under it - what the item could have rolled, so the two can be compared.
    if (entry.category != Category::Materials) {
        const auto found = g_handles.find(KeyOf(entry.category, entry.recordId));
        if (found != g_handles.end()) {
            answer.text = ReadItemTooltip(found->second);
            if (!answer.text.empty()) {
                if (spec.kind != PreviewKind::Unsupported) answer.ranges = ReadPreviewTooltip(spec);
                return answer;
            }
        }
    }
    // Missing, or the exact text failed: the catalog's own preview (its ranges).
    if (spec.kind != PreviewKind::Unsupported) answer.text = ReadPreviewTooltip(spec);
    return answer;
}

void __cdecl OnUiThread(const D2RL::PluginContext*, void*) noexcept {
    Entry    entry;
    uint64_t revision = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_pending) { g_queued = false; return; }
        entry            = g_requestEntry;
        revision         = g_requestRevision;
        g_pending        = false;
        g_queued         = false;
    }
    Answer answer = Fetch(entry);
    {   // one line per pointed entry, so a headset session says whether the fetch
        // returned anything and which call answered it
        const char* source = answer.text.empty() ? "none"
                           : answer.ranges.empty() ? "preview" : "exact+preview";
        char line[320];
        std::snprintf(line, sizeof line, "collection tooltip: %s id %u '%s' -> %s (text %u B, ranges %u B)",
                      CategoryName(entry.category), entry.recordId, entry.name.c_str(), source,
                      unsigned(answer.text.size()), unsigned(answer.ranges.size()));
        framevr::logging::Write(line);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_resultText     = std::move(answer.text);
    g_resultRanges   = std::move(answer.ranges);
    g_resultRevision = revision;
}

}   // namespace

void InitProvider(const D2RL::PluginContext* ctx) {
    g_ctx     = ctx;
    g_items   = nullptr;
    g_threads = nullptr;
    g_handles.clear();
    if (ctx == nullptr) return;
    if (ctx->QueryService(&g_items) != D2RL::ServiceQueryResult::Success) g_items = nullptr;
    if (ctx->QueryService(&g_threads) != D2RL::ServiceQueryResult::Success) g_threads = nullptr;
}

void ShutdownProvider() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ctx = nullptr;
    g_items = nullptr;
    g_threads = nullptr;
    g_handles.clear();
    g_pending = g_queued = false;
    g_resultText.clear();
    g_resultRanges.clear();
    g_requestRevision = g_resultRevision = g_generation = 0;
}

void RememberHandle(Category category, uint32_t recordId, uint64_t handle) {
    if (handle == 0) return;
    g_handles[KeyOf(category, recordId)] = handle;
}

void ForgetHandles() { g_handles.clear(); }

uint64_t Request(const Entry& entry) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_requestEntry    = entry;
    g_requestRevision = ++g_generation;
    g_pending         = true;
    return g_requestRevision;
}

Answer Current() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return {g_resultText, g_resultRanges, g_resultRevision};
}

void Tick() {
    bool queue = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_pending && !g_queued) { g_queued = true; queue = true; }
    }
    if (!queue) return;
    if (g_threads != nullptr && g_threads->runOnUiThread != nullptr &&
        g_threads->runOnUiThread(g_ctx, &OnUiThread, nullptr) == D2RL::Threads::Result::Success)
        return;
    // No thread service (or it refused): retry on the next frame. Reading the item
    // service from the render thread is not safe, so it is not done here.
    std::lock_guard<std::mutex> lock(g_mutex);
    g_queued = false;
}

}   // namespace framevr::collection::tooltip
