# D2R PCVR controls

Windows preview for **SteamVR and Virtual Desktop (VDXR)**. These controls
use two tracked VR controllers; an Xbox controller is not required.

**VR Settings > Release Notes** shows one line per feature from the last
published release. It is available to everyone and works offline.

In [developer mode](steamvr/DEVELOPMENT_SCENARIOS.md), the hand-wheel **DEV** tab
provides Character, Equipment and act-grouped travel tools. These test tools are
hidden in ordinary play; launch with `--frame-vr --frame-dev` for isolated offline tests.

## Button names

| Hand / button | Quest / Touch | Valve Index | Steam Frame |
| --- | --- | --- | --- |
| Right primary / secondary | A / B | A / B | A / B |
| Left primary / secondary | X / Y | A / B | D-pad Down / Up |
| Trigger | Index trigger | Index trigger | Index trigger |
| Grip | Grip button | Squeeze | Grip input |
| Stick click | Press the thumbstick | Press the thumbstick | Press the thumbstick |

**Left primary** below means **Quest X / Index A / Frame D-pad Down**.
**Left secondary** means **Quest Y / Index B / Frame D-pad Up**.

Wheel button badges, grip combinations, skill-picker headings and the Controls
help use the active controller layout. Open either wheel > **VR SETTINGS** >
**VR**, then use the **Headset / controllers** dropdown to choose **Automatic**
(default), **Quest / Touch**, **Valve Index**, **Steam Frame**, or **Generic**.
Select **Apply** to save; closing without Apply discards the choice.

Automatic reads each hand's active OpenXR interaction profile and refreshes
when controllers change. This also supports mixed controller layouts. Unknown
profiles display Primary / Secondary rather than guessed button letters. The
legacy WMR profile displays Unbound for face slots, which it does not bind.
The override changes labels only; it preserves skill assignments and controls.
Custom SteamVR remappings may need the Generic labels.

