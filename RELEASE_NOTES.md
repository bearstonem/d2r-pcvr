# D2R PCVR v0.3.15 - Windows preview

This update adds controller vibration controls, clearer loot labels, area and
act information on the hand compass, and fixes for NPC conversations. It
includes the combined changes in [#83](https://github.com/bearstonem/d2r-pcvr/pull/83).

Download **D2R-PCVR-Setup-0.3.15.exe** for graphical installation, or
**d2r-pcvr-windows.zip** for portable setup. Existing users should choose
**Update / repair and keep calibration and video settings**, keeping their
existing helper folders and setup backups. Existing saved settings are preserved.

You need your own purchased Diablo II: Resurrected: Steam 3.3.93787 or
Battle.net 3.3.93847 (experimental). Use local offline characters only.
No Blizzard game assets are included; icons and potion models are read locally
from your installed game. SteamVR and Virtual Desktop/VDXR remain supported.
The installer is unsigned; SHA-256 checksums accompany both downloads.

## Controller vibration

- **Tune feedback for your controllers:** the new **VR Settings > Haptics** tab
  offers overall strength plus separate controls for melee impacts, bow draw,
  bow/throw release, spell casts, interactions and resource alerts. Each ranges
  from Off to 200% in 10% steps. Apply saves; Close discards unsaved edits.
- **Tests follow the action:** Weapon tests the right hand and Shield the left.
  Bow draw and Bow release test both hands; Throw tests the right. Spells and
  interactions have left/right tests, and alerts test the left hand. Tests use
  the displayed settings, including edits you have not saved yet.
- **More noticeable melee feedback:** weapon impacts use a 90 ms tap and shield
  impacts a 45 ms tap. **Short melee pulses** defaults Off; enable it for the
  earlier 10 ms / 180 Hz pulse. Weaker incidental cues cannot cut an impact short.
- **Correct hand routing:** separate controller outputs and isolation from the
  game's ordinary gamepad rumble keep weapon and shield feedback on their
  intended hands. Normal desktop rumble remains available outside active VR.
- **Spell feedback:** supported independent hand spells pulse on the casting
  hand after a successful cast, once per cast or volley.

## Loot and hand displays

- **Hold to show nearby loot:** hold **Right Grip + Right Trigger**, grip first,
  to use the game's Show Items (Alt) behavior. Release either to restore normal
  drop/hover labels. It uses the native nearby-item range regardless of where
  you aim, and the combo does not also cast or shoot. The shortcut is listed in
  **VR Settings > Controls**.
- **Clearer item names:** dropped-item labels are 150% size with darker backing.
- **Area and act at a glance:** the left-hand minimap/compass now shows the
  current area and act. The label follows the navigation display's position,
  and the live location lookup works in town and outdoors.
- **Adjustable health/mana tilt:** **VR Settings > HUD > Health / mana tilt**
  changes the hand display's angle independently of the minimap/compass.

## Conversations and bows

- **Visible NPC dialogue choices:** preserve the complete dialogue panel after
  VR resolution changes, fixing menus that showed only a narrow strip of their
  options. Placement stays consistent through story and choice transitions.
- **Keep the world behind conversations:** enabled sky, fog and ceiling effects
  remain visible while talking to NPCs.
- **Remove the teal bowstring guide:** the debug-looking V-shaped overlay is
  gone. Existing native string posing remains for supported bows; this release
  does not add native posing support for additional bow rigs.

## Validation and remaining limits

The merged gameplay build passed **65/65 native checks**. The owner verified
the installed combined build after the final haptics changes. Earlier feedback
covered the loot hold, area display and NPC dialogue, plus Paladin sword/shield
feedback. The release uses that exact installed DLL from `0fd3491`; subsequent
source changes update release documentation and installer version only.

All **114 Python checks** passed for setup, repair/removal, launchers, asset
preparation, diagnostics and package staging. The compiled installer lifecycle
fixture passed installation, registration, failed-removal helper retention and
successful removal with an isolated stub backend. These checks do not replace
a new end-to-end installer run against a real game installation.

Headset validation is owner-reported. The final confirmation did not enumerate
every controller, class or feature combination; there is no new Quest, Index,
Battle.net or VDXR gameplay validation. Battle.net remains experimental.
Earlier combat, potion, dual-casting and VR resolution improvements are retained.
Unmerged experimental features are excluded.

See [installation instructions](INSTALL-WINDOWS.md) and [controls](CONTROLS.md).

[Full source comparison](https://github.com/bearstonem/d2r-pcvr/compare/v0.3.14-windows-preview...v0.3.15-windows-preview)
