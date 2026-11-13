# Sorceress ice armor assets

These are authored assets, not extracted Blizzard models or game caches.
`ic1.pvr`–`ic3.pvr` are the authored ice pieces, including their RGBA texture
and mip chain. `current-ice.blend` is the editable source with the packed
texture; `ice-texture.png` is the same 512×512 base image.

Geometry adapted from **PK Icicles**, FacadeGaikan / PlanetKiller, CC0:
https://opengameart.org/content/pk-icicles
The geometry has been isolated, resized, rotated, positioned and mirrored for
the forearm icicles. The frost texture was generated with OpenAI image generation
and subsequently tinted; it contains no extracted game imagery.

The installer journals the three runtime files beside the potion models.
If any piece is missing or invalid, the renderer disables the icicles and
leaves the native effect visible. These assets do not replace Bone Armor.

The icicles follow native Frozen Armor (state 10), Shiver Armor (88), and
Chilling Armor (20), rather than an estimated timer. `[render]` options:
`frozen_armor_graft=0` disables it; `frozen_armor_hide_native=0` retains the native
overlay. The forearm rig supplies attachment axes and scaling. Ice uses a 4 cm base
radius, a 2.4 cm elbow shift, and 1.1025 size multiplier. Outer pieces face
75 degrees from center with their attachment positions at 40 degrees. Their
additional outward offsets are 20 mm; the center uses 5 mm. The outer meshes
are flipped end-for-end around their own centers. Bone Armor is unchanged.
Scene depth occludes ice behind equipment; missing depth retains capsule
occlusion only. The final placement was visually accepted on Quest 3 / Virtual Desktop with
Steam game 3.4.95041 and PCVR 0.3.35. This does not establish all shield or
character/equipment combinations.
