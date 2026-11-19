#pragma once
// Quick cast: the radial. A Zelda-style
// item ring of up to 12 of the player's skills: hold the bound button, the ring opens about 1 m ahead; the held hand's
// thumbstick points like a clock hand (left = 9 o'clock) at the item whose angle is nearest; let go to cast it at your aim
// through upstream's own glyph press (the glue: vr/quick_radial.cpp); let go in the center: nothing. The ring is use-only;
// it is filled in the ring menu: a tap on Quick cast
// there (the center's third, or its General shelf entry) opens its page of 12 places (vr/ring_wheel.cpp, link::view/edits). This header is the pure engine: the layout from N, the stick's pick with its dead zone and hysteresis, the press's
// whole life, the edits, the per-character text and the atlas layout. No game calls, no allocation per frame.
//
// Clock degrees: 0 at 12 o'clock, clockwise (90 = 3 o'clock). Plane degrees: x right, y up, from the ring's center.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "interactions.h"
#include "stick_select.h"

namespace framevr::quickradial {

constexpr int MaxItems = 12;
// The bindable entry's id on a hand button (never a game skill, never written to the game's bindings: the button's game skill is
// cleared when it is bound, as for Draw glyph).
constexpr int ItemId = 9003;
// The ring menu calls it Quick cast, on its shelf and on the center's third alike.
inline constexpr const char* EntryName = "Quick cast";
inline constexpr const char* EntryHelp = "Hold a button on this to put Quick cast there. Then tap that button: your skills and items in a ring; push the stick at one and let it come back to cast it. Tap this (or the center's Quick cast): its 12 places.";
inline int SlotOfButton(float stored, int slots) noexcept { const int b = int(stored + .5f) - 1; return b >= 0 && b < slots ? b : -1; }
// The radial's own state, shared with the ring menu (vr/ring_wheel.cpp) without a settings table: whether it runs, and the hand
// button it is on. The button is stored as slot + 1 (0: none, the default: unbound until the player assigns it); the glue keeps it
// in the plugin's ini ([quick_radial] button) and reads it once at the first frame (LoadButton).
namespace state {
inline std::atomic<bool> enabled{true};
inline std::atomic<int> button{0};        // slot + 1
inline std::atomic<uint32_t> buttonSet{0};   // counts SetButton calls: the glue saves the button when it changes
}  // namespace state
inline bool Enabled() noexcept { return state::enabled.load(std::memory_order_relaxed); }   // true unless the ring says otherwise (SetEnabled)
inline void SetEnabled(bool on) noexcept { state::enabled.store(on, std::memory_order_relaxed); }
inline float ButtonValue() noexcept { return float(state::button.load(std::memory_order_relaxed)); }
inline int ButtonSlot() noexcept { return SlotOfButton(ButtonValue(), 10); }   // hand slot 0..9, -1 none
// The ring calls it when a Quick cast bind lands (slot + 1), or with 0 when another menu takes the button.
inline void SetButton(float slotPlusOne) noexcept {
    const int v = slotPlusOne < .5f ? 0 : std::clamp(int(slotPlusOne + .5f), 1, 10);
    state::button.store(v, std::memory_order_relaxed);
    state::buttonSet.fetch_add(1, std::memory_order_release);
}
// Classes the radial opens for: the seven of the base game. Any other (the Warlock, 7, or unknown) keeps upstream: the
// button stays upstream's (unknown keeps upstream, memory unknown-keeps-upstream).
constexpr bool ClassKnown(int skillClass) noexcept { return skillClass >= 0 && skillClass <= 6; }
// Its entry on the ring wheel's General shelf, read by the ring wheel from here: faint for a class that keeps upstream; slot: the
// button it is on (-1 none).
struct ShelfEntry { int id; const char* name; const char* help; bool dim; int slot; };
inline ShelfEntry ShelfEntryFor(bool haveHero, int skillClass, float storedButton, int slots) noexcept {
    return {ItemId, EntryName, EntryHelp, !haveHero || !ClassKnown(skillClass), SlotOfButton(storedButton, slots)};
}
// The bind: the ring wheel's hold to assign on the shelf entry sends upstream's BindSkill with ItemId;
// Take makes it upstream's own clear of that button's game skill (interactionstate's BindSkill, as Draw glyph's pick) and
// remembers it; the reply's Applied makes that slot the radial's button (SetButton(slot + 1), kept in the plugin's ini).
struct BindWatch {
    uint64_t seq = 0; int slot = -1;
    enum class Done { None, Applied, Refused };
    bool Take(interaction::Request& r) noexcept {
        if (r.action != interaction::Action::BindSkill || r.skill != ItemId) return false;
        seq = r.sequence; slot = r.index;
        r.skill = -1; r.skillOwner = -1;
        return true;
    }
    Done Reply(const interaction::Reply& reply, int& slotOut) noexcept {
        if (!seq || reply.sequence != seq || reply.result == interaction::Result::Pending || reply.result == interaction::Result::None) return Done::None;
        slotOut = slot;
        const Done d = reply.result == interaction::Result::Applied ? Done::Applied : Done::Refused;
        Forget();
        return d;
    }
    void Forget() noexcept { seq = 0; slot = -1; }
};

// ---- The ring's items, edits and their text ----
struct Item {
    int skill = -1, owner = -1;   // owner: the live catalog's (an item skill's owner changes each game; saved: the id only)
    bool operator==(const Item&) const = default;
};
// Quick cast layout: a set 12 places and an item can sit out of order (at number 9 and 3, not locked in as item 1 and 2): Fixed places (the default) or Growing (the packed ring,
// as before). The ring is one list for both: items[0, n) are its places up to the last filled one; Growing keeps every one of them
// filled (no gaps, as before), Fixed may leave a place empty (skill -1) before n. Places past n are always empty.
struct Ring {
    std::array<Item, MaxItems> items{};
    int n = 0;
    bool Has(int skill) const noexcept { for (int i = 0; i < n; ++i) if (items[size_t(i)].skill == skill) return true; return false; }
    bool Filled(int place) const noexcept { return place >= 0 && place < n && items[size_t(place)].skill >= 0; }
    int Count() const noexcept { int c = 0; for (int i = 0; i < n; ++i) c += items[size_t(i)].skill >= 0; return c; }
    bool Packed() const noexcept { return Count() == n; }
    void Trim() noexcept { while (n > 0 && items[size_t(n - 1)].skill < 0) items[size_t(--n)] = {}; }
    // Growing's view of a ring Fixed left with gaps: the items in their order, no gaps (nothing else moves).
    void Pack() noexcept {
        int k = 0;
        for (int i = 0; i < n; ++i) if (items[size_t(i)].skill >= 0) items[size_t(k++)] = items[size_t(i)];
        for (int i = k; i < n; ++i) items[size_t(i)] = {};
        n = k;
    }
    // Fixed: `it` in place `place` (whatever was there goes); n grows to take it.
    void Set(int place, Item it) noexcept { items[size_t(place)] = it; if (place >= n) n = place + 1; Trim(); }
    // Fixed: `it` in the first empty place (a skill a menu moved in, a put made before the save was read); false: full or there.
    bool AddFree(Item it) noexcept {
        if (it.skill < 0 || Has(it.skill)) return false;
        for (int p = 0; p < MaxItems; ++p) if (!Filled(p)) { Set(p, it); return true; }
        return false;
    }
    bool Add(Item it) noexcept {
        if (n >= MaxItems || it.skill < 0 || Has(it.skill)) return false;
        items[size_t(n++)] = it;
        return true;
    }
    bool Remove(int i) noexcept {
        if (i < 0 || i >= n) return false;
        for (int k = i; k + 1 < n; ++k) items[size_t(k)] = items[size_t(k + 1)];
        items[size_t(--n)] = {};
        return true;
    }
    // Takes the item at `from` out and puts it back at `to` (0..n-1 of the list as it is now); from == to: nothing.
    bool Move(int from, int to) noexcept {
        if (from < 0 || from >= n || to < 0 || to >= n || from == to) return false;
        const Item it = items[size_t(from)];
        if (from < to) for (int k = from; k < to; ++k) items[size_t(k)] = items[size_t(k + 1)];
        else for (int k = from; k > to; --k) items[size_t(k)] = items[size_t(k - 1)];
        items[size_t(to)] = it;
        return true;
    }
    bool operator==(const Ring&) const = default;
};
// The ring menu's Quick cast page: `it` put in place
// `place` (0-11). Growing: a place past the last item fills the next one (the ring has no gaps); an item already on the ring trades
// places with what is there (the same item twice never). Fixed (2026-11-17): exactly that place, the others stay where they are;
// an item already on the ring moves there (what was there, if anything, takes its old place). True when the ring changed.
inline bool PutAt(Ring& r, int place, Item it, bool fixed = false) noexcept {
    if (place < 0 || place >= MaxItems || it.skill < 0) return false;
    int at = -1;
    if (fixed) {
        for (int i = 0; i < r.n; ++i) if (r.items[size_t(i)].skill == it.skill) at = i;
        if (at == place) { const bool same = r.items[size_t(at)] == it; r.items[size_t(at)] = it; return !same; }
        const Item there = r.Filled(place) ? r.items[size_t(place)] : Item{};
        if (at >= 0) r.items[size_t(at)] = there;
        r.Set(place, it);
        return true;
    }
    if (!r.Packed()) r.Pack();   // a ring Fixed left with gaps: Growing edits it packed, as the page shows it
    for (int i = 0; i < r.n; ++i) if (r.items[size_t(i)].skill == it.skill) at = i;
    if (at >= 0) {
        if (place >= r.n) return r.Move(at, r.n - 1);
        if (place == at) { const bool same = r.items[size_t(at)] == it; r.items[size_t(at)] = it; return !same; }
        std::swap(r.items[size_t(at)], r.items[size_t(place)]);
        r.items[size_t(place)] = it;
        return true;
    }
    if (place < r.n) { r.items[size_t(place)] = it; return true; }
    return r.Add(it);
}
// Clear on the chooser: the place's item off the ring. Growing: the ones after it move up one place; Fixed: the place is empty and
// nothing else moves. False: an empty place.
inline bool ClearAt(Ring& r, int place, bool fixed = false) noexcept {
    if (fixed) {
        if (!r.Filled(place)) return false;
        r.items[size_t(place)] = {}; r.Trim();
        return true;
    }
    if (!r.Packed()) r.Pack();
    return r.Remove(place);
}
// Move on the chooser (2026-11-17): the item in place `from` to place `to`. A filled `to`: the two swap. An empty `to`: Fixed, it
// goes exactly there and `from` is empty; Growing, it goes last (the ring has no gaps). False: `from` empty, or nothing changed.
inline bool MoveTo(Ring& r, int from, int to, bool fixed = false) noexcept {
    if (to < 0 || to >= MaxItems || from == to) return false;
    if (!fixed && !r.Packed()) r.Pack();
    if (!r.Filled(from)) return false;
    if (r.Filled(to)) { std::swap(r.items[size_t(from)], r.items[size_t(to)]); return true; }
    if (!fixed) return r.Move(from, r.n - 1);
    const Item it = r.items[size_t(from)];
    r.items[size_t(from)] = {};
    r.Set(to, it);
    return true;
}
// A real place as Growing's packed page counts it (the filled places before it), and back: the k-th filled place (-1: none).
// Growing over a ring Fixed left with gaps (review F2, 2026-11-17) edits the real places through these, so the gaps survive.
inline int PackedIndex(const Ring& r, int real) noexcept { int k = 0; for (int i = 0; i < real && i < r.n; ++i) k += r.Filled(i); return k; }
inline int RealPlace(const Ring& r, int k) noexcept { for (int i = 0; i < r.n; ++i) if (r.Filled(i) && k-- == 0) return i; return -1; }
// Fixed places are named as a clock's hours (the place at the top is 12, the one at 3 o'clock is 3); Growing's are numbered from 1
// round the ring (as before). place: 0 at the top, clockwise.
constexpr int PlaceNumber(int place, bool fixed) noexcept { return fixed ? (place == 0 ? 12 : place) : place + 1; }
// ---- Items on the ring (potions, Town Portal and Identify too, from the belt and the inventory) ----
// A ring item's id is a skill id (0-4095), an exact item (ExactBit | its code's three letters: "hp3", "rvs": that potion and
// no other), or a family (FamilyBit | Family: "Healing (best)", Town Portal, Identify: the first of its kind by Potion order).
// Families are upstream's own potion kinds (interaction::PotionType, the codes the game gives each potion) plus the two
// scroll-and-tome pairs upstream's quick use names (vr/quick_items.cpp). Rank within a family: the game's own code order
// (hp1 < hp2 < ... < hp5, rvs < rvl; the Boss's rule 2026-11-15: no restore-amount trace yet).
constexpr int ExactBit = 1 << 28, FamilyBit = 2 << 28;
enum Family : int { FamNone = 0, FamHealth = 1, FamMana = 2, FamRejuv = 3, FamAntidote = 4, FamThawing = 5, FamStamina = 6,
                    FamPortal = 16, FamIdentify = 17 };
constexpr uint32_t Code3(uint32_t code) noexcept { return code & 0xffffffu; }
constexpr bool IsSkillId(int id) noexcept { return id >= 0 && id < 4096; }
constexpr bool IsExactId(int id) noexcept { return (id >> 28) == 1; }
constexpr bool IsFamilyId(int id) noexcept { return (id >> 28) == 2; }
constexpr bool IsItemId(int id) noexcept { return IsExactId(id) || IsFamilyId(id); }
constexpr int ExactId(uint32_t code) noexcept { return ExactBit | int(Code3(code)); }
constexpr int FamilyId(int family) noexcept { return FamilyBit | family; }
constexpr uint32_t CodeOfExact(int id) noexcept { return Code3(uint32_t(id)) | (uint32_t(' ') << 24); }
constexpr uint32_t C3(char a, char b, char c) noexcept { return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16; }
inline int FamilyOf(uint32_t code) noexcept {
    const uint32_t c = Code3(code);
    if (c == C3('t', 'b', 'k') || c == C3('t', 's', 'c')) return FamPortal;
    if (c == C3('i', 'b', 'k') || c == C3('i', 's', 'c')) return FamIdentify;
    switch (interaction::PotionType(c | (uint32_t(' ') << 24))) {
        case interaction::Potion::Health: return FamHealth;
        case interaction::Potion::Mana: return FamMana;
        case interaction::Potion::Rejuvenation: return FamRejuv;
        case interaction::Potion::Antidote: return FamAntidote;
        case interaction::Potion::Thawing: return FamThawing;
        case interaction::Potion::Stamina: return FamStamina;
        default: return FamNone;
    }
}
// Higher is stronger. Tomes above scrolls (a tome's charges before a single scroll).
inline int RankOf(uint32_t code) noexcept {
    const uint32_t c = Code3(code);
    const char a = char(c & 0xff), b = char((c >> 8) & 0xff), d = char((c >> 16) & 0xff);
    if ((a == 'h' || a == 'm') && b == 'p' && d >= '1' && d <= '9') return d - '0';
    if (c == C3('r', 'v', 'l')) return 2;
    if (c == C3('t', 'b', 'k') || c == C3('i', 'b', 'k')) return 2;
    return 1;
}
inline const char* FamilyName(int f) noexcept {
    switch (f) {
        case FamHealth: return "Healing"; case FamMana: return "Mana"; case FamRejuv: return "Rejuvenation";
        case FamAntidote: return "Antidote"; case FamThawing: return "Thawing"; case FamStamina: return "Stamina";
        case FamPortal: return "Town Portal"; case FamIdentify: return "Identify"; default: return "Item";
    }
}
// The English name for a code when the inventory list has none (belt items carry no name): the game's names, as upstream's
// quick use falls back to them (vr/quick_items.cpp Name).
inline std::string CodeName(uint32_t code) {
    const uint32_t c = Code3(code);
    const char a = char(c & 0xff), b = char((c >> 8) & 0xff), d = char((c >> 16) & 0xff);
    static constexpr const char* tier[5] = {"Minor ", "Light ", "", "Greater ", "Super "};
    if ((a == 'h' || a == 'm') && b == 'p' && d >= '1' && d <= '5') return std::string(tier[d - '1']) + (a == 'h' ? "Healing Potion" : "Mana Potion");
    if (c == C3('r', 'v', 's')) return "Rejuvenation Potion";
    if (c == C3('r', 'v', 'l')) return "Full Rejuvenation Potion";
    if (c == C3('y', 'p', 's')) return "Antidote Potion";
    if (c == C3('w', 'm', 's')) return "Thawing Potion";
    if (c == C3('v', 'p', 's')) return "Stamina Potion";
    if (c == C3('t', 'b', 'k')) return "Tome of Town Portal";
    if (c == C3('t', 's', 'c')) return "Scroll of Town Portal";
    if (c == C3('i', 'b', 'k')) return "Tome of Identify";
    if (c == C3('i', 's', 'c')) return "Scroll of Identify";
    std::string s; for (int k = 0; k < 3; ++k) { const char ch = char((c >> (8 * k)) & 0xff); if (ch > ' ') s += ch; }
    return s.empty() ? std::string("Item") : s;
}
// Review N5 (2026-11-16): a family entry with nothing carried still shows its kind: the weakest of its potions, the scroll.
inline uint32_t FamilyPictureCode(int family) noexcept {
    uint32_t c = 0;
    switch (family) {
        case FamHealth: c = C3('h', 'p', '1'); break; case FamMana: c = C3('m', 'p', '1'); break; case FamRejuv: c = C3('r', 'v', 's'); break;
        case FamAntidote: c = C3('y', 'p', 's'); break; case FamThawing: c = C3('w', 'm', 's'); break; case FamStamina: c = C3('v', 'p', 's'); break;
        case FamPortal: c = C3('t', 's', 'c'); break; case FamIdentify: c = C3('i', 's', 'c'); break; default: return 0;
    }
    return c | (uint32_t(' ') << 24);
}
// The ring menu's letters for a family with no picture (2026-11-16): two, as a skill's initials.
inline const char* FamilyInitials(int family) noexcept {
    switch (family) {
        case FamHealth: return "HP"; case FamMana: return "MP"; case FamRejuv: return "RJ"; case FamAntidote: return "AN";
        case FamThawing: return "TH"; case FamStamina: return "ST"; case FamPortal: return "TP"; case FamIdentify: return "ID"; default: return "IT";
    }
}
// What an item entry finds: the belt (each column bottom first; the belt key uses only the bottom one) and the carried
// inventory items the game calls usable (upstream's quick-use list, by code, a stack's or a tome's count).
struct BeltCol { std::array<uint32_t, 4> codes{}; int n = 0; };
struct InvItem { uint32_t code = 0; int qty = 0; };
struct ItemPick {
    bool ok = false;
    uint32_t code = 0;      // the item it uses next (its icon); 0: none
    int column = -1;        // the belt column whose key it presses; -1 with ok: the inventory
    int count = 0;          // all it could use: belt and inventory
    const char* why = "";   // not ok: why (shown dim)
};
inline bool Matches(int id, uint32_t code) noexcept {
    if (!code) return false;
    if (IsExactId(id)) return Code3(code) == Code3(uint32_t(id));
    if (IsFamilyId(id)) return FamilyOf(code) != FamNone && FamilyOf(code) == (id & 0xffff);
    return false;
}
// Belt first when it has the chosen one (no inventory to open), then the inventory. An exact entry uses exactly its code; a
// family entry the strongest (or the weakest: Potion order) of what it can reach. Town Portal and Identify take the belt's
// scroll first, then the inventory's tome before a scroll.
inline ItemPick PickFor(int id, const std::array<BeltCol, 4>& belt, const InvItem* inv, int invN, bool strongest) noexcept {
    ItemPick p;
    for (const auto& b : belt) for (int k = 0; k < b.n && k < 4; ++k) p.count += Matches(id, b.codes[size_t(k)]);
    for (int i = 0; i < invN; ++i) if (Matches(id, inv[i].code)) p.count += std::max(0, inv[i].qty);
    const bool scrolls = IsFamilyId(id) && ((id & 0xffff) == FamPortal || (id & 0xffff) == FamIdentify);
    int bestRank = 0; uint32_t best = 0;
    const auto better = [&](uint32_t code) {
        const int r = RankOf(code);
        return !best || (strongest ? r > bestRank : r < bestRank);
    };
    for (const auto& b : belt) if (b.n > 0 && Matches(id, b.codes[0]) && better(b.codes[0])) { best = b.codes[0]; bestRank = RankOf(best); }
    if (!(scrolls && best))
        for (int i = 0; i < invN; ++i) if (inv[i].qty > 0 && Matches(id, inv[i].code) && (scrolls ? (!best || RankOf(inv[i].code) > bestRank) : better(inv[i].code))) { best = inv[i].code; bestRank = RankOf(best); }
    if (!best) {
        p.why = p.count > 0 ? "only higher up a belt column" : "none left";
        // the icon still shows what it would use: the first matching anywhere
        for (const auto& b : belt) for (int k = 0; k < b.n && k < 4 && !p.code; ++k) if (Matches(id, b.codes[size_t(k)])) p.code = b.codes[size_t(k)];
        if (!p.code && IsExactId(id)) p.code = CodeOfExact(id);
        if (!p.code && IsFamilyId(id)) p.code = FamilyPictureCode(id & 0xffff);   // review N5: its kind's picture, dim
        return p;
    }
    p.ok = true; p.code = best;
    for (int c = 0; c < 4; ++c) if (belt[size_t(c)].n > 0 && Code3(belt[size_t(c)].codes[0]) == Code3(best)) { p.column = c; break; }
    return p;
}
// The hero's belt (each column bottom first) and carried usable items (upstream's quick-use list: a code, a stack's or a tome's
// count), as the snapshot gives them: what an item entry finds (PickFor) and what the ring menu offers exactly (CarriedOf).
inline std::array<BeltCol, 4> BeltOf(const interaction::Snapshot& s) noexcept {
    std::array<BeltCol, 4> b{};
    if (!s.beltKnown) return b;
    for (int c = 0; c < 4; ++c) {
        const auto& col = s.belt[size_t(c)];
        if (!col.known) continue;
        for (int k = 0; k < int(col.count) && k < 4; ++k) if (col.items[size_t(k)].handle) b[size_t(c)].codes[size_t(b[size_t(c)].n++)] = col.items[size_t(k)].code;
    }
    return b;
}
inline int InvOf(const interaction::Snapshot& s, InvItem* out, int cap) noexcept {
    if (!s.quick.known) return 0;
    int n = 0;
    for (unsigned i = 0; i < s.quick.count && i < s.quick.items.size() && n < cap; ++i)
        if (s.quick.items[i].item.handle) out[n++] = {s.quick.items[i].item.code, s.quick.items[i].quantity};
    return n;
}
// A plain Rejuvenation put exactly never drinks a Full one. The exact items of `family` the hero carries (belt and inventory,
// each code once), strongest first, as exact ids into out (at most cap): the ring menu's kind page (2026-11-16 review).
inline int CarriedOf(int family, const std::array<BeltCol, 4>& belt, const InvItem* inv, int invN, int* out, int cap) noexcept {
    std::array<uint32_t, 32> codes{}; int n = 0;
    const auto add = [&](uint32_t c) {
        if (FamilyOf(c) != family || family == FamNone) return;
        for (int k = 0; k < n; ++k) if (Code3(codes[size_t(k)]) == Code3(c)) return;
        if (n < int(codes.size())) codes[size_t(n++)] = c;
    };
    for (const auto& b : belt) for (int k = 0; k < b.n && k < 4; ++k) add(b.codes[size_t(k)]);
    for (int i = 0; i < invN; ++i) add(inv[i].code);
    std::sort(codes.begin(), codes.begin() + n, [](uint32_t a, uint32_t b) { return RankOf(a) != RankOf(b) ? RankOf(a) > RankOf(b) : Code3(a) < Code3(b); });
    int m = 0;
    for (int k = 0; k < n && m < cap; ++k) out[m++] = ExactId(codes[size_t(k)]);
    return m;
}
// The atlas picture of a ring item (the ring menu's pages): a family shows its kind's (FamilyPictureCode), as an exact id, so a
// family and the potion it pictures share one icon cell.
inline int PictureId(int id) noexcept { return IsFamilyId(id) ? ExactId(FamilyPictureCode(id & 0xffff)) : id; }
// Every potion and scroll an exact entry can be (the ring menu's atlas asks each picture once).
inline constexpr uint32_t ExactCodes[] = {C3('h', 'p', '1'), C3('h', 'p', '2'), C3('h', 'p', '3'), C3('h', 'p', '4'), C3('h', 'p', '5'),
                                          C3('m', 'p', '1'), C3('m', 'p', '2'), C3('m', 'p', '3'), C3('m', 'p', '4'), C3('m', 'p', '5'),
                                          C3('r', 'v', 's'), C3('r', 'v', 'l'), C3('v', 'p', 's'), C3('y', 'p', 's'), C3('w', 'm', 's'),
                                          C3('t', 's', 'c'), C3('t', 'b', 'k'), C3('i', 's', 'c'), C3('i', 'b', 'k')};
// Potion choice: only Healing, Mana and
// Rejuvenation come in tiers, so only they get a "best or exact" choice (and a kind page); Antidote, Thawing and Stamina have one
// potion each and are one plain entry. The tiers are every code in ExactCodes whose game family (FamilyOf, upstream's
// interaction::PotionType) is the kind, strongest first by RankOf: no hard-coded list per kind. A kind with 2+ tiers has tiers.
inline int TiersOf(int family, int* out, int cap) noexcept {
    std::array<uint32_t, 32> codes{}; int n = 0;
    for (const uint32_t c : ExactCodes) if (family != FamNone && FamilyOf(c) == family && n < int(codes.size())) codes[size_t(n++)] = c;
    std::sort(codes.begin(), codes.begin() + n, [](uint32_t a, uint32_t b) { return RankOf(a) != RankOf(b) ? RankOf(a) > RankOf(b) : Code3(a) < Code3(b); });
    int m = 0;
    for (int k = 0; k < n && m < cap; ++k) out[m++] = ExactId(codes[size_t(k)]);
    return m;
}
inline bool HasTiers(int family) noexcept { int ids[4]; return family != FamPortal && family != FamIdentify && TiersOf(family, ids, 4) >= 2; }
// What a family entry is called: "Healing (best)" / "(weakest)" for a kind with tiers (the Potion order), the potion's own name for
// the others ("Antidote Potion"), Town Portal and Identify as before.
inline std::string FamilyEntryName(int family, bool strongest) {
    if (HasTiers(family)) return std::string(FamilyName(family)) + (strongest ? " (best)" : " (weakest)");
    if (family == FamPortal || family == FamIdentify) return FamilyName(family);
    const uint32_t c = FamilyPictureCode(family);
    return c ? CodeName(c) : std::string(FamilyName(family));
}

// Saved per character in d2r_vr_quick_radial.ini beside the plugin, [quick_radial] <Key> = the entries in ring order: skill
// ids, "i:<code>" for an exact item, "f:<family>" for a family, e.g. "54,i:rvs,f:1" (empty: an empty ring). An empty place before
// the last filled one (Fixed places, 2026-11-17) is "-", e.g. "-,-,-,54,-,-,-,-,-,f:1" (54 at 3 o'clock, Healing at 9): a ring
// with no gaps is written exactly as before, and a save from before loads into places 1..n as it was. Decode refuses anything
// else (a bad id, a duplicate, more than 12 places): nothing loaded.
//
// The save (review N1, evening review S-C): the lines go into a copy of the ini, flushed, then moved over it. The copy starts
// from the ini (Use); from nothing only when there is no ini yet (Fresh); any other failure to copy (a sharing violation while a
// sync tool or a scanner holds it) waits and tries again (Retry): a fresh copy then would drop every other character's ring.
enum class SaveStart { Use, Fresh, Retry };
constexpr SaveStart SaveStartOf(bool copied, bool noIniYet) noexcept { return copied ? SaveStart::Use : noIniYet ? SaveStart::Fresh : SaveStart::Retry; }
// A failed batch goes back in front of what was queued since (the newer text of a key is written after, so it wins).
using SaveLines = std::vector<std::pair<std::string, std::string>>;
inline void Requeue(SaveLines& queue, SaveLines failed) {
    failed.insert(failed.end(), std::make_move_iterator(queue.begin()), std::make_move_iterator(queue.end()));
    queue = std::move(failed);
}
inline std::string Encode(const Ring& r) {
    std::string t; char part[24];
    for (int i = 0; i < r.n; ++i) {
        const int id = r.items[size_t(i)].skill;
        if (id < 0) std::snprintf(part, sizeof part, "%s-", i ? "," : "");   // an empty place (Fixed)
        else if (IsExactId(id)) {
            char code[4] = {}; for (int k = 0; k < 3; ++k) { const char ch = char((id >> (8 * k)) & 0xff); code[k] = ch > ' ' ? ch : 0; }
            std::snprintf(part, sizeof part, "%si:%s", i ? "," : "", code);
        } else if (IsFamilyId(id)) std::snprintf(part, sizeof part, "%sf:%d", i ? "," : "", id & 0xffff);
        else std::snprintf(part, sizeof part, "%s%d", i ? "," : "", id);
        t += part;
    }
    return t;
}
inline bool Decode(const char* text, Ring& out) {
    Ring r;
    const char* p = text ? text : "";
    while (*p == ' ') ++p;
    while (*p) {
        int id = 0, used = 0;
        if (p[0] == '-' && (!p[1] || p[1] == ',' || p[1] == ' ')) {   // an empty place (Fixed); "-3" is no id
            if (r.n >= MaxItems) return false;
            r.items[size_t(r.n++)] = {};
            used = 1;
        } else if (p[0] == 'i' && p[1] == ':') {   // an exact item: 2 or 3 code letters or digits
            uint32_t c = 0; int k = 0;
            for (p += 2; k < 3 && ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')); ++p, ++k) c |= uint32_t(uint8_t(*p)) << (8 * k);
            if (k < 2) return false;
            for (; k < 3; ++k) c |= uint32_t(' ') << (8 * k);
            if (!r.Add({ExactId(c), -1})) return false;
            used = 0;
        } else if (p[0] == 'f' && p[1] == ':') {
            if (std::sscanf(p + 2, "%d%n", &id, &used) != 1 || used <= 0 || !(id >= FamHealth && id <= FamStamina) && id != FamPortal && id != FamIdentify) return false;
            if (!r.Add({FamilyId(id), -1})) return false;
            used += 2;
        } else {
            if (std::sscanf(p, "%d%n", &id, &used) != 1 || used <= 0 || id < 0 || id > 4095) return false;
            if (!r.Add({id, -1})) return false;   // a duplicate or a 13th
        }
        p += used;
        while (*p == ' ') ++p;
        if (*p == ',') { ++p; while (*p == ' ') ++p; if (!*p) return false; }
        else if (*p) return false;
    }
    r.Trim();   // trailing empty places say nothing
    out = r;
    return true;
}
// One ini key per character: class and save name, letters, digits, _ and - only (letters, digits, _ and - only, as the per-weapon-set skills' keys).
inline std::string Key(int heroClass, const char* name) {
    std::string k = "class" + std::to_string(heroClass) + "_";
    for (const char* c = name; c && *c; ++c)
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-') k += *c;
    return k;
}

// ---- Every number the ring's feel is made of (the defaults are the tuned values) ----
struct Tuning {
    float distM = 1.0f;          // the ring's distance from the head at open
    float downDeg = 8.f;         // its center below eye level
    float radiusDeg = 10.f;      // the items' centers from the ring's center, degrees of view
    float iconDeg = 5.f;         // the largest item (fewer items: this; more: smaller, so neighbors never touch)
    float deadZone = .35f;       // the stick's center: inside it nothing is picked (0..1 of a full push; 0: any push picks)
    float hysteresisDeg = 8.f;   // a pick holds until the stick is this far past the half-way line to a neighbor
    float centerMs = 120.f;      // back in the dead zone this long, the pick clears (a flick released with the thumb still coming back casts)
    float tickMs = 25.f, tickAmp = .3f;     // the tick on each new pick (0 ms or 0: none)
    float castMs = 70.f, castAmp = .7f;     // the firm buzz when a cast starts
    bool strongest = true;       // Potion order: a family entry ("Healing (best)") drinks its strongest first; false: its weakest
    // Open by (Toggle or Hold): true, a
    // tap opens the ring and it stays open; the stick back in the middle (after Center hold) casts the item picked then, a tap
    // closes it with nothing cast. false (Hold): open while the button is held, its release casts.
    bool toggle = true;
    bool closeAfterCast = true;  // Toggle only: the ring shuts after a cast (On); false: it stays open for more casts
    // Quick cast layout (2026-11-17): true, Fixed places: 12 clock places, each item where it was put, the empty ones skipped by the
    // stick; false, Growing: the ring is its N items split into N equal parts (as before).
    bool fixed = true;
};

// ---- Layout: N items split the circle into N equal parts, item 1 at 12 o'clock, clockwise ----
// The clock layout and the stick's pick: the shared rule (steamvr/stick_select.h).
using stickselect::Vec2f; using stickselect::StepDeg; using stickselect::ItemClock; using stickselect::AtClock;
using stickselect::ClockOf; using stickselect::ClockDiff; using stickselect::NearestItem;
inline stickselect::Feel FeelOf(const Tuning& t) noexcept { return {t.deadZone, t.hysteresisDeg, t.centerMs}; }
// The items' size for N: Tuning::iconDeg, smaller when N would make neighbors touch (86% of the chord between two centers).
inline float IconDegFor(int n, const Tuning& t) noexcept {
    if (n <= 1) return t.iconDeg;
    const float chord = 2.f * t.radiusDeg * std::sin(3.14159265f / float(n));
    return std::max(.5f, std::min(t.iconDeg, chord * .86f));
}
// The ring picture's half width in degrees: the items' outer edge plus the bronze band and a margin.
inline float PictureHalfDeg(const Tuning& t) noexcept { return t.radiusDeg + t.iconDeg * .5f + 1.6f; }
inline float HysteresisFor(int n, const Tuning& t) noexcept { return stickselect::HysteresisFor(n, FeelOf(t)); }
// The radial's pick: the shared Selector with its Tuning's feel.
struct Selector : stickselect::Selector {
    bool Step(float x, float y, int n, double nowMs, const Tuning& t) noexcept { return stickselect::Selector::Step(x, y, n, nowMs, FeelOf(t)); }
    bool StepFilled(float x, float y, int n, unsigned filled, double nowMs, const Tuning& t) noexcept { return stickselect::Selector::StepFilled(x, y, n, filled, nowMs, FeelOf(t)); }
};

// ---- What the ring shows: its items ----
// The radial only casts; its 12 places are filled in the ring menu (vr/ring_wheel.cpp, Quick cast's page).
enum class Mode { Closed, Cast };
enum class Kind : uint8_t { Skill };
struct Entry { Kind kind = Kind::Skill; int index = -1; bool operator==(const Entry&) const = default; };   // index: the ring's place (-1: an empty place, Fixed)
constexpr int MaxEntries = MaxItems;
struct Entries {
    std::array<Entry, MaxEntries> e{};
    int n = 0;
    unsigned filled = 0;   // bit k: entry k holds an item (Growing: all n)
    int count = 0;         // how many do
    bool fixed = false;    // Fixed places: 12 entries, the empty ones never picked
    void Push(Kind k, int index = -1) noexcept {
        if (n >= MaxEntries) return;
        if (index >= 0) { filled |= 1u << n; ++count; }
        e[size_t(n++)] = {k, index};
    }
    bool Holds(int k) const noexcept { return k >= 0 && k < n && (filled >> k & 1u); }
    bool operator==(const Entries&) const = default;
};
// Growing: the ring's items, in order. Fixed: its 12 places, the empty ones too (index -1), each at its own clock place.
inline Entries EntriesFor(Mode m, const Ring& r, bool fixed = false) noexcept {
    Entries out;
    if (m != Mode::Cast) return out;
    out.fixed = fixed;
    if (fixed) for (int i = 0; i < MaxItems; ++i) out.Push(Kind::Skill, r.Filled(i) ? i : -1);
    else for (int i = 0; i < r.n; ++i) if (r.Filled(i)) out.Push(Kind::Skill, i);
    return out;
}

// ---- Placement: a body-locked plane, fixed in the room while open ----
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
struct Vec3 { float x = 0, y = 0, z = 0; };
inline Quat Mul(Quat a, Quat b) noexcept {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Vec3 Rotate(Quat q, Vec3 v) noexcept {
    const Vec3 u{q.x, q.y, q.z};
    const auto cross = [](Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    const Vec3 t = cross(u, v); const Vec3 t2{t.x * 2, t.y * 2, t.z * 2};
    const Vec3 c = cross(u, t2);
    return {v.x + q.w * t2.x + c.x, v.y + q.w * t2.y + c.y, v.z + q.w * t2.z + c.z};
}
inline Quat AxisAngle(Vec3 axis, float deg) noexcept {
    const float h = deg * 0.00872665f, s = std::sin(h);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(h)};
}
inline float HeadYawDeg(Quat head) noexcept {   // about +y; 0 = facing -z (OpenXR's forward)
    const Vec3 f = Rotate(head, {0, 0, -1});
    return std::atan2(-f.x, -f.z) * 57.29578f;
}
struct Plane {
    Vec3 center; Quat q;   // the ring's center and its plane's turn (+z toward the eyes, +y up)
    float dist = 1;        // m from the eyes, for degrees -> m
};
inline Plane Place(Vec3 eye, Quat head, const Tuning& t) noexcept {
    Plane p;
    p.q = Mul(AxisAngle({0, 1, 0}, HeadYawDeg(head)), AxisAngle({1, 0, 0}, -t.downDeg));
    p.dist = t.distM;
    const Vec3 f = Rotate(p.q, {0, 0, -t.distM});
    p.center = {eye.x + f.x, eye.y + f.y, eye.z + f.z};
    return p;
}
inline Vec3 At(const Plane& p, Vec2f deg) noexcept {
    const Vec3 o = Rotate(p.q, {p.dist * std::tan(deg.x * 0.01745329f), p.dist * std::tan(deg.y * 0.01745329f), 0});
    return {p.center.x + o.x, p.center.y + o.y, p.center.z + o.z};
}
inline float Meters(const Plane& p, float deg) noexcept { return 2 * p.dist * std::tan(deg * 0.00872665f); }

// ---- The atlas: two regions of upstream's shared dynamic image (steamvr/panel_atlas.h), painted on the radial's worker ----
// Region A: the ring picture (every item's plate and icon, the bronze band), the pick's rim, labels 0-9. Region B: labels
// 10-19. A label is a name strip shown in the ring's center: the pointed item's (a skill's name and level, an item's count), or
// a hint. Both regions are free in the shared image (the glue's static_asserts).
namespace atlas {
struct Rect { int x, y, w, h; };
constexpr int AW = 480, AH = 848, BW = 512, BH = 232, Gap = 8;
constexpr int RingPx = 448, RimPx = 112, LabelW = 232, LabelH = 40;
constexpr Rect RingRect{0, 0, RingPx, RingPx};
constexpr Rect RimRect{0, RingPx + Gap, RimPx, RimPx};
constexpr int LabelsA = 10, LabelCount = 20;
enum Region : int { A = 0, B = 1 };
struct Cell { Region region; Rect r; };
constexpr int LabelTopA = RimRect.y + RimPx + Gap;
constexpr Cell LabelCell(int i) noexcept {
    return i < LabelsA ? Cell{A, {(i % 2) * (LabelW + Gap), LabelTopA + (i / 2) * (LabelH + Gap), LabelW, LabelH}}
                       : Cell{B, {((i - LabelsA) % 2) * (LabelW + Gap), ((i - LabelsA) / 2) * (LabelH + Gap), LabelW, LabelH}};
}
// The labels: an item's name in the cell of its place (0-11), then the stick's hint and the empty ring's (the rest spare).
constexpr int LabelHint = 12, LabelEmpty = 13;
static_assert(LabelCell(LabelsA - 1).r.y + LabelH <= AH && LabelCell(1).r.x + LabelW <= AW, "region A's labels fit");
static_assert(LabelCell(LabelCount - 1).r.y + LabelH <= BH && LabelCell(LabelsA + 1).r.x + LabelW <= BW, "region B's labels fit");
static_assert(MaxItems <= LabelHint, "a label for every item before the fixed ones");
}  // namespace atlas
inline int LabelOf(const Entries& es, int e) noexcept { return e >= 0 && e < es.n ? e : -1; }

// ---- One frame's draw list: quads from the two regions, back to front ----
constexpr int MaxQuads = 3;   // the ring, the pick's rim, the center's label: three layers, all or none
struct Quad { float pose[7]; float w, h; atlas::Region region; atlas::Rect rect; };
struct DrawList {
    std::array<Quad, MaxQuads> q{}; int n = 0;
    void Add(const Plane& p, Vec2f at, float wDeg, float hDeg, atlas::Region region, atlas::Rect r) noexcept {
        if (n >= MaxQuads) return;
        const Vec3 c = At(p, at);
        q[size_t(n++)] = {{c.x, c.y, c.z, p.q.x, p.q.y, p.q.z, p.q.w}, Meters(p, wDeg), Meters(p, hDeg), region, r};
    }
};
// The label's height in degrees: 40% of the largest item, so it fits the ring's empty middle.
inline float LabelHDeg(const Tuning& t) noexcept { return std::max(.6f, t.iconDeg * .4f); }
// The ring as seen: the picture, the pick's rim over its item, and the center label (the pick's, else the hint: with no items
// LabelEmpty, where to fill it; else the stick's hint).
inline DrawList Draw(const Plane& p, Mode m, const Entries& es, int pick, const Tuning& t) noexcept {
    DrawList d;
    if (m == Mode::Closed) return d;
    const float half = PictureHalfDeg(t);
    d.Add(p, {0, 0}, half * 2, half * 2, atlas::A, atlas::RingRect);
    const float lh = LabelHDeg(t), lw = lh * float(atlas::LabelW) / float(atlas::LabelH);
    if (es.Holds(pick)) {
        const float s = IconDegFor(es.n, t) * 1.28f;
        d.Add(p, AtClock(ItemClock(pick, es.n), t.radiusDeg), s, s, atlas::A, atlas::RimRect);
        const auto c = atlas::LabelCell(LabelOf(es, pick));
        d.Add(p, {0, 0}, lw, lh, c.region, c.r);
    } else {
        const auto c = atlas::LabelCell(es.count == 0 ? atlas::LabelEmpty : atlas::LabelHint);
        d.Add(p, {0, 0}, lw, lh, c.region, c.r);
    }
    return d;
}

// ---- The press's whole life, one call a frame (vr/quick_radial.cpp is glue around it) ----
// Toggle's smallest middle (review N5, 2026-11-16): with Dead zone 0 a resting stick's drift still counts as back in the middle.
constexpr float ToggleMiddle = .05f;
struct FrameIn {
    double nowMs = 0;
    bool on = false;            // the radial runs (Enabled())
    int bound = -1;             // the slot the radial is on (-1: none)
    int held = -1, hand = 0;    // upstream's held slot this frame (handOutput.slot) and the bound slot's hand (0 right, 1 left)
    bool down = false;          // the bound button physically down (the raw trigger or face button)
    bool slotFree = false;      // the bound slot has no game skill and is no Draw glyph button: the radial owns it
    bool classOk = false;       // a class the radial knows (ClassKnown); else upstream keeps the button
    bool wheelOpen = false;     // the skill wheel or the Ring wheel is open
    bool occupied = false;      // the hand took a potion or a holster
    bool tracked = false;       // the head is tracked (the ring needs a place)
    float stick[2]{};           // the bound hand's thumbstick, -1..1 (x right, y up)
    Vec3 eye; Quat head;
};
enum class Event { None, Opened, Cast, Canceled };
enum class Why { None, Center, Off, Wheel, Taken, Occupied, Empty, Closed };   // Closed: Toggle, a tap of its button
struct FrameOut {
    bool mute = false;          // the bound hand's press does nothing else this frame (upstream's cast and attack skipped)
    bool ownStick = false;      // the bound hand's stick goes to the ring, not to the game (walk, turn), this frame
    Event event = Event::None;
    Why why = Why::None;
    int index = -1;             // Cast: the ring item to cast
    bool tick = false;          // a new pick: a light tick
    bool entries = false;       // what the ring shows changed (opened, shut): paint it again
    bool emptyBack = false;     // Toggle: the stick came back from a place with no item (an empty ring): two pulses, the ring stays open
    int hand = 0;
};
struct Controller {
    Mode mode = Mode::Closed;
    Plane plane;
    Selector sel;
    int hand = 0;
    bool downWas = false, armed = false;
    bool waitUp = false;        // closed without a release: the button must come up before it opens again
    bool stickHold = false;     // closed with the stick pushed: it stays the ring's until it comes back to the middle
    bool pressHold = false;     // Toggle: closed (a tap, a cast) with its button down: that press stays muted until the button comes up
    bool pushedEmpty = false;   // Toggle: the stick is out of the middle with no item picked (an empty ring)
    bool stickWait = false;     // Toggle (review 2026-11-16 S2): opened with the stick pushed: no pick until it is back in the middle once
    bool toggle = true;         // Open by, latched at open (review N6: switched while open, the open ring keeps its own)
    double openedMs = 0;
    Entries shown;              // the entries this frame (the glue paints them)
    bool Open() const noexcept { return mode != Mode::Closed; }
    void Close(FrameOut& out, bool down) noexcept {
        mode = Mode::Closed; sel.Reset();
        waitUp = down; stickHold = true; out.entries = true;
    }
    FrameOut Step(const FrameIn& in, Ring& ring, const Tuning& t) noexcept {
        FrameOut out; out.hand = hand;
        const bool owns = in.on && in.classOk && in.bound >= 0 && in.slotFree;
        if (mode == Mode::Closed) {
            // the closing press never reaches the game; another slot on the same button (its grip layer, review S1) is the game's
            if (pressHold) { if (!in.down) pressHold = false; else if (in.held == in.bound) out.mute = true; }
            if (stickHold && !(std::hypot(in.stick[0], in.stick[1]) > .25f)) stickHold = false;
            out.ownStick = stickHold && in.on;
            if (waitUp && !in.down) waitUp = false;
            downWas = in.down;
            if (owns && !waitUp && in.held == in.bound && in.down && !in.wheelOpen && in.tracked && !in.occupied) {
                mode = Mode::Cast; hand = out.hand = in.hand; plane = Place(in.eye, in.head, t);
                sel.Reset(); openedMs = in.nowMs; armed = false; pushedEmpty = false; toggle = t.toggle;
                stickWait = toggle && std::hypot(in.stick[0], in.stick[1]) > std::max(t.deadZone, ToggleMiddle);
                out.event = Event::Opened; out.mute = out.ownStick = true; out.entries = true;
                shown = EntriesFor(mode, ring, t.fixed);
            }
            return out;
        }
        out.mute = out.ownStick = true;
        // Toggle's open ring: only its own slot's press is muted (the hand's other buttons, its trigger and the same button's other
        // layer stay the game's, review S1), and the opening press until it comes up
        if (toggle) out.mute = in.held == in.bound || (in.down && !armed);
        if (!owns || in.wheelOpen || in.occupied) {   // switched Off, unbound, the button taken, a wheel, a potion or holster: nothing cast
            out.event = Event::Canceled;
            out.why = !in.on ? Why::Off : in.wheelOpen ? Why::Wheel : in.occupied ? Why::Occupied : Why::Taken;
            Close(out, in.down);
            downWas = in.down;
            return out;
        }
        const Entries before = shown;
        shown = EntriesFor(mode, ring, t.fixed);
        if (shown.n != before.n || shown.filled != before.filled) sel.Reset();   // Growing: filled is all n, so as before
        const int pickWas = sel.pick;
        const float r = std::hypot(in.stick[0], in.stick[1]);
        Tuning feel = t;
        if (toggle) feel.deadZone = std::max(t.deadZone, ToggleMiddle);   // review N5: a drifting stick still reaches the middle
        if (toggle && stickWait && !(r > feel.deadZone * .8f)) stickWait = false;
        // Fixed places: only a filled place is picked (the nearest by angle); Growing: the shared rule over its N items, as before
        if (!(toggle && stickWait)) out.tick = (shown.fixed ? sel.StepFilled(in.stick[0], in.stick[1], shown.n, shown.filled, in.nowMs, feel)
                                                           : sel.Step(in.stick[0], in.stick[1], shown.n, in.nowMs, feel)) && sel.pick >= 0;
        if (toggle) {   // Toggle: the stick picks round the clock; back in the middle: cast
            if (!in.down) armed = true;
            const bool tap = armed && in.down && !downWas && in.held == in.bound;   // review S1: its own slot, not the grip layer's
            downWas = in.down;
            if (tap) {   // its button again: shut, nothing cast; that press stays muted until it comes up
                out.event = Event::Canceled; out.why = Why::Closed;
                Close(out, true); pressHold = true; out.mute = true;
                stickHold = r > .25f;
                return out;
            }
            // the middle as the Selector sees it (80% of the dead zone once picked); its Center hold ran out: the pick it held is cast
            const bool middle = !(r > feel.deadZone * .8f);
            if (pickWas >= 0 && sel.pick < 0 && middle && shown.Holds(pickWas)) {
                out.event = Event::Cast; out.index = shown.e[size_t(pickWas)].index;
                // review F1: shut with its button still down: that press stays muted until it comes up (no swing after the cast)
                if (t.closeAfterCast) { Close(out, in.down); stickHold = r > .25f; pressHold = in.down; out.mute = out.mute || in.down; }
                else sel.Reset();
                pushedEmpty = false;
                return out;
            }
            // nothing to pick where the stick points (an empty ring): back in the middle it casts nothing, two pulses, still open
            if (!stickWait && sel.pick < 0 && r > feel.deadZone) pushedEmpty = true;
            else if (pushedEmpty && middle) { pushedEmpty = false; out.emptyBack = true; }
            return out;
        }
        if (!in.down) {   // Hold: the physical up edge, the release decides (the trigger is the game's)
            const int pick = sel.pick;
            if (shown.Holds(pick)) { out.event = Event::Cast; out.index = shown.e[size_t(pick)].index; }
            else { out.event = Event::Canceled; out.why = shown.count == 0 ? Why::Empty : Why::Center; }
            Close(out, false);
            stickHold = std::hypot(in.stick[0], in.stick[1]) > .25f;
        }
        downWas = in.down;
        return out;
    }
    DrawList DrawNow(const Tuning& t) const noexcept { return Draw(plane, mode, shown, sel.pick, t); }
    void Reset() noexcept { *this = {}; }
};

}  // namespace framevr::quickradial
