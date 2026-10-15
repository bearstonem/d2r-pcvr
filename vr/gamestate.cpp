// gamestate.cpp - what the hero holds and whether a menu is open, read through
// D2RLoader's services and published for BodyWalk's D2R Bridge
// (shared/d2r_vr_state.h), which switches the mapping category from it.
//
// Items: the local player and his equipped items may only be read on the UI
// thread, so Tick() (vrcam's timer, 5 times a second) queues a read there.
// Body locations 4 and 5 are the right and left hand of the active set.
//
// Menus: D2R's UI sends messages ("target", "command", "text"). PanelManager's
// OpenPanel / OpenExclusivePanel / ClosePanel / UnloadPanel carry the panel's
// name in the text and UnloadAll clears them; the set of open panels is kept,
// and a menu is open while one of them is a menu (inventory, trade, stash,
// dialogue...) - not the HUD, the automap or the chat, which are open all game.
// The first guess (any "open" in a command) stuck at "menu" for the whole game:
// the HUD's own panels open that way and never close.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

#include <D2RLPlugin/api.h>

#include "d2r_vr_state.h"
#include "interactionstate.h"
#include "journal_ui.h"
#include "../steamvr/native_panels.h"
#include "../steamvr/callback_ticket.h"
#include "../steamvr/native_hud.h"
#include "../steamvr/attack_speed.h"
#include <mutex>
#ifdef D2RVR_NATIVE_OPENXR
#include "logging.h"
#include "panel_input.h"
#include "render_resolution.h"
#include "sigscan.h"
#include "memory.h"
#endif

using namespace D2RL;

