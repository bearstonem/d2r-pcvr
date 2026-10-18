# Shrine symbol placement

Mana-regeneration and stamina shrine symbols receive a horizontal placement
correction in native first-person OpenXR builds, including regular builds.
The correction removes X/Z from these scripts' native view-direction input and
copies Y exactly. It does not normalize the result or assign a new height.
This keeps the vertical contribution while removing sideways camera offsets.

## Scope and guards

Only these two exact assets are corrected (case and slash variants accepted):

- `overlays/common/shrine_mana_regen/vfx_shrine_mana_regen.particles`
- `overlays/common/shrine_stamina/vfx_shrine_stamina_looping.particles`

The prefix is `data/hd/vfx/particles/`. Both stationary shrine instances and
character-attached instances of those assets use the correction. Attachment
cannot be inferred from the overlay directory alone. Other symbols, unrelated
buffs, environmental shrine effects and town portals retain their existing paths.

The existing signature-checked script hook runs the native callback first.
Correction requires active first-person world rendering, no game menu, a fresh
shared head pose, a matching native camera, valid stream bounds and verified
instance/definition layouts. Each output lane reads and validates its own finite
unit-length native direction before preserving its Y. Effect origins, authored
transforms, assets and the normal quad-facing path are not modified. The one-time
`VR shrines` log confirms an actual correction write.

## Validation and remaining coverage

The owner requested merging after running developer integration `0324f39`
(candidate DLL `7da60006...`) on 2026-10-17. The latest Steam 3.3.93787 log uses
SteamVR/OpenXR and confirms the correction ran at 22:38:05. It contains stamina
and skill-symbol traces; skill symbols are outside the correction scope. A
headset model, character class and detailed per-symbol visual results were not
supplied. This merge authorization is not evidence of testing every shrine or
headset. The promotion to regular builds retains the same two-asset algorithm.

Earlier capture identified stationary mana symbols and a separate moving mana
instance after activation, plus stamina. All 70 captured symbol camera queries
matched the shared head. It did not capture the scripts' final rendered
positions. The owner clarified that the existing height was already right.
Automated fixtures replay those native direction values and check exact Y
preservation, removal of the horizontal additive contribution, invalid-input
rejection and the two-asset scope. Do not generalize the correction to other
symbol scripts without checking their consumers and visual outcome.

## Useful investigation details

The forest altar and deer shrine have separate environmental particle assets.
Their flame/smoke/drip inputs contain legitimate local offsets: forest altar
repeatedly uses `(0,3,2.75)` and `(0,0.35,0)`. These are not the floating symbol's
coordinates and must not be zeroed to fix its placement. Model silhouettes also
omit native skeleton/root transforms unless evaluated explicitly; packed Granny
vertices need the mesh's `VertexScale` before comparison with world positions.

The portal's independently measured `(0,4,0)` anchor and radius-three shell are
not shrine coordinates. Portal correction allowlists remain unchanged.

## Developer capture

Only developer native OpenXR builds compile the detailed tracing callbacks;
regular builds contain the correction without these traces. Build with
`steamvr/build_windows.ps1 -Developer -Test` for a capture.

Tracing observes direction, position and mask-19 local-to-world helpers. The
filter includes the exact forest-altar asset, shrine object directories and ten
exact symbol assets: mana regeneration, stamina, armor, combat, experience,
four resistances and skill. Unknown overlay variants and portals are excluded.
The diagnostic filter is broader than the two-asset correction allowlist.

Search the developer game's `d2rloader/logs/d2r-vr-frame.log` for:

- `Frame shrine trace`: sample, operation, symbol/environment layer, instance,
  definition, input stream, lane/count/stride and asset path.
- `Frame shrine values`: native view/eye, shared head, camera match, local input
  and native output. `local19` has no view index (`-1`); its zero native-eye and
  `camera_match=0` do not indicate a camera failure. View queries have no local
  input. `output_ok=0` marks unusable output.
- `Frame shrine transform`: effect origin and orientation/scale basis.

Inputs and transform are copied before the native helper; output is read after
it and before the direction correction. Sample IDs correlate interleaved worker
lines. The capture is read-only and retains camera mismatches for diagnosis.

At most eight lanes are examined per callback. A try-lock protects a 64-entry
sampling table; contention drops samples. A resident instance/stream/operation/
lane is sampled at most once per 500 ms. Eviction can shorten that interval.
The process budget is 1024 samples, with environment samples capped at 128 to
reserve space for symbols. This bounds capture to 3072 sample lines plus status
messages using the existing buffered writer. Restart the game for a fresh
budget. Missing records do not disprove a mechanism: creation-time execution and
other helper types can fall outside capture. Runtime overhead is not benchmarked.

For further offline testing, compare a symbol before activation while turning
and walking around it, then activate it and move away. Check horizontal
centering, unchanged height and normal appearance in both attachment cases.
Retain captures locally; do not commit game assets, decoded geometry or logs.
