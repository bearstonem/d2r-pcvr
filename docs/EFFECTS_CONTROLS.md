# Graphics > Effects

Visual controls for armor, auras and positive shout buffs. Gameplay bonuses
and damage absorption remain active when their visuals are hidden.

## Controls

- Ice Armor: Off / Original / Icicles.
- Cyclone Armor: Off / Original / White mist.
- Bone Armor: Off / Bones.
- Psychic Ward: Off / Original / Purple mist; defaults Original.
- Assassin charges: Off / Default / Orbs; defaults Default.
- Your auras: Off / On; defaults On.
- Friendly auras: Off / On; defaults On. Includes owned companion recipients.
- Your shout buffs: Off / On, independently of auras; defaults On.
- Friendly shout buffs: Off / On, independently of hero shouts; defaults On.

Changes use the existing Apply/save flow and normal INI reload. Explicit new
armor keys override legacy graft keys; missing armor keys migrate old choices.
Aura keys are `render.player_aura_visual` and `render.summon_aura_visual`.
Shout keys are `render.player_shout_visual` and `render.summon_shout_visual`.
The accepted icicle geometry, texture, placement and size remain unchanged.
Bone grafts require a fresh native Bone Armor state (14); the absorb pool alone
is shared with Cyclone Armor and must not select the bones.
No skill, buff state, absorption, damage, save or game table changes.

## Aura recipient mapping

The read-only mapping follows native render data:
particle instance -> effect component -> full-generation entity -> transform
parent hierarchy -> unit-reference component -> exact client unit type/class/ID.
The current verified server pet roster supplies friendly IDs; native client
hash lookup supplies the matching client pointers. No position matching is used.
Live inspection confirmed the chain for the local Barbarian and wolves across
rebuilt render entity IDs. The shout was used to verify ownership; it is not
classified as an aura.

Storage numbers are discovered dynamically. The transform storage must match
PCVR's verified HeroTransform; the unit-reference storage must contain that
hero's exact client pointer. Ambiguous storages are rejected. Native code guards
check the client table address and effect back-reference layout before use.

The existing game-thread roster callback builds a bounded snapshot every 200 ms,
only while a selected visual needs filtering. The draw path checks session, snapshot age,
entity generations, effect-component identity, recipient identity and client
bucket membership again. Batch-local caching avoids resolving the same emitter
for each particle. Recipient discovery reuses the existing game-thread callback;
one guarded particle-VM hook filters the rendering-enabled result.

Unknown recipients, enemies, unavailable layouts, incomplete/expired data and
recycled entities remain visible. The friendly roster is refreshed; historical
capture IDs are never used by the runtime. There is a 65-unit roster limit,
4096-row effect-storage limit, 1024 mapped-aura limit and 16-parent depth limit;
exceeding the relevant limit preserves visuals.

## Effect scope and performance

An exact list of 24 native persistent aura particle paths covers Paladin auras
and Druid Oak Sage / Heart of Wolverine / Spirit of Barbs. A separate six-path
category covers Shout, Battle Orders and Battle Command buff/cast overlays.
Paths were checked
against local OverlayDefinition dependency metadata. Unrelated cast/hit/target
effects unrelated to those shouts, curses, hand effects and weapon effects are excluded.
Unrecognized modded aura paths stay visible. No extracted assets are distributed.

Aura/shout and Cyclone hiding also overrides the native rendering-enabled
bool result for the exact selected emitter, covering layers that do not use
the local-position callback. The scene flags are never modified. Bone/Ice
retain their verified local-point hiding path. Simulation cost and FPS gains
have not been measured; hiding visuals does not guarantee a performance gain.
Icicles/Bones Off stops the additional custom model rendering.

## Validation

Native build succeeded. Resolver/filter fixtures cover exact parent mapping,
independent player/summon choices, enemy/curse preservation, changed ownership,
session expiry, recycled IDs, reattachments, replaced components, missing reads,
cycles and source memory remaining unchanged. Settings tests cover defaults and
On/Off persistence through Apply while preserving unrelated keys.

Build with `steamvr/build_windows.ps1 -Test`. Focused native fixtures include
armor-graft, frozen-armor-overlay, aura-recipient, aura-filter,
effect-visibility and VR settings/store tests. The Effects page also supports
the standard settings preview and fit checks.

User confirmed Holy Fire and Might visual hiding in VR, including on the
mercenary. Ice/Bone modes and hero/companion shout hiding were also confirmed.
An explicit enemy negative case and complete Cyclone retest remain pending.

## Forearm mist and Assassin charges

Cyclone Armor (native state 151) and Psychic Ward (206) can replace the local
native overlay with white or purple mist. Psychic Ward matches only its ward
and ward-hit particle paths, preserving other units and unrelated Warlock effects.
Mist follows the drawn forearm: Cyclone 4 cm and Psychic Ward 10 cm toward the
elbow from the wrist. It fades near the head and clears on stale state, death,
tracking loss or session change. No extra light or central glow is added.

Assassin Orbs shows native 1/2/3 charge counts for Tiger Strike, Cobra Strike,
Fists of Fire, Claws of Thunder, Blades of Ice and Phoenix Strike on both arms.
Rings have fixed size and start 4 cm toward the elbow; concurrent skills have
separate rings. Colours approximate the native skill palettes. Exact local-player
orbit paths are hidden only when replacement counts are valid. Unknown counts
leave native effects visible. Finishers, missiles and gameplay remain unchanged.
Native stats 318, 319, 321, 323, 322 and 320 are checked against states 122, 123,
125, 127, 126 and 124 using the existing game-thread sample and guarded accessor.

Mist and orbs reuse the hand renderer with and without the spell-sprite cache.
A simple head-to-particle ray against either forearm hides whole particles behind
arms. It is approximate visibility, not per-eye pixel clipping or game depth;
weapons and scenery do not occlude these custom particles. No new assets,
dependencies, hooks or extracted game data are distributed.

Settings are render.psychic_ward_mode and render.assassin_charges_visual (0 Off,
1 native, 2 replacement); render.cyclone_armor_visual gains value 2 for White mist.
All use the existing Apply/save flow. Your and Friendly auras share one row to
keep the Effects page within its eight-row layout; both remain independent.

Regression checks cover ownership, stale/invalid counts, simultaneous skills,
state-driven mist, forearm placement/occlusion, INI modes and GPU rendering with
and without the sprite cache. Headset screenshots should accompany the PR.