namespace gamestate {
void __cdecl FinishMenuOpen(const PluginContext*,void*) noexcept;
void __cdecl DispatchAutoMap(const PluginContext*,void*) noexcept;
namespace {

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
// The map: the game's "AutoMap" panel, open or closed as PanelManager says
// (OpenPanel / ClosePanel 'AutoMap', the same messages its own key sends), and
// set by sending those messages ourselves (SetAutoMap) - no key, no button.
const WidgetService* g_widgets = nullptr;
framevr::nativehud::VisibilityOverride g_toolbarVisibility;
#ifdef D2RVR_NATIVE_OPENXR
void MaintainMapLayout(bool capture) {
    // D2R 3.3.93787 native automap mode setter: 0 full, 1 left, 2 right.
    // This updates transient map geometry only, not the saved options file.
    constexpr uint64_t modeRva=0xd3d80;
    constexpr uint8_t modeSig[]={0x48,0x83,0xec,0x28,0x83,0xf9,0x01,0x0f,0x94,0x05,0x47,0x70,0x95,0x02,
        0x3b,0x0d,0x3c,0x70,0x95,0x02,0x74,0x1d,0x33,0xc0,0x85,0xc9,0x0f,0x95,0xc0,
        0x89,0x05,0x2d,0x70,0x95,0x02};
    static bool checked=false;
    static framevr::wrist::MapLayoutLease layout;
    if(!checked){if(!d2rsig::Check(modeRva,modeSig,sizeof modeSig))return;checked=true;}
    int small=0;uint8_t left=0;
    if(!framevr::ReadMemory(&small,reinterpret_cast<void*>(d2rsig::Addr(0x2a2add0)),sizeof small)||
       !framevr::ReadMemory(&left,reinterpret_cast<void*>(d2rsig::Addr(0x2a2add5)),sizeof left)||small<0||small>1)return;
    const int action=layout.Step(capture,small?(left?1:2):0);
    if(action>=0)reinterpret_cast<void(*)(int)>(d2rsig::Addr(modeRva))(action);
}
using ToolbarUpdate=void(*)(void*);
ToolbarUpdate g_originalToolbarUpdate=nullptr;
void OnToolbarUpdate(void* panel) {
    if(hud::NativeToolbarHidden()) {
        uintptr_t widgets[2]{};
        const bool readable=panel&&framevr::ReadMemory(widgets,
            reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(panel)+0x170),sizeof widgets);
        if(framevr::nativehud::DeferUpdate(true,readable,widgets[0],widgets[1])) {
            static ULONGLONG told=0;const auto now=GetTickCount64();
            if(!told||now-told>=5000) {
                told=now;framevr::logging::Write("Frame HUD: deferred hidden toolbar update; notification widgets not ready");
            }
            return;
        }
    }
    g_originalToolbarUpdate(panel);
}
bool ProtectToolbarUpdate() {
    if(g_originalToolbarUpdate)return true;
    // 3.3.93787: HUDPanel update. Validate its entry AND both unconditional
    // widget reads; another build must match before we hide its native HUD.
    constexpr uint8_t entry[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20};
    constexpr uint8_t reads[]={0x49,0x8b,0x87,0x70,0x01,0x00,0x00,0x44,0x38,0xb0,0x45,0x01,0x00,0x00,0x75,0x2e,
        0x49,0x8b,0x87,0x78,0x01,0x00,0x00,0x44,0x38,0xb0,0x45,0x01,0x00,0x00,0x75,0x1e};
    if(!d2rsig::Check(0x150177b,reads,sizeof reads))return false;
    if(!d2rsig::Hook(0x1500fe0,entry,sizeof entry,reinterpret_cast<void*>(&OnToolbarUpdate),
        reinterpret_cast<void**>(&g_originalToolbarUpdate)))return false;
    framevr::logging::Write("Frame HUD: hidden toolbar notification guard installed");
    return true;
}
// UI-thread-only refreshes; native menu handlers may rebuild/show HUDPanel.
framevr::CallbackTicket g_toolbarTicket;
bool g_toolbarUpdating=false;
void MaintainToolbar(const PluginContext* ctx) {
    if(g_toolbarUpdating||!g_widgets||!g_widgets->findPanel||!g_widgets->setWidgetVisible)return;
    const bool hide=hud::NativeToolbarHidden()&&ProtectToolbarUpdate();
    if(!hide&&!g_toolbarVisibility.hidden)return;
    g_toolbarUpdating=true;
    Widgets::WidgetHandle toolbar=0;
    const bool found=g_widgets->findPanel(ctx,"HUDPanel",&toolbar)==Widgets::Result::Success&&toolbar;
    const bool previous=g_toolbarVisibility.hidden;
    g_toolbarVisibility.Update(hide,found,[&](bool visible){
        return g_widgets->setWidgetVisible(ctx,toolbar,visible)==Widgets::Result::Success;
    });
    if(previous!=g_toolbarVisibility.hidden)framevr::logging::Write(g_toolbarVisibility.hidden?
        "Frame HUD: native HUDPanel hidden for VR session":"Frame HUD: native HUDPanel visibility released");
    g_toolbarUpdating=false;
}
void __cdecl RefreshToolbar(const PluginContext* ctx,void* token) noexcept {
    if(g_toolbarTicket.Claim(reinterpret_cast<uintptr_t>(token))){MaintainToolbar(ctx);ReadNpcChoicesRect();}
}
void RefreshToolbarAfterMessage(const PluginContext* ctx) {
    if(g_toolbarUpdating)return;
    MaintainToolbar(ctx);
    if(!hud::NativeToolbarHidden()||!g_threads||!g_threads->runOnUiThread)return;
    // Also run after the native message handler, not only before it. Coalesce
    // bursts of panel messages; retry if the loader discards queued work.
    const auto now=GetTickCount64();
    if(g_toolbarTicket.Expired(now,1000))g_toolbarTicket.Cancel();
    const auto token=g_toolbarTicket.Start(now);
    if(token&&g_threads->runOnUiThread(ctx,&RefreshToolbar,reinterpret_cast<void*>(token))!=Threads::Result::Success)
        g_toolbarTicket.Claim(token);
}
#endif
std::atomic<int> g_autoMap{0};
std::atomic<uint64_t> g_menuMapAt{0};
framevr::wrist::MapLease g_menuMapLease;
const InventoryService* g_inventory = nullptr;
const ItemService* g_items = nullptr;
const SharedEventService* g_events = nullptr;
SharedEvents::ListenerHandle g_uiListener = 0;

HANDLE g_map = nullptr;
D2RVR_State* g_state = nullptr;
std::mutex g_readMutex;
framevr::CallbackTicket g_readTicket;
std::atomic<uint32_t> g_weapon{D2RVR_WEAPON_UNKNOWN};
std::atomic<uint32_t> g_menu{0};
std::atomic<uint32_t> g_menuEpoch{0};
std::atomic<bool> g_journalOnly{false};
std::atomic<bool> g_opaqueMenu{false};
std::atomic<bool> g_upperBandMenu{false};
std::atomic<bool> g_npcChoices{false};
std::mutex npcChoicesMutex;
int32_t npcChoicesRect[4]{};
uint64_t npcChoicesAt=0;
uint64_t g_menuOpenUntil=0; // UI thread only: wait for native layout transition
std::mutex journalMutex;
int32_t journalRect[4]{};
uint64_t journalAt=0;
std::atomic<uint32_t> g_set{0};      // 1 / 2: the active weapon set
std::atomic<uint32_t> g_view{0};     // vrcam's view: 0 off, 1 first person, 2 third person, 3 F1 in perspective, 4 the game on the floor

void Log(const char* msg) { if(g_ctx){
#ifdef D2RVR_NATIVE_OPENXR
    framevr::logging::Write(msg);
#else
    g_ctx->LogInfo(msg);
#endif
} }

std::string CodeText(uint32_t code) {
    char c[5] = {(char)(code & 0xFF), (char)((code >> 8) & 0xFF), (char)((code >> 16) & 0xFF), (char)((code >> 24) & 0xFF), 0};
    for (char& ch : c) if (ch && (ch < 32 || ch > 126)) ch = '?';
    return c;
}

// Weapons.txt codes: bows (normal, exceptional, elite, Amazon) and crossbows.
bool IsBow(const std::string& c) {
    static const std::set<std::string> k = {"sbw ", "hbw ", "lbw ", "cbw ", "sbb ", "lbb ", "swb ", "lwb ",
                                            "8sb ", "8hb ", "8lb ", "8cb ", "8s8 ", "8l8 ", "8sw ", "8lw ",
                                            "6sb ", "6hb ", "6lb ", "6cb ", "6s7 ", "6l7 ", "6sw ", "6lw ",
                                            "am1 ", "am2 ", "am6 ", "am7 ", "amb ", "amc "};
    return k.count(c) != 0;
}
bool IsCrossbow(const std::string& c) {
    static const std::set<std::string> k = {"lxb ", "mxb ", "hxb ", "rxb ", "8lx ", "8mx ", "8hx ", "8rx ", "6lx ", "6mx ", "6hx ", "6rx "};
    return k.count(c) != 0;
}

// The weapon's kind, from the game's tables: an Items row holds the index of
// its ItemTypes row, at an offset the SDK does not name. It is found once, on
// the UI thread (the tables answer only there): the u16 that holds the right
// type index for a short bow, a light crossbow, a short sword and a hand axe
// at once. Each kind's ItemTypes index then maps to a BodyWalk tab.
const DataTableService* g_tables = nullptr;
std::atomic<uint32_t> g_type{D2RVR_TYPE_UNKNOWN};
std::atomic<uint32_t> g_held{0};   // bit 0: something in the right hand's slot (body location 4), bit 1: the left's (5)
int g_typeOffset = -2;                       // -2 not tried yet, -1 failed
DataTables::Bank g_bank = DataTables::Bank::Lod;
std::vector<std::pair<uint32_t, uint32_t>> g_typeIndex;   // ItemTypes row index -> D2RVR_WeaponType
// Weapons.txt "2handed" in an Items row: a byte at an offset the SDK does not
// name either, found the same way - the one byte that reads 1 for a set of
// two-handed weapons and 0 for a set of one-handed ones (CalibrateTwoHanded).
int g_twoOffset = -2;                        // -2 not tried yet, -1 failed
std::atomic<uint32_t> g_two{0};
std::atomic<uint32_t> g_from{0};
std::atomic<bool> g_leftShield{false};
std::vector<uint32_t> g_shieldTypes;
std::atomic<uint32_t> g_handsKey{0};          // what is in the two hands, as one number (HandsKey)             // the hand slot the weapon kind came from: 0 right, 1 left              // D2RVR_TWO_HANDED* bits of what is held

constexpr uint32_t Four(const char* s) { return DataTables::MakeFourCC(s[0], s[1], s[2], s[3]); }

bool TypeRow(uint32_t code, uint32_t* index) {
    DataTables::RowView v{};
    v.structSize = DataTables::RowViewSize;
    if (g_tables->findRowByCode(g_ctx, g_bank, DataTables::TableId::ItemTypes, code, &v) != DataTables::Result::Success) return false;
    *index = v.rowIndex;
    return true;
}

const uint8_t* ItemRow(uint32_t code, uint32_t* size) {
    DataTables::RowView v{};
    v.structSize = DataTables::RowViewSize;
    if (g_tables->findRowByCode(g_ctx, g_bank, DataTables::TableId::Items, code, &v) != DataTables::Result::Success || !v.row) return nullptr;
    *size = v.rowSize;
    return (const uint8_t*)v.row;
}

void CalibrateTypes() {
    g_typeOffset = -1;
    if (!g_tables) { Log("gamestate: no data table service - weapon kinds unknown, tabs by bow/crossbow only"); return; }
    for (DataTables::Bank bank : {DataTables::Bank::Lod, DataTables::Bank::Rotw, DataTables::Bank::Classic}) {
        g_bank = bank;
        const char* probe[4][2] = {{"sbw ", "bow "}, {"lxb ", "xbow"}, {"ssd ", "swor"}, {"hax ", "axe "}};
        const uint8_t* rows[4]; uint32_t sizes[4], want[4];
        bool ok = true;
        for (int i = 0; i < 4 && ok; ++i) ok = (rows[i] = ItemRow(Four(probe[i][0]), &sizes[i])) && TypeRow(Four(probe[i][1]), &want[i]);
        if (!ok) continue;
        const uint32_t size = std::min({sizes[0], sizes[1], sizes[2], sizes[3]});
        for (uint32_t o = 0; o + 2 <= size; o += 2) {
            bool all = true;
            for (int i = 0; i < 4 && all; ++i) { uint16_t t; memcpy(&t, rows[i] + o, 2); all = t == want[i]; }
            if (all) { g_typeOffset = (int)o; break; }
        }
        if (g_typeOffset >= 0) break;
    }
    if (g_typeOffset < 0) { Log("gamestate: the item type field was not found - weapon kinds unknown, tabs by bow/crossbow only"); return; }
    static const struct { const char* code; uint32_t type; } kKinds[] = {
        {"bow ", D2RVR_TYPE_BOW}, {"abow", D2RVR_TYPE_BOW}, {"xbow", D2RVR_TYPE_CROSSBOW},
        {"swor", D2RVR_TYPE_SWORD}, {"axe ", D2RVR_TYPE_AXE},
        {"club", D2RVR_TYPE_MACE}, {"mace", D2RVR_TYPE_MACE}, {"hamm", D2RVR_TYPE_MACE},
        {"scep", D2RVR_TYPE_SCEPTER}, {"wand", D2RVR_TYPE_WAND}, {"staf", D2RVR_TYPE_STAFF},
        {"pole", D2RVR_TYPE_POLEARM}, {"spea", D2RVR_TYPE_SPEAR}, {"aspe", D2RVR_TYPE_SPEAR},
        {"knif", D2RVR_TYPE_DAGGER}, {"tkni", D2RVR_TYPE_THROWING}, {"taxe", D2RVR_TYPE_THROWING},
        {"jave", D2RVR_TYPE_JAVELIN}, {"ajav", D2RVR_TYPE_JAVELIN},
        {"h2h ", D2RVR_TYPE_CLAW}, {"h2h2", D2RVR_TYPE_CLAW}, {"orb ", D2RVR_TYPE_ORB}};
    std::string found;
    for(const char* code:{"shie","ashd","head"}) {
        uint32_t index=0;
        if(TypeRow(Four(code),&index))g_shieldTypes.push_back(index);
    }
    for (const auto& k : kKinds) {
        uint32_t idx;
        if (TypeRow(Four(k.code), &idx)) g_typeIndex.push_back({idx, k.type});
        else found += std::string(" -") + k.code;
    }
    char b[200];
    snprintf(b, sizeof b, "gamestate: item type at row offset 0x%X (bank %u), %d weapon kinds%s", g_typeOffset, (unsigned)g_bank,
             (int)g_typeIndex.size(), found.empty() ? "" : (", not in ItemTypes:" + found).c_str());
    Log(b);
}

// Weapons.txt "2handed" in the Items row (bank as CalibrateTypes found it): the
// byte - or failing that, the bit - that is 1 for every weapon of the first
// list and 0 for every one of the second. Swords stay out of the first try (a
// barbarian's sword is "1or2handed" as well, should a build keep that apart);
// they only narrow it down when more than one place fits.
void CalibrateTwoHanded() {
    g_twoOffset = -1;
    if (!g_tables || g_typeOffset < 0) return;
    // spear, bardiche, short staff, short bow, light crossbow, large axe, great axe, maul
    static const char* const kTwo[] = {"spr ", "bar ", "sst ", "sbw ", "lxb ", "lax ", "gax ", "mau "};
    // short sword, long sword, broad sword, scimitar, hand axe, axe, wand, club, dagger
    static const char* const kOne[] = {"ssd ", "lsd ", "bsd ", "scm ", "hax ", "axe ", "wnd ", "clb ", "dgr "};
    // two-handed sword, claymore, great sword
    static const char* const kSwords[] = {"2hs ", "clm ", "gsd "};
    struct Probe { const uint8_t* row; uint32_t size; uint8_t want; };
    std::vector<Probe> probes;
    auto add = [&](const char* const* codes, size_t count, uint8_t want) {
        for (size_t i = 0; i < count; ++i) {
            uint32_t size = 0;
            if (const uint8_t* r = ItemRow(Four(codes[i]), &size)) probes.push_back({r, size, want});
        }
    };
    add(kTwo, std::size(kTwo), 1);
    add(kOne, std::size(kOne), 0);
    if (probes.size() < 10) {
        char b[120]; snprintf(b, sizeof b, "gamestate: two-handed field not looked for - only %d probe weapons in the tables", (int)probes.size()); Log(b);
        return;
    }
    // candidates: (offset << 4) | bit, bit 8 = the whole byte
    auto find = [&](std::vector<uint32_t>& out) {
        out.clear();
        uint32_t size = UINT32_MAX;
        for (const Probe& p : probes) size = std::min(size, p.size);
        for (int bit = 8; bit >= 0 && out.empty(); --bit)   // whole bytes first
            for (uint32_t o = 0; o < size; ++o) {
                bool all = true;
                for (const Probe& p : probes) {
                    const uint8_t v = bit == 8 ? p.row[o] : (uint8_t)((p.row[o] >> bit) & 1u);
                    if (v != p.want) { all = false; break; }
                }
                if (all) out.push_back((o << 4) | (uint32_t)bit);
            }
    };
    std::vector<uint32_t> found;
    find(found);
    if (found.size() > 1) { add(kSwords, std::size(kSwords), 1); find(found); }
    std::string list;
    for (size_t i = 0; i < found.size() && i < 8; ++i) {
        char c[24]; snprintf(c, sizeof c, " 0x%X%s", found[i] >> 4, (found[i] & 15u) == 8 ? "" : (":" + std::to_string(found[i] & 15u)).c_str());
        list += c;
    }
    if (found.size() == 1) g_twoOffset = (int)found[0];
    char b[300];
    snprintf(b, sizeof b, "gamestate: two-handed field %s (%d place(s) fit:%s)", g_twoOffset >= 0 ? "found" : "NOT found - by kind only (spear, polearm, staff)",
             (int)found.size(), list.empty() ? " none" : list.c_str());
    Log(b);
    if (g_twoOffset >= 0) {   // what the swords read there: a barbarian's two-handed sword must be 1
        std::string swords;
        for (const char* s : kSwords) {
            uint32_t size = 0;
            const uint8_t* r = ItemRow(Four(s), &size);
            const uint32_t o = (uint32_t)g_twoOffset >> 4, bit = (uint32_t)g_twoOffset & 15u;
            swords += std::string(" ") + std::string(s, 3) + "=" + (r && o < size ? std::to_string(bit == 8 ? r[o] : (r[o] >> bit) & 1u) : "?");
        }
        snprintf(b, sizeof b, "gamestate: two-handed swords read:%s", swords.c_str());
        Log(b);
    }
}

// The item's "2handed"; -1 not known (no field found, or no such row).
int TwoHandedOf(const std::string& code) {
    if (code.size() != 4 || g_twoOffset < 0) return -1;
    uint32_t size = 0;
    const uint8_t* row = ItemRow(Four(code.c_str()), &size);
    const uint32_t o = (uint32_t)g_twoOffset >> 4, bit = (uint32_t)g_twoOffset & 15u;
    if (!row || o >= size) return -1;
    return bit == 8 ? row[o] != 0 : (int)((row[o] >> bit) & 1u);
}

// D2RVR_TYPE_* of one hand's item; 0 for nothing, or a quiver/shield (not a weapon).
uint32_t KindOf(const std::string& code) {
    if (code.size() != 4 || g_typeOffset < 0) return 0;
    uint32_t size = 0;
    const uint8_t* row = ItemRow(Four(code.c_str()), &size);
    if (!row || (uint32_t)g_typeOffset + 2 > size) return 0;
    uint16_t t; memcpy(&t, row + g_typeOffset, 2);
    for (const auto& [idx, type] : g_typeIndex) if (idx == t) return type;
    return 0;
}

bool IsShield(const std::string& code) {
    if(code.size()!=4||g_typeOffset<0)return false;
    uint32_t size=0;const auto* row=ItemRow(Four(code.c_str()),&size);uint16_t type=0;
    if(!row||uint32_t(g_typeOffset)+sizeof type>size)return false;
    memcpy(&type,row+g_typeOffset,sizeof type);
    return std::find(g_shieldTypes.begin(),g_shieldTypes.end(),type)!=g_shieldTypes.end();
}

// Weapons.txt "speed" (WSM) and "wclass"/"2handedwclass" in an Items row, for
// the melee attack cycle (attack_speed.h). Found like the two-handed flag: the
// one place every probe weapon reads its known value. WSM values are normal-
// tier bases from the Amazon Basin wiki ("Weapon speed"). A failed search
// leaves the cycle unknown, and an unknown cycle never gates melee.
int g_wsmOffset = -2, g_wsmWidth = 0;        // -2 not tried yet, -1 failed
int g_wclassOffset = -1, g_wclass2Offset = -1;
std::atomic<int> g_attackWeapon{-1}, g_attackWsm{0};
int SignedAt(const uint8_t* p, int width) {
    if (width == 4) { int32_t v; memcpy(&v, p, 4); return (int)v; }
    if (width == 2) { int16_t v; memcpy(&v, p, 2); return (int)v; }
    return (int)(int8_t)*p;
}
void CalibrateAttack() {
    g_wsmOffset = -1;
    if (!g_tables || g_typeOffset < 0) return;
    static const struct { const char* code; int wsm; } kSpeed[] = {
        {"ssd ", 0}, {"scm ", -20}, {"sbr ", -10}, {"flc ", 20}, {"lsd ", -10}, {"axe ", 10}, {"lax ", -10}, {"clb ", -10},
        {"mst ", 10}, {"whm ", 20}, {"gma ", 20}, {"dgr ", -20}, {"bld ", -10}, {"spr ", -10}, {"clm ", 10}, {"bar ", 10}};
    std::vector<std::pair<const uint8_t*, int>> speed;
    uint32_t speedSize = UINT32_MAX;
    for (const auto& k : kSpeed) {
        uint32_t size = 0;
        if (const uint8_t* r = ItemRow(Four(k.code), &size)) { speed.push_back({r, k.wsm}); speedSize = std::min(speedSize, size); }
    }
    std::vector<int> found;
    int width = 0;
    if (speed.size() >= 12)
        for (int w : {4, 2, 1}) {   // whole ints first
            for (uint32_t o = 0; o + w <= speedSize; o += w) {
                bool all = true;
                for (const auto& [row, wsm] : speed) if (SignedAt(row + o, w) != wsm) { all = false; break; }
                if (all) found.push_back((int)o);
            }
            if (!found.empty()) { width = w; break; }
        }
    if (found.size() == 1) { g_wsmOffset = found[0]; g_wsmWidth = width; }
    static const struct { const char* code; const char* wclass; } kClass[] = {
        {"ssd ", "1hs"}, {"dgr ", "1ht"}, {"jav ", "1ht"}, {"spr ", "2ht"}, {"sst ", "stf"}, {"sbw ", "bow"}, {"lxb ", "xbw"}};
    std::vector<std::pair<const uint8_t*, const char*>> classes;
    uint32_t classSize = UINT32_MAX;
    for (const auto& k : kClass) {
        uint32_t size = 0;
        if (const uint8_t* r = ItemRow(Four(k.code), &size)) { classes.push_back({r, k.wclass}); classSize = std::min(classSize, size); }
    }
    std::vector<int> places;
    if (classes.size() == std::size(kClass))
        for (uint32_t o = 0; o + 3 <= classSize; ++o) {
            bool all = true;
            for (const auto& [row, wclass] : classes) if (memcmp(row + o, wclass, 3) != 0) { all = false; break; }
            if (all) places.push_back((int)o);
        }
    // "wclass" and "2handedwclass" sit side by side and agree for these probes.
    if (places.size() == 1 || (places.size() == 2 && places[1] == places[0] + 4)) {
        g_wclassOffset = places[0];
        g_wclass2Offset = places.size() == 2 ? places[1] : -1;
    }
    char b[260];
    snprintf(b, sizeof b, "gamestate: attack speed fields - WSM %s at 0x%X/%d (%d place(s) fit, %d probes); weapon class %s at 0x%X (%d place(s) fit)",
             g_wsmOffset >= 0 ? "found" : "NOT found - WSM 0", g_wsmOffset >= 0 ? g_wsmOffset : 0, width, (int)found.size(), (int)speed.size(),
             g_wclassOffset < 0 ? "NOT found - melee cycle unknown" : g_wclass2Offset >= 0 ? "found with its two-handed column" : "found",
             g_wclassOffset >= 0 ? g_wclassOffset : 0, (int)places.size());
    Log(b);
}
// The attack cycle's inputs for one hand's item: weapon class (-1 unknown) and
// WSM (0 when the field is unknown). twoHands: a two-handed weapon in both.
void AttackInputs(const std::string& code, bool twoHands, int& weapon, int& wsm) {
    weapon = -1; wsm = 0;
    uint32_t size = 0;
    const uint8_t* row = code.size() == 4 ? ItemRow(Four(code.c_str()), &size) : nullptr;
    if (!row) return;
    const int column = twoHands && g_wclass2Offset >= 0 ? g_wclass2Offset : g_wclassOffset;
    framevr::attackspeed::Weapon w{};
    if (column >= 0 && (uint32_t)column + 3 <= size && framevr::attackspeed::FromCode(reinterpret_cast<const char*>(row + column), w))
        weapon = (int)w;
    if (g_wsmOffset >= 0 && (uint32_t)(g_wsmOffset + g_wsmWidth) <= size) wsm = SignedAt(row + g_wsmOffset, g_wsmWidth);
}

void Publish() {
    if (!g_state) return;
    g_state->weaponClass = g_weapon.load();
    g_state->menuOpen = g_menu.load();
    g_state->weaponSet = g_set.load();
    g_state->weaponType = g_type.load();
    g_state->viewMode = g_view.load();
    g_state->twoHanded = g_two.load();
    g_state->version = D2RVR_STATE_VERSION;
    g_state->counter++;
}

std::atomic<uint64_t> g_player{0};   // the local player's handle, as the SDK gives it (vrcam tries it as the hero unit)
void CalibrateObjectLights();   // the Objects table's light fields, below
extern int g_objLitOffset;

// On the UI thread: the two hands of the active set.
void __cdecl ReadItems(const PluginContext* ctx, void* token) noexcept {
    {std::lock_guard lock(g_readMutex);if(!g_readTicket.Claim(reinterpret_cast<uintptr_t>(token)))return;}
    PlayerHandle player = InvalidPlayerHandle;
    const bool havePlayer = g_inventory && g_inventory->getLocalPlayer(ctx, &player) == Inventory::Result::Success && player != InvalidPlayerHandle;
    g_player.store(havePlayer ? player : 0);
#ifdef D2RVR_NATIVE_OPENXR
    panelinput::MaintainWorldController(havePlayer,g_menu.load()!=0);
    MaintainToolbar(ctx);
    const auto now=GetTickCount64(),mapAt=g_menuMapAt.load();
    const bool mapWanted=mapAt&&now>=mapAt&&now-mapAt<400;
    const int mapAction=g_menuMapLease.Step(havePlayer?player:0,now,mapWanted,g_menu.load()!=0,g_autoMap.load()!=0);
    // Keep native widget calls on this UI callback, never on the XR thread.
    const bool capture=g_menuMapLease.Active(now,g_menu.load()!=0);
    MaintainMapLayout(capture);
    if(mapAction>=0)DispatchAutoMap(ctx,mapAction?(void*)1:nullptr);
    hud::MenuMapCapture(capture);
#endif
    if(!havePlayer)g_menuOpenUntil=0;
    else if(g_menuOpenUntil)FinishMenuOpen(ctx,nullptr);
    if (!g_inventory || !g_items || !havePlayer) {
        g_weapon.store(D2RVR_WEAPON_UNKNOWN);
        g_type.store(D2RVR_TYPE_UNKNOWN);
        g_held.store(0);
        g_leftShield=false;
        g_two.store(0);
        g_set.store(0);
#ifdef D2RVR_NATIVE_OPENXR
        interactionstate::Capture();
#endif
        Publish();
        return;
    }
    // Body locations 4/5 are the hands as the game asks for them; 11/12 the
    // other set's. Which set is active - I or II - the SDK does not say, so:
    // the item's own body location, if it reads 11/12 while it sits in the
    // hands, says set II; failing that, every swap (the two pairs trading
    // places) flips the count, starting from set I.
    std::string code[2];          // [0] right (body location 4), [1] left (5)
    int32_t ownLoc[2] = {-1, -1}; // the items' own body location
    uint32_t id[4] = {};          // runtime ids at 4, 5, 11, 12
    std::string other[2];
    for (int i = 0; i < 4; ++i) {
        const int loc = i < 2 ? 4 + i : 9 + i;
        ItemHandle item = InvalidItemHandle;
        if (g_inventory->getEquippedItem(ctx, player, loc, &item) != Inventory::Result::Success || item == InvalidItemHandle) continue;
        Items::ItemInfo info{};
        info.structSize = sizeof info;
        if (g_items->getItemInfo(ctx, item, &info) != Items::Result::Success) continue;
        id[i] = info.runtimeId ? info.runtimeId : info.code;
        if (i < 2) { code[i] = CodeText(info.code); ownLoc[i] = info.bodyLocation; }
        else other[i - 2] = CodeText(info.code);
    }
    static uint32_t lastHands[2] = {}, lastOther[2] = {};
    static bool seen = false;
    static PlayerHandle lastPlayer=InvalidPlayerHandle;
    if(lastPlayer!=player){seen=false;g_set.store(0);lastPlayer=player;}
    uint32_t set = g_set.load();
    if (ownLoc[0] == 11 || ownLoc[1] == 12) set = 2;                    // the game's own word for it
    else if (ownLoc[0] == 4 || ownLoc[1] == 5 || (seen&&!id[0]&&!id[1])) {
        // no word: a swap shows as the hands holding what the other set held
        const bool swapped = seen && (id[0] != lastHands[0] || id[1] != lastHands[1]) &&
                             id[0] == lastOther[0] && id[1] == lastOther[1] &&
                             id[2] == lastHands[0] && id[3] == lastHands[1];
        if (set == 0) set = 1;
        else if (swapped) set = set == 1 ? 2 : 1;
    }
    if (id[0] || id[1] || id[2] || id[3]) {
        seen = true;
        lastHands[0] = id[0]; lastHands[1] = id[1]; lastOther[0] = id[2]; lastOther[1] = id[3];
    }
    if (set != g_set.load()) {
        g_set.store(set);
        char b[200];
        snprintf(b, sizeof b, "gamestate: weapon set %u (hands '%s' '%s' own locations %d %d, other set '%s' '%s')", set,
                 code[0].c_str(), code[1].c_str(), ownLoc[0], ownLoc[1], other[0].c_str(), other[1].c_str());
        Log(b);
    }
    uint32_t cls = D2RVR_WEAPON_NONE;
    for (const std::string& c : code) {
        if (IsBow(c)) cls = D2RVR_WEAPON_BOW;
        else if (IsCrossbow(c) && cls != D2RVR_WEAPON_BOW) cls = D2RVR_WEAPON_CROSSBOW;
    }
    if (cls == D2RVR_WEAPON_NONE && (!code[0].empty() || !code[1].empty())) cls = D2RVR_WEAPON_MELEE;
    // The kind, whichever set: the right hand's weapon, else the left's (a bow
    // sits in the left with the quiver in the right); a hand holding no weapon
    // kind we know (a shield) is skipped. Unknown kinds fall back to the class.
    if (g_typeOffset == -2) CalibrateTypes();
    // Native inventory body locations do not identify the drawn VR wrist.
    // The tracked shield is on the left wrist even when the SDK reports it
    // in body location 4 (observed with the development Paladin).
    g_leftShield=IsShield(code[0])||IsShield(code[1]);
    if (g_objLitOffset == -2) CalibrateObjectLights();
    if (g_twoOffset == -2 && g_typeOffset >= 0) CalibrateTwoHanded();
    uint32_t kind = KindOf(code[0]);
    int from = 0;   // the hand the kind came from
    if (!kind && (kind = KindOf(code[1])) != 0) from = 1;
    if (!kind) kind = cls == D2RVR_WEAPON_BOW ? D2RVR_TYPE_BOW : cls == D2RVR_WEAPON_CROSSBOW ? D2RVR_TYPE_CROSSBOW
                    : cls == D2RVR_WEAPON_MELEE ? D2RVR_TYPE_OTHER : D2RVR_TYPE_UNARMED;
    g_type.store(kind);
    g_held.store((code[0].empty() ? 0u : 1u) | (code[1].empty() ? 0u : 2u));
    // Two-handed: the weapon's own "2handed"; without the field, a spear, a polearm
    // or a staff (all two-handed in D2) and nothing else. In both hands: the other
    // hand's slot is empty too - a barbarian's two-handed sword beside another weapon
    // is held in one (a bow's quiver sits in the other slot, but a bow is never held so).
    uint32_t two = 0;
    if (const int t = TwoHandedOf(code[from]); t >= 0) two = (t ? D2RVR_TWO_HANDED : 0u) | D2RVR_TWO_HANDED_KNOWN;
    else if (kind == D2RVR_TYPE_SPEAR || kind == D2RVR_TYPE_POLEARM || kind == D2RVR_TYPE_STAFF) two = D2RVR_TWO_HANDED;
    if ((two & D2RVR_TWO_HANDED) && code[from ^ 1].empty()) two |= D2RVR_TWO_HANDS_ON;
    if (KindOf(code[1]) != 0) two |= D2RVR_LEFT_WEAPON;   // a weapon in the left hand (KindOf: no shield, no quiver)
    g_two.store(two);
    g_from.store((uint32_t)from);
    if (g_wsmOffset == -2 && g_typeOffset >= 0) CalibrateAttack();
    {   // The melee attack cycle's weapon: nothing but shields in hand is hand-to-hand.
        int weapon = -1, wsm = 0;
        if ((code[0].empty() || IsShield(code[0])) && (code[1].empty() || IsShield(code[1])))
            weapon = (int)framevr::attackspeed::Weapon::HandToHand;
        else AttackInputs(code[from], (two & D2RVR_TWO_HANDS_ON) != 0, weapon, wsm);
        g_attackWeapon.store(weapon);
        g_attackWsm.store(wsm);
    }
    {   // FNV-1a over both codes: a new grip for any change of what the hands hold
        uint32_t h = 2166136261u;
        for (const std::string* c : {&code[0], &code[1]}) {
            for (const char ch : *c) { h ^= (uint8_t)ch; h *= 16777619u; }
            h ^= 0xFFu; h *= 16777619u;
        }
        g_handsKey.store(h);
    }
    static std::string told[2];
    if (code[0] != told[0] || code[1] != told[1]) {
        told[0] = code[0]; told[1] = code[1];
        char b[200];
        snprintf(b, sizeof b, "gamestate: hands right '%s' left '%s' -> %s, tab D2R %s%s%s", code[0].c_str(), code[1].c_str(),
                 cls == D2RVR_WEAPON_BOW ? "bow" : cls == D2RVR_WEAPON_CROSSBOW ? "crossbow" : cls == D2RVR_WEAPON_MELEE ? "melee" : "empty",
                 kD2RVRWeaponTypeNames[kind], !(two & D2RVR_TWO_HANDED) || cls != D2RVR_WEAPON_MELEE ? "" : (two & D2RVR_TWO_HANDS_ON) ? ", two-handed, in both hands" : ", two-handed, in one hand (the other holds something)",
                 (two & D2RVR_TWO_HANDED) && cls == D2RVR_WEAPON_MELEE && !(two & D2RVR_TWO_HANDED_KNOWN) ? " (by kind)" : "");
        Log(b);
    }
    g_weapon.store(cls);
    if (g_state) {
        memset(g_state->rightCode, ' ', 4); memset(g_state->leftCode, ' ', 4);
        memcpy(g_state->rightCode, code[0].data(), std::min<size_t>(4, code[0].size()));
        memcpy(g_state->leftCode, code[1].data(), std::min<size_t>(4, code[1].size()));
    }
    Publish();
#ifdef D2RVR_NATIVE_OPENXR
    interactionstate::Capture();
#endif
    ReadJournalRect();
    ReadNpcChoicesRect();
}

std::set<std::string> g_panels;   // open panels, UI thread only

// A panel that takes the controller away from the game. Names as D2R 3.3
// loads them (2026-10-02 log): VendorPanelLayout, PlayerInventoryExpansionLayout,
// BankExpansionLayout, HoradricCubeLayout, CharacterStatsPanel, SkillsTreePanel,
// SkillSelect, QuestLogPanelExpansion, PauseLayoutGarden. The HUD's own panels
// (HUDPanel, MiniMenuPanel, HireablesPanel, TooltipsPanel, ShowItemsPanel...)
// stay open all game and are not menus - "Menu" alone matched MiniMenuPanel.
bool IsMenuPanel(const std::string& n) {
    return framevr::panels::Modal(n);
}

bool Has(const char* s, const char* part) {
    if (!s) return false;
    for (; *s; ++s) if (_strnicmp(s, part, strlen(part)) == 0) return true;
    return false;
}

SharedEvents::UiMessageAction __cdecl OnUiMessage(const PluginContext* ctx, const SharedEvents::UiMessageEvent* e, void*) noexcept {
    if (!e) return SharedEvents::UiMessageAction::Continue;
    const char* target = e->target ? e->target : "";
    const char* command = e->command ? e->command : "";
    const char* text = e->text ? e->text : "";
    static std::set<std::string> seen;
    std::string key = std::string(target) + ":" + command;
    const std::string keyText = key + ":" + text;   // the panel's name is in the text
    if (seen.size() < 4000 && seen.insert(keyText).second && strcmp(target, "InputMessage") != 0) {
        char b[300];
        snprintf(b, sizeof b, "gamestate: ui message '%s' '%s' text '%.80s'", target, command, text);
        Log(b);
    }
    if (strcmp(target, "PanelManager") == 0) {
        const bool before = g_menu.load() != 0;
        if (!strcmp(command, "OpenPanel") || !strcmp(command, "OpenExclusivePanel")) { if (*text) g_panels.insert(text); }
        else if (!strcmp(command, "ClosePanel") || !strcmp(command, "UnloadPanel")) g_panels.erase(text);
        else if (!strcmp(command, "UnloadAll") || !strcmp(command, "CloseAll")) g_panels.clear();
        g_autoMap.store(g_panels.count("AutoMap") ? 1 : 0);
        g_upperBandMenu.store(std::any_of(g_panels.begin(),g_panels.end(),
            [](const auto& name){return framevr::panels::UpperBand(name);}));
        g_opaqueMenu.store(std::any_of(g_panels.begin(),g_panels.end(),
            [](const auto& name){return framevr::panels::OpaqueBackground(name);}));
        bool menu = false;
        for (const std::string& n : g_panels) if (IsMenuPanel(n)) { menu = true; break; }
        if(menu!=before)++g_menuEpoch;
        g_menu.store(menu ? 1 : 0);
        int menus=0;bool quest=false;
        for(const auto& name:g_panels)if(IsMenuPanel(name)){++menus;quest=name.find("QuestLog")!=std::string::npos;}
        g_journalOnly=menus==1&&quest;
        const bool npc=menus==1&&g_panels.count("NpcDialogPanel")!=0;
        if(g_npcChoices.exchange(npc)!=npc){std::lock_guard lock(npcChoicesMutex);npcChoicesAt=0;}
        if(g_journalOnly.load())ReadJournalRect();
        if (menu != before) {
            std::string list;
            for (const std::string& n : g_panels) list += n + " ";
            char b[300]; snprintf(b, sizeof b, "gamestate: menu %s (open: %.200s)", menu ? "OPEN" : "closed", list.c_str()); Log(b);
        }
    }
#ifdef D2RVR_NATIVE_OPENXR
    // Native AcceptChanges is observed even when the output dimensions stay
    // unchanged. Do not listen to ResolutionChanged here: our own WM_SIZE
    // recovery emits that event and would schedule another resize forever.
    if(!strcmp(target,"OptionsPanelMessage")&&!strcmp(command,"AcceptChanges"))
        framevr::resolution::OptionsApplied();
    if(!strcmp(target,"PanelManager")||!strcmp(target,"HUDPanel")||
       (!strcmp(target,"SystemMessage")&&!strcmp(command,"ResolutionChanged")))RefreshToolbarAfterMessage(ctx);
#endif
    if (g_state) {
        strncpy_s(g_state->lastUi, key.c_str(), _TRUNCATE);
        Publish();
    }
    return SharedEvents::UiMessageAction::Continue;
}

// Which objects give light, from the game's own Objects table (Objects.txt): Lit0..Lit7,
// the light's radius in each of the object's modes (a torch: 0 unlit, 19 burning), and
// Red/Green/Blue, its colour. The compiled row's offsets are not named by the SDK: found
// once, on the UI thread, as the bytes that read Objects.txt's values for five objects
// at once - a brazier, a tiki torch, a bonfire, a small fire and hellfire (2026-10-07).
struct ObjLight { uint8_t lit[8]; uint8_t rgb[3]; };
constexpr uint32_t kObjMax = 1024;
ObjLight g_objLight[kObjMax];
std::atomic<uint32_t> g_objCount{0};   // rows known; 0 = not (yet)
int g_objLitOffset = -2;                // -2 not tried yet, -1 failed (declared above ReadItems)

void CalibrateObjectLights() {
    g_objLitOffset = -1;
    if (!g_tables || !g_tables->getTable) return;
    static const struct { uint32_t id; uint8_t lit[8]; uint8_t rgb[3]; } kProbe[] = {
        {29, {0, 19, 18, 0, 0, 0, 0, 0}, {255, 236, 176}},   // Brazier
        {37, {0, 19, 19, 0, 0, 0, 0, 0}, {255, 236, 176}},   // TikiTorch1
        {39, {0, 19, 19, 0, 0, 0, 0, 0}, {255, 236, 176}},   // RogueBonfire
        {160, {0, 16, 17, 0, 0, 0, 0, 0}, {255, 255, 255}},  // FireSmall
        {345, {6, 0, 0, 0, 0, 0, 0, 0}, {255, 100, 100}}};   // Hellfire1
    for (DataTables::Bank bank : {DataTables::Bank::Lod, DataTables::Bank::Rotw, DataTables::Bank::Classic}) {
        DataTables::TableView v{};
        v.structSize = DataTables::TableViewSize;
        if (g_tables->getTable(g_ctx, bank, DataTables::TableId::Objects, &v) != DataTables::Result::Success || !v.rows || v.rowCount <= 345 || !v.rowSize)
            continue;
        const uint8_t* rows = (const uint8_t*)v.rows;
        int lit = -1, rgb = -1;
        for (uint32_t o = 0; o + 8 <= v.rowSize && (lit < 0 || rgb < 0); ++o) {
            bool l = lit < 0, c = rgb < 0 && o + 3 <= v.rowSize;
            for (const auto& p : kProbe) {
                const uint8_t* r = rows + (size_t)p.id * v.rowSize;
                l = l && memcmp(r + o, p.lit, 8) == 0;
                c = c && memcmp(r + o, p.rgb, 3) == 0;
            }
            if (l) lit = (int)o;
            if (c) rgb = (int)o;
        }
        char b[160];
        snprintf(b, sizeof b, "gamestate: object lights - bank %u, %u rows of %u bytes, Lit at %d, colour at %d", (unsigned)bank, v.rowCount, v.rowSize, lit, rgb);
        Log(b);
        if (lit < 0 || rgb < 0) continue;
        const uint32_t n = std::min(v.rowCount, kObjMax);
        uint32_t lights = 0;
        for (uint32_t i = 0; i < n; ++i) {
            const uint8_t* r = rows + (size_t)i * v.rowSize;
            memcpy(g_objLight[i].lit, r + lit, 8);
            memcpy(g_objLight[i].rgb, r + rgb, 3);
            for (uint8_t x : g_objLight[i].lit) if (x) { ++lights; break; }
        }
        g_objLitOffset = lit;
        g_objCount.store(n);
        snprintf(b, sizeof b, "gamestate: object lights - %u of %u objects give light", lights, n);
        Log(b);
        return;
    }
    Log("gamestate: object lights NOT found in the Objects table - the ceiling's torches stay as seen in the picture");
}

template <class S> const S* Query(const PluginContext* ctx) {
    const S* s = nullptr;
    return ctx->QueryService(&s) == ServiceQueryResult::Success ? s : nullptr;
}

}  // namespace

