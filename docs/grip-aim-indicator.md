# Grip aim indicator

VR Settings > Comfort > Moving > Grip aim indicator offers Off (default), Left,
Right, or Both. Hold the selected grip to show a small, faint violet floor ring;
release it to hide the ring. Menus, unavailable tracking and stale input suppress it.

The preview uses the same free-aim ray selection and range-limited floor calculation
as ordinary point-aimed teleport casts. It does not change casting or check terrain
collision. The native game can reject blocked destinations. Motion-gesture teleport
continues to use its separate arc preview.

Validation: standalone grip-aim-test, settings tests and production syntax checks.
Headset appearance and landing comparison still require an offline game test.
