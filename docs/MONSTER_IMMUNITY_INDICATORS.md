# Monster immunity indicators

Colored orbiting wisps automatically mark immune enemies during offline VR
play. The indicator is always on: it has no settings-menu entry or saved
configuration key.

| Immunity | Wisp color |
| --- | --- |
| Physical | White |
| Magic | Purple |
| Fire | Red |
| Lightning | Yellow |
| Cold | Blue |
| Poison | Green |

Each immunity has a separate wisp and a short fading trail. Wisps follow the
nearest 24 qualifying hostile monsters within 32 game cells. The scan uses
the existing bounded hostile search (up to 128 candidates), excludes corpses,
friendly units and town NPCs, and requires native line of sight. Scene depth
hides sprites behind walls and bodies. Menus, desktop mode, leaving offline
play clear or hide the indicators. Snapshots expire
after 500 ms, including when game-thread callbacks are dropped.

The scan reads the monster's full current physical, magic, fire, lightning,
cold and poison resistance stats on the game thread every 100 ms. A resistance
of at least 100 marks that type. Reading current stats allows shared resistance
changes such as curses and auras to affect the marker. Attacker-specific
calculations, including Sunder and item effects, are not evaluated here: a
marker describes the monster's shared resistance, not a guarantee that a
particular player's attack cannot damage it.

The fixed-size orbit follows each monster's rendered root position, including
its own terrain height and the renderer's horizontal terrain offset. Position
is smoothed in three dimensions; vertical teleports snap to the new height.
The read-only lookup uses the exact client unit ID and validates the model's
entity generation and transform component. Missing models or failed layout
checks hide that marker until a valid position is available. Native particle
emission and individual monster bounds are future improvements.

The artwork is the gritty glow texture referenced by the game's Feral Rage
overlay. Our renderer supplies the orbit, trail, scale and color; this is not
the complete native Feral Rage particle emitter. No Feral Rage state, skill
bonus, monster resistance, native overlay table or existing skill asset is
changed. If the artwork cache is missing, the same indicator uses a procedural
glow. Original player Feral Rage effects are unaffected.

`steamvr/prepare_monster_immunity.py` validates the overlay-to-particle-to-texture
reference and decodes the BC4 artwork from the owner's installed game. The
installer stages this cache through its normal backup transaction. The PNG and
receipt remain local and must not be committed or included in public releases.
Developer setup copies prepared textures with its existing shader texture tree.

For a developer update, both the DLL and the new shader must be copied;
updating just the DLL cannot add the render pass. Copying the optional texture
provides the Feral Rage artwork; otherwise the procedural fallback is used.
The local test stage contains these files and a manual installer. It does not modify
settings, launch the game, or update the regular D2R-PCVR installation.


## Validation

Automated checks cover resistance boundaries, multiple types, failed reads,
nearest selection, snapshot/session expiry, movement interpolation, teleports,
individual heights, stale model/entity/component rejection, bounded native
lookups and player movement independent of monster height,
artwork validation, and the production vertex and
pixel shaders on D3D11 WARP. Shader checks include both eyes, each color,
occlusion, interface masking, inactive-world hiding, trail fading and both texture paths.
These are separate from live ReShade compilation and headset validation.

In-game checks for this candidate:

- [x] Compare each colored wisp with the game's immunity label on an offline enemy.
- [x] Find a dual-immune enemy and confirm that both colors remain distinguishable.
- [x] Move around an enemy and check depth, motion, walls, death and area changes.
- [x] Apply resistance-lowering effects and check that broken shared immunities clear.
- [x] Open menus and leave/rejoin a game; check that indicators hide and return automatically.
- [x] Use Feral Rage on the player and verify that its own effect is unchanged.
- [x] Check positions on slopes and different monster sizes.

All tests have been carried out in-game using a VR headset. Feature functions as expected.

Headset: Pico VR 4 - Visual Desktop
