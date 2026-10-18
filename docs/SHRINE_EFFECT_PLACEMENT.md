# Shrine effect placement investigation

Status: diagnostic candidate; no shrine placement correction yet.

The owner reports floating mana/buff symbols beside shrines in native first
person VR, including an Act I rock-spire shrine. The symbols stay to the same
side while the owner turns and walks around them. The first developer capture
on 2026-10-17 did not include the floating-symbol asset family. Do not treat
that capture as proof of the symbols' coordinates or of their cause.

## Evidence from the first capture

The owner tested developer integration 4d8bb59 on Steam game 3.3.93787 and
reported the same offset. Headset/runtime/class were not specified. The log
contains 165 samples: 115 for `shrines_other/deer_shrine` and 50 for
`env_stone/forest_altar`. There are no health/mana-directory records, and no
floating-symbol records because the first filter excluded all shrine overlays.

The forest altar repeatedly transforms local `(0,3,2.75)` and `(0,0.35,0)`
through an identity basis plus its fixed world origin. Its asset contains flame,
smoke and blood-drip emitters. The deer shrine also has built-in environmental
effects; its six recorded view-position queries match the VR camera. Neither
observation establishes the floating symbol's anchor or correct placement.
Moving these environmental offsets would risk moving unrelated shrine effects.

Read-only inspection of local assets found a separate family at
`data/hd/vfx/particles/overlays/common/shrine_*`. The mana-regeneration asset
uses sphere gradients; the armor, combat, experience, resistance, skill and
stamina assets reference their corresponding symbol meshes. All ten reference
`view.direction`. These are relevant to the floating-symbol investigation;
being under `overlays` is not evidence that an instance belongs to the player.
The revised capture includes these ten exact asset paths and labels them
`symbol`. It does not assume whether an instance is attached to a shrine or
character; that must be distinguished by its origin and movement in the run.

Model silhouettes identified the rock-spire model as `forest_altar`, but are
not native evaluated transforms. Its packed vertices need the mesh's
`VertexScale`, skeleton/animation and root transform before comparing with game
coordinates. No decoded geometry, assets, screenshots or logs belong in this PR.

The portal's independently measured `(0,4,0)` anchor and radius-three offset
shell are not evidence for shrine coordinates. Portal correction gates remain
unchanged, including exclusion of every shrine symbol.

## Developer capture

Build with `steamvr/build_windows.ps1 -Developer -Test`. Only developer native
OpenXR builds compile tracing callbacks. Existing signature, first-person,
world/menu, fresh shared-head, instance-vtable and bounded-path guards apply.

The trace observes direction, position and mask-19 local-to-world helpers.
Its asset filter includes the exact forest-altar path, diagnostic shrine
object directories and the ten exact symbol paths. Unknown overlay variants,
unrelated player buffs and portals are excluded. This is a read-only diagnostic
filter, not a correction allowlist. Regular builds do not trace shrines.

Search the active developer game's `d2rloader/logs/d2r-vr-frame.log` for:

- `Frame shrine trace`: sample, operation, `layer=symbol` or `environment`,
  effect/definition, input stream, lane/count/stride and exact asset path.
- `Frame shrine values`: native view/eye, shared head, camera match, local input
  and native output. `local19` has no view index (`-1`); its native-eye zero and
  `camera_match=0` do not indicate a camera failure. View queries have no local
  input. `output_ok=0` means output is unusable as measured coordinates.
- `Frame shrine transform`: effect origin and full orientation/scale basis.

Inputs and transform are copied before the native helper and output afterward.
No shrine VM input, output, transform or asset is modified. Sample IDs correlate
lines even when workers interleave them. Camera mismatches are retained.

Each callback examines at most eight lanes. A try-lock protects a 64-entry
sampling table; contention drops samples. Each effect/stream/operation/lane
is sampled at most once per 500 ms while resident. Eviction can shorten that
interval. The global budget is 1024 samples per process, with environmental
samples capped at 128 so smoke cannot exhaust the symbol capture. Symbols may
use the entire budget if no environment records are present. At most 3072 sample
lines plus status messages use the existing buffered writer. Restart the game
for a fresh budget. Missing records do not disprove a mechanism: other helper
types and creation-time execution may fall outside capture. Runtime overhead
has not been measured in a headset.

## Next offline test

The owner launches the developer build. Revisit a shrine with a floating mana
or buff symbol before activating it. Stand still and turn the head, then walk
around it. If convenient, activate it and move away to distinguish a
player-attached effect from a stationary shrine instance. Save and exit.

First verify that `layer=symbol` records exist. Compare origins, native view
results, head movement and any local inputs for the same instance. Establish
which operation causes the displacement and the correct anchor before adding a
narrowly scoped correction. Do not move the environmental flame/drip samples
merely because their local offsets are nonzero. Keep this PR draft until the
fix and owner headset verification exist.

Automated fixtures cover both diagnostic layers, exact symbol paths, unrelated
overlay exclusions, mixed instances, constant/strided streams, camera mismatch,
transform/path rejection, read-only snapshots and both capture limits. They do
not establish interception or symbol placement in a headset.