// From D2RLoaderLoadPlugin.
void Init(const PluginContext* ctx) {
    g_ctx = ctx;
#ifdef D2RVR_NATIVE_OPENXR
    interactionstate::Init(ctx);
#endif
    g_threads = Query<ThreadService>(ctx);
    g_widgets = Query<WidgetService>(ctx);
    Log(g_widgets ? "gamestate: widgets ok - the map can be opened and closed directly" : "gamestate: widgets MISSING - the map cannot be set");
    g_inventory = Query<InventoryService>(ctx);
    g_items = Query<ItemService>(ctx);
    g_events = Query<SharedEventService>(ctx);
    g_tables = Query<DataTableService>(ctx);
    char b[160];
    snprintf(b, sizeof b, "gamestate: threads %s, inventory %s, items %s, events %s", g_threads ? "ok" : "MISSING",
             g_inventory ? "ok" : "MISSING", g_items ? "ok" : "MISSING", g_events ? "ok" : "MISSING");
    Log(b);
    if (g_events) {
        SharedEvents::UiMessageListener l{};
        l.structSize = sizeof l;
        l.priority = -1000;   // after everyone: only listens
        l.callback = &OnUiMessage;
        if (g_events->registerUiMessageListener(ctx, &l, &g_uiListener) != SharedEvents::Result::Success) Log("gamestate: could not listen to UI messages");
    }
    // Low integrity may read it too (BodyWalk and the game can run at different levels).
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, FALSE};
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)", SDDL_REVISION_1, &sd, nullptr))
        sa.lpSecurityDescriptor = sd;
    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(D2RVR_State), D2RVR_STATE_NAME);
    if (sd) LocalFree(sd);
    if (g_map) g_state = (D2RVR_State*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(D2RVR_State));
    if (g_state) { memset(g_state, 0, sizeof *g_state); g_state->version = D2RVR_STATE_VERSION; }
}

