# Game-drawn dungeon ceilings

Status: candidate, not yet seen in a headset. Adapted from `natpoh/diablo2-vr-port`
at `91cb1e85466c730652eecb665f5355e92b56d1dc` (MIT; see [THIRD_PARTY.md](../THIRD_PARTY.md)).

In VR first person (F4) a dungeon's ceiling is a model the game draws itself: the
area's own rock, stone or beams, lit by the game's torches, fires and spells and
fogged by its fog. It only draws; nothing in the game's logic changes. The ReShade
vault stays in flat first person, in an area with no model of its own, and
whenever any of the checks below fails.

## What Setup makes

`steamvr/prepare_game_ceiling.py` runs with the other makers on install and on
Update / repair. From the player's own game files it remeshes
`act1_caves_rocks/rocks03_lod1.model` into a flat slab for each kind, with each
area's own tiling terrain as its maps, plus the caves' rim wall. It writes them
over developer test-model paths that nothing in play names (a collision guard
refuses any path a level preset names), with one unit JSON per kind (one root,
the model, `lightMask` 19 as the caves' own pieces, `shadowMask` 0).

It also writes `measures.txt`, each area's walls' top measured from its own
models, and the receipt `pcvr-game-ceiling.json` at the mod's root (beside the
`.mpq` folder): `{version, game_build, mpq, kinds, skipped, files}`, with `files`
relative to the `.mpq` folder. Same-build reinstalls reuse the measures; a new
build measures again (about 2.5 minutes). A file the player changed stops the
step, as for the Arcane sky. Nothing it makes is committed or shipped.

- `install.py --dev` writes into `mods/pcvr-dev`, `--remove` takes the files out,
  `--output <dir>` is a dry run.
- `prepare_dev_scenarios` copies only the `.mpq` folder, so its copy has no
  receipt; use `--dev`.

## In the game

`vr/engine_units.cpp` places the unit through the game's own definition loader
and instance placer, then lays it each frame with its top over the area's floor
and its middle on the whole tile nearest the hero. Height: `height_<biome>`, else
the measured walls' top (`height_from_walls`), else `height`.

It needs two hooks: the registry's set-mutable (`0x912C90`) and the world's
teardown (`0xD72610`), which lets our units go when a game is left. Nothing is
placed until the teardown hook is in. Each of the five new functions is checked
against its 3.3.93787 prologue before use; the in-tree snapshot matches every one.
A desktop session never hooks anything. A file missing, not on the receipt, or a
unit without exactly one root leaves that kind to the shader, and the log says why.

## Settings

`[ceiling]` in `d2r_pcvr.ini`:

- `game_ceiling=1`: the game draws the ceiling in VR first person.
- `game_ceiling_wall=0`: in caves the rim bends down to the floor (closes the void
  past its edge, but hides big objects beyond it).
- `height_from_walls=1`: an area with no `height_<biome>` sits on its walls' top.
- `game_ceiling_biomes`: the areas the game draws.

## Known gaps

- Pits and two-level caves: one slab at one height.
- The barracks' beams and the domed vaults are not ported.
- The Battle.net (CascLib) read path is untested.
- The five new functions are found by address and prologue only, with no
  search pattern; a game update that moves them leaves the shader's ceiling.

## Validation

- `steamvr/game_ceiling_test.cpp` (tables, measures, receipt, held units) and the
  Windows `-Test` build.
- `steamvr/prepare_game_ceiling_test.py`: the golden test builds the source's own
  C++ remesher (`third_party/natpoh_ceiling`, test only) and checks the Python
  writes byte-identical meshes and models; a drift test checks the kinds against
  `shared/game_ceiling.h`.
- A read-only run against Steam 3.3.94995 made all 15 kinds (110 files, 21.5 MB);
  every model re-parses and every unit is valid JSON.
- Still needed in a headset: each kind's look and height, torchlight on the rock,
  leaving a game and starting another, and the log's hook lines on 3.3.94995.
