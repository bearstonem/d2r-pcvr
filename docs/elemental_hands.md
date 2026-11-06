# Sorceress elemental hand effects

Always on for a Sorceress with Motion combat. Until 2026-11-04 it was an opt-in
setting (VR Settings > World > Spell glow on hands, off by default); the owner
made it on for everyone and took the option out of the menu, so
`[interactions] elemental_hands` is no longer read.

Each Sorceress hand shows a small, live effect of its trigger-bound skill,
drawn with the game's own effect textures from the installed game:

- Fire Bolt and other fire skills: a flame cupped in the fist, from the
  game's 64-frame fire flipbook licked by Fire Bolt's flame distortion, with
  embers rising and a little smoke.
- Fireball and Meteor: a churning fireball core in a ring of flames.
- Charged Bolt and other lightning skills: Charged Bolt's own flash at the
  fist with lightning arcs and bolts crawling off it and sparks.
- Ice Bolt and other cold skills: cold mist sinking from the hand, snowflakes
  circling it and glints.
- Ice Blast, Glacial Spike, Blizzard and Frozen Orb: the Frozen Orb's turning
  shards with ice chunks circling, mist and glints.

The effects are particles (2026-11-05, after a tester found the first
version "very very rudimentary"): each lives, moves, animates and fades on
its own in the room, so a moving hand leaves a short trail of fire, frost
or sparks. Each effect stays about a fist across; embers and mist drift a
little further and fade. The game drawing its own spell effects at the hand
was tried on 2026-10-21 and was much too big (its particles cannot be
scaled), so these are the game's textures on effects of the hand's size.
They are drawn by the mod and are separate from the game's spells, so
projectiles in flight keep their normal size and visuals.