// vrcam's timer, every 200 ms: queue a read of the hands on the UI thread.
void Tick() {
    if (!g_threads || !g_ctx) return;
    uintptr_t token=0;bool recovered=false;
    {std::lock_guard lock(g_readMutex);
     if(g_readTicket.Expired(GetTickCount64(),1000)){g_readTicket.Cancel();recovered=true;}
     token=g_readTicket.Start(GetTickCount64());}
    if(recovered)Log("gamestate: recovering an abandoned UI read callback");
    if(!token)return;
    if(g_threads->runOnUiThread(g_ctx,&ReadItems,reinterpret_cast<void*>(token))!=Threads::Result::Success) {
        std::lock_guard lock(g_readMutex);g_readTicket.Claim(token);
    }
}

uint32_t WeaponClass() { return g_weapon.load(); }
uint32_t WeaponSet() { return g_set.load(); }
uint32_t WeaponType() { return g_type.load(); }
uint32_t HandsHeld() { return g_held.load(); }
bool LeftShield() {return g_leftShield.load();}
uint32_t TwoHanded() { return g_two.load(); }
uint32_t WeaponHand() { return g_from.load(); }
uint32_t HandsKey() { return g_handsKey.load(); }
int AttackWeapon() { return g_attackWeapon.load(); }
int AttackWsm() { return g_attackWsm.load(); }
bool MenuOpen() { return g_menu.load() != 0; }
uint32_t MenuEpoch() { return g_menuEpoch.load(); }
uint64_t LocalPlayer() { return g_player.load(); }
uint32_t DataBank() {return uint32_t(g_bank);} // UI-thread-only table bank
void MenuMap(bool wanted) {g_menuMapAt.store(wanted?GetTickCount64():0);}
// An object's light in a mode: its radius (Objects.txt Lit, 0 = none) and colour 0..1.
int ObjectLight(uint32_t txt, uint32_t mode, float rgb[3]) {
    if (txt >= g_objCount.load() || mode > 7) return 0;
    const ObjLight& o = g_objLight[txt];
    for (int c = 0; c < 3; ++c) rgb[c] = o.rgb[c] / 255.0f;
    return o.lit[mode];
}

