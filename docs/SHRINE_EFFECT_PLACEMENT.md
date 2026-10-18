# Shrine effect placement investigation

Status: diagnostic candidate; no shrine placement correction yet.

The owner reports a blue orb beside an Act I rock-spire shrine in native first
person VR. On 2026-10-17 they clarified that the orb stays on the same side of
the rock as they turn or move. This favors a fixed offset, but does not distinguish
authored placement from a camera-dependent value computed at effect creation.
Health/mana comparisons have not been reported.

## Asset evidence

Read-only inspection of the owner's Steam 3.3.93787 assets found a strong visual
match in `data/hd/objects/env_stone/forest_altar.json`: the model has the same two
unequal pointed rock tips. `data/hd/objects/objects.json` places `forest_altar` in
`env_stone`, outside the two shrine directories used by the initial investigation.
Its VFX component references:

`data/hd/vfx/particles/objects/env_stone/forest_altar/vfx_forest_altar.particles`

The component has no separate authored entity translation; the unit root has the
usual 45-degree Y rotation. The compiled particle asset contains
`view.position_int_SceneCtx`. This is static evidence, not proof of a live camera
offset or identification of a particular effect instance. The model comparison
uses locally decoded mesh silhouettes, not a captured native rendered transform.
No game assets, screenshots, decoded geometry or capture logs belong in the PR.

The portal investigation in `HANDOFF.md` establishes why extending its asset gate
alone is insufficient: correcting view queries did not remove the portal's
separate local center displacement. Its `(0,4,0)` anchor and radius-three shell
were verified on live NewStuff portals. Neither value is established for shrines.

## Developer capture

Build with `steamvr/build_windows.ps1 -Developer -Test`. Only developer native
OpenXR builds compile the tracing callbacks. All existing hook signatures,
first-person/world/menu gates, fresh shared-head requirement, instance vtables,
bounded path reads and portal correction gates remain in force.

The trace observes the existing direction, position and mask-19 local-to-world
helpers. It accepts the exact forest-altar particle path above and diagnostic
paths in `objects/shrines_other` and `objects/shrines_healthmana`. Player-attached
`overlays/common/shrine_*` effects and other environmental effects are excluded.
This diagnostic scope is not a placement-correction allowlist.

Search the active developer game's `d2rloader/logs/d2r-vr-frame.log` for:

- `Frame shrine trace`: sample ID, operation, effect/definition identity,
  input stream, lane/count/stride and asset path.
- `Frame shrine values`: native view index/eye, shared head position, camera
  match, local input and native output. `local19` has no view index (`-1`);
  its unused native-eye field is zero. View queries have no local input.
- `Frame shrine transform`: effect origin and full 3x3 orientation/scale basis.

Inputs and transform are copied before the native helper; output is read after
it. No shrine VM input, output, transform or asset is modified. Mismatched native
cameras are recorded with `camera_match=0` rather than silently filtered out.
Unusable output is marked `output_ok=0`; do not treat it as measured coordinates.
Sample IDs correlate the three lines even when worker threads interleave them.

Each callback examines at most eight lanes, with the existing 64-lane local /
1024-lane view stream bounds. A try-lock protects a 64-entry sampling table;
contention drops samples. Each effect/stream/operation/lane key is sampled at
most once per 500 ms while resident. Eviction can shorten that interval, but a
global 1024-sample budget bounds the entire process to 3072 sample lines plus
status messages. Restart the developer game to reset it. Logging uses the
existing buffered writer. These limits, unsupported helper types (e.g. axis
queries), and creation-time execution mean missing records do not disprove a
script mechanism. Runtime overhead has not yet been measured in a headset.

## Next offline test

Start the installed developer build only when the owner requests a launch or
launches it themselves. Find the rock-spire shrine before activating it. Stand
still and turn the head, then walk halfway around it; retain timestamps for both
phases. Compare a health/mana shrine if convenient. Save and exit normally.

Correlate the forest-altar path and stable effect identity with the screenshot
location. Compare its origin/basis, local input and transformed output across
movement. If a fixed local displacement is observed, determine the actual model
anchor in the same transform space before changing it. If a view-derived value
is responsible, verify its lifetime and camera context first. Add correction
fixtures from those measured values and restrict the eventual fix to verified
paths/operations. Keep this PR draft until that evidence and the focused fix exist.

Automated fixtures cover diagnostic classification, mixed instances, constant
and strided streams, camera mismatch, transform/path rejection, read-only
snapshots and sample bounds. They do not establish native interception, shrine
identity, correct placement or headset behavior.
