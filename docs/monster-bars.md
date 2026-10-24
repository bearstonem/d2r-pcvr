# Ordinary monster bars in native VR

Open **VR Settings > Interface > Weak monster bars** and press **Apply** to
show or hide ordinary monsters' health and name plates together. The default
is Off, including existing profiles without `[hud] ordinary_mob_bars`.
Bosses, champions, uniques and their minions retain their native bars.
NPC names and desktop label calls are preserved. This is independent of
`[hud] stereo_labels`, the stereo anchor correction option.

The filter runs in the existing signature-checked `UnitToScreen` hook,
only for above-unit label anchors during native stereo UI rendering with an
active camera override. It rejects the native label's visibility result,
without clearing a screen rectangle or changing the selected/attack target.
It reads the current unit on the calling thread; no target is cached between
eyes or frames. Failed reads and unrecognized records retain native labels.

Layout evidence:

- Unit type/class at `+0/+4`, monster data pointer at `+0x10`, and the
  monster data's table pointer at `+0` and special flags at `+0x1a` agree with
  [D2RMH's published structures](https://github.com/soarqin/D2RMH/blob/master/src/d2r/d2rdefs.h).
  Only layout facts are used; no implementation is copied.
- The owner's local D2RLoader 3.3.0 compiler descriptors for MonStats identify
  the row ID at `+0` and flags at `+0x3c`: enabled bit 25, killable bit 15,
  boss/primeevil bits 6/7, NPC/interact/inTown bits 8/9/10. The compiled
  skeleton, Akara and five act-boss records were checked against these fields.
  No compiler cache or game table data is distributed with the mod.
- A matching row ID and both enabled/killable bits are required before
  suppression. Any special-monster flag preserves the bar.

Automated fixtures cover filtering, failed reads, both eye passes, settings
defaults, hit regions and INI persistence. Actual native label disappearance
and boss retention still require offline headset verification: ordinary packs,
hovered targets, champion/unique packs, an act boss, NPCs, and toggling both
ways while moving between targets. Repeat with stereo anchor correction off.
