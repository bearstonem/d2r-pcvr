# Body calibration and shoulder weapon swapping

On the first offline character load without a saved calibration, the headset
shows a body calibration prompt. Stand upright, look straight ahead, and stretch
both arms out sideways at shoulder height. Release both triggers, then hold
both for about 1.2 seconds while keeping still. Wait for **Saved**, then release
the triggers to return to gameplay. Right B skips or cancels before saving.
The game world remains live, so calibrate somewhere safe.

Open VR settings with **Left Grip + Y**, then select **Calibrate body** to repeat
the same flow. Apply any pending settings edits first, or close and reopen the
menu to discard them. Calibration requires tracked headset/controllers and a
SteamVR stage floor; the prompt explains tracking, posture or floor problems.
It will not infer floor height from a floorless LOCAL reference space.
Resting/bent arms ask for the T-pose; they do not request SteamVR playspace setup.

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
save offers another capture. Saved calibration applies on later launches.

To switch between native weapon configurations I and II, reach the dominant
hand over **either shoulder** and press that hand's grip once. Release before
pressing again. **VR → Dominant hand** selects Right (default) or Left for this
gesture. Other controller mappings remain as listed in VR Settings > Guide.
The gesture requests the opposite observed native set and waits for confirmation;
the grip cannot repeat while held. Reaching behind the head without pressing
grip leaves combat available, including the wind-up for a javelin throw.
Tracking loss, menu/calibration ownership, held bottles, and pending actions
prevent a new swap. Hip anchors are absent; legacy `holsters`/`hip_width` settings
do not enable them in the native transport. Belt potion grabs remain available.

Native fixtures cover pose/floor/tracking validation, stable sampling, persistence
acknowledgements and failure/conflict paths, recalibration, arm proportions,
both hands/shoulders, rotated/seated poses, native set confirmation, grip holds,
and held-grip/context guards. Headset validation is recorded in
[WINDOWS_HANDOFF.md](../WINDOWS_HANDOFF.md).
