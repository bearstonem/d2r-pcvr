# Teleport skill aiming

VR Settings > Gameplay > Combat > Skills and throws > Teleport skill aiming:
Off (default), Left grip, or Right grip. Hold the selected physical grip and cast
Teleport to land at the bright violet marker. Point farther down for a nearer
landing. Level/upward pointing uses maximum range. Release hides the marker.

Off preserves existing teleport behavior and does not calculate this destination.
The selected controller pointer is used directly, without staff, arm or gaze
fallback. The shared destination feeds the native cast and controller command as
an absolute point, independent of general spell-lock options. Other skills retain
their existing behavior. Menus, tracking loss and stale input disable this aim.
Blocked terrain can still cause the game to reject a landing.

The marker uses a larger, brighter sigil without a vertical pillar. Automated tests
cover selection, expiry, near/far points, command propagation and disabled/non-teleport
fallback. Headset appearance and actual landing require offline verification.

## Landing feedback

The marker and native command use the same integer destination cell. The native
room lookup and solid-ground collision check run on the game thread, independently
of the held-item and dropped-bottle samplers. Red means a solid ground obstruction
or a destination outside the currently reachable loaded rooms. Amber means the
check is pending/unavailable; violet means no solid-ground obstruction was found.
Results expire after 250 ms and match the exact cell and player session, so a
result from another target or session cannot color the current marker.

This is a terrain check, not a guarantee of teleport success. Mana, temporary unit
occupancy and other native skill restrictions remain authoritative. The change
does not force teleport through invalid terrain or redirect the cast elsewhere.
