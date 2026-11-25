# Local voice-command prototype

This is an experimental **desktop console trainer**, not an in-headset settings
page yet. It learns your sounds rather than spelling or language. The six labels
are HUUUU, HAAAAA, ATTACK, FALL BACK, RRRRAAAAA and DIE; pronounce them however you
like, consistently. Different sounds must be acoustically distinct.

## Start

1. Build/install the current game DLL with your usual test-current.ps1, then enter
   an offline VR game. The trainer alone cannot add support to an old DLL.
2. Enable microphone forwarding in Virtual Desktop and select that microphone
   from the trainer's device list. Prefer headphones to reduce game-audio pickup.
3. Run `steamvr/voice-trainer.ps1` in PowerShell. It builds **only the helper**, then
   opens its console. Alternatively run the already built
   `build-frame/voice-prototype/voice-trainer.exe`.
4. `devices` lists microphones; `mic 2` selects device 2 for this run.
5. `train 1`: say HUUUU three times with a pause of at least half a second between
   examples. Recording ends automatically after the third. Repeat `train 2`
   through `train 6`. Training a label again replaces its three samples.
6. `test` recognizes without sending actions. Speak normally and try background
   noises too. Press any key **in the trainer console** to stop. At least two
   labels must have three examples each before recognition is enabled.
7. `skills` lists your current character's learned skills and applicable army
   commands. `assign 1 149` binds HUUUU to Battle Orders, for example; `assign 1 -1`
   disables that action. `list` shows all assignments and trained sample counts.
8. `live` enables actions for this listening session. Return focus to D2R before
   shouting. The trainer shows recognition scores and game acceptance/refusal;
   accepted skill presses also give a controller pulse. Return to the console and
   press a key to stop. `quit` exits. Live mode is never saved or enabled at startup.

Default assignments: ATTACK = Army: Spearhead Assault (3904), FALL BACK = Army:
Regroup (3906). These need an available summon army; they are not generic mercenary
orders. Other sounds start unassigned. Any learned, non-item skill listed by
`skills` can be assigned to any sound. Item-charge skill owners are not supported
by this first version. Skill availability, mana and cooldowns remain native.

## Tuning

`threshold 0.20` sets the maximum matching distance; smaller is stricter, larger
accepts more variation (range .02-.5). `level 0.012` sets microphone activation RMS
(range .001-.2); lower it if quiet speech never records, raise it for noisy rooms.
Ambiguous matches are rejected even below the threshold. Similar vowel shouts
may need different consonants or a more distinct rhythm. Recognition has not yet
been validated against your voice or headset audio; the included tests use synthetic
signals and check dispatch safeguards, not real-world recognition accuracy.

## Behavior and privacy

The microphone opens only during train/test/live and closes on leaving that mode.
The helper uses Windows waveform input; no network, cloud speech engine, raw-audio
files or transcript is used. Acoustic templates and assignments are stored locally
in `%LOCALAPPDATA%/D2R-PCVR/voice-prototype-v1.bin` and persist across branches/builds.
Delete that file to reset your voice profile. Templates contain personal voice
features and should not be published.

Matching runs in the helper process. A local Windows-session shared-memory channel
carries skill IDs, timestamps, session identity and game acknowledgments. There is
no keyboard simulation. The game consumes a fresh request once and dispatches via
its existing glyph/native casting path, or its army-command API. Requests expire
in 400 ms; helper heartbeat expires in 500 ms; commands have a 1.5-second cooldown.
Menus, death, unfocused game, multiplayer, stale snapshots and occupied controls
reject requests instead of saving them for later. Normal gameplay has no microphone
work when the helper is absent.

The voice matcher uses normalized log-frequency acoustic features and dynamic time
warping over three personal templates per label. Long speech is discarded until
silence; similar competing labels must have a clear score margin. This is a small
personal-shout experiment, not general speech recognition or speaker authentication.

Validation: voice-commands-test (acoustics, ambiguity, segmentation, stale/replayed
commands, session mismatch and blocked-input discard), trainer standalone compilation
and help smoke test, and transport.cpp production-flag syntax check. Game build,
real microphone and headset testing are separate.
