# In-game speech recognition

Build and install the branch normally. Recognition is included in the build and
installer; the local test-current.ps1 updater also copies the speech payload.
Do not launch the desktop voice app.

1. Enter an offline VR game and open **VR Settings > Gameplay > Voice**.
2. Select the Quest / Virtual Desktop microphone.
3. Choose a phrase slot. Use the on-panel keyboard to enter your words.
4. Use the action arrows to select a learned skill or available army command.
5. Set Recognition to **Test** and press **Save voice changes**. Speak your phrase;
   the result appears on the page, without casting.
6. Set Recognition to **Cast**, save, and close settings. Say the phrase to cast.
   Set it to **Off** and save to close the microphone.

Six independent phrase assignments are saved in voice-assignments.ini beside
the game plugin. They survive restart; recognition itself starts Off each game.
You can assign multiple phrases to the same skill. No recordings or speaker
training are required. The skill list updates from your current character.
Army actions require summons; they are not generic mercenary commands.

The bundled Vosk English model recognizes spoken words. Invented spellings such
as HUUUU or RRRRAAAAA are not reliably recognized speech; unknown vocabulary is
reported on the Voice page. Recognition depends on microphone quality and accent.
Use Test to check each phrase. The default confidence threshold is 75%.

Speech processing is local. The mod starts its isolated background recognizer
when enabled; it has no visible app or console. No audio is stored or sent online.
The first build obtains checksum-pinned Vosk runtime/model files; subsequent
builds reuse the local cache. They are staged under the build's voice directory
and installed beside the plugin. An incomplete speech payload fails installation.

Casting uses the existing native skill dispatch, with normal mana, availability
and cooldown checks. Commands expire after 400 ms, carry the current game session,
and have a 1.5-second voice cooldown. Menus, unfocused gameplay, occupied controls,
death and multiplayer block casts. Test mode can recognize while settings are open.

Checks: voice-integration-test covers VR editing, skill assignments, hit targets
and persistence; voice-commands-test covers command gates; voice-payload-test covers
packaging. The helper's --test checks phrase confidence and the shared-memory path;
--transcribe checks real decoding from PCM without opening a microphone.
Headset recognition and actual native spell casting still need in-game verification.
