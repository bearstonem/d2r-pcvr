#pragma once
// Whirlwind on (owner, 2026-11-25: "Since we are in VR and in first person, instead of picking a spot, we should be
// able to walk with whirlwind on ... when we activate it it should be on for a while so we can like you know walk
// around with it on and then we can cast it again after that ... Maybe like, 5, 6 seconds ... But it should
// definitely be controllable by us"; then: "the steering should be controllable by us", and "based on the skill
// points you spend on the skill itself it can go up").
//
// The T (both arms out, a moment, both grips held) or a press of a hand button that holds Whirlwind turns it on for
// Seconds(level). While it is on the hero whirls on, every whirl a native Whirlwind - the game's own mana, hits,
// dual-wield strikes and repeat of a held button - and the walk stick steers: each whirl heads where the stick
// pushes, as walking goes (Walk direction and Left-handed apply, so turning with the turn stick or your body turns
// it too); with the stick at rest each whirl keeps the way the last one went (the first: where you face). It goes
// off when its time runs out, on the T again, its button pressed again, another cast or action, a menu, town, a
// mana pool below one whirl, the hero's death or leaving the game; then it can be turned on again.
//
// How (whirl_steer.h): the game makes the hero uninterruptable from a whirl's start until he reaches its spot, and
// refuses every command meanwhile (D2MOO SrvSt38: the path is fixed at the start). A whirl cannot turn, so the stick
// steers between whirls: Whirlwind's button is held for the game (its hand slot, else the right trigger's spare
// record with Whirlwind put on it, glyph_cast.h) and the game's own repeat starts the next whirl the moment one
// ends (D2R 2.4.3: "a new Whirlwind ... immediately after a Whirlwind ends"), each aimed SegmentCells ahead along
// the way of the moment (vrcam WhirlSteerAim). Nothing of the game's is written: only where its own commands go.
// Re-aiming a whirl under way would need the server's path and its collision footprint rewritten; not done.
//
// Numbers (the owner's level-90 Barbarian; logs of 2026-11-12 and 2026-11-25, positions every 250 ms):
//  - whirl speed: 13.4 cells/s with a shield, 23-25 with two weapons (Frenzy's speed, it seems: D2R's whirl goes
//    with the hero's walk speed, so speed bonuses count). The game's own figure for no bonus: walk velocity
//    6 << 8 = 1536, 25 ticks a second of 1536/4096 cells = 9.4 cells/s.
//  - mana: skills.txt Whirlwind mana 25, lvlmana 1, manashift 7: (25 + level - 1) / 2 a whirl, the level with
//    +skills - 12.5 at level 1, 22 at 20 (seen: 99 -> 77 on one whirl), 27 at 30. One whirl's mana however far it
//    goes; paid only when the whirl starts (a whirl that cannot start, into a wall, costs nothing).
//  - segments: half a second of whirling (SegmentSeconds) at the speed measured as it goes, at least 8 cells and at
//    most 12: about 0.5 s from a push of the stick to the turn. The mod's whirls were 10 cells (the T and a button,
//    before), as a desktop cursor some 10 cells away; mana a second is speed / length x a whirl's, so against
//    10-cell whirls: 1.25x up to 16 cells/s (8 cells: 13.4 turns within 0.6 s), 1.0x at 20, 0.83x from 24 (12
//    cells) - never more than a quarter above, less for fast whirlers. Shorter would turn sooner and cost more:
//    6 cells, 1.67x; half a second with no 8-cell floor, 1.5x at 13.4 cells/s.
//
// Value-only: steamvr/whirl_on_test.cpp.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace framevr::whirlon {
constexpr int Whirlwind=151;

// Its time on by the skill's level, +skills counted (the game's own level, as its mana and damage take it): 5 s at
// level 1 and a tenth of a second a level, as the game's own durations grow (Battle Orders, Shout: so much a level),
// to at most 8 s (level 31): 6.9 s with 20 points, 7.4-7.9 with the +5 to +10 of endgame gear. A level unread: 5 s.
constexpr float BaseSeconds=5.f,LevelSeconds=.1f,MaxSeconds=8.f;
inline float Seconds(int level) {
    if(level<1)return BaseSeconds;
    return std::min(MaxSeconds,BaseSeconds+LevelSeconds*float(level-1));
}
// One whirl's mana at a level (the game's sum, D2MOO SKILLMANA_Consume: (mana + (level-1) x lvlmana) << manashift,
// in 256ths, at least minmana). A level unread counts as 1.
inline float WhirlMana(int level) {
    const int l=std::clamp(level,1,1000);
    return std::max(float((25+(l-1))<<7)/256.f,1.f);
}

