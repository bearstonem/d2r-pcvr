# Recorded sound commands in VR

Build/install normally with test-current.ps1. Do not launch a separate app.
Open **VR Settings > Gameplay > Voice** in an offline game.

1. Select the Quest / Virtual Desktop microphone and a **Sound** slot (1-6).
2. Select **Sample 1**, press **Record / replace**, make your sound, then pause.
   Wait for the saved message. Record natural repetitions into samples 2 and 3.
3. **Play sample** plays the selected recording through the Windows default audio
   output (normally Virtual Desktop/headset). Recording/playback pauses casting.
   Record / replace overwrites only the selected sample; Delete sample removes it.
4. Select a learned skill or available summon command using the **Action** arrows.
5. Select **Test**, press **Save voice changes**, and repeat your sound. The page
   reports the matched slot, rejection or ambiguity without casting anything.
6. Choose **Strict**, **Balanced** (default), or **Loose** tolerance. Save after
   changing tolerance or bindings. If Loose confuses two sounds, choose more
   distinct sounds and re-record. Test both desired sounds and unrelated speech.
7. Select **Cast**, save, close settings, and repeat the sound to cast its skill.
   Select **Off** and save to stop listening. Listening starts Off each game.

Sounds can be words, shouts or other vocal commands in any language. Nothing needs
spelling or speech-to-text recognition. All three examples of a slot are required
before it can match. You may record before assigning a skill; Test still reports
the sound, while Cast reports an unassigned slot without casting.

Matching uses normalized mel-frequency cepstral features and dynamic time warping.
This tolerates loudness and duration changes and some pitch/pronunciation variation;
it does not require an identical waveform. Tolerance sets an acceptance distance,
not a probability. A match must resemble at least two examples and beat other slots
by an ambiguity margin, even on Loose. Very similar commands may be indistinguishable.
This is a personal sound matcher, not a universal model of what a shout means.

Use short sounds around 0.3-2 seconds, with a pause afterward. Silence, extremely
quiet/clipped recordings and overlong utterances are rejected. The page shows mic
level, recording status, match feedback and errors. Record times out if no usable
sound arrives. A bad re-recording leaves the previous saved sample intact.

Recordings are local PCM WAV files under **voice-samples** beside the game plugin;
preview needs these audio files. No audio is sent online. Bindings and tolerance
are in voice-assignments.ini. The installer never includes or overwrites samples.
The recognition helper starts invisibly from the mod and exits with the game.
Recorded matching needs no Vosk model/download. The older standalone text-recognition
utility is separate and is not the in-game matching path.

Normal native skill/mana/cooldown checks apply. Casting is blocked in menus,
unfocused gameplay, occupied interactions, death and multiplayer. Requests expire
in 400 ms and a 1.5-second cooldown prevents repeated voice casts. Playback and
recording cannot trigger casts. Summon commands need a compatible army.

Validation: synthetic audio tests cover timing/gain/pitch variation, unrelated and
ambiguous sounds, silence/overlong rejection and exact WAV playback data. Native
checks cover UI hit targets, assignments and persistence; helper tests cover control,
errors, Off and shutdown without recording a real microphone. Real Quest recordings,
false-trigger rate and actual spell casting still require headset testing.