bool JournalOnly(){return g_journalOnly.load();}
bool OpaqueMenuOpen(){return g_opaqueMenu.load();}
bool UpperBandMenuOpen(){return g_upperBandMenu.load();}
bool NpcChoicesOnly(){return g_npcChoices.load();}
void ReadNpcChoicesRect() {
    if(!g_npcChoices.load()||!g_widgets||!g_widgets->getWidgetRect||!g_widgets->findPanel)return;
    Widgets::WidgetHandle panel=0,border=0;Widgets::Rect root{},box{};
    if(g_widgets->findPanel(g_ctx,"NpcDialogPanel",&panel)!=Widgets::Result::Success||
       g_widgets->getWidgetRect(g_ctx,panel,&root)!=Widgets::Result::Success)return;
    box=root;
    // DialogBorder includes the title, all available choices and the outline;
    // the panel also contains a full-screen click catcher which is not content.
    if(g_widgets->findWidget&&g_widgets->findWidget(g_ctx,panel,"DialogBorder",&border)==Widgets::Result::Success) {
        Widgets::Rect child{};
        if(g_widgets->getWidgetRect(g_ctx,border,&child)==Widgets::Result::Success&&child.width>0&&child.height>0)
            box={root.x+child.x,root.y+child.y,child.width,child.height};
    }
    if(box.width<16||box.height<16||box.width>3840||box.height>2160||
       box.x<0||box.y<0||box.x>8192||box.y>8192)return;
    std::lock_guard lock(npcChoicesMutex);
    if(!npcChoicesAt||npcChoicesRect[2]!=box.width||npcChoicesRect[3]!=box.height) {
        char b[180];snprintf(b,sizeof b,"Frame NPC choices: native bounds %d,%d %dx%d",box.x,box.y,box.width,box.height);Log(b);
    }
    npcChoicesRect[0]=box.x;npcChoicesRect[1]=box.y;npcChoicesRect[2]=box.width;npcChoicesRect[3]=box.height;npcChoicesAt=GetTickCount64();
}
bool NpcChoicesRect(int32_t out[4]) {
    std::lock_guard lock(npcChoicesMutex);
    if(!g_npcChoices.load()||!npcChoicesAt||GetTickCount64()-npcChoicesAt>650)return false;
    std::copy_n(npcChoicesRect,4,out);return true;
}
void ReadJournalRect() {
    if(!g_journalOnly.load()||!g_widgets)return;
    Widgets::WidgetHandle panel=0;Widgets::Rect r{};
    if(g_widgets->findPanel(g_ctx,"QuestLogPanelExpansion",&panel)!=Widgets::Result::Success)
        if(g_widgets->findPanel(g_ctx,"QuestLogPanelOriginal",&panel)!=Widgets::Result::Success)return;
    if(g_widgets->getWidgetRect(g_ctx,panel,&r)!=Widgets::Result::Success||r.width<100||r.height<100||r.width>4096||r.height>4096)return;
    std::lock_guard lock(journalMutex);journalRect[0]=r.x;journalRect[1]=r.y;journalRect[2]=r.width;journalRect[3]=r.height;journalAt=GetTickCount64();
}
bool JournalRect(int32_t out[4]) {
    std::lock_guard lock(journalMutex);if(!g_journalOnly.load()||GetTickCount64()-journalAt>650)return false;
    std::copy_n(journalRect,4,out);return true;
}
bool AutoMapOpen() { return g_autoMap.load() != 0; }

