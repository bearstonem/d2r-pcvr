// Ring skill menu (build steps -): ring-wheel-preview [--out <folder>] [--icons <icon cache folder>] [--kit <kit folder>] [--badges <folder>] [--t036 <folder>].
// By hand, never in ctest: --shot-ms <folder> [--adapter hardware] times a screenshot of the full ring at 150% (1296 x 1320), the
// frame's half and the worker's, beside the old all-on-the-frame save; its PNGs go into <folder>.
// The compose pass (vr/ring_wheel_compose.cpp) and the atlases (vr/ring_wheel_atlas.cpp) on WARP, plus the design notes
// items 14-16. With no arguments (the test): synthetic icons (numbered colored discs, no game art), upstream's menu fonts
// from the source tree, made-up text. Local only, never in a test or a commit: --icons reads the player's own icon cache
// (d2r_vr_icons), --kit runs the blurb check over every skill of the design kit's data, --out writes the pictures at 100%
// and 150% (straight alpha and over a dark backdrop). No game or headset needed.
#include "ring_wheel_draw.h"
#include "ring_wheel_skill_names.h"
#include "ring_wheel_pages.h"
#include "../vr/ring_wheel_compose.h"
#include "../vr/ring_wheel_atlas.h"
#include "ring_wheel_shader_blob.h"   // generated at build time (steamvr/ring.cmake)
#include "ring_wheel_png.h"
#include "ring_wheel_shot.h"
#include "ring_wheel_menu_art.h"   //: the sub-menus' pictures (ring_wheel_menu_art.rc is linked in)
#include "ring_wheel_glyph_art_decode.h"   // Glyph icon: the painted glyph and sigil squares (the sheet is in ring_wheel_menu_art.rc)
#include "ring_wheel_tree_art_decode.h"   // the painted tree squares (the sheet is in ring_wheel_menu_art.rc)
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef RING_WHEEL_FONTS
#define RING_WHEEL_FONTS L"assets/menu/fonts"
#endif

using Microsoft::WRL::ComPtr;
namespace rw = framevr::ringwheel;
namespace tx = framevr::ringwheel::text;
namespace icons = framevr::interaction::icons;
namespace {
int failures = 0;
void Check(bool ok, const std::string& what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what.c_str()); } }
void Need(HRESULT hr, const char* what) { if (FAILED(hr)) throw std::runtime_error(what); }
void Say(const char* m) { std::printf("%s\n", m); }

// A synthetic icon: a hue disc with a light ring and a bar turned by its id, so a wrong cell or turn shows. Straight alpha.
std::shared_ptr<const icons::Image> SyntheticIcon(int id) {
    auto im = std::make_shared<icons::Image>();
    im->width = im->height = 120;
    im->rgba.assign(120 * 120 * 4, 0);
    const float hue = std::fmod(id * .137f, 1.f), turn = id * 20 * rw::Pi / 180;
    auto channel = [&](float k) { const float x = std::fmod(hue * 6 + k, 6.f); return std::clamp(std::min(x, 4 - x), 0.f, 1.f); };
    for (int y = 0; y < 120; ++y) for (int x = 0; x < 120; ++x) {
        const float dx = x + .5f - 60, dy = y + .5f - 60, d = std::hypot(dx, dy), shade = .25f + .75f * std::clamp(1 - d / 75, 0.f, 1.f);
        const float along = dx * std::cos(turn) + dy * std::sin(turn), across = -dx * std::sin(turn) + dy * std::cos(turn);
        const bool light = std::fabs(d - 32) < 3 || (std::fabs(across) < 4 && std::fabs(along) < 25);
        uint8_t* p = &im->rgba[(size_t(y) * 120 + size_t(x)) * 4];
        p[0] = uint8_t(255 * (light ? .95f : channel(4) * shade)); p[1] = uint8_t(255 * (light ? .92f : channel(2) * shade));
        p[2] = uint8_t(255 * (light ? .85f : channel(0) * shade)); p[3] = 255;
    }
    return im;
}

struct Fixture {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    uint64_t value = 0;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<ID3D12Resource> target, readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT foot{};
    std::wstring adapter;   // the adapter's name (--shot-ms prints it)
    // hardware: the machine's default GPU (--shot-ms, by hand only); the tests always run on WARP.
    explicit Fixture(DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, bool hardware = false) {
        ComPtr<IDXGIFactory4> factory;
        Need(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
        ComPtr<IDXGIAdapter> warp;
        if (hardware) {
            ComPtr<IDXGIAdapter1> first;
            Need(factory->EnumAdapters1(0, &first), "the first adapter");
            Need(first.As(&warp), "adapter");
        } else Need(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter");
        DXGI_ADAPTER_DESC ad{};
        if (SUCCEEDED(warp->GetDesc(&ad))) adapter = ad.Description;
        Need(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12 device");
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Need(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)), "queue");
        Need(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "allocator");
        Need(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "list");
        Need(list->Close(), "close");
        Need(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence");
        // the ring's layer swapchain's size, made once; the content at the render scale sits at its top left.
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = rw::SwapchainW; d.Height = rw::SwapchainH; d.DepthOrArraySize = 1;
        d.MipLevels = 1; d.Format = format; d.SampleDesc.Count = 1; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Need(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&target)), "target");
        UINT64 bytes = 0;
        device->GetCopyableFootprints(&d, 0, 1, 0, &foot, nullptr, nullptr, &bytes);
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC b{};
        b.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; b.Width = bytes; b.Height = 1; b.DepthOrArraySize = 1; b.MipLevels = 1;
        b.SampleDesc.Count = 1; b.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Need(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &b, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "readback");
    }
    ~Fixture() { if (event) CloseHandle(event); }
    void Finish() {
        Need(queue->Signal(fence.Get(), ++value), "signal");
        if (fence->GetCompletedValue() < value) { fence->SetEventOnCompletion(value, event); WaitForSingleObject(event, 10000); }
    }
    // The target's content rectangle, premultiplied RGBA8, tight rows (the queue's earlier work finishes first).
    std::vector<uint8_t> Read(int w, int h) {
        Need(allocator->Reset(), "reset"); Need(list->Reset(allocator.Get(), nullptr), "list reset");
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {target.Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION to{readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}}; to.PlacedFootprint = foot;
        D3D12_TEXTURE_COPY_LOCATION from{target.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}}; from.SubresourceIndex = 0;
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
        Need(list->Close(), "close");
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        Finish();
        std::vector<uint8_t> out(size_t(w) * h * 4);
        uint8_t* p = nullptr;
        Need(readback->Map(0, nullptr, reinterpret_cast<void**>(&p)), "map");
        for (int y = 0; y < h; ++y) std::memcpy(&out[size_t(y) * w * 4], p + foot.Offset + size_t(y) * foot.Footprint.RowPitch, size_t(w) * 4);
        const D3D12_RANGE none{0, 0};
        readback->Unmap(0, &none);
        return out;
    }
};

// The seats a picture shows. The test: ten made-up skills a tree, three general and two item seats. --icons: the
// Sorceress's active skills (skills.txt ids; the names are the game's, already in upstream's tables).
struct SeatData { int id; int tree; const char* name; };
const SeatData Sorceress[] = {
    {36, 0, "Fire Bolt"}, {41, 0, "Inferno"}, {46, 0, "Blaze"}, {47, 0, "Fire Ball"}, {51, 0, "Fire Wall"}, {52, 0, "Enchant"}, {56, 0, "Meteor"}, {62, 0, "Hydra"},
    {38, 1, "Charged Bolt"}, {42, 1, "Static Field"}, {43, 1, "Telekinesis"}, {48, 1, "Nova"}, {49, 1, "Lightning"}, {53, 1, "Chain Lightning"},
    {54, 1, "Teleport"}, {57, 1, "Thunder Storm"}, {58, 1, "Energy Shield"},
    {39, 2, "Ice Bolt"}, {40, 2, "Frozen Armor"}, {44, 2, "Frost Nova"}, {45, 2, "Ice Blast"}, {50, 2, "Shiver Armor"}, {55, 2, "Glacial Spike"},
    {59, 2, "Blizzard"}, {60, 2, "Chilling Armor"}, {64, 2, "Frozen Orb"},
    {0, -1, "Attack"}, {217, -1, "Scroll of Identify"}, {220, -1, "Scroll of Town Portal"},
};
const char* const SorceressTrees[rw::Trees] = {"Fire Spells", "Lightning Spells", "Cold Spells"};
const char* const TestTrees[rw::Trees] = {"First Tree", "Second Tree Skills", "Third Tree"};
struct Data {
    std::vector<rw::SeatIn> in;
    std::vector<std::string> names;   // by seat id order of `in`
    const char* const* trees = TestTrees;
};
Data TestData() {
    Data d;
    for (int t = 0; t < rw::Trees; ++t) for (int i = 0; i < 10; ++i) { d.in.push_back({100 + t * 20 + i, -1, t}); d.names.push_back("Skill " + std::to_string(t * 10 + i + 1)); }
    for (int i = 0; i < 3; ++i) { d.in.push_back({200 + i, -1, -1}); d.names.push_back("General " + std::to_string(i + 1)); }
    for (int i = 0; i < 2; ++i) { d.in.push_back({210 + i, 7, -1}); d.names.push_back("Item Skill " + std::to_string(i + 1)); }
    return d;
}
Data SorceressData() {
    Data d; d.trees = SorceressTrees;
    for (const auto& s : Sorceress) { d.in.push_back({s.id, s.tree < 0 && s.id >= 200 ? 7 : -1, s.tree}); d.names.push_back(s.name); }
    return d;
}
// Per seat index of a built layout: its name and the atlas request ids.
struct Seats {
    rw::Layout layout;
    std::vector<std::string> names, initials;
    std::vector<const char*> initialPtr;
    rw::AtlasWorker::Request request;
};
Seats Lay(const Data& d, rw::TreeLayout mode, int pct) {
    Seats s; s.layout = rw::Build(d.in.data(), int(d.in.size()), mode);
    s.request.scalePct = pct; s.request.n = s.layout.count; s.request.classId = 1;
    for (int i = 0; i < s.layout.count; ++i) {
        const int id = s.layout.seats[size_t(i)].id;
        s.request.ids[size_t(i)] = id;
        std::string name;
        for (size_t k = 0; k < d.in.size(); ++k) if (d.in[k].id == id) name = d.names[k];
        s.names.push_back(name);
        std::string ini;
        for (const auto& w : tx::Words(name)) if (!w.empty() && ini.size() < 2 && std::isalpha(uint8_t(w[0]))) ini += w[0];
        s.initials.push_back(ini);
    }
    for (const auto& i : s.initials) s.initialPtr.push_back(i.c_str());
    return s;
}

uint8_t At(const std::vector<uint8_t>& px, int w, float cx, float cy, const rw::ContentRect& c, int ch) {
    const int x = int((cx - c.x0) * c.w / c.canvasW), y = int((cy - c.y0) * c.w / c.canvasW);
    return px[(size_t(y) * w + size_t(x)) * 4 + size_t(ch)];
}
// Bright text-like pixels (red channel over 160 and opaque) in a canvas box.
int Bright(const std::vector<uint8_t>& px, int w, const rw::ContentRect& c, float x0, float y0, float x1, float y1) {
    const float k = c.w / c.canvasW;
    int n = 0;
    for (int y = int((y0 - c.y0) * k); y < int((y1 - c.y0) * k); ++y) for (int x = int((x0 - c.x0) * k); x < int((x1 - c.x0) * k); ++x) {
        const uint8_t* p = &px[(size_t(y) * w + size_t(x)) * 4];
        if (p[0] > 160 && p[3] > 200) ++n;
    }
    return n;
}
void Save(const std::filesystem::path& folder, const std::string& name, const std::vector<uint8_t>& px, int w, int h) {
    std::vector<uint8_t> straight(px.size()), dark(px.size());
    for (size_t i = 0; i < px.size(); i += 4) {
        const float a = px[i + 3] / 255.f;
        // A dark backdrop like a dungeon floor (a soft vertical ramp), the layer over it the way the runtime composes it.
        const size_t y = i / 4 / size_t(w);
        const float bg[3] = {.07f + .05f * y / h, .06f + .04f * y / h, .05f + .03f * y / h};
        for (int k = 0; k < 3; ++k) {
            straight[i + k] = a > 0 ? uint8_t(std::min(255.f, px[i + k] / a + .5f)) : 0;
            dark[i + k] = uint8_t(std::min(255.f, px[i + k] + bg[k] * 255 * (1 - a) + .5f));
        }
        straight[i + 3] = px[i + 3]; dark[i + 3] = 255;
    }
    framevr::ringwheel::png::Save((folder / (name + ".png")).c_str(), straight, UINT(w), UINT(h));
    framevr::ringwheel::png::Save((folder / (name + "-on-dark.png")).c_str(), dark, UINT(w), UINT(h));
}
// match stage 1b (the design notes): the kit's busy stand-in ground (r8-match/busy-plate-1x.png, its grout transparent),
// put on black and premultiplied, 768 x 800 wheel px; empty when the file is missing.
std::vector<uint8_t> ReadPng(const std::filesystem::path& file, int& w, int& h);
std::vector<uint8_t> LoadPlate(const std::filesystem::path& file, int& w, int& h) {
    auto out = ReadPng(file, w, h);
    for (size_t i = 0; i < out.size(); i += 4) for (int k = 0; k < 3; ++k) out[i + k] = uint8_t(out[i + k] * out[i + 3] / 255);
    return out;
}
// A PNG file as RGBA8, straight alpha, tight rows; empty when it cannot be read.
std::vector<uint8_t> ReadPng(const std::filesystem::path& file, int& w, int& h) {
    std::vector<uint8_t> out; w = h = 0;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
    UINT fw = 0, fh = 0;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(conv->GetSize(&fw, &fh)))
        return out;
    out.resize(size_t(fw) * fh * 4);
    if (FAILED(conv->CopyPixels(nullptr, fw * 4, UINT(out.size()), out.data()))) { out.clear(); return out; }
    w = int(fw); h = int(fh);
    return out;
}
// A content-rect frame (premultiplied) over the plate at 1x (content px = wheel px + the content's offset): straight RGB.
std::vector<uint8_t> OverPlate(const std::vector<uint8_t>& px, int w, int h, const rw::ContentRect& c, const std::vector<uint8_t>& plate, int pw, int ph) {
    std::vector<uint8_t> out(px.size());
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const int wx = x + int(c.x0 - rw::CanvasX), wy = y + int(c.y0 - rw::CanvasY);
        const uint8_t* q = &px[(size_t(y) * w + size_t(x)) * 4];
        const uint8_t* b = wx >= 0 && wy >= 0 && wx < pw && wy < ph ? &plate[(size_t(wy) * pw + size_t(wx)) * 4] : nullptr;
        uint8_t* o = &out[(size_t(y) * w + size_t(x)) * 4];
        for (int k = 0; k < 3; ++k) o[k] = uint8_t(std::min(255.f, q[k] + (b ? b[k] : 0) * (1 - q[3] / 255.f) + .5f));
        o[3] = 255;
    }
    return out;
}
// A square crop around a canvas point (texels: size canvas px x the render scale), saved on the dark backdrop.
void SaveCrop(const std::filesystem::path& folder, const std::string& name, const std::vector<uint8_t>& px, const rw::ContentRect& c,
              float cx, float cy, float size) {
    const float k = c.w / c.canvasW;
    const int s = int(size * k), x0 = std::clamp(int((cx - c.x0) * k) - s / 2, 0, std::max(0, c.w - s)), y0 = std::clamp(int((cy - c.y0) * k) - s / 2, 0, std::max(0, c.h - s));
    const int w = std::min(s, c.w), h = std::min(s, c.h);
    std::vector<uint8_t> crop(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) std::memcpy(&crop[size_t(y) * w * 4], &px[(size_t(y0 + y) * c.w + size_t(x0)) * 4], size_t(w) * 4);
    Save(folder, name, crop, w, h);
}
// A picture draw: waits (up to 5 s) while the 3-slot ring is busy. The pass never waits by design (Busy is its answer to
// a full ring); under a loaded parallel ctest WARP can lag 3 composes behind, which is not what these checks are about.
rw::Compose::Result DrawWait(rw::Compose& c, ID3D12Resource* target, const rw::ContentRect& content, const rw::Instance* list, int n) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        const auto r = c.Draw(target, content, list, n);
        if (r != rw::Compose::Result::Busy || std::chrono::steady_clock::now() > end) return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
