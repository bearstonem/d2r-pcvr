#pragma once
// Quick cast radial and the ring menu: the ring menu edits the radial (its Quick cast page). The hand-offs between the two are these, each read by the radial's press:
//  - moveAsk (a menu never silently drops a game skill): a menu put on a button cleared that
//    button's game skill and no free button could take it back; the ring wheel stores the skill id and the button it left
//    (MoveCode; its words in moveFrom, written before the release store), the radial adds it to this character's ring (appended
//    last, saved) and says so, or says it was taken off that button, with three short pulses on its hand  .
//    Both run on transport's Input thread; a run with the radial disabled drops both asks.
//  - view, edits (ring menu > Quick cast): the ring
//    menu's Quick cast page shows this character's ring (view: the radial publishes a copy on each change) and asks for place
//    edits (edits: put an item in a place, clear it, or move its item to another place); the radial applies them on its next press frame (editWaiting), saves
//    and publishes again. Both copies are small values under m; nothing points into the other side.
// Nothing else is shared.
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace framevr::quickradial::link {
inline std::atomic<int> moveAsk{-1};   // -1 none, else MoveCode(skill id, the hand slot it left)
inline std::atomic<int> bindApplied{-1};   // a bind through the ring menu's Quick cast entry landed on this slot (the radial stored it with SetButton); the ring reads and clears it, then lets any other menu on that button go
inline char moveFrom[48] = "";          // that button's words ("Right B"), set before moveAsk
constexpr int MoveCode(int skill, int slot) noexcept { return (skill & 0xfff) | ((slot + 1) & 0xff) << 12; }
constexpr int MoveSkill(int code) noexcept { return code & 0xfff; }
constexpr int MoveSlot(int code) noexcept { return (code >> 12) - 1; }   // -1: not known
// save: Reading: the character's save is not read yet (the page shows no places and asks nothing);
// Saved: the ring is this character's stored one (edits are saved); ThisGame: no save to tell it by (two saves of its class and
// level, or none yet): edits are kept for this game and added to the stored ring once it is read, never over it.
enum class Save : uint8_t { Reading, Saved, ThisGame };
// fixed Quick cast layout is Fixed places: ids[k] is place k's item, -1 an empty place (n: up to the last filled one);
// Growing: ids[0, n) are the packed ring's items. The page draws and names the places from it (the radial owns the layout key).
struct View { bool ready = false; Save save = Save::Reading; uint64_t session = 0; int cls = -2, n = 0; std::array<int, 12> ids{}; uint32_t rev = 0; bool fixed = false; };
// id -1: clear the place; hand: the pointing hand (a drop's pulses); from (Move): the place whose item goes to `place`
// (a filled one: the two swap), id unused.
struct Edit { uint64_t session = 0; int place = -1, id = -1, owner = -1, hand = -1, from = -1; };
constexpr int MaxEdits = 8;
inline std::mutex m;
inline View view;                                  // under m
inline std::array<Edit, MaxEdits> edits{}; inline int editN = 0;   // under m
inline std::atomic<bool> editWaiting{false};       // the radial's one-branch check
// The ring menu: queue an edit (false: MaxEdits waiting already).
inline bool Ask(const Edit& e) {
    std::lock_guard lock(m);
    if (editN >= MaxEdits) return false;
    edits[size_t(editN++)] = e;
    editWaiting.store(true, std::memory_order_release);
    return true;
}
inline View Read() { std::lock_guard lock(m); return view; }
}
