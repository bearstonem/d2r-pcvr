#pragma once
// Ring skill menu: the 7 Commander orders (upstream v0.3.35, #439: Commander of the Dead) as the ring
// shows them. THE ONE FILE for an order's picture and words: every draw site, blurb and log line reads them here, so new
// drawn formation icons are a change to IconSkill alone.
// the player's pick: upstream's text and its shared icon for now. Which orders exist, when they show and what they
// do stay upstream's (summon_commander.h, interaction::HasSummonArmy, interaction::MakeCommanderSkill): never copied here.
#include <cstring>
#include <string>
#include <string_view>
#include "skill_usage.h"
#include "summon_commander.h"

namespace framevr::ringwheel::orders {
constexpr int First = commander::SkillPhalanxDef, Last = commander::SkillFallOut, Count = Last - First + 1;
inline bool Is(int id) { return commander::IsCommanderSkill(id); }
// The id whose picture stands for the order in the ring's icon atlas. The order's own id: upstream's icon cache answers
// "s3901".."s3907" with its shared icon (interaction_icons.cpp, commander::CommanderSkillIconId).
inline int IconSkill(int id) { return id; }
// Upstream's name ("Army: Phalanx Wall"): the blurb's title and every log line.
inline const char* Label(int id) { return commander::CommanderSkillName(id); }
// The name where it stands alone on one short line (the hold's "Replaces" line is 192.8 px: "Army: Spearhead Assault" is 221).
inline const char* Short(int id) {
    const char* name = Label(id);
    return std::strncmp(name, "Army: ", 6) == 0 ? name + 6 : name;
}
// A missing icon's letters on its disc.
inline const char* Initials(int id) {
    static constexpr const char* letters[Count] = {"PW", "PR", "ME", "SA", "HG", "R", "FO"};
    return Is(id) ? letters[id - First] : "";
}
// Under the name: an order has no level and no mana (upstream's card: "Squad order", "no mana cost").
inline const char* Sub() { return "Squad order, no mana"; }
// What it does, short enough for the center: upstream's own line (skill_usage.h Line, 105 to 150 characters), cut here at
// run time and never copied, so an upstream edit reaches the ring. The design's rule (gate): up to its first
// full stop; a line that is one sentence, up to its first comma (closed with a full stop).
inline std::string HelpCut(std::string_view line) {
    const size_t stop = line.find('.');
    if (stop == std::string_view::npos) return std::string(line);
    const bool oneSentence = line.find_first_not_of(' ', stop + 1) == std::string_view::npos;
    const size_t comma = oneSentence ? line.find(',') : std::string_view::npos;
    return comma < stop ? std::string(line.substr(0, comma)) + "." : std::string(line.substr(0, stop + 1));
}
// Upstream's line for an order is the same from every slot (a test holds that): slot 0 is only a valid one to ask with.
inline std::string Help(int id) { return Is(id) ? HelpCut(skillusage::Line(id, 0)) : std::string(); }
// The row's small name over its seats on the glyph chooser (capitals: the footer's small face holds no others), and how
// far its middle sits above the seats' centers: over their hit circles (27 px), under the trees' lowest hit edge.
inline const char* RowLabel() { return "ARMY"; }
constexpr float RowLabelDy = 40;
// The pointed order's last line on the glyph chooser (the design's words): only the pointing hand's trigger
// picks (rw::OrderActOf), so the line says Trigger; the title above already reads "Choose a skill for <shape>". The longer
// "Trigger puts it on the glyph, hold assigns" left Hold Ground one line over the circle's eight.
inline const char* ChooserAction() { return "Trigger picks, hold assigns"; }
}  // namespace framevr::ringwheel::orders
