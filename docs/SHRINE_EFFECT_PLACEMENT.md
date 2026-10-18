# Shrine effect placement investigation

Status: developer-only horizontal-placement experiment; headset verification pending.

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

## Second capture and placement candidate

The revised capture on 2026-10-17 contains 198 samples. It identifies 57
view-direction samples from stationary mana-regeneration symbols (two instance
lifetimes at the same origin), ten from a separate moving mana symbol after
activation, and three stamina-symbol samples. All 70 native cameras match the
shared VR head. The remaining 128 records are environmental effects. A stamina
initialization sample has a zero origin; do not use it as a placement anchor.
No symbol mask-19 local-position records were observed.

The owner clarified that the symbols are already at the correct height and only
displaced sideways. The mana shrine's origin is constant while native view
direction varies. The moving mana instance has a changing origin and orientation,
consistent with character attachment. These records establish the relevant
scripts and camera inputs; they do not capture the scripts' final rendered
positions or prove which consumer produces the offset.

The developer-only candidate tests whether the horizontal view-direction
contribution causes that displacement. For the two captured exact mana/stamina
assets, it replaces native `(x,y,z)` direction with `(0,y,0)` after the original
callback and trace. Y is copied exactly, without normalization or a new height.
For an additive camera-direction displacement this removes lateral motion while
preserving height. A headset test must verify that interpretation and check
symbol shape/orientation: other consumers of this direction could behave
differently. Do not describe this as a verified fix or promote it to regular
builds until that check is complete.

The experiment uses the existing signature-checked hook, active first-person
world/menu gate, fresh shared head, matching native camera, per-lane instance
resolution and bounded exact asset lookup. Each lane reads its own native
direction, validates it as finite and unit length, and retains its own Y.
Nothing writes effect origins or transforms. Unknown symbols (including the
other eight traced families), environment effects and portals are excluded.
Both stationary and character-attached instances of the two selected symbol
assets are affected; the code does not infer attachment from the asset path.
All new correction code is behind developer + native OpenXR build guards.

Native input traces still run before this experiment. The one-time
`Frame shrine candidate` log confirms a successful candidate write. It does not
confirm final visual position. Regular builds retain their original behavior.

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
The capture itself is read-only. The developer experiment runs afterward on
direction output for its two exact assets; origins/transforms/assets are never
modified. Sample IDs correlate lines even when workers interleave them. Camera
mismatches are retained.

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

The owner launches the developer build. Revisit the same mana-regeneration
shrine and, if convenient, a stamina shrine before activation. Turn and walk
around it: the symbol should remain centered horizontally, retain its previous
height, and retain its normal appearance. Activate it and check the effect while
moving away if practical. Save and exit. Check the candidate-active log as well
as the native-input samples. If the symbol changes height/shape or still sits to
the side, the placement hypothesis needs revisiting; do not widen the asset gate.

Automated fixtures cover both diagnostic layers, exact symbol/candidate paths,
unrelated overlay exclusions, mixed diagnostic instances, constant/strided
streams, camera mismatch, transform/path rejection, read-only snapshots and
capture budgets. Placement fixtures use the captured native direction values
and verify unchanged Y, invalid-input rejection and the additive-displacement
contract. They do not establish the script's final positions or visual outcome.