Steam Frame uses its native profile when the runtime advertises it. With an
older runtime's Touch compatibility binding, Automatic can report Touch;
select Steam Frame manually to show its D-pad labels. The compatibility binding
also aliases the upper face/D-pad buttons to the secondary action; the wheel
shows B and D-pad Up as the representative controls. See
[Valve's Frame input documentation](https://partner.steamgames.com/doc/steamhardware/steamframe/input)
and the [OpenXR controller profiles](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#interaction-profiles).

The saved override is `[interactions] controller_labels`: 0 Automatic, 1 Quest /
Touch, 2 Index, 3 Frame, 4 Generic. Missing or invalid values use Automatic.

## Gameplay

With **Motion combat** enabled, each hand has its own saved five-button layout.
Quest left X/Y correspond to Index left A/B; right A/B labels are the same.

| Action | VR control |
| --- | --- |
| Walk / turn | Left stick / right stick sideways, including with a wheel open; snap by default, with optional smooth turning |
| Open or close a hand wheel | Click that hand's thumbstick; it stays open without holding |
| Switch to the other wheel | Click the other thumbstick |
| Assign a button | Opposite hand's laser + trigger: select a wedge, then a skill |
| Use an inventory item | Either wheel > **QUICK USE**: point with the opposite hand and briefly click its trigger; use happens on release |
| Assign / change quick-use item | Hold the trigger over **QUICK USE** for 0.65 seconds, then select a carried scroll, tome or other usable item. An empty slot also opens the picker on a short click. **CLEAR SLOT** removes the assignment |
| Toggle running / walking | Push the right stick up (once per push; outside wheels), or either wheel > **RUN / WALK**, using the opposite hand's laser + trigger |
| Finish editing | Click the wheel hand's stick again, or select CLOSE in its center |
| Inventory / character / skills / journal | Left Grip + left stick click; or either wheel > INVENTORY; native tabs retain their controls |
| VR settings / body calibration | Either wheel → VR SETTINGS |
| Explain a VR setting | Point the right-hand laser at its label or controls for about half a second. Help appears beside the panel and disappears when you point away; no click needed |
| Minimap | Either wheel → MAP; movement remains available |
| Reset height | VR Settings > Reset eye height |
| Adjust controller vibration | **VR Settings > Haptics**: overall strength plus separate melee, bow draw, bow/throw release, spell, interaction and resource-alert strengths. Tests match each action's hands; **Apply** saves |
| Hold to show all nearby loot labels (Alt) | Hold the **right stick down** during gameplay, outside wheels. Labels stay shown while it is held; let go to restore normal drop/hover labels. Uses the native Alt range, independently of where you aim. A mostly vertical push never turns you. Also listed in **VR Settings > Controls** |
| Interact / pick up / talk | Right A by default; requires an aimed interactable. One press acts on one target: press again for each item, including loot a chest just dropped |
| Free melee swing / thrust | Move the equipped weapon through an enemy |
| Physical dual wield | Equip two supported melee weapons on a Barbarian or two claws on an Assassin; release both triggers and swing or thrust with either hand. Active skills retain their button bindings |
| Break pots / barrels | Swing a held melee weapon through them with its trigger released. Either Barbarian weapon or Assassin claw works; ordinary button interaction/kicking remains available |
| Choose melee rules / motion thresholds | **VR Settings > Combat**: Unleashed or Classic, Melee hit speed, and Melee thrust speed |
| Bone Spear | Assign either hand's trigger or shortcut; aim with that controller and hold to repeat while moving. **Projectile aim assist** under Controls applies to Bone Spear too; Strong adds gentle homing |
| Bone Wall / Bone Prison | Aim with the casting controller: the wall or prison lands where its ray meets the floor (six cells ahead when level). With **VR Settings > Aim > Ground skills** on Aimed monster, Bone Prison lands on the aimed monster and Bone Wall rises 3 cells in front of it. Native placement rules still apply |
| Holy Bolt | Aim with either casting controller, including up/down with Planar spell aiming Off. Light assists at launch; Strong also adds gentle homing toward undead/demons. Directly aim at your mercenary to heal; native damage/healing rules apply |
| See which monsters you are aiming at | **VR Settings > Aim > Monster outline** (Off by default; 10-100% sets its brightness) draws a thin, steady outline round each hand's monster: a hand held out picks the monster it points at; with no hand on one, the monster you look at is outlined. **Monster highlights**: Both hands (default) outlines each hand's monster, Dominant hand only that hand's (**VR > Dominant hand**; the bow hand while a bow is out, whose color also wins when both hands are on one monster). **Hand colors** (one row: its left stepper is the left hand's, its right stepper the right hand's) each choose By range (red near, orange mid-range, yellow far) or one hue; gold and blue by default. The monster you look at takes the color of the hand that would cast at it: the left (bow) hand with a bow equipped, else your dominant hand. A button you hold keeps showing the monster its cast took. Walls in between rule a monster out. A guide only: it does not change how skills aim |
| Send skills to that monster | **VR Settings > Aim > Skills go to the aimed monster** (Off by default; Spells, Bow skills or Both. A class without bow skills sees it as **Spells go to the aimed monster**, On or Off): a spell takes the monster of the hand whose button casts it (arm held out in front of you, along where it points, at least a tenth of your Controller span: 15 cm on a 1.5 m span; a hand pointing within 45 degrees of straight down, or hanging at your side, is resting), else the other hand's, else the one you look at, and keeps it until you let go or it walks out of the game's reach. Bolts and Lightning fly at it; curses and Corpse Explosion land on it; Guided Arrow, Bone Spirit, Charge, Leap Attack, Dragon Flight, Attract and Taunt are handed the monster itself; Teleport, Leap, golems and raises go where you point. With no monster, a spell goes where its hand's arm points, else the other arm, else where you look. Only monsters within the game's own cast reach count (50 cells on either axis, less a margin). Off, spells aim along the casting hand as before. With Bow skills or Both, bow skills take the bow hand's monster first, aimed from the bow hand. The Warlock's skills are left out for now: they keep the hand aim they had before these settings |
| Keep the aim on what you face | **VR Settings > Aim > Aim follows your view** (Off by default; shown once Monster outline, Skills go to the aimed monster or Ground skills on Aimed monster is on): the monster you look at must come within 17 degrees of where your head faces, and stays yours while it is within 30, until another comes within 17; a hand picks only monsters within 30 degrees of where your head faces. A casting hand (the bow hand for a bow skill) held out and pointing aims alone: its cast takes its own monster, else goes where it points, never the other hand's or the one you look at, and while the hand whose color the one you look at takes points, that monster is not outlined. A lowered hand still takes the other hand's, then the one you look at. The melee attack skills (Charge, Leap Attack, Dragon Flight, Whirlwind) from a raised hand take its own monster, else the other hand's while that one is outlined (Monster highlights Both hands, or the other hand is the dominant one), never the one you look at; lowered, they try all three like any cast |
| Cast with a staff | **VR Settings > Aim > Staff aims where your arm points** (Off by default): an upright staff casts along your arm, shoulder through hand; leveled out like a wand, it casts from its top. The Warlock's skills keep the staff's own aim |
| Aim ground skills | **VR Settings > Aim > Ground skills** sets where Fire Blast, Shock Web and the other traps (Blade Sentinel, Mind Blast...), Blizzard, Meteor, Fire Wall, Hydra, Fissure, Volcano, Bone Wall and Bone Prison land. **Game** (default): the game's own aim, as before. **Where you point**: on the floor where the casting hand points, held out or not (else the other arm, else where you look); aimed at the horizon or above, as far as the game lets a skill be cast (45 cells along an axis, more diagonally). **Aimed monster**: on the monster the casting hand, the other hand or your eyes aim at, else where you point |
| See where a ground skill will hit | **VR Settings > Aim > Landing circles** (default On; with Ground skills on Where you point or Aimed monster): once the game accepts the cast, a circle in the element's color fills from the centre outward until the skill lands (a bomb over its flight, timed by the game's own missile, Meteor over its fall, Blizzard while the ice falls); Fire Wall shows as a strip across your aim. It paints the floor only, not monsters' legs or walls |
| Seat a held shield on your arm | **VR Settings > Shield**: Grip offset slides the shield up the forearm so your fingers reach its grip strap; Out and Up move it away from and across the forearm. In centimeters (Grip -10 to 40, Out and Up -30 to 30), all 0 by default (the original position when Shield follows is 100%); about 3 / 2 / 2 suits a buckler, large shields need more. The shield blocks where it is shown. Shield follows sets how much it turns with your wrist (0% Forearm, strapped to it; 100% Wrist, with the hand; default 50%, 10% steps), and Shield roll turns it about the forearm (default 0, -180 to +180 in 5° steps, + toward the little finger) |
| Increase offline difficulty | **VR Settings > Combat > Offline difficulty**: Game default or Players 1-8; higher counts make monsters tougher and increase rewards |
| Bow draw | Right trigger near the left-hand bow string; draw and release |
| See where a drawn arrow will fly | At full draw a faint line runs from the arrow's tip along its straight flight, with a brighter dot where it would reach the floor; otherwise it fades out at 15 m. It does not stop at walls or monsters. Basic arrows and Magic, Fire, Cold, Ice, Exploding, Immolation and Freezing Arrow; not Multiple Shot, Guided Arrow, Strafe or crossbows. **VR Settings > Aim > Bow path laser** chooses Off or its color, 15 degrees at a time around the color wheel (Off by default; the first step is red 0°); stepping past 345° goes to Off and on to 0°, in either direction |
| Fire a crossbow | Equip bolts and assign **Attack** to Right Trigger; aim and fire while moving. |
| Support a crossbow | Hold Left Grip near the stock; release to free the hand. After a shoulder swap, release the swap grip before grabbing the stock again |
| Javelin, throwing knife or axe throw | Right trigger on Attack, Throw or nothing; hold the trigger, throw, release |
| Cancel a physical attack / draw | Open either wheel, then release the held controls |
| Shout a warcry | Barbarian, with a warcry on that hand's trigger: bring the hand to your mouth, hold it there a moment, then push it out forward like a shout. The trigger still casts it as before ([details](#weapon-skill-gestures)) |
| Swap weapon sets I / II | Dominant grip over either shoulder; or wheel → EQUIPMENT |
| Stow / draw weapons | Wheel → EQUIPMENT → Stow / Draw |
| Grab / drink potion | Grip a belt bottle, bring its opening to the mouth and tip it; release to return unused |
| Drop the empty bottle | Open your hand after drinking; it falls, bounces and rolls on the floor (visual only). Swing a weapon or shield into it to knock it away |
| Knock over cups, plates, vases, stools | Swing a weapon, punch or block into the level's small props; they fall and roll where they land until the area reloads (visual only) |
| Heal your mercenary | Toss a held Health, Rejuvenation, Antidote or Thawing bottle so it lands near its feet; with bottle physics it flies as a real body and breaks where it first hits. A miss keeps the potion |
| Quick potion | Wheel → EQUIPMENT → potion, using opposite laser + trigger |
| Grab wheel potion | Wheel → EQUIPMENT → potion, using the pointing hand's grip |
| Wrist health / mana / XP | Look at the back of the left wrist |
| Wipe blood off the visor | Scrub either hand side to side in front of your face, trigger released. About six strokes clear a full visor. **HUD > Face wipe gesture** turns this off |
| Recenter / stereo capture | PC F11 / F8 |

Editing consumes combat inputs but never pauses the game or zeros locomotion.
Selection clicks do not cast. Release buttons and triggers after closing before
using them in combat. Menu, tracking, equipment and assignment changes also
require a fresh press. Only one wheel is open at a time.

Interact latches the target present when you press A. Pressing at empty space
does not wait for an item to come into range. Release and press again for each
new target. A release during an inventory/NPC menu rearms interaction; holding
the button through a menu or game-session change cannot reuse the old target.

On the **VR** settings tab, **Smooth turning** defaults to **Off** (30-degree
snap turns). Turn it **On**, then **Apply**, for continuous right-stick turning.
**Snap turn angle** and **Smooth turn speed** keep their values when switching
modes. Existing smooth-turn profiles retain their selected mode.

The **Haptics** tab offers **Weapon / Shield** tests for melee, a combined bow-draw
test, **Bow / Throw** release tests, **Test L / Test R** for spells and interactions,
and one left-hand alert test. Tests use the displayed values before Apply.
**Short melee pulses** defaults Off: weapon impacts last 90 ms and shield impacts
45 ms at the controller's preferred frequency. On selects the earlier 10 ms /
180 Hz pulse. **HUD > Low health / mana alerts** must also be enabled for
automatic resource warnings.

**Combat > Melee rules** defaults to **Unleashed**: physical strokes can hit
each creature they reach once, without hit rolls or an attack-speed cooldown.
**Classic** uses native hit/block checks and the modeled class/weapon/IAS cycle,
with one target per physical stroke. Both modes require deliberate motion.
**Melee thrust speed** is separate from swing speed: 2 m/s by default, adjustable
from 0.75 to 10 m/s in 0.25 m/s steps. Push along the blade and pull back between
stabs. The old `attack_speed` INI key does not select Classic; choose it here.

**Unleashed swing damage:** every stroke that connects lands, but how hard it
hits depends on your character. Your attack rating against the monster's
defense scales the damage (the game's own hit chance, 5-95%), and so does your
rhythm: a stroke before your attack speed has recovered hits much weaker, so
measured swings beat flailing. Faster attack speed (IAS) shortens the wait.
One stroke can hit several monsters, each further one a little weaker.

**Unleashed incoming damage:** monsters' attacks and projectiles always reach
you; your shield is what stops them. Your Defense decides how hard they land:
the chance a monster would have had to hit you (5-95%) scales the whole
attack, elemental damage included. Spells never roll to hit, so Defense
doesn't reduce them; resistances still matter.

### Shield: block, parry and strain (Unleashed)

- **Block:** raise the shield and put its broad face between you and the
  attacker. You can look elsewhere while holding that position, including
  while walking or running. A valid guard replaces the random block-chance
  roll; the game's other block eligibility checks still apply. Coverage uses
  the rendered shield's orientation and size with a small allowance around the
  incoming attack line. Unsupported meshes use an attachment-based fallback.
  Lowering it, holding it away from the attack line or turning it edge-on opens
  the guard.
  Blows that meet the shield wear it, at the rate it wears as one piece of
  your armor when you are struck (about 2% a block), but only the shield
  wears; the rest of your armor wears only from hits that land. A parry
  spares it.
- **Parry:** as a melee attack comes in, swing the shield diagonally across
  your view (lower right to upper left with a left-hand shield) as the blow
  comes down at you, from late in its wind-up until just after the blow meets
  your shield (0.2 s). A swing while the
  enemy is still raising its arms is too early and is no parry. The swing
  only needs to finish with the shield raised on the attacker's side, even if
  it ends edge-on. The blow meets your shield and the attacker staggers.
  A monster with no stagger animation (Griswold, for one) is
  **deflected**: the parry sound and the riposte, but it keeps its footing.
  **Act bosses** (Andariel, Duriel, Mephisto, Diablo, Baal) cannot be parried:
  the swing meets them as a plain block.
  Every enemy attack makes contact: without a parry, where your shield is at
  that moment decides whether it blocks or hits you.
- **Riposte:** your next weapon hit on the parried monster within 1.5 s is a
  clean hit: full damage, whatever your attack rating or timing.
- **Guard strain:** every block (not a parry) strains your guard, and the
  rarer the attacker the more: a minion strains it a little more than a
  normal monster, a champion twice as much, a unique or super unique two and
  a half times and an act boss four times. A pale light
  creeps in from the edge of your view; at full strain
  the guard breaks and the light swells over most of your view. The shield
  keeps blocking; the cost is sight, then chip damage (below). It fades once
  no hit has reached your shield for a moment, even held up, and is gone about
  3 seconds after the last hit; lowering the shield starts the fade at once.
  **Combat > Guard strain** (left stepper: color) switches the light to a black shadow, or
  Off to hide it (strain, guard breaks and chip damage still apply).
  Its right stepper (buildup) selects Slow / Medium / Fast: about 20 / 10 / 5
  normal-monster blocks to full strain with default tuning (an act boss: about
  6 / 3 / 2). Medium keeps the original rate.
  This changes how quickly it builds, not how much of your view it covers.
- **Guard break:** the guard breaks with a bone snap, and while it is broken
  blocked hits leak a **chip**: part of their physical damage gets through,
  less the higher your chance to block (more while running). Elemental damage
  and its effects land in full, so resistances still matter. It recovers as
  the strain fades: let hits stop or lower the shield.

**HUD > Loot label size** adjusts dropped-item text and its darker backing from
20% to 150% in five-point steps. The original size is 45%; Apply saves the choice
to `[hud] labels_size`. Existing explicit sizes are preserved on update.

**HUD > Companions & effects** (default On) adds summon icons and live counts below
the left-hand minimap. Necromancers see skeletons, skeletal mages, golem and
revives as those skills become available; other classes use their own summon
groups. Item-granted summons also qualify. A hired mercenary has its own count.
Counts come from your summon roster, including companions outside your view.
Used/available groups stay visible at zero, with the zero highlighted. The row
follows **Minimap & Compass** wrist/palm placement and hides with navigation;
turning off Companions & effects preserves the map and compass. Unknown or stale
rosters are hidden during loading rather than presented as a lost army.

The same display shows **effects currently on your character**: active buffs,
auras (including received auras), armor spells, warcries, curses, poison/chill,
potion effects and shrine bonuses. These come from native active states, not
learned skills or button assignments. Indicators disappear when the state ends.
Harmful effects appear first in red, beneficial effects in green, and other
statuses in gold. Names remain visible when an effect has no cached skill icon.
Permanent passive skills and internal animation flags are omitted. No duration
or charge count is inferred; this first version indicates presence only.

The display is bounded to three effect rows. If more than 21 effects overlap,
the final cell shows how many additional effects are active. Existing
`[interactions] companion_counts` preferences also control this expanded row.

**HUD > Blood on visor** (default On) splashes blood around the edge of the
view as life drops below 60%, 40% and 20%; large splats drip slowly, then dry.
Healing back above a band, from any source, fades that band's blood out over
about a second. Changing maximum life alone (Battle Orders, Oak Sage, +life gear)
neither splashes nor fades. With **HUD > Face wipe gesture** On (the default),
scrubbing a hand side to side in front of the face wipes blood
at any time: each stroke ticks the controller and smears some splats away. While
you are still hurt, the visor then stays clean until the next hit splashes it
again. The centre of the view always stays clear. While a hand is scrubbing it
does not attack; a held potion, squeezed trigger or bow draw never counts as
wiping.

**Low-life heartbeat:** **Combat > Heartbeat starts at** (default 60%, or Off)
picks the life where a quiet heartbeat begins. Every 10% of life you lose below
that, it gets louder and faster, at full strength below 10%. Healing winds it
down beat by beat. Wiping the visor clean fades it out too, until the next hit
brings it back. It plays in Classic and Unleashed; `[gameplay]
low_life_heartbeat` sets its loudness (200%, 0 = off).

## Hand skill layouts

| Slot | Quest left | Quest right | Index, either hand |
| --- | --- | --- | --- |
| Primary skill | Trigger | Trigger | Trigger |
| Second action | X | A (Interact by default) | A |
| Third action | Y | B | B |
| Alternate second | Left Grip + X | Right Grip + A | Same-hand Grip + A |
| Alternate third | Left Grip + Y | Right Grip + B | Same-hand Grip + B |

There is no left-trigger modifier layer. Each hand aims its own spells. Face
shortcuts do not change either trigger assignment. On a Sorceress, Fire Bolt,
Fireball, Ice Bolt, Ice Blast and Charged Bolt can fire from both hands simultaneously,
each with independent aim and its own Faster Cast Rate timing. Hold the input to repeat. Native
mana costs apply to each cast, and walking remains available without entering
the original casting animation. See [hand projectile spells](docs/hand_projectile_spells.md)
for the experimental adapter's scope and offline testing checklist.

**VR Settings > Controls > Planar spell aiming** defaults to Off for new or
missing settings, allowing up/down aiming of straight projectile spells. Turn
it On and Apply to keep those spells level; Charged Bolt keeps its native planar
spread. Existing saved preferences are retained, and changes apply to new casts.

**Projectile aim assist** on the same tab defaults to Strong for new or missing
settings; existing saved choices are retained. Off disables assistance. Light corrects nearby
misses at launch; Strong adds gentle homing toward the enemy selected at launch
for straight hand spells. Charged Bolt remains launch-only. This does not affect
bows, thrown weapons or other native spells, and does not guarantee a hit.

Other skills retain native repeat behavior, delays and animation restrictions.
A native action has one owner: the first held input wins, with right-hand
priority for simultaneous presses. Native and physical casts do not start
together. A competing input must be released and pressed again; it is never
queued to fire unexpectedly when the first hand stops.

Select a wheel wedge to edit its binding directly. **CLEAR SLOT** removes the
skill; clearing right A restores Interact, and clearing right trigger restores
basic weapon handling. Interact can also be chosen explicitly from the picker.
Assigning a spell to right A deliberately replaces its interaction action.
The skill picker has **All**, the character's three skill-tree categories, and
**General / Items** tabs. For example, a Sorceress has Fire, Lightning and Cold;
a Paladin has Combat, Offensive Auras and Defensive Auras. General / Items holds
Attack, Interact, Town Portal and skills from equipment or another class. Each
tab shows its available count; empty categories are dimmed. Choose a category,
then a skill, using **PREVIOUS / NEXT** when needed. Only available active skills
appear; passive skills cannot be assigned. Existing bindings are preserved.
Grip changes cannot switch a held face button to another spell: release the
face button first. Held potions and holsters take priority over spell shortcuts.

Bindings use the game's controller assignments, saved by the game per character
and weapon-set bank. Existing assignments are retained in these native slots:

| Hand | Trigger | Primary face | Secondary face | Grip + primary | Grip + secondary |
| --- | --- | --- | --- | --- | --- |
| Right | B | A | X | Y | RB |
| Left | LT + B | LT + X | LT + Y | LT + RB | LT + A |

These are underlying **native gamepad slots**, not physical VR button labels.
Native duplicate-assignment rules still apply: assigning a skill may clear its
previous native slot. Native menus show their usual gamepad prompts. The mod
does not directly edit saves or invent extra game-owned skill records.

## Weapon skill gestures

Active skill icons and name panels beside the weapons are **off by default**.
Review assignments by opening either hand's wheel. To restore the weapon
indicators, open **VR Settings > Controls > Weapon skill indicators**, turn it
**On**, and press **Apply**. This changes both hands' visual indicators only;
assigned spells and weapon skill gestures keep working. The saved preference
is `[interactions] weapon_skill_indicators=0` (hidden) or `1` (shown).

**VR Settings > Combat > Shield bash speed** adjusts physical bash/Smite
independently of weapon swings: default 2 m/s, range 0.75-10 m/s in 0.25 m/s
steps. Move the hand at least 12 cm in a deliberate stroke. Wrist rotation alone
does not attack, and contact still requires the selected speed. Blocking, parries
and button-triggered attacks use their existing rules.

The trigger skill is the persistent gesture skill for its hand. Optional
hand-side icons show the assignments. Left-trigger Smite supports physical shield swings
and thrusts when a shield and its native hook are available. Supported arrow
skills assigned to left trigger fire on bow draw/release using the right hand
to pull the string. The left trigger remains a button fallback; right trigger
away from the string uses the right hand's own assignment.

**Bow quiver.** With a bow, a face button assigned to Magic Arrow, Fire Arrow,
Cold Arrow, Exploding Arrow, Ice Arrow, Immolation Arrow or Freezing Arrow loads
that skill instead of casting it. Every following arrow you nock shows the
skill on the string and flies the basic arrow's physical path when released;
press the same button again for basic arrows, or another arrow skill's button
to swap. The skill's own mana cost, damage and impact effects apply to each
shot. If the game refuses it (usually not enough mana), the drawn arrow flies as
a basic arrow and the character's mana warning plays. With nothing loaded, a
supported arrow skill on left trigger uses the same physical release. Multiple
Shot, Guided Arrow and Strafe keep their native behavior. Set
`[bow] quiver=0` to restore native buttons for these skills.
The selected skill also changes the nocked arrow's model and particles. Set
`[bow] quiver_preview=0` to keep the basic nocked model while still firing skill
arrows. Model swaps wait for the native client update to retire the previous
arrow; a brief basic fallback can appear while the new model loads.

**Javelin skills.** With a javelin in hand, the hand throw carries Poison
Javelin, Lightning Bolt, Plague Javelin or Lightning Fury: bind one to the right
trigger (it arms the throw), or press a face button bound to one to load it
into every following throw, like the bow quiver (press again for basic throws).
The skill's own mana cost, damage, clouds and lightning apply, and each throw
uses up a javelin. If the game refuses it, a basic javelin flies instead.
Poison and Plague Javelin fly the javelin model; Lightning Bolt and Fury fly as
their lightning. `[bow] quiver=0` turns this off too.

**Double Throw.** A Barbarian with a throwing weapon in each hand can bind
Double Throw to the right trigger, or load it from a face button the same way.
Each hand then throws its own weapon: the right with the right trigger, the
left with the left trigger, together or one after the other. While it is armed
the left trigger throws instead of casting its own skill. Each throw is one
native Double Throw of that hand's weapon, with its attack bonus, mana and
quantity. The game's own Double Throw throws both weapons for one mana cost;
here each hand's throw pays it. If the game refuses it, the hand throws its
weapon as a basic throw.

**Throwing traps and bombs.** With an Assassin's Fire Blast, Shock Web, Blade
Sentinel, Charged Bolt, Lightning or Death Sentry, Wake of Fire or Wake of
Inferno on the right trigger, press the trigger: the bomb or trap appears in
your hand. Throw it and let go, and the game casts the skill where its arc
lands, within the skill's own reach. A quick press still casts where you point
(with **Ground skills** on Where you point; on Game, where the game aims), and
holding the trigger still for a moment casts natively as before, repeating
while held. Start the throw right after pressing; a slow reach casts nothing.
The models are the game's own, cached from your installed game by the
installer. **Weapon skill gestures** Off turns this off.

**Warcries by gesture.** A Barbarian can shout a warcry instead of pressing for
it. Bind Howl, Shout, Taunt, Battle Cry, Battle Orders, War Cry or Battle
Command to either hand's trigger. Bring that hand up to your mouth and hold it
there for a moment, until a light tick confirms it is ready. Then push it out
forward like a shout. A firmer pulse confirms the cast. It is one native cast
of that hand's warcry, with its own mana cost, cooldown and effect; if the game
refuses it, the game's own warning plays. Taunt goes to the monster that hand's
cast would take, as a trigger press would. Find Potion, Find Item and Grim Ward
need a corpse and stay on their buttons. The gesture never fires while that
hand holds a bottle or another grabbed item, while its trigger is held, or
with a wheel open. A swing that only passes your face, a slow reach, or a push
down or to the side is no shout. **Weapon skill gestures** Off turns this off.

Face-button skills never replace gesture skills. Ordinary melee swings remain
basic attacks for skills without a supported physical adapter; assigning a
spell does not cast it on every swing. Physical skill gestures can be disabled
under **VR settings → VR → Weapon skill gestures** (on by default), retaining
button casting and basic physical attacks. This implementation supports the
existing Smite and bow adapters; it does not generalize arbitrary melee skills.

Turning gestures off or changing equipment/assignments cancels an in-progress
skill draw. Shield contact and its trigger fallback cannot duplicate a cast.
Projectile targets follow the casting hand rather than locomotion direction.
The five supported Sorceress projectile spells bypass the native casting
animation; other skills can still stop movement through their native animation.
The new physical casting path still requires offline headset validation.

## Menus: laser and native buttons

Both methods work in the main menu and native game panels, including inventory,
stash and merchants. Keep the D2R window focused on the PC.

Use a **stick or face button** to start native button navigation and follow the
game's displayed prompts. The idle laser will not move your native selection.
To return to the laser, release the native controls and deliberately move your
aim across the panel. A small hand tremor will not change methods.

### Laser controls

| Action | VR control |
| --- | --- |
| Hover / point | Aim the right controller's laser |
| Select / drag an item | Right trigger; hold while moving the item and release to place it |
| Context / alternate click | Left Grip + Right Trigger (hold grip first) |
| Native Ctrl-click shortcut | Hold Left Trigger, then click with Right Trigger; the game chooses drop, sell or transfer for the current panel |
| Native Shift modifier | Hold Right Grip while using the laser |
| Push / pull the panel | Point at the panel, hold Right Grip and move that hand toward or away from you; release to leave it there. The VR Settings panel can also be moved and tilted this way |
| Back / close | Right B, or point and click the panel's close/back button |
| Tabs / page controls | Point and click the native on-screen controls |

Dragging keeps the item held if the ray briefly leaves the panel. Releasing
the trigger ends the drag at the last valid pointer position. Tracking or
desktop focus loss releases the injected mouse button; release the trigger
before starting again. Pressing or releasing Left Grip during a drag will
not convert it into a context-click.

### Native menu prompts

| Game's displayed input | Quest control | Steam Frame control |
| --- | --- | --- |
| A | Right A | Right A |
| B / back | Right B | Right B |
| X | Left X | Left A |
| Y | Left Y | Left B |
| Left stick / right stick | Left stick / right stick | Left stick / right stick |
| LS / RS click | Left stick click / right stick click | Left stick click / right stick click |
| Inventory: X / drop selected item | Left X; hold if the native prompt requires it | Left A; hold if the native prompt requires it |
| Inventory: hold RS / sort | Press and hold right stick click | Press and hold right stick click |
| LB / RB | Left Grip / Right Grip | Left Grip / Right Grip |
| LT / RT | Left Trigger / Right Trigger | Left Trigger / Right Trigger |
| Menu / Start, where available to the game | Controller menu button | Controller menu button |

Native menu actions use the same buttons in the main menu, tabbed menu, stash,
inventory and merchant panels. **Inside a native menu, left primary performs the displayed X action.**
During motion gameplay, Inventory and VR Settings are available in either wheel.
Right-stick click toggles the right wheel during gameplay. Use the shoulder
gesture or **EQUIPMENT / BELT** wheel for weapon configurations.

Quest controllers do not need a separate D-pad or Xbox controller: use sticks
for native navigation and the laser for the on-screen controls. When using
the laser, Right Trigger is a mouse click and the grips/triggers above provide
its modifiers; once native navigation is selected, they perform LB/RB/LT/RT.
Release buttons, triggers and sticks when opening or closing menus so a held
input cannot also act in the next screen or in gameplay.

The mod's own settings and calibration panels use their VR laser controls:
right laser + trigger to select, Right B to close/cancel. Native gamepad input
does not pass through those panels.

### Quick-use inventory slot

Both wheels share one assignment, remembered by item type across restocks and
sessions. The displayed count includes carried inventory items or tome charges;
belt items keep their existing potion controls. Stash and cube contents are not
included. Use follows the native game's restrictions. Identify opens inventory
for target selection. A long press edits without spending an item. Move the
pointer away before releasing to cancel a short press. If the item changed,
ran out, or tracking was lost, the use is discarded rather than retried.

The minimap has a narrow bronze bevel and inset corner details. The full native
map is fitted inside the frame so edge markers remain visible.

A gold arrow sits on the game's blue hero marker and points the way you face.
Change its view under **VR Settings > HUD**, then press **Apply**:

- **Map view: Isometric / Top-down.** Top-down flattens the game's slanted map into
  a plan, so rooms are square and the arrow matches your real turns. Map names
  are left out in top-down, since they would turn 45 degrees with the floor.
- **Rotate map: Off / On** (top-down only). On turns the map so the way you face
  is up; Off keeps north up.

Both are saved like any setting. Isometric with north up is the default.