template <class F> bool PumpUntil(rw::Compose& c, F done, int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        c.Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}
std::string ReadAll(const std::filesystem::path& p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()}; }
// The design kit's data (local only): every skill's name and tree, and its short description and level.
struct KitSkill { std::string name, desc, req, tree; };
std::vector<KitSkill> ReadKit(const std::filesystem::path& kit) {
    std::map<std::string, std::pair<std::string, std::string>> tips;
    const std::string t = ReadAll(kit / "tooltips.js");
    const std::regex tip("\"([^\"]+)\":\\s*\\{\"short\":\\s*\"([^\"]*)\",\\s*\"req\":\\s*\"(\\d+)\"");
    for (std::sregex_iterator i(t.begin(), t.end(), tip), e; i != e; ++i) tips[(*i)[1]] = {(*i)[2], (*i)[3]};
    std::vector<KitSkill> out;
    // Each class block starts at its "sheet": its class name and three tree names come first, then its skills (name, tree).
    const std::regex name("name['\"]?\\s*:\\s*['\"]([^'\"]+)['\"]"), skill("\\[\\s*['\"]([^'\"]+)['\"]\\s*,\\s*([1-3])\\s*,\\s*\\d+");
    for (const char* f : {"skills.js", "skills-more.js"}) {
        const std::string s = ReadAll(kit / f);
        std::vector<size_t> starts;
        const std::regex sheet("\\bsheet['\"]?\\s*:");
        for (std::sregex_iterator i(s.begin(), s.end(), sheet), e; i != e; ++i) starts.push_back(s.rfind("name", size_t(i->position())));
        for (size_t c = 0; c < starts.size(); ++c) {
            const std::string block = s.substr(starts[c], (c + 1 < starts.size() ? starts[c + 1] : s.size()) - starts[c]);
            std::vector<std::string> names;
            for (std::sregex_iterator i(block.begin(), block.end(), name), e; i != e && names.size() < 4; ++i) names.push_back((*i)[1]);            for (std::sregex_iterator i(block.begin(), block.end(), skill), e; i != e; ++i) {
                KitSkill k; k.name = (*i)[1];
                const int tree = std::stoi((*i)[2]);
                k.tree = tree < int(names.size()) ? names[size_t(tree)] : "";
                if (auto it = tips.find(k.name); it != tips.end()) { k.desc = it->second.first; k.req = it->second.second; }
                out.push_back(k);
            }
        }
    }
    return out;
}
}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path out, iconDir, kit, frames, footerOut, r7cOut, matchOut, badgeOut, shotMs, ordersOut, t036Out, treeOut, glyphOut;
    bool fallback = false, hardware = false;
    std::filesystem::path sprites;   // stage 3a: a local spell sprite cache (prepare_spell_sprites.py --output <scratch>); none: level 4
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--fallback") fallback = true;   // perf 1b finding 1: time the no-fxc path (manual: its compile is too slow for ctest)
        if (a == "--out") out = argv[i + 1]; else if (a == "--icons") iconDir = argv[i + 1]; else if (a == "--kit") kit = argv[i + 1];
        else if (a == "--frames") frames = argv[i + 1];   // the animation layers' frames (contact sheets: make-sheets.py)
        else if (a == "--footer") footerOut = argv[i + 1];   // feedback 1 item 4: the footer at 5 and 6 tabs
        else if (a == "--r7c") r7cOut = argv[i + 1];   // R7c: every class's tree marks and the rune band
        else if (a == "--sprites") sprites = argv[i + 1];   // stage 3a: <folder>\sprites.ssp (never the game folder)
        else if (a == "--match") matchOut = argv[i + 1];   // match spec: every class on real icons (needs --icons, a local cache)
        else if (a == "--orders") ordersOut = argv[i + 1];   // the Commander orders' row on the glyph chooser (3 classes, with and without an army)
        else if (a == "--t036") t036Out = argv[i + 1];   // the five layers, the center in thirds, the Quick cast page
        else if (a == "--tree") treeOut = argv[i + 1];   // the Sorceress page's painted tree icons at 50, 100 and 200%, circle and square, drawn; saved there
        else if (a == "--glyph") glyphOut = argv[i + 1];   // Glyph icon: the Glyphs page and the center's sigils painted, circle and square, at 50-300%; saved there
        else if (a == "--badges") badgeOut = argv[i + 1];   // feedback 1 items 3 and 5: the button badges' 12 settings
        else if (a == "--shot-ms") shotMs = argv[i + 1];   // Job B, by hand: where a screenshot's ms go (writes its PNGs there)
        else if (a == "--adapter") hardware = std::string(argv[i + 1]) == "hardware";   // with --shot-ms: the machine's GPU, not WARP
    }
    // review-bug9 nit: --match judges real icons only; without --icons it used to run nothing and still say "all passed".
    if (!matchOut.empty() && iconDir.empty()) {
        std::printf("ring-wheel-preview: --match needs --icons <the d2r_vr_icons cache folder> (real skill icons); nothing run\n");
        return 2;
    }
    if (fallback) {   // Setup without the blob: PS + the placeholder effects; then FinishEffects (the worker's step)
        Fixture f(DXGI_FORMAT_R8G8B8A8_UNORM);
        rw::Compose cf;
        rw::ComposeForceCompile = true;
        const auto t0 = std::chrono::steady_clock::now();
        Check(cf.Setup(f.device.Get(), f.queue.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, Say), "fallback setup");
        const double setupMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        Check(cf.EffectsPending(), "fallback: the effects wait for their compile");
        const auto t1 = std::chrono::steady_clock::now();
        Check(cf.FinishEffects(Say) && !cf.EffectsPending(), "fallback: the effects compiled and swapped in");
        std::printf("fallback: setup %.0f ms (ring shown), effects %.0f ms later\n", setupMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count());
        if (failures) { std::printf("ring-wheel-preview: %d failed\n", failures); return 1; }
        return 0;
    }
    if (!shotMs.empty()) {   // Job B, by hand: where a screenshot's ms go. Synthetic icons, the test's made-up skills; nothing else runs.
        try {
            std::filesystem::create_directories(shotMs);
            const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;   // the picture's slower path (decode, divide, encode)
            Fixture f(format, hardware);
            std::printf("shot-ms: adapter %ls (%s)\n", f.adapter.c_str(), hardware ? "the machine's GPU" : "WARP, a software device: not the headset's number");
            rw::Fonts fonts;
            if (!rw::FontsFromFolder(RING_WHEEL_FONTS, fonts)) throw std::runtime_error("upstream's menu fonts");
            rw::Compose compose;
            rw::AtlasWorker worker;
            worker.SetFxTexCache({});
            worker.Start(&compose, f.device.Get(), f.queue.Get(), format, fonts, [](int id) { return SyntheticIcon(id); }, [] {}, Say);
            if (!PumpUntil(compose, [&] { return compose.Ready() || worker.Failed(); }, 60000) || !compose.Ready()) throw std::runtime_error("the compose pass's setup");
            const Data data = TestData();
            const auto seats = Lay(data, rw::TreeLayout::Shown, 150);
            worker.Want(seats.request);
            std::shared_ptr<const rw::AtlasWorker::Built> built;
            if (!PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == seats.request; }, 60000)) throw std::runtime_error("the atlases");
            rw::Scene s; s.open = true; s.layout = &seats.layout; s.anim = rw::PresetAnim(rw::Preset::Off); s.skillClass = 1;
            s.atlasPct = built->icons.scalePct; s.cells = built->icons.cellOf.data(); s.metrics = &built->metrics; s.initials = seats.initialPtr.data();
            for (int t = 0; t < rw::Trees; ++t) s.treeNames[size_t(t)] = data.trees[t];
            static rw::Instance list[rw::MaxDraw];
            const int n = rw::Build(s, list);
            const auto c = rw::Content(864, 150);   // the 864 footer: 1296 x 1320, the size of the 's five shots
            std::printf("shot-ms: the full ring, %d instances, %d x %d\n", n, c.w, c.h);
            rw::shot::SaveQueue saver;
            const int Shots = 9;   // the first is the cold one (the codec's and the driver's first use), listed apart
            std::vector<double> before, gpuPart, picture, png, frame, workerMs;
            for (int k = 0; k < Shots; ++k) {
                // As in play: this frame's compose is in flight when the screenshot comes.
                DrawWait(compose, f.target.Get(), c, list, n);
                double t0 = rw::shot::NowMs(), t1 = t0, t2 = t0, t3 = t0;
                {   // before Job B: all of it on the frame thread
                    rw::ShotRows rows; rw::shot::Raw raw;
                    const char* why = compose.Shot(c, list, n, 500, rows);
                    rows.Read(raw);
                    t1 = rw::shot::NowMs();
                    const auto rgba = rw::shot::Picture(raw);
                    t2 = rw::shot::NowMs();
                    const bool ok = !why && framevr::ringwheel::png::Save((shotMs / ("before-" + std::to_string(k) + ".png")).c_str(), rgba, UINT(raw.w), UINT(raw.h));
                    t3 = rw::shot::NowMs();
                    if (!ok) throw std::runtime_error(why ? why : "the old save's file");
                }
                DrawWait(compose, f.target.Get(), c, list, n);
                std::string line; double worker = 0;
                const double f0 = rw::shot::NowMs();
                auto rows = std::make_shared<rw::ShotRows>();
                const char* why = compose.Shot(c, list, n, 500, *rows);
                const bool pushed = !why && saver.Push([&, rows, f0, file = (shotMs / ("now-" + std::to_string(k) + ".png")).wstring()](const rw::shot::SaveQueue::task& t) {
                    rw::shot::Raw raw;
                    rows->Read(raw);
                    rw::shot::Finish(raw, file, f0, t, framevr::ringwheel::png::Save, [&](const char* text) { line = text; });
                    worker = rw::shot::NowMs() - t.begun;
                });
                const double f1 = rw::shot::NowMs();
                if (!pushed) throw std::runtime_error(why ? why : "the save queue");
                saver.Drain();
                std::printf("shot %d%s: before %.1f ms on the frame (GPU part and the rows %.2f, picture %.1f, PNG %.1f); now %.2f ms on the frame, %.1f ms on the worker\n    %s\n",
                            k, k ? "" : " (cold)", t3 - t0, t1 - t0, t2 - t1, t3 - t2, f1 - f0, worker, line.c_str());
                if (k) { before.push_back(t3 - t0); gpuPart.push_back(t1 - t0); picture.push_back(t2 - t1); png.push_back(t3 - t2); frame.push_back(f1 - f0); workerMs.push_back(worker); }
            }
            auto mid = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
            auto top = [](const std::vector<double>& v) { return *std::max_element(v.begin(), v.end()); };
            std::printf("shot-ms: %d warm shots, median (max): before %.1f (%.1f) ms on the frame = GPU part and rows %.2f (%.2f) + picture %.1f (%.1f) + PNG %.1f (%.1f);\n"
                        "         now %.2f (%.2f) ms on the frame, %.1f (%.1f) ms on the worker\n",
                        Shots - 1, mid(before), top(before), mid(gpuPart), top(gpuPart), mid(picture), top(picture), mid(png), top(png), mid(frame), top(frame), mid(workerMs), top(workerMs));
            worker.Retire([] {});
            worker.Stop();
            compose.WaitIdle(5000);
            compose.Release(5000);
        } catch (const std::exception& e) {
            std::printf("shot-ms: failed: %s\n", e.what());
            return 1;
        }
        return 0;
    }
    if (!frames.empty()) std::filesystem::create_directories(frames);
    if (!out.empty()) std::filesystem::create_directories(out);
    // ---- Pure: the content rectangle at each render scale ----
    {
        const auto a = rw::Content(768, 100), b = rw::Content(768, 125), c = rw::Content(1024, 150), d = rw::Content(864, 150);
        Check(a.w == 768 && a.h == 880 && a.x0 == 128 && a.y0 == 112, "#14 render 100%: 768 x 880 from canvas (128, 112)");
        Check(b.w == 960 && b.h == 1100, "#14 render 125%: 960 x 1100");
        Check(c.w == rw::SwapchainW && c.h == rw::SwapchainH && c.x0 == 0, "#14 render 150%, full-width footer: the whole 1536 x 1320 swapchain");
        Check(d.w == 1296 && d.h == 1320 && d.x0 == 80, "#14 render 150%, the 864 footer: 1296 x 1320 from canvas x 80");
        Check(rw::Content(768, 300).w == 1152 && rw::Content(2000, 150).w <= rw::SwapchainW, "#14 out-of-range scale and width clamp inside the swapchain");
        Check(rw::AtlasCells * rw::CellPx(150) <= rw::AtlasTexPx && rw::AtlasCells * rw::CellPx(100) <= rw::AtlasTexPx, "#15 81 cells fit the atlas at 100% and 150%");
    }
    const Data test = TestData();
    // ---- Pure: the instance caps and a closed wheel ----
    {
        rw::Instance list[rw::MaxDraw];
        const auto layout = rw::Build(test.in.data(), int(test.in.size()), rw::TreeLayout::Shown);
        rw::Scene closed; closed.layout = &layout; closed.open = false;
        Check(rw::Build(closed, list) == 0, "#14 a closed wheel builds zero instances");
        rw::Scene faded = closed; faded.open = true; faded.wheelAlpha = 0;
        Check(rw::Build(faded, list) == 0, "#14 a wheel faded out builds zero instances");
        std::vector<rw::SeatIn> full;
        for (int i = 0; i < 80; ++i) full.push_back({i, -1, i % 3});
        const auto big = rw::Build(full.data(), int(full.size()), rw::TreeLayout::Shown);
        rw::Scene busy; busy.open = true; busy.layout = &big; busy.step.under = {rw::Kind::Seat, 5};
        const int n = rw::Build(busy, list);
        Check(big.count == rw::MaxSeats && n <= rw::MaxInstances && n > rw::MaxSeats, "#14 64 seats pointed: within the 128 instances");
        Check(rw::Build(busy, list, 10) == 10, "#14 the cap holds at any size");
        // The design's sigil fix: the sigil fades by half the center's grow, gone at g 0.5 (r 108).
        auto sigil = [&](float g) {
            rw::Scene s; s.open = true; s.layout = &layout; s.grow = g;
            const int n = rw::Build(s, list);
            const float x = rw::CanvasCx + 128 * std::cos(layout.dir[0]), y = rw::CanvasCy + 128 * std::sin(layout.dir[0]);
            float a = 0;
            for (int i = 0; i < n; ++i) if (list[i].recipe == rw::RecipeRing && std::hypot(list[i].cx - x, list[i].cy - y) < 1) a = std::max(a, list[i].a);
            return a;
        };
        Check(sigil(0) > .99f && sigil(.25f) > .4f && sigil(.25f) < .6f && sigil(.5f) == 0 && sigil(1) == 0,
            "L1 R4 fix: a tree's sigil fades with the center's grow, gone by half of it");
    }
    // ---- Pure: when a frame composes (frame-budget section 2; item 3) ----
    {
        const auto layout = rw::Build(test.in.data(), int(test.in.size()), rw::TreeLayout::Shown);
        rw::Scene s; s.layout = &layout;
        auto frames = [&](rw::ComposePolicy& p, int n) { for (int i = 0; i < n; ++i) if (p.Want(s)) p.Done(true); };
        rw::ComposePolicy closed;
        frames(closed, 100);
        Check(closed.composes == 0, "#14 closed for 100 frames: zero composes");
        s.open = true; s.anim = rw::PresetAnim(rw::Preset::Off);
        rw::ComposePolicy still;
        frames(still, 100);
        Check(still.composes == 1 && still.reused == 99, "#14 Animation Off at rest: one compose, then the image re-submitted");
        s.step.under = {rw::Kind::Seat, 3};
        Check(still.Want(s), "#14 Animation Off: a change composes at once");
        still.Done(false);
        Check(still.Want(s) && still.busy == 1, "perf R4 #3: the ring was busy: the change stays due the next frame");
        still.Done(true);
        Check(!still.Want(s), "#14 Animation Off: composed, then rests");
        s.wheelAlpha = .5f;   // a field Build reads, changed alone
        Check(still.Want(s), "J5 Animation Off: the wheel's fade alone redraws");
        still.Done(true);
        s.step.pressed = {rw::Kind::Seat, 3};
        Check(still.Want(s), "J5 Animation Off: a press alone redraws");
        still.Done(true);
        s.step.picked = 4;   // (d)
        Check(still.Want(s), "J5 Animation Off: a pick alone redraws");
        still.Done(true);
        s.step.locked = true;
        Check(still.Want(s), "J5 Animation Off: the page lockout alone redraws");
        still.Done(true);
        s.revision = 9;   // seats or atlas rebuilt in place
        Check(still.Want(s), "K6 Animation Off: a data revision alone redraws");
        still.Done(true);
        s.holdFrac = .3f;
        Check(still.Want(s), "R6Q2 Animation Off: the hold ring starting redraws");
        still.Done(true);
        s.holdFrac = .31f;
        Check(still.Want(s), "R6Q2 Animation Off: the hold ring filling redraws");
        still.Done(true);
        s.holdFrac = -1;
        Check(still.Want(s), "R6Q2 Animation Off: the hold ending redraws");
        still.Done(true);
        s.step.picked = -1; s.step.locked = false; s.revision = 0;
        s.wheelAlpha = 1; s.step.pressed = {};
        if (still.Want(s)) still.Done(true);
        s.anim = rw::PresetAnim(rw::Preset::Hover);
        rw::ComposePolicy ambient;
        frames(ambient, 100);
        Check(ambient.composes >= 50 && ambient.composes <= 51, "#14 floating icons at rest: half the display rate");
        s.open = false;
        Check(!ambient.Want(s), "#14 a close composes nothing");
        s.open = true;
        Check(ambient.Want(s), "#14 a reopen composes at once (nothing drawn is kept across a close)");
    }
    // ---- Pure: the slot ring with a fake fence, the cost window ----
    {
        rw::SlotRing r;
        Check(r.Acquire(0) == 0, "#14 a fresh ring: slot 0");
        r.Submitted(0, 1); r.Submitted(r.Acquire(0), 2); r.Submitted(r.Acquire(0), 3);
        Check(r.Acquire(0) == -1 && r.skippedBusy == 1, "#14 three in flight, none done: busy, skipped and counted, no wait");
        Check(r.Acquire(1) == 0 && r.skippedBusy == 1, "#14 the oldest done: its slot again");
        rw::CostWindow w;
        for (int i = 1; i <= 100; ++i) w.Add(i * .01);
        Check(std::fabs(w.Mean() - .505) < 1e-6 && w.max == 1 && w.P95() >= .95 && w.P95() <= .97 && w.n == 100, "perf R4 #4: mean, max and p95 over every sample");
    }
    // ---- Text: the glyph atlas from upstream's fonts, clean strings, the blurb's shaped box ----
    rw::Fonts fonts;
    Check(rw::FontsFromFolder(RING_WHEEL_FONTS, fonts), "upstream's menu fonts read");
    tx::Metrics m100;
    {
        rw::AtlasImage glyphs; std::string error;
        const auto t = std::chrono::steady_clock::now();
        Check(rw::BuildGlyphs(fonts, 100, glyphs, m100, error), "#16 glyph atlas at 100%: " + error);
        std::printf("glyph atlas 100%%: %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count());
        Check(m100.ok && tx::Width(m100, tx::Name24, "WWW") > tx::Width(m100, tx::Name24, "iii") && tx::Width(m100, tx::Body22, " ") > 0,
            "#16 measured widths (wide letters wider, a space has an advance)");
        Check(tx::Width(m100, tx::Name24, "Skill") > tx::Width(m100, tx::Name22, "Skill"), "#16 24 px wider than 22 px");
        Check(m100.g[tx::Body22][size_t(tx::EllipsisSlot)].w > 0, "#16 the ellipsis has a glyph");
        Check(tx::Clean("\xC3\xBF" "c4Fire\n Bolt  ") == "Fire Bolt" && tx::Clean("\xFF" "c1Ice") == "Ice", "#16 color codes stripped (UTF-8 and Latin-1), line breaks to a space");
        auto fits = [&](const tx::Blurb& b) { return b.over.empty() && int(b.lines.size()) <= b.maxLines; };
        auto lines = [&](const tx::Blurb& b, tx::Kind k) { return int(std::count_if(b.lines.begin(), b.lines.end(), [&](const tx::Line& l) { return l.kind == k; })); };
        const auto shortOne = tx::LayBlurb(m100, {"Nova", "Second Tree Skills, level 12", "a short line", "Tap casts, hold assigns"});
        Check(fits(shortOne) && shortOne.lines[0].style == tx::Name24 && lines(shortOne, tx::KSub) == 1, "#16 a short blurb: the name at 24 px, every line kept");
        const auto longName = tx::LayBlurb(m100, {"Wwwwwww of the Heavens", "Second Tree Skills, level 30", "a short line", "Tap casts, hold assigns"});
        Check(fits(longName) && longName.lines[0].style == tx::Name22 && lines(longName, tx::KName) == 2 && lines(longName, tx::KSub) == 0,
            "#16 a long name: 22 px on two lines, the tree line dropped");
        if (failures) for (const auto& l : longName.lines) std::printf("  [%d %d] %s %.0f\n", int(l.kind), int(l.style), l.text.c_str(), tx::Width(m100, l.style, l.text));
        const std::string essay = "this description runs on and on with many more words than the center circle could ever hold in two short lines of text";
        const auto longDesc = tx::LayBlurb(m100, {"Nova", "Second Tree Skills, level 12", essay, "Tap casts, hold assigns"});
        const auto lastDesc = std::find_if(longDesc.lines.rbegin(), longDesc.lines.rend(), [](const tx::Line& l) { return l.kind == tx::KDesc; });
        Check(fits(longDesc) && lines(longDesc, tx::KDesc) == 2 && lastDesc != longDesc.lines.rend() && lastDesc->text.size() > 3 &&
            lastDesc->text.compare(lastDesc->text.size() - 3, 3, "\xE2\x80\xA6") == 0, "#16 a long description: two lines, words dropped, an ellipsis");
        Check(lines(longDesc, tx::KHelp) == 1, "#16 the help line always kept");
        tx::BlurbCache cache;
        for (int f = 0; f < 1000; ++f) cache.Get(m100, 7, {"Nova", "x", "y", "z"});
        Check(cache.layouts == 1, "#16 a 1000-frame hover on one skill: one layout");
        cache.Get(m100, 8, {"Ice Bolt", "x", "y", "z"});
        Check(cache.layouts == 2, "#16 another skill: one more layout");
        if (!kit.empty()) {   // local only: every skill of the kit's 8 classes
            const auto skills = ReadKit(kit);
            int bad = 0; std::string longest;
            for (const auto& s : skills) {
                const auto b = tx::LayBlurb(m100, {s.name, s.tree + ", level " + (s.req.empty() ? std::string("30") : s.req), s.desc, "Tap casts, hold assigns"});
                if (s.tree.empty() && bad++ < 3) std::printf("  no tree name: %s\n", s.name.c_str());
                if (!fits(b)) { if (bad++ < 3) std::printf("  over: %s: %s\n", s.name.c_str(), b.over.empty() ? "too many lines" : b.over[0].c_str()); }
                if (s.name.size() > longest.size()) longest = s.name;
            }
            std::printf("kit blurbs: %zu skills, longest name \"%s\", %d over\n", skills.size(), longest.c_str(), bad);
            Check(skills.size() > 200 && bad == 0, "#16 the kit's every skill fits the r 124 circle");
        }
    }
    // ---- WARP: the atlas worker, the slices and the compose pass ----
    try {
        Fixture f;
        // Eval #15: packing (pure CPU, the test's synthetic source): 81 fit, an 82nd is listed missing, a null icon too.
        {
            std::vector<int> ids;
            for (int i = 0; i < 83; ++i) ids.push_back(1000 + i);
            ids.push_back(-1);
            rw::AtlasImage image; rw::IconPack pack;
            rw::BuildIcons(ids.data(), int(ids.size()), 150, [](int id) { return id == 1005 ? nullptr : SyntheticIcon(id); }, [] {}, image, pack);
            Check(pack.wanted == 83 && pack.found == 81 && pack.missing.size() == 2 && pack.missing[0] == 1005 && pack.missing[1] == 1082,
                "#15 83 wanted, one icon absent: all 81 cells used, the absent one and the one past the grid listed missing");
            rw::BuildIcons(ids.data(), 10, 150, [](int id) { return id == 1005 ? nullptr : SyntheticIcon(id); }, [] {}, image, pack);
            Check(pack.found == 9 && pack.missing.size() == 1 && pack.missing[0] == 1005 && pack.cellOf[5] == -1 && pack.cellOf[6] >= 0,
                "#15 a missing icon is listed, its seat has no cell (the initials disc)");
            bool premul = true;
            for (size_t i = 0; i < image.data.size(); i += 4) if (image.data[i] > image.data[i + 3] || image.data[i + 1] > image.data[i + 3] || image.data[i + 2] > image.data[i + 3]) { premul = false; break; }
            Check(premul, "#15 the icon atlas (all mips) is premultiplied");
            // item 6: the fullest wheel and every page card, every class: all packed, inside the atlas
            for (int cls = 0; cls < 8; ++cls) {
                std::array<int, rw::MaxSeats + rw::ExtraIcons> full{};
                for (int i = 0; i < rw::MaxSeats; ++i) full[size_t(i)] = 5000 + i;
                const int total = rw::AddGesturePageIcons(cls, full.data(), rw::MaxSeats, int(full.size()));
                rw::BuildIcons(full.data(), total, 100, [](int id) { return SyntheticIcon(id); }, [] {}, image, pack);
                bool all = int(pack.cellOf.size()) == total;
                for (int16_t c : pack.cellOf) all = all && c >= 0 && c < rw::AtlasCells * rw::AtlasCells;
                Check(all && pack.found == total && pack.missing.empty(), "item 6 64 seats and class " + std::to_string(cls) + "'s page cards: every one has a cell (" + std::to_string(total) + " icons)");
            }
        }
        rw::Compose compose;
        rw::AtlasWorker worker;
        const bool real = !iconDir.empty();
        if (real) icons::Directory(iconDir.wstring());
        rw::IconSource source = real ? rw::IconSource([](int id) { return icons::Get("s" + std::to_string(id)); })
                                     : rw::IconSource([](int id) { return id == 105 ? nullptr : SyntheticIcon(id); });
        source = [inner = source](int id) {   //: the sub-menus' own pictures, from this exe's copy of the plugin's.rc (as the ring's worker)
            const int t = rw::menuart::ThirdOfArt(id);
            if (rw::treeart::SlotOfArt(id) >= 0) return rw::treeart::Picture(GetModuleHandleW(nullptr), id);   // a painted tree square, a cell of the sheet
            if (rw::glyphart::SlotOfArt(id) >= 0) return rw::glyphart::Picture(GetModuleHandleW(nullptr), id);   // Glyph icon: a painted glyph or sigil square, a cell of its sheet
            return t >= 0 ? rw::menuart::Get(GetModuleHandleW(nullptr), t).image : inner(id);
        };
        worker.SetFxTexCache(sprites.empty() ? std::filesystem::path{} : sprites / "sprites.ssp");   // never next to the exe
        worker.Start(&compose, f.device.Get(), f.queue.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, fonts, source, [real] { if (real) icons::Wait(3000); }, Say);
        Check(PumpUntil(compose, [&] { return compose.Ready() || worker.Failed(); }, 20000) && compose.Ready(), "compose setup on the worker");
        {   // The Commander orders' row on the glyph chooser: a Necromancer,
            // a Druid and an Amazon, each with and without a summon army, at the panel's real size. Checked every run;
            // saved with --orders <folder>. Which skills are dimmed is illustrative (every third needs a target here).
            namespace ia = framevr::interaction;
            if (!ordersOut.empty()) std::filesystem::create_directories(ordersOut);
            const struct { int cls; const char* name; } heroes[3] = {{2, "necromancer"}, {5, "druid"}, {0, "amazon"}};
            for (const auto& hero : heroes) for (int army = 1; army >= 0; --army) {
                const std::string name = std::string("orders-") + hero.name + (army ? "-army" : "-no-army");
                ia::Snapshot snap; snap.skillClass = hero.cls;
                int count = 0;
                std::vector<std::pair<int, std::string>> named;
                auto add = [&](int id, int owner, bool directed, const std::string& label) {
                    auto& x = snap.catalog[size_t(count++)]; x = {}; x.id = id; x.owner = owner; x.known = x.available = true; x.directed = directed;
                    std::snprintf(x.name, sizeof x.name, "%s", label.c_str()); named.push_back({id, label});
                };
                const auto& k = framevr::skillcategories::Classes[size_t(hero.cls)];
                for (int i = 0; i < 30; ++i) {
                    const int id = k.first + i, type = framevr::companions::SkillType(id);
                    if (!army && type > 0 && (framevr::companions::Bit(type) & framevr::companions::ArmyTypeMask)) continue;   // no army: its summons not learned
                    add(id, -1, i % 3 == 0, "Skill " + std::to_string(id));
                }
                add(0, -1, true, "Attack"); add(359, -1, true, "Town Portal"); add(217, -1, false, "Identify"); add(54, 7, false, "Item Skill");
                snap.skillCount = unsigned(count);
                Check(ia::HasSummonArmy(snap) == (army != 0), name + ": upstream's army test");
                std::array<rw::SeatIn, rw::MaxSeats> in{}; int n = 0;
                Check(rw::SeatsFrom(snap, in, n), name + ": seats");
                Data d;
                for (int i = 0; i < n; ++i) { d.in.push_back(in[size_t(i)]); std::string label; for (const auto& x : named) if (x.first == in[size_t(i)].id) label = x.second; d.names.push_back(label); }
                Seats seats = Lay(d, rw::TreeLayout::Shown, 150);
                seats.request.classId = hero.cls;
                if (ia::HasSummonArmy(snap)) seats.request.n = rw::AddOrderIcons(seats.request.ids.data(), seats.request.n, int(seats.request.ids.size()));
                worker.Want(seats.request);
                std::shared_ptr<const rw::AtlasWorker::Built> built;
                if (!PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == seats.request; }, 60000)) { Check(false, name + ": the atlases"); continue; }
                const auto choices = rw::GlyphChoices(snap);
                const rw::Layout cl = rw::ChooserLayout(seats.layout, choices);
                std::vector<uint8_t> dim(size_t(cl.count), 1); std::vector<int16_t> cells(size_t(cl.count), int16_t(-1));
                std::vector<std::string> letters(size_t(cl.count)); std::vector<const char*> letterPtr;
                int orderSeats = 0, firstOrder = -1;
                for (int i = 0; i < cl.count; ++i) {
                    const auto& seat = cl.seats[size_t(i)];
                    const bool order = rw::OrderSeat(cl, i);
                    if (order) { ++orderSeats; if (firstOrder < 0) firstOrder = i; }
                    if (rw::ChoiceOf(choices, seat.id, seat.owner)) dim[size_t(i)] = 0;
                    cells[size_t(i)] = int16_t(rw::CellOfSkill(seats.request.ids.data(), built->icons.cellOf.data(), seats.request.n, order ? rw::orders::IconSkill(seat.id) : seat.id));
                    if (order) letters[size_t(i)] = rw::orders::Initials(seat.id);
                    else for (int j = 0; j < seats.layout.count; ++j) if (seats.layout.seats[size_t(j)].id == seat.id) letters[size_t(i)] = seats.initials[size_t(j)];
                }
                for (const auto& x : letters) letterPtr.push_back(x.c_str());
                Check(orderSeats == (army ? rw::orders::Count : 0) && (army || cl.count == seats.layout.count), name + ": the 7 orders with an army, the main ring's seats without (" + std::to_string(orderSeats) + ")");
                rw::Scene m; m.open = true; m.layout = &cl; m.anim = rw::PresetAnim(rw::Preset::Off);
                m.atlasPct = built->icons.scalePct; m.cells = cells.data(); m.metrics = &built->metrics; m.initials = letterPtr.data();
                m.hasMotions = true; m.chooseTitle = "Triangle"; m.clearLine = "Triangle will cast nothing"; m.seatDim = dim.data(); m.skillClass = hero.cls;
                m.footer = {true, ia::MenuPage::Glyphs, false, false, true};
                for (int t = 0; t < rw::Trees; ++t) m.treeNames[size_t(t)] = framevr::skillcategories::Name(hero.cls, rw::PageAt(hero.cls, t) + 1);
                static rw::Instance list[rw::MaxDraw];
                const auto c = rw::Content(rw::FooterWidth(false, 5), 150);
                std::vector<uint8_t> restPx;
                for (int held = 0; held <= army; ++held) {   // at rest; with an army also a button held on an order
                    const int heldSeat = firstOrder + 3;
                    if (held) {   // as the game has it: the ray on the held order
                        m.holdFrac = .5f; m.holdSeat = heldSeat; m.holdSlot = "Right trigger"; m.holdTop = "Replaces"; m.holdStatus = "Bone Armor"; m.step.centerGrown = true;
                        m.step.under = {rw::Kind::Seat, heldSeat}; m.holdName = rw::orders::Short(cl.seats[size_t(heldSeat)].id);
                    }
                    const int count2 = rw::Build(m, list);
                    const std::string shot = name + (held ? "-hold" : "");
                    Check(count2 > 0 && count2 <= rw::MaxDraw && DrawWait(compose, f.target.Get(), c, list, count2) == rw::Compose::Result::Composed, shot + ": composed");
                    const auto px = f.Read(c.w, c.h);
                    int lit = 0; float x0 = 1e9f, x1 = -1e9f;
                    for (int i = 0; i < cl.count; ++i) if (rw::OrderSeat(cl, i)) { const auto& seat = cl.seats[size_t(i)]; lit += At(px, c.w, rw::CanvasX + seat.x, rw::CanvasY + seat.y, c, 3) > 100; x0 = std::min(x0, seat.x); x1 = std::max(x1, seat.x); }
                    Check(lit == orderSeats, shot + ": every order drawn on the row (" + std::to_string(lit) + " of " + std::to_string(orderSeats) + ")");
                    if (army) {
                        const float lx = rw::CanvasX + (x0 + x1) / 2, ly = rw::CanvasY + rw::ShelfY - rw::orders::RowLabelDy;
                        Check(Bright(px, c.w, c, lx - 30, ly - 9, lx + 30, ly + 9) > 8, shot + ": the row's small ARMY name over the orders");
                    }
                    Check(Bright(px, c.w, c, rw::CanvasCx - 200, rw::CanvasCy - 335, rw::CanvasCx + 200, rw::CanvasCy - 290) > 60, shot + ": the chooser's title on the top arc");
                    if (army && !held) restPx = px;
                    if (held && restPx.size() == px.size()) {   // the hold picture differs from the rest one in the row, at the held order only
                        const auto& hs = cl.seats[size_t(heldSeat)];
                        const float k = c.w / c.canvasW, ringR = rw::IconPx * 1.3f / 2 + 3;   // the 3 px hover ring's middle
                        auto changed = [&](float from, float to) {
                            int count = 0;
                            for (int y = int((rw::CanvasY + rw::ShelfY - 34 - c.y0) * k); y < int((rw::CanvasY + rw::ShelfY + 34 - c.y0) * k); ++y)
                                for (int x = int((rw::CanvasX + from - c.x0) * k); x < int((rw::CanvasX + to - c.x0) * k); ++x) {
                                    const size_t at = (size_t(y) * size_t(c.w) + size_t(x)) * 4;
                                    count += std::abs(int(px[at]) - int(restPx[at])) > 24 || std::abs(int(px[at + 3]) - int(restPx[at + 3])) > 24;
                                }
                            return count;
                        };
                        const float halo = (rw::IconPx * 1.3f + 32) / 2 + 1;   // the marked seat's dark halo reaches its neighbors' edge
                        const int own = changed(hs.x - 34, hs.x + 34), others = changed(x0 - 34, hs.x - halo) + changed(hs.x + halo, x1 + 34);
                        Check(own > 400 && others == 0, shot + ": the held order is marked, the six others are as at rest past its dark halo (" + std::to_string(own) + " changed px on it, " + std::to_string(others) + " on the others)");
                        const int ringOn = At(px, c.w, rw::CanvasX + hs.x + ringR, rw::CanvasY + hs.y, c, 0), ringOff = At(restPx, c.w, rw::CanvasX + hs.x + ringR, rw::CanvasY + hs.y, c, 0);
                        Check(ringOn > 200 && ringOff < 120, shot + ": the hover ring around the held order, 1.3 x out (" + std::to_string(ringOn) + " against " + std::to_string(ringOff) + " at rest)");
                        const float nameW = tx::Width(built->metrics, tx::Body22, m.holdName) + 5;
                        const int nameOn = Bright(px, c.w, c, rw::CanvasCx - 104, rw::CanvasCy + rw::HoldNameY - 12, rw::CanvasCx + 104, rw::CanvasCy + rw::HoldNameY + 12);
                        Check(nameOn > 150 && nameW <= 207, shot + ": the held order's short name in the center (" + std::to_string(nameOn) + " bright px, " + std::to_string(int(nameW)) + " px wide of 207)");
                    }
                    std::printf("%s: %d instances, %d seats\n", shot.c_str(), count2, cl.count);
                    if (!ordersOut.empty()) Save(ordersOut, shot, px, c.w, c.h);
                }
                float widest = 0;   // every order's short name fits the hold's center line
                for (int id = rw::orders::First; id <= rw::orders::Last; ++id) widest = std::max(widest, tx::Width(built->metrics, tx::Body22, rw::orders::Short(id)) + 5);
                Check(widest <= 207, name + ": the widest short name fits the hold's line (" + std::to_string(widest) + " of 207 px)");
                int allFour = 0; std::string limited;   // how many orders show all four parts
                if (army && hero.cls == 2) for (int j = 0; j < rw::orders::Count; ++j) {   // each order pointed, its blurb whole
                    const int at = firstOrder + j, id = cl.seats[size_t(at)].id;
                    std::string slug = rw::orders::Short(id);
                    for (auto& ch : slug) ch = ch == ' ' ? '-' : char(std::tolower(static_cast<unsigned char>(ch)));
                    const std::string shot = "orders-pointed-" + std::to_string(j + 1) + "-" + slug;
                    const auto words = tx::WholeBlurb(built->metrics, {rw::orders::Label(id), rw::orders::Sub(), rw::orders::Help(id), rw::orders::ChooserAction()}, rw::orders::Short(id));
                    const tx::Blurb blurb = tx::LayBlurb(built->metrics, words);
                    auto of = [&](tx::Kind kind) { std::string t; for (const auto& l : blurb.lines) if (l.kind == kind) t += (t.empty() ? "" : " ") + l.text; return t; };
                    const std::string help = rw::orders::Help(id);
                    std::printf("%s: %zu lines, title \"%s\"\n", shot.c_str(), blurb.lines.size(), words.name.c_str());
                    for (size_t li = 0; li < blurb.lines.size(); ++li) { const auto& l = blurb.lines[li]; std::printf("    [%d] %s (%.1f of %.1f px)\n", int(l.kind), l.text.c_str(), tx::Width(built->metrics, l.style, l.text) + 5, tx::Chord(int(li), int(blurb.lines.size()))); }
                    Check(blurb.over.empty() && blurb.lines.size() <= 8 && blurb.maxLines == 8, shot + ": the blurb fits the circle in 8 lines or fewer (" + std::to_string(blurb.lines.size()) + (blurb.over.empty() ? std::string(")") : ", " + blurb.over[0] + ")"));
                    Check(of(tx::KName) == words.name && (words.name == rw::orders::Label(id) || words.name == rw::orders::Short(id)), shot + ": the name, whole (" + of(tx::KName) + ")");
                    // The line under the name shows wherever all four parts can fit with the short title; where they cannot at any
                    // line breaking, it is the one that goes (never the help line).
                    tx::BlurbText four{rw::orders::Short(id), rw::orders::Sub(), help, rw::orders::ChooserAction()}; four.whole = true;
                    const tx::Blurb fourLaid = tx::LayBlurb(built->metrics, four);
                    if (fourLaid.over.empty()) { ++allFour; Check(of(tx::KSub) == rw::orders::Sub(), shot + ": the line under the name kept (" + of(tx::KSub) + ")"); }
                    else {
                        limited += (limited.empty() ? "" : ", ") + std::string(rw::orders::Short(id)) + " (" + std::to_string(fourLaid.lines.size()) + " lines with it)";
                        Check(of(tx::KSub).empty() && words.name == rw::orders::Short(id), shot + ": all four parts do not fit: the line under the name goes, the short title stays");
                    }
                    Check(!help.empty() && of(tx::KDesc) == help && help.find("\xE2\x80\xA6") == std::string::npos, shot + ": the help line word for word, no ellipsis (" + of(tx::KDesc) + ")");
                    Check(of(tx::KHelp) == rw::orders::ChooserAction(), shot + ": the action line kept (" + of(tx::KHelp) + ")");
                    rw::Scene p = m;
                    p.holdFrac = -1; p.holdSeat = -1; p.holdSlot = p.holdTop = p.holdStatus = p.holdName = nullptr;
                    p.step.under = {rw::Kind::Seat, at}; p.step.blurb = at; p.step.centerGrown = true; p.blurb = &blurb;
                    const int count3 = rw::Build(p, list);
                    Check(count3 > 0 && count3 <= rw::MaxDraw && DrawWait(compose, f.target.Get(), c, list, count3) == rw::Compose::Result::Composed, shot + ": composed");
                    const auto px = f.Read(c.w, c.h);
                    const float half = float(blurb.lines.size()) * tx::LineH / 2 + 4;
                    Check(Bright(px, c.w, c, rw::CanvasCx - 124, rw::CanvasCy - half, rw::CanvasCx + 124, rw::CanvasCy + half) > 900, shot + ": the blurb drawn in the center");
                    Check(Bright(px, c.w, c, rw::CanvasCx - 124, rw::CanvasCy - 124, rw::CanvasCx + 124, rw::CanvasCy - half - 6) == 0 && Bright(px, c.w, c, rw::CanvasCx - 124, rw::CanvasCy + half + 6, rw::CanvasCx + 124, rw::CanvasCy + 124) == 0,
                          shot + ": no text above or below its lines");
                    if (!ordersOut.empty()) Save(ordersOut, shot, px, c.w, c.h);
                }
                if (army && hero.cls == 2) {
                    std::printf("orders blurbs: %d of %d show all four parts%s%s\n", allFour, rw::orders::Count, limited.empty() ? "" : "; without the line under the name: ", limited.c_str());
                    // The design's rule (gate D3): all four parts for all seven. The longer action line left Hold Ground at 9 lines.
                    Check(allFour == rw::orders::Count && limited.empty(), "orders blurbs: every order shows its name, the line under it, the whole help line and the action line (" + std::to_string(allFour) + " of 7" + (limited.empty() ? "" : "; not: " + limited) + ")");
                }
            }
            (void)compose.TakeWindow();   // these pictures are not in the cost window the check below reads
        }
        {   // (ring > Ring wheel > Layers > Tree icon): a Sorceress page with the tree marks painted, circle
            // and square, at 50, 100 and 200%, then Drawn at 100% and Hidden, at the panel's real size. Checked every run; saved with --tree <folder>.
            // The squares: the built sheet's (tools/ring_tree_sheet.py) when it holds any; else a stand-in sheet of three flat-colored plates
            // (the proof of the drawing, not of the art: its file names say stand-in).
            namespace ta = framevr::ringwheel::treeart;
            if (!treeOut.empty()) std::filesystem::create_directories(treeOut);
            const bool standIn = ta::PaintedCount() == 0;
            if (standIn) {
                auto sheet = std::make_shared<framevr::interaction::icons::Image>();
                sheet->width = ta::SheetW; sheet->height = ta::SheetH; sheet->rgba.assign(size_t(ta::SheetW) * ta::SheetH * 4, 0);
                const uint8_t body[3][3] = {{214, 96, 30}, {236, 206, 70}, {120, 190, 235}};   // flame-ish, bolt-ish, ice-ish
                for (int k = 0; k < 3; ++k) {
                    int x0, y0; ta::CellOrigin(3 + k, x0, y0);
                    for (int y = 0; y < ta::CellPx; ++y) for (int x = 0; x < ta::CellPx; ++x) {
                        const float u = (x + .5f) / ta::CellPx - .5f, v = (y + .5f) / ta::CellPx - .5f;
                        const bool motif = std::hypot(u, v) < .26f;   // inside the middle 80%
                        uint8_t* p = &sheet->rgba[(size_t(y0 + y) * ta::SheetW + size_t(x0 + x)) * 4];
                        const uint8_t plate[3] = {140, 82, 58};   // borderless full-bleed rust
                        for (int c = 0; c < 3; ++c) p[c] = motif ? body[k][c] : plate[c];
                        p[3] = 255;
                    }
                }
                ta::SetSheet(sheet); ta::SetMask(0b111000);
            }
            namespace ia = framevr::interaction;
            const int cls = 1;   // the Sorceress (the pilot class; the stand-in sheet paints her too)
            const auto& k = framevr::skillcategories::Classes[size_t(cls)];
            Data d;
            for (int i = 0; i < 30; ++i) { d.in.push_back({k.first + i, -1, k.pages[i] - '1'}); d.names.push_back("Skill " + std::to_string(k.first + i)); }
            Seats st = Lay(d, rw::TreeLayout::Shown, 100);
            st.request.classId = cls;
            st.request.n = ta::AddIcons(ta::Mode::Circle, 100, cls, st.request.ids.data(), st.request.n, int(st.request.ids.size()));
            worker.Want(st.request);
            std::shared_ptr<const rw::AtlasWorker::Built> built;
            if (!PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == st.request; }, 60000)) Check(false, "the atlases");
            else {
                const auto c = rw::Content(rw::FooterWidth(false, 5), 100);
                std::array<int16_t, rw::Trees> cells{-1, -1, -1};
                for (int t = 0; t < rw::Trees; ++t) { const int slot = ta::Slot(cls, t); cells[size_t(t)] = ta::Painted(slot) ? int16_t(rw::CellOfSkill(st.request.ids.data(), built->icons.cellOf.data(), st.request.n, ta::ArtId(slot))) : int16_t(-1); }
                Check(cells[0] >= 0 && cells[1] >= 0 && cells[2] >= 0 || !standIn, "the Sorceress's three squares each have an atlas cell");
                auto shot = [&](ta::Mode mode, int pct, const std::string& name) {
                    rw::Scene s; s.open = true; s.layout = &st.layout; s.atlasPct = built->icons.scalePct; s.cells = built->icons.cellOf.data(); s.metrics = &built->metrics;
                    s.initials = st.initialPtr.data(); s.skillClass = cls; s.hasMotions = true; s.tMs = 4000; s.anim = rw::PresetAnim(rw::Preset::Off);
                    s.footer = {true, ia::MenuPage::Spells, false, false, true};
                    for (int t = 0; t < rw::Trees; ++t) s.treeNames[size_t(t)] = framevr::skillcategories::Name(cls, rw::PageAt(cls, t) + 1);
                    s.treeMode = int(mode); s.treeSizePct = pct; s.treeCell = cells;
                    static rw::Instance list[rw::MaxDraw];
                    const int n = rw::Build(s, list);
                    Check(n > 0 && n <= rw::MaxDraw && DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "" + name + ": composed");
                    auto px = f.Read(c.w, c.h);
                    if (!treeOut.empty()) Save(treeOut, std::string("tree-") + (standIn ? "standin-" : "") + name, px, c.w, c.h);
                    // the mark's own pixels: opaque ones in a box round the first tree's mark (the 14 px mark at 100%)
                    float mx = rw::CanvasCx + 128 * std::cos(st.layout.dir[0]), my = rw::CanvasCy + 128 * std::sin(st.layout.dir[0]);
                    int lit = 0; const float half = 14.f * std::max(1.f, pct / 100.f) + 6;
                    for (float dy = -half; dy <= half; dy += 1) for (float dx = -half; dx <= half; dx += 1) lit += At(px, c.w, mx + dx, my + dy, c, 3) > 128;
                    std::printf("%s: %d instances, %d opaque px at the first mark\n", name.c_str(), n, lit);
                    return lit;
                };
                const int c50 = shot(ta::Mode::Circle, 50, "circle-50"), c100 = shot(ta::Mode::Circle, 100, "circle-100"), c200 = shot(ta::Mode::Circle, 200, "circle-200"), c300 = shot(ta::Mode::Circle, 300, "circle-300");
                const int s100 = shot(ta::Mode::Square, 100, "square-100"), dr = shot(ta::Mode::Drawn, 100, "drawn-100"), hid = shot(ta::Mode::Circle, 0, "hidden");
                shot(ta::Mode::Square, 50, "square-50"); shot(ta::Mode::Square, 200, "square-200");
                Check(c50 > 40 && c100 > c50 * 2 && c200 > c100 * 2 && c300 > c200, "circles: 50, 100 and 200% grow (opaque px at the mark)");
                Check(s100 > c100, "square: fills its corners, more px than the circle at 100%");
                Check(dr > 100 && hid < dr / 4, "Drawn is today's mark; Hidden leaves almost nothing at its place");
            }
            if (standIn) { ta::SetMask(0); ta::SetSheet(nullptr); }
        }
        {   // Glyph icon (ring > Ring wheel > Center > Glyph icon): the Glyphs page's eight shapes and the main
            // page's center sigils (star, hand) drawn, painted circle at 50 / 100 / 200 / 300% and painted square. Checked every run; saved with
            // --glyph <folder>. The squares: the built sheet's (tools/ring_glyph_sheet.py) when it holds any; else a stand-in sheet of twelve
            // flat rust plates with a silver disc (the proof of the drawing, not of the art: its file names say stand-in).
            namespace ga = framevr::ringwheel::glyphart;
            namespace ia = framevr::interaction;
            if (!glyphOut.empty()) std::filesystem::create_directories(glyphOut);
            const bool standIn = ga::PaintedCount() == 0;
            if (standIn) {
                auto sheet = std::make_shared<ga::Image>();
                sheet->width = ga::SheetW; sheet->height = ga::SheetH; sheet->rgba.assign(size_t(ga::SheetW) * ga::SheetH * 4, 0);
                for (int slot = 0; slot < ga::Count; ++slot) {
                    int x0, y0; ga::CellOrigin(slot, x0, y0);
                    for (int y = 0; y < ga::CellPx; ++y) for (int x = 0; x < ga::CellPx; ++x) {
                        const float u = (x + .5f) / ga::CellPx - .5f, v = (y + .5f) / ga::CellPx - .5f;
                        const bool motif = std::hypot(u, v) < .22f;   // inside the middle 80%
                        uint8_t* p = &sheet->rgba[(size_t(y0 + y) * ga::SheetW + size_t(x0 + x)) * 4];
                        const uint8_t plate[3] = {140, 82, 58}, silver[3] = {206, 210, 214};
                        for (int c = 0; c < 3; ++c) p[c] = motif ? silver[c] : plate[c];
                        p[3] = 255;
                    }
                }
                ga::SetSheet(sheet); ga::SetMask((1u << ga::Count) - 1);
            }
            const int cls = 1;
            const auto& k = framevr::skillcategories::Classes[size_t(cls)];
            Data d;
            for (int i = 0; i < 30; ++i) { d.in.push_back({k.first + i, -1, k.pages[i] - '1'}); d.names.push_back("Skill " + std::to_string(k.first + i)); }
            Seats st = Lay(d, rw::TreeLayout::Shown, 100);
            st.request.classId = cls;
            st.request.n = ga::AddIcons(ga::Mode::Circle, st.request.ids.data(), st.request.n, int(st.request.ids.size()));
            worker.Want(st.request);
            std::shared_ptr<const rw::AtlasWorker::Built> built;
            if (!PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == st.request; }, 60000)) Check(false, "glyph icon: the atlases");
            else {
                const auto c = rw::Content(rw::FooterWidth(false, 5), 150);
                ga::View view; view.sizePct = 100;
                for (int slot = 0; slot < ga::Count; ++slot) view.cell[size_t(slot)] = ga::Painted(slot) ? int16_t(rw::CellOfSkill(st.request.ids.data(), built->icons.cellOf.data(), st.request.n, ga::ArtId(slot))) : int16_t(-1);
                int cellsIn = 0; for (const int16_t cell : view.cell) cellsIn += cell >= 0;
                Check(cellsIn == ga::PaintedCount(), "glyph icon: every painted slot has an atlas cell (" + std::to_string(cellsIn) + " of " + std::to_string(ga::PaintedCount()) + ")");
                auto sets = ia::NoGlyphs; sets[0][0] = k.first; sets[0][2] = k.first + 4; sets[0][5] = k.first + 9;
                auto cellOf = [&](int id) { return rw::CellOfSkill(st.request.ids.data(), built->icons.cellOf.data(), st.request.n, id); };
                rw::PageView pv;
                rw::FillGlyphs(pv, sets, 0, [&](int id) { return "Skill " + std::to_string(id); }, cellOf);
                // The page shot: the glyph shape 0 (the circle) at its ring place; its middle is hollow when drawn, the picture's disc when painted.
                auto page = [&](ga::Mode mode, int pct, const std::string& name) {
                    rw::Scene s; s.open = true; s.anim = rw::PresetAnim(rw::Preset::Off); s.tMs = 2000; s.atlasPct = built->icons.scalePct; s.metrics = &built->metrics;
                    s.glyph = view; s.glyph.mode = int(mode); s.glyph.sizePct = pct;
                    static rw::Instance list[rw::MaxDraw];
                    const int n = rw::BuildPage(s, pv, list);
                    Check(n > 0 && n <= rw::MaxDraw && DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "glyph icon page " + name + ": composed");
                    auto px = f.Read(c.w, c.h);
                    if (!glyphOut.empty()) Save(glyphOut, std::string("glyphs-") + (standIn ? "standin-" : "") + name, px, c.w, c.h);
                    float x, y; rw::RingPos(0, pv.count, rw::GlyphR, x, y);
                    const int mid = At(px, c.w, rw::CanvasX + x, rw::CanvasY + y, c, 0) + At(px, c.w, rw::CanvasX + x, rw::CanvasY + y, c, 2);   // R + B at the face's middle
                    std::printf("glyph icon page %s: %d instances, middle R+B %d\n", name.c_str(), n, mid);
                    return mid;
                };
                const int dr = page(ga::Mode::Drawn, 100, "drawn"), c50 = page(ga::Mode::Circle, 50, "circle-50"), c100 = page(ga::Mode::Circle, 100, "circle-100");
                const int c200 = page(ga::Mode::Circle, 200, "circle-200"), c300 = page(ga::Mode::Circle, 300, "circle-300"), s100 = page(ga::Mode::Square, 100, "square-100");
                if (standIn)   // the stand-in's motif is a solid silver disc in the middle
                    Check(dr < 150 && c100 > 300 && c200 > 300 && c50 > 300 && c300 > 300 && s100 > 300, "glyph icon: the drawn circle is hollow in the middle, every painted one shows its silver disc there");
                else if (cellsIn > 0)   // real art: the motif is not a disc, but the face's middle is the picture's, not the drawn circle's hollow
                    Check(c100 > dr + 20 && c200 > dr + 20 && s100 > dr + 20, "glyph icon: real art in the face's middle where the drawn circle is hollow");
                // The center's sigils on the main page's split (the Glyphs half's star, the Gestures half's hand): drawn, painted circle and square at 200%.
                auto mainPage = [&](ga::Mode mode, int pct, const std::string& name) {
                    rw::Scene s; s.open = true; s.layout = &st.layout; s.atlasPct = built->icons.scalePct; s.cells = built->icons.cellOf.data(); s.metrics = &built->metrics;
                    s.initials = st.initialPtr.data(); s.skillClass = cls; s.hasMotions = true; s.tMs = 4000; s.anim = rw::PresetAnim(rw::Preset::Off);
                    s.footer = {true, ia::MenuPage::Spells, false, false, true};
                    for (int t = 0; t < rw::Trees; ++t) s.treeNames[size_t(t)] = framevr::skillcategories::Name(cls, rw::PageAt(cls, t) + 1);
                    s.glyph = view; s.glyph.mode = int(mode); s.glyph.sizePct = pct;
                    static rw::Instance list[rw::MaxDraw];
                    const int n = rw::Build(s, list);
                    Check(n > 0 && n <= rw::MaxDraw && DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "glyph icon main " + name + ": composed");
                    if (!glyphOut.empty()) Save(glyphOut, std::string("main-") + (standIn ? "standin-" : "") + name, f.Read(c.w, c.h), c.w, c.h);
                    std::printf("glyph icon main %s: %d instances\n", name.c_str(), n);
                };
                mainPage(ga::Mode::Drawn, 100, "drawn"); mainPage(ga::Mode::Circle, 100, "circle-100"); mainPage(ga::Mode::Circle, 200, "circle-200"); mainPage(ga::Mode::Square, 200, "square-200");
            }
            if (standIn) { ga::SetMask(0); ga::SetSheet(nullptr); }
        }
        {   // the five layers one at a time, the center in
            // thirds (rest, each third pointed, grow Off, dim Off and Hidden, a bound Quick cast badge, Quick cast Off; the Quick cast page
            // went). A Sorceress with the three sub-menu entries on the General row. Checked every run; saved with --t036 <folder>
            // (with --icons the player's own icons: keep those pictures private).
            namespace ia = framevr::interaction;
            if (!t036Out.empty()) std::filesystem::create_directories(t036Out);
            const int cls = 1;
            const auto& k = framevr::skillcategories::Classes[size_t(cls)];
            Data d;
            for (int i = 0; i < 30; ++i) { d.in.push_back({k.first + i, -1, k.pages[i] - '1'}); d.names.push_back("Skill " + std::to_string(k.first + i)); }
            for (int t = 0; t < rw::ThirdCount; ++t) { d.in.push_back({rw::MenuEntryId(t), -1, -1}); d.names.push_back(rw::MenuEntryName[t]); }
            Seats st = Lay(d, rw::TreeLayout::Shown, 100);
            st.request.classId = cls;
            // the sub-menu entries ask for their own pictures by their ids (as the ring asks), the thirds use the same cells
            worker.Want(st.request);
            std::shared_ptr<const rw::AtlasWorker::Built> built;
            if (!PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == st.request; }, 60000)) Check(false, "the atlases");
            else {
                std::vector<uint8_t> el(size_t(st.layout.count)), mark(size_t(st.layout.count), 0);
                for (int i = 0; i < st.layout.count; ++i) {
                    el[size_t(i)] = uint8_t(rw::ElementOf(cls, st.layout.seats[size_t(i)].tree));
                    if (const int t = rw::MenuOfEntry(st.layout.seats[size_t(i)].id); t >= 0) mark[size_t(i)] = uint8_t(1 + t);
                }
                const auto c = rw::Content(rw::FooterWidth(false, 5), 100);
                auto base = [&] {
                    rw::Scene s; s.open = true; s.layout = &st.layout; s.atlasPct = built->icons.scalePct; s.cells = built->icons.cellOf.data(); s.metrics = &built->metrics;
                    s.initials = st.initialPtr.data(); s.elements = el.data(); s.seatMark = mark.data(); s.skillClass = cls; s.hasMotions = true; s.tMs = 4000;
                    s.anim = rw::PresetAnim(rw::Preset::Hover); s.anim.floats = false;   // a still picture: the floats' drift is Float's own test
                    s.layers = {true, true, false, false}; s.thirds = true;   // the ring's defaults: Element frame and Animation Off
                    for (int t = 0; t < rw::ThirdCount; ++t) s.thirdCell[size_t(t)] = int16_t(rw::CellOfSkill(st.request.ids.data(), built->icons.cellOf.data(), st.request.n, rw::menuart::ArtId(t)));
                    s.footer = {true, ia::MenuPage::Spells, false, false, true};
                    for (int t = 0; t < rw::Trees; ++t) s.treeNames[size_t(t)] = framevr::skillcategories::Name(cls, rw::PageAt(cls, t) + 1);
                    return s;
                };
                auto draw = [&](const rw::Scene& s, const std::string& name) {
                    static rw::Instance list[rw::MaxDraw];
                    const int n = rw::Build(s, list);
                    Check(n > 0 && n <= rw::MaxDraw && DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "" + name + ": composed");
                    auto px = f.Read(c.w, c.h);
                    if (!t036Out.empty()) Save(t036Out, name, px, c.w, c.h);
                    std::printf("%s: %d instances\n", name.c_str(), n);
                    return px;
                };
                // A seat of the first tree (a fire skill): its middle (the art), its ring (r 22-25 at 100%) and the frame's band (r 27-34)
                int seat = -1;
                for (int i = 0; i < st.layout.count && seat < 0; ++i) if (st.layout.seats[size_t(i)].tree == 0) seat = i;
                const float sx = rw::CanvasX + st.layout.seats[size_t(seat)].x, sy = rw::CanvasY + st.layout.seats[size_t(seat)].y;
                auto band = [&](const std::vector<uint8_t>& px, float r0, float r1) {   // the mean alpha in a ring round the seat
                    double sum = 0; int n = 0;
                    for (float a = 0; a < 2 * rw::Pi; a += .05f) for (float r = r0; r <= r1; r += 1) { sum += At(px, c.w, sx + r * std::cos(a), sy + r * std::sin(a), c, 3); ++n; }
                    return sum / std::max(n, 1);
                };
                auto middle = [&](const std::vector<uint8_t>& px) { double sum = 0; int n = 0; for (int y = -8; y <= 8; ++y) for (int x = -8; x <= 8; ++x) { sum += At(px, c.w, sx + x, sy + y, c, 3); ++n; } return sum / n; };
                const auto def = draw(base(), "t036-layers-1-default");
                auto off = [&](auto edit, const std::string& name) { auto s = base(); edit(s.layers); return draw(s, name); };
                const auto noIcon = off([](rw::SeatLayers& l) { l.icon = false; }, "t036-layers-2-icon-off");
                const auto noRing = off([](rw::SeatLayers& l) { l.ring = false; }, "t036-layers-3-color-ring-off");
                const auto frame = off([](rw::SeatLayers& l) { l.frame = true; }, "t036-layers-4-element-frame-on");
                const auto fx = off([](rw::SeatLayers& l) { l.fx = true; }, "t036-layers-5-animation-on");
                const auto all = off([](rw::SeatLayers& l) { l.frame = l.fx = true; }, "t036-layers-6-all-on");
                const auto none = off([](rw::SeatLayers& l) { l.icon = l.ring = l.frame = l.fx = false; }, "t036-layers-7-all-off");
                std::printf("seat %d: middle alpha %.0f (Icon Off %.0f), ring %.0f (Color ring Off %.0f), band %.0f (Element frame On %.0f)\n",
                            seat, middle(def), middle(noIcon), band(def, 22, 25), band(noRing, 22, 25), band(def, 28, 33), band(frame, 28, 33));
                Check(middle(def) > 200 && middle(noIcon) < 20, "Icon Off: the seat's middle is empty (its disc and art gone)");
                Check(band(def, 22, 25) > band(noRing, 22, 25) + 40, "Color ring Off: the ring's band loses its ring");
                Check(band(frame, 28, 33) > band(def, 28, 33) + 20 && band(def, 28, 33) < 30, "Element frame On: its band outside the ring, nothing there Off");
                Check(std::abs(middle(frame) - middle(def)) < 3 && std::abs(band(noIcon, 22, 25) - band(def, 22, 25)) < 25, "one layer's switch leaves the others as they were");
                Check(middle(none) < 20 && band(none, 22, 33) < 20 && band(all, 28, 33) > 20, "all four Off: the seat is gone; all On: framed");
                (void)fx;
                // The center in thirds
                auto pointed = [&](rw::Kind kind) { auto s = base(); s.step.under = {kind}; s.step.centerGrown = true; s.grow = 1; return s; };
                const auto rest = draw(base(), "t036-center-1-rest");
                const auto pg = draw(pointed(rw::Kind::Glyphs), "t036-center-2-glyphs");
                const auto pm = draw(pointed(rw::Kind::Gestures), "t036-center-3-gestures");
                const auto pq = draw(pointed(rw::Kind::Quick), "t036-center-4-quick-cast");
                { auto s = pointed(rw::Kind::Quick); s.thirdGrowPct = 0; draw(s, "t036-center-5-grow-off"); }
                { auto s = pointed(rw::Kind::Quick); s.thirdDimPct = 100; draw(s, "t036-center-6-dim-off"); }
                { auto s = pointed(rw::Kind::Quick); s.thirdDimPct = 0; draw(s, "t036-center-7-dim-hidden"); }
                { auto s = base(); s.menuBadge[rw::ThirdQuick] = {true, rw::BadgeKind::Coin, 'Y'}; draw(s, "t036-center-8-quick-cast-on-a-button"); }
                const auto pqOff = [&] { auto s = pointed(rw::Kind::Quick); s.quickWord = rw::QuickThirdWord(false, true); return draw(s, "t036-center-9-quick-cast-off"); }();   // its switch Off
                { auto s = pointed(rw::Kind::Quick); s.quickWord = rw::QuickThirdWord(true, false); draw(s, "t036-center-10-quick-cast-not-this-class"); }
                {   // each third's mark and each sub-menu shelf entry is its own picture,
                    // silver on our dark disc; a picture missing: the drawn mark in silver. Never gold: no bright pixel with red far over blue.
                    std::vector<int16_t> noArt(built->icons.cellOf.begin(), built->icons.cellOf.end());
                    for (int i = 0; i < st.layout.count; ++i) if (rw::MenuOfEntry(st.layout.seats[size_t(i)].id) >= 0) noArt[size_t(i)] = -1;
                    const auto bare = [&] { auto s = base(); s.thirdCell = {-1, -1, -1}; s.cells = noArt.data(); return draw(s, "t036-center-11-no-pictures"); }();
                    auto tally = [&](const std::vector<uint8_t>& px, float x, float y, float r, int& silver, int& gold) {
                        silver = gold = 0;
                        for (float dy = -r; dy <= r; dy += 1) for (float dx = -r; dx <= r; dx += 1) {
                            if (dx * dx + dy * dy > r * r) continue;
                            const int R = At(px, c.w, x + dx, y + dy, c, 0), G = At(px, c.w, x + dx, y + dy, c, 1), B = At(px, c.w, x + dx, y + dy, c, 2);
                            const int lum = (3 * R + 6 * G + B) / 10;
                            if (lum > 100 && R - B > 50) ++gold; else if (lum > 100 && std::abs(R - B) < 30) ++silver;
                        }
                    };
                    for (int t = 0; t < rw::ThirdCount; ++t) {
                        Check(built->icons.cellOf.size() == size_t(st.request.n) && base().thirdCell[size_t(t)] >= 0, std::string("") + rw::ThirdName[t] + ": its picture has an atlas cell");
                        float mx, my; rw::ThirdMarkAt(t, 0, mx, my);
                        int silver = 0, gold = 0, bs = 0, bg = 0;
                        tally(rest, mx, my, 15, silver, gold); tally(bare, mx, my, 15, bs, bg);
                        std::printf("%s mark: picture silver %d gold %d px; drawn mark silver %d gold %d px\n", rw::ThirdName[t], silver, gold, bs, bg);
                        Check(silver > 25 && gold == 0, std::string("") + rw::ThirdName[t] + "'s mark: its picture, silver, no gold");
                        Check(bs > 5 && bg == 0, std::string("") + rw::ThirdName[t] + "'s mark without its picture: the drawn mark in silver");
                    }
                    for (int i = 0; i < st.layout.count; ++i) if (const int t = rw::MenuOfEntry(st.layout.seats[size_t(i)].id); t >= 0) {
                        int silver = 0, gold = 0;
                        tally(rest, rw::CanvasX + st.layout.seats[size_t(i)].x, rw::CanvasY + st.layout.seats[size_t(i)].y, 17, silver, gold);
                        Check(silver > 25 && gold == 0, std::string("") + rw::MenuEntryName[t] + "'s shelf entry: its picture, silver, no gold");
                    }
                }
                const float nameY = rw::CanvasCy - 13;
                auto nameAt = [&](const std::vector<uint8_t>& px) {   // either side of the 12 o'clock spoke's column (at rest it crosses here)
                    return Bright(px, c.w, c, rw::CanvasCx - 22, nameY - 10, rw::CanvasCx - 4, nameY + 10) + Bright(px, c.w, c, rw::CanvasCx + 4, nameY - 10, rw::CanvasCx + 22, nameY + 10); };
                for (const auto* p : {&pg, &pm, &pq}) Check(nameAt(*p) > 15, "a pointed third: its name in the middle");
                Check(nameAt(rest) == 0, "at rest: no name in the middle");
                float widest = 0;
                for (int t = 0; t < rw::ThirdCount; ++t) widest = std::max(widest, tx::Width(built->metrics, tx::Name24, rw::ThirdName[t]));
                const float room = 2 * std::sqrt(rw::CenterR * rw::CenterR - 25.f * 25.f) - 5;   // the mockup's measure: the hub's chord at the name's top edge
                Check(widest <= room, "the widest third's name fits the hub at the skill name's size (" + std::to_string(int(widest)) + " of " + std::to_string(int(room)) + " px)");
                {   // the grown slice reaches 151 px (11%): bright at 145 px in its middle direction, nothing there at rest
                    const float a = rw::ThirdMidDeg[rw::ThirdQuick] * rw::Pi / 180, x = rw::CanvasCx + 145 * std::cos(a), y = rw::CanvasCy + 145 * std::sin(a);
                    Check(At(pq, c.w, x, y, c, 3) > 100 && At(rest, c.w, x, y, c, 3) < 60, "the Quick cast third grown: its slice at 145 px");
                }
                {   // Quick cast's switch Off: the pointed third's second line differs ("Off in the ring", muted), its name the same
                    double diff = 0;
                    for (float y = rw::CanvasCy + 5; y <= rw::CanvasCy + 30; y += 1) for (float x = rw::CanvasCx - 90; x <= rw::CanvasCx + 90; x += 1) diff += std::abs(At(pqOff, c.w, x, y, c, 0) - At(pq, c.w, x, y, c, 0));
                    Check(diff > 500 && nameAt(pqOff) > 15, "Quick cast Off: its third says so under its name");
                }
            }
            (void)compose.TakeWindow();
        }
        {   // match stage 1a perf fix: the On edge loads the build's bytecode, no compile
            const auto st = compose.Stats();
            std::printf("compose shaders: %s, %.0f ms, pixel shaders %u + effects %u instructions, %u + %u slots (budget %u each)\n", st.embedded ? "built offline" : "compiled",
                st.compileMs, st.psInstructions, st.fxInstructions, rw::ShaderBlobPsSlots, rw::ShaderBlobPsFxSlots, rw::ShaderBlobSlotBudget);
            if (rw::ShaderBlobOk) {
                Check(st.embedded, "perf: the compose shaders come from the build's blob (its hash matches the source)");
                Check(st.compileMs < 1000, "perf: the On edge's shader setup (load + both pipelines) under 1 s (" + std::to_string(int(st.compileMs)) + " ms)");
                Check(st.warmMs >= 0, "perf: both pipelines warmed on the worker (" + std::to_string(int(st.warmMs)) + " ms)");
                Check(rw::ShaderBlobPsTemps <= rw::ShaderBlobPsTempBudget && rw::ShaderBlobPsFxTemps <= rw::ShaderBlobPsFxTempBudget,
                      "perf: both pixel shaders within the temp budget (" + std::to_string(rw::ShaderBlobPsTemps) + ", " + std::to_string(rw::ShaderBlobPsFxTemps) + ")");
                Check(!compose.EffectsPending(), "perf: the offline blob has the effects from the start");
                Check(rw::ShaderBlobPsSlots > 0 && rw::ShaderBlobPsSlots <= rw::ShaderBlobSlotBudget && rw::ShaderBlobPsFxSlots > 0 && rw::ShaderBlobPsFxSlots <= rw::ShaderBlobSlotBudget,
                      "perf: both pixel shaders within their slot budget (" + std::to_string(rw::ShaderBlobPsSlots) + ", effects " + std::to_string(rw::ShaderBlobPsFxSlots) + ")");
            }
        }
        const Data data = real ? SorceressData() : test;
        for (int pct : {100, 150}) {
            const auto shown = Lay(data, rw::TreeLayout::Shown, pct), orbs = Lay(data, rw::TreeLayout::Orbs, pct);
            const unsigned before = worker.Builds();
            worker.Want(shown.request);
            std::shared_ptr<const rw::AtlasWorker::Built> built;
            Check(PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == shown.request; }, 60000),
                "#15 atlases built and flipped in at " + std::to_string(pct) + "%");
            if (!built) break;
            Check(worker.Builds() == before + 1, "#15 one build for the change");
            Check(compose.MaxSliceBytes() > 0 && compose.MaxSliceBytes() <= (1u << 20), "#15 slices at most 1 MB (" + std::to_string(compose.MaxSliceBytes()) + " bytes)");
            size_t after = 0;
            for (int i = 0; i < 20; ++i) after += compose.Pump();
            Check(after == 0, "#15 zero uploads after the build");
            worker.Want(shown.request);
            PumpUntil(compose, [] { return false; }, 200);
            Check(worker.Builds() == before + 1, "#15 the same request again: no rebuild");
            std::printf("atlas %d%%: icons %d/%d, glyphs %.0f ms, icons %.0f ms, max slice %zu bytes\n", pct, built->icons.found, built->icons.wanted,
                built->glyphMs, built->iconMs, compose.MaxSliceBytes());
            if (!real) Check(built->icons.missing.size() == 1 && built->icons.missing[0] == 105, "#15 the missing icon listed");
            struct Shot { const char* name; bool orbs = false; int seat = -1; int tree = -1; bool center = false; };
            int coldSeat = 0;   // a seat inside the third tree
            for (int i = shown.layout.count - 1; i >= 0; --i) if (shown.layout.seats[size_t(i)].tree == 2) coldSeat = i;
            coldSeat += 3;
            const Shot shots[] = {{"rest"}, {"pointed-blurb", false, coldSeat}, {"center-glyphs", false, -1, -1, true}, {"orbs-open", true, -1, 1}};
            tx::BlurbCache blurbs;
            for (const auto& shot : shots) {
                const auto& seats = shot.orbs ? orbs : shown;
                rw::Settings set; set.layout = seats.layout.mode;
                rw::Engine e; e.Reset(seats.layout);
                rw::StepOut step;
                for (double t = 0; t <= 600; t += 11) {
                    rw::StepIn si; si.nowMs = t;
                    if (shot.seat >= 0) { si.hit = true; si.x = rw::CanvasX + seats.layout.seats[size_t(shot.seat)].x; si.y = rw::CanvasY + seats.layout.seats[size_t(shot.seat)].y; }
                    if (shot.tree >= 0) { const float a = seats.layout.dir[size_t(shot.tree)]; si.hit = true; si.x = rw::CanvasCx + 110 * std::cos(a); si.y = rw::CanvasCy + 110 * std::sin(a); }
                    if (shot.center) { si.hit = true; si.x = rw::CanvasCx; si.y = rw::CanvasCy - 40; }
                    step = e.Step(si, set);
                }
                rw::Scene s; s.open = true; s.layout = &seats.layout; s.step = step; s.anim = rw::PresetAnim(rw::Preset::Hover); s.tMs = 2000;
                s.atlasPct = built->icons.scalePct; s.cells = built->icons.cellOf.data(); s.metrics = &built->metrics;
                s.initials = seats.initialPtr.data();
                for (int t = 0; t < rw::Trees; ++t) s.treeNames[size_t(t)] = data.trees[t];
                if (step.blurb >= 0) {
                    const auto& seat = seats.layout.seats[size_t(step.blurb)];
                    std::string desc = "a made-up description line for the test", req = "18";
                    if (!kit.empty()) for (const auto& k : ReadKit(kit)) if (k.name == seats.names[size_t(step.blurb)]) { desc = k.desc; req = k.req; }
                    s.blurb = &blurbs.Get(built->metrics, uint64_t(seat.id), {seats.names[size_t(step.blurb)],
                        std::string(seat.tree >= 0 ? data.trees[seat.tree] : "General") + ", level " + req, desc, "Tap casts, hold assigns"});
                }
                rw::Instance list[rw::MaxDraw];
                const int n = rw::Build(s, list);
                const auto c = rw::Content(rw::FooterWidth(false, 5), pct);
                const std::string name = std::string(shot.name) + "-" + std::to_string(pct);
                Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, name + ": composed");
                const auto px = f.Read(c.w, c.h);
                // Premultiplied everywhere but the effects' light: the kit's lighter layers are premultiplied additive (match
                // stage 1b), so pixels within a seat's rest cap (+ the float's 5 px and a grown icon) are not held to rgb <= alpha.
                const auto& lay = seats.layout;
                const float scale = float(c.w) / c.canvasW;
                int over = 0;
                for (size_t i = 0; i < px.size(); i += 4) {
                    if (!(px[i] > px[i + 3] || px[i + 1] > px[i + 3] || px[i + 2] > px[i + 3])) continue;
                    const size_t k = i / 4;
                    const float cx = c.x0 + (float(k % size_t(c.w)) + .5f) / scale, cy = c.y0 + (float(k / size_t(c.w)) + .5f) / scale;
                    bool inFx = false;
                    for (int q = 0; q < lay.count && !inFx; ++q)
                        inFx = std::hypot(cx - (rw::CanvasX + lay.seats[size_t(q)].x), cy - (rw::CanvasY + lay.seats[size_t(q)].y)) <= rw::RestCapPx * 1.3f + 6;
                    over += !inFx;
                }
                Check(over == 0, name + ": premultiplied, no channel above alpha (outside the effects' additive light)");
                Check(At(px, c.w, rw::CanvasCx, rw::CanvasCy + 60, c, 3) >= 150, name + ": the center's backing disc");
                Check(At(px, c.w, rw::CanvasX + 8, rw::CanvasY + 8, c, 3) == 0, name + ": the content's corner is clear");
                const int centerText = Bright(px, c.w, c, rw::CanvasCx - 110, rw::CanvasCy - 100, rw::CanvasCx + 110, rw::CanvasCy + 100);
                if (shot.seat >= 0) {
                    Check(step.blurb == shot.seat && s.blurb && !s.blurb->lines.empty(), name + ": the held seat shows its blurb");
                    Check(centerText > 300, name + ": the blurb's text drawn in the center (" + std::to_string(centerText) + " px)");
                } else if (!shot.orbs) {
                    Check(centerText > 100, name + ": Glyphs / Gestures drawn in the center (" + std::to_string(centerText) + " px)");
                }
                if (!shot.orbs && shot.seat < 0) {
                    const float a = seats.layout.dir[0];
                    const int arc = Bright(px, c.w, c, rw::CanvasCx + rw::NameArcR * std::cos(a) - 90, rw::CanvasCy + rw::NameArcR * std::sin(a) - 20,
                                           rw::CanvasCx + rw::NameArcR * std::cos(a) + 90, rw::CanvasCy + rw::NameArcR * std::sin(a) + 20);
                    Check(arc > 60, name + ": the top tree's name on its arc (" + std::to_string(arc) + " px)");
                }
                if (!real && !shot.orbs && shot.seat < 0) {
                    int missingSeat = -1;
                    for (int i = 0; i < seats.layout.count; ++i) if (seats.layout.seats[size_t(i)].id == 105) missingSeat = i;
                    const auto& ms = seats.layout.seats[size_t(missingSeat)];
                    Check(missingSeat >= 0 && At(px, c.w, rw::CanvasX + ms.x + 14, rw::CanvasY + ms.y, c, 3) >= 200 &&
                        Bright(px, c.w, c, rw::CanvasX + ms.x - 14, rw::CanvasY + ms.y - 12, rw::CanvasX + ms.x + 14, rw::CanvasY + ms.y + 12) > 10,
                        name + ": the missing icon's initials disc");
                }
                std::printf("%s: %d instances, %d x %d\n", name.c_str(), n, c.w, c.h);
                if (!out.empty()) Save(out, name, px, c.w, c.h);
            }
            {   // R6Q2: the hold ring half full
                rw::Scene s; s.open = true; s.layout = &shown.layout; s.anim = rw::PresetAnim(rw::Preset::Off);
                s.atlasPct = built->icons.scalePct; s.cells = built->icons.cellOf.data(); s.metrics = &built->metrics;
                s.step.under = {rw::Kind::Seat, 3}; s.holdFrac = .5f; s.holdSeat = 3; s.holdSlot = "Right A"; s.holdTop = "Replaces"; s.holdStatus = "Fire Bolt";
                rw::Instance list[rw::MaxDraw];
                const int n = rw::Build(s, list);
                const auto c = rw::Content(rw::FooterWidth(false, 5), pct);
                const std::string name = "hold-half-" + std::to_string(pct);
                Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, name + ": composed");
                const auto px = f.Read(c.w, c.h);
                auto ringAt = [&](float deg) {   // the ring's pixel at an angle clockwise from the top
                    const float a = (deg - 90) * rw::Pi / 180;
                    return At(px, c.w, rw::CanvasCx + rw::HoldRingR * std::cos(a), rw::CanvasCy + rw::HoldRingR * std::sin(a), c, 0);
                };
                Check(ringAt(45) > 180 && ringAt(135) > 180, name + ": filled from the top clockwise to half (" + std::to_string(ringAt(45)) + ")");
                Check(ringAt(225) < 140 && ringAt(315) < 140 && ringAt(225) > 30, name + ": the other half shows only the gold track (" + std::to_string(ringAt(225)) + ")");
                Check(Bright(px, c.w, c, rw::CanvasCx - 100, rw::CanvasCy - 40, rw::CanvasCx + 100, rw::CanvasCy + 95) > 200, name + ": the slot's name and the status line in the center");
                if (!out.empty()) Save(out, name, px, c.w, c.h);
            }
        }
        f.Finish();
        const auto w = compose.TakeWindow();
        std::printf("composes %u: cpu mean %.3f max %.3f p95 %.3f ms; WARP gpu %u samples mean %.3f ms (a software device: not the headset's number)\n",
            w.composes, w.cpuMean, w.cpuMax, w.cpuP95, w.gpuSamples, w.gpuMean);
        Check(w.composes == 10 && (w.gpuSamples == 0 || w.gpuSamples == 10), "perf R4 #4: the window holds every compose");
        {   // item 4: a screenshot's picture: right pixels straight after composes that may still be in flight
            const rw::ContentRect c{256, 128, 0, 0, 256};
            rw::Instance list[2];
            list[0] = {40, 60, 60, 60, 0, 0, 0, 0, 0, .5f, .25f, .75f, 1, rw::RecipeRect, 0};
            list[1] = {150, 60, 60, 60, 0, 0, 0, 0, 0, .5f, .25f, .75f, .5f, rw::RecipeRect, 0};
            Check(compose.Draw(f.target.Get(), c, list, 2) == rw::Compose::Result::Composed, "shot: the reference draw");
            const auto ref = f.Read(256, 128);
            compose.TakeWindow();
            unsigned drew = 0;
            for (int k = 0; k < 3; ++k) drew += compose.Draw(f.target.Get(), c, list, 2) == rw::Compose::Result::Composed;
            struct { std::vector<uint8_t> rgba; int w = 0, h = 0; } image;
            rw::ShotRows held;
            const char* why = compose.Shot(c, list, 2, 5000, held);
            {   // Job B: the frame's half ends with the filled buffer held; its rows and the picture are made off the frame
                rw::shot::Raw rows;
                const bool has = held.Has(), read = held.Read(rows);
                Check((why != nullptr) == !has && has == read && !held.Has(), "shot: done = a filled buffer held; read once, then let go");
                rw::shot::Raw again;
                Check(!held.Read(again) && again.rows.empty(), "shot: a second read has nothing");
                if (read) { image.rgba = rw::shot::Picture(rows); image.w = rows.w; image.h = rows.h; }
            }
            {
                compose.WaitIdle(5000);
                const auto win = compose.TakeWindow();
                Check(win.composes == drew && (win.gpuSamples == 0 || win.gpuSamples == drew), "shot: its draw stays out of the 10 s cost window (" + std::to_string(win.composes) + " composes, " + std::to_string(drew) + " drawn)");
            }
            Check(!why && image.w == 256 && image.h == 128 && image.rgba.size() == 256u * 128u * 4u, std::string("shot: saved at the content's size") + (why ? std::string(": ") + why : ""));
            if (!why && image.rgba.size() == 256u * 128u * 4u) {
                auto at = [](const std::vector<uint8_t>& v, int x, int y, int ch) { return int(v[(size_t(y) * 256 + x) * 4 + ch]); };
                bool opaque = true; for (int ch = 0; ch < 4; ++ch) opaque = opaque && at(image.rgba, 40, 60, ch) == at(ref, 40, 60, ch);
                Check(opaque && at(image.rgba, 40, 60, 3) == 255, "shot: an opaque part as the swapchain's picture");
                const int a = at(ref, 150, 60, 3);
                Check(std::abs(at(image.rgba, 150, 60, 3) - a) <= 0 && a > 100 && a < 150 && std::abs(at(image.rgba, 150, 60, 0) - std::min(255, at(ref, 150, 60, 0) * 255 / a)) <= 1, "shot: a half-clear part in straight alpha");
                Check(at(image.rgba, 5, 5, 3) == 0 && at(image.rgba, 5, 5, 0) == 0, "shot: outside the art it is clear");
                // Job B: the worker reads the rows out of the buffer itself, so every texel is held to the swapchain's picture
                // (the same list drawn into the fixture's target, through the same conversion), not three sample points.
                const auto whole = rw::shot::ToStraightRgba8(ref.data(), 256 * 4, 256, 128, false, false);
                size_t differ = 0;
                for (size_t i = 0; i < whole.size(); ++i) differ += whole[i] != image.rgba[i];
                Check(differ == 0, "shot: every texel as the swapchain's picture (" + std::to_string(differ) + " bytes differ)");
            }
            {   // Job B: the frame's half ends with the rows handed over; the PNG is the worker's.
                // The worker is held at a gate, so both shots' frame halves are back before any file exists; then both files
                // are whole (read back: the picture's own pixels) and each has its line. A second shot while one is pending.
                rw::shot::SaveQueue saver;
                std::mutex m; std::condition_variable cv; bool open = false;
                std::vector<std::string> lines; std::vector<std::thread::id> on;
                std::error_code ec;
                const std::filesystem::path files[2] = {std::filesystem::current_path() / "ring-wheel-shot-test-1.png", std::filesystem::current_path() / "ring-wheel-shot-test-2.png"};
                for (const auto& p : files) std::filesystem::remove(p, ec);
                bool pushed = true;
                for (int k = 0; k < 2; ++k) {
                    const double t0 = rw::shot::NowMs();
                    auto rows = std::make_shared<rw::ShotRows>();
                    const char* no = compose.Shot(c, list, 2, 5000, *rows);
                    pushed = pushed && !no && saver.Push([&, rows, t0, file = files[k].wstring()](const rw::shot::SaveQueue::task& t) {
                        { std::unique_lock l(m); cv.wait_for(l, std::chrono::seconds(30), [&] { return open; }); on.push_back(std::this_thread::get_id()); }
                        rw::shot::Raw raw;
                        rows->Read(raw);
                        rw::shot::Finish(raw, file, t0, t, framevr::ringwheel::png::Save, [&](const char* s) { std::lock_guard l(m); lines.push_back(s); });
                    });
                }
                bool early; { std::lock_guard l(m); early = !lines.empty() || !on.empty(); }
                Check(pushed && !early && !std::filesystem::exists(files[0]) && !std::filesystem::exists(files[1]) && saver.Waiting() == 2,
                      "shot save: both shots' frame halves are back with no file written and no line said (the PNG is the worker's)");
                { std::lock_guard l(m); open = true; }
                cv.notify_all();
                saver.Drain();
                Check(lines.size() == 2 && on.size() == 2 && on[0] != std::this_thread::get_id() && on[1] != std::this_thread::get_id() && saver.Idle(),
                      "shot save: the drain returns with both saves written, on the worker");
                if (lines.size() == 2) {
                    Check(lines[0].rfind("Frame capture: ring wheel saved ", 0) == 0 && lines[0].find("ring-wheel-shot-test-1.png (256x128, ") != std::string::npos &&
                          lines[0].find(" ms on the frame, ") != std::string::npos && lines[0].find(" ms on the worker)") != std::string::npos, "shot save: the line names the file, its size, the frame's ms and the worker's: " + lines[0]);
                    Check(lines[1].find("ring-wheel-shot-test-2.png (256x128, ") != std::string::npos && lines[1].find(" ms behind 1 other save)") != std::string::npos,
                          "shot save: the second shot, taken while the first was pending, is saved too: " + lines[1]);
                    std::printf("%s\n%s\n", lines[0].c_str(), lines[1].c_str());
                }
                for (const auto& p : files) {
                    int pw = 0, ph = 0;
                    const auto back = ReadPng(p, pw, ph);
                    Check(pw == 256 && ph == 128 && back == image.rgba, "shot save: " + p.filename().string() + " reads back as the layer's picture");
                    std::filesystem::remove(p, ec);
                }
            }
            compose.WaitIdle(5000);
            compose.TakeWindow();   // these draws are this block's (the next measured window starts clean)
        }
        if (!out.empty()) {   // item 2: the Other trees row at 0, 49, 50 and 100, a tree pointed through the real engine, for the design
            const auto seats = Lay(data, rw::TreeLayout::Shown, 150);
            worker.Want(seats.request);
            std::shared_ptr<const rw::AtlasWorker::Built> built;
            if (PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == seats.request; }, 60000)) {
                for (int pct : {0, 49, 50, 100}) {
                    rw::Settings set; set.othersPct = float(pct);
                    rw::Engine e; e.Reset(seats.layout);
                    rw::StepOut step;
                    const float dir = seats.layout.dir[0];
                    for (double t = 0; t <= 600; t += 11) {
                        rw::StepIn si; si.nowMs = t; si.hit = true; si.x = rw::CanvasCx + 150 * std::cos(dir); si.y = rw::CanvasCy + 150 * std::sin(dir);
                        step = e.Step(si, set);
                    }
                    rw::Scene s; s.open = true; s.layout = &seats.layout; s.step = step; s.anim = rw::PresetAnim(rw::Preset::Off); s.skillClass = 1;
                    s.atlasPct = built->icons.scalePct; s.cells = built->icons.cellOf.data(); s.metrics = &built->metrics; s.initials = seats.initialPtr.data();
                    for (int t = 0; t < rw::Trees; ++t) s.treeNames[size_t(t)] = data.trees[t];
                    static rw::Instance list[rw::MaxDraw];
                    const int n = rw::Build(s, list);
                    const auto c = rw::Content(rw::FooterWidth(false, 5), 150);
                    Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "other trees render: composed");
                    char name[32]; std::snprintf(name, sizeof name, "other-trees-%03d", pct);
                    Save(out, name, f.Read(c.w, c.h), c.w, c.h);
                }
            }
            compose.TakeWindow();
        }
        // match stage 1 (r8-match-spec 7.3, B2): the rest cap in pixels. Every rest kind, under and over the face, on one
        // canvas at 100% (1 px = 1 canvas px), 10 s at 250 ms with its rest event cycling: no pixel over 2% alpha farther than
        // 28.6 px from its icon's center, premultiplied (rgb <= alpha). Each kind's mean added luma in the 1.0-1.3 r band
        // over black under the seat's rings (the reference's measure, the design notes; black is its upper bound) printed.
        {
            const int kinds = rw::RestKindCount;
            const auto c = rw::Content(rw::FooterWidth(false, 5), 100);
            auto centerOf = [&](int k, float& x, float& y) { x = c.x0 + 44.f + (k % 10) * 76.f; y = c.y0 + 44.f + (k / 10) * 76.f; };   // 76 px apart: 33 px caps never meet
            auto frame = [&](double tMs, bool glow) {
                static rw::Instance list[rw::MaxDraw];
                int n = 0;
                for (int k = 0; k < kinds; ++k) {
                    float x, y; centerOf(k, x, y);
                    const rw::Rgb tint = k >= 12 || k == int(rw::RestKind::Aura) || k == int(rw::RestKind::Charge) ? rw::Hex(0xE8D8B0) : rw::Rgb{1, 1, 1};
                    const float t = float(tMs / 1000), ev = float(std::fmod(tMs + k * 97, 1400.0));
                    const float d = 2 * rw::RestCapPx, quad = rw::IconPx + 2 * rw::SeatRingPx, seed = rw::Seed(k + 1);
                    if (glow) list[n++] = {x, y, d, d, 0, t, seed, ev, seed, tint.r, tint.g, tint.b, 1, rw::RecipeFx, float(k)};
                    list[n++] = {x, y, quad, quad, 0, 0, 0, 0, 0, rw::Gold.r, rw::Gold.g, rw::Gold.b, 1, rw::RecipeSeat, -2};
                    if (glow && rw::OverFace(rw::RestKind(k))) list[n++] = {x, y, d, d, 0, t, seed, ev, seed, tint.r, tint.g, tint.b, 1, rw::RecipeFx, float(k + 512)};
                }
                Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "stage 1 cap frame composed");
                return f.Read(c.w, c.h);
            };
            const auto t0 = std::chrono::steady_clock::now();
            const auto base = frame(0, false);
            float worst = 0; int worstKind = -1; bool premul = true;
            std::vector<double> band(size_t(kinds), 0); std::vector<int> bandN(size_t(kinds), 0);
            for (double tMs = 0; tMs <= 10000; tMs += 250) {
                const auto px = frame(tMs, true);
                for (int k = 0; k < kinds; ++k) {
                    float x, y; centerOf(k, x, y);
                    for (int py = int(y - c.y0) - 38; py < int(y - c.y0) + 38; ++py) for (int pxx = int(x - c.x0) - 38; pxx < int(x - c.x0) + 38; ++pxx) {
                        if (pxx < 0 || py < 0 || pxx >= c.w || py >= c.h) continue;
                        const uint8_t* q = &px[(size_t(py) * c.w + size_t(pxx)) * 4];
                        const uint8_t* b = &base[(size_t(py) * c.w + size_t(pxx)) * 4];
                        const float dd = std::hypot(c.x0 + pxx + .5f - x, c.y0 + py + .5f - y);
                        if ((q[3] > 5 || std::max({q[0], q[1], q[2]}) > 5) && dd > worst) { worst = dd; worstKind = k; }
                        if (dd >= rw::IconPx / 2 && dd <= 1.3f * rw::IconPx / 2) {   /* the rest-glow band, 1.0-1.3 r */
                            const double l = .2126 * q[0] + .7152 * q[1] + .0722 * q[2], l0 = .2126 * b[0] + .7152 * b[1] + .0722 * b[2];
                            band[size_t(k)] += std::max(0.0, l - l0); ++bandN[size_t(k)];
                        }
                    }
                }
            }
            Check(worst <= rw::RestCapPx, "stage 1 rest cap: no pixel over 2% alpha past 28.6 px in 10 s, every kind (farthest " + std::to_string(worst) + " px, kind " + std::to_string(worstKind) + ")");
            (void)premul;   // stage 1b: the kit's lighter layers are premultiplied additive (light, no alpha): no rgb <= alpha bound
            std::printf("stage 1 rest band luma (1.0-1.3 r, over black, 10 s):");
            for (int k = 0; k < kinds; ++k) std::printf(" %s %.1f", rw::RestKindName[k], bandN[size_t(k)] ? band[size_t(k)] / bandN[size_t(k)] : 0.0);
            std::printf("\n");
            std::printf("stage 1 cap test %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            compose.TakeWindow();
        }
        {   // match stage 3a: the BC4 decoder (a0 255, a1 0: eight steps, index i at bits 3i) and the no-cache path
            uint8_t b[8] = {255, 0, 0, 0, 0, 0, 0, 0}, v[16];
            uint64_t bits = 0; for (int i = 0; i < 16; ++i) bits |= uint64_t(i % 8) << (3 * i);
            for (int i = 0; i < 6; ++i) b[2 + i] = uint8_t(bits >> (8 * i));
            rw::DecodeBc4Block(b, v);
            uint8_t w[8] = {0, 0, 0, 0, 0, 0, 0, 0}; w[0] = 40; w[1] = 200; uint64_t bits2 = 0; for (int i = 0; i < 16; ++i) bits2 |= uint64_t(6 + i % 2) << (3 * i);
            for (int i = 0; i < 6; ++i) w[2 + i] = uint8_t(bits2 >> (8 * i));
            uint8_t v2[16]; rw::DecodeBc4Block(w, v2);
            Check(v[0] == 255 && v[1] == 0 && v[2] == 218 && v[7] == 36 && v[8] == 255 && v2[0] == 0 && v2[1] == 255,
                  "stage 3a: BC4 blocks decode (8-step and 6-step palettes)");
            rw::AtlasImage none; int served = -1; std::string why;
            Check(!rw::BuildFxTex({}, none, served, why) && served == 0 && !rw::BuildFxTex(L"Z:\\no\\sprites.ssp", none, served, why) && served == 0,
                  "stage 3a: no cache, no effect atlas (level 4)");
            if (sprites.empty()) Check(!compose.HasAtlas(rw::AtlasKind::FxTex), "stage 3a: without --sprites the preview never reads a cache");
        }
        // match stage 2 (r8-match-spec 7.3 "Pointed reach"): every element default (the 12 rest kinds' hover recipes) and
        // the farthest lightning bolts the draw list makes, 0-4 s at 100 ms: no pixel over 2% farther than 90 px from the
        // pointed icon's center (the quad's 15 px soft edge included).
        {
            const auto c = rw::Content(rw::FooterWidth(false, 5), 100);
            auto centerOf = [&](int k, float& x, float& y) { x = c.x0 + 95.f + (k % 4) * 190.f; y = c.y0 + 95.f + (k / 4) * 190.f; };
            auto frame = [&](double tMs) {
                static rw::Instance list[rw::MaxDraw];
                int n = 0;
                const float r = rw::IconPx * 1.3f / 2, t = float(tMs / 1000);
                const rw::Rgb tint = rw::Hex(rw::HoverAuraRgb);
                for (int k = 0; k < 12; ++k) {
                    float x, y; centerOf(k, x, y);
                    const float seed = rw::Seed(k + 1);
                    list[n++] = {x, y, 2 * rw::PointedReachPx, 2 * rw::PointedReachPx, 0, t, seed, r, seed, tint.r, tint.g, tint.b, 1, rw::RecipeFx, float(k + 256)};
                    if (k == int(rw::RestKind::Light) || k == int(rw::RestKind::Multi))
                        for (int b = 0; b < 2; ++b) {   // the farthest the draw list aims: the neighbor's rim side at 85 px
                            const float a = b ? 2.2f : -.4f, len = rw::PointedReachPx - 5 - r, mid = r + len / 2;
                            list[n++] = {x + std::cos(a) * mid, y + std::sin(a) * mid, len + 24, 40, a, t, seed + 7.f * (b + 1), len, seed, 1, 1, 1, 1, rw::RecipeFx, float(int(rw::RestKind::Light) + 256 * 3)};
                        }
                }
                Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "stage 2 reach frame composed");
                return f.Read(c.w, c.h);
            };
            float worst = 0; int worstKind = -1; std::array<int, 12> lit{};
            for (double tMs = 0; tMs <= 4000; tMs += 100) {
                const auto px = frame(tMs);
                if (!out.empty() && int(tMs) % 400 == 0 && tMs >= 400 && tMs <= 1600) {   // the 12 element defaults, for the sheets
                    char nm[48]; std::snprintf(nm, sizeof nm, "stage2-defaults-t%04d", int(tMs)); Save(out, nm, px, c.w, c.h);
                }
                for (int k = 0; k < 12; ++k) {
                    float x, y; centerOf(k, x, y);
                    for (int py = int(y - c.y0) - 95; py < int(y - c.y0) + 95; ++py) for (int pxx = int(x - c.x0) - 95; pxx < int(x - c.x0) + 95; ++pxx) {
                        if (pxx < 0 || py < 0 || pxx >= c.w || py >= c.h) continue;
                        const uint8_t* q = &px[(size_t(py) * c.w + size_t(pxx)) * 4];
                        if (std::max({q[0], q[1], q[2], q[3]}) <= 5) continue;
                        const float dd = std::hypot(c.x0 + pxx + .5f - x, c.y0 + py + .5f - y);
                        ++lit[size_t(k)];
                        if (dd > worst) { worst = dd; worstKind = k; }
                    }
                }
            }
            bool all = true;
            for (int k = 0; k < 12; ++k) all &= lit[size_t(k)] > 0;
            Check(all, "stage 2: every element default draws something");
            Check(worst <= rw::PointedReachPx, "stage 2 pointed reach: nothing past 90 px in 4 s, every element default (farthest " + std::to_string(worst) + " px, kind " + std::to_string(worstKind) + ")");
            compose.TakeWindow();
        }
        // the Glyphs and Gestures pages of all 8 classes at 150%, the
        // class's 30 skills as seats (synthetic icons by id unless --icons), upstream's card order; the Warlock: Glyphs only.
        if (!real) {
            const char* const classNames[8] = {"amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin", "warlock"};
            for (int cls = 0; cls < 8; ++cls) {
                const auto& k = framevr::skillcategories::Classes[size_t(cls)];
                Data d;
                for (int i = 0; i < 30; ++i) { d.in.push_back({k.first + i, -1, k.pages[i] - '1'}); d.names.push_back("Skill " + std::to_string(k.first + i)); }
                Seats seats = Lay(d, rw::TreeLayout::Shown, 150);
                seats.request.classId = cls;
                worker.Want(seats.request);
                std::shared_ptr<const rw::AtlasWorker::Built> built;
                if (!PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == seats.request; }, 60000)) { Check(false, std::string("R7 atlas for ") + classNames[cls]); continue; }
                auto cellOf = [&](int id) -> int { for (int i = 0; i < seats.layout.count; ++i) if (seats.layout.seats[size_t(i)].id == id) return built->icons.cellOf[size_t(i)]; return -1; };
                auto known = [&](int id) { return cellOf(id) >= 0; };
                auto nameOf = [&](int id) { return "Skill " + std::to_string(id); };
                for (int which = 0; which < 2; ++which) {
                    rw::PageView v;
                    if (which == 0) {   // the right hand's set: shapes 0, 2, 5 set; the rest sockets
                        auto sets = framevr::interaction::NoGlyphs;
                        sets[0][0] = k.first; sets[0][2] = k.first + 4; sets[0][5] = k.first + 9;
                        rw::FillGlyphs(v, sets, 0, nameOf, cellOf);
                    } else {
                        if (!framevr::spellmotion::HasMotions(cls)) { Check(cls == 7, "R7 only the Warlock has no Gestures page"); continue; }
                        rw::FillGestures(v, cls, 0b101, nameOf, cellOf, known);   // cards 1 and 3 off
                        const auto page = framevr::spellmotion::PageOf(cls);
                        bool order = v.count == page.count;
                        for (int i = 0; i < v.count && order; ++i) order = framevr::spellmotion::Family(page.skills[i]) || v.skill[size_t(i)] == page.skills[i];
                        Check(order, std::string("R7 ") + classNames[cls] + ": the Gestures page in upstream's card order");
                    }
                    rw::Scene s; s.open = true; s.anim = rw::PresetAnim(rw::Preset::Off); s.tMs = 2000;
                    s.atlasPct = built->icons.scalePct; s.metrics = &built->metrics;
                    s.step.under = {rw::Kind::Seat, 2};   // the third item pointed, its lines shown
                    s.step.blurb = 2; s.step.centerGrown = true;
                    tx::BlurbCache b;
                    s.blurb = &b.Get(built->metrics, 1, {v.name[2], "", v.line[2], v.help[2]});
                    rw::Instance list[rw::MaxDraw];
                    const int n = rw::BuildPage(s, v, list);
                    const auto c = rw::Content(rw::FooterWidth(false, 5), 150);
                    const std::string name = std::string(which ? "gestures-" : "glyphs-") + classNames[cls];
                    Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, name + ": composed");
                    const auto px = f.Read(c.w, c.h);
                    const float ring = which ? rw::GestureR : rw::GlyphR;
                    int lit = 0;
                    for (int i = 0; i < v.count; ++i) {
                        float x, y; rw::RingPos(i, v.count, ring, x, y);
                        lit += At(px, c.w, rw::CanvasX + x, rw::CanvasY + y, c, 3) > 100;   // an Off card: 50%
                    }
                    Check(lit == v.count, name + ": every item drawn on its ring (" + std::to_string(lit) + " of " + std::to_string(v.count) + ")");
                    Check(Bright(px, c.w, c, rw::CanvasCx - 120, rw::CanvasCy - 330, rw::CanvasCx + 120, rw::CanvasCy - 285) > 60, name + ": the title on the top arc");
                    Check(Bright(px, c.w, c, rw::CanvasCx - 120, rw::CanvasCy - 100, rw::CanvasCx + 120, rw::CanvasCy + 100) > 200, name + ": the pointed item's lines in the center");
                    if (which == 1) {   // the first card off: its "Off" pill at its lower right
                        int offCard = 0;
                        while (offCard < v.count - 1 && v.on[size_t(offCard)]) ++offCard;
                        Check(!v.on[size_t(offCard)], name + ": a card off");
                        float x, y; rw::RingPos(offCard, v.count, rw::GestureR, x, y);
                        Check(Bright(px, c.w, c, rw::CanvasX + x + 4, rw::CanvasY + y + 10, rw::CanvasX + x + 36, rw::CanvasY + y + 30) > 8, name + ": the Off pill's word");
                    }
                    std::printf("%s: %d instances\n", name.c_str(), n);
                    if (!out.empty()) Save(out, name, px, c.w, c.h);
                }
                if (cls == 1) {   // the Sorceress: the left hand's set (shape 0 follows the right's) with nothing pointed, and the glyph chooser
                    rw::PageView v;
                    auto sets = framevr::interaction::NoGlyphs;
                    sets[0][0] = 54; sets[1][1] = 40;
                    rw::FillGlyphs(v, sets, 1, [](int id) { return std::string(id == 54 ? "Teleport" : "Frozen Armor"); }, cellOf);
                    Check(v.follows[0] && v.skill[0] == 54 && v.line[0] == "Casts the right hand's Teleport" && !v.follows[1] && v.skill[1] == 40, "R7 the left hand's shape follows the right's (GlyphFollows)");
                    rw::Scene s; s.open = true; s.anim = rw::PresetAnim(rw::Preset::Off); s.atlasPct = built->icons.scalePct; s.metrics = &built->metrics;
                    s.step.under = {rw::Kind::Hand}; s.step.centerGrown = true;
                    rw::Instance list[rw::MaxDraw];
                    int n = rw::BuildPage(s, v, list);
                    auto c = rw::Content(rw::FooterWidth(false, 5), 150);
                    Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "glyphs-left-hand: composed");
                    auto px = f.Read(c.w, c.h);
                    Check(Bright(px, c.w, c, rw::CanvasCx - 100, rw::CanvasCy + 40, rw::CanvasCx + 100, rw::CanvasCy + 95) > 60, "glyphs-left-hand: the pointed lower half (Right hand) lit");
                    if (!out.empty()) Save(out, "glyphs-left-hand", px, c.w, c.h);
                    // The chooser: the three trees, upstream's glyph list lit (here: every other seat dimmed), Back / Clear glyph.
                    std::vector<uint8_t> dim(size_t(seats.layout.count));
                    for (size_t i = 0; i < dim.size(); ++i) dim[i] = uint8_t(i % 2);
                    rw::Scene m; m.open = true; m.layout = &seats.layout; m.anim = rw::PresetAnim(rw::Preset::Off);
                    m.atlasPct = built->icons.scalePct; m.cells = built->icons.cellOf.data(); m.metrics = &built->metrics; m.initials = seats.initialPtr.data();
                    m.hasMotions = true; m.chooseTitle = "Lightning"; m.clearLine = "Lightning will cast nothing"; m.seatDim = dim.data();
                    m.step.under = {rw::Kind::Gestures}; m.step.centerGrown = true;   // the Clear half pointed
                    for (int t = 0; t < rw::Trees; ++t) m.treeNames[size_t(t)] = SorceressTrees[t];
                    n = rw::Build(m, list);
                    Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "glyph-chooser: composed");
                    px = f.Read(c.w, c.h);
                    Check(Bright(px, c.w, c, rw::CanvasCx - 90, rw::CanvasCy - 45, rw::CanvasCx + 90, rw::CanvasCy + 45) > 100, "glyph-chooser: Back / Clear glyph in the center");
                    Check(Bright(px, c.w, c, rw::CanvasCx - 200, rw::CanvasCy - 335, rw::CanvasCx + 200, rw::CanvasCy - 290) > 60, "glyph-chooser: its title on the top r 313 arc");
                    const auto& lit = seats.layout.seats[0]; const auto& dimmed = seats.layout.seats[1];
                    Check(At(px, c.w, rw::CanvasX + lit.x, rw::CanvasY + lit.y, c, 3) > At(px, c.w, rw::CanvasX + dimmed.x, rw::CanvasY + dimmed.y, c, 3) + 80, "glyph-chooser: a seat off the glyph list drawn at 25%");
                    if (!out.empty()) Save(out, "glyph-chooser", px, c.w, c.h);
                    {   // Quick cast's page, three places filled (a potion with no picture: its disc), the
                        // rest empty sockets with their numbers, the center Back and "3 of 12 filled"; then its chooser's words
                        rw::PageView qv; const int qids[3] = {54, k.first + 4, (2 << 28) | 1};
                        rw::FillQuick(qv, qids, 3, [](int id) { return id == 54 ? std::string("Teleport") : std::string("Healing (best)"); }, cellOf);
                        rw::Scene qs; qs.open = true; qs.anim = rw::PresetAnim(rw::Preset::Off); qs.atlasPct = built->icons.scalePct; qs.metrics = &built->metrics;
                        n = rw::BuildPage(qs, qv, list);
                        Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "quick-cast-page: composed");
                        px = f.Read(c.w, c.h);
                        int filled = 0, empty = 0;
                        for (int i = 0; i < qv.count; ++i) {
                            float x, y; rw::RingPos(i, qv.count, rw::QuickR, x, y);
                            const float b = Bright(px, c.w, c, rw::CanvasX + x - 20, rw::CanvasY + y - 20, rw::CanvasX + x + 20, rw::CanvasY + y + 20);
                            const bool disc = i < 3 && qv.cell[size_t(i)] < 0;   // no picture: its opaque disc in its rim
                            (i < 3 ? filled : empty) += disc ? At(px, c.w, rw::CanvasX + x, rw::CanvasY + y, c, 3) > 200 : b > (i < 3 ? 60.f : 5.f);
                        }
                        Check(filled == 3 && empty == 9, "quick-cast-page: three places filled, nine empty sockets drawn (" + std::to_string(filled) + ", " + std::to_string(empty) + ")");
                        Check(Bright(px, c.w, c, rw::CanvasCx - 120, rw::CanvasCy - 330, rw::CanvasCx + 120, rw::CanvasCy - 285) > 60, "quick-cast-page: the title on the top arc");
                        Check(Bright(px, c.w, c, rw::CanvasCx - 80, rw::CanvasCy - 40, rw::CanvasCx + 80, rw::CanvasCy + 40) > 40, "quick-cast-page: Back in the center");
                        if (!out.empty()) Save(out, "quick-cast-page", px, c.w, c.h);
                        rw::Scene qm = m; qm.chooseTitle = "place 4"; qm.clearLine = "Place 4 is empty"; qm.chooseLead = "Choose for "; qm.chooseBack = "To Quick cast";
                        qm.step.under = {rw::Kind::Back}; qm.step.centerGrown = true;
                        n = rw::Build(qm, list);
                        Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "quick-cast-chooser: composed");
                        px = f.Read(c.w, c.h);
                        Check(Bright(px, c.w, c, rw::CanvasCx - 200, rw::CanvasCy - 335, rw::CanvasCx + 200, rw::CanvasCy - 290) > 60, "quick-cast-chooser: its title on the top arc");
                        { rw::Scene q2 = qm; q2.chooseLead = nullptr; rw::Scene q3 = qm; q3.chooseBack = nullptr; Check(!(rw::KeyOf(qm) == rw::KeyOf(q2)) && !(rw::KeyOf(qm) == rw::KeyOf(q3)), "quick-cast-chooser: its own words redraw (the draw key)"); }
                        if (!out.empty()) Save(out, "quick-cast-chooser", px, c.w, c.h);
                        // Review (exact items): a kind's page: Rejuvenation (Best), then the Full and the plain one carried
                        const int kids[3] = {(2 << 28) | 3, (1 << 28) | 0x6c7672, (1 << 28) | 0x737672};
                        rw::PageView kv;
                        rw::FillQuickKind(kv, kids, 3, 3, "Rejuvenation", [&](int id) { return id == kids[0] ? std::string("Rejuvenation") : id == kids[1] ? std::string("Full Rejuvenation Potion") : std::string("Rejuvenation Potion"); },
                                          [](int) { return -1; });
                        n = rw::BuildPage(qs, kv, list);
                        Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "quick-cast-kind: composed");
                        px = f.Read(c.w, c.h);
                        int discs = 0;
                        for (int i = 0; i < kv.count; ++i) { float x, y; rw::RingPos(i, kv.count, rw::QuickR, x, y); discs += At(px, c.w, rw::CanvasX + x, rw::CanvasY + y, c, 3) > 200; }
                        float bx, by; rw::RingPos(0, kv.count, rw::QuickR, bx, by); bx += rw::CanvasX + 24; by += rw::CanvasY + 24;
                        float ex, ey; rw::RingPos(1, kv.count, rw::QuickR, ex, ey); ex += rw::CanvasX + 24; ey += rw::CanvasY + 24;
                        const float bestText = Bright(px, c.w, c, bx - 22, by - 8, bx + 22, by + 8), exactCorner = Bright(px, c.w, c, ex - 22, ey - 8, ex + 22, ey + 8);
                        Check(discs == 3, "quick-cast-kind: the kind and two exact ones drawn (" + std::to_string(discs) + ")");
                        Check(bestText > exactCorner + 20, "quick-cast-kind: the kind wears its Best pill, an exact one none (" + std::to_string(int(bestText)) + ", " + std::to_string(int(exactCorner)) + ")");
                        Check(Bright(px, c.w, c, rw::CanvasCx - 120, rw::CanvasCy - 330, rw::CanvasCx + 120, rw::CanvasCy - 285) > 60, "quick-cast-kind: the kind's name on the top arc");
                        Check(Bright(px, c.w, c, rw::CanvasCx - 80, rw::CanvasCy - 40, rw::CanvasCx + 80, rw::CanvasCy + 40) > 40, "quick-cast-kind: Back in the center");
                        if (!out.empty()) Save(out, "quick-cast-kind", px, c.w, c.h);
                        {   // Potion choice: Mana with none carried but Light Mana: Best, then all 5 tiers, the 4 not carried dim
                            const auto mp = [](char d) { return (1 << 28) | int('m') | int('p') << 8 | int(d) << 16; };
                            const int mids[6] = {(2 << 28) | 2, mp('5'), mp('4'), mp('3'), mp('2'), mp('1')};
                            rw::PageView mv;
                            rw::FillQuickKind(mv, mids, 6, 3, "Mana", [&](int id) { return id == mids[0] ? std::string("Mana") : std::string("Mana Potion"); }, [](int) { return -1; }, [](int i) { return i != 4; });
                            n = rw::BuildPage(qs, mv, list);
                            Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "quick-cast-kind-mana: composed");
                            px = f.Read(c.w, c.h);
                            float lx, ly, dx, dy; rw::RingPos(4, mv.count, rw::QuickR, lx, ly); rw::RingPos(3, mv.count, rw::QuickR, dx, dy);   // the carried Light tier, a dim plain Mana one (no atlas icons here: bare discs, so only the outline shows; in game the dim ones are gray icons at 45%)
                            const auto glow = [&](float gx, float gy) {   // the red light in a 60 px box round the item (its rim and disc)
                                const float k = c.w / c.canvasW; long sum = 0;
                                for (int y = int((rw::CanvasY + gy - 30 - c.y0) * k); y < int((rw::CanvasY + gy + 30 - c.y0) * k); ++y)
                                    for (int x = int((rw::CanvasX + gx - 30 - c.x0) * k); x < int((rw::CanvasX + gx + 30 - c.x0) * k); ++x) sum += px[(size_t(y) * c.w + size_t(x)) * 4];
                                return sum;
                            };
                            const long lit = glow(lx, ly), dimmed = glow(dx, dy);
                            Check(lit > dimmed, "quick-cast-kind-mana: a carried tier draws brighter than a dim one (" + std::to_string(lit) + " vs " + std::to_string(dimmed) + ")");
                            if (!out.empty()) Save(out, "quick-cast-kind-mana", px, c.w, c.h);
                        }
                    }
                    // The main page's lower half pointed and grown: "Gestures", its count and "Trigger to open".
                    rw::Scene h; h.open = true; h.layout = &seats.layout; h.anim = rw::PresetAnim(rw::Preset::Off);
                    h.atlasPct = built->icons.scalePct; h.cells = built->icons.cellOf.data(); h.metrics = &built->metrics; h.initials = seats.initialPtr.data();
                    h.step.under = {rw::Kind::Gestures}; h.step.centerGrown = true;
                    const std::string count = rw::GesturesCount(1, 0b101), glyphCount = rw::GlyphsCount(0);
                    h.topCount = glyphCount.c_str(); h.bottomCount = count.c_str();
                    for (int t = 0; t < rw::Trees; ++t) h.treeNames[size_t(t)] = SorceressTrees[t];
                    n = rw::Build(h, list);
                    Check(count == "12 of 14 on", "R7 the Gestures half's count (" + count + ")");
                    Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "center-gestures-pointed: composed");
                    px = f.Read(c.w, c.h);
                    Check(Bright(px, c.w, c, rw::CanvasCx - 110, rw::CanvasCy + 44, rw::CanvasCx + 110, rw::CanvasCy + 96) > 150, "center-gestures-pointed: the count and Trigger to open under the label");
                    if (!out.empty()) Save(out, "center-gestures-pointed", px, c.w, c.h);
                }
                // Designer RS4: the states not shown yet. Nothing pointed on both pages (the Sorceress: 14 cards), a shape
                // with no skill pointed, and the Warlock's chooser.
                auto pageShot = [&](const std::string& name, const rw::PageView& v, rw::Target under, bool lines) {
                    rw::Scene s; s.open = true; s.anim = rw::PresetAnim(rw::Preset::Off); s.atlasPct = built->icons.scalePct; s.metrics = &built->metrics;
                    s.step.under = under;
                    tx::BlurbCache b;
                    if (lines) { s.step.blurb = under.index; s.step.centerGrown = true; s.blurb = &b.Get(built->metrics, 1, {v.name[size_t(under.index)], "", v.line[size_t(under.index)], v.help[size_t(under.index)]}); }
                    rw::Instance list[rw::MaxDraw];
                    const int n = rw::BuildPage(s, v, list);
                    const auto c = rw::Content(rw::FooterWidth(false, 5), 150);
                    Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, name + ": composed");
                    const auto px = f.Read(c.w, c.h);
                    Check(Bright(px, c.w, c, rw::CanvasCx - 90, rw::CanvasCy - 45, rw::CanvasCx + 90, rw::CanvasCy + 45) > 60, name + ": the center's words");
                    if (!out.empty()) Save(out, name, px, c.w, c.h);
                };
                if (cls == 1) {
                    rw::PageView g, m;
                    auto sets = framevr::interaction::NoGlyphs;
                    sets[0][0] = k.first; sets[0][2] = k.first + 4; sets[0][5] = k.first + 9;
                    rw::FillGlyphs(g, sets, 0, nameOf, cellOf);
                    pageShot("glyphs-rest-sorceress", g, {}, false);
                    pageShot("glyph-no-skill-pointed", g, {rw::Kind::Seat, 1}, true);
                    Check(g.line[1] == "No skill yet", "R7 a shape with no skill says No skill yet");
                    rw::FillGestures(m, cls, 0b101, nameOf, cellOf, known);
                    Check(m.count == 14, "R7 the Sorceress: 14 cards");
                    pageShot("gestures-rest-sorceress", m, {}, false);
                }
                if (cls == 7) {   // the Warlock: no Gestures; its chooser still has Back / Clear glyph
                    rw::Scene m; m.open = true; m.layout = &seats.layout; m.anim = rw::PresetAnim(rw::Preset::Off);
                    m.atlasPct = built->icons.scalePct; m.cells = built->icons.cellOf.data(); m.metrics = &built->metrics; m.initials = seats.initialPtr.data();
                    m.hasMotions = true; m.chooseTitle = "Circle"; m.clearLine = "Circle will cast nothing";
                    const char* const names[3] = {"Demon", "Eldritch", "Chaos"};
                    for (int t = 0; t < rw::Trees; ++t) m.treeNames[size_t(t)] = names[t];
                    rw::Instance list[rw::MaxDraw];
                    const int n = rw::Build(m, list);
                    const auto c = rw::Content(rw::FooterWidth(false, 5), 150);
                    Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "glyph-chooser-warlock: composed");
                    const auto px = f.Read(c.w, c.h);
                    Check(Bright(px, c.w, c, rw::CanvasCx - 90, rw::CanvasCy - 45, rw::CanvasCx + 90, rw::CanvasCy + 45) > 100, "glyph-chooser-warlock: Back / Clear glyph");
                    if (!out.empty()) Save(out, "glyph-chooser-warlock", px, c.w, c.h);
                }
            }
        }
        // each animation layer's frames (contact sheets per layer, L5 #6). The Sorceress (fire, lightning, cold trees) and
        // the Paladin (Zeal 106, Charge 107); synthetic icons. Checks: the effects draw outside the icon, the Off glow is still.
        if (!real) for (int cls : {1, 3}) {
            const auto& k = framevr::skillcategories::Classes[size_t(cls)];
            Data d;
            for (int i = 0; i < 30; ++i) { d.in.push_back({k.first + i, -1, k.pages[i] - '1'}); d.names.push_back("Skill " + std::to_string(k.first + i)); }
            Seats seats = Lay(d, rw::TreeLayout::Shown, 150), orbs = Lay(d, rw::TreeLayout::Orbs, 150);
            seats.request.classId = cls; orbs.request.classId = cls;
            worker.Want(seats.request);
            std::shared_ptr<const rw::AtlasWorker::Built> built;
            if (!PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == seats.request; }, 60000)) { Check(false, "R8 atlas"); continue; }
            std::vector<uint8_t> el(size_t(seats.layout.count)); std::vector<int8_t> ci(size_t(seats.layout.count), -1); int nc = 0;
            for (int i = 0; i < seats.layout.count; ++i) { el[size_t(i)] = uint8_t(rw::ElementOf(cls, seats.layout.seats[size_t(i)].tree)); if (el[size_t(i)] == uint8_t(rw::Element::Light)) ci[size_t(i)] = int8_t(nc++); }
            const auto c = rw::Content(rw::FooterWidth(false, 5), 150);
            auto base = [&](const Seats& st) {
                rw::Scene s; s.open = true; s.layout = &st.layout; s.atlasPct = built->icons.scalePct; s.cells = built->icons.cellOf.data(); s.metrics = &built->metrics;
                s.initials = st.initialPtr.data(); s.elements = el.data(); s.crawlIndex = ci.data(); s.crawlCount = nc; s.anim = rw::PresetAnim(rw::Preset::All);
                return s;
            };
            auto draw = [&](const rw::Scene& s) {
                static rw::Instance list[rw::MaxDraw];
                const int n = rw::Build(s, list);
                Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "R8 composed");
                return f.Read(c.w, c.h);
            };
            auto seatAt = [&](const Seats& st, int i, float& x, float& y) { x = rw::CanvasX + st.layout.seats[size_t(i)].x; y = rw::CanvasY + st.layout.seats[size_t(i)].y; };
            const std::filesystem::path fr = frames;
            if (cls == 1) {   // feedback 1 item 4: every footer label fits its tab at its row's one size (22 down to 17 px); the footer at 5 and 6 tabs
                using P = framevr::interaction::MenuPage;
                std::printf("footer font: %s\n", built->metrics.georgia ? "Georgia" : "Cinzel (Georgia missing)");
                int fits = 0, labels = 0; std::string sizes;
                for (P page : {P::Spells, P::Glyphs, P::EditSkills}) for (int dev = 0; dev < 2; ++dev) for (int full = 0; full < 2; ++full)
                    for (int i = 0; i < framevr::interaction::FooterCount(page, dev); ++i) {
                        const auto b = rw::FooterBoxAt(i, page, dev, full);
                        const char* label = rw::FooterLabel(i, page, dev, true);
                        if (!*label) continue;
                        const auto row = rw::FooterRowStyle(built->metrics, page, dev, full, true);
                        const auto fit = rw::FooterFitAt(built->metrics, row, label, b.x1 - b.x0);
                        ++labels; fits += fit.fits;
                        if (!fit.fits) std::printf("  over: %s page %d dev %d full %d\n", label, int(page), dev, full);
                        if (i == 0) sizes += "page " + std::to_string(int(page)) + (dev ? " 5 tabs" : " 4 tabs") + (full ? " full" : "") + " = " + std::to_string(int(tx::Specs[row].px)) + " px; ";
                        if (page == P::Spells && !full && fit.split != std::string_view::npos) sizes += std::string(label) + " two lines; ";
                    }
                std::printf("footer sizes: %s\n", sizes.c_str());
                Check(labels > 0 && fits == labels, "item 4: every footer label fits its tab (" + std::to_string(fits) + " of " + std::to_string(labels) + ")");
                for (int dev = 0; dev < 2; ++dev) {
                    auto s = base(seats); s.anim = rw::PresetAnim(rw::Preset::Off);
                    s.footer = {true, P::Spells, dev == 1, false, true};
                    s.step.under = {rw::Kind::Footer, framevr::interaction::FooterCell(3, P::Spells)};   // INVENTORY pointed
                    const auto cf = rw::Content(rw::FooterWidth(false, dev ? 6 : 5), 150);
                    static rw::Instance list[rw::MaxDraw];
                    const int n = rw::Build(s, list);
                    Check(DrawWait(compose, f.target.Get(), cf, list, n) == rw::Compose::Result::Composed, "item 4 footer composed");   // no raw Draw
                    const auto px = f.Read(cf.w, cf.h);
                    const auto b = rw::FooterBoxAt(3, P::Spells, dev == 1, false);
                    Check(Bright(px, cf.w, cf, b.x0 + 6, b.y0 + 6, b.x1 - 6, b.y1 - 6) > 40, "item 4: INVENTORY's label drawn in its tab");
                    Check(At(px, cf.w, b.x0 + 1, (b.y0 + b.y1) / 2, cf, 0) > 150, "item 4: the pointed tab's rim gold");
                    if (!footerOut.empty()) { std::filesystem::create_directories(footerOut); Save(footerOut, dev ? "footer-6-tabs" : "footer-5-tabs", px, cf.w, cf.h); }
                }
            }
            if (cls == 1) {
                const int seat = 4;   // an inner fire seat
                float sx, sy; seatAt(seats, seat, sx, sy);
                // Glow: one icon per element (all seats set to it), glow Everywhere, 6 frames over 2 s; and the Off glow frozen.
                for (int e = 0; e < int(rw::Element::Count); ++e) {
                    std::vector<uint8_t> one(el.size(), uint8_t(e));
                    std::vector<uint8_t> first;
                    for (int t = 0; t < 6; ++t) {
                        auto s = base(seats); s.elements = one.data(); s.anim.floats = false; s.tMs = 1000 + t * 400;
                        const auto px = draw(s);
                        if (t == 0) first = px;
                        if (t == 3) Check(px != first, "R8 glow " + std::to_string(e) + ": it moves");
                        if (!fr.empty()) SaveCrop(fr, "glow-" + std::to_string(e) + "-" + std::to_string(t), px, c, sx, sy, 130);
                    }
                    auto s = base(seats); s.elements = one.data(); s.anim = rw::PresetAnim(rw::Preset::Off);
                    s.tMs = 1000; const auto a = draw(s); s.tMs = 2200; const auto b = draw(s);
                    // stage 1: steel has no rest glow (the kit's glint is on the face, in its rest event only)
                    int rimA = 0;   // stage 1: the glow's brightest place round the rim (poison's blobs sit at four places)
                    for (int k = 0; k < 24; ++k)   // stage 1b: the light (the kit's lighter layers are additive: rgb, no alpha)
                        for (int ch = 0; ch < 4; ++ch) rimA = std::max<int>(rimA, At(a, c.w, sx + rw::IconPx * .6f * std::cos(k * rw::Tau / 24), sy + rw::IconPx * .6f * std::sin(k * rw::Tau / 24), c, ch));
                    Check(a == b && (e == int(rw::Element::Phys) || rimA > 8)   /* stage 1: the fitted levels (arcane is the kit's dimmest) */, "R8 glow " + std::to_string(e) + " Off: a steady rim, the same each frame");
                }
                // Hover effects: the pointed icon, one per element, 6 frames over 2.2 s.
                for (int e = 0; e < int(rw::Element::Count); ++e) {
                    std::vector<uint8_t> one(el.size(), uint8_t(e));
                    int outside = 0;
                    for (int t = 0; t < 6; ++t) {
                        auto s = base(seats); s.elements = one.data(); s.step.under = {rw::Kind::Seat, seat}; s.pointedSinceMs = 0; s.tMs = 200 + t * 400;
                        const auto px = draw(s);
                        outside += Bright(px, c.w, c, sx - 90, sy - 90, sx + 90, sy + 90) - Bright(px, c.w, c, sx - 40, sy - 40, sx + 40, sy + 40);
                        if (!fr.empty()) SaveCrop(fr, "hover-" + std::to_string(e) + "-" + std::to_string(t), px, c, sx, sy, 220);
                    }
                    Check(outside > 0 || e == 4 || e == 7, "R8 hover " + std::to_string(e) + ": its effect reaches past the icon");
                }
                // Lightning crawl over a pointed lightning icon: 6 frames 70 ms apart; at rest (All effects, 8 s) the bursts.
                int light = -1; for (int i = 0; i < seats.layout.count; ++i) if (ci[size_t(i)] >= 0) { light = i; break; }
                float lx, ly; seatAt(seats, light, lx, ly);
                for (int t = 0; t < 6; ++t) {
                    auto s = base(seats); s.step.under = {rw::Kind::Seat, light}; s.anim.hoverFx = false; s.tMs = 3000 + t * 70;
                    if (!fr.empty()) SaveCrop(fr, "crawl-0-" + std::to_string(t), draw(s), c, lx, ly, 130);
                }
                // Float: one tree's icons over 10 s (Floating icons), 6 frames.
                for (int t = 0; t < 6; ++t) {
                    auto s = base(seats); s.anim = rw::PresetAnim(rw::Preset::Floating); s.tMs = t * 2000.0;
                    if (!fr.empty()) SaveCrop(fr, "float-0-" + std::to_string(t), draw(s), c, sx, sy, 240);
                }
                // The fight dim (a hit at 0, as FightFade gives it) and death: whole wheel at half size (make-sheets scales).
                rw::FightFade ff; ff.Step(-10, true, 100, true, 1);
                const double times[5] = {0, 75, 150, 1000, 1650};
                for (int t = 0; t < 6; ++t) {
                    rw::FightOut o = t < 5 ? ff.Step(times[t], true, t == 0 ? 80.f : 80.f, true, 1) : rw::FightOut{1, .15f, true};
                    auto s = base(seats); s.anim = rw::PresetAnim(rw::Preset::Off); s.step.under = {rw::Kind::Seat, seat}; s.fight = o.dim; s.wheelAlpha = o.wheel;
                    const auto px = draw(s);
                    if (t == 2) Check(At(px, c.w, sx, sy, c, 3) > 200, "R8 fight: the pointed icon stays full");
                    if (!fr.empty()) SaveCrop(fr, "fight-0-" + std::to_string(t), px, c, rw::CanvasCx, rw::CanvasCy, 720);
                }
                // Tree burst (Tree orbs): tree 0 opened, 6 frames over 400 ms.
                for (int t = 0; t < 6; ++t) {
                    auto s = base(orbs); s.anim.floats = false; s.step.tree = 0; s.step.alpha = {1, 0, 0}; s.step.treeAgeMs = t * 80.0;
                    if (!fr.empty()) SaveCrop(fr, "burst-0-" + std::to_string(t), draw(s), c, rw::CanvasCx, rw::CanvasCy - 150, 520);
                }
            } else {   // the Paladin: Zeal (106) and Charge (107), then steel lunge and shadow rise on another seat
                int zeal = -1, charge = -1;
                for (int i = 0; i < seats.layout.count; ++i) { if (seats.layout.seats[size_t(i)].id == 106) zeal = i; if (seats.layout.seats[size_t(i)].id == 107) charge = i; }
                const int rows[4] = {zeal, charge, 0, 0};
                for (int r = 0; r < 4; ++r) {
                    std::vector<uint8_t> one = el;
                    if (r == 2) one[size_t(rows[r])] = uint8_t(rw::Element::Phys);
                    if (r == 3) one[size_t(rows[r])] = uint8_t(rw::Element::Shadow);
                    float x, y; seatAt(seats, rows[r], x, y);
                    for (int t = 0; t < 6; ++t) {
                        auto s = base(seats); s.elements = one.data(); s.step.under = {rw::Kind::Seat, rows[r]}; s.anim.hoverFx = false; s.anim.floats = false;
                        s.pointedSinceMs = 0; s.tMs = r == 3 ? t * 140.0 : (r == 1 ? 400 + t * 120.0 : t * 200.0);
                        if (!fr.empty()) SaveCrop(fr, "face-" + std::to_string(r) + "-" + std::to_string(t), draw(s), c, x, y, 220);
                    }
                }
            }
        }
        // R7c: each class's tree marks (the kit's glyphs) and the center's rune band, Trees shown and Tree
        // orbs, at 150%; every rune of the rest band read back against ringwheel::RuneOf (the shader's hash is the kit's).
        if (!real) {
            const char* const classNames[8] = {"amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin", "warlock"};
            if (!r7cOut.empty()) std::filesystem::create_directories(r7cOut);
            int runeMiss = 0, runeRead = 0;
            for (int cls = 0; cls < 8; ++cls) {
                const auto& k = framevr::skillcategories::Classes[size_t(cls)];
                Data d;
                for (int i = 0; i < 30; ++i) { d.in.push_back({k.first + i, -1, rw::PositionOf(cls, k.pages[i] - '1')}); d.names.push_back("Skill " + std::to_string(k.first + i)); }
                Seats shown = Lay(d, rw::TreeLayout::Shown, 150), orbs = Lay(d, rw::TreeLayout::Orbs, 150);
                shown.request.classId = cls;
                worker.Want(shown.request);
                std::shared_ptr<const rw::AtlasWorker::Built> built;
                if (!PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == shown.request; }, 60000)) { Check(false, std::string("R7c atlas for ") + classNames[cls]); continue; }
                const auto c = rw::Content(rw::FooterWidth(false, 5), 150);
                std::array<const char*, rw::Trees> names{};
                for (int t = 0; t < rw::Trees; ++t) names[size_t(t)] = framevr::skillcategories::Name(cls, rw::PageAt(cls, t) + 1);
                for (int shot = 0; shot < (cls == 1 ? 3 : 2); ++shot) {   // 0 Trees shown, 1 Tree orbs, 2 (Sorceress) the grown center, Glyphs pointed
                    const Seats& st = shot == 1 ? orbs : shown;
                    rw::Scene s; s.open = true; s.layout = &st.layout; s.atlasPct = built->icons.scalePct; s.cells = built->icons.cellOf.data(); s.metrics = &built->metrics;
                    s.initials = st.initialPtr.data(); s.anim = rw::PresetAnim(rw::Preset::Off); s.skillClass = cls; s.hasMotions = framevr::spellmotion::HasMotions(cls);
                    s.treeNames = names;
                    if (shot == 2) { s.grow = 1; s.step.under = {rw::Kind::Glyphs, 0}; s.topCount = "8 shapes, right hand"; }
                    static rw::Instance list[rw::MaxDraw];
                    const int n = rw::Build(s, list);
                    const std::string name = std::string(classNames[cls]) + (shot == 0 ? "-shown" : shot == 1 ? "-orbs" : "-center-grown-glyphs-pointed");
                    Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "R7c " + name + " composed");
                    const auto px = f.Read(c.w, c.h);
                    if (shot < 2) for (int t = 0; t < rw::Trees; ++t) {   // the glyph lights the mark's inside (gold light over the dark disc)
                        const float r = shot ? 34.f : 12.f, x = rw::CanvasCx + 128 * std::cos(st.layout.dir[size_t(t)]), y = rw::CanvasCy + 128 * std::sin(st.layout.dir[size_t(t)]);
                        const int lit = Bright(px, c.w, c, x - .8f * r, y - .8f * r, x + .8f * r, y + .8f * r);
                        Check(lit > (shot ? 60 : 15), "R7c " + name + ": tree " + std::to_string(t) + "'s glyph drawn (" + std::to_string(lit) + " px)");
                    }
                    if (shot == 0) for (int i = 0; i < rw::RuneCount(rw::CenterR); ++i) {   // each rune's shape (TS4 8 px, 2 px strokes): only the stem lights (0, 2.5); then only the caret (-0.625, -2); else Z
                        const float ac = i * rw::Tau / rw::RuneCount(rw::CenterR), th = ac + rw::Pi / 2, rr = rw::CenterR - 6.5f;
                        auto lit = [&](float qx, float qy) {
                            const float x = rw::CanvasCx + rr * std::cos(ac) + qx * std::cos(th) - qy * std::sin(th), y = rw::CanvasCy + rr * std::sin(ac) + qx * std::sin(th) + qy * std::cos(th);
                            return At(px, c.w, x, y, c, 0) > 60;
                        };
                        const auto got = lit(0, 2.5f) ? rw::RuneShape::Line : lit(-.625f, -2) ? rw::RuneShape::Caret : rw::RuneShape::Zed;
                        ++runeRead; runeMiss += got != rw::RuneOf(i, int(rw::CenterR));
                    }
                    if (shot == 2) {   // Glyphs pointed: the bottom half's runes at 55% of the top's (summed over each half's band)
                        double top = 0, bottom = 0;
                        for (int deg = 20; deg <= 160; ++deg) for (float dr = -4; dr <= 4; dr += .5f) {
                            const float a = deg * rw::Pi / 180, rr = rw::CenterGrownR - 6.5f + dr;
                            bottom += At(px, c.w, rw::CanvasCx + rr * std::cos(a), rw::CanvasCy + rr * std::sin(a), c, 0);
                            top += At(px, c.w, rw::CanvasCx + rr * std::cos(-a), rw::CanvasCy + rr * std::sin(-a), c, 0);
                        }
                        Check(top > 0 && bottom > .35 * top && bottom < .75 * top, "R7c: Glyphs pointed, the bottom half's runes dim (" + std::to_string(int(100 * bottom / std::max(top, 1.0))) + "% of the top's)");
                    }
                    if (!r7cOut.empty()) {
                        Save(r7cOut, name, px, c.w, c.h);
                        if (shot == 0) SaveCrop(r7cOut, name + "-center-crop", px, c, rw::CanvasCx, rw::CanvasCy, 200);
                        if (shot == 0) SaveCrop(r7cOut, name + "-top-mark-crop", px, c, rw::CanvasCx + 128 * std::cos(st.layout.dir[0]), rw::CanvasCy + 128 * std::sin(st.layout.dir[0]), 60);
                    }
                }
            }
            Check(runeRead == 8 * 36 && runeMiss == 0, "R7c: every rest rune drawn as ringwheel::RuneOf says (" + std::to_string(runeRead - runeMiss) + " of " + std::to_string(runeRead) + ")");
        }
        // match spec (sections 6 and 7; B1: judged on the real skill icons, --icons: a local icon cache in
        // upstream's format, never in git). Stage 1: every class with its per-skill rest glow, seated where the design kit seats
        // it (r8-match/kit-seats.json beside the --match folder's parent, its active skills only: the game's catalog holds no
        // passives), at 1x like the kit's clips: Trees shown at rest, each tree open in Tree orbs, the Assassin's Traps tree
        // over 2 s at 0.2 s. Then the check (the design notes): per class and tree, the mean added luma in the
        // 1.0-1.3 r band around the open tree's icons, with vs without the rest glow, 1 to 11 s at 250 ms, over the kit's busy
        // ground (r8-match/busy-plate-1x.png on black, as the reference), at most the reference's x 1.10, no frame over its highest x 1.10.
        if (real && !matchOut.empty()) {
            const char* const classNames[8] = {"amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin", "warlock"};
            const auto stageDir = matchOut;
            std::filesystem::create_directories(stageDir);
            auto slurp = [](const std::filesystem::path& p) { std::ifstream in(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(in), {}); };
            const std::string seatsJson = slurp(matchOut.parent_path().parent_path() / "kit-seats.json"), skillsJson = slurp(iconDir / "kit-skills.json");
            Check(!seatsJson.empty() && !skillsJson.empty(), "stage 1: kit-seats.json (r8-match) and kit-skills.json (the icon cache) read");
            int pw = 0, ph = 0;
            const auto plate = LoadPlate(matchOut.parent_path().parent_path() / "busy-plate-1x.png", pw, ph);
            Check(pw == 768 && ph == 800, "stage 1b: the busy plate read (768 x 800)");
            std::map<std::string, int> idOf;   // "<Class>/<name>" -> skill id
            {
                const std::regex e("\"(\\d+)\":\\s*\\{\\s*\"class\":\\s*\"([^\"]+)\",\\s*\"name\":\\s*\"([^\"]+)\"");
                for (std::sregex_iterator it(skillsJson.begin(), skillsJson.end(), e), end; it != end; ++it) {
                    std::string cls = (*it)[2]; for (auto& ch : cls) ch = char(std::tolower(uint8_t(ch)));
                    idOf[cls + "/" + std::string((*it)[3])] = std::stoi((*it)[1]);
                }
            }
            struct KitSeat { std::string name; int tree; bool shelf; float x, y; };
            auto kitSeats = [&](const std::string& key) {
                std::vector<KitSeat> out;
                const size_t at = seatsJson.find("\"" + key + "\"");
                if (at == std::string::npos) return out;
                const size_t end = seatsJson.find("}]", at) + 1;   // the seat list's end (an el list ends in "])
                const std::string part = seatsJson.substr(at, end - at);
                // one object per seat; its fields in any order, with or without spaces (the kit added `el` on)
                const std::regex obj("\\{[^{}]*\\}"), name("\"name\":\\s*\"([^\"]+)\""), tree("\"tree\":\\s*(\\d+)"), shelf("\"shelf\":\\s*(true|false)"),
                    x("\"x\":\\s*([-\\d.]+)"), y("\"y\":\\s*([-\\d.]+)");
                for (std::sregex_iterator it(part.begin(), part.end(), obj), stop; it != stop; ++it) {
                    const std::string o = it->str();
                    std::smatch n, t, s, xm, ym;
                    if (std::regex_search(o, n, name) && std::regex_search(o, t, tree) && std::regex_search(o, s, shelf) && std::regex_search(o, xm, x) && std::regex_search(o, ym, y))
                        out.push_back({n[1], std::stoi(t[1]), s[1] == "true", std::stof(xm[1]), std::stof(ym[1])});
                }
                return out;
            };
            // The reference (the design notes): mean and highest frame per class (classNames order) and page; -1: no icons.
            const float refMean[8][3] = {{18.0f, 15.4f, 22.0f}, {17.1f, 38.0f, 30.2f}, {12.1f, 17.2f, 12.5f}, {12.4f, 20.6f, 16.4f},
                                         {10.6f, -1, 21.5f}, {14.9f, 10.1f, 18.0f}, {22.4f, 22.5f, 27.3f}, {12.2f, 20.3f, 6.9f}};
            const float refHigh[8][3] = {{18.8f, 17.7f, 23.0f}, {19.2f, 41.0f, 33.8f}, {12.3f, 17.8f, 13.6f}, {14.3f, 21.1f, 16.9f},
                                         {10.9f, -1, 21.8f}, {15.5f, 11.0f, 18.9f}, {23.0f, 23.3f, 29.1f}, {12.6f, 20.6f, 7.6f}};
            std::ofstream table(stageDir / "rest-brightness-engine.tsv", std::ios::binary);
            table << "class\tpage\ticons\tengine mean\tengine highest\treference mean\treference highest\tpass\r\n";
            std::ofstream tableShown(stageDir / "rest-brightness-engine-trees-shown.tsv", std::ios::binary);
            tableShown << "class\tpage\ticons\tengine mean\tengine highest\r\n";   // Trees shown at rest, the tree's icons only
            int s1Bad = 0;
            for (int cls = 0; cls < 8; ++cls) {
                const std::string cname = classNames[cls];
                std::string title = cname; title[0] = char(std::toupper(uint8_t(title[0])));
                Data d; std::vector<KitSeat> pinS, pinO;
                for (const auto& k : kitSeats(cname + "/trees-shown")) {
                    const auto it = idOf.find(cname + "/" + k.name);
                    if (k.shelf || it == idOf.end() || k.tree < 1 || k.tree > 3) continue;   // the kit's shelf (general, item skills): not seated here
                    d.in.push_back({it->second, -1, rw::PositionOf(cls, k.tree - 1)}); d.names.push_back(k.name); pinS.push_back(k);
                }
                for (const auto& k : kitSeats(cname + "/tree-orbs")) if (!k.shelf) pinO.push_back(k);
                Seats shown = Lay(d, rw::TreeLayout::Shown, 100), orbs = Lay(d, rw::TreeLayout::Orbs, 100);
                auto pin = [&](Seats& st, const std::vector<KitSeat>& kit) {   // the kit's places for its seats
                    int pinned = 0;
                    for (int i = 0; i < st.layout.count; ++i)
                        for (const auto& k : kit) if (k.name == st.names[size_t(i)]) { st.layout.seats[size_t(i)].x = k.x; st.layout.seats[size_t(i)].y = k.y; ++pinned; break; }
                    return pinned;
                };
                const int pinnedS = pin(shown, pinS), pinnedO = pin(orbs, pinO);
                Check(pinnedS == shown.layout.count && pinnedO == orbs.layout.count && shown.layout.count > 0, "stage 1 " + cname + ": every seat at the kit's place (" + std::to_string(pinnedS) + "/" + std::to_string(pinnedO) + " of " + std::to_string(shown.layout.count) + ")");
                shown.request.classId = cls; orbs.request.classId = cls;
                worker.Want(shown.request);
                std::shared_ptr<const rw::AtlasWorker::Built> built;
                if (!PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == shown.request; }, 60000)) { Check(false, "stage 1 atlas for " + cname); continue; }
                int found = 0;
                for (int i = 0; i < shown.layout.count; ++i) found += built->icons.cellOf[size_t(i)] >= 0;
                Check(found == shown.layout.count, "stage 1: every " + cname + " skill has its real icon");
                if (!sprites.empty()) Check(PumpUntil(compose, [&] { return compose.HasAtlas(rw::AtlasKind::FxTex); }, 20000), "stage 3a: the game effect textures in (--sprites)");
                std::vector<rw::RestGlow> rest(size_t(shown.layout.count)), none(size_t(shown.layout.count));
                std::vector<uint8_t> el(size_t(shown.layout.count));
                std::vector<int8_t> ci(size_t(shown.layout.count), int8_t(-1)); int nc = 0;   // design S3a: the crawl's index, as the ring
                int fromKit = 0;
                for (int i = 0; i < shown.layout.count; ++i) {   // the ring's own fill (vr/ring_wheel.cpp Seats)
                    const auto& seat = shown.layout.seats[size_t(i)];
                    rest[size_t(i)] = rw::RestGlowOf(seat.id, cls, seat.tree); el[size_t(i)] = uint8_t(rw::ElementOfGlow(rest[size_t(i)]));
                    if (rw::CrawlsAtRest(rest[size_t(i)]) && nc < 127) ci[size_t(i)] = int8_t(nc++);
                    fromKit += rw::SkillFxOf(seat.id) != nullptr;
                }
                Check(fromKit == shown.layout.count, "stage 1 " + cname + ": every seat's glow from the kit table (" + std::to_string(fromKit) + ")");
                std::array<const char*, rw::Trees> names{};
                for (int t = 0; t < rw::Trees; ++t) names[size_t(t)] = framevr::skillcategories::Name(cls, rw::PageAt(cls, t) + 1);
                const auto c = rw::Content(rw::FooterWidth(false, 5), 100);
                auto scene = [&](const Seats& st, int openTree, double tMs, const rw::RestGlow* glow) {
                    rw::Scene s; s.open = true; s.layout = &st.layout; s.atlasPct = built->icons.scalePct; s.cells = built->icons.cellOf.data(); s.metrics = &built->metrics;
                    s.initials = st.initialPtr.data(); s.anim = rw::PresetAnim(rw::Preset::All); s.anim.floats = false; s.skillClass = cls;
                    s.hasMotions = framevr::spellmotion::HasMotions(cls); s.treeNames = names; s.elements = el.data(); s.rest = glow; s.tMs = tMs;
                    s.crawlIndex = ci.data(); s.crawlCount = nc;
                    s.fxTex = compose.HasAtlas(rw::AtlasKind::FxTex);
                    if (openTree >= 0) { s.step.tree = openTree; s.step.treeAgeMs = 2000; for (int t = 0; t < rw::Trees; ++t) s.step.alpha[size_t(t)] = t == openTree ? 1.f : 0.f; }
                    return s;
                };
                auto render = [&](const rw::Scene& s) {
                    static rw::Instance list[rw::MaxDraw];
                    const int n = rw::Build(s, list);
                    Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, "stage 1 " + cname + " composed");
                    return f.Read(c.w, c.h);
                };
                Save(stageDir, cname + "-rest", render(scene(shown, -1, 2000, rest.data())), c.w, c.h);
                // (the design notes): the mean added luma in the 1.0-1.3 r band (the reference's band, whatever the rest cap)
                // around the icons of tree `pos`, with vs without the rest glow, over the busy plate, 1 to 11 s at 250 ms.
                struct Measured { double mean, high; int seats; };
                auto measure = [&](const Seats& st, int openTree, int pos) {
                    std::vector<int> seats;
                    for (int i = 0; i < st.layout.count; ++i) if (st.layout.seats[size_t(i)].tree == pos) seats.push_back(i);
                    if (seats.empty()) return Measured{0, 0, 0};
                    const auto base = OverPlate(render(scene(st, openTree, 1000, none.data())), c.w, c.h, c, plate, pw, ph);
                    double sum = 0, high = 0; int frames = 0;
                    for (double tMs = 1000; tMs <= 11000; tMs += 250) {
                        const auto px = OverPlate(render(scene(st, openTree, tMs, rest.data())), c.w, c.h, c, plate, pw, ph);
                        double add = 0; int count = 0;
                        for (const int i : seats) {
                            const float x = rw::CanvasX + st.layout.seats[size_t(i)].x, y = rw::CanvasY + st.layout.seats[size_t(i)].y;
                            for (int py = int(y - c.y0) - 30; py <= int(y - c.y0) + 30; ++py) for (int pxx = int(x - c.x0) - 30; pxx <= int(x - c.x0) + 30; ++pxx) {
                                if (pxx < 0 || py < 0 || pxx >= c.w || py >= c.h) continue;
                                const float dd = std::hypot(c.x0 + pxx + .5f - x, c.y0 + py + .5f - y);
                                if (dd < rw::IconPx / 2 || dd > 1.3f * rw::IconPx / 2) continue;
                                const uint8_t* q = &px[(size_t(py) * c.w + size_t(pxx)) * 4];
                                const uint8_t* b = &base[(size_t(py) * c.w + size_t(pxx)) * 4];
                                add += std::max(0.0, (.2126 * q[0] + .7152 * q[1] + .0722 * q[2]) - (.2126 * b[0] + .7152 * b[1] + .0722 * b[2])); ++count;
                            }
                        }
                        const double m = count ? add / count : 0;
                        sum += m; high = std::max(high, m); ++frames;
                    }
                    return Measured{sum / frames, high, int(seats.size())};
                };
                for (int page = 0; page < rw::Trees; ++page) {
                    const int pos = rw::PositionOf(cls, page);
                    Save(stageDir, cname + "-orbs-open-" + std::to_string(page + 1), render(scene(orbs, pos, 2000, rest.data())), c.w, c.h);
                    if (page == 0 && compose.HasAtlas(rw::AtlasKind::FxTex)) {   // stage 3a: each game-texture default of the class, pointed, and one Trees shown frame
                        bool seen[12] = {};
                        for (int i = 0; i < orbs.layout.count; ++i) {
                            const auto hk = rw::HoverKindOf(rest[size_t(i)]);
                            if (rw::FxStageOf(hk, true) != 3 || seen[uint32_t(hk)]) continue;
                            seen[uint32_t(hk)] = true;
                            const int tree = orbs.layout.seats[size_t(i)].tree;
                            for (double age = 400; age <= 1600; age += 400) {
                                rw::Scene s = scene(orbs, tree, 2000 + age, rest.data());
                                s.step.under.kind = rw::Kind::Seat; s.step.under.index = i; s.fxSeat = i; s.fxAgeMs = age; s.fxAlpha = 1;
                                char nm[96]; std::snprintf(nm, sizeof nm, "%s-game-%s-t%04d", cname.c_str(), rw::RestKindName[uint32_t(hk)], int(age));
                                const auto on = render(s);
                                framevr::ringwheel::png::Save((stageDir / (std::string(nm) + "-on-busy.png")).c_str(), OverPlate(on, c.w, c.h, c, plate, pw, ph), UINT(c.w), UINT(c.h));
                                if (hk == rw::RestKind::Poison) {   // S3a recheck (Boss): the cloud over the face only adds light
                                    rw::Scene s0 = s; s0.fxSeat = -1;
                                    const auto off = render(s0);
                                    const float fx = rw::CanvasX + orbs.layout.seats[size_t(i)].x - float(c.x0), fy = rw::CanvasY + orbs.layout.seats[size_t(i)].y - float(c.y0);
                                    const float fr = rw::IconPx * 1.3f / 2 - 3;   // inside the pointed face, clear of its rim
                                    int darker = 0, lit = 0;
                                    for (int py = std::max(0, int(fy - fr)); py <= std::min(c.h - 1, int(fy + fr)); ++py)
                                        for (int px = std::max(0, int(fx - fr)); px <= std::min(c.w - 1, int(fx + fr)); ++px) {
                                            if (std::hypot(px + .5f - fx, py + .5f - fy) > fr) continue;
                                            for (int ch = 0; ch < 3; ++ch) {
                                                const int d = int(on[(size_t(py) * c.w + px) * 4 + ch]) - int(off[(size_t(py) * c.w + px) * 4 + ch]);
                                                darker += d < -1; lit += d > 8;
                                            }
                                        }
                                    Check(darker == 0 && lit > 0, "" + std::string(nm) + ": the poison over the face only adds light (" + std::to_string(darker) + " darker, " + std::to_string(lit) + " lit)");
                                }
                            }
                        }
                        for (int i = 0; i < shown.layout.count; ++i) {   // one Trees shown pointed frame per class (the first seat)
                            rw::Scene s = scene(shown, -1, 2800, rest.data());
                            s.step.under.kind = rw::Kind::Seat; s.step.under.index = i; s.fxSeat = i; s.fxAgeMs = 800; s.fxAlpha = 1;
                            framevr::ringwheel::png::Save((stageDir / (cname + "-shown-pointed-on-busy.png")).c_str(), OverPlate(render(s), c.w, c.h, c, plate, pw, ph), UINT(c.w), UINT(c.h));
                            break;
                        }
                    }
                    if (page == 0) {   // match stage 2: the tree's first two skills pointed (their element defaults), 0.4-1.6 s, on busy and dark
                        int done = 0;
                        for (int i = 0; i < orbs.layout.count && done < 2; ++i) {
                            if (orbs.layout.seats[size_t(i)].tree != pos) continue;
                            for (double age = 400; age <= 1600; age += 400) {
                                rw::Scene s = scene(orbs, pos, 2000 + age, rest.data());
                                s.step.under.kind = rw::Kind::Seat; s.step.under.index = i;
                                s.fxSeat = i; s.fxAgeMs = age; s.fxAlpha = 1;
                                char nm[96];
                                std::snprintf(nm, sizeof nm, "%s-pointed-%s-t%04d", cname.c_str(), (rw::ValidKind(rw::HoverKindOf(rest[size_t(i)])) ? rw::RestKindName[uint32_t(rw::HoverKindOf(rest[size_t(i)]))] : "none"), int(age));
                                for (char* ch = nm; *ch; ++ch) if (*ch == '/') *ch = '-';
                                const auto px = render(s);
                                Save(stageDir, nm, px, c.w, c.h);
                                framevr::ringwheel::png::Save((stageDir / (std::string(nm) + "-on-busy.png")).c_str(), OverPlate(px, c.w, c.h, c, plate, pw, ph), UINT(c.w), UINT(c.h));
                            }
                            ++done;
                        }
                    }
                    if (cls == 6 && page == 0)   // the Assassin's Traps tree (mixed elements) over 2 s at 0.2 s, for the sheets
                        for (int k = 0; k <= 10; ++k) { char nm[64]; std::snprintf(nm, sizeof nm, "assassin-traps-t%04d", 1000 + k * 200); Save(stageDir, nm, render(scene(orbs, pos, 1000 + k * 200, rest.data())), c.w, c.h); }
                    {   // the open tree over the busy ground, as the design's sheets show it; Trees shown too
                        const auto busy = OverPlate(render(scene(orbs, pos, 2000, rest.data())), c.w, c.h, c, plate, pw, ph);
                        framevr::ringwheel::png::Save((stageDir / (cname + "-orbs-open-" + std::to_string(page + 1) + "-on-busy.png")).c_str(), busy, UINT(c.w), UINT(c.h));
                    }
                    // Tree orbs with this tree open; then Trees shown at rest, this tree's icons only (no reference yet).
                    const auto [mean, high, nSeats] = measure(orbs, pos, pos);
                    if (nSeats == 0) continue;
                    {
                        const auto [smean, shigh, sn] = measure(shown, -1, pos);
                        char line[160];
                        std::snprintf(line, sizeof line, "%s\t%d\t%d\t%.1f\t%.1f", title.c_str(), page + 1, sn, smean, shigh);
                        tableShown << line << "\r\n";
                        std::printf("stage 1 shown %s\n", line);
                    }
                    const float rm = refMean[cls][page], rh = refHigh[cls][page];
                    const bool pass = rm < 0 || (mean <= rm * 1.10 && high <= rh * 1.10 && mean >= rm * 0.70);   // a match, not only a ceiling (>= 0.7 x)
                    s1Bad += !pass;
                    char line[200];
                    std::snprintf(line, sizeof line, "%s\t%d\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%s", title.c_str(), page + 1, nSeats, mean, high, rm, rh, rm < 0 ? "no reference" : pass ? "pass" : "FAIL");
                    table << line << "\r\n";
                    std::printf("stage 1 %s\n", line);
                }
            }
            Check(s1Bad == 0, "every class and tree's rest band light within 0.70-1.10 x the reference's (" + std::to_string(s1Bad) + " over)");
        }
        // Feedback 1 items 3 and 5: the bound-button badges on WARP, Sorceress Trees shown at 100% and 150% with
        // the kit's stand-in binds (qbauto: A B RT gA gB X Y LT gX gY spread over the trees). Each kind's pixels against the
        // kit's colors (disc #4A3820 to #14110D, coin #FFF0C4 / #E8C77E / #C9A25E, rim #E8C77E, a dark halo), the coin's letter
        // drawn without the dark outline (as thick as the disc's same letter), every size's widest letter 2 px inside its badge,
        // size Off draws none. --badges <folder> writes the 12 settings of the kit's contact sheet (the design's compare).
        if (!real) {
            if (!badgeOut.empty()) std::filesystem::create_directories(badgeOut);
            const int cls = 1;
            const auto& k = framevr::skillcategories::Classes[size_t(cls)];
            Data d;
            for (int i = 0; i < 30; ++i) { d.in.push_back({k.first + i, -1, rw::PositionOf(cls, k.pages[i] - '1')}); d.names.push_back("Skill " + std::to_string(k.first + i)); }
            std::array<const char*, rw::Trees> names{};
            for (int t = 0; t < rw::Trees; ++t) names[size_t(t)] = framevr::skillcategories::Name(cls, rw::PageAt(cls, t) + 1);
            for (int pct : {100, 150}) {
                Seats st = Lay(d, rw::TreeLayout::Shown, pct);
                st.request.classId = cls;
                worker.Want(st.request);
                std::shared_ptr<const rw::AtlasWorker::Built> built;
                if (!PumpUntil(compose, [&] { built = worker.Current(compose); return built && built->request == st.request; }, 60000)) { Check(false, "badges: the atlas at " + std::to_string(pct) + "%"); continue; }
                const auto& l = st.layout;
                rw::BoundSlots bound;
                {
                    const int order[10][2] = {{0, 1}, {0, 2}, {0, 0}, {0, 3}, {0, 4}, {1, 1}, {1, 2}, {1, 0}, {1, 3}, {1, 4}};
                    std::vector<int> seats;
                    for (int i = 0; i < l.count; ++i) if (l.seats[size_t(i)].tree >= 0) seats.push_back(i);
                    for (int i = 0; i < 10; ++i) {
                        const int s = seats[std::min(seats.size() - 1, size_t(i * int(seats.size()) / 10 + 1))];
                        bound.id[size_t(order[i][0])][size_t(order[i][1])] = l.seats[size_t(s)].id; bound.owner[size_t(order[i][0])][size_t(order[i][1])] = -1;
                    }
                }
                struct Setting { const char* tag; rw::BadgeLook look; };
                const Setting settings[] = {{"default", {}}, {"angle-0", {0, rw::BadgeDepth::Outline, 28}}, {"angle-90", {90, rw::BadgeDepth::Outline, 28}},
                                            {"angle-180", {180, rw::BadgeDepth::Outline, 28}}, {"angle-270", {270, rw::BadgeDepth::Outline, 28}},
                                            {"depth-outside", {-1, rw::BadgeDepth::Outside, 28}}, {"depth-inside", {-1, rw::BadgeDepth::Inside, 28}},
                                            {"depth-center", {-1, rw::BadgeDepth::Center, 28}}, {"size-16", {-1, rw::BadgeDepth::Outline, 16}},
                                            {"size-22", {-1, rw::BadgeDepth::Outline, 22}}, {"size-40", {-1, rw::BadgeDepth::Outline, 40}}, {"size-off", {-1, rw::BadgeDepth::Outline, 0}}};
                const auto c = rw::Content(rw::FooterWidth(false, 5), pct);
                for (const auto& set : settings) {
                    rw::BadgeSet badges; rw::PlaceBadges(l, bound, framevr::controllers::Layout::Touch, set.look, badges);
                    rw::Scene s; s.open = true; s.layout = &l; s.atlasPct = built->icons.scalePct; s.cells = built->icons.cellOf.data(); s.metrics = &built->metrics;
                    s.initials = st.initialPtr.data(); s.anim = rw::PresetAnim(rw::Preset::Off); s.skillClass = cls; s.hasMotions = true; s.treeNames = names; s.badges = &badges;
                    static rw::Instance list[rw::MaxDraw];
                    const int n = rw::Build(s, list);
                    int bodies = 0; for (int i = 0; i < n; ++i) bodies += list[i].recipe == rw::RecipeBadge;
                    const std::string name = std::string("badges-") + set.tag + "-" + std::to_string(pct);
                    Check(bodies == (set.look.sizePx > 0 ? 10 : 0), name + ": " + std::to_string(bodies) + " badges drawn");
                    Check(DrawWait(compose, f.target.Get(), c, list, n) == rw::Compose::Result::Composed, name + ": composed");
                    const auto px = f.Read(c.w, c.h);
                    if (std::string(set.tag) == "default") {
                        int discOk = 0, coinOk = 0, squareOk = 0, rimOk = 0, haloOk = 0, discs = 0, coins = 0, squares = 0;
                        int discA = 0, coinA = 0;   // the right A disc's light letter px and the right grip + A coin's dark letter px
                        for (int i = 0; i < badges.count; ++i) {
                            const auto& b = badges.b[size_t(i)];
                            const float x = rw::CanvasX + b.x, y = rw::CanvasY + b.y;
                            const uint8_t r = At(px, c.w, x - 10, y + 5, c, 0), g = At(px, c.w, x - 10, y + 5, c, 1);
                            if (b.kind == rw::BadgeKind::Coin) { ++coins; coinOk += r >= 185 && g >= 145; }
                            else if (b.kind == rw::BadgeKind::Square) { ++squares; squareOk += r >= 12 && r <= 85 && g <= 70; }
                            else { ++discs; discOk += r >= 12 && r <= 85 && g <= 70; }
                            float ux = (b.x - l.seats[size_t(b.seat)].x) / std::max(b.dist, 1.f), uy = (b.y - l.seats[size_t(b.seat)].y) / std::max(b.dist, 1.f);
                            if (b.kind == rw::BadgeKind::Square) { const bool h = std::fabs(ux) >= std::fabs(uy); ux = h ? (ux < 0 ? -1.f : 1.f) : 0.f; uy = h ? 0.f : (uy < 0 ? -1.f : 1.f); }   // the square's edge along an axis
                            rimOk += At(px, c.w, x + 13 * ux, y + 13 * uy, c, 0) >= 190 && At(px, c.w, x + 13 * ux, y + 13 * uy, c, 1) >= 150;
                            const uint8_t ha = At(px, c.w, x + 15.5f * ux, y + 15.5f * uy, c, 3), hr = At(px, c.w, x + 15.5f * ux, y + 15.5f * uy, c, 0);
                            haloOk += ha >= 90 && hr <= 70;
                            if (b.hand == 0 && (b.slot == 1 || b.slot == 3)) {
                                const float kk = c.w / c.canvasW;
                                for (int yy = int((y - 8 - c.y0) * kk); yy < int((y + 9 - c.y0) * kk); ++yy) for (int xx = int((x - 8 - c.x0) * kk); xx < int((x + 8 - c.x0) * kk); ++xx) {
                                    const uint8_t* p = &px[(size_t(yy) * c.w + size_t(xx)) * 4];
                                    if (b.slot == 1) discA += p[0] > 140; else coinA += p[0] < 110;   // each at about half coverage: parchment over iron, ink over gold
                                }
                            }
                        }
                        Check(discs == 4 && discOk == discs, name + ": the face discs dark iron (kit #4A3820 to #14110D): " + std::to_string(discOk) + " of " + std::to_string(discs));
                        Check(coins == 4 && coinOk == coins, name + ": the grip coins gold (kit #FFF0C4 / #E8C77E / #C9A25E): " + std::to_string(coinOk) + " of " + std::to_string(coins));
                        Check(squares == 2 && squareOk == squares, name + ": the trigger squares dark iron: " + std::to_string(squareOk) + " of " + std::to_string(squares));
                        Check(rimOk == badges.count, name + ": every badge's 2 px gold light rim (kit #E8C77E): " + std::to_string(rimOk) + " of " + std::to_string(badges.count));
                        Check(haloOk >= badges.count - 1, name + ": the dark halo outside each badge: " + std::to_string(haloOk) + " of " + std::to_string(badges.count));
                        Check(discA > 0 && coinA > .7 * discA && coinA < 1.4 * discA, name + ": the coin's A as thick as the disc's A, no dark outline (" + std::to_string(coinA) + " vs " + std::to_string(discA) + " px)");
                    }
                    if (!badgeOut.empty()) {
                        Save(badgeOut, name, px, c.w, c.h);
                        if (pct == 150) SaveCrop(badgeOut, name + "-top-crop", px, c, rw::CanvasCx, rw::CanvasCy - 230, 260);
                    }
                }
                {   // #3: the Draw glyph button's badge and the hold's center text. The Sorceress frames always run (checks); the
                    // Warlock's and Tree orbs' need their own atlas, so only with --badges <folder> (the design's frames).
                    using K = rw::Kind;
                    struct Shot { const char* tag; int cls; rw::TreeLayout mode; int hand, slot, size; float hold; const char* status; K under; bool grown; int tree; };
                    constexpr auto Sh = rw::TreeLayout::Shown;
                    const Shot shots[] = {{"disc-A", 1, Sh, 0, 1, 28, -1, nullptr, K::None, false, -1}, {"coin-grip-X", 1, Sh, 1, 3, 28, -1, nullptr, K::None, false, -1}, {"square-R", 1, Sh, 0, 0, 28, -1, nullptr, K::None, false, -1},
                                          {"pointing-glyphs-grown", 1, Sh, 0, 1, 28, -1, nullptr, K::Glyphs, true, -1}, {"pointing-gestures-grown", 1, Sh, 0, 1, 28, -1, nullptr, K::Gestures, true, -1},
                                          {"hold-bound-long-name", 1, Sh, 0, 1, 28, .5f, "Fist of the Heavens", K::Glyphs, true, -1}, {"hold-empty", 1, Sh, 0, 1, 28, .5f, "Draw glyph", K::Glyphs, true, -1}, {"hold-assigned", 1, Sh, 0, 1, 28, 1, "Assigned", K::Glyphs, true, -1},
                                          {"warlock-rest", 7, Sh, 0, 1, 28, -1, nullptr, K::None, false, -1}, {"warlock-pointed", 7, Sh, 0, 1, 28, -1, nullptr, K::Glyphs, true, -1},
                                          {"orbs-tree-open-size-40", 1, rw::TreeLayout::Orbs, 0, 1, 40, -1, nullptr, K::None, false, 0}};
                    static rw::Instance list[rw::MaxDraw];
                    for (const auto& h : shots) {
                        const bool own = h.cls != cls || h.mode != Sh;
                        if (own && (badgeOut.empty() || pct != 100)) continue;
                        Data od;
                        const auto& ok = framevr::skillcategories::Classes[size_t(h.cls)];
                        for (int i = 0; i < 30; ++i) { od.in.push_back({ok.first + i, -1, rw::PositionOf(h.cls, ok.pages[i] - '1')}); od.names.push_back("Skill " + std::to_string(ok.first + i)); }
                        Seats ost = Lay(od, h.mode, pct);
                        ost.request.classId = h.cls;
                        std::shared_ptr<const rw::AtlasWorker::Built> ob = built;
                        if (own) {
                            worker.Want(ost.request);
                            if (!PumpUntil(compose, [&] { ob = worker.Current(compose); return ob && ob->request == ost.request; }, 60000)) { Check(false, std::string("glyph-draw atlas: ") + h.tag); continue; }
                        }
                        const auto& sl = own ? ost.layout : l;
                        rw::BoundSlots sb;
                        if (!own) sb = bound;
                        else {
                            std::vector<int> ss; for (int i = 0; i < sl.count; ++i) if (sl.seats[size_t(i)].tree >= 0) ss.push_back(i);
                            const int order[6][2] = {{0, 2}, {0, 3}, {1, 1}, {1, 0}, {0, 0}, {1, 4}};
                            for (int i = 0; i < 6; ++i) { const int q = ss[std::min(ss.size() - 1, size_t(i * int(ss.size()) / 6 + 1))]; sb.id[size_t(order[i][0])][size_t(order[i][1])] = sl.seats[size_t(q)].id; sb.owner[size_t(order[i][0])][size_t(order[i][1])] = -1; }
                        }
                        rw::BadgeSet badges; rw::PlaceBadges(sl, sb, framevr::controllers::Layout::Touch, {rw::BadgeAuto, rw::BadgeDepth::Outline, h.size}, badges);
                        std::array<const char*, rw::Trees> onames{};
                        for (int t = 0; t < rw::Trees; ++t) onames[size_t(t)] = framevr::skillcategories::Name(h.cls, rw::PageAt(h.cls, t) + 1);
                        rw::Scene s; s.open = true; s.layout = &sl; s.atlasPct = ob->icons.scalePct; s.cells = ob->icons.cellOf.data(); s.metrics = &ob->metrics;
                        s.initials = (own ? ost : st).initialPtr.data(); s.anim = rw::PresetAnim(rw::Preset::Off); s.skillClass = h.cls; s.hasMotions = h.cls != 7; s.treeNames = onames; s.badges = &badges;
                        std::array<int, 2> slots{-1, -1}; slots[size_t(h.hand)] = rw::SlotOf(h.hand, h.slot);
                        s.drawBadge = rw::DrawBadgeFor(slots, h.hand, framevr::controllers::Layout::Touch, h.size);
                        s.step.under = {h.under}; s.step.centerGrown = h.grown;
                        s.holdFrac = h.hold; s.holdSeat = h.hold >= 0 ? rw::GlyphsSeat : -1; s.holdSlot = h.hold >= 0 ? "Right A" : nullptr; s.holdStatus = h.status;
                        s.holdTop = std::string(h.tag) == "hold-bound-long-name" ? "Replaces" : nullptr; s.holdGreen = std::string(h.tag) == "hold-assigned";
                        if (h.tree >= 0) { s.step.tree = h.tree; s.step.alpha = {}; s.step.alpha[size_t(h.tree)] = 1; s.step.treeAgeMs = 1e9; }
                        const int n = rw::Build(s, list);
                        int bodies = 0; for (int i = 0; i < n; ++i) bodies += list[i].recipe == rw::RecipeBadge;
                        const std::string name = std::string("glyph-draw-") + h.tag + "-" + std::to_string(pct);
                        if (!own) Check(bodies == (h.hold >= 0 ? 10 : 11), name + ": " + std::to_string(bodies) + " badges drawn");
                        Check(compose.Draw(f.target.Get(), c, list, n) == rw::Compose::Result::Composed, name + ": composed");
                        const auto px = f.Read(c.w, c.h);   // always (it waits for the GPU; the compose has few frames in flight)
                        if (!badgeOut.empty()) Save(badgeOut, name, px, c.w, c.h);
                    }
                }
                {   // #3: every skill name the game can show fits the hold's line: Width(Body22) + 5 <= 192.8, with the real font
                    float widest = 0; std::string widestName; int over = 0;
                    for (const char* nm : rw::AllSkillNames) {
                        const float w = tx::Width(built->metrics, tx::Body22, nm);
                        if (w > widest) { widest = w; widestName = nm; }
                        over += w + 5 > rw::HoldLineMaxW;
                    }
                    std::printf("hold line %d%%: the widest skill name is %s, %.1f px (+ 5 against %.1f); %d over\n", pct, widestName.c_str(), widest, rw::HoldLineMaxW, over);
                    Check(over == 0, "hold line " + std::to_string(pct) + "%: every skill name + 5 fits " + std::to_string(rw::HoldLineMaxW) + " px (" + widestName + " " + std::to_string(widest) + ")");
                }
                int fit = 0, fitBad = 0;   // every size: the widest of A B X Y R L inside its badge, 2 px each side
                for (int size = 16; size <= 40; size += 2) for (const char ch : {'A', 'B', 'X', 'Y', 'R', 'L'}) {
                    const auto& gl = built->metrics.g[size_t(tx::Bold22)][size_t(tx::SlotOf(uint8_t(ch)))];
                    ++fit; fitBad += gl.advance * size / 28.f > size - 4;
                }
                Check(fitBad == 0, "badges " + std::to_string(pct) + "%: every size's letters fit their badge with 2 px each side (" + std::to_string(fit - fitBad) + " of " + std::to_string(fit) + ")");
            }
        }
        worker.Stop();
        if (real) icons::Shutdown();
        {   // the engine's clock moves in sub-millisecond steps (GetTickCount64: about 15.6 ms)
            const double a = rw::NowMs();
            double b = a;
            for (int i = 0; i < 1000000 && b == a; ++i) b = rw::NowMs();
            Check(b > a && b - a < 1, "X5 NowMs steps under 1 ms (" + std::to_string(b - a) + " ms)");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            const double c = rw::NowMs() - b;
            Check(c >= 15 && c < 500, "X5 NowMs counts real milliseconds (" + std::to_string(c) + " ms over a 20 ms sleep)");
        }
        // the upload handshake's rules, on a second pass over the same queue.
        {
            rw::Compose c2;
            Check(c2.Setup(f.device.Get(), f.queue.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, Say), "K1 a second pass");
            const int ids[3] = {1, 2, 3};
            rw::AtlasImage img; rw::IconPack pk;
            rw::BuildIcons(ids, 3, 150, [](int id) { return SyntheticIcon(id); }, [] {}, img, pk);
            Check(c2.Stage(img) && !c2.Stage(img), "K1 (c) Stage twice without a Pump: the second is refused");
            const unsigned before = c2.Flips(rw::AtlasKind::Icons);
            int slices = 0;
            while (c2.Flips(rw::AtlasKind::Icons) == before && slices < 100) {
                if (c2.Pump()) ++slices;
                f.Finish();
                if (slices == 1) Check(c2.Flips(rw::AtlasKind::Icons) == before, "K1 (a) one slice in: not flipped yet");
            }
            Check(slices > 1 && c2.Flips(rw::AtlasKind::Icons) == before + 1, "K1 (a) after the last slice: flipped once");
            // Again, with the GPU held before the last slice: the staging stays the frame thread's until that slice is done.
            PumpUntil(c2, [&] { return c2.StagingFree(); }, 5000);
            Check(c2.Stage(img), "K1 the staging free again: a second image staged");
            for (int i = 0; i < slices - 1; ++i) { c2.Pump(); f.Finish(); }
            ComPtr<ID3D12Fence> blocker;
            Need(f.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&blocker)), "blocker fence");
            Need(f.queue->Wait(blocker.Get(), 1), "queue wait");
            c2.Pump();
            Check(c2.Flips(rw::AtlasKind::Icons) == before + 2, "K1 (a) the second image flipped at its last slice");
            for (int i = 0; i < 5; ++i) { c2.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
            Check(!c2.StagingFree(), "K1 (b) the GPU still holds the last slice: the staging is not handed back");
            Need(blocker->Signal(1), "unblock");
            Check(PumpUntil(c2, [&] { return c2.StagingFree(); }, 5000), "K1 (b) the last slice done: the staging handed back");
            Check(c2.Release(5000), "K1 released");
        }
        // a bright, half-transparent edge stays premultiplied through every mip.
        {
            auto soft = std::make_shared<icons::Image>();
            soft->width = soft->height = 64; soft->rgba.assign(64 * 64 * 4, 255);
            for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) soft->rgba[(size_t(y) * 64 + size_t(x)) * 4 + 3] = uint8_t(x * 4);
            const int id = 9;
            rw::AtlasImage img; rw::IconPack pk;
            rw::BuildIcons(&id, 1, 150, [&](int) { return std::shared_ptr<const icons::Image>(soft); }, [] {}, img, pk);
            int over = 0, partial = 0;
            for (size_t i = 0; i < img.data.size(); i += 4) {
                if (img.data[i] > img.data[i + 3] || img.data[i + 1] > img.data[i + 3] || img.data[i + 2] > img.data[i + 3]) ++over;
                if (img.data[i + 3] > 20 && img.data[i + 3] < 235) ++partial;
            }
            Check(partial > 1000 && over == 0, "K1 a white icon with an alpha ramp: premultiplied in every mip (" + std::to_string(partial) + " soft texels)");
        }
        // the game's sRGB target. A mid-gray texel (sRGB 128) at 50% must store sRGB(0.216 x 0.5) = 92, not 64 (a _SRGB view would
        // decode the premultiplied value and darken it).
        {
            Fixture fs(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
            rw::Compose cs;
            Check(cs.Setup(fs.device.Get(), fs.queue.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, Say), "K4 a pass on an sRGB target");
            auto half = std::make_shared<icons::Image>();
            half->width = half->height = 64; half->rgba.assign(64 * 64 * 4, 255);
            for (size_t i = 0; i < half->rgba.size(); ++i) half->rgba[i] = 128;   // mid gray (sRGB 128), 50%
            const int id = 9;
            rw::AtlasImage img; rw::IconPack pk;
            rw::BuildIcons(&id, 1, 100, [&](int) { return std::shared_ptr<const icons::Image>(half); }, [] {}, img, pk);
            Check(cs.Stage(img) && PumpUntil(cs, [&] { return cs.HasAtlas(rw::AtlasKind::Icons); }, 10000), "K4 the atlas in");
            rw::Instance one{rw::CanvasCx, rw::CanvasCy, 80, 80, 0};
            rw::CellRect(0, 100, one.u0, one.v0, one.u1, one.v1);
            one.recipe = rw::RecipeIcon; one.param = 0;
            const auto c = rw::Content(768, 100);
            Check(DrawWait(cs, fs.target.Get(), c, &one, 1) == rw::Compose::Result::Composed, "K4 composed");   // no raw Draw (a busy slot under load)
            const auto px = fs.Read(c.w, c.h);
            const uint8_t r = At(px, c.w, rw::CanvasCx, rw::CanvasCy, c, 0), a = At(px, c.w, rw::CanvasCx, rw::CanvasCy, c, 3);
            Check(std::abs(int(r) - 92) <= 4 && std::abs(int(a) - 128) <= 2, "K4 sRGB target: a mid-gray 50% texel stores " + std::to_string(r) + "/" + std::to_string(a) + " (want 92/128: linear 0.216 x 0.5, encoded)");
            fs.Finish();
            Check(cs.Release(5000), "K4 released");
        }
        // the safe free with a compose in flight. The queue waits on a fence only the CPU signals, so the
        // compose cannot finish: the bounded wait runs out and nothing is freed; once released, the free goes through.
        {
            ComPtr<ID3D12Fence> blocker;
            Need(f.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&blocker)), "blocker fence");
            Need(f.queue->Wait(blocker.Get(), 1), "queue wait");
            rw::Instance one[1] = {{rw::CanvasCx, rw::CanvasCy, 100, 100, 0, 0, 0, 0, 0, 1, 1, 1, 1, rw::RecipeDisc, 2}};
            const auto drawn = compose.Draw(f.target.Get(), rw::Content(768, 100), one, 1);
            {   // bound: the Off edge's WaitIdle on a GPU held past the bound: one event wait ended by a high-resolution
                // timer, false. Timings on a shared CPU only ever grow, so the fastest quiet try is the bound's measure; the
                // counter proves the precise path ran (the plain, tick-rounded wait fails it every time, not by luck).
                // a load changes from one moment to the next, so one sample cannot say the CPU is quiet. 20 tries, each beside
                // a bare high-resolution 16 ms timer wait with no GPU in it; the fastest of each is the measure, as a timing can only grow.
                // Not even one bare timer on time: the CPU was too busy to judge milliseconds (parallel builds; WARP runs on it).
                const auto bareTimer = [] {
                    HANDLE tm = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
                    LARGE_INTEGER due; due.QuadPart = -160000;
                    const auto c0 = std::chrono::steady_clock::now();
                    if (tm && SetWaitableTimer(tm, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(tm, 1000);
                    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count();
                    if (tm) CloseHandle(tm);
                    return ms;
                };
                constexpr int Tries = 20;
                double best = 1e9, worst = 0, calib = 1e9; bool anyIdle = false;
                const unsigned before = compose.PreciseWaits();
                for (int k = 0; k < Tries; ++k) {
                    calib = std::min(calib, bareTimer());
                    const auto t0 = std::chrono::steady_clock::now();
                    anyIdle = compose.WaitIdle(16) || anyIdle;
                    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    best = std::min(best, ms); worst = std::max(worst, ms);
                }
                const bool quietCpu = calib <= 17.5;
                double ms0 = 1e9; bool idle0 = false;
                for (int k = 0; k < 5; ++k) {
                    const auto t0 = std::chrono::steady_clock::now();
                    idle0 = compose.WaitIdle(0) || idle0;
                    ms0 = std::min(ms0, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                }
                std::printf("X1 WaitIdle(16) on a held GPU, %d tries: fastest %.1f ms, slowest %.1f ms; WaitIdle(0) %.2f ms; the fastest bare 16 ms timer %.1f ms%s\n",
                            Tries, best, worst, ms0, calib, quietCpu ? "" : " (CPU busy: timings not judged)");
                Check(!anyIdle && compose.PreciseWaits() == before + unsigned(Tries), "X1 WaitIdle(16) on a held GPU: false, each wait on the high-resolution timer");
                // Judged against the bare timer measured in the same run (WaitIdle adds only a fence query and a second handle);
                // the tick-rounded wait it replaces runs to 24-31 ms.
                if (quietCpu) Check(best >= 15.5 && best <= calib + 4 && best < 24, "X1 WaitIdle(16): the wait ends at the bound, " + std::to_string(best) +
                                    " ms (fastest of 20; the fastest bare timer " + std::to_string(calib) + " ms; the old tick-rounded wait 24-31)");
                Check(!idle0 && ms0 < 2, "X1 WaitIdle(0): no wait (" + std::to_string(ms0) + " ms)");
            }
            const auto t = std::chrono::steady_clock::now();
            const bool freed = compose.Release(16);
            const double waited = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
            Check(drawn == rw::Compose::Result::Composed && !freed && compose.Ready() && compose.Completed() < compose.Submitted(),
                "J1 a compose in flight: the bounded wait runs out, nothing freed, the compose kept");
            Check(waited >= 10 && waited < 500, "J1 the wait is bounded (" + std::to_string(int(waited)) + " ms)");
            {   // the GPU let go during the wait: WaitIdle returns at that moment, not at the bound or the next poll. WARP
                // runs the held compose on this same CPU, so one try can lose 100 ms to a build; a wait that sleeps or polls loses it
                // on every try. So the time from the fence's completion (a thread watching it) to WaitIdle's return is what is
                // judged: seven tries, each with its own held GPU, the median under 20 ms (it was 100 ms from the wait's start, which
                // counted WARP's own rendering; a true wait is back within a few ms). The median, not the fastest: a slow WaitIdle looks
                // on time in a try where WARP happens to finish later than its delay. "Not idle" is honest only at the bound: WARP under
                // load can take over 500 ms, but an early false (a stale wake) fails. Under three tries idle: printed, not judged.
                double slowestTotal = 0; bool honest = true; int idleTries = 0;
                std::array<double, 7> lates{};
                for (uint64_t k = 1; k <= 7; ++k) {
                    if (k > 1) {
                        Need(f.queue->Wait(blocker.Get(), k), "queue wait");
                        Check(compose.Draw(f.target.Get(), rw::Content(768, 100), one, 1) == rw::Compose::Result::Composed, "X1 a compose held again");
                    }
                    const uint64_t value = compose.Submitted();
                    std::chrono::steady_clock::time_point doneAt{};
                    std::thread release([&, k, value] {
                        std::this_thread::sleep_for(std::chrono::milliseconds(4));
                        blocker->Signal(k);
                        const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                        while (compose.Completed() < value && std::chrono::steady_clock::now() < giveUp) {}
                        doneAt = std::chrono::steady_clock::now();
                    });
                    const auto t0 = std::chrono::steady_clock::now();
                    const bool idle = compose.WaitIdle(500);
                    const auto t1 = std::chrono::steady_clock::now();
                    release.join();
                    const double total = std::chrono::duration<double, std::milli>(t1 - t0).count();
                    honest = honest && (idle || total >= 495);
                    slowestTotal = std::max(slowestTotal, total);
                    if (!idle) continue;
                    lates[size_t(idleTries++)] = std::max(0.0, std::chrono::duration<double, std::milli>(t1 - doneAt).count());
                }
                std::sort(lates.begin(), lates.begin() + idleTries);
                const double median = idleTries ? lates[size_t(idleTries / 2)] : 0, worst = idleTries ? lates[size_t(idleTries - 1)] : 0;
                std::printf("X1 WaitIdle, GPU let go at 4 ms, 7 tries, %d idle: median %.1f ms after the fence, worst %.1f ms (slowest wait %.1f ms)\n", idleTries, median, worst, slowestTotal);
                Check(honest && (idleTries < 3 || median < 20), "X1 WaitIdle returns when the GPU finishes (median " + std::to_string(median) + " ms after the fence, worst " + std::to_string(worst) + ", " + std::to_string(idleTries) + " of 7 idle; slowest wait " + std::to_string(slowestTotal) + " ms of a 500 ms bound)");
            }
            // Job B: the ring going Off with a save pending. The save holds the filled readback buffer and
            // nothing else, so the compose pass is freed under it (what the Off edge's worker does), the buffer is read after
            // that, and the file still comes out whole.
            rw::shot::SaveQueue saver;
            std::mutex m; std::condition_variable cv; bool open = false;
            std::string line;
            std::error_code ec;
            const auto file = std::filesystem::current_path() / "ring-wheel-shot-test-off.png";
            std::filesystem::remove(file, ec);
            const rw::ContentRect sc{256, 128, 0, 0, 256};
            rw::Instance two[2];
            two[0] = {40, 60, 60, 60, 0, 0, 0, 0, 0, .5f, .25f, .75f, 1, rw::RecipeRect, 0};
            two[1] = {150, 60, 60, 60, 0, 0, 0, 0, 0, .5f, .25f, .75f, .5f, rw::RecipeRect, 0};
            std::vector<uint8_t> want;   // the same list's picture, read at once
            {
                rw::ShotRows now; rw::shot::Raw raw;
                if (!compose.Shot(sc, two, 2, 5000, now) && now.Read(raw)) want = rw::shot::Picture(raw);
            }
            auto rows = std::make_shared<rw::ShotRows>();
            const double t0 = rw::shot::NowMs();
            const char* no = compose.Shot(sc, two, 2, 5000, *rows);
            const bool pushed = !no && saver.Push([&, rows, t0, name = file.wstring()](const rw::shot::SaveQueue::task& t) {
                { std::unique_lock l(m); cv.wait_for(l, std::chrono::seconds(30), [&] { return open; }); }
                rw::shot::Raw raw;
                rows->Read(raw);
                rw::shot::Finish(raw, name, t0, t, framevr::ringwheel::png::Save, [&](const char* s) { std::lock_guard l(m); line = s; });
            });
            rows.reset();   // the save's own now
            Check(pushed, std::string("shot save at Off: a shot taken and queued") + (no ? std::string(": ") + no : ""));
            Check(compose.Release(5000), "#14 the safe free once the GPU is done");
            Check(!compose.Ready() && saver.Waiting() == (pushed ? 1 : 0) && !std::filesystem::exists(file), "shot save at Off: the compose pass is freed with the save still pending");
            { std::lock_guard l(m); open = true; }
            cv.notify_all();
            saver.Drain();
            int pw = 0, ph = 0;
            const auto back = ReadPng(file, pw, ph);
            Check(pw == 256 && ph == 128 && !want.empty() && back == want && line.rfind("Frame capture: ring wheel saved ", 0) == 0, "shot save at Off: the file is whole and its line said after the ring's GPU side is gone (" + line + ")");
            std::filesystem::remove(file, ec);
        }
        Check(!compose.Ready() && compose.Submitted() == 0, "#14 released: nothing kept");
    } catch (const std::exception& e) {
        ++failures; std::printf("FAIL WARP: %s\n", e.what());
    }
    if (failures) { std::printf("ring-wheel-preview: %d failed\n", failures); return 1; }
    std::printf("ring-wheel-preview: all passed\n");
    return 0;
}
