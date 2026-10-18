# D2R VR Unleashed v0.3.19 - Windows preview

This hotfix focuses on making physical shield parries more reliable and responsive
in Unleashed combat.

## Gameplay and combat

- **More forgiving shield parries:** A deliberate diagonal shield sweep now has
  a 750 ms parry window and can begin shortly before an enemy starts attacking.
  Turning your shield edge-on during the sweep no longer rejects an otherwise
  valid parry. Keep the shield raised toward the nearby attacker.
- **Immediate parry feedback:** A valid parry staggers the enemy as soon as your
  sweep and its attack have both begun, instead of waiting for the hit to land.
  Attacks that would miss their normal hit roll can also be parried, so enemies
  with low accuracy no longer make a successful gesture appear to do nothing.
  Each sweep parries one attacker; follow with a strike on that enemy within
  1.5 seconds for a full-damage riposte.

To parry, select **VR Settings > Combat > Melee rules > Unleashed**, equip a
shield in your left hand, and sweep it briskly from lower right to upper left
as a nearby enemy attacks. No extra button is required. Normal shield blocking
and Classic combat rules are unchanged.

## Updating

Download **D2R-PCVR-Setup-0.3.19.exe** for graphical installation, or
**d2r-pcvr-windows.zip** for portable setup. Existing users should choose
**Update / repair and keep calibration and video settings**.

Existing settings, calibration and saves are preserved. No settings reset is
needed for the parry fix.

## Verification and known issues

The release build passed **108 native checks** and **195 Python checks** covering
installation, launching, migrations, local asset preparation, release metadata
and package staging. One additional Python symlink test was skipped because this
Windows session cannot create symbolic links. The compiled installer lifecycle
fixture passed installation, registration, failed-removal recovery and successful
removal with an isolated stub backend. The in-headset release page was rendered
and visually inspected.

The owner verified the parry changes with an offline Barbarian against Act I
Fallen and Zombies. The final development run recorded 13 parries and 5 ripostes,
with 10 parries resolving before the hit frame. Other classes, shields and
monster types still need broader testing; the packaged release has not had a
fresh headset pass.

Shield coverage remains an approximation of the visible mesh. Battle.net remains
experimental. The Sorceress elemental-hands prototype is excluded.

## Special thanks

Thanks to **ChuckRibbits** for the original shield defense, parry and riposte
features, and to everyone testing and reporting issues!

D2R VR Unleashed is a mod by **monkeylashes**, based on the original D2R VR work
by **Sergious / BodyWalkVR**, with contributions from the community.

## Downloads and links

- [Windows installer, portable ZIP and checksums](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.19-windows-preview)
- [Installation instructions](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.19-windows-preview/INSTALL-WINDOWS.md)
- [Controls](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.19-windows-preview/CONTROLS.md)
- [Full source comparison](https://github.com/bearstonem/d2r-pcvr/compare/v0.3.18-windows-preview...v0.3.19-windows-preview)
- [Project on GitHub](https://github.com/bearstonem/d2r-pcvr)

<!-- PCVR_IN_HEADSET_NOTES
release: v0.3.19-windows-preview
credits: ChuckRibbits (original shield combat); Sergious / BodyWalkVR.
- Shield parries are more forgiving: a 750 ms window, including edge-on sweeps.
- Parries stagger enemies as soon as your sweep meets their attack wind-up.
- Enemy attacks that would miss can also be parried; each sweep parries one attacker.
- Unleashed: sweep your left shield lower-right to upper-left; riposte within 1.5 seconds.
PCVR_IN_HEADSET_NOTES -->
