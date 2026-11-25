# Kurast sewer entrance investigation

Discord report, tracked in [#169](https://github.com/bearstonem/d2r-pcvr/issues/169):
two entrances show missing stairwell surfaces, and entering requires crouching
and looking right. Headset/runtime and reported release version are unknown.

Read-only inspection of the local Steam 3.3.93787 archives confirms that Kurast
Bazaar (80) and Upper Kurast (81) connect to Sewers Level 1 (92) with warp 57.
All four `burbssewer0/1` and `metrosewer0/1` presets reference the same
`act3_kurast_architecture/sewer_entrance02` and
`act3_kurast_stairs/stairs_sewers01` model families. These entities do not have
the roof fade component previously overridden for Kurast buildings.

The two entrance materials (`roof_lip_corner01`, `elevation_walls_a`) and five
stairs materials (`kurast_stairs_A/B`, `elevation_walls_a`,
`pasted__sewer_column_corner`, `pasted__sewer_wall05`) all have native
`IsDoubleSided=0` in the inspected LOD0, 1, 3 and 4 files. The rendering candidate
allows only these exact model families/materials through the existing scoped
two-sided material override, including their normal LOD0-4 variants. Names are
canonicalized before selection. Unrelated stairs, foliage, water, collision
and opacity are unchanged. No assets are shipped.
This can restore culled backs; it cannot manufacture missing geometry. In the
owner's outside-entrance test, all seven LOD0 overrides were active, but sky
remained visible through the end of the stairwell. Inspection of the original
LOD0, 1 and 4 meshes found an open floor-end boundary without a closing wall.
The owner verified the revised dark backdrop covers the opening and that
pressing Right A enters the sewers. The missing prompt remained after entry
was working; matching logs show repeated Bazaar -> Sewers Level 1 travel.

Warp 57 retains its native 120-by-90 selection rectangle. The existing Interact
guard depends on the native controller target. That makes native targeting a
candidate for the standing/crouching difference, not a confirmed diagnosis.
The interaction candidate adds a fallback to the native Interact (357) target
query, shared by the prompt and action. It keeps any existing native target;
when no target exists, first-person VR in Bazaar/Upper Kurast (80/81) can select
the real nearby sewer warp (type 5, class 57) while facing the stairs. Vertical
head tilt is ignored. Native interaction reach and the collision line check
must still pass; travel, quest rules and the one-target-per-press guard stay
native. Other skills, exits, areas, menus and desktop play keep native selection.

The reader validates the live local player and a fresh location, audits native
layout signatures and bounds the neighboring-room scan. Room units use the
native `+0x160` link (audited in the room iterator); the hash-list link is
`+0x158` and must not be used here. New once-per-second diagnostics report
nearby/facing warps and native-range acceptance. The owner verified both Right A
entry and the replacement VR prompt in the headset, alongside the backdrop.

The native target-label path also uses a separate legacy screen projection
outside the existing stereo unit-anchor hook. The VR replacement shows
**ENTER SEWERS / Right A** while that actual exit is selected. It preserves the
entrance bearing, stays readable when directly on the stair trigger and uses
the existing static gameplay atlas (no per-frame text repaint or new swapchain).
Only session/area/ID/coordinates cross to the XR thread; native pointers do not.
The prompt clears on target loss, stale updates, travel, menus or tracking loss.
This presentation change does not issue actions or change the working A input.

The read-only DEV trace records native and queried target pointers, type,
class/ID, eye height above the player and view pitch. The trace itself does not
alter selection or travel. The diagnostic uses existing
**GEAR > Render debug draws** and starts Off each session.

DEV travel is grouped into Act I-II, Act III and Act IV-V. **Act III > Kurast
Sewers** goes to level 92; take a stair exit outside to inspect its counterpart.
**Upper Kurast** provides the other reported region, alongside the existing
**Kurast Bazaar** shortcut. Test standing selection/entry, both orientations,
inside/outside travel, and whether missing stairwell faces remain. Capture the
active mod's frame log after testing; hook installation alone is insufficient
to claim the materials were overridden or the interaction bug was fixed.

## Installation and local developer preparation

`steamvr/prepare_sewer_backdrop.py` adds a render-only dark end behind the
stairs in the four native entrance presets. It reuses the game's existing
`black_square01` rectangle, measures the opening from the player's own model
files, and follows each preset's stair transform. Two opposite faces cover
both viewing directions. Existing terrain, entities, physics and warp data
remain intact. Full install and Update / repair run this preparation automatically
for `D2R-PCVR`, on both Steam and Battle.net archive paths. Generated presets and
their `pcvr-sewer-backdrop.json` ownership receipt are included in the existing
installer backup transaction, so restoration and removal cover these files too.
Repeat updates replace only unchanged owned presets or identical files. Edited
or untracked overrides are retained, with a preparation warning in the setup log.
Only the preparation code is packaged; game assets remain local.
The first headset preview covered most of the opening but left a visible strip
on the right. The revised preview extends the backdrop farther toward that
stair-local side and increases the overlap on both sides. The owner verified
the revised placement in the headset.

For an already prepared isolated developer environment, the manual preview
command remains available. Close the game and its loader, then run:

```powershell
.\deps\python\python.exe steamvr/prepare_sewer_backdrop.py --game "YOUR GAME DIRECTORY"
```

It requires the existing isolated `pcvr-dev` metadata/save path and refuses
conflicting preset overrides. Generated presets stay inside
`mods/pcvr-dev/pcvr-dev.mpq`; the ownership receipt is
`pcvr-sewer-backdrop-preview.json` there. Do not commit or distribute these
generated files. After restarting **pcvr-dev**, use **DEV > Act III > Kurast
Sewers**, exit outside, inspect the opening from several angles, then try
entering while standing normally. Compare entrances in Bazaar and Upper Kurast.
The visual preview alone does not change entry behavior; the native interaction
fallback and VR prompt are separate DLL changes, also verified by the owner.

With the game closed, remove only unchanged preview files using:

```powershell
.\deps\python\python.exe steamvr/prepare_sewer_backdrop.py --game "YOUR GAME DIRECTORY" --remove
```

Synthetic checks (no game assets required):

```powershell
.\deps\python\python.exe -m unittest discover -s steamvr -p prepare_sewer_backdrop_test.py
```

Native checks include prompt expiry, area/session transitions, elevated-ground
shear, snap-turn placement, invalid frames and close-range readability. The
standalone `interaction-preview ... --sewer-prompt-check` verifies both text
lines fit and produces a preview without game data.