// On the UI thread, as widget calls must be: the same message the game's own
// map key sends, so the game opens or closes its map itself.
void __cdecl DispatchAutoMap(const PluginContext* ctx, void* user) noexcept {
    if (!g_widgets || !g_widgets->dispatchUiAction) return;
    const bool open = user != nullptr;
    Widgets::UiAction a{};
    a.structSize = sizeof a;
    a.target = "PanelManager";
    a.command = open ? "OpenPanel" : "ClosePanel";
    a.text = "AutoMap";
    const Widgets::Result r = g_widgets->dispatchUiAction(ctx, &a);
    char b[120];
    snprintf(b, sizeof b, "gamestate: map %s sent to the game (result %u)", open ? "OPEN" : "CLOSE", (unsigned)r);
    Log(b);
}

// The name plate over the target monster: the game's HUDMonsterHealth panel. Its
// rectangle, read on the UI thread (widget calls must be) a few times a second.
std::atomic<int32_t> g_plate[4] = {};
std::atomic<ULONGLONG> g_plateAt{0};
Widgets::WidgetHandle g_platePanel = Widgets::InvalidHandle;
void __cdecl ReadPlate(const PluginContext* ctx, void*) noexcept {
    if (!g_widgets || !g_widgets->findPanel || !g_widgets->getWidgetRect) return;
    static int told = 0;
    static Widgets::Rect last{};
    Widgets::Result r = Widgets::Result::Success;
    if (g_platePanel == Widgets::InvalidHandle) r = g_widgets->findPanel(ctx, "HUDMonsterHealth", &g_platePanel);
    Widgets::Rect rc{};
    if (r == Widgets::Result::Success && g_platePanel != Widgets::InvalidHandle) r = g_widgets->getWidgetRect(ctx, g_platePanel, &rc);
    if (r == Widgets::Result::StaleHandle || r == Widgets::Result::NotFound) g_platePanel = Widgets::InvalidHandle;
    const bool ok = r == Widgets::Result::Success && rc.width > 0 && rc.height > 0;
    if (ok) {
        g_plate[0].store(rc.x); g_plate[1].store(rc.y); g_plate[2].store(rc.width); g_plate[3].store(rc.height);
        g_plateAt.store(GetTickCount64());
    }
    if (told < 6 && (!ok || rc.x != last.x || rc.y != last.y || rc.width != last.width || rc.height != last.height)) {
        ++told;
        last = rc;
        char b[160];
        snprintf(b, sizeof b, "gamestate: the monster's name plate (HUDMonsterHealth) - result %u, at %d,%d size %dx%d",
                 (unsigned)r, rc.x, rc.y, rc.width, rc.height);
        Log(b);
    }
}
void PollPlate() {
    if (g_ctx && g_threads && g_threads->runOnUiThread && g_widgets) g_threads->runOnUiThread(g_ctx, &ReadPlate, nullptr);
}
// The plate's rectangle (x, y, w, h, the panel's own units), if read lately.
bool PlateRect(int32_t out[4]) {
    if (GetTickCount64() - g_plateAt.load() > 1000) return false;
    for (int i = 0; i < 4; ++i) out[i] = g_plate[i].load();
    return true;
}

