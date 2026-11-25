# Local speech recognition and skill assignments

The old trained-sound matcher and console trainer have been replaced. No recordings,
voice enrollment, preset shout slots or speaker training are required. Type phrases
in the graphical window, choose actions, and speak them.

## Start

Run `steamvr/setup-voice-recognition.ps1` to download the pinned Vosk runtime and
English model, compile the **desktop helper only**, and open its window. Or run the
already prepared `build-frame/voice-recognition/voice-recognition.exe`.

1. Select your microphone (Virtual Desktop's forwarded Quest microphone).
2. Enter an offline VR game with the voice-enabled mod DLL. Click **Refresh skills**.
3. Add rows: a phrase of your choice and the skill/army action to execute.
4. Click **Save assignments**. There are no predefined phrases; up to 32 can be added.
5. **Test recognition** shows recognized speech without casting.
6. **Start casting**, then focus D2R. **Stop** closes the microphone and disables casts.

Example assignments, optional: “battle orders” to Battle Orders; “attack” to Army:
Spearhead Assault; “fall back” to Army: Regroup. These army actions require summons;
they are not generic mercenary orders. Only learned non-item skills are currently
listed. Existing skill availability, mana and cooldown checks remain authoritative.

The game bridge is compatible with the previous voice-enabled DLL. Replacing only
the recognition helper does not require reinstalling that DLL. If voice support was
never installed, build/install the mod with the usual test-current.ps1 first.

## Recognition

Vosk recognizes speech using a pre-trained acoustic/language model. The supplied
small English model works across speakers without user enrollment, but accuracy
still depends on accent, microphone and background noise. Commands are constrained
to the phrases you entered plus an unknown-word alternative. Only complete exact
phrase matches above the confidence threshold can dispatch; partial hypotheses and
unknown speech never dispatch. A word missing from the model vocabulary is reported
before listening instead of silently dropped.

Use ordinary spoken words. Strings such as “HUUUU” and “RRRRAAAAA” are not reliably
recognizable words; entering an invented spelling does not teach a speech engine a
new sound. Choose words the selected language model knows. For another language,
download a compatible small/dynamic-grammar Vosk model and select its folder; use
phrases in that language. This is speaker-independent, not language-independent.

A model folder switch does not retrain your voice. The first model load takes a few
seconds. Start/stop and error feedback appear in the window. Confidence defaults to
75%; increase it for stricter matching. Game audio can still be mistaken for speech,
so headphones and recognition-only testing are recommended.

## Privacy and safeguards

After initial setup, speech processing is local. No speech is sent to a service,
no raw audio is saved, and no personal voice template is created. Editable phrases,
action IDs and device/model choices are saved in
`%LOCALAPPDATA%/D2R-PCVR/voice-recognition.json`. The previous template file is not read.
Live casting is never saved or enabled at startup.

The helper feeds the existing native skill/army-command bridge, not keyboard input.
It resets speech context when gameplay becomes unavailable; requests carry the
current session and expire in 400 ms. Commands have a 1.5 s cooldown. The game blocks
requests while unfocused, dead, in menus, multiplayer or occupied interactions.
The helper cannot bypass native game skill rules.

The Windows helper uses .NET Framework WinForms, native waveform microphone input,
and Vosk's C API. The runtime/model downloads are version-pinned with SHA-256 checks.
Third-party binaries and test audio stay under ignored build-frame/deps directories.

## Checks

`voice-commands-test` checks the native request gate. Compile voice_recognition.cs
as a console executable and use `--test` for phrase validation, duplicate rejection,
exact assignment and confidence gating. `--transcribe MODEL PCM_FILE phrase ...`
exercises the actual recognizer with mono 16 kHz signed little-endian PCM audio.
Real Quest microphone and in-game casting still require headset testing.

Sources: https://alphacephei.com/vosk/models and
https://github.com/alphacep/vosk-api/blob/v0.3.45/src/vosk_api.h
