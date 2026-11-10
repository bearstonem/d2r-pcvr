# Body calibration and shoulder weapon swapping

Every time a character loads into the game, the headset shows a body calibration
prompt (not on an act change, and not in Seated play once a measurement is saved).
Right B closes it and keeps the measurement you already have. Stand upright, look straight ahead, and stretch
both arms out sideways at shoulder height: a T cross. Release both triggers, then hold
both for about 1.2 seconds while keeping still. Wait for **Saved**, then release
the triggers to return to gameplay. Right B skips or cancels before saving.
The game world remains live, so calibrate somewhere safe.

Open VR settings with **Left Grip + Y**, then select **Calibrate body** to repeat
the same flow. Apply any pending settings edits first, or close and reopen the
menu to discard them. Calibration requires tracked headset/controllers and a
SteamVR stage floor; the prompt explains tracking, posture or floor problems.
It will not infer floor height from a floorless LOCAL reference space.
Resting/bent arms ask for the T cross; they do not request SteamVR playspace setup.

## The T cross, with no menu

You can do the T cross at any time in the game to open the same prompt: stand in
it with nothing pressed and no potion in hand, and hold it for 2 seconds. Your hero
can hold anything. Both controllers buzz lightly
for as long as you hold the pose and the timer runs. If the buzz stops, you left
the pose and the 2 seconds start over; get back into it and the buzz returns. A
stronger buzz comes as the prompt opens. Nothing is saved by the pose:
saving still needs both triggers held, and Right B cancels. Once open, the prompt
stays until Right B or a save, even if a monster walks up.

The buzz follows **VR Settings > Controls > Vibration > Overall vibration** and **Loot,
potions and menus**. With either at 0 there is no buzz, and the prompt opening is
the only cue. Turn the gesture off with **VR Settings > Comfort > Body > T cross to calibrate**,
its own row under Height and arm span (`[interactions] t_cross_calibrate`, On by default).

It starts on the prompt's own posture check, then allows more while you hold it:
16 cm between the hands' heights instead of 9, for example, and more head tilt.
The buzz starts 0.2 seconds into the pose, so an arm passing through it never
buzzes. A hand or the head moving faster than 1 m/s ends the hold, and starting
needs under 0.5 m/s, each measured over a tenth of a second, so swings do not start it.

It waits, with no buzz and no timer, while any of these is true:

- **Seated play** is on (VR Settings > Comfort > Body): calibrating asks you to stand, so
  the gesture does nothing and its switch is left out of the menu.
- A menu, VR Settings, or a hand wheel is open, or the prompt itself.
- A trigger is past 30%, a grip past 55%, or a button is down, or a stick is pushed.
- A hand holds a bottle, draws a bow, holds a trap to throw, draws a glyph or
  is casting a skill.
- A monster in sight is within 12 cells. While you hold the pose the game's
  list of hostile monsters is read ten times a second, on the same path as
  the aimed-monster outline.
- SteamVR reports no stage floor.

In a game you joined the game's monster list cannot be read, so monsters do not
block the T cross there; the log says so once per run. Monsters past 12 cells or
behind a wall do not stop it either: the still 2 seconds with nothing pressed is
what keeps it out of a fight.

After the prompt closes, by saving or canceling, the gesture stays quiet until
your arms have been out of the pose for half a second, so holding the pose cannot
reopen it, unless a controller loses tracking for half a second. The log's
`Frame body calibration:` lines say when the T cross opened the prompt and, at most
once in 30 seconds each, why it refused and why a running hold ended.

## What it measures

The measured standing eye height sets the world's units per metre. The measured
grip-to-grip span fits each character rig's upper/lower arm proportions, keeping
hands and equipment at their authored size. Controller motion stays 1:1; saved
calibration supersedes the legacy manual reach multiplier. Arm proportions are
bounded to 0.65–1.6 of the rig's authored length. This measures controller reach,
not full head-top height or fingertip wingspan.

Measurements are saved atomically to the existing `d2r_pcvr.ini` on the settings
thread, with the same original-config backup, conflict checks and save status as
the settings menu. The record is `[calibration] version=1`, `eye_height_m` and
`arm_span_m`. Cancel leaves the previous calibration intact. A failed/conflicting
save offers another capture.

Saved calibration applies right away. The Saved state sets the eye-height reference
from where your head was at the end of the capture (the same reference **Set eye
height** moves) and straight ahead from where you face as it saves, and the update
thread re-reads the file in the same half-second tick that saves it, so the world's
scale and each character's arm fit change without a restart.

## Shoulder weapon swap

To switch between native weapon configurations I and II, reach your main
hand over **either shoulder** and press that hand's grip once. Release before
pressing again. The main hand is the right one, or the left with **VR Settings >
Controls > Hands > Left-handed** on. Other controller mappings remain as listed in VR Settings > Guide.
The gesture requests the opposite observed native set and waits for confirmation;
the grip cannot repeat while held. Reaching behind the head without pressing
grip leaves combat available, including the wind-up for a javelin throw.
Tracking loss, menu/calibration ownership, held bottles, and pending actions
prevent a new swap. Hip anchors are absent; legacy `holsters`/`hip_width` settings
do not enable them in the native transport. Belt potion grabs remain available.

Native fixtures cover pose/floor/tracking validation, stable sampling, persistence
acknowledgements and failure/conflict paths, recalibration, arm proportions,
both hands/shoulders, rotated/seated poses, native set confirmation, grip holds,
held-grip/context guards, and the T cross's buzz, timer, guards and rearm
(`body_calibration_gesture_test.cpp`). Headset validation is recorded in
[WINDOWS_HANDOFF.md](../WINDOWS_HANDOFF.md).