bool SetAutoMap(bool open) {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || !g_widgets) return false;
    return g_threads->runOnUiThread(g_ctx, &DispatchAutoMap, open ? (void*)1 : nullptr) == Threads::Result::Success;
}

void __cdecl FinishMenuOpen(const PluginContext* ctx,void*) noexcept {
    if(!g_menuOpenUntil)return;
    if(GetTickCount64()>=g_menuOpenUntil){g_menuOpenUntil=0;Log("gamestate: controller layout not ready; tabbed menu opening cancelled");return;}
#ifdef D2RVR_NATIVE_OPENXR
    if(!panelinput::ControllerReady())return;
#endif
    g_menuOpenUntil=0;
    Widgets::UiAction action{};action.structSize=sizeof action;
    action.target="PanelManager";action.text=framevr::panels::TabbedMenu;action.command="OpenPanel";
    const auto result=g_widgets->dispatchUiAction(ctx,&action);
    char line[180];snprintf(line,sizeof line,"gamestate: left menu button OpenPanel UISwitcher in controller mode (result %u)",unsigned(result));Log(line);
}
void __cdecl DispatchMenuToggle(const PluginContext* ctx,void*) noexcept {
    if(!g_widgets||!g_widgets->dispatchUiAction)return;
    // UISwitcher owns the selected page, ControllerOverlay, tabs, focus and
    // close lifecycle. Opening a bare inventory page bypasses that setup and
    // leaves detached pages/overlays after Back or tab changes.
    if(g_menuOpenUntil){g_menuOpenUntil=0;return;} // a second press cancels a pending open
    if(!g_panels.count(framevr::panels::TabbedMenu)){
        g_menuOpenUntil=GetTickCount64()+2000;FinishMenuOpen(ctx,nullptr);return;
    }
    Widgets::UiAction action{};action.structSize=sizeof action;
    action.target="PanelManager";action.text=framevr::panels::TabbedMenu;action.command="ClosePanel";
    const auto result=g_widgets->dispatchUiAction(ctx,&action);
    char line[180];snprintf(line,sizeof line,"gamestate: left menu button %s %s (result %u)",action.command,action.text,unsigned(result));Log(line);
}
bool ToggleMenus() {
    return g_ctx&&g_threads&&g_threads->runOnUiThread&&g_widgets&&g_widgets->dispatchUiAction&&
        g_threads->runOnUiThread(g_ctx,&DispatchMenuToggle,nullptr)==Threads::Result::Success;
}
void SetViewMode(uint32_t mode) { if (g_view.exchange(mode) != mode) Publish(); }

}  // namespace gamestate
