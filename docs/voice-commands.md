# Recorded sound commands

Build/install normally with test-current.ps1. Open **VR Settings > Gameplay > Voice**.
No separate app is needed. There are six independent sound-to-skill bindings.

First setup: check the character name, choose a sound slot, **Record example**,
then **Choose skill**. Repeat the sound and watch **Last match**. Select **On**
and **SAVE CHANGES**, then close settings to cast. The bottom hint guides you
through the next step. Extra examples are optional; playback and deletion are
available only for a recorded example. **RETRY MICROPHONE** restarts listening
if it has stopped.

- **On / Off** controls gameplay casting. Press the prominent **SAVE CHANGES**
  button to apply it, along with skill bindings, microphone and sensitivity.
- Unsaved edits show an **UNSAVED CHANGES** warning and the current saved On/Off
  state. Closing settings returns you to Voice until you **Save** or **Discard**.
  Gameplay continues using the saved configuration, never an unsaved draft.
- The Voice page automatically listens and validates your draft. Its microphone
  meter and persistent Last detection show matches and rejections. No casts occur
  while settings are open. Closing the page restores saved gameplay On/Off.
- **High** sensitivity is the old Loose threshold and the new default. **Medium**
  and **Low** are stricter. Threshold values themselves have not increased.
- Choose a sound slot and record a short sample, then pause. One sample works;
  two more natural repetitions are optional. Play previews the selected sample.
  Record / replace and Delete affect only that selected recording. Recordings are
  stored immediately; these operations never change your saved On/Off setting.
- Choose skill opens a paged picker with native icons and names. Choose the skill,
  save, and repeat the sound in gameplay. Record/playback completion automatically
  returns to validation, and leaving settings restores saved gameplay mode.

Saved On/Off persists across launches; new installations default Off. Saved
bindings, microphone, sensitivity and recordings are separate for each character
under voice-profiles beside the plugin (name, class and game data bank).
New characters start Off with empty bindings. The Voice page shows the character.
Old global voice-assignments.ini and voice-samples remain untouched and are not
automatically imported. Reconfigure each character once. Unsaved drafts stay with
their character while the game is running. Audio is local PCM WAV. No audio is uploaded or
included in builds, and installs preserve the samples.

Matching uses the same normalized mel-cepstral features and dynamic time warping
in settings and gameplay. Volume/timing and some pitch variation are tolerated;
ambiguous sounds are rejected. High sensitivity is not a guarantee against false
matches. Use distinct sounds and check unrelated speech during validation.

Use sounds around 0.3-2 seconds. Quiet/clipped/overlong sounds are rejected with
feedback. Normal game mana, learned-skill and cooldown checks apply. The game
also blocks voice casts when unfocused, dead, in menus, multiplayer or occupied
interactions. Requests expire after 400 ms; voice casts have a 1.5-second cooldown.

Automated checks cover saved/draft separation, all six bindings, automatic
validation, preservation of saved On across recording/playback/settings changes,
worker recognition and command dispatch, timing/gain/pitch variation and rejection.
Real headset reliability and actual skill execution still require gameplay testing.

