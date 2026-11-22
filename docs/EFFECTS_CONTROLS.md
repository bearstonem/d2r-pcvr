# Graphics > Effects

Visual controls for armor, auras and positive shout buffs. Gameplay bonuses
and damage absorption remain active when their visuals are hidden.

## Controls

- Ice Armor: Off / Original / Icicles.
- Cyclone Armor: Off / Original. No animated replacement is included.
- Bone Armor: Off / Bones.
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
only while an aura or shout option is Off. The draw path checks session, snapshot age,
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
against local OverlayDefinition dependency metadata. Charge, cast/hit/target
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
