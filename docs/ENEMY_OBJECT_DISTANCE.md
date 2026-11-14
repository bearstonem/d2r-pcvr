# Enemy and object distance

This feature extends offline enemy/object availability and ongoing delivery to
the local client. Expanding the renderer alone cannot show units that the
client has not received. It combines server-room activation, client room
subscriptions, and native ongoing unit update/removal passes over the same
retained room set.

The implementation was extracted from the owner-tested combined distance
experiment onto upstream main `9975da7` (release 0.3.33). It includes the
draw-distance binding retry fix and independent read-only runtime diagnostics.
Grass placement shaders, candidate-capacity experiments, foliage controls,
settings-menu changes and desktop transport overrides are outside this branch.

## Settings

`[render] subscription_rings` defaults to `5`; `0` restores native server/client
coverage. The exact 3.4.95032 guards and verified offline local client/server
association must pass before any expansion takes effect. Coverage initializes
automatically on the authoritative game thread when the local character is
ready, including while stationary. Changes also apply without walking.

The existing terrain/object distance controls retain their upstream meanings:

| Setting in `[render]` | Controls |
| --- | --- |
| `rings` | Renderer room coverage (1–8); existing Terrain draw distance control |
| `model_radius` | Distance at which models stay active; existing Object draw distance control |
| `subscription_rings` | Server activation and client unit delivery (0–8 rings; 0 keeps native coverage) |

The owner-tested distance configuration used `rings=8`, `model_radius=3260`,
and `subscription_rings=8`. Shipped defaults are terrain rings 4, fog distance 400 and subscription rings 5;
renderer coverage and model radius can still shorten the visible range.

The previous experiment stored the subscription key in `[foliage]`. This
standalone feature reads it only from `[render]`. Move an explicit old value
there if needed; the default is five. Subscriptions leave renderer settings
unchanged.

In **VR Settings > Graphics > Performance > Draw distance**, the **Enemy
activation distance** slider selects **Native** (0) or **1–8 rings**. Press
**Apply** to save it and update coverage while stationary, without restarting.
Reducing the value removes extra memberships before releasing retained rooms.
The default is five; terrain and scenery distances remain independent.

Changing subscriptions using an explicit profile path:

```powershell
.\steamvr\Set-Subscriptions.ps1 -Ini 'PATH\TO\d2r_pcvr.ini' -Rings 8
```

Larger coverage costs memory and simulation/rendering time, and distant enemies
can act as well as become visible. A graph walk is bounded to 512 rooms and
depth eight within the current level; native immediate neighbors across level
boundaries are preserved. Actual reachable counts depend on the level layout.

Renderer/entity pool growth remains the existing upstream feature. Increasing
the owner's pools to 256/1024 MiB did not fix missing enemies by itself; this
branch does not change those defaults.

## Native lifecycle and delivery

Native room transitions receive scoped expanded old/new neighbor lists. Each
expanded room retains room-status and level references before subscription;
both old/new sets stay alive until native removals finish. Old references are
released afterward. Stationary initialization uses native per-room membership
operations, checking membership before each operation to avoid duplicates.
Disconnect removes retained memberships before releasing references.

The native per-client dirty-unit update and removal passes borrow the same
retained coverage at their two guarded neighbor-getter call sites. This is
necessary for delayed creation, movement/animation and removal events after
the initial room snapshot. Other callers keep their native neighbor arrays;
the game's actual ROOM/DRLG arrays are never rewritten. Unit filtering,
packet generation, update cadence and deletion queues remain native.

Invalid graph or activation results roll back newly acquired references and
attempt contraction. The code does not expand remote multiplayer simulation,
periodically respawn enemies, or create substitute client models.

## Diagnostics and validation

Read the selected running profile's current shared mapping:

```powershell
python steamvr/subscription_status.py --ini 'PATH\TO\d2r_pcvr.ini'
```

The reader uses a dedicated render-distance mapping, with no game-memory reads
or INI writes. Status includes renderer binding/coverage fields and:

- `subscription_ready`, `subscription_requested_rings`, `subscription_state`;
- `subscription_held_rooms`, `subscription_client_rooms`, `subscription_depth`,
  `subscription_capped`, `subscription_transitions`;
- `subscription_update_passes`, `subscription_removal_passes` (expanded native
  passes since DLL load) and `subscription_update_rooms` (last update's count).

State 0 is native/waiting for local initialization; 1 guards/hooks pending;
2 activation/transition; 3 expanded; 4 graph/activation failure; 5 membership
mismatch; 6 stale owner; 7 unsupported build. Readiness alone does not prove
coverage, and coverage/model handles do not prove actual drawing.

On November 11, 2026, the owner confirmed that the combined build's enemy
visibility fix works in-game on offline Battle.net build 3.4.95032. Hardware,
headset/runtime details and an extended stability run were not separately
recorded. That build passed 235 native CTests. This extraction must pass its
own native suite and status-reader integration tests; the freshly extracted
DLL has not yet received a separate gameplay test.

Fixtures exercise graph bounds, retained overlap, rollback, contraction,
disconnect, scoped update/removal routing and delayed draw-distance bindings.
They do not execute the native game's activation/spawn functions. Broader
teleport, death, level-travel and long-session performance need validation.
