# Teleport / Leap aiming

VR Settings > Gameplay > Combat > Skills and throws > Teleport / Leap aiming:
Off (default), Left grip, or Right grip. Hold the selected physical grip and cast
Teleport, Leap or Leap Attack toward the violet marker. Point farther down for a nearer landing.
Level/upward pointing uses maximum range. Release hides the marker.

When the marked destination is outside loaded rooms or blocked, the cast searches
back toward the player and uses the farthest available landing on that line.
The native solid-ground check covers the player footprint (size 2). Walls along
the route do not stop the search: Teleport can cross them if it can land beyond.
If no landing exists, the destination remains the player's current position.
Native terrain queries run only in the game-thread cast hooks and are signature
verified. Other native restrictions, including mana, still apply.

The marker stays one violet color at the requested aim point, without pending or
blocked color transitions. The destination is resolved fresh for each cast.
Off preserves existing behavior for all three skills and does not calculate this destination.
The selected controller pointer is used directly, without staff, arm or gaze
fallback. Other skills retain their existing behavior.

Tests cover map edges, blocked endpoints, crossing walls, no valid landing,
diagonal and reverse aiming, exact native command propagation and disabled mode.
Headset appearance and actual landing still require offline verification.

The existing `gameplay.teleport_skill_aiming` INI key is retained so saved grip
preferences carry over. Native Leap/Leap Attack range and skill restrictions still apply.