// A whirl's length from the speed whirls go (cells a second): SegmentSeconds of it, MinCells to MaxCells.
constexpr float SegmentSeconds=.5f,MinCells=8.f,MaxCells=12.f;
constexpr float DefaultSpeed=13.4f,MinSpeed=4.f,MaxSpeed=40.f;
inline float SegmentCells(float speed) {
    if(!std::isfinite(speed)||speed<=0)speed=DefaultSpeed;
    return std::clamp(speed*SegmentSeconds,MinCells,MaxCells);
}

// The whirls of one turn-on, as the game takes them (vrcam: a Whirlwind command its own check passed, with the hero
// where he stood): how many, the ground covered, and the speed they go, which the next whirl's length follows. The
// speed is kept from one turn-on to the next (a new hero starts at DefaultSpeed).
class Chain {
public:
    int count=0;float moved=0;float speed=DefaultSpeed;bool measured=false;
    void Begin(){count=0;moved=0;lastAt=-1;seenKnown=false;}
    void Forget(){Begin();speed=DefaultSpeed;measured=false;}
    float Cells()const{return SegmentCells(speed);}
    // The game took a whirl's command at `time` (s) with the hero at x, y (cells), the whirl `cells` long. True: a
    // new whirl. A command again while the last is under way (the game refuses it), false: one is new once the hero
    // reached the last one's spot (within ReachSlack), or twice its time went by (cut short: a wall, a monster's
    // body). Whirls that reached their spot set the speed: the cells between two starts over the time between them.
    bool Took(double time,float x,float y,float cells) {
        if(!std::isfinite(time)||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(cells)||cells<=0)return false;
        if(count>0) {
            const double dt=time-lastAt;
            const float d=std::hypot(x-lastX,y-lastY);
            if(dt<MinGap)return false;
            const bool reached=d>=lastCells-ReachSlack;
            if(!reached&&dt<2.0*lastCells/speed)return false;
            if(reached&&d<=lastCells*1.5f&&dt>.1&&dt<3) {
                const float m=d/float(dt);
                if(m>=MinSpeed&&m<=MaxSpeed){speed=measured?.5f*speed+.5f*m:m;measured=true;}
            }
        }
        ++count;lastAt=time;lastX=x;lastY=y;lastCells=cells;
        return true;
    }
    // The hero seen at x, y while it is on: the ground he covers. A step longer than JumpCells (a waypoint, another
    // area) is no whirl's.
    void Seen(float x,float y) {
        if(!std::isfinite(x)||!std::isfinite(y))return;
        if(seenKnown){const float step=std::hypot(x-seenX,y-seenY);if(step<=JumpCells)moved+=step;}
        seenX=x;seenY=y;seenKnown=true;
    }
    static constexpr double MinGap=.1;
    static constexpr float ReachSlack=1.5f,JumpCells=20.f;
private:
    double lastAt=-1;float lastX=0,lastY=0,lastCells=0;
    bool seenKnown=false;float seenX=0,seenY=0;
};

enum class By:uint8_t{None,Motion,Button};
enum class Why:uint8_t{None,Time,Motion,Button,Released,Other,Mana,Town,Menu,Died,Left,Unavailable,Record,Lost,Off};
inline const char* Name(Why w) {
    switch(w) {
    case Why::Time:return "its time ran out";
    case Why::Motion:return "the T again";
    case Why::Button:return "its button pressed again";
    case Why::Released:return "its button let go after holding it";
    case Why::Other:return "another cast or action";
    case Why::Mana:return "out of mana";
    case Why::Town:return "in town";
    case Why::Menu:return "a menu or wheel opened";
    case Why::Died:return "the hero died";
    case Why::Left:return "left the game";
    case Why::Unavailable:return "Whirlwind unavailable (its weapon gone?)";
    case Why::Record:return "the right trigger's record did not hold Whirlwind";
    case Why::Lost:return "motion combat paused (tracking or the hands taken)";
    case Why::Off:return "motion combat off";
    default:return "-";
    }
}
// How its presses reach the game (glyphcast::Dispatch): its hand slot, the right trigger's record holding it, or
// that record once Whirlwind is put on it (Park); None: no way.
enum class Press:uint8_t{None,Slot,Record,Park};

// A press of its button held this long is a hold, as the game's own: let go, it ends (the button chained whirls
// while held before; it still does). A quicker tap leaves it on for its time.
constexpr double HoldSeconds=.4;
// The right trigger's record: this long to read back Whirlwind after it is put there (glyphcast::ParkWaitSeconds),
// or to read it again once lost. The hands taken (motion combat paused) this long ends it.
constexpr double RecordWaitSeconds=.6,LostSeconds=1.0;

