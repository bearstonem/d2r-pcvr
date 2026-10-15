# Sorceress elemental hand effects (prototype)

Enable **VR Settings > Combat > Elemental hand effects**, then **Apply**.
The setting defaults to Off and is saved as `[interactions] elemental_hands=1`.
Motion combat must also be enabled.

Each Sorceress hand shows a subtle procedural aura for its trigger-bound skill:
fire wisps and embers, cold mist, or short lightning arcs. A held spell shortcut
temporarily selects that shortcut's element; releasing it restores the trigger
binding. The hands can display different elements simultaneously. Utility
skills, passive skills and unassigned or unavailable bindings show no effect.

The five supported physical hand spells (Fire Bolt, Fireball, Ice Bolt,
Ice Blast and Charged Bolt) brighten the effect briefly after a confirmed native
projectile launch. Holding a cast input or attempting a refused cast does not
produce a success pulse. Other elemental skills can select an aura but do not
yet provide confirmed-cast pulses through their native fallback path.

Effects disappear during menus, wheel editing, potion handling, holstering,
death, tracking loss and stale or unknown player state. They fade in after
selection and fade out as a hand approaches the face (15–30 cm). Casting rules,
damage and mana costs are unaffected.

## Rendering and current limits

This first implementation uses original procedural shaders and geometry. It
does not load or distribute game art and adds no dependency. Animation and
tracked hand poses are frozen once per stereo pair, then projected separately
through each eye. Each visible hand draws 32 small quads in one draw call.

The auras follow the controller grip poses, including while holding equipment.
They are composited over the completed VR eye image, without the game's scene
depth. They can therefore appear over fingers, weapons or nearby scenery.
They do not coat the hand mesh, follow individual finger bones, emit light onto
the world, or replace the native spell visuals. The renderer supports the
existing RGBA/BGRA SDR eye targets; initialization failure leaves effects hidden.
The feature remains an opt-in draft pending headset review of these limitations.

## Validation

Run the native Windows build and registered tests:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File steamvr/build_windows.ps1 -Test
```

`elemental-hands-test` exercises independent skill selection, utility overrides,
class/lifecycle guards, proximity fading and confirmed-cast feedback matching.
The settings fixtures cover the toggle and INI transactions beyond bit 31; the
settings change mask now has 64 bits, without changing existing setting indices.

`elemental-hands-render-test` uses D3D12 WARP without game assets. It checks all
three shaders, animation, pulse brightness, stereo disparity, bounded coverage,
target-alpha preservation, hidden scenes, and typed/typeless RGBA/BGRA targets
with UNORM and sRGB views. If installed, the D3D12 debug layer also checks errors.
Pass a PPM path to save a synthetic preview (fire, cold and lightning rows;
left and right eye columns):

```powershell
.\build-frame\windows-native\elemental-hands-render-test.exe .\build-frame\elemental-hands.ppm
.\build-frame\windows-native\vr-settings-preview.exe .\build-frame\elemental-settings.ppm 4
```

Headset testing is still required and has not been performed for this prototype.
Use only offline local characters. Record headset, runtime/connection, game
edition/build and the tested skills. Check:

- Different elements in each hand, shortcut overrides, release and rebinding.
- Successful casts versus empty-mana/town refusals; no duplicate cast pulse.
- Hand alignment with bare hands, gloves, staff, wand and shield equipment.
- Both eyes while moving/turning, including nearby scenery and hands at the face.
- Menus, wheel editing, bottles, weapon swapping, tracking loss, death and area loads.
- Other classes and desktop play, plus toggle persistence after restart.
- GPU cost and visual comfort at the target headset resolution and refresh rate.

Scene-depth occlusion, actual surface frost and finger-following lightning remain
follow-up work rather than claims of the initial prototype.
