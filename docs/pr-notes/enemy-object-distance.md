## Summary

Extend offline enemy and object availability beyond the game's native nearby rooms. A new **VR Settings > Graphics > Performance > Draw distance > Enemy activation distance** slider selects **Native (0)** or **1–8 room rings**. Press **Apply** to update coverage in-game, including while standing still, without restarting.

- Base: Upstream main at `9975da7` (v0.3.33).
- Settings: `[render] subscription_rings` controls enemy/object activation and ongoing client delivery. Terrain and scenery distances remain independent.
- Swapchains: None added.
- Logging: Reports subscription readiness, coverage changes and runtime status.
- Overlap with other open PRs: No trial merges were run.
- Performance: Example measurements are included below. Larger coverage increases native simulation and memory costs.

## Why

Increasing terrain and object draw distance alone cannot reveal enemies that the local client has not received. Expanding room subscriptions makes distant enemies and objects available to the client and keeps their updates and removals flowing as play continues.

The slider lets players balance that extra coverage against performance during play.

## Implementation

The selected ring count controls server-room activation and client subscriptions around the offline character. Native unit update and removal passes use the same retained room coverage, so distant units continue to receive movement, animation and removal events after their initial delivery.

Changing the slider refreshes coverage on the authoritative game thread. Increasing it acquires the additional rooms before subscribing; decreasing it removes extra memberships before releasing retained room references. Native immediate neighbors remain available, including across level boundaries. **Native** restores the game's normal subscription coverage.

Terrain distance and Scenery distance still limit what can be rendered. Distant enemies can also act within the expanded active area.

## Technical details

The native adapter checks exact **D2R 3.4.95032** signatures and verifies the offline local client/server association before expanding coverage. Traversal stays within the current level, up to eight rings and 512 rooms. Invalid activation results roll back newly acquired references and attempt contraction.

Scoped native calls borrow the retained room set without rewriting the game's room-neighbor arrays. Native unit filtering, packet generation, update cadence and deletion queues remain in use.

The change also retries optional draw-distance bindings after the room hooks have installed and exposes read-only runtime diagnostics for renderer and subscription coverage.

## Performance example

Example FPS measurements at each enemy activation ring setting:

| Enemy activation distance | FPS |
| --- | ---: |
| 1 ring | 42 |
| 2 rings | 41 |
| 3 rings | 40 |
| 4 rings | 40 |
| 5 rings | 39 |
| 6 rings | 37 |
| 7 rings | 36 |
| 8 rings | 36 |

These are example results; performance depends on the area, room layout and system.

## Validation

- Windows Release build succeeded. The subscription slider passed all 233 native CTests, including corrected menu checks on rerun.
- Six Python integration tests passed for subscription controls and shared-memory status readback.
- The updated defaults passed the settings model, menu layout and persistence tests.
- Coverage tests exercise stationary changes through **8 → 3 → Native → 8**, every intermediate ring value, reference cleanup and repeated transitions.
- Menu previews were checked visually; fit checks passed for both hand layouts.
- `git diff --check` passed.

In-game checks:

- [x] Outdoor tested.
- [x] Indoor tested.
- [x] All distances tested.
- [x] Elevation tested.
- [x] Performance tested.

## Notes

New shipped defaults: **fog distance 400**, **terrain distance 4 rings**, and **enemy activation distance 5 rings**. The default INI, menu defaults and missing-setting fallbacks agree. Existing explicit saved preferences are preserved.

No game assets or new dependencies were added. Foliage controls and grass-placement experiments are outside this change.

Prepared with AI assistance, tested by humans.

## Contributor checklist

- [x] I read [CONTRIBUTING.md](https://github.com/bearstonem/d2r-pcvr/blob/main/CONTRIBUTING.md).
- [ ] This PR targets `main` and comes from my fork, or I am a maintainer using a repository branch.
- [x] The change is focused, with relevant documentation and regression coverage.
- [x] I accurately described validation and remaining limitations above.
- [ ] I have the right to contribute this work under the project license and preserved required credits.
- [x] I included no game assets, extracted caches, memory dumps, saves, build output or secrets.
- [x] Any game testing used offline characters only; otherwise, no game testing was performed.
