# Quality of Life

The **VR Settings > Quality of Life** page is built into PCVR. No additional
plugin is needed.

The four multipliers default to **1**; **Merc potion assist** and
**Click to drink** default to **Off**, preserving existing behavior. These
settings persist
in `d2r_pcvr.ini` under `[quality_of_life]` through the normal menu Apply,
backup and conflict-handling path. Use with **local offline characters only**.

| INI key | Range | Takes effect |
| --- | --- | --- |
| `incoming_damage_multiplier` | 0–1 | After Apply |
| `monster_movement_multiplier` | 0–3 | Next native velocity assignment after Apply |
| `druid_aggro_multiplier` | 1-5 | Restart after Apply |
| `druid_leash_multiplier` | 1-5 | Restart after Apply |
| `click_to_drink` | 0 (Off) or 1 (On) | After Apply |
| `merc_potion_assist` | 0 (Off) or 1 (On) | After Apply |

With **Click to drink** on, hold a flask and press the trigger on the same
hand to drink it without bringing it to your face. The mouth-and-tip gesture
remains available. Off keeps gesture-only drinking. One potion is consumed per
grab; an already-held trigger must be released and pressed again. Presets leave
this preference alone. While a potion is held with this option on, mercenary
commands are blocked so the drinking trigger cannot also issue an order.

With **Merc potion assist** on, a thrown Health, Rejuvenation, Antidote or
Thawing potion feeds your living mercenary wherever the bottle first lands,
including a miss against the ground, a wall or a prop. With it off, the bottle
must land within 1.5 m of the mercenary's feet; a miss keeps the potion on your
belt. Both bottle physics modes support assistance. Slow releases still return
the bottle. Native potion rules and belt-item checks still apply; an absent or
dead mercenary never consumes the potion. Presets leave this preference alone.

The damage control covers the verified direct-damage final health write.
Healing is unchanged. Zero prevents supported damage; it does not promise
protection from poison, burning or other paths. Fixed-point damage rounds up,
and overkill uses the original damage record rather than a clamped HP delta.
Existing shield/defense damage factors compose with this multiplier once.

Movement affects ordinary hostile monster WALK/RUN velocity assignments.
Players, mercenaries, NPCs, projectiles, attacks, knockback and special skill
movement retain their native behavior. The server's own
thread checks hostility; only unit IDs/session tags cross to the client.
Unknown cache entries retain native speed. The shared velocity and server
lookup hooks continue serving player run speed, projectiles and melee.

Aggro range and leash range cover Druid wolves, Grizzly, ravens, spirits and vines;
Necromancer skeletons, mages, golems and revives; Amazon Valkyrie; and Assassin
Shadow Warrior/Master. Mercenaries and monster-owned minions retain native behavior.
Noncombat spirits retain their defensive AI; utility vines search farther for
corpses rather than gaining attacks. The legacy `druid_*` INI keys are retained
to preserve existing settings. The controls change target
search/pursuit and owner-follow/catch-up distances, including teleport catch-up.
They do not alter native attack reach, combat eligibility, attack speed or
attack chance. Dire Wolf corpse-eating chance is preserved. Wolves reuse their
pursuit threshold for some owner-follow checks, so the two controls interact.
Other summons use a scoped owner-distance adapter, rounded upward to whole native
cells. Enemy distances and native combat eligibility are never rescaled.
Skeleton warriors, skeletal mages and all four golems use the aggression setting
squared for their own target searches and pursuit limits: 4x gives 16x native
aggro range, and 5x gives 25x. Their leash still uses the ordinary leash setting.
The owner-centered fallback search retains its native cap.
Shadow Master has mixed-use parameter slots: only its owner-leash field is scaled.
Larger idle follow distances let summons spread out farther. Native room,
collision and pathfinding rules still limit reachable targets.

Skeletal mage projectile speed also follows the startup summon aggression
multiplier (1-5x), for all four elements on client and server. Missile lifetime
stays native, so faster bolts can travel farther before expiring. This uses the
existing creation/velocity hooks and matches both the summoned mage class and
its missile IDs; enemy bolts and mercenary projectiles keep their native speed.
It requires restart after Apply, just like summon aggression.

Summon ranges support a 5x limit: guarded comparison detours replace
the native signed 8-bit immediate comparisons with 32-bit thresholds, preserving
the original registers, flags and branch destinations. The bear search argument
already uses a 32-bit immediate. These startup code changes must not be enabled
in an online session. They remain as loaded until the plugin unloads; changing the menu or
camera mode does not undo those patches. Damage and movement additionally
check the native offline-mode getter before applying their factors.

There is no new timer, worker thread, per-frame settings read or enemy scan.
Settings update on the existing settings reload/apply path; native hooks act
on the game's existing damage, velocity and AI decisions. Larger search ranges
can increase the work of the native searches. Adapter availability appears on
the control's help card; preferences stay editable if an adapter is inactive.

## Native evidence and validation

Existing combat, velocity and lookup signatures come from the mod's native
modules. New summon signatures and calling conventions were checked read-only
against running D2R build **93847**. Wolf AI 108 dispatches at RVA `0x5ca610`
to Spirit Wolf `0x5ca070` or Dire Wolf `0x5c99b0`; Grizzly AI 112 is `0x5c95d0`.
The scoped friendly search calls `0x596340` at `0x595d8c`. Its callback at
`0x597e00` checks the query's search cap at offset `0x20`. Every hook, layout
guard and patched instruction is checked before any summon mutation. These
sites are fixed signatures; unsupported/moved code disables the adapter.
Partial installation rejects plugin loading and shuts down the runtime so
the loader can restore owned mutations.

The legacy community [AI reconstruction](https://github.com/ThePhrozenKeep/D2MOO/blob/master/source/D2Game/src/AI/AiThink.cpp)
and [target-search reconstruction](https://github.com/ThePhrozenKeep/D2MOO/blob/master/source/D2Game/src/AI/AiUtil.cpp)
were behavioral references, not D2R ABI evidence. No game assets or private
memory/disassembly captures are distributed.

Automated checks cover damage arithmetic, both wolf layouts, patch/signature
failures, friendly/hostile movement, client/server cache isolation, actual comparison-detour execution
above 127, lifecycle,
settings persistence and menu fit/reachability. The integrated Windows Release
build passed all 176 CTests. Human reports successful offline QoL headset testing
with Druid, Necromancer, Assassin and Amazon on Pico 4 through Virtual Desktop
and SteamVR, Battle.net build 93847, including the final summon changes. Human
also confirmed the integration with current main works as expected. This report
does not establish that every individual edge case above was tested in game.

The newer **Click to drink** and **Merc potion assist** options also have
automated coverage, and Robin confirms both have passed headset testing.
