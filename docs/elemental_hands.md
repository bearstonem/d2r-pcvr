# Sorceress elemental hand effects (prototype)

Enable **VR Settings > Video > Elemental hand effects**, then **Apply**.
The setting defaults to Off and is saved as `[interactions] elemental_hands=1`.
Motion combat must also be enabled.

Each Sorceress hand shows a small representation of its trigger-bound skill,
made from that spell's own effect textures in the installed game:

- Fire Bolt and other fire skills: a flame with rising embers.
- Fireball and Meteor: a fireball core with three orbiting flame wisps.
- Charged Bolt and other lightning skills: flickering lightning flashes around
  a faint glow.
- Ice Bolt and other cold skills: a cold glow with slowly orbiting ice glints.
- Ice Blast, Glacial Spike, Blizzard and Frozen Orb: a slowly spinning Frozen
  Orb core with orbiting glints.

Each look is a few centimetres across and stays within about 5 cm of the hand.
It is drawn by the mod and is separate from the game's spells, so projectiles
in flight keep their normal size and visuals.

A held spell shortcut temporarily selects that shortcut's skill; releasing it
restores the trigger binding. The hands can display different spells
simultaneously. Utility skills, passive skills and unassigned or unavailable
bindings show no effect.

The five supported physical hand spells (Fire Bolt, Fireball, Ice Bolt,
Ice Blast and Charged Bolt) make the effect flare briefly after a confirmed
native projectile launch. Holding a cast input or attempting a refused cast does
not produce a success pulse. Other elemental skills can select a look but do not
yet provide confirmed-cast pulses through their native fallback path.

Effects disappear during menus, wheel editing, potion handling, holstering,
death, tracking loss and stale or unknown player state. They fade in after
selection and fade out as a hand approaches the face (15–30 cm). Casting rules,
damage and mana costs are unaffected.

## Spell textures

The installer reads 11 textures from the user's installed game: flame plates,
flash and glow masks, the Frozen Orb shard and their colour ramps. Before
caching each one it checks that the owning spell's current particle effect
still names it. They are stored next to the plugin in `d2r_vr_spell_sprites`
(`sprites.ssp` and `cache.json`), like the original potion models, and kept in
their native block-compressed formats for direct GPU sampling. Install and
Update / repair prepare it again from the current game, and it is removed with
the mod. The mod never
commits or distributes these textures; tests use synthetic fixtures.

If the cache is missing or invalid, the log reports why and the hands fall back
to the original procedural aura: fire wisps and embers, cold mist or short
lightning arcs, from original shaders without game art. To rebuild the cache by
hand with the game closed, from the checkout:

```powershell
.\deps\python\python.exe steamvr\prepare_spell_sprites.py --game "<game folder>" --output "<game folder>\mods\D2R-PCVR\d2rloader\plugins\d2r_vr_spell_sprites" --force
```

## Rendering and current limits

Animation and tracked hand poses are frozen once per stereo pair, then
projected separately through each eye. Each visible hand draws up to 8
camera-facing sprites (32 quads for the procedural fallback) in one draw call.
With Stereo rendering By turns, the eye not drawn that frame gets the effects
drawn fresh at its own pose, like the belt's potions, rather than moved with
the scene's depth.

The effects follow the controller grip poses, centred slightly ahead of the
grip, including while holding equipment. They are composited over the
completed VR eye image, without the game's scene depth. They can therefore
appear over fingers, weapons or nearby scenery. They do not coat the hand mesh,
follow individual finger bones, emit light onto the world, or use the game's
own particle simulation. The renderer supports the existing RGBA/BGRA SDR eye
targets; initialization failure leaves effects hidden. The feature remains an
opt-in draft pending further headset review.

## Validation

Run the native Windows build and registered tests:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test
```

`elemental-hands-test` exercises independent skill selection, each skill's
look, utility overrides, class/lifecycle guards, proximity fading,
confirmed-cast feedback matching and rejection of corrupt sprite caches.
The settings fixtures cover the toggle, its own change-mask bit, conflicts and
INI opt-in/opt-out; existing setting indices are unchanged.
`prepare_spell_sprites_test` covers texture formats and mips, invalid
textures, the cache layout, reuse and rebuilds after a game update, and
refusal when a spell no longer names a texture.

`elemental-hands-render-test` uses D3D12 WARP and a synthetic sprite cache
without game assets. It checks the procedural fallback and all five looks for
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
