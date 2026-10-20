# Wrist vitals artwork

`wrist-vitals.png` is new artwork generated with the built-in OpenAI image tool
on 2026-10-09. The style reference was the life/mana HUD in a local D2R gameplay
capture (`build-frame/dlss-artifacts-20261009-162519/dlss-off.png`). That screenshot
is development-only and is not distributed. The exact generation prompt is in
[`wrist-vitals-prompt.txt`](wrist-vitals-prompt.txt).

The source is a 1774 × 887 RGBA PNG with transparent surroundings. It is embedded
in the plugin and preview executable by `interaction_assets.rc`, then decoded
once with WIC at 512 × 256. No loose asset installation is required.

Both source orbs are full. `interaction_ui.cpp` darkens the drained portion of
each glass well from authoritative live life/mana values, retaining its glass
highlight and leaving the stone figures intact. Runtime text is drawn into the
empty nameplates. Unknown/stale vitals use empty wells and `-- / --`, never a
misleading full orb. The final atlas preserves the source alpha.

At the 512 × 256 runtime size the glass centers are `(147.8,125)` and `(364,125)`;
their radii are approximately `(57.5,56.5)`. If replacing the artwork, update
these masks and the nameplate rectangles in `interaction_ui.cpp` together.

Run `interaction-preview.exe <output.png> --wrist-check` to render full, partial,
empty and unavailable states and check fill/alpha/other-atlas-tile isolation.
The preview uses synthetic values and never opens game data.

## Wheel frame

`wheel-frame.png` is new transparent artwork generated with the built-in OpenAI
image tool on 2026-10-09, using the generated wrist artwork as its style reference.
The exact prompt is in `wheel-frame-prompt.txt`. It supplies carved stone,
aged-bronze dividers, eight empty slots, a center socket and two footer buttons.
It contains no spell/item art or baked labels. It is embedded with the wrist art.

At 768 square, the interactive center is `(384,345.6)`. Segments start at the
top divider; their centers run clockwise from 22.5 degrees (upper right).
`WheelCell` is shared by highlights and ray selection. Fonts, native icons,
counts and state highlights are composited at runtime; the exterior retains
source alpha. `interaction-preview.exe <output.png> --wheel-check <cache-dir>`
renders both pages with synthetic assignments and locally extracted icons.

Native spell/item icons are **not** in this folder or in the release. Run
`steamvr/prepare_icons.py` against a legitimate local Steam installation to
create `d2rloader/plugins/d2r_vr_icons`. See INSTALL.md for the command.

## Five-slot hand wheel frame

`hand-wheel-frame.png` is original transparent artwork generated with the
built-in OpenAI image tool on 2026-10-13. Its exact prompt is saved in
[`hand-wheel-frame-prompt.txt`](hand-wheel-frame-prompt.txt). No game screenshot,
extracted artwork, or third-party image was used as an input. It follows the
existing charcoal-stone and aged-bronze UI materials.

Both hand wheels share this frame. It contains five empty 72-degree sectors,
with the trigger sector centered at twelve o'clock and a divider at six
o'clock. Button labels, assignment icons, center labels, utility buttons and
highlights remain runtime content. The equipment page retains the eight-sector
frame. Both frames are embedded in the DLL, decoded once, and preserve their
transparent silhouette. No additional loose files are needed for installation.

Run `interaction-preview.exe <output.png> --hands-check` to preview both hand
layouts. Runtime hover masks use the same five-sector `WheelCell` geometry as
the laser hit testing.

## Riposte sound

`riposte-impact.wav` (16-bit PCM, 48 kHz stereo, 1.08 s) is the impact played when
a riposte lands. It is "Combat Impact" by Universfield on Pixabay (item 352458),
supplied by the contributor (ChuckRibbits) as an MP3 and converted to WAV without
other changes. It is embedded by `interaction_assets.rc` and played from memory at
the game's Master x Combat volume; `[gameplay] riposte_sound_file` replaces it.

## Low-life heartbeat

`guard-heartbeat.wav` (16-bit PCM, 48 kHz stereo, 0.50 s) is one lub-dub played
while life is low, below VR Settings > Combat > Heartbeat starts at (60 % by default). It is the third beat of "Heartbeat" by
SoundReality on Pixabay (item 549797), supplied by the contributor (ChuckRibbits)
as an MP3: decoded at its own 48 kHz, cut from 20 ms before the lub to 0.50 s with a
5 ms fade in and a 60 ms fade out, and peak-normalised to -1 dBFS. It is embedded by
`interaction_assets.rc` and queued once per beat at a tempo and loudness that follow
the life left; `[gameplay] low_life_heart_file` replaces it.

## Guard break bone snap

`guard-break-bone.wav` (16-bit PCM, 48 kHz stereo, 0.86 s) plays once when the
shield guard breaks. It is "Bone Snap" by saboteurcomics on Pixabay (item 408148),
supplied by the contributor (ChuckRibbits) as an MP3: resampled to 48 kHz, given a
+3 dB low shelf at 120 Hz for more weight, then raised 12 dB into a -1 dBFS peak
limiter (louder in game: about +2 LU over the source). It is
embedded by `interaction_assets.rc` and played on its own waveOut stream at the
game's Master x Combat volume x `[gameplay] guard_break_volume` (100 %);
`guard_break_sound_file` replaces it.

## Visor blood

`blood-splatters.png` packs eight transparent blood-splatter images supplied by
the contributor (ChuckRibbits) into one 3072 × 384 strip of 384-pixel cells, by
`prepare_blood.py`. The contributor states they are free, openly licensed vector
splatters; the original download source was not recorded. Replace them with
traceable art if that is required for distribution.
It is embedded by `interaction_assets.rc` and decoded once with WIC on the blood
painter thread. `blood_screen.h` places the cells around the visor (`Layout`),
grows drips from each placement's largest blobs and keeps the view's centre
clear. A missing or wrong-sized strip falls back to generated discs.
