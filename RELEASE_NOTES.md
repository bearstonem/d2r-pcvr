# D2R VR Unleashed v0.3.18 - Windows preview

This update focuses on physical combat and shield defense, wrist information,
quicker item and skill selection, and more complete first-person environments.

## Gameplay and combat

- **Unleashed combat - contributed by ChuckRibbits:** Physical swings still
  connect with every enemy they reach, while damage now reflects attack rating
  against enemy defense, recovery between strikes, and reduced damage to later
  targets in a cleave. Increased Attack Speed helps recovery. Deliberate,
  well-timed blows are more effective than rapid flailing. Classic remains
  available under **VR Settings > Combat > Melee rules**.
- **Physical shield blocking, parries and ripostes - contributed by ChuckRibbits:**
  In Unleashed, hold your shield between yourself and the attacker to block,
  including while walking or running. Move the shield diagonally across your
  view just before impact to parry; strike the parried enemy within 1.5 seconds
  for a full-damage riposte. Blocking builds guard strain, shown by an edge
  vignette and heartbeat. Lower your shield to recover. Choose light or dark
  feedback and **Slow / Medium / Fast** buildup under Combat; Medium is the
  default. These choices change how quickly strain builds, not vignette size.
  Shield coverage accounts for the displayed shield's orientation and size
  where available, with a fallback for unsupported models.
- **Deliberate shield bashes and Smite:** Physical shield attacks now require
  forward movement, reducing accidental attacks while guarding. Adjust
  **Combat > Shield bash speed** from 0.75-10 m/s; the default is 2 m/s.
- **Barbarian physical dual wielding:** Swing or thrust either equipped melee
  weapon, with damage and vibration from the hand that struck. Release both
  triggers for physical attacks; no extra Attack binding is required. This adds
  basic physical attacks, not gesture versions of Double Swing or Frenzy.
- **Break pots and barrels with your weapon:** Deliberate weapon swings can
  smash nearby breakables. The existing Interact-button kick remains available.
- **Adjustable offline difficulty:** **Combat > Offline difficulty** offers
  native **Players 1-8** scaling for tougher monsters and increased rewards.
  **Game default** preserves the game's existing choice. Changes affect newly
  spawned monsters; **Save and Exit, then start a new game** to apply the change
  throughout an area you have already visited.

## Controls and interface

- **Companions and active effects on your wrist:** The minimap now shows
  companions, including Necromancer skeleton counts, alongside current buffs,
  auras, curses and other character effects. This reflects what is affecting
  your character, including effects received from others. It follows your
  wrist/palm navigation display and can be toggled under
  **HUD > Companions & effects**.
- **Quick-use item slot:** Both hand wheels now include **QUICK USE**. Short-click
  with the opposite hand's trigger to use the assigned item; hold for 0.65 seconds
  to choose a scroll, tome or other usable inventory item. The assignment survives
  restocking and restarts. Identify opens inventory so you can select a target.
- **Run/walk from the wheel:** A dedicated **RUN / WALK** button changes movement
  mode without leaving VR.
- **Class skill categories:** Skill selection now has your class's three skill
  trees, **All**, and **General / Items**, making larger skill lists easier to
  navigate while retaining existing assignments.
- **Settings help and release notes in VR:** Hover over settings for a short
  explanation beside the menu. The **Release Notes** page shows this version's
  main changes, credits and project link, and works offline.
- **Framed minimap:** A minimal bronze frame surrounds the map while preserving
  its full contents and space for companion and effect information.

## Visuals and stability

- **Surrounding Arcane Sanctuary sky:** Stars and nebulae now surround the level
  instead of appearing as a flat backdrop on one side. Setup prepares the sky
  locally from your installed game.
- **More complete building interiors:** Selected roof and tavern wall materials
  render from both sides, addressing missing undersides and window surrounds in
  first person. This improves the inspected Lut Gholein buildings; it is not a
  claim that every building asset has been checked.

## Updating

Download **D2R-PCVR-Setup-0.3.18.exe** for graphical installation, or
**d2r-pcvr-windows.zip** for portable setup. Existing users should choose
**Update / repair and keep calibration and video settings**.

Existing saved settings, calibration and normal characters are preserved.
Use Update / repair so the new locally prepared sky and current helper files
are installed along with the plugin. If physical weapon swings and shield bashes
do not register after updating, select **Classic**, Apply, then select
**Unleashed** and Apply again; this recovered the issue in one development test.

## Verification and known issues

Validation passed **108 native checks** and **195 Python checks** covering the
installer, launcher, migrations, local asset preparation, release metadata and
package staging. One additional Python symlink test was skipped because this
Windows session cannot create symbolic links. The compiled installer lifecycle
fixture passed installation, registration, failed-removal recovery and successful
removal using an isolated stub backend. The in-headset release page was rendered
and visually inspected.

The owner verified wrist companions and active effects, hover tooltips, Barbarian
dual wielding, weapon and button pot breaking, the Arcane Sanctuary sky, and shield
improvements during development. These confirmations do not cover every headset,
runtime, weapon or area. The final release artifact has not had a fresh complete
headset pass; quick-use consumption across all item types and a full Players 1/8
comparison still need broader testing.

Shield coverage is an approximation and may not exactly match the visible mesh.
Battle.net remains experimental. Airborne-enemy targeting is unchanged, and
projectile terrain collision still uses the existing launch-floor approximation.
Long Act III crash-reproduction testing remains outstanding. The unmerged
Sorceress elemental-hands prototype is excluded.

## Special thanks

Huge thanks to **ChuckRibbits** for the Unleashed combat, shield defense, parry,
riposte and guard feedback contributions, and to everyone testing, reporting
bugs and helping improve the mod!

D2R VR Unleashed is a mod by **monkeylashes**, based on the original D2R VR work
by **Sergious / BodyWalkVR**, with contributions from the community.

## Downloads and links

- [Windows installer, portable ZIP and checksums](https://github.com/bearstonem/d2r-pcvr/releases/tag/v0.3.18-windows-preview)
- [Installation instructions](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.18-windows-preview/INSTALL-WINDOWS.md)
- [Controls](https://github.com/bearstonem/d2r-pcvr/blob/v0.3.18-windows-preview/CONTROLS.md)
- [Full source comparison](https://github.com/bearstonem/d2r-pcvr/compare/v0.3.17-windows-preview...v0.3.18-windows-preview)
- [Project on GitHub](https://github.com/bearstonem/d2r-pcvr)

<!-- PCVR_IN_HEADSET_NOTES
release: v0.3.18-windows-preview
credits: ChuckRibbits (combat and shields); Sergious / BodyWalkVR.
- Unleashed damage reflects attack rating, strike recovery and cleave targets.
- Physically block while moving; parry attacks and follow with a riposte.
- Guard strain: choose light/dark feedback and Slow/Medium/Fast buildup.
- Shield bash speed is adjustable separately from weapon swings.
- Barbarians can swing or thrust either of two equipped melee weapons.
- Smash pots and barrels with weapon swings or the Interact button.
- Offline difficulty: native Players 1-8 scaling under Combat.
- Wrist display shows companions, summon counts and active character effects.
- Wheel Quick Use: click to use an item; hold to assign a scroll, tome or consumable.
- Toggle Run/Walk directly from either hand wheel.
- Find skills by class tree, All, or General / Items.
- Hover over settings for help; read this release summary in VR.
- Arcane Sanctuary stars and nebulae surround the level.
- Two-sided roof and tavern materials improve first-person interiors.
PCVR_IN_HEADSET_NOTES -->