A spell hand also lights the dungeon around it, as a held torch does: fire
orange and flickering, lightning blue and crackling, frost a steady cyan
(`[torch] spell_hands`, on by default; a hand holding a torch keeps the
torch's light).

A held spell shortcut temporarily selects that shortcut's skill; releasing it
restores the trigger binding. The hands can display different spells
simultaneously. Utility skills, passive skills and unassigned or unavailable
bindings show no effect.

The five supported physical hand spells (Fire Bolt, Fireball, Ice Bolt,
Ice Blast and Charged Bolt) make the effect flare briefly and throw a burst
of embers, sparks or flakes after a confirmed native projectile launch. Holding a cast input or attempting a refused cast does
not produce a success pulse. Other elemental skills can select a look but do not
yet provide confirmed-cast pulses through their native fallback path.

Effects disappear during menus, wheel editing, potion handling, holstering,
death, tracking loss and stale or unknown player state. They fade in after
selection and fade out as a hand approaches the face (15–30 cm). Casting rules,
damage and mana costs are unaffected.

## Spell textures

The installer reads 21 textures from the user's installed game:

- the spells' own eleven: the flame plates, flash and glow masks, the Frozen
  Orb's shards and their colour ramps. Before caching each one it checks
  that the owning spell's current particle effect still names it;
- since version 2 of the cache, ten more of the game's effect textures: its
  fire flipbook (8x8 frames), smoke and wisp atlases, two lightning
  flipbooks (arcs and bolts), snowflakes and ice chunks, an ice caustic, a
  glow, a star flare and Fire Bolt's flame distortion.

They are stored next to the plugin in `d2r_vr_spell_sprites` (`sprites.ssp`
and `cache.json`), like the original potion models, and kept in their native
block-compressed formats for direct GPU sampling. Install and Update /
repair prepare it again from the current game, and it is removed with the
mod. The mod never commits or distributes these textures; tests use
synthetic fixtures.

If the cache is missing or invalid (or is version 1, from an older install,
until Update / repair), the log reports why and the hands fall back to the
original procedural aura: fire wisps and embers, cold mist or short
lightning arcs, from original shaders without game art. To rebuild the cache by
hand with the game closed, from the checkout:

```powershell
.\deps\python\python.exe steamvr\prepare_spell_sprites.py --game "<game folder>" --output "<game folder>\mods\D2R-PCVR\d2rloader\plugins\d2r_vr_spell_sprites" --force
```

## Rendering and current limits

The particles (`steamvr/elemental_particles.h`) are stepped once a frame on
the tracked hand poses: each look's emitters spawn them along the hand's way
since the last frame, then they move (rising, sinking, drag, a little
turbulence), grow, turn and fade; a few stay on the hand (a glow, the
fireball's core, the lightning flash, the Frozen Orb). At most 144 a hand
and 320 in all. Both eyes draw the same particles in one instanced draw from
a ring of upload buffers, those covering what is behind (smoke, mist) first,
farthest first, then those adding light. Each kind's material follows the
game's: a mask or flipbook frame, distorted for flames, its brightness
turned into colour through the spell's own gradient. With Stereo rendering
By turns, the eye not drawn that frame gets the effects drawn fresh at its
own pose, like the belt's potions.

The effects are centred slightly ahead of the controller grip, including
while holding equipment. They are composited over the completed VR eye
image, without the game's scene depth, so they can appear over fingers,
weapons or nearby scenery. They do not coat the hand mesh or follow finger
bones. The light is the depth fog shader's game light (as a torch's), so it
lights what the shader lights. The renderer supports the existing RGBA/BGRA
SDR eye targets; initialization failure leaves effects hidden.

## Validation

Run the native Windows build and registered tests:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test
```

`elemental-hands-test` exercises independent skill selection, each skill's
look, utility overrides, class/lifecycle guards, proximity fading,
confirmed-cast feedback matching, the hand light and rejection of corrupt or
version 1 sprite caches. `elemental-particles-test` runs each look's
particles: bounded and near a held hand, a trail behind a moving one, flakes
circling with it, fading with the hand, a burst on a cast, gaps, long frames
and bad input.
`prepare_spell_sprites_test` covers texture formats and mips, invalid
textures, the cache layout, reuse and rebuilds after a game update, and
refusal when a spell no longer names a texture.

`elemental-hands-render-test` uses D3D12 WARP and a synthetic sprite cache
without game assets. It runs the particles a second before each shot and
checks the procedural fallback and all five looks for
animation, cast intensity, stereo disparity, bounded coverage, target-alpha
preservation, hidden scenes, and typed/typeless RGBA/BGRA targets with UNORM
and sRGB views. If installed, the D3D12 debug layer also checks errors. Pass a
PPM path to save a preview of the five looks at arm's length (rows in the order
listed above; left and right eye columns). Add a sprite cache folder to preview
your own game's textures; keep that image local:

```powershell
.\build-frame\windows-native\elemental-hands-render-test.exe .\build-frame\elemental-hands.ppm
.\build-frame\windows-native\elemental-hands-render-test.exe .\build-frame\elemental-hands-game.ppm "<game folder>\mods\D2R-PCVR\d2rloader\plugins\d2r_vr_spell_sprites"
.\build-frame\windows-native\vr-settings-preview.exe .\build-frame\elemental-settings.ppm 2
```

A first headset check on 2026-10-21 confirmed the textures load from the game
and the looks display on the hands. Use only offline local characters. Record
headset, runtime/connection, game edition/build and the tested skills. Still to
check:

- Different spells in each hand, shortcut overrides, release and rebinding.
- Successful casts versus empty-mana/town refusals; no duplicate cast pulse.
- Hand alignment with bare hands, gloves, staff, wand and shield equipment.
- Both eyes while moving/turning, including nearby scenery and hands at the face.
- Menus, wheel editing, bottles, weapon swapping, tracking loss, death and area loads.
- Other classes and desktop play, plus toggle persistence after restart.
- GPU cost and visual comfort at the target headset resolution and refresh rate.

Scene-depth occlusion, actual surface frost and finger-following lightning remain
follow-up work rather than claims of the initial prototype.