struct Input {
    double time=0;
    uint64_t hero=0;            // the game session, 0 none
    bool combat=false;          // motion combat on in world play
    bool live=false;            // motion input active and the skill snapshot fresh this frame (else its presses pause)
    bool motion=false;          // the T cast this frame
    int button=-1;              // the hand slot of a Whirlwind button the player holds now, -1 none
    bool other=false;           // another skill, attack or interact pressed, or another motion or glyph cast, this frame
    bool menu=false,town=false,dead=false,available=false;
    bool manaKnown=false;float mana=0;
    int level=0;                // Whirlwind's level, +skills counted; 0 unread
    bool whirling=false;        // the hero is in a whirl now (his sequence mode)
    Press press=Press::None;int slot=-1;   // where a T's presses go
    bool recordHolds=false;     // the right trigger's record reads back Whirlwind
};
struct Output {
    bool on=false;
    bool started=false;By by=By::None;
    Why stopped=Why::None;
    const char* refused=nullptr;   // a start asked for and not made, why
    bool consume=false;            // the player's Whirlwind button is this mode's this frame: not pressed for the game
    int holdSlot=-1;bool holdRecord=false;
    bool park=false;               // put Whirlwind on the right trigger's record (until it reads back)
    float seconds=0;double elapsed=0;
};

class Mode {
public:
    bool On()const{return on;}
    By Started()const{return by;}
    Press Way()const{return press;}
    // Every input frame. One start or one stop at most a frame.
    Output Step(const Input& in) {
        Output o;
        const bool down=in.button>=0,pressed=down&&!buttonWas,released=!down&&buttonWas;
        buttonWas=down;
        if(!down)consumeUntilUp=false;
        if(on) {
            o.seconds=seconds;o.elapsed=in.time-from;
            if(in.live)lostAt=-1;else if(lostAt<0)lostAt=in.time;
            Why why=Why::None;
            if(!in.combat)why=Why::Off;
            else if(in.hero!=hero)why=Why::Left;
            else if(in.dead)why=Why::Died;
            else if(in.menu)why=Why::Menu;
            else if(in.town)why=Why::Town;
            else if(lostAt>=0&&in.time-lostAt>=LostSeconds)why=Why::Lost;
            else if(in.live&&!in.available)why=Why::Unavailable;
            else if(in.motion)why=Why::Motion;
            else if(pressed){why=Why::Button;consumeUntilUp=true;}
            else if(in.other)why=Why::Other;
            else if(held&&released&&in.time-heldFrom>=HoldSeconds)why=Why::Released;
            else if(in.manaKnown&&in.mana<WhirlMana(in.level)&&!in.whirling)why=Why::Mana;
            if(released)held=false;
            if(why==Why::None&&in.time-from>=seconds&&!(held&&down))why=Why::Time;
            if(why==Why::None&&press==Press::Park) {
                if(in.recordHolds){press=Press::Record;recordLostAt=-1;}
                else if(in.time-parkAt>=RecordWaitSeconds)why=Why::Record;
                else o.park=true;
            }
            if(why==Why::None&&press==Press::Record) {
                if(in.recordHolds)recordLostAt=-1;
                else if(recordLostAt<0)recordLostAt=in.time;
                else if(in.time-recordLostAt>=RecordWaitSeconds)why=Why::Record;
            }
            if(why!=Why::None) {
                on=false;held=false;o.stopped=why;o.by=by;
                o.consume=down&&consumeUntilUp;
                return o;
            }
        } else if(in.motion||pressed) {
            const By b=pressed?By::Button:By::Motion;
            if(pressed)consumeUntilUp=true;   // the press is this mode's, made or not
            const Press way=b==By::Button?Press::Slot:in.press;
            o.refused=!in.combat||!in.live?"motion combat is paused":in.dead?"the hero is dead":in.menu?"a menu is open":
                in.town?"in town":!in.available?"Whirlwind is unavailable now":
                in.manaKnown&&in.mana<WhirlMana(in.level)?"not enough mana for a whirl":
                way==Press::None?"no button or record to press it with":nullptr;
            if(!o.refused) {
                on=true;by=b;from=in.time;seconds=Seconds(in.level);hero=in.hero;press=way;
                slot=b==By::Button?in.button:in.slot;
                held=b==By::Button;heldFrom=in.time;parkAt=in.time;recordLostAt=-1;lostAt=-1;
                o.started=true;o.by=b;o.seconds=seconds;
                if(press==Press::Park){if(in.recordHolds)press=Press::Record;else o.park=true;}
            }
        }
        o.on=on;
        o.consume=down&&(on||consumeUntilUp);
        if(on&&in.live) {
            if(press==Press::Slot)o.holdSlot=slot;
            else if(press==Press::Record&&in.recordHolds)o.holdRecord=true;
        }
        return o;
    }
    // Gone without a stop of its own (the transport reset): nothing pressed, no line.
    void Reset(){*this=Mode{};}
private:
    bool on=false,held=false,buttonWas=false,consumeUntilUp=false;
    By by=By::None;Press press=Press::None;int slot=-1;
    double from=0,heldFrom=0,parkAt=0,recordLostAt=-1,lostAt=-1;
    float seconds=0;uint64_t hero=0;
};
}
