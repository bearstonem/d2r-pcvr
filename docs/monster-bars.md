# Monster names in native VR

Open **VR Settings > Interface > Labels > Monster names**, pick one and press
**Apply**:

| Choice | Monster plates shown |
| --- | --- |
| **All** | every monster's name and health |
| **Bosses, elites** (the default) | bosses, champions, uniques and their minions only |
| **None** | no monster plates at all, bosses included |

NPC, mercenary, player, object and item labels are not monster names: they
show with every choice. The setting applies only to native stereo VR; the
desktop and flat views keep the game's own labels.

## Ini keys

`[hud] monster_names`: 0 All, 1 Bosses and elites, 2 None. New installs ship
`monster_names=1`. Without it the older Weak monster bars key (2026-10-13 to
2026-11-11) is read: `[hud] ordinary_mob_bars=1` is All, `0` or missing is
Bosses and elites. Nothing is migrated: a saved file is read as it is. VR
Settings writes both keys when the choice is applied (`ordinary_mob_bars` 1
only for All), so a build from before Monster names reads the nearest choice.

## How the game shows a monster's plate

Evidence from the decrypted 3.3.93787 code (a local snapshot; addresses are
offsets from the image base). Every UI pass, `TooltipsPanel`'s draw
(0x880578) runs the game's target routine 0xC0420 for the hovered unit
(0xF1900 of the local player, at 0x880AA5) and, in the controller layout that
VR keeps (0x77E10), for the controller's target (0x18D960, at 0x880AE3) and
second target (0x18DCA0, at 0x880B0C) when they differ. For a monster with a
health bar the routine:

1. asks 0xCE850 for the overhead anchor, which calls `UnitToScreen`
   (0x76A7D0, above the unit) and returns -1 when that says no (call at
   0xC0C72);
2. then always calls 0xCEA50 (0xC0C8D), which sends the `MonsterHealth` panel
   `SetInfo` - name, health, immunities - with that anchor;
3. draws no name label of its own for that monster: its label text stays
   empty, so the routine returns after a second anchor request.

The panel's update (0x1F84E0) moves the attached plate only to an anchor
whose x is not -1 (the call to 0x857050 is skipped), but fills in the name
and health either way. So the panel shows the last monster it was sent, where
the last found anchor put it. Mercenaries take a name label instead: 0xC0420
tests unit flag bit 9 at +0x124 (0x34FBA0, at 0xC0A4E). A monster whose
MonStats row fails a flag test there gets the name label as well; NPCs show
that way (the test's masks are data, not read here).

## What went wrong with Weak monster bars Off

The filter (364cfd9f) took away only the overhead anchor of an ordinary
monster. Step 2 still ran: the hovered or targeted ordinary monster's panel
showed its name and health, at the place an earlier boss, elite, eye pass or
frame had left the panel, or at the panel's own position when none had. In a
recording of 2026-11-06 the plate stayed at one spot of the view for seconds
while the head turned. 0ccea647 (2026-11-10, one read per record) answers as
the filter did before it; `monster-bars-test` runs both on the same fake
memory.

## The filter now

In each native stereo eye pass (`cleanroom/camera/d2rcam.cpp`):

- `UnitToScreen` above a unit: the unit is classified once per pass
  (`monsterbars::Classify`, the game asks twice in a row). A monster the
  choice hides gets no anchor, so the game draws no name for it.
- The anchors feed `monsterbars::PanelGate` in the game's order; the last
  monster decides, as it decides the panel's content. Where the panel is
  painted (the same `MonsterHealth` draw slot as the draw-order trial, after
  this eye's tooltip updates) it is held back when that monster is hidden by
  the choice, or when this pass found it no anchor - then it would stand
  where another eye or frame left it. A pass with no monster anchor leaves
  the panel to the game. So a shown monster gets one plate, at this eye's
  anchor.
- The two draw slots go in when the draw-order trial is on (the default) or
  the choice is not All at start. With `[hud] monster_draw_order=0` the panel
  is held back where the game paints it, before this eye's update.

Layout: unit type/class at `+0/+4`, monster data at `+0x10`, unit flags at
`+0x124`; the monster data's MonStats pointer at `+0` and special flags at
`+0x1a` agree with
[D2RMH's published structures](https://github.com/soarqin/D2RMH/blob/master/src/d2r/d2rdefs.h)
(layout facts only; no implementation is copied). The owner's local D2RLoader
3.3.0 compiler descriptors for MonStats identify the row ID at `+0` and flags
at `+0x3c`: enabled bit 25, killable bit 15, boss/primeevil bits 6/7,
NPC/interact/inTown bits 8/9/10. Any special-monster flag makes an elite;
boss or primeevil a boss. Failed reads and unexpected records leave the plate
to the game. No compiler cache or game table data is distributed.

## Checking it

`Frame labels: monster names <All|Bosses and elites|None>` is logged when the
choice changes. The ten-second `Frame target lock:` line, written while
monsters are near, ends with `monster names <choice>: hidden by the setting
N overhead anchors and N panel paints; N panel paints with no place in their
eye pass`.

`monster-bars-test` covers the ini mapping, each kind of unit, each choice,
the panel's verdict over a pass and the 0ccea647 rewrite; the settings tests
cover the row, its default and the ini round trip. Not yet tried in a headset:
for each choice, aim at ordinary monsters, a champion or unique pack and an
act boss, and check an NPC in town, the mercenary and item labels; switch
choices while moving between targets; with By turns and Both eyes.
