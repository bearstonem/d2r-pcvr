# Sorceress ice armor assets

These are authored assets, not extracted Blizzard models or game caches.
`ic1.pvr`–`ic3.pvr` are the current v6 pieces, including their RGBA texture
and mip chain. `current-ice.blend` is the editable source with the packed
texture; `ice-texture.png` is the same 512×512 base image.

Geometry adapted from **PK Icicles**, FacadeGaikan / PlanetKiller, CC0:
https://opengameart.org/content/pk-icicles
The geometry has been isolated, resized, rotated, positioned and mirrored for
the forearm graft. The frost texture was generated with OpenAI image generation
and subsequently tinted; it contains no extracted game imagery.

The installer journals the three runtime files beside the potion models.
If any piece is missing or invalid, the renderer disables the ice graft and
leaves the native effect visible. These assets do not replace Bone Armor.

The graft follows native Frozen Armor (state 10), Shiver Armor (88), and
Chilling Armor (20), rather than an estimated timer. `[render]` options:
`frozen_armor_graft=0` disables it; `frozen_armor_hide_native=0` retains the native
overlay. Existing Bone Armor placement settings determine forearm attachment.
Scene depth occludes ice behind equipment; missing depth retains capsule
occlusion only. Exact behavior still needs current-main headset validation.
