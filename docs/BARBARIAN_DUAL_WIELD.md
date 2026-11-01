# Physical dual wield: Barbarian weapons and Assassin claws

Equip two supported melee weapons on a Barbarian, or two claws on an Assassin,
and release both triggers.
Each tracked weapon supports deliberate swings and thrusts using the existing
VR Settings > Combat speeds to hit. No extra grip or Attack binding is needed
for physical contacts. This adds native basic attacks from the second weapon;
Double Swing, Frenzy, Dragon Claw and other active skills retain their button
casting.

- Unleashed tracks contacts and cleave independently for each hand. Both blades
  share one attack cycle (Cal, 2026-10-30, as a shapeshift form's claws do): a
  full-cycle hit with either blade starts the next cycle, so two blades earn one
  full hit (on-hit effects, strike skills) per cycle, as one native attack rhythm.
  Each stroke is timed as native times a dual-wield attack (2026-10-30): both
  weapons at their averaged WSM, with the swinging weapon's IAS (read with that
  weapon merged; the other weapon's IAS does not count). Both blades can strike
  the same creature, including in the same input batch.
- A strike skill (Bash, Stun, Concentrate, Berserk) cast from either blade
  swings the primary weapon, the rendered right one, as native single-weapon
  skills do; basic attacks swing the blade that struck.
- Classic keeps a shared basic attack cycle across hands and button attacks,
  native hit rolls, and one target per stroke.
- A weapon impact vibrates its driving controller with the weapon-impact
  profile, including the existing strength and short-pulse preferences.
- Shield Smite, shield blocking and two-handed support grips keep their own
  paths. Bows, crossbows, shields, empty off-hands and unknown item types do not
  qualify as a second melee weapon. Assassins require a claw in each hand;
  mixed claw/weapon pairs and other classes do not gain physical dual wield.

## Implementation and validation

Input history and final posed weapon attachments retain their own frame stamp
and world epoch. A separate left-hand engine prevents stroke IDs, hit lists,
recovery or cleave from interfering with the right hand. Menu, stow, tracking,
recenter and equipment transitions cancel old contact samples. Native server
checks validate both item IDs again before dispatching damage.

The native current-weapon selector at RVA `34B000` is scoped to the physical
attacker and the weapon on its rendered R/L attachment. These are composite
slots 5/6, not inventory body slots; equip order can change their relationship.
The inventory accessor `34A360`, composite lookup `3885E0`, weapon-type check
`373890`, usability check `36AD20` and stat-list refresh `4242C0` are guarded
against the supported 3.3.93787 code. Caller signatures also check the damage
and stat-refresh routes. Missing or unexpected code leaves dual attacks off.

Before the basic attack, native stat-list refresh selects the swinging weapon
for accuracy and damage. The native attack still owns damage, resistances,
criticals, procs, leech and durability. A scoped exit restores the native weapon
selection and refreshes its stat lists. There are no permanent equipment,
skill-binding, item-stat or save edits. Other units and threads keep the
original selector. Each stroke's cycle follows D2Common
`UNITS_UpdateAttackAnimRateAndVelocity` (D2MOO): the swinging weapon is merged,
the hero's item IAS and attackrate (skill speed and slows included) are read,
and the two weapons' WSMs are averaged for both arms (D2R 2.4). Native
dual-skill animation sequences (Double Swing, Frenzy) remain outside it.

The legacy [D2MOO basic attack implementation](https://github.com/ThePhrozenKeep/D2MOO/blob/master/source/D2Game/src/SKILLS/Skills.cpp)
helped identify the corresponding native flow. Current addresses and behavior
were checked against the owner's local supported-build snapshot; legacy names
alone are not an ABI guarantee. No snapshot or game assets are distributed.

Automated coverage exercises actual selector/scoping code with synthetic game
functions, rejected classes/items/stale IDs, reversed equip order, nested
scopes, thread isolation and stat restoration. Contact tests cover simultaneous
strokes, independent damage bookkeeping, weapon changes and cancellation.

For headset testing, check left-only/right-only hits, alternating
and simultaneous strokes, thrusts, different weapon types and reversed equip
order. Compare both melee rules and verify shield/single-weapon swaps and menus.
With a Barbarian, **DEV > Hero > Level 90** and **DEV > Gear >
Dual-wield gear** supply test attributes and a unique Phase Blade, Berserker
Axe and combat armor; equip the items manually.
Use **Blood Moor** with an isolated offline `pcvr-dev` character.

For Assassin verification, load an Assassin in `pcvr-dev`, select
**DEV > Hero > Level 90**, then
**DEV > Gear > Claw gear** (Bartuc's and Jade Talon), and equip both claws.
Check each hand separately, alternating/simultaneous swings and forward jabs,
then reverse equip order and compare Unleashed with Classic. Both triggers
must be released. Single-claw/shield swaps should retain their existing paths.
The selector fixture covers both class restrictions, both attachment orders,
each claw's stat selection/restoration, stale IDs and thread isolation. Assassin
headset verification remains pending.

Logs report `Frame dual wield: ... selection ready`, then per-hand landed hits
and stale/unavailable weapon refusals. Ready alone is not gameplay verification.
`[debug] melee_trace=1` adds the hand to each detailed attack record.
