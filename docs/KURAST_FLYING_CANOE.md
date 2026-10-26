# Kurast Docks flying canoe investigation

## Issue description

In Kurast Docks (Act III town), players in first-person VR observe a canoe floating
high in the sky (~50 meters elevation) toward the northwest, bobbing on invisible
water waves. In the original flat game with its fixed isometric camera, this object
is never visible.

## Root cause analysis

Inspection of the game's native town preset
(`data/hd/env/preset/act3/docktown/docktown3.json`) reveals:

- Entity index 4719 has ID `3418903870` and name `prefab014`.
- It instantiates prefab `data/hd/env/model/act3/docktown/act3_docktown_boats/prefab/pf_canoe01_07_bob.json`,
  which packages `canoe01.model`, associated supplies (sacks, bedroll, oar) and a
  `WaveBobSimpleComponent` that applies procedural sine-wave rocking.
- Its `TransformDefinitionComponent` is placed at:
  $$\begin{cases} X = 0.0 \\ Y = 50.0 \\ Z = 0.0 \end{cases}$$
- In contrast, all legitimate watercraft in Kurast Docks are anchored between
  $Y = -3.5$ and $Y = -0.5$.
- Position $(0.0, 50.0, 0.0)$ represents a misplaced level-designer test asset left
  at the coordinate origin with an elevation of 50 meters.
- In original flat gameplay, the isometric camera is locked at a ~55° downward pitch
  centered on the player $(X \approx 200..350, Z \approx 200..350)$. The coordinate
  $(0, 50, 0)$ is completely outside the frustum.
- In VR, unrestricted 360° pitch and yaw allow the player to look directly up and
  toward the origin, exposing the bobbing canoe in the sky.

## Resolution

Rather than hooking entity instantiation in the game engine at runtime, the issue is
resolved cleanly via the established local asset override pipeline:

1. `steamvr/prepare_kurast_dock.py` extracts `docktown3.json` locally from the
   player's game archives using `CascLib`.
2. It detects and removes only the rogue entity (ID `3418903870` / canoe prefab with
   $Y \ge 25.0$), keeping all 4,988 legitimate town entities intact.
3. The corrected preset is staged in `mods/D2R-PCVR/D2R-PCVR.mpq/data/hd/env/preset/act3/docktown/docktown3.json`
   with an accompanying receipt (`pcvr-kurast-dock.json`).
4. D2RLoader automatically prioritizes files in the mod directory over base game archives.
5. `steamvr/install.py` runs this preparation step during mod installation, and
   `steamvr/prepare_kurast_dock_test.py` covers the fix with unit tests on synthetic fixtures.

